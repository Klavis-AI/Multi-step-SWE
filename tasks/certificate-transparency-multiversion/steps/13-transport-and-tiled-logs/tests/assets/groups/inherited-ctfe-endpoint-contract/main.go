// INHERITED. The RFC 6962 request contract of the CTFE personality: which
// method each entrypoint answers, which malformed requests are refused, and
// what the two read endpoints return when the request is well formed.
//
// This is the right thing to re-run at v1.3.3 because the release rewrites
// the file that decides all of it: handlers.go gains a ninth entrypoint, the
// add-chain path is wrapped in http.MaxBytesHandler and a rate limiter, the
// leaf is now built through a leafChainBuilder interface rather than a
// concrete type, and get-entries chooses between two Cache-Control values.
// None of that may move a status code that RFC 6962 clients already depend
// on, so this group pins the pre-existing ones.
//
// Every case below is behaviour v1.2.0 already had, so the group is expected
// to pass against the previous tree as well. SCOPE-BACKEND: the log backend
// is this group's own in-process stub over a loopback gRPC socket, answering
// with pinned constants. The boundary is a real loopback socket on a port
// obtained by binding :0.
package main

import (
	stdx509 "crypto/x509"
	"encoding/base64"
	"encoding/json"
	"net/http"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/tls"
	"github.com/google/certificate-transparency-go/x509util"
	"github.com/google/trillian"

	"ctverify/vhelp"
)

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

func b64all(ders [][]byte) []string {
	out := make([]string, 0, len(ders))
	for _, d := range ders {
		out = append(out, base64.StdEncoding.EncodeToString(d))
	}
	return out
}

func chainBody(t *vhelp.T, ders [][]byte) []byte {
	b, err := json.Marshal(map[string]interface{}{"chain": b64all(ders)})
	if err != nil {
		t.Fatalf("marshal chain body: %v", err)
	}
	return b
}

func sameSet(a, b map[string]bool) bool {
	if len(a) != len(b) {
		return false
	}
	for k := range a {
		if !b[k] {
			return false
		}
	}
	return true
}

func main() {
	t := vhelp.New()

	privDER := pemBody(t, vhelp.ReadFixture("minted", "keys", "log-key.pem"))
	pubDER := pemBody(t, vhelp.ReadFixture("minted", "keys", "log-pubkey.pem"))
	pub, err := stdx509.ParsePKIXPublicKey(pubDER)
	if err != nil {
		t.Fatalf("parse pinned log public key: %v", err)
	}
	verifier, err := ct.NewSignatureVerifier(pub)
	if err != nil {
		t.Fatalf("new signature verifier: %v", err)
	}

	roots := derList(t, "minted", "roots-bench-plus-int.pem")
	valid := derList(t, "minted", "chain-valid.pem")
	unrooted := derList(t, "minted", "chain-unrooted.pem")

	// The single entry the pinned backend serves from get-entries.
	be := newStub()
	storedLeaf, err := tls.Marshal(ct.MerkleTreeLeaf{
		Version:  ct.V1,
		LeafType: ct.TimestampedEntryLeafType,
		TimestampedEntry: &ct.TimestampedEntry{
			Timestamp: pinnedTimestampMS,
			EntryType: ct.X509LogEntryType,
			X509Entry: &ct.ASN1Cert{Data: valid[0]},
		},
	})
	if err != nil {
		t.Fatalf("marshal stored leaf: %v", err)
	}
	storedExtra, err := tls.Marshal(ct.CertificateChain{Entries: []ct.ASN1Cert{{Data: valid[1]}}})
	if err != nil {
		t.Fatalf("marshal stored extra data: %v", err)
	}
	be.setStored(&trillian.LogLeaf{LeafValue: storedLeaf, ExtraData: storedExtra})

	h := startPersonality(t, be, []logSpec{{
		id: pinnedLogID, prefix: logPrefix, privKey: privDER, pubKey: pubDER,
	}}, nil)
	defer h.stop()

	// --- liveness and routing --------------------------------------------
	r := h.get(t, h.baseURL+"/healthz")
	t.Check("healthz-is-ok", r.status == http.StatusOK && string(r.body) == "ok",
		"GET /healthz = %d %q, want 200 \"ok\"", r.status, string(r.body))

	r = h.get(t, h.baseURL+"/nosuchlog/ct/v1/get-sth")
	t.Equal("an-unknown-log-prefix-is-not-found", r.status, http.StatusNotFound)

	// The other half of that catch-all, narrowed at v1.0.13/v1.0.17 and
	// unchanged since: the exact path "/" is 200 with an empty body, where
	// every other unmatched path is the 404 just asserted (HTTP-API.md 7).
	r = h.get(t, h.baseURL+"/")
	t.Check("the-bare-root-path-is-200-with-an-empty-body",
		r.status == http.StatusOK && len(r.body) == 0,
		"GET / = %d with a %d-byte body %q, want 200 and nothing",
		r.status, len(r.body), lastBytes(r.body, 200))

	// --- method discipline -------------------------------------------------
	r = h.get(t, h.url("/ct/v1/add-chain"))
	t.Equal("get-on-add-chain-is-method-not-allowed", r.status, http.StatusMethodNotAllowed)

	r = h.get(t, h.url("/ct/v1/add-pre-chain"))
	t.Equal("get-on-add-pre-chain-is-method-not-allowed", r.status, http.StatusMethodNotAllowed)

	r = h.postJSON(t, h.url("/ct/v1/get-sth"), []byte("{}"))
	t.Equal("post-on-get-sth-is-method-not-allowed", r.status, http.StatusMethodNotAllowed)

	r = h.postJSON(t, h.url("/ct/v1/get-roots"), []byte("{}"))
	t.Equal("post-on-get-roots-is-method-not-allowed", r.status, http.StatusMethodNotAllowed)

	// --- submission refusals ------------------------------------------------
	r = h.postJSON(t, h.url("/ct/v1/add-chain"), []byte("{this is not json"))
	t.Equal("a-malformed-add-chain-body-is-rejected", r.status, http.StatusBadRequest)

	r = h.postJSON(t, h.url("/ct/v1/add-chain"), []byte(`{"chain":[]}`))
	t.Equal("an-empty-chain-is-rejected", r.status, http.StatusBadRequest)

	r = h.postJSON(t, h.url("/ct/v1/add-chain"), []byte(`{"chain":["!!not base64!!"]}`))
	t.Equal("a-chain-entry-that-is-not-base64-is-rejected", r.status, http.StatusBadRequest)

	r = h.postJSON(t, h.url("/ct/v1/add-chain"), chainBody(t, unrooted))
	t.Equal("a-chain-that-reaches-no-trusted-root-is-rejected", r.status, http.StatusBadRequest)

	r = h.postJSON(t, h.url("/ct/v1/add-pre-chain"), chainBody(t, valid))
	t.Equal("a-certificate-submitted-as-a-precertificate-is-rejected", r.status, http.StatusBadRequest)

	// --- query refusals ------------------------------------------------------
	r = h.get(t, h.url("/ct/v1/get-entries"))
	t.Equal("get-entries-without-parameters-is-rejected", r.status, http.StatusBadRequest)

	r = h.get(t, h.url("/ct/v1/get-entries?start=5&end=2"))
	t.Equal("get-entries-with-start-beyond-end-is-rejected", r.status, http.StatusBadRequest)

	r = h.get(t, h.url("/ct/v1/get-entries?start=-1&end=3"))
	t.Equal("get-entries-with-a-negative-start-is-rejected", r.status, http.StatusBadRequest)

	r = h.get(t, h.url("/ct/v1/get-proof-by-hash"))
	t.Equal("get-proof-by-hash-without-parameters-is-rejected", r.status, http.StatusBadRequest)

	r = h.get(t, h.url("/ct/v1/get-entry-and-proof"))
	t.Equal("get-entry-and-proof-without-parameters-is-rejected", r.status, http.StatusBadRequest)

	r = h.get(t, h.url("/ct/v1/get-sth-consistency?first=-1&second=4"))
	t.Equal("get-sth-consistency-with-a-negative-size-is-rejected", r.status, http.StatusBadRequest)

	// --- the well-formed reads ------------------------------------------------
	r = h.get(t, h.url("/ct/v1/get-roots"))
	var gotRoots struct {
		Certificates []string `json:"certificates"`
	}
	if err := json.Unmarshal(r.body, &gotRoots); err != nil {
		t.Fatalf("get-roots body %q: %v", lastBytes(r.body, 400), err)
	}
	wantRoots := map[string]bool{}
	for _, d := range roots {
		wantRoots[base64.StdEncoding.EncodeToString(d)] = true
	}
	gotSet := map[string]bool{}
	for _, s := range gotRoots.Certificates {
		gotSet[s] = true
	}
	t.Check("get-roots-serves-the-configured-anchors",
		r.status == http.StatusOK && sameSet(gotSet, wantRoots),
		"get-roots returned %d certificates (status %d), want the %d configured anchors",
		len(gotSet), r.status, len(wantRoots))

	r = h.get(t, h.url("/ct/v1/get-sth"))
	var sthResp ct.GetSTHResponse
	if err := json.Unmarshal(r.body, &sthResp); err != nil {
		t.Fatalf("get-sth body %q: %v", lastBytes(r.body, 400), err)
	}
	sth, err := sthResp.ToSignedTreeHead()
	if err != nil {
		t.Fatalf("decode STH: %v", err)
	}
	sthErr := verifier.VerifySTHSignature(*sth)
	t.Check("get-sth-serves-the-pinned-tree-head",
		r.status == http.StatusOK && sth.TreeSize == pinnedTreeSize &&
			sth.Timestamp == pinnedTimestampMS && sth.SHA256RootHash == pinnedRootHash && sthErr == nil,
		"get-sth = %d, tree size %d (want %d), timestamp %d (want %d), signature %v",
		r.status, sth.TreeSize, pinnedTreeSize, sth.Timestamp, pinnedTimestampMS, sthErr)

	r = h.get(t, h.url("/ct/v1/get-entries?start=0&end=0"))
	var entries struct {
		Entries []struct {
			LeafInput []byte `json:"leaf_input"`
			ExtraData []byte `json:"extra_data"`
		} `json:"entries"`
	}
	if err := json.Unmarshal(r.body, &entries); err != nil {
		t.Fatalf("get-entries body %q: %v", lastBytes(r.body, 400), err)
	}
	okEntry := r.status == http.StatusOK && len(entries.Entries) == 1 &&
		string(entries.Entries[0].LeafInput) == string(storedLeaf) &&
		string(entries.Entries[0].ExtraData) == string(storedExtra)
	t.Check("get-entries-serves-the-stored-entry-verbatim", okEntry,
		"get-entries = %d with %d entries, want 200 and the one pinned entry byte for byte",
		r.status, len(entries.Entries))

	t.Done()
}
