// Group inherited-x509-nonfatal-errors.
//
// The forked ./x509 parser's oldest contract, and the one v1.2.0's new
// issuance-chain storage sits directly on top of: a certificate a log must be
// able to record parses anyway, returns an error the caller is expected to
// carry rather than act on, and still yields a fully populated certificate.
// x509.IsFatal is the exported boundary between "malformed, but logged" and
// "not a certificate", and a log that stores its issuance chains out of line
// still has to make exactly the same call about what is a chain.
//
// Laxness has a floor, and the floor is asserted here too: truncated DER and
// trailing data after a certificate are fatal and yield nothing. No error
// string and no error type is graded -- the verdict is always IsFatal, and the
// evidence that parsing really continued is exact field values recovered from
// the certificate.
package main

import (
	"crypto/rsa"
	"encoding/pem"
	"fmt"
	"strings"

	"github.com/google/certificate-transparency-go/x509"

	"ctverify/vhelp"
)

// poisonOID is RFC 6962 s3.1's critical poison extension.
const poisonOID = "1.3.6.1.4.1.11129.2.4.3"

func derOf(t *vhelp.T, dir, name string) []byte {
	blk, _ := pem.Decode(vhelp.ReadFixture(dir, name))
	if blk == nil {
		t.Fatalf("fixture %s/%s is not PEM", dir, name)
	}
	return blk.Bytes
}

func oidList(c *x509.Certificate) string {
	parts := make([]string, 0, len(c.Extensions))
	for _, e := range c.Extensions {
		parts = append(parts, e.Id.String())
	}
	return strings.Join(parts, ",")
}

func main() {
	t := vhelp.New()

	// ---- 1. The boundary function's base case. -------------------------
	t.Check("is-fatal-of-nil-is-false", !x509.IsFatal(nil),
		"IsFatal(nil) reported a fatal error")

	// ---- 2. A well-formed certificate raises nothing at all. -----------
	goodDER := vhelp.ReadFixture("minted", "leaf-valid.der")
	good, err := x509.ParseCertificate(goodDER)
	t.Check("well-formed-certificate-has-no-error",
		err == nil && good != nil && good.SerialNumber != nil && good.SerialNumber.String() == "4097",
		"ParseCertificate(leaf-valid): err=%v cert==nil=%v", err, good == nil)

	// ---- 3/4. A DER violation is non-fatal, and parsing continues. -----
	badSerial := derOf(t, "malformed", "xf-der-invalid-nonminimal-int.pem")
	c, err := x509.ParseCertificate(badSerial)
	t.Check("nonfatal-parse-returns-certificate-and-error",
		err != nil && !x509.IsFatal(err) && c != nil,
		"err=%v fatal=%v cert==nil=%v; want an error that is not fatal plus a certificate",
		err, x509.IsFatal(err), c == nil)
	if c != nil {
		t.Equal("nonfatal-parse-populates-the-certificate",
			fmt.Sprintf("%s/%s/%s", c.SerialNumber.String(), c.PublicKeyAlgorithm.String(), c.Subject.CommonName),
			"4660/ECDSA/*.google.com")
	} else {
		t.Check("nonfatal-parse-populates-the-certificate", false, "no certificate to inspect")
	}

	// ---- 5. A second, unrelated violation classifies the same way. -----
	nullParams := derOf(t, "malformed", "xf-pubkey-rsa-param-nonnull.pem")
	c2, err2 := x509.ParseCertificate(nullParams)
	ok := err2 != nil && !x509.IsFatal(err2) && c2 != nil
	if ok {
		k, isRSA := c2.PublicKey.(*rsa.PublicKey)
		ok = isRSA && k.E == 65537
	}
	t.Check("second-nonfatal-sample-classifies-the-same-way", ok,
		"err=%v fatal=%v cert==nil=%v", err2, x509.IsFatal(err2), c2 == nil)

	// ---- 6. Truncated DER is fatal and yields nothing. -----------------
	trunc, errTrunc := x509.ParseCertificate(goodDER[:len(goodDER)/2])
	t.Check("truncated-der-is-fatal", x509.IsFatal(errTrunc) && trunc == nil,
		"fatal=%v cert==nil=%v", x509.IsFatal(errTrunc), trunc == nil)

	// ---- 7. So is a certificate with anything after it. ----------------
	withTail := append(append([]byte{}, goodDER...), 0x00, 0x00)
	tailCert, errTail := x509.ParseCertificate(withTail)
	t.Check("trailing-data-after-certificate-is-fatal",
		x509.IsFatal(errTail) && tailCert == nil,
		"fatal=%v cert==nil=%v", x509.IsFatal(errTail), tailCert == nil)

	// ---- 8. A concatenation is read in order. --------------------------
	var concat []byte
	for _, f := range []string{"leaf-valid.der", "bench-intermediate.der", "bench-root.der"} {
		concat = append(concat, vhelp.ReadFixture("minted", f)...)
	}
	certs, errConcat := x509.ParseCertificates(concat)
	serials := make([]string, 0, len(certs))
	for _, cc := range certs {
		serials = append(serials, cc.SerialNumber.String())
	}
	t.Check("parse-certificates-reads-a-concatenation",
		errConcat == nil && strings.Join(serials, ",") == "4097,2,1",
		"err=%v serials=%q", errConcat, strings.Join(serials, ","))

	// ---- 9. A pool keeps what is only non-fatally wrong, and nothing
	//         that is not a certificate at all. -------------------------
	pool := x509.NewCertPool()
	keptBad := pool.AppendCertsFromPEM(vhelp.ReadFixture("malformed", "xf-der-invalid-nonminimal-int.pem"))
	junk := x509.NewCertPool()
	keptJunk := junk.AppendCertsFromPEM([]byte("-----BEGIN CERTIFICATE-----\nAAAA\n-----END CERTIFICATE-----\n"))
	t.Check("cert-pool-keeps-only-nonfatally-invalid-certificates",
		keptBad && !keptJunk,
		"pool accepted the non-fatal sample=%v, accepted junk=%v", keptBad, keptJunk)

	// ---- 10. The TBS-only entry point agrees with the full one. --------
	if good != nil {
		tbs, errTBS := x509.ParseTBSCertificate(good.RawTBSCertificate)
		agree := errTBS == nil && tbs != nil && tbs.SerialNumber != nil &&
			tbs.SerialNumber.Cmp(good.SerialNumber) == 0 &&
			tbs.Subject.CommonName == good.Subject.CommonName &&
			oidList(tbs) == oidList(good)
		t.Check("parse-tbs-certificate-matches-the-parsed-certificate", agree,
			"err=%v tbs==nil=%v", errTBS, tbs == nil)
	} else {
		t.Check("parse-tbs-certificate-matches-the-parsed-certificate", false, "no certificate to compare")
	}

	// ---- 11. Poison removal takes out exactly one extension. -----------
	poisoned, errP := x509.ParseCertificate(vhelp.ReadFixture("minted", "precert-poisoned.der"))
	if errP != nil && x509.IsFatal(errP) {
		t.Fatalf("ParseCertificate(precert-poisoned): %v", errP)
	}
	before := oidList(poisoned)
	defanged, errRemove := x509.RemoveCTPoison(poisoned.RawTBSCertificate)
	if errRemove != nil {
		t.Check("remove-ct-poison-drops-exactly-the-poison-extension", false,
			"RemoveCTPoison: %v", errRemove)
	} else {
		after, errParse := x509.ParseTBSCertificate(defanged)
		wantAfter := []string{}
		for _, o := range strings.Split(before, ",") {
			if o != poisonOID {
				wantAfter = append(wantAfter, o)
			}
		}
		got := ""
		if after != nil {
			got = oidList(after)
		}
		t.Check("remove-ct-poison-drops-exactly-the-poison-extension",
			errParse == nil && after != nil && strings.Contains(before, poisonOID) &&
				got == strings.Join(wantAfter, ","),
			"err=%v before=%q after=%q", errParse, before, got)
	}

	// ---- 12. ... and refuses to run when there is no poison. -----------
	clean, errC := x509.ParseCertificate(vhelp.ReadFixture("minted", "precert-nopoison.der"))
	if errC != nil && x509.IsFatal(errC) {
		t.Fatalf("ParseCertificate(precert-nopoison): %v", errC)
	}
	_, errNone := x509.RemoveCTPoison(clean.RawTBSCertificate)
	t.Check("remove-ct-poison-requires-a-poison-extension", errNone != nil,
		"RemoveCTPoison accepted a TBSCertificate with no poison extension")

	t.Done()
}
