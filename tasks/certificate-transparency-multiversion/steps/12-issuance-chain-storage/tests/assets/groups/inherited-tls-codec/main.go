// Group inherited-tls-codec.
//
// The RFC 5246 presentation-language codec underneath everything this product
// signs. v1.2.0 adds two new structures to the top-level package --
// CertificateChainHash and PrecertChainEntryHash -- and encodes them with this
// same codec, so the milestone is only additive if the codec's existing rules
// are untouched: the width a length prefix gets from its declared maximum, the
// width an enum gets from maxval, the fixed-width fields that get no prefix at
// all, the selector-driven union, and the refusal to marshal a value outside
// its declared bounds.
//
// Every expectation below is written out as literal bytes derived from those
// rules, and the new v1.2.0 types are deliberately not touched, so the group
// says the same thing at the previous milestone as it does here.
package main

import (
	"bytes"
	"encoding/hex"
	"strings"

	ct "github.com/google/certificate-transparency-go"
	"github.com/google/certificate-transparency-go/tls"
	"github.com/google/certificate-transparency-go/x509"
	"github.com/google/certificate-transparency-go/x509util"

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

func kat(t *vhelp.T) map[string]string {
	out := map[string]string{}
	for _, line := range strings.Split(string(vhelp.ReadFixture("rfc6962", "kat.txt")), "\n") {
		if i := strings.IndexByte(line, ' '); i > 0 {
			out[line[:i]] = strings.TrimSpace(line[i+1:])
		}
	}
	if len(out) == 0 {
		t.Fatalf("known-answer table is empty")
	}
	return out
}

func mustHex(t *vhelp.T, s string) []byte {
	b, err := hex.DecodeString(s)
	if err != nil {
		t.Fatalf("bad pinned hex: %v", err)
	}
	return b
}

func u24(n int) []byte { return []byte{byte(n >> 16), byte(n >> 8), byte(n)} }

func u16(n int) []byte { return []byte{byte(n >> 8), byte(n)} }

func marshalled(t *vhelp.T, id string, v interface{}) []byte {
	b, err := tls.Marshal(v)
	if err != nil {
		t.Check(id, false, "tls.Marshal: %v", err)
		return nil
	}
	return b
}

func main() {
	t := vhelp.New()
	k := kat(t)

	// ---- 1. Integer widths. --------------------------------------------
	if b := marshalled(t, "integer-widths-follow-the-tag", widths{
		Small: 0x7f, Medium: 0x0102, Three: 0x030405, Fixed: 0x06070809,
	}); b != nil {
		t.EqualBytes("integer-widths-follow-the-tag", b,
			[]byte{0x7f, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09})
	}

	// ---- 2. An enum outside its declared maximum is not encodable. -----
	_, err := tls.Marshal(widths{Small: 256})
	t.Check("enum-above-maxval-rejected", err != nil,
		"tls.Marshal encoded an enum of 256 into a maxval:255 field")

	// ---- 3. Opaque vector bounds, both ends. ---------------------------
	_, errShort := tls.Marshal(bounded{Body: []byte{1}})
	_, errLong := tls.Marshal(bounded{Body: []byte{1, 2, 3, 4, 5}})
	okMid := marshalled(t, "opaque-vector-bounds-enforced", bounded{Body: []byte{1, 2, 3}})
	t.Check("opaque-vector-bounds-enforced",
		errShort != nil && errLong != nil && bytes.Equal(okMid, []byte{0x03, 0x01, 0x02, 0x03}),
		"short=%v long=%v mid=%x", errShort, errLong, okMid)

	// ---- 4. ASN.1Cert carries a uint24 length prefix. ------------------
	der := vhelp.ReadFixture("minted", "leaf-valid.der")
	if b := marshalled(t, "asn1cert-uses-a-uint24-length-prefix", ct.ASN1Cert{Data: der}); b != nil {
		t.EqualBytes("asn1cert-uses-a-uint24-length-prefix", b, append(u24(len(der)), der...))
	}

	// ---- 5. ... and refuses to be empty (minlen:1). --------------------
	_, err = tls.Marshal(ct.ASN1Cert{Data: nil})
	t.Check("asn1cert-minlen-1-rejects-empty", err != nil,
		"tls.Marshal encoded a zero-length ASN.1Cert")

	// ---- 6. A vector of structs gets one outer length plus each inner. --
	inter := vhelp.ReadFixture("minted", "bench-intermediate.der")
	chain := ct.CertificateChain{Entries: []ct.ASN1Cert{{Data: der}, {Data: inter}}}
	if b := marshalled(t, "certificate-chain-vector-encoding", chain); b != nil {
		var w bytes.Buffer
		inner := len(der) + len(inter) + 6
		w.Write(u24(inner))
		w.Write(u24(len(der)))
		w.Write(der)
		w.Write(u24(len(inter)))
		w.Write(inter)
		t.EqualBytes("certificate-chain-vector-encoding", b, w.Bytes())
	}

	// ---- 7. The empty vector is its length prefix and nothing else. ----
	if b := marshalled(t, "empty-certificate-chain-is-three-zero-bytes", ct.CertificateChain{}); b != nil {
		t.EqualBytes("empty-certificate-chain-is-three-zero-bytes", b, []byte{0x00, 0x00, 0x00})
	}

	// ---- 8/9. The selector-driven union, both arms. --------------------
	x509Entry := ct.TimestampedEntry{
		Timestamp: pinnedTimestamp,
		EntryType: ct.X509LogEntryType,
		X509Entry: &ct.ASN1Cert{Data: der},
	}
	if b := marshalled(t, "timestamped-entry-selects-the-x509-arm", x509Entry); b != nil {
		var w bytes.Buffer
		w.Write([]byte{0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef})
		w.Write(u16(0))
		w.Write(u24(len(der)))
		w.Write(der)
		w.Write(u16(0))
		t.EqualBytes("timestamped-entry-selects-the-x509-arm", b, w.Bytes())
	}

	var ikh [32]byte
	for i := range ikh {
		ikh[i] = byte(i)
	}
	tbs := []byte{0x30, 0x03, 0x02, 0x01, 0x2a}
	preEntry := ct.TimestampedEntry{
		Timestamp:    pinnedTimestamp,
		EntryType:    ct.PrecertLogEntryType,
		PrecertEntry: &ct.PreCert{IssuerKeyHash: ikh, TBSCertificate: tbs},
		Extensions:   ct.CTExtensions{0xaa, 0xbb},
	}
	if b := marshalled(t, "timestamped-entry-selects-the-precert-arm", preEntry); b != nil {
		var w bytes.Buffer
		w.Write([]byte{0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef})
		w.Write(u16(1))
		w.Write(ikh[:]) // fixed-size array: no length prefix
		w.Write(u24(len(tbs)))
		w.Write(tbs)
		w.Write(u16(2))
		w.Write([]byte{0xaa, 0xbb})
		t.EqualBytes("timestamped-entry-selects-the-precert-arm", b, w.Bytes())
	}

	// ---- 10. A selector value with no arm is not decodable. ------------
	var badEntry ct.TimestampedEntry
	_, err = tls.Unmarshal(append([]byte{0, 0, 0, 0, 0, 0, 0, 0}, 0x00, 0x05, 0x00, 0x00), &badEntry)
	t.Check("unknown-selector-value-rejected", err != nil,
		"tls.Unmarshal accepted entry type 5, which has no union arm")

	// ---- 11. DigitallySigned is two enums and a uint16 vector. ---------
	ds := tls.DigitallySigned{
		Algorithm: tls.SignatureAndHashAlgorithm{Hash: tls.SHA256, Signature: tls.ECDSA},
		Signature: []byte("signature"),
	}
	if b := marshalled(t, "digitally-signed-exact-encoding", ds); b != nil {
		want := append([]byte{0x04, 0x03, 0x00, 0x09}, []byte("signature")...)
		t.EqualBytes("digitally-signed-exact-encoding", b, want)
	}

	// ---- 12. Truncation is an error; trailing data is handed back. -----
	full := append([]byte{0x04, 0x03, 0x00, 0x09}, []byte("signature")...)
	var trunc tls.DigitallySigned
	_, errTrunc := tls.Unmarshal(full[:len(full)-1], &trunc)
	var tail tls.DigitallySigned
	rest, errTail := tls.Unmarshal(append(append([]byte{}, full...), 0xff, 0xfe), &tail)
	t.Check("truncation-errors-and-trailing-bytes-are-returned",
		errTrunc != nil && errTail == nil && bytes.Equal(rest, []byte{0xff, 0xfe}),
		"truncated err=%v, trailing err=%v rest=%x", errTrunc, errTail, rest)

	// ---- 13. The pinned SCT-list known answer decodes field for field. --
	listHex := mustHex(t, k["defaultSCTListHexString"])
	// The pinned blob is the X.509 extension value: a DER OCTET STRING
	// wrapping the SignedCertificateTimestampList itself.
	if len(listHex) < 2 || listHex[0] != 0x04 || int(listHex[1]) != len(listHex)-2 {
		t.Fatalf("pinned SCT list is not an OCTET STRING of the expected length")
	}
	var sctList x509.SignedCertificateTimestampList
	leftover, err := tls.Unmarshal(listHex[2:], &sctList)
	if err != nil || len(leftover) != 0 {
		t.Check("pinned-sct-list-decodes-field-for-field", false,
			"tls.Unmarshal(SCT list): err=%v trailing=%d", err, len(leftover))
	} else {
		scts, perr := x509util.ParseSCTsFromSCTList(&sctList)
		ok := perr == nil && len(scts) == 2
		if ok {
			for _, s := range scts {
				ok = ok && s.SCTVersion == ct.V1 && s.Timestamp == 1234 &&
					string(s.LogID.KeyID[:]) == "iamapublickeyshatwofivesixdigest" &&
					len(s.Extensions) == 0 &&
					s.Signature.Algorithm.Hash == tls.SHA256 &&
					s.Signature.Algorithm.Signature == tls.ECDSA &&
					string(s.Signature.Signature) == "signature"
			}
		}
		t.Check("pinned-sct-list-decodes-field-for-field", ok,
			"ParseSCTsFromSCTList: err=%v n=%d", perr, len(scts))
	}

	t.Done()
}
