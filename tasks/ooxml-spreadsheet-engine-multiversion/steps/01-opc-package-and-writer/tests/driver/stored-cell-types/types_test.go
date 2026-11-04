// Scored group: stored-cell-types.
//
// The reading side of the typed cell model. The fixture package
// assets/typed-cells.xlsx is hand-built SpreadsheetML: it carries a shared
// string table (ECMA-376 Part 1 clause 18.4), cells of every c/@t flavour the
// clause 18.18.11 ST_CellType enumeration defines that this milestone reads,
// and cells whose style selects a built-in number format (clause 18.8.30).
package storedcelltypes

import (
	"os"
	"path/filepath"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// fixture opens a fresh copy of the checked-in package. Every test opens its
// own copy: reading a workbook is allowed to materialise parts of it, so the
// tests must not share one.
func fixture(t *testing.T) *seedcalc.File {
	t.Helper()
	dir := os.Getenv("SEEDCALC_ASSETS")
	if dir == "" {
		t.Fatal("SEEDCALC_ASSETS is not set")
	}
	f, err := seedcalc.OpenFile(filepath.Join(dir, "typed-cells.xlsx"))
	if err != nil {
		t.Fatalf("OpenFile on the fixture package: %v", err)
	}
	return f
}

func TestSharedStringCellsResolveThroughTheStringTable(t *testing.T) {
	f := fixture(t)
	// A1 and A3 hold t="s" cells whose <v> is an index into xl/sharedStrings.xml.
	if got := f.GetCellValue("Sheet1", "A1"); got != "alpha" {
		t.Errorf("A1 = %q, want %q", got, "alpha")
	}
	if got := f.GetCellValue("Sheet1", "A3"); got != "beta" {
		t.Errorf("A3 = %q, want %q", got, "beta")
	}
}

func TestSharedStringsKeepInsignificantWhitespace(t *testing.T) {
	f := fixture(t)
	// C2 indexes an si whose t carries xml:space="preserve".
	if got := f.GetCellValue("Sheet1", "C2"); got != "  padded  " {
		t.Errorf("C2 = %q, want %q", got, "  padded  ")
	}
}

func TestRichTextSharedStringsConcatenateTheirRuns(t *testing.T) {
	f := fixture(t)
	// A2 indexes an si built from two r runs, "rich" and "Text".
	rows := f.GetRows("Sheet1")
	if len(rows) < 2 || len(rows[1]) < 1 {
		t.Fatalf("GetRows returned %d rows, want the fixture's three: %q", len(rows), rows)
	}
	if got := rows[1][0]; got != "richText" {
		t.Errorf("A2 read through GetRows = %q, want the runs concatenated as %q", got, "richText")
	}
}

func TestUntypedCellsAreNumbers(t *testing.T) {
	f := fixture(t)
	if got := f.GetCellValue("Sheet1", "B1"); got != "3.5" {
		t.Errorf("B1 = %q, want %q", got, "3.5")
	}
}

func TestBooleanAndErrorCellsYieldTheirStoredValue(t *testing.T) {
	f := fixture(t)
	// C1 is t="b" with <v>1</v>; E1 is t="e" with an error literal.
	if got := f.GetCellValue("Sheet1", "C1"); got != "1" {
		t.Errorf("C1 = %q, want the stored boolean value %q", got, "1")
	}
	if got := f.GetCellValue("Sheet1", "E1"); got != "#DIV/0!" {
		t.Errorf("E1 = %q, want the stored error literal %q", got, "#DIV/0!")
	}
}

func TestFormulaCellsExposeBothTheFormulaAndItsCachedResult(t *testing.T) {
	f := fixture(t)
	// D1 is t="str": an f element plus the cached string result in v.
	if got := f.GetCellFormula("Sheet1", "D1"); got != "CONCATENATE(A1,B1)" {
		t.Errorf("GetCellFormula(D1) = %q, want %q", got, "CONCATENATE(A1,B1)")
	}
	if got := f.GetCellValue("Sheet1", "D1"); got != "alpha3.5" {
		t.Errorf("GetCellValue(D1) = %q, want the cached result %q", got, "alpha3.5")
	}
}

func TestNumberFormatsAreAppliedWhenTheValueIsRead(t *testing.T) {
	f := fixture(t)
	// B2 carries style 1, whose numFmtId is the built-in 22 ("m/d/yy h:mm").
	if got := f.GetCellValue("Sheet1", "B2"); got != "7/4/17 12:00" {
		t.Errorf("B2 = %q, want the serial date rendered through built-in format 22", got)
	}
	// D2 carries style 2, whose numFmtId is the built-in 2 ("0.00").
	if got := f.GetCellValue("Sheet1", "D2"); got != "1234.57" {
		t.Errorf("D2 = %q, want the number rendered through built-in format 2", got)
	}
	// An unstyled cell is returned as stored.
	if got := f.GetCellValue("Sheet1", "B1"); got != "3.5" {
		t.Errorf("B1 = %q, want the unformatted value %q", got, "3.5")
	}
}
