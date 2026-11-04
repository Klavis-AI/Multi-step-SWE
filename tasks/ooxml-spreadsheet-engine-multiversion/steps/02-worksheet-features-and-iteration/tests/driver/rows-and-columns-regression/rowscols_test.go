// Gate group: rows-and-columns-regression.
//
// Row and column geometry established earlier and unchanged here: reading
// every row as a rectangular grid, hiding rows and columns, column widths,
// and the fact that inserting then removing a row at the same position is an
// identity.
//
// The matching identity for columns is not here. Milestone 01 widened every
// row with a trailing empty cell on each InsertCol and never reclaimed it;
// this milestone corrects that, which makes the column identity new
// behaviour, scored in column-geometry-correction.
//
// Graded against ECMA-376 Part 1 clause 18.3.1.73 (row, and its hidden
// attribute) and clause 18.3.1.13 (col, and its width, customWidth and
// hidden attributes).
package rowsandcolumnsregression

import (
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

func grid(t *testing.T, f *seedcalc.File) [][]string {
	t.Helper()
	return f.GetRows("Sheet1")
}

func sameGrid(a, b [][]string) bool {
	if len(a) != len(b) {
		return false
	}
	for i := range a {
		if len(a[i]) != len(b[i]) {
			return false
		}
		for j := range a[i] {
			if a[i][j] != b[i][j] {
				return false
			}
		}
	}
	return true
}

func TestGetRowsReturnsARectangularGrid(t *testing.T) {
	f := seedcalc.NewFile()
	f.SetCellValue("Sheet1", "A1", "a1")
	f.SetCellValue("Sheet1", "C1", "c1")
	f.SetCellValue("Sheet1", "A3", "a3")

	rows := grid(t, f)
	if len(rows) != 3 {
		t.Fatalf("GetRows returned %d rows, want 3: %v", len(rows), rows)
	}
	for i, r := range rows {
		if len(r) != 3 {
			t.Fatalf("row %d has %d columns, want 3 (the grid is padded to the widest row): %v", i+1, len(r), rows)
		}
	}
	if rows[0][0] != "a1" || rows[0][2] != "c1" || rows[0][1] != "" {
		t.Errorf("row 1 = %v, want [a1  c1]", rows[0])
	}
	if rows[1][0] != "" {
		t.Errorf("row 2 = %v, want an empty row", rows[1])
	}
	if rows[2][0] != "a3" {
		t.Errorf("row 3 = %v, want a3 in the first column", rows[2])
	}
}

func TestRowVisibilityRoundTrips(t *testing.T) {
	f := seedcalc.NewFile()
	f.SetCellValue("Sheet1", "A1", "a")
	f.SetCellValue("Sheet1", "A2", "b")

	if !f.GetRowVisible("Sheet1", 1) {
		t.Fatal("a row is hidden before anything hid it")
	}
	f.SetRowVisible("Sheet1", 1, false)
	if f.GetRowVisible("Sheet1", 1) {
		t.Error("the row is still visible after being hidden")
	}
	if !f.GetRowVisible("Sheet1", 0) {
		t.Error("hiding one row hid a different one")
	}
	f.SetRowVisible("Sheet1", 1, true)
	if !f.GetRowVisible("Sheet1", 1) {
		t.Error("the row is still hidden after being shown again")
	}
}

func TestColumnWidthRoundTrips(t *testing.T) {
	f := seedcalc.NewFile()
	f.SetColWidth("Sheet1", "B", "D", 25)

	for _, col := range []string{"B", "C", "D"} {
		if got := f.GetColWidth("Sheet1", col); got != 25 {
			t.Errorf("GetColWidth(%q) = %v, want 25", col, got)
		}
	}
	untouched := f.GetColWidth("Sheet1", "A")
	if untouched == 25 {
		t.Errorf("GetColWidth(\"A\") = %v; the width was set for B..D", untouched)
	}
	if untouched <= 0 {
		t.Errorf("GetColWidth on a column with no explicit width = %v, want the positive default", untouched)
	}
	if got := f.GetColWidth("Sheet1", "Z"); got != untouched {
		t.Errorf("GetColWidth(\"Z\") = %v, want the same default as any other untouched column (%v)", got, untouched)
	}
}

func TestColumnVisibilityRoundTrips(t *testing.T) {
	f := seedcalc.NewFile()
	if !f.GetColVisible("Sheet1", "C") {
		t.Fatal("a column is hidden before anything hid it")
	}
	f.SetColVisible("Sheet1", "C", false)
	if f.GetColVisible("Sheet1", "C") {
		t.Error("the column is still visible after being hidden")
	}
	if !f.GetColVisible("Sheet1", "B") {
		t.Error("hiding one column hid a different one")
	}
	f.SetColVisible("Sheet1", "C", true)
	if !f.GetColVisible("Sheet1", "C") {
		t.Error("the column is still hidden after being shown again")
	}
}

func TestInsertAndRemoveRowAreInverses(t *testing.T) {
	f := seedcalc.NewFile()
	f.SetCellValue("Sheet1", "A1", "a1")
	f.SetCellValue("Sheet1", "B1", "b1")
	f.SetCellValue("Sheet1", "A2", "a2")
	before := grid(t, f)

	f.InsertRow("Sheet1", 1)
	after := grid(t, f)
	if len(after) != len(before)+1 {
		t.Fatalf("InsertRow left %d rows, want %d", len(after), len(before)+1)
	}
	if after[0][0] != "a1" || after[0][1] != "b1" {
		t.Errorf("the first row changed after an insert below it: %v", after[0])
	}
	if after[len(after)-1][0] != "a2" {
		t.Errorf("the last row = %v, want the previously last row shifted down", after[len(after)-1])
	}

	f.RemoveRow("Sheet1", 1)
	if got := grid(t, f); !sameGrid(got, before) {
		t.Errorf("insert then remove at the same position changed the grid: %v, want %v", got, before)
	}
}

// The insert-then-remove column identity is not asserted here. Milestone 01
// widened every row with a trailing empty cell on each InsertCol and never
// reclaimed it; this milestone corrects that, so the identity is new
// behaviour and is scored in column-geometry-correction rather than gated.
