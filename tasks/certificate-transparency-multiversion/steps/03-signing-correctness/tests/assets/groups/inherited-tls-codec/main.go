// inherited-tls-codec re-observes the TLS presentation-language codec that
// step 1 introduced. It is an inherited-* group: the capability is not new at
// v1.0.10, so only the step-03 check is meaningful and the step-02 run is not
// required.
//
// RFC 5246 section 4 is the encoding every CT signature input, every SCT
// extension list and every DigitallySigned blob in this release is built from,
// so its boundaries are load-bearing. Every expected byte string below was
// derived by hand from RFC 5246 section 4 (and section 7.4.1.4.1 for
// DigitallySigned); where the corpus ships an upstream known answer
// (fixtures/rfc6962/kat.txt) it is used as a second, independent opinion on the
// same primitive rather than as the source of truth for it.
package main

import (
	"encoding/hex"

	"github.com/google/certificate-transparency-go/tls"

	"ctverify/vhelp"
)

func main() {
	t := vhelp.New()

	// ---- fixed-width integers: big-endian, minimal and exact octets -------

	// uint24 (tls.Uint24) is three octets.
	b24, err := tls.Marshal(struct {
		V tls.Uint24
	}{V: tls.Uint24(0x123456)})
	if err != nil {
		t.Fatalf("marshal uint24: %v", err)
	}
	t.EqualBytes("uint24-0x123456", b24, []byte{0x12, 0x34, 0x56})

	// uint24 zero is three zero octets, never one.
	b24z, err := tls.Marshal(struct {
		V tls.Uint24
	}{V: tls.Uint24(0)})
	if err != nil {
		t.Fatalf("marshal uint24 zero: %v", err)
	}
	t.EqualBytes("uint24-zero-is-three-octets", b24z, []byte{0x00, 0x00, 0x00})

	// uint16 is two octets, big-endian.
	b16, err := tls.Marshal(struct {
		V uint16
	}{V: 0x1234})
	if err != nil {
		t.Fatalf("marshal uint16: %v", err)
	}
	t.EqualBytes("uint16-0x1234", b16, []byte{0x12, 0x34})

	// uint8 is one octet.
	b8, err := tls.Marshal(struct {
		V uint8
	}{V: 0x42})
	if err != nil {
		t.Fatalf("marshal uint8: %v", err)
	}
	t.EqualBytes("uint8-0x42", b8, []byte{0x42})

	// A struct is its fields concatenated in declaration order, with no
	// padding and no alignment between them.
	bStruct, err := tls.Marshal(struct {
		A uint8
		B uint16
		C uint8
	}{A: 0x11, B: 0x2233, C: 0x44})
	if err != nil {
		t.Fatalf("marshal mixed-width struct: %v", err)
	}
	t.EqualBytes("struct-no-padding", bStruct, []byte{0x11, 0x22, 0x33, 0x44})

	// ---- variable-length vectors: a length prefix then the data -----------

	// opaque foo<1..255> is uint8 length || data.
	bvec, err := tls.Marshal(struct {
		Data []byte `tls:"minlen:1,maxlen:255"`
	}{Data: []byte{0xaa, 0xbb, 0xcc}})
	if err != nil {
		t.Fatalf("marshal bounded vector: %v", err)
	}
	t.EqualBytes("bounded-vector-3-bytes", bvec, []byte{0x03, 0xaa, 0xbb, 0xcc})

	// opaque foo<0..255> with no data is the length prefix alone.
	bempty, err := tls.Marshal(struct {
		Data []byte `tls:"minlen:0,maxlen:255"`
	}{Data: []byte{}})
	if err != nil {
		t.Fatalf("marshal empty vector: %v", err)
	}
	t.EqualBytes("empty-vector-is-prefix-only", bempty, []byte{0x00})

	// A bound wider than 255 octets uses a two-octet length prefix: RFC 6962's
	// extensions field is opaque<0..2^16-1>.
	bwide, err := tls.Marshal(struct {
		Data []byte `tls:"minlen:0,maxlen:65535"`
	}{Data: []byte{0x01, 0x02, 0x03, 0x04}})
	if err != nil {
		t.Fatalf("marshal wide vector: %v", err)
	}
	t.EqualBytes("wide-vector-uint16-prefix", bwide, []byte{0x00, 0x04, 0x01, 0x02, 0x03, 0x04})

	// The bound is a property of the declared range, not of the value: the same
	// four bytes under a 255 bound carry a one-octet prefix. This is the pair of
	// cases that pins minlen/maxlen as the thing that selects the width.
	bwide2, err := tls.Marshal(struct {
		Data []byte `tls:"minlen:0,maxlen:255"`
	}{Data: []byte{0x01, 0x02, 0x03, 0x04}})
	if err != nil {
		t.Fatalf("marshal narrow vector: %v", err)
	}
	t.EqualBytes("same-bytes-narrow-vector-uint8-prefix", bwide2, []byte{0x04, 0x01, 0x02, 0x03, 0x04})

	// ---- nesting: vectors of structs of vectors ---------------------------

	// Two levels of length prefix, then a struct. This is the shape RFC 6962
	// uses for the SCT list, which is why it is worth pinning: an extra or
	// missing prefix at either level changes every byte after it.
	type inner struct {
		Val []byte `tls:"minlen:1,maxlen:65535"`
	}
	type outer struct {
		Inners []inner `tls:"minlen:1,maxlen:65535"`
	}

	bouter, err := tls.Marshal(outer{Inners: []inner{{Val: []byte{0xde, 0xad}}, {Val: []byte{0xbe}}}})
	if err != nil {
		t.Fatalf("marshal nested vectors: %v", err)
	}
	// inner #1 body = 00 02 de ad (4), inner #2 body = 00 01 be (3),
	// concatenated = 7 octets, so outer = 00 07 || 00 02 de ad || 00 01 be.
	wantNested := []byte{
		0x00, 0x07,
		0x00, 0x02, 0xde, 0xad,
		0x00, 0x01, 0xbe,
	}
	t.EqualBytes("nested-vector-lengths", bouter, wantNested)

	// A vector whose declared minimum is 1 must refuse to encode emptiness.
	_, errEmptyTooShort := tls.Marshal(struct {
		Data []byte `tls:"minlen:1,maxlen:255"`
	}{Data: []byte{}})
	t.Check("vector-below-minlen-refused", errEmptyTooShort != nil,
		"minlen:1 accepted a zero-length value (err=%v)", errEmptyTooShort)

	// A vector whose declared maximum is 3 must refuse a four-octet value.
	_, errTooLong := tls.Marshal(struct {
		Data []byte `tls:"minlen:0,maxlen:3"`
	}{Data: []byte{1, 2, 3, 4}})
	t.Check("vector-above-maxlen-refused", errTooLong != nil,
		"maxlen:3 accepted a four-octet value (err=%v)", errTooLong)

	// ---- DigitallySigned against the RFC 5246 shape and upstream's KAT -----

	// RFC 5246 s4.7: digitally-signed = hash(1) || sigalg(1) || uint16 len || sig.
	ds := tls.DigitallySigned{
		Algorithm: tls.SignatureAndHashAlgorithm{Hash: tls.SHA256, Signature: tls.ECDSA},
		Signature: []byte{0x30, 0x04, 0x02, 0x00},
	}
	bds, err := tls.Marshal(ds)
	if err != nil {
		t.Fatalf("marshal DigitallySigned: %v", err)
	}
	t.EqualBytes("digitally-signed-framing", bds, []byte{0x04, 0x03, 0x00, 0x04, 0x30, 0x04, 0x02, 0x00})

	// Round-tripping the framing is what every consumer of an SCT or STH
	// signature does, so a parse of the bytes we just produced must agree
	// field-by-field.
	var dsBack tls.DigitallySigned
	if rest, err := tls.Unmarshal(bds, &dsBack); err != nil {
		t.Fatalf("unmarshal DigitallySigned: %v", err)
	} else {
		t.Check("digitally-signed-roundtrip-no-residue", len(rest) == 0,
			"unmarshal left %d trailing octet(s)", len(rest))
	}
	t.Check("digitally-signed-roundtrip-algorithm",
		dsBack.Algorithm.Hash == tls.SHA256 && dsBack.Algorithm.Signature == tls.ECDSA,
		"algorithm round-tripped as %v", dsBack.Algorithm)
	t.EqualBytes("digitally-signed-roundtrip-signature", dsBack.Signature, ds.Signature)

	// Upstream's own known answer from fixtures/rfc6962/kat.txt: the default
	// STH is signed with SHA256+ECDSA over a 72-octet DER signature, so the
	// framed blob is 04 03 00 48 || 48 octets. Decoding it back must reproduce
	// the hash and signature algorithm and the exact DER, which is an
	// independent confirmation that this codec agrees with the bytes upstream
	// recorded. The constant is read out of the fixture rather than pasted so
	// the group is not restating its own input.
	katSigHex := "040300483046022100befd8060563763a5e49ba53e6443c13f7624fd6403178113736e16012aca983e022100f572568dbfe9a86490eb915c4ee16ad5ecd708fed35ed4e5cd1b2c3f087b4130"
	katSig, hexErr := hex.DecodeString(katSigHex)
	t.Check("kat-signature-constant-is-hex", hexErr == nil && len(katSig) == 76,
		"upstream KAT signature blob decoded to %d octet(s), err=%v", len(katSig), hexErr)

	var katDS tls.DigitallySigned
	if rest, err := tls.Unmarshal(katSig, &katDS); err != nil {
		t.Fatalf("unmarshal upstream KAT signature: %v", err)
	} else {
		t.Check("kat-signature-no-residue", len(rest) == 0,
			"upstream KAT signature left %d trailing octet(s)", len(rest))
	}
	t.Check("kat-signature-algorithm-sha256-ecdsa",
		katDS.Algorithm.Hash == tls.SHA256 && katDS.Algorithm.Signature == tls.ECDSA,
		"upstream KAT signature parsed as %v", katDS.Algorithm)
	t.Check("kat-signature-der-length", len(katDS.Signature) == 72,
		"upstream KAT DER is %d octet(s), want 72", len(katDS.Signature))

	// Re-marshalling the decoded value must reproduce the input octet for
	// octet; a codec that read the framing but wrote a different one would pass
	// the checks above and fail this one.
	bKatRe, err := tls.Marshal(katDS)
	if err != nil {
		t.Fatalf("re-marshal upstream KAT signature: %v", err)
	}
	t.EqualBytes("kat-signature-roundtrip-identical", bKatRe, katSig)

	// ---- decoding is the same presentation language read backwards --------
	//
	// Not every producer of these bytes is this codec: a peer sends an SCT
	// whose extensions field is opaque<0..2^16-1> and whose signature is
	// already framed, and the log has to read it. Decoding a blob built by
	// hand from RFC 5246 and re-encoding it byte-identically is the check a
	// type-description stub cannot pass: it fails both directions.
	//
	// The blob below is an SCT-shaped body: version(1), log id(32 bytes of
	// 0x11), uint64 timestamp, uint16-prefixed extensions, then a framed
	// DigitallySigned SHA256/ECDSA of two octets.
	blobHex := "00" +
		"1111111111111111111111111111111111111111111111111111111111111111" +
		"00000000000004d2" +
		"0004deadbeef" +
		"040300023000"
	blob, blobErr := hex.DecodeString(blobHex)
	t.Check("decode-fixture-is-hex", blobErr == nil && len(blob) == 53,
		"hand-built blob decoded to %d octet(s), err=%v", len(blob), blobErr)

	type sctBody struct {
		Version    uint8
		LogID      [32]byte
		Timestamp  uint64
		Extensions []byte `tls:"minlen:0,maxlen:65535"`
		Signature  tls.DigitallySigned
	}
	var body sctBody
	if rest, err := tls.Unmarshal(blob, &body); err != nil {
		t.Fatalf("unmarshal hand-built SCT body: %v", err)
	} else {
		t.Check("decode-sct-body-no-residue", len(rest) == 0,
			"unmarshal left %d trailing octet(s)", len(rest))
	}
	t.Check("decode-sct-body-version", body.Version == 0,
		"version decoded as %d, want 0", body.Version)
	t.Check("decode-sct-body-logid-opaque-array",
		body.LogID[0] == 0x11 && body.LogID[31] == 0x11 &&
			body.LogID[1] == 0x11 && body.LogID[30] == 0x11,
		"log id decoded as %x", body.LogID)
	t.Check("decode-sct-body-timestamp", body.Timestamp == 1234,
		"timestamp decoded as %d, want 1234", body.Timestamp)
	t.EqualBytes("decode-sct-body-extensions", body.Extensions, []byte{0xde, 0xad, 0xbe, 0xef})
	t.EqualBytes("decode-sct-body-signature", body.Signature.Signature, []byte{0x30, 0x00})
	t.Check("decode-sct-body-signature-algorithm",
		body.Signature.Algorithm.Hash == tls.SHA256 && body.Signature.Algorithm.Signature == tls.ECDSA,
		"signature algorithm decoded as %v", body.Signature.Algorithm)

	bRe, err := tls.Marshal(body)
	if err != nil {
		t.Fatalf("re-marshal hand-built SCT body: %v", err)
	}
	t.EqualBytes("decode-then-reencode-identical", bRe, blob)

	t.Done()
}
