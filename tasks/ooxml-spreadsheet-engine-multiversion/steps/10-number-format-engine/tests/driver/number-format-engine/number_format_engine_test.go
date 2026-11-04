// Scored group: number-format-engine.
//
// The engine that turns a custom number-format code into displayed text. A
// code is parsed into sections and each section into tokens; the section
// chosen depends on the sign of the value, and each token the engine
// recognises contributes to the result. Anything it does not recognise makes
// it give up on the whole section and hand back the stored value unchanged.
//
// Format-code syntax is ECMA-376 Part 1 section 18.8.30 (numFmt, Number
// Format), inside the styles material of 18.8; the four-section positive /
// negative / zero / text ordering is described there.
package numberformatengine

import (
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// formatted stores v in A1 under the custom format code, then reads the cell
// back through the display path. The raw value is returned alongside so a
// case can show that formatting never rewrites what is stored.
func formatted(t *testing.T, code string, v interface{}) (display, raw string) {
	t.Helper()
	f := seedcalc.NewFile()
	c := code
	style, err := f.NewStyle(&seedcalc.Style{CustomNumFmt: &c})
	if err != nil {
		t.Fatalf("NewStyle(%q): %v", code, err)
	}
	if err := f.SetCellValue("Sheet1", "A1", v); err != nil {
		t.Fatalf("SetCellValue(%v): %v", v, err)
	}
	if err := f.SetCellStyle("Sheet1", "A1", "A1", style); err != nil {
		t.Fatalf("SetCellStyle: %v", err)
	}
	if display, err = f.GetCellValue("Sheet1", "A1"); err != nil {
		t.Fatalf("GetCellValue(%q, %v): %v", code, v, err)
	}
	if raw, err = f.GetCellValue("Sheet1", "A1", seedcalc.Options{RawCellValue: true}); err != nil {
		t.Fatalf("GetCellValue(%q, %v, raw): %v", code, v, err)
	}
	return display, raw
}

func want(t *testing.T, code string, v interface{}, expect string) {
	t.Helper()
	got, _ := formatted(t, code, v)
	if got != expect {
		t.Fatalf("format %q of %v = %q, want %q", code, v, got, expect)
	}
}

func TestTheZeroPlaceholderRoundsToAWholeNumber(t *testing.T) {
	// A lone 0 is the "digit, zero if absent" placeholder. One of them asks
	// for one integer digit and no fraction, so the value is rounded to a
	// whole number.
	want(t, "0", 3.7, "4")
	want(t, "0", 3.14159, "3")
	want(t, "0", 1234.5678, "1235")
	want(t, "0", 1.0, "1")
	// At a .5 boundary the rounding is half-to-even, which is what the
	// standard library's fixed-point formatting does.
	want(t, "0", 2.5, "2")
	want(t, "0", 3.5, "4")
}

func TestAMagnitudeBelowOneCollapsesToASingleZero(t *testing.T) {
	// The handler special-cases a value whose magnitude is under one: it
	// emits the placeholder's own "0" rather than rounding, so 0.999 does
	// not become 1.
	want(t, "0", 0.4, "0")
	want(t, "0", 0.999, "0")
	want(t, "0", 0.0001, "0")
}

func TestOnlyTheSingleCharacterZeroPlaceholderIsHandled(t *testing.T) {
	// The handler tests the token's text for exactly "0". A run of two or
	// more zeroes parses as one placeholder token whose text is "00", which
	// matches nothing, so the section contributes no characters at all.
	want(t, "00", 3.7, "")
	want(t, "000", 1.0, "")
	want(t, "00", 1234.5678, "")
	// A single zero in the same position still works.
	want(t, "0", 3.7, "4")
}

func TestADecimalPointIsNotAToken(t *testing.T) {
	// A decimal point is not one of the token types the engine recognises,
	// so a code containing one is abandoned and the stored value comes back
	// untouched -- including all of its digits.
	want(t, "0.00", 3.14159, "3.14159")
	want(t, "0.0", 3.7, "3.7")
	want(t, "0.0000", 1.5, "1.5")
	// Which is not what the same value does under a code the engine accepts.
	want(t, "0", 3.14159, "3")
}

func TestALiteralSectionReplacesTheValueEntirely(t *testing.T) {
	// A quoted run is a literal token: it contributes its own text. A section
	// made only of literals therefore never mentions the value.
	want(t, `"flat"`, 3.7, "flat")
	want(t, `"a""b"`, 1.0, "ab")
	// Literals and the placeholder compose in written order.
	want(t, `"n="0`, 3.7, "n=4")
	want(t, `0" items"`, 3.7, "4 items")
}

func TestTheNegativeSectionFormatsTheAbsoluteValue(t *testing.T) {
	// With two sections the second is selected for a negative value, and the
	// zero placeholder there renders the magnitude: the minus sign is the
	// format's job to write, not the placeholder's.
	want(t, "0;0", -3.7, "4")
	want(t, `0;"("0")"`, -3.7, "(4)")
	want(t, "0;0", 3.7, "4")
	// The under-one shortcut applies to the magnitude on this side too.
	want(t, "0;0", -0.4, "0")
}

func TestASingleSectionCoversOnlyPositiveValues(t *testing.T) {
	// One section describes positive values. A negative or zero value finds
	// no section of its own type and falls out of the loop, which returns
	// the stored value.
	want(t, "0", -2.5, "-2.5")
	want(t, "0", -0.4, "-0.4")
	want(t, "0", 0, "0")
	want(t, `"flat"`, -3.7, "-3.7")
	// The same code and the same magnitude on the positive side is formatted.
	want(t, "0", 2.5, "2")
	want(t, `"flat"`, 3.7, "flat")
}

func TestTheZeroSectionAlwaysReturnsTheStoredValue(t *testing.T) {
	// A third section is selected for a value of exactly zero, but its
	// handler ignores the tokens and returns the value, so no zero-section
	// code can change what a zero displays as.
	want(t, `0;0;"nil"`, 0, "0")
	want(t, `0;0;"---"`, 0, "0")
	// The positive section of the same code still formats.
	want(t, `0;0;"nil"`, 3.7, "4")
}

func TestTheTextSectionConcatenatesLiteralsAroundThePlaceholder(t *testing.T) {
	// A value that will not parse as a number selects the fourth section,
	// where @ stands for the text and literals surround it.
	want(t, "@", "abc", "abc")
	want(t, `"<"@">"`, "abc", "<abc>")
	want(t, `@" units"`, "abc", "abc units")
	want(t, `0;0;0;"txt"`, "abc", "txt")
	// With no fourth section the text is returned unchanged.
	want(t, "0", "abc", "abc")
}

func TestAnUnsupportedTokenAbandonsTheWholeSection(t *testing.T) {
	// The digit placeholder #, the percent sign, scientific notation and the
	// fraction and alignment codes are all outside the recognised token set.
	// Meeting one anywhere in the section stops the section dead and returns
	// the stored value, so no partial result is ever produced.
	want(t, "#,##0", 1234567.0, "1234567")
	want(t, "0%", 0.256, "0.256")
	want(t, "0E+00", 1234.5, "1234.5")
	want(t, "# ?/?", 1.25, "1.25")
	want(t, `_-0_-`, 1234.0, "1234")
	// The literal that precedes the unsupported token is discarded with it.
	want(t, `"n="#,##0`, 1234.0, "1234")
	// And the same value under a supported code is formatted.
	want(t, "0", 1234.5678, "1235")
}

func TestGeneralIsRecognisedAndThenDeclined(t *testing.T) {
	// General is in the token set, which is what lets it appear beside a
	// language identifier, but the handler treats it exactly like an
	// unsupported token: the section is abandoned and the value returned.
	want(t, "General", 3.7, "3.7")
	want(t, "General", "abc", "abc")
	want(t, `"n="General`, 3.7, "3.7")
	want(t, "0", 3.7, "4")
}

func TestTheColorTokenIsConsumedWithoutContributingText(t *testing.T) {
	// A bracketed colour is recognised, so it does not abandon the section,
	// and it contributes nothing to the displayed string.
	want(t, "[Red]0", 3.7, "4")
	want(t, "[Blue]0;[Red]0", -3.7, "4")
	want(t, `[Red]"flat"`, 3.7, "flat")
	// General beside a colour still declines.
	want(t, "[Blue]General", 3.7, "3.7")
}

func TestFormattingNeverRewritesTheStoredValue(t *testing.T) {
	// Every case above reads the display path. The raw path is unaffected by
	// the format code: the number in the sheet part is whatever was set.
	for code, display := range map[string]string{
		"0":       "4",
		"00":      "",
		"#,##0":   "3.7",
		"General": "3.7",
		`"flat"`:  "flat",
	} {
		got, raw := formatted(t, code, 3.7)
		if got != display {
			t.Fatalf("format %q of 3.7 = %q, want %q", code, got, display)
		}
		if raw != "3.7" {
			t.Fatalf("under %q the raw value is %q, want %q", code, raw, "3.7")
		}
	}
}
