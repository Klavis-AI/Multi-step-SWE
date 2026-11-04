// Group sth-signature-cache-not-shared-between-logs.
//
// One ct_server process serves many logs. The signature cache is per-log
// state, so two log instances that happen to present the identical tree head
// must each sign it themselves, must each cache their own answer, and must not
// be able to evict or overwrite each other's. A cache held in package-level
// state would show up here as the second log being handed the first log's
// signature.
//
// v1.0.5 caches nothing, so the four "stable" cases below cannot hold there.
package main

import "ctverify/vhelp"

func main() {
	t := vhelp.New()
	pub := loadPublicKey(t)

	const tag = "sth-shared"
	root := rootDigest(tag)

	// Two independent log instances over an identical tree head.
	a := newHarness(t, "logalpha", fixedRoot(pinnedNanos, pinnedTreeSize, tag))
	b := newHarness(t, "logbeta", fixedRoot(pinnedNanos, pinnedTreeSize, tag))

	a1, err := a.getSTH()
	if err != nil {
		t.Fatalf("log alpha first get-sth: %v", err)
	}
	b1, err := b.getSTH()
	if err != nil {
		t.Fatalf("log beta first get-sth: %v", err)
	}
	t.Case("log-alpha-verifies", checkSTH(pub, a1, uint64(pinnedTreeSize), pinnedMillis, root))
	t.Case("log-beta-verifies", checkSTH(pub, b1, uint64(pinnedTreeSize), pinnedMillis, root))
	t.Check("distinct-logs-sign-independently", !sameBytes(b1.sth.TreeHeadSignature, a1.sth.TreeHeadSignature),
		"log beta was served log alpha's signature %x for the same tree head", a1.sth.TreeHeadSignature)

	a2, err := a.getSTH()
	if err != nil {
		t.Fatalf("log alpha second get-sth: %v", err)
	}
	b2, err := b.getSTH()
	if err != nil {
		t.Fatalf("log beta second get-sth: %v", err)
	}
	t.Check("log-alpha-repeat-identical", sameBytes(a2.sth.TreeHeadSignature, a1.sth.TreeHeadSignature),
		"log alpha re-signed an unchanged tree head: %x then %x", a1.sth.TreeHeadSignature, a2.sth.TreeHeadSignature)
	t.Check("log-beta-repeat-identical", sameBytes(b2.sth.TreeHeadSignature, b1.sth.TreeHeadSignature),
		"log beta re-signed an unchanged tree head: %x then %x", b1.sth.TreeHeadSignature, b2.sth.TreeHeadSignature)

	// Log beta moves on to a different tree head; log alpha's cache entry must
	// be untouched.
	b.setRoot(pinnedNanos, pinnedTreeSize+7, root)
	b3, err := b.getSTH()
	if err != nil {
		t.Fatalf("log beta third get-sth: %v", err)
	}
	t.Case("log-beta-new-input-verifies", checkSTH(pub, b3, uint64(pinnedTreeSize+7), pinnedMillis, root))

	a3, err := a.getSTH()
	if err != nil {
		t.Fatalf("log alpha third get-sth: %v", err)
	}
	t.Check("log-alpha-unaffected-by-log-beta", sameBytes(a3.sth.TreeHeadSignature, a1.sth.TreeHeadSignature),
		"log alpha's cached signature changed after log beta signed a different tree head: %x, want %x",
		a3.sth.TreeHeadSignature, a1.sth.TreeHeadSignature)

	t.Done()
}
