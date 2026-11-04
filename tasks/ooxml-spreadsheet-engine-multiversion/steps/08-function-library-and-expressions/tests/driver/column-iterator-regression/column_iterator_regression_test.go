// Gate group: column-iterator-regression.
//
// The two capabilities milestone 07 added outside the formula engine: the
// column iterator reached through Cols and GetCols, and the sheetFormatPr
// option accessors. Nothing in this milestone touches either, so both must
// keep behaving exactly as they did. The worksheet element is ECMA-376 Part 1
// section 18.3.1.81 "sheetFormatPr (Sheet Format Properties)".
package columniteratorregression

import (
	"bytes"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// grid writes a three-by-three block: columns A, B and C each hold three rows.
func grid(t *testing.T) *seedcalc.File {
	t.Helper()
	f := seedcalc.NewFile()
	for axis, v := range map[string]string{
		"A1": "a", "B1": "b", "C1": "c",
		"A2": "1", "B2": "2", "C2": "3",
		"A3": "x", "B3": "y", "C3": "z",
	} {
		if err := f.SetCellValue("Sheet1", axis, v); err != nil {
			t.Fatalf("SetCellValue(%s): %v", axis, err)
		}
	}
	return f
}

func TestGetColsStillReturnsColumnMajorData(t *testing.T) {
	f := grid(t)
	cols, err := f.GetCols("Sheet1")
	if err != nil {
		t.Fatalf("GetCols: %v", err)
	}
	want := [][]string{
		{"a", "1", "x"},
		{"b", "2", "y"},
		{"c", "3", "z"},
	}
	if len(cols) != len(want) {
		t.Fatalf("GetCols returned %d columns, want %d", len(cols), len(want))
	}
	for i, col := range want {
		for j, cell := range col {
			if cols[i][j] != cell {
				t.Fatalf("column %d = %v, want %v", i+1, cols[i], col)
			}
		}
	}
}

func TestTheColumnIteratorStillWalksColumnsInOrder(t *testing.T) {
	f := grid(t)
	it, err := f.Cols("Sheet1")
	if err != nil {
		t.Fatalf("Cols: %v", err)
	}
	// Asserting the contents, not merely the count: an iterator that walked
	// rows instead of columns would still report three of them.
	want := [][]string{
		{"a", "1", "x"},
		{"b", "2", "y"},
		{"c", "3", "z"},
	}
	n := 0
	for it.Next() {
		rows, err := it.Rows()
		if err != nil {
			t.Fatalf("Rows on column %d: %v", n+1, err)
		}
		if n >= len(want) {
			t.Fatalf("iterator produced more than %d columns", len(want))
		}
		if len(rows) != len(want[n]) {
			t.Fatalf("column %d = %v, want %v", n+1, rows, want[n])
		}
		for i, cell := range want[n] {
			if rows[i] != cell {
				t.Fatalf("column %d = %v, want %v", n+1, rows, want[n])
			}
		}
		n++
	}
	if n != len(want) {
		t.Fatalf("iterator produced %d columns, want %d", n, len(want))
	}
}

func TestBothColumnEntryPointsStillRejectWhatTheyCannotName(t *testing.T) {
	f := grid(t)
	if _, err := f.GetCols("NoSuchSheet"); err == nil {
		t.Fatal("GetCols on an absent sheet returned no error")
	}
	// Sheet names are matched case sensitively by both entry points.
	if _, err := f.GetCols("sheet1"); err == nil {
		t.Fatal("GetCols(\"sheet1\") returned no error; sheet names are case sensitive")
	}
	if _, err := f.Cols("sheet1"); err == nil {
		t.Fatal("Cols(\"sheet1\") returned no error; sheet names are case sensitive")
	}
}

func TestColumnsStillSurviveARoundTrip(t *testing.T) {
	f := grid(t)
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	g, err := seedcalc.OpenReader(bytes.NewReader(buf.Bytes()))
	if err != nil {
		t.Fatalf("OpenReader: %v", err)
	}
	cols, err := g.GetCols("Sheet1")
	if err != nil {
		t.Fatalf("GetCols after round trip: %v", err)
	}
	if len(cols) != 3 || cols[1][1] != "2" || cols[2][2] != "z" {
		t.Fatalf("columns after round trip = %v, want the same three columns", cols)
	}
}

func TestTheSheetFormatPrOptionsStillRoundTrip(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetSheetFormatPr("Sheet1",
		seedcalc.DefaultRowHeight(21.0),
		seedcalc.DefaultColWidth(12.5),
		seedcalc.BaseColWidth(9),
		seedcalc.CustomHeight(true),
		seedcalc.ZeroHeight(true),
		seedcalc.ThickTop(true),
		seedcalc.ThickBottom(true),
	); err != nil {
		t.Fatalf("SetSheetFormatPr: %v", err)
	}
	var (
		rowHeight   seedcalc.DefaultRowHeight
		colWidth    seedcalc.DefaultColWidth
		baseWidth   seedcalc.BaseColWidth
		custom      seedcalc.CustomHeight
		zero        seedcalc.ZeroHeight
		thickTop    seedcalc.ThickTop
		thickBottom seedcalc.ThickBottom
	)
	if err := f.GetSheetFormatPr("Sheet1", &rowHeight, &colWidth, &baseWidth, &custom, &zero, &thickTop, &thickBottom); err != nil {
		t.Fatalf("GetSheetFormatPr: %v", err)
	}
	if float64(rowHeight) != 21.0 || float64(colWidth) != 12.5 || uint8(baseWidth) != 9 {
		t.Fatalf("numeric options read back as (%v, %v, %v), want (21, 12.5, 9)", float64(rowHeight), float64(colWidth), uint8(baseWidth))
	}
	if !bool(custom) || !bool(zero) || !bool(thickTop) || !bool(thickBottom) {
		t.Fatalf("boolean options read back as (%v, %v, %v, %v), want all true", bool(custom), bool(zero), bool(thickTop), bool(thickBottom))
	}
}

func TestTheSheetFormatPrAccessorsStillRejectAnAbsentSheet(t *testing.T) {
	f := seedcalc.NewFile()
	var rowHeight seedcalc.DefaultRowHeight
	if err := f.GetSheetFormatPr("NoSuchSheet", &rowHeight); err == nil {
		t.Fatal("GetSheetFormatPr on an absent sheet returned no error")
	}
	if err := f.SetSheetFormatPr("NoSuchSheet", seedcalc.DefaultRowHeight(21.0)); err == nil {
		t.Fatal("SetSheetFormatPr on an absent sheet returned no error")
	}
}
