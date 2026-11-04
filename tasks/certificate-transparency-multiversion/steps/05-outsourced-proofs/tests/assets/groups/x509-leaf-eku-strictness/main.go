// Group x509-leaf-eku-strictness.
//
// The same commit that made the verification checks switchable (41939987)
// also splits ekuPermittedBy into two contexts. The lenient exceptions --
// "ServerAuth in a CA permits ClientAuth in the leaf", "any CA may issue an
// OCSP responder certificate", the two Microsoft code-signing mappings --
// were written for issuer certificates, and v1.0.13 applied them to the
// leaf's own EKU list as well. v1.0.17 keeps them for issuers and drops them
// for the leaf: a requested usage now has to be present in the leaf itself
// (or be covered by ExtKeyUsageAny).
//
// No new identifier is involved, so this driver compiles against both
// milestones and the two answer differently: at v1.0.13 the first two cases
// verify successfully, at v1.0.17 they are refused. The remaining cases are
// controls that must hold at both, so the group cannot be passed by an
// implementation that simply refuses everything.
//
// SCOPE-TIME: opts.CurrentTime is a pinned constant.
package main

import (
	"time"

	"github.com/google/certificate-transparency-go/x509"

	"ctverify/vhelp"
)

var pinnedNow = time.Date(2030, 1, 1, 0, 0, 0, 0, time.UTC)

func mustParse(t *vhelp.T, rel ...string) *x509.Certificate {
	cert, err := x509.ParseCertificate(vhelp.ReadFixture(rel...))
	if err != nil {
		t.Fatalf("parse %v: %v", rel, err)
	}
	return cert
}

func main() {
	t := vhelp.New()

	root := mustParse(t, "minted", "bench-root.der")
	inter := mustParse(t, "minted", "bench-intermediate.der")
	serverOnly := mustParse(t, "minted", "leaf-eku-serverauth.der")
	clientOnly := mustParse(t, "minted", "leaf-eku-clientauth.der")
	noEKU := mustParse(t, "minted", "leaf-eku-none.der")

	roots := x509.NewCertPool()
	roots.AddCert(root)
	inters := x509.NewCertPool()
	inters.AddCert(inter)

	verify := func(leaf *x509.Certificate, want ...x509.ExtKeyUsage) ([][]*x509.Certificate, error) {
		return leaf.Verify(x509.VerifyOptions{
			Roots:         roots,
			Intermediates: inters,
			CurrentTime:   pinnedNow,
			KeyUsages:     want,
		})
	}

	// --- 1. the CA-side "ServerAuth permits ClientAuth" exception is gone --
	if _, err := verify(serverOnly, x509.ExtKeyUsageClientAuth); err == nil {
		t.Check("serverauth-leaf-refused-for-clientauth-request", false,
			"Verify accepted a leaf whose only EKU is ServerAuth for a ClientAuth request")
	} else {
		t.Check("serverauth-leaf-refused-for-clientauth-request", true, "")
	}

	// --- 2. so is the "any CA may issue an OCSP responder" exception -------
	if _, err := verify(serverOnly, x509.ExtKeyUsageOCSPSigning); err == nil {
		t.Check("serverauth-leaf-refused-for-ocsp-signing-request", false,
			"Verify accepted a leaf whose only EKU is ServerAuth for an OCSPSigning request")
	} else {
		t.Check("serverauth-leaf-refused-for-ocsp-signing-request", true, "")
	}

	// --- 3. control: the leaf's own usage is still accepted ----------------
	chains, err := verify(serverOnly, x509.ExtKeyUsageServerAuth)
	switch {
	case err != nil:
		t.Check("serverauth-leaf-accepted-for-serverauth-request", false, "Verify: %v", err)
	case len(chains) != 1:
		t.Check("serverauth-leaf-accepted-for-serverauth-request", false, "got %d chains, want 1", len(chains))
	default:
		t.Equal("serverauth-leaf-accepted-for-serverauth-request", len(chains[0]), 3)
	}

	// --- 4. control: and so is the mirror-image leaf -----------------------
	chains, err = verify(clientOnly, x509.ExtKeyUsageClientAuth)
	switch {
	case err != nil:
		t.Check("clientauth-leaf-accepted-for-clientauth-request", false, "Verify: %v", err)
	case len(chains) != 1:
		t.Check("clientauth-leaf-accepted-for-clientauth-request", false, "got %d chains, want 1", len(chains))
	default:
		t.Equal("clientauth-leaf-accepted-for-clientauth-request", len(chains[0]), 3)
	}

	// --- 5. control: a leaf with no EKU extension is unconstrained ---------
	chains, err = verify(noEKU, x509.ExtKeyUsageClientAuth)
	switch {
	case err != nil:
		t.Check("eku-free-leaf-accepted-for-any-request", false, "Verify: %v", err)
	case len(chains) != 1:
		t.Check("eku-free-leaf-accepted-for-any-request", false, "got %d chains, want 1", len(chains))
	default:
		t.Equal("eku-free-leaf-accepted-for-any-request", len(chains[0]), 3)
	}

	t.Done()
}
