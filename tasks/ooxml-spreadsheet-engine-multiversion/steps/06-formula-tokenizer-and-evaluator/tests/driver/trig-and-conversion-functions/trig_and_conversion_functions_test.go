// Scored group: trig-and-conversion-functions.
//
// The transcendental, combinatorial, matrix and radix-conversion half of the
// function library that arrives with the evaluator, exercised through
// File.CalcCellValue. Behaviour transcribed from the upstream checkpoint this
// milestone tracks, qax-os/excelize tag v2.2.0 (calc_test.go), restricted to
// the public interface. Function semantics follow OpenDocument v1.3 part 4
// (OpenFormula) section 6.16 "Mathematical Functions" -- PI 6.16.45,
// DEGREES 6.16.25, RADIANS 6.16.49, SIN 6.16.55, COS 6.16.19, TAN 6.16.69,
// COT 6.16.21, CSC 6.16.23, SEC 6.16.52, ASIN 6.16.7, ACOS 6.16.3,
// ATAN 6.16.9, ATAN2 6.16.10, ACOT 6.16.5, SINH 6.16.56, COSH 6.16.20,
// TANH 6.16.70, COTH 6.16.22, CSCH 6.16.24, SECH 6.16.57, ASINH 6.16.8,
// ACOSH 6.16.4, ATANH 6.16.11, ACOTH 6.16.6, FACT 6.16.32,
// FACTDOUBLE 6.16.33, COMBIN 6.16.16, COMBINA 6.16.17, MULTINOMIAL 6.16.43,
// GCD 6.16.36, LCM 6.16.38, SQRTPI 6.16.59, RAND 6.16.50,
// RANDBETWEEN 6.16.51 -- section 6.19 "Number Representation Conversion
// Functions" -- ARABIC 6.19.2, BASE 6.19.3, DECIMAL 6.19.10, ROMAN 6.19.17 --
// and section 6.5 "Matrix Functions" -- MDETERM 6.5.2.
package trigandconversionfunctions

import (
	"strconv"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

func newBook(t *testing.T) *seedcalc.File {
	t.Helper()
	return seedcalc.NewFile()
}

func eval(t *testing.T, f *seedcalc.File, formula string) (string, error) {
	t.Helper()
	if err := f.SetCellFormula("Sheet1", "H1", formula); err != nil {
		t.Fatalf("SetCellFormula(%q): %v", formula, err)
	}
	return f.CalcCellValue("Sheet1", "H1")
}

// check asserts that every formula evaluates to its paired result exactly.
func check(t *testing.T, cases map[string]string) {
	t.Helper()
	f := newBook(t)
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

// checkNear asserts that every formula evaluates to a number within 1e-9 of its
// paired result. Used where the exact decimal rendering of a transcendental
// result is not something an implementation should be pinned to.
func checkNear(t *testing.T, cases map[string]float64) {
	t.Helper()
	f := newBook(t)
	for formula, want := range cases {
		got, err := eval(t, f, formula)
		if err != nil {
			t.Fatalf("CalcCellValue(%q): unexpected error %v", formula, err)
		}
		n, convErr := strconv.ParseFloat(got, 64)
		if convErr != nil {
			t.Fatalf("CalcCellValue(%q) = %q, which is not a number", formula, got)
		}
		if diff := n - want; diff > 1e-9 || diff < -1e-9 {
			t.Fatalf("CalcCellValue(%q) = %v, want %v", formula, n, want)
		}
	}
}

func TestPiAndAngleConversion(t *testing.T) {
	checkNear(t, map[string]float64{
		"=PI()":          3.141592653589793,
		"=DEGREES(PI())": 180,
		"=RADIANS(180)":  3.141592653589793,
		// RADIANS(0) returns 0 with no error.
		"=RADIANS(0)": 0,
	})
	// The Oracle's DEGREES implementation raises #DIV/0! when the argument is
	// zero (a known quirk of this checkpoint: the guard is val==0 instead of
	// only guarding a division). TEST_INTERFACE 4.06.1 names both halves --
	// the failure and the error value it carries -- so both are graded: a
	// division-by-zero condition is ("", err) with err.Error() = "#DIV/0!".
	f := newBook(t)
	got, err := eval(t, f, "=DEGREES(0)")
	if err == nil {
		t.Fatalf("CalcCellValue(=DEGREES(0)) = %q, expected an error from this Oracle checkpoint", got)
	}
	if got != "" {
		t.Fatalf("CalcCellValue(=DEGREES(0)) returned result %q alongside an error", got)
	}
	if err.Error() != "#DIV/0!" {
		t.Fatalf("CalcCellValue(=DEGREES(0)): err.Error() = %q, want the spreadsheet error value %q", err.Error(), "#DIV/0!")
	}
}

func TestCircularFunctions(t *testing.T) {
	checkNear(t, map[string]float64{
		"=SIN(0)":      0,
		"=COS(0)":      1,
		"=TAN(0)":      0,
		"=SIN(PI()/2)": 1,
		"=COT(PI()/4)": 1,
		"=CSC(PI()/2)": 1,
		"=SEC(0)":      1,
		"=TAN(PI()/4)": 1,
	})
}

func TestInverseCircularFunctions(t *testing.T) {
	checkNear(t, map[string]float64{
		"=ASIN(0)":    0,
		"=ASIN(1)":    1.5707963267948966,
		"=ACOS(1)":    0,
		"=ATAN(0)":    0,
		"=ATAN(1)":    0.7853981633974483,
		"=ATAN2(1,1)": 0.7853981633974483,
		"=ACOT(1)":    0.7853981633974483,
	})
}

func TestHyperbolicFunctions(t *testing.T) {
	checkNear(t, map[string]float64{
		"=SINH(0)": 0,
		"=COSH(0)": 1,
		"=TANH(0)": 0,
		"=SECH(0)": 1,
		"=COTH(1)": 0.7615941559557649,
		"=CSCH(1)": 0.8509181282393216,
		"=TANH(1)": 0.7615941559557649,
	})
}

func TestInverseHyperbolicFunctions(t *testing.T) {
	checkNear(t, map[string]float64{
		"=ASINH(0)": 0,
		"=ACOSH(1)": 0,
		"=ATANH(0)": 0,
		"=ACOTH(2)": 0.5493061443340548,
		"=ASINH(1)": 0.8813735870195429,
	})
}

func TestCombinatorialFunctions(t *testing.T) {
	check(t, map[string]string{
		"=FACT(5)":            "120",
		"=FACT(0)":            "1",
		"=FACTDOUBLE(5)":      "15",
		"=FACTDOUBLE(6)":      "48",
		"=COMBIN(5,2)":        "10",
		"=COMBIN(0,0)":        "1",
		"=COMBINA(4,3)":       "20",
		"=MULTINOMIAL(2,3)":   "10",
		"=MULTINOMIAL(3,1,2)": "60",
		"=GCD(12,18)":         "6",
		"=GCD(0,6)":           "6",
		"=LCM(4,6)":           "12",
		"=LCM(1,8,12)":        "24",
	})
	checkNear(t, map[string]float64{"=SQRTPI(1)": 1.7724538509055159})
}

func TestRadixAndRomanConversion(t *testing.T) {
	check(t, map[string]string{
		`=BASE(255,16)`:      "FF",
		`=BASE(7,2)`:         "111",
		`=BASE(7,2,8)`:       "00000111",
		`=DECIMAL("FF",16)`:  "255",
		`=DECIMAL("1010",2)`: "10",
		`=ROMAN(4)`:          "IV",
		`=ROMAN(1999)`:       "MCMXCIX",
		`=ARABIC("IV")`:      "4",
		`=ARABIC("MCMXCIX")`: "1999",
	})
	// ARABIC reports a malformed numeral through its *result* and not through
	// err: the "#VALUE!" string comes back as a successful value at this
	// milestone (TEST_INTERFACE 4.06.1, "What a caller receives").
	f := newBook(t)
	got, err := eval(t, f, `=ARABIC("VV")`)
	if err != nil {
		t.Fatalf(`CalcCellValue(=ARABIC("VV")): unexpected error %v`, err)
	}
	if got != "#VALUE!" {
		t.Fatalf(`CalcCellValue(=ARABIC("VV")) = %q, want %q returned as a successful result`, got, "#VALUE!")
	}
}

func TestMatrixDeterminant(t *testing.T) {
	f := seedcalc.NewFile()
	// A1:B2 = {{1,2},{3,4}} has determinant 1*4 - 2*3 = -2.
	for axis, v := range map[string]int{"A1": 1, "B1": 2, "A2": 3, "B2": 4} {
		if err := f.SetCellValue("Sheet1", axis, v); err != nil {
			t.Fatalf("SetCellValue(%s): %v", axis, err)
		}
	}
	got, err := eval(t, f, "=MDETERM(A1:B2)")
	if err != nil {
		t.Fatalf("CalcCellValue(=MDETERM(A1:B2)): %v", err)
	}
	if got != "-2" {
		t.Fatalf("CalcCellValue(=MDETERM(A1:B2)) = %q, want %q", got, "-2")
	}
}

func TestRandomFunctionsStayWithinTheirRange(t *testing.T) {
	f := newBook(t)
	got, err := eval(t, f, "=RAND()")
	if err != nil {
		t.Fatalf("CalcCellValue(=RAND()): %v", err)
	}
	n, convErr := strconv.ParseFloat(got, 64)
	if convErr != nil {
		t.Fatalf("CalcCellValue(=RAND()) = %q, which is not a number", got)
	}
	if n < 0 || n >= 1 {
		t.Fatalf("CalcCellValue(=RAND()) = %v, want a value in [0,1)", n)
	}
	// A degenerate range pins the result, which keeps the assertion determinate.
	check(t, map[string]string{
		"=RANDBETWEEN(5,5)": "5",
		"=RANDBETWEEN(0,0)": "0",
	})
}
