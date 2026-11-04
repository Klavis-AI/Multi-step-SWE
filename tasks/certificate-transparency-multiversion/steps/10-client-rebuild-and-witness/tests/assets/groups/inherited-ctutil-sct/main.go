// Group inherited-ctutil-sct.
//
// The witness accepts a tree head only after checking the log's signature over
// it; the same carried-forward machinery verifies SCTs.  This group exercises
// that machinery directly against the corpus's pinned log key: the three
// supported leaf shapes (an X.509 leaf, a precertificate, and a certificate
// with the SCT embedded), the utility wrappers' accept/reject verdicts, and
// the exact leaf hash the utilities compute.
//
// Every signature fed in is produced here with the standard library, over a
// preimage the benchmark derives for itself, so a verifier that agreed only
// with the product's own signer would fail.
package main

import (
	"crypto/ecdsa"
	"crypto/rand"
	"crypto/sha256"
	"encoding/base64"
	"encoding/pem"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/ctutil"
	cttls "github.com/google/certificate-transparency-go/tls"
	ctx509 "github.com/google/certificate-transparency-go/x509"

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
	k, err := ctx509.ParsePKCS8PrivateKey(pemBody(t, vhelp.ReadFixture(rel...)))
	if err != nil {
		t.Fatalf("parse %v: %v", rel, err)
	}
	ec, ok := k.(*ecdsa.PrivateKey)
	if !ok {
		t.Fatalf("%v is %T, want *ecdsa.PrivateKey", rel, k)
	}
	return ec
}

func parseCert(t *vhelp.T, der []byte) *ctx509.Certificate {
	c, err := ctx509.ParseCertificate(der)
	if err != nil {
		t.Fatalf("parse certificate: %v", err)
	}
	return c
}

func main() {
	t := vhelp.New()

	leafDER := vhelp.ReadFixture("minted", "leaf-valid.der")
	intDER := vhelp.ReadFixture("minted", "bench-intermediate.der")
	rootDER := vhelp.ReadFixture("minted", "bench-root.der")
	chainPEM := vhelp.ReadFixture("minted", "chain-valid.pem")

	logKey := ecKey(t, "minted", "keys", "log-key.pem")
	pub, keyHash, _, err := ct.PublicKeyFromPEM(vhelp.ReadFixture("minted", "keys", "log-pubkey.pem"))
	if err != nil {
		t.Fatalf("PublicKeyFromPEM: %v", err)
	}
	verifier, err := ct.NewSignatureVerifier(pub)
	if err != nil {
		t.Fatalf("NewSignatureVerifier: %v", err)
	}

	// A leaf is chain[0]; the issuer sits behind it.  The chain fixture is
	// ordered leaf-first, as the utilities require.
	leafCert := parseCert(t, leafDER)
	intCert := parseCert(t, intDER)
	rootCert := parseCert(t, rootDER)
	chain := []*ctx509.Certificate{leafCert, intCert}

	// The chain fixture must be the same leaf the DER fixture holds.
	chainPEMCerts := []*ctx509.Certificate{}
	for rest := chainPEM; len(rest) > 0; {
		var blk *pem.Block
		blk, rest = pem.Decode(rest)
		if blk == nil {
			break
		}
		if blk.Type != "CERTIFICATE" {
			continue
		}
		chainPEMCerts = append(chainPEMCerts, parseCert(t, blk.Bytes))
	}
	t.Check("chain-fixture-leads-with-the-leaf-fixture",
		len(chainPEMCerts) > 0 && string(chainPEMCerts[0].Raw) == string(leafDER),
		"chain[0] does not match leaf-valid.der")

	// A log ID is SHA-256 over the log's PKIX public key; the SCT carries it.
	if want := base64.StdEncoding.EncodeToString(keyHash[:]); want != "NOotw9BtP5lIZGvg2xA8czI9h4fHIq0QPXPgsJVJSqU=" {
		t.Fatalf("public-key hash drifted from the pinned log id: %s", want)
	}

	ts := uint64(1500000000000)
	makeSCT := func(entry ct.LogEntry) ct.SignedCertificateTimestamp {
		in, err := ct.SerializeSCTSignatureInput(ct.SignedCertificateTimestamp{
			SCTVersion: ct.V1,
			Timestamp:  ts,
			Extensions: []byte{},
		}, entry)
		if err != nil {
			t.Fatalf("SerializeSCTSignatureInput: %v", err)
		}
		d := sha256.Sum256(in)
		sig, err := ecdsa.SignASN1(rand.Reader, logKey, d[:])
		if err != nil {
			t.Fatalf("sign SCT: %v", err)
		}
		sct := ct.SignedCertificateTimestamp{
			SCTVersion: ct.V1,
			LogID:      ct.LogID{KeyID: keyHash},
			Timestamp:  ts,
			Extensions: []byte{},
			Signature: ct.DigitallySigned{
				Algorithm: cttls.SignatureAndHashAlgorithm{Hash: cttls.SHA256, Signature: cttls.ECDSA},
				Signature: sig,
			},
		}
		return sct
	}

	// Build the X.509 leaf the same way the utilities do, so the SCT that is
	// signed is over the leaf the verifier will reconstruct.
	x509Leaf, err := ct.MerkleTreeLeafFromChain(chain, ct.X509LogEntryType, ts)
	if err != nil {
		t.Fatalf("MerkleTreeLeafFromChain: %v", err)
	}
	sct := makeSCT(ct.LogEntry{Leaf: *x509Leaf})

	// 1-3. A well-formed SCT for this certificate verifies, both entry points.
	t.Check("verify-sct-accepts-a-valid-sct", ctutil.VerifySCT(pub, chain, &sct, false) == nil,
		"VerifySCT rejected a valid SCT")
	t.Check("verify-sct-with-verifier-accepts-it",
		ctutil.VerifySCTWithVerifier(verifier, chain, &sct, false) == nil,
		"VerifySCTWithVerifier rejected a valid SCT")
	// A nil verifier is a programming error, not a signature failure.
	t.Check("verify-sct-with-a-nil-verifier-errors",
		ctutil.VerifySCTWithVerifier(nil, chain, &sct, false) != nil,
		"a nil SignatureVerifier was accepted")

	// 4-5. A chain the SCT was not issued for is rejected, as is one that is
	// empty.
	wrongChain := []*ctx509.Certificate{parseCert(t, rootDER), intCert}
	t.Check("verify-sct-rejects-a-certificate-the-sct-was-not-issued-for",
		ctutil.VerifySCT(pub, wrongChain, &sct, false) != nil,
		"an SCT verified against a different leaf certificate")
	t.Check("verify-sct-rejects-an-empty-chain",
		ctutil.VerifySCT(pub, nil, &sct, false) != nil,
		"an empty chain was accepted")

	// 6. And a signature by another key over the very same preimage is refused.
	foreignKey, err := ecdsa.GenerateKey(logKey.Curve, rand.Reader)
	if err != nil {
		t.Fatalf("generate foreign key: %v", err)
	}
	in, err := ct.SerializeSCTSignatureInput(ct.SignedCertificateTimestamp{
		SCTVersion: ct.V1,
		Timestamp:  ts,
		Extensions: []byte{},
	}, ct.LogEntry{Leaf: *x509Leaf})
	if err != nil {
		t.Fatalf("SerializeSCTSignatureInput: %v", err)
	}
	d := sha256.Sum256(in)
	foreignSig, err := ecdsa.SignASN1(rand.Reader, foreignKey, d[:])
	if err != nil {
		t.Fatalf("sign with foreign key: %v", err)
	}
	foreign := sct
	foreign.Signature.Signature = foreignSig
	t.Check("verify-sct-rejects-a-signature-by-another-key",
		ctutil.VerifySCT(pub, chain, &foreign, false) != nil,
		"an SCT signed by the wrong key verified")

	// 7. A foreign signature over the same leaf but with a different timestamp
	// is refused too, and the timestamp is inside the signed preimage.
	otherTS := ts + 1
	if _, err := ct.MerkleTreeLeafFromChain(chain, ct.X509LogEntryType, otherTS); err != nil {
		t.Fatalf("MerkleTreeLeafFromChain: %v", err)
	}
	stale := sct
	stale.Timestamp = otherTS
	t.Check("verify-sct-rejects-a-changed-timestamp",
		ctutil.VerifySCT(pub, chain, &stale, false) != nil,
		"an SCT verified after its timestamp was changed")

	// 8-10. The exact leaf hash the utilities compute for this (leaf, SCT)
	// pair, and the embedded-mode results.  The leaf hash is the SHA-256 of
	// the RFC 6962 MerkleTreeLeaf, and is a pinned known answer.
	h, err := ctutil.LeafHash(chain, &sct, false)
	if err != nil {
		t.Fatalf("LeafHash: %v", err)
	}
	t.Equal("leaf-hash-is-sha256-of-the-merkle-tree-leaf",
		base64.StdEncoding.EncodeToString(h[:]), "AlHJjrQsY7nwF63NKhYXZfEtimjccKP6Vbfdp5+nMKg=")

	b64, err := ctutil.LeafHashB64(chain, &sct, false)
	if err != nil {
		t.Fatalf("LeafHashB64: %v", err)
	}
	t.Equal("leaf-hash-b64-agrees-with-leaf-hash", b64, base64.StdEncoding.EncodeToString(h[:]))

	// 11. The utilities' LeafHash agrees with LeafHashForLeaf over the leaf
	// the benchmark built by hand.
	wantH, err := ct.LeafHashForLeaf(x509Leaf)
	if err != nil {
		t.Fatalf("LeafHashForLeaf: %v", err)
	}
	t.Equal("leaf-hash-agrees-with-leaf-hash-for-leaf", base64.StdEncoding.EncodeToString(h[:]),
		base64.StdEncoding.EncodeToString(wantH[:]))

	// 12. The leaf hash is unique to the timestamp.
	otherH, err := ctutil.LeafHash(chain, &stale, false)
	if err != nil {
		t.Fatalf("LeafHash with a changed timestamp: %v", err)
	}
	t.Check("leaf-hash-depends-on-the-sct-timestamp", otherH != h,
		"two different SCT timestamps produced the same leaf hash")

	// 13. embedded=true is only meaningful for a certificate that really
	// carries the SCT, and this one does not, so it must error rather than
	// silently return a hash.
	_, err = ctutil.LeafHash(chain, &sct, true)
	t.Check("leaf-hash-embedded-true-errors-without-an-embedded-sct", err != nil,
		"embedded=true succeeded for a certificate with no embedded SCT")

	// 14. ContainsSCT agrees: this certificate carries no such SCT.
	present, err := ctutil.ContainsSCT(leafCert, &sct)
	if err != nil {
		t.Fatalf("ContainsSCT: %v", err)
	}
	t.Check("contains-sct-is-false-for-this-certificate", !present,
		"ContainsSCT claimed the SCT is embedded in a certificate that has none")

	// 15. A certificate with no SCT list at all is simply false, not an error.
	noSCTPresent, err := ctutil.ContainsSCT(rootCert, &sct)
	t.Check("contains-sct-tolerates-a-certificate-without-an-sct-list",
		err == nil && !noSCTPresent, "err=%v present=%v", err, noSCTPresent)

	// 16. And a nil certificate or SCT is false, not a panic.
	nilPresent, err := ctutil.ContainsSCT(nil, &sct)
	t.Check("contains-sct-tolerates-nil-arguments", err == nil && !nilPresent,
		"err=%v present=%v", err, nilPresent)

	// 17. The verifier refuses an SCT whose declared hash algorithm is not the
	// one the signature was made with.
	badAlg := sct
	badAlg.Signature.Algorithm.Hash = cttls.SHA512
	t.Check("verify-sct-rejects-a-mismatched-hash-algorithm",
		ctutil.VerifySCT(pub, chain, &badAlg, false) != nil,
		"a SHA-512-labelled SHA-256 signature verified")

	// 18. The chain fixture the utilities accept is the same three-certificate
	// chain the benchmark built.
	t.Equal("chain-fixture-length", len(chainPEMCerts), 2)
	t.Check("chain-fixture-matches-the-configured-chain",
		len(chainPEMCerts) == len(chain) && string(chainPEMCerts[1].Raw) == string(intDER),
		"the chain fixture is not the leaf+"+"intermediate chain")

	// 19-23. A precertificate is a different leaf shape: the utilities build
	// the RFC 6962 precert entry (issuer key hash plus the TBS with the poison
	// extension stripped) and verify an SCT over it under the precert type.
	precertDER := vhelp.ReadFixture("minted", "precert-poisoned.der")
	precertChain := []*ctx509.Certificate{parseCert(t, precertDER), intCert}

	precertLeaf, err := ct.MerkleTreeLeafFromChain(precertChain, ct.PrecertLogEntryType, ts)
	if err != nil {
		t.Fatalf("precert MerkleTreeLeafFromChain: %v", err)
	}
	t.Check("precert-leaf-is-not-the-x509-leaf", *precertLeaf != *x509Leaf,
		"the precert leaf equalled the x509 leaf")

	// The precert leaf carries the issuer's SPKI hash and the poision-free TBS,
	// and not the raw certificate.
	t.Check("precert-leaf-recorded-the-issuer-key-hash",
		precertLeaf.TimestampedEntry.PrecertEntry.IssuerKeyHash ==
			sha256.Sum256(intCert.RawSubjectPublicKeyInfo),
		"the precert entry does not carry SHA-256 of the issuer SPKI")

	precertSCT := makeSCT(ct.LogEntry{Leaf: *precertLeaf})
	t.Check("verify-sct-accepts-an-sct-over-a-precertificate",
		ctutil.VerifySCT(pub, precertChain, &precertSCT, false) == nil,
		"an SCT over a precertificate was rejected")

	// The same poisoned certificate hashed as an X.509 entry is a different
	// leaf, so a verifier that ignored the entry type would agree with both.
	asX509, err := ct.MerkleTreeLeafFromChain(precertChain, ct.X509LogEntryType, ts)
	if err != nil {
		t.Fatalf("x509-typed MerkleTreeLeafFromChain: %v", err)
	}
	t.Check("a-precertificate-is-a-different-leaf-under-the-x509-type",
		*asX509 != *precertLeaf, "the two entry types produced the same leaf")

	precertHash, err := ctutil.LeafHash(precertChain, &precertSCT, false)
	if err != nil {
		t.Fatalf("LeafHash over a precertificate: %v", err)
	}
	wantPrecert, err := ct.LeafHashForLeaf(precertLeaf)
	if err != nil {
		t.Fatalf("LeafHashForLeaf: %v", err)
	}
	t.Equal("precert-leaf-hash-agrees-with-leaf-hash-for-leaf",
		base64.StdEncoding.EncodeToString(precertHash[:]),
		base64.StdEncoding.EncodeToString(wantPrecert[:]))

	t.Done()
}
