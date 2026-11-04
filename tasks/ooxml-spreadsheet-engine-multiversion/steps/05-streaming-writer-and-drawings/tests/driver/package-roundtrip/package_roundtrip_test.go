// Gate group: package-roundtrip (milestone 05, "The streaming writer and
// the drawing registry").
//
// The one deterministic end-to-end path carried forward: a workbook this
// library writes can be reopened and its data read back unchanged. Nothing
// here depends on any surface this milestone adds -- every case passes
// against the milestone 04 checkpoint as well -- so it is an unpriced veto
// rather than something a milestone can earn credit for.
//
// Normative sources:
//   * ECMA-376 Part 2, 13 (SpreadsheetML Packages) for the ZIP-based OPC
//     container and required content types.
//   * ECMA-376 Part 1, 18.2 Workbook and 18.3 Worksheet for the top-level
//     elements the package must carry.
package packageroundtrip

import (
	"bytes"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// makeWorkbook builds a multi-sheet workbook with mixed value types.
func makeWorkbook(t *testing.T) *seedcalc.File {
	t.Helper()
	f := seedcalc.NewFile()
	sheet2 := f.NewSheet("Sheet2")
	if sheet2 == 0 {
		t.Fatal("NewSheet returned 0")
	}
	for _, tc := range []struct{ sheet, cell string; val interface{} }{
		{"Sheet1", "A1", "hello"},
		{"Sheet1", "B1", 42},
		{"Sheet1", "C1", true},
		{"Sheet2", "A1", 3.14},
		{"Sheet2", "B1", "world"},
	} {
		if err := f.SetCellValue(tc.sheet, tc.cell, tc.val); err != nil {
			t.Fatalf("SetCellValue(%s, %s): %v", tc.sheet, tc.cell, err)
		}
	}
	return f
}

func TestReopenedWorkbookPreservesSheetNames(t *testing.T) {
	f := makeWorkbook(t)
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	f2, err := seedcalc.OpenReader(bytes.NewReader(buf.Bytes()))
	if err != nil {
		t.Fatalf("OpenReader: %v", err)
	}
	sheetMap := f2.GetSheetMap()
	hasSheet := func(name string) bool {
		for _, v := range sheetMap {
			if v == name {
				return true
			}
		}
		return false
	}
	for _, name := range []string{"Sheet1", "Sheet2"} {
		if !hasSheet(name) {
			t.Errorf("reopened workbook is missing sheet %q; sheetMap = %v", name, sheetMap)
		}
	}
}

func TestReopenedWorkbookPreservesCellValues(t *testing.T) {
	f := makeWorkbook(t)
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	f2, err := seedcalc.OpenReader(bytes.NewReader(buf.Bytes()))
	if err != nil {
		t.Fatalf("OpenReader: %v", err)
	}
	for _, tc := range []struct{ sheet, cell, want string }{
		{"Sheet1", "A1", "hello"},
		{"Sheet1", "B1", "42"},
		{"Sheet2", "A1", "3.14"},
		{"Sheet2", "B1", "world"},
	} {
		got, err := f2.GetCellValue(tc.sheet, tc.cell)
		if err != nil {
			t.Fatalf("GetCellValue(%s, %s): %v", tc.sheet, tc.cell, err)
		}
		if got != tc.want {
			t.Errorf("GetCellValue(%s, %s) = %q, want %q", tc.sheet, tc.cell, got, tc.want)
		}
	}
}

func TestSaveToFileThenReopen(t *testing.T) {
	f := makeWorkbook(t)
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	// Reopen twice: once raw, once from a second Write to ensure
	// the internal state is consistent after the first serialisation.
	f2, err := seedcalc.OpenReader(bytes.NewReader(buf.Bytes()))
	if err != nil {
		t.Fatalf("first OpenReader: %v", err)
	}
	var buf2 bytes.Buffer
	if err := f2.Write(&buf2); err != nil {
		t.Fatalf("second Write: %v", err)
	}
	f3, err := seedcalc.OpenReader(bytes.NewReader(buf2.Bytes()))
	if err != nil {
		t.Fatalf("second OpenReader: %v", err)
	}
	v, err := f3.GetCellValue("Sheet1", "A1")
	if err != nil {
		t.Fatalf("GetCellValue after double round-trip: %v", err)
	}
	if v != "hello" {
		t.Errorf("after double round-trip Sheet1!A1 = %q, want %q", v, "hello")
	}
}
