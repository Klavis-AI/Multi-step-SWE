// Scored group: row-iteration.
//
// The streaming row iterator -- Rows/Next/Columns/Error -- and the cell shapes
// it has to be able to read back, including inline strings. Graded against
// ECMA-376 Part 1 clause 18.3.1.73 (row), clause 18.3.1.4 (c) and clause
// 18.3.1.53 (is, rich text inline), together with clause 18.18.11
// (ST_CellType), which fixes the inlineStr cell type. Behaviour cases were
// checked against the upstream release this checkpoint tracks (tag v1.4.1).
package rowiteration

import (
	"archive/zip"
	"bytes"
	"errors"
	"io"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// packageBytes saves f through the in-memory writer path.
func packageBytes(t *testing.T, f *seedcalc.File) []byte {
	t.Helper()
	buf, err := f.WriteToBuffer()
	if err != nil {
		t.Fatalf("WriteToBuffer: %v", err)
	}
	if buf.Len() == 0 {
		t.Fatal("WriteToBuffer produced an empty package")
	}
	return buf.Bytes()
}

// rebuildPackage rewrites a package, replacing the named parts with literal
// XML. It is the only way to present the reader with markup this library does
// not itself emit.
func rebuildPackage(t *testing.T, src []byte, replace map[string]string) []byte {
	t.Helper()
	zr, err := zip.NewReader(bytes.NewReader(src), int64(len(src)))
	if err != nil {
		t.Fatalf("source package is not a ZIP container: %v", err)
	}
	var out bytes.Buffer
	zw := zip.NewWriter(&out)
	for _, fh := range zr.File {
		w, err := zw.Create(fh.Name)
		if err != nil {
			t.Fatalf("create part %s: %v", fh.Name, err)
		}
		if s, ok := replace[fh.Name]; ok {
			if _, err := w.Write([]byte(s)); err != nil {
				t.Fatalf("write part %s: %v", fh.Name, err)
			}
			continue
		}
		rc, err := fh.Open()
		if err != nil {
			t.Fatalf("open part %s: %v", fh.Name, err)
		}
		if _, err := io.Copy(w, rc); err != nil {
			rc.Close()
			t.Fatalf("copy part %s: %v", fh.Name, err)
		}
		rc.Close()
	}
	if err := zw.Close(); err != nil {
		t.Fatalf("close rebuilt package: %v", err)
	}
	return out.Bytes()
}

func collect(t *testing.T, f *seedcalc.File, sheet string) [][]string {
	t.Helper()
	rows, err := f.Rows(sheet)
	if err != nil {
		t.Fatalf("Rows(%q): %v", sheet, err)
	}
	var got [][]string
	for rows.Next() {
		got = append(got, rows.Columns())
	}
	if err := rows.Error(); err != nil {
		t.Fatalf("Rows(%q).Error() after a complete traversal: %v", sheet, err)
	}
	return got
}

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

func TestRowsVisitsEveryRowInSheetOrder(t *testing.T) {
	f := seedcalc.NewFile()
	f.SetCellValue("Sheet1", "A1", "first")
	f.SetCellValue("Sheet1", "A2", "second")
	f.SetCellValue("Sheet1", "A3", "third")

	got := collect(t, f, "Sheet1")
	if len(got) != 3 {
		t.Fatalf("iterator produced %d rows, want 3: %v", len(got), got)
	}
	for i, want := range []string{"first", "second", "third"} {
		if len(got[i]) == 0 || got[i][0] != want {
			t.Errorf("row %d column A = %v, want %q", i+1, got[i], want)
		}
	}
}

func TestColumnsReportsValuesPositionedByColumn(t *testing.T) {
	f := seedcalc.NewFile()
	f.SetCellValue("Sheet1", "A1", "a")
	f.SetCellValue("Sheet1", "C1", "c")

	got := collect(t, f, "Sheet1")
	if len(got) != 1 {
		t.Fatalf("iterator produced %d rows, want 1: %v", len(got), got)
	}
	if want := []string{"a", "", "c"}; !equal(got[0], want) {
		t.Errorf("Columns() = %v, want %v -- a value must sit at its own column position", got[0], want)
	}
}

func TestColumnsIsEmptyBeforeTheFirstNext(t *testing.T) {
	f := seedcalc.NewFile()
	f.SetCellValue("Sheet1", "A1", "a")

	rows, err := f.Rows("Sheet1")
	if err != nil {
		t.Fatalf("Rows: %v", err)
	}
	if got := rows.Columns(); len(got) != 0 {
		t.Errorf("Columns() before the first Next() = %v, want an empty slice", got)
	}
}

func TestNextReportsExhaustionAndLeavesNoError(t *testing.T) {
	f := seedcalc.NewFile()
	f.SetCellValue("Sheet1", "A1", "only")

	rows, err := f.Rows("Sheet1")
	if err != nil {
		t.Fatalf("Rows: %v", err)
	}
	if !rows.Next() {
		t.Fatal("Next() returned false on a sheet that has one row")
	}
	if rows.Next() {
		t.Error("Next() returned true after the last row")
	}
	if rows.Next() {
		t.Error("Next() returned true when called again past the end")
	}
	if err := rows.Error(); err != nil {
		t.Errorf("Error() after exhaustion = %v, want nil -- the end of the sheet is not a failure", err)
	}
}

func TestRowsRejectsAnUnknownWorksheet(t *testing.T) {
	f := seedcalc.NewFile()
	rows, err := f.Rows("NoSuchSheet")
	if err == nil {
		t.Fatal("Rows on a worksheet that does not exist returned no error")
	}
	if rows != nil {
		t.Error("Rows returned a non-nil iterator together with an error")
	}
	var missing seedcalc.ErrSheetNotExist
	if !errors.As(err, &missing) {
		t.Fatalf("Rows error is %T, want it to unwrap to ErrSheetNotExist", err)
	}
	if missing.SheetName != "NoSuchSheet" {
		t.Errorf("ErrSheetNotExist.SheetName = %q, want %q", missing.SheetName, "NoSuchSheet")
	}
}

func TestRowsReadsInlineStringCells(t *testing.T) {
	const inlineSheet = `<?xml version="1.0" encoding="UTF-8" standalone="yes"?>` +
		`<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">` +
		`<dimension ref="A1:B1"/><sheetViews><sheetView workbookViewId="0"/></sheetViews>` +
		`<sheetData><row r="1">` +
		`<c r="A1" t="inlineStr"><is><t>inline text</t></is></c>` +
		`<c r="B1" t="str"><v>plain</v></c>` +
		`</row></sheetData></worksheet>`

	seed := seedcalc.NewFile()
	seed.SetCellValue("Sheet1", "A1", "placeholder")
	b := rebuildPackage(t, packageBytes(t, seed), map[string]string{
		"xl/worksheets/sheet1.xml": inlineSheet,
	})
	f, err := seedcalc.OpenReader(bytes.NewReader(b))
	if err != nil {
		t.Fatalf("OpenReader on a package carrying an inline string: %v", err)
	}
	if got := f.GetCellValue("Sheet1", "A1"); got != "inline text" {
		t.Errorf("GetCellValue on an inlineStr cell = %q, want %q", got, "inline text")
	}
	got := collect(t, f, "Sheet1")
	if len(got) != 1 {
		t.Fatalf("iterator produced %d rows, want 1: %v", len(got), got)
	}
	if want := []string{"inline text", "plain"}; !equal(got[0], want) {
		t.Errorf("Columns() = %v, want %v", got[0], want)
	}
}
