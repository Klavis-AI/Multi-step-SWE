// Group signature-verifier-key-compliance.
//
// RFC 6962 s2.1.4 allows a log key to be ECDSA on the NIST P-256 curve or RSA
// of at least 2048 bits, and the product refuses to build a signature
// verifier over anything else. Until v1.0.19 that refusal could be waived
// with a `flag` global declared in the root package. At v1.0.21 the waiver is
// the exported package variable ct.AllowVerificationWithNonCompliantKeys,
// which an importing program sets for itself and no command line can reach.
//
// Observed at the package boundary the documentation names (CLIENT-API.md
// section 3): ct.NewSignatureVerifier over compliant and non-compliant public
// keys, with the exported variable at its default and set both ways.
//
// Only the presence or absence of an error is graded, never its text. The
// key material is pinned: two keys come from the fixture corpus, the
// non-compliant RSA moduli are derived from the pinned 2048-bit fixture
// modulus by a fixed shift, and the off-curve cases use each curve's own
// standard base point, so nothing here reads a clock or a random source.
//
// v1.0.19 has no exported AllowVerificationWithNonCompliantKeys -- the waiver
// is an unexported *bool bound to a flag -- so this driver does not compile
// against that tree at all, and the waiver cases could not be expressed
// against it in any form.
package main

import (
	"crypto"
	"crypto/dsa"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rsa"
	"math/big"

	ct "github.com/google/certificate-transparency-go"

	"ctverify/vhelp"
)

// fixtureKey parses one of the pinned PEM public keys through the product's
// own PEM entry point.
func fixtureKey(t *vhelp.T, rel ...string) crypto.PublicKey {
	k, _, _, err := ct.PublicKeyFromPEM(vhelp.ReadFixture(rel...))
	if err != nil {
		t.Fatalf("parse pinned public key %v: %v", rel, err)
	}
	return k
}

// basePointKey is a genuine public key on the named curve: the curve's own
// standard base point, i.e. the public key whose private scalar is 1. Its
// value is a constant of the curve, so it is as pinned as a fixture.
func basePointKey(c elliptic.Curve) *ecdsa.PublicKey {
	p := c.Params()
	return &ecdsa.PublicKey{Curve: c, X: p.Gx, Y: p.Gy}
}

func main() {
	t := vhelp.New()

	// --- pinned key material ------------------------------------------------
	rsaPub, ok := fixtureKey(t, "keys", "rsa-public-key.pem").(*rsa.PublicKey)
	if !ok {
		t.Fatalf("pinned rsa-public-key.pem did not parse as RSA")
	}
	if rsaPub.N.BitLen() != 2048 {
		t.Fatalf("pinned RSA fixture is %d bits, expected 2048", rsaPub.N.BitLen())
	}
	ecdsaPub, ok := fixtureKey(t, "keys", "ecdsa-public-key.pem").(*ecdsa.PublicKey)
	if !ok {
		t.Fatalf("pinned ecdsa-public-key.pem did not parse as ECDSA")
	}
	if ecdsaPub.Curve != elliptic.P256() {
		t.Fatalf("pinned ECDSA fixture is not on P-256")
	}
	dsaPub, ok := fixtureKey(t, "keys", "dsa-public-key.pem").(*dsa.PublicKey)
	if !ok {
		t.Fatalf("pinned dsa-public-key.pem did not parse as DSA")
	}

	// Shifting the pinned 2048-bit modulus right by one bit and by 1024 bits
	// gives moduli of exactly 2047 and 1024 bits. Both are deterministic
	// functions of a fixture, and NewSignatureVerifier reads only N.BitLen().
	rsa2047 := &rsa.PublicKey{N: new(big.Int).Rsh(rsaPub.N, 1), E: rsaPub.E}
	rsa1024 := &rsa.PublicKey{N: new(big.Int).Rsh(rsaPub.N, 1024), E: rsaPub.E}
	if rsa2047.N.BitLen() != 2047 || rsa1024.N.BitLen() != 1024 {
		t.Fatalf("derived moduli are %d and %d bits", rsa2047.N.BitLen(), rsa1024.N.BitLen())
	}

	p384 := basePointKey(elliptic.P384())
	p521 := basePointKey(elliptic.P521())

	// accepts reports whether a verifier was built.
	accepts := func(pk crypto.PublicKey) (bool, error) {
		v, err := ct.NewSignatureVerifier(pk)
		return err == nil && v != nil, err
	}

	// --- the waiver's default ------------------------------------------------
	t.Check("waiver-defaults-to-false", !ct.AllowVerificationWithNonCompliantKeys,
		"AllowVerificationWithNonCompliantKeys is %v at start-up, want false",
		ct.AllowVerificationWithNonCompliantKeys)

	// --- compliant keys are accepted with the waiver off ---------------------
	got, err := accepts(rsaPub)
	t.Check("rsa-2048-accepted", got, "a 2048-bit RSA key was refused: %v", err)

	got, err = accepts(ecdsaPub)
	t.Check("ecdsa-p256-accepted", got, "a P-256 ECDSA key was refused: %v", err)

	// --- non-compliant keys are refused with the waiver off ------------------
	got, _ = accepts(rsa1024)
	t.Check("rsa-1024-refused", !got, "a 1024-bit RSA key was accepted with the waiver off")

	got, _ = accepts(rsa2047)
	t.Check("rsa-2047-refused", !got, "a 2047-bit RSA key was accepted with the waiver off")

	got, _ = accepts(p384)
	t.Check("ecdsa-p384-refused", !got, "a P-384 ECDSA key was accepted with the waiver off")

	got, _ = accepts(p521)
	t.Check("ecdsa-p521-refused", !got, "a P-521 ECDSA key was accepted with the waiver off")

	// A key of a type RFC 6962 does not mention is refused whatever the waiver
	// says: the waiver is about key size and curve, not key type.
	got, _ = accepts(dsaPub)
	t.Check("dsa-refused", !got, "a DSA key was accepted")

	// --- the waiver, set from inside the process -----------------------------
	ct.AllowVerificationWithNonCompliantKeys = true

	got, err = accepts(rsa1024)
	t.Check("waiver-set-rsa-1024-accepted", got, "the waiver did not admit a 1024-bit RSA key: %v", err)

	got, err = accepts(p384)
	t.Check("waiver-set-ecdsa-p384-accepted", got, "the waiver did not admit a P-384 ECDSA key: %v", err)

	got, _ = accepts(dsaPub)
	t.Check("waiver-set-dsa-still-refused", !got, "the waiver admitted a DSA key")

	got, err = accepts(rsaPub)
	t.Check("waiver-set-rsa-2048-still-accepted", got, "the waiver broke a compliant key: %v", err)

	// --- and unset again ------------------------------------------------------
	ct.AllowVerificationWithNonCompliantKeys = false

	got, _ = accepts(rsa1024)
	t.Check("waiver-cleared-rsa-1024-refused-again", !got,
		"a 1024-bit RSA key was still accepted after the waiver was cleared")

	t.Done()
}
