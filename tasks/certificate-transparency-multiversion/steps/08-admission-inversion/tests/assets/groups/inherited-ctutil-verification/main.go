// Group inherited-ctutil-verification.
//
// Step 4's boundary, re-run at this milestone: ctutil's importable
// verification surface -- LeafHash, LeafHashB64, VerifySCT and ContainsSCT.
// ctutil/ctutil.go is byte-identical at v1.0.21 and v1.1.0, so a do-nothing
// continuation is entitled to pass this group.
//
// The SCT verified here is manufactured inside the group: the signature input
// is serialised with the product's own code and signed with the minted log
// key, so the only thing under test is whether ctutil recomputes the same
// preimage and checks the signature over it. ECDSA signing is randomised, so
// the signature itself is never compared -- only the verdicts are, and the
// hashes, which are deterministic.
//
// SCOPE-TIME: the SCT timestamp is a pinned constant.
package main

import (
	"crypto/ecdsa"
	stdx509 "crypto/x509"
	"encoding/hex"
	"encoding/pem"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/ctutil"
	"github.com/google/certificate-transparency-go/tls"
	"github.com/google/certificate-transparency-go/x509"

	"ctverify/vhelp"
)

const (
	pinnedTS = uint64(1500000000000)
	// MerkleTreeLeaf hash for minted/leaf-valid.der at pinnedTS.
	wantLeafHash    = "0251c98eb42c63b9f017adcd2a161765f12d8a68dc70a3fa55b7dda79fa730a8"
	wantLeafHashB64 = "AlHJjrQsY7nwF63NKhYXZfEtimjccKP6Vbfdp5+nMKg="
)

func cert(t *vhelp.T, name string) *x509.Certificate {
	c, err := x509.ParseCertificate(vhelp.ReadFixture("minted", name))
	if err != nil {
		t.Fatalf("parse minted/%s: %v", name, err)
	}
	return c
}

func main() {
	t := vhelp.New()

	leaf := cert(t, "leaf-valid.der")
	rogue := cert(t, "rogue-root.der")

	blk, _ := pem.Decode(vhelp.ReadFixture("minted", "keys", "log-key.pem"))
	if blk == nil {
		t.Fatalf("minted/keys/log-key.pem is not PEM")
	}
	privAny, err := stdx509.ParsePKCS8PrivateKey(blk.Bytes)
	if err != nil {
		t.Fatalf("parse pinned private key: %v", err)
	}
	priv, ok := privAny.(*ecdsa.PrivateKey)
	if !ok {
		t.Fatalf("pinned private key is %T, want *ecdsa.PrivateKey", privAny)
	}
	pubKey, _, _, err := ct.PublicKeyFromPEM(vhelp.ReadFixture("minted", "keys", "log-pubkey.pem"))
	if err != nil {
		t.Fatalf("parse pinned public key: %v", err)
	}

	chain := []*x509.Certificate{leaf}

	// --- the leaf hash ctutil computes for an unembedded SCT ----------------
	h, err := ctutil.LeafHash(chain, &ct.SignedCertificateTimestamp{SCTVersion: ct.V1, Timestamp: pinnedTS}, false)
	t.Check("leafhash-x509-exact",
		err == nil && hex.EncodeToString(h[:]) == wantLeafHash,
		"err=%v hash=%s", err, hex.EncodeToString(h[:]))

	b64, err := ctutil.LeafHashB64(chain, &ct.SignedCertificateTimestamp{SCTVersion: ct.V1, Timestamp: pinnedTS}, false)
	t.Check("leafhashb64-x509-exact", err == nil && b64 == wantLeafHashB64, "err=%v b64=%s", err, b64)

	// The same value the serialization package computes from the leaf itself.
	mtl := ct.CreateX509MerkleTreeLeaf(ct.ASN1Cert{Data: leaf.Raw}, pinnedTS)
	direct, derr := ct.LeafHashForLeaf(mtl)
	t.Check("leafhash-agrees-with-serialization",
		derr == nil && hex.EncodeToString(direct[:]) == hex.EncodeToString(h[:]),
		"err=%v serialization=%s ctutil=%s", derr,
		hex.EncodeToString(direct[:]), hex.EncodeToString(h[:]))

	// --- a real signature over the RFC 6962 SCT signature input -------------
	mkSCT := func(ts uint64) *ct.SignedCertificateTimestamp {
		sct := &ct.SignedCertificateTimestamp{SCTVersion: ct.V1, Timestamp: ts}
		input, err := ct.SerializeSCTSignatureInput(*sct, ct.LogEntry{Leaf: *ct.CreateX509MerkleTreeLeaf(ct.ASN1Cert{Data: leaf.Raw}, ts)})
		if err != nil {
			t.Fatalf("serialize sct signature input: %v", err)
		}
		ds, err := tls.CreateSignature(*priv, tls.SHA256, input)
		if err != nil {
			t.Fatalf("sign sct signature input: %v", err)
		}
		sct.Signature = ct.DigitallySigned(ds)
		return sct
	}

	good := mkSCT(pinnedTS)
	t.Case("verifysct-accepts-pinned-signature", ctutil.VerifySCT(pubKey, chain, good, false))

	// A single flipped bit in the signature must not verify.
	tampered := *good
	tampered.Signature.Signature = append([]byte(nil), good.Signature.Signature...)
	tampered.Signature.Signature[len(tampered.Signature.Signature)-1] ^= 0x01
	t.Check("verifysct-rejects-tampered-signature",
		ctutil.VerifySCT(pubKey, chain, &tampered, false) != nil,
		"a flipped signature bit still verified")

	// The timestamp is part of the preimage, so moving it must not verify.
	moved := *good
	moved.Timestamp = pinnedTS + 1
	t.Check("verifysct-rejects-moved-timestamp",
		ctutil.VerifySCT(pubKey, chain, &moved, false) != nil,
		"changing the SCT timestamp still verified")

	// The certificate is part of the preimage too.
	t.Check("verifysct-rejects-wrong-certificate",
		ctutil.VerifySCT(pubKey, []*x509.Certificate{rogue}, good, false) != nil,
		"the signature verified over a different certificate")

	// And so is the key it was made with.
	t.Check("verifysct-rejects-wrong-public-key",
		ctutil.VerifySCT(rogue.PublicKey, chain, good, false) != nil,
		"the signature verified under an unrelated public key")

	// --- ContainsSCT -------------------------------------------------------
	found, err := ctutil.ContainsSCT(leaf, good)
	t.Check("containssct-false-without-sct-list", err == nil && !found,
		"err=%v found=%v; minted/leaf-valid.der carries no SCT list", err, found)

	// --- refusals ----------------------------------------------------------
	_, err = ctutil.LeafHash(nil, good, false)
	t.Check("leafhash-rejects-empty-chain", err != nil, "LeafHash accepted an empty chain")

	_, err = ctutil.LeafHash(chain, good, true)
	t.Check("leafhash-embedded-rejects-cert-without-sct-list", err != nil,
		"LeafHash claimed the SCT was embedded in a certificate that has no SCT list")

	t.Done()
}
