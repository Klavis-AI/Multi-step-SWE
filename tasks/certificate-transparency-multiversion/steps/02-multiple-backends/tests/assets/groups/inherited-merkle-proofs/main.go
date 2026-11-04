// Group inherited-merkle-proofs (step 1 boundary merkletree-proofs, re-run unchanged at step 2).
//
// RFC 6962 s2.1: MTH({}) = SHA-256(), MTH(D[1]) = SHA-256(0x00 || d0),
// MTH(D[n]) = SHA-256(0x01 || MTH(left) || MTH(right)) split at the largest
// power of two below n; s2.1.1 and s2.1.2 define PATH() and PROOF().
//
// This driver carries its own implementation of MTH/PATH/PROOF over the eight
// reference leaves of RFC 6962 s2.1.3, checks it against the published root,
// and then grades the product's merkletree.TreeHasher and
// merkletree.MerkleVerifier against every inclusion proof in the eight-leaf
// tree and every consistency pair in it, plus the refusals.
//
// Boundary: the merkletree package's exported constructors and methods.
package main

import (
	"crypto/sha256"
	"encoding/hex"
	"errors"
	"fmt"

	"github.com/google/certificate-transparency-go/merkletree"

	"ctverify/vhelp"
)

// The eight reference leaves of RFC 6962 s2.1.3 and the published root of the
// tree they build.
var refLeavesHex = []string{
	"",
	"00",
	"10",
	"2021",
	"3031",
	"40414243",
	"5051525354555657",
	"606162636465666768696a6b6c6d6e6f",
}

const refRootHex = "5dc9da79a70659a9ad559cb701ded9a2ab9d823aad2f4960cfe370eff4604328"
const emptyRootHex = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"

func sha(b []byte) []byte { h := sha256.Sum256(b); return h[:] }

// --- this driver's own reference implementation of RFC 6962 s2.1 ------------

func mth(d [][]byte) []byte {
	switch len(d) {
	case 0:
		return sha(nil)
	case 1:
		return sha(append([]byte{0x00}, d[0]...))
	}
	k := split(len(d))
	left := mth(d[:k])
	right := mth(d[k:])
	return sha(append(append([]byte{0x01}, left...), right...))
}

// split is the largest power of two strictly less than n.
func split(n int) int {
	k := 1
	for k<<1 < n {
		k <<= 1
	}
	return k
}

func path(m int, d [][]byte) [][]byte {
	if len(d) == 1 {
		return [][]byte{}
	}
	k := split(len(d))
	if m < k {
		return append(path(m, d[:k]), mth(d[k:]))
	}
	return append(path(m-k, d[k:]), mth(d[:k]))
}

func proof(m int, d [][]byte) [][]byte { return subproof(m, d, true) }

func subproof(m int, d [][]byte, b bool) [][]byte {
	if m == len(d) {
		if b {
			return [][]byte{}
		}
		return [][]byte{mth(d)}
	}
	k := split(len(d))
	if m <= k {
		return append(subproof(m, d[:k], b), mth(d[k:]))
	}
	return append(subproof(m-k, d[k:], false), mth(d[:k]))
}

func nonNil(err error, why string) error {
	if err == nil {
		return errors.New(why)
	}
	return nil
}

func main() {
	t := vhelp.New()

	leaves := make([][]byte, len(refLeavesHex))
	for i, h := range refLeavesHex {
		b, err := hex.DecodeString(h)
		if err != nil {
			t.Fatalf("reference leaf %d: %v", i, err)
		}
		leaves[i] = b
	}
	refRoot, _ := hex.DecodeString(refRootHex)
	if hex.EncodeToString(mth(leaves)) != refRootHex {
		t.Fatalf("driver's own MTH disagrees with RFC 6962: %x", mth(leaves))
	}

	th := merkletree.NewTreeHasher(sha)
	v := merkletree.NewMerkleVerifier(sha)

	// --- the hasher ---------------------------------------------------------
	t.Equal("treehasher-empty-root", hex.EncodeToString(th.HashEmpty()), emptyRootHex)
	t.EqualBytes("treehasher-leaf-prefix-is-0x00", th.HashLeaf(leaves[3]), sha(append([]byte{0x00}, leaves[3]...)))
	t.EqualBytes("treehasher-node-prefix-is-0x01",
		th.HashChildren(th.HashLeaf(leaves[0]), th.HashLeaf(leaves[1])),
		sha(append(append([]byte{0x01}, th.HashLeaf(leaves[0])...), th.HashLeaf(leaves[1])...)))
	t.Equal("treehasher-prefix-constants", fmt.Sprintf("%d/%d", merkletree.LeafPrefix, merkletree.NodePrefix), "0/1")

	// --- inclusion ----------------------------------------------------------
	bad := ""
	for n := 1; n <= len(leaves); n++ {
		want := mth(leaves[:n])
		for m := 0; m < n; m++ {
			got, err := v.RootFromInclusionProof(int64(m), int64(n), path(m, leaves[:n]), leaves[m])
			if err != nil || hex.EncodeToString(got) != hex.EncodeToString(want) {
				bad += fmt.Sprintf(" (m=%d,n=%d err=%v)", m, n, err)
			}
		}
	}
	t.Check("rootfrominclusionproof-all-36-reference-proofs", bad == "", "mismatches:%s", bad)

	bad = ""
	for m := 0; m < len(leaves); m++ {
		if err := v.VerifyInclusionProof(int64(m), 8, path(m, leaves), refRoot, leaves[m]); err != nil {
			bad += fmt.Sprintf(" (m=%d: %v)", m, err)
		}
	}
	t.Check("verifyinclusionproof-eight-leaf-tree", bad == "", "failures:%s", bad)

	wrongRoot := append([]byte{}, refRoot...)
	wrongRoot[0] ^= 0xff
	t.Case("verifyinclusionproof-wrong-root-rejected",
		nonNil(v.VerifyInclusionProof(2, 8, path(2, leaves), wrongRoot, leaves[2]),
			"a proof verified against an altered root"))

	t.Case("verifyinclusionproof-wrong-leaf-index-rejected",
		nonNil(v.VerifyInclusionProof(3, 8, path(2, leaves), refRoot, leaves[2]),
			"leaf 2's proof verified at index 3"))

	corrupt := append([][]byte{}, path(2, leaves)...)
	corrupt[0] = append([]byte{}, corrupt[0]...)
	corrupt[0][0] ^= 0x01
	t.Case("verifyinclusionproof-corrupt-node-rejected",
		nonNil(v.VerifyInclusionProof(2, 8, corrupt, refRoot, leaves[2]),
			"a proof with a flipped bit still verified"))

	_, err := v.RootFromInclusionProof(8, 8, path(0, leaves), leaves[0])
	t.Case("rootfrominclusionproof-index-past-tree-rejected",
		nonNil(err, "leaf index 8 was accepted in a tree of size 8"))

	// --- consistency ---------------------------------------------------------
	bad = ""
	for n := 1; n <= len(leaves); n++ {
		for m := 1; m <= n; m++ {
			err := v.VerifyConsistencyProof(int64(m), int64(n),
				mth(leaves[:m]), mth(leaves[:n]), proof(m, leaves[:n]))
			if err != nil {
				bad += fmt.Sprintf(" (m=%d,n=%d: %v)", m, n, err)
			}
		}
	}
	t.Check("verifyconsistencyproof-all-reference-pairs", bad == "", "failures:%s", bad)

	t.Case("verifyconsistencyproof-snapshot-zero-needs-empty-proof",
		nonNil(v.VerifyConsistencyProof(0, 8, nil, refRoot, proof(1, leaves)),
			"a non-empty proof was accepted for snapshot 0"))

	t.Case("verifyconsistencyproof-altered-root-rejected",
		nonNil(v.VerifyConsistencyProof(3, 8, mth(leaves[:3]), wrongRoot, proof(3, leaves)),
			"a consistency proof verified against an altered second root"))

	t.Case("verifyconsistencyproof-reversed-snapshots-rejected",
		nonNil(v.VerifyConsistencyProof(8, 3, refRoot, mth(leaves[:3]), proof(3, leaves)),
			"snapshot1 > snapshot2 was accepted"))

	t.Done()
}
