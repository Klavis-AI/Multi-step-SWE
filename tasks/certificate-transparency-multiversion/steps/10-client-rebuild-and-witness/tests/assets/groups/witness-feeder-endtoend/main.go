// Group witness-feeder-endtoend.
//
// The shape v1.1.3 is actually about, and the only place the candidate's own
// `feeder` binary is graded: three of the candidate's binaries wired together
// over real loopback sockets. The candidate's ct_server serves a pinned CT log
// in front of a pinned Trillian stub (SCOPE-BACKEND); the candidate's feeder
// reads that log's STH through a log list this group writes and pushes it to
// the candidate's witness; the candidate's witness stores it and countersigns
// it. The benchmark then reads the cosigned STH back over HTTP and checks it
// against the values it pinned and the keys it owns.
//
// Seven of these eight cases were the second half of this step's
// `gate-endtoend` until the 2026-09-21 tier audit; the eighth, the retired-log
// case, was added in the revision round that followed. They are this milestone's new capability -- at
// v1.1.2 there is no internal/witness, no witness binary and no feeder binary
// at all -- so under `reward = gate x score` they cannot sit in the gate: a
// no-op continuation seeded with the step-09 workspace would fail the gate and
// measure nothing (gate rule 3). They are graded here instead, and the gate
// keeps only the personality path v1.1.2 already had.
//
// Bringing the three processes up is a prerequisite, not a case: a failure
// there aborts the group rather than letting it report a vacuous pass.
// Everything asserted is exact: an exact log list, an exact tree size, an
// exact root hash, the log's signature byte for byte, and the witness's
// countersignature verified with the standard library against a pinned key.
//
// SCOPE-NET: loopback only. The benchmark's own listeners -- the Trillian stub
// and the retired-log stub -- bind :0 and serve on the listener the kernel
// hands back; ct_server's and the witness's ports have to be named on their
// command lines before those processes exist, so those are probed for below
// the kernel's ephemeral range. SCOPE-TIME: no elapsed time, deadline or
// timestamp is asserted; the feeder poll below and the settle before case 7
// are fatal prerequisites and a bring-up wait, not cases.
package main

import (
	"crypto/ecdsa"
	"crypto/sha256"
	"crypto/x509"
	"encoding/json"
	"fmt"
	"net"
	"net/http"
	"strings"
	"sync"
	"time"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/tls"

	"ctverify/vhelp"
)

// verifyLogSTH checks an RFC 6962 tree-head signature with the standard
// library: SHA-256 over the TLS-encoded TreeHeadSignature, ECDSA under the
// pinned log key. Only the serialisation is the product's.
func verifyLogSTH(t *vhelp.T, pub *ecdsa.PublicKey, sth *ct.SignedTreeHead) error {
	input, err := tls.Marshal(ct.TreeHeadSignature{
		Version:        sth.Version,
		SignatureType:  ct.TreeHashSignatureType,
		Timestamp:      sth.Timestamp,
		TreeSize:       sth.TreeSize,
		SHA256RootHash: sth.SHA256RootHash,
	})
	if err != nil {
		return fmt.Errorf("marshal TreeHeadSignature: %v", err)
	}
	digest := sha256.Sum256(input)
	if !ecdsa.VerifyASN1(pub, digest[:], sth.TreeHeadSignature.Signature) {
		return fmt.Errorf("tree-head signature does not verify under the pinned log key")
	}
	return nil
}

func main() {
	t := vhelp.New()

	logPubPEM := vhelp.ReadFixture("minted", "keys", "log-pubkey.pem")
	logPubDER := pemBody(t, logPubPEM)

	stdPub, err := x509.ParsePKIXPublicKey(logPubDER)
	if err != nil {
		t.Fatalf("parse pinned log public key: %v", err)
	}
	logPub, ok := stdPub.(*ecdsa.PublicKey)
	if !ok {
		t.Fatalf("pinned log key is %T, want *ecdsa.PublicKey", stdPub)
	}

	// --- prerequisite: the log ---------------------------------------------
	// The personality's own behaviour is the gate's subject, not this group's.
	// It is started here only so that the feeder has a real log to read, and
	// its tree head is read once so the witness's copy can be compared with it.
	p := startPersonality(t)
	defer p.stop()

	gs := httpGet(t, p.url(ct.GetSTHPath))
	if gs.status != http.StatusOK {
		t.Fatalf("get-sth returned %d: %s", gs.status, trunc(gs.body))
	}
	var sthRsp ct.GetSTHResponse
	if err := json.Unmarshal(gs.body, &sthRsp); err != nil {
		t.Fatalf("get-sth is not a GetSTHResponse: %v", err)
	}
	logSTH, err := sthRsp.ToSignedTreeHead()
	if err != nil {
		t.Fatalf("get-sth does not parse as an STH: %v", err)
	}

	// --- the witness -------------------------------------------------------
	w := startWitness(t)
	defer w.stop()

	// 1. The witness holds nothing for the log yet. w.logID is the
	//    benchmark's own base64(SHA-256(PKIX DER)) of the pinned key: it is
	//    the address the feeder will push to, and nothing about it is the
	//    product's answer. That the witness derives the same identifier for
	//    itself is graded at case 3 below, against what it returns.
	logs := w.do(t, http.MethodGet, w.baseURL+witnessAPILogs, nil)
	t.Check("witness-empty-before-feeder", logs.status == http.StatusOK && logs.body == "null",
		"got %d %q, want 200 \"null\"", logs.status, logs.body)

	// --- the feeder --------------------------------------------------------
	// A second log goes into the list in the `retired` state, on a
	// benchmark-owned endpoint of its own. Its key is a real PKIX DER, derived
	// from another pinned fixture, so a feeder that skips the state filter
	// builds a client for it successfully and goes on to poll it -- which is
	// what case 7 below detects. A feeder that filters never resolves this
	// URL at all.
	retiredKey := ecKey(t, "minted", "keys", "rogue-root-key.pem")
	retiredPubDER, err := x509.MarshalPKIXPublicKey(&retiredKey.PublicKey)
	if err != nil {
		t.Fatalf("marshal the retired log's public key: %v", err)
	}
	retired := newRetiredStub(t)
	defer retired.close()

	listPath := writeLogList(t, p.logBaseURL(), logPubDER, retired.URL, retiredPubDER)
	feeder := startFeeder(t, listPath, w.baseURL)
	// Done calls os.Exit, which does not run deferred functions, so this
	// teardown must be registered with the harness or the feeder outlives
	// the group. The shipped test.sh has no process-group trap to catch it.
	t.Cleanup(func() {
		_ = feeder.Kill()
		_, _ = feeder.Wait()
	})

	served := waitForWitnessSTH(t, w, w.logID)
	cos := decodeCosigned(t, served.body)

	// 2. The witness now follows the log.
	logs = w.do(t, http.MethodGet, w.baseURL+witnessAPILogs, nil)
	t.Equal("witness-follows-the-log-after-feeding",
		fmt.Sprintf("%d %s", logs.status, logs.body), fmt.Sprintf("200 [%q]", w.logID))

	// 3. The log id the cosigned STH carries is the witness's own derivation
	//    from the key it was configured with, not something it was handed.
	//    An RFC 6962 get-sth response has no log_id field at all, so the tree
	//    head the feeder pushed carried the zero hash; the copy the witness
	//    countersigns carries base64(SHA-256(PKIX DER)) of the pinned key,
	//    which is candidate code filling an empty LogID from the id in the
	//    URL it was addressed at. Only the right-hand side is computed here.
	var noLogID ct.SHA256Hash
	t.Equal("witness-log-id-derived-from-the-pinned-log-key",
		fmt.Sprintf("fed=%x cosigned=%s", logSTH.LogID[:], cos.LogID.Base64String()),
		fmt.Sprintf("fed=%x cosigned=%s", noLogID[:], logIDFor(logPubDER)))

	// 4. What it holds is the log's own tree head, unchanged.
	t.Equal("witness-holds-the-logs-tree-head",
		fmt.Sprintf("%d %d %x %s", cos.TreeSize, cos.Timestamp, cos.SHA256RootHash[:], cos.LogID.Base64String()),
		fmt.Sprintf("%d %d %x %s", pinnedTreeSize, pinnedTimestampMS, pinnedRootHash[:], w.logID))

	// 5. carrying the log's signature byte for byte.
	gotSig, err := cos.TreeHeadSignature.Base64String()
	if err != nil {
		t.Fatalf("re-encode witnessed tree-head signature: %v", err)
	}
	wantSig, err := logSTH.TreeHeadSignature.Base64String()
	if err != nil {
		t.Fatalf("re-encode log tree-head signature: %v", err)
	}
	t.Equal("witness-carries-the-logs-signature", gotSig, wantSig)
	t.Case("witness-log-signature-still-verifies", verifyLogSTH(t, logPub, &cos.SignedTreeHead))

	// 6. and the witness has added a countersignature of its own.
	t.Case("witness-cosignature-verifies", witnessSigVerifies(t, cos, &w.witnessKey.PublicKey))

	// 7. The second log in that list was `retired`, and CLI.md 4.10 makes the
	//    feeder follow only the entries whose state union has `usable` set. So
	//    its endpoint must never have been asked anything at all -- not asked
	//    and abandoned, and not asked once before the state was noticed.
	//
	//    The wait is a settle, not an assertion: --poll is 1s and a feeder
	//    that does poll every entry reaches this stub on its first cycle,
	//    long before the witness could answer the poll above, so this only
	//    widens a window that is already wide. It ends early the moment the
	//    stub is touched; a conforming feeder pays the full two seconds and
	//    this group's own passes do not depend on how long it took.
	for i := 0; i < 20 && len(retired.requests()) == 0; i++ {
		time.Sleep(100 * time.Millisecond)
	}
	t.Equal("feeder-never-polls-a-retired-log",
		strings.Join(retired.requests(), "|"), "")

	t.Done()
}

// retiredStub is a benchmark-owned loopback endpoint that records every
// request and serves nothing. It stands in for a log the feeder is required
// not to follow, so its whole contract is the request log it keeps: any
// answer it gave would already be a failure.
type retiredStub struct {
	mu   sync.Mutex
	hits []string

	ln  net.Listener
	srv *http.Server
	// URL is the base URI written into the log list for the retired entry.
	URL string
}

func newRetiredStub(t *vhelp.T) *retiredStub {
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("listen for the retired-log stub: %v", err)
	}
	s := &retiredStub{ln: ln, URL: "http://" + ln.Addr().String()}
	s.srv = &http.Server{Handler: s}
	go func() { _ = s.srv.Serve(ln) }()
	return s
}

func (s *retiredStub) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	s.mu.Lock()
	s.hits = append(s.hits, r.Method+" "+r.URL.Path)
	s.mu.Unlock()
	http.Error(w, "this log is retired", http.StatusGone)
}

func (s *retiredStub) requests() []string {
	s.mu.Lock()
	defer s.mu.Unlock()
	return append([]string{}, s.hits...)
}

func (s *retiredStub) close() {
	_ = s.srv.Close()
}

func trunc(b []byte) string {
	s := strings.ReplaceAll(string(b), "\n", " ")
	if len(s) > 200 {
		s = s[:200]
	}
	return s
}
