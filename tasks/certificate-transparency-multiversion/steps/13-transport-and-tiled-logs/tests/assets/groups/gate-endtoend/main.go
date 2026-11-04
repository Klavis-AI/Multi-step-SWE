// gate-endtoend: this step's inherited end-to-end boundary.
//
// It brings up the candidate's own ct_server over a real loopback socket and
// walks a submission all the way to a verifiable SCT and back out through
// get-sth and get-entries. Every flag it passes and every config field it
// writes already existed at v1.2.0, and every answer it asserts is one the
// personality already gave at v1.2.0.
//
// That is deliberate. Under `reward = gate x score` this group is the veto,
// not the payout: it exists to catch a candidate that broke the RFC 6962 path
// while landing this release's dozen changes, and it must therefore hold on
// the tree the candidate starts from. The release's genuinely new answers --
// /log.v3.json, the 413 for an oversized chain, the short cache lifetime, the
// 429 for a throttled stale submission, the TLS listener -- are graded by the
// scored groups that own those capabilities, and none of them is asserted
// here.
//
// SCOPE-BACKEND: the log backend is this group's own in-process stub over a
// loopback gRPC socket, answering with pinned constants. SCOPE-TIME: no
// assertion below reads a clock, an interval or an elapsed duration.
package main

import (
	"crypto/sha256"
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

// The day-long lifetime every get-entries answer has carried since long
// before this release. v1.3.3 adds a second, shorter value for a partial
// answer; the range asked for below is one the backend satisfies in full, so
// the value asserted here is the inherited one.
const wantDayLong = "public, max-age=86400"

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
	logIDBytes := sha256.Sum256(pubDER)

	pub, err := stdx509.ParsePKIXPublicKey(pubDER)
	if err != nil {
		t.Fatalf("parse pinned log public key: %v", err)
	}
	verifier, err := ct.NewSignatureVerifier(pub)
	if err != nil {
		t.Fatalf("new signature verifier: %v", err)
	}

	chain := derList(t, "minted", "chain-valid.pem")
	roots := derList(t, "minted", "roots-bench-plus-int.pem")

	// The single leaf the pinned backend serves from get-entries.
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

	// No extra flags and no extra config fields: everything below must be
	// reachable on a server started exactly the way the previous milestone
	// started one.
	h := startPersonality(t, be, []logSpec{{
		id: pinnedLogID, prefix: logPrefix, privKey: privDER, pubKey: pubDER,
	}}, nil)
	defer h.stop()

	// 1. The server came up and serves liveness.
	r := h.get(t, h.baseURL+"/healthz")
	t.Check("healthz-is-ok", r.status == http.StatusOK && string(r.body) == "ok",
		"GET /healthz = %d %q, want 200 \"ok\"", r.status, string(r.body))

	// 2. get-roots serves exactly the configured trust anchors.
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

	// 3. add-chain still issues an SCT signed by the log's own key.
	body, err := json.Marshal(map[string]interface{}{"chain": b64all(chain)})
	if err != nil {
		t.Fatalf("marshal add-chain body: %v", err)
	}
	r = h.postJSON(t, h.url("/ct/v1/add-chain"), body)
	if r.status != http.StatusOK {
		t.Fatalf("add-chain = %d %q", r.status, lastBytes(r.body, 400))
	}
	var acr ct.AddChainResponse
	if err := json.Unmarshal(r.body, &acr); err != nil {
		t.Fatalf("add-chain body %q: %v", lastBytes(r.body, 400), err)
	}
	sct, err := acr.ToSignedCertificateTimestamp()
	if err != nil {
		t.Fatalf("decode SCT: %v", err)
	}
	// The SCT timestamp is carried through into the expected leaf rather than
	// compared against anything, so nothing here depends on the host clock.
	merkleLeaf := ct.MerkleTreeLeaf{
		Version:  ct.V1,
		LeafType: ct.TimestampedEntryLeafType,
		TimestampedEntry: &ct.TimestampedEntry{
			Timestamp: sct.Timestamp,
			EntryType: ct.X509LogEntryType,
			X509Entry: &ct.ASN1Cert{Data: chain[0]},
		},
	}
	sigErr := verifier.VerifySCTSignature(*sct, ct.LogEntry{Leaf: merkleLeaf})
	t.Check("add-chain-issues-a-verifiable-sct",
		sct.SCTVersion == ct.V1 && sct.LogID.KeyID == logIDBytes && sigErr == nil,
		"SCT version %v, log id %x (want %x), signature check %v",
		sct.SCTVersion, sct.LogID.KeyID[:], logIDBytes[:], sigErr)

	// 4. ... over exactly the RFC 6962 leaf, with the whole issuance chain in
	//    the extra data.
	queued := be.queuedLeaves()
	if len(queued) != 1 {
		t.Fatalf("backend saw %d queued leaves, want 1", len(queued))
	}
	wantLeafValue, err := tls.Marshal(merkleLeaf)
	if err != nil {
		t.Fatalf("marshal expected leaf value: %v", err)
	}
	wantExtra, err := tls.Marshal(ct.CertificateChain{Entries: []ct.ASN1Cert{{Data: chain[1]}}})
	if err != nil {
		t.Fatalf("marshal expected extra data: %v", err)
	}
	wantID := sha256.Sum256(chain[0])
	t.Check("queued-leaf-is-the-rfc6962-one",
		string(queued[0].GetLeafValue()) == string(wantLeafValue) &&
			string(queued[0].GetExtraData()) == string(wantExtra) &&
			string(queued[0].GetLeafIdentityHash()) == string(wantID[:]),
		"queued leaf value %d bytes (want %d), extra data %d bytes (want %d), identity hash %x (want %x)",
		len(queued[0].GetLeafValue()), len(wantLeafValue),
		len(queued[0].GetExtraData()), len(wantExtra),
		queued[0].GetLeafIdentityHash(), wantID[:])

	// 5. get-sth reflects the backend's pinned root and is signed by the log.
	r = h.get(t, h.url("/ct/v1/get-sth"))
	var sthRsp ct.GetSTHResponse
	if err := json.Unmarshal(r.body, &sthRsp); err != nil {
		t.Fatalf("get-sth body %q: %v", lastBytes(r.body, 400), err)
	}
	sth, err := sthRsp.ToSignedTreeHead()
	if err != nil {
		t.Fatalf("decode STH: %v", err)
	}
	sthErr := verifier.VerifySTHSignature(*sth)
	t.Check("get-sth-serves-the-pinned-root",
		r.status == http.StatusOK && sth.TreeSize == pinnedTreeSize && sth.Timestamp == pinnedTimestampMS &&
			sth.SHA256RootHash == pinnedRootHash && sthErr == nil,
		"get-sth = %d, size %d (want %d), timestamp %d (want %d), root %x, signature %v",
		r.status, sth.TreeSize, pinnedTreeSize, sth.Timestamp, pinnedTimestampMS, sth.SHA256RootHash[:], sthErr)

	// 6. get-entries hands the stored leaf back byte for byte, and a range the
	//    backend answered in full keeps the day-long cache lifetime.
	r = h.get(t, h.url("/ct/v1/get-entries?start=0&end=0"))
	var entries ct.GetEntriesResponse
	if err := json.Unmarshal(r.body, &entries); err != nil {
		t.Fatalf("get-entries body %q: %v", lastBytes(r.body, 400), err)
	}
	t.Check("get-entries-returns-the-stored-entry",
		r.status == http.StatusOK && len(entries.Entries) == 1 &&
			string(entries.Entries[0].LeafInput) == string(storedLeafValue) &&
			string(entries.Entries[0].ExtraData) == string(storedExtra) &&
			r.header.Get("Cache-Control") == wantDayLong,
		"get-entries = %d with %d entries, Cache-Control %q (want %q)",
		r.status, len(entries.Entries), r.header.Get("Cache-Control"), wantDayLong)

	t.Done()
}
