// Group gate-endtoend. KIND=gate: it earns nothing and only vetoes.
//
// The inherited end-to-end path, plus proof that the artifact starts: the
// candidate's own ct_server binary, started as a subprocess, speaking RFC 6962
// over a real loopback socket to a pinned in-process Trillian backend
// (SCOPE-BACKEND). Everything asserted here is behaviour this milestone
// leaves alone -- the root pool it serves, the STH it signs, the SCT it issues
// for a current chain, the duplicate-submission rule of HTTP-API.md §3.2, the
// refusal of an unrooted chain and the 404 for an unknown log prefix -- and
// every case passes against the previous milestone's tree as well as this one.
//
// Deliberately NOT here, because they are what v1.1.0 changes and a gate case
// that grades a change makes the Oracle fail its own step (gate rule 3):
//
//	/healthz                     -> scored ctserver-flags-and-healthz
//	                                (case healthz-body-exact)
//	the reject_expired inversion -> scored admission-inversion-sct
//	                                (rejectexpired-expired-refused, its
//	                                 refusal body, and
//	                                 rejectexpired-notyetvalid-admitted)
//	reject_extensions            -> scored ctfe-reject-extensions
//
// The configuration below therefore carries no field this milestone adds, and
// serverkit.go passes no flag whose default moved. See its header for the two
// dependency-level differences between the anchors it has to bridge.
package main

import (
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"net/http"
	"sort"
	"strings"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/tls"

	"ctverify/vhelp"
)

func chainJSON(t *vhelp.T, name string) string {
	raw := vhelp.ReadFixture("minted", name)
	var ders []string
	rest := raw
	for {
		der, r := nextPEM(rest)
		if der == nil {
			break
		}
		rest = r
		ders = append(ders, base64.StdEncoding.EncodeToString(der))
	}
	if len(ders) == 0 {
		t.Fatalf("fixture %s held no certificate", name)
	}
	b, err := json.Marshal(map[string][]string{"chain": ders})
	if err != nil {
		t.Fatalf("marshal chain: %v", err)
	}
	return string(b)
}

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

func main() {
	t := vhelp.New()

	rootDER := vhelp.ReadFixture("minted", "bench-root.der")
	intDER := vhelp.ReadFixture("minted", "bench-intermediate.der")
	leafDER := vhelp.ReadFixture("minted", "leaf-valid.der")
	wantLogID := strings.Fields(string(vhelp.ReadFixture("minted", "keys", "log-id.txt")))[1]

	pubKey, _, _, err := ct.PublicKeyFromPEM(vhelp.ReadFixture("minted", "keys", "log-pubkey.pem"))
	if err != nil {
		t.Fatalf("parse pinned log public key: %v", err)
	}
	verifier, err := ct.NewSignatureVerifier(pubKey)
	if err != nil {
		t.Fatalf("build signature verifier: %v", err)
	}

	// No field this milestone adds appears in the configuration: it is the
	// log shape every milestone since v1.0.5 accepts.
	h := startPersonality(t, "gate", nil, "")
	defer h.stop()

	// 1. get-roots serves exactly the two configured roots.
	gr := h.get(t, h.url(ct.GetRootsPath))
	var roots ct.GetRootsResponse
	if gr.status != http.StatusOK || json.Unmarshal(gr.body, &roots) != nil {
		t.Check("get-roots-exact", false, "status %d body %q", gr.status, string(gr.body))
	} else {
		got := make([]string, 0, len(roots.Certificates))
		for _, c := range roots.Certificates {
			der, err := base64.StdEncoding.DecodeString(c)
			if err != nil {
				t.Fatalf("root not base64: %v", err)
			}
			s := sha256.Sum256(der)
			got = append(got, hex.EncodeToString(s[:]))
		}
		rs, is := sha256.Sum256(rootDER), sha256.Sum256(intDER)
		want := []string{hex.EncodeToString(rs[:]), hex.EncodeToString(is[:])}
		sort.Strings(got)
		sort.Strings(want)
		t.Equal("get-roots-exact", strings.Join(got, ","), strings.Join(want, ","))
	}

	// 2. get-sth reports the pinned backend root and a verifiable signature.
	gs := h.get(t, h.url(ct.GetSTHPath))
	var sthRsp ct.GetSTHResponse
	if gs.status != http.StatusOK || json.Unmarshal(gs.body, &sthRsp) != nil {
		t.Check("get-sth-tree-size", false, "status %d body %q", gs.status, string(gs.body))
		t.Check("get-sth-root-hash", false, "status %d", gs.status)
		t.Check("get-sth-signature-verifies", false, "status %d", gs.status)
	} else {
		t.Equal("get-sth-tree-size", sthRsp.TreeSize, pinnedTreeSize)
		t.Equal("get-sth-root-hash", hex.EncodeToString(sthRsp.SHA256RootHash),
			hex.EncodeToString(pinnedRootHash[:]))
		sth, err := sthRsp.ToSignedTreeHead()
		if err != nil {
			t.Check("get-sth-signature-verifies", false, "convert: %v", err)
		} else {
			t.Case("get-sth-signature-verifies", verifier.VerifySTHSignature(*sth))
		}
	}

	// 3. add-chain of the current leaf is accepted and the SCT it returns is a
	//    real signature by the pinned log key over the leaf the backend saw.
	ac := h.postJSON(t, h.url(ct.AddChainPath), chainJSON(t, "chain-valid.pem"))
	var sctRsp ct.AddChainResponse
	if ac.status != http.StatusOK || json.Unmarshal(ac.body, &sctRsp) != nil {
		t.Check("add-chain-accepted", false, "status %d body %q", ac.status, string(ac.body))
		t.Check("add-chain-log-id", false, "status %d", ac.status)
		t.Check("add-chain-backend-leaf", false, "status %d", ac.status)
		t.Check("add-chain-sct-verifies", false, "status %d", ac.status)
	} else {
		t.Check("add-chain-accepted", sctRsp.SCTVersion == ct.V1, "sct_version %d", sctRsp.SCTVersion)
		t.Equal("add-chain-log-id", base64.StdEncoding.EncodeToString(sctRsp.ID), wantLogID)

		// The leaf the backend was handed must be the RFC 6962 MerkleTreeLeaf
		// for this certificate at the timestamp the SCT reports.
		var got ct.MerkleTreeLeaf
		if _, err := tls.Unmarshal(h.backend.lastLeafValue(), &got); err != nil {
			t.Check("add-chain-backend-leaf", false, "unmarshal queued leaf: %v", err)
		} else {
			want := ct.MerkleTreeLeaf{
				Version:  ct.V1,
				LeafType: ct.TimestampedEntryLeafType,
				TimestampedEntry: &ct.TimestampedEntry{
					Timestamp: sctRsp.Timestamp,
					EntryType: ct.X509LogEntryType,
					X509Entry: &ct.ASN1Cert{Data: leafDER},
				},
			}
			wb, err := tls.Marshal(want)
			if err != nil {
				t.Fatalf("marshal expected leaf: %v", err)
			}
			t.EqualBytes("add-chain-backend-leaf", h.backend.lastLeafValue(), wb)
		}

		var ds ct.DigitallySigned
		exts, eerr := base64.StdEncoding.DecodeString(sctRsp.Extensions)
		if _, err := tls.Unmarshal(sctRsp.Signature, &ds); err != nil || eerr != nil {
			t.Check("add-chain-sct-verifies", false, "decode sct: %v %v", err, eerr)
		} else {
			var id ct.LogID
			copy(id.KeyID[:], sctRsp.ID)
			sct := ct.SignedCertificateTimestamp{
				SCTVersion: sctRsp.SCTVersion,
				LogID:      id,
				Timestamp:  sctRsp.Timestamp,
				Extensions: ct.CTExtensions(exts),
				Signature:  ds,
			}
			entry := ct.LogEntry{Leaf: ct.MerkleTreeLeaf{
				Version:  ct.V1,
				LeafType: ct.TimestampedEntryLeafType,
				TimestampedEntry: &ct.TimestampedEntry{
					Timestamp: sct.Timestamp,
					EntryType: ct.X509LogEntryType,
					X509Entry: &ct.ASN1Cert{Data: leafDER},
				},
			}}
			t.Case("add-chain-sct-verifies", verifier.VerifySCTSignature(sct, entry))
		}
	}

	// 3b. Submitting the same chain again returns 200 with the *same* SCT
	//     (HTTP-API.md §3.2): the SCT is reconstructed from the leaf the
	//     backend already holds for this identity hash, not minted afresh for
	//     the bytes just sent.
	//
	//     Only the four SCT fields below are compared. A case that asserted
	//     the *second submitted leaf* differs byte-for-byte from the stored
	//     one was deleted in round 5: the two MerkleTreeLeaf values differ
	//     only in the CTFE's millisecond timestamp, so two back-to-back
	//     loopback round trips inside one millisecond make them equal and
	//     fail a correct solution. See NOTES.md, "Deleted gate cases".
	dup := h.postJSON(t, h.url(ct.AddChainPath), chainJSON(t, "chain-valid.pem"))
	var dupRsp ct.AddChainResponse
	if dup.status != http.StatusOK || json.Unmarshal(dup.body, &dupRsp) != nil {
		t.Check("duplicate-add-chain-returns-the-same-sct", false,
			"status %d body %q", dup.status, string(dup.body))
	} else {
		// The four fields that are a function of the stored leaf must repeat.
		// The signature bytes are deliberately not compared: HTTP-API.md §3.2
		// says so, because ECDSA is randomised and SCT signing has no cache.
		t.Check("duplicate-add-chain-returns-the-same-sct",
			dupRsp.Timestamp == sctRsp.Timestamp &&
				dupRsp.SCTVersion == sctRsp.SCTVersion &&
				dupRsp.Extensions == sctRsp.Extensions &&
				base64.StdEncoding.EncodeToString(dupRsp.ID) ==
					base64.StdEncoding.EncodeToString(sctRsp.ID),
			"first ts %d second ts %d version %d/%d id equal=%v",
			sctRsp.Timestamp, dupRsp.Timestamp, sctRsp.SCTVersion, dupRsp.SCTVersion,
			base64.StdEncoding.EncodeToString(dupRsp.ID) ==
				base64.StdEncoding.EncodeToString(sctRsp.ID))
	}

	// 4. An unrooted chain is refused, and the queue was not touched. This is
	//    the one admission assertion the gate keeps: chain building against
	//    the configured roots is restructured by this milestone but not
	//    changed in what it decides here, and a personality that admitted
	//    everything would pass every case above.
	beforeQ, _ := h.backend.counts()
	ur := h.postJSON(t, h.url(ct.AddChainPath), chainJSON(t, "chain-unrooted.pem"))
	afterQ, _ := h.backend.counts()
	t.Check("add-chain-unrooted-refused",
		ur.status == http.StatusBadRequest && afterQ == beforeQ,
		"status %d queued %d->%d body %q", ur.status, beforeQ, afterQ, string(ur.body))

	// 5. A path that belongs to no log is a 404.
	nf := h.get(t, h.baseURL+"/no-such-log/ct/v1/get-sth")
	t.Check("unknown-prefix-404", nf.status == http.StatusNotFound, "status %d", nf.status)

	t.Done()
}
