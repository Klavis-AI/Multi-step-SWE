// Scored group: formula-evaluation.
//
// The formula tokenizer and the infix evaluator reached through
// File.CalcCellValue: which operators exist, how they bind, how a
// sub-expression in parentheses and a unary minus change that binding, and how
// a formula that cannot be tokenised or evaluated is reported. Behaviour
// transcribed from the upstream checkpoint this milestone tracks,
// qax-os/excelize tag v2.2.0 (calc_test.go), restricted to the public
// interface. Operator semantics follow OpenDocument v1.3 part 4 (OpenFormula),
// section 5.5 "Operators" and its precedence table; the evaluator implemented
// here carries only the additive and multiplicative subset of that table.
package formulaevaluation

import (
	"archive/zip"
	"bytes"
	"io"
	"strings"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// calc puts formula in a scratch cell of a fresh workbook and evaluates it.
func calc(t *testing.T, formula string) (string, error) {
	t.Helper()
	f := seedcalc.NewFile()
	if err := f.SetCellFormula("Sheet1", "H1", formula); err != nil {
		t.Fatalf("SetCellFormula(%q): %v", formula, err)
	}
	return f.CalcCellValue("Sheet1", "H1")
}

// mustCalc evaluates formula and fails unless it produced want.
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

// mustFail evaluates formula, fails unless it was rejected, and returns the
// error so a caller can go on to assert its message.
func mustFail(t *testing.T, formula string) error {
	t.Helper()
	got, err := calc(t, formula)
	if err == nil {
		t.Fatalf("CalcCellValue(%q) = %q, want an error", formula, got)
	}
	if got != "" {
		t.Fatalf("CalcCellValue(%q) returned result %q alongside an error; a rejected formula has no result", formula, got)
	}
	return err
}

// mustFailWith is mustFail for a failure that is one of the seven spreadsheet
// error conditions. TEST_INTERFACE 4.06.1 ("What a caller receives") makes the
// error's message *be* that error string in exactly those cases, so the string
// is contract and not a diagnostic: the descriptive wording of every other
// refusal is deliberately left unchecked by mustFail.
func mustFailWith(t *testing.T, formula, wantErr string) {
	t.Helper()
	err := mustFail(t, formula)
	if err.Error() != wantErr {
		t.Fatalf("CalcCellValue(%q): err.Error() = %q, want the spreadsheet error value %q", formula, err.Error(), wantErr)
	}
}

// partBytes returns the named part of the written package.
func partBytes(t *testing.T, f *seedcalc.File, name string) []byte {
	t.Helper()
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	zr, err := zip.NewReader(bytes.NewReader(buf.Bytes()), int64(buf.Len()))
	if err != nil {
		t.Fatalf("written package is not a ZIP container: %v", err)
	}
	for _, zf := range zr.File {
		if zf.Name != name {
			continue
		}
		rc, err := zf.Open()
		if err != nil {
			t.Fatalf("open %s: %v", name, err)
		}
		defer rc.Close()
		b, err := io.ReadAll(rc)
		if err != nil {
			t.Fatalf("read %s: %v", name, err)
		}
		return b
	}
	t.Fatalf("written package has no part %s", name)
	return nil
}

func TestCalcCellValueOnACellWithoutAFormulaIsEmpty(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetCellValue("Sheet1", "A1", 42); err != nil {
		t.Fatalf("SetCellValue: %v", err)
	}
	got, err := f.CalcCellValue("Sheet1", "A1")
	if err != nil {
		t.Fatalf("CalcCellValue on a literal cell: %v", err)
	}
	if got != "" {
		t.Fatalf("CalcCellValue on a literal cell = %q, want the empty string", got)
	}
	got, err = f.CalcCellValue("Sheet1", "Y9")
	if err != nil {
		t.Fatalf("CalcCellValue on an empty cell: %v", err)
	}
	if got != "" {
		t.Fatalf("CalcCellValue on an empty cell = %q, want the empty string", got)
	}
}

func TestCalcCellValueRejectsAnUnknownWorksheet(t *testing.T) {
	f := seedcalc.NewFile()
	got, err := f.CalcCellValue("NoSuchSheet", "A1")
	if err == nil {
		t.Fatalf("CalcCellValue on an absent sheet = %q, want an error", got)
	}
	if got != "" {
		t.Fatalf("CalcCellValue on an absent sheet returned result %q alongside an error", got)
	}
}

func TestCalcCellValueRejectsAMalformedCellReference(t *testing.T) {
	f := seedcalc.NewFile()
	for _, ref := range []string{"ZZZ", "1A", "", "A0"} {
		got, err := f.CalcCellValue("Sheet1", ref)
		if err == nil {
			t.Fatalf("CalcCellValue(Sheet1, %q) = %q, want an error", ref, got)
		}
		if got != "" {
			t.Fatalf("CalcCellValue(Sheet1, %q) returned result %q alongside an error", ref, got)
		}
	}
}

func TestArithmeticOperatorsFollowPrecedence(t *testing.T) {
	for _, tc := range []struct{ formula, want string }{
		{"=1+2", "3"},
		{"=9-4", "5"},
		{"=3*4", "12"},
		{"=10/4", "2.5"},
		{"=2*3+4", "10"},
		{"=4+2*3", "10"},
		{"=1-2-3", "-4"},
		{"=8/2/2", "2"},
		{"=100*0.1", "10"},
	} {
		mustCalc(t, tc.formula, tc.want)
	}
	// Only the additive and multiplicative subset of the OpenFormula 5.5
	// operator table is evaluated at this checkpoint. Exponentiation, percent
	// and the comparison operators tokenise but are not applied: the evaluator
	// yields the right-hand operand instead of a result. These are quirks of
	// the Oracle, pinned here so an implementation reproduces them.
	for _, tc := range []struct{ formula, want string }{
		{"=2^3", "3"},
		{"=50%", "50"},
		{"=1<>2", "2"},
	} {
		mustCalc(t, tc.formula, tc.want)
	}
	// String concatenation is not tokenised at all and is refused.
	mustFail(t, `="a"&"b"`)
}

func TestParenthesesAndUnaryMinusChangeGrouping(t *testing.T) {
	for _, tc := range []struct{ formula, want string }{
		{"=(1+2)*3", "9"},
		{"=((2+3)*(4-1))/5", "3"},
		{"=-3+5", "2"},
		{"=2*-3", "-6"},
		{"=-(2+3)", "-5"},
	} {
		mustCalc(t, tc.formula, tc.want)
	}
}

func TestDivisionByZeroIsReportedAsAnError(t *testing.T) {
	// Division by zero is one of the seven spreadsheet error conditions, so
	// the failure is ("", err) with err.Error() equal to the error value
	// itself -- not a result string, and not an arbitrary message.
	mustFailWith(t, "=1/0", "#DIV/0!")
	mustFailWith(t, "=(3+4)/(2-2)", "#DIV/0!")
}

func TestMalformedFormulaIsRejected(t *testing.T) {
	// "=(1+2" is accepted by the Oracle tokenizer; the trailing token is evaluated.
	// Only these patterns are truly rejected at this checkpoint.
	for _, formula := range []string{"=1+", "=*3", "=SUM(1,2"} {
		mustFail(t, formula)
	}
	// "=(1+2" tokenises without error and returns the result of (1+2).
	mustCalc(t, "=(1+2", "3")
}

func TestUnregisteredFunctionIsRejected(t *testing.T) {
	mustFail(t, "=NOSUCHFUNCTION(1)")
	// Function lookup is case sensitive: the registered name is upper case.
	mustFail(t, "=sum(1,2)")
}

func TestFormulaIsStoredWithoutACachedValue(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetCellFormula("Sheet1", "B2", "=1+2"); err != nil {
		t.Fatalf("SetCellFormula: %v", err)
	}
	formula, err := f.GetCellFormula("Sheet1", "B2")
	if err != nil {
		t.Fatalf("GetCellFormula: %v", err)
	}
	if formula != "=1+2" {
		t.Fatalf("GetCellFormula = %q, want %q", formula, "=1+2")
	}
	value, err := f.GetCellValue("Sheet1", "B2")
	if err != nil {
		t.Fatalf("GetCellValue: %v", err)
	}
	if value != "" {
		t.Fatalf("GetCellValue on a freshly written formula cell = %q, want the empty string: no result is cached until one is computed", value)
	}
	computed, err := f.CalcCellValue("Sheet1", "B2")
	if err != nil {
		t.Fatalf("CalcCellValue: %v", err)
	}
	if computed != "3" {
		t.Fatalf("CalcCellValue = %q, want %q", computed, "3")
	}
	// The formula, not the computed result, is what round-trips.
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	g, err := seedcalc.OpenReader(bytes.NewReader(buf.Bytes()))
	if err != nil {
		t.Fatalf("OpenReader: %v", err)
	}
	reread, err := g.GetCellFormula("Sheet1", "B2")
	if err != nil {
		t.Fatalf("GetCellFormula after round trip: %v", err)
	}
	if reread != "=1+2" {
		t.Fatalf("GetCellFormula after round trip = %q, want %q", reread, "=1+2")
	}
	recomputed, err := g.CalcCellValue("Sheet1", "B2")
	if err != nil {
		t.Fatalf("CalcCellValue after round trip: %v", err)
	}
	if recomputed != "3" {
		t.Fatalf("CalcCellValue after round trip = %q, want %q", recomputed, "3")
	}
}

func TestDroppingCachedValuesAsksTheHostToRecalculate(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetCellFormula("Sheet1", "A1", "=1+2"); err != nil {
		t.Fatalf("SetCellFormula: %v", err)
	}
	if err := f.UpdateLinkedValue(); err != nil {
		t.Fatalf("UpdateLinkedValue: %v", err)
	}
	wb := string(partBytes(t, f, "xl/workbook.xml"))
	if strings.Contains(wb, "<calcPr") {
		t.Fatal("workbook part still declares calcPr after cached values were dropped; the calculation properties must be removed so the host recalculates on load")
	}
	sheet := string(partBytes(t, f, "xl/worksheets/sheet1.xml"))
	if !strings.Contains(sheet, "1+2") {
		t.Fatal("worksheet part lost the formula when cached values were dropped")
	}
}
