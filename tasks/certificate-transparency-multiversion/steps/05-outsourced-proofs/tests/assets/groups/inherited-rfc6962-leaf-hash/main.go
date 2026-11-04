// Group inherited-rfc6962-leaf-hash.
//
// The RFC 6962 leaf-hash algorithm remains unchanged at this milestone: SHA-256
// over the domain-separated MerkleTreeLeaf (0x00 || tls-encoded leaf), as
// MERKLE.md §1 specifies it. This group confirms that a known-answer test from
// the previous step still produces the same hashes after the proof verifier is
// outsourced and `merkletree/` is deleted.
//
// The hash is computed here from `tls.Marshal` and `crypto/sha256` rather than
// through `ct.LeafHashForLeaf`. That function arrives at v1.0.14, i.e. *at*
// this milestone, so a driver that called it would not compile against the
// previous milestone's tree — and an `inherited-` group that cannot build at
// the previous anchor catches no regression at all, it just reports a
// capability the candidate never had into the inherited tally. Recomputing the
// same digest inline keeps both case names and both expected values exactly as
// they were: `LeafHashForLeaf` is defined as this very computation, and it is
// scored as a step-05 new capability by `leafhash-for-leaf-entrypoint`, which
// is the right bucket for it.
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

// treeLeafPrefix is RFC 6962's domain separator for a leaf node (MERKLE.md §1).
const treeLeafPrefix = byte(0x00)

const (
	wantX509LeafHash    = "e636bf389d689e929d82b47da63077950168a4f239b6d6d5e0a5adce0ac05246"
	wantPrecertLeafHash = "b554fc6378f66adce5dce490fdd6ce04981e2fdfed35889d317d629b3e8428a6"
)

// leafHashHex returns hex(SHA-256(0x00 || tls-encoded leaf)).
func leafHashHex(leaf *ct.MerkleTreeLeaf) (string, error) {
	leafData, err := tls.Marshal(*leaf)
	if err != nil {
		return "", err
	}
	sum := sha256.Sum256(append([]byte{treeLeafPrefix}, leafData...))
	return hex.EncodeToString(sum[:]), nil
}

func main() {
	t := vhelp.New()

	leafDER := vhelp.ReadFixture("minted", "leaf-valid.der")
	x509Leaf := ct.CreateX509MerkleTreeLeaf(ct.ASN1Cert{Data: leafDER}, pinnedTimestamp)
	gotX509, err := leafHashHex(x509Leaf)
	if err != nil {
		t.Check("x509-entry-leaf-hash-unchanged", false, "tls.Marshal: %v", err)
	} else {
		t.Equal("x509-entry-leaf-hash-unchanged", gotX509, wantX509LeafHash)
	}

	precertDER := vhelp.ReadFixture("minted", "precert-poisoned.der")
	interDER := vhelp.ReadFixture("minted", "bench-intermediate.der")
	precert, _ := x509.ParseCertificate(precertDER)
	inter, _ := x509.ParseCertificate(interDER)
	if precert == nil || inter == nil {
		t.Fatalf("parse certs")
	}
	preLeaf, err := ct.MerkleTreeLeafFromChain([]*x509.Certificate{precert, inter}, ct.PrecertLogEntryType, pinnedTimestamp)
	if err != nil {
		t.Check("precert-entry-leaf-hash-unchanged", false, "MerkleTreeLeafFromChain: %v", err)
	} else {
		gotPre, err := leafHashHex(preLeaf)
		if err != nil {
			t.Check("precert-entry-leaf-hash-unchanged", false, "tls.Marshal: %v", err)
		} else {
			t.Equal("precert-entry-leaf-hash-unchanged", gotPre, wantPrecertLeafHash)
		}
	}

	t.Done()
}
