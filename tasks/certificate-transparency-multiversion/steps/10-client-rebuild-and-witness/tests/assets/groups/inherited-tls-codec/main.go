// Group inherited-tls-codec.
//
// The RFC 5246 presentation-language codec that every signature in this
// product is computed over. v1.1.3 builds a new service, the witness, directly
// on top of it: the countersignature it returns is made over tls.Marshal of a
// whole SignedTreeHead, so a drift in this encoding would silently change what
// a witness signature means. Nothing about the codec is supposed to have moved
// at this milestone, and this group says so with known answers derived here
// from the wire format rather than read back from the product.
package main

import (
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/tls"

	"ctverify/vhelp"
)

func mustHex(s string) []byte {
	b, err := hex.DecodeString(s)
	if err != nil {
		panic(err)
	}
	return b
}

// The pinned tree head. 1500000000000 is 0x0000015d3ef79800 as a big-endian
// uint64; 4321 is 0x00000000000010e1.
const (
	pinnedTimestamp = uint64(1500000000000)
	pinnedTreeSize  = uint64(4321)
)

var pinnedRoot = mustHex("5f1c2a8b0d4e6f30112233445566778899aabbccddeeff00102030405060708a")

func main() {
	t := vhelp.New()

	var rh ct.SHA256Hash
	copy(rh[:], pinnedRoot)

	// A DigitallySigned is hash(1) signature(1) then a uint16-length-prefixed
	// opaque: SHA256 is 4, ECDSA is 3, and the body is four bytes.
	sig := ct.DigitallySigned{
		Algorithm: tls.SignatureAndHashAlgorithm{Hash: tls.SHA256, Signature: tls.ECDSA},
		Signature: mustHex("deadbeef"),
	}
	sigWire := "04030004deadbeef"
	got, err := tls.Marshal(sig)
	if err != nil {
		t.Fatalf("marshal DigitallySigned: %v", err)
	}
	t.Equal("digitally-signed-encoding", hex.EncodeToString(got), sigWire)

	b64, err := sig.Base64String()
	if err != nil {
		t.Fatalf("Base64String: %v", err)
	}
	t.Equal("digitally-signed-base64", b64, base64.StdEncoding.EncodeToString(mustHex(sigWire)))

	var back ct.DigitallySigned
	rest, err := tls.Unmarshal(mustHex(sigWire), &back)
	t.Check("digitally-signed-roundtrip",
		err == nil && len(rest) == 0 &&
			back.Algorithm.Hash == tls.SHA256 && back.Algorithm.Signature == tls.ECDSA &&
			hex.EncodeToString(back.Signature) == "deadbeef",
		"err=%v rest=%x back=%+v", err, rest, back)

	// Unmarshal consumes exactly one value and hands back what follows.
	rest, err = tls.Unmarshal(mustHex(sigWire+"aabb"), &back)
	t.Check("unmarshal-returns-trailing-bytes", err == nil && hex.EncodeToString(rest) == "aabb",
		"err=%v rest=%x", err, rest)

	// A truncated opaque body is not silently accepted.
	_, err = tls.Unmarshal(mustHex("04030004dead"), &back)
	t.Check("truncated-opaque-is-rejected", err != nil, "truncated DigitallySigned was accepted")

	// An opaque body longer than its declared maximum cannot be encoded.
	_, err = tls.Marshal(ct.DigitallySigned{
		Algorithm: tls.SignatureAndHashAlgorithm{Hash: tls.SHA256, Signature: tls.ECDSA},
		Signature: make([]byte, 70000),
	})
	t.Check("oversized-opaque-is-rejected", err != nil, "a 70000-byte signature was encoded")

	// TreeHeadSignature: version(1) signature_type(1) timestamp(8)
	// tree_size(8) root(32). version V1 is 0, tree_hash is 1.
	thsWire := "0001" + "0000015d3ef79800" + "00000000000010e1" +
		"5f1c2a8b0d4e6f30112233445566778899aabbccddeeff00102030405060708a"
	got, err = tls.Marshal(ct.TreeHeadSignature{
		Version: ct.V1, SignatureType: ct.TreeHashSignatureType,
		Timestamp: pinnedTimestamp, TreeSize: pinnedTreeSize, SHA256RootHash: rh,
	})
	if err != nil {
		t.Fatalf("marshal TreeHeadSignature: %v", err)
	}
	t.Equal("tree-head-signature-encoding", hex.EncodeToString(got), thsWire)

	sth := ct.SignedTreeHead{
		Version: ct.V1, TreeSize: pinnedTreeSize, Timestamp: pinnedTimestamp,
		SHA256RootHash: rh, TreeHeadSignature: sig,
	}

	// SerializeSTHSignatureInput builds exactly that structure from an STH.
	in, err := ct.SerializeSTHSignatureInput(sth)
	if err != nil {
		t.Fatalf("SerializeSTHSignatureInput: %v", err)
	}
	t.Equal("sth-signature-input-is-the-tree-head-signature", hex.EncodeToString(in), thsWire)

	// A version the signature scheme is not defined for has no encoding.
	_, err = ct.SerializeSTHSignatureInput(ct.SignedTreeHead{Version: 2, SHA256RootHash: rh})
	t.Check("unsupported-sth-version-has-no-signature-input", err != nil,
		"an STH claiming version 2 was serialised")

	// tls.Marshal of a whole SignedTreeHead -- the witness cosignature's
	// preimage -- is tree_size(8) timestamp(8) root(32) signature log_id(32).
	// The Version field carries no size tag, so it contributes nothing: the
	// encoding begins with the tree size.
	sthWire := "00000000000010e1" + "0000015d3ef79800" +
		"5f1c2a8b0d4e6f30112233445566778899aabbccddeeff00102030405060708a" +
		sigWire +
		"0000000000000000000000000000000000000000000000000000000000000000"
	got, err = tls.Marshal(sth)
	if err != nil {
		t.Fatalf("marshal SignedTreeHead: %v", err)
	}
	t.Equal("signed-tree-head-encoding-omits-the-version", hex.EncodeToString(got), sthWire)
	t.Equal("signed-tree-head-encoding-length", len(got), 88)

	// A MerkleTreeLeaf for an X.509 entry: version(1) leaf_type(1)
	// timestamp(8) entry_type(2) uint24-length-prefixed cert extensions(2).
	leafDER := vhelp.ReadFixture("minted", "leaf-valid.der")
	mtl := ct.MerkleTreeLeaf{
		Version: ct.V1, LeafType: ct.TimestampedEntryLeafType,
		TimestampedEntry: &ct.TimestampedEntry{
			Timestamp: pinnedTimestamp, EntryType: ct.X509LogEntryType,
			X509Entry: &ct.ASN1Cert{Data: leafDER},
		},
	}
	leafWire, err := tls.Marshal(mtl)
	if err != nil {
		t.Fatalf("marshal MerkleTreeLeaf: %v", err)
	}
	t.Equal("merkle-tree-leaf-encoding-length", len(leafWire), 2+8+2+3+len(leafDER)+2)
	wantHead := "0000" + "0000015d3ef79800" + "0000" +
		hex.EncodeToString([]byte{byte(len(leafDER) >> 16), byte(len(leafDER) >> 8), byte(len(leafDER))})
	t.Equal("merkle-tree-leaf-encoding-header", hex.EncodeToString(leafWire[:15]), wantHead)
	t.Equal("merkle-tree-leaf-carries-the-der-then-empty-extensions",
		hex.EncodeToString(leafWire[15:]), hex.EncodeToString(leafDER)+"0000")

	// SHA-256 over that encoding is a fixed value for a fixed fixture, and it
	// is what an inclusion proof is ultimately anchored to.
	sum := sha256.Sum256(leafWire)
	t.Equal("merkle-tree-leaf-sha256", hex.EncodeToString(sum[:]),
		"13fd1759acfd111b176958a692769fdd7f05aa868c092eb9346be9f440f411bf")

	// An SCT: version(1) log_id(32) timestamp(8) extensions(2) signature.
	sct := ct.SignedCertificateTimestamp{
		SCTVersion: ct.V1, Timestamp: pinnedTimestamp,
		Extensions: ct.CTExtensions{}, Signature: sig,
	}
	copy(sct.LogID.KeyID[:], mustHex("0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20"))
	got, err = tls.Marshal(sct)
	if err != nil {
		t.Fatalf("marshal SCT: %v", err)
	}
	t.Equal("sct-encoding", hex.EncodeToString(got),
		"00"+"0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20"+
			"0000015d3ef79800"+"0000"+sigWire)

	// The SCT's signature input drops the log ID and the signature and adds a
	// signature_type of 0, followed by the timestamped entry.
	in, err = ct.SerializeSCTSignatureInput(sct, ct.LogEntry{Leaf: mtl})
	if err != nil {
		t.Fatalf("SerializeSCTSignatureInput: %v", err)
	}
	t.Equal("sct-signature-input-encoding", hex.EncodeToString(in),
		"0000"+"0000015d3ef79800"+"0000"+
			hex.EncodeToString([]byte{byte(len(leafDER) >> 16), byte(len(leafDER) >> 8), byte(len(leafDER))})+
			hex.EncodeToString(leafDER)+"0000")

	// A two-byte prefix is not a MerkleTreeLeaf.
	var badLeaf ct.MerkleTreeLeaf
	_, err = tls.Unmarshal(mustHex("0009"), &badLeaf)
	t.Check("truncated-merkle-tree-leaf-is-rejected", err != nil, "a two-byte leaf was accepted")

	t.Done()
}
