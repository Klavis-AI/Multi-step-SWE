// witnesskit.go is copied verbatim into every step-10 group that drives the
// candidate's own witness binary. It is not a group: it holds no cases and
// prints nothing. Each copy is compiled into exactly one group's single
// compilation unit, so a group still owns one process tree and one binary.
//
// The witness's own packages live under internal/, so no external module can
// link them: the binary is the only boundary at which this capability can be
// observed at all. The kit therefore starts the candidate's witness on a
// loopback port freeLoopbackPort picks from below the kernel's ephemeral
// range -- the witness takes its listen address as a flag and never reports
// the port it actually bound, so the number has to be chosen before the
// process that binds it exists -- with a sqlite file inside the group's
// private scratch directory, and speaks its HTTP API from outside.
//
// SCOPE-NET: loopback only. SCOPE-TIME: every tree, timestamp and key below is
// a pinned constant, and the Merkle arithmetic is the benchmark's own plain
// SHA-256, not the product's.
package main

import (
	"bytes"
	"crypto/ecdsa"
	"crypto/rand"
	"crypto/sha256"
	"crypto/x509"
	"encoding/base64"
	"encoding/json"
	"encoding/pem"
	"fmt"
	"io"
	"net"
	"net/http"
	"net/url"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/tls"

	"ctverify/vhelp"
)

// Pinned witness inputs. Nothing here reads a clock.
const (
	witnessTimestampMS = uint64(1500000000000) // 2017-07-14T02:40:00Z; named apart from
	// the backend stub's own pinned timestamp so the two kits compose.
	witnessAPILogs = "/ctwitness/v0/logs"
)

// ---------------------------------------------------------------------------
// The benchmark's own Merkle tree. Eight pinned leaves, RFC 6962 hashing done
// here in plain SHA-256 so that every root and every proof in a driver is the
// benchmark's arithmetic and not the product's.
// ---------------------------------------------------------------------------

func leafData(i int) []byte { return []byte("harbor-ct-bench-step10-witness-leaf-" + strconv.Itoa(i)) }

func leafHash(i int) []byte {
	h := sha256.Sum256(append([]byte{0x00}, leafData(i)...))
	return h[:]
}

func nodeHash(l, r []byte) []byte {
	h := sha256.New()
	h.Write([]byte{0x01})
	h.Write(l)
	h.Write(r)
	return h.Sum(nil)
}

// mth is the RFC 6962 Merkle Tree Hash of leaves [lo, hi).
func mth(lo, hi int) []byte {
	n := hi - lo
	if n == 1 {
		return leafHash(lo)
	}
	k := 1
	for k*2 < n {
		k *= 2
	}
	return nodeHash(mth(lo, lo+k), mth(lo+k, hi))
}

// root is the Merkle Tree Hash of the first n pinned leaves.
func root(n int) []byte { return mth(0, n) }

// consistency is RFC 6962 PROOF(m, D[n]) for the pinned tree.
func consistency(m, n int) [][]byte { return subProof(m, 0, n, true) }

func subProof(m, lo, hi int, complete bool) [][]byte {
	n := hi - lo
	if m == n {
		if complete {
			return nil
		}
		return [][]byte{mth(lo, hi)}
	}
	k := 1
	for k*2 < n {
		k *= 2
	}
	if m <= k {
		return append(subProof(m, lo, lo+k, complete), mth(lo+k, hi))
	}
	return append(subProof(m-k, lo+k, hi, false), mth(lo, lo+k))
}

// ---------------------------------------------------------------------------
// Pinned keys and STHs.
// ---------------------------------------------------------------------------

func pemBody(t *vhelp.T, b []byte) []byte {
	blk, _ := pem.Decode(b)
	if blk == nil {
		t.Fatalf("fixture is not PEM")
	}
	return blk.Bytes
}

func ecKey(t *vhelp.T, rel ...string) *ecdsa.PrivateKey {
	k, err := x509.ParsePKCS8PrivateKey(pemBody(t, vhelp.ReadFixture(rel...)))
	if err != nil {
		t.Fatalf("parse %v: %v", rel, err)
	}
	ec, ok := k.(*ecdsa.PrivateKey)
	if !ok {
		t.Fatalf("%v is %T, want *ecdsa.PrivateKey", rel, k)
	}
	return ec
}

// logIDFor is the witness's own definition: base64(SHA-256(PKIX DER)).
func logIDFor(pubDER []byte) string {
	h := sha256.Sum256(pubDER)
	return base64.StdEncoding.EncodeToString(h[:])
}

// signedSTH builds an RFC 6962 STH over the pinned tree at size n and signs it
// with key, using plain ECDSA over the TLS-encoded TreeHeadSignature. The
// signing is the benchmark's: only the serialisation is the product's pinned
// presentation-language codec.
func signedSTH(t *vhelp.T, key *ecdsa.PrivateKey, n int, ts uint64, rootHash []byte, logID string) ct.SignedTreeHead {
	var rh ct.SHA256Hash
	copy(rh[:], rootHash)

	input, err := tls.Marshal(ct.TreeHeadSignature{
		Version:        ct.V1,
		SignatureType:  ct.TreeHashSignatureType,
		Timestamp:      ts,
		TreeSize:       uint64(n),
		SHA256RootHash: rh,
	})
	if err != nil {
		t.Fatalf("marshal TreeHeadSignature: %v", err)
	}
	digest := sha256.Sum256(input)
	der, err := ecdsa.SignASN1(rand.Reader, key, digest[:])
	if err != nil {
		t.Fatalf("sign STH: %v", err)
	}

	sth := ct.SignedTreeHead{
		Version:        ct.V1,
		TreeSize:       uint64(n),
		Timestamp:      ts,
		SHA256RootHash: rh,
		TreeHeadSignature: ct.DigitallySigned{
			Algorithm: tls.SignatureAndHashAlgorithm{Hash: tls.SHA256, Signature: tls.ECDSA},
			Signature: der,
		},
	}
	if logID != "" {
		var id ct.SHA256Hash
		if err := id.FromBase64String(logID); err != nil {
			t.Fatalf("decode logID: %v", err)
		}
		sth.LogID = id
	}
	return sth
}

func sthJSON(t *vhelp.T, sth ct.SignedTreeHead) []byte {
	b, err := json.Marshal(sth)
	if err != nil {
		t.Fatalf("marshal STH: %v", err)
	}
	return b
}

// updateBody is the witness's api.UpdateRequest, restated here because the
// product's copy is in an internal package.
type updateBody struct {
	STH   []byte   `json:"STH"`
	Proof [][]byte `json:"Proof"`
}

// cosignedSTH is the witness's api.CosignedSTH, restated for the same reason.
type cosignedSTH struct {
	ct.SignedTreeHead
	WitnessSigs []ct.DigitallySigned `json:"witness_signatures"`
}

// ---------------------------------------------------------------------------
// Running the candidate's witness.
// ---------------------------------------------------------------------------

type witnessHarness struct {
	proc     *os.Process
	baseURL  string
	stderrTo string

	logKey    *ecdsa.PrivateKey
	logPubDER []byte
	logID     string

	witnessKey *ecdsa.PrivateKey
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

// startWitness boots the candidate's witness over a pinned configuration and
// waits for its log-list endpoint to answer. A failure here aborts the group
// rather than letting it report a vacuous pass.
func startWitness(t *vhelp.T) *witnessHarness {
	scratch := os.Getenv("CT_SCRATCH")

	logKey := ecKey(t, "minted", "keys", "log-key.pem")
	logPubDER := pemBody(t, vhelp.ReadFixture("minted", "keys", "log-pubkey.pem"))
	witnessKey := ecKey(t, "minted", "keys", "root-key.pem")
	witnessKeyPEM := vhelp.ReadFixture("minted", "keys", "root-key.pem")

	cfgPath := filepath.Join(scratch, "witness-config.yaml")
	cfg := "Logs:\n  - PubKey: " + base64.StdEncoding.EncodeToString(logPubDER) + "\n"
	if err := os.WriteFile(cfgPath, []byte(cfg), 0o600); err != nil {
		t.Fatalf("write witness config: %v", err)
	}

	addr := "127.0.0.1:" + strconv.Itoa(freeLoopbackPort(t))
	args := []string{
		"--listen=" + addr,
		"--db_file=" + filepath.Join(scratch, "witness-sths.db"),
		"--config_file=" + cfgPath,
		"--private_key=" + string(witnessKeyPEM),
		"--logtostderr",
	}

	errPath := filepath.Join(scratch, "witness.stderr")
	errFile, err := os.Create(errPath)
	if err != nil {
		t.Fatalf("create stderr sink: %v", err)
	}
	// vhelp.Command strips CT_VERIFY_NONCE from the child's environment, and
	// everything the child writes goes to a file, never to this group's
	// stdout, so nothing the candidate prints can be mistaken for a result.
	cmd := vhelp.Command(vhelp.CandidateBin("witness"), args...)
	cmd.Stdout = errFile
	cmd.Stderr = errFile
	cmd.Dir = scratch
	if err := cmd.Start(); err != nil {
		t.Fatalf("start witness: %v", err)
	}

	h := &witnessHarness{
		proc:       cmd.Process,
		baseURL:    "http://" + addr,
		stderrTo:   errPath,
		logKey:     logKey,
		logPubDER:  logPubDER,
		logID:      logIDFor(logPubDER),
		witnessKey: witnessKey,
	}
	// Done calls os.Exit, which does not run deferred functions, so a
	// `defer h.stop()` in a driver never fires and the child survives the
	// group. Register the teardown with the harness instead.
	t.Cleanup(h.stop)

	deadline := time.Now().Add(60 * time.Second)
	for {
		if time.Now().After(deadline) {
			tail, _ := os.ReadFile(errPath)
			h.stop()
			t.Fatalf("witness never answered %s; stderr tail: %s", witnessAPILogs, lastBytes(tail, 1200))
		}
		rsp, err := http.Get(h.baseURL + witnessAPILogs)
		if err == nil {
			_ = rsp.Body.Close()
			if rsp.StatusCode == http.StatusOK {
				return h
			}
		}
		if cmd.ProcessState != nil && cmd.ProcessState.Exited() {
			tail, _ := os.ReadFile(errPath)
			t.Fatalf("witness exited early; stderr tail: %s", lastBytes(tail, 1200))
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
func (h *witnessHarness) stop() {
	if h.proc != nil {
		_ = h.proc.Kill()
		_, _ = h.proc.Wait()
	}
}

func (h *witnessHarness) sthPath(logID string) string {
	return h.baseURL + "/ctwitness/v0/logs/" + url.PathEscape(logID) + "/sth"
}

func (h *witnessHarness) updatePath(logID string) string {
	return h.baseURL + "/ctwitness/v0/logs/" + url.PathEscape(logID) + "/update"
}

type rsp struct {
	status int
	body   string
}

func (h *witnessHarness) do(t *vhelp.T, method, u string, body []byte) rsp {
	req, err := http.NewRequest(method, u, bytes.NewReader(body))
	if err != nil {
		t.Fatalf("new request %s %s: %v", method, u, err)
	}
	r, err := (&http.Client{Timeout: 60 * time.Second}).Do(req)
	if err != nil {
		t.Fatalf("%s %s: %v", method, u, err)
	}
	defer func() { _ = r.Body.Close() }()
	b, err := io.ReadAll(r.Body)
	if err != nil {
		t.Fatalf("read body of %s %s: %v", method, u, err)
	}
	return rsp{status: r.StatusCode, body: string(b)}
}

// update PUTs an api.UpdateRequest for logID.
func (h *witnessHarness) update(t *vhelp.T, logID string, raw []byte, proof [][]byte) rsp {
	if proof == nil {
		proof = [][]byte{}
	}
	body, err := json.Marshal(updateBody{STH: raw, Proof: proof})
	if err != nil {
		t.Fatalf("marshal update request: %v", err)
	}
	return h.do(t, http.MethodPut, h.updatePath(logID), body)
}

// witnessSigVerifies reports whether the cosigned STH carries exactly one
// witness signature, made with SHA-256/ECDSA, over the TLS encoding of the
// STH itself, under the witness's pinned key.
func witnessSigVerifies(t *vhelp.T, c cosignedSTH, pub *ecdsa.PublicKey) error {
	if len(c.WitnessSigs) != 1 {
		return fmt.Errorf("got %d witness signatures, want 1", len(c.WitnessSigs))
	}
	sig := c.WitnessSigs[0]
	if sig.Algorithm.Hash != tls.SHA256 || sig.Algorithm.Signature != tls.ECDSA {
		return fmt.Errorf("witness signature algorithm is %v/%v, want SHA256/ECDSA",
			sig.Algorithm.Hash, sig.Algorithm.Signature)
	}
	input, err := tls.Marshal(c.SignedTreeHead)
	if err != nil {
		return fmt.Errorf("marshal cosigned STH body: %v", err)
	}
	digest := sha256.Sum256(input)
	if !ecdsa.VerifyASN1(pub, digest[:], sig.Signature) {
		return fmt.Errorf("witness signature does not verify over the TLS-encoded STH")
	}
	return nil
}

func decodeCosigned(t *vhelp.T, body string) cosignedSTH {
	var c cosignedSTH
	if err := json.Unmarshal([]byte(body), &c); err != nil {
		t.Fatalf("decode cosigned STH %q: %v", body, err)
	}
	return c
}

// pubDER is the PKIX DER of the witness's own public key, used only to derive a
// second, pinned, deliberately-unrelated logID.
func (h *witnessHarness) pubDER(t *vhelp.T) []byte {
	b, err := x509.MarshalPKIXPublicKey(&h.witnessKey.PublicKey)
	if err != nil {
		t.Fatalf("marshal witness public key: %v", err)
	}
	return b
}

// hashFromB64 decodes a base64 logID into the product's SHA256Hash.
func hashFromB64(t *vhelp.T, s string) ct.SHA256Hash {
	var h ct.SHA256Hash
	if err := h.FromBase64String(s); err != nil {
		t.Fatalf("decode %q: %v", s, err)
	}
	return h
}

// sthSummary renders the parts of an STH a driver compares exactly.
func sthSummary(s ct.SignedTreeHead) string {
	sig, _ := s.TreeHeadSignature.Base64String()
	return fmt.Sprintf("v=%d size=%d ts=%d root=%x logid=%s sig=%s",
		s.Version, s.TreeSize, s.Timestamp, s.SHA256RootHash[:], s.LogID.Base64String(), sig)
}
