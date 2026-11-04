// Group inherited-tls-codec.
//
// The RFC 5246 TLS presentation codec in tls/ is unchanged at this milestone
// but is load-bearing for everything the milestone does move: every leaf hash,
// every SCT signature, and every proof traversal is built on it. This group
// pins the codec's wire format with known-answer vectors so that a refactor
// reaching into the leaf-hash path cannot silently change an encoding.
//
// SCOPE-TIME: no clock, no network. Every vector is a pinned constant.
package main

import (
	"encoding/hex"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/tls"

	"ctverify/vhelp"
)

// minlen/maxlen are how both this repo and Trillian declare a length-prefixed
// vector; the prefix width is the smallest that fits maxlen.
type opaqueVector struct {
	V []byte `tls:"minlen:0,maxlen:65535"`
}

type shortVector struct {
	V []byte `tls:"minlen:0,maxlen:255"`
}

func main() {
	t := vhelp.New()

	// --- 1. a 2-byte-max vector is prefixed by a 2-byte big-endian length ----
	got, err := tls.Marshal(opaqueVector{V: []byte{0xde, 0xad, 0xbe, 0xef}})
	if err != nil {
		t.Check("two-byte-length-prefix", false, "Marshal: %v", err)
	} else {
		t.Equal("two-byte-length-prefix", hex.EncodeToString(got), "0004deadbeef")
	}

	// --- 2. a 1-byte-max vector is prefixed by a single byte ----------------
	got2, err := tls.Marshal(shortVector{V: []byte{0xde, 0xad, 0xbe, 0xef}})
	if err != nil {
		t.Check("one-byte-length-prefix", false, "Marshal: %v", err)
	} else {
		t.Equal("one-byte-length-prefix", hex.EncodeToString(got2), "04deadbeef")
	}

	// --- 3. the round trip returns the original bytes -----------------------
	var back opaqueVector
	if _, err := tls.Unmarshal(got, &back); err != nil {
		t.Check("vector-round-trips", false, "Unmarshal: %v", err)
	} else {
		t.EqualBytes("vector-round-trips", back.V, []byte{0xde, 0xad, 0xbe, 0xef})
	}

	// --- 4. uint64 is big-endian on the wire --------------------------------
	u, err := tls.Marshal(uint64(0x0102030405060708))
	if err != nil {
		t.Check("uint64-is-big-endian", false, "Marshal: %v", err)
	} else {
		t.Equal("uint64-is-big-endian", hex.EncodeToString(u), "0102030405060708")
	}

	// --- 5. a truncated buffer is rejected, not silently accepted -----------
	var short opaqueVector
	_, err = tls.Unmarshal([]byte{0x00, 0x04, 0xde, 0xad}, &short)
	t.Check("truncated-buffer-is-rejected", err != nil, "Unmarshal accepted a short buffer")

	// --- 6/7. the MerkleTreeLeaf wire format is stable ----------------------
	// This is the structure ct.LeafHashForLeaf hashes, so its encoding is the
	// contract the whole milestone rests on. Version and leaf type lead the
	// struct, followed by the timestamped entry.
	leafDER := vhelp.ReadFixture("minted", "leaf-valid.der")
	leaf := ct.CreateX509MerkleTreeLeaf(ct.ASN1Cert{Data: leafDER}, uint64(1519737600000))
	leafBytes, err := tls.Marshal(*leaf)
	if err != nil {
		t.Check("merkletreeleaf-leads-with-version-and-type", false, "Marshal: %v", err)
	} else {
		t.Check("merkletreeleaf-leads-with-version-and-type",
			len(leafBytes) > 2 && leafBytes[0] == 0x00 && leafBytes[1] == 0x00,
			"prefix: %s", hex.EncodeToString(leafBytes[:min(4, len(leafBytes))]))

		var reread ct.MerkleTreeLeaf
		if _, err := tls.Unmarshal(leafBytes, &reread); err != nil {
			t.Check("merkletreeleaf-round-trips", false, "Unmarshal: %v", err)
		} else {
			t.Check("merkletreeleaf-round-trips",
				reread.Version == leaf.Version &&
					reread.LeafType == leaf.LeafType &&
					reread.TimestampedEntry.Timestamp == leaf.TimestampedEntry.Timestamp,
				"leaf fields drifted across a round trip")
		}
	}

	t.Done()
}

func min(a, b int) int {
	if a < b {
		return a
	}
	return b
}
