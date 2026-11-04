// Gate group: rows-and-columns-regression.
//
// Row and column geometry -- height/width, visibility, outline level, and the
// row shift on insert/remove -- is unchanged by milestone 04's own additions
// (pivot caches, sparklines, the vbaProject part). These cases exist to catch
// a regression introduced while wiring that new surface in, not to document
// anything new, which is what makes them a veto rather than a score.
//
// The one thing here that milestone 04 does change is not asserted: this
// milestone starts rejecting an outline level outside 1-7, where milestone 03
// accepted anything. That is scored in outline-level-validation.
//
// Behaviour measured directly against the Oracle sources at this milestone's
// checkpoint (tree-04): rows.go (SetRowHeight/GetRowHeight,
// SetRowVisible/GetRowVisible, SetRowOutlineLevel/GetRowOutlineLevel,
// InsertRow/RemoveRow, adjustRowDimensions) and col.go
// (SetColWidth/GetColWidth, SetColVisible/GetColVisible,
// SetColOutlineLevel/GetColOutlineLevel, convertColWidthToPixels).
package rowsandcolumnsregression

import (
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// TestRowHeightDefaultAndRoundTrip checks that a row with no explicit height
// reports the fixed default of 20, that SetRowHeight/GetRowHeight round-trip
// an explicit value, and that an invalid (< 1) row number is rejected by
// both.
func TestRowHeightDefaultAndRoundTrip(t *testing.T) {
	f := seedcalc.NewFile()
	if got, err := f.GetRowHeight("Sheet1", 5); err != nil || got != 20 {
		t.Errorf("GetRowHeight on an unset row = (%v, %v), want (20, nil)", got, err)
	}
	if err := f.SetRowHeight("Sheet1", 2, 30); err != nil {
		t.Fatalf("SetRowHeight: %v", err)
	}
	if got, err := f.GetRowHeight("Sheet1", 2); err != nil || got != 30 {
		t.Errorf("GetRowHeight after SetRowHeight(2, 30) = (%v, %v), want (30, nil)", got, err)
	}
	if _, err := f.GetRowHeight("Sheet1", 0); err == nil {
		t.Error("GetRowHeight(sheet, 0) returned nil error, want a non-nil error")
	}
	if err := f.SetRowHeight("Sheet1", 0, 30); err == nil {
		t.Error("SetRowHeight(sheet, 0, ...) returned nil error, want a non-nil error")
	}
}

// TestRowVisibilityAndOutlineLevel checks GetRowVisible's two different
// defaults -- false for a row past the end of the sheet's row data, true for
// an existing row whose Hidden flag was never set -- and that Set/Get
// round-trip both visibility and outline level. Whether a level outside 1-7
// is rejected is this milestone's own change, scored elsewhere.
func TestRowVisibilityAndOutlineLevel(t *testing.T) {
	f := seedcalc.NewFile()
	// A row past the sheet's row data reports false, not true: rows.go's
	// bounds check ("row > len(SheetData.Row)") short-circuits to false
	// before the Hidden field is even consulted. This is an asymmetry with
	// GetColVisible, whose default is unconditionally true.
	if got, err := f.GetRowVisible("Sheet1", 3); err != nil || got {
		t.Errorf("GetRowVisible on a row past the sheet's row data = (%v, %v), want (false, nil)", got, err)
	}
	// Materialising row 3 (Hidden still unset) makes it report visible.
	if err := f.SetRowHeight("Sheet1", 3, 20); err != nil {
		t.Fatalf("SetRowHeight: %v", err)
	}
	if got, err := f.GetRowVisible("Sheet1", 3); err != nil || !got {
		t.Errorf("GetRowVisible on an existing, not-hidden row = (%v, %v), want (true, nil)", got, err)
	}
	if err := f.SetRowVisible("Sheet1", 3, false); err != nil {
		t.Fatalf("SetRowVisible: %v", err)
	}
	if got, err := f.GetRowVisible("Sheet1", 3); err != nil || got {
		t.Errorf("GetRowVisible after SetRowVisible(3, false) = (%v, %v), want (false, nil)", got, err)
	}

	if got, err := f.GetRowOutlineLevel("Sheet1", 3); err != nil || got != 0 {
		t.Errorf("GetRowOutlineLevel on an unset row = (%v, %v), want (0, nil)", got, err)
	}
	if err := f.SetRowOutlineLevel("Sheet1", 3, 2); err != nil {
		t.Fatalf("SetRowOutlineLevel: %v", err)
	}
	if got, err := f.GetRowOutlineLevel("Sheet1", 3); err != nil || got != 2 {
		t.Errorf("GetRowOutlineLevel after SetRowOutlineLevel(3, 2) = (%v, %v), want (2, nil)", got, err)
	}
}

// TestColWidthDefaultAndRoundTrip checks that a column with no explicit
// width reports the fixed default of 64, that SetColWidth/GetColWidth
// round-trip an explicit value, and that setting the width to exactly 0
// still reads back as the default -- GetColWidth only returns a stored width
// when it is non-zero, per the Oracle's own "optimisation" comment.
func TestColWidthDefaultAndRoundTrip(t *testing.T) {
	f := seedcalc.NewFile()
	if got, err := f.GetColWidth("Sheet1", "D"); err != nil || got != 64 {
		t.Errorf("GetColWidth on an unset column = (%v, %v), want (64, nil)", got, err)
	}
	if err := f.SetColWidth("Sheet1", "D", "D", 20); err != nil {
		t.Fatalf("SetColWidth: %v", err)
	}
	if got, err := f.GetColWidth("Sheet1", "D"); err != nil || got != 20 {
		t.Errorf("GetColWidth after SetColWidth(D, D, 20) = (%v, %v), want (20, nil)", got, err)
	}
	if err := f.SetColWidth("Sheet1", "E", "E", 0); err != nil {
		t.Fatalf("SetColWidth(E, E, 0): %v", err)
	}
	if got, err := f.GetColWidth("Sheet1", "E"); err != nil || got != 64 {
		t.Errorf("GetColWidth after SetColWidth(E, E, 0) = (%v, %v), want the default (64, nil)", got, err)
	}
}

// TestColVisibilityAndOutlineLevel checks that a new column defaults to
// visible with outline level 0 and that Set/Get round-trip both. Whether a
// level outside 1-7 is rejected is this milestone's own change, scored
// elsewhere.
func TestColVisibilityAndOutlineLevel(t *testing.T) {
	f := seedcalc.NewFile()
	if got, err := f.GetColVisible("Sheet1", "C"); err != nil || !got {
		t.Errorf("GetColVisible on an unset column = (%v, %v), want (true, nil)", got, err)
	}
	if err := f.SetColVisible("Sheet1", "C", false); err != nil {
		t.Fatalf("SetColVisible: %v", err)
	}
	if got, err := f.GetColVisible("Sheet1", "C"); err != nil || got {
		t.Errorf("GetColVisible after SetColVisible(C, false) = (%v, %v), want (false, nil)", got, err)
	}

	if got, err := f.GetColOutlineLevel("Sheet1", "C"); err != nil || got != 0 {
		t.Errorf("GetColOutlineLevel on an unset column = (%v, %v), want (0, nil)", got, err)
	}
	if err := f.SetColOutlineLevel("Sheet1", "C", 4); err != nil {
		t.Fatalf("SetColOutlineLevel: %v", err)
	}
	if got, err := f.GetColOutlineLevel("Sheet1", "C"); err != nil || got != 4 {
		t.Errorf("GetColOutlineLevel after SetColOutlineLevel(C, 4) = (%v, %v), want (4, nil)", got, err)
	}
}

// TestInsertRowThenRemoveRowRestoresValues checks that InsertRow shifts every
// row at or after the insertion point down by one, leaving a blank row
// behind, and that RemoveRow on that blank row shifts everything back up,
// restoring the original values.
func TestInsertRowThenRemoveRowRestoresValues(t *testing.T) {
	f := seedcalc.NewFile()
	f.SetCellValue("Sheet1", "A1", "one")
	f.SetCellValue("Sheet1", "A2", "two")
	f.SetCellValue("Sheet1", "A3", "three")

	cell := func(axis string) string {
		t.Helper()
		v, err := f.GetCellValue("Sheet1", axis)
		if err != nil {
			t.Fatalf("GetCellValue(%s): %v", axis, err)
		}
		return v
	}

	if err := f.InsertRow("Sheet1", 2); err != nil {
		t.Fatalf("InsertRow: %v", err)
	}
	if got := cell("A1"); got != "one" {
		t.Errorf("A1 after InsertRow(2) = %q, want %q", got, "one")
	}
	if got := cell("A2"); got != "" {
		t.Errorf("A2 after InsertRow(2) = %q, want the new blank row \"\"", got)
	}
	if got := cell("A3"); got != "two" {
		t.Errorf("A3 after InsertRow(2) = %q, want the shifted-down %q", got, "two")
	}
	if got := cell("A4"); got != "three" {
		t.Errorf("A4 after InsertRow(2) = %q, want the shifted-down %q", got, "three")
	}

	if err := f.RemoveRow("Sheet1", 2); err != nil {
		t.Fatalf("RemoveRow: %v", err)
	}
	if got := cell("A1"); got != "one" {
		t.Errorf("A1 after RemoveRow(2) = %q, want %q", got, "one")
	}
	if got := cell("A2"); got != "two" {
		t.Errorf("A2 after RemoveRow(2) = %q, want the restored %q", got, "two")
	}
	if got := cell("A3"); got != "three" {
		t.Errorf("A3 after RemoveRow(2) = %q, want the restored %q", got, "three")
	}
}
