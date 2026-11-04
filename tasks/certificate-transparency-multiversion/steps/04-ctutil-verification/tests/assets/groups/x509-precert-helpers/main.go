// Group x509-precert-helpers.
//
// v1.0.13 adds two helpers to the forked x509 package that the new ctutil
// verification path is built on:
//
//	x509.RemoveSCTList(tbs)            strip the SCT-list extension from a
//	                                   DER TBSCertificate, keeping the order
//	                                   of every other extension
//	(*x509.Certificate).IsPrecertificate()  does this certificate carry the
//	                                   RFC 6962 s3.1 poison extension
//
// The pre-existing RemoveCTPoison/BuildPrecertTBS pair removes a *different*
// extension, so it cannot stand in for either.
//
// Boundary: the exported x509 functions, on pinned certificate fixtures.
package main

import (
	"bytes"
	"crypto/sha256"
	"encoding/hex"

	"github.com/google/certificate-transparency-go/x509"

	"ctverify/vhelp"
)

// The two CT extensions as they appear DER-encoded inside a TBSCertificate,
// written out here rather than taken from the product.
var (
	oidSCTList = []byte{0x06, 0x0a, 0x2b, 0x06, 0x01, 0x04, 0x01, 0xd6, 0x79, 0x02, 0x04, 0x02} // 1.3.6.1.4.1.11129.2.4.2
	oidPoison  = []byte{0x06, 0x0a, 0x2b, 0x06, 0x01, 0x04, 0x01, 0xd6, 0x79, 0x02, 0x04, 0x03} // 1.3.6.1.4.1.11129.2.4.3
)

const (
	wantStrippedLen    = 567
	wantStrippedSHA256 = "f891a12ef5a416b880296328e399ac1fce0b3d794781d11b62b33375beb3a956"
)

func main() {
	t := vhelp.New()

	embedded := pemCert(t, vhelp.ReadFixture("rfc6962", "test-embedded-cert.pem"))
	plain := pemCert(t, vhelp.ReadFixture("rfc6962", "test-cert.pem"))
	precert := pemCert(t, vhelp.ReadFixture("rfc6962", "test-pre-cert.pem"))
	ca := pemCert(t, vhelp.ReadFixture("rfc6962", "ca-cert.pem"))

	// --- IsPrecertificate ----------------------------------------------------
	t.Check("poisoned-precertificate-is-a-precertificate",
		precert.IsPrecertificate() && bytes.Contains(precert.RawTBSCertificate, oidPoison),
		"IsPrecertificate=%t on the pinned poisoned precertificate", precert.IsPrecertificate())

	t.Check("ordinary-certificate-is-not-a-precertificate",
		!plain.IsPrecertificate(), "a certificate with no poison extension was called a precertificate")

	// An embedded-SCT certificate is the *output* of logging a precertificate,
	// not a precertificate: it carries an SCT list and no poison.
	t.Check("embedded-sct-certificate-is-not-a-precertificate",
		!embedded.IsPrecertificate() &&
			bytes.Contains(embedded.RawTBSCertificate, oidSCTList) &&
			!bytes.Contains(embedded.RawTBSCertificate, oidPoison),
		"IsPrecertificate=%t on the embedded-SCT certificate", embedded.IsPrecertificate())

	t.Check("ca-certificate-is-not-a-precertificate",
		!ca.IsPrecertificate(), "the CA certificate was called a precertificate")

	var nilCert *x509.Certificate
	t.Check("nil-certificate-is-not-a-precertificate",
		!nilCert.IsPrecertificate(), "a nil *Certificate was called a precertificate")

	// --- RemoveSCTList -------------------------------------------------------
	stripped, err := x509.RemoveSCTList(embedded.RawTBSCertificate)
	if err != nil {
		t.Fatalf("RemoveSCTList on the pinned embedded-SCT certificate: %v", err)
	}
	sum := sha256.Sum256(stripped)
	t.Check("remove-sct-list-drops-exactly-the-sct-extension",
		len(stripped) == wantStrippedLen &&
			hex.EncodeToString(sum[:]) == wantStrippedSHA256 &&
			!bytes.Contains(stripped, oidSCTList),
		"len=%d sha256=%s (want %d / %s)", len(stripped), hex.EncodeToString(sum[:]),
		wantStrippedLen, wantStrippedSHA256)

	t.Check("remove-sct-list-preserves-the-rest-of-the-tbs",
		bytes.Contains(stripped, embedded.RawIssuer) &&
			bytes.Contains(stripped, embedded.RawSubject) &&
			bytes.Contains(stripped, embedded.RawSubjectPublicKeyInfo) &&
			len(stripped) < len(embedded.RawTBSCertificate),
		"stripped TBS (%d bytes, original %d) lost issuer, subject or SPKI bytes",
		len(stripped), len(embedded.RawTBSCertificate))

	_, err = x509.RemoveSCTList(stripped)
	t.Check("remove-sct-list-can-only-be-applied-once", err != nil,
		"removing the SCT list a second time succeeded")

	_, err = x509.RemoveSCTList(plain.RawTBSCertificate)
	t.Check("remove-sct-list-without-an-sct-extension-is-an-error", err != nil,
		"accepted a certificate that has no SCT-list extension")

	_, err = x509.RemoveSCTList(precert.RawTBSCertificate)
	t.Check("remove-sct-list-on-a-poisoned-precertificate-is-an-error", err != nil,
		"accepted a poisoned precertificate, which has no SCT list")

	_, err = x509.RemoveSCTList([]byte("this is not a DER TBSCertificate"))
	t.Check("remove-sct-list-rejects-non-der-input", err != nil, "accepted non-DER input")

	// --- the two removals are not interchangeable ----------------------------
	depoisoned, poisonErr := x509.RemoveCTPoison(precert.RawTBSCertificate)
	_, wrongWay := x509.RemoveCTPoison(embedded.RawTBSCertificate)
	t.Check("remove-ct-poison-and-remove-sct-list-target-different-extensions",
		poisonErr == nil && !bytes.Contains(depoisoned, oidPoison) && wrongWay != nil,
		"RemoveCTPoison on the precert: %v; RemoveCTPoison on the embedded-SCT cert: %v",
		poisonErr, wrongWay)

	t.Done()
}
