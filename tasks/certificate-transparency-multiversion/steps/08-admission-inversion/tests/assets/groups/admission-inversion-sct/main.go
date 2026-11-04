// Group admission-inversion-sct.
//
// The accepting side of v1.1.0's admission inversion, observed end to end.
// admission-reject-expired and admission-reject-unexpired grade the refusals
// at the library boundary; this group grades what the milestone now *admits*,
// over a real socket, by taking the issued SCT apart and verifying its
// signature against the pinned log key over the exact RFC 6962 preimage.
//
// Depends on v1.1.0 only:
//   - reject_expired keeps its name but changes meaning. At v1.0.21 it fed
//     `DisableTimeChecks: !rejectExpired` into x509 chain verification, so it
//     also refused a not-yet-valid certificate. At v1.1.0 it is a leaf-only
//     `now.After(NotAfter)` test, so the not-yet-valid chain below is admitted
//     and gets an SCT. The identical config file produces opposite answers.
//   - reject_unexpired (config.proto field 17) does not exist at v1.0.21 in
//     any form, so the second configuration here cannot even be loaded there.
//
// SCOPE-TIME: the fixture windows are 2001-2002 (expired) and 2090-2091
// (not yet valid), and the in-memory short-life pair of shortlife.go is
// 2000-2025 over 2020-2060, so every verdict here is stable for the life of
// the corpus; everything else asserted here is a pinned constant.
package main

import (
	"encoding/base64"
	"encoding/json"
	"net/http"
	"strings"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/tls"

	"ctverify/vhelp"
)

func nextPEM(b []byte) ([]byte, []byte) {
	const begin = "-----BEGIN CERTIFICATE-----"
	const end = "-----END CERTIFICATE-----"
	s := string(b)
	i := strings.Index(s, begin)
	if i < 0 {
		return nil, nil
	}
	j := strings.Index(s[i:], end)
	if j < 0 {
		return nil, nil
	}
	der, err := base64.StdEncoding.DecodeString(strings.Join(strings.Fields(s[i+len(begin):i+j]), ""))
	if err != nil {
		return nil, nil
	}
	return der, []byte(s[i+j+len(end):])
}

func chainBody(t *vhelp.T, ders ...[]byte) string {
	var b64 []string
	for _, d := range ders {
		b64 = append(b64, base64.StdEncoding.EncodeToString(d))
	}
	if len(b64) == 0 {
		t.Fatalf("empty chain")
	}
	b, err := json.Marshal(map[string][]string{"chain": b64})
	if err != nil {
		t.Fatalf("marshal chain: %v", err)
	}
	return string(b)
}

func chainJSON(t *vhelp.T, name string) string {
	raw := vhelp.ReadFixture("minted", name)
	var ders [][]byte
	rest := raw
	for {
		der, r := nextPEM(rest)
		if der == nil {
			break
		}
		rest = r
		ders = append(ders, der)
	}
	if len(ders) == 0 {
		t.Fatalf("fixture %s held no certificate", name)
	}
	return chainBody(t, ders...)
}

// leafFor rebuilds the RFC 6962 MerkleTreeLeaf for an X.509 entry.
func leafFor(ts uint64, der []byte) ct.MerkleTreeLeaf {
	return ct.MerkleTreeLeaf{
		Version:  ct.V1,
		LeafType: ct.TimestampedEntryLeafType,
		TimestampedEntry: &ct.TimestampedEntry{
			Timestamp: ts,
			EntryType: ct.X509LogEntryType,
			X509Entry: &ct.ASN1Cert{Data: der},
		},
	}
}

func main() {
	t := vhelp.New()

	wantLogID := strings.Fields(string(vhelp.ReadFixture("minted", "keys", "log-id.txt")))[1]
	pubKey, _, _, err := ct.PublicKeyFromPEM(vhelp.ReadFixture("minted", "keys", "log-pubkey.pem"))
	if err != nil {
		t.Fatalf("parse pinned log public key: %v", err)
	}
	verifier, err := ct.NewSignatureVerifier(pubKey)
	if err != nil {
		t.Fatalf("build signature verifier: %v", err)
	}

	// admitted submits chainName under an already-running personality and
	// grades the whole issued SCT: acceptance, log id, the leaf the backend
	// was handed, and the signature over that leaf.
	admitted := func(h *harness, prefix, body string, leafDER []byte) {
		r := h.postJSON(t, h.url(ct.AddChainPath), body)
		var rsp ct.AddChainResponse
		if r.status != http.StatusOK || json.Unmarshal(r.body, &rsp) != nil {
			t.Check(prefix+"-admitted", false, "status %d body %q", r.status, string(r.body))
			t.Check(prefix+"-sct-log-id", false, "status %d", r.status)
			t.Check(prefix+"-queued-leaf", false, "status %d", r.status)
			t.Check(prefix+"-sct-verifies", false, "status %d", r.status)
			return
		}
		t.Check(prefix+"-admitted", rsp.SCTVersion == ct.V1, "sct_version %d", rsp.SCTVersion)
		t.Equal(prefix+"-sct-log-id", base64.StdEncoding.EncodeToString(rsp.ID), wantLogID)

		want, err := tls.Marshal(leafFor(rsp.Timestamp, leafDER))
		if err != nil {
			t.Fatalf("marshal expected leaf: %v", err)
		}
		t.EqualBytes(prefix+"-queued-leaf", h.backend.lastLeafValue(), want)

		var ds ct.DigitallySigned
		exts, eerr := base64.StdEncoding.DecodeString(rsp.Extensions)
		if _, err := tls.Unmarshal(rsp.Signature, &ds); err != nil || eerr != nil {
			t.Check(prefix+"-sct-verifies", false, "decode sct: %v %v", err, eerr)
			return
		}
		var id ct.LogID
		copy(id.KeyID[:], rsp.ID)
		sct := ct.SignedCertificateTimestamp{
			SCTVersion: rsp.SCTVersion,
			LogID:      id,
			Timestamp:  rsp.Timestamp,
			Extensions: ct.CTExtensions(exts),
			Signature:  ds,
		}
		t.Case(prefix+"-sct-verifies",
			verifier.VerifySCTSignature(sct, ct.LogEntry{Leaf: leafFor(rsp.Timestamp, leafDER)}))
	}

	// --- reject_expired: the field whose meaning inverted --------------------
	// v1.1.0 refuses only the already-expired leaf, and issues an SCT for the
	// not-yet-valid one. v1.0.21 does the opposite with this same file.
	{
		h := startPersonality(t, "rejexp", nil, "    reject_expired: true\n")
		ex := h.postJSON(t, h.url(ct.AddChainPath), chainJSON(t, "chain-expired.pem"))
		t.Check("rejectexpired-expired-refused", ex.status == http.StatusBadRequest,
			"status %d body %q", ex.status, string(ex.body))

		// The whole documented body, not just the status. `rejecting expired
		// certificate` is one of the two refusal reasons §5.3 makes contract,
		// and §2.1's frame wraps it twice. Rehomed here from the gate group
		// gate-endtoend, where it could not stay: v1.0.21 answers this same
		// request 200, so the case grades the inversion and nothing else.
		t.Equal("rejectexpired-expired-refusal-body", string(ex.body),
			"Bad Request\nfailed to verify add-chain contents: chain failed to verify: rejecting expired certificate\n")

		admitted(h, "rejectexpired-notyetvalid",
			chainJSON(t, "chain-notyetvalid.pem"),
			vhelp.ReadFixture("minted", "leaf-notyetvalid.der"))

		// The leaf-only property, end to end. Every fixture chain is issued by
		// bench-intermediate, whose 2000..2100 window outlives every leaf, so a
		// server that expired the WHOLE chain would answer every request above
		// exactly as v1.1.0 does. This chain's intermediate died in 2025 and its
		// leaf runs to 2060: from v1.1.0 only the submitted leaf is consulted,
		// so reject_expired still issues an SCT for it (HTTP-API.md 5.2).
		sl := shortLifeChain(t)
		admitted(h, "rejectexpired-expired-intermediate", chainBody(t, sl...), sl[0])
		h.stop()
	}

	// --- reject_unexpired: the field that did not exist ----------------------
	// The mirror image: only the expired leaf is admitted, and it gets a real
	// SCT. A v1.0.21 personality cannot load this configuration at all.
	{
		h := startPersonality(t, "rejunexp", nil, "    reject_unexpired: true\n")
		cur := h.postJSON(t, h.url(ct.AddChainPath), chainJSON(t, "chain-valid.pem"))
		t.Check("rejectunexpired-current-refused", cur.status == http.StatusBadRequest,
			"status %d body %q", cur.status, string(cur.body))

		admitted(h, "rejectunexpired-expired",
			chainJSON(t, "chain-expired.pem"),
			vhelp.ReadFixture("minted", "leaf-expired.der"))
		h.stop()
	}

	t.Done()
}
