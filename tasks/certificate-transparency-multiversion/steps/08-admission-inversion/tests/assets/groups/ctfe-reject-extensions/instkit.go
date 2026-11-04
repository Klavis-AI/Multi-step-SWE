// instkit.go is compiled into exactly one group's single compilation unit.
// It holds no cases and prints nothing; it is not a group of its own.
//
// SCOPE-BACKEND: no Trillian server exists anywhere in this corpus. A CTFE
// instance is stood up in this process from a pinned LogConfig and its own
// handler set is served over a real loopback socket, but the log client it is
// given is either a gRPC client aimed at a loopback address nothing listens on
// (so every backend call is codes.Unavailable) or a client value that returns
// a plain error. Chain validation, get-roots and a frozen STH are all decided
// before any backend call, so they are graded exactly; a submission the
// milestone must accept is graded by the fact that it reaches the backend and
// fails there.
package main

import (
	"context"
	"encoding/base64"
	"encoding/json"
	"encoding/pem"
	"errors"
	"net"
	"net/http"
	"strings"
	"sync/atomic"
	"time"

	"github.com/golang/protobuf/ptypes"
	"github.com/google/certificate-transparency-go/trillian/ctfe"
	"github.com/google/certificate-transparency-go/trillian/ctfe/configpb"
	"github.com/google/trillian"
	"github.com/google/trillian/crypto/keyspb"
	"github.com/google/trillian/monitoring"
	"google.golang.org/grpc"

	// Register the ProtoHandlers for the private-key protos, exactly as
	// ct_server's own main does.
	_ "github.com/google/trillian/crypto/keys/der/proto"
	_ "github.com/google/trillian/crypto/keys/pem/proto"

	"ctverify/vhelp"
)

const (
	pinnedLogID = int64(1001)
	logPrefix   = "bench"
)

// erroringClient is a log client value, not a backend: it holds no state,
// serves no socket and answers the two calls the CTFE makes with a plain error
// that carries no gRPC status, which is what maps onto a 500.
type erroringClient struct {
	trillian.TrillianLogClient // nil; any other method would panic if called
}

func (erroringClient) GetLatestSignedLogRoot(_ context.Context, _ *trillian.GetLatestSignedLogRootRequest, _ ...grpc.CallOption) (*trillian.GetLatestSignedLogRootResponse, error) {
	return nil, errors.New("backend exploded")
}

func (erroringClient) QueueLeaves(_ context.Context, _ *trillian.QueueLeavesRequest, _ ...grpc.CallOption) (*trillian.QueueLeavesResponse, error) {
	return nil, errors.New("backend exploded")
}

// countingClient wraps whatever log client an instance was given and records
// how many backend calls the CTFE made through it.
//
// SCOPE-DIAGNOSTIC: "the backend was consulted" is graded on this counter and
// never on the text of the error the call came back with. That text is
// rendered by google.golang.org/grpc's own status type, not by the candidate,
// and REQUIREMENTS.md's SCOPE-DIAGNOSTIC rule 2 puts a dependency's message
// permanently out of grading. The counter is the observable; the rendering is
// not.
type countingClient struct {
	trillian.TrillianLogClient
	n *int32
}

func (c countingClient) GetLatestSignedLogRoot(ctx context.Context, in *trillian.GetLatestSignedLogRootRequest, opts ...grpc.CallOption) (*trillian.GetLatestSignedLogRootResponse, error) {
	atomic.AddInt32(c.n, 1)
	return c.TrillianLogClient.GetLatestSignedLogRoot(ctx, in, opts...)
}

func (c countingClient) QueueLeaves(ctx context.Context, in *trillian.QueueLeavesRequest, opts ...grpc.CallOption) (*trillian.QueueLeavesResponse, error) {
	atomic.AddInt32(c.n, 1)
	return c.TrillianLogClient.QueueLeaves(ctx, in, opts...)
}

// deadAddr returns a loopback address that nothing is listening on: the
// listener is bound with :0 and closed again, so no fixed port appears here.
func deadAddr(t *vhelp.T) string {
	// Nothing must be listening here for the whole life of the case, so
	// the port is taken from below the ephemeral range: the kernel cannot
	// hand it to an unrelated process and quietly make this address live.
	return lowBandLoopbackAddr(t)
}

// unreachableClient dials a loopback address nothing listens on. grpc.Dial is
// lazy, so this succeeds and every call made through it fails with
// codes.Unavailable, which the CTFE reports as 503.
func unreachableClient(t *vhelp.T) (trillian.TrillianLogClient, func()) {
	conn, err := grpc.Dial(deadAddr(t), grpc.WithInsecure())
	if err != nil {
		t.Fatalf("dial unreachable backend: %v", err)
	}
	return trillian.NewTrillianLogClient(conn), func() { _ = conn.Close() }
}

// ---------------------------------------------------------------------------

type instance struct {
	httpSrv *http.Server
	closer  func()
	baseURL string
	calls   *int32
}

// backendCalls is how many times the CTFE reached for its log client since
// this instance was built.
func (i *instance) backendCalls() int { return int(atomic.LoadInt32(i.calls)) }

func (i *instance) stop() {
	if i.httpSrv != nil {
		_ = i.httpSrv.Close()
	}
	if i.closer != nil {
		i.closer()
	}
}

func pemBody(t *vhelp.T, b []byte) []byte {
	blk, _ := pem.Decode(b)
	if blk == nil {
		t.Fatalf("fixture is not PEM")
	}
	return blk.Bytes
}

// pinnedConfig returns this milestone's LogConfig with the pinned key and the
// pinned roots, plus whatever v1.1.0-only fields the caller set on cfg.
func pinnedConfig(t *vhelp.T, cfg *configpb.LogConfig) *configpb.LogConfig {
	keyDER := pemBody(t, vhelp.ReadFixture("minted", "keys", "log-key.pem"))
	pubDER := pemBody(t, vhelp.ReadFixture("minted", "keys", "log-pubkey.pem"))
	anyKey, err := ptypes.MarshalAny(&keyspb.PrivateKey{Der: keyDER})
	if err != nil {
		t.Fatalf("marshal private key: %v", err)
	}
	cfg.LogId = pinnedLogID
	cfg.Prefix = logPrefix
	cfg.RootsPemFile = []string{vhelp.Fixture("minted", "roots-bench-plus-int.pem")}
	cfg.PrivateKey = anyKey
	cfg.PublicKey = &keyspb.PublicKey{Der: pubDER}
	cfg.MaxMergeDelaySec = 86400
	cfg.ExpectedMergeDelaySec = 120
	return cfg
}

// setUp validates cfg, builds the CTFE instance with the given masking flag
// and log client, and serves its handlers over loopback. It returns a nil
// instance and the candidate's own error if either step refuses the config.
func setUp(t *vhelp.T, cfg *configpb.LogConfig, mask bool, client trillian.TrillianLogClient, closer func()) (*instance, error) {
	if closer == nil {
		closer = func() {}
	}
	vCfg, err := ctfe.ValidateLogConfig(cfg)
	if err != nil {
		closer()
		return nil, err
	}
	var calls int32
	inst, err := ctfe.SetUpInstance(context.Background(), ctfe.InstanceOptions{
		Validated:          vCfg,
		Client:             countingClient{client, &calls},
		Deadline:           10 * time.Second,
		MetricFactory:      monitoring.InertMetricFactory{},
		RequestLog:         new(ctfe.DefaultRequestLog),
		MaskInternalErrors: mask,
	})
	if err != nil {
		closer()
		return nil, err
	}
	mux := http.NewServeMux()
	for p, h := range inst.Handlers {
		mux.Handle(p, h)
	}
	hl, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		closer()
		t.Fatalf("listen for handlers: %v", err)
	}
	srv := &http.Server{Handler: mux}
	go func() { _ = srv.Serve(hl) }()
	return &instance{httpSrv: srv, closer: closer, calls: &calls,
		baseURL: "http://" + hl.Addr().String() + "/" + logPrefix}, nil
}

// setUpDead is the common case: a CTFE instance whose backend does not exist.
func setUpDead(t *vhelp.T, cfg *configpb.LogConfig, mask bool) (*instance, error) {
	c, closer := unreachableClient(t)
	return setUp(t, cfg, mask, c, closer)
}

type httpResult struct {
	status int
	body   string
	ctype  string
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

func (i *instance) get(t *vhelp.T, path string) httpResult {
	resp, err := http.Get(i.baseURL + path)
	if err != nil {
		t.Fatalf("GET %s: %v", path, err)
	}
	defer func() { _ = resp.Body.Close() }()
	return httpResult{resp.StatusCode, string(readAll(resp.Body)), resp.Header.Get("Content-Type")}
}

func (i *instance) post(t *vhelp.T, path, payload string) httpResult {
	resp, err := http.Post(i.baseURL+path, "application/json", strings.NewReader(payload))
	if err != nil {
		t.Fatalf("POST %s: %v", path, err)
	}
	defer func() { _ = resp.Body.Close() }()
	return httpResult{resp.StatusCode, string(readAll(resp.Body)), resp.Header.Get("Content-Type")}
}

// chainJSON renders a pinned PEM chain fixture as an RFC 6962 add-chain body.
func chainJSON(t *vhelp.T, name string) string {
	rest := vhelp.ReadFixture("minted", name)
	var ders []string
	for {
		blk, r := pem.Decode(rest)
		if blk == nil {
			break
		}
		rest = r
		ders = append(ders, base64.StdEncoding.EncodeToString(blk.Bytes))
	}
	if len(ders) == 0 {
		t.Fatalf("fixture %s held no certificate", name)
	}
	b, err := json.Marshal(map[string][]string{"chain": ders})
	if err != nil {
		t.Fatalf("marshal chain: %v", err)
	}
	return string(b)
}

// lowBandLoopbackAddr returns a loopback host:port taken from BELOW the
// kernel's ephemeral range (32768+ on the harness host), probed free by an
// actual bind. A `:0` probe hands back a port the kernel may immediately
// reassign to any unrelated process on the machine in the window before it is
// used; a low-band port is never handed out by the ephemeral allocator, so
// only an explicit binder of that exact port can take it. The starting slot is
// dispersed by one throwaway `:0` bind so that sibling probes in the same run
// do not converge on the same slot.
func lowBandLoopbackAddr(t *vhelp.T) string {
	seed, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("listen 127.0.0.1:0 to disperse the probe: %v", err)
	}
	base := seed.Addr().(*net.TCPAddr).Port % 12000
	if err := seed.Close(); err != nil {
		t.Fatalf("close the dispersal listener: %v", err)
	}
	for i := 0; i < 512; i++ {
		l, err := net.ListenTCP("tcp", &net.TCPAddr{
			IP:   net.IPv4(127, 0, 0, 1),
			Port: 20000 + (base+i)%12000,
		})
		if err != nil {
			continue
		}
		a := l.Addr().String()
		if err := l.Close(); err != nil {
			t.Fatalf("close probe listener: %v", err)
		}
		return a
	}
	t.Fatalf("no free loopback port in 20000-31999 after 512 probes")
	return ""
}
