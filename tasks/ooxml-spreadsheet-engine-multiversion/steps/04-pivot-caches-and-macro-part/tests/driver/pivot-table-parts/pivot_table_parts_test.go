// Scored group: pivot-table-parts (milestone 04, "Pivot caches, sparklines and
// the macro part").
//
// Normative sources:
//   - TEST_INTERFACE.md section 4.04 -- "Pivot tables", "The pivot cache and
//     pivot table parts".
//   - ECMA-376 Part 1, 12.3.11 Pivot Table Part; 12.3.12 Pivot Table Cache
//     Definition Part; 12.3.13 Pivot Table Cache Records Part;
//     18.2.17 pivotCache (PivotCache); 18.2.18 pivotCaches (PivotCaches);
//     18.10.1.7 cacheSource; 18.10.1.49 location;
//     18.10.1.67 pivotCacheDefinition; 18.10.1.73 pivotTableDefinition;
//     18.10.1.95 worksheetSource.
//   - ECMA-376 Part 2, 6.5.3 Relationship markup; 7.2.3.2.5 Override element.
//   - Cases cross-checked against the upstream test corpus at tag v2.0.2
//     (pivotTable_test.go).
package pivottableparts

import (
	"archive/zip"
	"bytes"
	"encoding/xml"
	"fmt"
	"io/ioutil"
	"path"
	"strings"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

const relNS = "http://schemas.openxmlformats.org/officeDocument/2006/relationships"

// savedParts writes the workbook to a buffer and returns every package item.
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

type relationship struct {
	ID     string `xml:"Id,attr"`
	Type   string `xml:"Type,attr"`
	Target string `xml:"Target,attr"`
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

type override struct {
	PartName    string `xml:"PartName,attr"`
	ContentType string `xml:"ContentType,attr"`
}

type contentTypes struct {
	Override []override `xml:"Override"`
}

func parseContentTypes(t *testing.T, parts map[string]string) contentTypes {
	t.Helper()
	var ct contentTypes
	if err := xml.Unmarshal([]byte(parts["[Content_Types].xml"]), &ct); err != nil {
		t.Fatalf("[Content_Types].xml is not well-formed: %v", err)
	}
	return ct
}

func overrideFor(ct contentTypes, partName string) string {
	for _, o := range ct.Override {
		if o.PartName == partName {
			return o.ContentType
		}
	}
	return ""
}

type worksheetSource struct {
	Ref   string `xml:"ref,attr"`
	Sheet string `xml:"sheet,attr"`
	Name  string `xml:"name,attr"`
}

type cacheDefinition struct {
	SaveData      string `xml:"saveData,attr"`
	RefreshOnLoad string `xml:"refreshOnLoad,attr"`
	CacheSource   struct {
		Type            string          `xml:"type,attr"`
		WorksheetSource worksheetSource `xml:"worksheetSource"`
	} `xml:"cacheSource"`
	CacheFields struct {
		Count int `xml:"count,attr"`
		Field []struct {
			Name string `xml:"name,attr"`
		} `xml:"cacheField"`
	} `xml:"cacheFields"`
}

type tableDefinition struct {
	Name     string `xml:"name,attr"`
	CacheID  string `xml:"cacheId,attr"`
	Location struct {
		Ref            string `xml:"ref,attr"`
		FirstHeaderRow string `xml:"firstHeaderRow,attr"`
		FirstDataRow   string `xml:"firstDataRow,attr"`
		FirstDataCol   string `xml:"firstDataCol,attr"`
	} `xml:"location"`
	StyleInfo struct {
		Name string `xml:"name,attr"`
	} `xml:"pivotTableStyleInfo"`
}

type workbookDoc struct {
	PivotCaches struct {
		Cache []struct {
			CacheID string `xml:"cacheId,attr"`
			RID     string `xml:"http://schemas.openxmlformats.org/officeDocument/2006/relationships id,attr"`
		} `xml:"pivotCache"`
	} `xml:"pivotCaches"`
}

func parseCacheDefinition(t *testing.T, parts map[string]string, name string) cacheDefinition {
	t.Helper()
	raw, ok := parts[name]
	if !ok {
		t.Fatalf("the package has no %s part", name)
	}
	var d cacheDefinition
	if err := xml.Unmarshal([]byte(raw), &d); err != nil {
		t.Fatalf("%s is not well-formed: %v", name, err)
	}
	return d
}

func parseTableDefinition(t *testing.T, parts map[string]string, name string) tableDefinition {
	t.Helper()
	raw, ok := parts[name]
	if !ok {
		t.Fatalf("the package has no %s part", name)
	}
	var d tableDefinition
	if err := xml.Unmarshal([]byte(raw), &d); err != nil {
		t.Fatalf("%s is not well-formed: %v", name, err)
	}
	return d
}

// sourceWorkbook builds a five column, thirty row data set on Sheet1.
func sourceWorkbook(t *testing.T) *seedcalc.File {
	t.Helper()
	f := seedcalc.NewFile()
	header := []string{"Month", "Year", "Type", "Sales", "Region"}
	if err := f.SetSheetRow("Sheet1", "A1", &header); err != nil {
		t.Fatalf("SetSheetRow: %v", err)
	}
	months := []string{"Jan", "Feb", "Mar"}
	kinds := []string{"Meat", "Dairy", "Beverages"}
	regions := []string{"East", "West", "North"}
	for i := 0; i < 30; i++ {
		row := i + 2
		if err := f.SetCellValue("Sheet1", fmt.Sprintf("A%d", row), months[i%3]); err != nil {
			t.Fatalf("SetCellValue: %v", err)
		}
		f.SetCellValue("Sheet1", fmt.Sprintf("B%d", row), 2017+i%2)
		f.SetCellValue("Sheet1", fmt.Sprintf("C%d", row), kinds[i%3])
		f.SetCellValue("Sheet1", fmt.Sprintf("D%d", row), i*7)
		f.SetCellValue("Sheet1", fmt.Sprintf("E%d", row), regions[i%3])
	}
	return f
}

func defaultOption() *seedcalc.PivotTableOption {
	return &seedcalc.PivotTableOption{
		DataRange:       "Sheet1!$A$1:$E$31",
		PivotTableRange: "Sheet1!$G$2:$M$34",
		Rows:            []string{"Month", "Year"},
		Columns:         []string{"Type"},
		Data:            []string{"Sales"},
	}
}

func TestPivotTableWritesCacheDefinitionAndTableParts(t *testing.T) {
	f := sourceWorkbook(t)
	if err := f.AddPivotTable(defaultOption()); err != nil {
		t.Fatalf("AddPivotTable: %v", err)
	}
	parts := savedParts(t, f)
	for _, want := range []string{
		"xl/pivotCache/pivotCacheDefinition1.xml",
		"xl/pivotTables/pivotTable1.xml",
		"xl/pivotTables/_rels/pivotTable1.xml.rels",
	} {
		if _, ok := parts[want]; !ok {
			t.Errorf("the package has no %s part", want)
		}
	}
}

func TestPivotPartsAreDeclaredInTheContentTypeStream(t *testing.T) {
	f := sourceWorkbook(t)
	if err := f.AddPivotTable(defaultOption()); err != nil {
		t.Fatalf("AddPivotTable: %v", err)
	}
	ct := parseContentTypes(t, savedParts(t, f))
	if got := overrideFor(ct, "/xl/pivotTables/pivotTable1.xml"); got !=
		"application/vnd.openxmlformats-officedocument.spreadsheetml.pivotTable+xml" {
		t.Errorf("pivot table part content type = %q", got)
	}
	if got := overrideFor(ct, "/xl/pivotCache/pivotCacheDefinition1.xml"); got !=
		"application/vnd.openxmlformats-officedocument.spreadsheetml.pivotCacheDefinition+xml" {
		t.Errorf("pivot cache definition part content type = %q", got)
	}
}

func TestPivotTableRelatesToItsCacheDefinition(t *testing.T) {
	f := sourceWorkbook(t)
	if err := f.AddPivotTable(defaultOption()); err != nil {
		t.Fatalf("AddPivotTable: %v", err)
	}
	parts := savedParts(t, f)
	rels := parseRels(t, parts, "xl/pivotTables/_rels/pivotTable1.xml.rels")
	var found bool
	for _, r := range rels.Rel {
		if r.Type != seedcalc.SourceRelationshipPivotCache {
			continue
		}
		found = true
		target := path.Clean(path.Join("xl/pivotTables", r.Target))
		if _, ok := parts[target]; !ok {
			t.Errorf("pivot table relationship %s targets %q, which resolves to a missing part %q", r.ID, r.Target, target)
		}
	}
	if !found {
		t.Errorf("the pivot table part has no %s relationship", seedcalc.SourceRelationshipPivotCache)
	}
}

func TestWorkbookRegistersThePivotCache(t *testing.T) {
	f := sourceWorkbook(t)
	if err := f.AddPivotTable(defaultOption()); err != nil {
		t.Fatalf("AddPivotTable: %v", err)
	}
	parts := savedParts(t, f)
	var wb workbookDoc
	if err := xml.Unmarshal([]byte(parts["xl/workbook.xml"]), &wb); err != nil {
		t.Fatalf("xl/workbook.xml is not well-formed: %v", err)
	}
	if len(wb.PivotCaches.Cache) != 1 {
		t.Fatalf("the workbook declares %d pivotCache entries, want 1", len(wb.PivotCaches.Cache))
	}
	entry := wb.PivotCaches.Cache[0]
	if entry.CacheID == "" {
		t.Error("the workbook pivotCache entry carries no cacheId")
	}
	table := parseTableDefinition(t, parts, "xl/pivotTables/pivotTable1.xml")
	if table.CacheID != entry.CacheID {
		t.Errorf("pivotTableDefinition cacheId = %q but the workbook declares %q", table.CacheID, entry.CacheID)
	}
	rels := parseRels(t, parts, "xl/_rels/workbook.xml.rels")
	var target string
	for _, r := range rels.Rel {
		if r.ID == entry.RID {
			target = path.Clean(path.Join("xl", r.Target))
			if r.Type != seedcalc.SourceRelationshipPivotCache {
				t.Errorf("workbook relationship %s has type %q", r.ID, r.Type)
			}
		}
	}
	if target == "" {
		t.Fatalf("the workbook pivotCache entry references relationship %q, which the workbook relationship part does not define", entry.RID)
	}
	if _, ok := parts[target]; !ok {
		t.Errorf("the workbook pivot cache relationship resolves to a missing part %q", target)
	}
}

func TestWorksheetRelatesToThePivotTablePart(t *testing.T) {
	f := sourceWorkbook(t)
	if err := f.AddPivotTable(defaultOption()); err != nil {
		t.Fatalf("AddPivotTable: %v", err)
	}
	parts := savedParts(t, f)
	rels := parseRels(t, parts, "xl/worksheets/_rels/sheet1.xml.rels")
	var found bool
	for _, r := range rels.Rel {
		if r.Type != seedcalc.SourceRelationshipPivotTable {
			continue
		}
		found = true
		target := path.Clean(path.Join("xl/worksheets", r.Target))
		if _, ok := parts[target]; !ok {
			t.Errorf("worksheet relationship %s targets %q, which resolves to a missing part %q", r.ID, r.Target, target)
		}
	}
	if !found {
		t.Errorf("the worksheet holding the pivot table has no %s relationship", seedcalc.SourceRelationshipPivotTable)
	}
}

func TestCacheSourceDescribesTheSourceRange(t *testing.T) {
	f := sourceWorkbook(t)
	if err := f.AddPivotTable(defaultOption()); err != nil {
		t.Fatalf("AddPivotTable: %v", err)
	}
	parts := savedParts(t, f)
	cache := parseCacheDefinition(t, parts, "xl/pivotCache/pivotCacheDefinition1.xml")
	if cache.CacheSource.Type != "worksheet" {
		t.Errorf("cacheSource type = %q, want %q", cache.CacheSource.Type, "worksheet")
	}
	if cache.CacheSource.WorksheetSource.Sheet != "Sheet1" {
		t.Errorf("worksheetSource sheet = %q, want %q", cache.CacheSource.WorksheetSource.Sheet, "Sheet1")
	}
	if cache.CacheSource.WorksheetSource.Ref != "A1:E31" {
		t.Errorf("worksheetSource ref = %q, want %q", cache.CacheSource.WorksheetSource.Ref, "A1:E31")
	}
	table := parseTableDefinition(t, parts, "xl/pivotTables/pivotTable1.xml")
	if table.Location.Ref != "G2:M34" {
		t.Errorf("location ref = %q, want %q", table.Location.Ref, "G2:M34")
	}
	if table.StyleInfo.Name == "" {
		t.Error("the pivot table declares no pivotTableStyleInfo name")
	}
}

func TestReversedRangesAreNormalised(t *testing.T) {
	f := sourceWorkbook(t)
	opt := defaultOption()
	opt.DataRange = "Sheet1!$E$31:$A$1"
	opt.PivotTableRange = "Sheet1!$U$34:$O$2"
	if err := f.AddPivotTable(opt); err != nil {
		t.Fatalf("AddPivotTable: %v", err)
	}
	parts := savedParts(t, f)
	cache := parseCacheDefinition(t, parts, "xl/pivotCache/pivotCacheDefinition1.xml")
	if cache.CacheSource.WorksheetSource.Ref != "A1:E31" {
		t.Errorf("worksheetSource ref = %q, want the normalised %q", cache.CacheSource.WorksheetSource.Ref, "A1:E31")
	}
	table := parseTableDefinition(t, parts, "xl/pivotTables/pivotTable1.xml")
	if table.Location.Ref != "O2:U34" {
		t.Errorf("location ref = %q, want the normalised %q", table.Location.Ref, "O2:U34")
	}
}

func TestEachPivotTableGetsItsOwnCacheAndParts(t *testing.T) {
	f := sourceWorkbook(t)
	f.NewSheet("Sheet2")
	first := defaultOption()
	first.PivotTableRange = "Sheet2!$A$1:$G$20"
	if err := f.AddPivotTable(first); err != nil {
		t.Fatalf("AddPivotTable: %v", err)
	}
	second := defaultOption()
	second.PivotTableRange = "Sheet2!$A$25:$G$44"
	second.Rows = []string{"Year"}
	second.Columns = nil
	if err := f.AddPivotTable(second); err != nil {
		t.Fatalf("second AddPivotTable: %v", err)
	}
	parts := savedParts(t, f)
	for _, want := range []string{
		"xl/pivotCache/pivotCacheDefinition1.xml",
		"xl/pivotCache/pivotCacheDefinition2.xml",
		"xl/pivotTables/pivotTable1.xml",
		"xl/pivotTables/pivotTable2.xml",
		"xl/pivotTables/_rels/pivotTable2.xml.rels",
	} {
		if _, ok := parts[want]; !ok {
			t.Errorf("the package has no %s part", want)
		}
	}
	var wb workbookDoc
	if err := xml.Unmarshal([]byte(parts["xl/workbook.xml"]), &wb); err != nil {
		t.Fatalf("xl/workbook.xml is not well-formed: %v", err)
	}
	if len(wb.PivotCaches.Cache) != 2 {
		t.Fatalf("the workbook declares %d pivotCache entries, want 2", len(wb.PivotCaches.Cache))
	}
	if wb.PivotCaches.Cache[0].CacheID == wb.PivotCaches.Cache[1].CacheID {
		t.Errorf("both pivot caches share cacheId %q", wb.PivotCaches.Cache[0].CacheID)
	}
	one := parseTableDefinition(t, parts, "xl/pivotTables/pivotTable1.xml")
	two := parseTableDefinition(t, parts, "xl/pivotTables/pivotTable2.xml")
	if one.Name == two.Name {
		t.Errorf("both pivot tables are named %q", one.Name)
	}
	if one.CacheID == two.CacheID {
		t.Errorf("both pivot tables use cacheId %q", one.CacheID)
	}
	rels := parseRels(t, parts, "xl/worksheets/_rels/sheet2.xml.rels")
	var n int
	for _, r := range rels.Rel {
		if r.Type == seedcalc.SourceRelationshipPivotTable {
			n++
		}
	}
	if n != 2 {
		t.Errorf("the worksheet declares %d pivot table relationships, want 2", n)
	}
}

func TestMalformedRangesAreRejected(t *testing.T) {
	cases := []struct {
		name string
		opt  *seedcalc.PivotTableOption
	}{
		{"nil option", nil},
		{"no ranges at all", &seedcalc.PivotTableOption{}},
		{"single cell data range", &seedcalc.PivotTableOption{DataRange: "Sheet1!$A$1:$A$1", PivotTableRange: "Sheet1!$G$2:$M$34"}},
		{"data range without a sheet", &seedcalc.PivotTableOption{DataRange: "$A$1:$E$31", PivotTableRange: "Sheet1!$G$2:$M$34"}},
		{"unknown data sheet", &seedcalc.PivotTableOption{DataRange: "SheetX!$A$1:$E$31", PivotTableRange: "Sheet1!$G$2:$M$34"}},
		{"pivot range without a sheet", &seedcalc.PivotTableOption{DataRange: "Sheet1!$A$1:$E$31", PivotTableRange: "$G$2:$M$34"}},
		{"unknown pivot sheet", &seedcalc.PivotTableOption{DataRange: "Sheet1!$A$1:$E$31", PivotTableRange: "SheetX!$G$2:$M$34"}},
		{"single cell pivot range", &seedcalc.PivotTableOption{DataRange: "Sheet1!$A$1:$E$31", PivotTableRange: "Sheet1!$G$2:$G$2"}},
		{"row zero", &seedcalc.PivotTableOption{DataRange: "Sheet1!$A$0:$E$31", PivotTableRange: "Sheet1!$G$2:$M$34"}},
	}
	for _, c := range cases {
		f := sourceWorkbook(t)
		if err := f.AddPivotTable(c.opt); err == nil {
			t.Errorf("AddPivotTable accepted %s", c.name)
			continue
		}
		for name := range savedParts(t, f) {
			if strings.HasPrefix(name, "xl/pivotTables/") || strings.HasPrefix(name, "xl/pivotCache/") {
				t.Errorf("%s left a %s part behind", c.name, name)
			}
		}
	}
}

func TestPivotRelationshipTypeConstants(t *testing.T) {
	if seedcalc.SourceRelationshipPivotCache != relNS+"/pivotCacheDefinition" {
		t.Errorf("SourceRelationshipPivotCache = %q", seedcalc.SourceRelationshipPivotCache)
	}
	if seedcalc.SourceRelationshipPivotTable != relNS+"/pivotTable" {
		t.Errorf("SourceRelationshipPivotTable = %q", seedcalc.SourceRelationshipPivotTable)
	}
}
