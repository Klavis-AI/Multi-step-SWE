// Group inherited-ctfe-serving-contract.
//
// The RFC 6962 serving contract of the CTFE personality, exercised against
// the candidate's own ct_server binary over a real loopback socket. This is
// the obligation step 07 rearranges the most machinery around: instance
// set-up changes shape (SetUpInstance takes an options struct), the tree head
// is fetched through a new STHGetter indirection instead of being read
// directly, every handler can now be moved under a global --handler_prefix or
// a per-log override, and the config proto grows four fields. None of that is
// allowed to disturb what a plain, unprefixed, non-mirror log serves.
//
// So the group configures exactly what step 06 could configure -- one log,
// prefix "bench", no new fields, no new flags -- and pins the four responses
// that define the log: its trust anchors, its signed tree head, the SCT it
// mints, and the leaf it hands the backend.
//
// It is an inherited-obligation group: it is supposed to pass against the
// previous milestone too, and it does.
//
// SCOPE-BACKEND: the backend is this group's own in-process stub (serverkit.go),
// answering from pinned constants over a :0 loopback port. SCOPE-NET: loopback
// only, nothing resolved. SCOPE-TIME: the tree head is constant, so its
// signature is checked over pinned bytes; the SCT timestamp is the one value
// the candidate takes from a clock and no case asserts it -- only that the
// signature over it verifies and that the same value reached the backend
// inside the leaf.
package main

import (
	"crypto/sha256"
	"encoding/base64"
	"encoding/json"
	"encoding/pem"
	"fmt"
	"sort"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/ctutil"
	"github.com/google/certificate-transparency-go/tls"
	"github.com/google/certificate-transparency-go/x509"

	"ctverify/vhelp"
)

const servingLogID = int64(1801)

// pemCerts returns every certificate DER in a concatenated-PEM fixture.
func pemCerts(b []byte) [][]byte {
	var out [][]byte
	for {
		blk, rest := pem.Decode(b)
		if blk == nil {
			return out
		}
		if blk.Type == "CERTIFICATE" {
			out = append(out, blk.Bytes)
		}
		b = rest
	}
}

// sctFrom rebuilds an SCT from an add-chain response body, field for field as
// RFC 6962 section 4.1 defines it.
func sctFrom(body []byte) (*ct.SignedCertificateTimestamp, error) {
	var rsp ct.AddChainResponse
	if err := json.Unmarshal(body, &rsp); err != nil {
		return nil, err
	}
	var ds ct.DigitallySigned
	rest, err := tls.Unmarshal(rsp.Signature, &ds)
	if err != nil {
		return nil, err
	}
	if len(rest) > 0 {
		return nil, fmt.Errorf("%d trailing bytes after the signature", len(rest))
	}
	exts, err := base64.StdEncoding.DecodeString(rsp.Extensions)
	if err != nil {
		return nil, err
	}
	var logID ct.LogID
	copy(logID.KeyID[:], rsp.ID)
	return &ct.SignedCertificateTimestamp{
		SCTVersion: rsp.SCTVersion,
		LogID:      logID,
		Timestamp:  rsp.Timestamp,
		Extensions: ct.CTExtensions(exts),
		Signature:  ds,
	}, nil
}

func mustParse(t *vhelp.T, rel ...string) *x509.Certificate {
	cert, err := x509.ParseCertificate(vhelp.ReadFixture(rel...))
	if cert == nil {
		t.Fatalf("parse %v: %v", rel, err)
	}
	return cert
}

func main() {
	t := vhelp.New()

	h := startPersonalityNamed(t, "serving", nil,
		logConfigText(t, servingLogID, logPrefix, false, ""))
	defer h.stop()

	at := func(ep string) string { return h.baseURL + "/" + logPrefix + ep }

	pubDER := pemBody(t, vhelp.ReadFixture("minted", "keys", "log-pubkey.pem"))
	pubKey, err := x509.ParsePKIXPublicKey(pubDER)
	if err != nil {
		t.Fatalf("parse pinned log public key: %v", err)
	}
	leafDER := vhelp.ReadFixture("minted", "leaf-valid.der")
	interDER := vhelp.ReadFixture("minted", "bench-intermediate.der")

	// --- 1. the log publishes exactly its configured anchors ---------------
	res := h.get(t, at(ct.GetRootsPath))
	var roots struct {
		Certificates [][]byte `json:"certificates"`
	}
	if res.status != 200 {
		t.Check("get-roots-publishes-the-configured-trust-pool", false, "status %d", res.status)
	} else if err := json.Unmarshal(res.body, &roots); err != nil {
		t.Check("get-roots-publishes-the-configured-trust-pool", false, "decode: %v", err)
	} else {
		got := make([]string, 0, len(roots.Certificates))
		for _, d := range roots.Certificates {
			got = append(got, base64.StdEncoding.EncodeToString(d))
		}
		want := make([]string, 0, 2)
		for _, d := range pemCerts(vhelp.ReadFixture("minted", "roots-bench-plus-int.pem")) {
			want = append(want, base64.StdEncoding.EncodeToString(d))
		}
		sort.Strings(got)
		sort.Strings(want)
		t.Equal("get-roots-publishes-the-configured-trust-pool",
			fmt.Sprintf("%v", got), fmt.Sprintf("%v", want))
	}

	// --- 2. get-sth reports the backend's tree head, unchanged -------------
	res = h.get(t, at(ct.GetSTHPath))
	var sthRsp ct.GetSTHResponse
	var sth *ct.SignedTreeHead
	if res.status == 200 {
		if err := json.Unmarshal(res.body, &sthRsp); err == nil {
			sth, _ = sthRsp.ToSignedTreeHead()
		}
	}
	if sth == nil {
		for _, id := range []string{
			"get-sth-reports-the-backends-tree-head",
			"get-sth-signature-verifies-under-the-configured-key",
		} {
			t.Check(id, false, "no usable STH: status=%d body=%s", res.status, lastBytes(res.body, 120))
		}
	} else {
		t.Equal("get-sth-reports-the-backends-tree-head",
			fmt.Sprintf("size=%d ts=%d root=%x", sth.TreeSize, sth.Timestamp, sth.SHA256RootHash[:]),
			fmt.Sprintf("size=%d ts=%d root=%x", pinnedTreeSize, pinnedTimestampMS, pinnedRootHash[:]))

		// --- 3. and it is signed with the configured key -------------------
		signed, err := tls.Marshal(ct.TreeHeadSignature{
			Version:        ct.V1,
			SignatureType:  ct.TreeHashSignatureType,
			Timestamp:      sth.Timestamp,
			TreeSize:       sth.TreeSize,
			SHA256RootHash: sth.SHA256RootHash,
		})
		if err != nil {
			t.Check("get-sth-signature-verifies-under-the-configured-key", false, "marshal TreeHeadSignature: %v", err)
		} else if err := tls.VerifySignature(pubKey, signed, tls.DigitallySigned(sth.TreeHeadSignature)); err != nil {
			t.Check("get-sth-signature-verifies-under-the-configured-key", false, "VerifySignature: %v", err)
		} else {
			t.Check("get-sth-signature-verifies-under-the-configured-key", true, "")
		}
	}

	// --- 4. a well-rooted submission is admitted, with the right log id ----
	req, err := json.Marshal(ct.AddChainRequest{Chain: [][]byte{leafDER, interDER}})
	if err != nil {
		t.Fatalf("marshal add-chain request: %v", err)
	}
	res = h.postJSON(t, at(ct.AddChainPath), req)
	var sct *ct.SignedCertificateTimestamp
	if res.status == 200 {
		sct, err = sctFrom(res.body)
		if err != nil {
			t.Check("add-chain-issues-an-sct-for-the-configured-log", false, "malformed response: %v", err)
		}
	}
	if sct == nil {
		for _, id := range []string{
			"add-chain-issues-an-sct-for-the-configured-log",
			"add-chain-sct-signature-verifies-over-the-submission",
			"queued-leaf-is-the-rfc6962-leaf-for-the-submission",
		} {
			t.Check(id, false, "no SCT: status=%d body=%s", res.status, lastBytes(res.body, 120))
		}
	} else {
		wantID := sha256.Sum256(pubDER)
		t.Equal("add-chain-issues-an-sct-for-the-configured-log",
			fmt.Sprintf("v=%d id=%x ext=%d", sct.SCTVersion, sct.LogID.KeyID[:], len(sct.Extensions)),
			fmt.Sprintf("v=%d id=%x ext=%d", ct.V1, wantID[:], 0))

		// --- 5. the SCT is a real signature over what was submitted --------
		chain := []*x509.Certificate{
			mustParse(t, "minted", "leaf-valid.der"),
			mustParse(t, "minted", "bench-intermediate.der"),
		}
		if err := ctutil.VerifySCT(pubKey, chain, sct, false); err != nil {
			t.Check("add-chain-sct-signature-verifies-over-the-submission", false, "VerifySCT: %v", err)
		} else {
			t.Check("add-chain-sct-signature-verifies-over-the-submission", true, "")
		}

		// --- 6. and the backend was handed the RFC 6962 leaf for it --------
		want, err := tls.Marshal(ct.MerkleTreeLeaf{
			Version:  ct.V1,
			LeafType: ct.TimestampedEntryLeafType,
			TimestampedEntry: &ct.TimestampedEntry{
				Timestamp: sct.Timestamp,
				EntryType: ct.X509LogEntryType,
				X509Entry: &ct.ASN1Cert{Data: leafDER},
			},
		})
		if err != nil {
			t.Check("queued-leaf-is-the-rfc6962-leaf-for-the-submission", false, "tls.Marshal: %v", err)
		} else {
			queued, _, got := h.backend.counts()
			t.Equal("queued-leaf-is-the-rfc6962-leaf-for-the-submission",
				fmt.Sprintf("queued=%d leaf=%x", queued, got),
				fmt.Sprintf("queued=%d leaf=%x", 1, want))
		}
	}

	// --- 7. a chain that does not reach the pool is refused at the front ---
	// Refused with 400 and, crucially, without the backend ever being asked:
	// the queue count must still be the single entry case 6 accounted for.
	bad, err := json.Marshal(ct.AddChainRequest{Chain: [][]byte{
		vhelp.ReadFixture("minted", "leaf-unrooted.der"),
		vhelp.ReadFixture("minted", "rogue-root.der"),
	}})
	if err != nil {
		t.Fatalf("marshal unrooted add-chain request: %v", err)
	}
	res = h.postJSON(t, at(ct.AddChainPath), bad)
	queuedAfter, _, _ := h.backend.counts()
	t.Equal("an-unrooted-chain-is-refused-without-reaching-the-backend",
		fmt.Sprintf("status=%d queued=%d", res.status, queuedAfter),
		"status=400 queued=1")

	// --- 8. and the log occupies only its own URL space --------------------
	// The root handler answers, an unknown endpoint and an unconfigured log
	// prefix do not, so the 200s above cannot have come from a catch-all.
	t.Equal("only-the-configured-log-prefix-is-served",
		fmt.Sprintf("root=%d configured=%d unknown-log=%d unknown-endpoint=%d bare-endpoint=%d",
			h.get(t, h.baseURL+"/").status,
			h.get(t, at(ct.GetSTHPath)).status,
			h.get(t, h.baseURL+"/no-such-log"+ct.GetSTHPath).status,
			h.get(t, h.baseURL+"/"+logPrefix+"/ct/v1/no-such-endpoint").status,
			h.get(t, h.baseURL+ct.GetSTHPath).status),
		"root=200 configured=200 unknown-log=404 unknown-endpoint=404 bare-endpoint=404")

	t.Done()
}
