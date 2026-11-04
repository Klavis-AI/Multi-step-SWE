// Scored group: math-function-library.
//
// The arithmetic, rounding and aggregation half of the function library that
// arrives with the evaluator, exercised through File.CalcCellValue. Behaviour
// transcribed from the upstream checkpoint this milestone tracks, qax-os/excelize
// tag v2.2.0 (calc_test.go), restricted to the public interface. Function
// semantics follow OpenDocument v1.3 part 4 (OpenFormula) section 6.16
// "Mathematical Functions" -- SUM 6.16.61, PRODUCT 6.16.47, SUMSQ 6.16.65,
// ABS 6.16.2, SIGN 6.16.54, EVEN 6.16.30, ODD 6.16.44, QUOTIENT 6.16.48,
// MOD 6.16.42, POWER 6.16.46, SQRT 6.16.58, EXP 6.16.31, LN 6.16.39,
// LOG 6.16.40, LOG10 6.16.41 -- and section 6.17 "Rounding Functions" --
// CEILING 6.17.1, INT 6.17.2, FLOOR 6.17.3, MROUND 6.17.4, ROUND 6.17.5,
// ROUNDDOWN 6.17.6, ROUNDUP 6.17.7, TRUNC 6.17.8.
package mathfunctionlibrary

import (
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// book is a workbook holding the 2x2 block A1:B2 = {{1,2},{3,4}}.
func book(t *testing.T) *seedcalc.File {
	t.Helper()
	f := seedcalc.NewFile()
	for axis, v := range map[string]int{"A1": 1, "B1": 2, "A2": 3, "B2": 4} {
		if err := f.SetCellValue("Sheet1", axis, v); err != nil {
			t.Fatalf("SetCellValue(%s): %v", axis, err)
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

// check asserts that every formula in cases evaluates to its paired result.
func check(t *testing.T, cases map[string]string) {
	t.Helper()
	f := book(t)
	for formula, want := range cases {
		got, err := eval(t, f, formula)
		if err != nil {
			t.Fatalf("CalcCellValue(%q): unexpected error %v", formula, err)
		}
		if got != want {
			t.Fatalf("CalcCellValue(%q) = %q, want %q", formula, got, want)
		}
	}
}

// reject asserts that every formula listed is refused.
func reject(t *testing.T, formulas ...string) {
	t.Helper()
	f := book(t)
	for _, formula := range formulas {
		got, err := eval(t, f, formula)
		if err == nil {
			t.Fatalf("CalcCellValue(%q) = %q, want an error", formula, got)
		}
		if got != "" {
			t.Fatalf("CalcCellValue(%q) returned result %q alongside an error", formula, got)
		}
	}
}

// rejectWith asserts that formula is refused and that the error's message is
// the named spreadsheet error value. TEST_INTERFACE 4.06.1 makes the message
// *be* that string for the seven error conditions, so it is contract; the
// descriptive wording of an ordinary refusal is not, and reject() leaves it
// alone.
func rejectWith(t *testing.T, formula, wantErr string) {
	t.Helper()
	f := book(t)
	got, err := eval(t, f, formula)
	if err == nil {
		t.Fatalf("CalcCellValue(%q) = %q, want an error", formula, got)
	}
	if got != "" {
		t.Fatalf("CalcCellValue(%q) returned result %q alongside an error", formula, got)
	}
	if err.Error() != wantErr {
		t.Fatalf("CalcCellValue(%q): err.Error() = %q, want the spreadsheet error value %q", formula, err.Error(), wantErr)
	}
}

func TestSumAddsItsArguments(t *testing.T) {
	check(t, map[string]string{
		"=SUM(1,2,3)":        "6",
		"=SUM()":             "0",
		"=SUM(1,2,SUM(3,4))": "10",
		"=SUM(A1:B2)":        "10",
		"=SUM(-1,1)":         "0",
	})
}

func TestProductAndSumOfSquares(t *testing.T) {
	check(t, map[string]string{
		"=PRODUCT(2,3,4)": "24",
		"=PRODUCT(A1:B2)": "24",
		"=SUMSQ(3,4)":     "25",
		"=SUMSQ(1,2,3,4)": "30",
	})
}

func TestAbsoluteValueSignAndParity(t *testing.T) {
	check(t, map[string]string{
		"=ABS(-7)":  "7",
		"=ABS(7)":   "7",
		"=SIGN(-3)": "-1",
		"=SIGN(0)":  "0",
		"=SIGN(3)":  "1",
		"=EVEN(3)":  "4",
		"=EVEN(-3)": "-4",
		"=ODD(2)":   "3",
		"=ODD(-2)":  "-3",
	})
}

func TestIntegerTruncationQuotientAndRemainder(t *testing.T) {
	check(t, map[string]string{
		"=INT(2.9)":        "2",
		"=INT(-2.5)":       "-3",
		"=TRUNC(8.9)":      "8",
		"=TRUNC(-8.1)":     "-8",
		"=TRUNC(1.9,0)":    "1",
		"=QUOTIENT(7,2)":   "3",
		"=QUOTIENT(-10,3)": "-3",
		"=MOD(7,3)":        "1",
		"=MOD(6,3)":        "0",
		"=MOD(-3,2)":       "1",
	})
}

func TestRoundingToADigitCount(t *testing.T) {
	check(t, map[string]string{
		"=ROUND(2.5,0)":        "3",
		"=ROUND(-2.5,0)":       "-3",
		"=ROUND(1.5,0)":        "2",
		"=ROUND(12345,-2)":     "12300",
		"=ROUNDUP(1.1,0)":      "2",
		"=ROUNDUP(-1.1,0)":     "-2",
		"=ROUNDUP(12345,-2)":   "12400",
		"=ROUNDDOWN(1.9,0)":    "1",
		"=ROUNDDOWN(-1.9,0)":   "-1",
		"=ROUNDDOWN(12345,-2)": "12300",
	})
}

func TestRoundingToAMultiple(t *testing.T) {
	check(t, map[string]string{
		"=CEILING(2.1,1)":   "3",
		"=CEILING(2.5,1)":   "3",
		"=CEILING(-2.5,-1)": "-3",
		"=FLOOR(2.9,1)":     "2",
		"=FLOOR(3.7,2)":     "2",
		"=FLOOR(-2.5,-1)":   "-2",
		"=MROUND(5,2)":      "6",
		"=MROUND(21,5)":     "20",
		"=MROUND(-10,-3)":   "-9",
	})
}

func TestTheCeilingAndFloorFamilyIsReachedByEverySpelling(t *testing.T) {
	// The tokenizer strips the "_xlfn" prefix and every "." from a function
	// name before lookup, so these three spellings name one function.
	check(t, map[string]string{
		"=CEILINGMATH(2.1)":        "3",
		"=CEILING.MATH(2.1)":       "3",
		"=_xlfn.CEILING.MATH(2.1)": "3",
		"=CEILING.PRECISE(2.1)":    "3",
		"=ISO.CEILING(2.1)":        "3",
		"=FLOOR.MATH(-2.1)":        "-3",
		"=FLOOR.PRECISE(-2.1)":     "-3",
	})
}

func TestPowersRootsAndLogarithms(t *testing.T) {
	check(t, map[string]string{
		"=POWER(2,10)": "1024",
		"=POWER(2,0)":  "1",
		"=SQRT(16)":    "4",
		"=EXP(0)":      "1",
		"=LN(1)":       "0",
		"=LOG10(100)":  "2",
		"=LOG(100)":    "2",
		"=LOG(8,2)":    "3",
	})
}

func TestFunctionsRejectAWrongArgumentCount(t *testing.T) {
	reject(t, "=ABS()", "=ABS(1,2)", "=INT()", "=ROUND(2.5)", "=SIGN()", "=QUOTIENT(1)")
}

func TestOutOfDomainArgumentsAreReported(t *testing.T) {
	// =SQRT(-1) is the worked #NUM! example of 4.06.1's error contract: the
	// failure is ("", err) and err.Error() is the error value itself.
	rejectWith(t, "=SQRT(-1)", "#NUM!")
	// The remaining three are refused; which error value each selects is not
	// fixed by any document, so only the refusal is graded.
	reject(t, "=FACT(-1)", "=MOD(1,0)", "=QUOTIENT(1,0)")
}
