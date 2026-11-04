// Scored group: sheet-index-migration.
//
// The migration this milestone makes to the worksheet addressing model: a sheet
// is addressed by its zero-based position in the workbook's sheet list, not by
// the sheet identifier stored in the package. Behaviour transcribed from the
// upstream checkpoint this milestone tracks, qax-os/excelize tag v2.2.0
// (sheet_test.go, excelize_test.go), restricted to the public interface. The
// ordered list of sheets is ECMA-376 Part 1 section 18.2.20 "sheets (Sheets)"
// over section 18.2.19 "sheet (Sheet Information)"; the active sheet is the
// activeTab attribute of section 18.2.1 "bookViews (Workbook Views)".
package sheetindexmigration

import (
	"bytes"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// equal reports whether two string slices hold the same values in order.
func equal(a, b []string) bool {
	if len(a) != len(b) {
		return false
	}
	for i := range a {
		if a[i] != b[i] {
			return false
		}
	}
	return true
}

func TestGetSheetListReturnsNamesInWorkbookOrder(t *testing.T) {
	f := seedcalc.NewFile()
	if got := f.GetSheetList(); !equal(got, []string{"Sheet1"}) {
		t.Fatalf("GetSheetList on a new workbook = %v, want [Sheet1]", got)
	}
	f.NewSheet("Alpha")
	f.NewSheet("Beta")
	if got := f.GetSheetList(); !equal(got, []string{"Sheet1", "Alpha", "Beta"}) {
		t.Fatalf("GetSheetList = %v, want [Sheet1 Alpha Beta]", got)
	}
}

func TestSheetIndicesAreZeroBased(t *testing.T) {
	f := seedcalc.NewFile()
	if got := f.GetSheetName(0); got != "Sheet1" {
		t.Fatalf("GetSheetName(0) = %q, want %q: the first sheet is at index 0", got, "Sheet1")
	}
	if got := f.GetSheetIndex("Sheet1"); got != 0 {
		t.Fatalf("GetSheetIndex(Sheet1) = %d, want 0", got)
	}
	f.NewSheet("Alpha")
	f.NewSheet("Beta")
	for i, want := range []string{"Sheet1", "Alpha", "Beta"} {
		if got := f.GetSheetName(i); got != want {
			t.Fatalf("GetSheetName(%d) = %q, want %q", i, got, want)
		}
		if got := f.GetSheetIndex(want); got != i {
			t.Fatalf("GetSheetIndex(%q) = %d, want %d", want, got, i)
		}
	}
}

func TestNewSheetReturnsThePositionOfTheSheetItAdded(t *testing.T) {
	f := seedcalc.NewFile()
	if got := f.NewSheet("Alpha"); got != 1 {
		t.Fatalf("NewSheet(Alpha) = %d, want 1", got)
	}
	if got := f.NewSheet("Beta"); got != 2 {
		t.Fatalf("NewSheet(Beta) = %d, want 2", got)
	}
	if got := f.GetSheetName(f.NewSheet("Gamma")); got != "Gamma" {
		t.Fatalf("GetSheetName(NewSheet(Gamma)) = %q, want %q", got, "Gamma")
	}
}

func TestGetSheetNameIsEmptyForAPositionOutsideTheList(t *testing.T) {
	f := seedcalc.NewFile()
	f.NewSheet("Alpha")
	for _, i := range []int{2, 9, -1, -100} {
		if got := f.GetSheetName(i); got != "" {
			t.Fatalf("GetSheetName(%d) = %q, want the empty string", i, got)
		}
	}
}

func TestGetSheetIndexIsMinusOneForAnUnknownName(t *testing.T) {
	f := seedcalc.NewFile()
	for _, name := range []string{"Alpha", "", "sheet1"} {
		if got := f.GetSheetIndex(name); got != -1 {
			t.Fatalf("GetSheetIndex(%q) = %d, want -1", name, got)
		}
	}
	f.NewSheet("Alpha")
	if got := f.GetSheetIndex("Alpha"); got != 1 {
		t.Fatalf("GetSheetIndex(Alpha) after adding it = %d, want 1", got)
	}
}

func TestTheActiveSheetIsAddressedByPosition(t *testing.T) {
	f := seedcalc.NewFile()
	if got := f.GetActiveSheetIndex(); got != 0 {
		t.Fatalf("GetActiveSheetIndex on a new workbook = %d, want 0", got)
	}
	f.NewSheet("Alpha")
	f.NewSheet("Beta")
	f.SetActiveSheet(2)
	if got := f.GetActiveSheetIndex(); got != 2 {
		t.Fatalf("GetActiveSheetIndex after SetActiveSheet(2) = %d, want 2", got)
	}
	if got := f.GetSheetName(f.GetActiveSheetIndex()); got != "Beta" {
		t.Fatalf("the active sheet is %q, want %q", got, "Beta")
	}
	// A position past the end of the list matches no sheet: the call is not an
	// error and the active sheet is left where it was.
	f.SetActiveSheet(99)
	if got := f.GetActiveSheetIndex(); got != 2 {
		t.Fatalf("GetActiveSheetIndex after SetActiveSheet(99) = %d, want 2: an out-of-range position selects nothing", got)
	}
	// A negative position is clamped to the first sheet.
	f.SetActiveSheet(-5)
	if got := f.GetActiveSheetIndex(); got != 0 {
		t.Fatalf("GetActiveSheetIndex after SetActiveSheet(-5) = %d, want 0", got)
	}
}

func TestRenamingASheetKeepsItsPosition(t *testing.T) {
	f := seedcalc.NewFile()
	f.NewSheet("Alpha")
	f.NewSheet("Beta")
	f.SetSheetName("Alpha", "Gamma")
	if got := f.GetSheetList(); !equal(got, []string{"Sheet1", "Gamma", "Beta"}) {
		t.Fatalf("GetSheetList after rename = %v, want [Sheet1 Gamma Beta]", got)
	}
	if got := f.GetSheetIndex("Gamma"); got != 1 {
		t.Fatalf("GetSheetIndex(Gamma) = %d, want 1", got)
	}
	if got := f.GetSheetIndex("Alpha"); got != -1 {
		t.Fatalf("GetSheetIndex(Alpha) after it was renamed = %d, want -1", got)
	}
}

func TestDeletingASheetClosesTheGapInTheList(t *testing.T) {
	f := seedcalc.NewFile()
	f.NewSheet("Alpha")
	f.NewSheet("Beta")
	f.DeleteSheet("Alpha")
	if got := f.GetSheetList(); !equal(got, []string{"Sheet1", "Beta"}) {
		t.Fatalf("GetSheetList after delete = %v, want [Sheet1 Beta]", got)
	}
	if got := f.GetSheetIndex("Beta"); got != 1 {
		t.Fatalf("GetSheetIndex(Beta) after the sheet before it was deleted = %d, want 1", got)
	}
	if got := f.GetSheetName(1); got != "Beta" {
		t.Fatalf("GetSheetName(1) = %q, want %q", got, "Beta")
	}
	// The positions close up but the stored sheet identifiers do not, which is
	// what distinguishes the new addressing from the old: GetSheetMap is still
	// keyed by the sheetId attribute, so Beta stays at key 3 while its
	// position is 1.
	m := f.GetSheetMap()
	if len(m) != 2 {
		t.Fatalf("GetSheetMap after delete has %d entries, want 2", len(m))
	}
	if got := m[3]; got != "Beta" {
		t.Fatalf("GetSheetMap()[3] = %q, want %q: the map is keyed by sheetId, not by position", got, "Beta")
	}
	if _, ok := m[2]; ok {
		t.Fatal("GetSheetMap still holds the identifier of the deleted sheet")
	}
	if got := m[1]; got != "Sheet1" {
		t.Fatalf("GetSheetMap()[1] = %q, want %q", got, "Sheet1")
	}
}

func TestCopySheetAddressesTheFirstSheetAsZero(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetCellValue("Sheet1", "A1", 11); err != nil {
		t.Fatalf("SetCellValue: %v", err)
	}
	f.NewSheet("Alpha")
	if err := f.CopySheet(0, 1); err != nil {
		t.Fatalf("CopySheet(0, 1): %v", err)
	}
	got, err := f.GetCellValue("Alpha", "A1")
	if err != nil {
		t.Fatalf("GetCellValue: %v", err)
	}
	if got != "11" {
		t.Fatalf("GetCellValue(Alpha, A1) after CopySheet(0, 1) = %q, want %q", got, "11")
	}
	if err := f.CopySheet(-1, 1); err == nil {
		t.Fatal("CopySheet with a negative source position returned no error")
	}
	if err := f.CopySheet(0, 9); err == nil {
		t.Fatal("CopySheet to a position outside the sheet list returned no error")
	}
}

func TestTheSheetListSurvivesTheWriteReadRoundTrip(t *testing.T) {
	f := seedcalc.NewFile()
	f.NewSheet("Alpha")
	f.NewSheet("Beta")
	f.SetActiveSheet(1)
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	g, err := seedcalc.OpenReader(bytes.NewReader(buf.Bytes()))
	if err != nil {
		t.Fatalf("OpenReader: %v", err)
	}
	if got := g.GetSheetList(); !equal(got, []string{"Sheet1", "Alpha", "Beta"}) {
		t.Fatalf("GetSheetList after round trip = %v, want [Sheet1 Alpha Beta]", got)
	}
	if got := g.GetActiveSheetIndex(); got != 1 {
		t.Fatalf("GetActiveSheetIndex after round trip = %d, want 1", got)
	}
	if got := g.GetSheetName(2); got != "Beta" {
		t.Fatalf("GetSheetName(2) after round trip = %q, want %q", got, "Beta")
	}
}
