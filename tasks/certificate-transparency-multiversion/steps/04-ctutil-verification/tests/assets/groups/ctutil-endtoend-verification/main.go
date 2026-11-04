// Group ctutil-endtoend-verification -- KIND scored, WEIGHT 3.
//
// These thirteen cases were the whole of step 04's gate-endtoend group. Every
// one of them grades this milestone's headline new capability -- loglist,
// ctutil, x509util.ParseSCTsFromCertificate and the findlog binary -- none of
// which exists at v1.0.10, so the driver does not even compile against the
// previous milestone. In the gate that was an unpriced veto (gate rule 4) and
// it made this step's own no-op continuation report a FAILED GATE rather than
// a zero score, which measures nothing at all. The work is priced here at
// weight 3 instead; gate-endtoend now grades the inherited end-to-end path and
// the fact that the artifact starts.
//
// The whole of this milestone in one run: take a pinned certificate with an
// embedded SCT and, using only v1.0.13's own API, decide whether that SCT is
// a genuine promise by a known log to include that certificate.
//
//	signed log list  -> loglist.NewFromSignedJSON
//	logs by key hash -> ctutil.LogInfoByKeyHash
//	embedded SCT     -> x509util.ParseSCTsFromCertificate
//	issuing log      -> the SCT's LogID in that map
//	precert leaf     -> ct.MerkleTreeLeafForEmbeddedSCT
//	signature        -> LogInfo.VerifySCTSignature
//	leaf hash        -> ctutil.LeafHashB64
//	inclusion        -> LogInfo.VerifyInclusionLatest
//	the shorthands   -> ctutil.VerifySCT, ctutil.ContainsSCT
//	the tool         -> findlog, run from $CT_BIN over the same list
//
// The log side is this driver's own fake: a seven-leaf RFC 6962 Merkle tree
// built here, with the tree hashing implemented in this file so the expected
// root and audit path do not come from the code under test. Every key,
// certificate, SCT and timestamp is pinned; nothing is dialled.
package main

import (
	"bytes"
	"context"
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"
	"errors"
	"net/http"
	"os"
	"strings"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/ctutil"
	"github.com/google/certificate-transparency-go/loglist"
	"github.com/google/certificate-transparency-go/tls"
	"github.com/google/certificate-transparency-go/x509"
	"github.com/google/certificate-transparency-go/x509util"

	"ctverify/vhelp"
)

const (
	testLogKeyHashHex  = "df1c2ec11500945247a96168325ddc5c7959e8f7c6d388fc002e0bbd3f74d764"
	wantLeafTLSSHA256  = "09812696b22e8fda8e629fd934165d87451823d6af750ff922e80418f69752a7"
	pinnedSTHTimestamp = uint64(1400000000000)
	findlogDesc        = "Erw Wen RFC6962 Test Log"
	findlogURL         = "ct.example.com/testlog/"
)

// --- RFC 6962 s2.1 Merkle tree, implemented here ---------------------------

func leafHash(d []byte) []byte { h := sha256.Sum256(append([]byte{0x00}, d...)); return h[:] }

func nodeHash(l, r []byte) []byte {
	in := append([]byte{0x01}, l...)
	in = append(in, r...)
	h := sha256.Sum256(in)
	return h[:]
}

func split(n int) int {
	k := 1
	for k*2 < n {
		k *= 2
	}
	return k
}

func root(leaves [][]byte) []byte {
	if len(leaves) == 1 {
		return leafHash(leaves[0])
	}
	k := split(len(leaves))
	return nodeHash(root(leaves[:k]), root(leaves[k:]))
}

func path(leaves [][]byte, m int) [][]byte {
	if len(leaves) == 1 {
		return nil
	}
	k := split(len(leaves))
	if m < k {
		return append(path(leaves[:k], m), root(leaves[k:]))
	}
	return append(path(leaves[k:], m-k), root(leaves[:k]))
}

type fakeLog struct {
	leaves [][]byte
	root   []byte
}

func (f *fakeLog) GetSTH(ctx context.Context) (*ct.SignedTreeHead, error) {
	sth := &ct.SignedTreeHead{Version: ct.V1, TreeSize: uint64(len(f.leaves)), Timestamp: pinnedSTHTimestamp}
	copy(sth.SHA256RootHash[:], f.root)
	return sth, nil
}

func (f *fakeLog) GetSTHConsistency(ctx context.Context, first, second uint64) ([][]byte, error) {
	return nil, errors.New("not used by this group")
}

func (f *fakeLog) GetProofByHash(ctx context.Context, hash []byte, treeSize uint64) (*ct.GetProofByHashResponse, error) {
	for i, l := range f.leaves {
		if bytes.Equal(leafHash(l), hash) {
			return &ct.GetProofByHashResponse{LeafIndex: int64(i), AuditPath: path(f.leaves, i)}, nil
		}
	}
	return nil, errors.New("no leaf with that hash in this log")
}

// ---------------------------------------------------------------------------

func main() {
	t := vhelp.New()
	m := katFile("rfc6962", "kat.txt")
	ctx := context.Background()

	certPEM := vhelp.ReadFixture("rfc6962", "test-embedded-cert.pem")
	caPEM := vhelp.ReadFixture("rfc6962", "ca-cert.pem")

	// --- 1: the signed log list ---------------------------------------------
	llData := groupBytes(t, "loglist.json")
	llSig := groupBytes(t, "loglist.sig.ec")
	llKey, _, _, err := ct.PublicKeyFromPEM(vhelp.ReadFixture("rfc6962", "sig-test-ec256-public-key.pem"))
	if err != nil {
		t.Fatalf("parsing the log-list signing key: %v", err)
	}
	ll, err := loglist.NewFromSignedJSON(llData, llSig, llKey)
	t.Check("stage-1-the-signed-log-list-loads",
		err == nil && ll != nil && len(ll.Logs) == 4, "err=%v", err)
	if err != nil {
		t.Fatalf("cannot continue without a log list")
	}

	// --- 2: indexed by key hash ---------------------------------------------
	byHash, err := ctutil.LogInfoByKeyHash(ll, &http.Client{})
	hashBytes, _ := hex.DecodeString(testLogKeyHashHex)
	var wantHash [sha256.Size]byte
	copy(wantHash[:], hashBytes)
	t.Check("stage-2-every-log-is-indexed-by-the-hash-of-its-key",
		err == nil && len(byHash) == 4 && byHash[wantHash] != nil, "err=%v, %d entries", err, len(byHash))
	if err != nil {
		t.Fatalf("cannot continue without the log index")
	}

	// --- 3: the embedded SCT ------------------------------------------------
	scts, err := x509util.ParseSCTsFromCertificate(certPEM)
	wantSCTHex := kat(t, m, "TestPreCertProof")
	t.Check("stage-3-the-embedded-sct-is-recovered-from-the-certificate",
		err == nil && len(scts) == 1 && marshalHex(t, scts[0]) == wantSCTHex,
		"err=%v, got %d SCT(s)", err, len(scts))
	if err != nil || len(scts) != 1 {
		t.Fatalf("cannot continue without the embedded SCT")
	}
	sct := scts[0]

	// --- 4: which log issued it ---------------------------------------------
	li := byHash[sct.LogID.KeyID]
	t.Check("stage-4-the-scts-log-id-resolves-to-the-issuing-log",
		li != nil && li.Description == "Erw Wen RFC6962 Test Log" &&
			hex.EncodeToString(sct.LogID.KeyID[:]) == testLogKeyHashHex,
		"LogID %x resolved to %v", sct.LogID.KeyID[:], li)
	if li == nil {
		t.Fatalf("cannot continue without the issuing log")
	}

	// --- 5: rebuild what the log signed -------------------------------------
	chain := pemCerts(t, concat(certPEM, caPEM))
	leaf, err := ct.MerkleTreeLeafForEmbeddedSCT(chain, sct.Timestamp)
	var leafData []byte
	if err == nil {
		leafData, err = tls.Marshal(*leaf)
	}
	sum := sha256.Sum256(leafData)
	t.Check("stage-5-the-precertificate-leaf-is-rebuilt-to-the-pinned-encoding",
		err == nil && hex.EncodeToString(sum[:]) == wantLeafTLSSHA256,
		"err=%v, leaf sha256=%x", err, sum[:])
	if err != nil {
		t.Fatalf("cannot continue without the rebuilt leaf")
	}

	// --- 6: the signature ---------------------------------------------------
	sigErr := li.VerifySCTSignature(*sct, *leaf)
	t.Check("stage-6-the-sct-signature-verifies-under-the-logs-key",
		sigErr == nil, "verification failed: %v", sigErr)

	// --- 7: the leaf hash ---------------------------------------------------
	gotHash, err := ctutil.LeafHashB64(chain, sct, true)
	t.Check("stage-7-the-leaf-hash-matches-the-published-answer",
		err == nil && gotHash == kat(t, m, "TestPreCertB64LeafHash"),
		"err=%v, got %q", err, gotHash)

	// --- 8: inclusion --------------------------------------------------------
	leaves := [][]byte{
		[]byte("pinned filler leaf 0"),
		[]byte("pinned filler leaf 1"),
		[]byte("pinned filler leaf 2"),
		leafData,
		[]byte("pinned filler leaf 4"),
		[]byte("pinned filler leaf 5"),
		[]byte("pinned filler leaf 6"),
	}
	fake := &fakeLog{leaves: leaves, root: root(leaves)}
	li.Client = fake
	idx, err := li.VerifyInclusionLatest(ctx, *leaf, sct.Timestamp)
	t.Check("stage-8-the-log-can-prove-the-leaf-is-in-its-tree",
		err == nil && idx == 3 && li.LastSTH() != nil && li.LastSTH().TreeSize == 7,
		"err=%v, index=%d", err, idx)

	// --- 9 and 10: the one-call shorthands ----------------------------------
	logKey, keyErr := x509.ParsePKIXPublicKey(li.PublicKey)
	if keyErr != nil {
		t.Fatalf("the log list entry did not carry a parsable SubjectPublicKeyInfo: %v", keyErr)
	}
	t.Check("stage-9-verify-sct-accepts-the-same-chain-and-sct",
		ctutil.VerifySCT(logKey, chain, sct, true) == nil,
		"VerifySCT rejected the pinned SCT: %v", ctutil.VerifySCT(logKey, chain, sct, true))

	t.Check("stage-10-contains-sct-confirms-the-certificate-carries-it",
		containsSCT(t, chain[0], sct) && !containsSCT(t, chain[1], sct),
		"ContainsSCT disagreed with the certificate's own extension")

	// --- what must not pass --------------------------------------------------
	tampered := *sct
	tampered.Signature.Signature = append([]byte{}, sct.Signature.Signature...)
	tampered.Signature.Signature[len(tampered.Signature.Signature)-1] ^= 0x01
	t.Check("a-flipped-signature-bit-is-refused-at-every-entry-point",
		li.VerifySCTSignature(tampered, *leaf) != nil &&
			ctutil.VerifySCT(logKey, chain, &tampered, true) != nil,
		"a tampered SCT was accepted")

	strangerLL := &loglist.LogList{Logs: []loglist.Log{ll.Logs[1], ll.Logs[3]}}
	strangerMap, err := ctutil.LogInfoByKeyHash(strangerLL, &http.Client{})
	t.Check("an-sct-from-a-log-that-is-not-in-the-list-cannot-be-resolved",
		err == nil && len(strangerMap) == 2 && strangerMap[sct.LogID.KeyID] == nil,
		"err=%v, the SCT's log was found in a list that does not contain it", err)

	// --- the tool sees the same list -----------------------------------------
	rc, out := runFindlog("--log_list="+vhelp.GroupFile("loglist.json"),
		base64.StdEncoding.EncodeToString(sct.LogID.KeyID[:]))
	// TEST_INTERFACE.md section 6: one line per match, naming the log's
	// description and its URL. The rendering between them is not asserted.
	outLines := strings.Split(strings.TrimRight(out, "\n"), "\n")
	t.Check("findlog-resolves-the-same-log-from-the-same-list-by-the-scts-log-id",
		rc == 0 && len(outLines) == 1 &&
			strings.Contains(outLines[0], findlogDesc) &&
			strings.Contains(outLines[0], findlogURL),
		"rc=%d, stdout=%q", rc, out)

	t.Done()
}

func containsSCT(t *vhelp.T, cert *x509.Certificate, sct *ct.SignedCertificateTimestamp) bool {
	got, err := ctutil.ContainsSCT(cert, sct)
	if err != nil {
		t.Fatalf("ContainsSCT: %v", err)
	}
	return got
}

func marshalHex(t *vhelp.T, sct *ct.SignedCertificateTimestamp) string {
	b, err := tls.Marshal(*sct)
	if err != nil {
		t.Fatalf("tls.Marshal of an SCT: %v", err)
	}
	return hex.EncodeToString(b)
}

func groupBytes(t *vhelp.T, name string) []byte {
	b, err := os.ReadFile(vhelp.GroupFile(name))
	if err != nil {
		t.Fatalf("reading %s: %v", name, err)
	}
	return b
}

func runFindlog(arg ...string) (int, string) {
	cmd := vhelp.Command(vhelp.CandidateBin("findlog"), arg...)
	cmd.Env = append(cmd.Env, "TZ=UTC")
	var out, errOut bytes.Buffer
	cmd.Stdout = &out
	cmd.Stderr = &errOut
	rc := 0
	if err := cmd.Run(); err != nil {
		rc = 1
		if ee, ok := err.(interface{ ExitCode() int }); ok {
			rc = ee.ExitCode()
		}
	}
	return rc, out.String()
}

func concat(parts ...[]byte) []byte {
	var out []byte
	for _, p := range parts {
		out = append(out, p...)
	}
	return out
}
