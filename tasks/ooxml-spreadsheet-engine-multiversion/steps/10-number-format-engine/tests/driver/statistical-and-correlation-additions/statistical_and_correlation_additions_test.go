// Scored group: statistical-and-correlation-additions.
//
// The statistics that describe a pair of samples rather than one value's
// place in a distribution: correlation and covariance, the most frequent
// value, the three hypothesis tests, and the two pieces of the standard
// normal that GAUSS and PHI expose separately.
//
// Semantics follow OpenDocument v1.3 part 4 (OpenFormula) section 6.18.
package statisticalandcorrelationadditions

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

func TestCorrelationOfAPerfectlyLinearPair(t *testing.T) {
	f := sheet(t)
	// B is 2*A, so the correlation is exactly one.
	mustEval(t, f, `=CORREL(A1:A5,B1:B5)`, "1")
	mustEval(t, f, `=CORREL(A1:A5,A1:A5)`, "1")
	mustRejectWith(t, f, `=CORREL(A1:A5)`, "CORREL requires 2 arguments")
	// Unequal ranges have no pairs to correlate.
	mustRejectWith(t, f, `=CORREL(A1:A5,B1:B4)`, "#N/A")
}

func TestCovarianceIsThePopulationForm(t *testing.T) {
	f := sheet(t)
	// The population covariance of A with 2A is twice A's variance, 2*2 = 4.
	mustEval(t, f, `=COVAR(A1:A5,B1:B5)`, "4")
	mustEval(t, f, `=COVARIANCE.P(A1:A5,B1:B5)`, "4")
	mustEval(t, f, `=COVAR(A1:A5,A1:A5)`, "2")
	mustRejectWith(t, f, `=COVAR(A1:A5)`, "COVAR requires 2 arguments")
}

func TestTheModeFindsTheMostFrequentValue(t *testing.T) {
	f := sheet(t)
	mustEval(t, f, `=MODE(C1:C5)`, "3")
	mustEval(t, f, `=MODE.SNGL(C1:C5)`, "3")
	mustEval(t, f, `=MODE(1,2,2,3)`, "2")
	// With no value repeated there is no mode to report.
	mustRejectWith(t, f, `=MODE(A1:A5)`, "#N/A")
	mustRejectWith(t, f, `=MODE()`, "MODE requires at least 1 argument")
	mustRejectWith(t, f, `=MODE.SNGL()`, "MODE.SNGL requires at least 1 argument")
}

func TestTheChiSquaredTest(t *testing.T) {
	f := sheet(t)
	mustEval(t, f, `=CHITEST(A1:A5,B1:B5)`, "0.111709292816043")
	mustEval(t, f, `=CHISQ.TEST(A1:A5,B1:B5)`, "0.111709292816043")
	mustRejectWith(t, f, `=CHITEST(A1:A5)`, "CHITEST requires 2 arguments")
}

func TestTheFTest(t *testing.T) {
	f := sheet(t)
	mustEval(t, f, `=FTEST(A1:A5,B1:B5)`, "0.208")
	mustEval(t, f, `=F.TEST(A1:A5,B1:B5)`, "0.208")
	// Identical samples have identical variances, so the test is certain.
	mustEval(t, f, `=FTEST(A1:A5,A1:A5)`, "1")
	mustRejectWith(t, f, `=FTEST(A1:A5)`, "FTEST requires 2 arguments")
}

func TestTheStudentTTest(t *testing.T) {
	f := sheet(t)
	// The third argument is the tail count, the fourth the test type.
	mustEval(t, f, `=TTEST(A1:A5,B1:B5,2,1)`, "0.0132355995636827")
	mustEval(t, f, `=T.TEST(A1:A5,B1:B5,2,1)`, "0.0132355995636827")
	mustEval(t, f, `=TTEST(A1:A5,B1:B5,1,1)`, "0.00661779978184134")
	// Zero tails is not a choice.
	mustRejectWith(t, f, `=TTEST(A1:A5,B1:B5,0,1)`, "#NUM!")
	mustRejectWith(t, f, `=TTEST(A1:A5,B1:B5,2)`, "TTEST requires 4 arguments")
}

func TestGaussAndPhiSplitTheStandardNormal(t *testing.T) {
	f := sheet(t)
	// GAUSS is the probability between the mean and z, which is the
	// cumulative distribution less a half.
	mustEval(t, f, `=GAUSS(1)`, "0.341344746068543")
	mustEval(t, f, `=NORMSDIST(1)-0.5`, "0.341344746068543")
	mustEval(t, f, `=GAUSS(0)`, "0")
	// PHI is the density, not the distribution.
	mustEval(t, f, `=PHI(1)`, "0.241970724519143")
	mustEval(t, f, `=PHI(0)`, "0.398942280401433")
	mustRejectWith(t, f, `=GAUSS()`, "GAUSS requires 1 numeric argument")
	mustRejectWith(t, f, `=PHI()`, "PHI requires 1 argument")
}

func TestTheModeArrayFormReturnsItsFirstElement(t *testing.T) {
	f := sheet(t)
	// MODE.MULT is an array function; read as a single cell it yields the
	// empty string rather than the list.
	mustEval(t, f, `=MODE.MULT(C1:C5)`, "")
	mustRejectWith(t, f, `=MODE.MULT()`, "MODE.MULT requires at least 1 argument")
	// Its scalar sibling on the same range does report the value.
	mustEval(t, f, `=MODE.SNGL(C1:C5)`, "3")
}
