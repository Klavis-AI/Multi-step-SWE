// Group inherited-rfc6962-leaf-and-entry.
//
// The RFC 6962 leaf contract from step 1, still exactly as it was at v1.1.4:
// how a submission becomes a MerkleTreeLeaf, what that leaf encodes to on the
// wire, what its leaf hash is, and how a get-entries answer is turned back
// into a log entry. serialization.go is byte-identical at v1.1.3 and v1.1.4 --
// this release moves log lists around, not the leaf format -- so this group is
// an inherited-regression check and must pass at both anchors.
//
// Every expected value is derived here from the fixture bytes and the wire
// format, never read back from the product: the X.509 leaf encoding is
// assembled by hand from the pinned DER, the leaf hash is SHA-256 over that
// assembled encoding with the 0x00 tree-leaf prefix in front of it, and the
// pre-certificate's issuer key hash is SHA-256 over the issuer's DER
// SubjectPublicKeyInfo taken from the fixture. Two of them are additionally
// pinned as literal hex so the answer is visible without running anything.
//
// SCOPE-TIME: the timestamp is the pinned constant below; no clock is read.
package main

import (
	"crypto/sha256"
	"encoding/hex"
	"fmt"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/tls"
	"github.com/google/certificate-transparency-go/x509"

	"ctverify/vhelp"
)

// pinnedTimestamp is 1234567890000 ms == 0x0000011f71fb0450.
const (
	pinnedTimestamp    = uint64(1234567890000)
	pinnedTimestampHex = "0000011f71fb0450"
	poisonOID          = "1.3.6.1.4.1.11129.2.4.3"
)

// uint24 renders a length the way the presentation language does.
func uint24(n int) string {
	return hex.EncodeToString([]byte{byte(n >> 16), byte(n >> 8), byte(n)})
}

func parse(t *vhelp.T, der []byte) *x509.Certificate {
	c, err := x509.ParseCertificate(der)
	if x509.IsFatal(err) {
		t.Fatalf("parse fixture certificate: %v", err)
	}
	return c
}

func main() {
	t := vhelp.New()

	leafDER := vhelp.ReadFixture("minted", "leaf-valid.der")
	intDER := vhelp.ReadFixture("minted", "bench-intermediate.der")
	precertDER := vhelp.ReadFixture("minted", "precert-poisoned.der")

	leafCert := parse(t, leafDER)
	issuer := parse(t, intDER)
	precert := parse(t, precertDER)

	// --- 1. the X.509 leaf -------------------------------------------------
	// version(1)=0 leaf_type(1)=0 timestamp(8) entry_type(2)=0
	// uint24-prefixed certificate, extensions(2)=0.
	wantX509Wire := "0000" + pinnedTimestampHex + "0000" + uint24(len(leafDER)) +
		hex.EncodeToString(leafDER) + "0000"

	x509Leaf := ct.CreateX509MerkleTreeLeaf(ct.ASN1Cert{Data: leafDER}, pinnedTimestamp)
	gotWire, err := tls.Marshal(*x509Leaf)
	if err != nil {
		t.Fatalf("marshal X.509 leaf: %v", err)
	}
	t.Equal("x509-leaf-wire-format-is-rfc6962", hex.EncodeToString(gotWire), wantX509Wire)

	// The same leaf built from a raw chain is the same leaf: only chain[0]
	// contributes to an X.509 entry.
	fromRaw, err := ct.MerkleTreeLeafFromRawChain(
		[]ct.ASN1Cert{{Data: leafDER}, {Data: intDER}}, ct.X509LogEntryType, pinnedTimestamp)
	if err != nil {
		t.Check("raw-chain-and-created-x509-leaf-agree", false, "MerkleTreeLeafFromRawChain: %v", err)
	} else {
		rawWire, merr := tls.Marshal(*fromRaw)
		t.Check("raw-chain-and-created-x509-leaf-agree",
			merr == nil && hex.EncodeToString(rawWire) == wantX509Wire,
			"err=%v wire=%x", merr, rawWire)
	}

	// The leaf hash is SHA-256 over 0x00 || the encoding above.
	wantLeafHash := sha256.Sum256(append([]byte{0x00}, gotWire...))
	gotLeafHash, err := ct.LeafHashForLeaf(x509Leaf)
	if err != nil {
		t.Check("x509-leaf-hash-is-sha256-of-the-zero-prefixed-leaf", false, "LeafHashForLeaf: %v", err)
	} else {
		t.EqualBytes("x509-leaf-hash-is-sha256-of-the-zero-prefixed-leaf", gotLeafHash[:], wantLeafHash[:])
	}
	t.Equal("x509-leaf-hash-exact", hex.EncodeToString(gotLeafHash[:]),
		"4de27390cab1f581fad3d8f6e611f42720505216a70d95f78b6bca5c042dd5cd")

	// Prefixing with 0x01 -- the interior-node prefix -- is a different hash,
	// which is what keeps a leaf from being forged as an internal node.
	nodeHash := sha256.Sum256(append([]byte{0x01}, gotWire...))
	t.Check("the-tree-leaf-prefix-is-not-the-node-prefix",
		hex.EncodeToString(nodeHash[:]) != hex.EncodeToString(gotLeafHash[:]),
		"0x00 and 0x01 prefixes hashed alike")

	// --- 2. the pre-certificate leaf ---------------------------------------
	preLeaf, err := ct.MerkleTreeLeafFromChain(
		[]*x509.Certificate{precert, issuer}, ct.PrecertLogEntryType, pinnedTimestamp)
	if err != nil {
		t.Fatalf("MerkleTreeLeafFromChain(precert): %v", err)
	}
	wantIKH := sha256.Sum256(issuer.RawSubjectPublicKeyInfo)
	t.EqualBytes("precert-leaf-issuer-key-hash-is-sha256-of-the-issuer-spki",
		preLeaf.TimestampedEntry.PrecertEntry.IssuerKeyHash[:], wantIKH[:])

	// The TBSCertificate the leaf carries is the submitted one with the poison
	// extension taken out, and nothing else disturbed.
	defanged := preLeaf.TimestampedEntry.PrecertEntry.TBSCertificate
	tbs, err := x509.ParseTBSCertificate(defanged)
	if x509.IsFatal(err) {
		t.Check("precert-leaf-tbs-parses-with-the-poison-removed", false, "ParseTBSCertificate: %v", err)
	} else {
		poisoned := false
		for _, ext := range tbs.Extensions {
			if ext.Id.String() == poisonOID {
				poisoned = true
			}
		}
		t.Check("precert-leaf-tbs-parses-with-the-poison-removed",
			!poisoned && tbs.SerialNumber.String() == precert.SerialNumber.String() &&
				len(tbs.Extensions) == len(precert.Extensions)-1,
			"poisoned=%v serial=%s/%s exts=%d/%d", poisoned,
			tbs.SerialNumber, precert.SerialNumber, len(tbs.Extensions), len(precert.Extensions))
	}
	t.Check("removing-the-poison-shortens-the-submitted-tbs",
		len(defanged) < len(precert.RawTBSCertificate),
		"defanged %d bytes, submitted %d bytes", len(defanged), len(precert.RawTBSCertificate))

	// entry_type is 1, and the body is issuer_key_hash(32) then the
	// uint24-prefixed TBSCertificate.
	wantPreWire := "0000" + pinnedTimestampHex + "0001" + hex.EncodeToString(wantIKH[:]) +
		uint24(len(defanged)) + hex.EncodeToString(defanged) + "0000"
	preWire, err := tls.Marshal(*preLeaf)
	if err != nil {
		t.Fatalf("marshal precert leaf: %v", err)
	}
	t.Equal("precert-leaf-wire-format-is-rfc6962", hex.EncodeToString(preWire), wantPreWire)

	preHash, err := ct.LeafHashForLeaf(preLeaf)
	if err != nil {
		t.Check("precert-leaf-hash-is-sha256-of-the-zero-prefixed-leaf", false, "LeafHashForLeaf: %v", err)
	} else {
		want := sha256.Sum256(append([]byte{0x00}, preWire...))
		t.EqualBytes("precert-leaf-hash-is-sha256-of-the-zero-prefixed-leaf", preHash[:], want[:])
	}

	// A pre-certificate leaf needs an issuer, and an entry type nobody has
	// defined has no leaf at all.
	_, errNoIssuer := ct.MerkleTreeLeafFromChain(
		[]*x509.Certificate{precert}, ct.PrecertLogEntryType, pinnedTimestamp)
	_, errUnknown := ct.MerkleTreeLeafFromChain(
		[]*x509.Certificate{precert, issuer}, ct.LogEntryType(99), pinnedTimestamp)
	t.Check("a-precert-leaf-needs-an-issuer-and-a-known-entry-type",
		errNoIssuer != nil && errUnknown != nil,
		"no-issuer=%v unknown-type=%v", errNoIssuer, errUnknown)

	// The ordinary intermediate is not a pre-issuer: it carries no CT EKU.
	t.Check("an-ordinary-ca-is-not-a-precertificate-issuer", !ct.IsPreIssuer(issuer),
		"the bench intermediate was treated as a pre-issuer")

	// --- 3. get-entries, read back -----------------------------------------
	// An X.509 entry: leaf_input is the leaf above, extra_data a
	// CertificateChain holding the issuers.
	chainWire, err := tls.Marshal(ct.CertificateChain{Entries: []ct.ASN1Cert{{Data: intDER}}})
	if err != nil {
		t.Fatalf("marshal CertificateChain: %v", err)
	}
	raw, err := ct.RawLogEntryFromLeaf(7, &ct.LeafEntry{LeafInput: gotWire, ExtraData: chainWire})
	if err != nil {
		t.Check("raw-log-entry-round-trips-an-x509-entry", false, "RawLogEntryFromLeaf: %v", err)
	} else {
		t.Equal("raw-log-entry-round-trips-an-x509-entry",
			fmt.Sprintf("%d/%s/%d/%s", raw.Index, hex.EncodeToString(raw.Cert.Data),
				len(raw.Chain), hex.EncodeToString(raw.Chain[0].Data)),
			fmt.Sprintf("7/%s/1/%s", hex.EncodeToString(leafDER), hex.EncodeToString(intDER)))
	}

	entry, err := ct.LogEntryFromLeaf(7, &ct.LeafEntry{LeafInput: gotWire, ExtraData: chainWire})
	if err != nil || entry == nil || entry.X509Cert == nil {
		t.Check("log-entry-from-leaf-parses-the-certificate", false, "err=%v entry=%v", err, entry)
	} else {
		t.Equal("log-entry-from-leaf-parses-the-certificate",
			fmt.Sprintf("%d/%s/%s/%d", entry.Index, entry.X509Cert.SerialNumber,
				entry.X509Cert.Subject.CommonName, entry.Leaf.TimestampedEntry.Timestamp),
			fmt.Sprintf("7/%s/%s/%d", leafCert.SerialNumber, leafCert.Subject.CommonName, pinnedTimestamp))
	}

	// A pre-certificate entry: extra_data is a PrecertChainEntry carrying the
	// submitted (still poisoned) pre-certificate plus its chain.
	preChainWire, err := tls.Marshal(ct.PrecertChainEntry{
		PreCertificate:   ct.ASN1Cert{Data: precertDER},
		CertificateChain: []ct.ASN1Cert{{Data: intDER}},
	})
	if err != nil {
		t.Fatalf("marshal PrecertChainEntry: %v", err)
	}
	preEntry, err := ct.LogEntryFromLeaf(8, &ct.LeafEntry{LeafInput: preWire, ExtraData: preChainWire})
	if err != nil || preEntry == nil || preEntry.Precert == nil {
		t.Check("log-entry-from-leaf-reconstructs-the-precertificate", false, "err=%v entry=%v", err, preEntry)
	} else {
		t.Equal("log-entry-from-leaf-reconstructs-the-precertificate",
			fmt.Sprintf("%d/%s/%s/%s", preEntry.Index,
				preEntry.Precert.TBSCertificate.SerialNumber,
				hex.EncodeToString(preEntry.Precert.IssuerKeyHash[:]),
				hex.EncodeToString(preEntry.Precert.Submitted.Data)),
			fmt.Sprintf("8/%s/%s/%s", precert.SerialNumber,
				hex.EncodeToString(wantIKH[:]), hex.EncodeToString(precertDER)))
	}

	// Trailing bytes after a leaf, and an entry type the format does not
	// define, are both refused rather than half-read.
	_, errTrail := ct.RawLogEntryFromLeaf(0, &ct.LeafEntry{
		LeafInput: append(append([]byte{}, gotWire...), 0xaa), ExtraData: chainWire})
	undefined, derr := hex.DecodeString("0000" + pinnedTimestampHex + "0009")
	if derr != nil {
		t.Fatalf("decode undefined entry: %v", derr)
	}
	_, errType := ct.RawLogEntryFromLeaf(0, &ct.LeafEntry{LeafInput: undefined, ExtraData: chainWire})
	t.Check("a-malformed-leaf-input-is-refused",
		errTrail != nil && errType != nil, "trailing=%v undefined-type=%v", errTrail, errType)

	// --- 4. the timestamp -------------------------------------------------
	t.Equal("timestamp-to-time-is-exact-utc-milliseconds",
		ct.TimestampToTime(pinnedTimestamp).UTC().Format("2006-01-02T15:04:05.000Z"),
		"2009-02-13T23:31:30.000Z")

	t.Done()
}
