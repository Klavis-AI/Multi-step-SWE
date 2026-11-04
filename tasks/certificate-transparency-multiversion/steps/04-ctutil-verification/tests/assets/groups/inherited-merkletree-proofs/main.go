// Group inherited-merkletree-proofs.
//
// The tree hasher and inclusion-proof verifier are unchanged from v1.0.10.
// What this group pins is the arithmetic: four hand-picked leaves, the RFC
// 6962 hashes worked out here rather than copied from the tree, and the two
// audit paths that lead back to the same root.
//
// Nothing here names the domain-separation prefixes directly: they moved out of
// merkletree and into the root package at this milestone, so a driver that
// mentioned them by name would not compile against v1.0.10 at all. The hashes
// below pin their effect instead.
package main

import (
	"bytes"
	"crypto/sha256"
	"encoding/hex"

	"github.com/google/certificate-transparency-go/merkletree"

	"ctverify/vhelp"
)

var (
	pinnedLeaves = [][]byte{[]byte("alpha"), []byte("bravo"), []byte("charlie"), []byte("delta")}
	wantLeafHash = []string{
		"2a158d8afd48e3f88cb4195dfdb2a9e4817d95fa57fd34440d93f9aae5c4f82b",
		"798e6a07734241cb4ee9e30a512d3ac722a5fde3cbf9340755301d2715fd7810",
		"f931962f0917c346d447293c07b687ae1609f7003f8a44a06a75c4145b1e1929",
		"5c7117fb9edb0cec387257891105da6a6616722af247083e2d6eda671529cdc5",
	}
	wantRoot  = "e872bf22aae12fbbdc419c9a6b42ee30943539d08c5de1297abc4f847d3c1644"
	wantPath2 = []string{"5c7117fb9edb0cec387257891105da6a6616722af247083e2d6eda671529cdc5",
		"fb33dff7b9f27b94d57431d3c72e3268e5dda9c4de3d2b0d34ab34146d6e6806"}
	wantPath0 = []string{"798e6a07734241cb4ee9e30a512d3ac722a5fde3cbf9340755301d2715fd7810",
		"949d44dcd632bd90fef86f33c218f61f59e9880fba34fa10bbd89cdc704d8360"}
)

func main() {
	t := vhelp.New()

	h := merkletree.NewTreeHasher(sha256Sum)

	t.Check("the-hasher-exposes-the-empty-tree-hash",
		hex.EncodeToString(h.HashEmpty()) ==
			"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
		"got %x", h.HashEmpty())

	leafHashes := make([][]byte, len(pinnedLeaves))
	bad := ""
	for i, l := range pinnedLeaves {
		leafHashes[i] = h.HashLeaf(l)
		if got := hex.EncodeToString(leafHashes[i]); got != wantLeafHash[i] {
			bad += " " + string(l)
		}
	}
	t.Check("every-leaf-hashes-with-the-zero-prefix", bad == "", "wrong leaves:%s", bad)

	// The pinned values above are sha256(0x00 || leaf). What makes them that
	// rather than sha256(leaf) is the domain-separation prefix, so pin the
	// difference itself: a hasher that forgot the prefix would satisfy no
	// assertion here, but only this one says why.
	bareAlpha := sha256.Sum256(pinnedLeaves[0])
	t.Check("a-leaf-hash-is-not-the-bare-digest-of-the-leaf",
		!bytes.Equal(h.HashLeaf(pinnedLeaves[0]), bareAlpha[:]),
		"HashLeaf returned the unprefixed digest %x", bareAlpha)

	// The four-leaf root, built left to right the way RFC 6962 splits it.
	n01 := h.HashChildren(leafHashes[0], leafHashes[1])
	n23 := h.HashChildren(leafHashes[2], leafHashes[3])
	mkRoot := h.HashChildren(n01, n23)
	t.Equal("the-four-leaf-root-is-the-pinned-root",
		hex.EncodeToString(mkRoot), wantRoot)

	// Same root, reached by folding each audit path by hand.
	t.Equal("folding-the-path-for-leaf-2-reaches-the-same-root",
		hex.EncodeToString(fold(h, 2, pinnedLeaves[2], wantPath2)), wantRoot)
	t.Equal("folding-the-path-for-leaf-0-reaches-the-same-root",
		hex.EncodeToString(fold(h, 0, pinnedLeaves[0], wantPath0)), wantRoot)

	mv := merkletree.NewMerkleVerifier(sha256Sum)
	proof2 := decodeAll(t, wantPath2)
	proof0 := decodeAll(t, wantPath0)
	root := decodeAll(t, []string{wantRoot})[0]

	t.Case("the-verifier-accepts-the-proof-for-leaf-2",
		mv.VerifyInclusionProof(2, 4, proof2, root, pinnedLeaves[2]))
	t.Case("the-verifier-accepts-the-proof-for-leaf-0",
		mv.VerifyInclusionProof(0, 4, proof0, root, pinnedLeaves[0]))

	// The short-circuit: proof2 for leaf 0 must be refused with leaf 0's data.
	t.Check("the-verifier-refuses-a-proof-that-belongs-to-another-leaf",
		mv.VerifyInclusionProof(0, 4, proof2, root, pinnedLeaves[0]) != nil,
		"a mismatched index/proof pair was accepted")

	// RootFromInclusionProof does not need the leaf hash supplied.
	gotRoot, err := mv.RootFromInclusionProof(2, 4, proof2, pinnedLeaves[2])
	t.Check("the-root-can-be-rebuilt-from-a-single-proof",
		err == nil && bytes.Equal(gotRoot, root), "err=%v, root=%x", err, gotRoot)

	t.Done()
}

// sha256Sum is the HasherFunc shape the tree wants: bytes in, digest out.
func sha256Sum(b []byte) []byte {
	d := sha256.Sum256(b)
	return d[:]
}

// fold walks one audit path from the leaf to the root the way RFC 6962
// describes it: the accumulator sits on the right whenever the running index
// is even, and on the left whenever it is odd.
func fold(h *merkletree.TreeHasher, index int, leaf []byte, p []string) []byte {
	out := h.HashLeaf(leaf)
	idx := index
	for _, s := range p {
		sib, _ := hex.DecodeString(s)
		var next []byte
		if idx%2 == 0 {
			next = h.HashChildren(out, sib)
		} else {
			next = h.HashChildren(sib, out)
		}
		out = append([]byte(nil), next...)
		idx /= 2
	}
	return out
}

func decodeAll(t *vhelp.T, ss []string) [][]byte {
	out := make([][]byte, 0, len(ss))
	for _, s := range ss {
		b, err := hex.DecodeString(s)
		if err != nil {
			t.Fatalf("corpus hex %q: %v", s, err)
		}
		out = append(out, b)
	}
	return out
}
