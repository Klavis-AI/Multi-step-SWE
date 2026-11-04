// Scored group: pivot-table-fields (milestone 04, "Pivot caches, sparklines and
// the macro part").
//
// Normative sources:
//   - TEST_INTERFACE.md section 4.04 -- "Pivot tables", "Placing fields on the
//     row, column and data axes".
//   - ECMA-376 Part 1, 18.10.1.3 cacheField; 18.10.1.4 cacheFields;
//     18.10.1.14 colFields; 18.10.1.22 dataField; 18.10.1.23 dataFields;
//     18.10.1.49 location; 18.10.1.69 pivotField; 18.10.1.70 pivotFields;
//     18.10.1.74 pivotTableStyleInfo; 18.10.1.81 rowFields.
//   - Cases cross-checked against the upstream test corpus at tag v2.0.2
//     (pivotTable_test.go).
package pivottablefields

import (
	"archive/zip"
	"bytes"
	"encoding/xml"
	"fmt"
	"io/ioutil"
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

type indexedField struct {
	X int `xml:"x,attr"`
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
	PivotFields struct {
		Count int `xml:"count,attr"`
		Field []struct {
			Axis      string `xml:"axis,attr"`
			DataField string `xml:"dataField,attr"`
		} `xml:"pivotField"`
	} `xml:"pivotFields"`
	RowFields struct {
		Count int            `xml:"count,attr"`
		Field []indexedField `xml:"field"`
	} `xml:"rowFields"`
	ColFields struct {
		Count int            `xml:"count,attr"`
		Field []indexedField `xml:"field"`
	} `xml:"colFields"`
	DataFields struct {
		Count int `xml:"count,attr"`
		Field []struct {
			Fld int `xml:"fld,attr"`
		} `xml:"dataField"`
	} `xml:"dataFields"`
	StyleInfo struct {
		Name string `xml:"name,attr"`
	} `xml:"pivotTableStyleInfo"`
}

type cacheDefinition struct {
	CacheFields struct {
		Count int `xml:"count,attr"`
		Field []struct {
			Name string `xml:"name,attr"`
		} `xml:"cacheField"`
	} `xml:"cacheFields"`
}

func pivotTable(t *testing.T, parts map[string]string) tableDefinition {
	t.Helper()
	raw, ok := parts["xl/pivotTables/pivotTable1.xml"]
	if !ok {
		t.Fatal("the package has no xl/pivotTables/pivotTable1.xml part")
	}
	var d tableDefinition
	if err := xml.Unmarshal([]byte(raw), &d); err != nil {
		t.Fatalf("the pivot table part is not well-formed: %v", err)
	}
	return d
}

func pivotCache(t *testing.T, parts map[string]string) cacheDefinition {
	t.Helper()
	raw, ok := parts["xl/pivotCache/pivotCacheDefinition1.xml"]
	if !ok {
		t.Fatal("the package has no xl/pivotCache/pivotCacheDefinition1.xml part")
	}
	var d cacheDefinition
	if err := xml.Unmarshal([]byte(raw), &d); err != nil {
		t.Fatalf("the pivot cache definition part is not well-formed: %v", err)
	}
	return d
}

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
		f.SetCellValue("Sheet1", fmt.Sprintf("A%d", row), months[i%3])
		f.SetCellValue("Sheet1", fmt.Sprintf("B%d", row), 2017+i%2)
		f.SetCellValue("Sheet1", fmt.Sprintf("C%d", row), kinds[i%3])
		f.SetCellValue("Sheet1", fmt.Sprintf("D%d", row), i*7)
		f.SetCellValue("Sheet1", fmt.Sprintf("E%d", row), regions[i%3])
	}
	return f
}

func addPivot(t *testing.T, f *seedcalc.File, opt *seedcalc.PivotTableOption) map[string]string {
	t.Helper()
	if err := f.AddPivotTable(opt); err != nil {
		t.Fatalf("AddPivotTable: %v", err)
	}
	return savedParts(t, f)
}

func fullOption() *seedcalc.PivotTableOption {
	return &seedcalc.PivotTableOption{
		DataRange:       "Sheet1!$A$1:$E$31",
		PivotTableRange: "Sheet1!$G$2:$M$34",
		Rows:            []string{"Month", "Year"},
		Columns:         []string{"Type"},
		Data:            []string{"Sales"},
	}
}

func TestRowFieldsRecordTheSourceColumnIndex(t *testing.T) {
	parts := addPivot(t, sourceWorkbook(t), fullOption())
	table := pivotTable(t, parts)
	if table.RowFields.Count != 2 || len(table.RowFields.Field) != 2 {
		t.Fatalf("rowFields count = %d with %d entries, want 2 and 2", table.RowFields.Count, len(table.RowFields.Field))
	}
	if table.RowFields.Field[0].X != 0 || table.RowFields.Field[1].X != 1 {
		t.Errorf("rowFields indices = %d, %d, want 0 and 1", table.RowFields.Field[0].X, table.RowFields.Field[1].X)
	}
	for _, i := range []int{0, 1} {
		if table.PivotFields.Field[i].Axis != "axisRow" {
			t.Errorf("pivotField %d axis = %q, want axisRow", i, table.PivotFields.Field[i].Axis)
		}
	}
}

func TestColumnFieldsRecordTheSourceColumnIndex(t *testing.T) {
	parts := addPivot(t, sourceWorkbook(t), fullOption())
	table := pivotTable(t, parts)
	if table.ColFields.Count != 1 || len(table.ColFields.Field) != 1 {
		t.Fatalf("colFields count = %d with %d entries, want 1 and 1", table.ColFields.Count, len(table.ColFields.Field))
	}
	if table.ColFields.Field[0].X != 2 {
		t.Errorf("colFields index = %d, want 2", table.ColFields.Field[0].X)
	}
	if table.PivotFields.Field[2].Axis != "axisCol" {
		t.Errorf("pivotField 2 axis = %q, want axisCol", table.PivotFields.Field[2].Axis)
	}
}

func TestDataFieldsAreMarkedOnBothTheFieldAndTheList(t *testing.T) {
	parts := addPivot(t, sourceWorkbook(t), fullOption())
	table := pivotTable(t, parts)
	if table.DataFields.Count != 1 || len(table.DataFields.Field) != 1 {
		t.Fatalf("dataFields count = %d with %d entries, want 1 and 1", table.DataFields.Count, len(table.DataFields.Field))
	}
	if table.DataFields.Field[0].Fld != 3 {
		t.Errorf("dataField fld = %d, want 3", table.DataFields.Field[0].Fld)
	}
	if table.PivotFields.Field[3].DataField != "true" {
		t.Errorf("pivotField 3 dataField = %q, want true", table.PivotFields.Field[3].DataField)
	}
	if table.PivotFields.Field[3].Axis != "" {
		t.Errorf("pivotField 3 axis = %q, want no axis on a data field", table.PivotFields.Field[3].Axis)
	}
}

func TestUnplacedColumnsCarryNoAxis(t *testing.T) {
	parts := addPivot(t, sourceWorkbook(t), fullOption())
	table := pivotTable(t, parts)
	if table.PivotFields.Field[4].Axis != "" || table.PivotFields.Field[4].DataField != "" {
		t.Errorf("pivotField 4 = axis %q dataField %q, want neither on an unplaced column",
			table.PivotFields.Field[4].Axis, table.PivotFields.Field[4].DataField)
	}
}

func TestCacheFieldsMirrorTheHeaderRow(t *testing.T) {
	parts := addPivot(t, sourceWorkbook(t), fullOption())
	cache := pivotCache(t, parts)
	want := []string{"Month", "Year", "Type", "Sales", "Region"}
	if cache.CacheFields.Count != len(want) || len(cache.CacheFields.Field) != len(want) {
		t.Fatalf("cacheFields count = %d with %d entries, want %d", cache.CacheFields.Count, len(cache.CacheFields.Field), len(want))
	}
	for i, name := range want {
		if cache.CacheFields.Field[i].Name != name {
			t.Errorf("cacheField %d name = %q, want %q", i, cache.CacheFields.Field[i].Name, name)
		}
	}
	table := pivotTable(t, parts)
	if table.PivotFields.Count != len(want) || len(table.PivotFields.Field) != len(want) {
		t.Errorf("pivotFields count = %d with %d entries, want one per cache field (%d)",
			table.PivotFields.Count, len(table.PivotFields.Field), len(want))
	}
}

func TestUnknownFieldNamesArePassedOver(t *testing.T) {
	opt := fullOption()
	opt.Rows = []string{"Nowhere"}
	opt.Columns = []string{"Type"}
	f := sourceWorkbook(t)
	if err := f.AddPivotTable(opt); err != nil {
		t.Fatalf("AddPivotTable rejected an unmatched field name: %v", err)
	}
	table := pivotTable(t, savedParts(t, f))
	if table.RowFields.Count != 0 || len(table.RowFields.Field) != 0 {
		t.Errorf("rowFields count = %d with %d entries, want none for an unmatched name", table.RowFields.Count, len(table.RowFields.Field))
	}
	if table.ColFields.Count != 1 {
		t.Errorf("colFields count = %d, want the matched column still placed", table.ColFields.Count)
	}
}

func TestBlankHeaderCellsStillProduceACacheField(t *testing.T) {
	f := seedcalc.NewFile()
	header := []string{"Alpha", "", "Gamma"}
	if err := f.SetSheetRow("Sheet1", "A1", &header); err != nil {
		t.Fatalf("SetSheetRow: %v", err)
	}
	for i := 0; i < 4; i++ {
		row := []interface{}{"x", i, i * 2}
		f.SetSheetRow("Sheet1", fmt.Sprintf("A%d", i+2), &row)
	}
	err := f.AddPivotTable(&seedcalc.PivotTableOption{
		DataRange:       "Sheet1!$A$1:$C$5",
		PivotTableRange: "Sheet1!$E$1:$H$12",
		Rows:            []string{"Alpha"},
		Data:            []string{"Gamma"},
	})
	if err != nil {
		t.Fatalf("AddPivotTable rejected a source with a blank header cell: %v", err)
	}
	cache := pivotCache(t, savedParts(t, f))
	if cache.CacheFields.Count != 3 {
		t.Fatalf("cacheFields count = %d, want 3", cache.CacheFields.Count)
	}
	if cache.CacheFields.Field[1].Name != "" {
		t.Errorf("cacheField 1 name = %q, want the empty header name preserved", cache.CacheFields.Field[1].Name)
	}
}

func TestPageFieldsAreAccepted(t *testing.T) {
	opt := fullOption()
	opt.Page = []string{"Region"}
	f := sourceWorkbook(t)
	if err := f.AddPivotTable(opt); err != nil {
		t.Fatalf("AddPivotTable rejected a Page field: %v", err)
	}
	parts := savedParts(t, f)
	if _, ok := parts["xl/pivotTables/pivotTable1.xml"]; !ok {
		t.Error("the package has no xl/pivotTables/pivotTable1.xml part")
	}
	table := pivotTable(t, parts)
	if table.PivotFields.Count != 5 {
		t.Errorf("pivotFields count = %d, want 5", table.PivotFields.Count)
	}
}

func TestPivotTableLocationAndNameAreSet(t *testing.T) {
	parts := addPivot(t, sourceWorkbook(t), fullOption())
	table := pivotTable(t, parts)
	if table.Name == "" {
		t.Error("the pivot table definition carries no name")
	}
	if table.Location.Ref != "G2:M34" {
		t.Errorf("location ref = %q, want G2:M34", table.Location.Ref)
	}
	if table.Location.FirstHeaderRow != "1" || table.Location.FirstDataRow != "1" || table.Location.FirstDataCol != "1" {
		t.Errorf("location firstHeaderRow/firstDataRow/firstDataCol = %q/%q/%q, want 1/1/1",
			table.Location.FirstHeaderRow, table.Location.FirstDataRow, table.Location.FirstDataCol)
	}
	if table.StyleInfo.Name == "" {
		t.Error("the pivot table declares no pivotTableStyleInfo name")
	}
}
