// Group ctfe-quota-user-for-cert.
//
// trillian/ctfe/cert_quota.go is one of the eight files v1.0.19 adds. It is
// the whole of the "quota user derived from a certificate" half of this
// milestone, and it is a pure function over a certificate, so it is graded
// from a driver that calls it -- no server, no backend, no clock.
//
// The format is fixed by upstream:
//
//	"<CertificateQuotaUserPrefix> <Subject.String()> <hex(SHA256(SPKI)[0:5])>"
//
// Every expected string below is pinned: it is the value the Oracle tree
// produces for a fixture certificate in the shared corpus, and the last case
// recomputes the digest half independently from the certificate's own
// RawSubjectPublicKeyInfo so the group is not merely asserting that the
// implementation agrees with itself.
//
// SCOPE-NET / SCOPE-TIME: nothing listens, nothing dials, no clock is read.
//
// ctfe.QuotaUserForCert and ctfe.CertificateQuotaUserPrefix do not exist at
// v1.0.17, so this driver does not compile against the previous tree.
package main

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/pem"
	"fmt"

	"github.com/google/certificate-transparency-go/trillian/ctfe"
	"github.com/google/certificate-transparency-go/x509"

	"ctverify/vhelp"
)

// certOf parses a fixture that may be either DER or PEM. NonFatalErrors is
// the forked parser's way of saying "parsed, with reservations"; the
// certificate is still usable and upstream treats it that way everywhere.
func certOf(t *vhelp.T, rel ...string) *x509.Certificate {
	b := vhelp.ReadFixture(rel...)
	if blk, _ := pem.Decode(b); blk != nil {
		b = blk.Bytes
	}
	cert, err := x509.ParseCertificate(b)
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

func main() {
	t := vhelp.New()

	// --- 1. the prefix is the exact constant upstream defines --------------
	t.Equal("certificate-quota-user-prefix", ctfe.CertificateQuotaUserPrefix, "@intermediate")

	// --- 2..6. the derived identity, per pinned certificate ----------------
	// Minted corpus certificates first: a root, an intermediate and a leaf,
	// so the function is seen not to care what kind of certificate it is
	// handed (upstream: "the function does not enforce anything about the
	// passed in cert").
	for _, tc := range []struct {
		id   string
		rel  []string
		want string
	}{
		{
			id:   "quota-user-for-bench-root",
			rel:  []string{"minted", "bench-root.der"},
			want: "@intermediate CN=Harbor CT Bench Root,O=Harbor CT Bench ecc408ce9d",
		},
		{
			id:   "quota-user-for-bench-intermediate",
			rel:  []string{"minted", "bench-intermediate.der"},
			want: "@intermediate CN=Harbor CT Bench Intermediate,O=Harbor CT Bench 2f5b59344c",
		},
		{
			id:   "quota-user-for-leaf-certificate",
			rel:  []string{"minted", "leaf-valid.der"},
			want: "@intermediate CN=valid.bench.invalid,O=Harbor CT Bench bc6811c45b",
		},
		// Two upstream testdata certificates, whose subjects carry more than
		// one RDN, so the Subject.String() half of the format is exercised
		// beyond a bare CN+O.
		{
			id:   "quota-user-for-upstream-fake-ca",
			rel:  []string{"keys", "fake-ca.cert"},
			want: "@intermediate CN=FakeCertificateAuthority,OU=Eng,O=Google,L=London,ST=London,C=GB 098743cc7b",
		},
		{
			id:   "quota-user-for-upstream-int-ca",
			rel:  []string{"keys", "int-ca.cert"},
			want: "@intermediate CN=FakeIntermediateAuthority,OU=Eng,O=Google,L=London,ST=London,C=GB b0fb895781",
		},
	} {
		t.Equal(tc.id, ctfe.QuotaUserForCert(certOf(t, tc.rel...)), tc.want)
	}

	// --- 7. the suffix really is the SPKI digest, recomputed here ----------
	// Independent of the pinned strings above: a fourth certificate, whose
	// expected value is built from its own RawSubjectPublicKeyInfo rather
	// than from a constant in this file.
	rogue := certOf(t, "minted", "rogue-root.der")
	spki := sha256.Sum256(rogue.RawSubjectPublicKeyInfo)
	want := fmt.Sprintf("%s %s %s", "@intermediate", rogue.Subject.String(), hex.EncodeToString(spki[0:5]))
	t.Equal("quota-user-suffix-is-truncated-spki-digest", ctfe.QuotaUserForCert(rogue), want)

	t.Done()
}
