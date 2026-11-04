// Group leafhash-for-leaf-entrypoint.
//
// v1.0.17 splits the leaf-hash calculation out of ctutil.LeafHash into a new
// root-package entrypoint, ct.LeafHashForLeaf (commit 6d8e4ddc). Three callers
// move onto it in the same release: ctutil.LeafHash, ctutil.LogInfo and
// ctclient. This group grades the entrypoint itself at the library boundary.
//
// SCOPE-TIME: the leaf timestamp is a pinned constant, the certificates are
// pinned fixtures, and every expectation is an exact 32-byte hash.
//
// A v1.0.13 tree has no such identifier, so this driver does not compile
// against it and the group scores zero there, which is what the no-op
// continuation must see.
package main

import (
	"crypto/sha256"
	"encoding/hex"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/ctutil"
	"github.com/google/certificate-transparency-go/tls"
	"github.com/google/certificate-transparency-go/x509"

	"ctverify/vhelp"
)

// pinnedTimestamp is an arbitrary but fixed SCT timestamp in milliseconds
// (2018-02-27T13:20:00Z). Nothing here reads a clock.
const pinnedTimestamp = uint64(1519737600000)

// Known answers, recomputed from the pinned fixtures at this milestone.
const (
	wantX509LeafHash    = "e636bf389d689e929d82b47da63077950168a4f239b6d6d5e0a5adce0ac05246"
	wantPrecertLeafHash = "b554fc6378f66adce5dce490fdd6ce04981e2fdfed35889d317d629b3e8428a6"
)

func mustParse(t *vhelp.T, rel ...string) *x509.Certificate {
	der := vhelp.ReadFixture(rel...)
	cert, err := x509.ParseCertificate(der)
	if err != nil {
		t.Fatalf("parse %v: %v", rel, err)
	}
	return cert
}

func main() {
	t := vhelp.New()

	leafDER := vhelp.ReadFixture("minted", "leaf-valid.der")
	precert := mustParse(t, "minted", "precert-poisoned.der")
	inter := mustParse(t, "minted", "bench-intermediate.der")
	leafCert := mustParse(t, "minted", "leaf-valid.der")

	// --- 1. an x509 entry's leaf hash is exactly this ----------------------
	x509Leaf := ct.CreateX509MerkleTreeLeaf(ct.ASN1Cert{Data: leafDER}, pinnedTimestamp)
	gotX509, err := ct.LeafHashForLeaf(x509Leaf)
	if err != nil {
		t.Check("x509-entry-leaf-hash", false, "LeafHashForLeaf: %v", err)
	} else {
		t.Equal("x509-entry-leaf-hash", hex.EncodeToString(gotX509[:]), wantX509LeafHash)
	}

	// --- 2. a precert entry's leaf hash is exactly this --------------------
	preLeaf, err := ct.MerkleTreeLeafFromChain([]*x509.Certificate{precert, inter}, ct.PrecertLogEntryType, pinnedTimestamp)
	if err != nil {
		t.Check("precert-entry-leaf-hash", false, "MerkleTreeLeafFromChain: %v", err)
	} else {
		gotPre, err := ct.LeafHashForLeaf(preLeaf)
		if err != nil {
			t.Check("precert-entry-leaf-hash", false, "LeafHashForLeaf: %v", err)
		} else {
			t.Equal("precert-entry-leaf-hash", hex.EncodeToString(gotPre[:]), wantPrecertLeafHash)
		}
	}

	// --- 3. the RFC 6962 domain separation, recomputed here ----------------
	// leaf_hash = SHA-256(0x00 || tls-encoded MerkleTreeLeaf).
	marshalled, err := tls.Marshal(*x509Leaf)
	if err != nil {
		t.Check("rfc6962-leaf-prefix-domain-separation", false, "tls.Marshal: %v", err)
	} else {
		independent := sha256.Sum256(append([]byte{ct.TreeLeafPrefix}, marshalled...))
		t.EqualBytes("rfc6962-leaf-prefix-domain-separation", gotX509[:], independent[:])
	}

	// --- 4. the refactored caller still agrees with the new entrypoint -----
	// ctutil.LeafHash builds the same leaf from the chain and the SCT's
	// timestamp and now delegates to ct.LeafHashForLeaf; the two must be the
	// same 32 bytes.
	sct := &ct.SignedCertificateTimestamp{SCTVersion: ct.V1, Timestamp: pinnedTimestamp}
	viaCtutil, err := ctutil.LeafHash([]*x509.Certificate{leafCert}, sct, false)
	if err != nil {
		t.Check("ctutil-leafhash-delegates-to-entrypoint", false, "ctutil.LeafHash: %v", err)
	} else {
		t.EqualBytes("ctutil-leafhash-delegates-to-entrypoint", viaCtutil[:], gotX509[:])
	}

	// --- 5. a leaf recovered from the wire hashes to the same 32 bytes -----
	// This is the case that moved out of gate-endtoend, which used to reach
	// ct.LeafHashForLeaf over the tls-encoded MerkleTreeLeaf the CTFE hands
	// the backend. Naming a v1.0.17-only identifier in a gate group stopped
	// that driver compiling against the step-04 tree, so the shape lives here
	// instead, where the entrypoint is the priced capability: the leaf is
	// encoded, decoded with tls.Unmarshal exactly as a backend would, and the
	// entrypoint's answer is compared with an independent recomputation of
	// SHA-256(0x00 || leaf).
	if marshalled == nil {
		t.Check("wire-recovered-leaf-hashes-the-same", false, "leaf did not tls.Marshal")
	} else {
		var recovered ct.MerkleTreeLeaf
		rest, err := tls.Unmarshal(marshalled, &recovered)
		switch {
		case err != nil || len(rest) > 0:
			t.Check("wire-recovered-leaf-hashes-the-same", false,
				"tls.Unmarshal: %v (%d trailing bytes)", err, len(rest))
		default:
			gotWire, err := ct.LeafHashForLeaf(&recovered)
			if err != nil {
				t.Check("wire-recovered-leaf-hashes-the-same", false, "LeafHashForLeaf: %v", err)
			} else {
				independent := sha256.Sum256(append([]byte{ct.TreeLeafPrefix}, marshalled...))
				t.EqualBytes("wire-recovered-leaf-hashes-the-same", gotWire[:], independent[:])
			}
		}
	}

	t.Done()
}
