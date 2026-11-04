// Group tls-vectors-and-variants.
//
// The other half of the tls/ codec: RFC 5246 s4.3 variable-length vectors and
// the select(Enum){...} variant construct that RFC 6962's TimestampedEntry is
// built from. Graded here: the length prefix of a vector is exactly as wide as
// its declared maxlen needs; minlen and maxlen are enforced; a fixed-size array
// carries no prefix; vectors of structs and vectors of vectors encode flat; the
// variant destination chosen by the selector is the only one populated; an
// unknown selector value is refused on unmarshal, and on marshal both a nil
// chosen branch and a non-nil unchosen branch are refused.
//
// Boundary: tls.Marshal / tls.Unmarshal with struct types that belong to this
// driver.
package main

import (
	"errors"

	"github.com/google/certificate-transparency-go/tls"

	"ctverify/vhelp"
)

type vec8 struct {
	V []byte `tls:"minlen:0,maxlen:255"`
}

type vec16 struct {
	V []byte `tls:"minlen:2,maxlen:65535"`
}

type vec24 struct {
	V []byte `tls:"minlen:1,maxlen:16777215"`
}

type fixedArray struct {
	A [4]byte
	B uint8
}

type inner struct {
	X uint16
	Y uint8
}

type sliceOfStructs struct {
	Items []inner `tls:"minlen:0,maxlen:65535"`
}

type sliceOfSlices struct {
	Items []vec8 `tls:"minlen:0,maxlen:65535"`
}

type variant struct {
	Which tls.Enum `tls:"maxval:255"`
	A     *uint16  `tls:"selector:Which,val:0"`
	B     *uint32  `tls:"selector:Which,val:1"`
}

func nonNil(err error, why string) error {
	if err == nil {
		return errors.New(why)
	}
	return nil
}

func main() {
	t := vhelp.New()

	// --- prefix width follows maxlen ---------------------------------------
	g8, err := tls.Marshal(vec8{V: []byte{0xaa, 0xbb}})
	if err != nil {
		t.Fatalf("tls.Marshal(vec8): %v", err)
	}
	t.EqualBytes("vector-maxlen-255-one-byte-prefix", g8, []byte{0x02, 0xaa, 0xbb})

	g16, err := tls.Marshal(vec16{V: []byte{0xaa, 0xbb}})
	if err != nil {
		t.Fatalf("tls.Marshal(vec16): %v", err)
	}
	t.EqualBytes("vector-maxlen-65535-two-byte-prefix", g16, []byte{0x00, 0x02, 0xaa, 0xbb})

	g24, err := tls.Marshal(vec24{V: []byte{0xaa, 0xbb}})
	if err != nil {
		t.Fatalf("tls.Marshal(vec24): %v", err)
	}
	t.EqualBytes("vector-maxlen-16777215-three-byte-prefix", g24, []byte{0x00, 0x00, 0x02, 0xaa, 0xbb})

	var back vec16
	rest, err := tls.Unmarshal(g16, &back)
	t.Check("vector-unmarshal-roundtrip",
		err == nil && len(rest) == 0 && len(back.V) == 2 && back.V[0] == 0xaa && back.V[1] == 0xbb,
		"err=%v trailing=%d value=%x", err, len(rest), back.V)

	// --- the declared bounds are enforced -----------------------------------
	_, err = tls.Marshal(vec16{V: []byte{0x01}})
	t.Case("vector-below-minlen-rejected", nonNil(err, "a 1-byte vector marshalled under minlen:2"))

	_, err = tls.Marshal(vec8{V: make([]byte, 256)})
	t.Case("vector-above-maxlen-rejected", nonNil(err, "a 256-byte vector marshalled under maxlen:255"))

	var tooShort vec16
	_, err = tls.Unmarshal([]byte{0x00, 0x01, 0x07}, &tooShort)
	t.Case("vector-unmarshal-below-minlen-rejected", nonNil(err, "a 1-byte vector decoded under minlen:2"))

	// --- a fixed-size array is not a vector ---------------------------------
	ga, err := tls.Marshal(fixedArray{A: [4]byte{1, 2, 3, 4}, B: 9})
	if err != nil {
		t.Fatalf("tls.Marshal(fixedArray): %v", err)
	}
	t.EqualBytes("fixed-array-has-no-length-prefix", ga, []byte{1, 2, 3, 4, 9})

	// --- vectors of structs and of vectors ----------------------------------
	gs, err := tls.Marshal(sliceOfStructs{Items: []inner{{X: 0x0102, Y: 3}, {X: 0x0405, Y: 6}}})
	if err != nil {
		t.Fatalf("tls.Marshal(sliceOfStructs): %v", err)
	}
	t.EqualBytes("vector-of-structs-encodes-flat", gs, []byte{0x00, 0x06, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06})

	gss, err := tls.Marshal(sliceOfSlices{Items: []vec8{{V: []byte{0x11}}, {V: []byte{0x22, 0x33}}}})
	if err != nil {
		t.Fatalf("tls.Marshal(sliceOfSlices): %v", err)
	}
	t.EqualBytes("vector-of-vectors-encodes-flat", gss, []byte{0x00, 0x05, 0x01, 0x11, 0x02, 0x22, 0x33})

	// --- select(Enum) --------------------------------------------------------
	b := uint32(0xdeadbeef)
	gv, err := tls.Marshal(variant{Which: 1, B: &b})
	if err != nil {
		t.Fatalf("tls.Marshal(variant): %v", err)
	}
	t.EqualBytes("variant-encodes-only-selected-field", gv, []byte{0x01, 0xde, 0xad, 0xbe, 0xef})

	var vback variant
	if _, err := tls.Unmarshal(gv, &vback); err != nil {
		t.Fatalf("tls.Unmarshal(variant): %v", err)
	}
	t.Check("variant-unselected-field-left-nil",
		vback.A == nil && vback.B != nil && *vback.B == 0xdeadbeef,
		"A=%v B=%v", vback.A, vback.B)

	var unknown variant
	_, err = tls.Unmarshal([]byte{0x05, 0x00}, &unknown)
	t.Case("variant-unknown-selector-rejected", nonNil(err, "selector value 5 with no destination was accepted"))

	// TLS-CODEC.md s3.1: on marshal, exactly the branch the selector chooses must
	// be non-nil. Both halves of that rule are observable from outside.
	_, err = tls.Marshal(variant{Which: 1})
	t.Case("variant-marshal-nil-chosen-branch-rejected",
		nonNil(err, "a variant whose selected branch was nil still marshalled"))

	a16 := uint16(0x0102)
	_, err = tls.Marshal(variant{Which: 1, A: &a16, B: &b})
	t.Case("variant-marshal-unchosen-branch-non-nil-rejected",
		nonNil(err, "a variant with a populated unchosen branch still marshalled"))

	t.Done()
}
