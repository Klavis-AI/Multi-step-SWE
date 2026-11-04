// serverkit.go is compiled into exactly one group's single compilation unit.
// It holds no cases and prints nothing; it is not a group of its own.
//
// SCOPE-BACKEND: google/trillian is not a deliverable, so the kit runs a
// pinned in-process TrillianLogServer over a real loopback gRPC socket and
// records what the CTFE asked it for. SCOPE-NET: loopback only. The gRPC
// backend binds 127.0.0.1:0 and serves on the listener the kernel handed back;
// the HTTP endpoint has to be named on ct_server's command line before the
// process that binds it exists, so freeLoopbackPort probes for a free port in
// 20000-31999, below the kernel's ephemeral range, dispersed by one throwaway
// :0 bind. No port number is hard-coded and none is assumed free.
// SCOPE-TIME: every root, size and timestamp below is a constant.
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
	"reflect"
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/google/trillian"
	"github.com/google/trillian/types"
	"google.golang.org/grpc"

	"ctverify/vhelp"
)

const (
	pinnedTreeSize    = uint64(100000)
	pinnedTimestampMS = uint64(1500000000000)
	pinnedLogID       = int64(1001)
	logPrefix         = "bench"
)

var pinnedRootHash = sha256.Sum256([]byte("harbor-ct-bench-pinned-root"))

// ---------------------------------------------------------------------------
// The pinned backend. v1.1.0's CTFE speaks QueueLeaves (plural).
// ---------------------------------------------------------------------------

type stubBackend struct {
	mu         sync.Mutex
	queueCalls int
	sthCalls   int
	lastQueued []byte

	// stored is the log's dedup index, keyed the way HTTP-API.md §3.2 says a
	// backend keys it: by the leaf's identity hash, which the CTFE derives
	// from the certificate and which therefore does not move when the CTFE
	// rebuilds the leaf with a fresh timestamp.
	stored map[string][]byte
}

// storedLeaf returns the leaf value the backend is holding for an identity
// hash, if any.
func (s *stubBackend) storedLeaf(idHash []byte) []byte {
	s.mu.Lock()
	defer s.mu.Unlock()
	return append([]byte(nil), s.stored[string(idHash)]...)
}

func (s *stubBackend) counts() (queue, sth int) {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.queueCalls, s.sthCalls
}

func (s *stubBackend) lastLeafValue() []byte {
	s.mu.Lock()
	defer s.mu.Unlock()
	return append([]byte(nil), s.lastQueued...)
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
	slr := &trillian.SignedLogRoot{LogRoot: b}
	setLegacyRootFields(slr, r)
	return slr
}

// setLegacyRootFields fills the scalar tree-head fields of SignedLogRoot when
// the pinned trillian release still declares them.
//
// This is the second place the two anchors disagree about their shared
// dependency. At the trillian release v1.0.21 pins, SignedLogRoot carries
// timestamp_nanos / root_hash / tree_size as fields of their own and the CTFE
// reads them directly; at the one v1.1.0 pins those fields are gone and the
// CTFE unmarshals log_root into a types.LogRootV1 instead. Naming TreeSize in
// Go source would not compile at v1.1.0, and omitting it makes the v1.0.21
// CTFE see a tree of size zero -- which is what a gate driver must not do,
// because it would fail for a reason that is not a regression.
//
// Both spellings are therefore filled from the same LogRootV1, the legacy one
// through reflection so that it is simply skipped where the field is absent.
// Nothing about this is timing- or order-dependent: it is a field lookup on a
// generated struct.
func setLegacyRootFields(slr *trillian.SignedLogRoot, r types.LogRootV1) {
	v := reflect.ValueOf(slr).Elem()
	if f := v.FieldByName("TreeSize"); f.IsValid() && f.Kind() == reflect.Int64 {
		f.SetInt(int64(r.TreeSize))
	}
	if f := v.FieldByName("TimestampNanos"); f.IsValid() && f.Kind() == reflect.Int64 {
		f.SetInt(int64(r.TimestampNanos))
	}
	if f := v.FieldByName("RootHash"); f.IsValid() && f.Kind() == reflect.Slice {
		f.SetBytes(append([]byte(nil), r.RootHash...))
	}
}

// QueueLeaves deduplicates by leaf identity hash and hands back the leaf it
// already holds, which is what a real Trillian log does and what HTTP-API.md
// §3.2's duplicate-submission rule is stated against.
func (s *stubBackend) QueueLeaves(_ context.Context, req *trillian.QueueLeavesRequest) (*trillian.QueueLeavesResponse, error) {
	in := req.GetLeaves()[0]
	s.mu.Lock()
	s.queueCalls++
	s.lastQueued = append([]byte(nil), in.GetLeafValue()...)
	if s.stored == nil {
		s.stored = map[string][]byte{}
	}
	key := string(in.GetLeafIdentityHash())
	out := in
	if prev, ok := s.stored[key]; ok && len(key) > 0 {
		clone := *in
		clone.LeafValue = append([]byte(nil), prev...)
		out = &clone
	} else {
		s.stored[key] = append([]byte(nil), in.GetLeafValue()...)
	}
	s.mu.Unlock()
	return &trillian.QueueLeavesResponse{
		QueuedLeaves: []*trillian.QueuedLogLeaf{{Leaf: out}},
	}, nil
}

func (s *stubBackend) GetLatestSignedLogRoot(_ context.Context, _ *trillian.GetLatestSignedLogRootRequest) (*trillian.GetLatestSignedLogRootResponse, error) {
	s.mu.Lock()
	s.sthCalls++
	s.mu.Unlock()
	return &trillian.GetLatestSignedLogRootResponse{SignedLogRoot: s.signedRoot()}, nil
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

// ---------------------------------------------------------------------------
// The rest of the TrillianLogServer interface.
//
// The generated `trillian.UnimplementedTrillianLogServer` embed cannot be used
// here. It arrives with the trillian release v1.1.0 pins and is absent from the
// one v1.0.21 pins, and this driver has to compile against BOTH anchors -- a
// gate group runs against the candidate's tree and against the previous
// milestone's. The interface itself is identical across the two pins, so the
// methods the kit does not need are written out by hand instead. The compile
// -time assertion below is what keeps that list honest.
// ---------------------------------------------------------------------------

var _ trillian.TrillianLogServer = (*stubBackend)(nil)

func (s *stubBackend) QueueLeaf(context.Context, *trillian.QueueLeafRequest) (*trillian.QueueLeafResponse, error) {
	return nil, fmt.Errorf("QueueLeaf: this pinned backend does not implement it")
}

func (s *stubBackend) AddSequencedLeaf(context.Context, *trillian.AddSequencedLeafRequest) (*trillian.AddSequencedLeafResponse, error) {
	return nil, fmt.Errorf("AddSequencedLeaf: this pinned backend does not implement it")
}

func (s *stubBackend) GetInclusionProof(context.Context, *trillian.GetInclusionProofRequest) (*trillian.GetInclusionProofResponse, error) {
	return nil, fmt.Errorf("GetInclusionProof: this pinned backend does not implement it")
}

func (s *stubBackend) GetSequencedLeafCount(context.Context, *trillian.GetSequencedLeafCountRequest) (*trillian.GetSequencedLeafCountResponse, error) {
	return nil, fmt.Errorf("GetSequencedLeafCount: this pinned backend does not implement it")
}

func (s *stubBackend) GetEntryAndProof(context.Context, *trillian.GetEntryAndProofRequest) (*trillian.GetEntryAndProofResponse, error) {
	return nil, fmt.Errorf("GetEntryAndProof: this pinned backend does not implement it")
}

func (s *stubBackend) InitLog(context.Context, *trillian.InitLogRequest) (*trillian.InitLogResponse, error) {
	return nil, fmt.Errorf("InitLog: this pinned backend does not implement it")
}

func (s *stubBackend) AddSequencedLeaves(context.Context, *trillian.AddSequencedLeavesRequest) (*trillian.AddSequencedLeavesResponse, error) {
	return nil, fmt.Errorf("AddSequencedLeaves: this pinned backend does not implement it")
}

func (s *stubBackend) GetLeavesByIndex(context.Context, *trillian.GetLeavesByIndexRequest) (*trillian.GetLeavesByIndexResponse, error) {
	return nil, fmt.Errorf("GetLeavesByIndex: this pinned backend does not implement it")
}

func (s *stubBackend) GetLeavesByRange(context.Context, *trillian.GetLeavesByRangeRequest) (*trillian.GetLeavesByRangeResponse, error) {
	return nil, fmt.Errorf("GetLeavesByRange: this pinned backend does not implement it")
}

func (s *stubBackend) GetLeavesByHash(context.Context, *trillian.GetLeavesByHashRequest) (*trillian.GetLeavesByHashResponse, error) {
	return nil, fmt.Errorf("GetLeavesByHash: this pinned backend does not implement it")
}

// ---------------------------------------------------------------------------
// Bringing up the candidate's own personality binary.
// ---------------------------------------------------------------------------

type harness struct {
	backend *stubBackend
	grpcSrv *grpc.Server
	cmd     *exec.Cmd
	baseURL string
	errPath string
}

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

func scratch(name string) string {
	d := os.Getenv("CT_SCRATCH")
	if !strings.HasSuffix(d, "/") {
		d += "/"
	}
	return d + name
}

// pemBody returns the DER inside the first PEM block of b.
func pemBody(t *vhelp.T, b []byte) []byte {
	blk, _ := pem.Decode(b)
	if blk == nil {
		t.Fatalf("fixture is not PEM")
	}
	return blk.Bytes
}

func protoEscape(b []byte) string {
	var s strings.Builder
	for _, c := range b {
		fmt.Fprintf(&s, "\\x%02x", c)
	}
	return s.String()
}

// writeMultiConfig synthesises this group's own LogMultiConfig. Only the
// backend address varies between runs, because the kernel chooses the port.
func writeMultiConfig(t *vhelp.T, name, backendAddr, extraLogConfig string) string {
	keyDER := pemBody(t, vhelp.ReadFixture("minted", "keys", "log-key.pem"))
	pubDER := pemBody(t, vhelp.ReadFixture("minted", "keys", "log-pubkey.pem"))

	var b strings.Builder
	b.WriteString("backends {\n  backend {\n    name: \"default\"\n    backend_spec: \"" + backendAddr + "\"\n  }\n}\n")
	b.WriteString("log_configs {\n  config {\n")
	fmt.Fprintf(&b, "    log_id: %d\n", pinnedLogID)
	fmt.Fprintf(&b, "    prefix: %q\n", logPrefix)
	fmt.Fprintf(&b, "    roots_pem_file: %q\n", vhelp.Fixture("minted", "roots-bench-plus-int.pem"))
	b.WriteString("    private_key: {\n      [type.googleapis.com/keyspb.PrivateKey] {\n        der: \"" + protoEscape(keyDER) + "\"\n      }\n    }\n")
	b.WriteString("    public_key: { der: \"" + protoEscape(pubDER) + "\" }\n")
	b.WriteString("    log_backend_name: \"default\"\n")
	b.WriteString("    max_merge_delay_sec: 86400\n")
	b.WriteString("    expected_merge_delay_sec: 120\n")
	b.WriteString(extraLogConfig)
	b.WriteString("  }\n}\n")

	p := scratch(name)
	if err := os.WriteFile(p, []byte(b.String()), 0600); err != nil {
		t.Fatalf("write config: %v", err)
	}
	return p
}

// startPersonality boots the candidate's own ct_server against the pinned
// backend and waits for it to answer. A failure aborts the group rather than
// letting it report a vacuous pass.
func startPersonality(t *vhelp.T, tag string, extraArgs []string, extraLogConfig string) *harness {
	be := &stubBackend{}
	lis, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("listen for backend: %v", err)
	}
	gs := grpc.NewServer()
	trillian.RegisterTrillianLogServer(gs, be)
	go func() { _ = gs.Serve(lis) }()

	cfg := writeMultiConfig(t, tag+".textproto", lis.Addr().String(), extraLogConfig)
	addr := "127.0.0.1:" + strconv.Itoa(freeLoopbackPort(t))
	// Two flags are passed explicitly because their DEFAULTS differ between
	// this milestone and the previous one, and a gate driver has to behave
	// identically on both trees (CLI.md §2.1 -- the two withdrawn defaults):
	//
	//   --log_rpc_server  at v1.0.21 defaults to "localhost:8090", which sends
	//                     ct_server down the single-backend LogConfigSet
	//                     branch and makes it reject the LogMultiConfig this
	//                     kit writes. Empty selects the multi-config branch on
	//                     both trees.
	//   --metrics_endpoint at v1.0.21 defaults to "localhost:6963", a FIXED
	//                     port: two runs on this host, or another author's
	//                     server, would collide on it. Pointing it at the
	//                     kernel-chosen HTTP endpoint keeps every port in this
	//                     group bound from :0.
	//
	// Neither default is graded here; both are graded by the scored group
	// ctserver-flags-and-healthz, which passes neither flag.
	args := append([]string{
		"--log_config=" + cfg,
		"--http_endpoint=" + addr,
		"--log_rpc_server=",
		"--metrics_endpoint=" + addr,
		"--get_sth_interval=0",
		"--rpc_deadline=20s",
		"--logtostderr",
	}, extraArgs...)

	errPath := scratch(tag + ".stderr")
	errFile, err := os.Create(errPath)
	if err != nil {
		t.Fatalf("create stderr sink: %v", err)
	}
	// vhelp.Command strips CT_VERIFY_NONCE from the child environment.
	cmd := vhelp.Command(vhelp.CandidateBin("ct_server"), args...)
	// The candidate's own output must never reach this group's stdout.
	cmd.Stdout = errFile
	cmd.Stderr = errFile
	if err := cmd.Start(); err != nil {
		gs.Stop()
		t.Fatalf("start ct_server: %v", err)
	}
	h := &harness{backend: be, grpcSrv: gs, cmd: cmd, baseURL: "http://" + addr, errPath: errPath}
	// Done calls os.Exit, which does not run deferred functions, so a
	// `defer h.stop()` in a driver never fires and the child survives the
	// group. Register the teardown with the harness instead.
	t.Cleanup(h.stop)

	deadline := time.Now().Add(90 * time.Second)
	healthzWindow := time.Now().Add(15 * time.Second)
	for {
		if time.Now().After(deadline) {
			tail, _ := os.ReadFile(errPath)
			h.stop()
			t.Fatalf("ct_server never answered; stderr tail: %s", lastBytes(tail, 1500))
		}
		// Readiness must not depend on a graded endpoint. /healthz is new at
		// this milestone, and TEST_INTERFACE.md §7 promises that one broken
		// area never cancels a working one -- so /healthz is only the fast
		// path. After a short window the gate also accepts a 200 from GET /,
		// which has been a bare 200 since v1.0.13 (HTTP-API.md §7). A group
		// that grades neither is then unaffected by either being wrong.
		for _, p := range readyPaths(healthzWindow) {
			resp, err := http.Get(h.baseURL + p)
			if err != nil {
				continue
			}
			_ = resp.Body.Close()
			if resp.StatusCode == http.StatusOK {
				return h
			}
		}
		if cmd.ProcessState != nil && cmd.ProcessState.Exited() {
			tail, _ := os.ReadFile(errPath)
			h.stop()
			t.Fatalf("ct_server exited early; stderr tail: %s", lastBytes(tail, 1500))
		}
		time.Sleep(100 * time.Millisecond)
	}
}

// readyPaths is the readiness gate's path list: /healthz alone while the
// window is open, then /healthz or / once it has closed.
func readyPaths(healthzOnlyUntil time.Time) []string {
	if time.Now().Before(healthzOnlyUntil) {
		return []string{"/healthz"}
	}
	return []string{"/healthz", "/"}
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
		_, _ = h.cmd.Process.Wait()
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

func (h *harness) get(t *vhelp.T, url string) httpResult {
	resp, err := http.Get(url)
	if err != nil {
		t.Fatalf("GET %s: %v", url, err)
	}
	defer func() { _ = resp.Body.Close() }()
	return httpResult{status: resp.StatusCode, body: readAll(resp.Body), header: resp.Header}
}

func (h *harness) postJSON(t *vhelp.T, url string, payload string) httpResult {
	resp, err := http.Post(url, "application/json", strings.NewReader(payload))
	if err != nil {
		t.Fatalf("POST %s: %v", url, err)
	}
	defer func() { _ = resp.Body.Close() }()
	return httpResult{status: resp.StatusCode, body: readAll(resp.Body), header: resp.Header}
}
