// Scored group: outline-level-validation (milestone 04, new behaviour).
//
// Milestone 03 stored whatever outline level it was given. Milestone 04
// bounds it: SetRowOutlineLevel and SetColOutlineLevel now reject anything
// outside 1-7 and leave the stored level untouched when they do.
//
// This case moved out of rows-and-columns-regression, which is a gate group:
// behaviour a milestone deliberately changes cannot also be the regression it
// is measured against.
//
// ECMA-376 Part 1 clause 18.3.1.73 (row, @outlineLevel) and clause 18.3.1.13
// (col, @outlineLevel) both give the level as ST_OutlineLevel, whose value
// space clause 18.18.63 fixes at 0 to 7; the mutators take 1 as their floor
// because level 0 is the unset state a caller reaches by never setting one.
package outlinelevelvalidation

import (
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

func TestSetRowOutlineLevelRejectsALevelOutsideOneToSeven(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetRowOutlineLevel("Sheet1", 3, 2); err != nil {
		t.Fatalf("SetRowOutlineLevel(3, 2): %v", err)
	}
	for _, level := range []uint8{0, 8, 9, 255} {
		if err := f.SetRowOutlineLevel("Sheet1", 3, level); err == nil {
			t.Errorf("SetRowOutlineLevel(sheet, 3, %d) returned nil error, want a non-nil error", level)
		}
	}
	if got, err := f.GetRowOutlineLevel("Sheet1", 3); err != nil || got != 2 {
		t.Errorf("a rejected level changed the stored one: GetRowOutlineLevel = (%v, %v), want (2, nil)", got, err)
	}
}

func TestSetColOutlineLevelRejectsALevelOutsideOneToSeven(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetColOutlineLevel("Sheet1", "C", 4); err != nil {
		t.Fatalf(`SetColOutlineLevel("C", 4): %v`, err)
	}
	for _, level := range []uint8{0, 8, 9, 255} {
		if err := f.SetColOutlineLevel("Sheet1", "C", level); err == nil {
			t.Errorf("SetColOutlineLevel(sheet, C, %d) returned nil error, want a non-nil error", level)
		}
	}
	if got, err := f.GetColOutlineLevel("Sheet1", "C"); err != nil || got != 4 {
		t.Errorf("a rejected level changed the stored one: GetColOutlineLevel = (%v, %v), want (4, nil)", got, err)
	}
}

func TestTheAcceptedOutlineLevelsAreExactlyOneToSeven(t *testing.T) {
	f := seedcalc.NewFile()
	for level := uint8(1); level <= 7; level++ {
		if err := f.SetRowOutlineLevel("Sheet1", 2, level); err != nil {
			t.Errorf("SetRowOutlineLevel(sheet, 2, %d): %v", level, err)
			continue
		}
		if got, err := f.GetRowOutlineLevel("Sheet1", 2); err != nil || got != level {
			t.Errorf("GetRowOutlineLevel after setting %d = (%v, %v)", level, got, err)
		}
		if err := f.SetColOutlineLevel("Sheet1", "B", level); err != nil {
			t.Errorf("SetColOutlineLevel(sheet, B, %d): %v", level, err)
			continue
		}
		if got, err := f.GetColOutlineLevel("Sheet1", "B"); err != nil || got != level {
			t.Errorf("GetColOutlineLevel after setting %d = (%v, %v)", level, got, err)
		}
	}
	if err := f.SetRowOutlineLevel("Sheet1", 2, 8); err == nil {
		t.Error("level 8 was accepted for a row, so the accepted range is not 1-7")
	}
	if err := f.SetColOutlineLevel("Sheet1", "B", 8); err == nil {
		t.Error("level 8 was accepted for a column, so the accepted range is not 1-7")
	}
}
