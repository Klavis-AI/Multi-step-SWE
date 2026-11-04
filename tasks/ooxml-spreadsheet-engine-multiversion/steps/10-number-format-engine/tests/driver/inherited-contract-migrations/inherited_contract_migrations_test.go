// Scored group: inherited-contract-migrations.
//
// Three contracts the previous milestone established change here, and the
// regression groups have been narrowed to leave room for them. Each is a
// deliberate correction, not a side effect:
//
//   - a boolean cell displays as TRUE or FALSE rather than as the 1 or 0 it
//     is stored as
//   - the fifteen-figure rounding that was applied to more than fifteen
//     decimal places is applied to more than fifteen significant figures,
//     and to the stored value as well as the displayed one
//   - ISERR and ISERROR classify the error they are given instead of
//     propagating it
//
// The boolean representation is ECMA-376 Part 1 section 18.18.2
// (ST_CellType); the information functions are OpenDocument v1.3 part 4
// (OpenFormula) section 6.13.14 and 6.13.15.
package inheritedcontractmigrations

import (
	"math"
	"strconv"
	"strings"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

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

// values reads a cell both ways: as displayed and as stored.
func values(t *testing.T, f *seedcalc.File, axis string) (display, raw string) {
	t.Helper()
	var err error
	if display, err = f.GetCellValue("Sheet1", axis); err != nil {
		t.Fatalf("GetCellValue(%s): %v", axis, err)
	}
	if raw, err = f.GetCellValue("Sheet1", axis, seedcalc.Options{RawCellValue: true}); err != nil {
		t.Fatalf("GetCellValue(%s, raw): %v", axis, err)
	}
	return display, raw
}

func TestABooleanCellDisplaysAsAWordButIsStoredAsADigit(t *testing.T) {
	// Only the display moves. The sheet part still holds the ECMA-376
	// boolean, and the cell is still typed as one.
	f := seedcalc.NewFile()
	if err := f.SetCellBool("Sheet1", "A1", true); err != nil {
		t.Fatalf("SetCellBool(A1): %v", err)
	}
	if err := f.SetCellBool("Sheet1", "A2", false); err != nil {
		t.Fatalf("SetCellBool(A2): %v", err)
	}
	for axis, pair := range map[string][2]string{
		"A1": {"TRUE", "1"},
		"A2": {"FALSE", "0"},
	} {
		display, raw := values(t, f, axis)
		if display != pair[0] {
			t.Fatalf("GetCellValue(%s) = %q, want %q", axis, display, pair[0])
		}
		if raw != pair[1] {
			t.Fatalf("GetCellValue(%s, raw) = %q, want %q", axis, raw, pair[1])
		}
		typ, err := f.GetCellType("Sheet1", axis)
		if err != nil {
			t.Fatalf("GetCellType(%s): %v", axis, err)
		}
		if typ != seedcalc.CellTypeBool {
			t.Fatalf("GetCellType(%s) = %v, want CellTypeBool", axis, typ)
		}
	}
}

func TestTheBooleanDisplayReachesTheRowIterator(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetCellValue("Sheet1", "A1", true); err != nil {
		t.Fatalf("SetCellValue(A1): %v", err)
	}
	if err := f.SetCellValue("Sheet1", "A2", false); err != nil {
		t.Fatalf("SetCellValue(A2): %v", err)
	}
	rows, err := f.GetRows("Sheet1")
	if err != nil {
		t.Fatalf("GetRows: %v", err)
	}
	if len(rows) != 2 || len(rows[0]) != 1 || len(rows[1]) != 1 {
		t.Fatalf("GetRows = %q, want two single-cell rows", rows)
	}
	if rows[0][0] != "TRUE" || rows[1][0] != "FALSE" {
		t.Fatalf("GetRows = %q, want TRUE and FALSE", rows)
	}
	// Asking for raw values turns the words back into digits.
	raw, err := f.GetRows("Sheet1", seedcalc.Options{RawCellValue: true})
	if err != nil {
		t.Fatalf("GetRows(raw): %v", err)
	}
	if raw[0][0] != "1" || raw[1][0] != "0" {
		t.Fatalf("GetRows(raw) = %q, want 1 and 0", raw)
	}
}

func TestRoundingCountsSignificantFiguresNotDecimalPlaces(t *testing.T) {
	f := seedcalc.NewFile()
	// Sixteen significant figures with only fourteen after the point: the
	// previous rule left this alone, and this one shortens it.
	mustEval(t, f, `=100/7`, "14.2857142857143")
	mustEval(t, f, `=PI()`, "3.14159265358979")
	mustEval(t, f, `=1000/7`, "142.857142857143")
	// A result short enough to be exact is still untouched.
	mustEval(t, f, `=0.1+0.2`, "0.3")
	mustEval(t, f, `=1.1*1.1`, "1.21")
}

// significantFigures counts the figures a rendering carries, which is what
// this milestone's rule is written in terms of: fifteen *figures*, not fifteen
// places, with trailing zeros dropped.
func significantFigures(s string) int {
	if i := strings.IndexAny(s, "eE"); i >= 0 {
		s = s[:i]
	}
	s = strings.TrimPrefix(strings.TrimSpace(s), "-")
	s = strings.Replace(s, ".", "", 1)
	s = strings.TrimLeft(s, "0")
	return len(s)
}

// mustEvalRounded grades the milestone-10 narrowing without grading the
// arithmetic that produced the digits.
//
// The contract §4.10.15 states is a *width*: a result carries at most fifteen
// significant figures where milestone 09 gave sixteen or seventeen. Asserting
// the whole rendered string tests the width and, by accident, also pins the
// order in which the Oracle evaluated the closed form -- and for a function
// with algebraic freedom in it that is not the same requirement. `GEOMEAN` is
// the clear case: the geometric mean can be the n-th root of the product or
// the exponential of the mean logarithm, both correct, and they differ around
// the fifteenth figure -- exactly the figure the string pins. `PMT`, `FV` and
// `SYD` have the same freedom in a milder form, one or two units in the last
// place.
//
// So grade both halves of what the milestone actually promises: the value, to
// a relative tolerance far tighter than any real error and far looser than an
// arrangement difference, and the width, which is the part that changed here.
// A candidate that never implemented the narrowing renders seventeen figures
// and still fails.
func mustEvalRounded(t *testing.T, f *seedcalc.File, formula string, want float64, figures int) {
	t.Helper()
	got, err := eval(t, f, formula)
	if err != nil {
		t.Fatalf("CalcCellValue(%q): unexpected error %v", formula, err)
	}
	v, convErr := strconv.ParseFloat(got, 64)
	if convErr != nil {
		t.Fatalf("CalcCellValue(%q) = %q, want a number near %.17g", formula, got, want)
	}
	if math.Abs(v-want) > 1e-12*math.Abs(want) {
		t.Fatalf("CalcCellValue(%q) = %q, want %.17g within a relative 1e-12", formula, got, want)
	}
	if n := significantFigures(got); n > figures {
		t.Fatalf("CalcCellValue(%q) = %q, %d significant figures, want at most %d", formula, got, n, figures)
	}
}

func TestTheWidenedRoundingReachesTheFunctionLibrary(t *testing.T) {
	f := seedcalc.NewFile()
	// Every one of these was rendered a digit longer before.
	// The value and the width, separately: see mustEvalRounded.
	mustEvalRounded(t, f, `=GEOMEAN(1,2,3,4,5)`, 2.60517108469735, 15)
	mustEvalRounded(t, f, `=PMT(0.05/12,60,-50000)`, 943.561682200544, 15)
	mustEvalRounded(t, f, `=FV(0.05/12,60,-1000)`, 68006.0828408434, 15)
	mustEvalRounded(t, f, `=SYD(1000,100,10,1)`, 163.636363636364, 15)
}

func TestTheWidenedRoundingReachesTheStoredValue(t *testing.T) {
	// A float set into a cell is now shortened on the way in, so the raw
	// value and the displayed one agree rather than differing in the last
	// digits.
	f := seedcalc.NewFile()
	for axis, v := range map[string]float64{
		"A1": 1.0 / 3.0,
		"A2": 100.0 / 7.0,
		"A3": 2.0 / 3.0,
	} {
		if err := f.SetCellValue("Sheet1", axis, v); err != nil {
			t.Fatalf("SetCellValue(%s): %v", axis, err)
		}
	}
	for axis, want := range map[string]string{
		"A1": "0.333333333333333",
		"A2": "14.2857142857143",
		"A3": "0.666666666666667",
	} {
		display, raw := values(t, f, axis)
		if display != want {
			t.Fatalf("GetCellValue(%s) = %q, want %q", axis, display, want)
		}
		if raw != want {
			t.Fatalf("GetCellValue(%s, raw) = %q, want %q", axis, raw, want)
		}
	}
}

func TestISERRClassifiesRatherThanPropagates(t *testing.T) {
	f := seedcalc.NewFile()
	// The point of the function is to answer a question about an error, so
	// receiving one must not end the evaluation.
	mustEval(t, f, `=ISERR(1/0)`, "TRUE")
	mustEval(t, f, `=ISERROR(1/0)`, "TRUE")
	mustEval(t, f, `=ISERR(1)`, "FALSE")
	mustEval(t, f, `=ISERROR(1)`, "FALSE")
	// ISERR excludes the not-available error; ISERROR includes it.
	mustEval(t, f, `=ISERR(NA())`, "FALSE")
	mustEval(t, f, `=ISERROR(NA())`, "TRUE")
}

func TestTheClassifiersComposeWithTheRestOfTheLibrary(t *testing.T) {
	f := seedcalc.NewFile()
	// A classifier that answers instead of aborting can be read by the
	// operators and functions around it.
	mustEval(t, f, `=NOT(ISERR(1/0))`, "FALSE")
	mustEval(t, f, `=AND(ISERR(1/0),ISERROR(1/0))`, "TRUE")
	mustEval(t, f, `=ISERR(1/0)=TRUE()`, "TRUE")
	mustEval(t, f, `=SUM(1,2)+0*N(ISERR(1/0))`, "3")
	// IFERROR, which already swallowed the error, is unaffected.
	mustEval(t, f, `=IFERROR(1/0,"fallback")`, "fallback")
	// The answer is a rendered word, so arithmetic on it is refused for
	// that reason rather than by the error it was asked about.
	_, err := eval(t, f, `=ISERR(1/0)+0`)
	if err == nil || err.Error() != `strconv.ParseFloat: parsing "TRUE": invalid syntax` {
		t.Fatalf("ISERR(1/0)+0 error = %v, want a parse failure on TRUE", err)
	}
}
