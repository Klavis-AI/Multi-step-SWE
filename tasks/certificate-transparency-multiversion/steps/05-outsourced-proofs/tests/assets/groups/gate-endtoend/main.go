// Group gate-endtoend -- KIND gate (the inherited end-to-end path).
//
// One deterministic end-to-end path through the milestone step 05 inherits.
// The candidate's own ct_server binary is started against a pinned in-process
// backend over loopback gRPC (see serverkit.go), driven through its public
// HTTP surface with the candidate's own client package, and cross-checked at
// the backend: the anchors it publishes, the tree head it serves and the
// signature over it, the SCT it mints for a well-rooted submission, the RFC
// 6962 leaf it hands the backend, its refusal of a chain that reaches no
// configured anchor, and the binding of an SCT signature to the submission it
// was minted for. Every one of those is v1.0.13 behaviour that v1.0.17 leaves
// alone, so the whole group passes against the step-04 tree.
//
// Case 8 is a deliberate carryover. The SCT half of the v1.0.7 advisory -- an
// SCT signature is over the whole signature input, so no two distinct
// submissions can share one -- is graded at its own milestone by step 03's
// group inherited-sct-signature-inputs. Step 05 still requires it and Harbor
// never re-runs an earlier step's verifier, so a single submission was not
// enough here: the path only proves the property once two different chains
// have been through it.
//
// What is deliberately NOT here (gate rule 3, "behaviour this step changes is
// not regression", and rule 4, "nothing in the gate may be satisfiable by the
// new capability alone"):
//
//   - the leaf hash taken through ct.LeafHashForLeaf. That entrypoint is new
//     at v1.0.17, and naming it here did not merely fail against a step-04
//     tree, it stopped this driver compiling there at all. It is graded and
//     priced by the scored group leafhash-for-leaf-entrypoint, which carries
//     the wire-shaped case that moved out of here: a MerkleTreeLeaf recovered
//     with tls.Unmarshal, hashed through the entrypoint, compared against an
//     independent SHA-256(0x00 || leaf) recomputation.
//   - the narrowing of the catch-all. v1.0.13 answered 200 for every unmatched
//     path; v1.0.17 answers 404 and keeps the 200 for "/". That transition is
//     this release's, and it is graded and priced by the scored group
//     ctfe-cors-headers, whose cases root-path-is-cors-wrapped-too and
//     unclaimed-path-is-404-and-still-cors-wrapped assert both halves of it.
//   - the withdrawal of the merkletree/ package, this step's headline. A gate
//     case asserting the old import path still resolves would make the Oracle
//     fail its own step; one asserting the package is gone could never pass
//     against the step-04 tree, where it still exists. Either way the
//     withdrawal belongs in the scored tier, and it is graded there by
//     merkletree-package-withdrawn.
//
// Left in the gate, each of those would have vetoed the whole step for a
// candidate that regressed nothing, and would have made this step's own no-op
// continuation report a FAILED GATE rather than a zero score -- which measures
// nothing at all.
//
// SCOPE-TIME: the STH the backend serves is a constant, so the STH signature
// is verified over pinned bytes. The one value the candidate takes from a
// clock is the SCT timestamp; no case asserts it, only that the signature over
// it verifies and that the same value reached the backend. SCOPE-NET: loopback
// only, every port obtained from serverkit.go's freeLoopbackPort. Nothing below
// reads an elapsed time, a host property or an unordered iteration as if it
// were ordered: the one list compared (get-roots) is sorted on both sides.
//
// The personality coming up at all is a fatal prerequisite in serverkit.go,
// not a counted case.
package main

import (
	"context"
	"crypto/sha256"
	"encoding/base64"
	"encoding/json"
	"encoding/pem"
	"fmt"
	"sort"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/client"
	"github.com/google/certificate-transparency-go/ctutil"
	"github.com/google/certificate-transparency-go/jsonclient"
	"github.com/google/certificate-transparency-go/tls"
	"github.com/google/certificate-transparency-go/x509"

	"ctverify/vhelp"
)

func mustParse(t *vhelp.T, rel ...string) *x509.Certificate {
	cert, err := x509.ParseCertificate(vhelp.ReadFixture(rel...))
	if err != nil {
		t.Fatalf("parse %v: %v", rel, err)
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

	lc, err := client.New(h.logURL(""), nil, jsonclient.Options{PublicKeyDER: pubDER})
	if err != nil {
		t.Fatalf("client.New: %v", err)
	}
	ctx := context.Background()

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

	// --- 2. get-sth reports the backend's tree, unchanged -------------------
	sth, err := lc.GetSTH(ctx)
	if err != nil {
		t.Check("get-sth-reflects-backend-root", false, "GetSTH: %v", err)
		t.Check("get-sth-signature-verifies-under-log-key", false, "GetSTH: %v", err)
	} else {
		t.Equal("get-sth-reflects-backend-root",
			fmt.Sprintf("size=%d ts=%d root=%x", sth.TreeSize, sth.Timestamp, sth.SHA256RootHash[:]),
			fmt.Sprintf("size=%d ts=%d root=%x", pinnedTreeSize, pinnedTimestampMS, pinnedRootHash[:]))

		// --- 3. and signs it with the configured key -----------------------
		// Recomputed here rather than trusting the client's own check: the
		// signed structure is RFC 6962 s3.5's TreeHeadSignature over pinned
		// values.
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
	sct, err := lc.AddChain(ctx, []ct.ASN1Cert{{Data: leafDER}, {Data: interDER}})
	if err != nil {
		for _, id := range []string{
			"add-chain-issues-sct-for-the-configured-log",
			"add-chain-sct-signature-verifies-over-the-submission",
			"queued-leaf-is-the-rfc6962-leaf-for-the-submission",
		} {
			t.Check(id, false, "AddChain: %v", err)
		}
	} else {
		wantID := sha256.Sum256(pubDER)
		t.Equal("add-chain-issues-sct-for-the-configured-log",
			fmt.Sprintf("v=%d id=%x ext=%d", sct.SCTVersion, sct.LogID.KeyID[:], len(sct.Extensions)),
			fmt.Sprintf("v=%d id=%x ext=%d", ct.V1, wantID[:], 0))

		// --- 5. the SCT really is a signature over what was submitted ------
		chain := []*x509.Certificate{mustParse(t, "minted", "leaf-valid.der"), mustParse(t, "minted", "bench-intermediate.der")}
		if err := ctutil.VerifySCT(pubKey, chain, sct, false); err != nil {
			t.Check("add-chain-sct-signature-verifies-over-the-submission", false, "VerifySCT: %v", err)
		} else {
			t.Check("add-chain-sct-signature-verifies-over-the-submission", true, "")
		}

		// --- 6. and the backend was handed the matching RFC 6962 leaf ------
		calls, queued := h.backend.queueSnapshot()
		var logged ct.MerkleTreeLeaf
		rest, uerr := tls.Unmarshal(queued, &logged)
		switch {
		case calls != 1:
			t.Check("queued-leaf-is-the-rfc6962-leaf-for-the-submission", false, "backend saw %d queued leaves, want 1", calls)
		case uerr != nil || len(rest) > 0:
			t.Check("queued-leaf-is-the-rfc6962-leaf-for-the-submission", false, "queued leaf is not a MerkleTreeLeaf: %v (%d trailing bytes)", uerr, len(rest))
		default:
			t.Equal("queued-leaf-is-the-rfc6962-leaf-for-the-submission",
				fmt.Sprintf("v=%d type=%d entry=%d ts=%d der=%x",
					logged.Version, logged.LeafType, logged.TimestampedEntry.EntryType,
					logged.TimestampedEntry.Timestamp, logged.TimestampedEntry.X509Entry.Data),
				fmt.Sprintf("v=%d type=%d entry=%d ts=%d der=%x",
					ct.V1, ct.TimestampedEntryLeafType, ct.X509LogEntryType,
					sct.Timestamp, leafDER))
		}
	}

	// --- 7. a chain that does not reach a configured anchor is refused ------
	req, err := json.Marshal(ct.AddChainRequest{Chain: [][]byte{
		vhelp.ReadFixture("minted", "leaf-unrooted.der"),
		vhelp.ReadFixture("minted", "rogue-root.der"),
	}})
	if err != nil {
		t.Fatalf("marshal add-chain request: %v", err)
	}
	res = h.postJSON(t, h.logURL("/ct/v1/add-chain"), req)
	t.Equal("unrooted-chain-refused", res.status, 400)

	// --- 8. and a second, different submission gets its own signature ------
	// The SCT that came back for case 4 must not verify over this chain, and
	// this one must. A *different* pinned leaf is used rather than a repeat of
	// case 4's: HTTP-API.md section 3.2 reserves the identical-resubmission
	// case for the backend's deduplication, so this group does not ask for it.
	// leaf-eku-serverauth is admitted at both anchors under this group's
	// configuration, which declares no ext_key_usages, so nothing here depends
	// on v1.0.17's lax-verification rewrite. This runs after case 6 has already
	// taken its queue snapshot, so the extra queued leaf cannot disturb it.
	secondDER := vhelp.ReadFixture("minted", "leaf-eku-serverauth.der")
	sct2, aerr := lc.AddChain(ctx, []ct.ASN1Cert{{Data: secondDER}, {Data: interDER}})
	switch {
	case aerr != nil:
		t.Check("second-submission-sct-is-bound-to-its-own-chain", false, "AddChain: %v", aerr)
	case sct == nil:
		t.Check("second-submission-sct-is-bound-to-its-own-chain", false,
			"case 4 produced no SCT to cross-check against")
	default:
		chain2 := []*x509.Certificate{
			mustParse(t, "minted", "leaf-eku-serverauth.der"),
			mustParse(t, "minted", "bench-intermediate.der"),
		}
		own := ctutil.VerifySCT(pubKey, chain2, sct2, false)
		crossed := ctutil.VerifySCT(pubKey, chain2, sct, false)
		t.Check("second-submission-sct-is-bound-to-its-own-chain",
			own == nil && crossed != nil,
			"its own SCT over its own chain: %v (want nil); case 4's SCT over this chain: %v (want an error)",
			own, crossed)
	}

	t.Done()
}
