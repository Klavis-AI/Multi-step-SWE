// Group x509-san-parse-relaxed.
//
// v1.0.17 merges Go 1.10.1's x509 changes (commit ca5c0730). One of them moves
// the syntax check on subjectAltName entries out of the parser: v1.0.13's
// parseSANExtension rejects the whole certificate when a dNSName does not
// survive domainToReverseLabels or an rfc822Name does not parse as an RFC 2821
// mailbox, while v1.0.17 stores the raw strings and defers the complaint to
// name-constraint checking. The doc comment on Certificate.DNSNames changes in
// the same commit to say exactly that: the values "may not be valid".
//
// Observed at the parser boundary, on a certificate minted here from a pinned
// fixture key with a hand-encoded subjectAltName extension. It is built in the
// driver rather than shipped because the shared corpus has no certificate with
// a syntactically invalid SAN; every input to it is a compile-time constant
// (serial, validity window, the two SAN strings) and the key comes from
// $CT_FIXTURES, so nothing here reads a clock or the network.
//
// The relaxation is graded at its strong reading, which is the one the code
// takes: the malformed name is not demoted to a NonFatalErrors, it is not
// reported at all, so case 1 requires a nil error and not merely a non-fatal
// one. TEST_INTERFACE.md section 5, the x509 row, states that distinction; it
// matters because X509-FORK.md section 3.1 uses "tolerated" for both.
//
// This group covers only the parser half of the change. The check it removes
// does not disappear -- it moves into Verify's name-constraint walk, and the
// group x509-verify-disable-switches grades that half: a chain whose leaf
// carries a malformed dNSName is refused under an issuer with name
// constraints, and admitted again once DisableNameConstraintChecks is set.
//
// No new identifier is involved: this driver compiles against v1.0.13 too,
// where ParseCertificate refuses the certificate and the group scores zero.
package main

import (
	"crypto/ecdsa"
	"crypto/rand"
	stdx509 "crypto/x509"
	"crypto/x509/pkix"
	"encoding/asn1"
	"encoding/pem"
	"math/big"
	"strings"
	"time"

	"github.com/google/certificate-transparency-go/x509"

	"ctverify/vhelp"
)

// Pinned inputs. The SAN holds one dNSName with a trailing empty label and one
// rfc822Name with no '@': both are refused by v1.0.13's parser and accepted
// verbatim by v1.0.17's.
const (
	badDNSName = "bench.invalid."
	badEmail   = "no-at-sign"
)

var (
	sanNotBefore = time.Date(2020, 1, 1, 0, 0, 0, 0, time.UTC)
	sanNotAfter  = time.Date(2060, 1, 1, 0, 0, 0, 0, time.UTC)
	oidSAN       = asn1.ObjectIdentifier{2, 5, 29, 17}
)

// sanExtension hand-encodes GeneralNames{ dNSName, rfc822Name } so that the
// standard library's own SAN marshaller never gets a chance to sanitise it.
func sanExtension() []byte {
	var body []byte
	body = append(body, 0x82, byte(len(badDNSName)))
	body = append(body, badDNSName...)
	body = append(body, 0x81, byte(len(badEmail)))
	body = append(body, badEmail...)
	out := []byte{0x30, byte(len(body))}
	return append(out, body...)
}

func main() {
	t := vhelp.New()

	blk, _ := pem.Decode(vhelp.ReadFixture("minted", "keys", "leaf-key.pem"))
	if blk == nil {
		t.Fatalf("minted/keys/leaf-key.pem is not PEM")
	}
	anyKey, err := stdx509.ParsePKCS8PrivateKey(blk.Bytes)
	if err != nil {
		t.Fatalf("parse pinned key: %v", err)
	}
	key, ok := anyKey.(*ecdsa.PrivateKey)
	if !ok {
		t.Fatalf("pinned key is %T, want *ecdsa.PrivateKey", anyKey)
	}

	tmpl := &stdx509.Certificate{
		SerialNumber:          big.NewInt(0x5a17),
		Subject:               pkix.Name{CommonName: "Harbor CT Bench Bad SAN"},
		NotBefore:             sanNotBefore,
		NotAfter:              sanNotAfter,
		KeyUsage:              stdx509.KeyUsageDigitalSignature,
		BasicConstraintsValid: true,
		SignatureAlgorithm:    stdx509.ECDSAWithSHA256,
		ExtraExtensions: []pkix.Extension{{
			Id:    oidSAN,
			Value: sanExtension(),
		}},
	}
	der, err := stdx509.CreateCertificate(rand.Reader, tmpl, tmpl, &key.PublicKey, key)
	if err != nil {
		t.Fatalf("mint bad-SAN certificate: %v", err)
	}

	// --- 1. the parser no longer refuses it --------------------------------
	cert, err := x509.ParseCertificate(der)
	if err != nil {
		t.Check("bad-san-certificate-parses", false, "ParseCertificate: %v", err)
	} else {
		t.Check("bad-san-certificate-parses", true, "")
	}
	if cert == nil {
		// Nothing further is observable; report the remaining cases honestly.
		t.Check("bad-dnsname-stored-verbatim", false, "no certificate to inspect")
		t.Check("bad-email-stored-verbatim", false, "no certificate to inspect")
	} else {
		// --- 2 and 3. and keeps the raw strings ----------------------------
		t.Equal("bad-dnsname-stored-verbatim", strings.Join(cert.DNSNames, ","), badDNSName)
		t.Equal("bad-email-stored-verbatim", strings.Join(cert.EmailAddresses, ","), badEmail)
	}

	// --- 4. control: a well-formed SAN is unaffected ------------------------
	good, err := x509.ParseCertificate(vhelp.ReadFixture("minted", "leaf-valid.der"))
	if err != nil {
		t.Check("well-formed-san-unchanged", false, "ParseCertificate: %v", err)
	} else {
		t.Equal("well-formed-san-unchanged", strings.Join(good.DNSNames, ","), "valid.bench.invalid")
	}

	t.Done()
}
