// Scored group: opc-container-migration (milestone 02, new behaviour).
//
// The packaging layer established earlier, re-observed through the
// name-keyed interface: a package written after worksheets are created by
// name must still declare every part, resolve every relationship and read
// back to the values it was given.
//
// This is scored, not gated. Every case here is reached through spellings
// that arrive at this milestone -- WriteToBuffer, ReadZipReader and the
// one-argument NewSheet -- so none of it can be asserted of the workbook a
// no-op leaves behind. The signature-stable half of the same path is gated
// in package-container-regression.
//
// Graded against ECMA-376 Part 2 clause 7.3 (mapping a package to a ZIP
// file), clause 9.3 (the relationships part) and clause 10.1.2 (the content
// types stream), together with the part names ECMA-376 Part 1 clause 12.3
// fixes for SpreadsheetML.
package opccontainermigration

import (
	"archive/zip"
	"bytes"
	"encoding/xml"
	"io"
	"strings"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

func packageBytes(t *testing.T, f *seedcalc.File) []byte {
	t.Helper()
	buf, err := f.WriteToBuffer()
	if err != nil {
		t.Fatalf("WriteToBuffer: %v", err)
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

func TestWrittenPackageStillCarriesTheRequiredParts(t *testing.T) {
	f := seedcalc.NewFile()
	f.NewSheet("Second")
	zr := openZip(t, packageBytes(t, f))
	names := partNames(zr)
	for _, want := range []string{
		"[Content_Types].xml",
		"_rels/.rels",
		"xl/workbook.xml",
		"xl/_rels/workbook.xml.rels",
		"xl/worksheets/sheet1.xml",
		"xl/worksheets/sheet2.xml",
	} {
		if !names[want] {
			t.Errorf("written package is missing required part %s", want)
		}
	}
}

func TestContentTypesDeclaresEveryWorksheetAddedByName(t *testing.T) {
	f := seedcalc.NewFile()
	f.NewSheet("Second")
	zr := openZip(t, packageBytes(t, f))
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
		"/xl/worksheets/sheet2.xml": "application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml",
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

func TestRootRelationshipStillResolvesToTheWorkbookPart(t *testing.T) {
	f := seedcalc.NewFile()
	f.NewSheet("Second")
	zr := openZip(t, packageBytes(t, f))
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

func TestWorkbookRelationshipsResolveToWorksheetParts(t *testing.T) {
	f := seedcalc.NewFile()
	f.NewSheet("Second")
	zr := openZip(t, packageBytes(t, f))
	var rels relationships
	if err := xml.Unmarshal(readPart(t, zr, "xl/_rels/workbook.xml.rels"), &rels); err != nil {
		t.Fatalf("xl/_rels/workbook.xml.rels is not well-formed XML: %v", err)
	}
	const worksheetRel = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet"
	names := partNames(zr)
	seen := 0
	ids := map[string]bool{}
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
	if seen != 2 {
		t.Errorf("workbook declares %d worksheet relationships, want 2", seen)
	}
}

func TestValuesSurviveTheWriteReadRoundTripOnEveryWorksheet(t *testing.T) {
	f := seedcalc.NewFile()
	f.NewSheet("Second")
	f.SetCellValue("Sheet1", "A1", "text")
	f.SetCellValue("Sheet1", "B2", 42)
	f.SetCellFormula("Sheet1", "C3", "SUM(B2:B2)")
	f.SetCellValue("Second", "A1", "other")

	g, err := seedcalc.OpenReader(bytes.NewReader(packageBytes(t, f)))
	if err != nil {
		t.Fatalf("OpenReader on a package this library wrote: %v", err)
	}
	if got := g.GetCellValue("Sheet1", "A1"); got != "text" {
		t.Errorf("Sheet1!A1 = %q, want %q", got, "text")
	}
	if got := g.GetCellValue("Sheet1", "B2"); got != "42" {
		t.Errorf("Sheet1!B2 = %q, want %q", got, "42")
	}
	if got := g.GetCellFormula("Sheet1", "C3"); got != "SUM(B2:B2)" {
		t.Errorf("Sheet1!C3 formula = %q, want %q", got, "SUM(B2:B2)")
	}
	if got := g.GetCellValue("Second", "A1"); got != "other" {
		t.Errorf("Second!A1 = %q, want %q", got, "other")
	}
	if got := g.GetSheetIndex("Second"); got == 0 {
		t.Error("the worksheet created by name did not survive the round trip")
	}
	if _, err := seedcalc.OpenReader(strings.NewReader("this is not a package")); err == nil {
		t.Error("OpenReader accepted a byte stream that is not a ZIP container")
	}
}

func TestReadZipReaderReturnsThePartsAndTheWorksheetCount(t *testing.T) {
	f := seedcalc.NewFile()
	f.NewSheet("Second")
	b := packageBytes(t, f)

	parts, worksheets, err := seedcalc.ReadZipReader(openZip(t, b))
	if err != nil {
		t.Fatalf("ReadZipReader: %v", err)
	}
	if worksheets != 2 {
		t.Errorf("ReadZipReader reported %d worksheets, want 2", worksheets)
	}
	for _, want := range []string{"[Content_Types].xml", "xl/workbook.xml", "xl/worksheets/sheet1.xml"} {
		if _, ok := parts[want]; !ok {
			t.Errorf("ReadZipReader did not return part %s", want)
		}
	}
	if !bytes.Contains(parts["xl/workbook.xml"], []byte("<workbook")) {
		t.Error("the workbook part returned by ReadZipReader does not contain a workbook element")
	}

	g, err := seedcalc.OpenReader(bytes.NewReader(b))
	if err != nil {
		t.Fatalf("OpenReader: %v", err)
	}
	if !bytes.Equal(g.XLSX["xl/workbook.xml"], parts["xl/workbook.xml"]) {
		t.Error("the parts held by an opened file differ from what ReadZipReader returned")
	}
}
