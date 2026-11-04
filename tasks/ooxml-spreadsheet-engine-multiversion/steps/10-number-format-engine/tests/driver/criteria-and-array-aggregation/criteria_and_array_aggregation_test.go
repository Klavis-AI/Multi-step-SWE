// Scored group: criteria-and-array-aggregation.
//
// The plural criteria aggregates, which test several ranges at once, and the
// array arithmetic functions, which pair two ranges element by element. Both
// families walk more than one range in step, so both have to agree on what a
// mismatched pair of ranges means.
//
// Semantics follow OpenDocument v1.3 part 4 (OpenFormula) sections 6.16.62
// (SUMIF family), 6.16.63 (SUMPRODUCT) and 6.16.65 through 6.16.68 (the sum
// of squares family).
package criteriaandarrayaggregation

import (
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// sheet builds the fixture every case in this group reads:
//
//	     A   B    C
//	1    1   2    1
//	2    2   4    3
//	3    3   6    2
//	4    4   8    3
//	5    5   10   3
//
// B is exactly twice A, which makes the two columns perfectly correlated;
// C repeats 3 three times, which gives the mode functions something to find.
func sheet(t *testing.T) *seedcalc.File {
	t.Helper()
	f := seedcalc.NewFile()
	for _, c := range []struct {
		axis string
		v    int
	}{
		{"A1", 1}, {"A2", 2}, {"A3", 3}, {"A4", 4}, {"A5", 5},
		{"B1", 2}, {"B2", 4}, {"B3", 6}, {"B4", 8}, {"B5", 10},
		{"C1", 1}, {"C2", 3}, {"C3", 2}, {"C4", 3}, {"C5", 3},
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

// mustRejectWith insists on the exact diagnostic, not merely on failure: a
// function that is absent altogether reports that it is unsupported, which
// is a different sentence from the one a present function uses to turn down
// a malformed call.
func mustRejectWith(t *testing.T, f *seedcalc.File, formula, wantErr string) {
	t.Helper()
	got, err := eval(t, f, formula)
	if err == nil {
		t.Fatalf("CalcCellValue(%q) = %q, want error %q", formula, got, wantErr)
	}
	if err.Error() != wantErr {
		t.Fatalf("CalcCellValue(%q) error = %q, want %q", formula, err.Error(), wantErr)
	}
}

func TestSUMIFSAggregatesUnderEveryCriterion(t *testing.T) {
	// The sum range comes first and each criterion is a range and a test.
	mustEval(t, sheet(t), `=SUMIFS(B1:B5,A1:A5,">2")`, "24")
	// Two criteria narrow the selection further; both must hold.
	mustEval(t, sheet(t), `=SUMIFS(B1:B5,A1:A5,">1",A1:A5,"<5")`, "18")
	mustEval(t, sheet(t), `=SUMIFS(B1:B5,A1:A5,">=3",B1:B5,"<10")`, "14")
	// A criterion that nothing satisfies sums to zero rather than erroring.
	mustEval(t, sheet(t), `=SUMIFS(B1:B5,A1:A5,">99")`, "0")
}

func TestSUMIFSMatchesTextCriteriaToo(t *testing.T) {
	f := sheet(t)
	for axis, v := range map[string]string{"D1": "x", "D2": "y", "D3": "x", "D4": "y", "D5": "x"} {
		if err := f.SetCellValue("Sheet1", axis, v); err != nil {
			t.Fatalf("SetCellValue(%s): %v", axis, err)
		}
	}
	mustEval(t, f, `=SUMIFS(B1:B5,D1:D5,"x")`, "18")
	mustEval(t, f, `=SUMIFS(B1:B5,D1:D5,"y")`, "12")
	mustEval(t, f, `=SUMIFS(B1:B5,D1:D5,"x",A1:A5,">1")`, "16")
}

func TestAVERAGEIFSDividesBySelectedCount(t *testing.T) {
	mustEval(t, sheet(t), `=AVERAGEIFS(B1:B5,A1:A5,">2")`, "8")
	mustEval(t, sheet(t), `=AVERAGEIFS(B1:B5,A1:A5,">1",A1:A5,"<5")`, "6")
	mustEval(t, sheet(t), `=AVERAGEIFS(A1:A5,A1:A5,"<>3")`, "3")
}

func TestSUMPRODUCTMultipliesRangesElementwise(t *testing.T) {
	// 1*2 + 2*4 + 3*6 + 4*8 + 5*10.
	mustEval(t, sheet(t), `=SUMPRODUCT(A1:A5,B1:B5)`, "110")
	// A single range is summed: there is nothing to multiply it by.
	mustEval(t, sheet(t), `=SUMPRODUCT(A1:A5)`, "15")
	mustEval(t, sheet(t), `=SUMPRODUCT(A1:A3,B1:B3)`, "28")
	mustEval(t, sheet(t), `=SUMPRODUCT(A1:A5,B1:B5,C1:C5)`, "308")
}

func TestTheSumOfSquaresFamily(t *testing.T) {
	// SUMX2MY2 sums x^2-y^2 over the pairs: 55 - 220.
	mustEval(t, sheet(t), `=SUMX2MY2(A1:A5,B1:B5)`, "-165")
	// SUMX2PY2 sums x^2+y^2: 55 + 220.
	mustEval(t, sheet(t), `=SUMX2PY2(A1:A5,B1:B5)`, "275")
	// SUMXMY2 sums (x-y)^2, which here is the square of each A value.
	mustEval(t, sheet(t), `=SUMXMY2(A1:A5,B1:B5)`, "55")
	mustEval(t, sheet(t), `=SUMXMY2(A1:A5,A1:A5)`, "0")
}

func TestSERIESSUMEvaluatesAPowerSeries(t *testing.T) {
	// SERIESSUM(x, n, m, coefficients): 1*2^0 + 2*2^1 + 3*2^2 + 4*2^3.
	mustEval(t, sheet(t), `=SERIESSUM(2,0,1,A1:A4)`, "49")
	mustEval(t, sheet(t), `=SERIESSUM(1,0,1,A1:A4)`, "10")
	// n shifts the first exponent: 1*2^1 + 2*2^2 + 3*2^3 + 4*2^4.
	mustEval(t, sheet(t), `=SERIESSUM(2,1,1,A1:A4)`, "98")
	// m is the step between exponents: 1*2^0 + 2*2^2 + 3*2^4 + 4*2^6.
	mustEval(t, sheet(t), `=SERIESSUM(2,0,2,A1:A4)`, "313")
}

func TestTheArityOfTheCriteriaAggregates(t *testing.T) {
	f := sheet(t)
	mustRejectWith(t, f, `=SUMIFS(B1:B5,A1:A5)`, "SUMIFS requires at least 3 arguments")
	mustRejectWith(t, f, `=AVERAGEIFS(B1:B5,A1:A5)`, "AVERAGEIFS requires at least 3 arguments")
	mustRejectWith(t, f, `=SUMPRODUCT()`, "SUMPRODUCT requires at least 1 argument")
	mustRejectWith(t, f, `=SERIESSUM(1,2,3)`, "SERIESSUM requires 4 arguments")
}

func TestTheArityOfTheSumOfSquaresFamily(t *testing.T) {
	f := sheet(t)
	mustRejectWith(t, f, `=SUMX2MY2(A1:A5)`, "SUMX2MY2 requires 2 arguments")
	mustRejectWith(t, f, `=SUMX2PY2(A1:A5)`, "SUMX2PY2 requires 2 arguments")
	mustRejectWith(t, f, `=SUMXMY2(A1:A5)`, "SUMXMY2 requires 2 arguments")
}

func TestMismatchedRangesAreRejectedDifferentlyByEachFamily(t *testing.T) {
	f := sheet(t)
	// The sum of squares family calls an unequal pair unavailable data.
	mustRejectWith(t, f, `=SUMX2MY2(A1:A5,B1:B4)`, "#N/A")
	mustRejectWith(t, f, `=SUMXMY2(A1:A5,B1:B4)`, "#N/A")
	// SUMPRODUCT calls it a bad value, and says the same of a range paired
	// with a scalar.
	mustRejectWith(t, f, `=SUMPRODUCT(A1:A5,B1:B4)`, "#VALUE!")
	mustRejectWith(t, f, `=SUMPRODUCT(A1:A5,"x")`, "#VALUE!")
}
