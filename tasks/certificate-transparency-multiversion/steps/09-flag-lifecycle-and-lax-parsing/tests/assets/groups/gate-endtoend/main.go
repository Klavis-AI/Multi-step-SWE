// Group gate-endtoend.
//
// The inherited end-to-end path of this milestone, plus proof that the
// artifact starts: the candidate's own ct_server binary, started as a
// subprocess, answering RFC 6962 over a real loopback socket. It is a GATE
// group. It earns nothing and only vetoes, so everything below is behaviour
// v1.1.0 already had and v1.1.2 keeps, and it must pass against both trees.
//
// SCOPE-BACKEND: deliberately no Trillian backend and no stub here, unlike the
// groups that share _kit/serverkit.go. A gate group is compiled against
// v1.1.2 *and* against v1.1.0, and the pinned trillian module moves between
// those two anchors (v1.2.2-0.20190612132142 -> v1.4.0): at v1.1.0 the kit's
// stub is not a trillian.TrillianLogServer at all (no AddSequencedLeaf), so a
// driver carrying it does not compile at the predecessor. Where the dependency
// pin moves between adjacent anchors, the part of the milestone that needs a
// backend simply cannot be gated, and is graded by the scored tier instead
// (eku-filter-admission, getentries-alignment-coercion, cache-control-immutable).
// ct_server is configured with two backends because it only uses a blocking
// dial when exactly one is configured; with two it serves immediately and
// anything that does reach a backend answers 503. Everything decided before a
// backend call -- /healthz, get-roots, chain admission, get-entries range
// validation -- is fully observable, and is what this group grades.
//
// Nothing here touches this milestone's own capability: no --by_range and no
// --align_getentries, no coercion arithmetic, no Cache-Control, no configured
// EKU list, and nothing in either direction about the withdrawn DNS client or
// the withdrawn grpc/naming resolver (gate rule 3).
//
// SCOPE-NET: loopback only. The serving port is obtained by binding :0 and
// releasing it; because ct_server does not report the port it bound, that
// hand-off has a window, so a start that dies before serving is retried with a
// fresh number rather than allowed to look like a behavioural failure. The two
// backend addresses are deliberately *not* drawn from that pool: ports 1 and 2
// cannot be bound by an unprivileged process, so a dial there is refused,
// always. SCOPE-TIME: no elapsed time, deadline or timestamp is asserted; the
// readiness poll is a fatal prerequisite, not a case.
package main

import (
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"encoding/pem"
	"fmt"
	"io"
	"net"
	"net/http"
	"os"
	"os/exec"
	"sort"
	"strconv"
	"strings"
	"time"

	ct "github.com/google/certificate-transparency-go"

	"ctverify/vhelp"
)

const (
	pinnedLogID = int64(1001)
	logPrefix   = "bench"
	// Unbindable without privilege, so nothing can ever be listening here.
	deadBackendA = "127.0.0.1:1"
	deadBackendB = "127.0.0.1:2"
)

func scratch(name string) string { return os.Getenv("CT_SCRATCH") + "/" + name }

func freePort(t *vhelp.T) int {
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

func pemBody(t *vhelp.T, b []byte) []byte {
	blk, _ := pem.Decode(b)
	if blk == nil {
		t.Fatalf("fixture is not PEM")
	}
	return blk.Bytes
}

// protoEscape renders bytes as the \xNN escapes prototext accepts.
func protoEscape(b []byte) string {
	var s strings.Builder
	for _, c := range b {
		fmt.Fprintf(&s, "\\x%02x", c)
	}
	return s.String()
}

// writeConfig synthesises this group's pinned configuration. The keys and the
// root set are fixtures; only the two backend addresses are chosen, and they
// are chosen to be unreachable. The shape -- a LogMultiConfig with a backend
// set -- is step 02's milestone and is unchanged at both anchors.
func writeConfig(t *vhelp.T) string {
	keyDER := pemBody(t, vhelp.ReadFixture("minted", "keys", "log-key.pem"))
	pubDER := pemBody(t, vhelp.ReadFixture("minted", "keys", "log-pubkey.pem"))

	var b strings.Builder
	b.WriteString("backends {\n")
	b.WriteString("  backend { name: \"default\" backend_spec: \"" + deadBackendA + "\" }\n")
	b.WriteString("  backend { name: \"spare\"   backend_spec: \"" + deadBackendB + "\" }\n")
	b.WriteString("}\n")
	b.WriteString("log_configs {\n  config {\n")
	fmt.Fprintf(&b, "    log_id: %d\n", pinnedLogID)
	fmt.Fprintf(&b, "    prefix: %q\n", logPrefix)
	fmt.Fprintf(&b, "    roots_pem_file: %q\n", vhelp.Fixture("minted", "roots-bench-plus-int.pem"))
	b.WriteString("    private_key: {\n      [type.googleapis.com/keyspb.PrivateKey] {\n        der: \"" + protoEscape(keyDER) + "\"\n      }\n    }\n")
	b.WriteString("    public_key: { der: \"" + protoEscape(pubDER) + "\" }\n")
	b.WriteString("    log_backend_name: \"default\"\n")
	b.WriteString("  }\n}\n")

	p := scratch("ctfe.textproto")
	if err := os.WriteFile(p, []byte(b.String()), 0600); err != nil {
		t.Fatalf("write config: %v", err)
	}
	return p
}

type server struct {
	cmd     *exec.Cmd
	exited  chan error
	base    string
	errPath string
}

// stop terminates only the process this group started, by the exact PID it
// captured itself.
func (s *server) stop() {
	if s == nil || s.cmd == nil || s.cmd.Process == nil {
		return
	}
	_ = s.cmd.Process.Kill()
	if s.exited != nil {
		<-s.exited
	}
}

func tail(path string) string {
	b, _ := os.ReadFile(path)
	if len(b) > 800 {
		b = b[len(b)-800:]
	}
	return strings.ReplaceAll(string(b), "\n", " | ")
}

// start brings ct_server up, retrying a start that died before it served. The
// only thing a retry can cure is losing the race for the ephemeral port
// between releasing it and the child binding it; anything else fails the same
// way every time and is reported after the last attempt.
func start(t *vhelp.T) *server {
	why := ""
	for attempt := 1; attempt <= 4; attempt++ {
		s, reason := tryStart(t, attempt)
		if s != nil {
			return s
		}
		why = reason
	}
	t.Fatalf("ct_server never served after 4 attempts: %s", why)
	return nil
}

func tryStart(t *vhelp.T, attempt int) (*server, string) {
	cfg := writeConfig(t)
	httpAddr := "127.0.0.1:" + strconv.Itoa(freePort(t))

	errPath := scratch(fmt.Sprintf("ct_server-%d.stderr", attempt))
	errFile, err := os.Create(errPath)
	if err != nil {
		t.Fatalf("create stderr sink: %v", err)
	}
	// vhelp.Command strips CT_VERIFY_NONCE from the child, and the child's
	// output is captured to a file rather than sharing this process's stdout,
	// so nothing ct_server prints can be read as a case result.
	cmd := vhelp.Command(vhelp.CandidateBin("ct_server"),
		"--log_config="+cfg,
		"--http_endpoint="+httpAddr,
		"--get_sth_interval=0",
		// A dial to an unbindable port is refused instantly and the RPC fails
		// fast with codes.Unavailable, which both anchors map to 503; the
		// deadline is deliberately far longer than that path needs so that a
		// loaded host cannot turn the refusal into codes.DeadlineExceeded,
		// which both anchors would map to 408 instead. SCOPE-TIME: no elapsed
		// time is measured here, the margin only removes a race.
		"--rpc_deadline=30s",
		"--logtostderr",
	)
	cmd.Stdout = errFile
	cmd.Stderr = errFile
	if err := cmd.Start(); err != nil {
		t.Fatalf("start ct_server: %v", err)
	}
	s := &server{cmd: cmd, exited: make(chan error, 1), base: "http://" + httpAddr, errPath: errPath}
	go func() { s.exited <- cmd.Wait() }()

	deadline := time.Now().Add(60 * time.Second)
	for {
		select {
		case <-s.exited:
			// A child that is gone is never coming up; do not spend the rest
			// of the deadline discovering that.
			return nil, fmt.Sprintf("attempt %d on %s exited before serving: %s",
				attempt, httpAddr, tail(errPath))
		default:
		}
		if time.Now().After(deadline) {
			s.stop()
			return nil, fmt.Sprintf("attempt %d on %s never became healthy: %s",
				attempt, httpAddr, tail(errPath))
		}
		resp, err := http.Get(s.base + "/healthz")
		if err == nil {
			body, _ := io.ReadAll(resp.Body)
			_ = resp.Body.Close()
			if resp.StatusCode == http.StatusOK && string(body) == "ok" {
				return s, ""
			}
		}
		time.Sleep(100 * time.Millisecond)
	}
}

func (s *server) url(path string) string { return s.base + "/" + logPrefix + path }

type result struct {
	status int
	body   []byte
}

func (s *server) get(t *vhelp.T, url string) result {
	resp, err := http.Get(url)
	if err != nil {
		t.Fatalf("GET %s: %v", url, err)
	}
	body, err := io.ReadAll(resp.Body)
	_ = resp.Body.Close()
	if err != nil {
		t.Fatalf("read %s: %v", url, err)
	}
	return result{status: resp.StatusCode, body: body}
}

func (s *server) postChain(t *vhelp.T, chain [][]byte) result {
	body, err := json.Marshal(ct.AddChainRequest{Chain: chain})
	if err != nil {
		t.Fatalf("marshal add-chain request: %v", err)
	}
	resp, err := http.Post(s.url(ct.AddChainPath), "application/json", strings.NewReader(string(body)))
	if err != nil {
		t.Fatalf("POST add-chain: %v", err)
	}
	rb, err := io.ReadAll(resp.Body)
	_ = resp.Body.Close()
	if err != nil {
		t.Fatalf("read add-chain response: %v", err)
	}
	return result{status: resp.StatusCode, body: rb}
}

func sha256Hex(b []byte) string {
	s := sha256.Sum256(b)
	return hex.EncodeToString(s[:])
}

func trunc(b []byte) string {
	s := strings.ReplaceAll(string(b), "\n", " ")
	if len(s) > 200 {
		s = s[:200]
	}
	return s
}

func main() {
	t := vhelp.New()

	leafDER := vhelp.ReadFixture("minted", "leaf-valid.der")
	unrootedDER := vhelp.ReadFixture("minted", "leaf-unrooted.der")
	intDER := vhelp.ReadFixture("minted", "bench-intermediate.der")
	rootDER := vhelp.ReadFixture("minted", "bench-root.der")

	s := start(t)
	// Done calls os.Exit, which does not run deferred functions, so a
	// `defer s.stop()` here would never fire and the ct_server child would
	// outlive the group. Register the teardown with the harness instead --
	// this is what the shared kit (_kit/serverkit.go) already does.
	t.Cleanup(s.stop)

	// 1. The artifact started and /healthz answers exactly "ok" with 200
	// (step 08's milestone).
	hz := s.get(t, s.base+"/healthz")
	t.Check("healthz-exact", hz.status == http.StatusOK && string(hz.body) == "ok",
		"status %d body %q", hz.status, string(hz.body))

	// 2. get-roots returns exactly the two configured roots, byte for byte,
	// compared as a sorted set of SHA-256 digests. Served from the
	// configuration, so no backend is involved.
	gr := s.get(t, s.url(ct.GetRootsPath))
	var roots ct.GetRootsResponse
	if gr.status != http.StatusOK {
		t.Check("get-roots-exact", false, "status %d body %s", gr.status, trunc(gr.body))
	} else if err := json.Unmarshal(gr.body, &roots); err != nil {
		t.Check("get-roots-exact", false, "unmarshal: %v", err)
	} else {
		got := make([]string, 0, len(roots.Certificates))
		for _, c := range roots.Certificates {
			der, err := base64.StdEncoding.DecodeString(c)
			if err != nil {
				t.Fatalf("root not base64: %v", err)
			}
			got = append(got, sha256Hex(der))
		}
		want := []string{sha256Hex(rootDER), sha256Hex(intDER)}
		sort.Strings(got)
		sort.Strings(want)
		t.Equal("get-roots-exact", strings.Join(got, ","), strings.Join(want, ","))
	}

	// 3. A leaf issued by an untrusted root is refused by admission with 400,
	// and is refused before any backend call: the inherited admission
	// contract, unchanged at this milestone because the log below configures
	// no ext_key_usages list.
	ur := s.postChain(t, [][]byte{unrootedDER})
	t.Check("unrooted-chain-refused", ur.status == http.StatusBadRequest,
		"add-chain of an unrooted leaf returned %d, want 400: %s", ur.status, trunc(ur.body))

	// 4. The same endpoint admits the pinned leaf->intermediate chain and gets
	// as far as the backend, which is not there, so the answer is 503 and not
	// 400. This is the half of the submit path that can be gated without a
	// backend: a personality that wrongly refused a valid chain would answer
	// 400 here, and one that never reached the backend would answer 200.
	ok := s.postChain(t, [][]byte{leafDER, intDER})
	t.Check("valid-chain-admitted-then-backend-unavailable", ok.status == http.StatusServiceUnavailable,
		"add-chain of the pinned valid chain returned %d, want 503: %s", ok.status, trunc(ok.body))

	// 5. get-entries validates its own range before anything else: an
	// inverted range is refused with 400 and never reaches a backend. Pinned
	// at both anchors; this milestone's coercion runs after this check and
	// cannot reach an inverted range.
	inv := s.get(t, s.url(ct.GetEntriesPath)+"?start=10&end=5")
	t.Check("inverted-get-entries-range-refused", inv.status == http.StatusBadRequest,
		"get-entries[10,5] returned %d, want 400: %s", inv.status, trunc(inv.body))

	// 6. A prefix that is not configured is not served at all.
	nf := s.get(t, s.base+"/nosuchlog"+ct.GetRootsPath)
	t.Check("unconfigured-prefix-not-served", nf.status == http.StatusNotFound,
		"get-roots on an unconfigured prefix returned %d, want 404: %s", nf.status, trunc(nf.body))

	t.Done()
}
