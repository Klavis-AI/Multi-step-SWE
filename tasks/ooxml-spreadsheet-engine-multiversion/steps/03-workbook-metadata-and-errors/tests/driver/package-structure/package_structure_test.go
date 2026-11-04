// Scored group: package-structure (milestone 03, the error-return migration).
//
// The OPC container established by milestone 01 must still be produced
// correctly now that the writer path is reached through error-returning
// mutators and new parts (core properties, defined names) are written.
// Normative sources: TEST_INTERFACE.md sections 4.01 and 4.03; ECMA-376
// Part 2, 7.2.3 Mapping media types to parts and 6.5 Relationships part.
package packagestructure

import (
	"archive/zip"
	"bytes"
	"io/ioutil"
	"path/filepath"
	"strings"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

func partsOf(t *testing.T, data []byte) map[string]string {
	t.Helper()
	zr, err := zip.NewReader(bytes.NewReader(data), int64(len(data)))
	if err != nil {
		t.Fatalf("the saved workbook is not a readable zip package: %v", err)
	}
	out := make(map[string]string, len(zr.File))
	for _, zf := range zr.File {
		rc, err := zf.Open()
		if err != nil {
			t.Fatalf("open %s: %v", zf.Name, err)
		}
		b, err := ioutil.ReadAll(rc)
		rc.Close()
		if err != nil {
			t.Fatalf("read %s: %v", zf.Name, err)
		}
		out[zf.Name] = string(b)
	}
	return out
}

func savedParts(t *testing.T, f *seedcalc.File) map[string]string {
	t.Helper()
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	return partsOf(t, buf.Bytes())
}

func TestSavedPackageHoldsTheRequiredParts(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetCellValue("Sheet1", "A1", "value"); err != nil {
		t.Fatalf("SetCellValue: %v", err)
	}
	parts := savedParts(t, f)
	for _, name := range []string{
		"[Content_Types].xml",
		"_rels/.rels",
		"docProps/app.xml",
		"docProps/core.xml",
		"xl/workbook.xml",
		"xl/_rels/workbook.xml.rels",
		"xl/worksheets/sheet1.xml",
		"xl/styles.xml",
		"xl/theme/theme1.xml",
	} {
		if _, ok := parts[name]; !ok {
			t.Errorf("the saved package has no %s part", name)
		}
	}
	// Part names are stored without a leading slash, as the packaging
	// convention requires.
	for name := range parts {
		if strings.HasPrefix(name, "/") {
			t.Errorf("part name %q begins with a slash", name)
		}
	}
}

func TestContentTypesDeclaresEveryPart(t *testing.T) {
	f := seedcalc.NewFile()
	f.NewSheet("Second")
	parts := savedParts(t, f)
	ct := parts["[Content_Types].xml"]
	for _, want := range []string{
		`Extension="rels"`,
		`Extension="xml"`,
		`PartName="/xl/workbook.xml"`,
		`PartName="/xl/worksheets/sheet1.xml"`,
		`PartName="/xl/worksheets/sheet2.xml"`,
		`PartName="/docProps/core.xml"`,
		`PartName="/docProps/app.xml"`,
	} {
		if !strings.Contains(ct, want) {
			t.Errorf("[Content_Types].xml does not contain %s", want)
		}
	}
}

func TestRootRelationshipsPointAtTheWorkbook(t *testing.T) {
	parts := savedParts(t, seedcalc.NewFile())
	rels := parts["_rels/.rels"]
	if !strings.Contains(rels, "xl/workbook.xml") {
		t.Error("_rels/.rels does not relate the package to xl/workbook.xml")
	}
	if !strings.Contains(rels, "officeDocument") {
		t.Error("_rels/.rels declares no officeDocument relationship")
	}
	if !strings.Contains(rels, "docProps/core.xml") {
		t.Error("_rels/.rels does not relate the package to its core properties")
	}
	wbRels := parts["xl/_rels/workbook.xml.rels"]
	if !strings.Contains(wbRels, "worksheets/sheet1.xml") {
		t.Error("xl/_rels/workbook.xml.rels does not relate the workbook to its first worksheet")
	}
	if !strings.Contains(wbRels, "styles.xml") {
		t.Error("xl/_rels/workbook.xml.rels does not relate the workbook to its style sheet")
	}
}

func TestSaveAsAndOpenFileRoundTrip(t *testing.T) {
	path := filepath.Join(t.TempDir(), "book.xlsx")
	f := seedcalc.NewFile()
	if err := f.SetCellValue("Sheet1", "A1", "round trip"); err != nil {
		t.Fatalf("SetCellValue: %v", err)
	}
	if err := f.SetDocProps(&seedcalc.DocProperties{Title: "Saved"}); err != nil {
		t.Fatalf("SetDocProps: %v", err)
	}
	if err := f.SaveAs(path); err != nil {
		t.Fatalf("SaveAs: %v", err)
	}

	g, err := seedcalc.OpenFile(path)
	if err != nil {
		t.Fatalf("OpenFile: %v", err)
	}
	if g == nil {
		t.Fatal("OpenFile returned a nil *File with a nil error")
	}
	if v, err := g.GetCellValue("Sheet1", "A1"); err != nil || v != "round trip" {
		t.Errorf("A1 after the round trip = (%q, %v), want (\"round trip\", nil)", v, err)
	}
	props, err := g.GetDocProps()
	if err != nil {
		t.Fatalf("GetDocProps: %v", err)
	}
	if props.Title != "Saved" {
		t.Errorf("Title after the round trip = %q, want %q", props.Title, "Saved")
	}
	// Saving the reopened workbook again still produces a readable package.
	if err := g.Save(); err != nil {
		t.Fatalf("Save: %v", err)
	}
	data, err := ioutil.ReadFile(path)
	if err != nil {
		t.Fatalf("ReadFile: %v", err)
	}
	if _, ok := partsOf(t, data)["xl/workbook.xml"]; !ok {
		t.Error("the re-saved package has no xl/workbook.xml part")
	}
}

func TestNewSheetAddsAPartAndARelationship(t *testing.T) {
	f := seedcalc.NewFile()
	before := savedParts(t, f)
	if _, ok := before["xl/worksheets/sheet2.xml"]; ok {
		t.Fatal("a new workbook already has a second worksheet part")
	}
	if idx := f.NewSheet("Second"); idx != 2 {
		t.Fatalf("NewSheet = %d, want 2", idx)
	}
	if err := f.SetCellValue("Second", "A1", "on the second sheet"); err != nil {
		t.Fatalf("SetCellValue: %v", err)
	}
	after := savedParts(t, f)
	if _, ok := after["xl/worksheets/sheet2.xml"]; !ok {
		t.Error("NewSheet did not add xl/worksheets/sheet2.xml")
	}
	if !strings.Contains(after["xl/workbook.xml"], `name="Second"`) {
		t.Error("xl/workbook.xml does not list the new worksheet")
	}
	if !strings.Contains(after["xl/_rels/workbook.xml.rels"], "worksheets/sheet2.xml") {
		t.Error("the workbook relationships do not mention the new worksheet part")
	}
}
