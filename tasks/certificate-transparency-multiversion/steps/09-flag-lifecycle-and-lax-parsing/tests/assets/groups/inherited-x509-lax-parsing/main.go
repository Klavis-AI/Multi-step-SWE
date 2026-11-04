// Group inherited-x509-lax-parsing.
//
// The lax-parsing contract earlier milestones built into the forked ./x509:
// a certificate that violates DER or RFC 5280 in a way a log must still be
// able to record parses anyway, returns a non-fatal error, and yields a
// certificate whose other fields are fully populated. x509.IsFatal is the
// exported boundary that separates "malformed, but logged" from "not a
// certificate".
//
// Every fixture below is taken from the shared malformed corpus and every
// assertion is about exported fields, never about an error string: the
// classification is IsFatal, and the evidence that parsing really continued is
// the exact serial number, key parameters, subject and EKU list recovered from
// the certificate. Measured identical at v1.1.0 and v1.1.2, which is what
// makes this an inherited group; v1.1.2's own parser changes are graded by
// asn1-bmpstring, x509-ed25519 and x509-go114-merges.
package main

import (
	"crypto/ecdsa"
	"crypto/rsa"
	"encoding/pem"
	"fmt"
	"strings"

	"github.com/google/certificate-transparency-go/x509"

	"ctverify/vhelp"
)

// parseMalformed returns the certificate and whether the error was fatal.
func parseMalformed(t *vhelp.T, name string) (*x509.Certificate, bool) {
	raw := vhelp.ReadFixture("malformed", name)
	blk, _ := pem.Decode(raw)
	if blk == nil {
		t.Fatalf("fixture malformed/%s is not PEM", name)
	}
	c, err := x509.ParseCertificate(blk.Bytes)
	return c, x509.IsFatal(err)
}

// derOf returns the DER body of a malformed fixture.
func derOf(t *vhelp.T, name string) []byte {
	blk, _ := pem.Decode(vhelp.ReadFixture("malformed", name))
	if blk == nil {
		t.Fatalf("fixture malformed/%s is not PEM", name)
	}
	return blk.Bytes
}

// nonFatal asserts the shape every case below shares, then hands the
// certificate back for its own field assertions.
func nonFatal(t *vhelp.T, id, name string) *x509.Certificate {
	c, fatal := parseMalformed(t, name)
	if fatal || c == nil {
		t.Check(id, false, "%s: fatal=%v cert==nil=%v, want a non-fatally parsed certificate", name, fatal, c == nil)
		return nil
	}
	return c
}

func ekuNames(c *x509.Certificate) string {
	parts := make([]string, 0, len(c.ExtKeyUsage))
	for _, e := range c.ExtKeyUsage {
		parts = append(parts, fmt.Sprintf("%d", int(e)))
	}
	return strings.Join(parts, ",")
}

func main() {
	t := vhelp.New()

	// 1. A SerialNumber encoded with a redundant leading zero byte is not DER,
	// but the certificate is recorded and the integer recovered exactly.
	if c := nonFatal(t, "nonminimal-serial-parses", "xf-der-invalid-nonminimal-int.pem"); c != nil {
		t.Equal("nonminimal-serial-parses",
			c.SerialNumber.String()+"/"+c.PublicKeyAlgorithm.String()+"/"+c.Subject.CommonName,
			"4660/ECDSA/*.google.com")
	}

	// 2. The same violation inside the RSA public key: the exponent and
	// modulus are still both recovered.
	if c := nonFatal(t, "nonminimal-rsa-exponent-parses", "xf-der-pubkey-rsa-nonminimal-int.pem"); c != nil {
		k, ok := c.PublicKey.(*rsa.PublicKey)
		if !ok {
			t.Check("nonminimal-rsa-exponent-parses", false, "public key is %T, want *rsa.PublicKey", c.PublicKey)
		} else {
			t.Equal("nonminimal-rsa-exponent-parses",
				fmt.Sprintf("%d/%d/%s", k.E, k.N.BitLen(), c.SerialNumber.String()),
				"65537/2048/4660")
		}
	}

	// 3. A negative RSA modulus is meaningless, and is still parsed to a
	// known bit length rather than discarded.
	if c := nonFatal(t, "negative-rsa-modulus-parses", "xf-pubkey-rsa-modulus-negative.pem"); c != nil {
		k, ok := c.PublicKey.(*rsa.PublicKey)
		if !ok {
			t.Check("negative-rsa-modulus-parses", false, "public key is %T, want *rsa.PublicKey", c.PublicKey)
		} else {
			t.Equal("negative-rsa-modulus-parses",
				fmt.Sprintf("%d/%d/%s", k.E, k.N.BitLen(), c.SerialNumber.String()),
				"65537/2055/129201852361993560")
		}
	}

	// 4. An RSA AlgorithmIdentifier missing its required NULL parameters.
	if c := nonFatal(t, "rsa-missing-null-parameters-parses", "xf-pubkey-rsa-param-nonnull.pem"); c != nil {
		k, ok := c.PublicKey.(*rsa.PublicKey)
		if !ok {
			t.Check("rsa-missing-null-parameters-parses", false, "public key is %T, want *rsa.PublicKey", c.PublicKey)
		} else {
			t.Equal("rsa-missing-null-parameters-parses",
				fmt.Sprintf("%s/%d", c.PublicKeyAlgorithm.String(), k.E), "RSA/65537")
		}
	}

	// 5. An EKU extension present but empty leaves an empty EKU list; the
	// extension itself is still reported in the raw extension list.
	if c := nonFatal(t, "empty-eku-extension-parses", "xf-ext-extended-key-usage-empty.pem"); c != nil {
		t.Equal("empty-eku-extension-parses",
			fmt.Sprintf("%d/%d/%s", len(c.ExtKeyUsage), len(c.Extensions), c.SerialNumber.String()),
			"0/2/96096843357863639")
	}

	// 6. An EKU extension containing a zero-length OID alongside two real
	// ones keeps both real usages: ServerAuth(1) then ClientAuth(2).
	if c := nonFatal(t, "zero-length-eku-oid-keeps-real-usages", "xf-ext-extended-key-usage-empty-oid.pem"); c != nil {
		t.Equal("zero-length-eku-oid-keeps-real-usages",
			ekuNames(c)+"/"+c.SerialNumber.String(),
			fmt.Sprintf("%d,%d/140298796799689226", int(x509.ExtKeyUsageServerAuth), int(x509.ExtKeyUsageClientAuth)))
	}

	// 7. A key on a curve too small to be secure is recorded, with the curve
	// identified, rather than rejected.
	if c := nonFatal(t, "insecure-curve-parses", "xf-pubkey-ecdsa-secp192r1.pem"); c != nil {
		k, ok := c.PublicKey.(*ecdsa.PublicKey)
		if !ok || k.Curve == nil {
			t.Check("insecure-curve-parses", false, "public key is %T with usable curve %v", c.PublicKey, ok && k.Curve != nil)
		} else {
			t.Equal("insecure-curve-parses",
				k.Curve.Params().Name+"/"+c.SerialNumber.String(),
				"P-192/141222174073305197")
		}
	}

	// 8. A PrintableString holding a character PrintableString forbids: the
	// subject is still decoded, exactly as the bytes say.
	if c := nonFatal(t, "nonprintable-subject-parses", "xf-subject-nonprintable.pem"); c != nil {
		t.Equal("nonprintable-subject-parses",
			c.Subject.CommonName+"/"+strings.Join(c.Subject.Organization, "+"),
			"*.google.com/Google Inc")
	}

	// 9. An empty SubjectInfoAccess extension yields no access descriptions.
	if c := nonFatal(t, "empty-subject-info-access-parses", "xf-ext-subject-info-empty.pem"); c != nil {
		t.Equal("empty-subject-info-access-parses",
			fmt.Sprintf("%d/%d/%s", len(c.OCSPServer), len(c.IssuingCertificateURL), c.SerialNumber.String()),
			"0/0/143775503077481624")
	}

	// 10. A certificate whose outer signature algorithm disagrees with the one
	// inside the TBS is a policy problem, not a parse problem: no error at
	// all, and the TBS (inner) algorithm is what is reported -- the outer copy
	// is not exposed on the Certificate at all (X509-FORK.md 5.4). Tagged by
	// an earlier version of this comment as the outer one, which is the
	// opposite of both the fixture and the reference.
	c, fatal := parseMalformed(t, "mismatching-sig-alg.pem")
	if fatal || c == nil {
		t.Check("mismatching-signature-algorithm-parses", false, "fatal=%v cert==nil=%v", fatal, c == nil)
	} else {
		t.Equal("mismatching-signature-algorithm-parses",
			c.SignatureAlgorithm.String()+"/"+c.PublicKeyAlgorithm.String(), "ECDSA-SHA256/ECDSA")
	}

	// 11. Laxness has a floor. Truncated DER is not a certificate, and the
	// error is fatal with nothing returned.
	full := derOf(t, "xf-subject-nonprintable.pem")
	trunc, err := x509.ParseCertificate(full[:len(full)/2])
	t.Check("truncated-der-is-fatal", x509.IsFatal(err) && trunc == nil,
		"fatal=%v cert==nil=%v", x509.IsFatal(err), trunc == nil)

	t.Done()
}
