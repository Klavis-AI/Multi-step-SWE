// serverkit.go is copied verbatim into every step-12 group that drives the
// candidate's own ct_server binary -- gate-endtoend, ctserver-cache-flags,
// ctserver-shared-private-key and ctfe-backend-error-http-status. It is not a
// group: it holds no cases and prints nothing. Each copy is compiled into
// exactly one group's single compilation unit, so a group still owns one
// process tree and one binary.
//
// "Verbatim" is an invariant and not just a description: all five files must
// stay byte-identical (one md5 across _kit and the four copies), so an edit
// here is propagated by overwriting the copies and by nothing else. A group
// that needs a helper the other three do not gets it in its own main.go --
// ctserver-shared-private-key's awaitHealthy is there, not here, for exactly
// that reason. If a copy ever diverges, the next propagation deletes the
// divergence without a word, so the invariant is what keeps the copies safe
// to regenerate.
//
// SCOPE-BACKEND: google/trillian is not a deliverable and is never graded, so
// nothing below is an assertion about it. The kit supplies a backend because
// the capability under test needs one: this step is about what the log puts
// in a leaf's extra data and about how a backend's gRPC status becomes an
// HTTP status, and neither is reachable without a log that can answer. The
// stub is an in-process TrillianLogServer served over a real loopback gRPC
// socket, written against this step's pinned trillian v1.6.0 API only. It
// stays cheap because the CTFE performs no signature check on the root it is
// handed -- it calls UnmarshalBinary on GetLogRoot() and stops -- and the SCT
// is signed by the CTFE's own configured key, so a marshalled unsigned
// LogRootV1 is sufficient.
//
// SCOPE-DB: no database server is started anywhere in this kit. The logs it
// configures use the Trillian-gRPC issuance chain storage backend, which is
// the one that needs no database.
//
// SCOPE-NET: loopback only, every port obtained by binding :0. SCOPE-TIME:
// every timestamp and root below is a constant.
package main

import (
	"context"
	"crypto/sha256"
	"encoding/pem"
	"fmt"
	"net"
	"net/http"
	"os"
	"os/exec"
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/google/trillian"
	"github.com/google/trillian/types"
	"google.golang.org/grpc"

	"ctverify/vhelp"
)

// Pinned backend state. Nothing here reads a clock.
const (
	pinnedTreeSize    = uint64(100000)
	pinnedTimestampMS = uint64(1500000000000)
	pinnedLogID       = int64(1001)
	pinnedLogID2      = int64(1002)
	logPrefix         = "bench"
	logPrefix2        = "bench2"
)

var pinnedRootHash = sha256.Sum256([]byte("harbor-ct-bench-pinned-root"))

// ---------------------------------------------------------------------------
// The pinned backend.
// ---------------------------------------------------------------------------

type stubBackend struct {
	trillian.UnimplementedTrillianLogServer

	mu     sync.Mutex
	queued []*trillian.LogLeaf
	errFor map[string]error
	stored *trillian.LogLeaf
}

func newStub() *stubBackend { return &stubBackend{errFor: map[string]error{}} }

// failWith makes the named backend method answer with err until it is reset.
func (s *stubBackend) failWith(method string, err error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.errFor[method] = err
}

func (s *stubBackend) errOf(method string) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.errFor[method]
}

func (s *stubBackend) queuedLeaves() []*trillian.LogLeaf {
	s.mu.Lock()
	defer s.mu.Unlock()
	out := make([]*trillian.LogLeaf, len(s.queued))
	copy(out, s.queued)
	return out
}

// setStored fixes the single leaf the backend serves from get-entries.
func (s *stubBackend) setStored(l *trillian.LogLeaf) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.stored = l
}

func (s *stubBackend) storedLeaf(i int64) *trillian.LogLeaf {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.stored == nil {
		return &trillian.LogLeaf{LeafIndex: i, LeafValue: []byte{0x00}, ExtraData: []byte{0x00, 0x00, 0x00}}
	}
	l := *s.stored
	l.LeafIndex = i
	return &l
}

func (s *stubBackend) signedRoot() *trillian.SignedLogRoot {
	r := types.LogRootV1{
		TreeSize:       pinnedTreeSize,
		RootHash:       pinnedRootHash[:],
		TimestampNanos: pinnedTimestampMS * 1000000,
	}
	b, err := r.MarshalBinary()
	if err != nil {
		panic(err)
	}
	return &trillian.SignedLogRoot{LogRoot: b}
}

func (s *stubBackend) QueueLeaf(_ context.Context, req *trillian.QueueLeafRequest) (*trillian.QueueLeafResponse, error) {
	if err := s.errOf("QueueLeaf"); err != nil {
		return nil, err
	}
	s.mu.Lock()
	s.queued = append(s.queued, req.GetLeaf())
	s.mu.Unlock()
	return &trillian.QueueLeafResponse{QueuedLeaf: &trillian.QueuedLogLeaf{Leaf: req.GetLeaf()}}, nil
}

func (s *stubBackend) GetLeavesByRange(_ context.Context, req *trillian.GetLeavesByRangeRequest) (*trillian.GetLeavesByRangeResponse, error) {
	if err := s.errOf("GetLeavesByRange"); err != nil {
		return nil, err
	}
	n := req.GetCount()
	if n < 0 {
		n = 0
	}
	if n > 16 {
		n = 16
	}
	leaves := make([]*trillian.LogLeaf, 0, n)
	for i := int64(0); i < n; i++ {
		leaves = append(leaves, s.storedLeaf(req.GetStartIndex()+i))
	}
	return &trillian.GetLeavesByRangeResponse{Leaves: leaves, SignedLogRoot: s.signedRoot()}, nil
}

func (s *stubBackend) GetLatestSignedLogRoot(_ context.Context, _ *trillian.GetLatestSignedLogRootRequest) (*trillian.GetLatestSignedLogRootResponse, error) {
	if err := s.errOf("GetLatestSignedLogRoot"); err != nil {
		return nil, err
	}
	return &trillian.GetLatestSignedLogRootResponse{SignedLogRoot: s.signedRoot()}, nil
}

func (s *stubBackend) GetInclusionProof(_ context.Context, req *trillian.GetInclusionProofRequest) (*trillian.GetInclusionProofResponse, error) {
	if err := s.errOf("GetInclusionProof"); err != nil {
		return nil, err
	}
	return &trillian.GetInclusionProofResponse{
		Proof:         &trillian.Proof{LeafIndex: req.GetLeafIndex(), Hashes: [][]byte{pinnedRootHash[:]}},
		SignedLogRoot: s.signedRoot(),
	}, nil
}

func (s *stubBackend) GetInclusionProofByHash(_ context.Context, _ *trillian.GetInclusionProofByHashRequest) (*trillian.GetInclusionProofByHashResponse, error) {
	if err := s.errOf("GetInclusionProofByHash"); err != nil {
		return nil, err
	}
	return &trillian.GetInclusionProofByHashResponse{
		Proof:         []*trillian.Proof{{LeafIndex: 7, Hashes: [][]byte{pinnedRootHash[:]}}},
		SignedLogRoot: s.signedRoot(),
	}, nil
}

func (s *stubBackend) GetConsistencyProof(_ context.Context, _ *trillian.GetConsistencyProofRequest) (*trillian.GetConsistencyProofResponse, error) {
	if err := s.errOf("GetConsistencyProof"); err != nil {
		return nil, err
	}
	return &trillian.GetConsistencyProofResponse{
		Proof:         &trillian.Proof{Hashes: [][]byte{pinnedRootHash[:]}},
		SignedLogRoot: s.signedRoot(),
	}, nil
}

func (s *stubBackend) GetEntryAndProof(_ context.Context, req *trillian.GetEntryAndProofRequest) (*trillian.GetEntryAndProofResponse, error) {
	if err := s.errOf("GetEntryAndProof"); err != nil {
		return nil, err
	}
	return &trillian.GetEntryAndProofResponse{
		Proof:         &trillian.Proof{LeafIndex: req.GetLeafIndex(), Hashes: [][]byte{pinnedRootHash[:]}},
		Leaf:          s.storedLeaf(req.GetLeafIndex()),
		SignedLogRoot: s.signedRoot(),
	}, nil
}

func (s *stubBackend) InitLog(_ context.Context, _ *trillian.InitLogRequest) (*trillian.InitLogResponse, error) {
	return &trillian.InitLogResponse{Created: s.signedRoot()}, nil
}

func (s *stubBackend) AddSequencedLeaves(_ context.Context, _ *trillian.AddSequencedLeavesRequest) (*trillian.AddSequencedLeavesResponse, error) {
	return &trillian.AddSequencedLeavesResponse{}, nil
}

// ---------------------------------------------------------------------------
// Bringing up the personality.
// ---------------------------------------------------------------------------

type harness struct {
	backend  *stubBackend
	grpcSrv  *grpc.Server
	cmd      *exec.Cmd
	baseURL  string
	cfgPath  string
	stderrTo string
	// reaped is closed by the single goroutine that owns cmd.Wait(); see
	// launch. exited, waitExit and stop all read the child's fate through
	// it, so Wait is called exactly once for the harness's whole life.
	reaped chan struct{}
}

// freeLoopbackPort returns a free loopback port from BELOW the kernel's
// ephemeral range. It binds :0 once, but only to disperse the starting slot:
// the port it returns is one it has scanned for and held, never the ephemeral
// port the kernel handed the probe. No fixed port number appears anywhere in
// this corpus.
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

// logSpec describes one log in the generated configuration.
type logSpec struct {
	id        int64
	prefix    string
	privKey   []byte // PKCS#8 DER
	pubKey    []byte // SubjectPublicKeyInfo DER, optional
	extraText string // extra lines inside the log config
}

// writeConfig writes this group's own pinned CTFE configuration into the
// group's private scratch directory. It is synthesised rather than read from
// a file so that the backend address, which is only known once the kernel has
// chosen a port, is the only thing that varies between runs. Keys and roots
// are pinned fixtures (SCOPE-TIME).
func writeConfig(t *vhelp.T, backendAddr string, logs []logSpec) string {
	rootsPath := vhelp.Fixture("minted", "roots-bench-plus-int.pem")

	// LogMultiConfig.backends and .log_configs are singular message fields,
	// so the whole set nests inside one block each.
	var b strings.Builder
	b.WriteString("backends {\n  backend {\n    name: \"default\"\n    backend_spec: \"" + backendAddr + "\"\n  }\n}\n")
	b.WriteString("log_configs {\n")
	for _, l := range logs {
		b.WriteString("  config {\n")
		fmt.Fprintf(&b, "    log_id: %d\n", l.id)
		fmt.Fprintf(&b, "    prefix: %q\n", l.prefix)
		fmt.Fprintf(&b, "    roots_pem_file: %q\n", rootsPath)
		b.WriteString("    private_key: {\n      [type.googleapis.com/keyspb.PrivateKey] {\n        der: \"" + protoEscape(l.privKey) + "\"\n      }\n    }\n")
		if l.pubKey != nil {
			b.WriteString("    public_key: { der: \"" + protoEscape(l.pubKey) + "\" }\n")
		}
		b.WriteString("    log_backend_name: \"default\"\n")
		b.WriteString(l.extraText)
		b.WriteString("  }\n")
	}
	b.WriteString("}\n")

	p := scratchPath("ctfe.textproto")
	if err := os.WriteFile(p, []byte(b.String()), 0600); err != nil {
		t.Fatalf("write config: %v", err)
	}
	return p
}

// pemBody returns the DER inside the first PEM block of b.
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

func scratchPath(name string) string {
	d := os.Getenv("CT_SCRATCH")
	if strings.HasSuffix(d, "/") {
		return d + name
	}
	return d + "/" + name
}

// startPersonality boots the candidate's own ct_server against the pinned
// backend and waits for /healthz. A failure here aborts the group rather than
// letting it report a vacuous pass.
func startPersonality(t *vhelp.T, be *stubBackend, logs []logSpec, extraArgs []string) *harness {
	lis, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("listen for backend: %v", err)
	}
	gs := grpc.NewServer()
	trillian.RegisterTrillianLogServer(gs, be)
	go func() { _ = gs.Serve(lis) }()

	cfg := writeConfig(t, lis.Addr().String(), logs)
	h, err := launch(t, be, gs, cfg, extraArgs, true)
	if err != nil {
		t.Fatalf("%v", err)
	}
	return h
}

// launch starts ct_server and, if wait is set, blocks until /healthz answers.
// It returns an error rather than aborting so that a group may legitimately
// expect the server to refuse to start.
func launch(t *vhelp.T, be *stubBackend, gs *grpc.Server, cfg string, extraArgs []string, wait bool) (*harness, error) {
	port := freeLoopbackPort(t)
	addr := "127.0.0.1:" + strconv.Itoa(port)
	args := []string{
		"--log_config=" + cfg,
		"--http_endpoint=" + addr,
		"--get_sth_interval=0",
		"--rpc_deadline=20s",
		"--logtostderr",
	}
	args = append(args, extraArgs...)

	errPath := scratchPath("ct_server." + strconv.Itoa(port) + ".log")
	errFile, err := os.Create(errPath)
	if err != nil {
		t.Fatalf("create stderr sink: %v", err)
	}
	cmd := vhelp.Command(vhelp.CandidateBin("ct_server"), args...)
	// The candidate's own output must never reach this group's stdout, or it
	// could forge a nonce-prefixed line if it ever learned the nonce.
	cmd.Stdout = errFile
	cmd.Stderr = errFile
	if err := cmd.Start(); err != nil {
		if gs != nil {
			gs.Stop()
		}
		t.Fatalf("start ct_server: %v", err)
	}

	h := &harness{backend: be, grpcSrv: gs, cmd: cmd, baseURL: "http://" + addr, cfgPath: cfg, stderrTo: errPath, reaped: make(chan struct{})}
	// exec.Cmd records a child's exit status only inside Wait, so a harness
	// that never calls Wait can never notice its child die -- the health
	// loop below would poll out its whole deadline against a process that
	// left immediately. One goroutine owns that call, because two callers of
	// Wait on one Cmd is a double-wait race rather than a fix.
	go func() { _ = cmd.Wait(); close(h.reaped) }()
	// Done calls os.Exit, which does not run deferred functions, so a
	// `defer h.stop()` in a driver never fires and the child survives the
	// group. Register the teardown with the harness instead.
	t.Cleanup(h.stop)
	if !wait {
		return h, nil
	}

	deadline := time.Now().Add(90 * time.Second)
	for {
		if time.Now().After(deadline) {
			tail, _ := os.ReadFile(errPath)
			h.stop()
			return nil, fmt.Errorf("ct_server never became healthy; log tail: %s", lastBytes(tail, 1500))
		}
		resp, err := http.Get(h.baseURL + "/healthz")
		if err == nil {
			_ = resp.Body.Close()
			if resp.StatusCode == http.StatusOK {
				return h, nil
			}
		}
		if h.exited() {
			tail, _ := os.ReadFile(errPath)
			h.stop()
			return nil, fmt.Errorf("ct_server exited early; log tail: %s", lastBytes(tail, 1500))
		}
		time.Sleep(100 * time.Millisecond)
	}
}

// exited reports whether the server process is gone, without blocking. It
// reads the reaper's channel and not cmd.ProcessState: ProcessState stays nil
// until a Wait completes, and only the reaper calls Wait, so a ProcessState
// test here would answer "still running" forever.
func (h *harness) exited() bool {
	if h.cmd == nil || h.cmd.Process == nil {
		return true
	}
	select {
	case <-h.reaped:
		return true
	default:
		return false
	}
}

// waitExit reports the server's exit code once the reaper has reaped it, or
// -1 on timeout. It never calls Wait itself.
func (h *harness) waitExit(d time.Duration) int {
	select {
	case <-h.reaped:
		if h.cmd.ProcessState == nil {
			return -1
		}
		return h.cmd.ProcessState.ExitCode()
	case <-time.After(d):
		return -1
	}
}

func lastBytes(b []byte, n int) string {
	if len(b) > n {
		b = b[len(b)-n:]
	}
	return strings.ReplaceAll(string(b), "\n", " | ")
}

// stop terminates only the process this group started, by the exact PID it
// captured itself.
func (h *harness) stop() {
	if h.cmd != nil && h.cmd.Process != nil {
		_ = h.cmd.Process.Kill()
		// Let the reaper do the reaping. stop is called both explicitly and
		// from t.Cleanup, so it must be safe to run twice, and a Wait here
		// would race the reaper's.
		if h.reaped != nil {
			select {
			case <-h.reaped:
			case <-time.After(30 * time.Second):
			}
		}
	}
	if h.grpcSrv != nil {
		h.grpcSrv.Stop()
	}
}

func (h *harness) url(path string) string { return h.baseURL + "/" + logPrefix + path }

type httpResult struct {
	status int
	body   []byte
	header http.Header
}

func readAll(r interface{ Read([]byte) (int, error) }) []byte {
	b := make([]byte, 0, 4096)
	buf := make([]byte, 32*1024)
	for {
		n, err := r.Read(buf)
		b = append(b, buf[:n]...)
		if err != nil {
			break
		}
	}
	return b
}

func (h *harness) get(t *vhelp.T, url string) httpResult {
	resp, err := http.Get(url)
	if err != nil {
		t.Fatalf("GET %s: %v", url, err)
	}
	defer func() { _ = resp.Body.Close() }()
	return httpResult{status: resp.StatusCode, body: readAll(resp.Body), header: resp.Header}
}

func (h *harness) postJSON(t *vhelp.T, url string, payload []byte) httpResult {
	resp, err := http.Post(url, "application/json", strings.NewReader(string(payload)))
	if err != nil {
		t.Fatalf("POST %s: %v", url, err)
	}
	defer func() { _ = resp.Body.Close() }()
	return httpResult{status: resp.StatusCode, body: readAll(resp.Body), header: resp.Header}
}
