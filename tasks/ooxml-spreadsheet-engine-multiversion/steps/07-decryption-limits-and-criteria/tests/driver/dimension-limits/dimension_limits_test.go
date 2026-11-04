// Scored group: dimension-limits.
//
// The exported "Excel specifications and limits" constants and the places the
// library enforces them. The grid bounds are ECMA-376 Part 1 section 18.3.1.80
// "sheetData" (section 18.3.1.99 is the "worksheet" root element); the
// worksheet size ceiling of 16384 columns by 1048576 rows and
// the 32767-character cell text limit are the SpreadsheetML application limits
// the format is built around. Column width and row height ceilings are section
// 18.3.1.13 "col" and section 18.3.1.73 "row".
package dimensionlimits

import (
	"strings"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

func TestGridDimensionConstants(t *testing.T) {
	if seedcalc.TotalRows != 1048576 {
		t.Fatalf("TotalRows = %d, want 1048576", seedcalc.TotalRows)
	}
	if seedcalc.TotalColumns != 16384 {
		t.Fatalf("TotalColumns = %d, want 16384", seedcalc.TotalColumns)
	}
	// The constants and the coordinate helpers agree on where the grid ends.
	if _, err := seedcalc.CoordinatesToCellName(seedcalc.TotalColumns, seedcalc.TotalRows); err != nil {
		t.Fatalf("CoordinatesToCellName at the last cell of the grid: %v", err)
	}
	name, err := seedcalc.ColumnNumberToName(seedcalc.TotalColumns)
	if err != nil {
		t.Fatalf("ColumnNumberToName(TotalColumns): %v", err)
	}
	if name != "XFD" {
		t.Fatalf("ColumnNumberToName(TotalColumns) = %q, want XFD", name)
	}
}

func TestGeometryLimitConstants(t *testing.T) {
	if seedcalc.MaxColumnWidth != 255 {
		t.Fatalf("MaxColumnWidth = %d, want 255", seedcalc.MaxColumnWidth)
	}
	if seedcalc.MaxRowHeight != 409 {
		t.Fatalf("MaxRowHeight = %d, want 409", seedcalc.MaxRowHeight)
	}
}

func TestFontLimitConstants(t *testing.T) {
	if seedcalc.MaxFontSize != 409 {
		t.Fatalf("MaxFontSize = %d, want 409", seedcalc.MaxFontSize)
	}
	if seedcalc.MaxFontFamilyLength != 31 {
		t.Fatalf("MaxFontFamilyLength = %d, want 31", seedcalc.MaxFontFamilyLength)
	}
}

func TestFileAndCellCapacityConstants(t *testing.T) {
	if seedcalc.MaxFileNameLength != 207 {
		t.Fatalf("MaxFileNameLength = %d, want 207", seedcalc.MaxFileNameLength)
	}
	if seedcalc.TotalCellChars != 32767 {
		t.Fatalf("TotalCellChars = %d, want 32767", seedcalc.TotalCellChars)
	}
	if seedcalc.TotalSheetHyperlinks != 65529 {
		t.Fatalf("TotalSheetHyperlinks = %d, want 65529", seedcalc.TotalSheetHyperlinks)
	}
}

func TestColumnWidthCeilingIsEnforced(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetColWidth("Sheet1", "A", "A", float64(seedcalc.MaxColumnWidth)); err != nil {
		t.Fatalf("SetColWidth at exactly MaxColumnWidth: %v", err)
	}
	if err := f.SetColWidth("Sheet1", "A", "A", float64(seedcalc.MaxColumnWidth)+1); err == nil {
		t.Fatal("SetColWidth accepted a width above MaxColumnWidth")
	}
}

func TestRowHeightCeilingIsEnforced(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetRowHeight("Sheet1", 1, float64(seedcalc.MaxRowHeight)); err != nil {
		t.Fatalf("SetRowHeight at exactly MaxRowHeight: %v", err)
	}
	if err := f.SetRowHeight("Sheet1", 1, float64(seedcalc.MaxRowHeight)+1); err == nil {
		t.Fatal("SetRowHeight accepted a height above MaxRowHeight")
	}
}

func TestRowNumberAboveTotalRowsIsRejected(t *testing.T) {
	f := seedcalc.NewFile()
	// A cell name whose row exceeds the grid cannot be addressed.
	tooFar := "A" + strings.TrimSpace(itoa(seedcalc.TotalRows+1))
	if _, _, err := seedcalc.CellNameToCoordinates(tooFar); err == nil {
		t.Fatalf("CellNameToCoordinates(%q) returned no error; the row is past the end of the grid", tooFar)
	}
	if _, err := f.GetCellValue("Sheet1", tooFar); err == nil {
		t.Fatalf("GetCellValue(%q) returned no error; the row is past the end of the grid", tooFar)
	}
}

// itoa renders a non-negative int without pulling in strconv, keeping this
// group's import list to the library under test.
func itoa(n int) string {
	if n == 0 {
		return "0"
	}
	var b []byte
	for n > 0 {
		b = append([]byte{byte('0' + n%10)}, b...)
		n /= 10
	}
	return string(b)
}
