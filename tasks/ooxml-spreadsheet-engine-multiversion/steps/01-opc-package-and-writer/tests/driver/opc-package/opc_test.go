// Scored group: opc-package.
//
// The Open Packaging Conventions layer: a written workbook must be a valid OPC
// package -- a ZIP whose [Content_Types].xml declares every part, whose
// relationship parts resolve, and which this library reads back to the values
// it wrote. Graded against ECMA-376 Part 2 clauses 9-10 (specs/ecma-376-part2)
// and the part names ECMA-376 Part 1 clause 12.3 fixes for SpreadsheetML.
package opcpackage

import (
	"archive/zip"
	"bytes"
	"encoding/xml"
	"strings"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// writeToBuffer saves f through the documented io.Writer path and returns the
// package bytes.
func writeToBuffer(t *testing.T, f *seedcalc.File) []byte {
	t.Helper()
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	if buf.Len() == 0 {
		t.Fatal("Write produced an empty package")
	}
	return buf.Bytes()
}

// openZip opens package bytes as an OPC (ZIP) container.
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
		if _, err := buf.ReadFrom(rc); err != nil {
			t.Fatalf("read part %s: %v", name, err)
		}
		return buf.Bytes()
	}
	t.Fatalf("package has no part %s", name)
	return nil
}

func TestNewFileHasOneDefaultWorksheet(t *testing.T) {
	f := seedcalc.NewFile()
	if got := f.GetSheetName(1); got != "Sheet1" {
		t.Errorf("GetSheetName(1) = %q, want %q", got, "Sheet1")
	}
	// The active sheet is addressed by index; the index base is not fixed by
	// this milestone, so assert that it resolves to the default worksheet
	// rather than asserting a literal.
	if got := f.GetSheetName(f.GetActiveSheetIndex()); got != "Sheet1" {
		t.Errorf("GetSheetName(GetActiveSheetIndex()) = %q, want %q", got, "Sheet1")
	}
	m := f.GetSheetMap()
	if len(m) != 1 {
		t.Fatalf("GetSheetMap() has %d entries, want 1: %v", len(m), m)
	}
}

func TestWrittenPackageCarriesTheRequiredParts(t *testing.T) {
	f := seedcalc.NewFile()
	zr := openZip(t, writeToBuffer(t, f))
	names := partNames(zr)
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

func TestContentTypesDeclaresTheWorkbookAndWorksheet(t *testing.T) {
	f := seedcalc.NewFile()
	zr := openZip(t, writeToBuffer(t, f))
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

func TestRootRelationshipResolvesToTheWorkbookPart(t *testing.T) {
	f := seedcalc.NewFile()
	zr := openZip(t, writeToBuffer(t, f))
	var rels struct {
		Rel []struct {
			ID     string `xml:"Id,attr"`
			Type   string `xml:"Type,attr"`
			Target string `xml:"Target,attr"`
		} `xml:"Relationship"`
	}
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
		target := strings.TrimPrefix(r.Target, "/")
		if !names[target] {
			t.Errorf("officeDocument relationship targets %q, which is not a part in the package", r.Target)
		}
	}
	if !found {
		t.Errorf("_rels/.rels declares no officeDocument relationship (%d relationships present)", len(rels.Rel))
	}
}

func TestValuesSurviveTheWriteReadRoundTrip(t *testing.T) {
	f := seedcalc.NewFile()
	f.SetCellValue("Sheet1", "A1", "text")
	f.SetCellValue("Sheet1", "B2", 42)
	f.SetCellFormula("Sheet1", "C3", "SUM(B2:B2)")

	b := writeToBuffer(t, f)
	g, err := seedcalc.OpenReader(bytes.NewReader(b))
	if err != nil {
		t.Fatalf("OpenReader on a package this library wrote: %v", err)
	}
	if got := g.GetCellValue("Sheet1", "A1"); got != "text" {
		t.Errorf("A1 = %q, want %q", got, "text")
	}
	if got := g.GetCellValue("Sheet1", "B2"); got != "42" {
		t.Errorf("B2 = %q, want %q", got, "42")
	}
	if got := g.GetCellFormula("Sheet1", "C3"); got != "SUM(B2:B2)" {
		t.Errorf("C3 formula = %q, want %q", got, "SUM(B2:B2)")
	}
}

func TestOpenReaderRejectsAPackageThatIsNotAZipContainer(t *testing.T) {
	if _, err := seedcalc.OpenReader(strings.NewReader("this is not a package")); err == nil {
		t.Error("OpenReader accepted a byte stream that is not a ZIP container")
	}
}
