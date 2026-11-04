// Group ctfe-readonly-mode.
//
// v1.1.3 adds LogConfig.is_readonly (field 19) to the CTFE configuration and
// wires it into logInfo.Handlers: a read-only log registers every RFC 6962
// read endpoint and neither add-chain nor add-pre-chain.
//
// The capability is only half visible without a backend -- "read-only" means
// the write endpoints are gone WHILE the read endpoints still serve -- so this
// group brings up its own pinned Trillian stub over a loopback gRPC socket
// (see backendstub.go) and drives real CTFE instances mounted on loopback HTTP
// sockets. It asserts that a read-only log answers get-sth, get-entries and
// get-roots with exactly the pinned data, that its add-chain and add-pre-chain
// are not routed at all, and that across the whole group the backend was never
// asked to queue a leaf.
//
// At v1.1.2 configpb.LogConfig has no IsReadonly field (11, 12, 13 are
// followed by 14), so this driver does not build there; and even a
// hand-written configuration carrying the field is rejected by the loader,
// which parses with prototext and so refuses an unknown field.
package main

import (
	"context"
	"crypto/x509"
	"encoding/base64"
	"encoding/json"
	"encoding/pem"
	"fmt"
	"io"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"time"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/trillian/ctfe"
	"github.com/google/certificate-transparency-go/trillian/ctfe/configpb"
	"github.com/google/trillian"
	"github.com/google/trillian/crypto/keys"
	"github.com/google/trillian/crypto/keys/der"
	"github.com/google/trillian/crypto/keyspb"
	"github.com/google/trillian/monitoring"
	"google.golang.org/protobuf/proto"

	"ctverify/vhelp"
)

// configTemplate is a single-log CTFE configuration. @READONLY@ is replaced
// per case, @ROOTS@ is a pinned fixture path and @KEYDER@ is the pinned log
// signing key as escaped DER. Nothing in it depends on the clock: no
// not_after window, no frozen STH, no merge delays.
const configTemplate = `config {
  log_id: 1001
  prefix: "bench"
  roots_pem_file: "@ROOTS@"
  private_key: {
    [type.googleapis.com/keyspb.PrivateKey] {
      der: "@KEYDER@"
    }
  }
  is_readonly: @READONLY@
}
`

// The six read endpoints of RFC 6962 and the two write endpoints, under the
// configured "bench" prefix, sorted (the handler set is a map).
const readPaths = "/bench/ct/v1/get-entries /bench/ct/v1/get-entry-and-proof " +
	"/bench/ct/v1/get-proof-by-hash /bench/ct/v1/get-roots /bench/ct/v1/get-sth " +
	"/bench/ct/v1/get-sth-consistency"

const writePaths = "/bench/ct/v1/add-chain /bench/ct/v1/add-pre-chain "

func main() {
	t := vhelp.New()

	// The CTFE loads its signing key through Trillian's key registry, exactly
	// as ct_server does.
	keys.RegisterHandler(&keyspb.PrivateKey{}, der.FromProto)

	// ---- configuration -----------------------------------------------------

	// 1. A configuration carrying is_readonly loads, and the flag survives.
	roPath := writeConfig(t, "readonly.textproto", "true")
	roCfgs, err := ctfe.LogConfigFromFile(roPath)
	if err != nil {
		t.Fatalf("LogConfigFromFile(readonly): %v", err)
	}
	t.Check("readonly-config-loads",
		len(roCfgs) == 1 && roCfgs[0].IsReadonly && !roCfgs[0].IsMirror,
		"logs=%d cfg=%v", len(roCfgs), roCfgs)

	// 2. is_readonly is a field of its own: a log that clears it is writable,
	//    and nothing else in the configuration turns it on.
	rwPath := writeConfig(t, "writable.textproto", "false")
	rwCfgs, err := ctfe.LogConfigFromFile(rwPath)
	if err != nil {
		t.Fatalf("LogConfigFromFile(writable): %v", err)
	}
	t.Check("writable-config-loads",
		len(rwCfgs) == 1 && !rwCfgs[0].IsReadonly, "cfg=%v", rwCfgs)

	// 3. Read-only is not a reason to reject a log: validation passes.
	roVCfg, roErr := ctfe.ValidateLogConfig(roCfgs[0])
	t.Check("readonly-config-validates",
		roErr == nil && roVCfg != nil && roVCfg.Config.IsReadonly, "err=%v", roErr)

	// 4. It is a real proto field, not a text-only alias: the configuration
	//    round-trips through the binary wire format, which the loader also
	//    accepts.
	bin, err := proto.Marshal(&configpb.LogConfigSet{Config: roCfgs})
	if err != nil {
		t.Fatalf("marshal LogConfigSet: %v", err)
	}
	binPath := filepath.Join(os.Getenv("CT_SCRATCH"), "readonly.pb")
	if err := os.WriteFile(binPath, bin, 0o644); err != nil {
		t.Fatalf("write binary config: %v", err)
	}
	binCfgs, binErr := ctfe.LogConfigFromFile(binPath)
	t.Check("readonly-survives-binary-encoding",
		binErr == nil && len(binCfgs) == 1 && binCfgs[0].IsReadonly,
		"err=%v cfgs=%v", binErr, binCfgs)

	// ---- the handler sets --------------------------------------------------

	backend := newStubBackend(vhelp.ReadFixture("minted", "leaf-valid.der"))
	client, stopBackend := serveBackend(t, backend)
	defer stopBackend()

	// 5. A read-only instance registers the six read endpoints and no others.
	roInst := setUp(t, roCfgs[0], client)
	t.Equal("readonly-handler-set", handlerPaths(roInst), readPaths)

	// 6. A writable instance registers all eight, so the six above are a
	//    subtraction and not merely everything the CTFE knows how to serve.
	rwInst := setUp(t, rwCfgs[0], client)
	t.Equal("writable-handler-set", handlerPaths(rwInst), writePaths+readPaths)

	// 7. A mirror still drops the same two endpoints, so is_readonly is an
	//    addition to is_mirror rather than a rename of it.
	mInst := setUp(t, mirrorConfig(t), client)
	t.Equal("mirror-handler-set", handlerPaths(mInst), readPaths)

	// ---- the read-only log, served, with a live backend --------------------

	roURL, stopRO := serve(t, roInst)
	defer stopRO()
	rwURL, stopRW := serve(t, rwInst)
	defer stopRW()

	// 8/9/10. get-sth is answered from the backend's pinned log root and
	//         signed with the log's own configured key.
	code, body := do(t, http.MethodGet, roURL+"/bench/ct/v1/get-sth", "")
	t.Check("readonly-get-sth-status", code == http.StatusOK,
		"status=%d body=%s", code, oneLine(body))

	var sthRsp ct.GetSTHResponse
	if err := json.Unmarshal([]byte(body), &sthRsp); err != nil {
		t.Fatalf("decode get-sth body %q: %v", body, err)
	}
	t.Equal("readonly-get-sth-body",
		fmt.Sprintf("size=%d ts=%d root=%x", sthRsp.TreeSize, sthRsp.Timestamp, sthRsp.SHA256RootHash),
		fmt.Sprintf("size=%d ts=%d root=%x", pinnedTreeSize, pinnedTimestampMS, pinnedRootHash[:]))

	sth, err := sthRsp.ToSignedTreeHead()
	if err != nil {
		t.Fatalf("ToSignedTreeHead: %v", err)
	}
	verifier, err := ct.NewSignatureVerifier(logPublicKey(t))
	if err != nil {
		t.Fatalf("NewSignatureVerifier: %v", err)
	}
	t.Case("readonly-get-sth-signature-verifies", verifier.VerifySTHSignature(*sth))

	// 11/12. get-entries is answered from the backend's pinned leaves, and
	//        the range the CTFE asked the backend for is the one requested.
	code, body = do(t, http.MethodGet, roURL+"/bench/ct/v1/get-entries?start=0&end=1", "")
	t.Check("readonly-get-entries-status", code == http.StatusOK,
		"status=%d body=%s", code, oneLine(body))

	var entRsp struct {
		Entries []struct {
			LeafInput []byte `json:"leaf_input"`
			ExtraData []byte `json:"extra_data"`
		} `json:"entries"`
	}
	if err := json.Unmarshal([]byte(body), &entRsp); err != nil {
		t.Fatalf("decode get-entries body %q: %v", body, err)
	}
	leafDER := vhelp.ReadFixture("minted", "leaf-valid.der")
	gotEntries := make([]string, 0, len(entRsp.Entries))
	for _, e := range entRsp.Entries {
		gotEntries = append(gotEntries,
			base64.StdEncoding.EncodeToString(e.LeafInput)+"/"+
				base64.StdEncoding.EncodeToString(e.ExtraData))
	}
	wantEntries := []string{
		base64.StdEncoding.EncodeToString(pinnedLeafValue(leafDER, 0)) + "/" +
			base64.StdEncoding.EncodeToString(pinnedExtraData),
		base64.StdEncoding.EncodeToString(pinnedLeafValue(leafDER, 1)) + "/" +
			base64.StdEncoding.EncodeToString(pinnedExtraData),
	}
	gotStart, gotCount := backend.rangeArgs()
	t.Equal("readonly-get-entries-body",
		fmt.Sprintf("[%d,%d) %s", gotStart, gotCount, strings.Join(gotEntries, " ")),
		fmt.Sprintf("[%d,%d) %s", 0, 2, strings.Join(wantEntries, " ")))

	// 13/14. get-roots is answered out of the configured roots file, in file
	//        order, as raw DER; json.Encoder terminates the object with "\n".
	code, body = do(t, http.MethodGet, roURL+"/bench/ct/v1/get-roots", "")
	t.Check("readonly-get-roots-status", code == http.StatusOK,
		"status=%d body=%s", code, oneLine(body))
	var rootsRsp struct {
		Certificates [][]byte `json:"certificates"`
	}
	if err := json.Unmarshal([]byte(body), &rootsRsp); err != nil {
		t.Fatalf("decode get-roots body %q: %v", body, err)
	}
	t.Equal("readonly-get-roots-body",
		derHex(rootsRsp.Certificates),
		derHex(pemDERs(t, vhelp.Fixture("minted", "roots-bench-plus-int.pem"))))

	// ---- and what the read-only log refuses --------------------------------

	// 15/16. add-chain and add-pre-chain are not routed at all: the mux, not
	//        the handler, answers, and it answers on the very same server that
	//        just served three read endpoints correctly.
	code, body = do(t, http.MethodPost, roURL+"/bench/ct/v1/add-chain", `{"chain":["`+pinnedB64Leaf+`"]}`)
	t.Check("readonly-add-chain-unrouted",
		code == http.StatusNotFound,
		"status=%d body=%q", code, body)

	code, body = do(t, http.MethodPost, roURL+"/bench/ct/v1/add-pre-chain", `{"chain":["`+pinnedB64Leaf+`"]}`)
	t.Check("readonly-add-pre-chain-unrouted",
		code == http.StatusNotFound,
		"status=%d body=%q", code, body)

	// 17. That 404 really means "not registered": an endpoint that IS
	//     registered answers a wrong method with a 405 of its own, and names
	//     the method it turned down. Only that identifier is matched -- the
	//     sentence around it is the product's wording and TEST_INTERFACE.md
	//     section 3.3 does not grade wording.
	code, body = do(t, http.MethodPost, roURL+"/bench/ct/v1/get-roots", "{}")
	t.Check("readonly-wrong-method-is-405",
		code == http.StatusMethodNotAllowed && strings.Contains(body, "POST"),
		"status=%d body=%q", code, body)

	// 18. The writable instance, built from the same template with the single
	//     field flipped, does route add-chain: it reaches the admission path
	//     and is refused there on the chain's own merits, with the documented
	//     body. So the 404 above is caused by is_readonly and nothing else.
	code, body = do(t, http.MethodPost, rwURL+"/bench/ct/v1/add-chain", `{"chain":[]}`)
	t.Check("writable-add-chain-is-routed",
		code == http.StatusBadRequest &&
			body == "Bad Request\nbench{1001}: failed to parse add-chain body: cert chain was empty\n",
		"status=%d body=%q", code, body)

	// 19. Across everything this group did -- including two POSTs to a
	//     read-only log's add-chain and one to a writable log's -- the backend
	//     was never asked to queue a leaf, while the reads did reach it.
	t.Equal("backend-served-reads-and-queued-nothing",
		fmt.Sprintf("queue=%d range=%d root>=1:%t",
			backend.count("QueueLeaf"), backend.count("GetLeavesByRange"),
			backend.count("GetLatestSignedLogRoot") >= 1),
		"queue=0 range=1 root>=1:true")

	t.Done()
}

// ---------------------------------------------------------------------------
// Helpers. None of them is product code; they observe from outside.
// ---------------------------------------------------------------------------

func writeConfig(t *vhelp.T, name, readonly string) string {
	body := strings.NewReplacer(
		"@ROOTS@", vhelp.Fixture("minted", "roots-bench-plus-int.pem"),
		"@READONLY@", readonly,
		"@KEYDER@", protoEscape(pemBody(t, vhelp.ReadFixture("minted", "keys", "log-key.pem"))),
	).Replace(configTemplate)
	p := filepath.Join(os.Getenv("CT_SCRATCH"), name)
	if err := os.WriteFile(p, []byte(body), 0o600); err != nil {
		t.Fatalf("write config %s: %v", name, err)
	}
	return p
}

// mirrorConfig builds the mirror variant in memory: a mirror must carry a
// public key and must not carry a private one.
func mirrorConfig(t *vhelp.T) *configpb.LogConfig {
	return &configpb.LogConfig{
		LogId:     pinnedLogID,
		Prefix:    logPrefix,
		IsMirror:  true,
		PublicKey: &keyspb.PublicKey{Der: pemBody(t, vhelp.ReadFixture("minted", "keys", "log-pubkey.pem"))},
	}
}

func setUp(t *vhelp.T, cfg *configpb.LogConfig, client trillian.TrillianLogClient) *ctfe.Instance {
	vCfg, err := ctfe.ValidateLogConfig(cfg)
	if err != nil {
		t.Fatalf("ValidateLogConfig(mirror=%t, readonly=%t): %v", cfg.IsMirror, cfg.IsReadonly, err)
	}
	inst, err := ctfe.SetUpInstance(context.Background(), ctfe.InstanceOptions{
		Validated:     vCfg,
		Client:        client,
		Deadline:      30 * time.Second,
		MetricFactory: monitoring.InertMetricFactory{},
		RequestLog:    &ctfe.DefaultRequestLog{},
	})
	if err != nil {
		t.Fatalf("SetUpInstance: %v", err)
	}
	return inst
}

// handlerPaths renders an instance's registered endpoints, sorted.
func handlerPaths(inst *ctfe.Instance) string {
	out := make([]string, 0, len(inst.Handlers))
	for p := range inst.Handlers {
		out = append(out, p)
	}
	sort.Strings(out)
	return strings.Join(out, " ")
}

// serve mounts an instance's handlers on a loopback socket bound to :0.
func serve(t *vhelp.T, inst *ctfe.Instance) (string, func()) {
	mux := http.NewServeMux()
	for path, handler := range inst.Handlers {
		mux.Handle(path, handler)
	}
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("listen: %v", err)
	}
	srv := &http.Server{Handler: mux}
	go func() { _ = srv.Serve(ln) }()
	return "http://" + ln.Addr().String(), func() {
		ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
		defer cancel()
		_ = srv.Shutdown(ctx)
		_ = ln.Close()
	}
}

func do(t *vhelp.T, method, url, body string) (int, string) {
	req, err := http.NewRequest(method, url, strings.NewReader(body))
	if err != nil {
		t.Fatalf("new request %s %s: %v", method, url, err)
	}
	if body != "" {
		req.Header.Set("Content-Type", "application/json")
	}
	rsp, err := (&http.Client{Timeout: 60 * time.Second}).Do(req)
	if err != nil {
		t.Fatalf("%s %s: %v", method, url, err)
	}
	defer func() { _ = rsp.Body.Close() }()
	b, err := io.ReadAll(rsp.Body)
	if err != nil {
		t.Fatalf("read body of %s %s: %v", method, url, err)
	}
	return rsp.StatusCode, string(b)
}

// logPublicKey parses the pinned log public key with the standard library, so
// the STH signature check does not lean on the product to verify itself.
func logPublicKey(t *vhelp.T) interface{} {
	pk, err := x509.ParsePKIXPublicKey(pemBody(t, vhelp.ReadFixture("minted", "keys", "log-pubkey.pem")))
	if err != nil {
		t.Fatalf("parse log public key: %v", err)
	}
	return pk
}

// pemDERs returns the DER of every CERTIFICATE block in a PEM file, in file
// order. The get-roots expectation is therefore the benchmark's own reading of
// the pinned roots file, not the product's.
func pemDERs(t *vhelp.T, path string) [][]byte {
	raw, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("read %s: %v", path, err)
	}
	var out [][]byte
	for {
		var blk *pem.Block
		blk, raw = pem.Decode(raw)
		if blk == nil {
			break
		}
		if blk.Type == "CERTIFICATE" {
			out = append(out, blk.Bytes)
		}
	}
	if len(out) == 0 {
		t.Fatalf("no certificates in %s", path)
	}
	return out
}

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

func derHex(in [][]byte) string {
	out := make([]string, 0, len(in))
	for _, d := range in {
		out = append(out, fmt.Sprintf("%x", d))
	}
	return strings.Join(out, ",")
}

func oneLine(s string) string {
	return strings.ReplaceAll(strings.TrimSpace(s), "\n", " / ")
}

// pinnedB64Leaf is a fixed, well-formed base64 blob. Its content never
// matters: a read-only log must refuse the request before looking at it.
const pinnedB64Leaf = "MIIBAA=="
