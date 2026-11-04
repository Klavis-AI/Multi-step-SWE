// Group ct-signature-verifier.
//
// RFC 6962 s2.1.4/s3: a log's key id is SHA-256 over its DER SubjectPublicKeyInfo,
// and a compliant log key is either ECDSA on P-256 or RSA of at least 2048 bits.
// This group grades ct.PublicKeyFromPEM's key id against the pinned corpus
// answers, the admission rules of ct.NewSignatureVerifier, and real signature
// verification of a pinned SCT and a pinned STH, including the tamper and
// wrong-key cases and the two unsupported-algorithm refusals.
//
// Boundary: exported root-package functions and the exported methods of
// SignatureVerifier. Only nil-versus-non-nil error is graded, never a type or
// a string.
package main

import (
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/rsa"
	"crypto/sha256"
	"encoding/hex"
	"encoding/pem"
	"errors"
	"strings"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/tls"

	"ctverify/vhelp"
)

// Pinned from the RFC 6962 reference vectors the corpus was distilled from.
// The SCT timestamp belongs with sigTestCertSCTSignature{EC,RSA}.
const (
	sctTimestamp = uint64(1348589665525)
	sthTimestamp = uint64(1348589667204)
	sthTreeSize  = uint64(42)
)

func katMap() map[string]string {
	m := map[string]string{}
	for _, ln := range strings.Split(string(vhelp.ReadFixture("rfc6962", "kat.txt")), "\n") {
		if f := strings.Fields(ln); len(f) == 2 {
			m[f[0]] = f[1]
		}
	}
	return m
}

func unhex(t *vhelp.T, s string) []byte {
	b, err := hex.DecodeString(s)
	if err != nil {
		t.Fatalf("corpus hex: %v", err)
	}
	return b
}

func pubKey(t *vhelp.T, name string) (interface{}, ct.SHA256Hash) {
	k, id, rest, err := ct.PublicKeyFromPEM(vhelp.ReadFixture("rfc6962", name))
	if err != nil {
		t.Fatalf("PublicKeyFromPEM(%s): %v", name, err)
	}
	if strings.TrimSpace(string(rest)) != "" {
		t.Fatalf("PublicKeyFromPEM(%s) left %d trailing bytes", name, len(rest))
	}
	return k, id
}

func digitallySigned(t *vhelp.T, raw []byte) ct.DigitallySigned {
	var ds tls.DigitallySigned
	if _, err := tls.Unmarshal(raw, &ds); err != nil {
		t.Fatalf("tls.Unmarshal(DigitallySigned): %v", err)
	}
	return ct.DigitallySigned(ds)
}

func nonNil(err error, why string) error {
	if err == nil {
		return errors.New(why)
	}
	return nil
}

func main() {
	t := vhelp.New()
	kat := katMap()

	ecKey, ecID := pubKey(t, "sig-test-ec256-public-key.pem")
	rsaKey, rsaID := pubKey(t, "sig-test-rsa-public-key.pem")
	ecKey2, _ := pubKey(t, "sig-test-ec256-public-key2.pem")

	t.EqualBytes("logid-ec-is-spki-sha256", ecID[:], unhex(t, kat["sigTestKeyIDEC"]))

	// The same rule, recomputed here over the DER this driver decodes itself,
	// for the RSA key. (The corpus carries no pinned answer for this one.)
	rsaBlock, _ := pem.Decode(vhelp.ReadFixture("rfc6962", "sig-test-rsa-public-key.pem"))
	if rsaBlock == nil {
		t.Fatalf("sig-test-rsa-public-key.pem has no PEM block")
	}
	wantRSAID := sha256.Sum256(rsaBlock.Bytes)
	t.EqualBytes("logid-rsa-is-spki-sha256", rsaID[:], wantRSAID[:])

	_, _, _, err := ct.PublicKeyFromPEM([]byte("this is not a PEM block\n"))
	t.Case("logid-rejects-non-pem", nonNil(err, "a non-PEM input produced a public key"))

	ecV, err := ct.NewSignatureVerifier(ecKey)
	if err != nil {
		t.Fatalf("NewSignatureVerifier(P-256): %v", err)
	}
	rsaV, err := ct.NewSignatureVerifier(rsaKey)
	if err != nil {
		t.Fatalf("NewSignatureVerifier(RSA-2048): %v", err)
	}
	ecV2, err := ct.NewSignatureVerifier(ecKey2)
	if err != nil {
		t.Fatalf("NewSignatureVerifier(P-256 #2): %v", err)
	}

	// --- non-compliant keys are refused -----------------------------------
	p384, err := ecdsa.GenerateKey(elliptic.P384(), rand.Reader)
	if err != nil {
		t.Fatalf("generate P-384: %v", err)
	}
	_, err = ct.NewSignatureVerifier(&p384.PublicKey)
	t.Case("verifier-refuses-non-p256-ecdsa", nonNil(err, "an ECDSA key off P-256 was accepted"))

	small, err := rsa.GenerateKey(rand.Reader, 1024)
	if err != nil {
		t.Fatalf("generate RSA-1024: %v", err)
	}
	_, err = ct.NewSignatureVerifier(&small.PublicKey)
	t.Case("verifier-refuses-rsa-under-2048", nonNil(err, "a 1024-bit RSA key was accepted"))

	// --- an SCT over the reference certificate ----------------------------
	der := unhex(t, kat["sigTestDERCertString"])
	entry := ct.LogEntry{Leaf: ct.MerkleTreeLeaf{
		Version:  ct.V1,
		LeafType: ct.TimestampedEntryLeafType,
		TimestampedEntry: &ct.TimestampedEntry{
			Timestamp: sctTimestamp,
			EntryType: ct.X509LogEntryType,
			X509Entry: &ct.ASN1Cert{Data: der},
		},
	}}
	sct := ct.SignedCertificateTimestamp{
		SCTVersion: ct.V1,
		Timestamp:  sctTimestamp,
		Signature:  digitallySigned(t, unhex(t, kat["sigTestCertSCTSignatureEC"])),
	}
	t.Case("sct-signature-ecdsa-verifies", ecV.VerifySCTSignature(sct, entry))

	sctRSA := sct
	sctRSA.Signature = digitallySigned(t, unhex(t, kat["sigTestCertSCTSignatureRSA"]))
	t.Case("sct-signature-rsa-verifies", rsaV.VerifySCTSignature(sctRSA, entry))

	tampered := sct
	tampered.Timestamp = sctTimestamp + 1
	t.Case("sct-signature-tampered-timestamp-fails",
		nonNil(ecV.VerifySCTSignature(tampered, entry), "a changed timestamp still verified"))

	badHash := sct
	badHash.Signature = digitallySigned(t, unhex(t, kat["sigTestCertSCTSignatureUnsupportedHashAlgorithm"]))
	t.Case("sct-signature-unsupported-hash-rejected",
		nonNil(ecV.VerifySCTSignature(badHash, entry), "hash algorithm 3 (SHA-224) was accepted"))

	badAlg := sct
	badAlg.Signature = digitallySigned(t, unhex(t, kat["sigTestCertSCTSignatureUnsupportedSignatureAlgorithm"]))
	t.Case("sct-signature-unsupported-algorithm-rejected",
		nonNil(ecV.VerifySCTSignature(badAlg, entry), "signature algorithm 2 (DSA) verified with an ECDSA key"))

	// --- the pinned STH ----------------------------------------------------
	var rh ct.SHA256Hash
	copy(rh[:], unhex(t, kat["sigTestDefaultRootHash"]))
	sth := ct.SignedTreeHead{
		Version:           ct.V1,
		TreeSize:          sthTreeSize,
		Timestamp:         sthTimestamp,
		SHA256RootHash:    rh,
		TreeHeadSignature: digitallySigned(t, unhex(t, kat["sigTestDefaultSTHSignature"])),
	}
	t.Case("sth-signature-verifies", ecV.VerifySTHSignature(sth))
	t.Case("sth-signature-wrong-key-fails",
		nonNil(ecV2.VerifySTHSignature(sth), "the STH verified under a different log key"))

	t.Done()
}
