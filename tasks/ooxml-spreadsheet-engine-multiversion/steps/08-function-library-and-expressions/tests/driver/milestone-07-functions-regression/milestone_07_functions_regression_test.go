// Gate group: milestone-07-functions-regression.
//
// The function set milestone 07 added -- the boolean connectives, the IS-family
// information functions, the criteria and aggregation functions, the case and
// whitespace text functions, and the rounding family -- checked against the
// much larger library that arrives here. Every case passes unchanged against
// the milestone 07 checkpoint, which is what makes the group a veto rather
// than a score.
//
// The rounding cases are here rather than in dotted-function-names because the
// dotted spellings already evaluate at the milestone 07 checkpoint: the
// tokenizer there drops the dot before the lookup, so =CEILING.MATH(1.2) finds
// the run-together registration and answers 2. What milestone 08 changes is
// which spellings the engine will accept, not what the dotted ones compute,
// and that change is what dotted-function-names scores.
//
// Three milestone 07 contracts do move at this checkpoint -- a bare TRUE or
// FALSE in an argument position, NA() at the top level, and MEDIAN over a
// scalar argument list. None of them is asserted here; they are scored in
// inherited-contract-migrations.
package milestone07functionsregression

import (
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// sheet builds the fixture:
//
//	     A     B          C        D
//	1    1     apple      10
//	2    2     banana     20       written
//	3    3     apricot    30
//	4    4     cherry     40
func sheet(t *testing.T) *seedcalc.File {
	t.Helper()
	f := seedcalc.NewFile()
	for _, c := range []struct {
		axis string
		v    interface{}
	}{
		{"A1", 1}, {"A2", 2}, {"A3", 3}, {"A4", 4},
		{"B1", "apple"}, {"B2", "banana"}, {"B3", "apricot"}, {"B4", "cherry"},
		{"C1", 10}, {"C2", 20}, {"C3", 30}, {"C4", 40},
		{"D2", "written"},
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

func TestANDAndORStillConjoinAndDisjoin(t *testing.T) {
	f := sheet(t)
	mustEval(t, f, `=AND("TRUE","TRUE")`, "TRUE")
	mustEval(t, f, `=AND("TRUE","FALSE")`, "FALSE")
	mustEval(t, f, `=OR("TRUE","FALSE")`, "TRUE")
	mustEval(t, f, `=OR("FALSE","FALSE")`, "FALSE")
	mustReject(t, f, "=AND()")
	mustReject(t, f, "=OR()")
}

func TestORStillKeepsOnlyTheLastNumericArgument(t *testing.T) {
	f := sheet(t)
	// Unchanged quirk: for numeric arguments OR assigns rather than
	// accumulates, so the last numeric argument alone decides the result.
	mustEval(t, f, "=OR(1,0)", "FALSE")
	mustEval(t, f, "=OR(0,1)", "TRUE")
	mustEval(t, f, "=OR(1,1,1,0)", "FALSE")
}

func TestTheInformationFunctionsStillClassify(t *testing.T) {
	f := sheet(t)
	mustEval(t, f, "=ISBLANK(D1)", "TRUE")
	mustEval(t, f, "=ISBLANK(D2)", "FALSE")
	mustEval(t, f, "=ISNUMBER(A1)", "TRUE")
	mustEval(t, f, "=ISNUMBER(B1)", "FALSE")
	mustEval(t, f, "=ISNONTEXT(B1)", "FALSE")
	mustEval(t, f, "=ISNONTEXT(D1)", "TRUE")
	mustEval(t, f, "=ISEVEN(4)", "TRUE")
	mustEval(t, f, "=ISODD(3)", "TRUE")
}

func TestSUMIFStillSelectsByCriterion(t *testing.T) {
	f := sheet(t)
	mustEval(t, f, `=SUMIF(B1:B4,"apple",C1:C4)`, "10")
	mustEval(t, f, `=SUMIF(A1:A4,">2")`, "7")
	mustEval(t, f, `=SUMIF(A1:A4,"<=2")`, "3")
	// Wildcards still match: apple and apricot contribute 10 and 30.
	mustEval(t, f, `=SUMIF(B1:B4,"ap*",C1:C4)`, "40")
	mustReject(t, f, `=SUMIF(A1:A4)`)
}

func TestCOUNTAAndDATEAreUnchanged(t *testing.T) {
	f := sheet(t)
	mustEval(t, f, "=COUNTA(A1:A4)", "4")
	// D1, D3 and D4 were never written; only D2 was.
	mustEval(t, f, "=COUNTA(D1:D4)", "1")
	// DATE still renders the constructed instant rather than a serial number,
	// and still takes exactly three arguments.
	mustEval(t, f, "=DATE(2020,5,11)", "2020-05-11 00:00:00 +0000 UTC")
	mustReject(t, f, "=DATE(2020,5)")
}

func TestTheCaseAndWhitespaceTextFunctionsAreUnchanged(t *testing.T) {
	f := sheet(t)
	// TRIM still strips the ends without collapsing the interior run, so the
	// two spaces between a and b survive. A single interior space would not
	// have told the two behaviours apart.
	mustEval(t, f, `=TRIM("  a  b  ")`, "a  b")
	mustEval(t, f, `=LOWER("ABC")`, "abc")
	mustEval(t, f, `=UPPER("abc")`, "ABC")
	mustEval(t, f, `=PROPER("hello world")`, "Hello World")
	// A numeric operand is still taken as its text form, and the functions
	// still nest.
	mustEval(t, f, "=LOWER(123)", "123")
	mustEval(t, f, `=UPPER(TRIM("  ab  "))`, "AB")
	mustReject(t, f, "=TRIM()")
}

func TestTheRenamedCeilingFunctions(t *testing.T) {
	f := sheet(t)
	mustEval(t, f, "=CEILING.MATH(1.2)", "2")
	mustEval(t, f, "=CEILING.PRECISE(1.2)", "2")
	mustEval(t, f, "=ISO.CEILING(1.2)", "2")
}

func TestTheRenamedFloorFunctions(t *testing.T) {
	f := sheet(t)
	mustEval(t, f, "=FLOOR.MATH(1.8)", "1")
	mustEval(t, f, "=FLOOR.PRECISE(1.8)", "1")
}

func TestTheUndottedCeilingAndFloorAreUntouched(t *testing.T) {
	f := sheet(t)
	// The plain two-argument names were never part of the rename and keep
	// working exactly as milestone 06 left them.
	mustEval(t, f, "=CEILING(1.2,1)", "2")
	mustEval(t, f, "=FLOOR(1.8,1)", "1")
}

func TestTheRenamedFunctionsKeepTheirOptionalArguments(t *testing.T) {
	f := sheet(t)
	// CEILING.MATH still takes a significance and a mode, and the mode is still
	// read as a sign rather than as the flag OpenFormula 6.17.1 describes: only
	// a NEGATIVE mode rounds a negative operand away from zero, so a mode of 1
	// leaves -1.2 rounding toward zero. TEST_INTERFACE.md 4.08.8 records the
	// deviation and both answers; 6.17.1 on its own would give -2 here.
	mustEval(t, f, "=CEILING.MATH(-1.2,1,1)", "-1")
	mustEval(t, f, "=CEILING.MATH(-1.2,1,-1)", "-2")
	mustEval(t, f, "=CEILING.PRECISE(-1.2,1)", "-1")
	// An empty argument list is still an arity error.
	mustReject(t, f, "=CEILING.MATH()")
}
