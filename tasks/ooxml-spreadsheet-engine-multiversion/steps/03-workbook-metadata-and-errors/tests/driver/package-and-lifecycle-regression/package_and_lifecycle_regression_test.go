// Gate group: package-and-lifecycle-regression.
//
// The end-to-end path milestones 01 and 02 established, carried forward: add
// worksheets by name, arrange them, write the workbook, and find a
// well-formed OPC package on the other side whose relationships resolve and
// whose worksheet set reopens intact.
//
// Milestone 03 gives every mutator an error return, so nothing that writes a
// cell can appear in a gate group -- SetCellValue, SetCellStr, SetSheetRow
// and the rest are all re-spelled here and are priced in cell-model,
// row-access, worksheet-features and package-structure instead. What is left
// is the container and the worksheet lifecycle, every call of which has the
// same signature at milestone 02 and at this one.
//
// Graded against ECMA-376 Part 2 clause 7.3 (mapping a package to a ZIP
// file), clause 9.3 (the relationships part) and clause 10.1.2 (the content
// types stream), together with ECMA-376 Part 1 clause 12.3 (part names),
// clause 18.2.19 (sheet) and clause 18.2.20 (sheets).
package packageandlifecycleregression

import (
	"archive/zip"
	"bytes"
	"encoding/xml"
	"io"
	"sort"
	"strings"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// sample is a three-worksheet workbook built only through calls whose
// signature milestone 02 and milestone 03 share.
func sample(t *testing.T) *seedcalc.File {
	t.Helper()
	f := seedcalc.NewFile()
	if idx := f.NewSheet("Second"); idx < 1 {
		t.Fatalf(`NewSheet("Second") returned index %d, want a positive index`, idx)
	}
	if idx := f.NewSheet("Third"); idx < 1 {
		t.Fatalf(`NewSheet("Third") returned index %d, want a positive index`, idx)
	}
	return f
}

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

// sheetNames returns the worksheet names in index order, so that a map with
// no defined iteration order is never compared as though it had one.
func sheetNames(f *seedcalc.File) []string {
	m := f.GetSheetMap()
	idx := make([]int, 0, len(m))
	for i := range m {
		idx = append(idx, i)
	}
	sort.Ints(idx)
	out := make([]string, 0, len(idx))
	for _, i := range idx {
		out = append(out, m[i])
	}
	return out
}

func TestTheWrittenPackageCarriesEveryRequiredPart(t *testing.T) {
	names := partNames(openZip(t, packageBytes(t, sample(t))))
	for _, want := range []string{
		"[Content_Types].xml",
		"_rels/.rels",
		"xl/workbook.xml",
		"xl/_rels/workbook.xml.rels",
		"xl/worksheets/sheet1.xml",
		"xl/worksheets/sheet2.xml",
		"xl/worksheets/sheet3.xml",
	} {
		if !names[want] {
			t.Errorf("written package is missing required part %s", want)
		}
	}
}

func TestTheContentTypesStreamDeclaresEveryWorksheet(t *testing.T) {
	zr := openZip(t, packageBytes(t, sample(t)))
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
		"/xl/worksheets/sheet3.xml": "application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml",
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

func TestTheRelationshipGraphStillResolves(t *testing.T) {
	zr := openZip(t, packageBytes(t, sample(t)))
	names := partNames(zr)

	var root relationships
	if err := xml.Unmarshal(readPart(t, zr, "_rels/.rels"), &root); err != nil {
		t.Fatalf("_rels/.rels is not well-formed XML: %v", err)
	}
	const officeDoc = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument"
	found := false
	for _, r := range root.Rel {
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
		t.Errorf("_rels/.rels declares no officeDocument relationship (%d relationships present)", len(root.Rel))
	}

	var wb relationships
	if err := xml.Unmarshal(readPart(t, zr, "xl/_rels/workbook.xml.rels"), &wb); err != nil {
		t.Fatalf("xl/_rels/workbook.xml.rels is not well-formed XML: %v", err)
	}
	const worksheetRel = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet"
	ids := map[string]bool{}
	seen := 0
	for _, r := range wb.Rel {
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
	if seen != 3 {
		t.Errorf("workbook declares %d worksheet relationships, want 3", seen)
	}
}

func TestTheWorksheetSetIsAddressableByNameAndIndex(t *testing.T) {
	f := sample(t)
	if got := sheetNames(f); len(got) != 3 || got[0] != "Sheet1" || got[1] != "Second" || got[2] != "Third" {
		t.Fatalf("GetSheetMap in index order = %v, want [Sheet1 Second Third]", got)
	}
	for _, name := range []string{"Sheet1", "Second", "Third"} {
		idx := f.GetSheetIndex(name)
		if idx < 1 {
			t.Errorf("GetSheetIndex(%q) = %d, want a positive index", name, idx)
			continue
		}
		if got := f.GetSheetName(idx); got != name {
			t.Errorf("GetSheetName(GetSheetIndex(%q)) = %q, want %q", name, got, name)
		}
		if !f.GetSheetVisible(name) {
			t.Errorf("GetSheetVisible(%q) = false on a freshly added worksheet", name)
		}
	}
	second := f.GetSheetIndex("Second")
	f.SetActiveSheet(second)
	if got := f.GetActiveSheetIndex(); got != second {
		t.Errorf("GetActiveSheetIndex() = %d after SetActiveSheet(%d)", got, second)
	}
	if got := f.GetSheetIndex("NoSuchSheet"); got != 0 {
		t.Errorf("GetSheetIndex on an absent worksheet = %d, want 0", got)
	}
}

func TestRenamingCopyingAndDeletingAWorksheet(t *testing.T) {
	f := sample(t)
	f.SetSheetName("Second", "Renamed")
	if got := sheetNames(f); len(got) != 3 || got[1] != "Renamed" {
		t.Errorf("after SetSheetName the worksheets are %v, want Renamed in the second position", got)
	}
	if got := f.GetSheetIndex("Second"); got != 0 {
		t.Errorf("the old name still resolves to index %d", got)
	}

	if err := f.CopySheet(f.GetSheetIndex("Renamed"), f.GetSheetIndex("Third")); err != nil {
		t.Errorf("CopySheet between two existing worksheets: %v", err)
	}
	if got := len(f.GetSheetMap()); got != 3 {
		t.Errorf("CopySheet changed the worksheet count to %d, want 3", got)
	}

	f.DeleteSheet("Third")
	if got := sheetNames(f); len(got) != 2 || got[0] != "Sheet1" || got[1] != "Renamed" {
		t.Errorf("after DeleteSheet the worksheets are %v, want [Sheet1 Renamed]", got)
	}
	if got := f.GetSheetIndex("Third"); got != 0 {
		t.Errorf("the deleted worksheet still resolves to index %d", got)
	}
}

func TestTheWorkbookReopensWithItsWorksheetSetIntact(t *testing.T) {
	f := sample(t)
	f.SetSheetName("Second", "Renamed")
	b := packageBytes(t, f)

	g, err := seedcalc.OpenReader(bytes.NewReader(b))
	if err != nil {
		t.Fatalf("OpenReader on a package this library wrote: %v", err)
	}
	if got := sheetNames(g); len(got) != 3 || got[0] != "Sheet1" || got[1] != "Renamed" || got[2] != "Third" {
		t.Errorf("the reopened worksheets are %v, want [Sheet1 Renamed Third]", got)
	}

	parts, worksheets, err := seedcalc.ReadZipReader(openZip(t, b))
	if err != nil {
		t.Fatalf("ReadZipReader: %v", err)
	}
	if worksheets != 3 {
		t.Errorf("ReadZipReader reported %d worksheets, want 3", worksheets)
	}
	if !bytes.Contains(parts["xl/workbook.xml"], []byte("<workbook")) {
		t.Error("the workbook part returned by ReadZipReader does not contain a workbook element")
	}
	if !bytes.Equal(g.XLSX["xl/workbook.xml"], parts["xl/workbook.xml"]) {
		t.Error("the parts held by an opened file differ from what ReadZipReader returned")
	}
	if _, err := seedcalc.OpenReader(strings.NewReader("this is not a package")); err == nil {
		t.Error("OpenReader accepted a byte stream that is not a ZIP container")
	}
}

func TestTheStyleAndTableRegistriesStillAcceptTheirInput(t *testing.T) {
	f := sample(t)
	style, err := f.NewStyle(`{"font":{"bold":true}}`)
	if err != nil {
		t.Fatalf("NewStyle: %v", err)
	}
	if style < 1 {
		t.Errorf("NewStyle returned id %d, want a positive style id", style)
	}
	cond, err := f.NewConditionalStyle(`{"font":{"color":"#9A0511"}}`)
	if err != nil {
		t.Fatalf("NewConditionalStyle: %v", err)
	}
	if cond < 0 {
		t.Errorf("NewConditionalStyle returned id %d", cond)
	}
	if err := f.AddTable("Sheet1", "A1", "C3", `{"table_name":"t","table_style":"TableStyleMedium2"}`); err != nil {
		t.Fatalf("AddTable: %v", err)
	}
	if err := f.AutoFilter("Second", "A1", "C1", ""); err != nil {
		t.Fatalf("AutoFilter: %v", err)
	}
	if err := f.SetConditionalFormat("Third", "A1:A5",
		`[{"type":"cell","criteria":">","format":`+itoa(cond)+`,"value":"10"}]`); err != nil {
		t.Fatalf("SetConditionalFormat: %v", err)
	}

	g, err := seedcalc.OpenReader(bytes.NewReader(packageBytes(t, f)))
	if err != nil {
		t.Fatalf("the package no longer opens once the registries have been written to: %v", err)
	}
	if got := len(g.GetSheetMap()); got != 3 {
		t.Errorf("the reopened workbook has %d worksheets, want 3", got)
	}
	if _, ok := g.XLSX["xl/tables/table1.xml"]; !ok {
		t.Error("the reopened package has no xl/tables/table1.xml")
	}
}

// itoa avoids pulling strconv in for a single conversion inside a format
// string that is otherwise a literal.
func itoa(n int) string {
	if n == 0 {
		return "0"
	}
	var b []byte
	for n > 0 {
		b = append([]byte{byte('0' + n%10)}, b...)
		n /= 10
	}
	return string(b)
}
