// Scored group: column-geometry-correction (milestone 02, new behaviour).
//
// Milestone 01 widened every row by one trailing empty cell each time a
// column was inserted, and never reclaimed it: inserting at B on a
// three-column sheet produced five columns, and removing the inserted
// column left four. Milestone 02 corrects that, so InsertCol and RemoveCol
// are finally inverses and the grid a caller reads back has exactly the
// width it should.
//
// The inverse case moved here out of rows-and-columns-regression, which is
// a gate group: behaviour a milestone deliberately changes cannot also be
// the regression it is measured against.
//
// Graded against ECMA-376 Part 1 clause 18.3.1.73 (row and its spans
// attribute) and clause 18.3.1.4 (c, and the r attribute that fixes a
// cell's column).
package columngeometrycorrection

import (
	"bytes"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// base is a three-column, two-row sheet with one gap, so that a shift is
// visible in both the values and the row widths.
func base(t *testing.T) *seedcalc.File {
	t.Helper()
	f := seedcalc.NewFile()
	f.SetCellValue("Sheet1", "A1", "a1")
	f.SetCellValue("Sheet1", "B1", "b1")
	f.SetCellValue("Sheet1", "C1", "c1")
	f.SetCellValue("Sheet1", "A2", "a2")
	f.SetCellValue("Sheet1", "C2", "c2")
	return f
}

func widths(rows [][]string) []int {
	w := make([]int, len(rows))
	for i, r := range rows {
		w[i] = len(r)
	}
	return w
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

func TestInsertColNoLongerAppendsATrailingEmptyCell(t *testing.T) {
	f := base(t)
	f.InsertCol("Sheet1", "B")
	rows := f.GetRows("Sheet1")
	for i, r := range rows {
		if len(r) != 4 {
			t.Errorf("row %d has %d cells after one insert into a three-column sheet, want 4: %v", i+1, len(r), r)
		}
	}
	if len(rows) != 2 {
		t.Fatalf("InsertCol changed the row count to %d, want 2", len(rows))
	}
	if got := rows[0]; got[0] != "a1" || got[1] != "" || got[2] != "b1" || got[3] != "c1" {
		t.Errorf("row 1 = %v, want [a1  b1 c1]", got)
	}
}

func TestInsertAndRemoveColumnAreInverses(t *testing.T) {
	f := base(t)
	before := f.GetRows("Sheet1")

	f.InsertCol("Sheet1", "A")
	after := f.GetRows("Sheet1")
	if len(after) != len(before) {
		t.Fatalf("InsertCol changed the row count from %d to %d", len(before), len(after))
	}
	if after[0][0] != "" {
		t.Errorf("the inserted column is not empty: %v", after[0])
	}
	if after[0][1] != "a1" {
		t.Errorf("row 1 = %v, want the previous contents shifted one column right", after[0])
	}

	f.RemoveCol("Sheet1", "A")
	if got := f.GetRows("Sheet1"); !sameGrid(got, before) {
		t.Errorf("insert then remove of the same column changed the grid: %v, want %v", got, before)
	}
}

func TestInsertingBeforeTheFirstColumnKeepsTheRowWidthExact(t *testing.T) {
	f := base(t)
	f.InsertCol("Sheet1", "A")
	rows := f.GetRows("Sheet1")
	if got := widths(rows); len(got) != 2 || got[0] != 4 || got[1] != 4 {
		t.Errorf("row widths after an insert at A = %v, want [4 4]", got)
	}
	for _, c := range []struct{ axis, want string }{
		{"A1", ""}, {"B1", "a1"}, {"C1", "b1"}, {"D1", "c1"},
		{"A2", ""}, {"B2", "a2"}, {"D2", "c2"},
	} {
		if got := f.GetCellValue("Sheet1", c.axis); got != c.want {
			t.Errorf("%s = %q, want %q", c.axis, got, c.want)
		}
	}
}

func TestRepeatedInsertsDoNotAccumulateTrailingCells(t *testing.T) {
	f := base(t)
	for i := 0; i < 3; i++ {
		f.InsertCol("Sheet1", "B")
	}
	rows := f.GetRows("Sheet1")
	if got := widths(rows); len(got) != 2 || got[0] != 6 || got[1] != 6 {
		t.Errorf("row widths after three inserts into a three-column sheet = %v, want [6 6]", got)
	}
	if got := rows[0]; got[0] != "a1" || got[1] != "" || got[2] != "" || got[3] != "" || got[4] != "b1" || got[5] != "c1" {
		t.Errorf("row 1 = %v, want [a1    b1 c1]", got)
	}
}

func TestTheCorrectedWidthSurvivesAWriteAndRead(t *testing.T) {
	f := base(t)
	before := f.GetRows("Sheet1")
	f.InsertCol("Sheet1", "B")
	f.RemoveCol("Sheet1", "B")

	buf, err := f.WriteToBuffer()
	if err != nil {
		t.Fatalf("WriteToBuffer: %v", err)
	}
	g, err := seedcalc.OpenReader(bytes.NewReader(buf.Bytes()))
	if err != nil {
		t.Fatalf("OpenReader: %v", err)
	}
	if got := g.GetRows("Sheet1"); !sameGrid(got, before) {
		t.Errorf("the reopened grid is %v, want %v", got, before)
	}
}
