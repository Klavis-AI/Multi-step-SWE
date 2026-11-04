// Group inherited-rfc6962-leaf-hash.
//
// Inherited obligation from steps 1 and 4: the RFC 6962 Merkle tree leaf hash
// and the ctutil entry points that compute it. Every answer here is pinned in
// the shared known-answer table, so this group is a regression fence, not a
// new-capability probe -- it is expected to pass at the previous milestone too.
package main

import (
	"encoding/base64"
	"encoding/hex"
	"encoding/pem"
	"strings"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/ctutil"
	"github.com/google/certificate-transparency-go/tls"
	"github.com/google/certificate-transparency-go/x509"

	"ctverify/vhelp"
)

func kat(t *vhelp.T) map[string]string {
	out := map[string]string{}
	for _, line := range strings.Split(string(vhelp.ReadFixture("rfc6962", "kat.txt")), "\n") {
		if i := strings.IndexByte(line, ' '); i > 0 {
			out[line[:i]] = strings.TrimSpace(line[i+1:])
		}
	}
	return out
}

func cert(t *vhelp.T, name string) *x509.Certificate {
	block, _ := pem.Decode(vhelp.ReadFixture("rfc6962", name))
	if block == nil {
		t.Fatalf("fixture %s is not PEM", name)
	}
	c, err := x509.ParseCertificate(block.Bytes)
	if err != nil && x509.IsFatal(err) {
		t.Fatalf("ParseCertificate(%s): %v", name, err)
	}
	return c
}

// sctFromProof decodes a wire-format SignedCertificateTimestamp.
func sctFromProof(t *vhelp.T, hexed string) *ct.SignedCertificateTimestamp {
	raw, err := hex.DecodeString(hexed)
	if err != nil {
		t.Fatalf("bad pinned proof hex: %v", err)
	}
	var sct ct.SignedCertificateTimestamp
	rest, err := tls.Unmarshal(raw, &sct)
	if err != nil {
		t.Fatalf("tls.Unmarshal(proof): %v", err)
	}
	if len(rest) != 0 {
		t.Fatalf("pinned proof has %d trailing bytes", len(rest))
	}
	return &sct
}

func main() {
	t := vhelp.New()
	k := kat(t)

	leaf := cert(t, "test-cert.pem")
	pre := cert(t, "test-pre-cert.pem")
	ca := cert(t, "ca-cert.pem")
	embedded := cert(t, "test-embedded-cert.pem")

	certSCT := sctFromProof(t, k["TestCertProof"])
	preSCT := sctFromProof(t, k["TestPreCertProof"])

	// 1. The leaf hash of a plain X.509 entry.
	got, err := ctutil.LeafHashB64([]*x509.Certificate{leaf}, certSCT, false)
	if err != nil {
		t.Check("x509-leaf-hash-b64", false, "LeafHashB64: %v", err)
	} else {
		t.Equal("x509-leaf-hash-b64", got, k["TestCertB64LeafHash"])
	}

	// 2. The leaf hash of a precertificate entry, which needs the issuer.
	gotPre, err := ctutil.LeafHashB64([]*x509.Certificate{pre, ca}, preSCT, false)
	if err != nil {
		t.Check("precert-leaf-hash-b64", false, "LeafHashB64: %v", err)
	} else {
		t.Equal("precert-leaf-hash-b64", gotPre, k["TestPreCertB64LeafHash"])
	}

	// 3. The raw form is the same 32 bytes.
	raw, err := ctutil.LeafHash([]*x509.Certificate{leaf}, certSCT, false)
	if err != nil {
		t.Check("leaf-hash-bytes-match-b64", false, "LeafHash: %v", err)
	} else {
		want, derr := base64.StdEncoding.DecodeString(k["TestCertB64LeafHash"])
		if derr != nil {
			t.Fatalf("bad pinned leaf hash: %v", derr)
		}
		t.EqualBytes("leaf-hash-bytes-match-b64", raw[:], want)
	}

	// 4. A precertificate on its own cannot be hashed: rebuilding the TBS
	//    needs the issuer.
	_, err = ctutil.LeafHashB64([]*x509.Certificate{pre}, preSCT, false)
	t.Check("precert-without-issuer-rejected", err != nil,
		"LeafHashB64 accepted a precertificate with no issuer in the chain")

	// 5. A certificate carrying an embedded SCT hashes to the precertificate
	//    entry it was issued from, once the poison is put back.
	gotEmb, err := ctutil.LeafHashB64([]*x509.Certificate{embedded, ca}, preSCT, true)
	if err != nil {
		t.Check("embedded-sct-leaf-hash", false, "LeafHashB64(embedded): %v", err)
	} else {
		t.Equal("embedded-sct-leaf-hash", gotEmb, k["TestPreCertB64LeafHash"])
	}

	t.Done()
}
