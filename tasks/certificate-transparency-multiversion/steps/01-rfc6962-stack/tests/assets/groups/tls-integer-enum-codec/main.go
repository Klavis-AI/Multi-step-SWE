// Group tls-integer-enum-codec.
//
// The tls/ package is a reflective codec for the TLS presentation language of
// RFC 5246 s4. This group grades its scalar half: fixed-width integers are
// network byte order; tls.Uint24 occupies three bytes; a tls.Enum field
// occupies as many bytes as its declared `maxval` needs and a value too wide
// for that field is refused; a type aliased to tls.Enum is treated as one; an
// Enum with no size information is refused; truncated input is an error and
// trailing input is handed back.
//
// Boundary: tls.Marshal / tls.MarshalWithParams / tls.Unmarshal only. The
// struct definitions below belong to this driver, so nothing here depends on
// any product type.
package main

import (
	"errors"

	"github.com/google/certificate-transparency-go/tls"

	"ctverify/vhelp"
)

type ints struct {
	U8  uint8
	U16 uint16
	U32 uint32
	U64 uint64
}

// Uint24 is kept first so it is read at offset zero.
type withUint24 struct {
	U24  tls.Uint24
	Tail uint16
}

type enum1 struct {
	E tls.Enum `tls:"maxval:255"`
}

type enum2 struct {
	E tls.Enum `tls:"maxval:65535"`
}

type enum3 struct {
	E tls.Enum `tls:"maxval:16777215"`
}

// A type whose kind matches tls.Enum is treated as an enum, so callers can
// alias enums of their own (ct.Version and ct.LogEntryType are exactly this).
type aliasEnum tls.Enum

type withAlias struct {
	A aliasEnum `tls:"maxval:65535"`
}

type enumUnsized struct {
	E tls.Enum
}

func nonNil(err error, why string) error {
	if err == nil {
		return errors.New(why)
	}
	return nil
}

func main() {
	t := vhelp.New()

	// --- fixed-width integers ---------------------------------------------
	in := ints{U8: 0x12, U16: 0x3456, U32: 0x789abcde, U64: 0x0102030405060708}
	got, err := tls.Marshal(in)
	if err != nil {
		t.Fatalf("tls.Marshal(ints): %v", err)
	}
	t.EqualBytes("ints-marshal-big-endian", got, []byte{
		0x12,
		0x34, 0x56,
		0x78, 0x9a, 0xbc, 0xde,
		0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
	})

	var back ints
	rest, err := tls.Unmarshal(got, &back)
	t.Check("ints-unmarshal-roundtrip", err == nil && len(rest) == 0 && back == in,
		"err=%v trailing=%d got=%+v", err, len(rest), back)

	// --- Uint24 -------------------------------------------------------------
	u24, err := tls.Marshal(withUint24{U24: tls.Uint24(0xabcdef), Tail: 0x0102})
	if err != nil {
		t.Fatalf("tls.Marshal(withUint24): %v", err)
	}
	t.EqualBytes("uint24-marshal-three-bytes", u24, []byte{0xab, 0xcd, 0xef, 0x01, 0x02})

	var u24back withUint24
	if _, err := tls.Unmarshal(u24, &u24back); err != nil {
		t.Fatalf("tls.Unmarshal(withUint24): %v", err)
	}
	t.Equal("uint24-unmarshal-value", uint32(u24back.U24), uint32(0xabcdef))

	// --- enum width follows maxval -----------------------------------------
	e1, err1 := tls.Marshal(enum1{E: 0x7f})
	e2, err2 := tls.Marshal(enum2{E: 0x7f})
	e3, err3 := tls.Marshal(enum3{E: 0x7f})
	if err1 != nil || err2 != nil || err3 != nil {
		t.Fatalf("tls.Marshal(enum): %v / %v / %v", err1, err2, err3)
	}
	t.EqualBytes("enum-maxval-255-is-one-byte", e1, []byte{0x7f})
	t.EqualBytes("enum-maxval-65535-is-two-bytes", e2, []byte{0x00, 0x7f})
	t.EqualBytes("enum-maxval-16777215-is-three-bytes", e3, []byte{0x00, 0x00, 0x7f})

	_, err = tls.Marshal(enum1{E: 256})
	t.Case("enum-marshal-value-too-wide-rejected", nonNil(err, "enum value 256 marshalled into the one byte maxval:255 allows"))

	alias, err := tls.Marshal(withAlias{A: 0x1234})
	if err != nil {
		t.Fatalf("tls.Marshal(withAlias): %v", err)
	}
	var aliasBack withAlias
	_, aerr := tls.Unmarshal(alias, &aliasBack)
	t.Check("enum-alias-type-supported",
		aerr == nil && len(alias) == 2 && alias[0] == 0x12 && alias[1] == 0x34 && aliasBack.A == 0x1234,
		"encoded=%x err=%v decoded=%d", alias, aerr, uint64(aliasBack.A))

	_, err = tls.Marshal(enumUnsized{E: 1})
	t.Case("enum-without-maxval-rejected", nonNil(err, "an Enum with no maxval was given a size"))

	// --- input framing ------------------------------------------------------
	var short ints
	_, err = tls.Unmarshal(got[:len(got)-1], &short)
	t.Case("unmarshal-truncated-rejected", nonNil(err, "a truncated encoding was accepted"))

	var head ints
	rest, err = tls.Unmarshal(append(append([]byte{}, got...), 0xde, 0xad), &head)
	t.Check("unmarshal-returns-trailing-bytes", err == nil && len(rest) == 2 && rest[0] == 0xde && rest[1] == 0xad,
		"err=%v rest=%x", err, rest)

	// --- a top-level value can carry its own params -------------------------
	var e tls.Enum
	_, err = tls.UnmarshalWithParams([]byte{0x00, 0x2a}, &e, "maxval:65535")
	t.Check("unmarshalwithparams-toplevel-enum", err == nil && e == tls.Enum(42),
		"err=%v value=%d", err, uint64(e))

	t.Done()
}
