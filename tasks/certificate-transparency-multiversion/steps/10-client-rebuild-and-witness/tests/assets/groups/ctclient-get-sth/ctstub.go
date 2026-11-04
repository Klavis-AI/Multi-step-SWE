package main

// ctstub is the benchmark-owned half of every ctclient group: a pinned CT log
// served over a real loopback socket on a port obtained by binding :0, plus a
// runner that executes the candidate's own ctclient binary as a subprocess and
// captures its streams. Nothing here imports the product, so the stub's wire
// shape is the benchmark's, not the candidate's.

import (
	"bytes"
	"context"
	"fmt"
	"net"
	"net/http"
	"os"
	"os/exec"
	"sort"
	"strings"
	"sync"
	"time"

	"ctverify/vhelp"
)

type stubLog struct {
	mu     sync.Mutex
	hits   []string
	bodies map[string][]string

	mux *http.ServeMux
	ln  net.Listener
	srv *http.Server
	// URL is the base URI to hand to --log_uri.
	URL string
}

func newStubLog() *stubLog {
	s := &stubLog{bodies: map[string][]string{}, mux: http.NewServeMux()}
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		fmt.Fprintf(os.Stderr, "stub listen: %v\n", err)
		os.Exit(3)
	}
	s.ln = ln
	s.URL = "http://" + ln.Addr().String()
	s.srv = &http.Server{Handler: s}
	go func() { _ = s.srv.Serve(ln) }()
	return s
}

func (s *stubLog) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	body := new(bytes.Buffer)
	if r.Body != nil {
		_, _ = body.ReadFrom(r.Body)
	}
	s.mu.Lock()
	rec := r.Method + " " + r.URL.Path
	if r.URL.RawQuery != "" {
		rec += "?" + r.URL.RawQuery
	}
	s.hits = append(s.hits, rec)
	s.bodies[r.URL.Path] = append(s.bodies[r.URL.Path], body.String())
	s.mu.Unlock()
	s.mux.ServeHTTP(w, r)
}

// json pins one endpoint to an exact status and an exact response body.
func (s *stubLog) json(path string, status int, body string) {
	s.mux.HandleFunc(path, func(w http.ResponseWriter, _ *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(status)
		_, _ = w.Write([]byte(body))
	})
}

// handle pins one endpoint to a benchmark-owned handler.
func (s *stubLog) handle(path string, fn func(http.ResponseWriter, *http.Request)) {
	s.mux.HandleFunc(path, fn)
}

func (s *stubLog) close() {
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()
	_ = s.srv.Shutdown(ctx)
	_ = s.ln.Close()
}

// requests returns the request lines the stub saw, in arrival order.
func (s *stubLog) requests() []string {
	s.mu.Lock()
	defer s.mu.Unlock()
	out := append([]string{}, s.hits...)
	return out
}

// paths returns the distinct request paths the stub saw, sorted.
func (s *stubLog) paths() []string {
	s.mu.Lock()
	defer s.mu.Unlock()
	seen := map[string]bool{}
	for _, h := range s.hits {
		p := h
		if i := strings.IndexByte(p, '?'); i >= 0 {
			p = p[:i]
		}
		seen[p] = true
	}
	out := make([]string, 0, len(seen))
	for p := range seen {
		out = append(out, p)
	}
	sort.Strings(out)
	return out
}

func (s *stubLog) bodyFor(path string) string {
	s.mu.Lock()
	defer s.mu.Unlock()
	if v := s.bodies[path]; len(v) > 0 {
		return v[0]
	}
	return ""
}

type runResult struct {
	stdout string
	stderr string
	code   int
}

// runClient executes the candidate's ctclient with a pinned environment.
//
// The child is built with vhelp.Command, which strips CT_VERIFY_NONCE, and its
// streams are captured into buffers, so the candidate binary can neither learn
// the nonce nor write on this group's stdout. TZ is pinned so that every
// time.Time the tool renders is deterministic.
func runClient(args ...string) runResult {
	cmd := vhelp.Command(vhelp.CandidateBin("ctclient"), args...)
	scratch := os.Getenv("CT_SCRATCH")
	env := make([]string, 0, len(cmd.Env)+3)
	for _, kv := range cmd.Env {
		if strings.HasPrefix(kv, "TZ=") || strings.HasPrefix(kv, "HOME=") ||
			strings.HasPrefix(kv, "TMPDIR=") {
			continue
		}
		env = append(env, kv)
	}
	cmd.Env = append(env, "TZ=UTC", "HOME="+scratch, "TMPDIR="+scratch)
	cmd.Dir = scratch

	var out, errb bytes.Buffer
	cmd.Stdout = &out
	cmd.Stderr = &errb
	if err := cmd.Start(); err != nil {
		return runResult{stderr: "start error: " + err.Error(), code: -1}
	}
	// A hung candidate must not hang the group.
	timer := time.AfterFunc(60*time.Second, func() {
		if cmd.Process != nil {
			_ = cmd.Process.Kill()
		}
	})
	err := cmd.Wait()
	timer.Stop()

	code := 0
	if ee, ok := err.(*exec.ExitError); ok {
		code = ee.ExitCode()
	} else if err != nil {
		code = -1
		errb.WriteString("\nrun error: " + err.Error())
	}
	return runResult{stdout: out.String(), stderr: errb.String(), code: code}
}

// requireClient fails the whole group when there is no ctclient to drive, so
// the group can never report success having measured nothing.
func requireClient(t *vhelp.T) {
	st, err := os.Stat(vhelp.CandidateBin("ctclient"))
	if err != nil || st.Size() == 0 {
		t.Fatalf("no ctclient binary built from the candidate tree: %v", err)
	}
}
