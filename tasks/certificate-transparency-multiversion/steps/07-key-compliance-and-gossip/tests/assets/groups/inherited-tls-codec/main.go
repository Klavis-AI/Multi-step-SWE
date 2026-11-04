// Group inherited-tls-codec.
//
// The RFC 5246 presentation codec in tls/ is unchanged at this milestone, and
// step 07 puts fresh weight on it: the new gossip/minimal x509ext STH
// extension is a TLS-encoded structure carried inside a certificate, and the
// new mirror STH path re-serves a tree head that was TLS-encoded before it
// was signed. This group pins the wire forms all of that rests on -- the
// 24-bit opaque vector, the nested vector-of-vectors a certificate chain
// encodes to, and the DigitallySigned structure -- with known-answer vectors,
// so a change reaching into any of them cannot pass unnoticed.
//
// It is an inherited-obligation group: it is supposed to pass against the
// previous milestone too, and it does.
//
// SCOPE-TIME: no clock, no network. Every vector is a pinned constant.
package main

import (
	"encoding/hex"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/tls"

	"ctverify/vhelp"
)

// The three vector widths this repo declares, written the way both this repo
// and Trillian declare them: the prefix is the smallest width that fits maxlen.
type wideVector struct {
	V []byte `tls:"minlen:0,maxlen:16777215"`
}

type mediumVector struct {
	V []byte `tls:"minlen:0,maxlen:65535"`
}

func main() {
	t := vhelp.New()

	// --- 1. a 24-bit-max vector takes a three-byte big-endian length --------
	// This is the width extra data and every ASN1Cert use.
	wide, err := tls.Marshal(wideVector{V: []byte{0xde, 0xad, 0xbe, 0xef}})
	if err != nil {
		t.Check("twenty-four-bit-vector-length-prefix", false, "Marshal: %v", err)
	} else {
		t.Equal("twenty-four-bit-vector-length-prefix", hex.EncodeToString(wide), "000004deadbeef")
	}

	// --- 2. and a 16-bit-max vector takes two bytes -------------------------
	medium, err := tls.Marshal(mediumVector{V: []byte{0xde, 0xad, 0xbe, 0xef}})
	if err != nil {
		t.Check("sixteen-bit-vector-length-prefix", false, "Marshal: %v", err)
	} else {
		t.Equal("sixteen-bit-vector-length-prefix", hex.EncodeToString(medium), "0004deadbeef")
	}

	// --- 3. a chain is a vector of vectors ----------------------------------
	// Exactly the shape ExtraDataForChain produces; here over two-byte and
	// one-byte "certificates" so the nesting is legible.
	chain, err := tls.Marshal(ct.CertificateChain{Entries: []ct.ASN1Cert{
		{Data: []byte{0xaa}},
		{Data: []byte{0xbb, 0xcc}},
	}})
	if err != nil {
		t.Check("certificate-chain-is-a-vector-of-vectors", false, "Marshal: %v", err)
	} else {
		t.Equal("certificate-chain-is-a-vector-of-vectors",
			hex.EncodeToString(chain), "000009000001aa000002bbcc")
	}

	// --- 4. DigitallySigned is algorithm, algorithm, then the signature -----
	// The structure ToSignedTreeHead decodes out of every get-sth response.
	ds := tls.DigitallySigned{
		Algorithm: tls.SignatureAndHashAlgorithm{Hash: tls.SHA256, Signature: tls.ECDSA},
		Signature: []byte{0xde, 0xad, 0xbe, 0xef},
	}
	dsBytes, err := tls.Marshal(ds)
	if err != nil {
		t.Check("digitally-signed-wire-form", false, "Marshal: %v", err)
	} else {
		t.Equal("digitally-signed-wire-form", hex.EncodeToString(dsBytes), "04030004deadbeef")
	}

	// --- 5. a truncated buffer is rejected, not silently accepted -----------
	var short wideVector
	_, terr := tls.Unmarshal([]byte{0x00, 0x00, 0x04, 0xde, 0xad}, &short)
	t.Check("truncated-buffer-is-rejected", terr != nil, "Unmarshal accepted a short buffer")

	// --- 6. and a Merkle tree leaf survives a round trip byte for byte ------
	leafDER := vhelp.ReadFixture("minted", "leaf-valid.der")
	leaf := ct.CreateX509MerkleTreeLeaf(ct.ASN1Cert{Data: leafDER}, uint64(1519737600000))
	leafBytes, err := tls.Marshal(*leaf)
	if err != nil {
		t.Check("merkle-tree-leaf-round-trips-byte-for-byte", false, "Marshal: %v", err)
	} else {
		var reread ct.MerkleTreeLeaf
		rest, uerr := tls.Unmarshal(leafBytes, &reread)
		again, merr := tls.Marshal(reread)
		t.Equal("merkle-tree-leaf-round-trips-byte-for-byte",
			hex.EncodeToString(again)+" unmarshalErr="+errStr(uerr)+" marshalErr="+errStr(merr)+" trailing="+itoa(len(rest)),
			hex.EncodeToString(leafBytes)+" unmarshalErr=<nil> marshalErr=<nil> trailing=0")
	}

	t.Done()
}

func errStr(err error) string {
	if err == nil {
		return "<nil>"
	}
	return err.Error()
}

func itoa(n int) string {
	if n == 0 {
		return "0"
	}
	out := ""
	for n > 0 {
		out = string(rune('0'+n%10)) + out
		n /= 10
	}
	return out
}
