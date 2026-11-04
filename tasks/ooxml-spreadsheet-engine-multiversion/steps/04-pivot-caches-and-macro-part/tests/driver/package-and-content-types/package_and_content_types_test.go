// Gate group: package-and-content-types (milestone 04 regression).
//
// Milestone 04 widens what a package may contain -- pivot cache, pivot table
// and macro parts, each with its own media type -- and replaces the three
// relationship caches earlier checkpoints kept with a single part-keyed
// registry. A workbook that uses none of those features must still produce
// exactly the container earlier milestones established.
//
// Normative sources:
//   - TEST_INTERFACE.md 4.01.1 (the parts and declarations every written
//     package carries) and 4.03.13 (the container is still a valid OPC
//     package after the error migration).
//   - ECMA-376 Part 2, 6.2.2 Part names -- a part name is a sequence of
//     "/"-prefixed segments and two part names in one package shall not be
//     equivalent under ASCII case-insensitive matching.
//   - ECMA-376 Part 2, 6.5.2 Relationships part and 6.5.3 Relationship
//     markup -- the relationships of the package as a whole live in
//     /_rels/.rels, those of /a/b.xml in /a/_rels/b.xml.rels, and each
//     Relationship carries Id, Type and Target.
//   - ECMA-376 Part 2, 6.4 Resolving relative references.
//   - ECMA-376 Part 2, 7.2.3 Mapping media types to parts, 7.2.3.2.3 Types
//     element, 7.2.3.2.4 Default element and 7.2.3.2.5 Override element --
//     every part's media type comes from a default mapping for its extension
//     or from an override naming the part, and an override wins.
package packageandcontenttypes

import (
	"archive/zip"
	"bytes"
	"encoding/xml"
	"io/ioutil"
	"path"
	"strings"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// savedParts writes the workbook through the documented io.Writer path and
// returns every item of the resulting package.
func savedParts(t *testing.T, f *seedcalc.File) map[string]string {
	t.Helper()
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	return openParts(t, buf.Bytes())
}

func openParts(t *testing.T, b []byte) map[string]string {
	t.Helper()
	zr, err := zip.NewReader(bytes.NewReader(b), int64(len(b)))
	if err != nil {
		t.Fatalf("the saved workbook is not a readable package: %v", err)
	}
	out := make(map[string]string, len(zr.File))
	for _, zf := range zr.File {
		rc, err := zf.Open()
		if err != nil {
			t.Fatalf("open %s: %v", zf.Name, err)
		}
		raw, err := ioutil.ReadAll(rc)
		rc.Close()
		if err != nil {
			t.Fatalf("read %s: %v", zf.Name, err)
		}
		out[zf.Name] = string(raw)
	}
	return out
}

type relationship struct {
	ID         string `xml:"Id,attr"`
	Type       string `xml:"Type,attr"`
	Target     string `xml:"Target,attr"`
	TargetMode string `xml:"TargetMode,attr"`
}

type relationships struct {
	Rel []relationship `xml:"Relationship"`
}

func parseRels(t *testing.T, parts map[string]string, name string) relationships {
	t.Helper()
	raw, ok := parts[name]
	if !ok {
		t.Fatalf("the package has no %s part", name)
	}
	var r relationships
	if err := xml.Unmarshal([]byte(raw), &r); err != nil {
		t.Fatalf("%s is not well-formed relationship markup: %v", name, err)
	}
	return r
}

type mediaDefault struct {
	Extension   string `xml:"Extension,attr"`
	ContentType string `xml:"ContentType,attr"`
}

type mediaOverride struct {
	PartName    string `xml:"PartName,attr"`
	ContentType string `xml:"ContentType,attr"`
}

type contentTypes struct {
	Default  []mediaDefault  `xml:"Default"`
	Override []mediaOverride `xml:"Override"`
}

func parseContentTypes(t *testing.T, parts map[string]string) contentTypes {
	t.Helper()
	raw, ok := parts["[Content_Types].xml"]
	if !ok {
		t.Fatal("the package has no [Content_Types].xml stream")
	}
	var ct contentTypes
	if err := xml.Unmarshal([]byte(raw), &ct); err != nil {
		t.Fatalf("[Content_Types].xml is not well-formed: %v", err)
	}
	return ct
}

func defaultFor(ct contentTypes, ext string) string {
	for _, d := range ct.Default {
		if strings.EqualFold(d.Extension, ext) {
			return d.ContentType
		}
	}
	return ""
}

func overrideFor(ct contentTypes, partName string) string {
	for _, o := range ct.Override {
		if strings.EqualFold(o.PartName, partName) {
			return o.ContentType
		}
	}
	return ""
}

// populated builds a workbook exercising the inherited surface, so that the
// package under test is not merely the empty default one.
func populated(t *testing.T) *seedcalc.File {
	t.Helper()
	f := seedcalc.NewFile()
	if idx := f.NewSheet("Ledger"); idx < 1 {
		t.Fatalf("NewSheet returned index %d", idx)
	}
	if err := f.SetCellValue("Sheet1", "A1", "Region"); err != nil {
		t.Fatalf("SetCellValue: %v", err)
	}
	if err := f.SetCellValue("Sheet1", "B1", 42); err != nil {
		t.Fatalf("SetCellValue: %v", err)
	}
	if err := f.SetCellValue("Ledger", "A1", 3.5); err != nil {
		t.Fatalf("SetCellValue: %v", err)
	}
	if err := f.SetCellFormula("Ledger", "B1", "SUM(A1:A2)"); err != nil {
		t.Fatalf("SetCellFormula: %v", err)
	}
	return f
}

func TestBasePackageHoldsTheRequiredParts(t *testing.T) {
	parts := savedParts(t, populated(t))
	for _, want := range []string{
		"[Content_Types].xml",
		"_rels/.rels",
		"xl/workbook.xml",
		"xl/_rels/workbook.xml.rels",
		"xl/worksheets/sheet1.xml",
		"xl/worksheets/sheet2.xml",
	} {
		if _, ok := parts[want]; !ok {
			t.Errorf("the package has no %s part", want)
		}
	}
	// A part name in the package is stored without a leading slash and two
	// part names may not be equivalent under ASCII case-insensitive matching.
	seen := make(map[string]string, len(parts))
	for name := range parts {
		if strings.HasPrefix(name, "/") {
			t.Errorf("part %q carries a leading slash", name)
		}
		if prev, dup := seen[strings.ToLower(name)]; dup {
			t.Errorf("part names %q and %q are equivalent", prev, name)
		}
		seen[strings.ToLower(name)] = name
	}
}

func TestContentTypeStreamDeclaresEveryPart(t *testing.T) {
	parts := savedParts(t, populated(t))
	ct := parseContentTypes(t, parts)

	if got := defaultFor(ct, "rels"); got != "application/vnd.openxmlformats-package.relationships+xml" {
		t.Errorf("the default media type for the rels extension = %q", got)
	}
	if got := overrideFor(ct, "/xl/workbook.xml"); got !=
		"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml" {
		t.Errorf("the workbook part content type = %q", got)
	}
	for _, sheet := range []string{"/xl/worksheets/sheet1.xml", "/xl/worksheets/sheet2.xml"} {
		if got := overrideFor(ct, sheet); got !=
			"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml" {
			t.Errorf("the content type of %s = %q", sheet, got)
		}
	}

	// Every part is covered, by an override naming it or by a default for its
	// extension. [Content_Types].xml is itself not a part and is not declared.
	for name := range parts {
		if name == "[Content_Types].xml" {
			continue
		}
		if overrideFor(ct, "/"+name) != "" {
			continue
		}
		ext := strings.TrimPrefix(path.Ext(name), ".")
		if defaultFor(ct, ext) == "" {
			t.Errorf("part %q has neither an override nor a default for extension %q", name, ext)
		}
	}
	// And every override names a part that is in the package.
	for _, o := range ct.Override {
		if _, ok := parts[strings.TrimPrefix(o.PartName, "/")]; !ok {
			t.Errorf("override %q names a part the package does not contain", o.PartName)
		}
	}
}

func TestPackageRelationshipsResolveToExistingParts(t *testing.T) {
	parts := savedParts(t, populated(t))

	root := parseRels(t, parts, "_rels/.rels")
	var officeDoc string
	for _, r := range root.Rel {
		if r.ID == "" {
			t.Error("a package relationship carries an empty Id")
		}
		if r.Type == "http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" {
			officeDoc = r.Target
		}
	}
	if officeDoc == "" {
		t.Fatal("_rels/.rels declares no officeDocument relationship")
	}
	if _, ok := parts[strings.TrimPrefix(officeDoc, "/")]; !ok {
		t.Errorf("the officeDocument relationship targets %q, which is not a part of the package", officeDoc)
	}

	// Every internal relationship of every relationship part resolves, by the
	// rule that a target is relative to the folder of the part it belongs to.
	for name := range parts {
		if !strings.HasSuffix(name, ".rels") {
			continue
		}
		base := path.Dir(path.Dir(name)) // strip "<dir>/_rels"
		if base == "." {
			base = ""
		}
		for _, r := range parseRels(t, parts, name).Rel {
			if r.TargetMode == "External" {
				continue
			}
			target := path.Clean(path.Join(base, r.Target))
			if _, ok := parts[target]; !ok {
				t.Errorf("%s: relationship %s targets %q, resolving to the missing part %q", name, r.ID, r.Target, target)
			}
		}
	}

	ids := map[string]bool{}
	for _, r := range parseRels(t, parts, "xl/_rels/workbook.xml.rels").Rel {
		if ids[r.ID] {
			t.Errorf("the workbook relationship part repeats the identifier %q", r.ID)
		}
		ids[r.ID] = true
	}
}

func TestNonMacroPackageDeclaresNoMacroTypes(t *testing.T) {
	parts := savedParts(t, populated(t))
	ct := parseContentTypes(t, parts)

	// The macro part and the extension set that carries it arrive only when a
	// macro project has been added; a workbook without one is unaffected.
	for _, ext := range []string{"bin"} {
		if got := defaultFor(ct, ext); got != "" {
			t.Errorf("a workbook with no macro project declares a default media type %q for the %q extension", got, ext)
		}
	}
	for name := range parts {
		if strings.HasSuffix(name, ".bin") {
			t.Errorf("a workbook with no macro project wrote the part %q", name)
		}
		if strings.HasPrefix(name, "xl/pivotCache/") || strings.HasPrefix(name, "xl/pivotTables/") {
			t.Errorf("a workbook with no pivot table wrote the part %q", name)
		}
	}
	for _, o := range ct.Override {
		if strings.Contains(o.ContentType, "macroEnabled") ||
			strings.Contains(o.ContentType, "pivot") {
			t.Errorf("part %s is declared as %q", o.PartName, o.ContentType)
		}
	}
	// The relationship type identifiers are spelled out here rather than taken
	// from the library, so that this group exercises only the surface earlier
	// milestones established.
	for _, r := range parseRels(t, parts, "xl/_rels/workbook.xml.rels").Rel {
		switch r.Type {
		case "http://schemas.microsoft.com/office/2006/relationships/vbaProject":
			t.Error("a workbook with no macro project declares a vbaProject relationship")
		case "http://schemas.openxmlformats.org/officeDocument/2006/relationships/pivotCacheDefinition":
			t.Error("a workbook with no pivot table declares a pivotCacheDefinition relationship")
		}
	}
}

func TestReadZipReaderReportsPartsAndWorksheetCount(t *testing.T) {
	var buf bytes.Buffer
	if err := populated(t).Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	zr, err := zip.NewReader(bytes.NewReader(buf.Bytes()), int64(buf.Len()))
	if err != nil {
		t.Fatalf("the saved workbook is not a readable package: %v", err)
	}
	items, sheets, err := seedcalc.ReadZipReader(zr)
	if err != nil {
		t.Fatalf("ReadZipReader on a package this library wrote: %v", err)
	}
	if sheets != 2 {
		t.Errorf("ReadZipReader reports %d worksheets, want 2", sheets)
	}
	for _, want := range []string{
		"[Content_Types].xml",
		"_rels/.rels",
		"xl/workbook.xml",
		"xl/_rels/workbook.xml.rels",
		"xl/worksheets/sheet1.xml",
	} {
		body, ok := items[want]
		if !ok {
			t.Errorf("ReadZipReader does not report the part %s", want)
			continue
		}
		if len(body) == 0 {
			t.Errorf("ReadZipReader reports %s as empty", want)
		}
	}
}

func TestWrittenPackageReopensWithItsContent(t *testing.T) {
	var buf bytes.Buffer
	if err := populated(t).Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	g, err := seedcalc.OpenReader(bytes.NewReader(buf.Bytes()))
	if err != nil {
		t.Fatalf("OpenReader on a package this library wrote: %v", err)
	}
	if got := g.GetSheetName(1); got != "Sheet1" {
		t.Errorf("after reopening, GetSheetName(1) = %q, want Sheet1", got)
	}
	if got := g.GetSheetIndex("Ledger"); got < 1 {
		t.Errorf("after reopening, GetSheetIndex(\"Ledger\") = %d", got)
	}
	for _, c := range []struct{ sheet, axis, want string }{
		{"Sheet1", "A1", "Region"},
		{"Sheet1", "B1", "42"},
		{"Ledger", "A1", "3.5"},
	} {
		got, err := g.GetCellValue(c.sheet, c.axis)
		if err != nil {
			t.Fatalf("GetCellValue(%q, %q): %v", c.sheet, c.axis, err)
		}
		if got != c.want {
			t.Errorf("after reopening, %s!%s = %q, want %q", c.sheet, c.axis, got, c.want)
		}
	}
	formula, err := g.GetCellFormula("Ledger", "B1")
	if err != nil {
		t.Fatalf("GetCellFormula: %v", err)
	}
	if formula != "SUM(A1:A2)" {
		t.Errorf("after reopening, the formula at Ledger!B1 = %q", formula)
	}
	// The reopened workbook is itself serialisable, so the round trip is
	// repeatable rather than one-way.
	again := savedParts(t, g)
	if _, ok := again["xl/workbook.xml"]; !ok {
		t.Error("the reopened workbook does not write a workbook part")
	}
}
