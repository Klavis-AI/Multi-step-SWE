// Group ctutil-contains-sct.
//
// v1.0.13 exposes ctutil.ContainsSCT, the membership test that decides whether
// a given SCT is one of the SCTs embedded in a given certificate. It is an
// exact test over the TLS encoding of the whole SCT, not over the log id or
// the timestamp alone, and it is what LeafHash and VerifySCT consult before
// they agree to rebuild a precertificate from an embedded SCT.
//
// Boundary: ctutil.ContainsSCT, called from a driver with the two RFC 6962
// reference certificates that carry embedded SCTs and the two pinned SCTs
// those certificates carry.
package main

import (
	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/ctutil"
	"github.com/google/certificate-transparency-go/x509"

	"ctverify/vhelp"
)

func main() {
	t := vhelp.New()
	m := katFile("rfc6962", "kat.txt")

	plain := pemCert(t, vhelp.ReadFixture("rfc6962", "test-cert.pem"))
	caCert := pemCert(t, vhelp.ReadFixture("rfc6962", "ca-cert.pem"))
	emb := pemCert(t, vhelp.ReadFixture("rfc6962", "test-embedded-cert.pem"))
	badEmb := pemCert(t, vhelp.ReadFixture("rfc6962", "test-invalid-embedded-cert.pem"))

	preSCT := parseSCT(t, kat(t, m, "TestPreCertProof"))
	invalidSCT := parseSCT(t, kat(t, m, "TestInvalidProof"))

	says := func(id string, cert *x509.Certificate, sct *ct.SignedCertificateTimestamp, want bool) {
		got, err := ctutil.ContainsSCT(cert, sct)
		if err != nil {
			t.Check(id, false, "ContainsSCT returned %v", err)
			return
		}
		t.Check(id, got == want, "ContainsSCT = %t, want %t", got, want)
	}

	// --- membership ---------------------------------------------------------
	says("embedded-cert-contains-its-own-sct", emb, preSCT, true)
	says("invalid-embedded-cert-contains-its-own-sct", badEmb, invalidSCT, true)

	// --- non-membership -----------------------------------------------------
	says("certificate-without-an-sct-list-contains-nothing", plain, preSCT, false)
	says("ca-certificate-contains-nothing", caCert, preSCT, false)
	says("embedded-cert-does-not-contain-a-foreign-sct", emb, invalidSCT, false)
	says("invalid-embedded-cert-does-not-contain-the-valid-sct", badEmb, preSCT, false)

	// The comparison is over the full TLS encoding: same log, same signature,
	// one millisecond's difference in the timestamp, and it is a different SCT.
	shifted := *preSCT
	shifted.Timestamp = preSCT.Timestamp + 1
	says("one-millisecond-of-timestamp-drift-is-a-different-sct", emb, &shifted, false)

	// --- the two nil shapes are answered, not panicked over -----------------
	says("nil-certificate-is-false", nil, preSCT, false)
	says("nil-sct-is-false", emb, nil, false)

	t.Done()
}
