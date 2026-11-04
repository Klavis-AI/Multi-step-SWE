// Scored group: page-breaks.
//
// Manual page breaks: a cell reference marks both the row above which and the
// column left of which the printed page is cut. Behaviour transcribed from the
// upstream checkpoint this milestone tracks, qax-os/excelize tag v2.2.0
// (sheet_test.go), restricted to the public interface. The stored form is
// ECMA-376 Part 1 section 18.3.1.74 "rowBreaks (Horizontal Page Breaks (Row))"
// and section 18.3.1.14 "colBreaks (Vertical Page Breaks)", each over section
// 18.3.1.3 "brk (Break)".
package pagebreaks

import (
	"archive/zip"
	"bytes"
	"encoding/xml"
	"io"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// brk mirrors the brk element.
type brk struct {
	ID  int  `xml:"id,attr"`
	Max int  `xml:"max,attr"`
	Man bool `xml:"man,attr"`
}

// breaks mirrors a rowBreaks or colBreaks element.
type breaks struct {
	Count            int   `xml:"count,attr"`
	ManualBreakCount int   `xml:"manualBreakCount,attr"`
	Brk              []brk `xml:"brk"`
}

// worksheet holds only the break collections of a worksheet part.
type worksheet struct {
	RowBreaks breaks `xml:"rowBreaks"`
	ColBreaks breaks `xml:"colBreaks"`
}

// readBreaks writes f and decodes the break collections of its first sheet.
func readBreaks(t *testing.T, f *seedcalc.File) worksheet {
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
		var ws worksheet
		if err := xml.Unmarshal(b, &ws); err != nil {
			t.Fatalf("worksheet part is not well-formed: %v", err)
		}
		return ws
	}
	t.Fatal("written package has no xl/worksheets/sheet1.xml part")
	return worksheet{}
}

func TestInsertPageBreakRecordsARowAndAColumnBreak(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.InsertPageBreak("Sheet1", "C2"); err != nil {
		t.Fatalf("InsertPageBreak: %v", err)
	}
	ws := readBreaks(t, f)
	if len(ws.RowBreaks.Brk) != 1 {
		t.Fatalf("worksheet holds %d row breaks, want 1", len(ws.RowBreaks.Brk))
	}
	// A break at C2 cuts above row 2, which is the zero-based row index 1.
	if ws.RowBreaks.Brk[0].ID != 1 {
		t.Fatalf("row break id = %d, want 1", ws.RowBreaks.Brk[0].ID)
	}
	if !ws.RowBreaks.Brk[0].Man {
		t.Fatal("row break is not marked manual")
	}
	if ws.RowBreaks.Brk[0].Max != 16383 {
		t.Fatalf("row break max = %d, want 16383 (the last column index)", ws.RowBreaks.Brk[0].Max)
	}
	if ws.RowBreaks.Count != 1 {
		t.Fatalf("rowBreaks count attribute = %d, want 1", ws.RowBreaks.Count)
	}
	// Every break this library writes is manual, so manualBreakCount equals
	// count on each collection.
	if ws.RowBreaks.ManualBreakCount != 1 {
		t.Fatalf("rowBreaks manualBreakCount attribute = %d, want 1", ws.RowBreaks.ManualBreakCount)
	}
	if len(ws.ColBreaks.Brk) != 1 {
		t.Fatalf("worksheet holds %d column breaks, want 1", len(ws.ColBreaks.Brk))
	}
	// A break at C2 cuts left of column C, which is the zero-based index 2.
	if ws.ColBreaks.Brk[0].ID != 2 {
		t.Fatalf("column break id = %d, want 2", ws.ColBreaks.Brk[0].ID)
	}
	if ws.ColBreaks.Brk[0].Max != 1048575 {
		t.Fatalf("column break max = %d, want 1048575 (the last row index)", ws.ColBreaks.Brk[0].Max)
	}
	if ws.ColBreaks.Count != 1 {
		t.Fatalf("colBreaks count attribute = %d, want 1", ws.ColBreaks.Count)
	}
	if ws.ColBreaks.ManualBreakCount != 1 {
		t.Fatalf("colBreaks manualBreakCount attribute = %d, want 1", ws.ColBreaks.ManualBreakCount)
	}
}

func TestABreakInTheFirstRowOrColumnIsNotRecorded(t *testing.T) {
	f := seedcalc.NewFile()
	// A5 is in column A, so it cuts a row but no column.
	if err := f.InsertPageBreak("Sheet1", "A5"); err != nil {
		t.Fatalf("InsertPageBreak(A5): %v", err)
	}
	ws := readBreaks(t, f)
	if len(ws.RowBreaks.Brk) != 1 || ws.RowBreaks.Brk[0].ID != 4 {
		t.Fatalf("row breaks = %+v, want a single break with id 4", ws.RowBreaks.Brk)
	}
	if len(ws.ColBreaks.Brk) != 0 {
		t.Fatalf("column breaks = %+v, want none: column A has nothing to its left", ws.ColBreaks.Brk)
	}
	// D1 is in row 1, so it cuts a column but no row.
	g := seedcalc.NewFile()
	if err := g.InsertPageBreak("Sheet1", "D1"); err != nil {
		t.Fatalf("InsertPageBreak(D1): %v", err)
	}
	ws = readBreaks(t, g)
	if len(ws.RowBreaks.Brk) != 0 {
		t.Fatalf("row breaks = %+v, want none: row 1 has nothing above it", ws.RowBreaks.Brk)
	}
	if len(ws.ColBreaks.Brk) != 1 || ws.ColBreaks.Brk[0].ID != 3 {
		t.Fatalf("column breaks = %+v, want a single break with id 3", ws.ColBreaks.Brk)
	}
	// A1 is the first cell of the sheet, so it records nothing at all.
	h := seedcalc.NewFile()
	if err := h.InsertPageBreak("Sheet1", "A1"); err != nil {
		t.Fatalf("InsertPageBreak(A1): %v", err)
	}
	ws = readBreaks(t, h)
	if len(ws.RowBreaks.Brk) != 0 || len(ws.ColBreaks.Brk) != 0 {
		t.Fatalf("breaks after InsertPageBreak(A1) = %+v / %+v, want none", ws.RowBreaks.Brk, ws.ColBreaks.Brk)
	}
}

func TestInsertingTheSameBreakTwiceChangesNothing(t *testing.T) {
	f := seedcalc.NewFile()
	for i := 0; i < 3; i++ {
		if err := f.InsertPageBreak("Sheet1", "C2"); err != nil {
			t.Fatalf("InsertPageBreak: %v", err)
		}
	}
	ws := readBreaks(t, f)
	if len(ws.RowBreaks.Brk) != 1 {
		t.Fatalf("worksheet holds %d row breaks after three identical inserts, want 1", len(ws.RowBreaks.Brk))
	}
	if len(ws.ColBreaks.Brk) != 1 {
		t.Fatalf("worksheet holds %d column breaks after three identical inserts, want 1", len(ws.ColBreaks.Brk))
	}
}

func TestRemovePageBreakDropsTheBreak(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.InsertPageBreak("Sheet1", "C2"); err != nil {
		t.Fatalf("InsertPageBreak: %v", err)
	}
	if err := f.RemovePageBreak("Sheet1", "C2"); err != nil {
		t.Fatalf("RemovePageBreak: %v", err)
	}
	ws := readBreaks(t, f)
	if len(ws.RowBreaks.Brk) != 0 {
		t.Fatalf("row breaks after removal = %+v, want none", ws.RowBreaks.Brk)
	}
	if len(ws.ColBreaks.Brk) != 0 {
		t.Fatalf("column breaks after removal = %+v, want none", ws.ColBreaks.Brk)
	}
	// Removing a break that was never inserted is not an error.
	if err := f.RemovePageBreak("Sheet1", "H9"); err != nil {
		t.Fatalf("RemovePageBreak for an absent break: %v", err)
	}
}

func TestPageBreaksRejectAnUnknownSheetOrMalformedCell(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.InsertPageBreak("NoSuchSheet", "A2"); err == nil {
		t.Fatal("InsertPageBreak on an absent sheet returned no error")
	}
	if err := f.InsertPageBreak("Sheet1", "ZZZ"); err == nil {
		t.Fatal("InsertPageBreak at a malformed cell reference returned no error")
	}
	if err := f.RemovePageBreak("NoSuchSheet", "A2"); err == nil {
		t.Fatal("RemovePageBreak on an absent sheet returned no error")
	}
	if err := f.RemovePageBreak("Sheet1", "ZZZ"); err == nil {
		t.Fatal("RemovePageBreak at a malformed cell reference returned no error")
	}
}
