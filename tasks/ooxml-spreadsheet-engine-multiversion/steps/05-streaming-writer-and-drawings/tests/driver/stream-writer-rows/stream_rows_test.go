// Scored group: stream-writer-rows (milestone 05, "The streaming writer and
// the drawing registry").
//
// Normative sources:
//   * TEST_INTERFACE.md section 4.05 -- "The streaming write path",
//     "Cell values on the streaming path", "Spilling".
//   * ECMA-376 Part 1, 18.3.1.99 worksheet (CT_Worksheet) and 18.3.1.80
//     sheetData (CT_SheetData) for the element that rows are appended to,
//     18.3.1.73 row (CT_Row/@r) for the 1-based row index, and 18.3.1.4
//     c (CT_Cell/@r, @s, @t) for the cell reference, style index and cell
//     data type written for each value.
//   * ECMA-376 Part 1, 18.18.11 ST_CellType for the @t token set ("b", "str",
//     and the absence of @t for a numeric cell).
//   * Cases cross-checked against the upstream test corpus at tag v2.1.0
//     (stream_test.go).
package streamwriterrows

import (
	"archive/zip"
	"bytes"
	"io/ioutil"
	"strings"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// worksheetXML serialises f and returns the first worksheet part as text.
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

// sheetDataOf returns the <sheetData>...</sheetData> run of a worksheet part.
func sheetDataOf(t *testing.T, xml string) string {
	t.Helper()
	i := strings.Index(xml, "<sheetData>")
	j := strings.Index(xml, "</sheetData>")
	if i < 0 || j < i {
		t.Fatalf("worksheet part has no sheetData element")
	}
	return xml[i : j+len("</sheetData>")]
}

func TestNewStreamWriterRequiresExistingSheet(t *testing.T) {
	f := seedcalc.NewFile()

	sw, err := f.NewStreamWriter("Sheet1")
	if err != nil {
		t.Fatalf("NewStreamWriter on the default worksheet: %v", err)
	}
	if sw == nil {
		t.Fatal("NewStreamWriter returned a nil writer and a nil error")
	}
	if sw.File != f {
		t.Error("StreamWriter.File does not refer back to the workbook it was made from")
	}
	if sw.Sheet != "Sheet1" {
		t.Errorf("StreamWriter.Sheet = %q, want %q", sw.Sheet, "Sheet1")
	}
	if sw.SheetID != f.GetSheetIndex("Sheet1") {
		t.Errorf("StreamWriter.SheetID = %d, want the worksheet index %d", sw.SheetID, f.GetSheetIndex("Sheet1"))
	}

	missing, err := f.NewStreamWriter("NoSuchSheet")
	if err == nil {
		t.Fatal("NewStreamWriter on an unknown worksheet returned a nil error")
	}
	if missing != nil {
		t.Error("NewStreamWriter returned both a writer and an error")
	}
}

func TestStreamWriterWritesTypedCells(t *testing.T) {
	f := seedcalc.NewFile()
	sw, err := f.NewStreamWriter("Sheet1")
	if err != nil {
		t.Fatalf("NewStreamWriter: %v", err)
	}
	if err := sw.SetRow("A1", []interface{}{"Name", 42, 3.5, true, nil}); err != nil {
		t.Fatalf("SetRow: %v", err)
	}
	if err := sw.SetRow("A2", []interface{}{int64(7), []byte("bytes"), false}); err != nil {
		t.Fatalf("SetRow: %v", err)
	}
	if err := sw.Flush(); err != nil {
		t.Fatalf("Flush: %v", err)
	}

	data := sheetDataOf(t, worksheetXML(t, f))
	for _, want := range []string{
		// Row markers
		`<row r="1">`,
		`<row r="2">`,
		// string: t="str" (ECMA-376 §18.18.11 ST_CellType "s" is for SST; "str"
		// is the inline-string type used by this library for Go string values)
		`<c r="A1" t="str"><v>Name</v></c>`,
		// numeric: no @t attribute (ECMA-376 §18.18.11 — absence means numeric)
		`<c r="B1"><v>42</v></c>`,
		`<c r="C1"><v>3.5</v></c>`,
		`<c r="A2"><v>7</v></c>`,
		// bool: t="b", value "1" for true and "0" for false (ECMA-376 §18.18.11)
		`<c r="D1" t="b"><v>1</v></c>`,
		`<c r="C2" t="b"><v>0</v></c>`,
		// nil and []byte encoding is implementation-defined; checked via
		// GetCellValue below rather than asserting a specific @t token.
	} {
		if !strings.Contains(data, want) {
			t.Errorf("streamed sheetData does not contain %s\ngot: %s", want, data)
		}
	}

	for _, tc := range []struct{ axis, want string }{
		{"A1", "Name"}, {"B1", "42"}, {"C1", "3.5"}, {"D1", "1"}, {"E1", ""},
		{"A2", "7"}, {"B2", "bytes"}, {"C2", "0"},
	} {
		got, err := f.GetCellValue("Sheet1", tc.axis)
		if err != nil {
			t.Fatalf("GetCellValue(%s): %v", tc.axis, err)
		}
		if got != tc.want {
			t.Errorf("GetCellValue(%s) = %q, want %q", tc.axis, got, tc.want)
		}
	}
}

func TestStreamWriterAppliesCellStyle(t *testing.T) {
	f := seedcalc.NewFile()
	styleID, err := f.NewStyle(`{"font":{"bold":true}}`)
	if err != nil {
		t.Fatalf("NewStyle: %v", err)
	}
	if styleID == 0 {
		t.Fatal("NewStyle returned the default style index 0")
	}
	sw, err := f.NewStreamWriter("Sheet1")
	if err != nil {
		t.Fatalf("NewStreamWriter: %v", err)
	}
	value := seedcalc.Cell{StyleID: styleID, Value: 100}
	if err := sw.SetRow("A1", []interface{}{value, &value, 200}); err != nil {
		t.Fatalf("SetRow: %v", err)
	}
	if err := sw.Flush(); err != nil {
		t.Fatalf("Flush: %v", err)
	}

	for _, axis := range []string{"A1", "B1"} {
		got, err := f.GetCellStyle("Sheet1", axis)
		if err != nil {
			t.Fatalf("GetCellStyle(%s): %v", axis, err)
		}
		if got != styleID {
			t.Errorf("GetCellStyle(%s) = %d, want the style registered by NewStyle (%d)", axis, got, styleID)
		}
		v, err := f.GetCellValue("Sheet1", axis)
		if err != nil {
			t.Fatalf("GetCellValue(%s): %v", axis, err)
		}
		if v != "100" {
			t.Errorf("GetCellValue(%s) = %q, want %q -- Cell.Value must still be stored", axis, v, "100")
		}
	}
	plain, err := f.GetCellStyle("Sheet1", "C1")
	if err != nil {
		t.Fatalf("GetCellStyle(C1): %v", err)
	}
	if plain != 0 {
		t.Errorf("GetCellStyle(C1) = %d, want 0 for a value written without a Cell wrapper", plain)
	}
}

func TestStreamWriterRejectsMalformedAxis(t *testing.T) {
	f := seedcalc.NewFile()
	sw, err := f.NewStreamWriter("Sheet1")
	if err != nil {
		t.Fatalf("NewStreamWriter: %v", err)
	}
	for _, axis := range []string{"", "A", "1", "1A", "A0", "-A1", "A1:B2", " A1", "$A$1"} {
		if err := sw.SetRow(axis, []interface{}{1}); err == nil {
			t.Errorf("SetRow(%q) returned a nil error; a malformed reference must be rejected", axis)
		}
	}
	// A rejected row must not stop the writer from accepting a valid one.
	if err := sw.SetRow("A1", []interface{}{1}); err != nil {
		t.Fatalf("SetRow after a rejected reference: %v", err)
	}
	if err := sw.Flush(); err != nil {
		t.Fatalf("Flush: %v", err)
	}
	got, err := f.GetCellValue("Sheet1", "A1")
	if err != nil {
		t.Fatalf("GetCellValue: %v", err)
	}
	if got != "1" {
		t.Errorf("GetCellValue(A1) = %q, want %q", got, "1")
	}
}

func TestStreamWriterFlushPublishesTheSheet(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetCellValue("Sheet1", "A1", "before"); err != nil {
		t.Fatalf("SetCellValue: %v", err)
	}
	sw, err := f.NewStreamWriter("Sheet1")
	if err != nil {
		t.Fatalf("NewStreamWriter: %v", err)
	}
	if err := sw.SetRow("A1", []interface{}{"after"}); err != nil {
		t.Fatalf("SetRow: %v", err)
	}
	unflushed, err := f.GetCellValue("Sheet1", "A1")
	if err != nil {
		t.Fatalf("GetCellValue before Flush: %v", err)
	}
	if unflushed != "before" {
		t.Errorf("GetCellValue(A1) before Flush = %q, want %q -- streamed rows are not visible until Flush", unflushed, "before")
	}
	if err := sw.Flush(); err != nil {
		t.Fatalf("Flush: %v", err)
	}
	flushed, err := f.GetCellValue("Sheet1", "A1")
	if err != nil {
		t.Fatalf("GetCellValue after Flush: %v", err)
	}
	if flushed != "after" {
		t.Errorf("GetCellValue(A1) after Flush = %q, want %q", flushed, "after")
	}
}

func TestStreamWriterReplacesExistingSheetContent(t *testing.T) {
	f := seedcalc.NewFile()
	for _, axis := range []string{"A1", "B1", "A2", "Z26"} {
		if err := f.SetCellValue("Sheet1", axis, "old"); err != nil {
			t.Fatalf("SetCellValue(%s): %v", axis, err)
		}
	}
	sw, err := f.NewStreamWriter("Sheet1")
	if err != nil {
		t.Fatalf("NewStreamWriter: %v", err)
	}
	if err := sw.SetRow("A1", []interface{}{"new"}); err != nil {
		t.Fatalf("SetRow: %v", err)
	}
	if err := sw.Flush(); err != nil {
		t.Fatalf("Flush: %v", err)
	}
	rows, err := f.GetRows("Sheet1")
	if err != nil {
		t.Fatalf("GetRows: %v", err)
	}
	if len(rows) != 1 || len(rows[0]) != 1 || rows[0][0] != "new" {
		t.Errorf("GetRows = %v, want exactly the streamed rows -- Flush replaces the worksheet, it does not merge", rows)
	}
	if v, err := f.GetCellValue("Sheet1", "Z26"); err != nil || v != "" {
		t.Errorf("GetCellValue(Z26) = %q, %v; want an empty value -- rows not streamed are discarded", v, err)
	}
}

func TestStreamWriterHandlesStreamLargerThanMemoryBuffer(t *testing.T) {
	f := seedcalc.NewFile()
	sw, err := f.NewStreamWriter("Sheet1")
	if err != nil {
		t.Fatalf("NewStreamWriter: %v", err)
	}
	// Well over the in-memory chunk the writer is allowed to hold, so the
	// implementation has to spill to backing storage and read it back.
	const rows, cols = 2000, 8
	payload := strings.Repeat("x", 1200)
	for r := 1; r <= rows; r++ {
		axis, err := seedcalc.CoordinatesToCellName(1, r)
		if err != nil {
			t.Fatalf("CoordinatesToCellName(1, %d): %v", r, err)
		}
		values := make([]interface{}, cols)
		for c := range values {
			values[c] = payload
		}
		if err := sw.SetRow(axis, values); err != nil {
			t.Fatalf("SetRow(%s): %v", axis, err)
		}
	}
	if err := sw.Flush(); err != nil {
		t.Fatalf("Flush: %v", err)
	}
	got, err := f.GetRows("Sheet1")
	if err != nil {
		t.Fatalf("GetRows: %v", err)
	}
	if len(got) != rows {
		t.Fatalf("GetRows returned %d rows, want %d", len(got), rows)
	}
	last, err := f.GetCellValue("Sheet1", "H2000")
	if err != nil {
		t.Fatalf("GetCellValue(H2000): %v", err)
	}
	if last != payload {
		t.Errorf("GetCellValue(H2000) returned %d bytes, want %d -- the spilled tail of the stream was lost", len(last), len(payload))
	}
}
