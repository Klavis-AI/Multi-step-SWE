// Gate group: formula-engine-regression.
//
// Inherited formula evaluation and reference behaviour that the criteria,
// lookup and comparison work of this milestone must not disturb: operator
// precedence, cell references, range aggregation and the formula round trip,
// all first established at milestone 06. The one contract that does move --
// how a numeric result is rendered -- is scored in
// inherited-contract-migrations, not asserted here.
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

func TestStringFunctionsFromStep06StillWork(t *testing.T) {
	// LOWER and UPPER were added this step but SUM, PRODUCT were inherited.
	mustCalc(t, "=SUM(1,2,3)", "6")
	mustCalc(t, "=ABS(-5)", "5")
}
