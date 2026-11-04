// Scored group: formula-attributes (milestone 04, "Pivot caches, sparklines and
// the macro part").
//
// Normative sources:
//   - TEST_INTERFACE.md section 4.04 -- "Formula type and range attributes".
//   - ECMA-376 Part 1, 18.3.1.40 f (Formula) -- the formula expression is the
//     character content of the element, the t attribute gives the formula
//     type and ref gives the range of cells the shared or array formula
//     applies to.
//   - ECMA-376 Part 1, 18.18.6 ST_CellFormulaType (Formula Type) -- the
//     enumeration is array, dataTable, normal and shared.
//   - Cases cross-checked against the upstream test corpus at tag v2.0.2
//     (cell_test.go, excelize_test.go).
package formulaattributes

import (
	"archive/zip"
	"bytes"
	"encoding/xml"
	"io/ioutil"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

func savedParts(t *testing.T, f *seedcalc.File) map[string]string {
	t.Helper()
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	zr, err := zip.NewReader(bytes.NewReader(buf.Bytes()), int64(buf.Len()))
	if err != nil {
		t.Fatalf("the saved workbook is not a readable package: %v", err)
	}
	out := make(map[string]string, len(zr.File))
	for _, zf := range zr.File {
		rc, err := zf.Open()
		if err != nil {
			t.Fatalf("open %s: %v", zf.Name, err)
		}
		b, err := ioutil.ReadAll(rc)
		rc.Close()
		if err != nil {
			t.Fatalf("read %s: %v", zf.Name, err)
		}
		out[zf.Name] = string(b)
	}
	return out
}

type formula struct {
	T       string `xml:"t,attr"`
	Ref     string `xml:"ref,attr"`
	Content string `xml:",chardata"`
}

type cell struct {
	R string   `xml:"r,attr"`
	F *formula `xml:"f"`
}

type worksheetDoc struct {
	SheetData struct {
		Row []struct {
			C []cell `xml:"c"`
		} `xml:"row"`
	} `xml:"sheetData"`
}

// storedFormula returns the f element written for the given cell, or nil.
func storedFormula(t *testing.T, f *seedcalc.File, axis string) *formula {
	t.Helper()
	raw, ok := savedParts(t, f)["xl/worksheets/sheet1.xml"]
	if !ok {
		t.Fatal("the package has no xl/worksheets/sheet1.xml part")
	}
	var ws worksheetDoc
	if err := xml.Unmarshal([]byte(raw), &ws); err != nil {
		t.Fatalf("the worksheet part is not well-formed: %v", err)
	}
	for _, row := range ws.SheetData.Row {
		for _, c := range row.C {
			if c.R == axis {
				return c.F
			}
		}
	}
	return nil
}

func seeded(t *testing.T) *seedcalc.File {
	t.Helper()
	f := seedcalc.NewFile()
	for i := 1; i <= 5; i++ {
		row := []int{i, i * 2}
		if err := f.SetSheetRow("Sheet1", "A"+string(rune('0'+i)), &row); err != nil {
			t.Fatalf("SetSheetRow: %v", err)
		}
	}
	return f
}

func TestSharedFormulaCarriesItsTypeAndRange(t *testing.T) {
	f := seeded(t)
	typ := seedcalc.STCellFormulaTypeShared
	ref := "C1:C5"
	err := f.SetCellFormula("Sheet1", "C1", "=A1+B1",
		seedcalc.FormulaOpts{Type: &typ, Ref: &ref})
	if err != nil {
		t.Fatalf("SetCellFormula: %v", err)
	}
	got := storedFormula(t, f, "C1")
	if got == nil {
		t.Fatal("C1 has no f element")
	}
	if got.T != "shared" {
		t.Errorf("formula type = %q, want shared", got.T)
	}
	if got.Ref != ref {
		t.Errorf("formula ref = %q, want %q", got.Ref, ref)
	}
	if got.Content != "=A1+B1" {
		t.Errorf("formula content = %q, want %q", got.Content, "=A1+B1")
	}
}

func TestArrayFormulaCarriesItsTypeAndRange(t *testing.T) {
	f := seeded(t)
	typ := seedcalc.STCellFormulaTypeArray
	ref := "E1:E1"
	err := f.SetCellFormula("Sheet1", "E1", "=SUM(A1:A5*B1:B5)",
		seedcalc.FormulaOpts{Type: &typ, Ref: &ref})
	if err != nil {
		t.Fatalf("SetCellFormula: %v", err)
	}
	got := storedFormula(t, f, "E1")
	if got == nil {
		t.Fatal("E1 has no f element")
	}
	if got.T != "array" {
		t.Errorf("formula type = %q, want array", got.T)
	}
	if got.Ref != ref {
		t.Errorf("formula ref = %q, want %q", got.Ref, ref)
	}
}

func TestFormulaOptionsCombineAcrossArguments(t *testing.T) {
	f := seeded(t)
	typ := seedcalc.STCellFormulaTypeShared
	ref := "D1:D5"
	err := f.SetCellFormula("Sheet1", "D1", "=A1*B1",
		seedcalc.FormulaOpts{},
		seedcalc.FormulaOpts{Type: &typ},
		seedcalc.FormulaOpts{Ref: &ref})
	if err != nil {
		t.Fatalf("SetCellFormula: %v", err)
	}
	got := storedFormula(t, f, "D1")
	if got == nil {
		t.Fatal("D1 has no f element")
	}
	if got.T != "shared" || got.Ref != ref {
		t.Errorf("combined options produced t=%q ref=%q, want shared and %q", got.T, got.Ref, ref)
	}
}

func TestFormulaWithoutOptionsHasNoTypeOrRange(t *testing.T) {
	f := seeded(t)
	if err := f.SetCellFormula("Sheet1", "C1", "=A1+B1"); err != nil {
		t.Fatalf("SetCellFormula: %v", err)
	}
	got := storedFormula(t, f, "C1")
	if got == nil {
		t.Fatal("C1 has no f element")
	}
	if got.T != "" || got.Ref != "" {
		t.Errorf("a plain formula was written with t=%q ref=%q, want neither attribute", got.T, got.Ref)
	}
}

func TestSettingAnEmptyFormulaRemovesIt(t *testing.T) {
	f := seeded(t)
	typ := seedcalc.STCellFormulaTypeShared
	ref := "C1:C5"
	if err := f.SetCellFormula("Sheet1", "C1", "=A1+B1",
		seedcalc.FormulaOpts{Type: &typ, Ref: &ref}); err != nil {
		t.Fatalf("SetCellFormula: %v", err)
	}
	if storedFormula(t, f, "C1") == nil {
		t.Fatal("the formula was not written in the first place")
	}
	if err := f.SetCellFormula("Sheet1", "C1", ""); err != nil {
		t.Fatalf("clearing the formula: %v", err)
	}
	if got := storedFormula(t, f, "C1"); got != nil {
		t.Errorf("C1 still carries a formula %q after being cleared", got.Content)
	}
	text, err := f.GetCellFormula("Sheet1", "C1")
	if err != nil {
		t.Fatalf("GetCellFormula: %v", err)
	}
	if text != "" {
		t.Errorf("GetCellFormula returned %q for a cleared cell", text)
	}
}

func TestGetCellFormulaReadsBackTheStoredText(t *testing.T) {
	f := seeded(t)
	typ := seedcalc.STCellFormulaTypeShared
	ref := "C1:C5"
	if err := f.SetCellFormula("Sheet1", "C1", "=A1+B1",
		seedcalc.FormulaOpts{Type: &typ, Ref: &ref}); err != nil {
		t.Fatalf("SetCellFormula: %v", err)
	}
	got, err := f.GetCellFormula("Sheet1", "C1")
	if err != nil {
		t.Fatalf("GetCellFormula: %v", err)
	}
	if got != "=A1+B1" {
		t.Errorf("GetCellFormula = %q, want %q", got, "=A1+B1")
	}
}

func TestFormulaTypeNames(t *testing.T) {
	for name, pair := range map[string][2]string{
		"array":     {seedcalc.STCellFormulaTypeArray, "array"},
		"dataTable": {seedcalc.STCellFormulaTypeDataTable, "dataTable"},
		"normal":    {seedcalc.STCellFormulaTypeNormal, "normal"},
		"shared":    {seedcalc.STCellFormulaTypeShared, "shared"},
	} {
		if pair[0] != pair[1] {
			t.Errorf("the %s formula type constant is %q", name, pair[0])
		}
	}
	f := seeded(t)
	typ := seedcalc.STCellFormulaTypeDataTable
	ref := "C1:C2"
	if err := f.SetCellFormula("Sheet1", "C1", "=A1+B1",
		seedcalc.FormulaOpts{Type: &typ, Ref: &ref}); err != nil {
		t.Fatalf("SetCellFormula: %v", err)
	}
	if got := storedFormula(t, f, "C1"); got == nil || got.T != "dataTable" {
		t.Errorf("a dataTable formula was written as %+v", got)
	}
}

func TestFormulaRejectsUnknownSheetAndBadReference(t *testing.T) {
	f := seeded(t)
	typ := seedcalc.STCellFormulaTypeShared
	ref := "C1:C5"
	if err := f.SetCellFormula("SheetX", "C1", "=A1+B1",
		seedcalc.FormulaOpts{Type: &typ, Ref: &ref}); err == nil {
		t.Error("SetCellFormula accepted a sheet that does not exist")
	}
	if err := f.SetCellFormula("Sheet1", "1C", "=A1+B1",
		seedcalc.FormulaOpts{Type: &typ, Ref: &ref}); err == nil {
		t.Error("SetCellFormula accepted a malformed cell reference")
	}
	if _, err := f.GetCellFormula("SheetX", "C1"); err == nil {
		t.Error("GetCellFormula accepted a sheet that does not exist")
	}
}
