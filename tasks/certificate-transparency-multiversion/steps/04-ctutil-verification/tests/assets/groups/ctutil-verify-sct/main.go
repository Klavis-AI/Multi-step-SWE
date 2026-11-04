// Group ctutil-verify-sct.
//
// v1.0.13 exposes ctutil.VerifySCT: given a log's public key, a chain and an
// SCT, decide whether that SCT really was issued by that log for chain[0].
// The signature input is rebuilt internally from the chain, so the same call
// has to cover a plain certificate, a precertificate with its issuer, and a
// certificate whose SCT is embedded in it -- and has to reject an SCT that was
// issued for a different entry, a tampered signature, or the wrong log key.
//
// Boundary: ctutil.VerifySCT plus ct.PublicKeyFromB64 / ct.PublicKeyFromPEM,
// against the RFC 6962 reference log key pinned in the corpus.
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
	keys := katFile("keys", "kat.txt")

	ca := vhelp.ReadFixture("rfc6962", "ca-cert.pem")
	certChain := pemCerts(t, join(vhelp.ReadFixture("rfc6962", "test-cert.pem"), ca))
	preChain := pemCerts(t, join(vhelp.ReadFixture("rfc6962", "test-pre-cert.pem"), ca))
	embChain := pemCerts(t, join(vhelp.ReadFixture("rfc6962", "test-embedded-cert.pem"), ca))
	badEmbChain := pemCerts(t, join(vhelp.ReadFixture("rfc6962", "test-invalid-embedded-cert.pem"), ca))

	certSCT := parseSCT(t, kat(t, m, "TestCertProof"))
	preSCT := parseSCT(t, kat(t, m, "TestPreCertProof"))
	invalidSCT := parseSCT(t, kat(t, m, "TestInvalidProof"))

	// The log key, reached both ways the milestone offers.
	logKey, err := ct.PublicKeyFromB64(kat(t, keys, "LogPublicKeyB64"))
	if err != nil {
		t.Fatalf("PublicKeyFromB64 on the pinned log key: %v", err)
	}
	pemKey, _, _, err := ct.PublicKeyFromPEM(vhelp.ReadFixture("keys", "log-public-key.pem"))
	if err != nil {
		t.Fatalf("PublicKeyFromPEM on the pinned log key: %v", err)
	}
	otherKey, _, _, err := ct.PublicKeyFromPEM(vhelp.ReadFixture("rfc6962", "sig-test-ec256-public-key.pem"))
	if err != nil {
		t.Fatalf("PublicKeyFromPEM on the pinned decoy key: %v", err)
	}

	ok := func(id string, chain []*x509.Certificate, sct *ct.SignedCertificateTimestamp, embedded bool) {
		t.Case(id, ctutil.VerifySCT(logKey, chain, sct, embedded))
	}
	rejects := func(id string, key interface{}, chain []*x509.Certificate, sct *ct.SignedCertificateTimestamp, embedded bool) {
		err := ctutil.VerifySCT(key, chain, sct, embedded)
		t.Check(id, err != nil, "VerifySCT accepted it (returned nil)")
	}

	// --- the three shapes that must verify ---------------------------------
	ok("verifies-sct-for-x509-certificate", certChain, certSCT, false)
	ok("verifies-sct-for-precertificate", preChain, preSCT, false)
	ok("verifies-embedded-sct-by-rebuilding-the-precert", embChain, preSCT, true)

	// The same answer via a key read from PEM rather than base64.
	t.Case("verifies-with-log-key-read-from-pem", ctutil.VerifySCT(pemKey, certChain, certSCT, false))

	// --- what must not verify ----------------------------------------------
	rejects("rejects-sct-issued-for-a-different-entry", logKey, preChain, certSCT, false)
	rejects("rejects-invalid-embedded-sct", logKey, badEmbChain, invalidSCT, true)
	rejects("rejects-a-different-logs-key", otherKey, certChain, certSCT, false)
	rejects("rejects-nil-sct", logKey, certChain, nil, false)
	rejects("rejects-empty-chain", logKey, nil, certSCT, false)

	// A single flipped bit in the signature must be fatal.
	tampered := *certSCT
	sig := append([]byte{}, certSCT.Signature.Signature...)
	if len(sig) == 0 {
		t.Fatalf("pinned SCT carries no signature bytes")
	}
	sig[len(sig)-1] ^= 0x01
	tampered.Signature.Signature = sig
	rejects("rejects-a-flipped-bit-in-the-signature", logKey, certChain, &tampered, false)

	t.Done()
}

func join(a, b []byte) []byte {
	out := make([]byte, 0, len(a)+len(b))
	return append(append(out, a...), b...)
}
