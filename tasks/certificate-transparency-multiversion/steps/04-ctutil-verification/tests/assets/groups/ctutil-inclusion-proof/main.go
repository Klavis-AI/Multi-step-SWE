// Group ctutil-inclusion-proof.
//
// v1.0.13's ctutil.LogInfo can prove a Merkle tree leaf is in the log:
// VerifyInclusionAt against a caller-supplied tree size and root,
// VerifyInclusion against whatever STH the log currently serves, and
// VerifyInclusionLatest against the last STH LogInfo already knows, fetching
// one only if it has none. All three re-stamp the leaf with the supplied
// timestamp, hash it with the RFC 6962 leaf prefix, ask the log for a proof
// by that hash, and check the proof against the root.
//
// Boundary: the exported LogInfo methods, driven through the
// client.CheckLogClient interface v1.0.13 introduces. The log side is this
// driver's own fake: a seven-leaf RFC 6962 Merkle tree built here from pinned
// leaf data, with the tree hashing implemented in this file rather than taken
// from the product, so the expected root and audit paths are independent of
// the code under test. Nothing listens and nothing is dialled.
package main

import (
	"context"
	"crypto/sha256"
	"errors"
	"net/http"
	"os"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/ctutil"
	"github.com/google/certificate-transparency-go/loglist"
	"github.com/google/certificate-transparency-go/tls"

	"ctverify/vhelp"
)

const pinnedSTHTimestamp = uint64(1400000000000)

// ---------------------------------------------------------------------------
// RFC 6962 s2.1 Merkle tree, implemented here.

func leafHash(d []byte) []byte {
	h := sha256.Sum256(append([]byte{0x00}, d...))
	return h[:]
}

func nodeHash(l, r []byte) []byte {
	in := append([]byte{0x01}, l...)
	in = append(in, r...)
	h := sha256.Sum256(in)
	return h[:]
}

// split returns the largest power of two strictly less than n.
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

// ---------------------------------------------------------------------------
// The fake log.

type fakeLog struct {
	leaves      [][]byte
	root        []byte
	sthCalls    int
	sthErr      bool
	corruptPath bool
}

func (f *fakeLog) GetSTH(ctx context.Context) (*ct.SignedTreeHead, error) {
	f.sthCalls++
	if f.sthErr {
		return nil, errors.New("this log is not serving an STH")
	}
	sth := &ct.SignedTreeHead{
		Version:   ct.V1,
		TreeSize:  uint64(len(f.leaves)),
		Timestamp: pinnedSTHTimestamp,
	}
	copy(sth.SHA256RootHash[:], f.root)
	return sth, nil
}

func (f *fakeLog) GetSTHConsistency(ctx context.Context, first, second uint64) ([][]byte, error) {
	return nil, errors.New("not needed by this group")
}

func (f *fakeLog) GetProofByHash(ctx context.Context, hash []byte, treeSize uint64) (*ct.GetProofByHashResponse, error) {
	for i, l := range f.leaves {
		if string(leafHash(l)) != string(hash) {
			continue
		}
		p := path(f.leaves, i)
		if f.corruptPath && len(p) > 0 {
			bad := make([][]byte, len(p))
			copy(bad, p)
			flipped := append([]byte{}, bad[0]...)
			flipped[0] ^= 0x01
			bad[0] = flipped
			p = bad
		}
		return &ct.GetProofByHashResponse{LeafIndex: int64(i), AuditPath: p}, nil
	}
	return nil, errors.New("no leaf with that hash in this log")
}

// ---------------------------------------------------------------------------

func main() {
	t := vhelp.New()
	m := katFile("rfc6962", "kat.txt")
	ctx := context.Background()

	precertSCT := parseSCT(t, kat(t, m, "TestPreCertProof"))
	x509SCT := parseSCT(t, kat(t, m, "TestCertProof"))

	ca := vhelp.ReadFixture("rfc6962", "ca-cert.pem")
	embChain := pemCerts(t, concat(vhelp.ReadFixture("rfc6962", "test-embedded-cert.pem"), ca))
	certChain := pemCerts(t, concat(vhelp.ReadFixture("rfc6962", "test-cert.pem"), ca))

	precertLeaf, err := ct.MerkleTreeLeafForEmbeddedSCT(embChain, precertSCT.Timestamp)
	if err != nil {
		t.Fatalf("rebuilding the embedded-SCT leaf: %v", err)
	}
	x509Leaf, err := ct.MerkleTreeLeafFromChain(certChain, ct.X509LogEntryType, x509SCT.Timestamp)
	if err != nil {
		t.Fatalf("building the x509 leaf: %v", err)
	}
	precertData := marshal(t, precertLeaf)
	x509Data := marshal(t, x509Leaf)

	// Seven leaves; the two real ones sit at 3 and 5 so both audit paths are
	// three nodes long and neither is the tree's first or last entry.
	leaves := [][]byte{
		[]byte("pinned filler leaf 0"),
		[]byte("pinned filler leaf 1"),
		[]byte("pinned filler leaf 2"),
		precertData,
		[]byte("pinned filler leaf 4"),
		x509Data,
		[]byte("pinned filler leaf 6"),
	}
	treeRoot := root(leaves)

	data, err := os.ReadFile(vhelp.GroupFile("loglist.json"))
	if err != nil {
		t.Fatalf("reading the pinned log list: %v", err)
	}
	ll, err := loglist.NewFromJSON(data)
	if err != nil {
		t.Fatalf("parsing the pinned log list: %v", err)
	}

	newLog := func(f *fakeLog) *ctutil.LogInfo {
		li, err := ctutil.NewLogInfo(&ll.Logs[0], &http.Client{})
		if err != nil {
			t.Fatalf("NewLogInfo: %v", err)
		}
		f.leaves = leaves
		f.root = treeRoot
		li.Client = f
		return li
	}

	// --- VerifyInclusionAt ---------------------------------------------------
	li := newLog(&fakeLog{})
	idx, err := li.VerifyInclusionAt(ctx, *precertLeaf, precertSCT.Timestamp, uint64(len(leaves)), treeRoot)
	t.Check("verify-inclusion-at-returns-the-index-of-the-precertificate-leaf",
		err == nil && idx == 3, "err=%v, index=%d, want 3", err, idx)

	idx, err = li.VerifyInclusionAt(ctx, *x509Leaf, x509SCT.Timestamp, uint64(len(leaves)), treeRoot)
	t.Check("verify-inclusion-at-returns-the-index-of-the-x509-leaf",
		err == nil && idx == 5, "err=%v, index=%d, want 5", err, idx)

	// The timestamp argument, not the leaf's own field, decides which leaf is
	// looked for: a leaf built at the wrong instant still verifies.
	skewed, err := ct.MerkleTreeLeafForEmbeddedSCT(embChain, precertSCT.Timestamp+5000)
	if err != nil {
		t.Fatalf("rebuilding the skewed leaf: %v", err)
	}
	idx, err = li.VerifyInclusionAt(ctx, *skewed, precertSCT.Timestamp, uint64(len(leaves)), treeRoot)
	t.Check("the-timestamp-argument-restamps-the-leaf-before-hashing",
		err == nil && idx == 3, "err=%v, index=%d, want 3", err, idx)

	_, err = li.VerifyInclusionAt(ctx, *precertLeaf, precertSCT.Timestamp+1, uint64(len(leaves)), treeRoot)
	t.Check("one-millisecond-of-drift-is-a-leaf-the-log-does-not-have", err != nil,
		"a leaf with a different timestamp was found in the tree")

	badRoot := append([]byte{}, treeRoot...)
	badRoot[0] ^= 0x01
	idx, err = li.VerifyInclusionAt(ctx, *precertLeaf, precertSCT.Timestamp, uint64(len(leaves)), badRoot)
	t.Check("a-proof-that-does-not-reach-the-given-root-is-rejected",
		err != nil && idx == -1, "err=%v, index=%d", err, idx)

	idx, err = li.VerifyInclusionAt(ctx, *precertLeaf, precertSCT.Timestamp, 4, treeRoot)
	t.Check("a-proof-checked-against-the-wrong-tree-size-is-rejected",
		err != nil && idx == -1, "err=%v, index=%d", err, idx)

	corrupt := newLog(&fakeLog{corruptPath: true})
	idx, err = corrupt.VerifyInclusionAt(ctx, *precertLeaf, precertSCT.Timestamp, uint64(len(leaves)), treeRoot)
	t.Check("a-tampered-audit-path-is-rejected",
		err != nil && idx == -1, "err=%v, index=%d", err, idx)

	unknown, err := ct.MerkleTreeLeafFromChain(certChain, ct.X509LogEntryType, 1)
	if err != nil {
		t.Fatalf("building an unlogged leaf: %v", err)
	}
	idx, err = li.VerifyInclusionAt(ctx, *unknown, 1, uint64(len(leaves)), treeRoot)
	t.Check("a-leaf-the-log-has-never-seen-is-rejected",
		err != nil && idx == -1, "err=%v, index=%d", err, idx)

	// --- VerifyInclusion: always asks the log for a fresh STH ----------------
	f := &fakeLog{}
	fresh := newLog(f)
	i1, e1 := fresh.VerifyInclusion(ctx, *precertLeaf, precertSCT.Timestamp)
	i2, e2 := fresh.VerifyInclusion(ctx, *precertLeaf, precertSCT.Timestamp)
	t.Check("verify-inclusion-fetches-the-sth-every-time-and-records-it",
		e1 == nil && e2 == nil && i1 == 3 && i2 == 3 && f.sthCalls == 2 &&
			fresh.LastSTH() != nil && fresh.LastSTH().TreeSize == uint64(len(leaves)),
		"errs=%v/%v indices=%d/%d sth calls=%d", e1, e2, i1, i2, f.sthCalls)

	// --- VerifyInclusionLatest: asks only when it has no STH -----------------
	g := &fakeLog{}
	latest := newLog(g)
	i1, e1 = latest.VerifyInclusionLatest(ctx, *precertLeaf, precertSCT.Timestamp)
	i2, e2 = latest.VerifyInclusionLatest(ctx, *x509Leaf, x509SCT.Timestamp)
	t.Check("verify-inclusion-latest-fetches-the-sth-only-once",
		e1 == nil && e2 == nil && i1 == 3 && i2 == 5 && g.sthCalls == 1,
		"errs=%v/%v indices=%d/%d sth calls=%d", e1, e2, i1, i2, g.sthCalls)

	h := &fakeLog{}
	preset := newLog(h)
	known := &ct.SignedTreeHead{Version: ct.V1, TreeSize: uint64(len(leaves)), Timestamp: pinnedSTHTimestamp}
	copy(known.SHA256RootHash[:], treeRoot)
	preset.SetSTH(known)
	idx, err = preset.VerifyInclusionLatest(ctx, *precertLeaf, precertSCT.Timestamp)
	t.Check("verify-inclusion-latest-uses-an-sth-it-already-has",
		err == nil && idx == 3 && h.sthCalls == 0,
		"err=%v index=%d sth calls=%d", err, idx, h.sthCalls)

	// --- a log that cannot serve an STH --------------------------------------
	k := &fakeLog{sthErr: true}
	broken := newLog(k)
	idx, err = broken.VerifyInclusion(ctx, *precertLeaf, precertSCT.Timestamp)
	t.Check("a-log-that-cannot-serve-an-sth-fails-the-check",
		err != nil && idx == -1 && broken.LastSTH() == nil,
		"err=%v index=%d last STH=%v", err, idx, broken.LastSTH())

	t.Done()
}

func marshal(t *vhelp.T, leaf *ct.MerkleTreeLeaf) []byte {
	b, err := tls.Marshal(*leaf)
	if err != nil {
		t.Fatalf("tls.Marshal: %v", err)
	}
	return b
}

func concat(parts ...[]byte) []byte {
	var out []byte
	for _, p := range parts {
		out = append(out, p...)
	}
	return out
}
