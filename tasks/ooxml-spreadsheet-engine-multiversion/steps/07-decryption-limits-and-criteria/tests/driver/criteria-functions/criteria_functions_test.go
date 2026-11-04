// Scored group: criteria-functions.
//
// The criteria-driven aggregate SUMIF and the statistical and date functions
// that arrive alongside it: MEDIAN, COUNTA and DATE. Semantics follow
// OpenDocument v1.3 part 4 (OpenFormula) section 6.16 "Mathematical Functions"
// (SUMIF, 6.16.62), section 6.18 "Statistical Functions" (MEDIAN, 6.18.47),
// section 6.13 "Information Functions" (COUNTA, 6.13.7 -- COUNTA is filed
// under Information, not Statistical) and section 6.10 "Date and Time
// Functions" (DATE, 6.10.2), with this checkpoint's deviations pinned rather
// than glossed over.
package criteriafunctions

import (
	"strings"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

func sheet(t *testing.T) *seedcalc.File {
	t.Helper()
	f := seedcalc.NewFile()
	for _, c := range []struct {
		axis string
		v    interface{}
	}{
		{"A1", 1}, {"A2", 2}, {"A3", 3}, {"A4", 4},
		{"B1", "x"}, {"B2", "y"}, {"B3", "x"}, {"B4", "z"},
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

func TestSUMIFSumsASeparateRangeSelectedByATextCriterion(t *testing.T) {
	f := sheet(t)
	// Rows whose B value is "x" are rows 1 and 3, so the sum over A is 1+3.
	mustEval(t, f, `=SUMIF(B1:B4,"x",A1:A4)`, "4")
	mustEval(t, f, `=SUMIF(B1:B4,"y",A1:A4)`, "2")
	mustEval(t, f, `=SUMIF(B1:B4,"absent",A1:A4)`, "0")
}

func TestSUMIFWithoutASumRangeAggregatesTheTestedRange(t *testing.T) {
	f := sheet(t)
	// With two arguments the criterion range is also the summed range.
	mustEval(t, f, `=SUMIF(A1:A4,">2")`, "7")
	mustEval(t, f, `=SUMIF(A1:A4,">=2")`, "9")
	mustEval(t, f, `=SUMIF(A1:A4,"<2")`, "1")
	mustEval(t, f, `=SUMIF(A1:A4,"<=2")`, "3")
	// An equals sign is optional: a bare value is an equality criterion.
	mustEval(t, f, `=SUMIF(A1:A4,"=2")`, "2")
	mustEval(t, f, `=SUMIF(A1:A4,"2")`, "2")
	// Quirk of this checkpoint: "<>" is not a recognised operator. The parser
	// matches its leading "<" and is left with ">2" as the operand, which is
	// not a number, so the criterion selects nothing at all rather than
	// selecting everything unequal.
	mustEval(t, f, `=SUMIF(A1:A4,"<>2")`, "0")
}

func TestSUMIFAcceptsLeadingAndTrailingWildcards(t *testing.T) {
	f := seedcalc.NewFile()
	for _, c := range []struct {
		axis string
		v    interface{}
	}{
		{"A1", 1}, {"A2", 2}, {"A3", 3}, {"A4", 4},
		{"B1", "apple"}, {"B2", "banana"}, {"B3", "apricot"}, {"B4", "cherry"},
	} {
		if err := f.SetCellValue("Sheet1", c.axis, c.v); err != nil {
			t.Fatalf("SetCellValue(%s): %v", c.axis, err)
		}
	}
	// A trailing star is a prefix match, a leading star a suffix match.
	mustEval(t, f, `=SUMIF(B1:B4,"ap*",A1:A4)`, "4")
	mustEval(t, f, `=SUMIF(B1:B4,"*rry",A1:A4)`, "4")
	mustEval(t, f, `=SUMIF(B1:B4,"*a",A1:A4)`, "2")
	// Without a star the text must match in full.
	mustEval(t, f, `=SUMIF(B1:B4,"apple",A1:A4)`, "1")
	mustEval(t, f, `=SUMIF(B1:B4,"ap",A1:A4)`, "0")
}

func TestSUMIFRequiresAtLeastTwoArguments(t *testing.T) {
	f := sheet(t)
	// The count in the message is singular where English wants a plural; the
	// assertion records the text the engine actually emits.
	mustRejectWith(t, f, "=SUMIF(A1:A4)", "SUMIF requires at least 2 argument")
	mustRejectWith(t, f, "=SUMIF()", "SUMIF requires at least 2 argument")
}

func TestMEDIANOverARangeIsTheRealMedian(t *testing.T) {
	f := sheet(t)
	// Given a range the function behaves as documented: the middle value of an
	// odd-sized sample, the mean of the middle pair of an even-sized one.
	mustEval(t, f, "=MEDIAN(A1:A3)", "2")
	mustEval(t, f, "=MEDIAN(A1:A4)", "2.5")
	mustEval(t, f, "=MEDIAN(A1:A1)", "1")
}

func TestMEDIANOverLiteralArgumentsUsesOnlyTheLastOne(t *testing.T) {
	f := sheet(t)
	// Quirk of this checkpoint: for scalar arguments MEDIAN re-reads the *last*
	// argument once per argument, so the sample is that value repeated and the
	// median is always the final argument. Ranges are unaffected.
	mustEval(t, f, "=MEDIAN(1,2,3)", "3")
	mustEval(t, f, "=MEDIAN(3,1,2)", "2")
	mustEval(t, f, "=MEDIAN(9,9,1)", "1")
	// A single argument is its own median either way.
	mustEval(t, f, "=MEDIAN(5)", "5")
}

func TestMEDIANRequiresAtLeastOneArgument(t *testing.T) {
	f := sheet(t)
	mustRejectWith(t, f, "=MEDIAN()", "MEDIAN requires at least 1 argument")
}

func TestCOUNTACountsNonEmptyCells(t *testing.T) {
	f := sheet(t)
	mustEval(t, f, "=COUNTA(A1:A4)", "4")
	mustEval(t, f, "=COUNTA(A1)", "1")
	// Text counts as much as a number.
	mustEval(t, f, "=COUNTA(B1:B4)", "4")
	// An empty argument list counts nothing rather than failing.
	mustEval(t, f, "=COUNTA()", "0")
}

func TestDATERequiresExactlyThreeArguments(t *testing.T) {
	f := sheet(t)
	// Too few, too many, none at all, and three of which one is not a number
	// all land on the same complaint.
	mustRejectWith(t, f, "=DATE(2020,5)", "DATE requires 3 number arguments")
	mustRejectWith(t, f, "=DATE(2020,5,11,1)", "DATE requires 3 number arguments")
	mustRejectWith(t, f, "=DATE()", "DATE requires 3 number arguments")
	mustRejectWith(t, f, `=DATE(2020,5,"x")`, "DATE requires 3 number arguments")
}

func TestDATERendersTheConstructedInstant(t *testing.T) {
	f := sheet(t)
	// The result is the constructed time rendered in full, not a serial number.
	got, err := eval(t, f, "=DATE(2020,5,11)")
	if err != nil {
		t.Fatalf("CalcCellValue(=DATE(2020,5,11)): %v", err)
	}
	if !strings.HasPrefix(got, "2020-05-11") {
		t.Fatalf("CalcCellValue(=DATE(2020,5,11)) = %q, want a rendering beginning %q", got, "2020-05-11")
	}
}
