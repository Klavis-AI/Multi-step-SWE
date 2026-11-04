// Scored group: name-keyed-api.
//
// The migration this milestone makes: worksheets are created and addressed by
// name rather than by a caller-supplied index, row heights are addressed by
// the one-based row number the markup itself uses, and the worksheet mutators
// that can fail report it. Graded against ECMA-376 Part 1 clause 18.3.1.73
// (row: the r attribute is the one-based row index, ht is the row height and
// customHeight records that it was set explicitly) and clause 18.2.19 (sheet).
// Behaviour cases were checked against the upstream release this checkpoint
// tracks (tag v1.4.1).
package namekeyedapi

import (
	"archive/zip"
	"bytes"
	"encoding/xml"
	"io"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

type wsRow struct {
	R            int      `xml:"r,attr"`
	Ht           *float64 `xml:"ht,attr"`
	CustomHeight bool     `xml:"customHeight,attr"`
}

type worksheet struct {
	Rows []wsRow `xml:"sheetData>row"`
}

func partBytes(t *testing.T, f *seedcalc.File, name string) []byte {
	t.Helper()
	buf, err := f.WriteToBuffer()
	if err != nil {
		t.Fatalf("WriteToBuffer: %v", err)
	}
	b := buf.Bytes()
	zr, err := zip.NewReader(bytes.NewReader(b), int64(len(b)))
	if err != nil {
		t.Fatalf("written package is not a ZIP container: %v", err)
	}
	for _, fh := range zr.File {
		if fh.Name != name {
			continue
		}
		rc, err := fh.Open()
		if err != nil {
			t.Fatalf("open part %s: %v", name, err)
		}
		defer rc.Close()
		var out bytes.Buffer
		if _, err := io.Copy(&out, rc); err != nil {
			t.Fatalf("read part %s: %v", name, err)
		}
		return out.Bytes()
	}
	t.Fatalf("written package has no part %s", name)
	return nil
}

func decodeSheet(t *testing.T, f *seedcalc.File, part string) worksheet {
	t.Helper()
	var ws worksheet
	if err := xml.Unmarshal(partBytes(t, f, part), &ws); err != nil {
		t.Fatalf("%s is not well-formed XML: %v", part, err)
	}
	return ws
}

func TestNewSheetTakesANameAndReturnsAnIndexThatResolvesBack(t *testing.T) {
	f := seedcalc.NewFile()
	idx := f.NewSheet("Budget")
	if idx <= 0 {
		t.Fatalf("NewSheet(%q) = %d, want a positive worksheet index", "Budget", idx)
	}
	if got := f.GetSheetName(idx); got != "Budget" {
		t.Errorf("GetSheetName(NewSheet(%q)) = %q, want %q", "Budget", got, "Budget")
	}
	if got := f.GetSheetIndex("Budget"); got != idx {
		t.Errorf("GetSheetIndex(%q) = %d, want the index NewSheet returned (%d)", "Budget", got, idx)
	}
	m := f.GetSheetMap()
	if len(m) != 2 {
		t.Fatalf("GetSheetMap() has %d entries after one NewSheet, want 2: %v", len(m), m)
	}
	if m[idx] != "Budget" {
		t.Errorf("GetSheetMap()[%d] = %q, want %q", idx, m[idx], "Budget")
	}
}

func TestNewSheetOnAnExistingNameDoesNotAddAWorksheet(t *testing.T) {
	f := seedcalc.NewFile()
	f.SetCellValue("Sheet1", "A1", "keep me")
	before := f.GetSheetIndex("Sheet1")

	idx := f.NewSheet("Sheet1")
	if got := f.GetSheetIndex("Sheet1"); got != before {
		t.Errorf("GetSheetIndex(%q) moved from %d to %d after NewSheet on an existing name", "Sheet1", before, got)
	}
	if m := f.GetSheetMap(); len(m) != 1 {
		t.Errorf("GetSheetMap() has %d entries, want 1 -- NewSheet must not duplicate an existing name: %v", len(m), m)
	}
	if got := f.GetSheetName(idx); got != "Sheet1" {
		t.Errorf("GetSheetName(%d) = %q, want %q", idx, got, "Sheet1")
	}
	if got := f.GetCellValue("Sheet1", "A1"); got != "keep me" {
		t.Errorf("A1 = %q after NewSheet on an existing name, want %q -- the worksheet must not be recreated", got, "keep me")
	}
}

func TestRowHeightIsAddressedByTheOneBasedRowNumber(t *testing.T) {
	f := seedcalc.NewFile()
	f.SetRowHeight("Sheet1", 1, 50)

	ws := decodeSheet(t, f, "xl/worksheets/sheet1.xml")
	var found bool
	for _, r := range ws.Rows {
		if r.R != 1 {
			if r.Ht != nil {
				t.Errorf("row r=%d carries ht=%v; the height was set for row 1", r.R, *r.Ht)
			}
			continue
		}
		found = true
		if r.Ht == nil {
			t.Fatal("row r=1 carries no ht attribute after SetRowHeight(sheet, 1, 50)")
		}
		if *r.Ht != 50 {
			t.Errorf("row r=1 ht = %v, want 50", *r.Ht)
		}
		if !r.CustomHeight {
			t.Error("row r=1 does not record customHeight after an explicit SetRowHeight")
		}
	}
	if !found {
		t.Fatalf("written worksheet has no row with r=1: %+v", ws.Rows)
	}
}

func TestRowHeightReadsBackFromTheSameRowNumber(t *testing.T) {
	f := seedcalc.NewFile()
	f.SetRowHeight("Sheet1", 3, 77.5)

	if got := f.GetRowHeight("Sheet1", 3); got != 77.5 {
		t.Errorf("GetRowHeight(sheet, 3) = %v, want 77.5", got)
	}
	if got := f.GetRowHeight("Sheet1", 2); got == 77.5 {
		t.Errorf("GetRowHeight(sheet, 2) = %v; the height was set on row 3", got)
	}

	ws := decodeSheet(t, f, "xl/worksheets/sheet1.xml")
	for _, r := range ws.Rows {
		if r.R == 3 {
			if r.Ht == nil || *r.Ht != 77.5 {
				t.Errorf("row r=3 ht = %v, want 77.5", r.Ht)
			}
			return
		}
	}
	t.Fatalf("written worksheet has no row with r=3: %+v", ws.Rows)
}

func TestWorksheetMutatorsReportMalformedInputThroughError(t *testing.T) {
	f := seedcalc.NewFile()
	const broken = `{`
	if err := f.AddChart("Sheet1", "E1", broken); err == nil {
		t.Error("AddChart accepted a malformed format descriptor")
	}
	if err := f.AddComment("Sheet1", "A1", broken); err == nil {
		t.Error("AddComment accepted a malformed format descriptor")
	}
	if err := f.AddShape("Sheet1", "A1", broken); err == nil {
		t.Error("AddShape accepted a malformed format descriptor")
	}
	if err := f.AddTable("Sheet1", "A1", "B2", broken); err == nil {
		t.Error("AddTable accepted a malformed format descriptor")
	}
	if err := f.SetConditionalFormat("Sheet1", "A1:A5", broken); err == nil {
		t.Error("SetConditionalFormat accepted a malformed format descriptor")
	}
}

func TestEveryWorksheetIsAddressedByItsOwnName(t *testing.T) {
	f := seedcalc.NewFile()
	f.NewSheet("Second")
	f.SetCellValue("Sheet1", "A1", "on one")
	f.SetCellValue("Second", "A1", "on two")
	f.SetRowHeight("Second", 1, 42)
	f.SetColWidth("Second", "B", "B", 33)

	if got := f.GetCellValue("Sheet1", "A1"); got != "on one" {
		t.Errorf("Sheet1!A1 = %q, want %q", got, "on one")
	}
	if got := f.GetCellValue("Second", "A1"); got != "on two" {
		t.Errorf("Second!A1 = %q, want %q", got, "on two")
	}
	if got := f.GetRowHeight("Second", 1); got != 42 {
		t.Errorf("GetRowHeight(%q, 1) = %v, want 42", "Second", got)
	}
	if got := f.GetRowHeight("Sheet1", 1); got == 42 {
		t.Errorf("GetRowHeight(%q, 1) = %v; the height was set on another worksheet", "Sheet1", got)
	}
	if got := f.GetColWidth("Second", "B"); got != 33 {
		t.Errorf("GetColWidth(%q, \"B\") = %v, want 33", "Second", got)
	}
	if got := f.GetColWidth("Sheet1", "B"); got == 33 {
		t.Errorf("GetColWidth(%q, \"B\") = %v; the width was set on another worksheet", "Sheet1", got)
	}

	// A second worksheet must reach the package as its own part.
	if b := partBytes(t, f, "xl/worksheets/sheet2.xml"); len(b) == 0 {
		t.Error("the worksheet created by name produced an empty part")
	}
}
