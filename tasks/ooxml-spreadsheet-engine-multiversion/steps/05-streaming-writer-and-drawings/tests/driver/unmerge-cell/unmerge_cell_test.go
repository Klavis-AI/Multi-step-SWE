// Scored group: unmerge-cell (milestone 05, "The streaming writer and
// the drawing registry").
//
// Normative sources:
//   * TEST_INTERFACE.md section 4.05 -- "Unmerging cells".
//   * merge.go UnmergeCell: silently returns nil when MergeCells == nil;
//     removes all merged regions that overlap the given area.
package unmergecell

import (
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

func TestUnmergeCellOnSheetWithNoMergesIsNoop(t *testing.T) {
	f := seedcalc.NewFile()
	// No merges exist — must return nil without panicking.
	if err := f.UnmergeCell("Sheet1", "A1", "B2"); err != nil {
		t.Fatalf("UnmergeCell on a sheet with no merges: %v", err)
	}
}

func TestUnmergeCellRemovesOverlappingRegion(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.MergeCell("Sheet1", "A1", "C3"); err != nil {
		t.Fatalf("MergeCell: %v", err)
	}
	merged, err := f.GetMergeCells("Sheet1")
	if err != nil {
		t.Fatalf("GetMergeCells: %v", err)
	}
	if len(merged) != 1 {
		t.Fatalf("expected 1 merged region, got %d", len(merged))
	}

	if err := f.UnmergeCell("Sheet1", "A1", "C3"); err != nil {
		t.Fatalf("UnmergeCell: %v", err)
	}
	after, err := f.GetMergeCells("Sheet1")
	if err != nil {
		t.Fatalf("GetMergeCells after UnmergeCell: %v", err)
	}
	if len(after) != 0 {
		t.Errorf("expected 0 merged regions after UnmergeCell, got %d: %v", len(after), after)
	}
}

func TestUnmergeCellLeavesNonOverlappingRegions(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.MergeCell("Sheet1", "A1", "B2"); err != nil {
		t.Fatalf("MergeCell A1:B2: %v", err)
	}
	if err := f.MergeCell("Sheet1", "D4", "E5"); err != nil {
		t.Fatalf("MergeCell D4:E5: %v", err)
	}

	// Unmerge only the D4:E5 area.
	if err := f.UnmergeCell("Sheet1", "D4", "E5"); err != nil {
		t.Fatalf("UnmergeCell D4:E5: %v", err)
	}
	after, err := f.GetMergeCells("Sheet1")
	if err != nil {
		t.Fatalf("GetMergeCells: %v", err)
	}
	if len(after) != 1 {
		t.Errorf("expected 1 merged region remaining (A1:B2), got %d: %v", len(after), after)
	} else if after[0].GetStartAxis() != "A1" {
		t.Errorf("remaining region starts at %s, want A1", after[0].GetStartAxis())
	}
}

func TestUnmergeCellInvalidSheetReturnsError(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.UnmergeCell("NoSuchSheet", "A1", "B2"); err == nil {
		t.Error("UnmergeCell on a non-existent sheet must return an error")
	}
}
