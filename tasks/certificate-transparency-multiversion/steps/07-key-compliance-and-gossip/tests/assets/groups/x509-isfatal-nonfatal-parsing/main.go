// Group x509-isfatal-nonfatal-parsing.
//
// v1.0.21 splits x509 parse diagnostics in two. Conditions that leave a
// certificate structurally intelligible -- an empty ExtendedKeyUsage, an empty
// SubjectInfoAccess, an RSA SubjectPublicKeyInfo whose AlgorithmIdentifier
// omits the NULL parameters, a key on an insecure named curve -- stop
// aborting the parse. ParseCertificate now returns both a usable
// *x509.Certificate and a diagnostic, and the new exported predicate
// x509.IsFatal(err) is what tells a caller which kind it has. Conditions that
// leave the DER unreadable are unchanged and still fatal.
//
// Graded entirely through the two documented entrypoints: the certificate
// ParseCertificate hands back, and IsFatal's verdict on the error it hands
// back beside it. The diagnostic *strings* are never asserted -- they are not
// part of any protocol message -- and neither is the concrete error type;
// only IsFatal, err == nil and cert == nil, which is the whole contract a
// caller can rely on.
//
// Every input is a pinned PEM fixture from the shared corpus. SCOPE-TIME: no
// clock is consulted; nothing here verifies a signature or a validity window.
// SCOPE-NET: nothing is dialled.
package main

import (
	"crypto/ecdsa"
	"crypto/rsa"
	"encoding/pem"
	"errors"
	"fmt"
	"strings"

	"ctverify/vhelp"

	"github.com/google/certificate-transparency-go/x509"
)

// The four conditions v1.0.21 demotes to non-fatal, and the serial number
// each pinned fixture carries, so "usable certificate" means a certificate
// whose fields actually came out of this DER.
var survivable = []struct {
	file   string
	serial string
}{
	{"xf-ext-extended-key-usage-empty.pem", "96096843357863639"},
	{"xf-ext-subject-info-empty.pem", "143775503077481624"},
	{"xf-pubkey-rsa-param-nonnull.pem", "103563476275448133"},
	{"xf-pubkey-ecdsa-secp192r1.pem", "141222174073305197"},
}

// Conditions that remain fatal: the DER itself is unreadable.
var unreadable = []string{
	"xf-ext-extended-key-usage-empty-oid.pem",
	"xf-pubkey-rsa-modulus-negative.pem",
	"xf-der-invalid-nonminimal-int.pem",
	"xf-der-pubkey-rsa-nonminimal-int.pem",
}

// Fixtures that parse with no diagnostic at all. These are the control: they
// keep "non-fatal" from being the answer for every input.
var clean = []string{
	"xf-subject-nonprintable.pem",
	"mismatching-sig-alg.pem",
}

// derOf decodes one pinned PEM fixture. A missing or unreadable fixture is a
// hard stop, never a skip.
func derOf(t *vhelp.T, name string) []byte {
	raw := vhelp.ReadFixture("malformed", name)
	blk, _ := pem.Decode(raw)
	if blk == nil || len(blk.Bytes) == 0 {
		t.Fatalf("fixture malformed/%s is not a PEM block", name)
	}
	return blk.Bytes
}

// verdict renders the complete, observable outcome of one parse.
func verdict(t *vhelp.T, name string) string {
	cert, err := x509.ParseCertificate(derOf(t, name))
	return fmt.Sprintf("%s fatal=%v err=%v cert=%v",
		name, x509.IsFatal(err), err != nil, cert != nil)
}

// parseOf returns the certificate for a fixture that must yield one.
func parseOf(t *vhelp.T, name string) *x509.Certificate {
	cert, err := x509.ParseCertificate(derOf(t, name))
	if cert == nil {
		t.Fatalf("%s: ParseCertificate returned no certificate (err=%v)", name, err)
	}
	return cert
}

func main() {
	t := vhelp.New()

	// --- 1. the predicate's own two boundary cases -------------------------
	// No error is not a fatal error; an error that carries no parse
	// diagnostics at all is.
	t.Equal("isfatal-classifies-nil-and-unstructured-errors",
		fmt.Sprintf("nil=%v opaque=%v", x509.IsFatal(nil), x509.IsFatal(errors.New("harbor: unrelated failure"))),
		"nil=false opaque=true")

	// --- 2. the whole pinned table, in one exact assertion ------------------
	// Ten fixtures, each with the verdict v1.0.21 owes it. Asserting the
	// table as a unit means a build that classifies everything one way -- or
	// that simply returns nil for everything -- cannot pass any part of it.
	var rows []string
	for _, s := range survivable {
		rows = append(rows, verdict(t, s.file))
	}
	for _, name := range unreadable {
		rows = append(rows, verdict(t, name))
	}
	for _, name := range clean {
		rows = append(rows, verdict(t, name))
	}
	want := []string{
		"xf-ext-extended-key-usage-empty.pem fatal=false err=true cert=true",
		"xf-ext-subject-info-empty.pem fatal=false err=true cert=true",
		"xf-pubkey-rsa-param-nonnull.pem fatal=false err=true cert=true",
		"xf-pubkey-ecdsa-secp192r1.pem fatal=false err=true cert=true",
		"xf-ext-extended-key-usage-empty-oid.pem fatal=true err=true cert=false",
		"xf-pubkey-rsa-modulus-negative.pem fatal=true err=true cert=false",
		"xf-der-invalid-nonminimal-int.pem fatal=true err=true cert=false",
		"xf-der-pubkey-rsa-nonminimal-int.pem fatal=true err=true cert=false",
		"xf-subject-nonprintable.pem fatal=false err=false cert=true",
		"mismatching-sig-alg.pem fatal=false err=false cert=true",
	}
	t.Equal("every-pinned-fixture-gets-its-documented-verdict",
		strings.Join(rows, "\n"), strings.Join(want, "\n"))

	// --- 3. a non-fatal parse yields a certificate that really is this one --
	var got []string
	for _, s := range survivable {
		cert := parseOf(t, s.file)
		got = append(got, fmt.Sprintf("%s cn=%q serial=%v", s.file, cert.Subject.CommonName, cert.SerialNumber))
	}
	var wantCerts []string
	for _, s := range survivable {
		wantCerts = append(wantCerts, fmt.Sprintf("%s cn=%q serial=%s", s.file, "*.google.com", s.serial))
	}
	t.Equal("non-fatal-parse-returns-the-certificate-it-read",
		strings.Join(got, "\n"), strings.Join(wantCerts, "\n"))

	// --- 4. empty ExtendedKeyUsage ----------------------------------------
	// Survivable, and the empty extension leaves both usage lists empty
	// rather than inventing a usage.
	eku := parseOf(t, "xf-ext-extended-key-usage-empty.pem")
	t.Equal("empty-extended-key-usage-is-survivable-and-stays-empty",
		fmt.Sprintf("eku=%d unknown=%d exts=%v", len(eku.ExtKeyUsage), len(eku.UnknownExtKeyUsage), len(eku.Extensions) > 0),
		"eku=0 unknown=0 exts=true")

	// --- 5. empty SubjectInfoAccess ----------------------------------------
	sia := parseOf(t, "xf-ext-subject-info-empty.pem")
	t.Equal("empty-subject-info-access-is-survivable-and-stays-empty",
		fmt.Sprintf("timestamps=%d repos=%d exts=%v", len(sia.SubjectTimestamps), len(sia.SubjectCARepositories), len(sia.Extensions) > 0),
		"timestamps=0 repos=0 exts=true")

	// --- 6. RSA AlgorithmIdentifier without the NULL parameters ------------
	// The key is still decoded, as an RSA key.
	rsaCert := parseOf(t, "xf-pubkey-rsa-param-nonnull.pem")
	rsaKey, rsaOK := rsaCert.PublicKey.(*rsa.PublicKey)
	bits, exponent := 0, 0
	if rsaOK {
		bits, exponent = rsaKey.N.BitLen(), rsaKey.E
	}
	t.Equal("rsa-key-missing-null-parameters-is-survivable",
		fmt.Sprintf("alg=%v rsa=%v bits=%d e=%d", rsaCert.PublicKeyAlgorithm, rsaOK, bits, exponent),
		"alg=RSA rsa=true bits=2048 e=65537")

	// --- 7. a key on an insecure named curve -------------------------------
	// Detailed in x509-curves-secp192r1; here only that the diagnostic is
	// non-fatal and the key survives as an ECDSA key.
	ecCert := parseOf(t, "xf-pubkey-ecdsa-secp192r1.pem")
	ecKey, ecOK := ecCert.PublicKey.(*ecdsa.PublicKey)
	curve := ""
	if ecOK {
		curve = ecKey.Curve.Params().Name
	}
	t.Equal("insecure-curve-is-survivable",
		fmt.Sprintf("alg=%v ecdsa=%v curve=%q", ecCert.PublicKeyAlgorithm, ecOK, curve),
		`alg=ECDSA ecdsa=true curve="P-192"`)

	// --- 8..10. the fatal side, one condition at a time --------------------
	// Each is asserted on its own so a single over-broad demotion is named.
	fatalOf := func(name string) string {
		cert, err := x509.ParseCertificate(derOf(t, name))
		return fmt.Sprintf("fatal=%v cert=%v", x509.IsFatal(err), cert != nil)
	}
	t.Equal("zero-length-object-identifier-remains-fatal",
		fatalOf("xf-ext-extended-key-usage-empty-oid.pem"), "fatal=true cert=false")
	t.Equal("negative-rsa-modulus-remains-fatal",
		fatalOf("xf-pubkey-rsa-modulus-negative.pem"), "fatal=true cert=false")
	t.Equal("non-minimally-encoded-der-integers-remain-fatal",
		fmt.Sprintf("serial=%s exponent=%s",
			fatalOf("xf-der-invalid-nonminimal-int.pem"),
			fatalOf("xf-der-pubkey-rsa-nonminimal-int.pem")),
		"serial=fatal=true cert=false exponent=fatal=true cert=false")

	// --- 11. control: a clean certificate carries no diagnostic -------------
	// If IsFatal were simply "err != nil" -- or if ParseCertificate attached a
	// non-fatal diagnostic to everything -- these two would not come back
	// silent.
	var quiet []string
	for _, name := range clean {
		cert, err := x509.ParseCertificate(derOf(t, name))
		quiet = append(quiet, fmt.Sprintf("%s err=%v fatal=%v serial=%v",
			name, err != nil, x509.IsFatal(err), cert.SerialNumber))
	}
	t.Equal("well-formed-certificates-carry-no-diagnostic",
		strings.Join(quiet, "\n"),
		"xf-subject-nonprintable.pem err=false fatal=false serial=116666066052028467\n"+
			"mismatching-sig-alg.pem err=false fatal=false serial=0")

	t.Done()
}
