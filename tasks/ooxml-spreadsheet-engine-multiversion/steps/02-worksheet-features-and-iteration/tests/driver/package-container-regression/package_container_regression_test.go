// Gate group: package-container-regression.
//
// The one deterministic end-to-end path milestone 01 established, carried
// forward: build a workbook, write it, and find a well-formed OPC package on
// the other side whose relationships resolve and whose values read back.
//
// Every call here is spelled with a signature that milestone 01 and
// milestone 02 share, so the group is a true veto rather than a disguised
// assertion about this milestone's work: WriteToBuffer, ReadZipReader and
// the one-argument NewSheet all arrive at 02 and are therefore priced in
// opc-container-migration, not gated here.
//
// Graded against ECMA-376 Part 2 clause 7.3 (mapping a package to a ZIP
// file), clause 10.1.2 (the content types stream) and clause 9.3 (the
// relationships part), together with the SpreadsheetML part names ECMA-376
// Part 1 clause 12.3 fixes.
package packagecontainerregression

import (
	"archive/zip"
	"bytes"
	"encoding/xml"
	"io"
	"strings"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// sample builds the workbook every case in this group starts from. It uses
// only the no-error mutator spellings milestone 01 shipped.
func sample() *seedcalc.File {
	f := seedcalc.NewFile()
	f.SetCellValue("Sheet1", "A1", "text")
	f.SetCellValue("Sheet1", "B1", 42)
	f.SetCellStr("Sheet1", "C1", "str")
	f.SetCellFormula("Sheet1", "D1", "SUM(B1:B1)")
	f.SetCellValue("Sheet1", "A2", "second row")
	return f
}

func packageBytes(t *testing.T, f *seedcalc.File) []byte {
	t.Helper()
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	if buf.Len() == 0 {
		t.Fatal("the writer produced an empty package")
	}
	return buf.Bytes()
}

func openZip(t *testing.T, b []byte) *zip.Reader {
	t.Helper()
	zr, err := zip.NewReader(bytes.NewReader(b), int64(len(b)))
	if err != nil {
		t.Fatalf("written package is not a readable ZIP container: %v", err)
	}
	return zr
}

func partNames(zr *zip.Reader) map[string]bool {
	m := make(map[string]bool, len(zr.File))
	for _, f := range zr.File {
		m[f.Name] = true
	}
	return m
}

func readPart(t *testing.T, zr *zip.Reader, name string) []byte {
	t.Helper()
	for _, f := range zr.File {
		if f.Name != name {
			continue
		}
		rc, err := f.Open()
		if err != nil {
			t.Fatalf("open part %s: %v", name, err)
		}
		defer rc.Close()
		var buf bytes.Buffer
		if _, err := io.Copy(&buf, rc); err != nil {
			t.Fatalf("read part %s: %v", name, err)
		}
		return buf.Bytes()
	}
	t.Fatalf("package has no part %s", name)
	return nil
}

type relationships struct {
	Rel []struct {
		ID     string `xml:"Id,attr"`
		Type   string `xml:"Type,attr"`
		Target string `xml:"Target,attr"`
	} `xml:"Relationship"`
}

func TestTheWrittenPackageCarriesTheRequiredParts(t *testing.T) {
	names := partNames(openZip(t, packageBytes(t, sample())))
	for _, want := range []string{
		"[Content_Types].xml",
		"_rels/.rels",
		"xl/workbook.xml",
		"xl/_rels/workbook.xml.rels",
		"xl/worksheets/sheet1.xml",
	} {
		if !names[want] {
			t.Errorf("written package is missing required part %s", want)
		}
	}
}

func TestTheContentTypesStreamDeclaresTheWorkbookAndTheWorksheet(t *testing.T) {
	zr := openZip(t, packageBytes(t, sample()))
	var ct struct {
		Defaults []struct {
			Extension   string `xml:"Extension,attr"`
			ContentType string `xml:"ContentType,attr"`
		} `xml:"Default"`
		Overrides []struct {
			PartName    string `xml:"PartName,attr"`
			ContentType string `xml:"ContentType,attr"`
		} `xml:"Override"`
	}
	if err := xml.Unmarshal(readPart(t, zr, "[Content_Types].xml"), &ct); err != nil {
		t.Fatalf("[Content_Types].xml is not well-formed XML: %v", err)
	}
	seenRels := false
	for _, d := range ct.Defaults {
		if strings.EqualFold(d.Extension, "rels") {
			seenRels = true
			if d.ContentType != "application/vnd.openxmlformats-package.relationships+xml" {
				t.Errorf("Default for rels has content type %q", d.ContentType)
			}
		}
	}
	if !seenRels {
		t.Error("[Content_Types].xml has no Default for the rels extension")
	}
	want := map[string]string{
		"/xl/workbook.xml":          "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml",
		"/xl/worksheets/sheet1.xml": "application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml",
	}
	for _, o := range ct.Overrides {
		if w, ok := want[o.PartName]; ok {
			if o.ContentType != w {
				t.Errorf("Override %s content type = %q, want %q", o.PartName, o.ContentType, w)
			}
			delete(want, o.PartName)
		}
	}
	for part := range want {
		t.Errorf("[Content_Types].xml has no Override for %s", part)
	}
}

func TestTheRootRelationshipResolvesToTheWorkbookPart(t *testing.T) {
	zr := openZip(t, packageBytes(t, sample()))
	var rels relationships
	if err := xml.Unmarshal(readPart(t, zr, "_rels/.rels"), &rels); err != nil {
		t.Fatalf("_rels/.rels is not well-formed XML: %v", err)
	}
	const officeDoc = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument"
	names := partNames(zr)
	found := false
	for _, r := range rels.Rel {
		if r.Type != officeDoc {
			continue
		}
		found = true
		if r.ID == "" {
			t.Error("officeDocument relationship has an empty Id")
		}
		if target := strings.TrimPrefix(r.Target, "/"); !names[target] {
			t.Errorf("officeDocument relationship targets %q, which is not a part in the package", r.Target)
		}
	}
	if !found {
		t.Errorf("_rels/.rels declares no officeDocument relationship (%d relationships present)", len(rels.Rel))
	}
}

func TestTheWorkbookRelationshipResolvesToTheWorksheetPart(t *testing.T) {
	zr := openZip(t, packageBytes(t, sample()))
	var rels relationships
	if err := xml.Unmarshal(readPart(t, zr, "xl/_rels/workbook.xml.rels"), &rels); err != nil {
		t.Fatalf("xl/_rels/workbook.xml.rels is not well-formed XML: %v", err)
	}
	const worksheetRel = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet"
	names := partNames(zr)
	ids := map[string]bool{}
	seen := 0
	for _, r := range rels.Rel {
		if r.ID == "" {
			t.Error("a workbook relationship has an empty Id")
		}
		if ids[r.ID] {
			t.Errorf("relationship Id %q occurs more than once", r.ID)
		}
		ids[r.ID] = true
		if r.Type != worksheetRel {
			continue
		}
		seen++
		if !names["xl/"+strings.TrimPrefix(r.Target, "/")] {
			t.Errorf("worksheet relationship targets %q, which is not a part in the package", r.Target)
		}
	}
	if seen != 1 {
		t.Errorf("workbook declares %d worksheet relationships, want 1", seen)
	}
}

func TestEveryXMLPartOpensWithAnXMLDeclaration(t *testing.T) {
	zr := openZip(t, packageBytes(t, sample()))
	checked := 0
	for _, part := range []string{"[Content_Types].xml", "_rels/.rels", "xl/workbook.xml", "xl/worksheets/sheet1.xml"} {
		body := readPart(t, zr, part)
		if !bytes.HasPrefix(body, []byte("<?xml ")) {
			t.Errorf("part %s does not open with an XML declaration: %.40q", part, body)
			continue
		}
		decl := body[:bytes.IndexByte(body, '>')+1]
		if !bytes.Contains(decl, []byte(`version="1.0"`)) {
			t.Errorf("part %s declares %q, which does not state version 1.0", part, decl)
		}
		if !bytes.Contains(decl, []byte(`encoding="UTF-8"`)) {
			t.Errorf("part %s declares %q, which does not state UTF-8", part, decl)
		}
		checked++
	}
	if checked != 4 {
		t.Errorf("checked %d parts, want 4", checked)
	}
}

func TestTheWorkbookSurvivesAWriteAndReadWithItsSheetIdentity(t *testing.T) {
	f := sample()
	f.SetActiveSheet(f.GetSheetIndex("Sheet1"))

	g, err := seedcalc.OpenReader(bytes.NewReader(packageBytes(t, f)))
	if err != nil {
		t.Fatalf("OpenReader on a package this library wrote: %v", err)
	}
	// The worksheet is addressed by the index the workbook itself reports,
	// not by a literal, so the case says nothing about how this milestone
	// happens to number its sheets.
	idx := g.GetSheetIndex("Sheet1")
	if got := g.GetSheetName(idx); got != "Sheet1" {
		t.Errorf("GetSheetName(GetSheetIndex(%q)) = %q, want %q", "Sheet1", got, "Sheet1")
	}
	if got := g.GetSheetMap(); len(got) != 1 || got[idx] != "Sheet1" {
		t.Errorf("GetSheetMap() = %v, want the single entry %d: Sheet1", got, idx)
	}
	if got := g.GetActiveSheetIndex(); got != idx {
		t.Errorf("GetActiveSheetIndex() = %d, want the index of the sheet that was made active, %d", got, idx)
	}
	for _, c := range []struct{ axis, want string }{
		{"A1", "text"}, {"B1", "42"}, {"C1", "str"}, {"A2", "second row"},
	} {
		if got := g.GetCellValue("Sheet1", c.axis); got != c.want {
			t.Errorf("Sheet1!%s = %q, want %q", c.axis, got, c.want)
		}
	}
	if got := g.GetCellFormula("Sheet1", "D1"); got != "SUM(B1:B1)" {
		t.Errorf("Sheet1!D1 formula = %q, want %q", got, "SUM(B1:B1)")
	}
	if got := g.GetRows("Sheet1"); len(got) != 2 {
		t.Errorf("GetRows returned %d rows, want 2: %v", len(got), got)
	}
	if _, err := seedcalc.OpenReader(strings.NewReader("this is not a package")); err == nil {
		t.Error("OpenReader accepted a byte stream that is not a ZIP container")
	}
}
