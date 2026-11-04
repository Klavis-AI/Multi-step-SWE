// Scored group: statistical-function-library.
//
// The descriptive-statistics family this milestone adds: the means, the counts,
// the extrema and their text-aware twins, order statistics, dispersion,
// position measures, shape measures, the counting functions and the normal and
// Poisson distributions. Semantics follow OpenDocument v1.3 part 4
// (OpenFormula) section 6.18 "Statistical Functions". Numeric results are
// compared as the strings the evaluator returns, which is the only form the
// public interface exposes.
package statisticalfunctionlibrary

import (
	"math"
	"strconv"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// sheet builds the fixture: A1:A5 = 1..5, B1:B3 text, D1:D5 left blank.
func sheet(t *testing.T) *seedcalc.File {
	t.Helper()
	f := seedcalc.NewFile()
	for i := 1; i <= 5; i++ {
		axis, err := seedcalc.CoordinatesToCellName(1, i)
		if err != nil {
			t.Fatalf("CoordinatesToCellName: %v", err)
		}
		if err := f.SetCellValue("Sheet1", axis, i); err != nil {
			t.Fatalf("SetCellValue(%s): %v", axis, err)
		}
	}
	for axis, v := range map[string]string{"B1": "apple", "B2": "Banana", "B3": "cherry"} {
		if err := f.SetCellValue("Sheet1", axis, v); err != nil {
			t.Fatalf("SetCellValue(%s): %v", axis, err)
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

// mustEvalNear grades a numeric result on its value rather than on its
// rendering, to the relative tolerance the caller names. It exists for the one
// expectation in this group whose last digit belongs to the arithmetic route
// rather than to the value: KURT's fourth moment can be grouped in more than
// one correct way, and the renderings differ in the final figure.
func mustEvalNear(t *testing.T, f *seedcalc.File, formula string, want, rel float64) {
	t.Helper()
	got, err := eval(t, f, formula)
	if err != nil {
		t.Fatalf("CalcCellValue(%q): unexpected error %v", formula, err)
	}
	v, perr := strconv.ParseFloat(got, 64)
	if perr != nil {
		t.Fatalf("CalcCellValue(%q) = %q, want a number near %v", formula, got, want)
	}
	if math.Abs(v-want) > rel*math.Abs(want) {
		t.Fatalf("CalcCellValue(%q) = %q, want %v to a relative %v", formula, got, want, rel)
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

func TestAVERAGEOverRangesAndLiterals(t *testing.T) {
	f := sheet(t)
	mustEval(t, f, "=AVERAGE(A1:A5)", "3")
	mustEval(t, f, "=AVERAGE(1,2,3)", "2")
	// AVERAGEA counts text cells as zero, so a purely textual range averages to
	// zero rather than being skipped.
	mustEval(t, f, "=AVERAGEA(A1:A5)", "3")
	mustEval(t, f, "=AVERAGEA(B1:B3)", "0")
	// With no arguments there is nothing to divide by.
	mustReject(t, f, "=AVERAGE()")
}

func TestCOUNTAndCOUNTBLANK(t *testing.T) {
	f := sheet(t)
	// COUNT counts numbers only; text does not contribute.
	mustEval(t, f, "=COUNT(A1:A5)", "5")
	mustEval(t, f, "=COUNT(B1:B3)", "0")
	mustEval(t, f, "=COUNT(A1:A5,B1:B3)", "5")
	// COUNTBLANK is the complement over a range: D1:D5 was never written.
	mustEval(t, f, "=COUNTBLANK(A1:A5)", "0")
	mustEval(t, f, "=COUNTBLANK(D1:D5)", "5")
}

func TestMAXAndMINAndTheirTextAwareTwins(t *testing.T) {
	f := sheet(t)
	mustEval(t, f, "=MAX(A1:A5)", "5")
	mustEval(t, f, "=MIN(A1:A5)", "1")
	mustEval(t, f, "=MAXA(A1:A5)", "5")
	mustEval(t, f, "=MINA(A1:A5)", "1")
	// Text contributes nothing numeric, so an all-text range reports zero
	// rather than an error, under both the plain and the -A name.
	mustEval(t, f, "=MAX(B1:B3)", "0")
	mustEval(t, f, "=MAXA(B1:B3)", "0")
	mustEval(t, f, "=MIN(B1:B3)", "0")
	mustEval(t, f, "=MINA(B1:B3)", "0")
	mustReject(t, f, "=MAX()")
}

func TestLARGEAndSMALLTakeOrderStatistics(t *testing.T) {
	f := sheet(t)
	mustEval(t, f, "=LARGE(A1:A5,1)", "5")
	mustEval(t, f, "=LARGE(A1:A5,5)", "1")
	mustEval(t, f, "=SMALL(A1:A5,1)", "1")
	// k is one-based and bounded by the size of the array at both ends.
	mustReject(t, f, "=LARGE(A1:A5,0)")
	mustReject(t, f, "=LARGE(A1:A5,99)")
	mustReject(t, f, "=SMALL(A1:A5,99)")
}

func TestTheDispersionFunctions(t *testing.T) {
	f := sheet(t)
	// STDEV is the sample standard deviation; VARP the population variance.
	mustEval(t, f, "=STDEV(A1:A5)", "1.58113883008419")
	mustEval(t, f, "=STDEVA(A1:A5)", "1.58113883008419")
	mustEval(t, f, "=VARP(A1:A5)", "2")
	// A single observation has no sample deviation to report.
	mustReject(t, f, "=STDEV(1)")
}

func TestPERCENTILEAndQUARTILE(t *testing.T) {
	f := sheet(t)
	mustEval(t, f, "=PERCENTILE(A1:A5,0.5)", "3")
	mustEval(t, f, "=QUARTILE(A1:A5,1)", "2")
	// Each argument has its own domain: a percentile is a fraction, a quartile
	// an integer in 0..4, and each is refused outside it.
	mustReject(t, f, "=PERCENTILE(A1:A5,2)")
	mustReject(t, f, "=QUARTILE(A1:A5,5)")
}

func TestTheShapeMeasuresAndTheHarmonicMean(t *testing.T) {
	f := sheet(t)
	mustEvalNear(t, f, "=KURT(A1:A5)", -1.2, 1e-9)
	// A symmetric sample has no skew.
	mustEval(t, f, "=SKEW(A1:A5)", "0")
	mustEval(t, f, "=MEDIAN(A1:A5)", "3")
	// HARMEAN reads scalar arguments only; at this checkpoint it does not
	// flatten a range and reports #N/A when handed one.
	mustEval(t, f, "=HARMEAN(1,2,4)", "1.714285714285714")
	mustReject(t, f, "=HARMEAN(A1:A5)")
}

func TestThePermutationCounts(t *testing.T) {
	f := sheet(t)
	// Ordered selections without repetition, then with.
	mustEval(t, f, "=PERMUT(5,2)", "20")
	mustEval(t, f, "=PERMUTATIONA(5,2)", "25")
	// Choosing more than the population holds is out of domain.
	mustReject(t, f, "=PERMUT(2,5)")
}

func TestTheNormalDistributionFamily(t *testing.T) {
	f := sheet(t)
	mustEval(t, f, "=NORMDIST(1,0,1,TRUE)", "0.841344746068543")
	// The standard form fixes the mean at zero and the deviation at one, so it
	// agrees with the general form given those parameters.
	mustEval(t, f, "=NORMSDIST(1)", "0.841344746068543")
	// The inverses undo the cumulative form at the median.
	mustEval(t, f, "=NORMINV(0.5,0,1)", "0")
	mustEval(t, f, "=NORMSINV(0.5)", "0")
}

func TestPOISSONAndTheFisherTransforms(t *testing.T) {
	f := sheet(t)
	mustEval(t, f, "=POISSON(2,3,TRUE)", "0.423190081126844")
	mustEval(t, f, "=FISHER(0.5)", "0.549306144334055")
	mustEval(t, f, "=FISHERINV(0.5)", "0.46211715726001")
	// GAMMA extends the factorial: GAMMA(4) is 3!.
	mustEval(t, f, "=GAMMA(4)", "6")
	mustEval(t, f, "=GAMMALN(4)", "1.791759469228055")
}
