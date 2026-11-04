// Group x509-nonfatal-parse-certificate.
//
// v1.1.0 rewires x509.ParseCertificate: when the strict asn1.Unmarshal of the
// outer certificate fails it retries the same bytes with the new "lax"
// parameter, and if that succeeds the strict failure is demoted to a
// NonFatalError. The certificate is therefore returned *together with* an
// error, and x509.IsFatal(err) reports false.
//
// At v1.0.21 the retry does not exist: ParseCertificate returns `nil, err` for
// exactly these inputs, so every case below sees a nil certificate.
//
// The five fixtures used here are the ones the corpus records as reachable
// only from v1.1.0 on (fixtures/malformed/ORIGIN.txt). No error text is
// graded -- only the exported IsFatal predicate and the parsed field values,
// all of which are pinned.
package main

import (
	"crypto/rsa"
	"encoding/pem"
	"fmt"

	"ctverify/vhelp"

	"github.com/google/certificate-transparency-go/x509"
)

func der(t *vhelp.T, name string) []byte {
	blk, _ := pem.Decode(vhelp.ReadFixture("malformed", name))
	if blk == nil || blk.Type != "CERTIFICATE" {
		t.Fatalf("fixture malformed/%s is not a PEM certificate", name)
	}
	return blk.Bytes
}

// summary renders the pinned identity of a parsed certificate.
func summary(c *x509.Certificate) string {
	ids := ""
	for i, e := range c.Extensions {
		if i > 0 {
			ids += ","
		}
		ids += e.Id.String()
	}
	return fmt.Sprintf("%s|%s|%s|%s|%s|%s|%v",
		c.SerialNumber.String(), c.Subject.String(), c.Issuer.String(), ids,
		c.NotBefore.UTC().Format("2006-01-02T15:04:05Z"),
		c.NotAfter.UTC().Format("2006-01-02T15:04:05Z"),
		c.PublicKeyAlgorithm)
}

const (
	subj = "CN=*.google.com,O=Google Inc,L=Mountain View,ST=California,C=US"
	issr = "CN=FakeCertificateAuthority,OU=Eng,O=Google,L=London,ST=London,C=GB"
)

func main() {
	t := vhelp.New()

	// name -> the exact rendering the v1.1.0 parser produces for it.
	type want struct {
		file string
		sum  string
	}
	cases := []want{
		{"xf-der-invalid-nonminimal-int.pem",
			"4660|" + subj + "|" + issr + "|2.5.29.35,2.5.29.15|2018-04-14T14:00:44Z|2020-12-14T14:00:44Z|ECDSA"},
		{"xf-der-pubkey-rsa-nonminimal-int.pem",
			"4660|" + subj + "|" + issr + "|2.5.29.35,2.5.29.15|2018-06-15T14:34:33Z|2021-02-14T14:34:33Z|RSA"},
		{"xf-ext-extended-key-usage-empty-oid.pem",
			"140298796799689226|" + subj + "|" + issr + "|2.5.29.35,2.5.29.37|2018-08-04T13:28:52Z|2021-04-05T13:28:52Z|ECDSA"},
		{"xf-pubkey-rsa-modulus-negative.pem",
			"129201852361993560|" + subj + "|" + issr + "|2.5.29.35|2018-04-14T14:00:43Z|2020-12-14T14:00:43Z|RSA"},
		{"xf-subject-nonprintable.pem",
			"116666066052028467|" + subj + "|" + issr + "|2.5.29.35,2.5.29.15|2018-04-14T14:00:40Z|2020-12-14T14:00:40Z|ECDSA"},
	}

	parsed := map[string]*x509.Certificate{}
	for _, w := range cases {
		id := w.file[:len(w.file)-4]
		c, err := x509.ParseCertificate(der(t, w.file))
		// The whole point of the milestone: an error AND a certificate, and the
		// error is not fatal.
		t.Check("nonfatal-"+id, c != nil && err != nil && !x509.IsFatal(err),
			"cert=%v err=%v fatal=%v; want a certificate plus a non-fatal error", c != nil, err, x509.IsFatal(err))
		if c == nil {
			t.Check("fields-"+id, false, "no certificate was returned")
			continue
		}
		parsed[id] = c
		t.Equal("fields-"+id, summary(c), w.sum)
	}

	// The recovered certificate is not a husk: the lax retry really did decode
	// the offending structures.
	if c := parsed["xf-der-pubkey-rsa-nonminimal-int"]; c != nil {
		k, ok := c.PublicKey.(*rsa.PublicKey)
		t.Check("nonminimal-rsa-exponent-recovered",
			ok && k.E == 65537 && k.N != nil && k.N.BitLen() == 2048,
			"public key %T recovered as %v", c.PublicKey, c.PublicKey)
	} else {
		t.Check("nonminimal-rsa-exponent-recovered", false, "certificate did not parse")
	}
	if c := parsed["xf-ext-extended-key-usage-empty-oid"]; c != nil {
		t.Equal("zero-length-eku-oid-recovered", fmt.Sprint(c.ExtKeyUsage),
			fmt.Sprint([]x509.ExtKeyUsage{x509.ExtKeyUsageServerAuth, x509.ExtKeyUsageClientAuth}))
	} else {
		t.Check("zero-length-eku-oid-recovered", false, "certificate did not parse")
	}

	// A well-formed certificate still parses with no error at all, so "returns
	// an error" is not the parser's default answer.
	good, err := x509.ParseCertificate(vhelp.ReadFixture("minted", "leaf-valid.der"))
	t.Check("well-formed-certificate-has-no-errors",
		err == nil && good != nil && good.Subject.CommonName == "valid.bench.invalid",
		"err=%v cert=%v", err, good != nil)

	// And laxness is bounded: a certificate truncated mid-structure is still a
	// fatal failure with no certificate returned.
	raw := der(t, "xf-subject-nonprintable.pem")
	bad, err := x509.ParseCertificate(raw[:len(raw)/2])
	t.Check("truncated-der-is-still-fatal", bad == nil && err != nil && x509.IsFatal(err),
		"cert=%v err=%v fatal=%v", bad != nil, err, x509.IsFatal(err))

	// ParseCertificates (plural) is the third member of the family
	// (X509-FORK.md §3.2). Everything parseCertificate recovers, it recovers
	// too, and returns as its own non-fatal error.
	one, err := x509.ParseCertificates(der(t, "xf-pubkey-rsa-modulus-negative.pem"))
	t.Check("parse-certificates-plural-nonfatal",
		len(one) == 1 && err != nil && !x509.IsFatal(err) &&
			one[0].SerialNumber.String() == "129201852361993560",
		"n=%d err=%v fatal=%v", len(one), err, err != nil && x509.IsFatal(err))

	// A defect in the *outer* certificate structure is the documented
	// exception: the plural function's lax retry is wired to the address of
	// an already-pointer local -- a `**certificate`, not a `*certificate`
	// (X509-FORK.md 3.2) -- so it can never recover, and the same bytes that
	// ParseCertificate returns as a certificate plus a non-fatal error are
	// fatal here. Both halves are asserted together, because it is the
	// contrast that is the contract.
	outer := der(t, "xf-der-invalid-nonminimal-int.pem")
	sing, serr := x509.ParseCertificate(outer)
	plur, perr := x509.ParseCertificates(outer)
	t.Check("parse-certificates-outer-defect-stays-fatal",
		sing != nil && serr != nil && !x509.IsFatal(serr) &&
			plur == nil && perr != nil && x509.IsFatal(perr),
		"singular cert=%v err=%v; plural n=%d err=%v", sing != nil, serr, len(plur), perr)

	// And over a concatenation: the well-formed certificate in front does not
	// stop the malformed one behind it from being recovered, and the one
	// non-fatal error covers the pair.
	pair := append(append([]byte{}, vhelp.ReadFixture("minted", "leaf-valid.der")...),
		der(t, "xf-pubkey-rsa-modulus-negative.pem")...)
	two, err := x509.ParseCertificates(pair)
	t.Check("parse-certificates-plural-over-concatenation",
		len(two) == 2 && err != nil && !x509.IsFatal(err) &&
			two[0].Subject.CommonName == "valid.bench.invalid" &&
			two[1].SerialNumber.String() == "129201852361993560",
		"n=%d err=%v fatal=%v", len(two), err, err != nil && x509.IsFatal(err))

	// The other bound on the retry, and the one it cannot reach: X509-FORK.md
	// §3.2 -- "Trailing data after the certificate is always fatal:
	// asn1.SyntaxError{Msg: "trailing data"}, with no lax fallback." The
	// leftover-length test is applied to whatever the parse left over, and the
	// singular function runs it whichever parse succeeded.
	//
	// First on bytes that need no recovery at all. This is exactly the
	// concatenation the plural function accepted two certificates from, four
	// cases up, so the pair of assertions is the whole of "which function to
	// call when you have more than one certificate".
	single, serr2 := x509.ParseCertificate(pair)
	t.Check("trailing-data-after-well-formed-certificate-is-fatal",
		single == nil && serr2 != nil && x509.IsFatal(serr2),
		"cert=%v err=%v fatal=%v; want no certificate and a fatal error",
		single != nil, serr2, serr2 != nil && x509.IsFatal(serr2))

	// Then on bytes the lax retry *does* recover. The same prefix alone is the
	// nonfatal-xf-der-invalid-nonminimal-int case at the top: a certificate
	// plus a non-fatal error. Two extra bytes -- an empty SEQUENCE, so the
	// tail is well-formed DER and only its position is wrong -- make it fatal
	// and suppress the certificate, because the retry's own leftovers are
	// length-checked too and no amount of laxness excuses a tail.
	tailed := append(append([]byte{}, der(t, "xf-der-invalid-nonminimal-int.pem")...), 0x30, 0x00)
	tc, terr := x509.ParseCertificate(tailed)
	t.Check("trailing-data-after-lax-recovered-certificate-is-fatal",
		tc == nil && terr != nil && x509.IsFatal(terr),
		"cert=%v err=%v fatal=%v; want no certificate and a fatal error",
		tc != nil, terr, terr != nil && x509.IsFatal(terr))

	// ParseTBSCertificate is part of the same family and gained the same retry.
	if c := parsed["xf-der-invalid-nonminimal-int"]; c != nil {
		tbs, err := x509.ParseTBSCertificate(c.RawTBSCertificate)
		t.Check("parse-tbs-certificate-nonfatal",
			tbs != nil && err != nil && !x509.IsFatal(err) && tbs.SerialNumber.String() == "4660",
			"tbs=%v err=%v fatal=%v", tbs != nil, err, x509.IsFatal(err))
	} else {
		t.Check("parse-tbs-certificate-nonfatal", false, "certificate did not parse")
	}

	t.Done()
}
