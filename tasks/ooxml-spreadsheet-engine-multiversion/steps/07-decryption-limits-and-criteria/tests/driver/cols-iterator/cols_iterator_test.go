// Scored group: cols-iterator.
//
// The Cols column iterator added at this milestone: Cols() returns a *Cols
// that advances via Next/Rows, GetCols returns the whole sheet as a
// [][]string slice. Behaviour follows the streaming column model introduced
// at v2.3.2.
package colsiterator

import (
	"bytes"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// populate fills a 3×3 grid.
//
//	     A     B     C
//	1    a     b     c
//	2    1     2     3
//	3    x     y     z
func populate(t *testing.T) *seedcalc.File {
	t.Helper()
	f := seedcalc.NewFile()
	data := [][]interface{}{
		{"a", "b", "c"},
		{1, 2, 3},
		{"x", "y", "z"},
	}
	for i, row := range data {
		cell, _ := seedcalc.CoordinatesToCellName(1, i+1)
		if err := f.SetSheetRow("Sheet1", cell, &row); err != nil {
			t.Fatalf("SetSheetRow row %d: %v", i+1, err)
		}
	}
	return f
}

func TestGetColsReturnsAllColumns(t *testing.T) {
	f := populate(t)
	cols, err := f.GetCols("Sheet1")
	if err != nil {
		t.Fatalf("GetCols: %v", err)
	}
	// Three columns: A, B, C.
	if len(cols) != 3 {
		t.Fatalf("GetCols returned %d columns, want 3", len(cols))
	}
	// Column A should be ["a", "1", "x"].
	if len(cols[0]) != 3 {
		t.Fatalf("column A has %d rows, want 3", len(cols[0]))
	}
	if cols[0][0] != "a" || cols[0][1] != "1" || cols[0][2] != "x" {
		t.Fatalf("column A = %v, want [a 1 x]", cols[0])
	}
	// Column C should be ["c", "3", "z"].
	if cols[2][0] != "c" || cols[2][1] != "3" || cols[2][2] != "z" {
		t.Fatalf("column C = %v, want [c 3 z]", cols[2])
	}
}

func TestColsIteratorTraversesEachColumn(t *testing.T) {
	f := populate(t)
	it, err := f.Cols("Sheet1")
	if err != nil {
		t.Fatalf("Cols: %v", err)
	}
	// Asserting the contents, not merely the count: an iterator that walked
	// rows instead of columns, or visited the columns out of order, would
	// still report three of them.
	want := [][]string{
		{"a", "1", "x"},
		{"b", "2", "y"},
		{"c", "3", "z"},
	}
	count := 0
	for it.Next() {
		rows, err := it.Rows()
		if err != nil {
			t.Fatalf("Rows() at column %d: %v", count+1, err)
		}
		if count >= len(want) {
			t.Fatalf("iterator visited more than %d columns", len(want))
		}
		if len(rows) != len(want[count]) {
			t.Fatalf("column %d: Rows() returned %d rows, want %d", count+1, len(rows), len(want[count]))
		}
		for i, cell := range want[count] {
			if rows[i] != cell {
				t.Fatalf("column %d = %v, want %v", count+1, rows, want[count])
			}
		}
		count++
	}
	if err := it.Error(); err != nil {
		t.Fatalf("Cols iterator error: %v", err)
	}
	if count != 3 {
		t.Fatalf("iterator visited %d columns, want 3", count)
	}
}

func TestGetColsOnEmptySheetReturnsEmptySlice(t *testing.T) {
	f := seedcalc.NewFile()
	cols, err := f.GetCols("Sheet1")
	if err != nil {
		t.Fatalf("GetCols on empty sheet: %v", err)
	}
	if len(cols) != 0 {
		t.Fatalf("GetCols on empty sheet returned %d columns, want 0", len(cols))
	}
}

func TestGetColsOnAbsentSheetIsAnError(t *testing.T) {
	f := seedcalc.NewFile()
	_, err := f.GetCols("NoSuch")
	if err == nil {
		t.Fatal("GetCols on absent sheet returned no error")
	}
}

func TestColsIteratorNextReturnsFalseOnAbsentSheet(t *testing.T) {
	f := seedcalc.NewFile()
	it, err := f.Cols("NoSuch")
	if err == nil && it != nil && it.Next() {
		t.Fatal("Cols iterator on absent sheet advanced; expected no columns")
	}
	// Either Cols itself errors or the iterator has no columns — both are acceptable.
}

func TestGetColsSurvivesRoundTrip(t *testing.T) {
	f := populate(t)
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	g, err := seedcalc.OpenReader(bytes.NewReader(buf.Bytes()))
	if err != nil {
		t.Fatalf("OpenReader: %v", err)
	}
	cols, err := g.GetCols("Sheet1")
	if err != nil {
		t.Fatalf("GetCols after round trip: %v", err)
	}
	if len(cols) != 3 {
		t.Fatalf("GetCols after round trip returned %d columns, want 3", len(cols))
	}
	if cols[1][0] != "b" {
		t.Fatalf("column B row 1 after round trip = %q, want %q", cols[1][0], "b")
	}
}

func TestBothEntryPointsNameTheSheetCaseSensitively(t *testing.T) {
	// "sheet1" is not "Sheet1": the sheet name is matched exactly, so the
	// wrong case is an absent sheet rather than a near miss that resolves.
	f := populate(t)
	if _, err := f.GetCols("sheet1"); err == nil {
		t.Fatal("GetCols(\"sheet1\") resolved; the sheet name is matched case-sensitively")
	}
	if _, err := f.Cols("sheet1"); err == nil {
		t.Fatal("Cols(\"sheet1\") resolved; the sheet name is matched case-sensitively")
	}
	if _, err := f.GetCols("Sheet1"); err != nil {
		t.Fatalf("GetCols(\"Sheet1\"): %v", err)
	}
}
