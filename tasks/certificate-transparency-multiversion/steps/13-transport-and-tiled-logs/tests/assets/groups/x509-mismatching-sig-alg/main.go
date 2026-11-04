// x509-mismatching-sig-alg
//
// v1.3.3 makes the forked parser reject a certificate whose two
// AlgorithmIdentifier structures -- the one inside the signed tbsCertificate
// and the one in the outer, unsigned Certificate -- do not match byte for
// byte, with the exact error "x509: mismatching signature algorithm
// identifiers".  The bare TBS entry point is deliberately exempt: it is
// handed only one identifier, so there is nothing to compare, and it must go
// on accepting exactly the bytes it accepted at v1.2.0.
//
// Everything here is a pinned fixture: x509/testdata/invalid/
// mismatching-sig-alg.pem as upstream added it at this release, and the
// minted leaf for the positive control.  No clock, no socket, no network.
package main

import (
	"encoding/asn1"
	"encoding/pem"
	"fmt"

	"github.com/google/certificate-transparency-go/x509"

	"ctverify/vhelp"
)

const wantErr = "x509: mismatching signature algorithm identifiers"

// firstElement returns the DER of the first element inside the SEQUENCE that
// b encodes. Applied to a Certificate it yields the tbsCertificate, which is
// what ParseTBSCertificate is defined over. Done with the standard library so
// that the bytes handed to the product are not produced by the product.
func firstElement(b []byte) ([]byte, error) {
	var outer asn1.RawValue
	if _, err := asn1.Unmarshal(b, &outer); err != nil {
		return nil, err
	}
	var inner asn1.RawValue
	if _, err := asn1.Unmarshal(outer.Bytes, &inner); err != nil {
		return nil, err
	}
	return inner.FullBytes, nil
}

func derOfPEM(t *vhelp.T, rel ...string) []byte {
	blk, _ := pem.Decode(vhelp.ReadFixture(rel...))
	if blk == nil {
		t.Fatalf("fixture %v is not PEM", rel)
	}
	return blk.Bytes
}

func main() {
	t := vhelp.New()

	bad := derOfPEM(t, "malformed", "mismatching-sig-alg.pem")
	good := vhelp.ReadFixture("minted", "leaf-valid.der")

	// --- control: the parser works at all on a well-formed certificate.
	gotGood, err := x509.ParseCertificate(good)
	t.Check("control-good-certificate-parses", err == nil && gotGood != nil,
		"ParseCertificate(leaf-valid.der) = %v, %v; want a certificate and no error", gotGood, err)

	// --- ParseCertificate refuses the mismatch, with the exact message.
	c, err := x509.ParseCertificate(bad)
	msg := ""
	if err != nil {
		msg = err.Error()
	}
	t.Check("parsecertificate-rejects-mismatch", c == nil && msg == wantErr,
		"ParseCertificate(mismatching-sig-alg) = %v, %q; want nil, %q", c, msg, wantErr)

	// --- and so does the multi-certificate entry point, over the same bytes.
	cs, err := x509.ParseCertificates(bad)
	msg = ""
	if err != nil {
		msg = err.Error()
	}
	t.Check("parsecertificates-rejects-mismatch", cs == nil && msg == wantErr,
		"ParseCertificates(mismatching-sig-alg) = %v, %q; want nil, %q", cs, msg, wantErr)

	// --- the refusal is not a survivable non-fatal note: no certificate at all
	// comes back, so a caller that ignores NonFatalErrors still gets nothing.
	c2, _ := x509.ParseCertificate(bad)
	t.Check("mismatch-yields-no-certificate", c2 == nil,
		"ParseCertificate(mismatching-sig-alg) returned a certificate %v", c2)

	// --- the exemption: the bare TBS parse of the very same bytes succeeds.
	badTBS, ferr := firstElement(bad)
	if ferr != nil {
		t.Fatalf("extracting tbsCertificate from the fixture: %v", ferr)
	}
	tbs, err := x509.ParseTBSCertificate(badTBS)
	ok := tbs != nil && (err == nil || !x509.IsFatal(err))
	t.Check("parsetbscertificate-accepts-mismatch", ok,
		"ParseTBSCertificate(tbs of mismatching-sig-alg) = %v, %v; want a certificate", tbs, err)

	// --- and it really parsed those exact bytes back.
	var round []byte
	if tbs != nil {
		round = tbs.RawTBSCertificate
	}
	t.EqualBytes("parsetbscertificate-round-trips-the-exact-bytes", round, badTBS)

	// --- control for the exemption: a well-formed certificate's TBS too.
	goodTBS, ferr := firstElement(good)
	if ferr != nil {
		t.Fatalf("extracting tbsCertificate from leaf-valid.der: %v", ferr)
	}
	gt, err := x509.ParseTBSCertificate(goodTBS)
	t.Check("control-parsetbscertificate-accepts-good-tbs", gt != nil && (err == nil || !x509.IsFatal(err)),
		"ParseTBSCertificate(tbs of leaf-valid.der) = %v, %v", gt, err)

	// --- the two fixtures are genuinely different inputs, so the refusal above
	// is about this certificate and not about every certificate.
	t.Check("fixtures-are-distinct", fmt.Sprintf("%x", bad) != fmt.Sprintf("%x", good),
		"the malformed and control fixtures are the same bytes")

	t.Done()
}
