// Group asn1-lax-printable-string.
//
// The third named laxness v1.1.0 adds to the forked asn1 package:
//
//	parsePrintableString() copes with invalid PrintableString contents, e.g.
//	use of tagPrintableString when the string data is really ISO8859-1.
//
// The recovery is a three-way decision -- ISO8859-1 if every byte could be
// ISO8859-1, else T.61 if every byte could be T.61, else give up with a
// distinct message -- and each of the three branches is pinned below with an
// input chosen to land in exactly that branch.
//
// At v1.0.21 the `lax` struct-tag component does not exist, so
// UnmarshalWithParams(..., "lax") is strict and every recovery case below
// still reports the strict error.
package main

import (
	"ctverify/vhelp"

	"github.com/google/certificate-transparency-go/asn1"
)

// Pinned encodings: SEQUENCE { PrintableString <three bytes> }.
var (
	// 'A', 0xE9 (e-acute in ISO8859-1), 'B'  -- every byte could be ISO8859-1
	isoBytes = []byte{0x30, 0x05, 0x13, 0x03, 0x41, 0xE9, 0x42}
	// 'A', 0x0A, 'B'  -- 0x0A is below 0x20 so not ISO8859-1, but is legal T.61
	t61Bytes = []byte{0x30, 0x05, 0x13, 0x03, 0x41, 0x0A, 0x42}
	// 'A', 0x7F, 'B'  -- 0x7F is in the 0x7F..0x9F hole, but is legal T.61
	delBytes = []byte{0x30, 0x05, 0x13, 0x03, 0x41, 0x7F, 0x42}
	// 'A', 0xA0, 'B'  -- 0xA0 is the first byte back inside ISO8859-1
	nbspBytes = []byte{0x30, 0x05, 0x13, 0x03, 0x41, 0xA0, 0x42}
	// 'A', 0x00, 'B'  -- NUL is neither ISO8859-1 nor T.61
	nulBytes = []byte{0x30, 0x05, 0x13, 0x03, 0x41, 0x00, 0x42}
	// SEQUENCE { SEQUENCE { PrintableString 'A' 0xE9 'B' } }
	nestedIsoBytes = []byte{0x30, 0x07, 0x30, 0x05, 0x13, 0x03, 0x41, 0xE9, 0x42}
	// SEQUENCE { PrintableString "Harbor CT Bench" } -- entirely legal
	legalBytes = append([]byte{0x30, 0x11, 0x13, 0x0F}, []byte("Harbor CT Bench")...)
)

type holder struct{ S string }
type nested struct{ Inner holder }

func errString(err error) string {
	if err == nil {
		return "<nil>"
	}
	return err.Error()
}

func main() {
	t := vhelp.New()

	strict := func(id string, in []byte) {
		var v holder
		_, err := asn1.Unmarshal(in, &v)
		t.Check(id, err != nil && v.S == "", "err=%s s=%q; want an error and no value", errString(err), v.S)
	}
	lax := func(id string, in []byte, want []byte) {
		var v holder
		rest, err := asn1.UnmarshalWithParams(in, &v, "lax")
		if err != nil || len(rest) != 0 {
			t.Check(id, false, "err=%s rest=%d", errString(err), len(rest))
			return
		}
		t.EqualBytes(id, []byte(v.S), want)
	}

	// --- branch 1: ISO8859-1 ------------------------------------------------
	strict("strict-iso8859-byte-rejected", isoBytes)
	// 0xE9 is U+00E9, which is 0xC3 0xA9 in UTF-8.
	lax("lax-iso8859-byte-transcoded-to-utf8", isoBytes, []byte{0x41, 0xC3, 0xA9, 0x42})
	strict("strict-nbsp-byte-rejected", nbspBytes)
	// 0xA0 is U+00A0, which is 0xC2 0xA0 in UTF-8.
	lax("lax-nbsp-byte-transcoded-to-utf8", nbspBytes, []byte{0x41, 0xC2, 0xA0, 0x42})

	// --- branch 2: T.61, which keeps the bytes as they stand -----------------
	strict("strict-control-byte-rejected", t61Bytes)
	lax("lax-control-byte-kept-verbatim", t61Bytes, []byte{0x41, 0x0A, 0x42})
	strict("strict-delete-byte-rejected", delBytes)
	lax("lax-delete-byte-kept-verbatim", delBytes, []byte{0x41, 0x7F, 0x42})

	// --- branch 3: neither, so the parse still fails, with its own message ---
	strict("strict-nul-byte-rejected", nulBytes)
	// NUL is the one byte the recovery refuses to guess at: neither branch
	// applies, so even in lax mode the parse fails and yields no string.
	var v holder
	_, err := asn1.UnmarshalWithParams(nulBytes, &v, "lax")
	t.Check("lax-nul-byte-still-rejected", err != nil && v.S == "",
		"err=%s s=%q; want an error and no value", errString(err), v.S)

	// --- the tag is inherited by nested structures ---------------------------
	var n nested
	_, err = asn1.Unmarshal(nestedIsoBytes, &n)
	t.Check("strict-nested-iso8859-byte-rejected", err != nil, "err=%s s=%q", errString(err), n.Inner.S)

	n = nested{}
	_, err = asn1.UnmarshalWithParams(nestedIsoBytes, &n, "lax")
	if err != nil {
		t.Check("lax-nested-iso8859-byte-transcoded", false, "err=%s", errString(err))
	} else {
		t.EqualBytes("lax-nested-iso8859-byte-transcoded", []byte(n.Inner.S), []byte{0x41, 0xC3, 0xA9, 0x42})
	}

	// --- and a legal PrintableString is untouched either way -----------------
	var a, b holder
	_, errA := asn1.Unmarshal(legalBytes, &a)
	_, errB := asn1.UnmarshalWithParams(legalBytes, &b, "lax")
	t.Check("legal-printable-string-identical-strict-and-lax",
		errA == nil && errB == nil && a.S == "Harbor CT Bench" && b.S == "Harbor CT Bench",
		"strict err=%s s=%q; lax err=%s s=%q", errString(errA), a.S, errString(errB), b.S)

	t.Done()
}
