// Scored group: text-functions.
//
// The text functions this milestone adds: TRIM, CLEAN, LOWER, UPPER and
// PROPER. Semantics follow OpenDocument v1.3 part 4 (OpenFormula) section 6.20
// "Text Functions", with this checkpoint's deviations pinned rather than
// glossed over. Case conversion is Unicode-aware throughout; the one place
// this checkpoint departs from OpenFormula is TRIM, which does not collapse
// interior whitespace.
package textfunctions

import (
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// sheet builds a workbook holding the awkward strings these functions are
// asked about: padded text, mixed case, and text carrying a control character.
func sheet(t *testing.T) *seedcalc.File {
	t.Helper()
	f := seedcalc.NewFile()
	for _, c := range []struct {
		axis string
		v    interface{}
	}{
		{"A1", "  spaced  "},
		{"A2", "MiXeD cAsE"},
		{"A3", "a\x07b"},
		{"A4", 42},
	} {
		if err := f.SetCellValue("Sheet1", c.axis, c.v); err != nil {
			t.Fatalf("SetCellValue(%s): %v", c.axis, err)
		}
	}
	return f
}

func eval(t *testing.T, f *seedcalc.File, formula string) (string, error) {
	t.Helper()
	if err := f.SetCellFormula("Sheet1", "H1", formula); err != nil {
		t.Fatalf("SetCellFormula(%q): %v", formula, err)
	}
	return f.CalcCellValue("Sheet1", "H1")
}

func mustEval(t *testing.T, f *seedcalc.File, formula, want string) {
	t.Helper()
	got, err := eval(t, f, formula)
	if err != nil {
		t.Fatalf("CalcCellValue(%q): unexpected error %v", formula, err)
	}
	if got != want {
		t.Fatalf("CalcCellValue(%q) = %q, want %q", formula, got, want)
	}
}

func mustReject(t *testing.T, f *seedcalc.File, formula string) {
	t.Helper()
	got, err := eval(t, f, formula)
	if err == nil {
		t.Fatalf("CalcCellValue(%q) = %q, want an error", formula, got)
	}
	if got != "" {
		t.Fatalf("CalcCellValue(%q) returned result %q alongside an error", formula, got)
	}
}

// mustRejectWith is mustReject with the rejection message pinned. An arity
// complaint names the function and the count it wanted; a formula the engine
// has never heard of is refused for an entirely different reason, and the two
// must not be allowed to look alike to this group.
func mustRejectWith(t *testing.T, f *seedcalc.File, formula, wantErr string) {
	t.Helper()
	got, err := eval(t, f, formula)
	if err == nil {
		t.Fatalf("CalcCellValue(%q) = %q, want an error", formula, got)
	}
	if err.Error() != wantErr {
		t.Fatalf("CalcCellValue(%q) failed with %q, want %q", formula, err, wantErr)
	}
	if got != "" {
		t.Fatalf("CalcCellValue(%q) returned result %q alongside an error", formula, got)
	}
}

func TestTRIMRemovesSurroundingButNotInteriorSpace(t *testing.T) {
	f := sheet(t)
	// Quirk of this checkpoint: TRIM strips leading and trailing whitespace
	// only. The OpenFormula function also collapses runs of interior spaces to
	// a single space; this one leaves them alone.
	mustEval(t, f, `=TRIM("  hello  world  ")`, "hello  world")
	mustEval(t, f, `=TRIM("  pad  ")`, "pad")
	mustEval(t, f, "=TRIM(A1)", "spaced")
	mustEval(t, f, `=TRIM("a")`, "a")
	mustEval(t, f, `=TRIM("")`, "")
	// Tabs count as surrounding whitespace, not just spaces.
	mustEval(t, f, "=TRIM(\"\tx\t\")", "x")
}

func TestCLEANRemovesControlCharacters(t *testing.T) {
	f := sheet(t)
	mustEval(t, f, "=CLEAN(\"a\x07b\")", "ab")
	mustEval(t, f, "=CLEAN(A3)", "ab")
	// Printable text passes through untouched, spaces included.
	mustEval(t, f, `=CLEAN("a b")`, "a b")
	mustEval(t, f, `=CLEAN("")`, "")
}

func TestLOWERAndUPPERConvertCase(t *testing.T) {
	f := sheet(t)
	mustEval(t, f, `=LOWER("AbC")`, "abc")
	mustEval(t, f, `=UPPER("AbC")`, "ABC")
	mustEval(t, f, "=LOWER(A2)", "mixed case")
	mustEval(t, f, "=UPPER(A2)", "MIXED CASE")
	mustEval(t, f, `=LOWER("")`, "")
	mustEval(t, f, `=UPPER("")`, "")
}

func TestCaseConversionIsUnicodeAware(t *testing.T) {
	f := sheet(t)
	// Conversion is not restricted to ASCII.
	mustEval(t, f, `=UPPER("ärger")`, "ÄRGER")
	mustEval(t, f, `=LOWER("ÄRGER")`, "ärger")
}

func TestPROPERCapitalisesEachWord(t *testing.T) {
	f := sheet(t)
	mustEval(t, f, `=PROPER("hello world")`, "Hello World")
	mustEval(t, f, `=PROPER("hELLo")`, "Hello")
	mustEval(t, f, "=PROPER(A2)", "Mixed Case")
	mustEval(t, f, `=PROPER("a")`, "A")
}

func TestPROPERTreatsAnyNonLetterAsAWordBoundary(t *testing.T) {
	f := sheet(t)
	// An apostrophe and a digit both end the current word, so the letter that
	// follows starts a new one and is capitalised.
	mustEval(t, f, `=PROPER("o'neil mcdonald")`, "O'Neil Mcdonald")
	mustEval(t, f, `=PROPER("123abc def")`, "123Abc Def")
}

func TestTheTextFunctionsAcceptANumericOperandAsItsText(t *testing.T) {
	f := sheet(t)
	// A1 holds the number 42; the text functions see the string it is stored
	// as and return it unchanged.
	mustEval(t, f, "=UPPER(A4)", "42")
	mustEval(t, f, "=LOWER(A4)", "42")
}

func TestTheTextFunctionsNest(t *testing.T) {
	f := sheet(t)
	mustEval(t, f, "=UPPER(TRIM(A1))", "SPACED")
	mustEval(t, f, "=PROPER(LOWER(A2))", "Mixed Case")
}

func TestEachTextFunctionTakesExactlyOneArgument(t *testing.T) {
	f := sheet(t)
	for _, name := range []string{"TRIM", "CLEAN", "LOWER", "UPPER", "PROPER"} {
		mustRejectWith(t, f, "="+name+"()", name+" requires 1 argument")
		mustRejectWith(t, f, "="+name+`("a","b")`, name+" requires 1 argument")
	}
}
