// Group merkletree-inclusion-by-hash.
//
// v1.0.5 splits the inclusion-proof verifier so that a caller who already
// holds the leaf *hash* -- which is what a log serves in a
// get-proof-by-hash exchange, and all an auditor of someone else's entry ever
// has -- can verify without being handed the leaf bytes. The release adds
// MerkleVerifier.RootFromInclusionProofAndHash and
// MerkleVerifier.VerifyInclusionProofByHash, and reimplements the existing
// leaf-taking entry points on top of them.
//
// The expected roots and audit paths here are computed from RFC 6962 section
// 2.1 by a reference implementation in this driver, over pinned leaf data.
package main

import (
	"bytes"
	"crypto/sha256"
	"encoding/hex"

	"github.com/google/certificate-transparency-go/merkletree"

	"ctverify/vhelp"
)

func sha(b []byte) []byte { h := sha256.Sum256(b); return h[:] }

// --- RFC 6962 s2.1 reference implementation, benchmark-owned. --------------

func mth(d [][]byte) []byte {
	switch len(d) {
	case 0:
		return sha(nil)
	case 1:
		return sha(append([]byte{0}, d[0]...))
	}
	k := split(len(d))
	return sha(append(append([]byte{1}, mth(d[:k])...), mth(d[k:])...))
}

// path returns the audit path for leaf m of the tree over d.
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

// split returns the largest power of two strictly less than n.
func split(n int) int {
	k := 1
	for k*2 < n {
		k *= 2
	}
	return k
}

func leaves(n int) [][]byte {
	out := make([][]byte, 0, n)
	for i := 0; i < n; i++ {
		out = append(out, []byte("harbor-ct-step02-merkle-leaf-"+string(rune('0'+i))))
	}
	return out
}

func main() {
	t := vhelp.New()

	v := merkletree.NewMerkleVerifier(sha)
	hasher := merkletree.NewTreeHasher(sha)

	four := leaves(4)
	rootFour := mth(four)

	// The hash-taking reconstruction rebuilds the root from every position.
	var got, want []string
	for i := range four {
		r, err := v.RootFromInclusionProofAndHash(int64(i), int64(len(four)), path(i, four), hasher.HashLeaf(four[i]))
		if err != nil {
			got = append(got, "error: "+err.Error())
		} else {
			got = append(got, hex.EncodeToString(r))
		}
		want = append(want, hex.EncodeToString(rootFour))
	}
	t.Equal("root-from-hash-reconstructs-the-tree-root", got, want)

	// And agrees, position by position, with the leaf-taking entry point that
	// this release reimplemented on top of it.
	agree := true
	for i := range four {
		byHash, err1 := v.RootFromInclusionProofAndHash(int64(i), int64(len(four)), path(i, four), hasher.HashLeaf(four[i]))
		byLeaf, err2 := v.RootFromInclusionProof(int64(i), int64(len(four)), path(i, four), four[i])
		if err1 != nil || err2 != nil || !bytes.Equal(byHash, byLeaf) {
			agree = false
		}
	}
	t.Check("root-from-hash-agrees-with-root-from-leaf", agree,
		"the two entry points disagreed on a four-leaf tree")

	// Verification, over a power-of-two tree and an odd-sized one.
	for _, tc := range []struct {
		id string
		n  int
	}{
		{"verify-by-hash-accepts-every-leaf-of-a-power-of-two-tree", 4},
		{"verify-by-hash-accepts-every-leaf-of-an-odd-sized-tree", 7},
	} {
		d := leaves(tc.n)
		root := mth(d)
		var bad []string
		for i := range d {
			if err := v.VerifyInclusionProofByHash(int64(i), int64(tc.n), path(i, d), root, hasher.HashLeaf(d[i])); err != nil {
				bad = append(bad, hex.EncodeToString([]byte{byte(i)})+": "+err.Error())
			}
		}
		t.Equal(tc.id, bad, []string(nil))
	}

	one := leaves(1)
	t.Check("verify-by-hash-accepts-the-single-leaf-tree-with-an-empty-proof",
		v.VerifyInclusionProofByHash(0, 1, [][]byte{}, mth(one), hasher.HashLeaf(one[0])) == nil,
		"a one-leaf tree did not verify with an empty proof")

	// Refusals.
	t.Check("verify-by-hash-rejects-a-wrong-root",
		v.VerifyInclusionProofByHash(1, 4, path(1, four), sha([]byte("not the root")), hasher.HashLeaf(four[1])) != nil,
		"a proof verified against the wrong root")

	t.Check("verify-by-hash-rejects-a-wrong-leaf-hash",
		v.VerifyInclusionProofByHash(1, 4, path(1, four), rootFour, hasher.HashLeaf([]byte("not a leaf of this tree"))) != nil,
		"a proof verified for a leaf hash that is not in the tree")

	t.Check("verify-by-hash-rejects-an-index-outside-the-tree",
		v.VerifyInclusionProofByHash(4, 4, path(3, four), rootFour, hasher.HashLeaf(four[3])) != nil,
		"leafIndex == treeSize was accepted")

	short := path(1, four)
	t.Check("verify-by-hash-rejects-a-truncated-proof",
		v.VerifyInclusionProofByHash(1, 4, short[:len(short)-1], rootFour, hasher.HashLeaf(four[1])) != nil,
		"a proof with a missing node was accepted")

	t.Done()
}
