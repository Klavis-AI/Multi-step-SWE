// Group gate-endtoend.
//
// The whole-milestone gate: the candidate's own ct_server binary, started
// against a pinned in-process backend over loopback gRPC (see serverkit.go),
// exercised through its public HTTP surface.
//
// GATE SCOPE. Under `reward = gate x score` this group is proof of life plus
// *inherited* regression only. Every case below asserts behaviour the step-05
// anchor (v1.0.17) already has: the log publishes the anchors it was
// configured with, it publishes and signs the tree its backend reports, it
// admits a well-rooted submission and returns a verifiable SCT for it, it
// refuses a chain that reaches no configured anchor without queueing
// anything, and it routes only the paths it registered. Nothing here is
// satisfied by step 06's new capability, and nothing here needs it: the
// driver compiles and passes against the step-05 tree, so a no-op
// continuation reports `gate = PASS, score = 0` rather than
// `GATE FAILED -- MEASURED NOTHING`.
//
// Deliberately NOT here (see NOTES.md, step 06):
//   - the two ChargeTo assertions -- step-06 new capability, graded and priced
//     by the scored groups ctfe-read-endpoints-charge-remote-user (w=3) and
//     ctfe-add-chain-charges-intermediates (w=3);
//   - the BuildLogLeaf byte-identity of the queued leaf -- moved to the scored
//     group trillian-util-build-log-leaf (w=2);
//   - (*ct.GetSTHResponse).ToSignedTreeHead, which does not exist at v1.0.17.
//     The STH below is therefore assembled from the response's own fields, the
//     way the client open-coded it before this release; the new conversion is
//     graded by the scored group sth-response-to-signed-tree-head (w=1).
//
// SCOPE-BACKEND: the backend is the group's own in-process stub, answering
// from constants over a :0 loopback port.
// SCOPE-TIME: the STH the backend serves is a constant, so its signature is
// verified over pinned bytes. The one value the candidate takes from a clock
// is the SCT timestamp; no case asserts it, only that the signature over it
// verifies.
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

func mustParse(t *vhelp.T, rel ...string) *x509.Certificate {
	cert, err := x509.ParseCertificate(vhelp.ReadFixture(rel...))
	if err != nil {
		if _, ok := err.(x509.NonFatalErrors); !ok {
			t.Fatalf("parse %v: %v", rel, err)
		}
	}
	return cert
}

// pemCerts returns every DER body in a concatenated-PEM fixture.
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

// treeHeadFrom assembles a SignedTreeHead out of a get-sth response body. It
// is the conversion v1.0.17's client.GetSTH performs inline -- copy the root
// hash into the fixed-size array, TLS-decode the tree head signature -- and
// is written out here rather than called through the v1.0.19 method
// (*ct.GetSTHResponse).ToSignedTreeHead, which the gate must not depend on.
func treeHeadFrom(body []byte) (*ct.SignedTreeHead, error) {
	var rsp ct.GetSTHResponse
	if err := json.Unmarshal(body, &rsp); err != nil {
		return nil, err
	}
	if len(rsp.SHA256RootHash) != sha256.Size {
		return nil, fmt.Errorf("sha256_root_hash is %d bytes, want %d", len(rsp.SHA256RootHash), sha256.Size)
	}
	sth := &ct.SignedTreeHead{
		Version:   ct.V1,
		TreeSize:  rsp.TreeSize,
		Timestamp: rsp.Timestamp,
	}
	copy(sth.SHA256RootHash[:], rsp.SHA256RootHash)
	var ds ct.DigitallySigned
	if rest, err := tls.Unmarshal(rsp.TreeHeadSignature, &ds); err != nil {
		return nil, err
	} else if len(rest) > 0 {
		return nil, fmt.Errorf("%d trailing bytes after the tree head signature", len(rest))
	}
	sth.TreeHeadSignature = ds
	return sth, nil
}

// sctFrom rebuilds the SCT from an add-chain response body, exactly as
// RFC 6962 section 4.1 defines the fields.
func sctFrom(body []byte) (*ct.SignedCertificateTimestamp, error) {
	var rsp ct.AddChainResponse
	if err := json.Unmarshal(body, &rsp); err != nil {
		return nil, err
	}
	var ds ct.DigitallySigned
	if rest, err := tls.Unmarshal(rsp.Signature, &ds); err != nil {
		return nil, err
	} else if len(rest) > 0 {
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

func main() {
	t := vhelp.New()

	h := startPersonality(t, nil, "")
	defer h.stop()

	pubDER := pemBody(t, vhelp.ReadFixture("minted", "keys", "log-pubkey.pem"))
	pubKey, err := x509.ParsePKIXPublicKey(pubDER)
	if err != nil {
		t.Fatalf("parse pinned log public key: %v", err)
	}
	leafDER := vhelp.ReadFixture("minted", "leaf-valid.der")
	interDER := vhelp.ReadFixture("minted", "bench-intermediate.der")

	// --- 1. the log publishes the anchors it was configured with -----------
	res := h.get(t, h.logURL("/ct/v1/get-roots"))
	var roots struct {
		Certificates [][]byte `json:"certificates"`
	}
	if res.status != 200 {
		t.Check("get-roots-returns-configured-anchors", false, "status %d", res.status)
	} else if err := json.Unmarshal(res.body, &roots); err != nil {
		t.Check("get-roots-returns-configured-anchors", false, "decode: %v", err)
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
		t.Equal("get-roots-returns-configured-anchors", fmt.Sprintf("%v", got), fmt.Sprintf("%v", want))
	}

	// --- 2. get-sth reports the backend's tree ------------------------------
	res = h.get(t, h.logURL("/ct/v1/get-sth"))
	var sth *ct.SignedTreeHead
	if res.status == 200 {
		sth, _ = treeHeadFrom(res.body)
	}
	if sth == nil {
		for _, id := range []string{"get-sth-reflects-backend-root", "get-sth-signature-verifies-under-log-key"} {
			t.Check(id, false, "no usable STH: status=%d body=%s", res.status, lastBytes(res.body, 120))
		}
	} else {
		t.Equal("get-sth-reflects-backend-root",
			fmt.Sprintf("size=%d ts=%d root=%x", sth.TreeSize, sth.Timestamp, sth.SHA256RootHash[:]),
			fmt.Sprintf("size=%d ts=%d root=%x", pinnedTreeSize, pinnedTimestampMS, pinnedRootHash[:]))

		// --- 3. and signs it with the configured key -----------------------
		signed, err := tls.Marshal(ct.TreeHeadSignature{
			Version:        ct.V1,
			SignatureType:  ct.TreeHashSignatureType,
			Timestamp:      sth.Timestamp,
			TreeSize:       sth.TreeSize,
			SHA256RootHash: sth.SHA256RootHash,
		})
		if err != nil {
			t.Check("get-sth-signature-verifies-under-log-key", false, "marshal TreeHeadSignature: %v", err)
		} else if err := tls.VerifySignature(pubKey, signed, tls.DigitallySigned(sth.TreeHeadSignature)); err != nil {
			t.Check("get-sth-signature-verifies-under-log-key", false, "VerifySignature: %v", err)
		} else {
			t.Check("get-sth-signature-verifies-under-log-key", true, "")
		}
	}

	// --- 4. a well-rooted submission is admitted and gets an SCT ------------
	req, err := json.Marshal(ct.AddChainRequest{Chain: [][]byte{leafDER, interDER}})
	if err != nil {
		t.Fatalf("marshal add-chain request: %v", err)
	}
	res = h.postJSON(t, h.logURL("/ct/v1/add-chain"), req)
	var sct *ct.SignedCertificateTimestamp
	if res.status == 200 {
		sct, err = sctFrom(res.body)
		if err != nil {
			t.Check("add-chain-issues-sct-for-the-configured-log", false, "malformed response: %v", err)
		}
	}
	if sct == nil {
		for _, id := range []string{
			"add-chain-issues-sct-for-the-configured-log",
			"add-chain-sct-signature-verifies-over-the-submission",
		} {
			t.Check(id, false, "no SCT: status=%d body=%s", res.status, lastBytes(res.body, 120))
		}
	} else {
		wantID := sha256.Sum256(pubDER)
		t.Equal("add-chain-issues-sct-for-the-configured-log",
			fmt.Sprintf("v=%d id=%x ext=%d", sct.SCTVersion, sct.LogID.KeyID[:], len(sct.Extensions)),
			fmt.Sprintf("v=%d id=%x ext=%d", ct.V1, wantID[:], 0))

		// --- 5. the SCT really is a signature over what was submitted ------
		chain := []*x509.Certificate{
			mustParse(t, "minted", "leaf-valid.der"),
			mustParse(t, "minted", "bench-intermediate.der"),
		}
		if err := ctutil.VerifySCT(pubKey, chain, sct, false); err != nil {
			t.Check("add-chain-sct-signature-verifies-over-the-submission", false, "VerifySCT: %v", err)
		} else {
			t.Check("add-chain-sct-signature-verifies-over-the-submission", true, "")
		}
	}

	// --- 6. a chain that does not reach a configured anchor is refused, and
	//        never reaches the backend at all -------------------------------
	// Chain validation runs before the queue call at every anchor in range, so
	// the leaf count the backend saw must not move. This is the inherited
	// admission-control path; the quota half of the same refusal (that it also
	// costs nobody any quota) is step-06 capability and is graded by the
	// scored group ctfe-add-chain-charges-remote-user.
	beforeQueued, _, _ := h.backend.queueSnapshot()
	req, err = json.Marshal(ct.AddChainRequest{Chain: [][]byte{
		vhelp.ReadFixture("minted", "leaf-unrooted.der"),
		vhelp.ReadFixture("minted", "rogue-root.der"),
	}})
	if err != nil {
		t.Fatalf("marshal unrooted add-chain request: %v", err)
	}
	bad := h.postJSON(t, h.logURL("/ct/v1/add-chain"), req)
	afterQueued, _, _ := h.backend.queueSnapshot()
	t.Equal("unrooted-chain-refused-and-never-queued",
		fmt.Sprintf("status=%d queued=%d", bad.status, afterQueued),
		fmt.Sprintf("status=%d queued=%d", 400, beforeQueued))

	// --- 7. and the root handler answers only for "/" -----------------------
	onRoot := h.get(t, h.rootURL("/"))
	elsewhere := h.get(t, h.rootURL("/no-such-endpoint"))
	t.Equal("unmatched-path-returns-404",
		fmt.Sprintf("root=%d other=%d", onRoot.status, elsewhere.status),
		"root=200 other=404")

	t.Done()
}
