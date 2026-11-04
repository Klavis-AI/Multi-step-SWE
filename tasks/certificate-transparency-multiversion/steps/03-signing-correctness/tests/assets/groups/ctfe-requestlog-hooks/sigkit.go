// Shared, benchmark-owned scaffolding for the STH signature-cache groups.
//
// This file is copied verbatim into every group directory that needs it, so
// each group stays exactly one compilation unit and one process.
//
// Nothing here uses the product to decide a verdict: the RFC 6962 STH
// signature input, the TLS DigitallySigned framing and the ECDSA verification
// are all recomputed from the specification by this file, and the key comes
// from the pinned corpus.
package main

import (
	"context"
	"crypto/ecdsa"
	"crypto/sha256"
	stdx509 "crypto/x509"
	"encoding/asn1"
	"encoding/binary"
	"encoding/json"
	"encoding/pem"
	"fmt"
	"math/big"
	"net/http"
	"net/http/httptest"
	"time"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/trillian/ctfe"
	"github.com/google/certificate-transparency-go/trillian/util"
	"github.com/google/trillian"
	tcrypto "github.com/google/trillian/crypto"
	"github.com/google/trillian/monitoring"
	"google.golang.org/grpc"

	"ctverify/vhelp"
)

// Pinned constants. SCOPE-TIME: no wall clock is read anywhere in this file.
const (
	pinnedLogID    = int64(0x43544631)
	pinnedNanos    = int64(1500000000000000000) // 2017-07-14T02:40:00Z
	pinnedMillis   = uint64(1500000000000)
	pinnedTreeSize = int64(100000)
)

var pinnedClock = time.Unix(1500000000, 0).UTC()

// fixedRoot builds a pinned trillian.SignedLogRoot. The root carries no
// signature: the CT personality signs the STH with its own configured key and
// never verifies the backend's root, so an unsigned root is sufficient.
func fixedRoot(nanos, size int64, tag string) *trillian.SignedLogRoot {
	h := sha256.Sum256([]byte("harbor-ct-bench/" + tag))
	return &trillian.SignedLogRoot{
		TimestampNanos: nanos,
		TreeSize:       size,
		RootHash:       h[:],
		LogId:          pinnedLogID,
	}
}

func rootDigest(tag string) []byte {
	h := sha256.Sum256([]byte("harbor-ct-bench/" + tag))
	return h[:]
}

// backend is a driver-local value implementing the trillian.TrillianLogClient
// interface. It is not a server: nothing is started and nothing listens. It
// exists because ctfe.NewLogContext, the only exported constructor of the
// personality's signing path, takes a client.
type backend struct {
	trillian.TrillianLogClient // embedded; any unused method panics if reached
	root                       *trillian.SignedLogRoot
}

func (b *backend) GetLatestSignedLogRoot(ctx context.Context, in *trillian.GetLatestSignedLogRootRequest, opts ...grpc.CallOption) (*trillian.GetLatestSignedLogRootResponse, error) {
	return &trillian.GetLatestSignedLogRootResponse{SignedLogRoot: b.root}, nil
}

// The read entrypoints this file drives are exercised only for the hooks the
// personality reports before it calls the backend, so these four refuse. They
// are declared rather than left to the embedded nil interface because a refusal
// lets the entrypoint return a status, while a nil call would abort the
// process before ServeHTTP could report one. GetConsistencyProof is
// deliberately NOT declared: a group that asserts first=0 needs no backend
// round trip is only meaningful if reaching the backend is fatal.
var errNoSuchData = fmt.Errorf("driver-local backend serves only GetLatestSignedLogRoot")

func (b *backend) GetLeavesByIndex(ctx context.Context, in *trillian.GetLeavesByIndexRequest, opts ...grpc.CallOption) (*trillian.GetLeavesByIndexResponse, error) {
	return nil, errNoSuchData
}

func (b *backend) GetLeavesByRange(ctx context.Context, in *trillian.GetLeavesByRangeRequest, opts ...grpc.CallOption) (*trillian.GetLeavesByRangeResponse, error) {
	return nil, errNoSuchData
}

func (b *backend) GetInclusionProofByHash(ctx context.Context, in *trillian.GetInclusionProofByHashRequest, opts ...grpc.CallOption) (*trillian.GetInclusionProofByHashResponse, error) {
	return nil, errNoSuchData
}

func (b *backend) GetEntryAndProof(ctx context.Context, in *trillian.GetEntryAndProofRequest, opts ...grpc.CallOption) (*trillian.GetEntryAndProofResponse, error) {
	return nil, errNoSuchData
}

type harness struct {
	be     *backend
	ap     ctfe.AppHandler
	prefix string
}

func newHarness(t *vhelp.T, prefix string, root *trillian.SignedLogRoot) *harness {
	priv := loadPrivateKey(t)
	be := &backend{root: root}
	// RequestLog is the release's own no-op implementation: AppHandler.ServeHTTP
	// calls Start/LogPrefix/Status on it around every request, so an in-process
	// driver that goes through the wrapper has to supply one. TEST_INTERFACE.md
	// §5 names DefaultRequestLog and says it is usable as new(DefaultRequestLog);
	// nothing here observes it.
	opts := ctfe.InstanceOptions{
		Deadline:      10 * time.Second,
		MetricFactory: monitoring.InertMetricFactory{},
		RequestLog:    new(ctfe.DefaultRequestLog),
	}
	lc := ctfe.NewLogContext(pinnedLogID, prefix, ctfe.CertValidationOpts{}, be,
		tcrypto.NewSHA256Signer(priv), opts, util.NewFixedTimeSource(pinnedClock))
	hs := lc.Handlers(prefix)
	ap, ok := hs["/"+prefix+ct.GetSTHPath]
	if !ok {
		t.Fatalf("no handler registered at /%s%s", prefix, ct.GetSTHPath)
	}
	return &harness{be: be, ap: ap, prefix: prefix}
}

type sthResponse struct {
	status int
	hdr    http.Header
	body   []byte
	fields map[string]json.RawMessage
	sth    sthJSON
}

type sthJSON struct {
	TreeSize          uint64 `json:"tree_size"`
	Timestamp         uint64 `json:"timestamp"`
	SHA256RootHash    []byte `json:"sha256_root_hash"`
	TreeHeadSignature []byte `json:"tree_head_signature"`
}

// getSTH drives the personality's get-sth entrypoint the way TEST_INTERFACE.md
// §5 describes it: the AppHandler taken out of Handlers(prefix) is an
// http.Handler, and the driver calls ServeHTTP on it with a recorder standing
// in for the socket. There is no server and no port. Going through the wrapper
// rather than round it is what makes the response the driver reads -- status,
// headers and body -- the whole response an HTTP client would have seen.
func (h *harness) getSTH() (*sthResponse, error) {
	rec := httptest.NewRecorder()
	req := httptest.NewRequest(http.MethodGet, "/"+h.prefix+ct.GetSTHPath, nil)
	h.ap.ServeHTTP(rec, req)
	if rec.Code != http.StatusOK {
		return nil, fmt.Errorf("get-sth returned HTTP %d: %s", rec.Code, string(rec.Body.Bytes()))
	}
	out := &sthResponse{status: rec.Code, hdr: rec.Header(), body: rec.Body.Bytes()}
	if err := json.Unmarshal(out.body, &out.sth); err != nil {
		return nil, fmt.Errorf("get-sth body is not the documented JSON object: %v (%q)", err, string(out.body))
	}
	if err := json.Unmarshal(out.body, &out.fields); err != nil {
		return nil, fmt.Errorf("get-sth body is not a JSON object: %v", err)
	}
	return out, nil
}

// sthSignatureInput rebuilds RFC 6962 section 3.5's tree head signature input:
//
//	Version version (1) || SignatureType signature_type (1) ||
//	uint64 timestamp (8) || uint64 tree_size (8) || opaque sha256_root_hash[32]
func sthSignatureInput(timestamp, treeSize uint64, rootHash []byte) []byte {
	b := make([]byte, 0, 50)
	b = append(b, 0x00) // v1
	b = append(b, 0x01) // tree_hash
	var u [8]byte
	binary.BigEndian.PutUint64(u[:], timestamp)
	b = append(b, u[:]...)
	binary.BigEndian.PutUint64(u[:], treeSize)
	b = append(b, u[:]...)
	return append(b, rootHash...)
}

// splitDigitallySigned decodes RFC 5246 digitally-signed framing as RFC 6962
// uses it: hash algorithm (1) || signature algorithm (1) || uint16 length ||
// signature.
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
		return fmt.Errorf("ECDSA verification failed over the RFC 6962 signature input")
	}
	return nil
}

// checkSTH verifies one get-sth response against the pinned backend root in
// full: field values, digitally-signed framing and the signature itself.
func checkSTH(pub *ecdsa.PublicKey, r *sthResponse, wantTreeSize uint64, wantTimestamp uint64, wantRoot []byte) error {
	if r.status != http.StatusOK {
		return fmt.Errorf("status %d, want 200", r.status)
	}
	if r.sth.TreeSize != wantTreeSize {
		return fmt.Errorf("tree_size %d, want %d", r.sth.TreeSize, wantTreeSize)
	}
	if r.sth.Timestamp != wantTimestamp {
		return fmt.Errorf("timestamp %d, want %d", r.sth.Timestamp, wantTimestamp)
	}
	if fmt.Sprintf("%x", r.sth.SHA256RootHash) != fmt.Sprintf("%x", wantRoot) {
		return fmt.Errorf("sha256_root_hash %x, want %x", r.sth.SHA256RootHash, wantRoot)
	}
	hashAlg, sigAlg, sig, err := splitDigitallySigned(r.sth.TreeHeadSignature)
	if err != nil {
		return err
	}
	if hashAlg != 4 || sigAlg != 3 {
		return fmt.Errorf("tree_head_signature algorithms are (%d,%d), want (4,3) = sha256/ecdsa", hashAlg, sigAlg)
	}
	return verifyECDSA(pub, sthSignatureInput(wantTimestamp, wantTreeSize, wantRoot), sig)
}

func loadPrivateKey(t *vhelp.T) *ecdsa.PrivateKey {
	blk, _ := pem.Decode(vhelp.ReadFixture("minted", "keys", "log-key.pem"))
	if blk == nil {
		t.Fatalf("minted/keys/log-key.pem is not PEM")
	}
	k, err := stdx509.ParsePKCS8PrivateKey(blk.Bytes)
	if err != nil {
		t.Fatalf("minted/keys/log-key.pem: %v", err)
	}
	ek, ok := k.(*ecdsa.PrivateKey)
	if !ok {
		t.Fatalf("minted/keys/log-key.pem is not an EC key")
	}
	return ek
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

func sameBytes(a, b []byte) bool {
	return fmt.Sprintf("%x", a) == fmt.Sprintf("%x", b)
}

// setRoot replaces the pinned root the driver-local client returns.
func (h *harness) setRoot(nanos, size int64, hash []byte) {
	h.be.root = &trillian.SignedLogRoot{
		TimestampNanos: nanos,
		TreeSize:       size,
		RootHash:       hash,
		LogId:          pinnedLogID,
	}
}

// flipLastBit returns a copy of b differing from it in exactly one bit. It is
// the smallest possible change to a signing input.
func flipLastBit(b []byte) []byte {
	out := append([]byte(nil), b...)
	out[len(out)-1] ^= 0x01
	return out
}

// jsonRaw lets a group name an undecoded JSON value without importing
// encoding/json itself. It is declared here, in the shared kit, so that every
// copy of this file stays byte-identical to steps/03/groups/_sigkit.
type jsonRaw = json.RawMessage
