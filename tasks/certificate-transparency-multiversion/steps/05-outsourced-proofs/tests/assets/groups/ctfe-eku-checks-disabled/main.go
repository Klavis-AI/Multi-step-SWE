// Group ctfe-eku-checks-disabled.
//
// v1.0.17 rewrites ctfe.ValidateChain to stop editing parsed certificates in
// place and to ask x509 for lax verification instead (commits bce7f278 and
// 684d6eee, on top of the switchable VerifyOptions added by 41939987). The
// comment states the policy: "CT is intended to observe certificates rather
// than police them", so the CTFE now sets DisableCriticalExtensionChecks,
// DisableEKUChecks, DisablePathLenChecks and DisableNameConstraintChecks.
//
// The externally visible consequence is that a log configured with
// ext_key_usages no longer bounces a submission whose leaf lacks that usage.
// v1.0.13 passed the configured usages to Verify as KeyUsages and the chain
// was refused with 400 if the leaf did not permit them.
//
// Graded over loopback HTTP against the candidate's own ct_server, configured
// with ext_key_usages: "CodeSigning" -- a usage none of the minted leaves
// carries, and one that v1.0.13's leniency rules do not manufacture from
// ServerAuth or ClientAuth. Cases 3, 4 and 5 are controls that hold at both
// anchors, so the group cannot be passed by a CTFE that admits everything or
// refuses everything.
//
// Case 5 is the negative half of the same requirement. The release names
// FOUR switches and DisableNameChecks is deliberately not among them
// (HTTP-API.md section 5.1, X509-FORK.md section 5.6), so issuer/subject
// chaining is still enforced. Without that case a CTFE that sets all six
// switches -- an over-application of the release's "stop policing
// certificates" theme -- is indistinguishable from one that sets the four.
//
// SCOPE-TIME: reject_expired is left unset and no case asserts the SCT
// timestamp; case 6 only checks that the signature over it verifies.
//
// No new identifier is involved: this driver compiles against v1.0.13.
package main

import (
	"encoding/json"
	"fmt"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/ctutil"
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

func main() {
	t := vhelp.New()

	h := startPersonality(t, nil, "  ext_key_usages: \"CodeSigning\"\n")
	defer h.stop()

	interDER := vhelp.ReadFixture("minted", "bench-intermediate.der")

	submit := func(chain ...[]byte) httpResult {
		body, err := json.Marshal(ct.AddChainRequest{Chain: chain})
		if err != nil {
			t.Fatalf("marshal add-chain request: %v", err)
		}
		return h.postJSON(t, h.logURL("/ct/v1/add-chain"), body)
	}

	// --- 1. a ServerAuth-only leaf is admitted under a CodeSigning config --
	serverAuthDER := vhelp.ReadFixture("minted", "leaf-eku-serverauth.der")
	res := submit(serverAuthDER, interDER)
	t.Equal("serverauth-leaf-admitted-under-codesigning-config", res.status, 200)

	// --- 2. so is a ClientAuth-only one ------------------------------------
	clientAuthRes := submit(vhelp.ReadFixture("minted", "leaf-eku-clientauth.der"), interDER)
	t.Equal("clientauth-leaf-admitted-under-codesigning-config", clientAuthRes.status, 200)

	// --- 3. control: a leaf with no EKU extension was always unconstrained -
	noEKURes := submit(vhelp.ReadFixture("minted", "leaf-eku-none.der"), interDER)
	t.Equal("eku-free-leaf-admitted", noEKURes.status, 200)

	// --- 4. control: chain building is untouched ---------------------------
	// Laxer usage checking is not laxer trust: a chain terminating in an
	// untrusted self-signed root is still refused.
	unrooted := submit(
		vhelp.ReadFixture("minted", "leaf-unrooted.der"),
		vhelp.ReadFixture("minted", "rogue-root.der"))
	t.Equal("unrooted-chain-still-refused", unrooted.status, 400)

	// --- 5. control: issuer/subject chaining is untouched ------------------
	// leaf-issuer-mismatch carries bench-intermediate's AuthorityKeyId and a
	// signature bench-intermediate's key really made, so path building finds
	// the right parent and CheckSignatureFrom succeeds; its issuer DN is a
	// different name, which is the single defect x509's DisableNameChecks
	// suppresses. Refused at both anchors -- v1.0.13 has no such switch and
	// v1.0.17 leaves it false -- so a correct implementation of either
	// release passes this case, and only an over-application of the switch
	// set fails it.
	nameMismatch := submit(vhelp.ReadFixture("minted", "leaf-issuer-mismatch.der"), interDER)
	t.Equal("issuer-name-mismatch-still-refused", nameMismatch.status, 400)

	// --- 6. and what came back for case 1 is a real SCT ---------------------
	if res.status != 200 {
		t.Check("admitted-submission-yields-verifiable-sct", false, "add-chain returned %d", res.status)
	} else {
		var wire ct.AddChainResponse
		if err := json.Unmarshal(res.body, &wire); err != nil {
			t.Check("admitted-submission-yields-verifiable-sct", false, "decode add-chain response: %v", err)
		} else {
			var ds ct.DigitallySigned
			if _, err := tls.Unmarshal(wire.Signature, &ds); err != nil {
				t.Check("admitted-submission-yields-verifiable-sct", false, "unmarshal SCT signature: %v", err)
			} else {
				sct := &ct.SignedCertificateTimestamp{
					SCTVersion: ct.Version(wire.SCTVersion),
					Timestamp:  wire.Timestamp,
					Extensions: ct.CTExtensions(wire.Extensions),
					Signature:  ds,
				}
				copy(sct.LogID.KeyID[:], wire.ID)
				pubKey, err := x509.ParsePKIXPublicKey(pemBody(t, vhelp.ReadFixture("minted", "keys", "log-pubkey.pem")))
				if err != nil {
					t.Fatalf("parse pinned log public key: %v", err)
				}
				chain := []*x509.Certificate{
					mustParse(t, "minted", "leaf-eku-serverauth.der"),
					mustParse(t, "minted", "bench-intermediate.der"),
				}
				if err := ctutil.VerifySCT(pubKey, chain, sct, false); err != nil {
					t.Check("admitted-submission-yields-verifiable-sct", false, "VerifySCT: %v", err)
				} else {
					t.Equal("admitted-submission-yields-verifiable-sct",
						fmt.Sprintf("v=%d", sct.SCTVersion), fmt.Sprintf("v=%d", ct.V1))
				}
			}
		}
	}

	t.Done()
}
