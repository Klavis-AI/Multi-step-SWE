// Scored group: col-visibility (milestone 05, "The streaming writer and
// the drawing registry").
//
// Normative source: TEST_INTERFACE.md section 4.05.7 -- "SetColVisible range
// syntax". A range such as "D:F" hides every column in it, a bare name is a
// degenerate range, and an invalid column name is a non-nil error. The v2.0.x
// predecessor accepted only a single column name.
package colvisibility

import (
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

func TestSetColVisibleHidesSingleColumn(t *testing.T) {
	// Use range notation "C:C" — the range-accepting signature is new in v2.1.0;
	// the predecessor accepted only a bare column name.
	f := seedcalc.NewFile()
	if err := f.SetColVisible("Sheet1", "C:C", false); err != nil {
		t.Fatalf("SetColVisible single-column range C:C: %v", err)
	}
	vis, err := f.GetColVisible("Sheet1", "C")
	if err != nil {
		t.Fatalf("GetColVisible: %v", err)
	}
	if vis {
		t.Error("column C should be hidden but GetColVisible returned true")
	}
}

func TestSetColVisibleShowsSingleColumn(t *testing.T) {
	// Use range notation "B:B" for the same reason.
	f := seedcalc.NewFile()
	if err := f.SetColVisible("Sheet1", "B:B", false); err != nil {
		t.Fatalf("SetColVisible hide B:B: %v", err)
	}
	if err := f.SetColVisible("Sheet1", "B:B", true); err != nil {
		t.Fatalf("SetColVisible show B:B: %v", err)
	}
	vis, err := f.GetColVisible("Sheet1", "B")
	if err != nil {
		t.Fatalf("GetColVisible: %v", err)
	}
	if !vis {
		t.Error("column B should be visible after SetColVisible true")
	}
}

func TestSetColVisibleRangeHidesAllColumns(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetColVisible("Sheet1", "D:F", false); err != nil {
		t.Fatalf("SetColVisible range D:F: %v", err)
	}
	for _, col := range []string{"D", "E", "F"} {
		vis, err := f.GetColVisible("Sheet1", col)
		if err != nil {
			t.Fatalf("GetColVisible(%s): %v", col, err)
		}
		if vis {
			t.Errorf("column %s should be hidden after SetColVisible(D:F, false)", col)
		}
	}
}

func TestSetColVisibleRangeDoesNotAffectOutsideColumns(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetColVisible("Sheet1", "D:F", false); err != nil {
		t.Fatalf("SetColVisible range D:F: %v", err)
	}
	for _, col := range []string{"C", "G"} {
		vis, err := f.GetColVisible("Sheet1", col)
		if err != nil {
			t.Fatalf("GetColVisible(%s): %v", col, err)
		}
		if !vis {
			t.Errorf("column %s outside the hidden range D:F should remain visible", col)
		}
	}

	// The range endpoints are validated: an invalid column name is rejected
	// rather than silently hiding nothing.
	if err := f.SetColVisible("Sheet1", "A:*", false); err == nil {
		t.Error("SetColVisible with an invalid range endpoint returned a nil error")
	}
}


