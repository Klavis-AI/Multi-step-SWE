// Group getentries-alignment-metric.
//
// v1.1.2 coerces get-entries ranges so that large runs of requests converge on
// the same boundaries, and reports what it did through a new exported counter,
// aligned_get_entries{logid,aligned}. The coercion happens in the handler's own
// range parsing, before any backend call, and so does the counter increment.
// This group therefore runs the candidate's ct_server with no log backend at
// all and reads the counter off the metrics endpoint.
//
// SCOPE-BACKEND: deliberately no backend and no stub here, unlike the groups
// that share _kit/serverkit.go. Nothing below imports google/trillian, which is
// the point: the driver compiles against v1.1.0 as well, so the step-08
// cross-check is a real behavioural failure -- the predecessor coerces nothing
// and exports no such counter -- rather than a compile error. ct_server is
// configured with two backends because it only uses a blocking dial when
// exactly one is configured; with two it serves immediately and anything that
// does reach a backend answers 503.
//
// SCOPE-NET: loopback only. The two serving ports are obtained by binding :0
// and releasing; because ct_server does not report the port it bound, that
// hand-off has a window, so a start that dies before serving is retried with
// fresh numbers rather than allowed to look like a behavioural failure. The
// two backend addresses are deliberately *not* drawn from that pool: a port
// this process released could be bound by something else a moment later,
// which would make a supposedly dead backend answer. Ports 1 and 2 cannot be
// bound by an unprivileged process, so a dial there is refused, always.
// SCOPE-TIME: the expected counter values are pinned arithmetic on
// --max_get_entries=16, not measurements.
package main

import (
	"encoding/pem"
	"fmt"
	"io"
	"net"
	"net/http"
	"os"
	"os/exec"
	"strconv"
	"strings"
	"time"

	"ctverify/vhelp"
)

const (
	pinnedLogID = int64(1001)
	logPrefix   = "bench"
	// The whole group is arithmetic on this one number. 16 is small enough
	// that every boundary below is checkable by hand and is not a multiple of
	// the built-in MaxGetEntriesAllowed, so nothing can pass by accident.
	maxGetEntries = 16
	// Unbindable without privilege, so nothing can ever be listening here.
	deadBackendA = "127.0.0.1:1"
	deadBackendB = "127.0.0.1:2"
)

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

func scratch(name string) string { return os.Getenv("CT_SCRATCH") + "/" + name }

// writeConfig synthesises this group's pinned configuration. The keys and the
// root set are fixtures; only the two backend addresses are chosen, and they
// are chosen to be unreachable.
func writeConfig(t *vhelp.T, deadA, deadB string) string {
	keyDER := pemBody(t, vhelp.ReadFixture("minted", "keys", "log-key.pem"))
	pubDER := pemBody(t, vhelp.ReadFixture("minted", "keys", "log-pubkey.pem"))

	var b strings.Builder
	b.WriteString("backends {\n")
	b.WriteString("  backend { name: \"default\" backend_spec: \"" + deadA + "\" }\n")
	b.WriteString("  backend { name: \"spare\"   backend_spec: \"" + deadB + "\" }\n")
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
	http    string
	metrics string
	errPath string
}

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

// start brings ct_server up, retrying a start that died before it served.
// The only thing a retry can cure is losing the race for one of the two
// ephemeral ports between releasing it and the child binding it; anything
// else fails the same way every time and is reported after the last attempt.
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
	cfg := writeConfig(t, deadBackendA, deadBackendB)
	httpAddr := "127.0.0.1:" + strconv.Itoa(freePort(t))
	metricsAddr := "127.0.0.1:" + strconv.Itoa(freePort(t))

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
		"--metrics_endpoint="+metricsAddr,
		"--max_get_entries="+strconv.Itoa(maxGetEntries),
		"--get_sth_interval=0",
		"--rpc_deadline=3s",
		"--logtostderr",
	)
	cmd.Stdout = errFile
	cmd.Stderr = errFile
	if err := cmd.Start(); err != nil {
		t.Fatalf("start ct_server: %v", err)
	}
	s := &server{
		cmd:     cmd,
		exited:  make(chan error, 1),
		http:    "http://" + httpAddr,
		metrics: "http://" + metricsAddr + "/metrics",
		errPath: errPath,
	}
	go func() { s.exited <- cmd.Wait() }()

	deadline := time.Now().Add(60 * time.Second)
	for {
		select {
		case <-s.exited:
			// A child that is gone is never coming up; do not spend the
			// rest of the deadline discovering that.
			return nil, fmt.Sprintf("attempt %d on %s exited before serving: %s",
				attempt, httpAddr, tail(errPath))
		default:
		}
		if time.Now().After(deadline) {
			s.stop()
			return nil, fmt.Sprintf("attempt %d on %s never became healthy: %s",
				attempt, httpAddr, tail(errPath))
		}
		resp, err := http.Get(s.http + "/healthz")
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

// getEntries issues one get-entries request and returns its HTTP status.
func (s *server) getEntries(t *vhelp.T, start, end int64) int {
	u := fmt.Sprintf("%s/%s/ct/v1/get-entries?start=%d&end=%d", s.http, logPrefix, start, end)
	resp, err := http.Get(u)
	if err != nil {
		t.Fatalf("GET %s: %v", u, err)
	}
	_, _ = io.Copy(io.Discard, resp.Body)
	_ = resp.Body.Close()
	return resp.StatusCode
}

// counters reads aligned_get_entries for this log. A label combination that
// has never been incremented is absent from the exposition and reads as 0, so
// "no counter at all" and "counted nothing" are the same observation here --
// which is exactly the predecessor's behaviour.
func (s *server) counters(t *vhelp.T) (aligned, misaligned int) {
	resp, err := http.Get(s.metrics)
	if err != nil {
		t.Fatalf("GET %s: %v", s.metrics, err)
	}
	body, err := io.ReadAll(resp.Body)
	_ = resp.Body.Close()
	if err != nil {
		t.Fatalf("read metrics: %v", err)
	}
	want := `logid="` + strconv.FormatInt(pinnedLogID, 10) + `"`
	for _, line := range strings.Split(string(body), "\n") {
		if !strings.HasPrefix(line, "aligned_get_entries{") || !strings.Contains(line, want) {
			continue
		}
		sp := strings.LastIndexByte(line, ' ')
		if sp < 0 {
			continue
		}
		v, err := strconv.Atoi(strings.TrimSpace(line[sp+1:]))
		if err != nil {
			continue
		}
		switch {
		case strings.Contains(line, `aligned="true"`):
			aligned = v
		case strings.Contains(line, `aligned="false"`):
			misaligned = v
		}
	}
	return aligned, misaligned
}

func (s *server) observe(t *vhelp.T, id string, start, end int64, wantStatus, wantAligned, wantMis int) {
	got := s.getEntries(t, start, end)
	a, m := s.counters(t)
	t.Equal(id, fmt.Sprintf("status=%d aligned=%d misaligned=%d", got, a, m),
		fmt.Sprintf("status=%d aligned=%d misaligned=%d", wantStatus, wantAligned, wantMis))
}

func main() {
	t := vhelp.New()
	s := start(t)
	// Done calls os.Exit, which does not run deferred functions, so a
	// `defer s.stop()` here would never fire and the ct_server child would
	// outlive the group. Register the teardown with the harness instead --
	// this is what the shared kit (_kit/serverkit.go) already does.
	t.Cleanup(s.stop)

	// 1. Before any get-entries request there is nothing to report.
	a, m := s.counters(t)
	t.Equal("counter-starts-empty", fmt.Sprintf("%d/%d", a, m), "0/0")

	// The requests below are cumulative and must run in this order. With
	// maxRange = 16 the release coerces end to end - ((end+1) mod 16) for any
	// request whose span is at least maxRange, and records whether that
	// subtraction was a no-op.

	// 2. [0,15] spans exactly 16 and 16 mod 16 == 0, so it is already
	// aligned: coerced to itself, counted as aligned. It then reaches the
	// backend, which is not there, so the request itself answers 503.
	s.observe(t, "already-aligned-range-counted-aligned", 0, 15, http.StatusServiceUnavailable, 1, 0)

	// 3. [5,20] also spans exactly 16, but 21 mod 16 == 5, so the end moves
	// back to 15 and the request is counted as misaligned.
	s.observe(t, "shifted-maximal-range-counted-misaligned", 5, 20, http.StatusServiceUnavailable, 1, 1)

	// 4. [5,1000] is first clamped to [5,20] by the maximum, and then the
	// same alignment applies: misaligned again.
	s.observe(t, "oversized-range-clamped-then-counted-misaligned", 5, 1000, http.StatusServiceUnavailable, 1, 2)

	// 5. [5,9] spans 5, below the maximum, so nothing is coerced and nothing
	// is counted. The counters keep the values case 4 left.
	s.observe(t, "short-range-not-counted", 5, 9, http.StatusServiceUnavailable, 1, 2)

	// 6. An inverted range never reaches the coercion: it is refused by the
	// handler's own validation with 400 and leaves the counters alone.
	s.observe(t, "inverted-range-rejected-before-coercion", 10, 5, http.StatusBadRequest, 1, 2)

	t.Done()
}
