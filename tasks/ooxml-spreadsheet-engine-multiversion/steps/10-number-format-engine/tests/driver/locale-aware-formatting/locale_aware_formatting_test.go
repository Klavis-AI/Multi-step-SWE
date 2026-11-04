// Scored group: locale-aware-formatting.
//
// A format code may open with a bracketed language identifier, which selects
// the month, day and meridiem names the date tokens render with. The engine
// matches that identifier against a table keyed by the hexadecimal Windows
// locale identifier and nothing else, so a code written with an IETF language
// tag is not recognised at all.
//
// The [$-...] syntax is ECMA-376 Part 1 section 18.8.30 (numFmt).
package localeawareformatting

import (
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

const jan1 = 44562.0

func custom(t *testing.T, code string, v interface{}, expect string) {
	t.Helper()
	f := seedcalc.NewFile()
	c := code
	id, err := f.NewStyle(&seedcalc.Style{CustomNumFmt: &c})
	if err != nil {
		t.Fatalf("NewStyle(%q): %v", code, err)
	}
	if err := f.SetCellValue("Sheet1", "A1", v); err != nil {
		t.Fatalf("SetCellValue: %v", err)
	}
	if err := f.SetCellStyle("Sheet1", "A1", "A1", id); err != nil {
		t.Fatalf("SetCellStyle: %v", err)
	}
	got, err := f.GetCellValue("Sheet1", "A1")
	if err != nil {
		t.Fatalf("GetCellValue(%q): %v", code, err)
	}
	if got != expect {
		t.Fatalf("format %q of %v = %q, want %q", code, v, got, expect)
	}
}

func TestTheMonthNameFollowsTheLanguageIdentifier(t *testing.T) {
	custom(t, "[$-407]yyyy mmmm dd", jan1, "2022 Januar 01")
	custom(t, "[$-419]mmmm", jan1, "январь")
	custom(t, "[$-411]mmmm", jan1, "1月")
	custom(t, "[$-804]mmmm", jan1, "一月")
	custom(t, "[$-404]mmmm", jan1, "1月")
}

func TestTheDefaultIdentifierIsEnglish(t *testing.T) {
	// 409 is US English, which is what the untagged code already renders, so
	// tagging with it changes nothing.
	custom(t, "[$-409]yyyy mmmm dd", jan1, "2022 January 01")
	custom(t, "[$-409]mmmm", jan1, "January")
	// And a tag that the table does not carry falls through instead.
	custom(t, "[$-de-DE]mmmm", jan1, "44562")
}

func TestTheMeridiemNameFollowsTheLanguageIdentifier(t *testing.T) {
	custom(t, "[$-411]h:mm AM/PM", 0.5678, "1:37 午後")
	custom(t, "[$-409]h:mm AM/PM", 0.5678, "1:37 PM")
	// German shares the English marker, so the identifier is honoured
	// without changing the text.
	custom(t, "[$-407]h:mm AM/PM", 0.5678, "1:37 PM")
}

func TestAnIetfLanguageTagIsNotRecognised(t *testing.T) {
	// The table is keyed by locale identifier only. A tag in de-DE or ja-JP
	// form matches nothing, which makes the whole section unsupported, so
	// the stored serial is displayed instead of a date.
	custom(t, "[$-de-DE]yyyy mmmm dd", jan1, "44562")
	custom(t, "[$-ja-JP]yyyy mmmm dd", jan1, "44562")
	custom(t, "[$-en-US]dddd", jan1, "44562")
}

func TestALeadingZeroInTheIdentifierIsNotRecognised(t *testing.T) {
	// The keys are written without padding, so the four-digit spelling of a
	// three-digit identifier misses.
	custom(t, "[$-0409]yyyy mmmm dd", jan1, "44562")
	custom(t, "[$-409]yyyy mmmm dd", jan1, "2022 January 01")
}

func TestTheIdentifierIsMatchedWithoutRegardToCase(t *testing.T) {
	// The lookup upper-cases before matching, so the hexadecimal letters may
	// be written either way.
	custom(t, "[$-c07]mmmm", jan1, "Jänner")
	custom(t, "[$-C07]mmmm", jan1, "Jänner")
}
