// Group x509-go114-merges.
//
// v1.1.2's fork header records "Merged upstream Go 1.13 and Go 1.14 changes".
// Three of those changes are observable through the exported boundary without
// naming any new symbol, so this group is graded on behaviour alone:
//
//   - FIPS 186-3 section 4.6: a DSA digest longer than the subgroup order is
//     truncated before verification. crypto/dsa does not do this, so the fork
//     must. A 1024-bit DSA key with a SHA-256 signature is exactly that case.
//   - Certificate.Equal is nil-safe.
//   - RFC 8410 section 3, applied to PKCS#8: id-Ed25519 with algorithm
//     parameters present is not a valid private key.
//
// The DSA vectors live in this group's own data/ directory; their provenance
// is recorded in data/PROVENANCE.txt.
package main

import (
	"bytes"
	"encoding/hex"
	"encoding/pem"
	"fmt"
	"os"
	"strings"

	"github.com/google/certificate-transparency-go/x509"

	"ctverify/vhelp"
)

func groupBytes(t *vhelp.T, name string) []byte {
	b, err := os.ReadFile(vhelp.GroupFile("data", name))
	if err != nil {
		t.Fatalf("read %s: %v", name, err)
	}
	return b
}

// The pinned Ed25519 seed; only its length and its position in the DER matter.
var seed = []byte{
	0x9d, 0x61, 0xb1, 0x9d, 0xef, 0xfd, 0x5a, 0x60, 0xba, 0x84, 0x4a, 0xf4, 0x92, 0xec, 0x2c, 0xc4,
	0x44, 0x49, 0xc5, 0x69, 0x7b, 0x32, 0x69, 0x19, 0x70, 0x3b, 0xac, 0x03, 0x1c, 0xae, 0x7f, 0x60,
}

// pkcs8Ed25519 builds an RFC 8410 section 7 PrivateKeyInfo. When withParams is
// true a NULL is inserted into the AlgorithmIdentifier, which RFC 8410 section
// 3 forbids.
func pkcs8Ed25519(withParams bool) []byte {
	algo := []byte{0x30, 0x05, 0x06, 0x03, 0x2b, 0x65, 0x70}
	if withParams {
		algo = []byte{0x30, 0x07, 0x06, 0x03, 0x2b, 0x65, 0x70, 0x05, 0x00}
	}
	priv := append([]byte{0x04, 0x22, 0x04, 0x20}, seed...)
	body := append([]byte{0x02, 0x01, 0x00}, algo...)
	body = append(body, priv...)
	return append([]byte{0x30, byte(len(body))}, body...)
}

func main() {
	t := vhelp.New()

	// ---- DSA hash truncation -------------------------------------------
	block, _ := pem.Decode(groupBytes(t, "dsa-1024-sha256.pem"))
	if block == nil {
		t.Fatalf("pinned DSA certificate is not PEM")
	}
	cert, err := x509.ParseCertificate(block.Bytes)
	if err != nil && x509.IsFatal(err) {
		t.Fatalf("ParseCertificate(pinned DSA cert): %v", err)
	}

	// 1. The certificate is what the vector claims: a DSA key signed with
	//    dsa-with-sha256, i.e. a 32-byte digest against a 160-bit subgroup.
	t.Equal("dsa-vector-is-sha256-over-dsa",
		cert.PublicKeyAlgorithm.String()+"/"+cert.SignatureAlgorithm.String(),
		"DSA/DSA-SHA256")

	// 2. Its own self-signature only verifies if the digest is truncated.
	selfErr := cert.CheckSignatureFrom(cert)
	t.Check("dsa-selfsignature-verifies", selfErr == nil,
		"CheckSignatureFrom on the pinned DSA cert: %v", selfErr)

	// 3. So does the pinned detached signature over the pinned message.
	sig, err := hex.DecodeString(strings.TrimSpace(string(groupBytes(t, "dsa-1024-sha256.sig.hex"))))
	if err != nil {
		t.Fatalf("bad pinned signature hex: %v", err)
	}
	detErr := cert.CheckSignature(x509.DSAWithSHA256, groupBytes(t, "dsa-1024-sha256.msg"), sig)
	t.Check("dsa-detached-signature-verifies", detErr == nil,
		"CheckSignature(DSAWithSHA256, ...): %v", detErr)

	// 4. A digest that is already short enough is unaffected, and a corrupted
	//    message is still rejected: truncation is not a weakening.
	corrupt := append([]byte(nil), groupBytes(t, "dsa-1024-sha256.msg")...)
	corrupt[0] ^= 0x01
	t.Check("dsa-corrupt-message-rejected",
		cert.CheckSignature(x509.DSAWithSHA256, corrupt, sig) != nil,
		"CheckSignature accepted a corrupted message")

	// ---- PKCS#8 Ed25519 parameter strictness ---------------------------

	// 5. The well-formed key parses back to the pinned seed.
	good, err := x509.ParsePKCS8PrivateKey(pkcs8Ed25519(false))
	if err != nil {
		t.Check("pkcs8-ed25519-parses", false, "ParsePKCS8PrivateKey: %v", err)
	} else {
		type seeder interface{ Seed() []byte }
		s, ok := good.(seeder)
		t.Check("pkcs8-ed25519-parses", ok && bytes.Equal(s.Seed(), seed),
			"got %T", good)
	}

	// 6. The same key with parameters present is rejected.
	withParams, err := x509.ParsePKCS8PrivateKey(pkcs8Ed25519(true))
	t.Check("pkcs8-ed25519-parameters-rejected", err != nil,
		"ParsePKCS8PrivateKey accepted id-Ed25519 with parameters: %T", withParams)

	// ---- nil-safe Certificate.Equal ------------------------------------

	// 7. None of the four nil combinations may panic; two nils compare equal and
	//    a nil never equals a real certificate.
	func() {
		defer func() {
			if r := recover(); r != nil {
				t.Check("certificate-equal-nil-safe", false, "panicked: %v", r)
			}
		}()
		var a, b *x509.Certificate
		t.Equal("certificate-equal-nil-safe",
			fmt.Sprintf("%v%v%v%v", a.Equal(b), a.Equal(cert), cert.Equal(a), cert.Equal(cert)),
			"truefalsefalsetrue")
	}()

	t.Done()
}
