// Group asn1-bmpstring.
//
// v1.1.2 merged the Go 1.14 asn1 change a3a1bdff7960 ("encoding/asn1: handle
// ASN1's string type BMPString") into the forked ./asn1 package. Observed
// entirely through the exported boundary: the TagBMPString constant and what
// asn1.Unmarshal does with a universal tag-30 element.
//
// At v1.1.0 asn1.TagBMPString does not exist (the driver cannot compile) and
// Unmarshal rejects a BMPString with a tags-don't-match structural error.
package main

import (
	"encoding/hex"
	"fmt"

	"github.com/google/certificate-transparency-go/asn1"

	"ctverify/vhelp"
)

// mustHex is only used on constants in this file.
func mustHex(s string) []byte {
	b, err := hex.DecodeString(s)
	if err != nil {
		panic(err)
	}
	return b
}

// A BMPString holding "Beavis" plus the optional two-byte terminator, taken
// from the upstream Go test vector for a3a1bdff7960.
const bmpBeavisBody = "0042006500610076006900730000"

// "ℕ - Double-struck N": characters outside the BMP-ASCII range.
const bmpMathBody = "21150020002d00200044006f00750062006c0065002d00730074007200750063006b0020004e0000"

func der(tag byte, body []byte) []byte {
	if len(body) > 127 {
		return append([]byte{tag, 0x81, byte(len(body))}, body...)
	}
	return append([]byte{tag, byte(len(body))}, body...)
}

func main() {
	t := vhelp.New()

	// 1. The constant itself has an exact value.
	t.Equal("tag-bmpstring-is-30", asn1.TagBMPString, 30)

	beavis := mustHex(bmpBeavisBody)
	math := mustHex(bmpMathBody)

	// 2. SEQUENCE { BMPString } into a struct with a string field.
	type oneString struct {
		V string
	}
	seq := der(0x30, der(0x1e, beavis))
	var got oneString
	rest, err := asn1.Unmarshal(seq, &got)
	if err != nil {
		t.Check("string-field-beavis", false, "Unmarshal: %v", err)
	} else if len(rest) != 0 {
		t.Check("string-field-beavis", false, "trailing %d bytes", len(rest))
	} else {
		t.Equal("string-field-beavis", got.V, "Beavis")
	}

	// 3. The same element into an interface{} field yields a Go string.
	type oneAny struct {
		V interface{}
	}
	var anyGot oneAny
	if _, err := asn1.Unmarshal(der(0x30, der(0x1e, math)), &anyGot); err != nil {
		t.Check("interface-field-is-string", false, "Unmarshal: %v", err)
	} else {
		s, ok := anyGot.V.(string)
		t.Check("interface-field-is-string", ok && s == "ℕ - Double-struck N",
			"got %#v", anyGot.V)
	}

	// 4. SEQUENCE OF BMPString decodes into []string.
	type manyStrings struct {
		V []string
	}
	inner := append(der(0x1e, beavis), der(0x1e, math)...)
	var many manyStrings
	if _, err := asn1.Unmarshal(der(0x30, der(0x30, inner)), &many); err != nil {
		t.Check("sequence-of-bmpstring", false, "Unmarshal: %v", err)
	} else {
		t.Equal("sequence-of-bmpstring", fmt.Sprintf("%q", many.V),
			`["Beavis" "ℕ - Double-struck N"]`)
	}

	// 5. A BMPString is UTF-16BE, so an odd byte count cannot be one. It is
	// rejected, and the destination is left untouched rather than
	// half-populated. Graded on the verdict and the destination, not on the
	// error's text.
	var odd oneString
	odd.V = "sentinel"
	rest, oddErr := asn1.Unmarshal(der(0x30, der(0x1e, []byte{0x00, 0x42, 0x00})), &odd)
	t.Check("odd-length-rejected", oddErr != nil && odd.V == "sentinel" && rest == nil,
		"err=%v, field=%q, rest=%d bytes", oddErr, odd.V, len(rest))

	// 6. The terminator is stripped only when it is a real terminator: a
	// BMPString whose last unit is U+0000 preceded by content keeps that
	// content. A single "B" followed by the terminator decodes to "B".
	var term oneString
	if _, err := asn1.Unmarshal(der(0x30, der(0x1e, []byte{0x00, 0x42, 0x00, 0x00})), &term); err != nil {
		t.Check("terminator-stripped", false, "Unmarshal: %v", err)
	} else {
		t.Equal("terminator-stripped", term.V, "B")
	}

	t.Done()
}
