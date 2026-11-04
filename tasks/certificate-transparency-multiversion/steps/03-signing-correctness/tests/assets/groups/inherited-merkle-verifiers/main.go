// inherited-merkle-verifiers re-observes the inclusion- and consistency-proof
// verifiers that step 1 introduced and that this release must not have
// changed. It is an inherited-* group, so only the step-03 check is
// meaningful.
//
// Everything here is derived from RFC 6962 section 2.1 read directly: the leaf
// hash is SHA-256 over 0x00 || leaf, the node hash is SHA-256 over 0x01 || left
// || right, an odd node at any level is promoted unchanged, and the audit path
// and consistency proof are the recursive definitions in sections 2.1.1 and
// 2.1.2. The expected roots and audit paths below were computed from those
// definitions by hand (independently of this package) for a nine-leaf tree
// whose leaves are the single bytes 0x01..0x09, so agreement here is evidence
// about the verifier and not a restatement of its own arithmetic.
//
// The consistency proofs are produced here rather than transcribed: the RFC's
// PROOF/SUBPROOF recursion is run over the driver's own Merkle tree hash, the
// same independent implementation whose roots are cross-checked against the
// RFC's values for every tree size from one to nine. That generator is itself
// anchored to hand-derived proofs for eight (m, n) pairs chosen to cover the
// balanced case, the promoted-subtree case and the right-subtree descent, so a
// slip in the recursion cannot quietly agree with a slip in the package.
//
// The negative cases matter as much as the positive ones: a verifier that
// returns nil for everything would pass every accept case, so each proof is
// also replayed with one component corrupted, with a component removed, with
// an extra component appended, and against a root from a different tree size.
package main

import (
	"bytes"
	"crypto/sha256"
	"encoding/hex"

	"github.com/google/certificate-transparency-go/merkletree"

	"ctverify/vhelp"
)

// leafHashes for the nine-leaf tree: leaf i (zero-based) is the byte i+1.
func leafHashes(n int) [][]byte {
	out := make([][]byte, n)
	for i := 0; i < n; i++ {
		out[i] = leafHash([]byte{byte(i + 1)})
	}
	return out
}

// leafHash is RFC 6962 s2.1: SHA-256(0x00 || leaf).
func leafHash(leaf []byte) []byte {
	h := sha256.New()
	h.Write([]byte{0x00})
	h.Write(leaf)
	return h.Sum(nil)
}

// nodeHash is RFC 6962 s2.1: SHA-256(0x01 || left || right).
func nodeHash(l, r []byte) []byte {
	h := sha256.New()
	h.Write([]byte{0x01})
	h.Write(l)
	h.Write(r)
	return h.Sum(nil)
}

// rootFromLeaves computes the RFC 6962 Merkle tree hash of a leaf-hash slice,
// promoting an unpaired node at every level unchanged.
func rootFromLeaves(lv [][]byte) []byte {
	cur := lv
	for len(cur) > 1 {
		var next [][]byte
		i := 0
		for ; i+1 < len(cur); i += 2 {
			next = append(next, nodeHash(cur[i], cur[i+1]))
		}
		if i < len(cur) {
			next = append(next, cur[i])
		}
		cur = next
	}
	return cur[0]
}

// largestPowerOfTwoSmallerThan is the RFC 6962 "k": the largest power of two
// strictly smaller than n, the split point of both MTH and SUBPROOF.
func largestPowerOfTwoSmallerThan(n int) int {
	k := 1
	for k*2 < n {
		k *= 2
	}
	return k
}

// subproof is RFC 6962 s2.1.2 SUBPROOF(m, lv, b) verbatim, over a slice of
// leaf hashes rather than of leaves. b records whether MTH(lv[0:m]) is already
// known to the verifier, which is what decides whether the terminating case
// emits a commitment or nothing at all.
func subproof(m int, lv [][]byte, b bool) [][]byte {
	if m == len(lv) {
		if b {
			return nil
		}
		return [][]byte{rootFromLeaves(lv)}
	}
	k := largestPowerOfTwoSmallerThan(len(lv))
	if m <= k {
		// The right subtree exists only in the newer tree: prove the left
		// subtree consistent and commit to the right one.
		return append(subproof(m, lv[:k], b), rootFromLeaves(lv[k:]))
	}
	// The left subtree is shared: prove the right subtree consistent and
	// commit to the left one.
	return append(subproof(m-k, lv[k:], false), rootFromLeaves(lv[:k]))
}

// consistencyProof is RFC 6962 s2.1.2 PROOF(m, D[n]) for the nine-leaf corpus.
func consistencyProof(m, n int) [][]byte {
	return subproof(m, leafHashes(n), true)
}

func joinHex(bs [][]byte) string {
	out := ""
	for i, b := range bs {
		if i > 0 {
			out += ","
		}
		out += hex.EncodeToString(b)
	}
	return out
}

func hexes(hs []string) [][]byte {
	out := make([][]byte, len(hs))
	for i, s := range hs {
		b, err := hex.DecodeString(s)
		if err != nil || len(b) != sha256.Size {
			panic("bad vector " + s)
		}
		out[i] = b
	}
	return out
}

func mustHex(s string) []byte {
	b, err := hex.DecodeString(s)
	if err != nil {
		panic(err)
	}
	return b
}

// The nine leaf hashes, so the expected roots and proof components can be
// derived in the driver rather than trusted as constants. These are the four
// distinct subtree roots the tree is built from; listing them lets the driver
// rebuild every root below and compare, which is the check that the constants
// themselves are the RFC's values and not a transcription of the package's
// output.
var (
	l0 = leafHash([]byte{0x01})
	l1 = leafHash([]byte{0x02})
	l2 = leafHash([]byte{0x03})
	l3 = leafHash([]byte{0x04})
	l4 = leafHash([]byte{0x05})
	l5 = leafHash([]byte{0x06})
	l6 = leafHash([]byte{0x07})
	l7 = leafHash([]byte{0x08})
	l8 = leafHash([]byte{0x09})
)

// n01 = node(l0,l1), n23 = node(l2,l3), n45, n67, n0123, n4567, n01234567.
var (
	n01       = nodeHash(l0, l1)
	n23       = nodeHash(l2, l3)
	n45       = nodeHash(l4, l5)
	n67       = nodeHash(l6, l7)
	n0123     = nodeHash(n01, n23)
	n4567     = nodeHash(n45, n67)
	n01234567 = nodeHash(n0123, n4567)
	// n7r46 is MTH(D[4:7]) for the seven-leaf tree: the promotion of the
	// five-leaf subtree leaves node(l4,l5) paired with l6, with l6 promoted.
	n7r46      = nodeHash(n45, l6)
	n012345678 = nodeHash(n01234567, l8)
)

func main() {
	t := vhelp.New()

	verifier := merkletree.NewMerkleVerifier(sha256Sum)
	hasher := merkletree.NewTreeHasher(sha256Sum)

	// ---- the tree-hasher primitives themselves ----------------------------

	t.EqualBytes("hash-empty-is-sha256-of-nothing",
		hasher.HashEmpty(), mustHex("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"))

	t.EqualBytes("hash-leaf-of-0x01", hasher.HashLeaf([]byte{0x01}), l0)
	t.EqualBytes("hash-children-of-two-leaves",
		hasher.HashChildren(l0, l1), n01)

	// A leaf hash is not the same as the hash of the leaf's content: the 0x00
	// domain-separation byte is what stops a leaf from being confused with an
	// interior node.
	plain := sha256.Sum256([]byte{0x01})
	t.Check("leaf-hash-is-domain-separated", !bytes.Equal(l0, plain[:]),
		"HashLeaf(x) equals SHA-256(x); the RFC 6962 0x00 prefix is missing")

	// ---- known roots, rebuilt from the leaves -----------------------------

	// Root of n=1 is the lone leaf hash; root of n=2..9 per RFC 6962.
	//
	//   1 b413f47d…  2 6bcf0e2e…  3 e2da0242…  4 fa02d31a…  5 b2165c86…
	//   6 de4cafcc…  7 5e932791…  8 c1ad6548…  9 2b690a5a…
	wantRoots := map[int]string{
		1: "b413f47d13ee2fe6c845b2ee141af81de858df4ec549a58b7970bb96645bc8d2",
		2: "6bcf0e2e93e0a18e22789aee965e6553f4fbe93f0acfc4a705d691c8311c4965",
		3: "e2da0242936eb38ec996a543601b3a1da4226391ff92014ed1a7a248ace36347",
		4: "fa02d31a63cc11cc624881e52af14af7a1c6ab745efa71021cb24086b9b1793f",
		5: "b2165c86fbfb34fa51840bb6ba3d5ce0d7dc31f2e605b5ba62c98fa86ff6746d",
		6: "de4cafcc35ff7b182a17d3149d1a5b3f3e41159aaf6e2e37131d9fbb050bec3f",
		7: "5e932791a9098819cce0ed256d1360ac88673390b07e5fe662b2a11ab0f4dde9",
		8: "c1ad6548cb4c7663110df219ec8b36ca63b01158956f4be31a38a88d0c7f7071",
		9: "2b690a5a1dc7f4daa1df140fb70d040abab5cfd86651f9ce0962f58851448d5c",
	}

	// Rebuild each root two ways: through the package's hasher over the leaf
	// bytes, and from the driver's own node arithmetic. They must agree, and
	// both must agree with the RFC's value. A drifting hash means every proof
	// below is meaningless.
	for n := 1; n <= 9; n++ {
		lv := leafHashes(n)
		viaPkg := rootFromLeavesViaHasher(hasher, lv)
		viaOwn := rootFromLeaves(lv)
		want := mustHex(wantRoots[n])
		t.Check("root-crosscheck-via-own-arithmetic-n"+itoa(n),
			bytes.Equal(viaOwn, want),
			"n=%d driver-built root %x, RFC value %x", n, viaOwn, want)
		t.Check("root-crosscheck-via-package-hasher-n"+itoa(n),
			bytes.Equal(viaPkg, want),
			"n=%d package-built root %x, RFC value %x", n, viaPkg, want)
	}

	// The nine-leaf tree's root, spelled out from the subtree names above, so
	// the constant is anchored to the leaves and not only to itself.
	t.Check("nine-leaf-root-is-node-of-eight-and-nine",
		bytes.Equal(hasher.HashChildren(n01234567, l8), n012345678) &&
			bytes.Equal(n012345678, mustHex(wantRoots[9])),
		"n=9 root %x is not node(root(8), leaf(9))", n012345678)

	// ---- inclusion proofs: accepted ---------------------------------------

	// Audit paths, RFC 6962 s2.1.1, for the nine-leaf tree. Each is the list of
	// sibling hashes from the leaf level upward, promoted nodes omitted.
	type incCase struct {
		id    string
		index int
		size  int
		leaf  byte
		proof []string
	}
	incCases := []incCase{
		// RFC 6962 s2.1.1 audit paths, in the order the tree is climbed from
		// the leaf upward. `n7r46` is the promoted subtree MTH(D[4:7]), which
		// is node(root(4..5), leaf(6)) and therefore not a node of any
		// balanced tree of the same size.
		//
		// n = 9, k = 8.
		{"include-n9-leaf0", 0, 9, 0x01, []string{hexOf(l1), hexOf(n23), hexOf(n4567), hexOf(l8)}},
		{"include-n9-leaf1", 1, 9, 0x02, []string{hexOf(l0), hexOf(n23), hexOf(n4567), hexOf(l8)}},
		{"include-n9-leaf2", 2, 9, 0x03, []string{hexOf(l3), hexOf(n01), hexOf(n4567), hexOf(l8)}},
		{"include-n9-leaf3", 3, 9, 0x04, []string{hexOf(l2), hexOf(n01), hexOf(n4567), hexOf(l8)}},
		{"include-n9-leaf4", 4, 9, 0x05, []string{hexOf(l5), hexOf(n67), hexOf(n0123), hexOf(l8)}},
		{"include-n9-leaf5", 5, 9, 0x06, []string{hexOf(l4), hexOf(n67), hexOf(n0123), hexOf(l8)}},
		{"include-n9-leaf6", 6, 9, 0x07, []string{hexOf(l7), hexOf(n45), hexOf(n0123), hexOf(l8)}},
		{"include-n9-leaf7", 7, 9, 0x08, []string{hexOf(l6), hexOf(n45), hexOf(n0123), hexOf(l8)}},
		{"include-n9-leaf8", 8, 9, 0x09, []string{hexOf(n01234567)}},
		// n = 8.
		{"include-n8-leaf0", 0, 8, 0x01, []string{hexOf(l1), hexOf(n23), hexOf(n4567)}},
		{"include-n8-leaf3", 3, 8, 0x04, []string{hexOf(l2), hexOf(n01), hexOf(n4567)}},
		{"include-n8-leaf4", 4, 8, 0x05, []string{hexOf(l5), hexOf(n67), hexOf(n0123)}},
		{"include-n8-leaf7", 7, 8, 0x08, []string{hexOf(l6), hexOf(n45), hexOf(n0123)}},
		// n = 7, k = 4.
		{"include-n7-leaf0", 0, 7, 0x01, []string{hexOf(l1), hexOf(n23), hexOf(n7r46)}},
		{"include-n7-leaf3", 3, 7, 0x04, []string{hexOf(l2), hexOf(n01), hexOf(n7r46)}},
		{"include-n7-leaf4", 4, 7, 0x05, []string{hexOf(l5), hexOf(l6), hexOf(n0123)}},
		{"include-n7-leaf5", 5, 7, 0x06, []string{hexOf(l4), hexOf(l6), hexOf(n0123)}},
		{"include-n7-leaf6", 6, 7, 0x07, []string{hexOf(n45), hexOf(n0123)}},
		// n = 6, k = 4.
		{"include-n6-leaf0", 0, 6, 0x01, []string{hexOf(l1), hexOf(n23), hexOf(n45)}},
		{"include-n6-leaf2", 2, 6, 0x03, []string{hexOf(l3), hexOf(n01), hexOf(n45)}},
		{"include-n6-leaf4", 4, 6, 0x05, []string{hexOf(l5), hexOf(n0123)}},
		{"include-n6-leaf5", 5, 6, 0x06, []string{hexOf(l4), hexOf(n0123)}},
		// n = 5, k = 4.
		{"include-n5-leaf0", 0, 5, 0x01, []string{hexOf(l1), hexOf(n23), hexOf(l4)}},
		{"include-n5-leaf2", 2, 5, 0x03, []string{hexOf(l3), hexOf(n01), hexOf(l4)}},
		{"include-n5-leaf4", 4, 5, 0x05, []string{hexOf(n0123)}},
		// n = 3, k = 2.
		{"include-n3-leaf0", 0, 3, 0x01, []string{hexOf(l1), hexOf(l2)}},
		{"include-n3-leaf1", 1, 3, 0x02, []string{hexOf(l0), hexOf(l2)}},
		{"include-n3-leaf2", 2, 3, 0x03, []string{hexOf(n01)}},
		// n = 2, k = 1.
		{"include-n2-leaf0", 0, 2, 0x01, []string{hexOf(l1)}},
		{"include-n2-leaf1", 1, 2, 0x02, []string{hexOf(l0)}},
		// n = 1: the path is empty and the leaf hash is the root.
		{"include-n1-leaf0", 0, 1, 0x01, []string{}},
	}
	for _, c := range incCases {
		root := mustHex(wantRoots[c.size])
		proof := hexes(c.proof)
		err := verifier.VerifyInclusionProof(int64(c.index), int64(c.size), proof, root, []byte{c.leaf})
		t.Case(c.id, err)

		// The by-hash entry point must agree with the by-leaf one for the same
		// leaf: it is the same computation with the leaf hash supplied.
		errHash := verifier.VerifyInclusionProofByHash(int64(c.index), int64(c.size), proof, root, leafHash([]byte{c.leaf}))
		t.Case(c.id+"-by-hash", errHash)

		// RootFromInclusionProof must return the same root the verifier
		// compared against, which pins the function the verifier delegates to.
		calc, errRoot := verifier.RootFromInclusionProof(int64(c.index), int64(c.size), proof, []byte{c.leaf})
		if errRoot != nil {
			t.Fatalf("%s: RootFromInclusionProof: %v", c.id, errRoot)
		}
		t.EqualBytes(c.id+"-reconstructed-root", calc, root)
	}

	// ---- inclusion proofs: refused ----------------------------------------

	// Replaying each size-9 proof with the wrong leaf index must fail, because
	// the sibling order is fixed by the index.
	for _, c := range incCases {
		if c.size != 9 {
			continue
		}
		root := mustHex(wantRoots[9])
		proof := hexes(c.proof)
		wrong := int64((c.index + 1) % 9)
		err := verifier.VerifyInclusionProof(wrong, 9, proof, root, []byte{c.leaf})
		t.Check("include-wrong-index-refused-"+c.id, err != nil,
			"index %d accepted for leaf %d's proof", wrong, c.index)
	}

	// A corrupted sibling must fail. Only one component is flipped, and the
	// flip is in the high bit of the first octet so the value stays a valid
	// 32-octet hash.
	for _, c := range incCases {
		if len(c.proof) == 0 {
			continue
		}
		root := mustHex(wantRoots[c.size])
		proof := hexes(c.proof)
		bad := make([][]byte, len(proof))
		copy(bad, proof)
		bad[0] = flipFirstBit(bad[0])
		err := verifier.VerifyInclusionProof(int64(c.index), int64(c.size), bad, root, []byte{c.leaf})
		t.Check("include-corrupt-sibling-refused-"+c.id, err != nil,
			"a corrupted sibling still verified for %s", c.id)
	}

	// A proof with a component removed must fail: the tree is not fully
	// covered by the remaining nodes.
	for _, c := range incCases {
		if len(c.proof) < 2 {
			continue
		}
		root := mustHex(wantRoots[c.size])
		proof := hexes(c.proof)
		err := verifier.VerifyInclusionProof(int64(c.index), int64(c.size), proof[:len(proof)-1], root, []byte{c.leaf})
		t.Check("include-short-proof-refused-"+c.id, err != nil,
			"a proof with a component removed still verified for %s", c.id)
	}

	// A proof with an extra component appended must fail: VerifyInclusionProof
	// insists the proof is consumed exactly, so a padded proof is not accepted.
	for _, c := range incCases {
		root := mustHex(wantRoots[c.size])
		proof := hexes(c.proof)
		padded := append(append([][]byte{}, proof...), l8)
		err := verifier.VerifyInclusionProof(int64(c.index), int64(c.size), padded, root, []byte{c.leaf})
		t.Check("include-padded-proof-refused-"+c.id, err != nil,
			"a proof with an extra component still verified for %s", c.id)
	}

	// The complete audit path for leaf 0 of the nine-leaf tree. It is four
	// components; the eight-leaf tree's path for the same leaf is the first
	// three of them. The two cases below change exactly one argument each.
	nineLeaf0Path := hexes([]string{hexOf(l1), hexOf(n23), hexOf(n4567), hexOf(l8)})

	// Right leaf, right root, wrong tree size. At treeSize 8 the climb stops
	// one level early, so the fourth component is left unconsumed and the
	// verifier must refuse rather than accept a path it did not finish
	// reading. (The reverse framing -- the eight-leaf path replayed at
	// treeSize 9 -- is a short proof, not a wrong-size one, and is covered by
	// the short-proof loop above.)
	{
		err := verifier.VerifyInclusionProof(0, 8, nineLeaf0Path,
			mustHex(wantRoots[8]), []byte{0x01})
		t.Check("include-wrong-tree-size-refused", err != nil,
			"the nine-leaf audit path for leaf 0 was accepted at treeSize 8")
	}

	// Right leaf, right proof, right tree size, wrong root: the verifier has
	// to rebuild the nine-leaf root and notice it is not the one it was given.
	{
		err := verifier.VerifyInclusionProof(0, 9, nineLeaf0Path,
			mustHex(wantRoots[8]), []byte{0x01})
		t.Check("include-wrong-root-refused", err != nil,
			"a complete nine-leaf audit path verified against the eight-leaf root")
	}

	// Out-of-range indices and sizes must be refused rather than panic or
	// silently succeed.
	{
		err := verifier.VerifyInclusionProof(9, 9, nil, mustHex(wantRoots[9]), []byte{0x01})
		t.Check("include-index-equal-size-refused", err != nil,
			"leafIndex == treeSize was accepted")
		err = verifier.VerifyInclusionProof(-1, 9, nil, mustHex(wantRoots[9]), []byte{0x01})
		t.Check("include-negative-index-refused", err != nil,
			"a negative leafIndex was accepted")
		err = verifier.VerifyInclusionProof(0, 0, nil, mustHex(wantRoots[9]), []byte{0x01})
		t.Check("include-zero-tree-size-refused", err != nil,
			"treeSize 0 was accepted")
	}

	// ---- consistency proofs: accepted -------------------------------------
	//
	// RFC 6962 s2.1.2. Each proof is the list of nodes the RFC's PROOF
	// recursion emits for (m, D[0:n]), bottom-up.
	type conCase struct {
		id string
		s1 int
		s2 int
		pf [][]byte
	}
	con := func(s1, s2 int) conCase {
		return conCase{
			id: "consistency-" + itoa(s1) + "-to-" + itoa(s2),
			s1: s1, s2: s2,
			pf: consistencyProof(s1, s2),
		}
	}
	conCases := []conCase{
		con(1, 2), con(1, 3), con(2, 3), con(3, 4), con(2, 5),
		con(3, 8), con(4, 5), con(3, 7), con(2, 6), con(1, 8),
		con(5, 8), con(4, 7), con(1, 4), con(2, 8), con(3, 5),
		con(6, 8), con(5, 6), con(1, 9), con(8, 9), con(3, 9),
	}

	// The generator is pinned to hand-derived proofs before anything is asked
	// of the package. {1,2} and {2,3} are the two-leaf and promoted three-leaf
	// bases; {3,4} descends into the right subtree with b=false and so carries
	// the two single-leaf commitments the naive reading drops; {4,5} and {8,9}
	// are the balanced-prefix cases where the proof is a single commitment;
	// {2,6} and {4,7} commit to a promoted subtree, MTH(D[4:6]) and MTH(D[4:7])
	// respectively, which is where a proof written from a balanced picture of
	// the tree goes wrong; {1,8} is the fully balanced ladder.
	conLiterals := []struct {
		m, n int
		want []string
	}{
		{1, 2, []string{hexOf(l1)}},
		{2, 3, []string{hexOf(l2)}},
		{3, 4, []string{hexOf(l2), hexOf(l3), hexOf(n01)}},
		{4, 5, []string{hexOf(l4)}},
		{2, 6, []string{hexOf(n23), hexOf(n45)}},
		{4, 7, []string{hexOf(n7r46)}},
		{1, 8, []string{hexOf(l1), hexOf(n23), hexOf(n4567)}},
		{8, 9, []string{hexOf(l8)}},
	}
	for _, c := range conLiterals {
		t.Equal("consistency-proof-crosscheck-"+itoa(c.m)+"-to-"+itoa(c.n),
			joinHex(consistencyProof(c.m, c.n)), joinHex(hexes(c.want)))
	}

	for _, c := range conCases {
		root1 := mustHex(wantRoots[c.s1])
		root2 := mustHex(wantRoots[c.s2])
		err := verifier.VerifyConsistencyProof(int64(c.s1), int64(c.s2), root1, root2, c.pf)
		t.Case(c.id, err)
	}

	// A snapshot is trivially consistent with itself and demands an empty
	// proof.
	{
		err := verifier.VerifyConsistencyProof(4, 4, mustHex(wantRoots[4]), mustHex(wantRoots[4]), nil)
		t.Case("consistency-equal-snapshots-empty-proof", err)
		err = verifier.VerifyConsistencyProof(4, 4, mustHex(wantRoots[4]), mustHex(wantRoots[4]), hexes([]string{hexOf(l4)}))
		t.Check("consistency-equal-snapshots-nonempty-proof-refused", err != nil,
			"a non-empty proof was accepted for snapshot1 == snapshot2")
	}

	// Snapshot 0 is consistent with anything, and likewise requires an empty
	// proof.
	{
		err := verifier.VerifyConsistencyProof(0, 9, nil, mustHex(wantRoots[9]), nil)
		t.Case("consistency-0-to-9-empty-proof", err)
		err = verifier.VerifyConsistencyProof(0, 9, nil, mustHex(wantRoots[9]), hexes([]string{hexOf(l0)}))
		t.Check("consistency-0-to-9-nonempty-proof-refused", err != nil,
			"a non-empty proof was accepted for snapshot1 == 0")
	}

	// ---- consistency proofs: refused --------------------------------------

	// snapshot1 > snapshot2 is not a question the proof can answer.
	{
		err := verifier.VerifyConsistencyProof(5, 3, mustHex(wantRoots[5]), mustHex(wantRoots[3]), hexes([]string{hexOf(l4)}))
		t.Check("consistency-reversed-snapshots-refused", err != nil,
			"snapshot1 > snapshot2 was accepted")
	}

	// Corrupting either root must fail. These are separate cases because the
	// two roots are checked at different points and a verifier that stopped
	// after the first would still accept a bad second root.
	for _, c := range conCases {
		root1 := mustHex(wantRoots[c.s1])
		root2 := mustHex(wantRoots[c.s2])
		proof := c.pf
		err := verifier.VerifyConsistencyProof(int64(c.s1), int64(c.s2), flipFirstBit(root1), root2, proof)
		t.Check("consistency-corrupt-root1-refused-"+c.id, err != nil,
			"a corrupted root1 still verified for %s", c.id)
		err = verifier.VerifyConsistencyProof(int64(c.s1), int64(c.s2), root1, flipFirstBit(root2), proof)
		t.Check("consistency-corrupt-root2-refused-"+c.id, err != nil,
			"a corrupted root2 still verified for %s", c.id)
	}

	// Corrupting a proof component must fail.
	for _, c := range conCases {
		proof := c.pf
		bad := make([][]byte, len(proof))
		copy(bad, proof)
		bad[0] = flipFirstBit(bad[0])
		err := verifier.VerifyConsistencyProof(int64(c.s1), int64(c.s2),
			mustHex(wantRoots[c.s1]), mustHex(wantRoots[c.s2]), bad)
		t.Check("consistency-corrupt-component-refused-"+c.id, err != nil,
			"a corrupted proof component still verified for %s", c.id)
	}

	// A short proof and a padded proof must both fail.
	for _, c := range conCases {
		proof := c.pf
		if len(proof) >= 2 {
			err := verifier.VerifyConsistencyProof(int64(c.s1), int64(c.s2),
				mustHex(wantRoots[c.s1]), mustHex(wantRoots[c.s2]), proof[:len(proof)-1])
			t.Check("consistency-short-proof-refused-"+c.id, err != nil,
				"a proof with a component removed still verified for %s", c.id)
		}
		padded := append(append([][]byte{}, proof...), l0)
		err := verifier.VerifyConsistencyProof(int64(c.s1), int64(c.s2),
			mustHex(wantRoots[c.s1]), mustHex(wantRoots[c.s2]), padded)
		t.Check("consistency-padded-proof-refused-"+c.id, err != nil,
			"a proof with an extra component still verified for %s", c.id)
	}

	// A proof between the right snapshots but for a different pair of trees
	// must fail: cross-wiring snapshot sizes is the classic way a verifier
	// that ignores one of its arguments goes unnoticed.
	{
		err := verifier.VerifyConsistencyProof(2, 5, mustHex(wantRoots[2]), mustHex(wantRoots[5]),
			hexes([]string{hexOf(n23), hexOf(l4)}))
		t.Case("consistency-2-to-5-correct-pair", err)
		err = verifier.VerifyConsistencyProof(2, 5, mustHex(wantRoots[3]), mustHex(wantRoots[5]),
			hexes([]string{hexOf(n23), hexOf(l4)}))
		t.Check("consistency-2-to-5-wrong-root1-refused", err != nil,
			"the 2-to-5 proof verified with root1 taken from tree size 3")
	}

	// A consistency proof must not be usable as an inclusion proof and vice
	// versa: the two verifiers take differently shaped arguments and must not
	// both accept the same bytes. Feeding the 3-to-4 consistency proof to the
	// inclusion verifier must fail.
	{
		err := verifier.VerifyInclusionProof(3, 4,
			consistencyProof(3, 4), mustHex(wantRoots[4]), []byte{0x04})
		t.Check("consistency-proof-not-an-inclusion-proof", err != nil,
			"the 3-to-4 consistency proof was accepted as an inclusion proof")
	}

	// What makes the refusal just above a root mismatch rather than a
	// complaint about a malformed proof is observable without naming any Go
	// type: RootFromInclusionProof, the function the verifier delegates to,
	// succeeds on exactly those arguments and hands back the root the path
	// really reconstructs. That root is the nine-leaf root, and it is not the
	// eight-leaf root the verifier was asked to accept.
	{
		calc, errRoot := verifier.RootFromInclusionProof(0, 9, nineLeaf0Path, []byte{0x01})
		t.Check("include-root-mismatch-reconstructs-nine-leaf-root",
			errRoot == nil && len(calc) == sha256.Size && bytes.Equal(calc, mustHex(wantRoots[9])),
			"RootFromInclusionProof returned (%x, %v), want the nine-leaf root and no error", calc, errRoot)
		t.Check("include-root-mismatch-reconstructed-root-differs-from-argument",
			errRoot == nil && !bytes.Equal(calc, mustHex(wantRoots[8])),
			"RootFromInclusionProof handed back the very root the proof does not support: %x", calc)
	}

	t.Done()
}

// rootFromLeavesViaHasher builds the tree with the package's own hasher, so the
// driver's arithmetic above is checked against the package rather than only
// against itself.
func rootFromLeavesViaHasher(h *merkletree.TreeHasher, lv [][]byte) []byte {
	cur := lv
	for len(cur) > 1 {
		var next [][]byte
		i := 0
		for ; i+1 < len(cur); i += 2 {
			next = append(next, h.HashChildren(cur[i], cur[i+1]))
		}
		if i < len(cur) {
			next = append(next, cur[i])
		}
		cur = next
	}
	return cur[0]
}

// sha256Sum adapts crypto/sha256 to the package's HasherFunc, which is
// func([]byte) []byte rather than func() hash.Hash.
func sha256Sum(b []byte) []byte {
	s := sha256.Sum256(b)
	return s[:]
}

func flipFirstBit(b []byte) []byte {
	out := make([]byte, len(b))
	copy(out, b)
	out[0] ^= 0x80
	return out
}

func hexOf(b []byte) string { return hex.EncodeToString(b) }

func itoa(n int) string {
	if n == 0 {
		return "0"
	}
	var buf [20]byte
	i := len(buf)
	for n > 0 {
		i--
		buf[i] = byte('0' + n%10)
		n /= 10
	}
	return string(buf[i:])
}
