// Group ct-merkle-leaf-for-embedded-sct.
//
// v1.0.13 adds ct.MerkleTreeLeafForEmbeddedSCT to the root package. Given a
// certificate that carries embedded SCTs and its issuer, it reconstructs the
// precertificate entry the log actually signed: the issuer key hash, plus the
// certificate's own TBSCertificate with the SCT-list extension taken back out.
// Before this release the root package could only build a precert leaf from a
// poisoned precertificate, so the embedded-SCT case was unreachable.
//
// Boundary: the root package's exported serialisation API and tls.Marshal.
// The expected leaf is pinned three ways: its exact byte length, the exact
// SHA-256 of its TLS encoding, and the published base64 leaf hash.
package main

import (
	"bytes"
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/tls"

	"ctverify/vhelp"
)

// The SCT-list extension, OID 1.3.6.1.4.1.11129.2.4.2, as it appears DER-encoded
// inside a TBSCertificate. Written out here rather than taken from the product.
var sctListOIDDER = []byte{0x06, 0x0a, 0x2b, 0x06, 0x01, 0x04, 0x01, 0xd6, 0x79, 0x02, 0x04, 0x02}

const (
	// SHA-256 of tls.Marshal of the reconstructed leaf, and its length.
	wantLeafTLSSHA256 = "09812696b22e8fda8e629fd934165d87451823d6af750ff922e80418f69752a7"
	wantLeafTLSLen    = 616
	// The TBSCertificate with the SCT list removed.
	wantTBSLen = 567
	// The SCT timestamp embedded in the reference certificate.
	pinnedTimestamp = uint64(1365181456275)
)

func main() {
	t := vhelp.New()
	m := katFile("rfc6962", "kat.txt")

	ca := vhelp.ReadFixture("rfc6962", "ca-cert.pem")
	embChain := pemCerts(t, join(vhelp.ReadFixture("rfc6962", "test-embedded-cert.pem"), ca))
	certChain := pemCerts(t, join(vhelp.ReadFixture("rfc6962", "test-cert.pem"), ca))
	if len(embChain) != 2 || len(certChain) != 2 {
		t.Fatalf("pinned chain fixtures did not parse to 2 certificates each")
	}

	leaf, err := ct.MerkleTreeLeafForEmbeddedSCT(embChain, pinnedTimestamp)
	if err != nil {
		t.Fatalf("MerkleTreeLeafForEmbeddedSCT: %v", err)
	}
	encoded, err := tls.Marshal(*leaf)
	if err != nil {
		t.Fatalf("tls.Marshal of the rebuilt leaf: %v", err)
	}

	// --- the encoding is exact ----------------------------------------------
	t.Check("leaf-tls-encoding-length", len(encoded) == wantLeafTLSLen,
		"tls.Marshal produced %d bytes, want %d", len(encoded), wantLeafTLSLen)
	sum := sha256.Sum256(encoded)
	t.Equal("leaf-tls-encoding-sha256", hex.EncodeToString(sum[:]), wantLeafTLSSHA256)

	// --- the structure is the precertificate entry RFC 6962 s3.2 describes ---
	te := leaf.TimestampedEntry
	structOK := leaf.Version == ct.V1 &&
		leaf.LeafType == ct.TimestampedEntryLeafType &&
		te != nil && te.EntryType == ct.PrecertLogEntryType &&
		te.PrecertEntry != nil && te.X509Entry == nil &&
		te.Timestamp == pinnedTimestamp && len(te.Extensions) == 0
	t.Check("leaf-is-a-v1-precertificate-timestamped-entry", structOK,
		"version=%v leaf_type=%v entry=%+v", leaf.Version, leaf.LeafType, te)

	// The issuer key hash is SHA-256 over the issuer's DER SubjectPublicKeyInfo,
	// recomputed here from the fixture rather than read back out of the product.
	wantIKH := sha256.Sum256(embChain[1].RawSubjectPublicKeyInfo)
	t.EqualBytes("issuer-key-hash-is-sha256-of-issuer-spki",
		te.PrecertEntry.IssuerKeyHash[:], wantIKH[:])

	// --- the SCT list really is gone, and nothing else is -------------------
	tbs := te.PrecertEntry.TBSCertificate
	t.Check("rebuilt-tbs-drops-the-sct-list-extension",
		len(tbs) == wantTBSLen && !bytes.Contains(tbs, sctListOIDDER) &&
			bytes.Contains(embChain[0].RawTBSCertificate, sctListOIDDER),
		"rebuilt TBS is %d bytes (want %d), contains SCT-list OID: %t",
		len(tbs), wantTBSLen, bytes.Contains(tbs, sctListOIDDER))

	// The remaining extensions keep their order and content: everything after
	// the removed extension in the original still appears, in sequence.
	t.Check("rebuilt-tbs-keeps-the-issuer-and-subject-bytes",
		bytes.Contains(tbs, embChain[0].RawIssuer) && bytes.Contains(tbs, embChain[0].RawSubject) &&
			bytes.Contains(tbs, embChain[0].RawSubjectPublicKeyInfo),
		"rebuilt TBS lost issuer/subject/SPKI bytes")

	// --- and it hashes to the published leaf hash ---------------------------
	lh := sha256.Sum256(append([]byte{ct.TreeLeafPrefix}, encoded...))
	t.Equal("rebuilt-leaf-hashes-to-the-published-answer",
		base64.StdEncoding.EncodeToString(lh[:]), kat(t, m, "TestPreCertB64LeafHash"))

	// --- error shapes -------------------------------------------------------
	_, err = ct.MerkleTreeLeafForEmbeddedSCT(embChain[:1], pinnedTimestamp)
	t.Check("no-issuer-in-chain-is-an-error", err != nil, "accepted a one-certificate chain")

	_, err = ct.MerkleTreeLeafForEmbeddedSCT(certChain, pinnedTimestamp)
	t.Check("certificate-without-an-sct-list-is-an-error", err != nil,
		"accepted a certificate that has no SCT-list extension to remove")

	// The pre-existing API cannot stand in for this one: asked for a precert
	// entry, MerkleTreeLeafFromChain looks for a poison extension the embedded
	// certificate does not have.
	_, err = ct.MerkleTreeLeafFromChain(embChain, ct.PrecertLogEntryType, pinnedTimestamp)
	t.Check("merkle-tree-leaf-from-chain-still-refuses-an-unpoisoned-cert", err != nil,
		"MerkleTreeLeafFromChain accepted an embedded-SCT certificate as a precertificate")

	t.Done()
}

func join(a, b []byte) []byte {
	out := make([]byte, 0, len(a)+len(b))
	return append(append(out, a...), b...)
}
