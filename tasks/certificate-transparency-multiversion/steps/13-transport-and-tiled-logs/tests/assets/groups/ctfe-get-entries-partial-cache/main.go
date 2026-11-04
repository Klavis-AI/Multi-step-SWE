// ctfe-get-entries-partial-cache: v1.3.3 makes the Cache-Control header of
// get-entries depend on whether the backend actually returned as many entries
// as the request asked for. A full answer keeps the day-long
// "public, max-age=86400"; a short answer -- the log has not sequenced that
// far yet, so the same range will hold more entries soon -- drops to
// "public, max-age=60". Before this release every get-entries answer carried
// the day-long value, short or not.
//
// Graded over a real loopback socket against the candidate's own ct_server.
// The backend is this group's own pinned stub (SCOPE-BACKEND): it serves at
// most sixteen leaves per GetLeavesByRange call and its stored leaf is a
// constant, so "fewer entries than requested" is a property of the fixture
// and not of any clock or of how much work the log has done (SCOPE-TIME).
package main

import (
	"encoding/base64"
	"encoding/json"
	"net/http"
	"net/url"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/tls"
	"github.com/google/certificate-transparency-go/x509util"
	"github.com/google/trillian"

	"ctverify/vhelp"
)

// The values the release names, spelled out here so the assertion is on the
// exact bytes of the header and not on a constant read back from the product.
const (
	wantImmutable = "public, max-age=86400"
	wantPartial   = "public, max-age=60"
)

// stubLeafCap mirrors the ceiling the pinned backend applies to
// GetLeavesByRange. Any request for more than this many entries is answered
// short.
const stubLeafCap = 16

func derList(t *vhelp.T, rel ...string) [][]byte {
	certs, err := x509util.CertificatesFromPEM(vhelp.ReadFixture(rel...))
	if err != nil || len(certs) == 0 {
		t.Fatalf("parse %v: %v", rel, err)
	}
	out := make([][]byte, 0, len(certs))
	for _, c := range certs {
		out = append(out, c.Raw)
	}
	return out
}

func main() {
	t := vhelp.New()

	privDER := pemBody(t, vhelp.ReadFixture("minted", "keys", "log-key.pem"))
	pubDER := pemBody(t, vhelp.ReadFixture("minted", "keys", "log-pubkey.pem"))
	chain := derList(t, "minted", "chain-valid.pem")

	be := newStub()
	storedLeafValue, err := tls.Marshal(ct.MerkleTreeLeaf{
		Version:  ct.V1,
		LeafType: ct.TimestampedEntryLeafType,
		TimestampedEntry: &ct.TimestampedEntry{
			Timestamp: pinnedTimestampMS,
			EntryType: ct.X509LogEntryType,
			X509Entry: &ct.ASN1Cert{Data: chain[0]},
		},
	})
	if err != nil {
		t.Fatalf("marshal stored leaf: %v", err)
	}
	storedExtra, err := tls.Marshal(ct.CertificateChain{Entries: []ct.ASN1Cert{{Data: chain[1]}}})
	if err != nil {
		t.Fatalf("marshal stored extra data: %v", err)
	}
	be.setStored(&trillian.LogLeaf{LeafValue: storedLeafValue, ExtraData: storedExtra})

	h := startPersonality(t, be, []logSpec{{
		id: pinnedLogID, prefix: logPrefix, privKey: privDER, pubKey: pubDER,
	}}, nil)
	defer h.stop()

	entriesOf := func(r httpResult) ct.GetEntriesResponse {
		var e ct.GetEntriesResponse
		if err := json.Unmarshal(r.body, &e); err != nil {
			t.Fatalf("get-entries body %q: %v", lastBytes(r.body, 300), err)
		}
		return e
	}

	// 1. Control. The entrypoint works at all: one entry asked for, one
	//    entry served, byte for byte the leaf the backend holds.
	r := h.get(t, h.url("/ct/v1/get-entries?start=0&end=0"))
	e := entriesOf(r)
	t.Check("control-get-entries-serves-the-pinned-leaf",
		r.status == http.StatusOK && len(e.Entries) == 1 &&
			string(e.Entries[0].LeafInput) == string(storedLeafValue) &&
			string(e.Entries[0].ExtraData) == string(storedExtra),
		"get-entries[0,0] = %d with %d entries", r.status, len(e.Entries))

	// 2. A request the backend can satisfy in full keeps the day-long value.
	r = h.get(t, h.url("/ct/v1/get-entries?start=0&end=15"))
	e = entriesOf(r)
	t.Check("full-range-is-cacheable-for-a-day",
		r.status == http.StatusOK && len(e.Entries) == stubLeafCap &&
			r.header.Get("Cache-Control") == wantImmutable,
		"get-entries[0,15] = %d, %d entries, Cache-Control %q, want 200, %d entries, %q",
		r.status, len(e.Entries), r.header.Get("Cache-Control"), stubLeafCap, wantImmutable)

	// 3. The same endpoint, asked for twenty-one and answered with sixteen,
	//    switches to the one-minute value.
	r = h.get(t, h.url("/ct/v1/get-entries?start=0&end=20"))
	shortHdr := r.header
	e = entriesOf(r)
	t.Check("short-range-is-cacheable-for-a-minute",
		r.status == http.StatusOK && len(e.Entries) == stubLeafCap &&
			shortHdr.Get("Cache-Control") == wantPartial,
		"get-entries[0,20] = %d, %d entries (asked for 21), Cache-Control %q, want 200, %d entries, %q",
		r.status, len(e.Entries), shortHdr.Get("Cache-Control"), stubLeafCap, wantPartial)

	// 4. Nothing else about the short answer changes: it is still a 200 of
	//    JSON carrying the same leaves, not an error or a truncation marker.
	t.Check("short-answer-is-an-ordinary-json-200",
		r.status == http.StatusOK && shortHdr.Get("Content-Type") == "application/json" &&
			len(e.Entries) > 0 && string(e.Entries[0].LeafInput) == string(storedLeafValue) &&
			string(e.Entries[stubLeafCap-1].ExtraData) == string(storedExtra),
		"short get-entries = %d, Content-Type %q, %d entries", r.status, shortHdr.Get("Content-Type"), len(e.Entries))

	// 5. The decision is made on the count, not on the start index: a
	//    one-entry request away from zero is still a full answer.
	r = h.get(t, h.url("/ct/v1/get-entries?start=5&end=5"))
	e = entriesOf(r)
	t.Check("offset-full-range-is-cacheable-for-a-day",
		r.status == http.StatusOK && len(e.Entries) == 1 &&
			r.header.Get("Cache-Control") == wantImmutable,
		"get-entries[5,5] = %d, %d entries, Cache-Control %q, want 200, 1 entry, %q",
		r.status, len(e.Entries), r.header.Get("Cache-Control"), wantImmutable)

	// 6. ... and a twenty-one entry request away from zero is still short.
	r = h.get(t, h.url("/ct/v1/get-entries?start=5&end=25"))
	e = entriesOf(r)
	t.Check("offset-short-range-is-cacheable-for-a-minute",
		r.status == http.StatusOK && len(e.Entries) == stubLeafCap &&
			r.header.Get("Cache-Control") == wantPartial,
		"get-entries[5,25] = %d, %d entries, Cache-Control %q, want 200, %d entries, %q",
		r.status, len(e.Entries), r.header.Get("Cache-Control"), stubLeafCap, wantPartial)

	// 7. The change is confined to get-entries: a proof, which can never be
	//    partial, keeps the day-long value.
	hash := url.QueryEscape(base64.StdEncoding.EncodeToString(pinnedRootHash[:]))
	r = h.get(t, h.url("/ct/v1/get-proof-by-hash?hash="+hash+"&tree_size=100000"))
	t.Check("get-proof-by-hash-is-still-cacheable-for-a-day",
		r.status == http.StatusOK && r.header.Get("Cache-Control") == wantImmutable,
		"get-proof-by-hash = %d, Cache-Control %q, want 200, %q",
		r.status, r.header.Get("Cache-Control"), wantImmutable)

	// 8. The other half of the same sentence: get-sth and get-roots set no
	//    Cache-Control at all, at any lifetime (HTTP-API.md 4.7). Absence is
	//    asserted on the header map, not on an empty string, so a header
	//    present with an empty value still fails.
	sth := h.get(t, h.url("/ct/v1/get-sth"))
	roots := h.get(t, h.url("/ct/v1/get-roots"))
	t.Check("get-sth-and-get-roots-set-no-cache-control",
		sth.status == http.StatusOK && roots.status == http.StatusOK &&
			len(sth.header.Values("Cache-Control")) == 0 &&
			len(roots.header.Values("Cache-Control")) == 0,
		"get-sth = %d with Cache-Control %v; get-roots = %d with Cache-Control %v; want 200 and no such header on either",
		sth.status, sth.header.Values("Cache-Control"),
		roots.status, roots.header.Values("Cache-Control"))

	t.Done()
}
