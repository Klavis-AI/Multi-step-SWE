// submission-proxy-server-v3 drives the submission proxy (submission/server)
// as a subprocess over a real loopback socket, with --dry_run so that every
// "log" it talks to is submission.NewStubLogClient and nothing leaves the
// process tree.
//
// Two things here are new at v1.1.4. The binary is a klog binary, so it
// accepts --one_output, which v1.1.3's glog build rejects outright: the
// server below would never come up against the previous release. And the
// --loglist_path it is given is read with loglist3, so the set of logs it
// then calls get-roots on -- visible on /metrics as the label set of
// last_get_roots_success -- is exactly the set Chrome's policy picks out of a
// v3 list.
package main

import (
	"bytes"
	"encoding/base64"
	"encoding/json"
	"encoding/pem"
	"fmt"
	"io"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strings"
	"time"

	"ctverify/vhelp"
)

func freeLoopbackPort(t *vhelp.T) int {
	// Ports are taken from BELOW the kernel's ephemeral range (32768+ here) so
	// that the kernel cannot hand this port to an unrelated process in the
	// window between the probe closing and the child binding it. That window
	// is the one way a loopback harness can fail under concurrent load, and a
	// gate group that fails under load scores the whole step zero for a
	// correct solution. The starting slot is dispersed by a single throwaway
	// ephemeral bind so sibling probes in one run do not converge; a slot a
	// sibling has already claimed is held by its live listener and so is
	// visibly busy here.
	seed, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("listen 127.0.0.1:0: %v", err)
	}
	base := seed.Addr().(*net.TCPAddr).Port % 12000
	if err := seed.Close(); err != nil {
		t.Fatalf("close probe listener: %v", err)
	}
	for i := 0; i < 512; i++ {
		p := 20000 + (base+i)%12000
		l, err := net.Listen("tcp", fmt.Sprintf("127.0.0.1:%d", p))
		if err != nil {
			continue
		}
		if err := l.Close(); err != nil {
			t.Fatalf("close probe listener: %v", err)
		}
		return p
	}
	t.Fatalf("no free loopback port in 20000-31999 after 512 probes")
	return 0
}

func chainJSON(t *vhelp.T, rel ...string) string {
	data := vhelp.ReadFixture(rel...)
	var b64 []string
	for {
		var blk *pem.Block
		blk, data = pem.Decode(data)
		if blk == nil {
			break
		}
		if blk.Type == "CERTIFICATE" {
			b64 = append(b64, base64.StdEncoding.EncodeToString(blk.Bytes))
		}
	}
	if len(b64) == 0 {
		t.Fatalf("no certificates in %v", rel)
	}
	out, err := json.Marshal(map[string][]string{"chain": b64})
	if err != nil {
		t.Fatalf("marshal chain: %v", err)
	}
	return string(out)
}

type reply struct {
	status int
	body   string
}

func post(t *vhelp.T, url, body string) reply {
	rsp, err := http.Post(url, "application/json", strings.NewReader(body))
	if err != nil {
		t.Fatalf("POST %s: %v", url, err)
	}
	defer rsp.Body.Close()
	b, err := io.ReadAll(rsp.Body)
	if err != nil {
		t.Fatalf("read %s: %v", url, err)
	}
	return reply{rsp.StatusCode, string(b)}
}

func get(t *vhelp.T, url string) reply {
	rsp, err := http.Get(url)
	if err != nil {
		t.Fatalf("GET %s: %v", url, err)
	}
	defer rsp.Body.Close()
	b, err := io.ReadAll(rsp.Body)
	if err != nil {
		t.Fatalf("read %s: %v", url, err)
	}
	return reply{rsp.StatusCode, string(b)}
}

func main() {
	t := vhelp.New()

	scratch := os.Getenv("CT_SCRATCH")
	listPath := filepath.Join(scratch, "log_list.json")
	if err := os.WriteFile(listPath, []byte(benchLogList3JSON), 0o644); err != nil {
		t.Fatalf("write log list: %v", err)
	}

	port := freeLoopbackPort(t)
	base := fmt.Sprintf("http://127.0.0.1:%d", port)

	logPath := filepath.Join(scratch, "server.log")
	logFile, err := os.Create(logPath)
	if err != nil {
		t.Fatalf("create server log: %v", err)
	}
	cmd := vhelp.Command(vhelp.CandidateBin("server"),
		fmt.Sprintf("--http_endpoint=127.0.0.1:%d", port),
		"--loglist_path="+listPath,
		"--dry_run",
		"--policy_type=chrome",
		"--load_pending_qualified_logs=true",
		"--loglist_refresh_interval=24h",
		"--roots_refresh_interval=24h",
		"--add_prechain_timeout=10s",
		// klog-only: v1.1.3's glog build refuses this command line.
		"--one_output",
		"--logtostderr",
	)
	cmd.Stdout = logFile
	cmd.Stderr = logFile
	if err := cmd.Start(); err != nil {
		t.Fatalf("start submission server: %v", err)
	}
	defer func() {
		_ = cmd.Process.Kill()
		_, _ = cmd.Process.Wait()
		logFile.Close()
	}()

	// Wait for the proxy to finish its first log-list refresh and start
	// serving. No assertion depends on how long that takes.
	var metrics string
	deadline := time.Now().Add(60 * time.Second)
	for time.Now().Before(deadline) {
		if cmd.ProcessState != nil {
			break
		}
		rsp, err := http.Get(base + "/metrics")
		if err == nil {
			b, _ := io.ReadAll(rsp.Body)
			rsp.Body.Close()
			if rsp.StatusCode == http.StatusOK && strings.Contains(string(b), "log_list_updates") {
				metrics = string(b)
				break
			}
		}
		time.Sleep(200 * time.Millisecond)
	}
	if metrics == "" {
		tail, _ := os.ReadFile(logPath)
		t.Fatalf("submission server never served /metrics on %s; log: %s", base, string(tail))
	}

	// 1. Control: the server came up and refreshed the log list exactly once.
	t.Check("control-proxy-server-refreshed-the-log-list-once",
		strings.Contains(metrics, "\nlog_list_updates 1\n"),
		"log_list_updates not 1 in /metrics")

	// 2. The logs it then asked for roots are exactly the ones Chrome's policy
	//    selects out of the pinned v3 list: every usable log, plus the pending
	//    and qualified ones. The readonly and retired logs are never called.
	re := regexp.MustCompile(`last_get_roots_success\{logurl="([^"]*)"\}`)
	var urls []string
	for _, m := range re.FindAllStringSubmatch(metrics, -1) {
		urls = append(urls, m[1])
	}
	sort.Strings(urls)
	want := []string{
		"https://google.bench.invalid/qualified/",
		"https://google.bench.invalid/usable/",
		"https://shop.bench.invalid/pending/",
		"https://shop.bench.invalid/usable/",
	}
	t.Equal("get-roots-reached-exactly-the-selected-v3-logs",
		strings.Join(urls, ","), strings.Join(want, ","))

	addChain := base + "/ct/v1/proxy/add-chain/"
	addPreChain := base + "/ct/v1/proxy/add-pre-chain/"

	// 3. A body that is not JSON at all.
	r := post(t, addChain, "not json")
	t.Check("add-chain-rejects-a-non-json-body",
		badBody(r, 400, "proxy: failed to parse add-chain body: "),
		"status=%d body=%q", r.status, r.body)

	// 4. A well-formed request with no certificates in it.
	r = post(t, addChain, `{"chain":[]}`)
	t.Check("add-chain-rejects-an-empty-chain",
		badBody(r, 400, "proxy: failed to parse add-chain body: "),
		"status=%d body=%q", r.status, r.body)

	// 5. The pre-chain entrypoint names itself in its own parse error.
	r = post(t, addPreChain, `{"chain":[]}`)
	t.Check("add-pre-chain-names-itself-in-the-parse-error",
		badBody(r, 400, "proxy: failed to parse add-pre-chain body: "),
		"status=%d body=%q", r.status, r.body)

	// 6. add-chain is POST-only.
	r = get(t, addChain)
	t.Equal("add-chain-is-post-only",
		fmt.Sprintf("%d|%s", r.status, r.body), "404|404 page not found\n")

	// 7. A chain the stub logs do not root is refused by the distributor, and
	//    the distributor's reason is what the proxy returns.
	r = post(t, addChain, chainJSON(t, "minted", "chain-valid.pem"))
	t.Check("unrooted-chain-is-a-bad-gateway",
		badBody(r, 502, "distributor unable to process cert-chain: "),
		"status=%d body=%q", r.status, r.body)

	// 8. A chain whose first element is not a certificate at all fails to
	//    parse inside the distributor, before any log is contacted.
	r = post(t, addChain, `{"chain":["QUJD"]}`)
	t.Check("unparseable-certificate-is-a-bad-gateway",
		badBody(r, 502, "distributor unable to process cert-chain: "),
		"status=%d body=%q", r.status, r.body)

	// 9. The same unrooted chain on the pre-chain entrypoint is refused the
	//    same way.
	r = post(t, addPreChain, chainJSON(t, "minted", "chain-unrooted.pem"))
	t.Check("unrooted-pre-chain-is-a-bad-gateway",
		badBody(r, 502, "distributor unable to process cert-chain: "),
		"status=%d body=%q", r.status, r.body)

	// 10. The flag that decided which list all of the above was read from
	//     defaults to the v3 list, as CLI.md section 4.4 gives it. This is
	//     TEST_INTERFACE requirement R10 and nothing else in this step
	//     observes it; the server is run with --help only, so it neither
	//     listens nor refreshes.
	hout, _ := vhelp.Command(vhelp.CandidateBin("server"), "--help").CombinedOutput()
	t.Check("loglist-path-defaults-to-the-v3-log-list",
		strings.Contains(string(hout), "  -loglist_path string\n") &&
			strings.Contains(string(hout), `(default "https://www.gstatic.com/ct/log_list/v3/log_list.json")`),
		"usage did not document the v3 --loglist_path default: %q", firstN(string(hout), 1200))

	// 11. The logging swap changed the logger's flag names and none of its
	//     exit statuses (TEST_INTERFACE section 4; CLI.md section 4.9). This
	//     server carries the one place in the product where a bad flag
	//     *value* is fatal rather than merely wrong: an unrecognised
	//     --policy_type goes through the logger's Fatal path and exits 255,
	//     not the 1 an Exit path gives. The value is rejected before anything
	//     is dialled or listened on, so nothing is started here and nothing
	//     needs stopping; the process is killed only if it fails to exit at
	//     all, which fails the case.
	pcmd := vhelp.Command(vhelp.CandidateBin("server"), "--policy_type=neither")
	var pbuf bytes.Buffer
	pcmd.Stdout, pcmd.Stderr = &pbuf, &pbuf
	if err := pcmd.Start(); err != nil {
		t.Fatalf("start the proxy with a bad --policy_type: %v", err)
	}
	timer := time.AfterFunc(60*time.Second, func() { _ = pcmd.Process.Kill() })
	prc := 0
	if werr := pcmd.Wait(); werr != nil {
		prc = -1
		if ee, ok := werr.(interface{ ExitCode() int }); ok {
			prc = ee.ExitCode()
		}
	}
	timer.Stop()
	t.Check("unrecognised-policy-type-is-fatal-with-255", prc == 255,
		"rc=%d (want 255), output %q", prc, firstN(pbuf.String(), 400))

	t.Done()
}

// badBody reports whether an error response carries the status and the
// message prefix CLIENT-API section 7.2 fixes for it. That document states
// that the <reason> after the prefix is whatever the underlying library
// produced and is not part of the interface, so nothing here reads it beyond
// requiring that it is there.
func badBody(r reply, status int, prefix string) bool {
	return r.status == status && strings.HasPrefix(r.body, prefix) &&
		strings.HasSuffix(r.body, "\n") && len(r.body) > len(prefix)+1
}

// firstN truncates a diagnostic so a failure message stays readable.
func firstN(s string, n int) string {
	if len(s) <= n {
		return s
	}
	return s[:n]
}
