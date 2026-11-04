// Group x509util-pem-parsing.
//
// v1.0.13 changes x509util.CertificateFromPEM to take []byte instead of a
// string, and adds x509util.CertificatesFromPEM for whole concatenated
// chains. The two differ deliberately in how strict they are about anything
// that follows the PEM data: CertificateFromPEM rejects trailing bytes,
// CertificatesFromPEM stops at the first thing that is not a PEM block.
//
// Boundary: the exported x509util functions, on pinned PEM fixtures. Each
// parse is checked against the DER the stdlib's own pem.Decode yields, so the
// assertion is byte-exact and does not restate the product's answer.
package main

import (
	"bytes"
	"encoding/pem"

	"github.com/google/certificate-transparency-go/x509util"

	"ctverify/vhelp"
)

func main() {
	t := vhelp.New()

	leafPEM := vhelp.ReadFixture("rfc6962", "test-cert.pem")
	caPEM := vhelp.ReadFixture("rfc6962", "ca-cert.pem")
	keyPEM := vhelp.ReadFixture("keys", "log-public-key.pem")

	// The DER the stdlib sees, used as the expected value throughout.
	leafBlock, leafRest := pem.Decode(leafPEM)
	caBlock, _ := pem.Decode(caPEM)
	if leafBlock == nil || caBlock == nil {
		t.Fatalf("pinned PEM fixtures did not decode")
	}

	// --- CertificateFromPEM --------------------------------------------------
	cert, err := x509util.CertificateFromPEM(leafPEM)
	if err != nil {
		t.Fatalf("CertificateFromPEM of the pinned leaf: %v", err)
	}
	t.Check("certificate-from-pem-accepts-a-byte-slice-and-parses-the-der",
		bytes.Equal(cert.Raw, leafBlock.Bytes) &&
			cert.Subject.Organization[0] == "Certificate Transparency" &&
			cert.SerialNumber.Int64() == 6 && len(leafRest) == 0,
		"raw DER mismatch, or subject=%v serial=%v", cert.Subject.Organization, cert.SerialNumber)

	_, err = x509util.CertificateFromPEM(append(append([]byte{}, leafPEM...), []byte("trailing junk\n")...))
	t.Check("certificate-from-pem-rejects-trailing-data", err != nil,
		"accepted PEM with bytes after the end of the block")

	_, err = x509util.CertificateFromPEM(keyPEM)
	t.Check("certificate-from-pem-rejects-a-block-that-is-not-a-certificate", err != nil,
		"accepted a PUBLIC KEY PEM block as a certificate")

	_, err = x509util.CertificateFromPEM([]byte{})
	t.Check("certificate-from-pem-rejects-input-with-no-pem-block", err != nil,
		"accepted empty input")

	// --- CertificatesFromPEM -------------------------------------------------
	chainPEM := concat(leafPEM, []byte("\n"), caPEM)
	chain, err := x509util.CertificatesFromPEM(chainPEM)
	if err != nil {
		t.Fatalf("CertificatesFromPEM of the pinned chain: %v", err)
	}
	t.Check("certificates-from-pem-parses-every-block-in-a-chain",
		len(chain) == 2 && bytes.Equal(chain[0].Raw, leafBlock.Bytes) &&
			bytes.Equal(chain[1].Raw, caBlock.Bytes),
		"got %d certificates, want the pinned leaf then the pinned CA", len(chain))

	t.Check("certificates-from-pem-keeps-the-order-of-the-file",
		len(chain) == 2 &&
			chain[0].Subject.Organization[0] == "Certificate Transparency" &&
			chain[1].Subject.Organization[0] == "Certificate Transparency CA" &&
			bytes.Equal(chain[0].RawIssuer, chain[1].RawSubject),
		"chain came back in the wrong order")

	empty, err := x509util.CertificatesFromPEM(nil)
	t.Check("certificates-from-pem-on-input-with-no-blocks-is-an-empty-chain",
		err == nil && len(empty) == 0, "got %d certificates, err %v", len(empty), err)

	// Unlike CertificateFromPEM, this one simply stops at the first byte that
	// is not the start of a PEM block.
	stopped, err := x509util.CertificatesFromPEM(concat(chainPEM, []byte("\nnot a pem block at all\n")))
	t.Check("certificates-from-pem-stops-at-trailing-non-pem-text",
		err == nil && len(stopped) == 2 && bytes.Equal(stopped[0].Raw, leafBlock.Bytes),
		"got %d certificates, err %v", len(stopped), err)

	_, err = x509util.CertificatesFromPEM(concat(leafPEM, []byte("\n"), keyPEM))
	t.Check("certificates-from-pem-rejects-a-non-certificate-block", err != nil,
		"accepted a chain with a PUBLIC KEY block in it")

	corrupt := pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: []byte("not a DER certificate")})
	_, err = x509util.CertificatesFromPEM(concat(leafPEM, []byte("\n"), corrupt))
	t.Check("certificates-from-pem-rejects-an-unparsable-certificate-block", err != nil,
		"accepted a CERTIFICATE block whose contents are not DER")

	t.Done()
}

func concat(parts ...[]byte) []byte {
	var out []byte
	for _, p := range parts {
		out = append(out, p...)
	}
	return out
}
