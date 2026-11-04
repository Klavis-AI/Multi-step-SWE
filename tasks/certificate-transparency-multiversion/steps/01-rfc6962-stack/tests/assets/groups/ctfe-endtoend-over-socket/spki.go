package main

import (
	"crypto/sha256"

	"github.com/google/certificate-transparency-go/x509"

	"ctverify/vhelp"
)

func sha256Of(b []byte) []byte {
	h := sha256.Sum256(b)
	return h[:]
}

// spkiOf returns the DER SubjectPublicKeyInfo of a DER certificate.
func spkiOf(t *vhelp.T, der []byte) []byte {
	c, err := x509.ParseCertificate(der)
	if c == nil {
		t.Fatalf("parse issuer certificate: %v", err)
	}
	return c.RawSubjectPublicKeyInfo
}
