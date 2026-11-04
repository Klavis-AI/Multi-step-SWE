// Scored group: row-styles-and-stream-additions.
//
// Formatting a whole row rather than a range of cells, and the row and column
// options the streaming writer gained to do the same thing while writing
// forwards only. The row element and the attributes these set are ECMA-376
// Part 1 section 18.3.1.73 "row": @s and @customFormat for the style, @ht and
// @customHeight for the height, @hidden for visibility; the column widths are
// section 18.3.1.13 "col" and the merges section 18.3.1.55 "mergeCell".
package rowstylesandstreamadditions

import (
	"archive/zip"
	"bytes"
	"errors"
	"fmt"
	"io"
	"strings"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// sheetXML returns the first worksheet part of the written package.
func sheetXML(t *testing.T, f *seedcalc.File) string {
	t.Helper()
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	zr, err := zip.NewReader(bytes.NewReader(buf.Bytes()), int64(buf.Len()))
	if err != nil {
		t.Fatalf("written package is not a ZIP container: %v", err)
	}
	for _, zf := range zr.File {
		if zf.Name != "xl/worksheets/sheet1.xml" {
			continue
		}
		rc, err := zf.Open()
		if err != nil {
			t.Fatalf("open worksheet part: %v", err)
		}
		defer rc.Close()
		b, err := io.ReadAll(rc)
		if err != nil {
			t.Fatalf("read worksheet part: %v", err)
		}
		return string(b)
	}
	t.Fatal("written package has no worksheet part")
	return ""
}

// aStyle returns a usable style identifier.
func aStyle(t *testing.T, f *seedcalc.File) int {
	t.Helper()
	id, err := f.NewStyle(&seedcalc.Style{Font: &seedcalc.Font{Bold: true}})
	if err != nil {
		t.Fatalf("NewStyle: %v", err)
	}
	if id <= 0 {
		t.Fatalf("NewStyle returned %d, want a positive identifier", id)
	}
	return id
}

func TestSetRowStyleFormatsEveryRowInTheRange(t *testing.T) {
	f := seedcalc.NewFile()
	style := aStyle(t, f)
	if err := f.SetRowStyle("Sheet1", 2, 4, style); err != nil {
		t.Fatalf("SetRowStyle: %v", err)
	}
	xml := sheetXML(t, f)
	// The style identifier and the flag that says the row carries one are
	// written onto each row of the range.
	for _, r := range []int{2, 3, 4} {
		want := fmt.Sprintf(`<row r="%d" s="%d" customFormat="true">`, r, style)
		if !strings.Contains(xml, want) {
			t.Fatalf("worksheet part does not contain %s", want)
		}
	}
	// Rows below the range are materialised so the range can be addressed, but
	// they carry no style of their own.
	if !strings.Contains(xml, `<row r="1"></row>`) {
		t.Fatal("row 1 was styled although the range began at row 2")
	}
}

func TestSetRowStyleNormalisesAReversedRange(t *testing.T) {
	f := seedcalc.NewFile()
	style := aStyle(t, f)
	// The larger row first: the two ends are swapped rather than rejected.
	if err := f.SetRowStyle("Sheet1", 5, 3, style); err != nil {
		t.Fatalf("SetRowStyle with the ends reversed: %v", err)
	}
	xml := sheetXML(t, f)
	for _, r := range []string{`r="3"`, `r="4"`, `r="5"`} {
		if !strings.Contains(xml, r) {
			t.Fatalf("worksheet part has no row %s after a reversed range", r)
		}
	}
}

func TestSetRowStyleRejectsRowsAndStylesOutsideTheSheet(t *testing.T) {
	f := seedcalc.NewFile()
	style := aStyle(t, f)
	if err := f.SetRowStyle("Sheet1", 0, 3, style); err == nil {
		t.Fatal("SetRowStyle starting at row 0 returned no error")
	}
	if err := f.SetRowStyle("Sheet1", 1, seedcalc.TotalRows+1, style); !errors.Is(err, seedcalc.ErrMaxRows) {
		t.Fatalf("SetRowStyle past the last row returned %v, want %v", err, seedcalc.ErrMaxRows)
	}
	if err := f.SetRowStyle("Sheet1", 1, 3, -1); err == nil {
		t.Fatal("SetRowStyle with a negative style identifier returned no error")
	}
	if err := f.SetRowStyle("NoSuchSheet", 1, 3, style); err == nil {
		t.Fatal("SetRowStyle on an absent sheet returned no error")
	}
}

func TestTheStreamingWriterTakesRowOptions(t *testing.T) {
	f := seedcalc.NewFile()
	style := aStyle(t, f)
	sw, err := f.NewStreamWriter("Sheet1")
	if err != nil {
		t.Fatalf("NewStreamWriter: %v", err)
	}
	if err := sw.SetRow("A1", []interface{}{"a", "b"}); err != nil {
		t.Fatalf("SetRow without options: %v", err)
	}
	if err := sw.SetRow("A2", []interface{}{"c", "d"}, seedcalc.RowOpts{Height: 30, StyleID: style}); err != nil {
		t.Fatalf("SetRow with a height and a style: %v", err)
	}
	if err := sw.SetRow("A3", []interface{}{"e", "f"}, seedcalc.RowOpts{Hidden: true}); err != nil {
		t.Fatalf("SetRow hidden: %v", err)
	}
	if err := sw.Flush(); err != nil {
		t.Fatalf("Flush: %v", err)
	}
	xml := sheetXML(t, f)
	if !strings.Contains(xml, `ht="30"`) || !strings.Contains(xml, `customHeight="true"`) {
		t.Fatalf("the streamed row does not carry its height: %s", xml)
	}
	if !strings.Contains(xml, `customFormat="true"`) {
		t.Fatalf("the streamed row does not carry its style: %s", xml)
	}
	if !strings.Contains(xml, `hidden="true"`) {
		t.Fatalf("the streamed row is not hidden: %s", xml)
	}
	// A row written without options carries none of those attributes.
	if !strings.Contains(xml, `<row r="1">`) {
		t.Fatalf("the unadorned row gained attributes: %s", xml)
	}
}

func TestTheStreamingWriterRejectsAnOversizeRowHeight(t *testing.T) {
	f := seedcalc.NewFile()
	sw, err := f.NewStreamWriter("Sheet1")
	if err != nil {
		t.Fatalf("NewStreamWriter: %v", err)
	}
	err = sw.SetRow("A1", []interface{}{"a"}, seedcalc.RowOpts{Height: seedcalc.MaxRowHeight + 1})
	if !errors.Is(err, seedcalc.ErrMaxRowHeight) {
		t.Fatalf("SetRow past the maximum row height returned %v, want %v", err, seedcalc.ErrMaxRowHeight)
	}
}

func TestTheStreamingWriterSetsColumnWidthsBeforeTheFirstRow(t *testing.T) {
	f := seedcalc.NewFile()
	sw, err := f.NewStreamWriter("Sheet1")
	if err != nil {
		t.Fatalf("NewStreamWriter: %v", err)
	}
	if err := sw.SetColWidth(2, 3, 20); err != nil {
		t.Fatalf("SetColWidth: %v", err)
	}
	if err := sw.SetRow("A1", []interface{}{"a", "b", "c"}); err != nil {
		t.Fatalf("SetRow: %v", err)
	}
	// The writer only moves forwards, so the column block is closed as soon as
	// the first row is written.
	if err := sw.SetColWidth(4, 5, 20); !errors.Is(err, seedcalc.ErrStreamSetColWidth) {
		t.Fatalf("SetColWidth after the first row returned %v, want %v", err, seedcalc.ErrStreamSetColWidth)
	}
	if err := sw.Flush(); err != nil {
		t.Fatalf("Flush: %v", err)
	}
	xml := sheetXML(t, f)
	if !strings.Contains(xml, `min="2"`) || !strings.Contains(xml, `max="3"`) {
		t.Fatalf("the streamed package has no column block: %s", xml)
	}
}

func TestTheStreamingWriterMergesARegion(t *testing.T) {
	f := seedcalc.NewFile()
	sw, err := f.NewStreamWriter("Sheet1")
	if err != nil {
		t.Fatalf("NewStreamWriter: %v", err)
	}
	if err := sw.SetRow("A1", []interface{}{"merged"}); err != nil {
		t.Fatalf("SetRow: %v", err)
	}
	if err := sw.MergeCell("A1", "C1"); err != nil {
		t.Fatalf("MergeCell: %v", err)
	}
	if err := sw.MergeCell("A2", "C2"); err != nil {
		t.Fatalf("MergeCell: %v", err)
	}
	if err := sw.MergeCell("A3", "notacell"); err == nil {
		t.Fatal("MergeCell with a malformed corner returned no error")
	}
	if err := sw.Flush(); err != nil {
		t.Fatalf("Flush: %v", err)
	}
	xml := sheetXML(t, f)
	if !strings.Contains(xml, `<mergeCells count="2">`) {
		t.Fatalf("the streamed package does not declare two merges: %s", xml)
	}
	if !strings.Contains(xml, `ref="A1:C1"`) || !strings.Contains(xml, `ref="A2:C2"`) {
		t.Fatalf("the streamed package is missing a merge reference: %s", xml)
	}
}
