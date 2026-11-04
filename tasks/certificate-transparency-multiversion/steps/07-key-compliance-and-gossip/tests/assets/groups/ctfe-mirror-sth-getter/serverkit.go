// serverkit.go is copied verbatim into every step-07 group that needs a log
// backend or the candidate's own ct_server binary. It is not a group: it
// holds no cases and prints nothing. Each copy is compiled into exactly one
// group's single compilation unit, so a group still owns one process tree and
// one binary.
//
// SCOPE-BACKEND: google/trillian is not a deliverable and is never graded, so
// nothing below is an assertion about it. The kit supplies a backend because
// the capability under test needs one -- a personality that cannot reach a
// log cannot answer get-sth, and SetUpInstance wants a client. The stub is an
// in-process TrillianLogServer served over a real loopback gRPC socket,
// written against this step's pinned trillian v1.2.1 API only. At this anchor
// trillian/ctfe reads the deprecated scalar fields of SignedLogRoot
// (slr.TreeSize, slr.RootHash, slr.TimestampNanos) and performs no signature
// check of any kind on them, and it signs the SCT/STH with its own configured
// key, so an unsigned root is sufficient and no log-root signing is
// reimplemented here.
//
// SCOPE-NET: loopback only, every port obtained by binding :0. SCOPE-TIME:
// every timestamp and root hash below is a compile-time constant. The only
// clock the candidate reads for itself is the SCT/STH timestamp, and no case
// asserts its value.
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
	"syscall"
	"time"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/tls"
	"github.com/google/trillian"
	"google.golang.org/grpc"

	// Registers the keyspb.PrivateKey ProtoHandler, exactly as ct_server's own
	// main does; without it keys.NewSigner cannot load the pinned DER key.
	_ "github.com/google/trillian/crypto/keys/der/proto"

	"ctverify/vhelp"
)

// Pinned backend state. Nothing here reads a clock.
const (
	pinnedTreeSize    = int64(100000)
	pinnedTimestampMS = int64(1500000000000)
	pinnedLogID       = int64(1001)
	pinnedLogID2      = int64(1002)
	logPrefix         = "bench"
	logPrefix2        = "mirrorish"
)

var pinnedRootHash = sha256.Sum256([]byte("harbor-ct-bench-pinned-root"))

// ---------------------------------------------------------------------------
// The pinned backend.
// ---------------------------------------------------------------------------

type stubBackend struct {
	mu sync.Mutex

	leafDER []byte

	queueCalls int
	lastQueued []byte
	sthCalls   int
}

func (s *stubBackend) signedRoot() *trillian.SignedLogRoot {
	return &trillian.SignedLogRoot{
		TimestampNanos: pinnedTimestampMS * 1000000,
		RootHash:       pinnedRootHash[:],
		TreeSize:       pinnedTreeSize,
		TreeRevision:   1,
	}
}

func (s *stubBackend) counts() (queue, sth int, lastLeaf []byte) {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.queueCalls, s.sthCalls, append([]byte(nil), s.lastQueued...)
}

// pinnedLeafValue is a well-formed RFC 6962 MerkleTreeLeaf for index i. Its
// bytes depend only on i and on a pinned fixture, so a driver can predict them.
func pinnedLeafValue(der []byte, i int64) []byte {
	mtl := ct.MerkleTreeLeaf{
		Version:  ct.V1,
		LeafType: ct.TimestampedEntryLeafType,
		TimestampedEntry: &ct.TimestampedEntry{
			Timestamp: uint64(pinnedTimestampMS + i),
			EntryType: ct.X509LogEntryType,
			X509Entry: &ct.ASN1Cert{Data: der},
		},
	}
	b, err := tls.Marshal(mtl)
	if err != nil {
		panic(err)
	}
	return b
}

func (s *stubBackend) pinnedLeaf(i int64) *trillian.LogLeaf {
	return &trillian.LogLeaf{
		LeafIndex: i,
		LeafValue: pinnedLeafValue(s.leafDER, i),
		ExtraData: []byte{0x00, 0x00, 0x00},
	}
}

func (s *stubBackend) QueueLeaf(_ context.Context, req *trillian.QueueLeafRequest) (*trillian.QueueLeafResponse, error) {
	s.mu.Lock()
	s.queueCalls++
	s.lastQueued = append([]byte(nil), req.GetLeaf().GetLeafValue()...)
	s.mu.Unlock()
	return &trillian.QueueLeafResponse{QueuedLeaf: &trillian.QueuedLogLeaf{Leaf: req.GetLeaf()}}, nil
}

func (s *stubBackend) QueueLeaves(_ context.Context, req *trillian.QueueLeavesRequest) (*trillian.QueueLeavesResponse, error) {
	out := make([]*trillian.QueuedLogLeaf, 0, len(req.GetLeaves()))
	s.mu.Lock()
	for _, l := range req.GetLeaves() {
		s.queueCalls++
		s.lastQueued = append([]byte(nil), l.GetLeafValue()...)
		out = append(out, &trillian.QueuedLogLeaf{Leaf: l})
	}
	s.mu.Unlock()
	return &trillian.QueueLeavesResponse{QueuedLeaves: out}, nil
}

func (s *stubBackend) GetLatestSignedLogRoot(_ context.Context, _ *trillian.GetLatestSignedLogRootRequest) (*trillian.GetLatestSignedLogRootResponse, error) {
	s.mu.Lock()
	s.sthCalls++
	s.mu.Unlock()
	return &trillian.GetLatestSignedLogRootResponse{SignedLogRoot: s.signedRoot()}, nil
}

func (s *stubBackend) GetLeavesByRange(_ context.Context, req *trillian.GetLeavesByRangeRequest) (*trillian.GetLeavesByRangeResponse, error) {
	n := req.GetCount()
	if n < 0 {
		n = 0
	}
	if n > 4096 {
		n = 4096
	}
	leaves := make([]*trillian.LogLeaf, 0, n)
	for i := int64(0); i < n; i++ {
		leaves = append(leaves, s.pinnedLeaf(req.GetStartIndex()+i))
	}
	return &trillian.GetLeavesByRangeResponse{Leaves: leaves, SignedLogRoot: s.signedRoot()}, nil
}

func (s *stubBackend) GetLeavesByIndex(_ context.Context, req *trillian.GetLeavesByIndexRequest) (*trillian.GetLeavesByIndexResponse, error) {
	leaves := make([]*trillian.LogLeaf, 0, len(req.GetLeafIndex()))
	for _, i := range req.GetLeafIndex() {
		leaves = append(leaves, s.pinnedLeaf(i))
	}
	return &trillian.GetLeavesByIndexResponse{Leaves: leaves, SignedLogRoot: s.signedRoot()}, nil
}

func (s *stubBackend) GetLeavesByHash(_ context.Context, _ *trillian.GetLeavesByHashRequest) (*trillian.GetLeavesByHashResponse, error) {
	return &trillian.GetLeavesByHashResponse{SignedLogRoot: s.signedRoot()}, nil
}

func (s *stubBackend) GetInclusionProof(_ context.Context, req *trillian.GetInclusionProofRequest) (*trillian.GetInclusionProofResponse, error) {
	return &trillian.GetInclusionProofResponse{
		Proof:         &trillian.Proof{LeafIndex: req.GetLeafIndex(), Hashes: [][]byte{pinnedRootHash[:]}},
		SignedLogRoot: s.signedRoot(),
	}, nil
}

func (s *stubBackend) GetInclusionProofByHash(_ context.Context, _ *trillian.GetInclusionProofByHashRequest) (*trillian.GetInclusionProofByHashResponse, error) {
	return &trillian.GetInclusionProofByHashResponse{
		Proof:         []*trillian.Proof{{LeafIndex: 7, Hashes: [][]byte{pinnedRootHash[:]}}},
		SignedLogRoot: s.signedRoot(),
	}, nil
}

func (s *stubBackend) GetConsistencyProof(_ context.Context, _ *trillian.GetConsistencyProofRequest) (*trillian.GetConsistencyProofResponse, error) {
	return &trillian.GetConsistencyProofResponse{
		Proof:         &trillian.Proof{Hashes: [][]byte{pinnedRootHash[:]}},
		SignedLogRoot: s.signedRoot(),
	}, nil
}

func (s *stubBackend) GetEntryAndProof(_ context.Context, req *trillian.GetEntryAndProofRequest) (*trillian.GetEntryAndProofResponse, error) {
	return &trillian.GetEntryAndProofResponse{
		Proof:         &trillian.Proof{LeafIndex: req.GetLeafIndex(), Hashes: [][]byte{pinnedRootHash[:]}},
		Leaf:          s.pinnedLeaf(req.GetLeafIndex()),
		SignedLogRoot: s.signedRoot(),
	}, nil
}

func (s *stubBackend) GetSequencedLeafCount(_ context.Context, _ *trillian.GetSequencedLeafCountRequest) (*trillian.GetSequencedLeafCountResponse, error) {
	return &trillian.GetSequencedLeafCountResponse{LeafCount: pinnedTreeSize}, nil
}

func (s *stubBackend) InitLog(_ context.Context, _ *trillian.InitLogRequest) (*trillian.InitLogResponse, error) {
	return &trillian.InitLogResponse{Created: s.signedRoot()}, nil
}

func (s *stubBackend) AddSequencedLeaf(_ context.Context, req *trillian.AddSequencedLeafRequest) (*trillian.AddSequencedLeafResponse, error) {
	return &trillian.AddSequencedLeafResponse{Result: &trillian.QueuedLogLeaf{Leaf: req.GetLeaf()}}, nil
}

func (s *stubBackend) AddSequencedLeaves(_ context.Context, _ *trillian.AddSequencedLeavesRequest) (*trillian.AddSequencedLeavesResponse, error) {
	return &trillian.AddSequencedLeavesResponse{}, nil
}

// ---------------------------------------------------------------------------
// Bringing up the personality.
// ---------------------------------------------------------------------------

type harness struct {
	backend    *stubBackend
	grpcSrv    *grpc.Server
	cmd        *exec.Cmd
	exited     chan struct{}
	baseURL    string
	metricsURL string
	cfgPath    string
	stderrTo   string
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

// startBackend brings up only the pinned backend -- no ct_server at all -- on
// a real loopback gRPC socket, and returns a TrillianLogClient speaking to it.
func startBackend(t *vhelp.T) (*stubBackend, *grpc.Server, trillian.TrillianLogClient) {
	be := &stubBackend{leafDER: vhelp.ReadFixture("minted", "leaf-valid.der")}

	lis, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("listen for backend: %v", err)
	}
	gs := grpc.NewServer()
	trillian.RegisterTrillianLogServer(gs, be)
	go func() { _ = gs.Serve(lis) }()

	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()
	conn, err := grpc.DialContext(ctx, lis.Addr().String(), grpc.WithInsecure(), grpc.WithBlock())
	if err != nil {
		gs.Stop()
		t.Fatalf("dial pinned backend: %v", err)
	}
	return be, gs, trillian.NewTrillianLogClient(conn)
}

// pinnedKeyDER returns the DER of the pinned CTFE signing key and its SPKI.
func pinnedKeyDER(t *vhelp.T) (priv, pub []byte) {
	return pemBody(t, vhelp.ReadFixture("minted", "keys", "log-key.pem")),
		pemBody(t, vhelp.ReadFixture("minted", "keys", "log-pubkey.pem"))
}

// logConfigText renders one `config { ... }` stanza. Every value in it is a
// pinned fixture or a compile-time constant; the key material is inlined only
// so no absolute path is baked into the corpus.
func logConfigText(t *vhelp.T, logID int64, prefix string, mirror bool, extra string) string {
	keyDER, pubDER := pinnedKeyDER(t)
	rootsPath := vhelp.Fixture("minted", "roots-bench-plus-int.pem")

	var b strings.Builder
	b.WriteString("config {\n")
	fmt.Fprintf(&b, "  log_id: %d\n", logID)
	fmt.Fprintf(&b, "  prefix: %q\n", prefix)
	if !mirror {
		fmt.Fprintf(&b, "  roots_pem_file: %q\n", rootsPath)
		b.WriteString("  private_key: {\n    [type.googleapis.com/keyspb.PrivateKey] {\n      der: \"" + protoEscape(keyDER) + "\"\n    }\n  }\n")
	}
	b.WriteString("  public_key: { der: \"" + protoEscape(pubDER) + "\" }\n")
	b.WriteString(extra)
	b.WriteString("}\n")
	return b.String()
}

// writeConfigFile drops a complete LogConfigSet into this group's private
// scratch directory under the given name.
func writeConfigFile(t *vhelp.T, name, body string) string {
	p := joinPath(os.Getenv("CT_SCRATCH"), name)
	if err := os.WriteFile(p, []byte(body), 0600); err != nil {
		t.Fatalf("write config %s: %v", name, err)
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

func joinPath(a, b string) string {
	if strings.HasSuffix(a, "/") {
		return a + b
	}
	return a + "/" + b
}

// startPersonality boots the candidate's own ct_server against the pinned
// backend and waits until it answers 200 on "/". At every anchor in this range
// ct_server registers that root handler only after every configured log has
// been set up, so a 200 there means the personality is fully wired. A failure
// aborts the group rather than letting it report a vacuous pass.
func startPersonality(t *vhelp.T, extraArgs []string, configBody string) *harness {
	return startPersonalityNamed(t, "ctfe", extraArgs, configBody)
}

// startPersonalityNamed is startPersonality with a caller-chosen name for the
// files it drops in the scratch directory, so one group may run more than one
// personality in turn without the two overwriting each other's evidence.
func startPersonalityNamed(t *vhelp.T, name string, extraArgs []string, configBody string) *harness {
	leafDER := vhelp.ReadFixture("minted", "leaf-valid.der")
	be := &stubBackend{leafDER: leafDER}

	lis, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("listen for backend: %v", err)
	}
	gs := grpc.NewServer()
	trillian.RegisterTrillianLogServer(gs, be)
	go func() { _ = gs.Serve(lis) }()

	cfg := writeConfigFile(t, name+".textproto", configBody)

	addr := "127.0.0.1:" + strconv.Itoa(freeLoopbackPort(t))
	metricsAddr := "127.0.0.1:" + strconv.Itoa(freeLoopbackPort(t))
	args := []string{
		"--log_config=" + cfg,
		"--log_rpc_server=" + lis.Addr().String(),
		"--http_endpoint=" + addr,
		"--metrics_endpoint=" + metricsAddr,
		"--get_sth_interval=0",
		"--rpc_deadline=20s",
		"--logtostderr",
	}
	args = append(args, extraArgs...)

	errPath := joinPath(os.Getenv("CT_SCRATCH"), name+".stderr")
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
		gs.Stop()
		t.Fatalf("start ct_server: %v", err)
	}

	// One Wait for the lifetime of the process, so the readiness loop below
	// notices a personality that refused its own command line immediately
	// instead of waiting out the deadline.
	exited := make(chan struct{})
	go func() { _ = cmd.Wait(); close(exited) }()

	h := &harness{backend: be, grpcSrv: gs, cmd: cmd, exited: exited,
		baseURL: "http://" + addr, metricsURL: "http://" + metricsAddr,
		cfgPath: cfg, stderrTo: errPath}
	// Done calls os.Exit, which does not run deferred functions, so a
	// `defer h.stop()` in a driver never fires and the child survives the
	// group. Register the teardown with the harness instead.
	t.Cleanup(h.stop)

	client := &http.Client{Timeout: 5 * time.Second}
	deadline := time.Now().Add(120 * time.Second)
	for {
		if time.Now().After(deadline) {
			tail, _ := os.ReadFile(errPath)
			h.stop()
			t.Fatalf("ct_server never answered on /; stderr tail: %s", lastBytes(tail, 1500))
		}
		resp, err := client.Get(h.baseURL + "/")
		if err == nil {
			_ = resp.Body.Close()
			if resp.StatusCode == http.StatusOK {
				return h
			}
		}
		select {
		case <-exited:
			tail, _ := os.ReadFile(errPath)
			h.stop()
			t.Fatalf("ct_server exited early: %v; stderr tail: %s", cmd.ProcessState, lastBytes(tail, 1500))
		default:
		}
		time.Sleep(100 * time.Millisecond)
	}
}

func lastBytes(b []byte, n int) string {
	if len(b) > n {
		b = b[len(b)-n:]
	}
	return strings.ReplaceAll(string(b), "\n", " | ")
}

// shutdownBySignal sends one signal to the personality -- to the exact PID
// this harness started, and to nothing else -- waits for it to end, and
// reports how it ended: "exit=N" for a normal return, "killed-by-<sig>" if
// the process died of the signal instead of handling it. A personality that
// is still running after the grace period reports "still-serving" rather
// than hanging the group.
//
// This ends the personality, so a group calls it last; the deferred stop()
// afterwards is then a no-op on a PID that has already been reaped.
func (h *harness) shutdownBySignal(t *vhelp.T, sig os.Signal) string {
	if h.cmd == nil || h.cmd.Process == nil {
		t.Fatalf("no ct_server process to signal")
	}
	if err := h.cmd.Process.Signal(sig); err != nil {
		t.Fatalf("send %v to ct_server: %v", sig, err)
	}
	select {
	case <-h.exited:
	case <-time.After(90 * time.Second):
		return "still-serving"
	}
	ps := h.cmd.ProcessState
	if ps == nil {
		return "no-exit-status"
	}
	if ws, ok := ps.Sys().(syscall.WaitStatus); ok && ws.Signaled() {
		return "killed-by-" + ws.Signal().String()
	}
	return "exit=" + strconv.Itoa(ps.ExitCode())
}

// listenerClosed reports whether the personality's HTTP endpoint has stopped
// accepting connections. Paired with shutdownBySignal it separates a server
// that really let go of its listener from one that merely happened to exit.
func (h *harness) listenerClosed() bool {
	client := &http.Client{Timeout: 5 * time.Second}
	resp, err := client.Get(h.baseURL + "/")
	if err != nil {
		return true
	}
	_ = resp.Body.Close()
	return false
}

// stop terminates only the process this group started, by the exact PID it
// captured itself.
func (h *harness) stop() {
	if h.cmd != nil && h.cmd.Process != nil {
		_ = h.cmd.Process.Kill()
		if h.exited != nil {
			select {
			case <-h.exited:
			case <-time.After(30 * time.Second):
			}
		}
	}
	if h.grpcSrv != nil {
		h.grpcSrv.Stop()
	}
}

type httpResult struct {
	status int
	body   []byte
	header http.Header
}

var httpClient = &http.Client{Timeout: 30 * time.Second}

func readAll(r *http.Response) []byte {
	b := make([]byte, 0, 4096)
	buf := make([]byte, 32*1024)
	for {
		n, err := r.Body.Read(buf)
		b = append(b, buf[:n]...)
		if err != nil {
			break
		}
	}
	return b
}

func (h *harness) do(t *vhelp.T, req *http.Request) httpResult {
	resp, err := httpClient.Do(req)
	if err != nil {
		t.Fatalf("%s %s: %v", req.Method, req.URL, err)
	}
	defer func() { _ = resp.Body.Close() }()
	return httpResult{status: resp.StatusCode, body: readAll(resp), header: resp.Header}
}

func (h *harness) get(t *vhelp.T, url string) httpResult {
	req, err := http.NewRequest(http.MethodGet, url, nil)
	if err != nil {
		t.Fatalf("build GET %s: %v", url, err)
	}
	return h.do(t, req)
}

func (h *harness) postJSON(t *vhelp.T, url string, payload []byte) httpResult {
	req, err := http.NewRequest(http.MethodPost, url, strings.NewReader(string(payload)))
	if err != nil {
		t.Fatalf("build POST %s: %v", url, err)
	}
	req.Header.Set("Content-Type", "application/json")
	return h.do(t, req)
}
