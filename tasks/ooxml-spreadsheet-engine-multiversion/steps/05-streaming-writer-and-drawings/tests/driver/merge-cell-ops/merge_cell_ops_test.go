// Gate group: merge-cell-ops (milestone 05, "The streaming writer and
// the drawing registry").
//
// Regression group: MergeCell and GetMergeCells existed in tree-04.
// Tests verify behaviour continues unchanged in tree-05 (particularly the
// MergeCell / GetMergeCells API that UnmergeCell extends).
//
// Normative sources:
//   * TEST_INTERFACE.md section 4.05 -- "Cell merging".
//   * ECMA-376 Part 1, 18.3.1.55 mergeCell (CT_MergeCell) for the ref
//     attribute that names the merged region.
package mergecellopss

import (
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

func TestMergeCellAndGetMergeCells(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.MergeCell("Sheet1", "A1", "C3"); err != nil {
		t.Fatalf("MergeCell: %v", err)
	}
	merged, err := f.GetMergeCells("Sheet1")
	if err != nil {
		t.Fatalf("GetMergeCells: %v", err)
	}
	if len(merged) != 1 {
		t.Fatalf("GetMergeCells = %d, want 1", len(merged))
	}
	if merged[0].GetStartAxis() != "A1" {
		t.Errorf("GetStartAxis = %q, want %q", merged[0].GetStartAxis(), "A1")
	}
	if merged[0].GetEndAxis() != "C3" {
		t.Errorf("GetEndAxis = %q, want %q", merged[0].GetEndAxis(), "C3")
	}
}

func TestMergeMultipleCells(t *testing.T) {
	f := seedcalc.NewFile()
	for _, pair := range [][2]string{
		{"A1", "B2"}, {"D4", "E5"}, {"G7", "H8"},
	} {
		if err := f.MergeCell("Sheet1", pair[0], pair[1]); err != nil {
			t.Fatalf("MergeCell %s:%s: %v", pair[0], pair[1], err)
		}
	}
	merged, err := f.GetMergeCells("Sheet1")
	if err != nil {
		t.Fatalf("GetMergeCells: %v", err)
	}
	if len(merged) != 3 {
		t.Errorf("GetMergeCells = %d, want 3", len(merged))
	}
}

func TestGetMergeCellsOnSheetWithNone(t *testing.T) {
	f := seedcalc.NewFile()
	merged, err := f.GetMergeCells("Sheet1")
	if err != nil {
		t.Fatalf("GetMergeCells on sheet with no merges: %v", err)
	}
	if len(merged) != 0 {
		t.Errorf("GetMergeCells = %d, want 0", len(merged))
	}
}
