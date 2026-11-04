// Group inherited-leaf-hash-known-answers (step 1 boundary ct-leaf-hash-known-answers, re-run unchanged at step 2).
//
// RFC 6962 s2.1: the leaf hash of a log entry is SHA-256 over 0x00 followed by
// the TLS-encoded MerkleTreeLeaf. This group pins the two published answers for
// the RFC 6962 reference certificate and reference precertificate, and the
// exact MerkleTreeLeaf encodings that produce them.
//
// Boundary: the root package's exported serialisation API
// (MerkleTreeLeafFromChain, tls.Marshal/Unmarshal) plus the x509 fork's
// ParseCertificate. Nothing private is touched and no error type is graded.
package main

import (
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"
	"encoding/pem"
	"errors"
	"fmt"
	"strings"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/tls"
	"github.com/google/certificate-transparency-go/x509"

	"ctverify/vhelp"
)

func katMap() map[string]string {
	m := map[string]string{}
	for _, ln := range strings.Split(string(vhelp.ReadFixture("rfc6962", "kat.txt")), "\n") {
		f := strings.Fields(ln)
		if len(f) == 2 {
			m[f[0]] = f[1]
		}
	}
	return m
}

func mustHex(t *vhelp.T, s string) []byte {
	b, err := hex.DecodeString(s)
	if err != nil {
		t.Fatalf("corpus hex: %v", err)
	}
	return b
}

func loadCert(t *vhelp.T, name string) *x509.Certificate {
	blk, _ := pem.Decode(vhelp.ReadFixture("rfc6962", name))
	if blk == nil {
		t.Fatalf("no PEM block in %s", name)
	}
	c, err := x509.ParseCertificate(blk.Bytes)
	if c == nil {
		t.Fatalf("%s did not parse: %v", name, err)
	}
	return c
}

// leafHash is RFC 6962's MTH of a single leaf: SHA-256(0x00 || leaf).
func leafHash(leaf []byte) string {
	h := sha256.Sum256(append([]byte{0x00}, leaf...))
	return base64.StdEncoding.EncodeToString(h[:])
}

// The TLS encoding of the two reference MerkleTreeLeaf structures, written out
// by hand from RFC 6962 s3.4 rather than copied from the product:
//
//	version(1) leaf_type(1) timestamp(8) entry_type(2) body extensions(2)
//
// for the certificate entry the body is uint24-length-prefixed DER; for the
// precertificate entry it is issuer_key_hash(32) then uint24-prefixed TBS.
func rebuildCertLeaf(ts uint64, der []byte) []byte {
	out := []byte{0x00, 0x00}
	out = append(out, be64(ts)...)
	out = append(out, 0x00, 0x00)
	out = append(out, uint24(len(der))...)
	out = append(out, der...)
	return append(out, 0x00, 0x00)
}

func rebuildPrecertLeaf(ts uint64, keyHash [32]byte, tbs []byte) []byte {
	out := []byte{0x00, 0x00}
	out = append(out, be64(ts)...)
	out = append(out, 0x00, 0x01)
	out = append(out, keyHash[:]...)
	out = append(out, uint24(len(tbs))...)
	out = append(out, tbs...)
	return append(out, 0x00, 0x00)
}

func be64(v uint64) []byte {
	b := make([]byte, 8)
	for i := 7; i >= 0; i-- {
		b[i] = byte(v)
		v >>= 8
	}
	return b
}

func uint24(n int) []byte {
	return []byte{byte(n >> 16), byte(n >> 8), byte(n)}
}

func main() {
	t := vhelp.New()
	kat := katMap()

	// --- certificate entry ------------------------------------------------
	var sct ct.SignedCertificateTimestamp
	rest, err := tls.Unmarshal(mustHex(t, kat["TestCertProof"]), &sct)
	t.Check("sct-x509-deserialises", err == nil && len(rest) == 0,
		"tls.Unmarshal(TestCertProof) err=%v trailing=%d", err, len(rest))
	t.Equal("sct-x509-timestamp", sct.Timestamp, uint64(1365181456089))

	cert := loadCert(t, "test-cert.pem")
	leaf, err := ct.MerkleTreeLeafFromChain([]*x509.Certificate{cert}, ct.X509LogEntryType, sct.Timestamp)
	if err != nil {
		t.Fatalf("MerkleTreeLeafFromChain(x509): %v", err)
	}
	leafData, err := tls.Marshal(*leaf)
	if err != nil {
		t.Fatalf("tls.Marshal(cert leaf): %v", err)
	}
	t.EqualBytes("leaf-x509-encoding", leafData, rebuildCertLeaf(sct.Timestamp, cert.Raw))
	t.Equal("leaf-x509-hash", leafHash(leafData), kat["TestCertB64LeafHash"])

	// --- precertificate entry ---------------------------------------------
	var psct ct.SignedCertificateTimestamp
	if _, err := tls.Unmarshal(mustHex(t, kat["TestPreCertProof"]), &psct); err != nil {
		t.Fatalf("tls.Unmarshal(TestPreCertProof): %v", err)
	}
	pre := loadCert(t, "test-pre-cert.pem")
	ca := loadCert(t, "ca-cert.pem")
	pleaf, err := ct.MerkleTreeLeafFromChain([]*x509.Certificate{pre, ca}, ct.PrecertLogEntryType, psct.Timestamp)
	if err != nil {
		t.Fatalf("MerkleTreeLeafFromChain(precert): %v", err)
	}
	pleafData, err := tls.Marshal(*pleaf)
	if err != nil {
		t.Fatalf("tls.Marshal(precert leaf): %v", err)
	}
	// The issuer key hash is SHA-256 over the issuer's SubjectPublicKeyInfo.
	wantKeyHash := sha256.Sum256(ca.RawSubjectPublicKeyInfo)
	defanged, err := x509.BuildPrecertTBS(pre.RawTBSCertificate, nil)
	if err != nil {
		t.Fatalf("BuildPrecertTBS: %v", err)
	}
	t.EqualBytes("leaf-precert-encoding", pleafData, rebuildPrecertLeaf(psct.Timestamp, wantKeyHash, defanged))
	t.Equal("leaf-precert-hash", leafHash(pleafData), kat["TestPreCertB64LeafHash"])

	// --- the encoding is reversible ---------------------------------------
	var back ct.MerkleTreeLeaf
	if _, err := tls.Unmarshal(pleafData, &back); err != nil {
		t.Fatalf("tls.Unmarshal(precert leaf): %v", err)
	}
	again, err := tls.Marshal(back)
	if err != nil {
		t.Fatalf("re-marshal: %v", err)
	}
	t.EqualBytes("leaf-precert-roundtrip", again, pleafData)

	// A precertificate entry needs its issuer; one certificate is not enough.
	_, err = ct.MerkleTreeLeafFromChain([]*x509.Certificate{pre}, ct.PrecertLogEntryType, psct.Timestamp)
	t.Case("leaf-precert-needs-issuer", func() error {
		if err == nil {
			return errors.New("a one-element chain was accepted for a precert leaf")
		}
		return nil
	}())
	_ = fmt.Sprint

	t.Done()
}
