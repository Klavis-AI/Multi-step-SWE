// Group sth-signature-cache-invisible-in-response.
//
// The second half of the v1.0.7 requirement: the cache must be invisible in
// output. A cached get-sth response must be byte-for-byte the response the log
// would have produced by signing afresh -- same status, same content type,
// exactly the documented field set, and a signature that still verifies over
// the RFC 6962 signature input. The only observable is that the whole JSON
// body repeats exactly.
//
// v1.0.5 re-signs on every call with the pinned EC P-256 key, so its two
// bodies differ in the tree_head_signature field.
package main

import "ctverify/vhelp"

func main() {
	t := vhelp.New()
	pub := loadPublicKey(t)

	const tag = "sth-invisible"
	h := newHarness(t, "cacheinvisible", fixedRoot(pinnedNanos, pinnedTreeSize, tag))
	want := rootDigest(tag)

	r1, err := h.getSTH()
	if err != nil {
		t.Fatalf("first get-sth: %v", err)
	}
	r2, err := h.getSTH()
	if err != nil {
		t.Fatalf("second get-sth: %v", err)
	}

	t.Equal("first-response-status-200", r1.status, 200)
	t.Equal("second-response-status-200", r2.status, 200)
	t.EqualBytes("response-body-byte-identical", r2.body, r1.body)

	wantFields := []string{"sha256_root_hash", "timestamp", "tree_head_signature", "tree_size"}
	got1 := keysOf(r1.fields)
	got2 := keysOf(r2.fields)
	t.Check("response-field-set-exact",
		join(got1) == join(wantFields) && join(got2) == join(wantFields),
		"get-sth fields are %v then %v, want exactly %v", got1, got2, wantFields)

	ct1 := r1.hdr.Get("Content-Type")
	ct2 := r2.hdr.Get("Content-Type")
	t.Check("content-type-application-json", ct1 == "application/json" && ct2 == "application/json",
		"Content-Type is %q then %q, want \"application/json\" both times", ct1, ct2)

	t.Case("cached-response-signature-verifies",
		checkSTH(pub, r2, uint64(pinnedTreeSize), pinnedMillis, want))

	t.Done()
}

func keysOf(m map[string]jsonRaw) []string {
	out := make([]string, 0, len(m))
	for k := range m {
		out = append(out, k)
	}
	sortStrings(out)
	return out
}

func sortStrings(s []string) {
	for i := 1; i < len(s); i++ {
		for j := i; j > 0 && s[j] < s[j-1]; j-- {
			s[j], s[j-1] = s[j-1], s[j]
		}
	}
}

func join(s []string) string {
	out := ""
	for i, v := range s {
		if i > 0 {
			out += ","
		}
		out += v
	}
	return out
}
