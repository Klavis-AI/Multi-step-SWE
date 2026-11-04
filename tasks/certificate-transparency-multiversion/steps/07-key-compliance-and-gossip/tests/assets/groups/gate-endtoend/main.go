// Group gate-endtoend -- KIND gate (the inherited end-to-end path).
//
// One deterministic end-to-end path through the milestone step 07 inherits.
// The candidate's own ct_server binary is started once against a pinned
// in-process backend over loopback gRPC (serverkit.go), driven through its
// public HTTP surface with the candidate's own client package, and
// cross-checked at the backend: the anchors it publishes, the tree head it
// serves and the signature over it, the SCT it mints for a well-rooted
// submission, the RFC 6962 leaf it hands the backend, its refusal of a chain
// that reaches no configured anchor, and the URL space it occupies. Every one
// of those is v1.0.19 behaviour that v1.0.21 leaves alone, so the whole group
// passes against the step-06 tree as well as its own.
//
// The log is configured exactly as step 06 could configure it -- one log,
// prefix "bench", no new config fields, no new flags -- and is reached over
// its bare path. Nothing below names a symbol that arrived at v1.0.21, so the
// driver compiles at both anchors.
//
// What is deliberately NOT here (gate rule 3, "behaviour this step changes is
// not regression", and rule 4, "nothing in the gate may be satisfiable by the
// new capability alone"). Every one of these was here and made this group an
// unpriced veto: it could not pass against a step-06 tree, so the step's own
// no-op continuation reported a FAILED GATE instead of a zero score and
// measured nothing.
//
//   - the global --handler_prefix, which the whole group used to be served
//     through. v1.0.19's ct_server exits 2 on that flag, so not one case could
//     report. It is graded and priced by ctserver-handler-prefix-paths, which
//     asserts get-sth, get-roots and add-chain under the prefix, the bare path
//     answering 404, and "/" and "/metrics" staying unprefixed -- against a
//     control personality started without the flag.
//   - the per-log override_handler_prefix. Field 13 does not exist in
//     v1.0.19's LogConfig, so the config was refused before start-up. Graded
//     and priced by ctserver-override-handler-prefix, which asks it both ways
//     round across four logs and three spellings of one override.
//   - the two advertised merge delays reaching /metrics. Fields 14 and 15 do
//     not exist at v1.0.19 either. The proto half is graded by
//     config-new-proto-fields and the exported-gauge half by the new scored
//     group ctserver-merge-delay-metrics, which carries the case that moved
//     out of here.
//   - ct.AllowVerificationWithNonCompliantKeys and x509.IsFatal. Neither
//     exists at v1.0.19: naming them did not merely fail against a step-06
//     tree, it stopped this driver compiling there at all. The waiver is
//     graded and priced by signature-verifier-key-compliance (its
//     waiver-defaults-to-false, rsa-1024-refused and rsa-2048-accepted cases
//     are exactly what stood here) and the non-fatal SIA verdict by
//     x509-isfatal-nonfatal-parsing.
//   - the graceful stop. v1.0.19's ct_server calls os.Exit(1) straight out of
//     its signal handler, so "exits 0 on SIGTERM" is this release's behaviour
//     and asserting it in the gate vetoed a candidate that regressed nothing.
//     Graded and priced by the new scored group ctserver-graceful-shutdown,
//     for SIGTERM and SIGINT alike.
//
// SCOPE-BACKEND: the backend is this group's own in-process stub, answering
// from compile-time constants over a :0 loopback port. SCOPE-NET: loopback
// only, nothing resolved. SCOPE-TIME: the tree head the backend serves is a
// constant, so its signature is verified over pinned bytes. The one value the
// candidate takes from a clock is the SCT timestamp, and no case asserts it --
// only that the signature over it verifies and that the same value reached the
// backend inside the leaf. Nothing here reads an elapsed time, a host
// property, or an unordered iteration as if it were ordered: the one list
// compared (get-roots) is sorted on both sides.
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

const gateLogID = int64(1901)

// mustParse parses a pinned fixture through the fork. It is deliberately
// written against the certificate rather than against the error: x509.IsFatal
// is a v1.0.21 name and this driver must compile at v1.0.19 too.
func mustParse(t *vhelp.T, rel ...string) *x509.Certificate {
	cert, err := x509.ParseCertificate(vhelp.ReadFixture(rel...))
	if cert == nil {
		t.Fatalf("parse %v: %v", rel, err)
	}
	return cert
}

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

func main() {
	t := vhelp.New()

	h := startPersonalityNamed(t, "gate", nil,
		logConfigText(t, gateLogID, logPrefix, false, ""))
	defer h.stop()

	at := func(ep string) string { return h.baseURL + "/" + logPrefix + ep }

	pubDER := pemBody(t, vhelp.ReadFixture("minted", "keys", "log-pubkey.pem"))
	pubKey, err := x509.ParsePKIXPublicKey(pubDER)
	if err != nil {
		t.Fatalf("parse pinned log public key: %v", err)
	}
	leafDER := vhelp.ReadFixture("minted", "leaf-valid.der")
	interDER := vhelp.ReadFixture("minted", "bench-intermediate.der")

	lc, err := client.New(at(""), nil, jsonclient.Options{PublicKeyDER: pubDER})
	if err != nil {
		t.Fatalf("client.New: %v", err)
	}
	ctx := context.Background()

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
	sth, err := lc.GetSTH(ctx)
	if err != nil {
		t.Check("get-sth-reports-the-backends-tree-head", false, "GetSTH: %v", err)
		t.Check("get-sth-signature-verifies-under-the-configured-key", false, "GetSTH: %v", err)
	} else {
		t.Equal("get-sth-reports-the-backends-tree-head",
			fmt.Sprintf("size=%d ts=%d root=%x", sth.TreeSize, sth.Timestamp, sth.SHA256RootHash[:]),
			fmt.Sprintf("size=%d ts=%d root=%x", pinnedTreeSize, pinnedTimestampMS, pinnedRootHash[:]))

		// --- 3. and it is signed with the configured key -------------------
		// Recomputed here rather than trusting the client's own check: the
		// signed structure is RFC 6962 s3.5's TreeHeadSignature over values
		// the backend pinned, so the bytes are known in advance.
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
	sct, err := lc.AddChain(ctx, []ct.ASN1Cert{{Data: leafDER}, {Data: interDER}})
	if err != nil {
		for _, id := range []string{
			"add-chain-issues-an-sct-for-the-configured-log",
			"add-chain-sct-signature-verifies-over-the-submission",
			"queued-leaf-is-the-rfc6962-leaf-for-the-submission",
		} {
			t.Check(id, false, "AddChain: %v", err)
		}
	} else {
		wantID := sha256.Sum256(pubDER)
		t.Equal("add-chain-issues-an-sct-for-the-configured-log",
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

		// --- 6. and the backend was handed the RFC 6962 leaf for it --------
		// Built here from the timestamp the SCT reports, with this corpus's
		// own tls codec, and compared against the bytes the stub received.
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

	// --- 7. a chain that does not reach a configured anchor is refused -----
	// Refused with 400 and, crucially, without the backend ever being asked:
	// the queue count must not move.
	before, _, _ := h.backend.counts()
	bad, err := json.Marshal(ct.AddChainRequest{Chain: [][]byte{
		vhelp.ReadFixture("minted", "leaf-unrooted.der"),
		vhelp.ReadFixture("minted", "rogue-root.der"),
	}})
	if err != nil {
		t.Fatalf("marshal unrooted add-chain request: %v", err)
	}
	res = h.postJSON(t, at(ct.AddChainPath), bad)
	after, _, _ := h.backend.counts()
	t.Equal("unrooted-chain-refused-without-reaching-the-backend",
		fmt.Sprintf("status=%d queued=%d", res.status, after-before),
		"status=400 queued=0")

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
