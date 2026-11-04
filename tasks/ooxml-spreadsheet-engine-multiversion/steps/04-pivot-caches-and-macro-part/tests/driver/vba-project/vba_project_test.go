// Scored group: vba-project (milestone 04, "Pivot caches, sparklines and the
// macro part").
//
// Normative sources:
//   - TEST_INTERFACE.md section 4.04 -- "The macro part".
//   - ECMA-376 Part 2, 7.2.3.2.4 Default element and 7.2.3.2.5 Override
//     element -- a package declares a part's media type through a default
//     mapping for its extension or an override for its part name, and an
//     override wins over a default.
//   - ECMA-376 Part 2, 6.5.2 Relationships part and 6.5.3 Relationship markup.
//   - ECMA-376 Part 1, 18.3.1.82 sheetPr (Sheet Properties) for the codeName
//     attribute that macro code uses to address a sheet.
//   - Oracle v2.0.2 for the macro-enabled media types, which ECMA-376 does not
//     define.
//   - Cases cross-checked against the upstream test corpus at tag v2.0.2
//     (excelize_test.go: TestAddVBAProject).
package vbaproject

import (
	"archive/zip"
	"bytes"
	"encoding/xml"
	"io/ioutil"
	"path/filepath"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

func savedParts(t *testing.T, f *seedcalc.File) map[string]string {
	t.Helper()
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	zr, err := zip.NewReader(bytes.NewReader(buf.Bytes()), int64(buf.Len()))
	if err != nil {
		t.Fatalf("the saved workbook is not a readable package: %v", err)
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

type contentTypes struct {
	Default []struct {
		Extension   string `xml:"Extension,attr"`
		ContentType string `xml:"ContentType,attr"`
	} `xml:"Default"`
	Override []struct {
		PartName    string `xml:"PartName,attr"`
		ContentType string `xml:"ContentType,attr"`
	} `xml:"Override"`
}

func parseContentTypes(t *testing.T, parts map[string]string) contentTypes {
	t.Helper()
	var ct contentTypes
	if err := xml.Unmarshal([]byte(parts["[Content_Types].xml"]), &ct); err != nil {
		t.Fatalf("[Content_Types].xml is not well-formed: %v", err)
	}
	return ct
}

type relationships struct {
	Rel []struct {
		ID     string `xml:"Id,attr"`
		Type   string `xml:"Type,attr"`
		Target string `xml:"Target,attr"`
	} `xml:"Relationship"`
}

func parseRels(t *testing.T, parts map[string]string, name string) relationships {
	t.Helper()
	var r relationships
	raw, ok := parts[name]
	if !ok {
		t.Fatalf("the package has no %s part", name)
	}
	if err := xml.Unmarshal([]byte(raw), &r); err != nil {
		t.Fatalf("%s is not well-formed relationship markup: %v", name, err)
	}
	return r
}

// macroBytes is a short, deliberately non-UTF-8 payload standing in for a
// compiled VBA storage. Its exact bytes are what must survive a round trip.
func macroBytes() []byte {
	return []byte{
		0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1,
		0x00, 0x00, 0xFF, 0xFE, 0x7F, 0x80, 0x0A, 0x0D,
	}
}

func writeMacro(t *testing.T, name string) string {
	t.Helper()
	p := filepath.Join(t.TempDir(), name)
	if err := ioutil.WriteFile(p, macroBytes(), 0o600); err != nil {
		t.Fatalf("writing the fixture: %v", err)
	}
	return p
}

func TestAddVBAProjectStoresThePartVerbatim(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.AddVBAProject(writeMacro(t, "vbaProject.bin")); err != nil {
		t.Fatalf("AddVBAProject: %v", err)
	}
	parts := savedParts(t, f)
	got, ok := parts["xl/vbaProject.bin"]
	if !ok {
		t.Fatal("the package has no xl/vbaProject.bin part")
	}
	if !bytes.Equal([]byte(got), macroBytes()) {
		t.Errorf("the stored macro part is %d bytes and differs from the input", len(got))
	}
}

func TestWorkbookPartBecomesMacroEnabled(t *testing.T) {
	f := seedcalc.NewFile()
	ct := parseContentTypes(t, savedParts(t, f))
	for _, o := range ct.Override {
		if o.PartName == "/xl/workbook.xml" &&
			o.ContentType != "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml" {
			t.Fatalf("a plain workbook already declares %q", o.ContentType)
		}
	}
	if err := f.AddVBAProject(writeMacro(t, "vbaProject.bin")); err != nil {
		t.Fatalf("AddVBAProject: %v", err)
	}
	ct = parseContentTypes(t, savedParts(t, f))
	var found bool
	for _, o := range ct.Override {
		if o.PartName != "/xl/workbook.xml" {
			continue
		}
		found = true
		if o.ContentType != "application/vnd.ms-excel.sheet.macroEnabled.main+xml" {
			t.Errorf("workbook part content type = %q, want the macro-enabled type", o.ContentType)
		}
	}
	if !found {
		t.Error("[Content_Types].xml has no override for /xl/workbook.xml")
	}
}

func TestBinExtensionGetsADefaultMediaType(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.AddVBAProject(writeMacro(t, "vbaProject.bin")); err != nil {
		t.Fatalf("AddVBAProject: %v", err)
	}
	ct := parseContentTypes(t, savedParts(t, f))
	var got string
	for _, d := range ct.Default {
		if d.Extension == "bin" {
			got = d.ContentType
		}
	}
	if got != "application/vnd.ms-office.vbaProject" {
		t.Errorf("the default media type for the bin extension = %q", got)
	}
}

func TestWorkbookRelatesToTheMacroPart(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.AddVBAProject(writeMacro(t, "vbaProject.bin")); err != nil {
		t.Fatalf("AddVBAProject: %v", err)
	}
	parts := savedParts(t, f)
	rels := parseRels(t, parts, "xl/_rels/workbook.xml.rels")
	var n int
	for _, r := range rels.Rel {
		if r.Type != seedcalc.SourceRelationshipVBAProject {
			continue
		}
		n++
		if r.Target != "vbaProject.bin" {
			t.Errorf("the macro relationship targets %q, want vbaProject.bin", r.Target)
		}
	}
	if n != 1 {
		t.Errorf("the workbook declares %d macro relationships, want 1", n)
	}
	if seedcalc.SourceRelationshipVBAProject != "http://schemas.microsoft.com/office/2006/relationships/vbaProject" {
		t.Errorf("SourceRelationshipVBAProject = %q", seedcalc.SourceRelationshipVBAProject)
	}
}

func TestAddVBAProjectTwiceKeepsOneRelationship(t *testing.T) {
	f := seedcalc.NewFile()
	p := writeMacro(t, "vbaProject.bin")
	if err := f.AddVBAProject(p); err != nil {
		t.Fatalf("first AddVBAProject: %v", err)
	}
	if err := f.AddVBAProject(p); err != nil {
		t.Fatalf("second AddVBAProject: %v", err)
	}
	parts := savedParts(t, f)
	rels := parseRels(t, parts, "xl/_rels/workbook.xml.rels")
	var n int
	for _, r := range rels.Rel {
		if r.Type == seedcalc.SourceRelationshipVBAProject {
			n++
		}
	}
	if n != 1 {
		t.Errorf("after two calls the workbook declares %d macro relationships, want 1", n)
	}
	ct := parseContentTypes(t, parts)
	var defaults int
	for _, d := range ct.Default {
		if d.Extension == "bin" {
			defaults++
		}
	}
	if defaults != 1 {
		t.Errorf("after two calls the content type stream declares %d defaults for bin, want 1", defaults)
	}
}

func TestNonBinExtensionIsRejected(t *testing.T) {
	f := seedcalc.NewFile()
	p := writeMacro(t, "vbaProject.xml")
	if err := f.AddVBAProject(p); err == nil {
		t.Fatal("AddVBAProject accepted a file whose extension is not .bin")
	}
	if _, ok := savedParts(t, f)["xl/vbaProject.bin"]; ok {
		t.Error("a rejected macro project was stored anyway")
	}
}

func TestMissingMacroFileIsRejected(t *testing.T) {
	f := seedcalc.NewFile()
	missing := filepath.Join(t.TempDir(), "absent.bin")
	if err := f.AddVBAProject(missing); err == nil {
		t.Fatal("AddVBAProject accepted a path that does not exist")
	}
	if _, ok := savedParts(t, f)["xl/vbaProject.bin"]; ok {
		t.Error("a missing macro project produced a part anyway")
	}
}

func TestMacroProjectSurvivesSaveAndReopen(t *testing.T) {
	dir := t.TempDir()
	f := seedcalc.NewFile()
	if err := f.AddVBAProject(writeMacro(t, "vbaProject.bin")); err != nil {
		t.Fatalf("AddVBAProject: %v", err)
	}
	if err := f.SetSheetPrOptions("Sheet1", seedcalc.CodeName("Sheet1")); err != nil {
		t.Fatalf("SetSheetPrOptions: %v", err)
	}
	out := filepath.Join(dir, "book.xlsm")
	if err := f.SaveAs(out); err != nil {
		t.Fatalf("SaveAs: %v", err)
	}
	g, err := seedcalc.OpenFile(out)
	if err != nil {
		t.Fatalf("OpenFile: %v", err)
	}
	parts := savedParts(t, g)
	got, ok := parts["xl/vbaProject.bin"]
	if !ok {
		t.Fatal("the reopened package has no xl/vbaProject.bin part")
	}
	if !bytes.Equal([]byte(got), macroBytes()) {
		t.Error("the macro part did not survive a save and reopen byte for byte")
	}
	var code seedcalc.CodeName
	if err := g.GetSheetPrOptions("Sheet1", &code); err != nil {
		t.Fatalf("GetSheetPrOptions: %v", err)
	}
	if string(code) != "Sheet1" {
		t.Errorf("the sheet code name after a reopen = %q, want Sheet1", string(code))
	}
}
