// serverkit.go is copied verbatim into every group that drives the candidate's
// own ct_server binary. It is not a group: it holds no cases and prints
// nothing. Each copy is compiled into exactly one group's single compilation
// unit, so a group still owns one process tree and one binary.
//
// SCOPE-BACKEND: google/trillian is not a deliverable, so the kit runs its own
// pinned in-process TrillianLogServer over a real loopback gRPC socket and
// records what the CTFE asked it for. The CTFE signs SCTs and STHs with its
// own configured key and never checks a signature on the backend's log root,
// so an unsigned pinned root is enough to reach an issued, verifiable SCT.
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

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/tls"
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
	logPrefix         = "bench"
)

var pinnedRootHash = sha256.Sum256([]byte("harbor-ct-bench-pinned-root"))

// ---------------------------------------------------------------------------
// The pinned backend.
// ---------------------------------------------------------------------------

type stubBackend struct {
	mu sync.Mutex

	leafDER []byte

	rangeCalls int
	lastStart  int64
	lastCount  int64

	queueCalls int
	lastQueued []byte
}

func (s *stubBackend) snapshot() (calls int, start, count int64) {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.rangeCalls, s.lastStart, s.lastCount
}

func (s *stubBackend) queueSnapshot() (calls int, leafValue []byte) {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.queueCalls, append([]byte(nil), s.lastQueued...)
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

// pinnedLeaf is a well-formed RFC 6962 MerkleTreeLeaf for index i. Its bytes
// depend only on i and on a pinned fixture, so a driver can predict them.
func (s *stubBackend) pinnedLeaf(i int64) *trillian.LogLeaf {
	return &trillian.LogLeaf{
		LeafIndex: i,
		LeafValue: pinnedLeafValue(s.leafDER, i),
		ExtraData: []byte{0x00, 0x00, 0x00},
	}
}

func pinnedLeafValue(der []byte, i int64) []byte {
	mtl := ct.MerkleTreeLeaf{
		Version:  ct.V1,
		LeafType: ct.TimestampedEntryLeafType,
		TimestampedEntry: &ct.TimestampedEntry{
			Timestamp: pinnedTimestampMS + uint64(i),
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

func (s *stubBackend) QueueLeaf(_ context.Context, req *trillian.QueueLeafRequest) (*trillian.QueueLeafResponse, error) {
	s.mu.Lock()
	s.queueCalls++
	s.lastQueued = append([]byte(nil), req.GetLeaf().GetLeafValue()...)
	s.mu.Unlock()
	return &trillian.QueueLeafResponse{QueuedLeaf: &trillian.QueuedLogLeaf{Leaf: req.GetLeaf()}}, nil
}

func (s *stubBackend) GetLeavesByRange(_ context.Context, req *trillian.GetLeavesByRangeRequest) (*trillian.GetLeavesByRangeResponse, error) {
	s.mu.Lock()
	s.rangeCalls++
	s.lastStart = req.GetStartIndex()
	s.lastCount = req.GetCount()
	s.mu.Unlock()

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

func (s *stubBackend) GetLatestSignedLogRoot(_ context.Context, _ *trillian.GetLatestSignedLogRootRequest) (*trillian.GetLatestSignedLogRootResponse, error) {
	return &trillian.GetLatestSignedLogRootResponse{SignedLogRoot: s.signedRoot()}, nil
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
	logPath  string
	stderrTo string
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

// writeMultiConfig writes this group's own pinned CTFE configuration into the
// group's private scratch directory. It is synthesised rather than read from a
// file so that the backend address, which is only known once the kernel has
// chosen a port, is the only thing that varies between runs. Keys and roots
// are pinned fixtures (SCOPE-TIME).
func writeMultiConfig(t *vhelp.T, backendAddr string, extraLogConfig string) string {
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
	b.WriteString(extraLogConfig)
	b.WriteString("  }\n}\n")

	p := filepathJoin(os.Getenv("CT_SCRATCH"), "ctfe.textproto")
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

// protoEscape renders bytes as the \\xNN escapes prototext accepts.
func protoEscape(b []byte) string {
	var s strings.Builder
	for _, c := range b {
		fmt.Fprintf(&s, "\\x%02x", c)
	}
	return s.String()
}

func filepathJoin(a, b string) string {
	if strings.HasSuffix(a, "/") {
		return a + b
	}
	return a + "/" + b
}

// startPersonality boots the candidate's own ct_server against the pinned
// backend and waits for /healthz. A failure here aborts the group rather than
// letting it report a vacuous pass.
func startPersonality(t *vhelp.T, extraArgs []string, extraLogConfig string) *harness {
	leafDER := vhelp.ReadFixture("minted", "leaf-valid.der")
	be := &stubBackend{leafDER: leafDER}

	lis, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("listen for backend: %v", err)
	}
	gs := grpc.NewServer()
	trillian.RegisterTrillianLogServer(gs, be)
	go func() { _ = gs.Serve(lis) }()

	cfg := writeMultiConfig(t, lis.Addr().String(), extraLogConfig)

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

	errPath := filepathJoin(os.Getenv("CT_SCRATCH"), "ct_server.stderr")
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

	h := &harness{backend: be, grpcSrv: gs, cmd: cmd, baseURL: "http://" + addr, logPath: cfg, stderrTo: errPath}
	// Done calls os.Exit, which does not run deferred functions, so a
	// `defer h.stop()` in a driver never fires and the child survives the
	// group. Register the teardown with the harness instead.
	t.Cleanup(h.stop)

	deadline := time.Now().Add(60 * time.Second)
	for {
		if time.Now().After(deadline) {
			tail, _ := os.ReadFile(errPath)
			h.stop()
			t.Fatalf("ct_server never became healthy; stderr tail: %s", lastBytes(tail, 1500))
		}
		resp, err := http.Get(h.baseURL + "/healthz")
		if err == nil {
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

func (h *harness) get(t *vhelp.T, path string) httpResult {
	resp, err := http.Get(path)
	if err != nil {
		t.Fatalf("GET %s: %v", path, err)
	}
	defer func() { _ = resp.Body.Close() }()
	b := make([]byte, 0, 4096)
	buf := make([]byte, 32*1024)
	for {
		n, err := resp.Body.Read(buf)
		b = append(b, buf[:n]...)
		if err != nil {
			break
		}
	}
	return httpResult{status: resp.StatusCode, body: b, header: resp.Header}
}

func (h *harness) postJSON(t *vhelp.T, path string, payload []byte) httpResult {
	resp, err := http.Post(path, "application/json", strings.NewReader(string(payload)))
	if err != nil {
		t.Fatalf("POST %s: %v", path, err)
	}
	defer func() { _ = resp.Body.Close() }()
	b := make([]byte, 0, 4096)
	buf := make([]byte, 32*1024)
	for {
		n, err := resp.Body.Read(buf)
		b = append(b, buf[:n]...)
		if err != nil {
			break
		}
	}
	return httpResult{status: resp.StatusCode, body: b, header: resp.Header}
}
