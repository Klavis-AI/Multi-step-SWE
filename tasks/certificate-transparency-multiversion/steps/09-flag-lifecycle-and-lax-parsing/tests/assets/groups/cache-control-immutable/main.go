// Group cache-control-immutable.
//
// v1.1.2 makes the four immutable RFC 6962 read endpoints cachable by sending
// an exact Cache-Control header, and deliberately does not send it on the two
// endpoints whose answers change as the log grows. The boundary is the HTTP
// response of the candidate's own ct_server, over a real loopback socket.
//
// v1.1.0 sets no Cache-Control header on any endpoint.
//
// Case 7 is inherited rather than new: it was the gate's
// `get-sth-matches-pinned-root` and moved here at the 2026-09-21 re-tier,
// because a gate group must compile against v1.1.0 too and the pinned
// trillian module -- and so _kit/serverkit.go -- does not survive that anchor.
// It lands here because this is the scored group that already asks the
// running personality for a full get-sth response, and it reads the body of
// that same endpoint rather than its headers. It cannot make this group
// satisfiable by inherited work alone: cases 1 to 6 are the v1.1.2 header
// contract. Every expected value is a constant of the pinned stub, so the
// case reads no clock: SCOPE-TIME.
package main

import (
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"net/http"
	neturl "net/url"

	ct "github.com/google/certificate-transparency-go"

	"ctverify/vhelp"
)

// The exact header value this milestone specifies.
const wantCacheControl = "public, max-age=86400"

func main() {
	t := vhelp.New()

	h := startPersonality(t, nil, "")
	defer h.stop()

	// A 32-byte hash the pinned backend answers for; its value is irrelevant to
	// the header contract but must decode from base64 and be non-empty.
	hashB64 := base64.StdEncoding.EncodeToString(pinnedRootHash[:])

	immutable := []struct {
		id  string
		url string
	}{
		{"get-entries-immutable", h.url(ct.GetEntriesPath) + "?start=0&end=2"},
		{"get-sth-consistency-immutable", h.url(ct.GetSTHConsistencyPath) + "?first=7&second=99"},
		{"get-proof-by-hash-immutable", h.url(ct.GetProofByHashPath) + "?" + neturl.Values{"hash": {hashB64}, "tree_size": {"99"}}.Encode()},
		{"get-entry-and-proof-immutable", h.url(ct.GetEntryAndProofPath) + "?leaf_index=7&tree_size=99"},
	}
	for _, c := range immutable {
		res := h.get(t, c.url)
		got := res.header.Get("Cache-Control")
		t.Check(c.id, res.status == http.StatusOK && got == wantCacheControl,
			"status %d Cache-Control %q, want 200 and %q", res.status, got, wantCacheControl)
	}

	// The two endpoints whose answer is not immutable must carry no
	// Cache-Control header at all.
	mutable := []struct {
		id  string
		url string
	}{
		{"get-sth-not-cached", h.url(ct.GetSTHPath)},
		{"get-roots-not-cached", h.url(ct.GetRootsPath)},
	}
	for _, c := range mutable {
		res := h.get(t, c.url)
		_, present := res.header["Cache-Control"]
		t.Check(c.id, res.status == http.StatusOK && !present,
			"status %d Cache-Control present=%v (%q)", res.status, present, res.header.Get("Cache-Control"))
	}

	// 7. The body of the same get-sth response: the tree head the personality
	// serves is the one the pinned backend root describes, and it is signed by
	// the log's own configured key. Inherited obligation, see the header.
	pubKey, _, _, err := ct.PublicKeyFromPEM(vhelp.ReadFixture("minted", "keys", "log-pubkey.pem"))
	if err != nil {
		t.Fatalf("parse pinned log public key: %v", err)
	}
	gs := h.get(t, h.url(ct.GetSTHPath))
	var sthRsp ct.GetSTHResponse
	if gs.status != http.StatusOK {
		t.Check("get-sth-matches-pinned-root", false, "status %d", gs.status)
	} else if err := json.Unmarshal(gs.body, &sthRsp); err != nil {
		t.Check("get-sth-matches-pinned-root", false, "unmarshal: %v", err)
	} else if sth, err := sthRsp.ToSignedTreeHead(); err != nil {
		t.Check("get-sth-matches-pinned-root", false, "ToSignedTreeHead: %v", err)
	} else {
		sv, err := ct.NewSignatureVerifier(pubKey)
		if err != nil {
			t.Fatalf("NewSignatureVerifier: %v", err)
		}
		verr := sv.VerifySTHSignature(*sth)
		ok := sth.TreeSize == pinnedTreeSize &&
			sth.Timestamp == pinnedTimestampMS &&
			hex.EncodeToString(sth.SHA256RootHash[:]) == hex.EncodeToString(pinnedRootHash[:]) &&
			verr == nil
		t.Check("get-sth-matches-pinned-root", ok,
			"size=%d ts=%d root=%x sigerr=%v", sth.TreeSize, sth.Timestamp, sth.SHA256RootHash[:], verr)
	}

	t.Done()
}
