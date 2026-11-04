// Scored group: text-function-library.
//
// The string family this milestone adds: length, the substring extractors and
// their byte-wise twins, search and replacement, repetition and concatenation,
// exact comparison, the character-code conversions, decimal formatting and the
// T/N coercion pair. Semantics follow OpenDocument v1.3 part 4 (OpenFormula)
// section 6.20 "Text Functions"; where this checkpoint departs from that text
// the deviation is asserted as the behaviour to reproduce, and said so.
package textfunctionlibrary

import (
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// sheet builds the fixture the reference cases read: A1:A3 numeric, B1:B3 text.
func sheet(t *testing.T) *seedcalc.File {
	t.Helper()
	f := seedcalc.NewFile()
	for _, c := range []struct {
		axis string
		v    interface{}
	}{
		{"A1", 1}, {"A2", 2}, {"A3", 3},
		{"B1", "apple"}, {"B2", "Banana"}, {"B3", "cherry"},
	} {
		if err := f.SetCellValue("Sheet1", c.axis, c.v); err != nil {
			t.Fatalf("SetCellValue(%s): %v", c.axis, err)
		}
	}
	return f
}

func eval(t *testing.T, f *seedcalc.File, formula string) (string, error) {
	t.Helper()
	if err := f.SetCellFormula("Sheet1", "Z1", formula); err != nil {
		t.Fatalf("SetCellFormula(%q): %v", formula, err)
	}
	return f.CalcCellValue("Sheet1", "Z1")
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

func TestLENCountsCharacters(t *testing.T) {
	f := sheet(t)
	mustEval(t, f, `=LEN("hello")`, "5")
	mustEval(t, f, `=LEN("")`, "0")
	mustEval(t, f, "=LEN(B1)", "5")
	// LENB is the byte-wise name; on this text it agrees with LEN.
	mustEval(t, f, `=LENB("hello")`, "5")
}

func TestLEFTAndRIGHTTakeFromEitherEnd(t *testing.T) {
	f := sheet(t)
	mustEval(t, f, `=LEFT("abcdef",2)`, "ab")
	mustEval(t, f, `=RIGHT("abcdef",2)`, "ef")
	// The count is optional and defaults to one character.
	mustEval(t, f, `=LEFT("abcdef")`, "a")
	mustEval(t, f, `=RIGHT("abcdef")`, "f")
	// Zero takes nothing; a count past the end takes the whole string.
	mustEval(t, f, `=LEFT("abcdef",0)`, "")
	mustEval(t, f, `=LEFT("abc",99)`, "abc")
	// A negative count is refused rather than clamped.
	mustReject(t, f, `=LEFT("abcdef",-1)`)
}

func TestMIDTakesFromTheMiddle(t *testing.T) {
	f := sheet(t)
	mustEval(t, f, `=MID("abcdef",2,3)`, "bcd")
	// A start position past the end yields nothing rather than an error.
	mustEval(t, f, `=MID("abcdef",99,2)`, "")
	// Unlike LEFT and RIGHT, MID has no optional argument: all three are required.
	mustReject(t, f, `=MID("abcdef",2)`)
}

func TestTheByteWiseNamesMirrorTheirCharacterTwins(t *testing.T) {
	f := sheet(t)
	// LEFTB, RIGHTB, MIDB and FINDB exist as distinct registered names. On
	// single-byte text each agrees with the name it shadows, which is what
	// makes them testable through the public interface.
	mustEval(t, f, `=LEFTB("abcdef",2)`, "ab")
	mustEval(t, f, `=RIGHTB("abcdef",2)`, "ef")
	mustEval(t, f, `=MIDB("abcdef",2,3)`, "bcd")
	mustEval(t, f, `=FINDB("c","abcdef")`, "3")
	mustEval(t, f, `=REPLACEB("abcdef",2,3,"XY")`, "aXYef")
}

func TestFINDLocatesASubstringCaseSensitively(t *testing.T) {
	f := sheet(t)
	mustEval(t, f, `=FIND("c","abcdef")`, "3")
	// The optional third argument is where the search starts, one-based.
	mustEval(t, f, `=FIND("c","abcabc",4)`, "5")
	// The empty needle is found at the start.
	mustEval(t, f, `=FIND("","abc")`, "1")
	// A miss is an error, and the search does not fold case.
	mustReject(t, f, `=FIND("z","abcdef")`)
	mustReject(t, f, `=FIND("C","abcdef")`)
}

func TestREPLACESplicesByPosition(t *testing.T) {
	f := sheet(t)
	mustEval(t, f, `=REPLACE("abcdef",2,3,"XY")`, "aXYef")
	// Positions are one-based; zero is out of range.
	mustReject(t, f, `=REPLACE("abcdef",0,1,"X")`)
}

func TestSUBSTITUTEReplacesByContent(t *testing.T) {
	f := sheet(t)
	mustEval(t, f, `=SUBSTITUTE("aXbXc","X","-")`, "a-b-c")
	// The optional fourth argument selects one occurrence, counting from one.
	mustEval(t, f, `=SUBSTITUTE("aXbXc","X","-",2)`, "aXb-c")
	// A needle that does not occur leaves the string alone.
	mustEval(t, f, `=SUBSTITUTE("aXbXc","Z","-")`, "aXbXc")
	// Occurrence zero does not mean "all"; it is out of range.
	mustReject(t, f, `=SUBSTITUTE("aXbXc","X","-",0)`)
}

func TestREPTAndTheConcatenationPair(t *testing.T) {
	f := sheet(t)
	mustEval(t, f, `=REPT("ab",3)`, "ababab")
	mustEval(t, f, `=REPT("ab",0)`, "")
	mustReject(t, f, `=REPT("ab",-1)`)
	// CONCAT and CONCATENATE are both registered and agree on scalars.
	mustEval(t, f, `=CONCAT("a","b","c")`, "abc")
	mustEval(t, f, `=CONCATENATE("a","b","c")`, "abc")
	mustEval(t, f, "=CONCAT(A1,A2)", "12")
	// Deviation from OpenFormula: at this checkpoint CONCAT does not flatten a
	// range argument, it refuses one.
	mustReject(t, f, "=CONCAT(A1:A3)")
}

func TestEXACTComparesWithoutFoldingCase(t *testing.T) {
	f := sheet(t)
	mustEval(t, f, `=EXACT("abc","abc")`, "TRUE")
	mustEval(t, f, `=EXACT("abc","ABC")`, "FALSE")
	// Comparison is on the string form, so a number matches its own digits.
	mustEval(t, f, `=EXACT(1,"1")`, "TRUE")
}

func TestTheCharacterCodeConversions(t *testing.T) {
	f := sheet(t)
	mustEval(t, f, "=CHAR(65)", "A")
	mustEval(t, f, "=UNICHAR(65)", "A")
	mustEval(t, f, `=CODE("A")`, "65")
	mustEval(t, f, `=UNICODE("A")`, "65")
	// CODE and UNICODE read the first character of a longer string.
	mustEval(t, f, `=CODE("abc")`, "97")
	mustEval(t, f, `=UNICODE("abc")`, "97")
	// The two pairs part company at their edges: CHAR is limited to the
	// single-byte range, UNICHAR has no code point zero, and CODE answers for
	// the empty string where UNICODE refuses it.
	mustReject(t, f, "=CHAR(256)")
	mustReject(t, f, "=UNICHAR(0)")
	mustEval(t, f, `=CODE("")`, "0")
	mustReject(t, f, `=UNICODE("")`)
}

func TestFIXEDFormatsWithSeparatorsAndRounding(t *testing.T) {
	f := sheet(t)
	// Deviation from OpenFormula, which specifies two decimals by default: at
	// this checkpoint an omitted decimals argument leaves the value unrounded.
	mustEval(t, f, "=FIXED(1234.567)", "1,234.567")
	mustEval(t, f, "=FIXED(1234.567,1)", "1,234.6")
	// The optional third argument suppresses the thousands separators.
	mustEval(t, f, "=FIXED(1234.567,1,TRUE)", "1234.6")
	// Negative decimals round to the left of the point.
	mustEval(t, f, "=FIXED(1234.567,-1)", "1,230")
}

func TestTAndNCoerceBetweenTextAndNumber(t *testing.T) {
	f := sheet(t)
	// N maps to a number: text is zero, a boolean is one.
	mustEval(t, f, "=N(1)", "1")
	mustEval(t, f, `=N("abc")`, "0")
	mustEval(t, f, "=N(TRUE())", "1")
	// Deviation from OpenFormula, where T of a non-text argument is the empty
	// string: this checkpoint hands the argument's string form straight back.
	mustEval(t, f, `=T("abc")`, "abc")
	mustEval(t, f, "=T(1)", "1")
}

func TestISTEXTAndENCODEURLInspectAndEscape(t *testing.T) {
	f := sheet(t)
	mustEval(t, f, `=ISTEXT("a")`, "TRUE")
	mustEval(t, f, "=ISTEXT(1)", "FALSE")
	mustEval(t, f, "=ISTEXT(B1)", "TRUE")
	mustEval(t, f, "=ISTEXT(A1)", "FALSE")
	mustEval(t, f, `=ENCODEURL("a b&c")`, "a%20b%26c")
}
