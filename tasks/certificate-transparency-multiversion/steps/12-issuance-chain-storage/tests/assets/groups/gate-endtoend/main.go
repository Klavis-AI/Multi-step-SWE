// gate-endtoend: the inherited end-to-end boundary of this step. It runs the
// candidate's own ct_server over a real loopback socket, configured exactly
// as the previous milestone configured it -- no flag and no configuration
// field this release adds -- and walks a submission all the way to a
// verifiable SCT and back out through get-sth and get-entries.
//
// GATE SCOPE: every case below is regression, satisfied by the tree as it
// stood at the end of step 11 and required to keep holding here. Nothing this
// release adds is asserted in this file: the cache flags, the per-log
// issuance chain storage backend, the hashed leaf layout and the re-cut
// backend-error status table are new capability and are graded by the scored
// groups (ctserver-cache-flags, ctfe-instance-cache-selection,
// leaf-both-layouts-coexist, ctfe-backend-error-http-status). A log left on
// the default keeps the RFC 6962 full-chain extra data byte for byte, which
// is what makes case 4 below inherited rather than new.
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

	be := newStub()
	// A pinned stored leaf for get-entries, in the classic layout.
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

	// 1. The server is up and serving its liveness endpoint.
	r := h.get(t, h.baseURL+"/healthz")
	t.Check("healthz-is-ok", r.status == http.StatusOK && string(r.body) == "ok",
		"GET /healthz = %d %q, want 200 \"ok\"", r.status, string(r.body))

	// 2. get-roots serves exactly the configured trust anchors.
	r = h.get(t, h.url("/ct/v1/get-roots"))
	var gotRoots struct {
		Certificates []string `json:"certificates"`
	}
	if err := json.Unmarshal(r.body, &gotRoots); err != nil {
		t.Fatalf("get-roots body %q: %v", string(r.body), err)
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
		r.status == http.StatusOK && len(gotSet) == len(wantRoots) && sameSet(gotSet, wantRoots),
		"get-roots returned %d certificates (status %d), want the %d configured anchors", len(gotSet), r.status, len(wantRoots))

	// 3. add-chain issues an SCT signed by the log's own key.
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
		t.Fatalf("add-chain body %q: %v", string(r.body), err)
	}
	sct, err := acr.ToSignedCertificateTimestamp()
	if err != nil {
		t.Fatalf("decode SCT: %v", err)
	}
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
		"SCT version %v, log id %x (want %x), signature check %v", sct.SCTVersion, sct.LogID.KeyID[:], logIDBytes[:], sigErr)

	// 4. The queued leaf carries the whole issuance chain, as it has since
	//    step 6: this log is on the default, which is the inherited layout.
	queued := be.queuedLeaves()
	if len(queued) != 1 {
		t.Fatalf("backend saw %d queued leaves, want 1", len(queued))
	}
	wantExtra, err := tls.Marshal(ct.CertificateChain{Entries: []ct.ASN1Cert{{Data: chain[1]}}})
	if err != nil {
		t.Fatalf("marshal expected extra data: %v", err)
	}
	t.EqualBytes("queued-leaf-keeps-the-full-issuance-chain", queued[0].GetExtraData(), wantExtra)

	// 5. ... and the leaf value and identity hash are the RFC 6962 ones.
	wantLeafValue, err := tls.Marshal(merkleLeaf)
	if err != nil {
		t.Fatalf("marshal expected leaf value: %v", err)
	}
	wantID := sha256.Sum256(chain[0])
	t.Check("queued-leaf-matches-the-issued-sct",
		string(queued[0].GetLeafValue()) == string(wantLeafValue) &&
			string(queued[0].GetLeafIdentityHash()) == string(wantID[:]),
		"queued leaf value %d bytes (want %d), identity hash %x (want %x)",
		len(queued[0].GetLeafValue()), len(wantLeafValue), queued[0].GetLeafIdentityHash(), wantID[:])

	// 6. get-sth reflects the backend's root and is signed by the log.
	r = h.get(t, h.url("/ct/v1/get-sth"))
	var sthRsp ct.GetSTHResponse
	if err := json.Unmarshal(r.body, &sthRsp); err != nil {
		t.Fatalf("get-sth body %q: %v", string(r.body), err)
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

	// 7. get-entries hands back the stored leaf byte for byte.
	r = h.get(t, h.url("/ct/v1/get-entries?start=0&end=0"))
	var entries ct.GetEntriesResponse
	if err := json.Unmarshal(r.body, &entries); err != nil {
		t.Fatalf("get-entries body %q: %v", string(r.body), err)
	}
	ok := r.status == http.StatusOK && len(entries.Entries) == 1 &&
		string(entries.Entries[0].LeafInput) == string(storedLeafValue) &&
		string(entries.Entries[0].ExtraData) == string(storedExtra)
	t.Check("get-entries-returns-the-stored-entry", ok,
		"get-entries = %d with %d entries", r.status, len(entries.Entries))

	t.Done()
}

func sameSet(a, b map[string]bool) bool {
	for k := range a {
		if !b[k] {
			return false
		}
	}
	return true
}

func b64all(ders [][]byte) []string {
	out := make([]string, 0, len(ders))
	for _, d := range ders {
		out = append(out, base64.StdEncoding.EncodeToString(d))
	}
	return out
}
