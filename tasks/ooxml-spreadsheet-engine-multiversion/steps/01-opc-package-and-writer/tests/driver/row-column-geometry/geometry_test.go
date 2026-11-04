// Scored group: row-column-geometry.
//
// Row height, column width, hidden rows and hidden columns -- the sheetFormatPr,
// cols/col and row attributes of ECMA-376 Part 1 clauses 18.3.1.81, 18.3.1.13
// and 18.3.1.73 -- together with the row and column edits that shift the cells
// around them. Row indices at this milestone are zero-based.
package rowcolumngeometry

import (
	"bytes"
	"fmt"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

func roundTrip(t *testing.T, f *seedcalc.File) *seedcalc.File {
	t.Helper()
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	g, err := seedcalc.OpenReader(bytes.NewReader(buf.Bytes()))
	if err != nil {
		t.Fatalf("OpenReader: %v", err)
	}
	return g
}

func seed(f *seedcalc.File) {
	for i, v := range []string{"r1", "r2", "r3"} {
		f.SetCellValue("Sheet1", fmt.Sprintf("A%d", i+1), v)
		f.SetCellValue("Sheet1", fmt.Sprintf("B%d", i+1), v+"b")
	}
}

func TestADefaultWorksheetReportsItsDefaultGeometry(t *testing.T) {
	f := seedcalc.NewFile()
	if got := f.GetRowHeight("Sheet1", 0); got != 20 {
		t.Errorf("default row height = %v, want 20", got)
	}
	if got := f.GetColWidth("Sheet1", "A"); got != 64 {
		t.Errorf("default column width = %v, want 64", got)
	}
}

func TestSetColWidthAppliesAcrossTheWholeRange(t *testing.T) {
	f := seedcalc.NewFile()
	f.SetColWidth("Sheet1", "D", "F", 27.5)

	for _, col := range []string{"D", "E", "F"} {
		if got := f.GetColWidth("Sheet1", col); got != 27.5 {
			t.Errorf("width of column %s = %v, want 27.5", col, got)
		}
	}
	for _, col := range []string{"C", "G"} {
		if got := f.GetColWidth("Sheet1", col); got != 64 {
			t.Errorf("width of column %s = %v, want the default 64", col, got)
		}
	}
	g := roundTrip(t, f)
	if got := g.GetColWidth("Sheet1", "E"); got != 27.5 {
		t.Errorf("after the round trip the width of column E = %v, want 27.5", got)
	}
}

func TestSetRowHeightAppliesToOneRowOnly(t *testing.T) {
	f := seedcalc.NewFile()
	f.SetCellValue("Sheet1", "A3", "x")
	f.SetRowHeight("Sheet1", 0, 50)

	if got := f.GetRowHeight("Sheet1", 0); got != 50 {
		t.Errorf("height of the first row = %v, want 50", got)
	}
	if got := f.GetRowHeight("Sheet1", 1); got != 20 {
		t.Errorf("height of the second row = %v, want the default 20", got)
	}
	g := roundTrip(t, f)
	if got := g.GetRowHeight("Sheet1", 0); got != 50 {
		t.Errorf("after the round trip the height of the first row = %v, want 50", got)
	}
}

func TestHidingAColumnIsRecorded(t *testing.T) {
	f := seedcalc.NewFile()
	f.SetColVisible("Sheet1", "C", false)

	if f.GetColVisible("Sheet1", "C") {
		t.Error("column C is still reported visible after being hidden")
	}
	if !f.GetColVisible("Sheet1", "A") {
		t.Error("hiding column C also hid column A")
	}
	g := roundTrip(t, f)
	if g.GetColVisible("Sheet1", "C") {
		t.Error("the hidden column came back visible after the round trip")
	}
	f.SetColVisible("Sheet1", "C", true)
	if !f.GetColVisible("Sheet1", "C") {
		t.Error("a column that was hidden and then shown is still reported hidden")
	}
}

func TestHidingARowIsRecorded(t *testing.T) {
	f := seedcalc.NewFile()
	f.SetCellValue("Sheet1", "A3", "x")
	f.SetRowVisible("Sheet1", 2, false)

	if f.GetRowVisible("Sheet1", 2) {
		t.Error("the third row is still reported visible after being hidden")
	}
	if !f.GetRowVisible("Sheet1", 0) {
		t.Error("hiding the third row also hid the first")
	}
	g := roundTrip(t, f)
	if g.GetRowVisible("Sheet1", 2) {
		t.Error("the hidden row came back visible after the round trip")
	}
	f.SetRowVisible("Sheet1", 2, true)
	if !f.GetRowVisible("Sheet1", 2) {
		t.Error("a row that was hidden and then shown is still reported hidden")
	}
}

func TestInsertingAndRemovingARowShiftsTheRowsBelowIt(t *testing.T) {
	f := seedcalc.NewFile()
	seed(f)

	f.InsertRow("Sheet1", 1)
	for axis, want := range map[string]string{"A1": "r1", "A2": "", "A3": "r2", "A4": "r3", "B3": "r2b"} {
		if got := f.GetCellValue("Sheet1", axis); got != want {
			t.Errorf("after InsertRow(1) %s = %q, want %q", axis, got, want)
		}
	}
	if got := len(f.GetRows("Sheet1")); got != 4 {
		t.Errorf("after InsertRow(1) the sheet has %d rows, want 4", got)
	}

	f.RemoveRow("Sheet1", 1)
	for axis, want := range map[string]string{"A1": "r1", "A2": "r2", "A3": "r3", "B2": "r2b"} {
		if got := f.GetCellValue("Sheet1", axis); got != want {
			t.Errorf("after RemoveRow(1) %s = %q, want %q", axis, got, want)
		}
	}
	if got := len(f.GetRows("Sheet1")); got != 3 {
		t.Errorf("after RemoveRow(1) the sheet has %d rows, want 3", got)
	}
}

func TestInsertingAndRemovingAColumnShiftsTheColumnsRightOfIt(t *testing.T) {
	f := seedcalc.NewFile()
	seed(f)

	f.InsertCol("Sheet1", "A")
	for axis, want := range map[string]string{"A1": "", "B1": "r1", "C1": "r1b", "B3": "r3"} {
		if got := f.GetCellValue("Sheet1", axis); got != want {
			t.Errorf("after InsertCol(A) %s = %q, want %q", axis, got, want)
		}
	}

	f.RemoveCol("Sheet1", "A")
	for axis, want := range map[string]string{"A1": "r1", "B1": "r1b", "A3": "r3"} {
		if got := f.GetCellValue("Sheet1", axis); got != want {
			t.Errorf("after RemoveCol(A) %s = %q, want %q", axis, got, want)
		}
	}
}
