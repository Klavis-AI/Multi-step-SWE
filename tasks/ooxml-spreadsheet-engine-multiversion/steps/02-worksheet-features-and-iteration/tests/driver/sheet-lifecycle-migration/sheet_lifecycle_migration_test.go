// Scored group: sheet-lifecycle-migration (milestone 02, new behaviour).
//
// Worksheet lifecycle -- creation, renaming, copying, deletion, activation
// and visibility -- re-observed now that every worksheet is addressed by its
// name.
//
// This is scored, not gated. Creating a worksheet is the very thing this
// milestone re-spells: milestone 01 took NewSheet(index, name) and returned
// nothing, this one takes NewSheet(name) and returns the index. A workbook
// built by a no-op cannot reach any of these cases, so they price new work
// rather than veto a regression. The single-worksheet half of the same
// contract is gated in package-container-regression.
//
// Graded against ECMA-376 Part 1 clause 18.2.19 (sheet, and its name,
// sheetId and state attributes) and clause 18.2.20 (sheets).
package sheetlifecyclemigration

import (
	"strings"
	"testing"
	"unicode/utf8"

	seedcalc "__SEEDCALC_MODULE__"
)

func namesOf(m map[int]string) map[string]bool {
	out := make(map[string]bool, len(m))
	for _, v := range m {
		out[v] = true
	}
	return out
}

func TestSheetMapAndIndexAgree(t *testing.T) {
	f := seedcalc.NewFile()
	second := f.NewSheet("Second")
	third := f.NewSheet("Third")

	m := f.GetSheetMap()
	if len(m) != 3 {
		t.Fatalf("GetSheetMap has %d entries, want 3: %v", len(m), m)
	}
	for idx, name := range m {
		if got := f.GetSheetIndex(name); got != idx {
			t.Errorf("GetSheetIndex(%q) = %d, but GetSheetMap keys it at %d", name, got, idx)
		}
		if got := f.GetSheetName(idx); got != name {
			t.Errorf("GetSheetName(%d) = %q, want %q", idx, got, name)
		}
	}
	if second == third {
		t.Errorf("two worksheets created by name share the index %d", second)
	}
	if got := f.GetSheetIndex("NoSuchSheet"); got != 0 {
		t.Errorf("GetSheetIndex on an unknown name = %d, want 0", got)
	}
}

func TestSetSheetNameRenamesInPlace(t *testing.T) {
	f := seedcalc.NewFile()
	idx := f.NewSheet("Second")
	f.SetCellValue("Second", "A1", "kept")

	f.SetSheetName("Second", "Renamed")
	if got := f.GetSheetName(idx); got != "Renamed" {
		t.Errorf("GetSheetName(%d) = %q, want %q", idx, got, "Renamed")
	}
	if got := f.GetSheetIndex("Renamed"); got != idx {
		t.Errorf("GetSheetIndex(%q) = %d, want %d", "Renamed", got, idx)
	}
	if got := f.GetSheetIndex("Second"); got != 0 {
		t.Errorf("the old name still resolves to index %d", got)
	}
	if got := f.GetCellValue("Renamed", "A1"); got != "kept" {
		t.Errorf("A1 under the new name = %q, want %q -- renaming must not move the content", got, "kept")
	}
	if n := len(f.GetSheetMap()); n != 2 {
		t.Errorf("GetSheetMap has %d entries after a rename, want 2", n)
	}
}

func TestDeleteSheetRemovesItByName(t *testing.T) {
	f := seedcalc.NewFile()
	f.NewSheet("Second")
	f.NewSheet("Third")
	f.SetCellValue("Third", "A1", "still here")

	f.DeleteSheet("Second")
	names := namesOf(f.GetSheetMap())
	if names["Second"] {
		t.Error("the deleted worksheet is still in the sheet map")
	}
	if !names["Sheet1"] || !names["Third"] {
		t.Errorf("deleting one worksheet removed others: %v", names)
	}
	if got := f.GetSheetIndex("Second"); got != 0 {
		t.Errorf("GetSheetIndex on the deleted name = %d, want 0", got)
	}
	if got := f.GetCellValue("Third", "A1"); got != "still here" {
		t.Errorf("Third!A1 = %q after deleting another worksheet, want %q", got, "still here")
	}
}

func TestCopySheetDuplicatesTheContent(t *testing.T) {
	f := seedcalc.NewFile()
	f.SetCellValue("Sheet1", "A1", "source")
	target := f.NewSheet("Copy")

	if err := f.CopySheet(f.GetSheetIndex("Sheet1"), target); err != nil {
		t.Fatalf("CopySheet: %v", err)
	}
	if got := f.GetCellValue("Copy", "A1"); got != "source" {
		t.Errorf("Copy!A1 = %q, want %q", got, "source")
	}
	f.SetCellValue("Copy", "A1", "changed")
	if got := f.GetCellValue("Sheet1", "A1"); got != "source" {
		t.Errorf("Sheet1!A1 = %q after editing the copy, want %q -- the copy must be independent", got, "source")
	}
}

func TestActiveSheetTracksTheSelectedWorksheet(t *testing.T) {
	f := seedcalc.NewFile()
	second := f.NewSheet("Second")

	if got := f.GetSheetName(f.GetActiveSheetIndex()); got != "Sheet1" {
		t.Errorf("the active worksheet is %q before anything changed it, want %q", got, "Sheet1")
	}
	f.SetActiveSheet(second)
	if got := f.GetSheetName(f.GetActiveSheetIndex()); got != "Second" {
		t.Errorf("the active worksheet is %q after SetActiveSheet, want %q", got, "Second")
	}
}

func TestSheetVisibilityRoundTrips(t *testing.T) {
	f := seedcalc.NewFile()
	f.NewSheet("Second")
	f.NewSheet("Third")

	for _, name := range []string{"Sheet1", "Second", "Third"} {
		if !f.GetSheetVisible(name) {
			t.Fatalf("the newly created worksheet %q is hidden", name)
		}
	}
	f.SetSheetVisible("Second", false)
	if f.GetSheetVisible("Second") {
		t.Error("the worksheet is still visible after being hidden")
	}
	if !f.GetSheetVisible("Sheet1") || !f.GetSheetVisible("Third") {
		t.Error("hiding one worksheet hid another")
	}
	// Hiding is idempotent: a second request leaves the worksheet hidden.
	f.SetSheetVisible("Second", false)
	if f.GetSheetVisible("Second") {
		t.Error("the worksheet became visible again after a second hide request")
	}
	f.SetSheetVisible("Second", true)
	if !f.GetSheetVisible("Second") {
		t.Error("the worksheet is still hidden after being shown again")
	}
	if f.GetSheetVisible("NoSuchSheet") {
		t.Error("a worksheet that does not exist reports as visible")
	}
}

func TestTheOnlyWorksheetCannotBeHidden(t *testing.T) {
	f := seedcalc.NewFile()
	f.SetSheetVisible("Sheet1", false)
	if !f.GetSheetVisible("Sheet1") {
		t.Error("the only worksheet in the workbook was hidden")
	}
}

func TestSheetNameIsLimitedToThirtyOneCharacters(t *testing.T) {
	f := seedcalc.NewFile()
	long := strings.Repeat("\u00e9", 40)
	f.SetSheetName("Sheet1", long)

	m := f.GetSheetMap()
	if len(m) != 1 {
		t.Fatalf("GetSheetMap has %d entries after a rename, want 1: %v", len(m), m)
	}
	var got string
	for _, v := range m {
		got = v
	}
	if n := utf8.RuneCountInString(got); n != 31 {
		t.Errorf("the stored worksheet name is %d characters long, want 31: %q", n, got)
	}
	if want := string([]rune(long)[:31]); got != want {
		t.Errorf("the stored worksheet name = %q, want the first 31 characters %q", got, want)
	}
	if f.GetSheetIndex(got) == 0 {
		t.Errorf("the truncated name %q does not resolve to a worksheet", got)
	}
}
