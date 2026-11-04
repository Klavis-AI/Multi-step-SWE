// Group sth-signature-cache-distinct-inputs.
//
// The v1.0.7 advisory: no two distinct signing inputs may share a cached
// signature. This group changes one component of the STH signature input at a
// time -- the tree size, then a single bit of the root hash, then the
// timestamp -- and requires each change to produce a signature that is not the
// previous one and that verifies over its own RFC 6962 preimage, while an
// unchanged input is still served from the cache.
//
// v1.0.5 has no cache at all, so the final "unchanged input is served from the
// cache" case cannot hold there: it re-signs with a fresh ECDSA nonce.
package main

import "ctverify/vhelp"

func main() {
	t := vhelp.New()
	pub := loadPublicKey(t)

	const tag = "sth-distinct"
	h := newHarness(t, "cachedistinct", fixedRoot(pinnedNanos, pinnedTreeSize, tag))

	rootA := rootDigest(tag)
	r1, err := h.getSTH()
	if err != nil {
		t.Fatalf("baseline get-sth: %v", err)
	}
	t.Case("baseline-verifies", checkSTH(pub, r1, uint64(pinnedTreeSize), pinnedMillis, rootA))
	sigA := r1.sth.TreeHeadSignature

	// 1. tree size changes by one; everything else is identical.
	sizeB := pinnedTreeSize + 1
	h.setRoot(pinnedNanos, sizeB, rootA)
	r2, err := h.getSTH()
	if err != nil {
		t.Fatalf("tree-size get-sth: %v", err)
	}
	sigB := r2.sth.TreeHeadSignature
	t.Check("tree-size-change-resigns", !sameBytes(sigB, sigA),
		"a one-entry tree-size change was served the previous signature %x", sigA)
	t.Case("tree-size-change-verifies", checkSTH(pub, r2, uint64(sizeB), pinnedMillis, rootA))

	// 2. exactly one bit of the root hash changes.
	rootC := flipLastBit(rootA)
	h.setRoot(pinnedNanos, sizeB, rootC)
	r3, err := h.getSTH()
	if err != nil {
		t.Fatalf("root-hash get-sth: %v", err)
	}
	sigC := r3.sth.TreeHeadSignature
	t.Check("root-hash-one-bit-change-resigns", !sameBytes(sigC, sigB),
		"a one-bit root-hash change was served the previous signature %x", sigB)
	t.Case("root-hash-one-bit-change-verifies", checkSTH(pub, r3, uint64(sizeB), pinnedMillis, rootC))

	// 3. the timestamp changes by one millisecond.
	nanosD := pinnedNanos + 1000000
	millisD := pinnedMillis + 1
	h.setRoot(nanosD, sizeB, rootC)
	r4, err := h.getSTH()
	if err != nil {
		t.Fatalf("timestamp get-sth: %v", err)
	}
	sigD := r4.sth.TreeHeadSignature
	t.Check("timestamp-change-resigns", !sameBytes(sigD, sigC),
		"a one-millisecond timestamp change was served the previous signature %x", sigC)
	t.Case("timestamp-change-verifies", checkSTH(pub, r4, uint64(sizeB), millisD, rootC))

	// 4. nothing changes: the cache must now serve the last signature.
	r5, err := h.getSTH()
	if err != nil {
		t.Fatalf("repeat get-sth: %v", err)
	}
	t.Check("unchanged-input-served-from-cache", sameBytes(r5.sth.TreeHeadSignature, sigD),
		"an unchanged signing input produced a second, different signature %x (previous %x)",
		r5.sth.TreeHeadSignature, sigD)

	t.Done()
}
