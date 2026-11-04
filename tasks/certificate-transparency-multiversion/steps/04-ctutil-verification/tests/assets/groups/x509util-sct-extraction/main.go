// Group x509util-sct-extraction.
//
// v1.0.13 adds the three functions that turn the SCT-list extension of a
// certificate back into ct.SignedCertificateTimestamp values:
//
//	x509util.ExtractSCT(*x509.SerializedSCT)
//	x509util.ParseSCTsFromSCTList(*x509.SignedCertificateTimestampList)
//	x509util.ParseSCTsFromCertificate([]byte)   PEM or DER
//
// Boundary: the exported x509util functions. The expected SCTs are the two
// pinned RFC 6962 proofs; every recovered SCT is re-encoded with tls.Marshal
// and compared to the pinned hex byte for byte.
package main

import (
	"encoding/hex"

	ct "github.com/google/certificate-transparency-go"

	"github.com/google/certificate-transparency-go/tls"
	"github.com/google/certificate-transparency-go/x509"
	"github.com/google/certificate-transparency-go/x509util"

	"ctverify/vhelp"
)

type ctSCT = ct.SignedCertificateTimestamp

func main() {
	t := vhelp.New()
	m := katFile("rfc6962", "kat.txt")
	wantValid := kat(t, m, "TestPreCertProof")
	wantInvalid := kat(t, m, "TestInvalidProof")

	embeddedPEM := vhelp.ReadFixture("rfc6962", "test-embedded-cert.pem")
	invalidPEM := vhelp.ReadFixture("rfc6962", "test-invalid-embedded-cert.pem")
	embedded := pemCert(t, embeddedPEM)
	plain := pemCert(t, vhelp.ReadFixture("rfc6962", "test-cert.pem"))

	// --- ParseSCTsFromCertificate -------------------------------------------
	scts, err := x509util.ParseSCTsFromCertificate(embeddedPEM)
	if err != nil {
		t.Fatalf("ParseSCTsFromCertificate on the pinned PEM: %v", err)
	}
	t.Check("parses-the-embedded-sct-from-pem",
		len(scts) == 1 && reencode(t, scts[0]) == wantValid,
		"got %d SCT(s), first = %s", len(scts), first(t, scts))

	derSCTs, err := x509util.ParseSCTsFromCertificate(embedded.Raw)
	if err != nil {
		t.Fatalf("ParseSCTsFromCertificate on the pinned DER: %v", err)
	}
	t.Check("parses-the-same-sct-from-der",
		len(derSCTs) == 1 && reencode(t, derSCTs[0]) == wantValid,
		"got %d SCT(s), first = %s", len(derSCTs), first(t, derSCTs))

	badSCTs, err := x509util.ParseSCTsFromCertificate(invalidPEM)
	t.Check("parses-the-sct-of-the-invalid-embedded-certificate",
		err == nil && len(badSCTs) == 1 && reencode(t, badSCTs[0]) == wantInvalid,
		"err=%v, got %d SCT(s), first = %s", err, len(badSCTs), first(t, badSCTs))

	none, err := x509util.ParseSCTsFromCertificate(plain.Raw)
	t.Check("certificate-without-an-sct-list-yields-no-scts",
		err == nil && len(none) == 0, "err=%v, got %d SCT(s)", err, len(none))

	_, err = x509util.ParseSCTsFromCertificate([]byte("neither PEM nor DER"))
	t.Check("input-that-is-not-a-certificate-is-an-error", err != nil,
		"accepted input that is not a certificate")

	// --- ParseSCTsFromSCTList ------------------------------------------------
	validDER := mustHex(t, wantValid)
	invalidDER := mustHex(t, wantInvalid)

	list := x509.SignedCertificateTimestampList{
		SCTList: []x509.SerializedSCT{{Val: validDER}, {Val: invalidDER}},
	}
	both, err := x509util.ParseSCTsFromSCTList(&list)
	t.Check("sct-list-is-walked-in-order",
		err == nil && len(both) == 2 &&
			reencode(t, both[0]) == wantValid && reencode(t, both[1]) == wantInvalid,
		"err=%v, got %d SCT(s)", err, len(both))

	emptyList := x509.SignedCertificateTimestampList{}
	nothing, err := x509util.ParseSCTsFromSCTList(&emptyList)
	t.Check("empty-sct-list-yields-no-scts",
		err == nil && len(nothing) == 0, "err=%v, got %d SCT(s)", err, len(nothing))

	corrupt := x509.SignedCertificateTimestampList{
		SCTList: []x509.SerializedSCT{{Val: validDER}, {Val: validDER[:8]}},
	}
	_, err = x509util.ParseSCTsFromSCTList(&corrupt)
	t.Check("one-corrupt-entry-fails-the-whole-list", err != nil,
		"accepted an SCT list containing a truncated SCT")

	// --- ExtractSCT ----------------------------------------------------------
	one, err := x509util.ExtractSCT(&x509.SerializedSCT{Val: validDER})
	t.Check("extract-sct-decodes-a-single-serialized-sct",
		err == nil && one != nil && reencode(t, one) == wantValid,
		"err=%v, got %s", err, first(t, []*ctSCT{one}))

	_, nilErr := x509util.ExtractSCT(nil)
	_, shortErr := x509util.ExtractSCT(&x509.SerializedSCT{Val: validDER[:4]})
	t.Check("extract-sct-rejects-nil-and-truncated-input",
		nilErr != nil && shortErr != nil,
		"nil -> %v, truncated -> %v", nilErr, shortErr)

	t.Done()
}

func reencode(t *vhelp.T, sct *ctSCT) string {
	if sct == nil {
		return "<nil>"
	}
	b, err := tls.Marshal(*sct)
	if err != nil {
		return "<unmarshalable: " + err.Error() + ">"
	}
	return hex.EncodeToString(b)
}

func first(t *vhelp.T, scts []*ctSCT) string {
	if len(scts) == 0 {
		return "<none>"
	}
	return reencode(t, scts[0])
}
