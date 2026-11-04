// Group sth-signature-cache-stable.
//
// v1.0.10 caches the signature it produced for the last STH signature input
// and returns the cached bytes for an identical input. Observable consequence:
// repeated get-sth over an unchanged tree head yields a byte-identical
// tree_head_signature, and every one of them still verifies over the RFC 6962
// signature input under the pinned log key.
//
// v1.0.5 has no cache and re-signs on every call. Its signer is the pinned
// EC P-256 log key, so ECDSA's per-signature nonce makes two signatures over
// the same input differ; the identity cases below cannot hold there.
package main

import "ctverify/vhelp"

func main() {
	t := vhelp.New()
	pub := loadPublicKey(t)

	const tag = "sth-stable"
	h := newHarness(t, "cachestable", fixedRoot(pinnedNanos, pinnedTreeSize, tag))
	want := rootDigest(tag)

	sigs := make([][]byte, 0, 12)
	verr := make([]error, 0, 12)
	for i := 0; i < 12; i++ {
		r, err := h.getSTH()
		if err != nil {
			t.Fatalf("call %d: %v", i+1, err)
		}
		verr = append(verr, checkSTH(pub, r, uint64(pinnedTreeSize), pinnedMillis, want))
		sigs = append(sigs, r.sth.TreeHeadSignature)
	}

	t.Case("call-1-verifies", verr[0])
	t.Case("call-2-verifies", verr[1])
	t.Check("call-2-signature-identical-to-call-1", sameBytes(sigs[1], sigs[0]),
		"call 2 tree_head_signature %x != call 1 %x", sigs[1], sigs[0])

	restOK := true
	restWhy := ""
	for i := 2; i < 12; i++ {
		if verr[i] != nil {
			restOK = false
			restWhy = "call " + itoa(i+1) + ": " + verr[i].Error()
			break
		}
	}
	t.Check("calls-3-to-12-verify", restOK, "%s", restWhy)

	sameAll := true
	sameWhy := ""
	for i := 2; i < 12; i++ {
		if !sameBytes(sigs[i], sigs[0]) {
			sameAll = false
			sameWhy = "call " + itoa(i+1) + " tree_head_signature differs from call 1"
			break
		}
	}
	t.Check("calls-3-to-12-signature-identical-to-call-1", sameAll, "%s", sameWhy)

	distinct := map[string]bool{}
	for _, s := range sigs {
		distinct[string(s)] = true
	}
	t.Check("whole-run-one-distinct-signature", len(distinct) == 1,
		"12 get-sth calls over one unchanged tree head produced %d distinct signatures, want 1", len(distinct))

	t.Done()
}

func itoa(n int) string {
	if n == 0 {
		return "0"
	}
	var b []byte
	for n > 0 {
		b = append([]byte{byte('0' + n%10)}, b...)
		n /= 10
	}
	return string(b)
}
