// Group inherited-tls-serialization.
//
// Step 1's boundary, re-run at this milestone: the tls/ presentation-language
// codec and the RFC 6962 structures built on it. Nothing here is new at
// v1.1.0, so a do-nothing continuation is entitled to pass this group -- that
// is what an inherited-* group is for.
//
// Every expected value is a constant measured once and written down: the
// MerkleTreeLeaf encoding of a pinned certificate at a pinned timestamp, its
// leaf hash, the SCT and STH signature inputs, and the codec's own framing.
// SCOPE-TIME: the timestamp is a constant, never the clock.
package main

import (
	"crypto/sha256"
	"encoding/binary"
	"encoding/hex"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/tls"

	"ctverify/vhelp"
)

const (
	pinnedTS = uint64(1500000000000)

	// tls.Marshal of the MerkleTreeLeaf for minted/leaf-valid.der at pinnedTS.
	wantLeafLen    = 513
	wantLeafSHA    = "13fd1759acfd111b176958a692769fdd7f05aa868c092eb9346be9f440f411bf"
	wantLeafHash   = "0251c98eb42c63b9f017adcd2a161765f12d8a68dc70a3fa55b7dda79fa730a8"
	wantPrecertSHA = "5842129d6cbef4358800f2b754ce7a39b0df1c15a63bd5461e35b7a64fb354a3"
	wantIssuerHash = "2f5b59344c8fd42ba31eaeeed9dbb5d93c931fd858e98c86bbae84e7841f5e05"

	// SerializeSTHSignatureInput for tree_size=4096, timestamp=1573689600000
	// and the pinned root hash below: version|signature_type|timestamp|size|hash.
	wantSTHInput = "0001" + "0000016e6735a800" + "0000000000001000" +
		"0617c7c846ca8a3b3f75148b399d0968b1e6201f487667ab53a06e7aeb8bc967"
	pinnedRootHex = "0617c7c846ca8a3b3f75148b399d0968b1e6201f487667ab53a06e7aeb8bc967"
)

type v24 struct {
	D []byte `tls:"minlen:0,maxlen:16777215"`
}

type v2 struct {
	D []byte `tls:"minlen:0,maxlen:2"`
}

func sha(b []byte) string { s := sha256.Sum256(b); return hex.EncodeToString(s[:]) }

func main() {
	t := vhelp.New()

	leafDER := vhelp.ReadFixture("minted", "leaf-valid.der")
	preDER := vhelp.ReadFixture("minted", "precert-poisoned.der")
	intDER := vhelp.ReadFixture("minted", "bench-intermediate.der")

	// --- the X.509 MerkleTreeLeaf -------------------------------------------
	mtl := ct.CreateX509MerkleTreeLeaf(ct.ASN1Cert{Data: leafDER}, pinnedTS)
	enc, err := tls.Marshal(*mtl)
	if err != nil {
		t.Fatalf("tls.Marshal(MerkleTreeLeaf): %v", err)
	}
	t.Equal("merkle-tree-leaf-marshal-exact", sha(enc)+"/"+itoa(len(enc)),
		wantLeafSHA+"/"+itoa(wantLeafLen))

	// The framing RFC 6962 s3.4 specifies, field by field: version(1),
	// leaf_type(1), timestamp(8), entry_type(2), 24-bit opaque length, then
	// the certificate, then a 16-bit extensions length of zero.
	framing := "bad length"
	if len(enc) == wantLeafLen {
		certLen := uint32(enc[12])<<16 | uint32(enc[13])<<8 | uint32(enc[14])
		framing = itoa(int(enc[0])) + "/" + itoa(int(enc[1])) + "/" +
			itoa(int(binary.BigEndian.Uint64(enc[2:10]))) + "/" +
			itoa(int(binary.BigEndian.Uint16(enc[10:12]))) + "/" +
			itoa(int(certLen)) + "/" +
			itoa(int(binary.BigEndian.Uint16(enc[len(enc)-2:])))
	}
	t.Equal("merkle-tree-leaf-framing-exact", framing,
		"0/0/"+itoa(int(pinnedTS))+"/0/"+itoa(len(leafDER))+"/0")

	var back ct.MerkleTreeLeaf
	rest, err := tls.Unmarshal(enc, &back)
	ok := err == nil && len(rest) == 0 && back.Version == ct.V1 &&
		back.LeafType == ct.TimestampedEntryLeafType &&
		back.TimestampedEntry != nil &&
		back.TimestampedEntry.Timestamp == pinnedTS &&
		back.TimestampedEntry.EntryType == ct.X509LogEntryType &&
		back.TimestampedEntry.X509Entry != nil &&
		hex.EncodeToString(back.TimestampedEntry.X509Entry.Data) == hex.EncodeToString(leafDER)
	t.Check("merkle-tree-leaf-roundtrip", ok, "err=%v rest=%d leaf=%+v", err, len(rest), back)

	// --- the leaf hash, and its 0x00 prefix ---------------------------------
	lh, err := ct.LeafHashForLeaf(mtl)
	prefixed := sha256.Sum256(append([]byte{0x00}, enc...))
	t.Check("leaf-hash-exact",
		err == nil && hex.EncodeToString(lh[:]) == wantLeafHash &&
			hex.EncodeToString(prefixed[:]) == wantLeafHash,
		"err=%v hash=%s prefixed=%s", err, hex.EncodeToString(lh[:]), hex.EncodeToString(prefixed[:]))

	// --- the two signature inputs -------------------------------------------
	si, err := ct.SerializeSCTSignatureInput(
		ct.SignedCertificateTimestamp{SCTVersion: ct.V1, Timestamp: pinnedTS},
		ct.LogEntry{Leaf: *mtl})
	t.Check("sct-signature-input-exact",
		err == nil && sha(si) == wantLeafSHA && len(si) == wantLeafLen,
		"err=%v sha=%s len=%d", err, sha(si), len(si))

	var root [sha256.Size]byte
	copy(root[:], unhex(t, pinnedRootHex))
	ss, err := ct.SerializeSTHSignatureInput(ct.SignedTreeHead{
		Version: ct.V1, TreeSize: 4096, Timestamp: 1573689600000, SHA256RootHash: root,
	})
	t.Check("sth-signature-input-exact",
		err == nil && hex.EncodeToString(ss) == wantSTHInput,
		"err=%v got=%s", err, hex.EncodeToString(ss))

	// --- the codec's own framing rules --------------------------------------
	m, err := tls.Marshal(v24{D: []byte{1, 2, 3}})
	t.Check("uint24-vector-length-prefix",
		err == nil && hex.EncodeToString(m) == "000003010203",
		"err=%v got=%s", err, hex.EncodeToString(m))

	_, err = tls.Marshal(v2{D: []byte{1, 2, 3}})
	t.Check("vector-over-maxlen-rejected", err != nil, "tls.Marshal accepted 3 bytes into maxlen:2")

	ds := tls.DigitallySigned{
		Algorithm: tls.SignatureAndHashAlgorithm{Hash: tls.SHA256, Signature: tls.ECDSA},
		Signature: []byte{0xde, 0xad, 0xbe, 0xef},
	}
	dm, err := tls.Marshal(ds)
	t.Check("digitally-signed-encoding-exact",
		err == nil && hex.EncodeToString(dm) == "04030004deadbeef",
		"err=%v got=%s", err, hex.EncodeToString(dm))

	var dback tls.DigitallySigned
	drest, derr := tls.Unmarshal(dm, &dback)
	t.Check("digitally-signed-roundtrip",
		derr == nil && len(drest) == 0 &&
			dback.Algorithm.Hash == tls.SHA256 && dback.Algorithm.Signature == tls.ECDSA &&
			hex.EncodeToString(dback.Signature) == "deadbeef",
		"err=%v rest=%d got=%+v", derr, len(drest), dback)

	// The entry_type field selects which arm of TimestampedEntry follows; a
	// value no arm claims must be refused rather than silently skipped.
	bad := append([]byte(nil), enc...)
	bad[10], bad[11] = 0x00, 0x09
	var junk ct.MerkleTreeLeaf
	_, err = tls.Unmarshal(bad, &junk)
	t.Check("unknown-entry-type-rejected", err != nil, "tls.Unmarshal accepted entry_type 9")

	var trunc ct.MerkleTreeLeaf
	_, err = tls.Unmarshal(enc[:len(enc)-1], &trunc)
	t.Check("truncated-input-rejected", err != nil, "tls.Unmarshal accepted a truncated leaf")

	// --- the precertificate MerkleTreeLeaf ----------------------------------
	pl, err := ct.MerkleTreeLeafFromRawChain(
		[]ct.ASN1Cert{{Data: preDER}, {Data: intDER}}, ct.PrecertLogEntryType, pinnedTS)
	got := "error"
	if err == nil {
		pb, merr := tls.Marshal(*pl)
		if merr == nil && pl.TimestampedEntry != nil && pl.TimestampedEntry.PrecertEntry != nil {
			got = sha(pb) + "/" + hex.EncodeToString(pl.TimestampedEntry.PrecertEntry.IssuerKeyHash[:])
		}
	}
	t.Equal("precert-merkle-tree-leaf-exact", got, wantPrecertSHA+"/"+wantIssuerHash)

	t.Done()
}

func itoa(n int) string {
	if n == 0 {
		return "0"
	}
	var b [24]byte
	i := len(b)
	for n > 0 {
		i--
		b[i] = byte('0' + n%10)
		n /= 10
	}
	return string(b[i:])
}

func unhex(t *vhelp.T, s string) []byte {
	b, err := hex.DecodeString(s)
	if err != nil {
		t.Fatalf("bad pinned hex constant: %v", err)
	}
	return b
}
