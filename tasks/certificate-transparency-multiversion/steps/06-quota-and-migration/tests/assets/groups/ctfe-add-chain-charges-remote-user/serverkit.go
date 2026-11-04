// serverkit.go is copied verbatim into every group that drives the
// candidate's own ct_server binary. It is not a group: it holds no cases and
// prints nothing. Each copy is compiled into exactly one group's single
// compilation unit, so a group still owns one process tree and one binary.
//
// SCOPE-BACKEND: google/trillian is not a deliverable and is never graded, so
// nothing below is an assertion about it. The kit supplies a backend because
// the capability under test needs one -- a log that cannot queue a leaf
// cannot issue an SCT. The stub is an in-process TrillianLogServer served
// over a real loopback gRPC socket, written against this step's pinned
// trillian v1.2.0 API only; a step on another anchor writes its own. At this
// anchor the CTFE reads the deprecated scalar fields of SignedLogRoot
// (slr.TreeSize, slr.RootHash, slr.TimestampNanos) and performs no signature
// check on them, and it signs the SCT with its own configured key, so an
// unsigned root is sufficient and no log-root signing is reimplemented here.
//
// SCOPE-NET: loopback only, every port obtained by binding :0. SCOPE-TIME:
// every timestamp and root below is a constant. The one clock the candidate
// itself reads is the SCT timestamp, which the CTFE takes from its own
// TimeSource; no case asserts its value, only that the signature over it
// verifies.
//
// STEP 06: this release is the one that makes the CTFE *populate*
// trillian.ChargeTo on its backend requests (trillian/ctfe/cert_quota.go and
// the two --quota_* flags). The field itself is not new -- it exists in the
// pinned trillian API at every anchor -- but before v1.0.19 nothing in the
// product tree ever set it, so the stub's per-method record of the charge it
// was handed is a step-06 observable and is empty on the previous tree.
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
	"sort"
	"strconv"
	"strings"
	"sync"
	"time"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/tls"
	"github.com/google/certificate-transparency-go/trillian/ctfe"
	"github.com/google/certificate-transparency-go/trillian/ctfe/configpb"
	"github.com/google/trillian"
	"github.com/google/trillian/monitoring"
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
	logPrefix         = "bench"
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

	rangeCalls int
	lastStart  int64
	lastCount  int64

	// charges records the ChargeTo the CTFE attached to each RPC, keyed by
	// method name and in arrival order. This is the step-06 observable: at the
	// previous anchor every slice below is empty, because no product code
	// builds a ChargeTo.
	charges map[string][][]string
}

func (s *stubBackend) recordCharge(method string, c *trillian.ChargeTo) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.charges == nil {
		s.charges = map[string][][]string{}
	}
	s.charges[method] = append(s.charges[method], append([]string(nil), c.GetUser()...))
}

// chargesFor returns every recorded charge for one method, oldest first.
func (s *stubBackend) chargesFor(method string) [][]string {
	s.mu.Lock()
	defer s.mu.Unlock()
	out := make([][]string, 0, len(s.charges[method]))
	for _, u := range s.charges[method] {
		out = append(out, append([]string(nil), u...))
	}
	return out
}

// lastCharge returns the most recent charge recorded for a method, or nil.
func (s *stubBackend) lastCharge(method string) []string {
	all := s.chargesFor(method)
	if len(all) == 0 {
		return nil
	}
	return all[len(all)-1]
}

// queueSnapshot returns the count of queued leaves together with the user list
// of the last charge seen on QueueLeaves.
func (s *stubBackend) queueSnapshot() (calls int, leafValue []byte, lastUsers []string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if n := len(s.charges["QueueLeaves"]); n > 0 {
		lastUsers = append([]string(nil), s.charges["QueueLeaves"][n-1]...)
	}
	return s.queueCalls, append([]byte(nil), s.lastQueued...), lastUsers
}

// methodsCharged lists the RPC methods on which at least one charge was seen.
func (s *stubBackend) methodsCharged() []string {
	s.mu.Lock()
	defer s.mu.Unlock()
	var out []string
	for m, v := range s.charges {
		if len(v) > 0 {
			out = append(out, m)
		}
	}
	sort.Strings(out)
	return out
}

func (s *stubBackend) rangeSnapshot() (calls int, start, count int64) {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.rangeCalls, s.lastStart, s.lastCount
}

func (s *stubBackend) signedRoot() *trillian.SignedLogRoot {
	return &trillian.SignedLogRoot{
		TimestampNanos: pinnedTimestampMS * 1000000,
		RootHash:       pinnedRootHash[:],
		TreeSize:       pinnedTreeSize,
		TreeRevision:   1,
	}
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
	s.recordCharge("QueueLeaf", req.GetChargeTo())
	s.mu.Lock()
	s.queueCalls++
	s.lastQueued = append([]byte(nil), req.GetLeaf().GetLeafValue()...)
	s.mu.Unlock()
	return &trillian.QueueLeafResponse{QueuedLeaf: &trillian.QueuedLogLeaf{Leaf: req.GetLeaf()}}, nil
}

func (s *stubBackend) QueueLeaves(_ context.Context, req *trillian.QueueLeavesRequest) (*trillian.QueueLeavesResponse, error) {
	s.recordCharge("QueueLeaves", req.GetChargeTo())
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

func (s *stubBackend) GetLatestSignedLogRoot(_ context.Context, req *trillian.GetLatestSignedLogRootRequest) (*trillian.GetLatestSignedLogRootResponse, error) {
	s.recordCharge("GetLatestSignedLogRoot", req.GetChargeTo())
	return &trillian.GetLatestSignedLogRootResponse{SignedLogRoot: s.signedRoot()}, nil
}

func (s *stubBackend) GetLeavesByRange(_ context.Context, req *trillian.GetLeavesByRangeRequest) (*trillian.GetLeavesByRangeResponse, error) {
	s.recordCharge("GetLeavesByRange", req.GetChargeTo())
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

func (s *stubBackend) GetLeavesByIndex(_ context.Context, req *trillian.GetLeavesByIndexRequest) (*trillian.GetLeavesByIndexResponse, error) {
	s.recordCharge("GetLeavesByIndex", req.GetChargeTo())
	leaves := make([]*trillian.LogLeaf, 0, len(req.GetLeafIndex()))
	for _, i := range req.GetLeafIndex() {
		leaves = append(leaves, s.pinnedLeaf(i))
	}
	return &trillian.GetLeavesByIndexResponse{Leaves: leaves, SignedLogRoot: s.signedRoot()}, nil
}

func (s *stubBackend) GetLeavesByHash(_ context.Context, req *trillian.GetLeavesByHashRequest) (*trillian.GetLeavesByHashResponse, error) {
	s.recordCharge("GetLeavesByHash", req.GetChargeTo())
	return &trillian.GetLeavesByHashResponse{SignedLogRoot: s.signedRoot()}, nil
}

func (s *stubBackend) GetInclusionProof(_ context.Context, req *trillian.GetInclusionProofRequest) (*trillian.GetInclusionProofResponse, error) {
	s.recordCharge("GetInclusionProof", req.GetChargeTo())
	return &trillian.GetInclusionProofResponse{
		Proof:         &trillian.Proof{LeafIndex: req.GetLeafIndex(), Hashes: [][]byte{pinnedRootHash[:]}},
		SignedLogRoot: s.signedRoot(),
	}, nil
}

func (s *stubBackend) GetInclusionProofByHash(_ context.Context, req *trillian.GetInclusionProofByHashRequest) (*trillian.GetInclusionProofByHashResponse, error) {
	s.recordCharge("GetInclusionProofByHash", req.GetChargeTo())
	return &trillian.GetInclusionProofByHashResponse{
		Proof:         []*trillian.Proof{{LeafIndex: 7, Hashes: [][]byte{pinnedRootHash[:]}}},
		SignedLogRoot: s.signedRoot(),
	}, nil
}

func (s *stubBackend) GetConsistencyProof(_ context.Context, req *trillian.GetConsistencyProofRequest) (*trillian.GetConsistencyProofResponse, error) {
	s.recordCharge("GetConsistencyProof", req.GetChargeTo())
	return &trillian.GetConsistencyProofResponse{
		Proof:         &trillian.Proof{Hashes: [][]byte{pinnedRootHash[:]}},
		SignedLogRoot: s.signedRoot(),
	}, nil
}

func (s *stubBackend) GetEntryAndProof(_ context.Context, req *trillian.GetEntryAndProofRequest) (*trillian.GetEntryAndProofResponse, error) {
	s.recordCharge("GetEntryAndProof", req.GetChargeTo())
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
	backend  *stubBackend
	grpcSrv  *grpc.Server
	cmd      *exec.Cmd
	baseURL  string
	cfgPath  string
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

// startBackend brings up only the pinned backend -- no ct_server at all -- on
// a real loopback gRPC socket, and returns a TrillianLogClient speaking to it.
// Groups that grade an exported ctfe entry point directly (rather than over
// the personality's HTTP surface) use this; the ChargeTo the entry point
// attaches is then read back off the stub.
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

// pinnedLogConfig parses this group's pinned configuration back through the
// candidate's own loader, so the *configpb.LogConfig handed to SetUpInstance
// is the one the product itself would have built from that file.
func pinnedLogConfig(t *vhelp.T, extraLogConfig string) *configpb.LogConfig {
	cfgs, err := ctfe.LogConfigFromFile(writeConfig(t, extraLogConfig))
	if err != nil {
		t.Fatalf("LogConfigFromFile: %v", err)
	}
	if len(cfgs) != 1 {
		t.Fatalf("pinned config holds %d logs, want exactly 1", len(cfgs))
	}
	return cfgs[0]
}

// setUpPinnedInstance calls the candidate's own ctfe.SetUpInstance against the
// pinned configuration and the supplied options. Besides returning the handler
// set, this is the only exported way to initialise ctfe's package-level metric
// gauges, which several of its exported entry points write to
// unconditionally; a driver that calls such an entry point without first
// standing up an instance crashes on a nil gauge.
func setUpPinnedInstance(t *vhelp.T, client trillian.TrillianLogClient, opts ctfe.InstanceOptions) *ctfe.PathHandlers {
	if opts.MetricFactory == nil {
		opts.MetricFactory = monitoring.InertMetricFactory{}
	}
	if opts.Deadline == 0 {
		opts.Deadline = 20 * time.Second
	}
	ctx, cancel := context.WithTimeout(context.Background(), 60*time.Second)
	defer cancel()
	h, err := ctfe.SetUpInstance(ctx, client, pinnedLogConfig(t, ""), opts)
	if err != nil {
		t.Fatalf("SetUpInstance: %v", err)
	}
	return h
}

// writeConfig writes this group's own pinned CTFE configuration -- a
// LogConfigSet, which is what ct_server parses when --log_rpc_server is given
// -- into the group's private scratch directory. It is synthesised rather
// than read from a file only so the key material can be inlined; every value
// in it is a pinned fixture or a constant.
func writeConfig(t *vhelp.T, extraLogConfig string) string {
	keyDER := pemBody(t, vhelp.ReadFixture("minted", "keys", "log-key.pem"))
	pubDER := pemBody(t, vhelp.ReadFixture("minted", "keys", "log-pubkey.pem"))
	rootsPath := vhelp.Fixture("minted", "roots-bench-plus-int.pem")

	var b strings.Builder
	b.WriteString("config {\n")
	fmt.Fprintf(&b, "  log_id: %d\n", pinnedLogID)
	fmt.Fprintf(&b, "  prefix: %q\n", logPrefix)
	fmt.Fprintf(&b, "  roots_pem_file: %q\n", rootsPath)
	b.WriteString("  private_key: {\n    [type.googleapis.com/keyspb.PrivateKey] {\n      der: \"" + protoEscape(keyDER) + "\"\n    }\n  }\n")
	b.WriteString("  public_key: { der: \"" + protoEscape(pubDER) + "\" }\n")
	b.WriteString(extraLogConfig)
	b.WriteString("}\n")

	p := joinPath(os.Getenv("CT_SCRATCH"), "ctfe.textproto")
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

func joinPath(a, b string) string {
	if strings.HasSuffix(a, "/") {
		return a + b
	}
	return a + "/" + b
}

// startPersonality boots the candidate's own ct_server against the pinned
// backend and waits until it answers on the root path. Readiness is probed on
// "/" and not "/healthz": no anchor in this range has a health endpoint, and
// "/" is registered only after every configured log instance has been set up,
// so a 200 there means the personality is fully wired. A failure here aborts
// the group rather than letting it report a vacuous pass.
//
// At this anchor ct_server itself decides whether to install the two quota
// callbacks -- InstanceOptions.RemoteQuotaUser comes from --quota_remote and
// InstanceOptions.CertificateQuotaUser from --quota_intermediate, both of
// which default to true -- so the caller passes the flags through verbatim
// rather than the harness calling SetUpInstance directly.
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

	cfg := writeConfig(t, extraLogConfig)

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

	errPath := joinPath(os.Getenv("CT_SCRATCH"), "ct_server.stderr")
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

	h := &harness{backend: be, grpcSrv: gs, cmd: cmd, baseURL: "http://" + addr, cfgPath: cfg, stderrTo: errPath}
	// Done calls os.Exit, which does not run deferred functions, so a
	// `defer h.stop()` in a driver never fires and the child survives the
	// group. Register the teardown with the harness instead.
	t.Cleanup(h.stop)

	client := &http.Client{Timeout: 5 * time.Second}
	deadline := time.Now().Add(90 * time.Second)
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

// logURL builds a URL under this log's prefix; rootURL builds one that is not.
func (h *harness) logURL(path string) string  { return h.baseURL + "/" + logPrefix + path }
func (h *harness) rootURL(path string) string { return h.baseURL + path }

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

// postJSONFrom is postJSON with a trusted client address declared in
// X-Forwarded-For. The flag the CTFE installs for --quota_remote at this
// anchor is tomasen/realip's FromRequest, which prefers the first non-private
// address in that header over r.RemoteAddr; a loopback origin therefore has
// to say who it is, and the CTFE turns that string into the quota user
// verbatim. Without the header the user is "127.0.0.1", which is a fine
// assertion too and is used by the sibling group.
func (h *harness) postJSONFrom(t *vhelp.T, url, forwardedFor string, payload []byte) httpResult {
	req, err := http.NewRequest(http.MethodPost, url, strings.NewReader(string(payload)))
	if err != nil {
		t.Fatalf("build POST %s: %v", url, err)
	}
	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("X-Forwarded-For", forwardedFor)
	return h.do(t, req)
}

// getFrom is get with a declared client address; see postJSONFrom.
func (h *harness) getFrom(t *vhelp.T, url, forwardedFor string) httpResult {
	req, err := http.NewRequest(http.MethodGet, url, nil)
	if err != nil {
		t.Fatalf("build GET %s: %v", url, err)
	}
	req.Header.Set("X-Forwarded-For", forwardedFor)
	return h.do(t, req)
}
