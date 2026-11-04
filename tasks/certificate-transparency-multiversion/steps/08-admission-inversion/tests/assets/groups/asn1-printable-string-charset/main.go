// Group asn1-printable-string-charset.
//
// The companion to asn1-lax-printable-string. That group grades the three-way
// recovery; this one grades the character set the parser works from, and how
// the recovery composes with it.
//
// The accepted set. isPrintable() admits two characters RFC-correct DER does
// not -- '*' (wildcard subject names in the wild) and '&' (long-lived CA
// certificates in the wild) -- on top of a-z A-Z 0-9, 0x27..0x29, 0x2B..0x2F,
// space, ':', '=' and '?'. Every case here pins the exact decoded string,
// never merely "no error", so withdrawing either allowance and keeping the
// allowance but mangling the value both show up. The complement is graded
// exhaustively: all 180 bytes outside the set must be refused in strict mode.
//
// The two allowances themselves are inherited -- isPrintable() is byte-identical
// at v1.0.21 -- and are graded here because nothing else in the corpus pins
// them and a value-preserving parser is the thing every later milestone builds
// on. What makes this group impossible at v1.0.21 is the composition: v1.0.21
// has no `lax` tag component at all, and its ISO8859-1 / T.61 recovery sits at
// the call site in parseField's interface{} branch, where it applies
// unconditionally and where a `string` struct field never reaches it. v1.1.0
// moves the recovery inside parsePrintableString() behind `if !lax`, so it
// applies to every field kind and only when asked. The three `lax-...` cases
// below -- including the two that put an allowed-but-not-RFC character next to
// a byte outside the set -- therefore report the strict error at v1.0.21.
//
// No fixture, no clock, no key: every input is a DER encoding built from
// constants in this file.
package main

import (
	"fmt"
	"strings"

	"ctverify/vhelp"

	"github.com/google/certificate-transparency-go/asn1"
)

type holder struct{ S string }
type nested struct{ Inner holder }

// printableSet is every byte isPrintable() admits, in ascending order.
// 0x26 '&' and 0x2A '*' are the two that are not RFC PrintableString.
var printableSet = func() []byte {
	var b []byte
	b = append(b, ' ')                    // 0x20
	b = append(b, '&')                    // 0x26  -- allowAmpersand
	for c := byte(0x27); c <= 0x29; c++ { // ' ( )
		b = append(b, c)
	}
	b = append(b, '*')                    // 0x2A  -- allowAsterisk
	for c := byte(0x2B); c <= 0x2F; c++ { // + , - . /
		b = append(b, c)
	}
	for c := byte('0'); c <= '9'; c++ {
		b = append(b, c)
	}
	b = append(b, ':', '=', '?') // 0x3A 0x3D 0x3F
	for c := byte('A'); c <= 'Z'; c++ {
		b = append(b, c)
	}
	for c := byte('a'); c <= 'z'; c++ {
		b = append(b, c)
	}
	return b
}()

// seqPS returns the DER for SEQUENCE { PrintableString s }. Both lengths stay
// below 128, so both are single-byte.
func seqPS(s []byte) []byte {
	if len(s) > 125 {
		panic("input too long for a single-byte length")
	}
	out := []byte{0x30, byte(len(s) + 2), 0x13, byte(len(s))}
	return append(out, s...)
}

func errString(err error) string {
	if err == nil {
		return "<nil>"
	}
	return err.Error()
}

func main() {
	t := vhelp.New()

	// strictValue parses SEQUENCE { PrintableString in } with no tag at all and
	// requires the exact decoded string.
	strictValue := func(id, in string) {
		var v holder
		rest, err := asn1.Unmarshal(seqPS([]byte(in)), &v)
		if err != nil || len(rest) != 0 {
			t.Check(id, false, "err=%s rest=%d", errString(err), len(rest))
			return
		}
		t.Equal(id, v.S, in)
	}
	// strictRejected requires a hard refusal and no value.
	strictRejected := func(id string, b byte) {
		var v holder
		_, err := asn1.Unmarshal(seqPS([]byte{'A', b, 'B'}), &v)
		t.Check(id, err != nil && v.S == "",
			"byte 0x%02x: err=%s s=%q; want an error and no value", b, errString(err), v.S)
	}

	// --- the two non-RFC characters the fork deliberately admits -------------
	strictValue("strict-asterisk-accepted-exact", "*.bench.invalid")
	strictValue("strict-ampersand-accepted-exact", "Harbor & Co")
	strictValue("strict-asterisk-and-ampersand-together-exact", "*&*&")

	// --- and the whole set at once, in one string ----------------------------
	strictValue("strict-whole-printable-set-accepted-exact", string(printableSet))

	// --- the contiguous runs either side of '&' and '*', pinned separately ---
	strictValue("strict-apostrophe-paren-run-exact", "'()")
	strictValue("strict-plus-slash-run-exact", "+,-./")
	strictValue("strict-colon-equals-question-exact", ":=?")

	// --- the complement: strict mode refuses, it does not silently recover ---
	// Each is ISO8859-1-representable, which is exactly why lax mode below
	// recovers it; strict mode must still refuse it outright.
	strictRejected("strict-percent-rejected", '%')    // 0x25, one below '&'
	strictRejected("strict-semicolon-rejected", ';')  // 0x3B, one above ':'
	strictRejected("strict-less-than-rejected", '<')  // 0x3C, one below '='
	strictRejected("strict-at-sign-rejected", '@')    // 0x40, one below 'A'
	strictRejected("strict-underscore-rejected", '_') // 0x5F, one below '`'

	// --- exhaustively, every byte outside the set is refused in strict mode --
	inSet := map[byte]bool{}
	for _, b := range printableSet {
		inSet[b] = true
	}
	var accepted []string
	for i := 0; i < 256; i++ {
		b := byte(i)
		if inSet[b] {
			continue
		}
		var v holder
		if _, err := asn1.Unmarshal(seqPS([]byte{b}), &v); err == nil {
			accepted = append(accepted, fmt.Sprintf("0x%02x=>%q", b, v.S))
		}
	}
	t.Check("strict-every-byte-outside-the-set-rejected", len(accepted) == 0,
		"%d bytes outside the set parsed in strict mode: %s",
		len(accepted), strings.Join(accepted, ","))

	// --- laxness does not widen or narrow the accepted set -------------------
	var lv holder
	_, err := asn1.UnmarshalWithParams(seqPS([]byte("*&")), &lv, "lax")
	t.Check("lax-asterisk-and-ampersand-unchanged", err == nil && lv.S == "*&",
		"err=%s s=%q; want %q", errString(err), lv.S, "*&")

	// What lax does add is the recovery strict mode no longer performs: '%' is
	// ISO8859-1-representable, so it comes back verbatim.
	var pv holder
	_, err = asn1.UnmarshalWithParams(seqPS([]byte("100%")), &pv, "lax")
	t.Check("lax-percent-recovered-exact", err == nil && pv.S == "100%",
		"err=%s s=%q; want %q", errString(err), pv.S, "100%")

	// The two allowances and the recovery compose: an allowed-but-not-RFC
	// character sitting next to a byte outside the set still reaches the
	// recovery, and the recovery transcodes the whole content.
	// 0x2A '*', 0xE9 -> every byte could be ISO8859-1, so 0xE9 becomes U+00E9.
	var iv holder
	_, err = asn1.UnmarshalWithParams(seqPS([]byte{'*', 0xE9}), &iv, "lax")
	t.Check("lax-asterisk-with-iso8859-byte-recovered",
		err == nil && iv.S == "*\u00e9",
		"err=%s s=%q; want %q", errString(err), iv.S, "*\u00e9")

	// 0x26 '&', 0x0A -> 0x0A is below 0x20, so not ISO8859-1; T.61 keeps it.
	var tv holder
	_, err = asn1.UnmarshalWithParams(seqPS([]byte{'&', 0x0A}), &tv, "lax")
	t.Check("lax-ampersand-with-t61-byte-kept-verbatim",
		err == nil && tv.S == "&\n",
		"err=%s s=%q; want %q", errString(err), tv.S, "&\n")

	// --- the set applies at any depth ---------------------------------------
	inner := seqPS([]byte("*&"))
	var nv nested
	_, err = asn1.Unmarshal(append([]byte{0x30, byte(len(inner))}, inner...), &nv)
	t.Check("strict-nested-asterisk-and-ampersand-accepted", err == nil && nv.Inner.S == "*&",
		"err=%s s=%q; want %q", errString(err), nv.Inner.S, "*&")

	t.Done()
}
