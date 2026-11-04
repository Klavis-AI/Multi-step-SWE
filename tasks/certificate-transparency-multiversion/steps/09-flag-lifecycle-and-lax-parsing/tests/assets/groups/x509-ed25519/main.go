// Group x509-ed25519.
//
// v1.1.2 merges the upstream Go Ed25519 work into the forked ./x509: the
// PureEd25519 signature algorithm and the Ed25519 public-key algorithm,
// SubjectPublicKeyInfo marshalling and parsing for id-Ed25519 (1.3.101.112),
// certificate creation and signature checking with no pre-hash, and the
// RFC 8410 section 3 rule that the algorithm parameters MUST be absent.
//
// Everything is derived from one pinned 32-byte seed, so Ed25519's determinism
// makes every byte below a fixed known answer.
package main

import (
	"crypto/ed25519"
	"crypto/rand"
	"crypto/sha256"
	"encoding/hex"
	"math/big"
	"time"

	"github.com/google/certificate-transparency-go/asn1"
	"github.com/google/certificate-transparency-go/x509"
	"github.com/google/certificate-transparency-go/x509/pkix"

	"ctverify/vhelp"
)

// The pinned seed. RFC 8032 makes the key pair, the SPKI and every signature a
// deterministic function of it.
const seedHex = "1c1f1b6b19d1fd3ba1ac0d41a1a7a0a0f0f9e6b1c4d7e0f3a6b9ccdf0215283b"

var (
	notBefore = time.Date(2020, time.January, 1, 0, 0, 0, 0, time.UTC)
	notAfter  = time.Date(2030, time.January, 1, 0, 0, 0, 0, time.UTC)
)

func main() {
	t := vhelp.New()

	seed, err := hex.DecodeString(seedHex)
	if err != nil || len(seed) != ed25519.SeedSize {
		t.Fatalf("bad pinned seed")
	}
	priv := ed25519.NewKeyFromSeed(seed)
	pub := priv.Public().(ed25519.PublicKey)

	// 1. The two new enumeration members render under their RFC names.
	t.Equal("ed25519-algorithm-names",
		x509.PureEd25519.String()+"/"+x509.Ed25519.String(), "Ed25519/Ed25519")

	// 2. id-Ed25519 with absent parameters maps to PureEd25519.
	t.Equal("signature-algorithm-from-ai-ed25519",
		x509.SignatureAlgorithmFromAI(pkix.AlgorithmIdentifier{
			Algorithm: asn1.ObjectIdentifier{1, 3, 101, 112},
		}), x509.PureEd25519)

	// 3. RFC 8410 section 3: with parameters present it is not a known
	//    algorithm, even though the OID is recognised.
	nullParams := asn1.RawValue{FullBytes: []byte{0x05, 0x00}}
	t.Equal("ed25519-ai-with-parameters-rejected",
		x509.SignatureAlgorithmFromAI(pkix.AlgorithmIdentifier{
			Algorithm:  asn1.ObjectIdentifier{1, 3, 101, 112},
			Parameters: nullParams,
		}), x509.UnknownSignatureAlgorithm)

	// 4. The SubjectPublicKeyInfo is exactly the RFC 8410 encoding: a 42-byte
	//    SEQUENCE carrying the bare OID and a 33-byte BIT STRING.
	spki, err := x509.MarshalPKIXPublicKey(pub)
	if err != nil {
		t.Check("marshal-pkix-ed25519-exact", false, "MarshalPKIXPublicKey: %v", err)
	} else {
		t.Equal("marshal-pkix-ed25519-exact", hex.EncodeToString(spki),
			"302a300506032b6570032100"+hex.EncodeToString(pub))
	}

	// 5. Parsing that SPKI returns the same key as an ed25519.PublicKey.
	got, err := x509.ParsePKIXPublicKey(spki)
	if err != nil {
		t.Check("parse-pkix-ed25519-roundtrip", false, "ParsePKIXPublicKey: %v", err)
	} else if gp, ok := got.(ed25519.PublicKey); !ok {
		t.Check("parse-pkix-ed25519-roundtrip", false, "got %T, want ed25519.PublicKey", got)
	} else {
		t.EqualBytes("parse-pkix-ed25519-roundtrip", gp, pub)
	}

	// 6. A self-signed Ed25519 certificate parses with both algorithm fields
	//    set from the OID alone -- there is no hash to negotiate.
	tmpl := &x509.Certificate{
		SerialNumber:          big.NewInt(0x5ed25519),
		Subject:               pkix.Name{CommonName: "harbor ed25519 leaf"},
		NotBefore:             notBefore,
		NotAfter:              notAfter,
		KeyUsage:              x509.KeyUsageCertSign | x509.KeyUsageDigitalSignature,
		BasicConstraintsValid: true,
		IsCA:                  true,
	}
	der, err := x509.CreateCertificate(rand.Reader, tmpl, tmpl, pub, priv)
	if err != nil {
		t.Fatalf("CreateCertificate: %v", err)
	}
	cert, err := x509.ParseCertificate(der)
	if err != nil && x509.IsFatal(err) {
		t.Fatalf("ParseCertificate: %v", err)
	}
	t.Equal("selfsigned-ed25519-cert-parses",
		cert.PublicKeyAlgorithm.String()+"/"+cert.SignatureAlgorithm.String()+"/"+
			hex.EncodeToString(sha256Of(cert.RawSubjectPublicKeyInfo)),
		"Ed25519/Ed25519/"+hex.EncodeToString(sha256Of(spki)))

	// 7. The signature checks out against the certificate itself, and stops
	//    checking out as soon as one TBS byte moves.
	self := cert.CheckSignatureFrom(cert)
	bad := cert.CheckSignature(x509.PureEd25519, flip(cert.RawTBSCertificate), cert.Signature)
	t.Check("selfsigned-ed25519-signature-checks", self == nil && bad != nil,
		"CheckSignatureFrom=%v tampered=%v", self, bad)

	t.Done()
}

func sha256Of(b []byte) []byte {
	s := sha256.Sum256(b)
	return s[:]
}

func flip(b []byte) []byte {
	out := append([]byte(nil), b...)
	out[len(out)-1] ^= 0x01
	return out
}
