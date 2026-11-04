// serverkit.go is shared source, not a group: it declares no cases and prints
// nothing. A copy is compiled into each group that drives the candidate's own
// ct_server binary, so every group still owns exactly one compilation unit and
// one process tree.
//
// SCOPE-BACKEND. google/trillian is not a deliverable and nothing below is an
// assertion about it. The kit supplies a backend because the capability under
// test needs one: the v1.0.7 signature-cache advisory is about what a running
// log emits, and a log that cannot answer GetLatestSignedLogRoot cannot emit an
// STH at all. The stub is an in-process trillian.TrillianLogServer served over
// a real loopback gRPC socket on a port obtained by binding :0, written against
// this step's pinned trillian v1.0.7-0.20180223125122-7c253b1ef81d. That pin
// has no Unimplemented embed and its TrillianLogServer interface is not the
// same method set as the previous step's pin, so the stub is registered with a
// hand-written grpc.ServiceDesc covering exactly the two methods the CTFE
// calls rather than with the generated registrar. It stays cheap because
// trillian/ctfe performs no signature check on the root it is handed at this
// anchor -- GetTreeHead reads SignedLogRoot's struct fields and stops -- and
// the SCT/STH are signed with the CTFE's own configured key, so an unsigned
// pinned root reaches a verifiable signature.
//
// SCOPE-NET: loopback only, and no port number is ever fixed: the gRPC stub
// serves on the listener a :0 bind handed back, while a port that must be
// named on a command line before the process that binds it exists is probed
// for by freeLoopbackPort below the kernel's ephemeral range (see the banner
// there). Nothing leaves the machine.
// SCOPE-TIME: every root timestamp, tree size and root hash below is a
// constant, and the signing key is the pinned fixture.
package main

import (
	"context"
	"crypto/ecdsa"
	"crypto/sha256"
	stdx509 "crypto/x509"
	"encoding/asn1"
	"encoding/base64"
	"encoding/binary"
	"encoding/json"
	"encoding/pem"
	"fmt"
	"math/big"
	"net"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/google/trillian"
	"google.golang.org/grpc"

	"ctverify/vhelp"
)

// Pinned coordinates. These match configs/multi-valid.textproto, which is the
// config fixture the kit materialises.
const (
	kitLogID  = int64(1001)
	kitPrefix = "bench"

	// A pinned backend root: 2017-07-14T02:40:00Z, tree size 100000.
	kitRootNanos = int64(1500000000000000000)
	kitRootMilli = uint64(1500000000000)
	kitTreeSize  = int64(100000)
)

// A second pinned coordinate set, used only by the two-log routing harness
// below: a different backend, a different log id, a different prefix and a
// visibly different tree head, so an answer can be attributed to exactly one
// backend by its contents alone.
const (
	kitLogIDA  = int64(1001)
	kitLogIDB  = int64(1002)
	kitPrefixA = "bencha"
	kitPrefixB = "benchb"

	kitRootNanosB = int64(1400000000000000000) // 2014-05-13T16:53:20Z
	kitRootMilliB = uint64(1400000000000)
	kitTreeSizeB  = int64(70000)
)

func kitRootHash(tag string) []byte {
	h := sha256.Sum256([]byte("harbor-ct-bench/" + tag))
	return h[:]
}

// ---------------------------------------------------------------------------
// The pinned backend.
// ---------------------------------------------------------------------------

type stubBackend struct {
	mu sync.Mutex

	nanos int64
	size  int64
	hash  []byte

	queueCalls  int
	queueLogIDs []int64
	lastLeaf    []byte
	rootCalls   int
}

func newStub() *stubBackend {
	return &stubBackend{nanos: kitRootNanos, size: kitTreeSize, hash: kitRootHash("root-a")}
}

// setRoot replaces the pinned root the backend serves. Every argument is a
// constant supplied by the caller; nothing here reads a clock.
func (s *stubBackend) setRoot(nanos, size int64, hash []byte) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.nanos, s.size, s.hash = nanos, size, append([]byte(nil), hash...)
}

func (s *stubBackend) root() *trillian.SignedLogRoot {
	s.mu.Lock()
	defer s.mu.Unlock()
	return &trillian.SignedLogRoot{
		TimestampNanos: s.nanos,
		RootHash:       append([]byte(nil), s.hash...),
		TreeSize:       s.size,
		LogId:          kitLogID,
		TreeRevision:   1,
	}
}

func (s *stubBackend) queueSnapshot() (calls int, logIDs []int64, lastLeaf []byte) {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.queueCalls, append([]int64(nil), s.queueLogIDs...), append([]byte(nil), s.lastLeaf...)
}

func (s *stubBackend) GetLatestSignedLogRoot(_ context.Context, _ *trillian.GetLatestSignedLogRootRequest) (*trillian.GetLatestSignedLogRootResponse, error) {
	s.mu.Lock()
	s.rootCalls++
	s.mu.Unlock()
	return &trillian.GetLatestSignedLogRootResponse{SignedLogRoot: s.root()}, nil
}

// QueueLeaves echoes the submitted leaf back, which is what the real backend
// does on a fresh entry: "the leaf data is the same as in the request".
func (s *stubBackend) QueueLeaves(_ context.Context, req *trillian.QueueLeavesRequest) (*trillian.QueueLeavesResponse, error) {
	s.mu.Lock()
	s.queueCalls++
	s.queueLogIDs = append(s.queueLogIDs, req.GetLogId())
	out := make([]*trillian.QueuedLogLeaf, 0, len(req.GetLeaves()))
	for _, l := range req.GetLeaves() {
		s.lastLeaf = append([]byte(nil), l.GetLeafValue()...)
		out = append(out, &trillian.QueuedLogLeaf{Leaf: l})
	}
	s.mu.Unlock()
	return &trillian.QueueLeavesResponse{QueuedLeaves: out}, nil
}

// ---------------------------------------------------------------------------
// Service registration.
//
// The generated RegisterTrillianLogServer insists on the whole fourteen-method
// TrillianLogServer interface of whichever trillian the module graph pins, and
// that method set is not the same at the two anchors this group is checked
// against. Registering a hand-written ServiceDesc for exactly the two methods
// the CTFE calls keeps this source portable across both, so the --expect-fail
// check fails on behaviour rather than on an API that moved underneath it.
// Anything else the CTFE might call gets gRPC's Unimplemented, which is a
// visible error rather than a silent wrong answer.
// ---------------------------------------------------------------------------

type minimalLogServer interface {
	GetLatestSignedLogRoot(context.Context, *trillian.GetLatestSignedLogRootRequest) (*trillian.GetLatestSignedLogRootResponse, error)
	QueueLeaves(context.Context, *trillian.QueueLeavesRequest) (*trillian.QueueLeavesResponse, error)
}

var logServiceDesc = grpc.ServiceDesc{
	ServiceName: "trillian.TrillianLog",
	HandlerType: (*minimalLogServer)(nil),
	Methods: []grpc.MethodDesc{
		{
			MethodName: "GetLatestSignedLogRoot",
			Handler: func(srv interface{}, ctx context.Context, dec func(interface{}) error, interceptor grpc.UnaryServerInterceptor) (interface{}, error) {
				in := new(trillian.GetLatestSignedLogRootRequest)
				if err := dec(in); err != nil {
					return nil, err
				}
				if interceptor == nil {
					return srv.(minimalLogServer).GetLatestSignedLogRoot(ctx, in)
				}
				info := &grpc.UnaryServerInfo{Server: srv, FullMethod: "/trillian.TrillianLog/GetLatestSignedLogRoot"}
				return interceptor(ctx, in, info, func(ctx context.Context, req interface{}) (interface{}, error) {
					return srv.(minimalLogServer).GetLatestSignedLogRoot(ctx, req.(*trillian.GetLatestSignedLogRootRequest))
				})
			},
		},
		{
			MethodName: "QueueLeaves",
			Handler: func(srv interface{}, ctx context.Context, dec func(interface{}) error, interceptor grpc.UnaryServerInterceptor) (interface{}, error) {
				in := new(trillian.QueueLeavesRequest)
				if err := dec(in); err != nil {
					return nil, err
				}
				if interceptor == nil {
					return srv.(minimalLogServer).QueueLeaves(ctx, in)
				}
				info := &grpc.UnaryServerInfo{Server: srv, FullMethod: "/trillian.TrillianLog/QueueLeaves"}
				return interceptor(ctx, in, info, func(ctx context.Context, req interface{}) (interface{}, error) {
					return srv.(minimalLogServer).QueueLeaves(ctx, req.(*trillian.QueueLeavesRequest))
				})
			},
		},
	},
	Streams:  []grpc.StreamDesc{},
	Metadata: "trillian_log_api.proto",
}

// ---------------------------------------------------------------------------
// Bringing up the candidate's personality.
// ---------------------------------------------------------------------------

type harness struct {
	be      *stubBackend
	grpcSrv *grpc.Server
	cmd     *exec.Cmd
	baseURL string
	errPath string
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

// materialiseConfig writes this group's own LogMultiConfig into its private
// scratch directory. It follows configs/multi-valid.textproto exactly except
// for the key wrapper: that fixture names the key with keyspb.PEMKeyFile,
// whose loader at this trillian pin refuses an empty password, so the pinned
// key fixture is inlined as keyspb.PrivateKey DER instead. The roots, the key
// bytes, the log id and the prefix are all pinned; the only thing that varies
// between runs is the loopback address the kernel chose for the stub.
func materialiseConfig(t *vhelp.T, backendAddr string) string {
	keyDER := pemBody(t, vhelp.ReadFixture("minted", "keys", "log-key.pem"))
	pubDER := pemBody(t, vhelp.ReadFixture("minted", "keys", "log-pubkey.pem"))

	var b strings.Builder
	b.WriteString("backends {\n  backend {\n    name: \"default\"\n    backend_spec: \"" + backendAddr + "\"\n  }\n}\n")
	b.WriteString("log_configs {\n  config {\n")
	fmt.Fprintf(&b, "    log_id: %d\n", kitLogID)
	fmt.Fprintf(&b, "    prefix: %q\n", kitPrefix)
	fmt.Fprintf(&b, "    roots_pem_file: %q\n", vhelp.Fixture("minted", "roots-bench-plus-int.pem"))
	b.WriteString("    private_key: {\n      [type.googleapis.com/keyspb.PrivateKey] {\n        der: \"" + protoEscape(keyDER) + "\"\n      }\n    }\n")
	b.WriteString("    public_key: { der: \"" + protoEscape(pubDER) + "\" }\n")
	b.WriteString("    log_backend_name: \"default\"\n")
	b.WriteString("  }\n}\n")

	p := filepath.Join(os.Getenv("CT_SCRATCH"), "ctfe.textproto")
	if err := os.WriteFile(p, []byte(b.String()), 0600); err != nil {
		t.Fatalf("write config: %v", err)
	}
	return p
}

// pemBody returns the DER inside the first PEM block of b.
func pemBody(t *vhelp.T, b []byte) []byte {
	blk, _ := pem.Decode(b)
	if blk == nil {
		t.Fatalf("key fixture is not PEM")
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

// startPersonality boots the candidate's own ct_server against the pinned stub
// and waits until it answers get-roots, which is served entirely from the
// configured root pool and so proves the HTTP mux is up. Anything that goes
// wrong here aborts the group rather than letting it report a vacuous pass.
func startPersonality(t *vhelp.T) *harness {
	be := newStub()

	lis, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("listen for backend: %v", err)
	}
	gs := grpc.NewServer()
	gs.RegisterService(&logServiceDesc, minimalLogServer(be))
	go func() { _ = gs.Serve(lis) }()

	cfg := materialiseConfig(t, lis.Addr().String())
	addr := "127.0.0.1:" + strconv.Itoa(freeLoopbackPort(t))

	errPath := filepath.Join(os.Getenv("CT_SCRATCH"), "ct_server.log")
	errFile, err := os.Create(errPath)
	if err != nil {
		gs.Stop()
		t.Fatalf("create ct_server log sink: %v", err)
	}

	// --log_rpc_server= is what selects the LogMultiConfig reader; the backend
	// address then comes from the config rather than the flag.
	// --metrics_endpoint= keeps metrics on the same loopback port instead of
	// the hard-coded default one, which no group may bind.
	cmd := vhelp.Command(vhelp.CandidateBin("ct_server"),
		"--log_config="+cfg,
		"--log_rpc_server=",
		"--http_endpoint="+addr,
		"--metrics_endpoint=",
		"--get_sth_interval=0",
		"--rpc_deadline=30s",
		"--logtostderr",
	)
	// The candidate's output is captured, never shared with this group's
	// stdout: a child that could write there could forge a report line.
	cmd.Stdout = errFile
	cmd.Stderr = errFile
	if err := cmd.Start(); err != nil {
		gs.Stop()
		t.Fatalf("start ct_server: %v", err)
	}

	h := &harness{be: be, grpcSrv: gs, cmd: cmd, baseURL: "http://" + addr, errPath: errPath}
	// Done calls os.Exit, which does not run deferred functions, so a
	// `defer h.stop()` in a driver never fires and the child survives the
	// group. Register the teardown with the harness instead.
	t.Cleanup(h.stop)

	client := &http.Client{Timeout: 5 * time.Second}
	deadline := time.Now().Add(90 * time.Second)
	for {
		resp, err := client.Get(h.url("/ct/v1/get-roots"))
		if err == nil {
			_ = resp.Body.Close()
			if resp.StatusCode == http.StatusOK {
				return h
			}
		}
		if time.Now().After(deadline) {
			tail, _ := os.ReadFile(errPath)
			h.stop()
			t.Fatalf("ct_server never served get-roots; log tail: %s", lastBytes(tail, 1500))
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

// stop terminates only the process this group started, by the PID it captured
// itself, and then the stub it owns.
func (h *harness) stop() {
	if h.cmd != nil && h.cmd.Process != nil {
		_ = h.cmd.Process.Kill()
		_, _ = h.cmd.Process.Wait()
	}
	if h.grpcSrv != nil {
		h.grpcSrv.Stop()
	}
}

func (h *harness) url(path string) string { return h.baseURL + "/" + kitPrefix + path }

type httpResult struct {
	status int
	body   []byte
	header http.Header
}

func (h *harness) do(t *vhelp.T, req *http.Request) httpResult {
	client := &http.Client{Timeout: 60 * time.Second}
	resp, err := client.Do(req)
	if err != nil {
		tail, _ := os.ReadFile(h.errPath)
		h.stop()
		t.Fatalf("%s %s: %v; ct_server log tail: %s", req.Method, req.URL, err, lastBytes(tail, 1200))
	}
	defer func() { _ = resp.Body.Close() }()
	var b []byte
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

func (h *harness) get(t *vhelp.T, path string) httpResult {
	req, err := http.NewRequest(http.MethodGet, h.url(path), nil)
	if err != nil {
		t.Fatalf("build GET %s: %v", path, err)
	}
	return h.do(t, req)
}

func (h *harness) postChain(t *vhelp.T, path string, pemChain []byte) httpResult {
	payload, err := json.Marshal(struct {
		Chain []string `json:"chain"`
	}{Chain: b64DERs(t, pemChain)})
	if err != nil {
		t.Fatalf("marshal add-chain body: %v", err)
	}
	req, err := http.NewRequest(http.MethodPost, h.url(path), strings.NewReader(string(payload)))
	if err != nil {
		t.Fatalf("build POST %s: %v", path, err)
	}
	req.Header.Set("Content-Type", "application/json")
	return h.do(t, req)
}

// ---------------------------------------------------------------------------
// Benchmark-owned encoding and verification. Nothing below calls the product.
// ---------------------------------------------------------------------------

// pemDERs returns the DER of every CERTIFICATE block in a PEM fixture.
func pemDERs(t *vhelp.T, body []byte) [][]byte {
	var out [][]byte
	rest := body
	for {
		blk, r := pem.Decode(rest)
		if blk == nil {
			break
		}
		rest = r
		if blk.Type == "CERTIFICATE" {
			out = append(out, blk.Bytes)
		}
	}
	if len(out) == 0 {
		t.Fatalf("fixture holds no CERTIFICATE block")
	}
	return out
}

func b64DERs(t *vhelp.T, body []byte) []string {
	ders := pemDERs(t, body)
	out := make([]string, 0, len(ders))
	for _, d := range ders {
		out = append(out, base64.StdEncoding.EncodeToString(d))
	}
	return out
}

// sthSignatureInput rebuilds RFC 6962 section 3.5's signature input from first
// principles: version(0) || signature_type(1) || uint64 timestamp ||
// uint64 tree_size || sha256_root_hash[32].
func sthSignatureInput(timestamp, treeSize uint64, rootHash []byte) []byte {
	b := make([]byte, 0, 50)
	b = append(b, 0x00, 0x01)
	var u [8]byte
	binary.BigEndian.PutUint64(u[:], timestamp)
	b = append(b, u[:]...)
	binary.BigEndian.PutUint64(u[:], treeSize)
	b = append(b, u[:]...)
	return append(b, rootHash...)
}

// sctSignatureInput rebuilds RFC 6962 section 3.2's signature input for an
// x509_entry: version(0) || signature_type(0) || uint64 timestamp ||
// entry_type(uint16 0) || uint24-prefixed leaf DER || uint16-prefixed
// extensions.
func sctSignatureInput(timestamp uint64, leafDER, extensions []byte) []byte {
	b := make([]byte, 0, 16+len(leafDER)+len(extensions))
	b = append(b, 0x00, 0x00)
	var u [8]byte
	binary.BigEndian.PutUint64(u[:], timestamp)
	b = append(b, u[:]...)
	b = append(b, 0x00, 0x00) // x509_entry
	n := len(leafDER)
	b = append(b, byte(n>>16), byte(n>>8), byte(n))
	b = append(b, leafDER...)
	b = append(b, byte(len(extensions)>>8), byte(len(extensions)))
	return append(b, extensions...)
}

// splitDigitallySigned takes apart RFC 5246's digitally-signed framing:
// hash(1) || signature(1) || uint16 length || opaque signature.
func splitDigitallySigned(b []byte) (hashAlg, sigAlg byte, sig []byte, err error) {
	if len(b) < 4 {
		return 0, 0, nil, fmt.Errorf("digitally-signed blob is %d bytes, need at least 4", len(b))
	}
	n := int(binary.BigEndian.Uint16(b[2:4]))
	if len(b) != 4+n {
		return 0, 0, nil, fmt.Errorf("digitally-signed length field says %d, %d bytes follow", n, len(b)-4)
	}
	return b[0], b[1], b[4 : 4+n], nil
}

func verifyECDSA(pub *ecdsa.PublicKey, preimage, sig []byte) error {
	var v struct{ R, S *big.Int }
	rest, err := asn1.Unmarshal(sig, &v)
	if err != nil {
		return fmt.Errorf("signature is not a DER ECDSA-Sig-Value: %v", err)
	}
	if len(rest) != 0 {
		return fmt.Errorf("%d trailing bytes after the ECDSA-Sig-Value", len(rest))
	}
	d := sha256.Sum256(preimage)
	if !ecdsa.Verify(pub, d[:], v.R, v.S) {
		return fmt.Errorf("ECDSA verification failed over the specified signature input")
	}
	return nil
}

func loadPublicKey(t *vhelp.T) *ecdsa.PublicKey {
	blk, _ := pem.Decode(vhelp.ReadFixture("minted", "keys", "log-pubkey.pem"))
	if blk == nil {
		t.Fatalf("minted/keys/log-pubkey.pem is not PEM")
	}
	k, err := stdx509.ParsePKIXPublicKey(blk.Bytes)
	if err != nil {
		t.Fatalf("minted/keys/log-pubkey.pem: %v", err)
	}
	ek, ok := k.(*ecdsa.PublicKey)
	if !ok {
		t.Fatalf("minted/keys/log-pubkey.pem is not an EC key")
	}
	return ek
}

// sthJSON is the benchmark's own reading of the get-sth response body.
type sthJSON struct {
	TreeSize          uint64 `json:"tree_size"`
	Timestamp         uint64 `json:"timestamp"`
	SHA256RootHash    []byte `json:"sha256_root_hash"`
	TreeHeadSignature []byte `json:"tree_head_signature"`
}

// sctJSON is the benchmark's own reading of the add-chain response body.
type sctJSON struct {
	SCTVersion uint8  `json:"sct_version"`
	ID         []byte `json:"id"`
	Timestamp  uint64 `json:"timestamp"`
	Extensions string `json:"extensions"`
	Signature  []byte `json:"signature"`
}

func parseSTH(body []byte) (*sthJSON, error) {
	var s sthJSON
	if err := json.Unmarshal(body, &s); err != nil {
		return nil, fmt.Errorf("get-sth body is not the documented JSON: %v", err)
	}
	return &s, nil
}

func sameBytes(a, b []byte) bool {
	if len(a) != len(b) {
		return false
	}
	for i := range a {
		if a[i] != b[i] {
			return false
		}
	}
	return true
}

// ---------------------------------------------------------------------------
// The two-log, two-backend harness.
//
// Everything above boots one backend and one log, which is all the signing
// capability needs. Step 02's milestone -- each log's requests reach the
// backend its config names -- is only observable with more than one of each,
// so this harness runs two independent stubs on two loopback ports and a
// config that binds one log to each. Nothing above is changed: existing
// groups keep the single-log path exactly as it was.
// ---------------------------------------------------------------------------

// multiHarness owns two stubs and one candidate ct_server.
type multiHarness struct {
	beA, beB *stubBackend
	grpcA    *grpc.Server
	grpcB    *grpc.Server
	cmd      *exec.Cmd
	baseURL  string
	errPath  string
}

// materialiseMultiConfig writes a LogMultiConfig with two named backends and
// one log bound to each. It is the same shape as materialiseConfig, repeated:
// the key, the roots file and the log ids are pinned, only the two loopback
// addresses vary between runs.
func materialiseMultiConfig(t *vhelp.T, addrA, addrB string) string {
	keyDER := pemBody(t, vhelp.ReadFixture("minted", "keys", "log-key.pem"))
	pubDER := pemBody(t, vhelp.ReadFixture("minted", "keys", "log-pubkey.pem"))
	roots := vhelp.Fixture("minted", "roots-bench-plus-int.pem")

	var b strings.Builder
	b.WriteString("backends {\n  backend {\n    name: \"alpha\"\n    backend_spec: \"" + addrA + "\"\n  }\n")
	b.WriteString("  backend {\n    name: \"beta\"\n    backend_spec: \"" + addrB + "\"\n  }\n}\n")
	// LogMultiConfig.log_configs is a single LogConfigSet holding repeated
	// `config` blocks (CONFIG.md §1), so the two logs go inside one
	// log_configs { } rather than into one block each.
	b.WriteString("log_configs {\n")
	for _, l := range []struct {
		id      int64
		prefix  string
		backend string
	}{{kitLogIDA, kitPrefixA, "alpha"}, {kitLogIDB, kitPrefixB, "beta"}} {
		b.WriteString("  config {\n")
		fmt.Fprintf(&b, "    log_id: %d\n", l.id)
		fmt.Fprintf(&b, "    prefix: %q\n", l.prefix)
		fmt.Fprintf(&b, "    roots_pem_file: %q\n", roots)
		b.WriteString("    private_key: {\n      [type.googleapis.com/keyspb.PrivateKey] {\n        der: \"" + protoEscape(keyDER) + "\"\n      }\n    }\n")
		b.WriteString("    public_key: { der: \"" + protoEscape(pubDER) + "\" }\n")
		fmt.Fprintf(&b, "    log_backend_name: %q\n", l.backend)
		b.WriteString("  }\n")
	}
	b.WriteString("}\n")

	p := filepath.Join(os.Getenv("CT_SCRATCH"), "ctfe-multi.textproto")
	if err := os.WriteFile(p, []byte(b.String()), 0600); err != nil {
		t.Fatalf("write multi-backend config: %v", err)
	}
	return p
}

func serveStub(t *vhelp.T, be *stubBackend) (*grpc.Server, string) {
	lis, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("listen for backend: %v", err)
	}
	gs := grpc.NewServer()
	gs.RegisterService(&logServiceDesc, minimalLogServer(be))
	go func() { _ = gs.Serve(lis) }()
	return gs, lis.Addr().String()
}

// startTwoLogPersonality boots the candidate's ct_server over two backends and
// waits until BOTH prefixes answer get-roots. get-roots is served from the
// configured root pool without a backend round trip, so the readiness poll
// leaves the call counters at zero.
func startTwoLogPersonality(t *vhelp.T) *multiHarness {
	beA := &stubBackend{nanos: kitRootNanos, size: kitTreeSize, hash: kitRootHash("routing-alpha")}
	beB := &stubBackend{nanos: kitRootNanosB, size: kitTreeSizeB, hash: kitRootHash("routing-beta")}

	gsA, addrA := serveStub(t, beA)
	gsB, addrB := serveStub(t, beB)

	cfg := materialiseMultiConfig(t, addrA, addrB)
	addr := "127.0.0.1:" + strconv.Itoa(freeLoopbackPort(t))

	errPath := filepath.Join(os.Getenv("CT_SCRATCH"), "ct_server-multi.log")
	errFile, err := os.Create(errPath)
	if err != nil {
		gsA.Stop()
		gsB.Stop()
		t.Fatalf("create ct_server log sink: %v", err)
	}

	cmd := vhelp.Command(vhelp.CandidateBin("ct_server"),
		"--log_config="+cfg,
		"--log_rpc_server=",
		"--http_endpoint="+addr,
		"--metrics_endpoint=",
		"--get_sth_interval=0",
		"--rpc_deadline=30s",
		"--logtostderr",
	)
	cmd.Stdout = errFile
	cmd.Stderr = errFile
	if err := cmd.Start(); err != nil {
		gsA.Stop()
		gsB.Stop()
		t.Fatalf("start ct_server: %v", err)
	}

	h := &multiHarness{beA: beA, beB: beB, grpcA: gsA, grpcB: gsB, cmd: cmd,
		baseURL: "http://" + addr, errPath: errPath}
	// Done calls os.Exit, which does not run deferred functions, so a
	// `defer h.stop()` in a driver never fires and the child survives the
	// group. Register the teardown with the harness instead.
	t.Cleanup(h.stop)

	client := &http.Client{Timeout: 5 * time.Second}
	deadline := time.Now().Add(90 * time.Second)
	for {
		okA, okB := false, false
		for _, pr := range []struct {
			prefix string
			ok     *bool
		}{{kitPrefixA, &okA}, {kitPrefixB, &okB}} {
			resp, err := client.Get(h.baseURL + "/" + pr.prefix + "/ct/v1/get-roots")
			if err == nil {
				_ = resp.Body.Close()
				*pr.ok = resp.StatusCode == http.StatusOK
			}
		}
		if okA && okB {
			return h
		}
		if time.Now().After(deadline) {
			tail, _ := os.ReadFile(errPath)
			h.stop()
			t.Fatalf("ct_server never served get-roots on both prefixes; log tail: %s", lastBytes(tail, 1500))
		}
		time.Sleep(100 * time.Millisecond)
	}
}

// stop terminates only the process this group started, by the PID it captured
// itself, and then the two stubs it owns.
func (h *multiHarness) stop() {
	if h.cmd != nil && h.cmd.Process != nil {
		_ = h.cmd.Process.Kill()
		_, _ = h.cmd.Process.Wait()
	}
	if h.grpcA != nil {
		h.grpcA.Stop()
	}
	if h.grpcB != nil {
		h.grpcB.Stop()
	}
}

func (h *multiHarness) get(t *vhelp.T, prefix, path string) httpResult {
	url := h.baseURL + "/" + prefix + path
	req, err := http.NewRequest(http.MethodGet, url, nil)
	if err != nil {
		t.Fatalf("build GET %s: %v", url, err)
	}
	client := &http.Client{Timeout: 60 * time.Second}
	resp, err := client.Do(req)
	if err != nil {
		tail, _ := os.ReadFile(h.errPath)
		h.stop()
		t.Fatalf("GET %s: %v; ct_server log tail: %s", url, err, lastBytes(tail, 1200))
	}
	defer func() { _ = resp.Body.Close() }()
	var b []byte
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

// rootCallCount reports how many GetLatestSignedLogRoot calls a stub has seen.
func (s *stubBackend) rootCallCount() int {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.rootCalls
}
