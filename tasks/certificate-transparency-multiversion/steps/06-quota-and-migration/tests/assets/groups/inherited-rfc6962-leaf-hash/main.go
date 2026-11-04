// Group inherited-rfc6962-leaf-hash.
//
// The RFC 6962 leaf-hash algorithm is unchanged at this milestone: SHA-256
// over the domain-separated Merkle tree leaf, 0x00 || tls-encoded leaf. Step
// 06 moves the code that *builds* that leaf into a new exported package, so
// the hash it produces is exactly the contract that must not move with it.
//
// The two known answers below are the same pinned hashes the previous step
// asserts, over the same pinned fixtures and timestamp; the third case
// recomputes the domain separation by hand, so the group is not merely
// asserting that the implementation agrees with itself.
//
// SCOPE-TIME: the leaf timestamp is a pinned constant, the certificates are
// pinned fixtures, and every expectation is an exact 32-byte hash.
package main

import (
	"crypto/sha256"
	"encoding/hex"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/tls"
	"github.com/google/certificate-transparency-go/x509"

	"ctverify/vhelp"
)

const pinnedTimestamp = uint64(1519737600000)

const (
	wantX509LeafHash    = "e636bf389d689e929d82b47da63077950168a4f239b6d6d5e0a5adce0ac05246"
	wantPrecertLeafHash = "b554fc6378f66adce5dce490fdd6ce04981e2fdfed35889d317d629b3e8428a6"
)

func parse(t *vhelp.T, rel ...string) *x509.Certificate {
	cert, err := x509.ParseCertificate(vhelp.ReadFixture(rel...))
	if err != nil {
		if _, ok := err.(x509.NonFatalErrors); !ok {
			t.Fatalf("parse %v: %v", rel, err)
		}
	}
	if cert == nil {
		t.Fatalf("parse %v: no certificate", rel)
	}
	return cert
}

func main() {
	t := vhelp.New()

	// --- 1. an x509 entry hashes to the same value as before ---------------
	leafDER := vhelp.ReadFixture("minted", "leaf-valid.der")
	x509Leaf := ct.CreateX509MerkleTreeLeaf(ct.ASN1Cert{Data: leafDER}, pinnedTimestamp)
	gotX509, err := ct.LeafHashForLeaf(x509Leaf)
	if err != nil {
		t.Check("x509-entry-leaf-hash-unchanged", false, "LeafHashForLeaf: %v", err)
	} else {
		t.Equal("x509-entry-leaf-hash-unchanged", hex.EncodeToString(gotX509[:]), wantX509LeafHash)
	}

	// --- 2. and so does a pre-certificate entry ----------------------------
	preLeaf, err := ct.MerkleTreeLeafFromChain(
		[]*x509.Certificate{parse(t, "minted", "precert-poisoned.der"), parse(t, "minted", "bench-intermediate.der")},
		ct.PrecertLogEntryType, pinnedTimestamp)
	if err != nil {
		t.Check("precert-entry-leaf-hash-unchanged", false, "MerkleTreeLeafFromChain: %v", err)
	} else if gotPre, err := ct.LeafHashForLeaf(preLeaf); err != nil {
		t.Check("precert-entry-leaf-hash-unchanged", false, "LeafHashForLeaf: %v", err)
	} else {
		t.Equal("precert-entry-leaf-hash-unchanged", hex.EncodeToString(gotPre[:]), wantPrecertLeafHash)
	}

	// --- 3. the hash really is SHA-256 of 0x00 || tls(leaf) ----------------
	// Recomputed here from the leaf's own encoding rather than from a
	// constant, which fixes the domain separation as well as the digest.
	encoded, err := tls.Marshal(*x509Leaf)
	if err != nil {
		t.Check("leaf-hash-is-domain-separated-sha256", false, "Marshal: %v", err)
	} else {
		want := sha256.Sum256(append([]byte{ct.TreeLeafPrefix}, encoded...))
		got, err := ct.LeafHashForLeaf(x509Leaf)
		if err != nil {
			t.Check("leaf-hash-is-domain-separated-sha256", false, "LeafHashForLeaf: %v", err)
		} else {
			t.Equal("leaf-hash-is-domain-separated-sha256",
				hex.EncodeToString(got[:])+" prefix="+hex.EncodeToString([]byte{ct.TreeLeafPrefix}),
				hex.EncodeToString(want[:])+" prefix=00")
		}
	}

	t.Done()
}
