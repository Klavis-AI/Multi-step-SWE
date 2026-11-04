// Gate group: formula-engine-regression.
//
// Inherited formula evaluation and reference behaviour that must continue to
// work after the encryption, column-iterator and SheetFormatPr additions of
// this milestone. Covers operator precedence, cell references, range
// aggregation, and the formula round-trip. A subset of step-06 behaviour;
// any regression here indicates a build ordering or API breakage.
package formulaengineregression

import (
	"bytes"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

func calc(t *testing.T, formula string) (string, error) {
	t.Helper()
	f := seedcalc.NewFile()
	if err := f.SetCellFormula("Sheet1", "Z1", formula); err != nil {
		t.Fatalf("SetCellFormula(%q): %v", formula, err)
	}
	return f.CalcCellValue("Sheet1", "Z1")
}

func mustCalc(t *testing.T, formula, want string) {
	t.Helper()
	got, err := calc(t, formula)
	if err != nil {
		t.Fatalf("CalcCellValue(%q): unexpected error %v", formula, err)
	}
	if got != want {
		t.Fatalf("CalcCellValue(%q) = %q, want %q", formula, got, want)
	}
}

func TestArithmeticOperatorsStillWork(t *testing.T) {
	mustCalc(t, "=1+2", "3")
	mustCalc(t, "=10/4", "2.5")
	mustCalc(t, "=2*3+4", "10")
	mustCalc(t, "=(1+2)*3", "9")
	mustCalc(t, "=-3+5", "2")
}

func TestDivisionByZeroStillErrors(t *testing.T) {
	_, err := calc(t, "=1/0")
	if err == nil {
		t.Fatal("1/0 returned no error; expected #DIV/0!")
	}
}

func TestFormulaReferenceStillResolvesValue(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetCellValue("Sheet1", "A1", 7); err != nil {
		t.Fatalf("SetCellValue: %v", err)
	}
	if err := f.SetCellFormula("Sheet1", "B1", "=A1*2"); err != nil {
		t.Fatalf("SetCellFormula: %v", err)
	}
	got, err := f.CalcCellValue("Sheet1", "B1")
	if err != nil {
		t.Fatalf("CalcCellValue: %v", err)
	}
	if got != "14" {
		t.Fatalf("CalcCellValue = %q, want %q", got, "14")
	}
}

func TestRangeAggregationFunctionsStillWork(t *testing.T) {
	f := seedcalc.NewFile()
	for i, v := range []int{1, 2, 3, 4, 5} {
		cell, _ := seedcalc.CoordinatesToCellName(1, i+1)
		if err := f.SetCellValue("Sheet1", cell, v); err != nil {
			t.Fatalf("SetCellValue: %v", err)
		}
	}
	if err := f.SetCellFormula("Sheet1", "B1", "=SUM(A1:A5)"); err != nil {
		t.Fatalf("SetCellFormula SUM: %v", err)
	}
	got, err := f.CalcCellValue("Sheet1", "B1")
	if err != nil {
		t.Fatalf("CalcCellValue SUM: %v", err)
	}
	if got != "15" {
		t.Fatalf("SUM(A1:A5) = %q, want %q", got, "15")
	}
}

func TestFormulaRoundTripStillWorks(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetCellFormula("Sheet1", "C2", "=3*3"); err != nil {
		t.Fatalf("SetCellFormula: %v", err)
	}
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	g, err := seedcalc.OpenReader(bytes.NewReader(buf.Bytes()))
	if err != nil {
		t.Fatalf("OpenReader: %v", err)
	}
	got, err := g.CalcCellValue("Sheet1", "C2")
	if err != nil {
		t.Fatalf("CalcCellValue after round trip: %v", err)
	}
	if got != "9" {
		t.Fatalf("CalcCellValue after round trip = %q, want %q", got, "9")
	}
}

func TestCalcCellValueOnAbsentSheetErrors(t *testing.T) {
	f := seedcalc.NewFile()
	got, err := f.CalcCellValue("NoSuch", "A1")
	if err == nil {
		t.Fatalf("CalcCellValue on absent sheet = %q, want an error", got)
	}
}

func TestTheInheritedFunctionLibraryStillEvaluates(t *testing.T) {
	// Milestone 06 left a numeric function library behind -- there were no
	// text functions before this one -- and adding TRIM, SUMIF and the
	// IS-family must not disturb any of it. A broad sample rather than two
	// names, so that a rewritten dispatcher is actually charged for what it
	// drops. The volatile functions (RAND, RANDBETWEEN, NOW, TODAY) are
	// excluded because they have no fixed value to assert.
	for _, c := range []struct{ formula, want string }{
		// Aggregation and the basic numeric operators.
		{"=SUM(1,2,3)", "6"},
		{"=SUMSQ(3,4)", "25"},
		{"=PRODUCT(2,3,4)", "24"},
		{"=ABS(-5)", "5"},
		{"=SIGN(-3)", "-1"},
		{"=POWER(2,10)", "1024"},
		{"=SQRT(16)", "4"},
		{"=EXP(0)", "1"},
		{"=LN(1)", "0"},
		{"=LOG10(1000)", "3"},
		// Integer arithmetic.
		{"=QUOTIENT(17,5)", "3"},
		{"=MOD(17,5)", "2"},
		{"=GCD(12,18)", "6"},
		{"=LCM(4,6)", "12"},
		{"=FACT(5)", "120"},
		{"=COMBIN(5,2)", "10"},
		{"=MULTINOMIAL(2,1)", "3"},
		// Rounding, each with its own direction.
		{"=ROUND(2.567,1)", "2.6"},
		{"=ROUNDUP(2.51,0)", "3"},
		{"=ROUNDDOWN(2.99,0)", "2"},
		{"=CEILING(2.1,1)", "3"},
		{"=FLOOR(2.9,1)", "2"},
		{"=ISO.CEILING(2.1)", "3"},
		{"=INT(7.8)", "7"},
		{"=TRUNC(7.8)", "7"},
		{"=EVEN(3)", "4"},
		{"=ODD(2)", "3"},
		// Trigonometry and the constant it is measured against.
		{"=PI()", "3.141592653589793"},
		{"=SIN(0)", "0"},
		{"=COS(0)", "1"},
		{"=SEC(0)", "1"},
		{"=DEGREES(3.141592653589793)", "180"},
		// Base conversion, in both directions.
		{"=BASE(255,16)", "FF"},
		{"=DECIMAL(\"FF\",16)", "255"},
		{"=ROMAN(2020)", "MMXX"},
		{"=ARABIC(\"MMXX\")", "2020"},
	} {
		mustCalc(t, c.formula, c.want)
	}
}
