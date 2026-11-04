// Scored group: write-path-parity (milestone 05, "The streaming writer and
// the drawing registry").
//
// Normative sources:
//   * TEST_INTERFACE.md section 4.05 -- "The two write paths must agree".
//   * ECMA-376 Part 1, 18.3.1.99 worksheet (CT_Worksheet) for the worksheet
//     element and the child elements the streaming path has to carry over,
//     18.3.1.80 sheetData and 18.3.1.73 row for row order, and 18.3.1.4
//     c (CT_Cell) for the cell markup both paths emit.
//   * Cases cross-checked against the upstream test corpus at tag v2.1.0
//     (stream_test.go).
package writepathparity

import (
	"archive/zip"
	"bytes"
	"io/ioutil"
	"reflect"
	"strings"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

func worksheetXML(t *testing.T, f *seedcalc.File) string {
	t.Helper()
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	zr, err := zip.NewReader(bytes.NewReader(buf.Bytes()), int64(buf.Len()))
	if err != nil {
		t.Fatalf("the saved workbook is not a readable package: %v", err)
	}
	for _, zf := range zr.File {
		if zf.Name != "xl/worksheets/sheet1.xml" {
			continue
		}
		rc, err := zf.Open()
		if err != nil {
			t.Fatalf("open xl/worksheets/sheet1.xml: %v", err)
		}
		defer rc.Close()
		b, err := ioutil.ReadAll(rc)
		if err != nil {
			t.Fatalf("read xl/worksheets/sheet1.xml: %v", err)
		}
		return string(b)
	}
	t.Fatal("the saved package has no part named xl/worksheets/sheet1.xml")
	return ""
}

func sheetDataOf(t *testing.T, xml string) string {
	t.Helper()
	i := strings.Index(xml, "<sheetData>")
	j := strings.Index(xml, "</sheetData>")
	if i < 0 || j < i {
		t.Fatal("worksheet part has no sheetData element")
	}
	return xml[i : j+len("</sheetData>")]
}

// grid is the sample block both write paths have to produce.
var grid = [][]interface{}{
	{1, 2.5, 3},
	{4, 5, 6.25},
	{7, 8, 9},
}

func buffered(t *testing.T) *seedcalc.File {
	t.Helper()
	f := seedcalc.NewFile()
	for r, row := range grid {
		for c, v := range row {
			axis, err := seedcalc.CoordinatesToCellName(c+1, r+1)
			if err != nil {
				t.Fatalf("CoordinatesToCellName: %v", err)
			}
			if err := f.SetCellValue("Sheet1", axis, v); err != nil {
				t.Fatalf("SetCellValue(%s): %v", axis, err)
			}
		}
	}
	return f
}

func streamed(t *testing.T) *seedcalc.File {
	t.Helper()
	f := seedcalc.NewFile()
	sw, err := f.NewStreamWriter("Sheet1")
	if err != nil {
		t.Fatalf("NewStreamWriter: %v", err)
	}
	for r, row := range grid {
		axis, err := seedcalc.CoordinatesToCellName(1, r+1)
		if err != nil {
			t.Fatalf("CoordinatesToCellName: %v", err)
		}
		if err := sw.SetRow(axis, row); err != nil {
			t.Fatalf("SetRow(%s): %v", axis, err)
		}
	}
	if err := sw.Flush(); err != nil {
		t.Fatalf("Flush: %v", err)
	}
	return f
}

func TestBothPathsEmitTheSameWorksheetPart(t *testing.T) {
	// Compare <sheetData> only: the two write paths must produce identical row
	// and cell markup (ECMA-376 §18.3.1.80, §18.3.1.73, §18.3.1.4), but
	// surrounding worksheet-level elements may differ between the paths and
	// are not part of this contract.
	want := sheetDataOf(t, worksheetXML(t, buffered(t)))
	got := sheetDataOf(t, worksheetXML(t, streamed(t)))
	if got != want {
		t.Errorf("the streamed sheetData differs from the buffered one\nbuffered: %s\nstreamed: %s",
			want, got)
	}
}

func TestBothPathsEmitTheSameStyledCells(t *testing.T) {
	a := seedcalc.NewFile()
	styleA, err := a.NewStyle(`{"font":{"bold":true}}`)
	if err != nil {
		t.Fatalf("NewStyle: %v", err)
	}
	if err := a.SetCellValue("Sheet1", "A1", 1); err != nil {
		t.Fatalf("SetCellValue: %v", err)
	}
	if err := a.SetCellStyle("Sheet1", "A1", "A1", styleA); err != nil {
		t.Fatalf("SetCellStyle: %v", err)
	}

	b := seedcalc.NewFile()
	styleB, err := b.NewStyle(`{"font":{"bold":true}}`)
	if err != nil {
		t.Fatalf("NewStyle: %v", err)
	}
	sw, err := b.NewStreamWriter("Sheet1")
	if err != nil {
		t.Fatalf("NewStreamWriter: %v", err)
	}
	if err := sw.SetRow("A1", []interface{}{seedcalc.Cell{StyleID: styleB, Value: 1}}); err != nil {
		t.Fatalf("SetRow: %v", err)
	}
	if err := sw.Flush(); err != nil {
		t.Fatalf("Flush: %v", err)
	}

	if styleA != styleB {
		t.Fatalf("the same style definition registered different indices (%d and %d)", styleA, styleB)
	}
	if got, want := worksheetXML(t, b), worksheetXML(t, a); got != want {
		t.Errorf("a styled cell is written differently by the two paths\nbuffered: %s\nstreamed: %s",
			sheetDataOf(t, want), sheetDataOf(t, got))
	}
}

func TestBothPathsReadBackIdentically(t *testing.T) {
	wantRows, err := buffered(t).GetRows("Sheet1")
	if err != nil {
		t.Fatalf("GetRows on the buffered workbook: %v", err)
	}
	f := streamed(t)
	gotRows, err := f.GetRows("Sheet1")
	if err != nil {
		t.Fatalf("GetRows on the streamed workbook: %v", err)
	}
	if !reflect.DeepEqual(gotRows, wantRows) {
		t.Errorf("GetRows = %v, want %v", gotRows, wantRows)
	}

	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	reopened, err := seedcalc.OpenReader(bytes.NewReader(buf.Bytes()))
	if err != nil {
		t.Fatalf("a streamed workbook did not reopen: %v", err)
	}
	reopenedRows, err := reopened.GetRows("Sheet1")
	if err != nil {
		t.Fatalf("GetRows after reopening: %v", err)
	}
	if !reflect.DeepEqual(reopenedRows, wantRows) {
		t.Errorf("after reopening GetRows = %v, want %v", reopenedRows, wantRows)
	}
}

func TestStreamingKeepsOtherWorksheetElements(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetColWidth("Sheet1", "A", "C", 20); err != nil {
		t.Fatalf("SetColWidth: %v", err)
	}
	if err := f.MergeCell("Sheet1", "D1", "E1"); err != nil {
		t.Fatalf("MergeCell: %v", err)
	}
	sw, err := f.NewStreamWriter("Sheet1")
	if err != nil {
		t.Fatalf("NewStreamWriter: %v", err)
	}
	if err := sw.SetRow("A1", []interface{}{1}); err != nil {
		t.Fatalf("SetRow: %v", err)
	}
	if err := sw.Flush(); err != nil {
		t.Fatalf("Flush: %v", err)
	}

	sheet := worksheetXML(t, f)
	for _, want := range []string{`<cols>`, `width="20"`, `<mergeCells>`, `ref="D1:E1"`, `<sheetViews>`} {
		if !strings.Contains(sheet, want) {
			t.Errorf("the streamed worksheet dropped %s; worksheet-level settings made before streaming must survive\ngot: %s", want, sheet)
		}
	}
	width, err := f.GetColWidth("Sheet1", "B")
	if err != nil {
		t.Fatalf("GetColWidth: %v", err)
	}
	if width != 20 {
		t.Errorf("GetColWidth(B) = %v, want 20", width)
	}
	merged, err := f.GetMergeCells("Sheet1")
	if err != nil {
		t.Fatalf("GetMergeCells: %v", err)
	}
	if len(merged) != 1 || merged[0].GetStartAxis() != "D1" || merged[0].GetEndAxis() != "E1" {
		t.Errorf("GetMergeCells = %v, want the single region D1:E1", merged)
	}
}
