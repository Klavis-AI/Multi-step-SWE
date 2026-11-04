// Group inherited-tls-codec.
//
// The RFC 5246 presentation-language codec underneath everything this product
// signs, re-run at v1.3.3. This release rewrites the handlers that build and
// serve leaves, adds a ninth entrypoint and changes how a leaf reaches the
// backend, so the milestone is only additive if the wire shapes those
// handlers produce are byte-for-byte the ones they were: the width a length
// prefix gets from its declared maximum, the width an enum gets from maxval,
// the fixed-width fields that get no prefix at all, the selector-driven
// union, the refusal to marshal a value outside its declared bounds, and the
// two signature inputs of RFC 6962 sections 3.2 and 3.5.
//
// Every expectation is written out as literal bytes assembled by this driver
// from those rules, never by asking the product to encode and decode its own
// output, and nothing this release added is touched -- so the group says the
// same thing at the previous milestone as it does here.
package main

import (
	"bytes"
	"crypto/sha256"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/tls"

	"ctverify/vhelp"
)

const pinnedTimestamp = uint64(0x0123456789abcdef)

// widths is a driver-owned struct exercising the three integer widths the
// codec derives from a tag rather than from the Go type.
type widths struct {
	Small  tls.Enum `tls:"maxval:255"`
	Medium tls.Enum `tls:"maxval:65535"`
	Three  tls.Uint24
	Fixed  uint32
}

// bounded exercises minlen/maxlen on an opaque vector.
type bounded struct {
	Body []byte `tls:"minlen:2,maxlen:4"`
}

func u24(n int) []byte { return []byte{byte(n >> 16), byte(n >> 8), byte(n)} }

func u16(n int) []byte { return []byte{byte(n >> 8), byte(n)} }

func u64(n uint64) []byte {
	return []byte{byte(n >> 56), byte(n >> 48), byte(n >> 40), byte(n >> 32),
		byte(n >> 24), byte(n >> 16), byte(n >> 8), byte(n)}
}

// marshalled runs tls.Marshal and records a failure rather than aborting, so
// one broken shape does not cost the whole group.
func marshalled(t *vhelp.T, id string, v interface{}) ([]byte, bool) {
	b, err := tls.Marshal(v)
	if err != nil {
		t.Check(id, false, "tls.Marshal: %v", err)
		return nil, false
	}
	return b, true
}

func main() {
	t := vhelp.New()

	// Two pinned DER bodies. They only have to be distinguishable byte
	// strings; the codec does not look inside them.
	derA := bytes.Repeat([]byte{0xa1}, 7)
	derB := bytes.Repeat([]byte{0xb2}, 3)
	issuerKeyHash := sha256.Sum256([]byte("harbor-ct-bench-pinned-issuer-key"))
	rootHash := sha256.Sum256([]byte("harbor-ct-bench-pinned-root"))

	// 1. Integer widths come from the tag, not from the Go type.
	if b, ok := marshalled(t, "integer-widths-follow-the-tag",
		widths{Small: 0x7f, Medium: 0x0102, Three: 0x030405, Fixed: 0x06070809}); ok {
		t.EqualBytes("integer-widths-follow-the-tag", b,
			[]byte{0x7f, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09})
	}

	// 2. An enum above its declared maximum is refused.
	_, err := tls.Marshal(widths{Small: 0x100})
	t.Check("enum-above-maxval-rejected", err != nil,
		"tls.Marshal accepted an Enum of 256 in a maxval:255 field")

	// 3. minlen/maxlen are enforced in both directions.
	_, tooShort := tls.Marshal(bounded{Body: []byte{0x01}})
	_, tooLong := tls.Marshal(bounded{Body: []byte{1, 2, 3, 4, 5}})
	inRange, okRange := tls.Marshal(bounded{Body: []byte{0x01, 0x02, 0x03}})
	t.Check("opaque-vector-bounds-enforced",
		tooShort != nil && tooLong != nil && okRange == nil &&
			bytes.Equal(inRange, []byte{0x03, 0x01, 0x02, 0x03}),
		"short=%v long=%v inRange=%x (%v)", tooShort, tooLong, inRange, okRange)

	// 4. ASN1Cert is a uint24-prefixed opaque vector, and refuses to be empty.
	if b, ok := marshalled(t, "asn1cert-uses-a-uint24-length-prefix", ct.ASN1Cert{Data: derA}); ok {
		t.EqualBytes("asn1cert-uses-a-uint24-length-prefix", b, append(u24(len(derA)), derA...))
	}
	_, err = tls.Marshal(ct.ASN1Cert{Data: nil})
	t.Check("asn1cert-minlen-1-rejects-empty", err != nil,
		"tls.Marshal accepted a zero-length ASN1Cert")

	// 5. A vector of them carries its own uint24 total length on top.
	var want bytes.Buffer
	inner := append(append(u24(len(derA)), derA...), append(u24(len(derB)), derB...)...)
	want.Write(u24(len(inner)))
	want.Write(inner)
	if b, ok := marshalled(t, "certificate-chain-vector-encoding",
		ct.CertificateChain{Entries: []ct.ASN1Cert{{Data: derA}, {Data: derB}}}); ok {
		t.EqualBytes("certificate-chain-vector-encoding", b, want.Bytes())
	}

	// 6. The MerkleTreeLeaf union selects the x509 arm on EntryType 0.
	want.Reset()
	want.Write([]byte{0x00, 0x00}) // version V1, leaf type timestamped_entry
	want.Write(u64(pinnedTimestamp))
	want.Write(u16(0)) // X509LogEntryType
	want.Write(u24(len(derA)))
	want.Write(derA)
	want.Write(u16(0)) // empty extensions
	if b, ok := marshalled(t, "merkle-tree-leaf-selects-the-x509-arm", ct.MerkleTreeLeaf{
		Version:  ct.V1,
		LeafType: ct.TimestampedEntryLeafType,
		TimestampedEntry: &ct.TimestampedEntry{
			Timestamp: pinnedTimestamp,
			EntryType: ct.X509LogEntryType,
			X509Entry: &ct.ASN1Cert{Data: derA},
		},
	}); ok {
		t.EqualBytes("merkle-tree-leaf-selects-the-x509-arm", b, want.Bytes())
	}

	// 7. ... and the precert arm on EntryType 1, where the issuer key hash is
	//    32 fixed bytes with no length prefix at all.
	want.Reset()
	want.Write([]byte{0x00, 0x00})
	want.Write(u64(pinnedTimestamp))
	want.Write(u16(1)) // PrecertLogEntryType
	want.Write(issuerKeyHash[:])
	want.Write(u24(len(derB)))
	want.Write(derB)
	want.Write(u16(0))
	if b, ok := marshalled(t, "merkle-tree-leaf-selects-the-precert-arm", ct.MerkleTreeLeaf{
		Version:  ct.V1,
		LeafType: ct.TimestampedEntryLeafType,
		TimestampedEntry: &ct.TimestampedEntry{
			Timestamp:    pinnedTimestamp,
			EntryType:    ct.PrecertLogEntryType,
			PrecertEntry: &ct.PreCert{IssuerKeyHash: issuerKeyHash, TBSCertificate: derB},
		},
	}); ok {
		t.EqualBytes("merkle-tree-leaf-selects-the-precert-arm", b, want.Bytes())
	}

	// 8. A selector value with no arm is refused rather than silently skipped.
	_, err = tls.Marshal(ct.MerkleTreeLeaf{
		Version:  ct.V1,
		LeafType: ct.TimestampedEntryLeafType,
		TimestampedEntry: &ct.TimestampedEntry{
			Timestamp: pinnedTimestamp,
			EntryType: ct.LogEntryType(7),
		},
	})
	t.Check("unknown-selector-value-rejected", err != nil,
		"tls.Marshal accepted entry type 7, which has no arm")

	// 9. DigitallySigned: two one-byte algorithm identifiers then a
	//    uint16-prefixed signature.
	sig := []byte{0xde, 0xad, 0xbe, 0xef}
	want.Reset()
	want.Write([]byte{0x04, 0x03}) // SHA256, ECDSA
	want.Write(u16(len(sig)))
	want.Write(sig)
	if b, ok := marshalled(t, "digitally-signed-exact-encoding", tls.DigitallySigned{
		Algorithm: tls.SignatureAndHashAlgorithm{Hash: tls.SHA256, Signature: tls.ECDSA},
		Signature: sig,
	}); ok {
		t.EqualBytes("digitally-signed-exact-encoding", b, want.Bytes())
	}

	// 10. The RFC 6962 3.2 SCT signature input, assembled independently.
	want.Reset()
	want.Write([]byte{0x00, 0x00}) // version V1, signature type certificate_timestamp
	want.Write(u64(pinnedTimestamp))
	want.Write(u16(0))
	want.Write(u24(len(derA)))
	want.Write(derA)
	want.Write(u16(0))
	gotSCT, err := ct.SerializeSCTSignatureInput(
		ct.SignedCertificateTimestamp{SCTVersion: ct.V1, Timestamp: pinnedTimestamp},
		ct.LogEntry{Leaf: ct.MerkleTreeLeaf{
			Version:  ct.V1,
			LeafType: ct.TimestampedEntryLeafType,
			TimestampedEntry: &ct.TimestampedEntry{
				Timestamp: pinnedTimestamp,
				EntryType: ct.X509LogEntryType,
				X509Entry: &ct.ASN1Cert{Data: derA},
			},
		}})
	if err != nil {
		t.Check("sct-signature-input-is-the-rfc6962-one", false, "SerializeSCTSignatureInput: %v", err)
	} else {
		t.EqualBytes("sct-signature-input-is-the-rfc6962-one", gotSCT, want.Bytes())
	}

	// 11. The RFC 6962 3.5 STH signature input: signature type 1, then the
	//     timestamp, the tree size and 32 unprefixed hash bytes.
	want.Reset()
	want.Write([]byte{0x00, 0x01})
	want.Write(u64(pinnedTimestamp))
	want.Write(u64(0x1122334455667788))
	want.Write(rootHash[:])
	gotSTH, err := ct.SerializeSTHSignatureInput(ct.SignedTreeHead{
		Version:        ct.V1,
		Timestamp:      pinnedTimestamp,
		TreeSize:       0x1122334455667788,
		SHA256RootHash: rootHash,
	})
	if err != nil {
		t.Check("sth-signature-input-is-the-rfc6962-one", false, "SerializeSTHSignatureInput: %v", err)
	} else {
		t.EqualBytes("sth-signature-input-is-the-rfc6962-one", gotSTH, want.Bytes())
	}

	// 12. Truncation is an error and trailing bytes are handed back, not eaten.
	full := append(u24(len(derA)), derA...)
	var cert ct.ASN1Cert
	_, truncErr := tls.Unmarshal(full[:len(full)-1], &cert)
	rest, trailErr := tls.Unmarshal(append(append([]byte{}, full...), 0x5a, 0x5b), &cert)
	t.Check("truncation-errors-and-trailing-bytes-are-returned",
		truncErr != nil && trailErr == nil && bytes.Equal(rest, []byte{0x5a, 0x5b}) &&
			bytes.Equal(cert.Data, derA),
		"trunc=%v trail=%v rest=%x data=%x", truncErr, trailErr, rest, cert.Data)

	t.Done()
}
