// Gate group: milestone-09-functions-regression.
//
// The function library milestone 09 added -- the criteria family, the lookup
// and reference family, the conditional aggregates, the date arithmetic, the
// distribution and rank functions, the radix and complex-number engineering
// functions, and the financial family -- checked against the far larger
// library that arrives here. Every one of them must behave exactly as
// milestone 09 left it.
//
// Three contracts in this family do move at this milestone, and none of them
// is asserted here; they are scored in inherited-contract-migrations instead,
// so that nothing in this gate group asks the previous milestone's own output
// to behave like this one:
//
//   - a numeric result of more than fifteen digits in total, rather than more
//     than fifteen digits after the point, is re-rendered
//   - ISERR stops propagating the error it is asked to classify
//   - a boolean cell renders as TRUE or FALSE rather than as 1 or 0
//
// Semantics follow OpenDocument v1.3 part 4 (OpenFormula) sections 6.9
// through 6.20.
package milestone09functionsregression

import (
	"math"
	"strconv"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// sheet builds the fixture every case in this group reads:
//
//	     A     B         C      E
//	1    1     apple     10     44562
//	2    2     banana    20     44927
//	3    3     cherry    30
//	4    4     date      40
//	5    5
//
// E1 and E2 are the serial numbers of 2022-01-01 and 2023-01-01: exactly one
// common year apart, so the day count is 365 with no leap day in it.
func sheet(t *testing.T) *seedcalc.File {
	t.Helper()
	f := seedcalc.NewFile()
	for _, c := range []struct {
		axis string
		v    interface{}
	}{
		{"A1", 1}, {"A2", 2}, {"A3", 3}, {"A4", 4}, {"A5", 5},
		{"B1", "apple"}, {"B2", "banana"}, {"B3", "cherry"}, {"B4", "date"},
		{"C1", 10}, {"C2", 20}, {"C3", 30}, {"C4", 40},
		{"E1", 44562}, {"E2", 44927},
	} {
		if err := f.SetCellValue("Sheet1", c.axis, c.v); err != nil {
			t.Fatalf("SetCellValue(%s): %v", c.axis, err)
		}
	}
	return f
}

func evalAt(t *testing.T, f *seedcalc.File, sheetName, axis, formula string) (string, error) {
	t.Helper()
	if err := f.SetCellFormula(sheetName, axis, formula); err != nil {
		t.Fatalf("SetCellFormula(%q): %v", formula, err)
	}
	return f.CalcCellValue(sheetName, axis)
}

func mustEval(t *testing.T, f *seedcalc.File, formula, want string) {
	t.Helper()
	got, err := evalAt(t, f, "Sheet1", "Z1", formula)
	if err != nil {
		t.Fatalf("CalcCellValue(%q): unexpected error %v", formula, err)
	}
	if got != want {
		t.Fatalf("CalcCellValue(%q) = %q, want %q", formula, got, want)
	}
}

// mustEvalNear grades a value rather than a rendering, on a relative tolerance.
//
// It exists for the iterative functions.  An expectation written as a fifteen
// digit string pins the arithmetic that produced those digits, which is fine
// for a closed form and wrong for a Newton iteration that stops on a loose
// step test: §4.09.7 fixes the residual's root, the starting guess and the
// `|delta| < 1e-6` stop, and those three do not fix the trailing digits.  Two
// implementations that follow that contract exactly -- same start, same stop,
// same iteration count -- part company around the eleventh significant digit
// depending on whether the derivative is the analytic one or a difference
// quotient, which the contract does not say.  Measured, not assumed: the
// Oracle's arrangement gives `0.00618341316212928` for `=RATE(60,-200,10000)`
// and the same iteration with a central-difference derivative gives
// `0.00618341316199073`.  This group is a GATE, so grading the rendering there
// would veto a conforming solution to `reward = 0` over a digit the
// specification never promised.  A relative 1e-6 still holds the rate to seven
// figures, and a RATE that is actually broken is wrong in the percent.
func mustEvalNear(t *testing.T, f *seedcalc.File, formula string, want, relTol float64) {
	t.Helper()
	got, err := evalAt(t, f, "Sheet1", "Z1", formula)
	if err != nil {
		t.Fatalf("CalcCellValue(%q): unexpected error %v", formula, err)
	}
	v, convErr := strconv.ParseFloat(got, 64)
	if convErr != nil {
		t.Fatalf("CalcCellValue(%q) = %q, want a number within %g of %.17g", formula, got, relTol, want)
	}
	if math.Abs(v-want) > relTol*math.Abs(want) {
		t.Fatalf("CalcCellValue(%q) = %q, want %.17g within a relative %g", formula, got, want, relTol)
	}
}

func mustReject(t *testing.T, f *seedcalc.File, formula string) {
	t.Helper()
	got, err := evalAt(t, f, "Sheet1", "Z1", formula)
	if err == nil {
		t.Fatalf("CalcCellValue(%q) = %q, want an error", formula, got)
	}
	if got != "" {
		t.Fatalf("CalcCellValue(%q) returned result %q alongside an error", formula, got)
	}
}

func TestTheCriteriaFamilyIsUnchanged(t *testing.T) {
	f := sheet(t)
	mustEval(t, f, `=SUMIF(A1:A5,">2")`, "12")
	// A third argument moves the summation to a parallel range.
	mustEval(t, f, `=SUMIF(A1:A5,">2",C1:C4)`, "70")
	mustEval(t, f, `=SUMIF(A1:A5,"<>3")`, "12")
	mustEval(t, f, `=AVERAGEIF(A1:A5,">2")`, "4")
	// A wildcard still matches a run of characters in a text criterion.
	mustEval(t, f, `=COUNTIF(B1:B4,"a*")`, "3")
	mustEval(t, f, `=COUNTIFS(A1:A5,">1",A1:A5,"<5")`, "3")
	mustEval(t, f, `=MAXIFS(A1:A5,A1:A5,">2")`, "5")
	mustEval(t, f, `=MINIFS(A1:A5,A1:A5,">2")`, "3")
}

func TestTheLookupAndReferenceFunctionsAreUnchanged(t *testing.T) {
	f := sheet(t)
	mustEval(t, f, `=VLOOKUP(3,A1:C4,3,FALSE)`, "30")
	mustEval(t, f, `=MATCH(3,A1:A5,0)`, "3")
	mustEval(t, f, `=LOOKUP(3,A1:A5)`, "3")
	mustEval(t, f, `=CHOOSE(2,"a","b","c")`, "b")
	mustEval(t, f, `=COLUMN(C1)`, "3")
	mustEval(t, f, `=ROW(A3)`, "3")
	mustEval(t, f, `=COLUMNS(A1:C4)`, "3")
	mustEval(t, f, `=ROWS(A1:C4)`, "4")
	// The reference ADDRESS builds is absolute by default and mixed on 2.
	mustEval(t, f, `=ADDRESS(2,3)`, "$C$3")
	mustEval(t, f, `=ADDRESS(2,3,2)`, "C$3")
	// A horizontal lookup that finds nothing is still an error, not a blank.
	mustReject(t, f, `=HLOOKUP(2,A1:C4,2,FALSE)`)
}

func TestTheConditionalAggregatesAreUnchanged(t *testing.T) {
	f := sheet(t)
	mustEval(t, f, `=IFS(A1=2,"two",A1=1,"one")`, "one")
	mustEval(t, f, `=SWITCH(A2,1,"one",2,"two","other")`, "two")
	mustEval(t, f, `=IF(A1=1,"one","other")`, "one")
	mustEval(t, f, `=IFERROR(1/0,"fallback")`, "fallback")
	mustEval(t, f, `=ISNA(NA())`, "TRUE")
	// NA() itself is still raised rather than returned as text.
	mustReject(t, f, `=NA()`)
}

func TestTheDateArithmeticIsUnchanged(t *testing.T) {
	f := sheet(t)
	mustEval(t, f, `=DATEDIF(E1,E2,"d")`, "365")
	mustEval(t, f, `=DAYS(E2,E1)`, "365")
	mustEval(t, f, `=YEARFRAC(E1,E2)`, "1")
	// 2022-01-01 falls in the final ISO week of 2021.
	mustEval(t, f, `=ISOWEEKNUM(E1)`, "52")
	// The three date functions milestone 09 left out are still absent.
	mustReject(t, f, `=WORKDAY(E1,5)`)
	mustReject(t, f, `=EOMONTH(E1,0)`)
}

func TestTheDistributionAndRankFunctionsAreUnchanged(t *testing.T) {
	f := sheet(t)
	mustEval(t, f, `=PERCENTILE(A1:A5,0.5)`, "3")
	mustEval(t, f, `=QUARTILE(A1:A5,1)`, "2")
	mustEval(t, f, `=RANK(3,A1:A5)`, "3")
	mustEval(t, f, `=LARGE(A1:A5,2)`, "4")
	mustEval(t, f, `=SMALL(A1:A5,2)`, "2")
	mustEval(t, f, `=STDEV(A1:A5)`, "1.58113883008419")
	mustEval(t, f, `=VAR(A1:A5)`, "2.5")
	mustEval(t, f, `=NORMDIST(1,0,1,TRUE)`, "0.841344746068543")
	mustEval(t, f, `=NORMSDIST(1)`, "0.841344746068543")
	mustEval(t, f, `=NORMSINV(0.5)`, "0")
	mustEval(t, f, `=SKEW(A1:A5)`, "0")
	mustEval(t, f, `=AVEDEV(A1:A5)`, "3")
	mustEval(t, f, `=DEVSQ(A1:A5)`, "10")
	// CHIDIST predates this milestone; the dotted spellings do not.
	mustEval(t, f, `=CHIDIST(0.5,3)`, "0.918891411654676")
}

func TestTheEngineeringAndTextFunctionsAreUnchanged(t *testing.T) {
	f := sheet(t)
	mustEval(t, f, `=BIN2DEC(1010)`, "10")
	mustEval(t, f, `=DEC2HEX(255)`, "FF")
	mustEval(t, f, `=HEX2DEC("FF")`, "255")
	mustEval(t, f, `=COMPLEX(1,2)`, "1+2i")
	mustEval(t, f, `=IMSUM("1+2i","3+4i")`, "4+6i")
	mustEval(t, f, `=DELTA(1,1)`, "1")
	mustEval(t, f, `=GESTEP(2,1)`, "1")
	mustEval(t, f, `=BESSELJ(1,0)`, "0.765197686557967")
	mustEval(t, f, `=TEXTJOIN("-",TRUE,A1:A3)`, "1-2-3")
	mustEval(t, f, `=UNICHAR(65)`, "A")
	mustEval(t, f, `=UNICODE("A")`, "65")
	// The fourth argument selects which occurrence is replaced.
	mustEval(t, f, `=SUBSTITUTE("abcabc","b","X",2)`, "abcaXc")
	mustEval(t, f, `=REPT("ab",3)`, "ababab")
	mustEval(t, f, `=FIND("b","abc")`, "2")
}

func TestTheFinancialFamilyIsUnchanged(t *testing.T) {
	f := sheet(t)
	// Every expectation here is a result whose shortest exact rendering fits
	// inside fifteen digits, so none of them is touched by the widened
	// re-rendering this milestone brings.
	mustEval(t, f, `=SLN(1000,100,10)`, "90")
	mustEval(t, f, `=DB(1000,100,10,1)`, "206")
	mustEval(t, f, `=DDB(1000,100,10,1)`, "200")
	mustEval(t, f, `=NPV(0.1,A1:A5)`, "0")
	// RATE is the one iterative function in this group; see mustEvalNear.
	mustEvalNear(t, f, `=RATE(60,-200,10000)`, 0.00618341316212928, 1e-6)
	mustEval(t, f, `=DOLLARDE(1.02,16)`, "1.125")
	mustEval(t, f, `=DOLLARFR(1.125,16)`, "1.02")
	mustEval(t, f, `=EFFECT(0.05,4)`, "0.0509453369140624")
	mustEval(t, f, `=NOMINAL(0.05,4)`, "0.0490889377161574")
	mustEval(t, f, `=ACCRINT(E1,E1,E2,0.1,1000,1,0,FALSE)`, "100")
}

func TestTheSheetFunctionAndTheExportedLimitsAreUnchanged(t *testing.T) {
	f := sheet(t)
	f.NewSheet("Second")
	mustEval(t, f, `=SHEET()`, "1")
	mustEval(t, f, `=SHEET("Sheet1")`, "1")
	mustEval(t, f, `=SHEET("Second")`, "2")
	if seedcalc.TotalRows != 1048576 {
		t.Fatalf("TotalRows = %d, want 1048576", seedcalc.TotalRows)
	}
	if seedcalc.TotalColumns != 16384 {
		t.Fatalf("TotalColumns = %d, want 16384", seedcalc.TotalColumns)
	}
	if got := seedcalc.ErrSheetIdx.Error(); got != "invalid worksheet index" {
		t.Fatalf("ErrSheetIdx = %q, want %q", got, "invalid worksheet index")
	}
}

func TestTheFifteenDigitRenderingMilestone09InstalledStillApplies(t *testing.T) {
	f := sheet(t)
	// Milestone 09 re-rendered a result with more than fifteen digits after
	// the decimal point. Those three still round exactly as they did; what
	// changes here is the set of results the rule reaches, not the rule's
	// output on the ones it already reached.
	mustEval(t, f, `=SQRT(2)`, "1.4142135623731")
	mustEval(t, f, `=10/3`, "3.33333333333333")
	mustEval(t, f, `=KURT(A1:A5)`, "-1.2")
	mustEval(t, f, `=HARMEAN(1,2,4)`, "1.71428571428571")
	// A short result is left exactly alone by either form of the rule.
	mustEval(t, f, `=0.1+0.2`, "0.3")
	mustEval(t, f, `=1.1*1.1`, "1.21")
}
