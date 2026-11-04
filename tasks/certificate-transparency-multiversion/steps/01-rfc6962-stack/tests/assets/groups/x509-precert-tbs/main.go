// Group x509-precert-tbs.
//
// RFC 6962 s3.1/s3.2: a precertificate carries a critical poison extension
// (1.3.6.1.4.1.11129.2.4.3) which the log removes before hashing, and when the
// precertificate was signed by a special Precertificate Signing Certificate
// the issuance information is rewritten to point at the next issuer in the
// chain. This group grades x509.RemoveCTPoison and x509.BuildPrecertTBS.
//
// The exact answer for the poison removal comes from the pinned corpus: the
// minted precert-poisoned and precert-nopoison certificates are the same
// certificate with and without the poison extension, so the defanged TBS must
// equal the other file's TBS byte for byte.
//
// preissuer.der is shipped beside this group: a pinned CA certificate carrying
// the CertificateTransparency extended key usage, which the corpus lacks.
//
// Boundary: exported functions of the forked x509 package. No error strings or
// error types are graded.
package main

import (
	"errors"
	"os"

	"github.com/google/certificate-transparency-go/x509"

	"ctverify/vhelp"
)

var oidPoison = []int{1, 3, 6, 1, 4, 1, 11129, 2, 4, 3}
var oidAuthorityKeyID = []int{2, 5, 29, 35}

func parse(t *vhelp.T, der []byte, what string) *x509.Certificate {
	c, err := x509.ParseCertificate(der)
	if c == nil {
		t.Fatalf("%s did not parse: %v", what, err)
	}
	return c
}

func extOIDs(c *x509.Certificate) string {
	s := ""
	for _, e := range c.Extensions {
		s += e.Id.String() + " "
	}
	return s
}

func extValue(c *x509.Certificate, oid []int) []byte {
	for _, e := range c.Extensions {
		if e.Id.Equal(oid) {
			return e.Value
		}
	}
	return nil
}

func nonNil(err error, why string) error {
	if err == nil {
		return errors.New(why)
	}
	return nil
}

func main() {
	t := vhelp.New()

	poisoned := parse(t, vhelp.ReadFixture("minted", "precert-poisoned.der"), "precert-poisoned.der")
	clean := parse(t, vhelp.ReadFixture("minted", "precert-nopoison.der"), "precert-nopoison.der")
	if extValue(poisoned, oidPoison) == nil {
		t.Fatalf("corpus fault: precert-poisoned.der carries no poison extension")
	}

	defanged, err := x509.RemoveCTPoison(poisoned.RawTBSCertificate)
	if err != nil {
		t.Fatalf("RemoveCTPoison: %v", err)
	}
	t.EqualBytes("removectpoison-exact-match-nopoison", defanged, clean.RawTBSCertificate)

	reparsed, err := x509.ParseTBSCertificate(defanged)
	if reparsed == nil {
		t.Fatalf("ParseTBSCertificate(defanged): %v", err)
	}
	t.Equal("removectpoison-extension-order-preserved", extOIDs(reparsed), extOIDs(clean))

	_, err = x509.RemoveCTPoison(clean.RawTBSCertificate)
	t.Case("removectpoison-no-poison-rejected", nonNil(err, "a TBS with no poison extension was accepted"))

	_, err = x509.RemoveCTPoison(append(append([]byte{}, poisoned.RawTBSCertificate...), 0x00))
	t.Case("removectpoison-trailing-data-rejected", nonNil(err, "trailing data after the TBSCertificate was accepted"))

	_, err = x509.RemoveCTPoison([]byte{0x30, 0x03, 0x02, 0x01, 0x01})
	t.Case("removectpoison-garbage-rejected", nonNil(err, "a non-TBSCertificate SEQUENCE was accepted"))

	viaBuild, err := x509.BuildPrecertTBS(poisoned.RawTBSCertificate, nil)
	if err != nil {
		t.Fatalf("BuildPrecertTBS(nil): %v", err)
	}
	t.EqualBytes("buildprecerttbs-nil-preissuer-equals-removectpoison", viaBuild, defanged)

	// --- the Precertificate Signing Certificate path -----------------------
	piDER, err := os.ReadFile(vhelp.GroupFile("preissuer.der"))
	if err != nil {
		t.Fatalf("preissuer.der: %v", err)
	}
	preIssuer := parse(t, piDER, "preissuer.der")
	hasCTEKU := false
	for _, eku := range preIssuer.ExtKeyUsage {
		if eku == x509.ExtKeyUsageCertificateTransparency {
			hasCTEKU = true
		}
	}
	t.Check("preissuer-fixture-has-ct-eku", hasCTEKU, "ExtKeyUsage=%v", preIssuer.ExtKeyUsage)

	rewritten, err := x509.BuildPrecertTBS(poisoned.RawTBSCertificate, preIssuer)
	if err != nil {
		t.Fatalf("BuildPrecertTBS(preIssuer): %v", err)
	}
	out, err := x509.ParseTBSCertificate(rewritten)
	if out == nil {
		t.Fatalf("ParseTBSCertificate(rewritten): %v", err)
	}
	t.EqualBytes("buildprecerttbs-preissuer-rewrites-issuer", out.RawIssuer, preIssuer.RawIssuer)
	t.EqualBytes("buildprecerttbs-preissuer-rewrites-authority-key-id",
		extValue(out, oidAuthorityKeyID), extValue(preIssuer, oidAuthorityKeyID))
	t.Check("buildprecerttbs-preissuer-preserves-subject-serial-validity",
		string(out.RawSubject) == string(clean.RawSubject) &&
			out.SerialNumber.Cmp(clean.SerialNumber) == 0 &&
			out.NotBefore.Equal(clean.NotBefore) && out.NotAfter.Equal(clean.NotAfter) &&
			extValue(out, oidPoison) == nil,
		"subject/serial/validity or poison changed: serial=%v notBefore=%v notAfter=%v poison=%v",
		out.SerialNumber, out.NotBefore.UTC(), out.NotAfter.UTC(), extValue(out, oidPoison) != nil)

	// Each refusal below removes exactly one of the two properties and leaves
	// the other in place, so whichever order an implementation checks them in,
	// only the named one can be the reason.

	// bench-intermediate.der carries an authority-key-id and no extended key
	// usage extension at all: the CT EKU is the only thing missing.
	noEKU := parse(t, vhelp.ReadFixture("minted", "bench-intermediate.der"), "bench-intermediate.der")
	_, err = x509.BuildPrecertTBS(poisoned.RawTBSCertificate, noEKU)
	t.Case("buildprecerttbs-preissuer-without-ct-eku-rejected",
		nonNil(err, "a pre-issuer without the CertificateTransparency EKU was accepted"))

	// The pre-issuer above, with its authority-key-id taken away and its CT EKU
	// left alone. Both the raw extension and the parsed field go, so an issuer
	// that reads either one finds nothing.
	noAKID := parse(t, piDER, "preissuer.der")
	kept := noAKID.Extensions[:0]
	for _, ext := range noAKID.Extensions {
		if !ext.Id.Equal(oidAuthorityKeyID) {
			kept = append(kept, ext)
		}
	}
	if len(kept) == len(noAKID.Extensions) {
		t.Fatalf("preissuer.der has no authority-key-id extension to remove")
	}
	noAKID.Extensions = kept
	noAKID.AuthorityKeyId = nil
	_, err = x509.BuildPrecertTBS(poisoned.RawTBSCertificate, noAKID)
	t.Case("buildprecerttbs-preissuer-without-authority-key-id-rejected",
		nonNil(err, "a pre-issuer with no authority-key-id extension was accepted"))

	t.Done()
}
