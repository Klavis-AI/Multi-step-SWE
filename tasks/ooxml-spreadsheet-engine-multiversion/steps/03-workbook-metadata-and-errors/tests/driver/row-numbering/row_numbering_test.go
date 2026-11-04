// Scored group: row-numbering (milestone 03, "Workbook metadata and the
// error contract").
//
// Normative sources:
//   * TEST_INTERFACE.md section 4.03 -- "1-based row addressing".
//   * ECMA-376 Part 1, 18.3.1.73 (CT_Row/@r): the row index is a 1-based
//     Excel row number.
//   * Cases cross-checked against the upstream test corpus at tag v2.0.1
//     (rows_test.go: TestRowHeight, TestRowVisibility, TestOutlineLevel,
//     TestRemoveRow, TestInsertRow).
package rownumbering

import (
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// defaultRowHeight is the height reported for a row whose height was never
// set, in points.
const defaultRowHeight = 20.0

// seedColumnA writes "A1".."A<n>" down column A of Sheet1.
func seedColumnA(t *testing.T, f *seedcalc.File, n int) {
	t.Helper()
	for i := 1; i <= n; i++ {
		cell, err := seedcalc.CoordinatesToCellName(1, i)
		if err != nil {
			t.Fatalf("CoordinatesToCellName(1, %d): %v", i, err)
		}
		if err := f.SetCellValue("Sheet1", cell, cell); err != nil {
			t.Fatalf("SetCellValue(%q): %v", cell, err)
		}
	}
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

func TestRowHeightIsOneBased(t *testing.T) {
	f := seedcalc.NewFile()

	if h, err := f.GetRowHeight("Sheet1", 1); err != nil || h != defaultRowHeight {
		t.Errorf("GetRowHeight of an untouched row = (%v, %v), want (%v, nil)", h, err, defaultRowHeight)
	}
	if err := f.SetRowHeight("Sheet1", 1, 50); err != nil {
		t.Fatalf("SetRowHeight(1, 50): %v", err)
	}
	if h, err := f.GetRowHeight("Sheet1", 1); err != nil || h != 50 {
		t.Errorf("GetRowHeight(1) = (%v, %v), want (50, nil)", h, err)
	}
	// Row 2 was not touched, and a row past the end is not an error.
	if h, err := f.GetRowHeight("Sheet1", 2); err != nil || h != defaultRowHeight {
		t.Errorf("GetRowHeight(2) = (%v, %v), want (%v, nil)", h, err, defaultRowHeight)
	}
	if h, err := f.GetRowHeight("Sheet1", 500); err != nil || h != defaultRowHeight {
		t.Errorf("GetRowHeight(500) = (%v, %v), want (%v, nil)", h, err, defaultRowHeight)
	}
	// Row 0 does not exist in the 1-based numbering.
	if h, err := f.GetRowHeight("Sheet1", 0); err == nil || h != defaultRowHeight {
		t.Errorf("GetRowHeight(0) = (%v, %v), want (%v, non-nil)", h, err, defaultRowHeight)
	}
	if err := f.SetRowHeight("Sheet1", 0, 50); err == nil {
		t.Error("SetRowHeight(0, 50) returned nil, want a non-nil error")
	}
}

func TestRowVisibilityIsOneBased(t *testing.T) {
	f := seedcalc.NewFile()
	seedColumnA(t, f, 3)

	if err := f.SetRowVisible("Sheet1", 2, false); err != nil {
		t.Fatalf("SetRowVisible(2, false): %v", err)
	}
	if v, err := f.GetRowVisible("Sheet1", 2); err != nil || v {
		t.Errorf("GetRowVisible(2) = (%v, %v), want (false, nil)", v, err)
	}
	if v, err := f.GetRowVisible("Sheet1", 1); err != nil || !v {
		t.Errorf("GetRowVisible(1) = (%v, %v), want (true, nil)", v, err)
	}
	if err := f.SetRowVisible("Sheet1", 2, true); err != nil {
		t.Fatalf("SetRowVisible(2, true): %v", err)
	}
	if v, err := f.GetRowVisible("Sheet1", 2); err != nil || !v {
		t.Errorf("GetRowVisible(2) after re-showing = (%v, %v), want (true, nil)", v, err)
	}
	// Past the last row: not visible, but not an error either.
	if v, err := f.GetRowVisible("Sheet1", 500); err != nil || v {
		t.Errorf("GetRowVisible(500) = (%v, %v), want (false, nil)", v, err)
	}
	if v, err := f.GetRowVisible("Sheet1", 0); err == nil || v {
		t.Errorf("GetRowVisible(0) = (%v, %v), want (false, non-nil)", v, err)
	}
}

func TestRowOutlineLevelIsOneBased(t *testing.T) {
	f := seedcalc.NewFile()

	if err := f.SetRowOutlineLevel("Sheet1", 2, 7); err != nil {
		t.Fatalf("SetRowOutlineLevel(2, 7): %v", err)
	}
	if lvl, err := f.GetRowOutlineLevel("Sheet1", 2); err != nil || lvl != 7 {
		t.Errorf("GetRowOutlineLevel(2) = (%d, %v), want (7, nil)", lvl, err)
	}
	if lvl, err := f.GetRowOutlineLevel("Sheet1", 1); err != nil || lvl != 0 {
		t.Errorf("GetRowOutlineLevel(1) = (%d, %v), want (0, nil)", lvl, err)
	}
	if lvl, err := f.GetRowOutlineLevel("Sheet1", 500); err != nil || lvl != 0 {
		t.Errorf("GetRowOutlineLevel(500) = (%d, %v), want (0, nil)", lvl, err)
	}
	if lvl, err := f.GetRowOutlineLevel("Sheet1", 0); err == nil || lvl != 0 {
		t.Errorf("GetRowOutlineLevel(0) = (%d, %v), want (0, non-nil)", lvl, err)
	}
}

func TestRemoveRowIsOneBased(t *testing.T) {
	f := seedcalc.NewFile()
	seedColumnA(t, f, 5)

	if err := f.RemoveRow("Sheet1", 1); err != nil {
		t.Fatalf("RemoveRow(1): %v", err)
	}
	got := columnA(t, f)
	want := []string{"A2", "A3", "A4", "A5"}
	if len(got) != len(want) {
		t.Fatalf("after RemoveRow(1), column A = %q, want %q", got, want)
	}
	for i := range want {
		if got[i] != want[i] {
			t.Fatalf("after RemoveRow(1), column A = %q, want %q", got, want)
		}
	}

	// Removing a row past the end is a no-op, not an error.
	if err := f.RemoveRow("Sheet1", 500); err != nil {
		t.Errorf("RemoveRow(500) = %v, want nil", err)
	}
	if err := f.RemoveRow("Sheet1", 0); err == nil {
		t.Error("RemoveRow(0) returned nil, want a non-nil error")
	}
	if err := f.RemoveRow("Sheet1", -1); err == nil {
		t.Error("RemoveRow(-1) returned nil, want a non-nil error")
	}
}

func TestInsertRowIsOneBased(t *testing.T) {
	f := seedcalc.NewFile()
	seedColumnA(t, f, 3)

	if err := f.InsertRow("Sheet1", 1); err != nil {
		t.Fatalf("InsertRow(1): %v", err)
	}
	got := columnA(t, f)
	want := []string{"", "A1", "A2", "A3"}
	if len(got) != len(want) {
		t.Fatalf("after InsertRow(1), column A = %q, want %q", got, want)
	}
	for i := range want {
		if got[i] != want[i] {
			t.Fatalf("after InsertRow(1), column A = %q, want %q", got, want)
		}
	}

	if err := f.InsertRow("Sheet1", 0); err == nil {
		t.Error("InsertRow(0) returned nil, want a non-nil error")
	}
	if err := f.InsertRow("Sheet1", -1); err == nil {
		t.Error("InsertRow(-1) returned nil, want a non-nil error")
	}
}

func TestRowAddressingRejectsNonPositiveRows(t *testing.T) {
	calls := map[string]func(f *seedcalc.File, row int) error{
		"SetRowHeight":       func(f *seedcalc.File, r int) error { return f.SetRowHeight("Sheet1", r, 25) },
		"SetRowVisible":      func(f *seedcalc.File, r int) error { return f.SetRowVisible("Sheet1", r, false) },
		"SetRowOutlineLevel": func(f *seedcalc.File, r int) error { return f.SetRowOutlineLevel("Sheet1", r, 1) },
		"RemoveRow":          func(f *seedcalc.File, r int) error { return f.RemoveRow("Sheet1", r) },
		"InsertRow":          func(f *seedcalc.File, r int) error { return f.InsertRow("Sheet1", r) },
		"GetRowHeight":       func(f *seedcalc.File, r int) error { _, err := f.GetRowHeight("Sheet1", r); return err },
		"GetRowVisible":      func(f *seedcalc.File, r int) error { _, err := f.GetRowVisible("Sheet1", r); return err },
		"GetRowOutlineLevel": func(f *seedcalc.File, r int) error { _, err := f.GetRowOutlineLevel("Sheet1", r); return err },
	}
	f := seedcalc.NewFile()
	seedColumnA(t, f, 3)
	for name, call := range calls {
		for _, row := range []int{0, -1, -100} {
			if err := call(f, row); err == nil {
				t.Errorf("%s with row %d returned nil, want a non-nil error", name, row)
			}
		}
		// Row 1 is the first legal row.
		if err := call(f, 1); err != nil {
			t.Errorf("%s with row 1 returned %v, want nil", name, err)
		}
	}
}
