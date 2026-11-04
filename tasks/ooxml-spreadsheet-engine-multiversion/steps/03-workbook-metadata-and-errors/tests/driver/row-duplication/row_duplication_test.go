// Scored group: row-duplication (milestone 03, "Workbook metadata and the
// error contract").
//
// Normative sources:
//   * TEST_INTERFACE.md section 4.03 -- "Row duplication".
//   * Cases cross-checked against the upstream test corpus at tag v2.0.1
//     (rows_test.go: TestDuplicateRow, TestDuplicateRowTo,
//     TestDuplicateRowInvalidRownum, TestDuplicateRowZeroWithNoRows).
package rowduplication

import (
	"bytes"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// seedColumnA writes "A1".."A<n>" down column A of Sheet1.
func seedColumnA(t *testing.T, n int) *seedcalc.File {
	t.Helper()
	f := seedcalc.NewFile()
	for i := 1; i <= n; i++ {
		cell, err := seedcalc.CoordinatesToCellName(1, i)
		if err != nil {
			t.Fatalf("CoordinatesToCellName(1, %d): %v", i, err)
		}
		if err := f.SetCellValue("Sheet1", cell, cell); err != nil {
			t.Fatalf("SetCellValue(%q): %v", cell, err)
		}
	}
	return f
}

// columnA returns the value of column A for each row of Sheet1.
func columnA(t *testing.T, f *seedcalc.File) []string {
	t.Helper()
	rows, err := f.GetRows("Sheet1")
	if err != nil {
		t.Fatalf("GetRows: %v", err)
	}
	out := make([]string, len(rows))
	for i, r := range rows {
		if len(r) > 0 {
			out[i] = r[0]
		}
	}
	return out
}

func assertColumnA(t *testing.T, f *seedcalc.File, what string, want []string) {
	t.Helper()
	got := columnA(t, f)
	if len(got) != len(want) {
		t.Fatalf("%s: column A = %q (%d rows), want %q (%d rows)", what, got, len(got), want, len(want))
	}
	for i := range want {
		if got[i] != want[i] {
			t.Fatalf("%s: column A = %q, want %q", what, got, want)
		}
	}
}

func TestDuplicateRowInsertsBelow(t *testing.T) {
	f := seedColumnA(t, 3)
	if err := f.DuplicateRow("Sheet1", 2); err != nil {
		t.Fatalf("DuplicateRow(2): %v", err)
	}
	assertColumnA(t, f, "DuplicateRow(2) on three rows", []string{"A1", "A2", "A2", "A3"})

	// Duplicating the last row appends one row.
	g := seedColumnA(t, 2)
	if err := g.DuplicateRow("Sheet1", 2); err != nil {
		t.Fatalf("DuplicateRow(2): %v", err)
	}
	assertColumnA(t, g, "DuplicateRow of the last row", []string{"A1", "A2", "A2"})
}

func TestDuplicateRowToEarlierRow(t *testing.T) {
	f := seedColumnA(t, 4)
	if err := f.DuplicateRowTo("Sheet1", 4, 2); err != nil {
		t.Fatalf("DuplicateRowTo(4, 2): %v", err)
	}
	assertColumnA(t, f, "DuplicateRowTo(4, 2) on four rows", []string{"A1", "A4", "A2", "A3", "A4"})

	g := seedColumnA(t, 4)
	if err := g.DuplicateRowTo("Sheet1", 3, 1); err != nil {
		t.Fatalf("DuplicateRowTo(3, 1): %v", err)
	}
	assertColumnA(t, g, "DuplicateRowTo(3, 1) on four rows", []string{"A3", "A1", "A2", "A3", "A4"})
}

func TestDuplicateRowToLaterRow(t *testing.T) {
	f := seedColumnA(t, 4)
	if err := f.DuplicateRowTo("Sheet1", 1, 3); err != nil {
		t.Fatalf("DuplicateRowTo(1, 3): %v", err)
	}
	assertColumnA(t, f, "DuplicateRowTo(1, 3) on four rows", []string{"A1", "A2", "A1", "A3", "A4"})

	// A target beyond the last row leaves the rows in between empty.
	g := seedColumnA(t, 4)
	if err := g.DuplicateRowTo("Sheet1", 2, 7); err != nil {
		t.Fatalf("DuplicateRowTo(2, 7): %v", err)
	}
	assertColumnA(t, g, "DuplicateRowTo(2, 7) on four rows",
		[]string{"A1", "A2", "A3", "A4", "", "", "A2"})
}

func TestDuplicateRowIsNoOpWhenTargetIsUnusable(t *testing.T) {
	unchanged := []string{"A1", "A2", "A3", "A4"}

	cases := []struct {
		what       string
		row, row2  int
	}{
		{"source equals target", 2, 2},
		{"target row is zero", 2, 0},
		{"target row is negative", 2, -3},
		{"source past the last row", 8, 2},
	}
	for _, c := range cases {
		f := seedColumnA(t, 4)
		if err := f.DuplicateRowTo("Sheet1", c.row, c.row2); err != nil {
			t.Errorf("DuplicateRowTo(%d, %d) (%s) = %v, want nil", c.row, c.row2, c.what, err)
			continue
		}
		assertColumnA(t, f, c.what, unchanged)
	}

	// An empty worksheet has no row to duplicate; that is not an error.
	empty := seedcalc.NewFile()
	if err := empty.DuplicateRow("Sheet1", 1); err != nil {
		t.Errorf("DuplicateRow(1) on an empty worksheet = %v, want nil", err)
	}
}

func TestDuplicateRowRejectsNonPositiveSourceRow(t *testing.T) {
	f := seedColumnA(t, 4)
	for _, row := range []int{0, -1, -100} {
		if err := f.DuplicateRow("Sheet1", row); err == nil {
			t.Errorf("DuplicateRow(%d) returned nil, want a non-nil error", row)
		}
		if err := f.DuplicateRowTo("Sheet1", row, 2); err == nil {
			t.Errorf("DuplicateRowTo(%d, 2) returned nil, want a non-nil error", row)
		}
	}
	assertColumnA(t, f, "after rejected duplications", []string{"A1", "A2", "A3", "A4"})
}

func TestDuplicatedRowKeepsStyleAcrossASaveAndReopen(t *testing.T) {
	f := seedColumnA(t, 3)
	style, err := f.NewStyle(`{"font":{"bold":true}}`)
	if err != nil {
		t.Fatalf("NewStyle: %v", err)
	}
	if style == 0 {
		t.Fatal("NewStyle returned style index 0")
	}
	if err := f.SetCellStyle("Sheet1", "A2", "A2", style); err != nil {
		t.Fatalf("SetCellStyle: %v", err)
	}
	if err := f.DuplicateRowTo("Sheet1", 2, 5); err != nil {
		t.Fatalf("DuplicateRowTo(2, 5): %v", err)
	}

	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	g, err := seedcalc.OpenReader(bytes.NewReader(buf.Bytes()))
	if err != nil {
		t.Fatalf("OpenReader: %v", err)
	}
	if v, err := g.GetCellValue("Sheet1", "A5"); err != nil || v != "A2" {
		t.Errorf("A5 after reopen = (%q, %v), want (\"A2\", nil)", v, err)
	}
	got, err := g.GetCellStyle("Sheet1", "A5")
	if err != nil {
		t.Fatalf("GetCellStyle(A5): %v", err)
	}
	if got != style {
		t.Errorf("style of the duplicated cell A5 = %d, want %d (the style of the source cell)", got, style)
	}
}
