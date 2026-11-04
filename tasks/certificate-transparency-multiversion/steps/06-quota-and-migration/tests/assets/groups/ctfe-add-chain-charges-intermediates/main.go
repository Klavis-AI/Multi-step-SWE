// Group ctfe-add-chain-charges-intermediates.
//
// The other half of v1.0.19's quota work: besides the submitter, the CTFE asks
// its backend to charge every CA in the submission, so that one busy issuer
// can be rate limited without touching anybody else. ct_server installs
// InstanceOptions.CertificateQuotaUser (from --quota_intermediate, default
// true) as ctfe.QuotaUserForCert, and addChainInternal appends one user per
// certificate of the *verified* chain after the leaf:
//
//	for _, cert := range chain[1:] {
//		req.ChargeTo = appendUserCharge(req.ChargeTo, li.instanceOpts.CertificateQuotaUser(cert))
//	}
//
// Graded over the log's HTTP surface with the candidate's own ct_server, and
// read back off this group's own pinned backend (SCOPE-BACKEND). Every
// expected list below is exact and complete, in order: the remote user first,
// then the chain's CAs in chain order. Exactness is the point -- it is what
// distinguishes "charges the intermediates" from "charges the leaf too", from
// "charges a list that grows across requests", and from "charges only the
// first CA".
//
// The trust pool this step's kit configures holds both bench-root and
// bench-intermediate, so a two-element submission verifies to
// [leaf, intermediate] and charges one CA, while the same submission with the
// root appended verifies to [leaf, intermediate, root] and charges two. Both
// are pinned, so the identity of the certificates actually charged is nailed
// down rather than just their number.
//
// SCOPE-TIME: no clock. SCOPE-NET: loopback only, port from binding :0.
//
// At v1.0.17 ctfe.QuotaUserForCert does not exist and nothing populates
// ChargeTo, so this driver does not even compile there.
package main

import (
	"encoding/json"
	"fmt"
	"strings"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/trillian/ctfe"
	"github.com/google/certificate-transparency-go/x509"

	"ctverify/vhelp"
)

const queueMethod = "QueueLeaves"

// The two pinned identities, spelled out rather than computed, so that a
// candidate whose QuotaUserForCert is wrong cannot agree with itself.
const (
	benchInterUser = "@intermediate CN=Harbor CT Bench Intermediate,O=Harbor CT Bench 2f5b59344c"
	benchRootUser  = "@intermediate CN=Harbor CT Bench Root,O=Harbor CT Bench ecc408ce9d"
	benchLeafUser  = "@intermediate CN=valid.bench.invalid,O=Harbor CT Bench bc6811c45b"
)

func chainJSON(t *vhelp.T, rel ...[]string) []byte {
	chain := make([][]byte, 0, len(rel))
	for _, r := range rel {
		chain = append(chain, vhelp.ReadFixture(r...))
	}
	b, err := json.Marshal(ct.AddChainRequest{Chain: chain})
	if err != nil {
		t.Fatalf("marshal add-chain request: %v", err)
	}
	return b
}

func certOf(t *vhelp.T, rel ...string) *x509.Certificate {
	cert, err := x509.ParseCertificate(vhelp.ReadFixture(rel...))
	if err != nil {
		if _, ok := err.(x509.NonFatalErrors); !ok {
			t.Fatalf("parse %v: %v", rel, err)
		}
	}
	if cert == nil {
		t.Fatalf("parse %v: no certificate", rel)
	}
	return cert
}

func render(status int, users []string) string {
	return fmt.Sprintf("status=%d charge=[%s]", status, strings.Join(users, " | "))
}

func main() {
	t := vhelp.New()

	h := startPersonality(t, nil, "")
	defer h.stop()

	twoElem := chainJSON(t, []string{"minted", "leaf-valid.der"}, []string{"minted", "bench-intermediate.der"})
	threeElem := chainJSON(t,
		[]string{"minted", "leaf-valid.der"},
		[]string{"minted", "bench-intermediate.der"},
		[]string{"minted", "bench-root.der"})
	precert := chainJSON(t, []string{"minted", "precert-poisoned.der"}, []string{"minted", "bench-intermediate.der"})

	// --- 1. leaf + intermediate: the intermediate alone is charged ----------
	res := h.postJSONFrom(t, h.logURL("/ct/v1/add-chain"), "203.0.113.9", twoElem)
	t.Equal("two-element-submission-charges-only-its-intermediate",
		render(res.status, h.backend.lastCharge(queueMethod)),
		render(200, []string{"203.0.113.9", benchInterUser}))

	// --- 2. leaf + intermediate + root: both CAs are charged ----------------
	res = h.postJSONFrom(t, h.logURL("/ct/v1/add-chain"), "203.0.113.9", threeElem)
	t.Equal("submitted-root-is-charged-as-an-intermediate",
		render(res.status, h.backend.lastCharge(queueMethod)),
		render(200, []string{"203.0.113.9", benchInterUser, benchRootUser}))

	// --- 3. and those strings really are QuotaUserForCert's ------------------
	// Independent of the two constants above: recomputed here from the same
	// fixture certificates through the exported entry point ct_server itself
	// installs.
	charged := h.backend.lastCharge(queueMethod)
	if len(charged) > 0 {
		charged = charged[1:] // drop the remote user; the sibling group owns it
	}
	t.Equal("charged-intermediates-match-quota-user-for-cert",
		strings.Join(charged, " | "),
		strings.Join([]string{
			ctfe.QuotaUserForCert(certOf(t, "minted", "bench-intermediate.der")),
			ctfe.QuotaUserForCert(certOf(t, "minted", "bench-root.der")),
		}, " | "))

	// --- 4. the leaf is never charged as a CA -------------------------------
	// chain[1:], not chain: an end-entity certificate is not an issuer and
	// must not get its own quota bucket.
	var leafCharged bool
	for _, c := range h.backend.chargesFor(queueMethod) {
		for _, u := range c {
			if u == benchLeafUser {
				leafCharged = true
			}
		}
	}
	t.Check("submitted-leaf-is-never-charged-as-an-issuer", !leafCharged,
		"the leaf's quota user %q appeared in a charge", benchLeafUser)

	// --- 5. add-pre-chain charges its issuer too ----------------------------
	res = h.postJSONFrom(t, h.logURL("/ct/v1/add-pre-chain"), "192.0.2.77", precert)
	t.Equal("add-pre-chain-charges-its-intermediate",
		render(res.status, h.backend.lastCharge(queueMethod)),
		render(200, []string{"192.0.2.77", benchInterUser}))

	// --- 6. every submission gets a fresh list ------------------------------
	// Repeating the first submission after three others must produce exactly
	// the first result again; a ChargeTo shared between requests would by now
	// have accumulated six users.
	res = h.postJSONFrom(t, h.logURL("/ct/v1/add-chain"), "203.0.113.9", twoElem)
	t.Equal("repeat-submission-charges-a-fresh-list",
		render(res.status, h.backend.lastCharge(queueMethod)),
		render(200, []string{"203.0.113.9", benchInterUser}))

	t.Done()
}
