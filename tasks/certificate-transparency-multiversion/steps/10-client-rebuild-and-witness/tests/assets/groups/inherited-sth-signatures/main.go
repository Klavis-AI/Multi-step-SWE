// Group inherited-sth-signatures.
//
// Everything v1.1.3's witness does rests on one carried-forward judgement: is
// this tree head really signed by that log? The witness makes that call with
// ct.SignatureVerifier.VerifySTHSignature and stores nothing it rejects, so
// this group re-checks the judgement itself against the corpus's pinned log
// key -- what it accepts, and the several different ways of being wrong that
// it must refuse.
//
// Signatures are produced here with the standard library, never by the
// product, so a verifier that agreed with its own signer and nobody else
// would fail.
package main

import (
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/sha256"
	"crypto/x509"
	"encoding/base64"
	"encoding/hex"
	"encoding/pem"
	"strings"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/tls"

	"ctverify/vhelp"
)

func pemBody(t *vhelp.T, b []byte) []byte {
	blk, _ := pem.Decode(b)
	if blk == nil {
		t.Fatalf("fixture is not PEM")
	}
	return blk.Bytes
}

func ecKey(t *vhelp.T, rel ...string) *ecdsa.PrivateKey {
	k, err := x509.ParsePKCS8PrivateKey(pemBody(t, vhelp.ReadFixture(rel...)))
	if err != nil {
		t.Fatalf("parse %v: %v", rel, err)
	}
	ec, ok := k.(*ecdsa.PrivateKey)
	if !ok {
		t.Fatalf("%v is %T, want *ecdsa.PrivateKey", rel, k)
	}
	return ec
}

// signSTH signs sth with key, using the standard library over the product's
// TLS encoding of the tree-head signature input.
func signSTH(t *vhelp.T, key *ecdsa.PrivateKey, sth *ct.SignedTreeHead) {
	in, err := ct.SerializeSTHSignatureInput(*sth)
	if err != nil {
		t.Fatalf("SerializeSTHSignatureInput: %v", err)
	}
	digest := sha256.Sum256(in)
	der, err := ecdsa.SignASN1(rand.Reader, key, digest[:])
	if err != nil {
		t.Fatalf("sign: %v", err)
	}
	sth.TreeHeadSignature = ct.DigitallySigned{
		Algorithm: tls.SignatureAndHashAlgorithm{Hash: tls.SHA256, Signature: tls.ECDSA},
		Signature: der,
	}
}

func main() {
	t := vhelp.New()

	logPubPEM := vhelp.ReadFixture("minted", "keys", "log-pubkey.pem")
	logPubDER := pemBody(t, logPubPEM)
	idFields := strings.Fields(string(vhelp.ReadFixture("minted", "keys", "log-id.txt")))

	// 1. The key hash a log is identified by is SHA-256 over the PKIX DER.
	pub, keyHash, rest, err := ct.PublicKeyFromPEM(logPubPEM)
	if err != nil {
		t.Fatalf("PublicKeyFromPEM: %v", err)
	}
	t.Equal("public-key-from-pem-key-hash", hex.EncodeToString(keyHash[:]), idFields[0])
	t.Equal("public-key-from-pem-consumes-the-block", len(strings.TrimSpace(string(rest))), 0)

	// 2. The same key read from base64 is the same key.
	pubB64, err := ct.PublicKeyFromB64(base64.StdEncoding.EncodeToString(logPubDER))
	if err != nil {
		t.Fatalf("PublicKeyFromB64: %v", err)
	}
	ec1, ok1 := pub.(*ecdsa.PublicKey)
	ec2, ok2 := pubB64.(*ecdsa.PublicKey)
	t.Check("public-key-from-b64-agrees-with-pem",
		ok1 && ok2 && ec1.X.Cmp(ec2.X) == 0 && ec1.Y.Cmp(ec2.Y) == 0,
		"pem=%T b64=%T equal=%v", pub, pubB64, ok1 && ok2 && ec1.X.Cmp(ec2.X) == 0)

	// 3. A P-256 key is an acceptable log key; a non-key is not.
	sv, err := ct.NewSignatureVerifier(pub)
	t.Check("signature-verifier-accepts-the-pinned-p256-key", err == nil, "err=%v", err)
	if err != nil {
		t.Fatalf("no verifier to test with: %v", err)
	}
	_, err = ct.NewSignatureVerifier([]byte{1, 2, 3})
	t.Check("signature-verifier-rejects-a-non-key", err != nil, "a []byte was accepted as a log key")

	// 4. A tree head this benchmark signed with the pinned log key verifies.
	var rh ct.SHA256Hash
	for i := range rh {
		rh[i] = 0x11
	}
	good := ct.SignedTreeHead{Version: ct.V1, TreeSize: 8, Timestamp: 1500000000000, SHA256RootHash: rh}
	signSTH(t, ecKey(t, "minted", "keys", "log-key.pem"), &good)
	t.Case("accepts-a-signature-by-the-pinned-log-key", sv.VerifySTHSignature(good))

	// The input it was signed over is exactly the RFC 6962 structure:
	// version(0) signature_type(1) timestamp tree_size root.
	in, err := ct.SerializeSTHSignatureInput(good)
	if err != nil {
		t.Fatalf("SerializeSTHSignatureInput: %v", err)
	}
	t.Equal("signature-input-encoding", hex.EncodeToString(in),
		"0001"+"0000015d3ef79800"+"0000000000000008"+strings.Repeat("11", 32))

	// 5. Every field the signature covers is actually covered.
	bigger := good
	bigger.TreeSize = 9
	t.Check("rejects-a-tampered-tree-size", sv.VerifySTHSignature(bigger) != nil,
		"the signature survived a change of tree size")

	later := good
	later.Timestamp = good.Timestamp + 1
	t.Check("rejects-a-tampered-timestamp", sv.VerifySTHSignature(later) != nil,
		"the signature survived a change of timestamp")

	forked := good
	forked.SHA256RootHash[0] ^= 0xff
	t.Check("rejects-a-tampered-root-hash", sv.VerifySTHSignature(forked) != nil,
		"the signature survived a change of root hash")

	// 6. A different key's signature over the very same tree head is refused.
	foreign := ct.SignedTreeHead{Version: ct.V1, TreeSize: 8, Timestamp: 1500000000000, SHA256RootHash: rh}
	signSTH(t, ecKey(t, "minted", "keys", "root-key.pem"), &foreign)
	t.Check("rejects-a-signature-by-another-key", sv.VerifySTHSignature(foreign) != nil,
		"a signature by the root key verified as the log's")

	// 7. So is a signature whose declared algorithms do not match the key or
	//    the digest that was actually signed.
	wrongHash := good
	wrongHash.TreeHeadSignature.Algorithm.Hash = tls.SHA512
	t.Check("rejects-a-mismatched-hash-algorithm", sv.VerifySTHSignature(wrongHash) != nil,
		"a SHA-512-labelled SHA-256 signature verified")

	wrongSigAlg := good
	wrongSigAlg.TreeHeadSignature.Algorithm.Signature = tls.RSA
	t.Check("rejects-a-mismatched-signature-algorithm", sv.VerifySTHSignature(wrongSigAlg) != nil,
		"an RSA-labelled signature verified under an ECDSA key")

	unknownHash := good
	unknownHash.TreeHeadSignature.Algorithm.Hash = tls.HashAlgorithm(99)
	t.Check("rejects-an-unknown-hash-algorithm", sv.VerifySTHSignature(unknownHash) != nil,
		"hash algorithm 99 was accepted")

	// 8. And a signature body that is not a well-formed ASN.1 ECDSA signature.
	truncated := good
	truncated.TreeHeadSignature.Signature = good.TreeHeadSignature.Signature[:4]
	t.Check("rejects-a-truncated-signature-body", sv.VerifySTHSignature(truncated) != nil,
		"a four-byte ECDSA signature verified")

	zeroed := good
	zeroed.TreeHeadSignature.Signature = mustDER(t, 0, 0)
	t.Check("rejects-a-zero-valued-signature", sv.VerifySTHSignature(zeroed) != nil,
		"an r=0,s=0 signature verified")

	// 9. A tree head claiming a version with no defined signature input can
	//    never verify, whatever is attached to it.
	v2 := good
	v2.Version = 2
	t.Check("rejects-an-unsupported-version", sv.VerifySTHSignature(v2) != nil,
		"an STH claiming version 2 verified")

	// 10. The product's verdict agrees with the standard library's, both ways.
	digest := sha256.Sum256(in)
	t.Check("agrees-with-the-standard-library",
		ecdsa.VerifyASN1(ec1, digest[:], good.TreeHeadSignature.Signature),
		"the standard library rejects a signature the product accepts")

	other, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		t.Fatalf("generate comparison key: %v", err)
	}
	t.Check("standard-library-also-rejects-a-foreign-key",
		!ecdsa.VerifyASN1(&other.PublicKey, digest[:], good.TreeHeadSignature.Signature),
		"a freshly generated key verified the pinned log's signature")

	t.Done()
}

// mustDER encodes an ASN.1 SEQUENCE { INTEGER r, INTEGER s } by hand, so the
// degenerate signature this group feeds the verifier is the benchmark's own.
func mustDER(t *vhelp.T, r, s byte) []byte {
	return []byte{0x30, 0x06, 0x02, 0x01, r, 0x02, 0x01, s}
}
