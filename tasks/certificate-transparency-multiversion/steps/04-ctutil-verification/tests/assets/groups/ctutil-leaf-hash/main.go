// Group ctutil-leaf-hash.
//
// v1.0.13 makes RFC 6962 leaf-hash computation an importable surface:
// ctutil.LeafHash and ctutil.LeafHashB64 turn a (pre)certificate chain plus
// the SCT that was issued for it into the leaf hash the get-proof-by-hash
// endpoint is keyed by. The three documented shapes -- a plain X.509
// certificate, a precertificate with its issuer, and a certificate carrying
// the SCT embedded within it -- are pinned here against the published
// known-answers, together with the four documented error shapes.
//
// Boundary: the ctutil package's exported functions, called from a driver.
// Every expected value is a pinned constant from the corpus; no clock, no key
// generation, no network.
package main

import (
	"encoding/base64"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/ctutil"
	"github.com/google/certificate-transparency-go/x509"

	"ctverify/vhelp"
)

func main() {
	t := vhelp.New()
	m := katFile("rfc6962", "kat.txt")

	ca := vhelp.ReadFixture("rfc6962", "ca-cert.pem")
	certChain := pemCerts(t, join(vhelp.ReadFixture("rfc6962", "test-cert.pem"), ca))
	preChain := pemCerts(t, join(vhelp.ReadFixture("rfc6962", "test-pre-cert.pem"), ca))
	embChain := pemCerts(t, join(vhelp.ReadFixture("rfc6962", "test-embedded-cert.pem"), ca))
	for _, c := range [][]*x509.Certificate{certChain, preChain, embChain} {
		if len(c) != 2 {
			t.Fatalf("pinned chain fixture has %d certificates, want 2", len(c))
		}
	}

	certSCT := parseSCT(t, kat(t, m, "TestCertProof"))
	preSCT := parseSCT(t, kat(t, m, "TestPreCertProof"))
	invalidSCT := parseSCT(t, kat(t, m, "TestInvalidProof"))

	wantCert := kat(t, m, "TestCertB64LeafHash")
	wantPre := kat(t, m, "TestPreCertB64LeafHash")

	check := func(id, wantB64 string, chain []*x509.Certificate, sct *ct.SignedCertificateTimestamp, embedded bool) {
		want, err := base64.StdEncoding.DecodeString(wantB64)
		if err != nil {
			t.Fatalf("corpus base64 %q: %v", wantB64, err)
		}
		got, err := ctutil.LeafHash(chain, sct, embedded)
		if err != nil {
			t.Check(id, false, "LeafHash returned %v", err)
			return
		}
		t.EqualBytes(id, got[:], want)
	}

	// --- the three documented leaf shapes, exact hashes ---------------------
	check("leaf-hash-x509-certificate", wantCert, certChain, certSCT, false)
	check("leaf-hash-precertificate", wantPre, preChain, preSCT, false)
	check("leaf-hash-embedded-sct", wantPre, embChain, preSCT, true)

	// --- the base64 convenience wrapper agrees, exactly ---------------------
	b64 := func(id, want string, chain []*x509.Certificate, sct *ct.SignedCertificateTimestamp, embedded bool) {
		got, err := ctutil.LeafHashB64(chain, sct, embedded)
		if err != nil {
			t.Check(id, false, "LeafHashB64 returned %v", err)
			return
		}
		t.Equal(id, got, want)
	}
	b64("leaf-hash-b64-x509-certificate", wantCert, certChain, certSCT, false)
	b64("leaf-hash-b64-precertificate", wantPre, preChain, preSCT, false)
	b64("leaf-hash-b64-embedded-sct", wantPre, embChain, preSCT, true)

	// --- the documented error shapes ----------------------------------------
	// An error must leave the hash at its zero value and the base64 form empty,
	// so a caller that ignores the error cannot mistake it for an answer.
	var zero [32]byte
	errCase := func(id string, chain []*x509.Certificate, sct *ct.SignedCertificateTimestamp, embedded bool) {
		got, err := ctutil.LeafHash(chain, sct, embedded)
		gotB64, errB64 := ctutil.LeafHashB64(chain, sct, embedded)
		t.Check(id, err != nil && got == zero && errB64 != nil && gotB64 == "",
			"LeafHash=(%x,%v) LeafHashB64=(%q,%v), want zero hash and an error from both", got, err, gotB64, errB64)
	}
	errCase("empty-chain-is-an-error", nil, certSCT, false)
	errCase("nil-sct-is-an-error", certChain, nil, false)
	errCase("embedded-requested-but-cert-has-no-scts", certChain, invalidSCT, true)
	errCase("embedded-requested-but-this-sct-is-not-the-embedded-one", embChain, invalidSCT, true)

	// --- the answer is not an artefact of the chain being two certs ---------
	// A bare X.509 certificate with no issuer still hashes to the same value,
	// because only chain[0] and the SCT timestamp feed an X509LogEntryType leaf.
	check("leaf-hash-x509-certificate-alone", wantCert, certChain[:1], certSCT, false)

	t.Done()
}

func join(a, b []byte) []byte {
	out := make([]byte, 0, len(a)+len(b))
	return append(append(out, a...), b...)
}
