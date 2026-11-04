// Scored group: shared-formulas.
//
// A shared formula becomes functional here. Milestone 04 exposed FormulaOpts
// as a pair of attributes the caller wrote by hand onto one cell, and reading
// any other cell of the range gave back the master's text unchanged. Now
// SetCellFormula populates the whole range with an auto-assigned index: the
// master cell carries the text, the range and the index, the rest of the range
// carries only the index back to it, and reading any of them returns the
// formula translated to that cell's position. The array and data-table types
// travel through the same option. Behaviour transcribed from the upstream checkpoint this
// milestone tracks, qax-os/excelize tag v2.5.0. The serialized form is
// ECMA-376 Part 1 section 18.3.1.40 "f (Formula)" -- its @t, @ref and @si
// attributes -- and section 18.18.6 "ST_CellFormulaType".
package sharedformulas

import (
	"archive/zip"
	"bytes"
	"errors"
	"io"
	"strings"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// grid builds a workbook whose columns A to D hold the number 100*col+row, so
// a shifted reference can be told apart from an unshifted one by its value.
func grid(t *testing.T) *seedcalc.File {
	t.Helper()
	f := seedcalc.NewFile()
	for col := 1; col <= 4; col++ {
		name, err := seedcalc.ColumnNumberToName(col)
		if err != nil {
			t.Fatalf("ColumnNumberToName(%d): %v", col, err)
		}
		for row := 1; row <= 6; row++ {
			axis, err := seedcalc.JoinCellName(name, row)
			if err != nil {
				t.Fatalf("JoinCellName(%s, %d): %v", name, row, err)
			}
			if err := f.SetCellValue("Sheet1", axis, col*100+row); err != nil {
				t.Fatalf("SetCellValue(%s): %v", axis, err)
			}
		}
	}
	return f
}

// share declares formula shared over ref, with master as the cell that holds
// the text. Note that a shared formula always needs a Ref: the option struct
// takes a pointer and the library dereferences it without checking.
func share(t *testing.T, f *seedcalc.File, master, formula, ref string) error {
	t.Helper()
	kind := seedcalc.STCellFormulaTypeShared
	return f.SetCellFormula("Sheet1", master, formula, seedcalc.FormulaOpts{Type: &kind, Ref: &ref})
}

// formulaAt reads back the formula the given cell resolves to.
func formulaAt(t *testing.T, f *seedcalc.File, axis string) string {
	t.Helper()
	got, err := f.GetCellFormula("Sheet1", axis)
	if err != nil {
		t.Fatalf("GetCellFormula(%s): %v", axis, err)
	}
	return got
}

// wantFormulas checks a whole range of cells at once.
func wantFormulas(t *testing.T, f *seedcalc.File, want map[string]string) {
	t.Helper()
	for axis, expected := range want {
		if got := formulaAt(t, f, axis); got != expected {
			t.Fatalf("GetCellFormula(%s) = %q, want %q", axis, got, expected)
		}
	}
}

// sheetXML returns the worksheet part of the written package.
func sheetXML(t *testing.T, f *seedcalc.File) string {
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
		if zf.Name != "xl/worksheets/sheet1.xml" {
			continue
		}
		rc, err := zf.Open()
		if err != nil {
			t.Fatalf("open the worksheet part: %v", err)
		}
		defer rc.Close()
		b, err := io.ReadAll(rc)
		if err != nil {
			t.Fatalf("read the worksheet part: %v", err)
		}
		return string(b)
	}
	t.Fatal("written package has no xl/worksheets/sheet1.xml")
	return ""
}

func TestTheFormulaTypeConstantsAreExported(t *testing.T) {
	for _, tc := range []struct{ got, want string }{
		{seedcalc.STCellFormulaTypeArray, "array"},
		{seedcalc.STCellFormulaTypeDataTable, "dataTable"},
		{seedcalc.STCellFormulaTypeNormal, "normal"},
		{seedcalc.STCellFormulaTypeShared, "shared"},
	} {
		if tc.got != tc.want {
			t.Fatalf("formula type constant = %q, want %q", tc.got, tc.want)
		}
	}
}

func TestASharedFormulaIsWrittenOnceAndReadBackEverywhere(t *testing.T) {
	f := grid(t)
	if err := share(t, f, "F1", "=A1+1", "F1:F3"); err != nil {
		t.Fatalf("SetCellFormula shared: %v", err)
	}
	wantFormulas(t, f, map[string]string{
		"F1": "=A1+1",
		"F2": "=A2+1",
		"F3": "=A3+1",
	})
	// Nothing outside the declared range picks the formula up.
	wantFormulas(t, f, map[string]string{"F4": ""})
	// And each cell computes its own row.
	for axis, want := range map[string]string{"F1": "102", "F2": "103", "F3": "104"} {
		got, err := f.CalcCellValue("Sheet1", axis)
		if err != nil {
			t.Fatalf("CalcCellValue(%s): %v", axis, err)
		}
		if got != want {
			t.Fatalf("CalcCellValue(%s) = %q, want %q", axis, got, want)
		}
	}
}

func TestTheTranslationFollowsBothTheRowAndTheColumnOffset(t *testing.T) {
	f := grid(t)
	if err := share(t, f, "F1", "=A1+1", "F1:H2"); err != nil {
		t.Fatalf("SetCellFormula shared: %v", err)
	}
	wantFormulas(t, f, map[string]string{
		"F1": "=A1+1", "G1": "=B1+1", "H1": "=C1+1",
		"F2": "=A2+1", "G2": "=B2+1", "H2": "=C2+1",
	})
}

func TestAnAbsoluteReferenceIsNotTranslated(t *testing.T) {
	f := grid(t)
	// A dollar sign pins that half of the reference, so only the free half
	// moves with the cell.
	if err := share(t, f, "F1", "=$A1+1", "F1:F3"); err != nil {
		t.Fatalf("SetCellFormula shared: %v", err)
	}
	wantFormulas(t, f, map[string]string{"F1": "=$A1+1", "F2": "=$A2+1", "F3": "=$A3+1"})

	g := grid(t)
	if err := share(t, g, "F1", "=A$1+1", "F1:F3"); err != nil {
		t.Fatalf("SetCellFormula shared: %v", err)
	}
	wantFormulas(t, g, map[string]string{"F1": "=A$1+1", "F2": "=A$1+1", "F3": "=A$1+1"})

	h := grid(t)
	if err := share(t, h, "F1", "=$A$1+1", "F1:F3"); err != nil {
		t.Fatalf("SetCellFormula shared: %v", err)
	}
	wantFormulas(t, h, map[string]string{"F1": "=$A$1+1", "F2": "=$A$1+1", "F3": "=$A$1+1"})
}

func TestOnlyTheReferencesInTheFormulaMove(t *testing.T) {
	f := grid(t)
	// Both ends of a range argument are translated.
	if err := share(t, f, "F1", "=SUM(A1:B2)", "F1:F3"); err != nil {
		t.Fatalf("SetCellFormula shared: %v", err)
	}
	wantFormulas(t, f, map[string]string{
		"F1": "=SUM(A1:B2)", "F2": "=SUM(A2:B3)", "F3": "=SUM(A3:B4)",
	})

	// A sheet qualifier stays attached to the reference it qualifies.
	g := grid(t)
	if err := share(t, g, "F1", "=Sheet1!A1+1", "F1:F3"); err != nil {
		t.Fatalf("SetCellFormula shared: %v", err)
	}
	wantFormulas(t, g, map[string]string{
		"F1": "=Sheet1!A1+1", "F2": "=Sheet1!A2+1", "F3": "=Sheet1!A3+1",
	})

	// Text inside the formula is left exactly as it was written.
	h := grid(t)
	if err := share(t, h, "F1", `=A1&"x"`, "F1:F3"); err != nil {
		t.Fatalf("SetCellFormula shared: %v", err)
	}
	wantFormulas(t, h, map[string]string{
		"F1": `=A1&"x"`, "F2": `=A2&"x"`, "F3": `=A3&"x"`,
	})
}

func TestTheMasterCellNeedNotBeTheTopOfTheRange(t *testing.T) {
	f := grid(t)
	// The offset is measured from wherever the master sits, so a master in
	// the middle translates backwards as well as forwards.
	if err := share(t, f, "F3", "=A3+1", "F1:F5"); err != nil {
		t.Fatalf("SetCellFormula shared: %v", err)
	}
	wantFormulas(t, f, map[string]string{
		"F1": "=A1+1", "F2": "=A2+1", "F3": "=A3+1", "F4": "=A4+1", "F5": "=A5+1",
	})
	// The range is normalised, so naming its corners the other way round
	// makes no difference.
	g := grid(t)
	if err := share(t, g, "F3", "=A3+1", "F5:F1"); err != nil {
		t.Fatalf("SetCellFormula shared: %v", err)
	}
	wantFormulas(t, g, map[string]string{"F1": "=A1+1", "F3": "=A3+1", "F5": "=A5+1"})
}

func TestAMasterCellOutsideTheDeclaredRangeIsAccepted(t *testing.T) {
	f := grid(t)
	// Declaring the range somewhere other than around the master is accepted,
	// but translating back to it would run off the left edge of the sheet, so
	// the cells in the range resolve to nothing at all.
	if err := share(t, f, "H1", "=A1+1", "F1:F3"); err != nil {
		t.Fatalf("SetCellFormula shared: %v", err)
	}
	wantFormulas(t, f, map[string]string{"H1": "=A1+1", "F1": "", "F2": "", "F3": ""})
}

func TestEachSharedRangeGetsItsOwnIndex(t *testing.T) {
	f := grid(t)
	if err := share(t, f, "F1", "=A1+1", "F1:F3"); err != nil {
		t.Fatalf("SetCellFormula shared: %v", err)
	}
	if err := share(t, f, "G1", "=A1*2", "G1:G2"); err != nil {
		t.Fatalf("SetCellFormula shared: %v", err)
	}
	// Two independent groups: neither one translates the other's text.
	wantFormulas(t, f, map[string]string{
		"F1": "=A1+1", "F2": "=A2+1", "F3": "=A3+1",
		"G1": "=A1*2", "G2": "=A2*2", "G3": "",
	})
}

func TestTheWorksheetPartCarriesTheFormulaOnlyOnce(t *testing.T) {
	f := grid(t)
	if err := share(t, f, "F1", "=A1+1", "F1:F3"); err != nil {
		t.Fatalf("SetCellFormula shared: %v", err)
	}
	if err := share(t, f, "G1", "=A1*2", "G1:G2"); err != nil {
		t.Fatalf("SetCellFormula shared: %v", err)
	}
	xml := sheetXML(t, f)
	// The master cell holds the type, the range and the index.
	if !strings.Contains(xml, `<f t="shared" ref="F1:F3" si="0">=A1+1</f>`) {
		t.Fatalf("worksheet part does not declare the first shared formula at its master cell:\n%s", xml)
	}
	if !strings.Contains(xml, `<f t="shared" ref="G1:G2" si="1">=A1*2</f>`) {
		t.Fatalf("worksheet part does not declare the second shared formula at its master cell:\n%s", xml)
	}
	// The other cells of the range carry the index and no text.
	if !strings.Contains(xml, `<f t="shared" si="0"></f>`) {
		t.Fatalf("worksheet part does not point the rest of the first range back at its master:\n%s", xml)
	}
	if !strings.Contains(xml, `<f t="shared" si="1"></f>`) {
		t.Fatalf("worksheet part does not point the rest of the second range back at its master:\n%s", xml)
	}
	if n := strings.Count(xml, "=A1+1"); n != 1 {
		t.Fatalf("the shared formula text appears %d times in the worksheet part, want 1", n)
	}
}

func TestASharedFormulaSurvivesAWriteAndReopen(t *testing.T) {
	f := grid(t)
	if err := share(t, f, "F1", "=A1+1", "F1:F3"); err != nil {
		t.Fatalf("SetCellFormula shared: %v", err)
	}
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	g, err := seedcalc.OpenReader(bytes.NewReader(buf.Bytes()))
	if err != nil {
		t.Fatalf("OpenReader: %v", err)
	}
	for axis, want := range map[string]string{"F1": "=A1+1", "F2": "=A2+1", "F3": "=A3+1"} {
		got, err := g.GetCellFormula("Sheet1", axis)
		if err != nil {
			t.Fatalf("GetCellFormula(%s) after round trip: %v", axis, err)
		}
		if got != want {
			t.Fatalf("GetCellFormula(%s) after round trip = %q, want %q", axis, got, want)
		}
	}
	got, err := g.CalcCellValue("Sheet1", "F3")
	if err != nil {
		t.Fatalf("CalcCellValue(F3) after round trip: %v", err)
	}
	if got != "104" {
		t.Fatalf("CalcCellValue(F3) after round trip = %q, want %q", got, "104")
	}
}

func TestClearingTheMasterCellEmptiesTheWholeRange(t *testing.T) {
	f := grid(t)
	if err := share(t, f, "F1", "=A1+1", "F1:F3"); err != nil {
		t.Fatalf("SetCellFormula shared: %v", err)
	}
	// An empty formula removes the cell's formula altogether. The rest of the
	// range still points at the master, which now has nothing to translate.
	if err := f.SetCellFormula("Sheet1", "F1", ""); err != nil {
		t.Fatalf("SetCellFormula with an empty formula: %v", err)
	}
	wantFormulas(t, f, map[string]string{"F1": "", "F2": "", "F3": ""})
}

func TestTheRefMustNameAnArea(t *testing.T) {
	f := grid(t)
	kind := seedcalc.STCellFormulaTypeShared
	// A ref that does not name two corners at all is the wrong kind of
	// parameter, and reports the sentinel for that.
	for _, ref := range []string{"F1", "", "notaref"} {
		r := ref
		err := f.SetCellFormula("Sheet1", "F1", "=A1+1", seedcalc.FormulaOpts{Type: &kind, Ref: &r})
		if err == nil {
			t.Fatalf("SetCellFormula with ref %q returned no error", ref)
		}
		if !errors.Is(err, seedcalc.ErrParameterInvalid) {
			t.Fatalf("SetCellFormula with ref %q returned %v, want %v", ref, err, seedcalc.ErrParameterInvalid)
		}
	}
	// A ref with two corners but a corner that is not a cell name is reported
	// by whichever corner failed to parse.
	for _, ref := range []string{"F1:", ":F3", "F0:F3"} {
		r := ref
		if err := f.SetCellFormula("Sheet1", "F1", "=A1+1", seedcalc.FormulaOpts{Type: &kind, Ref: &r}); err == nil {
			t.Fatalf("SetCellFormula with ref %q returned no error", ref)
		}
	}
}

func TestTheOtherFormulaTypesTravelThroughTheSameOption(t *testing.T) {
	f := grid(t)
	// An array formula keeps its own range, and is not translated anywhere.
	kind, ref := seedcalc.STCellFormulaTypeArray, "F1:F1"
	if err := f.SetCellFormula("Sheet1", "F1", "=SUM(A1:A5)", seedcalc.FormulaOpts{Type: &kind, Ref: &ref}); err != nil {
		t.Fatalf("SetCellFormula array: %v", err)
	}
	wantFormulas(t, f, map[string]string{"F1": "=SUM(A1:A5)"})
	got, err := f.CalcCellValue("Sheet1", "F1")
	if err != nil {
		t.Fatalf("CalcCellValue(F1): %v", err)
	}
	if got != "515" {
		t.Fatalf("CalcCellValue(F1) = %q, want %q", got, "515")
	}
	if !strings.Contains(sheetXML(t, f), `<f t="array" ref="F1:F1">=SUM(A1:A5)</f>`) {
		t.Fatal("worksheet part does not record the array formula with its own range")
	}

	// The normal type is the default, written out explicitly.
	g := grid(t)
	normal := seedcalc.STCellFormulaTypeNormal
	if err := g.SetCellFormula("Sheet1", "F1", "=A1+3", seedcalc.FormulaOpts{Type: &normal}); err != nil {
		t.Fatalf("SetCellFormula normal: %v", err)
	}
	wantFormulas(t, g, map[string]string{"F1": "=A1+3"})

	// A data-table formula is accepted and stored, but the type is dropped:
	// the option returns as soon as it recognises it.
	h := grid(t)
	table := seedcalc.STCellFormulaTypeDataTable
	if err := h.SetCellFormula("Sheet1", "F1", "=A1+2", seedcalc.FormulaOpts{Type: &table}); err != nil {
		t.Fatalf("SetCellFormula dataTable: %v", err)
	}
	wantFormulas(t, h, map[string]string{"F1": "=A1+2"})
	if strings.Contains(sheetXML(t, h), `t="dataTable"`) {
		t.Fatal("worksheet part records a dataTable formula type; the option returns before setting it")
	}
}
