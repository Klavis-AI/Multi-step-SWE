// Group tls-digitally-signed.
//
// RFC 5246 s4.7 / s7.4.1.4.1: a digitally-signed value is a
// SignatureAndHashAlgorithm pair followed by an opaque vector with a two-byte
// length. This group grades the tls package's rendering of that structure, the
// enum values of the two algorithm registries, and tls.VerifySignature /
// tls.CreateSignature against pinned corpus material.
//
// Boundary: the tls package's exported codec and signature functions, with
// pinned keys and a pinned signature. No wall clock is involved.
package main

import (
	"encoding/hex"
	"encoding/pem"
	"errors"
	"strings"

	"github.com/google/certificate-transparency-go/tls"
	"github.com/google/certificate-transparency-go/x509"

	"ctverify/vhelp"
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

func nonNil(err error, why string) error {
	if err == nil {
		return errors.New(why)
	}
	return nil
}

func main() {
	t := vhelp.New()
	kat := katMap()

	t.Check("algorithm-registry-values",
		tls.None == 0 && tls.MD5 == 1 && tls.SHA1 == 2 && tls.SHA224 == 3 &&
			tls.SHA256 == 4 && tls.SHA384 == 5 && tls.SHA512 == 6 &&
			tls.Anonymous == 0 && tls.RSA == 1 && tls.DSA == 2 && tls.ECDSA == 3,
		"hash=%d/%d/%d/%d/%d/%d/%d sig=%d/%d/%d/%d",
		tls.None, tls.MD5, tls.SHA1, tls.SHA224, tls.SHA256, tls.SHA384, tls.SHA512,
		tls.Anonymous, tls.RSA, tls.DSA, tls.ECDSA)

	// TLS-CODEC.md s1: both registries render their own names, and an
	// unrecognised code point as UNKNOWN(<n>). The renderings are the
	// observable here -- they are what ctclient's signature line prints.
	gotHash := []string{
		tls.None.String(), tls.MD5.String(), tls.SHA1.String(), tls.SHA224.String(),
		tls.SHA256.String(), tls.SHA384.String(), tls.SHA512.String(),
		tls.HashAlgorithm(99).String(),
	}
	t.Equal("hash-algorithm-string-renderings", strings.Join(gotHash, ","),
		"None,MD5,SHA1,SHA224,SHA256,SHA384,SHA512,UNKNOWN(99)")

	gotSig := []string{
		tls.Anonymous.String(), tls.RSA.String(), tls.DSA.String(), tls.ECDSA.String(),
		tls.SignatureAlgorithm(99).String(),
	}
	t.Equal("signature-algorithm-string-renderings", strings.Join(gotSig, ","),
		"Anonymous,RSA,DSA,ECDSA,UNKNOWN(99)")

	// --- encoding ------------------------------------------------------------
	ds := tls.DigitallySigned{
		Algorithm: tls.SignatureAndHashAlgorithm{Hash: tls.SHA256, Signature: tls.ECDSA},
		Signature: []byte{0xca, 0xfe, 0xba, 0xbe},
	}
	enc, err := tls.Marshal(ds)
	if err != nil {
		t.Fatalf("tls.Marshal(DigitallySigned): %v", err)
	}
	t.EqualBytes("digitally-signed-marshal-exact", enc,
		[]byte{0x04, 0x03, 0x00, 0x04, 0xca, 0xfe, 0xba, 0xbe})

	sthSigBytes := unhex(t, kat["sigTestDefaultSTHSignature"])
	var sthSig tls.DigitallySigned
	rest, err := tls.Unmarshal(sthSigBytes, &sthSig)
	t.Check("digitally-signed-unmarshal-corpus",
		err == nil && len(rest) == 0 &&
			sthSig.Algorithm.Hash == tls.SHA256 && sthSig.Algorithm.Signature == tls.ECDSA &&
			len(sthSig.Signature) == 0x48,
		"err=%v trailing=%d hash=%v sig=%v len=%d", err, len(rest),
		sthSig.Algorithm.Hash, sthSig.Algorithm.Signature, len(sthSig.Signature))

	// --- verification against the pinned STH signature -----------------------
	pubBlock, _ := pem.Decode(vhelp.ReadFixture("rfc6962", "sig-test-ec256-public-key.pem"))
	if pubBlock == nil {
		t.Fatalf("no PEM block in sig-test-ec256-public-key.pem")
	}
	pub, err := x509.ParsePKIXPublicKey(pubBlock.Bytes)
	if err != nil {
		t.Fatalf("ParsePKIXPublicKey: %v", err)
	}
	rsaBlock, _ := pem.Decode(vhelp.ReadFixture("rfc6962", "sig-test-rsa-public-key.pem"))
	rsaPub, err := x509.ParsePKIXPublicKey(rsaBlock.Bytes)
	if err != nil {
		t.Fatalf("ParsePKIXPublicKey(rsa): %v", err)
	}

	signed := unhex(t, kat["sigTestDefaultSTHSerialized"])
	t.Case("verify-pinned-sth-signature", tls.VerifySignature(pub, signed, sthSig))

	mangled := append([]byte{}, signed...)
	mangled[len(mangled)-1] ^= 0x01
	t.Case("verify-altered-data-fails",
		nonNil(tls.VerifySignature(pub, mangled, sthSig), "a flipped bit in the signed data still verified"))

	unknownHash := sthSig
	unknownHash.Algorithm.Hash = tls.HashAlgorithm(7)
	t.Case("verify-unknown-hash-algorithm-rejected",
		nonNil(tls.VerifySignature(pub, signed, unknownHash), "hash algorithm 7 was accepted"))

	unknownSig := sthSig
	unknownSig.Algorithm.Signature = tls.Anonymous
	t.Case("verify-unknown-signature-algorithm-rejected",
		nonNil(tls.VerifySignature(pub, signed, unknownSig), "signature algorithm anonymous was accepted"))

	t.Case("verify-key-type-mismatch-rejected",
		nonNil(tls.VerifySignature(rsaPub, signed, sthSig),
			"an ECDSA signature verified against an RSA key"))

	// --- creation -------------------------------------------------------------
	privBlock, _ := pem.Decode(vhelp.ReadFixture("rfc6962", "sig-test-ec256-private-key.pem"))
	if privBlock == nil {
		t.Fatalf("no PEM block in sig-test-ec256-private-key.pem")
	}
	priv, err := x509.ParseECPrivateKey(privBlock.Bytes)
	if err != nil {
		t.Fatalf("ParseECPrivateKey: %v", err)
	}
	fresh, err := tls.CreateSignature(*priv, tls.SHA256, signed)
	if err != nil {
		t.Fatalf("tls.CreateSignature: %v", err)
	}
	t.Check("create-signature-algorithm-fields",
		fresh.Algorithm.Hash == tls.SHA256 && fresh.Algorithm.Signature == tls.ECDSA && len(fresh.Signature) > 0,
		"hash=%v sig=%v len=%d", fresh.Algorithm.Hash, fresh.Algorithm.Signature, len(fresh.Signature))
	t.Case("create-signature-verifies-under-public-key", tls.VerifySignature(pub, signed, fresh))

	_, err = tls.CreateSignature(*priv, tls.HashAlgorithm(7), signed)
	t.Case("create-signature-unknown-hash-rejected", nonNil(err, "hash algorithm 7 produced a signature"))

	_, err = tls.CreateSignature([]byte("not a key"), tls.SHA256, signed)
	t.Case("create-signature-unsupported-key-rejected", nonNil(err, "a byte slice was accepted as a private key"))

	t.Done()
}
