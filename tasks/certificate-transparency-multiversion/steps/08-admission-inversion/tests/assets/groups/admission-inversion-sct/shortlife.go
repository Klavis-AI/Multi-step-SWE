// shortlife.go is compiled into exactly one group's single compilation unit.
// It holds no cases and prints nothing; it is not a group of its own.
//
// It mints, in memory, the one chain shape the pinned corpus does not have:
// an issuer that expires before its subject does. See shortLifeChain.
package main

import (
	"crypto/ecdsa"
	"crypto/rand"
	stdx509 "crypto/x509"
	"crypto/x509/pkix"
	"encoding/pem"
	"math/big"
	"time"

	"ctverify/vhelp"
)

// Subject common names of the in-memory short-life pair.
const (
	shortIntCN  = "Harbor CT Bench Short-Life Intermediate"
	shortLeafCN = "shortpath.bench.invalid"
)

// fixtureKey reads one of the pinned PKCS#8 EC keys out of minted/keys/.
func fixtureKey(t *vhelp.T, name string) *ecdsa.PrivateKey {
	blk, _ := pem.Decode(vhelp.ReadFixture("minted", "keys", name))
	if blk == nil {
		t.Fatalf("no PEM block in minted/keys/%s", name)
	}
	k, err := stdx509.ParsePKCS8PrivateKey(blk.Bytes)
	if err != nil {
		t.Fatalf("minted/keys/%s does not parse: %v", name, err)
	}
	ek, ok := k.(*ecdsa.PrivateKey)
	if !ok {
		t.Fatalf("minted/keys/%s is not an EC key", name)
	}
	return ek
}

// shortLifeChain builds, in memory, the one chain shape the pinned corpus does
// not have: an issuer that expires BEFORE its subject does.
//
//	Harbor CT Bench Short-Life Intermediate  notBefore 2000-01-01Z  notAfter 2025-01-01Z
//	shortpath.bench.invalid                  notBefore 2020-01-01Z  notAfter 2060-01-01Z
//
// At atNow (2030-01-01Z) the intermediate is five years expired and the leaf
// has thirty years to run. Every minted chain shares bench-intermediate, whose
// 2000..2100 window strictly contains every leaf's, so on those chains a
// whole-chain expiry loop and the v1.1.0 leaf-only test agree on every answer;
// this chain is the only one that tells them apart. HTTP-API.md 5.2: the
// v1.1.0 test is "explicit, leaf-only" and runs before path building, and path
// building itself has time checks disabled, so an expired *intermediate* does
// not reject the chain.
//
// Every key here is a pinned fixture -- minted/keys/{root,intermediate,leaf}-key.pem,
// the same three the corpus itself was minted with. Only the validity windows
// are new, and no assertion below reads a signature, a serial or a byte.
func shortLifeChain(t *vhelp.T) [][]byte {
	rootKey := fixtureKey(t, "root-key.pem")
	intKey := fixtureKey(t, "intermediate-key.pem")
	leafKey := fixtureKey(t, "leaf-key.pem")

	blk, _ := pem.Decode(vhelp.ReadFixture("minted", "bench-root.pem"))
	if blk == nil {
		t.Fatalf("no PEM block in minted/bench-root.pem")
	}
	root, err := stdx509.ParseCertificate(blk.Bytes)
	if err != nil {
		t.Fatalf("minted/bench-root.pem does not parse: %v", err)
	}

	intTmpl := &stdx509.Certificate{
		SerialNumber:          big.NewInt(0x08a1),
		Subject:               pkix.Name{CommonName: shortIntCN, Organization: []string{"Harbor CT Bench"}},
		NotBefore:             time.Date(2000, 1, 1, 0, 0, 0, 0, time.UTC),
		NotAfter:              time.Date(2025, 1, 1, 0, 0, 0, 0, time.UTC),
		KeyUsage:              stdx509.KeyUsageCertSign | stdx509.KeyUsageCRLSign | stdx509.KeyUsageDigitalSignature,
		BasicConstraintsValid: true,
		IsCA:                  true,
		MaxPathLen:            0,
		MaxPathLenZero:        true,
		SignatureAlgorithm:    stdx509.ECDSAWithSHA256,
	}
	intDER, err := stdx509.CreateCertificate(rand.Reader, intTmpl, root, &intKey.PublicKey, rootKey)
	if err != nil {
		t.Fatalf("could not mint the short-life intermediate: %v", err)
	}
	shortInt, err := stdx509.ParseCertificate(intDER)
	if err != nil {
		t.Fatalf("the short-life intermediate does not parse: %v", err)
	}

	leafTmpl := &stdx509.Certificate{
		SerialNumber:          big.NewInt(0x08a2),
		Subject:               pkix.Name{CommonName: shortLeafCN, Organization: []string{"Harbor CT Bench"}},
		NotBefore:             time.Date(2020, 1, 1, 0, 0, 0, 0, time.UTC),
		NotAfter:              time.Date(2060, 1, 1, 0, 0, 0, 0, time.UTC),
		KeyUsage:              stdx509.KeyUsageDigitalSignature | stdx509.KeyUsageKeyEncipherment,
		ExtKeyUsage:           []stdx509.ExtKeyUsage{stdx509.ExtKeyUsageServerAuth, stdx509.ExtKeyUsageClientAuth},
		BasicConstraintsValid: true,
		DNSNames:              []string{shortLeafCN},
		SignatureAlgorithm:    stdx509.ECDSAWithSHA256,
	}
	leafDER, err := stdx509.CreateCertificate(rand.Reader, leafTmpl, shortInt, &leafKey.PublicKey, intKey)
	if err != nil {
		t.Fatalf("could not mint the short-life leaf: %v", err)
	}
	return [][]byte{leafDER, intDER}
}
