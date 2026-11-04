// serverkit.go is shared source, copied verbatim into every step-02 group that
// drives the candidate's own ct_server binary. It is not a group: it holds no
// cases and prints nothing. Each copy is compiled into exactly one group's
// single compilation unit, so a group still owns one binary and one process
// tree.
//
// SCOPE-BACKEND: each group supplies its own in-process trillian.TrillianLogServer
// over a real loopback gRPC socket. The pin for this step is trillian
// v1.0.4-0.20171121150315-bc4e6834483a, whose SignedLogRoot still carries plain
// struct fields and whose TrillianLogServer has no Unimplemented embed, so every
// method of the interface is declared below and every answer is a pinned
// constant. Nothing here signs a log root: at this anchor trillian/ctfe contains
// no root-signature check, and the SCT is signed with the CTFE's own configured
// key.
//
// SCOPE-NET: loopback only. Every port a child process is told to bind is
// probed for below the kernel's ephemeral range (see freePort), never taken
// from a :0 bind that is then released. SCOPE-TIME: every root hash, tree size
// and root timestamp below is a compile-time constant.
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
	"google.golang.org/grpc"

	"ctverify/vhelp"
)

// Pinned identities. Two logs, two backends, one log on each.
const (
	logIDA    = int64(1001)
	logIDB    = int64(1002)
	prefixA   = "bench-a"
	prefixB   = "bench-b"
	backendA  = "backend-a"
	backendB  = "backend-b"
	legacyID  = int64(1001)
	legacyPfx = "bench"
)

// Pinned backend state. Each stub answers with its own tree size and root hash
// so that a response can be attributed to exactly one backend.
const (
	treeSizeA    = int64(11)
	treeSizeB    = int64(22)
	rootNanosA   = int64(1500000000000000000)
	rootNanosB   = int64(1600000000000000000)
	rootMillisA  = uint64(1500000000000)
	rootMillisB  = uint64(1600000000000)
	treeSizeLgcy = int64(33)
	rootNanosLgc = int64(1700000000000000000)
)

var (
	rootHashA    = sha256.Sum256([]byte("harbor-ct-step02-backend-a"))
	rootHashB    = sha256.Sum256([]byte("harbor-ct-step02-backend-b"))
	rootHashLgcy = sha256.Sum256([]byte("harbor-ct-step02-legacy"))
)

// ---------------------------------------------------------------------------
// The pinned backend.
// ---------------------------------------------------------------------------

type queued struct {
	logID     int64
	leafValue []byte
}

type logBackend struct {
	name string

	timestampNanos int64
	rootHash       []byte
	treeSize       int64

	mu        sync.Mutex
	queues    []queued
	rootCalls []int64
}

func newBackend(name string, nanos int64, root []byte, size int64) *logBackend {
	return &logBackend{name: name, timestampNanos: nanos, rootHash: root, treeSize: size}
}

func (b *logBackend) signedRoot(logID int64) *trillian.SignedLogRoot {
	return &trillian.SignedLogRoot{
		TimestampNanos: b.timestampNanos,
		RootHash:       b.rootHash,
		TreeSize:       b.treeSize,
		LogId:          logID,
	}
}

// queuedLogIDs reports, in call order, the log id of every QueueLeaves this
// backend received.
func (b *logBackend) queuedLogIDs() []int64 {
	b.mu.Lock()
	defer b.mu.Unlock()
	out := make([]int64, 0, len(b.queues))
	for _, q := range b.queues {
		out = append(out, q.logID)
	}
	return out
}

func (b *logBackend) queueCount() int {
	b.mu.Lock()
	defer b.mu.Unlock()
	return len(b.queues)
}

func (b *logBackend) lastLeafValue() []byte {
	b.mu.Lock()
	defer b.mu.Unlock()
	if len(b.queues) == 0 {
		return nil
	}
	return b.queues[len(b.queues)-1].leafValue
}

func (b *logBackend) rootLogIDs() []int64 {
	b.mu.Lock()
	defer b.mu.Unlock()
	return append([]int64(nil), b.rootCalls...)
}

func (b *logBackend) QueueLeaves(_ context.Context, req *trillian.QueueLeavesRequest) (*trillian.QueueLeavesResponse, error) {
	b.mu.Lock()
	var last []byte
	for _, l := range req.GetLeaves() {
		last = append([]byte(nil), l.GetLeafValue()...)
		b.queues = append(b.queues, queued{logID: req.GetLogId(), leafValue: last})
	}
	b.mu.Unlock()

	out := make([]*trillian.QueuedLogLeaf, 0, len(req.GetLeaves()))
	for _, l := range req.GetLeaves() {
		out = append(out, &trillian.QueuedLogLeaf{Leaf: l})
	}
	return &trillian.QueueLeavesResponse{QueuedLeaves: out}, nil
}

func (b *logBackend) QueueLeaf(_ context.Context, req *trillian.QueueLeafRequest) (*trillian.QueueLeafResponse, error) {
	b.mu.Lock()
	b.queues = append(b.queues, queued{logID: req.GetLogId(), leafValue: append([]byte(nil), req.GetLeaf().GetLeafValue()...)})
	b.mu.Unlock()
	return &trillian.QueueLeafResponse{QueuedLeaf: &trillian.QueuedLogLeaf{Leaf: req.GetLeaf()}}, nil
}

func (b *logBackend) GetLatestSignedLogRoot(_ context.Context, req *trillian.GetLatestSignedLogRootRequest) (*trillian.GetLatestSignedLogRootResponse, error) {
	b.mu.Lock()
	b.rootCalls = append(b.rootCalls, req.GetLogId())
	b.mu.Unlock()
	return &trillian.GetLatestSignedLogRootResponse{SignedLogRoot: b.signedRoot(req.GetLogId())}, nil
}

func (b *logBackend) GetInclusionProof(_ context.Context, req *trillian.GetInclusionProofRequest) (*trillian.GetInclusionProofResponse, error) {
	return &trillian.GetInclusionProofResponse{Proof: &trillian.Proof{LeafIndex: req.GetLeafIndex(), Hashes: [][]byte{b.rootHash}}}, nil
}

func (b *logBackend) GetInclusionProofByHash(_ context.Context, _ *trillian.GetInclusionProofByHashRequest) (*trillian.GetInclusionProofByHashResponse, error) {
	return &trillian.GetInclusionProofByHashResponse{Proof: []*trillian.Proof{{LeafIndex: 0, Hashes: [][]byte{b.rootHash}}}}, nil
}

func (b *logBackend) GetConsistencyProof(_ context.Context, _ *trillian.GetConsistencyProofRequest) (*trillian.GetConsistencyProofResponse, error) {
	return &trillian.GetConsistencyProofResponse{Proof: &trillian.Proof{Hashes: [][]byte{b.rootHash}}}, nil
}

func (b *logBackend) GetSequencedLeafCount(_ context.Context, _ *trillian.GetSequencedLeafCountRequest) (*trillian.GetSequencedLeafCountResponse, error) {
	return &trillian.GetSequencedLeafCountResponse{LeafCount: b.treeSize}, nil
}

func (b *logBackend) GetEntryAndProof(_ context.Context, req *trillian.GetEntryAndProofRequest) (*trillian.GetEntryAndProofResponse, error) {
	return &trillian.GetEntryAndProofResponse{
		Proof: &trillian.Proof{LeafIndex: req.GetLeafIndex(), Hashes: [][]byte{b.rootHash}},
		Leaf:  &trillian.LogLeaf{LeafIndex: req.GetLeafIndex()},
	}, nil
}

func (b *logBackend) GetLeavesByIndex(_ context.Context, req *trillian.GetLeavesByIndexRequest) (*trillian.GetLeavesByIndexResponse, error) {
	return &trillian.GetLeavesByIndexResponse{}, nil
}

func (b *logBackend) GetLeavesByHash(_ context.Context, _ *trillian.GetLeavesByHashRequest) (*trillian.GetLeavesByHashResponse, error) {
	return &trillian.GetLeavesByHashResponse{}, nil
}

// startedBackends holds every gRPC server this group started, so that a
// harness can shut all of them down without the driver tracking them.
var startedBackends []*grpc.Server

// serve puts b on a fresh loopback socket and returns its address.
func (b *logBackend) serve(t *vhelp.T) string {
	lis, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("listen for backend %s: %v", b.name, err)
	}
	gs := grpc.NewServer()
	trillian.RegisterTrillianLogServer(gs, b)
	go func() { _ = gs.Serve(lis) }()
	startedBackends = append(startedBackends, gs)
	return lis.Addr().String()
}

// ---------------------------------------------------------------------------
// Configuration.
// ---------------------------------------------------------------------------

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

func join(a, b string) string {
	if strings.HasSuffix(a, "/") {
		return a + b
	}
	return a + "/" + b
}

// logConfigBody renders one LogConfig. The private key is inlined as a
// keyspb.PrivateKey rather than a keyspb.PEMKeyFile because the trillian
// revision pinned at this anchor refuses a PEMKeyFile with an empty password
// and the corpus log key is unencrypted; the key bytes are the pinned fixture's
// either way.
func logConfigBody(t *vhelp.T, indent string, logID int64, prefix, backendName, extra string) string {
	keyDER := pemBody(t, vhelp.ReadFixture("minted", "keys", "log-key.pem"))
	pubDER := pemBody(t, vhelp.ReadFixture("minted", "keys", "log-pubkey.pem"))
	rootsPath := vhelp.Fixture("minted", "roots-bench-plus-int.pem")

	var b strings.Builder
	b.WriteString(indent + "config {\n")
	fmt.Fprintf(&b, "%s  log_id: %d\n", indent, logID)
	fmt.Fprintf(&b, "%s  prefix: %q\n", indent, prefix)
	fmt.Fprintf(&b, "%s  roots_pem_file: %q\n", indent, rootsPath)
	b.WriteString(indent + "  private_key: { [type.googleapis.com/keyspb.PrivateKey] { der: \"" + protoEscape(keyDER) + "\" } }\n")
	b.WriteString(indent + "  public_key: { der: \"" + protoEscape(pubDER) + "\" }\n")
	if backendName != "" {
		fmt.Fprintf(&b, "%s  log_backend_name: %q\n", indent, backendName)
	}
	b.WriteString(extra)
	b.WriteString(indent + "}\n")
	return b.String()
}

func writeScratch(t *vhelp.T, name, body string) string {
	p := join(os.Getenv("CT_SCRATCH"), name)
	if err := os.WriteFile(p, []byte(body), 0600); err != nil {
		t.Fatalf("write %s: %v", p, err)
	}
	return p
}

// ---------------------------------------------------------------------------
// Bringing up the personality.
// ---------------------------------------------------------------------------

type harness struct {
	cmd      *exec.Cmd
	done     chan struct{}
	baseURL  string
	cfgPath  string
	stderrTo string
}

// exited reports whether the child has already terminated.
func (h *harness) exited() bool {
	select {
	case <-h.done:
		return true
	default:
		return false
	}
}

func (h *harness) url(prefix, path string) string { return h.baseURL + "/" + prefix + path }

// start runs the candidate's ct_server with the given arguments and waits for
// the named prefix to answer get-roots. get-roots is served entirely from the
// configured root pool, so it becomes available as soon as the handlers are
// registered.
func start(t *vhelp.T, cfgPath string, extraArgs []string, readyPrefix string) *harness {
	httpAddr := "127.0.0.1:" + strconv.Itoa(freePort(t))
	metricsAddr := "127.0.0.1:" + strconv.Itoa(freePort(t))

	args := []string{
		"--log_config=" + cfgPath,
		"--http_endpoint=" + httpAddr,
		"--metrics_endpoint=" + metricsAddr,
		"--get_sth_interval=0",
		"--rpc_deadline=20s",
		"--logtostderr",
	}
	args = append(args, extraArgs...)

	errPath := join(os.Getenv("CT_SCRATCH"), "ct_server-"+strconv.Itoa(nextServerSeq())+".stderr")
	errFile, err := os.Create(errPath)
	if err != nil {
		t.Fatalf("create stderr sink: %v", err)
	}
	// vhelp.Command strips CT_VERIFY_NONCE from the child's environment, and
	// the child's own output is captured to a file rather than shared with this
	// group's stdout, so nothing the candidate prints can be read as a case.
	cmd := vhelp.Command(vhelp.CandidateBin("ct_server"), args...)
	cmd.Stdout = errFile
	cmd.Stderr = errFile
	if err := cmd.Start(); err != nil {
		t.Fatalf("start ct_server: %v", err)
	}

	h := &harness{cmd: cmd, done: make(chan struct{}), baseURL: "http://" + httpAddr, cfgPath: cfgPath, stderrTo: errPath}
	// Done calls os.Exit, which does not run deferred functions, so a
	// `defer h.stop()` in a driver never fires and the child survives the
	// group. Register the teardown with the harness instead.
	t.Cleanup(h.stop)
	go func() {
		_ = cmd.Wait()
		close(h.done)
	}()

	deadline := time.Now().Add(90 * time.Second)
	for {
		if time.Now().After(deadline) {
			tail, _ := os.ReadFile(errPath)
			h.stop()
			t.Fatalf("ct_server never served get-roots for %q; stderr: %s", readyPrefix, lastBytes(tail, 2000))
		}
		resp, err := http.Get(h.baseURL + "/" + readyPrefix + "/ct/v1/get-roots")
		if err == nil {
			_ = resp.Body.Close()
			if resp.StatusCode == http.StatusOK {
				return h
			}
		}
		if h.exited() {
			tail, _ := os.ReadFile(errPath)
			h.stop()
			t.Fatalf("ct_server exited early; stderr: %s", lastBytes(tail, 2000))
		}
		time.Sleep(100 * time.Millisecond)
	}
}

func (h *harness) stop() {
	if h.cmd != nil && h.cmd.Process != nil {
		_ = h.cmd.Process.Kill()
		<-h.done
	}
	for _, s := range startedBackends {
		if s != nil {
			s.Stop()
		}
	}
	startedBackends = nil
}

func lastBytes(b []byte, n int) string {
	if len(b) > n {
		b = b[len(b)-n:]
	}
	return strings.ReplaceAll(string(b), "\n", " | ")
}

var serverSeq int

func nextServerSeq() int {
	serverSeq++
	return serverSeq
}

// freePort returns a free loopback port for a child process to bind. No fixed
// port number appears anywhere in this corpus.
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

// ---------------------------------------------------------------------------
// HTTP.
// ---------------------------------------------------------------------------

type httpResult struct {
	status int
	body   []byte
	header http.Header
}

func readAll(resp *http.Response) []byte {
	b := make([]byte, 0, 4096)
	buf := make([]byte, 32*1024)
	for {
		n, err := resp.Body.Read(buf)
		b = append(b, buf[:n]...)
		if err != nil {
			break
		}
	}
	return b
}

func httpGet(t *vhelp.T, url string) httpResult {
	resp, err := http.Get(url)
	if err != nil {
		t.Fatalf("GET %s: %v", url, err)
	}
	defer func() { _ = resp.Body.Close() }()
	return httpResult{status: resp.StatusCode, body: readAll(resp), header: resp.Header}
}

func httpPostJSON(t *vhelp.T, url string, payload []byte) httpResult {
	resp, err := http.Post(url, "application/json", strings.NewReader(string(payload)))
	if err != nil {
		t.Fatalf("POST %s: %v", url, err)
	}
	defer func() { _ = resp.Body.Close() }()
	return httpResult{status: resp.StatusCode, body: readAll(resp), header: resp.Header}
}
