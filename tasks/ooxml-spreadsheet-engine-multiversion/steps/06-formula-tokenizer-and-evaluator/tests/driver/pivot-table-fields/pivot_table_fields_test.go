// Scored group: pivot-table-fields.
//
// The PivotTableField struct that replaces the plain []string fields from
// milestone 04 (Columns/Rows/Data were []string, and Page []string is gone in
// favour of Filter []PivotTableField). The new Filter field and the
// PivotTableField.Subtotal attribute are exercised here against the pivot
// table definition that reaches the package, not merely against a nil error.
// Behaviour verified against qax-os/excelize tag v2.2.0 (pivotTable.go). The
// pivot cache triple is ECMA-376 Part 1 section 18.10; the elements asserted
// on are section 18.10.1.70 "pivotFields" over 18.10.1.69 "pivotField",
// 18.10.1.63 "pageFields" over 18.10.1.62 "pageField", and 18.10.1.23
// "dataFields" over 18.10.1.22 "dataField".
package pivottablefields

import (
	"archive/zip"
	"bytes"
	"encoding/xml"
	"io"
	"strings"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// pivotXML writes f and returns xl/pivotTables/pivotTable1.xml as a string.
func pivotXML(t *testing.T, f *seedcalc.File) string {
	t.Helper()
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	zr, err := zip.NewReader(bytes.NewReader(buf.Bytes()), int64(buf.Len()))
	if err != nil {
		t.Fatalf("written package is not a readable ZIP container: %v", err)
	}
	for _, zf := range zr.File {
		if zf.Name != "xl/pivotTables/pivotTable1.xml" {
			continue
		}
		rc, err := zf.Open()
		if err != nil {
			t.Fatalf("open xl/pivotTables/pivotTable1.xml: %v", err)
		}
		raw, err := io.ReadAll(rc)
		rc.Close()
		if err != nil {
			t.Fatalf("read xl/pivotTables/pivotTable1.xml: %v", err)
		}
		return string(raw)
	}
	t.Fatal("written package has no xl/pivotTables/pivotTable1.xml")
	return ""
}

// wantIn asserts that the pivot table definition carries needle.
func wantIn(t *testing.T, def, needle, what string) {
	t.Helper()
	if !strings.Contains(def, needle) {
		t.Fatalf("pivot table definition does not carry %s (%q)", what, needle)
	}
}

// elementAttrs decodes def and returns the attributes of its first element
// named local, keyed by attribute local name. Attribute order is not
// significant in XML and no document fixes the order this library happens to
// emit, so each attribute is asserted on its own rather than against a
// serialized start tag.
func elementAttrs(t *testing.T, def, local string) map[string]string {
	t.Helper()
	dec := xml.NewDecoder(strings.NewReader(def))
	for {
		tok, err := dec.Token()
		if err == io.EOF {
			break
		}
		if err != nil {
			t.Fatalf("pivot table definition is not well-formed XML: %v", err)
		}
		se, ok := tok.(xml.StartElement)
		if !ok || se.Name.Local != local {
			continue
		}
		attrs := map[string]string{}
		for _, a := range se.Attr {
			attrs[a.Name.Local] = a.Value
		}
		return attrs
	}
	t.Fatalf("pivot table definition has no <%s> element", local)
	return nil
}

// wantAttr asserts that the first <local> element in def carries attr=value.
// Only the attribute the contract fixes is read: the element may carry any of
// the other attributes its ECMA-376 complex type permits, may spell them in
// any order, and may be self-closing, none of which this checks.
func wantAttr(t *testing.T, def, local, attr, value, what string) {
	t.Helper()
	if got := elementAttrs(t, def, local)[attr]; got != value {
		t.Fatalf("<%s> @%s = %q, want %q (%s)", local, attr, got, value, what)
	}
}

// pivotBook builds a small workbook with a header row and four data rows
// suitable as a pivot source, and creates Sheet2 as the pivot destination.
func pivotBook(t *testing.T) *seedcalc.File {
	t.Helper()
	f := seedcalc.NewFile()
	f.SetSheetRow("Sheet1", "A1", &[]interface{}{"Region", "Item", "Month", "Sales"})
	for i, row := range [][]interface{}{
		{"East", "Apple", 1, 100},
		{"West", "Banana", 2, 200},
		{"East", "Apple", 3, 300},
		{"West", "Banana", 4, 400},
	} {
		cell, _ := seedcalc.CoordinatesToCellName(1, i+2)
		f.SetSheetRow("Sheet1", cell, &row)
	}
	f.NewSheet("Sheet2")
	return f
}

func TestAddPivotTableWithPivotTableFieldSucceeds(t *testing.T) {
	f := pivotBook(t)
	opt := &seedcalc.PivotTableOption{
		DataRange:       "Sheet1!$A$1:$D$5",
		PivotTableRange: "Sheet2!$A$1:$AN$20",
		Rows:            []seedcalc.PivotTableField{{Data: "Month"}},
		Columns:         []seedcalc.PivotTableField{{Data: "Region"}},
		Data:            []seedcalc.PivotTableField{{Data: "Sales", Name: "SumOfSales", Subtotal: "Sum"}},
		Filter:          []seedcalc.PivotTableField{{Data: "Item"}},
	}
	if err := f.AddPivotTable(opt); err != nil {
		t.Fatalf("AddPivotTable: %v", err)
	}

	// Each placement has to reach the definition, and the three placements
	// have to stay distinguishable: a Filter field becomes an axisPage
	// pivotField plus a pageField naming its zero-based ordinal in the data
	// range, while Rows and Columns become axisRow and axisCol.
	def := pivotXML(t, f)
	wantIn(t, def, `axis="axisPage"`, "the Filter field's page axis")
	wantAttr(t, def, "pageFields", "count", "1", "one Filter field was given")
	wantAttr(t, def, "pageField", "fld", "1", "Item is column 1 of the data range")
	wantIn(t, def, `axis="axisRow"`, "the Rows field's row axis")
	wantIn(t, def, `axis="axisCol"`, "the Columns field's column axis")
	wantIn(t, def, `dataField="true"`, "the Data field's pivotField marker")
}

func TestPivotTableRoundTripsWithoutError(t *testing.T) {
	f := pivotBook(t)
	opt := &seedcalc.PivotTableOption{
		DataRange:       "Sheet1!$A$1:$D$5",
		PivotTableRange: "Sheet2!$A$1:$AN$20",
		Rows:            []seedcalc.PivotTableField{{Data: "Month"}},
		Columns:         []seedcalc.PivotTableField{{Data: "Region"}},
		Data:            []seedcalc.PivotTableField{{Data: "Sales", Name: "SumOfSales", Subtotal: "Sum"}},
	}
	if err := f.AddPivotTable(opt); err != nil {
		t.Fatalf("AddPivotTable: %v", err)
	}
	tmp := t.TempDir()
	path := tmp + "/pivot.xlsx"
	if err := f.SaveAs(path); err != nil {
		t.Fatalf("SaveAs: %v", err)
	}
	g, err := seedcalc.OpenFile(path)
	if err != nil {
		t.Fatalf("OpenFile after round trip: %v", err)
	}
	sheets := g.GetSheetList()
	if len(sheets) < 2 {
		t.Fatalf("reopened workbook has %d sheets, want at least 2", len(sheets))
	}
}

func TestAddPivotTableRejectsAnAbsentDataSheet(t *testing.T) {
	f := seedcalc.NewFile()
	opt := &seedcalc.PivotTableOption{
		DataRange:       "NoSheet!$A$1:$D$5",
		PivotTableRange: "Sheet1!$A$1:$AN$20",
		Rows:            []seedcalc.PivotTableField{{Data: "Month"}},
		Data:            []seedcalc.PivotTableField{{Data: "Sales", Name: "SumOfSales", Subtotal: "Sum"}},
	}
	if err := f.AddPivotTable(opt); err == nil {
		t.Fatal("AddPivotTable with absent data sheet returned no error")
	}
}

func TestPivotTableFieldNameAndSubtotalReachTheDataField(t *testing.T) {
	// The old []string API had no per-field name or subtotal. Both now travel
	// into the dataField element: Name verbatim into @name, Subtotal
	// lower-cased into @subtotal, and @fld holding the zero-based ordinal of
	// the source column within the data range.
	f := pivotBook(t)
	opt := &seedcalc.PivotTableOption{
		DataRange:       "Sheet1!$A$1:$D$5",
		PivotTableRange: "Sheet2!$A$1:$AN$20",
		Rows:            []seedcalc.PivotTableField{{Data: "Month"}},
		Columns:         []seedcalc.PivotTableField{{Data: "Region"}},
		Data:            []seedcalc.PivotTableField{{Data: "Sales", Name: "SumOfSales", Subtotal: "Sum"}},
	}
	if err := f.AddPivotTable(opt); err != nil {
		t.Fatalf("AddPivotTable: %v", err)
	}
	def := pivotXML(t, f)
	wantAttr(t, def, "dataFields", "count", "1", "one Data field was given")
	attrs := elementAttrs(t, def, "dataField")
	for _, want := range []struct{ attr, value, what string }{
		{"name", "SumOfSales", "Name written verbatim"},
		{"fld", "3", "the zero-based ordinal of Sales in the data range"},
		{"subtotal", "sum", "Subtotal lower-cased"},
	} {
		if got := attrs[want.attr]; got != want.value {
			t.Fatalf("<dataField> @%s = %q, want %q (%s)", want.attr, got, want.value, want.what)
		}
	}
	// A workbook given no Filter field carries no page axis at all.
	if strings.Contains(def, `axis="axisPage"`) {
		t.Fatal("pivot table definition declares a page axis though no Filter field was given")
	}
}
