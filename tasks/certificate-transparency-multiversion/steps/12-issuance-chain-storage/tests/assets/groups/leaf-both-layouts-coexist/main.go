// Observes the contract that both leaf layouts exist at once: this release
// adds BuildLogLeafWithChainHash beside BuildLogLeaf, and the two must agree
// on everything the log is indexed and deduplicated by while disagreeing on
// exactly one field. A log that switched wholesale to the new layout, or that
// changed the identity hash when it did, would break every existing entry.
//
// SCOPE-TIME: the Merkle leaf below carries a pinned timestamp, so every
// encoded byte is fixed.
package main

import (
	"bytes"
	"crypto/sha256"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/tls"
	"github.com/google/certificate-transparency-go/trillian/util"

	"ctverify/vhelp"
)

const (
	pinnedTimestampMS = uint64(1500000000000)
	pinnedLeafIndex   = int64(4242)
	logPrefix         = "bench"
)

var pinnedHash = []byte{
	0x9f, 0x86, 0xd0, 0x81, 0x88, 0x4c, 0x7d, 0x65, 0x9a, 0x2f, 0xea, 0xa0,
	0xc5, 0x5a, 0xd0, 0x15, 0xa3, 0xbf, 0x4f, 0x1b, 0x2b, 0x0b, 0x82, 0x2c,
	0xd1, 0x5d, 0x6c, 0x15, 0xb0, 0xf0, 0x0a, 0x08,
}

func u24(n int) []byte { return []byte{byte(n >> 16), byte(n >> 8), byte(n)} }

func main() {
	t := vhelp.New()

	leafDER := vhelp.ReadFixture("minted", "leaf-valid.der")
	intDER := vhelp.ReadFixture("minted", "bench-intermediate.der")
	preDER := vhelp.ReadFixture("minted", "precert-poisoned.der")

	cert := ct.ASN1Cert{Data: leafDER}
	chain := []ct.ASN1Cert{{Data: intDER}}

	mtl := ct.MerkleTreeLeaf{
		Version:  ct.V1,
		LeafType: ct.TimestampedEntryLeafType,
		TimestampedEntry: &ct.TimestampedEntry{
			Timestamp: pinnedTimestampMS,
			EntryType: ct.X509LogEntryType,
			X509Entry: &ct.ASN1Cert{Data: leafDER},
		},
	}
	wantLeafValue, err := tls.Marshal(mtl)
	if err != nil {
		t.Fatalf("marshal Merkle leaf: %v", err)
	}
	wantIDHash := sha256.Sum256(leafDER)

	classic, err := util.BuildLogLeaf(logPrefix, mtl, pinnedLeafIndex, cert, chain, false)
	if err != nil {
		t.Fatalf("BuildLogLeaf: %v", err)
	}
	hashed, err := util.BuildLogLeafWithChainHash(logPrefix, mtl, pinnedLeafIndex, cert, pinnedHash, false)
	if err != nil {
		t.Fatalf("BuildLogLeafWithChainHash: %v", err)
	}

	wantClassicExtra := append(u24(len(intDER)+3), u24(len(intDER))...)
	wantClassicExtra = append(wantClassicExtra, intDER...)
	t.EqualBytes("classic-extra-data-is-the-full-chain", classic.ExtraData, wantClassicExtra)
	t.EqualBytes("hashed-extra-data-is-the-chain-hash", hashed.ExtraData, append([]byte{0x00, 0x20}, pinnedHash...))

	t.Check("leaf-value-is-the-same-in-both-layouts",
		bytes.Equal(classic.LeafValue, wantLeafValue) && bytes.Equal(hashed.LeafValue, wantLeafValue),
		"classic LeafValue %d bytes, hashed %d bytes, want the same %d bytes", len(classic.LeafValue), len(hashed.LeafValue), len(wantLeafValue))

	t.Check("leaf-identity-hash-is-the-same-in-both-layouts",
		bytes.Equal(classic.LeafIdentityHash, wantIDHash[:]) && bytes.Equal(hashed.LeafIdentityHash, wantIDHash[:]),
		"classic %x, hashed %x, want %x", classic.LeafIdentityHash, hashed.LeafIdentityHash, wantIDHash[:])

	t.Check("leaf-index-is-the-same-in-both-layouts",
		classic.LeafIndex == pinnedLeafIndex && hashed.LeafIndex == pinnedLeafIndex,
		"classic index %d, hashed index %d, want %d", classic.LeafIndex, hashed.LeafIndex, pinnedLeafIndex)

	// An empty chain and an empty chain *hash* must not produce the same extra
	// data, or a reader could not tell the layouts apart. The two encoders are
	// compared directly rather than through the two leaf builders: which
	// builder a zero-length hash reaches is an implementation choice this
	// contract does not fix, but what each encoder produces for an empty
	// argument is fixed by TLS-CODEC.md section 5.4 -- a 24-bit prefix for the
	// chain, a 16-bit one for the hash.
	emptyChain, err := util.ExtraDataForChain(cert, nil, false)
	if err != nil {
		t.Fatalf("ExtraDataForChain(nil chain): %v", err)
	}
	emptyHash, err := util.ExtraDataForChainHash(cert, []byte{}, false)
	if err != nil {
		t.Fatalf("ExtraDataForChainHash(empty hash): %v", err)
	}
	t.Check("empty-chain-and-empty-hash-encode-differently",
		bytes.Equal(emptyChain, []byte{0x00, 0x00, 0x00}) && bytes.Equal(emptyHash, []byte{0x00, 0x00}),
		"empty chain gave %x (want 000000), empty hash gave %x (want 0000)", emptyChain, emptyHash)

	// Precertificate entries keep the same split.
	preCert := ct.ASN1Cert{Data: preDER}
	preMTL := ct.MerkleTreeLeaf{
		Version:  ct.V1,
		LeafType: ct.TimestampedEntryLeafType,
		TimestampedEntry: &ct.TimestampedEntry{
			Timestamp: pinnedTimestampMS,
			EntryType: ct.PrecertLogEntryType,
			PrecertEntry: &ct.PreCert{
				IssuerKeyHash:  sha256.Sum256(intDER),
				TBSCertificate: []byte{0x30, 0x03, 0x02, 0x01, 0x01},
			},
		},
	}
	preClassic, err := util.BuildLogLeaf(logPrefix, preMTL, pinnedLeafIndex, preCert, chain, true)
	if err != nil {
		t.Fatalf("BuildLogLeaf(precert): %v", err)
	}
	preHashed, err := util.BuildLogLeafWithChainHash(logPrefix, preMTL, pinnedLeafIndex, preCert, pinnedHash, true)
	if err != nil {
		t.Fatalf("BuildLogLeafWithChainHash(precert): %v", err)
	}
	wantPreClassic := append(u24(len(preDER)), preDER...)
	wantPreClassic = append(wantPreClassic, u24(len(intDER)+3)...)
	wantPreClassic = append(wantPreClassic, u24(len(intDER))...)
	wantPreClassic = append(wantPreClassic, intDER...)
	wantPreHashed := append(u24(len(preDER)), preDER...)
	wantPreHashed = append(wantPreHashed, 0x00, 0x20)
	wantPreHashed = append(wantPreHashed, pinnedHash...)
	t.Check("precert-layouts-coexist",
		bytes.Equal(preClassic.ExtraData, wantPreClassic) && bytes.Equal(preHashed.ExtraData, wantPreHashed) &&
			bytes.Equal(preClassic.LeafValue, preHashed.LeafValue),
		"precert classic extra %d bytes (want %d), hashed extra %d bytes (want %d)",
		len(preClassic.ExtraData), len(wantPreClassic), len(preHashed.ExtraData), len(wantPreHashed))

	t.Done()
}
