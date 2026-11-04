package main

// gatekit is the gate's own bring-up: the candidate's ct_server in front of the
// pinned backend stub. Nothing here holds a case; it only starts and stops
// processes and speaks HTTP to them. The feeder half of this file moved to the
// scored group witness-feeder-endtoend with the cases that used it.
//
// SCOPE-NET: loopback only. The backend stub binds :0 and serves on the
// listener it got back; ct_server's port is named on its command line, so it
// is probed for below the kernel's ephemeral range. SCOPE-TIME: the only
// value that varies between runs is a port number.

import (
	"fmt"
	"net"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"time"

	"github.com/google/trillian"
	"google.golang.org/grpc"

	"ctverify/vhelp"
)

// protoEscape renders bytes as the \xNN escapes prototext accepts.
func protoEscape(b []byte) string {
	var s strings.Builder
	for _, c := range b {
		fmt.Fprintf(&s, "\\x%02x", c)
	}
	return s.String()
}

// writeCTFEConfig synthesises this group's pinned CTFE configuration. The
// backend address is the only part that is not fixed, because it is only known
// once the kernel has chosen a port.
func writeCTFEConfig(t *vhelp.T, backendAddr string) string {
	keyDER := pemBody(t, vhelp.ReadFixture("minted", "keys", "log-key.pem"))
	pubDER := pemBody(t, vhelp.ReadFixture("minted", "keys", "log-pubkey.pem"))
	rootsPath := vhelp.Fixture("minted", "roots-bench-plus-int.pem")

	var b strings.Builder
	b.WriteString("backends {\n  backend {\n    name: \"default\"\n    backend_spec: \"" + backendAddr + "\"\n  }\n}\n")
	b.WriteString("log_configs {\n  config {\n")
	fmt.Fprintf(&b, "    log_id: %d\n", pinnedLogID)
	fmt.Fprintf(&b, "    prefix: %q\n", logPrefix)
	fmt.Fprintf(&b, "    roots_pem_file: %q\n", rootsPath)
	b.WriteString("    private_key: {\n      [type.googleapis.com/keyspb.PrivateKey] {\n        der: \"" + protoEscape(keyDER) + "\"\n      }\n    }\n")
	b.WriteString("    public_key: { der: \"" + protoEscape(pubDER) + "\" }\n")
	b.WriteString("    log_backend_name: \"default\"\n")
	b.WriteString("  }\n}\n")

	p := filepath.Join(os.Getenv("CT_SCRATCH"), "ctfe.textproto")
	if err := os.WriteFile(p, []byte(b.String()), 0o600); err != nil {
		t.Fatalf("write CTFE config: %v", err)
	}
	return p
}

type harness struct {
	backend  *stubBackend
	grpcSrv  *grpc.Server
	cmd      *exec.Cmd
	exited   chan error
	baseURL  string
	stderrTo string
}

// startPersonality boots the candidate's own ct_server against the pinned
// backend and waits for /healthz. A failure here aborts the group rather than
// letting it report a vacuous pass.
//
// SCOPE-TIME: freeLoopbackPort probes for a free port below the kernel's
// ephemeral range and releases it before handing it to --http_endpoint,
// because ct_server takes its listen address as a flag and never reports the
// port it bound. That hand-off has a window, and
// this host runs many of these probes at once, so a lost race is retried with
// a fresh number rather than allowed to look like a behavioural failure -- the
// same treatment step 09's gate gives it. Nothing here measures elapsed time;
// the readiness poll is a prerequisite, not a case.
func startPersonality(t *vhelp.T) *harness {
	be := newStubBackend(vhelp.ReadFixture("minted", "leaf-valid.der"))

	lis, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("listen for backend: %v", err)
	}
	gs := grpc.NewServer()
	trillian.RegisterTrillianLogServer(gs, be)
	go func() { _ = gs.Serve(lis) }()

	cfg := writeCTFEConfig(t, lis.Addr().String())

	why := ""
	for attempt := 1; attempt <= 4; attempt++ {
		h, reason := tryPersonality(t, be, gs, cfg, attempt)
		if h != nil {
			return h
		}
		why = reason
	}
	gs.Stop()
	t.Fatalf("ct_server never served after 4 attempts: %s", why)
	return nil
}

// tryPersonality makes one attempt on a freshly chosen port. It returns either
// a running harness or the reason this attempt did not produce one.
func tryPersonality(t *vhelp.T, be *stubBackend, gs *grpc.Server, cfg string, attempt int) (*harness, string) {
	addr := "127.0.0.1:" + strconv.Itoa(freeLoopbackPort(t))
	errPath := filepath.Join(os.Getenv("CT_SCRATCH"), fmt.Sprintf("ct_server-%d.stderr", attempt))
	errFile, err := os.Create(errPath)
	if err != nil {
		t.Fatalf("create stderr sink: %v", err)
	}
	cmd := vhelp.Command(vhelp.CandidateBin("ct_server"),
		"--log_config="+cfg,
		"--http_endpoint="+addr,
		"--get_sth_interval=0",
		"--rpc_deadline=20s",
		"--logtostderr")
	// The candidate's own output must never reach this group's stdout.
	cmd.Stdout = errFile
	cmd.Stderr = errFile
	if err := cmd.Start(); err != nil {
		gs.Stop()
		t.Fatalf("start ct_server: %v", err)
	}
	exited := make(chan error, 1)
	go func() { exited <- cmd.Wait() }()

	h := &harness{backend: be, grpcSrv: gs, cmd: cmd, exited: exited,
		baseURL: "http://" + addr, stderrTo: errPath}
	// Done calls os.Exit, which does not run deferred functions, so a
	// `defer h.stop()` in a driver never fires and the child survives the
	// group. Register the teardown with the harness instead.
	t.Cleanup(h.stop)

	deadline := time.Now().Add(60 * time.Second)
	for {
		select {
		case <-exited:
			// A child that is gone is never coming up; do not spend the rest
			// of the deadline discovering that.
			tail, _ := os.ReadFile(errPath)
			return nil, fmt.Sprintf("attempt %d on %s exited before serving: %s",
				attempt, addr, lastBytes(tail, 1500))
		default:
		}
		if time.Now().After(deadline) {
			_ = cmd.Process.Kill()
			<-exited
			tail, _ := os.ReadFile(errPath)
			return nil, fmt.Sprintf("attempt %d on %s never became healthy: %s",
				attempt, addr, lastBytes(tail, 1500))
		}
		resp, err := http.Get(h.baseURL + "/healthz")
		if err == nil {
			_ = resp.Body.Close()
			if resp.StatusCode == http.StatusOK {
				return h, ""
			}
		}
		time.Sleep(100 * time.Millisecond)
	}
}

// stop terminates only the process this group started, by the exact PID it
// captured itself.
func (h *harness) stop() {
	if h.cmd != nil && h.cmd.Process != nil {
		_ = h.cmd.Process.Kill()
		if h.exited != nil {
			<-h.exited
			h.exited = nil
		}
		h.cmd = nil
	}
	if h.grpcSrv != nil {
		h.grpcSrv.Stop()
		h.grpcSrv = nil
	}
}

// logBaseURL is the per-log prefix the feeder has to be pointed at.
func (h *harness) logBaseURL() string { return h.baseURL + "/" + logPrefix }

func (h *harness) url(path string) string { return h.baseURL + "/" + logPrefix + path }

type httpResult struct {
	status int
	body   []byte
}

func httpGet(t *vhelp.T, u string) httpResult {
	resp, err := http.Get(u)
	if err != nil {
		t.Fatalf("GET %s: %v", u, err)
	}
	defer func() { _ = resp.Body.Close() }()
	return httpResult{status: resp.StatusCode, body: drain(resp.Body)}
}

func httpPostJSON(t *vhelp.T, u string, payload []byte) httpResult {
	resp, err := http.Post(u, "application/json", strings.NewReader(string(payload)))
	if err != nil {
		t.Fatalf("POST %s: %v", u, err)
	}
	defer func() { _ = resp.Body.Close() }()
	return httpResult{status: resp.StatusCode, body: drain(resp.Body)}
}

func drain(r interface{ Read([]byte) (int, error) }) []byte {
	out := make([]byte, 0, 4096)
	buf := make([]byte, 32*1024)
	for {
		n, err := r.Read(buf)
		out = append(out, buf[:n]...)
		if err != nil {
			return out
		}
	}
}
