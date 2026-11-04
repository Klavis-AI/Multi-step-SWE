// Scored group: raw-cell-values.
//
// The RawCellValue option, which every reading entry point now accepts as a
// trailing argument: whether the cell's number format is applied to the stored
// value before it is returned, or the stored literal is handed back untouched.
// The stored value and its display are separate things in the file format --
// ECMA-376 Part 1 section 18.3.1.96 "v (Cell Value)" holds the former and
// section 18.8.30 "numFmt" describes the latter -- and this option chooses
// which of the two is read.
package rawcellvalues

import (
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// formatted builds a workbook whose column A carries the built-in two-decimal
// number format 0.00, so the stored literal and the displayed value differ.
func formatted(t *testing.T) *seedcalc.File {
	t.Helper()
	f := seedcalc.NewFile()
	style, err := f.NewStyle(&seedcalc.Style{NumFmt: 2})
	if err != nil {
		t.Fatalf("NewStyle: %v", err)
	}
	for axis, v := range map[string]int{"A1": 1, "A2": 2, "A3": 3} {
		if err := f.SetCellValue("Sheet1", axis, v); err != nil {
			t.Fatalf("SetCellValue(%s): %v", axis, err)
		}
	}
	if err := f.SetCellStyle("Sheet1", "A1", "A3", style); err != nil {
		t.Fatalf("SetCellStyle: %v", err)
	}
	return f
}

func TestGetCellValueAppliesTheFormatUnlessTheRawValueIsAsked(t *testing.T) {
	f := formatted(t)
	got, err := f.GetCellValue("Sheet1", "A1")
	if err != nil {
		t.Fatalf("GetCellValue: %v", err)
	}
	if got != "1.00" {
		t.Fatalf("GetCellValue(A1) = %q, want the formatted %q", got, "1.00")
	}
	raw, err := f.GetCellValue("Sheet1", "A1", seedcalc.Options{RawCellValue: true})
	if err != nil {
		t.Fatalf("GetCellValue with RawCellValue: %v", err)
	}
	if raw != "1" {
		t.Fatalf("GetCellValue(A1, RawCellValue) = %q, want the stored %q", raw, "1")
	}
	// Passing the option with the field left false is the same as not passing it.
	same, err := f.GetCellValue("Sheet1", "A1", seedcalc.Options{})
	if err != nil {
		t.Fatalf("GetCellValue with a zero Options: %v", err)
	}
	if same != "1.00" {
		t.Fatalf("GetCellValue(A1, Options{}) = %q, want the formatted %q", same, "1.00")
	}
}

func TestGetRowsCarriesTheOptionToEveryCell(t *testing.T) {
	f := formatted(t)
	rows, err := f.GetRows("Sheet1")
	if err != nil {
		t.Fatalf("GetRows: %v", err)
	}
	if len(rows) != 3 || rows[0][0] != "1.00" || rows[2][0] != "3.00" {
		t.Fatalf("GetRows = %v, want the formatted values", rows)
	}
	raw, err := f.GetRows("Sheet1", seedcalc.Options{RawCellValue: true})
	if err != nil {
		t.Fatalf("GetRows with RawCellValue: %v", err)
	}
	if len(raw) != 3 || raw[0][0] != "1" || raw[2][0] != "3" {
		t.Fatalf("GetRows with RawCellValue = %v, want the stored values", raw)
	}
}

func TestGetColsCarriesTheOptionToEveryCell(t *testing.T) {
	f := formatted(t)
	cols, err := f.GetCols("Sheet1")
	if err != nil {
		t.Fatalf("GetCols: %v", err)
	}
	if len(cols) != 1 || len(cols[0]) != 3 || cols[0][0] != "1.00" {
		t.Fatalf("GetCols = %v, want the formatted values", cols)
	}
	raw, err := f.GetCols("Sheet1", seedcalc.Options{RawCellValue: true})
	if err != nil {
		t.Fatalf("GetCols with RawCellValue: %v", err)
	}
	if len(raw) != 1 || len(raw[0]) != 3 || raw[0][0] != "1" {
		t.Fatalf("GetCols with RawCellValue = %v, want the stored values", raw)
	}
}

func TestTheRowIteratorTakesTheOptionPerRow(t *testing.T) {
	f := formatted(t)
	rows, err := f.Rows("Sheet1")
	if err != nil {
		t.Fatalf("Rows: %v", err)
	}
	// The option is given to Columns, not to Rows, so the same iterator can
	// hand back formatted and raw values on different rows.
	var got []string
	for i := 0; rows.Next(); i++ {
		var cells []string
		if i == 0 {
			cells, err = rows.Columns()
		} else {
			cells, err = rows.Columns(seedcalc.Options{RawCellValue: true})
		}
		if err != nil {
			t.Fatalf("Columns: %v", err)
		}
		if len(cells) == 0 {
			t.Fatalf("row %d came back empty", i+1)
		}
		got = append(got, cells[0])
	}
	if err := rows.Error(); err != nil {
		t.Fatalf("Rows.Error: %v", err)
	}
	if len(got) != 3 || got[0] != "1.00" || got[1] != "2" || got[2] != "3" {
		t.Fatalf("iterated values = %v, want [1.00 2 3]", got)
	}
	if err := rows.Close(); err != nil {
		t.Fatalf("Rows.Close: %v", err)
	}
}

func TestTheColumnIteratorTakesTheOptionPerColumn(t *testing.T) {
	f := formatted(t)
	cols, err := f.Cols("Sheet1")
	if err != nil {
		t.Fatalf("Cols: %v", err)
	}
	if !cols.Next() {
		t.Fatal("Cols.Next returned false on the first column")
	}
	raw, err := cols.Rows(seedcalc.Options{RawCellValue: true})
	if err != nil {
		t.Fatalf("Cols.Rows with RawCellValue: %v", err)
	}
	if len(raw) != 3 || raw[0] != "1" || raw[2] != "3" {
		t.Fatalf("Cols.Rows with RawCellValue = %v, want the stored values", raw)
	}
	if err := cols.Error(); err != nil {
		t.Fatalf("Cols.Error: %v", err)
	}
}

func TestTheOptionIsInertOnACellWithoutAFormat(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetCellValue("Sheet1", "A1", 1); err != nil {
		t.Fatalf("SetCellValue: %v", err)
	}
	if err := f.SetCellValue("Sheet1", "B1", "text"); err != nil {
		t.Fatalf("SetCellValue: %v", err)
	}
	for _, axis := range []string{"A1", "B1"} {
		plain, err := f.GetCellValue("Sheet1", axis)
		if err != nil {
			t.Fatalf("GetCellValue(%s): %v", axis, err)
		}
		raw, err := f.GetCellValue("Sheet1", axis, seedcalc.Options{RawCellValue: true})
		if err != nil {
			t.Fatalf("GetCellValue(%s, RawCellValue): %v", axis, err)
		}
		if plain != raw {
			t.Fatalf("GetCellValue(%s) = %q but the raw read = %q; an unformatted cell has only one reading", axis, plain, raw)
		}
	}
}

// The reading entry points apply a fifteen-significant-digit rendering of
// their own to an unstyled numeric cell whose stored text carries more than
// ten digits after the decimal point. That threshold is not the one
// CalcCellValue applies to a computed result, so the two call paths disagree
// about the same quantity, and RawCellValue is what reaches the stored text
// underneath. TEST_INTERFACE.md 4.09.17.
func TestAStoredNumericIsReRenderedPastTenDecimalPlaces(t *testing.T) {
	f := seedcalc.NewFile()
	// Fifteen places after the point: over the threshold, re-rendered.
	if err := f.SetCellValue("Sheet1", "A1", 100.0/7.0); err != nil {
		t.Fatalf("SetCellValue(A1): %v", err)
	}
	// Ten places exactly: under it, and handed back untouched even though it
	// carries seventeen significant figures. The trigger counts places, not
	// figures.
	if err := f.SetCellValue("Sheet1", "A2", 10000000.0/7.0); err != nil {
		t.Fatalf("SetCellValue(A2): %v", err)
	}
	got, err := f.GetCellValue("Sheet1", "A1")
	if err != nil {
		t.Fatalf("GetCellValue(A1): %v", err)
	}
	if got != "14.2857142857143" {
		t.Fatalf("GetCellValue(A1) = %q, want the re-rendered %q", got, "14.2857142857143")
	}
	raw, err := f.GetCellValue("Sheet1", "A1", seedcalc.Options{RawCellValue: true})
	if err != nil {
		t.Fatalf("GetCellValue(A1, RawCellValue): %v", err)
	}
	if raw != "14.285714285714286" {
		t.Fatalf("GetCellValue(A1, RawCellValue) = %q, want the stored %q", raw, "14.285714285714286")
	}
	under, err := f.GetCellValue("Sheet1", "A2")
	if err != nil {
		t.Fatalf("GetCellValue(A2): %v", err)
	}
	if under != "1428571.4285714286" {
		t.Fatalf("GetCellValue(A2) = %q, want it left alone at %q", under, "1428571.4285714286")
	}
	rows, err := f.GetRows("Sheet1")
	if err != nil {
		t.Fatalf("GetRows: %v", err)
	}
	if len(rows) != 2 || rows[0][0] != "14.2857142857143" || rows[1][0] != "1428571.4285714286" {
		t.Fatalf("GetRows = %v, want the same two renderings as GetCellValue", rows)
	}
	cols, err := f.GetCols("Sheet1")
	if err != nil {
		t.Fatalf("GetCols: %v", err)
	}
	if len(cols) != 1 || len(cols[0]) != 2 || cols[0][0] != "14.2857142857143" {
		t.Fatalf("GetCols = %v, want the same two renderings as GetCellValue", cols)
	}
	// The other call path. CalcCellValue leaves a result of fifteen places or
	// fewer exactly as produced, so the same quantity reads differently there.
	if err := f.SetCellFormula("Sheet1", "C1", "=100/7"); err != nil {
		t.Fatalf("SetCellFormula: %v", err)
	}
	calc, err := f.CalcCellValue("Sheet1", "C1")
	if err != nil {
		t.Fatalf("CalcCellValue: %v", err)
	}
	if calc != "14.285714285714286" {
		t.Fatalf("CalcCellValue(=100/7) = %q, want %q -- the computed path does not take the stored path's rendering", calc, "14.285714285714286")
	}
}
