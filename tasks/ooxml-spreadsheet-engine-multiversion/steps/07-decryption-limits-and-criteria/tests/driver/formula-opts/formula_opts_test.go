// Gate group: formula-opts.
//
// The FormulaOpts variadic parameter on SetCellFormula and the
// STCellFormulaType constants, both of which milestone 06 shipped and this
// milestone leaves alone. Callers may pass FormulaOpts{Type:} to mark a
// formula as array, shared, dataTable or normal; the type and optional Ref
// are written to the xlsxF element.
//
// This is a gate group, not a scored one: every case passes unchanged
// against the milestone 06 checkpoint, so there is nothing here for this
// milestone to earn. ECMA-376 Part 1 clause 18.3.1.40 (f element and its @t
// attribute).
package formulaopts

import (
	"bytes"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// strPtr returns a pointer to s.
func strPtr(s string) *string { return &s }

func TestSetCellFormulaWithNoOptsBehavesAsNormal(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetCellFormula("Sheet1", "A1", "=1+2"); err != nil {
		t.Fatalf("SetCellFormula no opts: %v", err)
	}
	got, err := f.GetCellFormula("Sheet1", "A1")
	if err != nil {
		t.Fatalf("GetCellFormula: %v", err)
	}
	if got != "=1+2" {
		t.Fatalf("GetCellFormula = %q, want %q", got, "=1+2")
	}
}

func TestSTCellFormulaTypeConstantsHaveCorrectValues(t *testing.T) {
	if seedcalc.STCellFormulaTypeArray != "array" {
		t.Fatalf("STCellFormulaTypeArray = %q, want %q", seedcalc.STCellFormulaTypeArray, "array")
	}
	if seedcalc.STCellFormulaTypeDataTable != "dataTable" {
		t.Fatalf("STCellFormulaTypeDataTable = %q, want %q", seedcalc.STCellFormulaTypeDataTable, "dataTable")
	}
	if seedcalc.STCellFormulaTypeNormal != "normal" {
		t.Fatalf("STCellFormulaTypeNormal = %q, want %q", seedcalc.STCellFormulaTypeNormal, "normal")
	}
	if seedcalc.STCellFormulaTypeShared != "shared" {
		t.Fatalf("STCellFormulaTypeShared = %q, want %q", seedcalc.STCellFormulaTypeShared, "shared")
	}
}

func TestFormulaOptsArrayTypeSurvivesRoundTrip(t *testing.T) {
	f := seedcalc.NewFile()
	typ := seedcalc.STCellFormulaTypeArray
	if err := f.SetCellFormula("Sheet1", "A1", "=SUM(B1:B3)",
		seedcalc.FormulaOpts{Type: &typ, Ref: strPtr("A1:A3")}); err != nil {
		t.Fatalf("SetCellFormula with FormulaOpts: %v", err)
	}
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	g, err := seedcalc.OpenReader(bytes.NewReader(buf.Bytes()))
	if err != nil {
		t.Fatalf("OpenReader: %v", err)
	}
	got, err := g.GetCellFormula("Sheet1", "A1")
	if err != nil {
		t.Fatalf("GetCellFormula after round trip: %v", err)
	}
	if got == "" {
		t.Fatal("GetCellFormula after round trip is empty; formula was not preserved")
	}
}

func TestFormulaOptsSharedTypeIsAccepted(t *testing.T) {
	f := seedcalc.NewFile()
	typ := seedcalc.STCellFormulaTypeShared
	ref := "A1:A5"
	if err := f.SetCellFormula("Sheet1", "A1", "=ROW()",
		seedcalc.FormulaOpts{Type: &typ, Ref: &ref}); err != nil {
		t.Fatalf("SetCellFormula shared type: %v", err)
	}
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write after shared formula: %v", err)
	}
	if buf.Len() == 0 {
		t.Fatal("written file is empty")
	}
}

func TestFormulaOptsNormalTypeIsAccepted(t *testing.T) {
	f := seedcalc.NewFile()
	typ := seedcalc.STCellFormulaTypeNormal
	if err := f.SetCellFormula("Sheet1", "C3", "=2+3",
		seedcalc.FormulaOpts{Type: &typ}); err != nil {
		t.Fatalf("SetCellFormula normal type: %v", err)
	}
	result, err := f.CalcCellValue("Sheet1", "C3")
	if err != nil {
		t.Fatalf("CalcCellValue after normal type opt: %v", err)
	}
	if result != "5" {
		t.Fatalf("CalcCellValue = %q, want %q", result, "5")
	}
}

func TestSettingEmptyFormulaRemovesIt(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetCellFormula("Sheet1", "A1", "=1+2"); err != nil {
		t.Fatalf("SetCellFormula: %v", err)
	}
	// An empty string removes the formula cell.
	if err := f.SetCellFormula("Sheet1", "A1", ""); err != nil {
		t.Fatalf("SetCellFormula empty: %v", err)
	}
	got, err := f.GetCellFormula("Sheet1", "A1")
	if err != nil {
		t.Fatalf("GetCellFormula after clear: %v", err)
	}
	if got != "" {
		t.Fatalf("GetCellFormula after clear = %q, want empty", got)
	}
}
