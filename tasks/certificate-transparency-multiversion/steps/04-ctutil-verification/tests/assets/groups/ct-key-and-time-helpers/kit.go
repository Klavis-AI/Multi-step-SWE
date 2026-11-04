// kit.go is shared source, not a group. It is copied verbatim into every
// step-04 driver that needs it, so each group stays exactly one compilation
// unit while the fixture plumbing is written once.
//
// Everything here uses only API that exists at BOTH v1.0.10 and v1.0.13, so an
// inherited-* group built on it still compiles against the previous milestone.
// Nothing here holds a case or prints a line.
package main

import (
	"encoding/hex"
	"encoding/pem"
	"strings"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/tls"
	"github.com/google/certificate-transparency-go/x509"

	"ctverify/vhelp"
)

// katFile parses one of the pinned "<name> <value>" known-answer files.
func katFile(rel ...string) map[string]string {
	m := map[string]string{}
	for _, ln := range strings.Split(string(vhelp.ReadFixture(rel...)), "\n") {
		f := strings.Fields(ln)
		if len(f) == 2 {
			m[f[0]] = f[1]
		}
	}
	return m
}

// kat returns a known answer or dies: a missing constant is a corpus fault,
// never a silent skip.
func kat(t *vhelp.T, m map[string]string, name string) string {
	v, ok := m[name]
	if !ok || v == "" {
		t.Fatalf("known answer %q missing from the pinned corpus", name)
	}
	return v
}

func mustHex(t *vhelp.T, s string) []byte {
	b, err := hex.DecodeString(s)
	if err != nil {
		t.Fatalf("corpus hex %q: %v", s, err)
	}
	return b
}

// pemCerts decodes every CERTIFICATE block in b with the product's own x509
// fork. It deliberately does not use x509util, which changes shape at this
// milestone, so the same helper serves both trees.
func pemCerts(t *vhelp.T, b []byte) []*x509.Certificate {
	var out []*x509.Certificate
	rest := b
	for {
		var blk *pem.Block
		blk, rest = pem.Decode(rest)
		if blk == nil {
			return out
		}
		if blk.Type != "CERTIFICATE" {
			t.Fatalf("fixture holds a %q PEM block", blk.Type)
		}
		c, err := x509.ParseCertificate(blk.Bytes)
		if c == nil {
			t.Fatalf("fixture certificate did not parse: %v", err)
		}
		out = append(out, c)
	}
}

// pemCert decodes exactly one certificate.
func pemCert(t *vhelp.T, b []byte) *x509.Certificate {
	cs := pemCerts(t, b)
	if len(cs) != 1 {
		t.Fatalf("want 1 certificate in fixture, got %d", len(cs))
	}
	return cs[0]
}

// parseSCT TLS-decodes a pinned hex SCT.
func parseSCT(t *vhelp.T, hexSCT string) *ct.SignedCertificateTimestamp {
	var sct ct.SignedCertificateTimestamp
	rest, err := tls.Unmarshal(mustHex(t, hexSCT), &sct)
	if err != nil {
		t.Fatalf("pinned SCT did not TLS-decode: %v", err)
	}
	if len(rest) != 0 {
		t.Fatalf("pinned SCT has %d trailing bytes", len(rest))
	}
	return &sct
}
