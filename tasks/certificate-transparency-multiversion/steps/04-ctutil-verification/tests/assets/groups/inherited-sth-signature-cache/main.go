// Group inherited-sth-signature-cache -- KIND gate.
//
// Step 03's milestone, re-run here so that this step cannot regress it. The
// v1.0.7 advisory says the personality's STH signature cache must be keyed on
// the whole RFC 6962 signature input: no two distinct signing inputs may ever
// share a signature, while an unchanged input must be served from the cache
// rather than re-signed. Harbor never re-runs an earlier step's verifier, so
// "it was graded at step 03" is not coverage; this group is that coverage.
//
// Why it is a GATE group. The cache is inherited plumbing. It earns nothing
// here and it is not on this step's feature list, but this step's required
// repair lands in trillian/ctfe/serialize.go -- the very file that holds
// signV1TreeHead and its two cache calls -- so a careless repair can delete
// the cache without any scored group noticing.
//
// Why it is built through ctfe.SetUpInstance and not ctfe.NewLogContext. The
// signer parameter of NewLogContext changes shape at exactly this step: the
// predecessor takes *trillian/crypto.Signer, this release takes a stdlib
// crypto.Signer. A driver that names either one compiles against one tree and
// not the other. SetUpInstance has the same signature at both, takes the same
// InstanceOptions at both, and loads the signing key itself out of the config,
// so no signer type crosses the driver boundary at all.
//
// Nothing is started and nothing listens: the backend below is a driver-local
// value implementing trillian.TrillianLogClient, not a TrillianLogServer, and
// the handler taken out of the returned PathHandlers is driven with an
// httptest recorder standing in for the socket.
//
// Determinism. No wall clock is read for any verdict. The STH's timestamp and
// tree size come from the pinned root this driver hands back, not from the
// time source SetUpInstance installs. The root hashes are SHA-256 of pinned
// strings. ECDSA signatures are randomised, which is what makes the cache
// observable: an uncached re-sign of an unchanged input is different with
// overwhelming probability, and a cached one is byte-identical.
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
	"os"
	"path/filepath"
	"strings"
	"time"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/trillian/ctfe"
	"github.com/google/trillian"
	"github.com/google/trillian/monitoring"
	"google.golang.org/grpc"

	// Registers the keyspb.PEMKeyFile handler that SetUpInstance needs to turn
	// the config's private_key into a signer. ct_server imports it for the
	// same reason.
	_ "github.com/google/trillian/crypto/keys/pem/proto"

	"ctverify/vhelp"
)

// Pinned constants. SCOPE-TIME: no wall clock is read anywhere in this file.
const (
	logPrefix      = "cachegate" // must match logconfig.textproto.in
	pinnedNanos    = int64(1500000000000000000)
	pinnedMillis   = uint64(1500000000000)
	pinnedTreeSize = int64(100000)
)

// backend is a driver-local value implementing trillian.TrillianLogClient. The
// interface is embedded so that a method set that differs between releases
// still compiles; only the one entrypoint get-sth reaches is declared.
type backend struct {
	trillian.TrillianLogClient
	root *trillian.SignedLogRoot
}

func (b *backend) GetLatestSignedLogRoot(ctx context.Context, in *trillian.GetLatestSignedLogRootRequest, opts ...grpc.CallOption) (*trillian.GetLatestSignedLogRootResponse, error) {
	return &trillian.GetLatestSignedLogRootResponse{SignedLogRoot: b.root}, nil
}

// setRoot replaces the pinned root the driver-local client returns. The LogId
// field is deliberately not set: it is absent from trillian.SignedLogRoot at
// this step's pin, and nothing on the get-sth path reads it at either.
func (b *backend) setRoot(nanos, size int64, hash []byte) {
	b.root = &trillian.SignedLogRoot{
		TimestampNanos: nanos,
		TreeSize:       size,
		RootHash:       hash,
	}
}

func rootDigest(tag string) []byte {
	h := sha256.Sum256([]byte("harbor-ct-bench/" + tag))
	return h[:]
}

// flipLastBit returns a copy of b differing from it in exactly one bit. It is
// the smallest possible change to a signing input.
func flipLastBit(b []byte) []byte {
	out := append([]byte(nil), b...)
	out[len(out)-1] ^= 0x01
	return out
}

type sthJSON struct {
	TreeSize          uint64 `json:"tree_size"`
	Timestamp         uint64 `json:"timestamp"`
	SHA256RootHash    []byte `json:"sha256_root_hash"`
	TreeHeadSignature []byte `json:"tree_head_signature"`
}

type harness struct {
	be *backend
	ap ctfe.AppHandler
}

// newHarness writes the group's pinned log configuration with the fixture root
// substituted in, hands it to the release's own config loader, and asks
// SetUpInstance for the handler set.
func newHarness(t *vhelp.T, be *backend) *harness {
	wd, err := os.Getwd()
	if err != nil {
		t.Fatalf("Getwd: %v", err)
	}
	tmpl, err := os.ReadFile(vhelp.GroupFile("logconfig.textproto.in"))
	if err != nil {
		t.Fatalf("group logconfig.textproto.in: %v", err)
	}
	cfgPath := filepath.Join(wd, "logs.textproto")
	body := strings.Replace(string(tmpl), "@FIXTURES@", vhelp.Fixture(), -1)
	if err := os.WriteFile(cfgPath, []byte(body), 0644); err != nil {
		t.Fatalf("write config: %v", err)
	}

	cfgs, err := ctfe.LogConfigFromFile(cfgPath)
	if err != nil {
		t.Fatalf("LogConfigFromFile: %v", err)
	}
	if len(cfgs) != 1 {
		t.Fatalf("pinned config holds %d logs, want 1", len(cfgs))
	}

	// RequestLog is the release's own no-op implementation: AppHandler.ServeHTTP
	// calls Start/LogPrefix/Status on it around every request, so an in-process
	// driver that goes through the wrapper has to supply one. Nothing here
	// observes it.
	opts := ctfe.InstanceOptions{
		Deadline:      10 * time.Second,
		MetricFactory: monitoring.InertMetricFactory{},
		RequestLog:    new(ctfe.DefaultRequestLog),
	}
	hs, err := ctfe.SetUpInstance(context.Background(), be, cfgs[0], opts)
	if err != nil {
		t.Fatalf("SetUpInstance: %v", err)
	}
	ap, ok := (*hs)["/"+logPrefix+ct.GetSTHPath]
	if !ok {
		t.Fatalf("no handler registered at /%s%s", logPrefix, ct.GetSTHPath)
	}
	return &harness{be: be, ap: ap}
}

// getSTH drives the personality's get-sth entrypoint through the AppHandler
// wrapper, with a recorder standing in for the socket. There is no server and
// no port.
func (h *harness) getSTH() (*sthJSON, error) {
	rec := httptest.NewRecorder()
	req := httptest.NewRequest(http.MethodGet, "/"+logPrefix+ct.GetSTHPath, nil)
	h.ap.ServeHTTP(rec, req)
	if rec.Code != http.StatusOK {
		return nil, fmt.Errorf("get-sth returned HTTP %d: %s", rec.Code, rec.Body.String())
	}
	out := &sthJSON{}
	if err := json.Unmarshal(rec.Body.Bytes(), out); err != nil {
		return nil, fmt.Errorf("get-sth body is not the documented JSON object: %v (%q)", err, rec.Body.String())
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
func checkSTH(pub *ecdsa.PublicKey, r *sthJSON, wantTreeSize, wantTimestamp uint64, wantRoot []byte) error {
	if r.TreeSize != wantTreeSize {
		return fmt.Errorf("tree_size %d, want %d", r.TreeSize, wantTreeSize)
	}
	if r.Timestamp != wantTimestamp {
		return fmt.Errorf("timestamp %d, want %d", r.Timestamp, wantTimestamp)
	}
	if !sameBytes(r.SHA256RootHash, wantRoot) {
		return fmt.Errorf("sha256_root_hash %x, want %x", r.SHA256RootHash, wantRoot)
	}
	hashAlg, sigAlg, sig, err := splitDigitallySigned(r.TreeHeadSignature)
	if err != nil {
		return err
	}
	if hashAlg != 4 || sigAlg != 3 {
		return fmt.Errorf("tree_head_signature algorithms are (%d,%d), want (4,3) = sha256/ecdsa", hashAlg, sigAlg)
	}
	return verifyECDSA(pub, sthSignatureInput(wantTimestamp, wantTreeSize, wantRoot), sig)
}

func sameBytes(a, b []byte) bool {
	return fmt.Sprintf("%x", a) == fmt.Sprintf("%x", b)
}

// loadPublicKey reads the public half of the key the pinned config names.
func loadPublicKey(t *vhelp.T) *ecdsa.PublicKey {
	blk, _ := pem.Decode(vhelp.ReadFixture("keys", "ct-http-server.pubkey.pem"))
	if blk == nil {
		t.Fatalf("keys/ct-http-server.pubkey.pem is not PEM")
	}
	k, err := stdx509.ParsePKIXPublicKey(blk.Bytes)
	if err != nil {
		t.Fatalf("keys/ct-http-server.pubkey.pem: %v", err)
	}
	ek, ok := k.(*ecdsa.PublicKey)
	if !ok {
		t.Fatalf("keys/ct-http-server.pubkey.pem is not an EC key")
	}
	return ek
}

func main() {
	t := vhelp.New()
	pub := loadPublicKey(t)

	rootA := rootDigest("inherited-sth-cache")
	be := &backend{}
	be.setRoot(pinnedNanos, pinnedTreeSize, rootA)
	h := newHarness(t, be)

	r1, err := h.getSTH()
	if err != nil {
		t.Fatalf("baseline get-sth: %v", err)
	}
	t.Case("baseline-verifies", checkSTH(pub, r1, uint64(pinnedTreeSize), pinnedMillis, rootA))
	sigA := r1.TreeHeadSignature

	// 1. tree size changes by one; everything else is identical.
	sizeB := pinnedTreeSize + 1
	h.be.setRoot(pinnedNanos, sizeB, rootA)
	r2, err := h.getSTH()
	if err != nil {
		t.Fatalf("tree-size get-sth: %v", err)
	}
	sigB := r2.TreeHeadSignature
	t.Check("tree-size-change-resigns", !sameBytes(sigB, sigA),
		"a one-entry tree-size change was served the previous signature %x", sigA)
	t.Case("tree-size-change-verifies", checkSTH(pub, r2, uint64(sizeB), pinnedMillis, rootA))

	// 2. exactly one bit of the root hash changes.
	rootC := flipLastBit(rootA)
	h.be.setRoot(pinnedNanos, sizeB, rootC)
	r3, err := h.getSTH()
	if err != nil {
		t.Fatalf("root-hash get-sth: %v", err)
	}
	sigC := r3.TreeHeadSignature
	t.Check("root-hash-one-bit-change-resigns", !sameBytes(sigC, sigB),
		"a one-bit root-hash change was served the previous signature %x", sigB)
	t.Case("root-hash-one-bit-change-verifies", checkSTH(pub, r3, uint64(sizeB), pinnedMillis, rootC))

	// 3. the timestamp changes by one millisecond.
	nanosD := pinnedNanos + 1000000
	millisD := pinnedMillis + 1
	h.be.setRoot(nanosD, sizeB, rootC)
	r4, err := h.getSTH()
	if err != nil {
		t.Fatalf("timestamp get-sth: %v", err)
	}
	sigD := r4.TreeHeadSignature
	t.Check("timestamp-change-resigns", !sameBytes(sigD, sigC),
		"a one-millisecond timestamp change was served the previous signature %x", sigC)
	t.Case("timestamp-change-verifies", checkSTH(pub, r4, uint64(sizeB), millisD, rootC))

	// 4. nothing changes: the cache must now serve the last signature.
	r5, err := h.getSTH()
	if err != nil {
		t.Fatalf("repeat get-sth: %v", err)
	}
	t.Check("unchanged-input-served-from-cache", sameBytes(r5.TreeHeadSignature, sigD),
		"an unchanged signing input produced a second, different signature %x (previous %x)",
		r5.TreeHeadSignature, sigD)

	t.Done()
}
