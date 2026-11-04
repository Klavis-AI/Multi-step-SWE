// Gate group: worksheet-features-regression.
//
// Inherited worksheet-level behaviour that this milestone's rewrite of the
// sheet-addressing model and of the style description must not disturb: row
// and column geometry, outline levels, merged regions, structural row and
// column edits, data validation and sheet protection. Row and column
// properties are ECMA-376 Part 1 section 18.3.1.73 "row (Row)" and section
// 18.3.1.13 "col (Column Width & Formatting)"; merged regions are section
// 18.3.1.55 "mergeCell (Merged Cell)"; sheet protection is section 18.3.1.85
// "sheetProtection (Sheet Protection Options)"; data validation is section
// 18.3.1.32 "dataValidation (Data Validation)".
package worksheetfeaturesregression

import (
	"archive/zip"
	"bytes"
	"io"
	"strings"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// sheetPart returns the bytes of xl/worksheets/sheet1.xml from the written
// package.
func sheetPart(t *testing.T, f *seedcalc.File) string {
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
			t.Fatalf("open worksheet part: %v", err)
		}
		defer rc.Close()
		b, err := io.ReadAll(rc)
		if err != nil {
			t.Fatalf("read worksheet part: %v", err)
		}
		return string(b)
	}
	t.Fatal("written package has no xl/worksheets/sheet1.xml part")
	return ""
}

func TestRowHeightAndVisibility(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetRowHeight("Sheet1", 2, 42.5); err != nil {
		t.Fatalf("SetRowHeight: %v", err)
	}
	h, err := f.GetRowHeight("Sheet1", 2)
	if err != nil {
		t.Fatalf("GetRowHeight: %v", err)
	}
	if h != 42.5 {
		t.Fatalf("GetRowHeight = %v, want 42.5", h)
	}
	if err := f.SetRowVisible("Sheet1", 2, false); err != nil {
		t.Fatalf("SetRowVisible: %v", err)
	}
	visible, err := f.GetRowVisible("Sheet1", 2)
	if err != nil {
		t.Fatalf("GetRowVisible: %v", err)
	}
	if visible {
		t.Fatal("GetRowVisible = true after the row was hidden")
	}
	if err := f.SetRowHeight("Sheet1", 0, 10); err == nil {
		t.Fatal("SetRowHeight accepted row number 0; rows are numbered from 1")
	}
	if _, err := f.GetRowHeight("Sheet1", 0); err == nil {
		t.Fatal("GetRowHeight accepted row number 0")
	}
}

func TestColumnWidthAndVisibility(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetColWidth("Sheet1", "B", "D", 25); err != nil {
		t.Fatalf("SetColWidth: %v", err)
	}
	for _, col := range []string{"B", "C", "D"} {
		w, err := f.GetColWidth("Sheet1", col)
		if err != nil {
			t.Fatalf("GetColWidth(%s): %v", col, err)
		}
		if w != 25 {
			t.Fatalf("GetColWidth(%s) = %v, want 25", col, w)
		}
	}
	if err := f.SetColVisible("Sheet1", "C", false); err != nil {
		t.Fatalf("SetColVisible: %v", err)
	}
	visible, err := f.GetColVisible("Sheet1", "C")
	if err != nil {
		t.Fatalf("GetColVisible: %v", err)
	}
	if visible {
		t.Fatal("GetColVisible = true after the column was hidden")
	}
	if err := f.SetColWidth("Sheet1", "*", "D", 10); err == nil {
		t.Fatal("SetColWidth accepted a malformed column name")
	}
}

func TestOutlineLevels(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetRowOutlineLevel("Sheet1", 2, 3); err != nil {
		t.Fatalf("SetRowOutlineLevel: %v", err)
	}
	level, err := f.GetRowOutlineLevel("Sheet1", 2)
	if err != nil {
		t.Fatalf("GetRowOutlineLevel: %v", err)
	}
	if level != 3 {
		t.Fatalf("GetRowOutlineLevel = %d, want 3", level)
	}
	if err := f.SetColOutlineLevel("Sheet1", "D", 4); err != nil {
		t.Fatalf("SetColOutlineLevel: %v", err)
	}
	colLevel, err := f.GetColOutlineLevel("Sheet1", "D")
	if err != nil {
		t.Fatalf("GetColOutlineLevel: %v", err)
	}
	if colLevel != 4 {
		t.Fatalf("GetColOutlineLevel = %d, want 4", colLevel)
	}
	if err := f.SetRowOutlineLevel("Sheet1", 0, 1); err == nil {
		t.Fatal("SetRowOutlineLevel accepted row number 0")
	}
}

func TestMergingAndUnmergingARegion(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetCellValue("Sheet1", "A1", "merged"); err != nil {
		t.Fatalf("SetCellValue: %v", err)
	}
	if err := f.MergeCell("Sheet1", "A1", "C3"); err != nil {
		t.Fatalf("MergeCell: %v", err)
	}
	cells, err := f.GetMergeCells("Sheet1")
	if err != nil {
		t.Fatalf("GetMergeCells: %v", err)
	}
	if len(cells) != 1 {
		t.Fatalf("GetMergeCells returned %d regions, want 1", len(cells))
	}
	if got := cells[0].GetStartAxis(); got != "A1" {
		t.Fatalf("merged region starts at %q, want A1", got)
	}
	if got := cells[0].GetEndAxis(); got != "C3" {
		t.Fatalf("merged region ends at %q, want C3", got)
	}
	if got := cells[0].GetCellValue(); got != "merged" {
		t.Fatalf("merged region value = %q, want %q", got, "merged")
	}
	if err := f.UnmergeCell("Sheet1", "A1", "C3"); err != nil {
		t.Fatalf("UnmergeCell: %v", err)
	}
	cells, err = f.GetMergeCells("Sheet1")
	if err != nil {
		t.Fatalf("GetMergeCells after unmerge: %v", err)
	}
	if len(cells) != 0 {
		t.Fatalf("GetMergeCells after unmerge returned %d regions, want 0", len(cells))
	}
}

func TestInsertingAndRemovingRowsAndColumns(t *testing.T) {
	f := seedcalc.NewFile()
	for axis, v := range map[string]string{"A1": "r1", "A2": "r2", "A3": "r3", "B1": "b1"} {
		if err := f.SetCellValue("Sheet1", axis, v); err != nil {
			t.Fatalf("SetCellValue(%s): %v", axis, err)
		}
	}
	if err := f.InsertRow("Sheet1", 2); err != nil {
		t.Fatalf("InsertRow: %v", err)
	}
	got, err := f.GetCellValue("Sheet1", "A3")
	if err != nil {
		t.Fatalf("GetCellValue: %v", err)
	}
	if got != "r2" {
		t.Fatalf("after inserting a row at 2, A3 = %q, want %q", got, "r2")
	}
	if err := f.RemoveRow("Sheet1", 2); err != nil {
		t.Fatalf("RemoveRow: %v", err)
	}
	got, err = f.GetCellValue("Sheet1", "A2")
	if err != nil {
		t.Fatalf("GetCellValue: %v", err)
	}
	if got != "r2" {
		t.Fatalf("after removing the inserted row, A2 = %q, want %q", got, "r2")
	}
	if err := f.InsertCol("Sheet1", "A"); err != nil {
		t.Fatalf("InsertCol: %v", err)
	}
	got, err = f.GetCellValue("Sheet1", "B1")
	if err != nil {
		t.Fatalf("GetCellValue: %v", err)
	}
	if got != "r1" {
		t.Fatalf("after inserting a column at A, B1 = %q, want %q", got, "r1")
	}
	if err := f.RemoveCol("Sheet1", "A"); err != nil {
		t.Fatalf("RemoveCol: %v", err)
	}
	got, err = f.GetCellValue("Sheet1", "A1")
	if err != nil {
		t.Fatalf("GetCellValue: %v", err)
	}
	if got != "r1" {
		t.Fatalf("after removing the inserted column, A1 = %q, want %q", got, "r1")
	}
	if err := f.InsertRow("Sheet1", 0); err == nil {
		t.Fatal("InsertRow accepted row number 0")
	}
	if err := f.RemoveCol("Sheet1", "*"); err == nil {
		t.Fatal("RemoveCol accepted a malformed column name")
	}
}

func TestDataValidationIsWrittenToTheWorksheet(t *testing.T) {
	f := seedcalc.NewFile()
	dv := seedcalc.NewDataValidation(true)
	dv.Sqref = "A1:B2"
	if err := dv.SetRange(10, 20, seedcalc.DataValidationTypeWhole, seedcalc.DataValidationOperatorBetween); err != nil {
		t.Fatalf("SetRange: %v", err)
	}
	dv.SetError(seedcalc.DataValidationErrorStyleStop, "out of range", "value must be between 10 and 20")
	if err := f.AddDataValidation("Sheet1", dv); err != nil {
		t.Fatalf("AddDataValidation: %v", err)
	}
	xml := sheetPart(t, f)
	if !strings.Contains(xml, "<dataValidation") {
		t.Fatal("worksheet part carries no dataValidation element")
	}
	list := seedcalc.NewDataValidation(true)
	list.Sqref = "D1:D5"
	if err := list.SetDropList([]string{"one", "two", "three"}); err != nil {
		t.Fatalf("SetDropList: %v", err)
	}
	if err := f.AddDataValidation("Sheet1", list); err != nil {
		t.Fatalf("AddDataValidation for a drop list: %v", err)
	}
	if err := f.AddDataValidation("NoSuchSheet", dv); err == nil {
		t.Fatal("AddDataValidation on an absent sheet returned no error")
	}
}

func TestSheetProtection(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.ProtectSheet("Sheet1", &seedcalc.FormatSheetProtection{
		Password:          "secret",
		EditScenarios:     false,
		FormatCells:       true,
		SelectLockedCells: true,
	}); err != nil {
		t.Fatalf("ProtectSheet: %v", err)
	}
	xml := sheetPart(t, f)
	if !strings.Contains(xml, "<sheetProtection") {
		t.Fatal("worksheet part carries no sheetProtection element after ProtectSheet")
	}
	if err := f.UnprotectSheet("Sheet1"); err != nil {
		t.Fatalf("UnprotectSheet: %v", err)
	}
	xml = sheetPart(t, f)
	if strings.Contains(xml, "<sheetProtection") {
		t.Fatal("worksheet part still carries a sheetProtection element after UnprotectSheet")
	}
	if err := f.ProtectSheet("NoSuchSheet", &seedcalc.FormatSheetProtection{}); err == nil {
		t.Fatal("ProtectSheet on an absent sheet returned no error")
	}
	if err := f.UnprotectSheet("NoSuchSheet"); err == nil {
		t.Fatal("UnprotectSheet on an absent sheet returned no error")
	}
}
