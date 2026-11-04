// Group x509-curves-secp192r1.
//
// v1.0.21 adds x509/curves.go: an elliptic curve the standard library does
// not carry, secp192r1 (NIST P-192), "observed in certificates in the wild".
// With it comes the exported OID x509.OIDNamedCurveP192, a new arm in the
// named-curve table, and the demotion of "insecure curve" to a non-fatal
// parse diagnostic -- so a certificate bearing a P-192 key now yields a fully
// populated *ecdsa.PublicKey instead of nothing.
//
// The interesting part of the change is that it is deliberately asymmetric:
// the curve is recognised when it arrives inside a certificate, but
// ParsePKIXPublicKey -- which has no NonFatalErrors channel to report on --
// still refuses a bare P-192 SubjectPublicKeyInfo outright. Both halves are
// graded.
//
// Every expected value is external truth, not an echo of the code under test:
// the domain parameters are SEC 2 section 2.2.2, the OIDs are ANSI
// X9.62 / SEC 1 dotted arcs, and the point coordinates and the 75-byte SPKI
// come out of the pinned fixture's own DER. SCOPE-TIME: no clock. SCOPE-NET:
// nothing dialled.
package main

import (
	"bytes"
	"crypto/ecdsa"
	"encoding/pem"
	"fmt"
	"strings"

	"ctverify/vhelp"

	"github.com/google/certificate-transparency-go/x509"
	"github.com/google/certificate-transparency-go/x509util"
)

// SEC 2, section 2.2.2 -- the secp192r1 domain parameters, written out here
// from the standard rather than read back from the implementation.
const (
	wantP  = "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFFFFFFFFFFFF"
	wantN  = "FFFFFFFFFFFFFFFFFFFFFFFF99DEF836146BC9B1B4D22831"
	wantB  = "64210519E59C80E70FA7E9AB72243049FEB8DEECC146B9B1"
	wantGx = "188DA80EB03090F67CBF20EB43A18800F4FF0AFD82FF1012"
	// Printed without its leading zero nibble: the standard's Gy is
	// 07192B95...
	wantGy = "7192B95FFC8DA78631011ED6B24CDD573F977A11E794811"
)

// The fixture's own uncompressed public point, and the whole DER
// SubjectPublicKeyInfo it sits in: SEQUENCE { SEQUENCE { id-ecPublicKey,
// secp192r1 }, BIT STRING { 04 || X || Y } }.
const (
	wantX    = "6950D6B2223D861CDEA8834BE39F73151A4AB250D4A0BA6B"
	wantY    = "924DBFFB03B10256D572BC24EAA2D33D4C261EE1E5A045E3"
	wantSPKI = "3049" + "3013" +
		"06072a8648ce3d0201" + // id-ecPublicKey
		"06082a8648ce3d030101" + // secp192r1
		"03320004" +
		"6950d6b2223d861cdea8834be39f73151a4ab250d4a0ba6b" +
		"924dbffb03b10256d572bc24eaa2d33d4c261ee1e5a045e3"
)

// pemCert decodes a pinned PEM certificate fixture; a missing or unreadable
// fixture is a hard stop, never a skip.
func pemCert(t *vhelp.T, dir, name string) *x509.Certificate {
	blk, _ := pem.Decode(vhelp.ReadFixture(dir, name))
	if blk == nil {
		t.Fatalf("fixture %s/%s is not a PEM block", dir, name)
	}
	cert, err := x509.ParseCertificate(blk.Bytes)
	if cert == nil {
		t.Fatalf("%s/%s: no certificate returned (err=%v)", dir, name, err)
	}
	return cert
}

// ecKey narrows a certificate's public key, failing the whole group if it is
// not an ECDSA key at all.
func ecKey(t *vhelp.T, cert *x509.Certificate, what string) *ecdsa.PublicKey {
	pub, ok := cert.PublicKey.(*ecdsa.PublicKey)
	if !ok {
		t.Fatalf("%s: public key is %T, not *ecdsa.PublicKey", what, cert.PublicKey)
	}
	return pub
}

// spkiLines keeps only the two key-description lines X509-FORK.md §5.7
// documents -- the bit-length line and the curve's ASN1 OID line -- so the
// assertion is on the curve's presentation and not on the surrounding block
// structure, whose header lines no document fixes.
func spkiLines(text string) string {
	var keep []string
	for _, line := range strings.Split(text, "\n") {
		line = strings.TrimSpace(line)
		switch {
		case strings.HasPrefix(line, "Public Key:"),
			strings.HasPrefix(line, "ASN1 OID"):
			keep = append(keep, line)
		}
	}
	return strings.Join(keep, "\n")
}

func main() {
	t := vhelp.New()

	// --- 1. the named-curve table gains an arc, and keeps the old four -----
	// Pinned as dotted strings so a renumbering of the existing curves is a
	// failure too, not just a missing P-192.
	t.Equal("named-curve-oids-include-p192",
		fmt.Sprintf("p192=%v p224=%v p256=%v p384=%v p521=%v",
			x509.OIDNamedCurveP192, x509.OIDNamedCurveP224,
			x509.OIDNamedCurveP256, x509.OIDNamedCurveP384, x509.OIDNamedCurveP521),
		"p192=1.2.840.10045.3.1.1 p224=1.3.132.0.33 p256=1.2.840.10045.3.1.7 "+
			"p384=1.3.132.0.34 p521=1.3.132.0.35")

	// --- 2. a P-192 certificate now parses, non-fatally, into an EC key ----
	blk, _ := pem.Decode(vhelp.ReadFixture("malformed", "xf-pubkey-ecdsa-secp192r1.pem"))
	if blk == nil {
		t.Fatalf("fixture malformed/xf-pubkey-ecdsa-secp192r1.pem is not a PEM block")
	}
	cert192, err192 := x509.ParseCertificate(blk.Bytes)
	if cert192 == nil {
		t.Fatalf("secp192r1 fixture: no certificate returned (err=%v)", err192)
	}
	pub192 := ecKey(t, cert192, "secp192r1 fixture")
	params := pub192.Curve.Params()
	t.Equal("secp192r1-certificate-parses-to-a-p192-ecdsa-key",
		fmt.Sprintf("fatal=%v diagnostic=%v alg=%v name=%q bits=%d",
			x509.IsFatal(err192), err192 != nil, cert192.PublicKeyAlgorithm,
			params.Name, params.BitSize),
		`fatal=false diagnostic=true alg=ECDSA name="P-192" bits=192`)

	// --- 3. the curve really is secp192r1, not a relabelled P-256 ----------
	t.Equal("p192-domain-parameters-match-sec2-2-2-2",
		fmt.Sprintf("P=%X\nN=%X\nB=%X\nGx=%X\nGy=%X", params.P, params.N, params.B, params.Gx, params.Gy),
		fmt.Sprintf("P=%s\nN=%s\nB=%s\nGx=%s\nGy=%s", wantP, wantN, wantB, wantGx, wantGy))

	// --- 4. and the decoded point is the fixture's, and lies on it ---------
	t.Equal("p192-public-point-is-the-fixtures-and-is-on-the-curve",
		fmt.Sprintf("X=%X Y=%X onCurve=%v", pub192.X, pub192.Y, pub192.Curve.IsOnCurve(pub192.X, pub192.Y)),
		fmt.Sprintf("X=%s Y=%s onCurve=true", wantX, wantY))

	// --- 5. re-encoding the key reproduces the certificate's own SPKI ------
	// Byte identity with the DER the parse consumed is the strongest
	// statement that nothing was lost or invented on the way in.
	der192, merr := x509.MarshalPKIXPublicKey(pub192)
	t.Check("p192-key-re-encodes-to-the-certificates-own-spki",
		merr == nil && fmt.Sprintf("%x", der192) == wantSPKI && bytes.Equal(der192, cert192.RawSubjectPublicKeyInfo),
		"err=%v got=%x want=%s matchesCert=%v", merr, der192, wantSPKI, bytes.Equal(der192, cert192.RawSubjectPublicKeyInfo))

	// --- 6. the reverse lookup knows the curve too -------------------------
	oid192, ok192 := x509.OIDFromNamedCurve(pub192.Curve)
	t.Equal("oid-from-named-curve-maps-p192-back",
		fmt.Sprintf("oid=%v ok=%v", oid192, ok192),
		"oid=1.2.840.10045.3.1.1 ok=true")

	// --- 7. the asymmetry: a bare P-192 SPKI is still refused --------------
	// ParsePKIXPublicKey has no non-fatal channel, so the insecure curve
	// remains fatal there and no key comes back -- the opposite of case 2 on
	// the very same bytes.
	key, perr := x509.ParsePKIXPublicKey(der192)
	t.Equal("standalone-spki-parse-still-refuses-the-insecure-curve",
		fmt.Sprintf("fatal=%v key=%v", x509.IsFatal(perr), key != nil),
		"fatal=true key=false")

	// --- 8. and the curve has a name in the human rendering ----------------
	t.Equal("x509util-renders-the-curve-by-name",
		spkiLines(x509util.CertificateToString(cert192)),
		"Public Key: (192 bit)\n"+
			"ASN1 OID: secp192r1")

	// --- 9..10. control: the supported curves are untouched ----------------
	// bench-root is a minted P-256 CA. It must still parse with no
	// diagnostic at all, and -- unlike P-192 -- its SPKI must still parse
	// standalone, so "refuses" is not simply the answer for every key.
	root, rerr := x509.ParseCertificate(vhelp.ReadFixture("minted", "bench-root.der"))
	if root == nil {
		t.Fatalf("minted/bench-root.der: no certificate returned (err=%v)", rerr)
	}
	rootKey := ecKey(t, root, "bench-root")
	rootOID, rootOK := x509.OIDFromNamedCurve(rootKey.Curve)
	t.Equal("p256-certificate-is-unaffected",
		fmt.Sprintf("diagnostic=%v fatal=%v name=%q bits=%d oid=%v ok=%v",
			rerr != nil, x509.IsFatal(rerr), rootKey.Curve.Params().Name,
			rootKey.Curve.Params().BitSize, rootOID, rootOK),
		`diagnostic=false fatal=false name="P-256" bits=256 oid=1.2.840.10045.3.1.7 ok=true`)

	rootDER, rmerr := x509.MarshalPKIXPublicKey(rootKey)
	rootBack, rperr := x509.ParsePKIXPublicKey(rootDER)
	sameKey := false
	if back, isEC := rootBack.(*ecdsa.PublicKey); isEC {
		sameKey = back.X.Cmp(rootKey.X) == 0 && back.Y.Cmp(rootKey.Y) == 0 &&
			back.Curve.Params().Name == "P-256"
	}
	t.Equal("p256-spki-still-parses-standalone",
		fmt.Sprintf("marshal=%v parse=%v same=%v", rmerr == nil, rperr == nil, sameKey),
		"marshal=true parse=true same=true")

	t.Done()
}
