// Scored group: formula-references.
//
// Operands that are cell references rather than literals: a single reference, a
// rectangular range consumed by a function, a sheet-qualified reference, the
// treatment of blank and textual cells inside a range, and what happens when a
// reference cannot be turned into a number. Behaviour transcribed from the
// upstream checkpoint this milestone tracks, qax-os/excelize tag v2.2.0
// (calc_test.go), restricted to the public interface. Reference syntax follows
// OpenDocument v1.3 part 4 (OpenFormula), section 5.8 "References"; the A1
// reference style itself is ECMA-376 Part 1 section 18.17.2.3.1.
package formulareferences

import (
	"bytes"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// grid builds a workbook with a small block of data:
//
//	     A     B      C
//	1    1    10   text
//	2    2    20
//	3    3    30
//
// and D1 holding an unevaluated formula.
func grid(t *testing.T) *seedcalc.File {
	t.Helper()
	f := seedcalc.NewFile()
	for axis, v := range map[string]int{
		"A1": 1, "A2": 2, "A3": 3,
		"B1": 10, "B2": 20, "B3": 30,
	} {
		if err := f.SetCellValue("Sheet1", axis, v); err != nil {
			t.Fatalf("SetCellValue(%s): %v", axis, err)
		}
	}
	if err := f.SetCellValue("Sheet1", "C1", "text"); err != nil {
		t.Fatalf("SetCellValue(C1): %v", err)
	}
	if err := f.SetCellFormula("Sheet1", "D1", "=A1+A2"); err != nil {
		t.Fatalf("SetCellFormula(D1): %v", err)
	}
	return f
}

// evalIn evaluates formula in cell H1 of f.
func evalIn(t *testing.T, f *seedcalc.File, formula string) (string, error) {
	t.Helper()
	if err := f.SetCellFormula("Sheet1", "H1", formula); err != nil {
		t.Fatalf("SetCellFormula(%q): %v", formula, err)
	}
	return f.CalcCellValue("Sheet1", "H1")
}

func mustEval(t *testing.T, f *seedcalc.File, formula, want string) {
	t.Helper()
	got, err := evalIn(t, f, formula)
	if err != nil {
		t.Fatalf("CalcCellValue(%q): unexpected error %v", formula, err)
	}
	if got != want {
		t.Fatalf("CalcCellValue(%q) = %q, want %q", formula, got, want)
	}
}

func mustReject(t *testing.T, f *seedcalc.File, formula string) error {
	t.Helper()
	got, err := evalIn(t, f, formula)
	if err == nil {
		t.Fatalf("CalcCellValue(%q) = %q, want an error", formula, got)
	}
	if got != "" {
		t.Fatalf("CalcCellValue(%q) returned result %q alongside an error", formula, got)
	}
	return err
}

// mustRejectWith is mustReject for a failure that is one of the seven
// spreadsheet error conditions. TEST_INTERFACE 4.06.1 makes the error's
// message *be* that error string in those cases -- it is contract, not a
// diagnostic -- so the value is asserted; the wording of every other refusal
// is left unchecked by mustReject.
func mustRejectWith(t *testing.T, f *seedcalc.File, formula, wantErr string) {
	t.Helper()
	err := mustReject(t, f, formula)
	if err.Error() != wantErr {
		t.Fatalf("CalcCellValue(%q): err.Error() = %q, want the spreadsheet error value %q", formula, err.Error(), wantErr)
	}
}

func TestASingleReferenceEvaluatesToTheReferencedValue(t *testing.T) {
	f := grid(t)
	mustEval(t, f, "=A1", "1")
	mustEval(t, f, "=A1+A2", "3")
	mustEval(t, f, "=B3-A3", "27")
	mustEval(t, f, "=A2*B1", "20")
}

func TestARangeReferenceIsAggregatedByAFunction(t *testing.T) {
	f := grid(t)
	mustEval(t, f, "=SUM(A1:A3)", "6")
	mustEval(t, f, "=SUM(A1:A1)", "1")
	mustEval(t, f, "=SUM(A1:B3)", "66")
	// The rectangle actually read is seeded at A1 and only widened to cover
	// the reference, so a range that does not start at A1 still reads from
	// there: B1:B3 is resolved as A1:B3 (66, not 60) and A2:A3 as A1:A3.
	// A quirk of this checkpoint, pinned so an implementation reproduces it.
	mustEval(t, f, "=SUM(B1:B3)", "66")
	mustEval(t, f, "=SUM(A2:A3)", "6")
	mustEval(t, f, "=PRODUCT(A1:A3)", "6")
}

func TestReferencesAndLiteralsMixInOneArgumentList(t *testing.T) {
	f := grid(t)
	mustEval(t, f, "=SUM(A1:A3,B1)", "16")
	mustEval(t, f, "=SUM(A1,B1,100)", "111")
	mustEval(t, f, "=SUM(A1:A3)*2", "12")
}

func TestASheetQualifiedReferenceNamesItsWorksheet(t *testing.T) {
	f := grid(t)
	mustEval(t, f, "=SUM(Sheet1!A1:A3)", "6")
	mustEval(t, f, "=Sheet1!B1", "10")
}

func TestBlankCellsInARangeContributeNothing(t *testing.T) {
	f := grid(t)
	// A4:A10 are blank, so widening the range does not change the sum.
	mustEval(t, f, "=SUM(A1:A10)", "6")
	// X1:X9 is resolved as A1:X9 by the same A1 clamp, so it reads over the
	// data block: C1 holds text, which is not a number in an aggregation, and
	// the sum fails with #VALUE!. A single blank cell reference is zero.
	mustRejectWith(t, f, "=SUM(X1:X9)", "#VALUE!")
	mustEval(t, f, "=SUM(X1)", "0")
}

func TestANonNumericOperandIsRejected(t *testing.T) {
	f := grid(t)
	// C1 holds text, which is not a number in any arithmetic position.
	mustReject(t, f, "=A1+C1")
	mustReject(t, f, "=SUM(C1)")
}

func TestABareRangeIsNotAValue(t *testing.T) {
	f := grid(t)
	// A range is only meaningful as a function argument, never as the result
	// of the expression itself. The refusal is the #VALUE! condition, so the
	// error carries that value as its message.
	mustRejectWith(t, f, "=A1:A3", "#VALUE!")
	mustRejectWith(t, f, "=A1:A3+1", "#VALUE!")
}

func TestTheIntersectionOperatorIsNotImplemented(t *testing.T) {
	f := grid(t)
	// The range operator `:` works inside a function argument; the
	// intersection operator -- a space between two references -- does not.
	// The refusal is the #VALUE! condition, so the error carries that value
	// as its message.
	mustRejectWith(t, f, "=SUM(A1:A3 A2:A3)", "#VALUE!")
	mustRejectWith(t, f, "=SUM(A1:A2 A2:A3)", "#VALUE!")
}

func TestAReferenceReadsTheCachedValueNotTheNeighbouringFormula(t *testing.T) {
	f := grid(t)
	// D1 holds "=A1+A2" but has never been computed, so it caches nothing and
	// cannot stand in for a number. Evaluation is single-cell: it does not
	// walk into the referenced cell's own formula.
	mustReject(t, f, "=D1+1")
	// Once a value is cached in that cell, the reference resolves.
	if err := f.SetCellValue("Sheet1", "D1", 3); err != nil {
		t.Fatalf("SetCellValue(D1): %v", err)
	}
	mustEval(t, f, "=D1+1", "4")
}

func TestReferencesStillResolveAfterAWriteReadRoundTrip(t *testing.T) {
	f := grid(t)
	if err := f.SetCellFormula("Sheet1", "H1", "=SUM(A1:B3)"); err != nil {
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
	got, err := g.CalcCellValue("Sheet1", "H1")
	if err != nil {
		t.Fatalf("CalcCellValue after round trip: %v", err)
	}
	if got != "66" {
		t.Fatalf("CalcCellValue after round trip = %q, want %q", got, "66")
	}
}
