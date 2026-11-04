// Scored group: sheet-lifecycle.
//
// Creating, copying, deleting and activating worksheets. Every operation has
// to keep four places consistent: the worksheet part itself, its Override in
// [Content_Types].xml, the relationship in xl/_rels/workbook.xml.rels, and the
// sheet element in xl/workbook.xml (ECMA-376 Part 1 clauses 12.3.24 and
// 18.2.19, and ECMA-376 Part 2 clause 9 for the relationship part).
package sheetlifecycle

import (
	"archive/zip"
	"bytes"
	"encoding/xml"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

const relationshipsNS = "http://schemas.openxmlformats.org/officeDocument/2006/relationships"

type pkg struct {
	names map[string]bool
	parts map[string][]byte
}

func write(t *testing.T, f *seedcalc.File) pkg {
	t.Helper()
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	zr, err := zip.NewReader(bytes.NewReader(buf.Bytes()), int64(buf.Len()))
	if err != nil {
		t.Fatalf("written package is not a ZIP container: %v", err)
	}
	p := pkg{names: map[string]bool{}, parts: map[string][]byte{}}
	for _, e := range zr.File {
		p.names[e.Name] = true
		rc, err := e.Open()
		if err != nil {
			t.Fatalf("open part %s: %v", e.Name, err)
		}
		var b bytes.Buffer
		if _, err := b.ReadFrom(rc); err != nil {
			t.Fatalf("read part %s: %v", e.Name, err)
		}
		rc.Close()
		p.parts[e.Name] = b.Bytes()
	}
	return p
}

func (p pkg) part(t *testing.T, name string) []byte {
	t.Helper()
	b, ok := p.parts[name]
	if !ok {
		t.Fatalf("package has no part %s", name)
	}
	return b
}

type workbook struct {
	BookViews struct {
		WorkbookView []struct {
			ActiveTab string `xml:"activeTab,attr"`
		} `xml:"workbookView"`
	} `xml:"bookViews"`
	Sheets struct {
		Sheet []struct {
			Name string `xml:"name,attr"`
			ID   string `xml:"http://schemas.openxmlformats.org/officeDocument/2006/relationships id,attr"`
		} `xml:"sheet"`
	} `xml:"sheets"`
}

type relationships struct {
	Relationship []struct {
		ID     string `xml:"Id,attr"`
		Type   string `xml:"Type,attr"`
		Target string `xml:"Target,attr"`
	} `xml:"Relationship"`
}

type contentTypes struct {
	Override []struct {
		PartName    string `xml:"PartName,attr"`
		ContentType string `xml:"ContentType,attr"`
	} `xml:"Override"`
}

type worksheet struct {
	SheetViews struct {
		SheetView []struct {
			TabSelected string `xml:"tabSelected,attr"`
		} `xml:"sheetView"`
	} `xml:"sheetViews"`
}

func decode(t *testing.T, b []byte, v interface{}) {
	t.Helper()
	if err := xml.Unmarshal(b, v); err != nil {
		t.Fatalf("part is not well-formed XML: %v", err)
	}
}

func TestNewSheetAddsAWorksheetPartAndAllItsDeclarations(t *testing.T) {
	f := seedcalc.NewFile()
	f.NewSheet(2, "Beta")
	p := write(t, f)

	if !p.names["xl/worksheets/sheet2.xml"] {
		t.Fatal("the package has no second worksheet part")
	}

	var ct contentTypes
	decode(t, p.part(t, "[Content_Types].xml"), &ct)
	found := false
	for _, o := range ct.Override {
		if o.PartName == "/xl/worksheets/sheet2.xml" {
			found = true
			if o.ContentType != "application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml" {
				t.Errorf("the new worksheet is declared as %q", o.ContentType)
			}
		}
	}
	if !found {
		t.Error("[Content_Types].xml has no Override for the new worksheet part")
	}

	var wb workbook
	decode(t, p.part(t, "xl/workbook.xml"), &wb)
	if len(wb.Sheets.Sheet) != 2 {
		t.Fatalf("xl/workbook.xml lists %d sheets, want 2", len(wb.Sheets.Sheet))
	}
	var rid string
	for _, s := range wb.Sheets.Sheet {
		if s.Name == "Beta" {
			rid = s.ID
		}
	}
	if rid == "" {
		t.Fatal("xl/workbook.xml has no sheet named Beta with a relationship identifier")
	}

	var rels relationships
	decode(t, p.part(t, "xl/_rels/workbook.xml.rels"), &rels)
	resolved := ""
	for _, r := range rels.Relationship {
		if r.ID == rid {
			resolved = r.Target
			if r.Type != relationshipsNS+"/worksheet" {
				t.Errorf("relationship %s has type %q, want a worksheet relationship", rid, r.Type)
			}
		}
	}
	if resolved != "worksheets/sheet2.xml" {
		t.Errorf("relationship %s resolves to %q, want %q", rid, resolved, "worksheets/sheet2.xml")
	}
}

func TestSheetNamesAndIndexesAgree(t *testing.T) {
	f := seedcalc.NewFile()
	f.NewSheet(2, "Beta")
	f.NewSheet(3, "Gamma")

	m := f.GetSheetMap()
	if len(m) != 3 {
		t.Fatalf("GetSheetMap has %d entries, want 3: %v", len(m), m)
	}
	for index, name := range m {
		if got := f.GetSheetName(index); got != name {
			t.Errorf("GetSheetName(%d) = %q, but GetSheetMap says %q", index, got, name)
		}
		if got := f.GetSheetIndex(name); got != index {
			t.Errorf("GetSheetIndex(%q) = %d, but GetSheetMap says %d", name, got, index)
		}
	}
	if got := f.GetSheetIndex("No Such Sheet"); got != 0 {
		t.Errorf("GetSheetIndex on an unknown name = %d, want 0", got)
	}
	if got := f.GetSheetName(99); got != "" {
		t.Errorf("GetSheetName(99) = %q, want the empty string", got)
	}
}

func TestCopySheetDuplicatesTheSourceContent(t *testing.T) {
	f := seedcalc.NewFile()
	f.SetCellValue("Sheet1", "A1", "source")
	f.SetCellValue("Sheet1", "B2", 17)
	f.NewSheet(2, "Beta")

	if err := f.CopySheet(1, 2); err != nil {
		t.Fatalf("CopySheet(1, 2): %v", err)
	}
	if got := f.GetCellValue("Sheet2", "A1"); got != "source" {
		t.Errorf("the copy's A1 = %q, want %q", got, "source")
	}
	if got := f.GetCellValue("Sheet2", "B2"); got != "17" {
		t.Errorf("the copy's B2 = %q, want %q", got, "17")
	}
	// The copy is independent of the original.
	f.SetCellValue("Sheet2", "A1", "changed")
	if got := f.GetCellValue("Sheet1", "A1"); got != "source" {
		t.Errorf("writing to the copy changed the source: A1 = %q", got)
	}
}

func TestCopySheetRejectsIndexesItCannotResolve(t *testing.T) {
	f := seedcalc.NewFile()
	f.NewSheet(2, "Beta")

	for _, c := range []struct {
		from, to int
		why      string
	}{
		{0, 2, "a source index below the first sheet"},
		{1, 0, "a target index below the first sheet"},
		{1, 9, "a target index past the last sheet"},
		{9, 1, "a source index past the last sheet"},
		{1, 1, "a source and target that are the same sheet"},
	} {
		if err := f.CopySheet(c.from, c.to); err == nil {
			t.Errorf("CopySheet(%d, %d) accepted %s", c.from, c.to, c.why)
		}
	}
}

func TestDeleteSheetRemovesTheSheetFromTheWorkbook(t *testing.T) {
	f := seedcalc.NewFile()
	f.NewSheet(2, "Beta")
	f.NewSheet(3, "Gamma")

	f.DeleteSheet("Beta")

	for _, name := range f.GetSheetMap() {
		if name == "Beta" {
			t.Fatalf("GetSheetMap still reports the deleted sheet: %v", f.GetSheetMap())
		}
	}
	if got := f.GetSheetIndex("Beta"); got != 0 {
		t.Errorf("GetSheetIndex(\"Beta\") = %d after deletion, want 0", got)
	}

	p := write(t, f)
	var wb workbook
	decode(t, p.part(t, "xl/workbook.xml"), &wb)
	if len(wb.Sheets.Sheet) != 2 {
		t.Fatalf("xl/workbook.xml lists %d sheets after deleting one of three, want 2", len(wb.Sheets.Sheet))
	}
	ids := map[string]bool{}
	for _, s := range wb.Sheets.Sheet {
		if s.Name == "Beta" {
			t.Error("xl/workbook.xml still lists the deleted sheet")
		}
		ids[s.ID] = true
	}
	var rels relationships
	decode(t, p.part(t, "xl/_rels/workbook.xml.rels"), &rels)
	for _, r := range rels.Relationship {
		if r.Type != relationshipsNS+"/worksheet" {
			continue
		}
		if !ids[r.ID] {
			t.Errorf("xl/_rels/workbook.xml.rels still carries worksheet relationship %s -> %s for a sheet the workbook no longer lists", r.ID, r.Target)
		}
	}
	if got := f.GetSheetName(f.GetSheetIndex("Gamma")); got != "Gamma" {
		t.Errorf("the surviving sheet is no longer reachable by name: %q", got)
	}
}

func TestDeleteSheetLeavesTheWorkbookIntactWhenItCannotApply(t *testing.T) {
	f := seedcalc.NewFile()
	f.NewSheet(2, "Beta")

	before := f.GetSheetMap()
	f.DeleteSheet("No Such Sheet")
	after := f.GetSheetMap()
	if len(after) != len(before) {
		t.Errorf("deleting an unknown sheet changed the workbook: %v then %v", before, after)
	}

	// A workbook must keep at least one worksheet.
	one := seedcalc.NewFile()
	one.DeleteSheet("Sheet1")
	if got := len(one.GetSheetMap()); got != 1 {
		t.Errorf("deleting the only worksheet left %d sheets, want 1", got)
	}
}

func TestSetActiveSheetRecordsTheActiveTab(t *testing.T) {
	f := seedcalc.NewFile()
	f.NewSheet(2, "Beta")
	f.SetActiveSheet(2)
	p := write(t, f)

	var wb workbook
	decode(t, p.part(t, "xl/workbook.xml"), &wb)
	if len(wb.BookViews.WorkbookView) == 0 {
		t.Fatal("xl/workbook.xml declares no workbookView")
	}
	// activeTab is zero-based, so the second sheet is tab 1.
	if got := wb.BookViews.WorkbookView[0].ActiveTab; got != "1" {
		t.Errorf("workbookView activeTab = %q after activating the second sheet, want %q", got, "1")
	}

	selected := func(part string) bool {
		var ws worksheet
		decode(t, p.part(t, part), &ws)
		if len(ws.SheetViews.SheetView) == 0 {
			return false
		}
		v := ws.SheetViews.SheetView[0].TabSelected
		return v == "true" || v == "1"
	}
	if selected("xl/worksheets/sheet1.xml") {
		t.Error("the first worksheet is still marked as the selected tab")
	}
	if !selected("xl/worksheets/sheet2.xml") {
		t.Error("the activated worksheet is not marked as the selected tab")
	}
}
