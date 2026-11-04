// Scored group: pivot-option-breadth (milestone 04, peripheral).
//
// The existing "new"-tier pivot-table-fields and pivot-table-parts drivers
// are thorough about the straightforward reading of PivotTableOption's Rows,
// Columns, Data and Page fields. TEST_INTERFACE.md 4.04.6 states two pairs of
// behaviours those drivers do not pin -- a per-column reading (one pivotField
// per source column, at most one axis, Rows before Columns before Data) and a
// per-option-list reading (each list built from its own option list with no
// awareness of the others or of duplicates within one list); and two places
// where output is emitted without regard to the option at all (rowItems is a
// fixed two-slot stub, colItems is never written, and Page is accepted but
// never reaches the part).
//
// None of this can be observed before milestone 04: PivotTableOption and
// AddPivotTable are new symbols with no milestone-03 counterpart, so every
// case here fails to compile against the milestone-03 predecessor.
package pivotoptionbreadth

import (
	"archive/zip"
	"bytes"
	"encoding/xml"
	"fmt"
	"io/ioutil"
	"strings"
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

type rowItem struct {
	X []struct{} `xml:"x"`
}

type tableDefinition struct {
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
	RowItems struct {
		Count int       `xml:"count,attr"`
		I     []rowItem `xml:"i"`
	} `xml:"rowItems"`
	ColFields struct {
		Count int            `xml:"count,attr"`
		Field []indexedField `xml:"field"`
	} `xml:"colFields"`
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

func pivotTableRaw(t *testing.T, parts map[string]string) string {
	t.Helper()
	raw, ok := parts["xl/pivotTables/pivotTable1.xml"]
	if !ok {
		t.Fatal("the package has no xl/pivotTables/pivotTable1.xml part")
	}
	return raw
}

// sourceWorkbook builds the same five column, thirty row data set used by
// the "new"-tier pivot drivers: Month, Year, Type, Sales, Region at columns
// 0-4.
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

// TestFieldListedInBothRowsAndColumnsGetsCrossListedDespiteSingleAxis puts
// the same field name in both Rows and Columns. addPivotFields walks the
// source header once per column and checks Rows before Columns, so the
// field's pivotField is marked axisRow only. But RowFields and ColFields are
// each built by an independent call to getPivotFieldsIndex(fields, opt) that
// only checks membership in its own list against the header -- it has no
// idea addPivotFields already claimed the field for the row axis -- so the
// field's index ends up listed under both rowFields and colFields even
// though only one axis was actually assigned.
func TestFieldListedInBothRowsAndColumnsGetsCrossListedDespiteSingleAxis(t *testing.T) {
	opt := &seedcalc.PivotTableOption{
		DataRange:       "Sheet1!$A$1:$E$31",
		PivotTableRange: "Sheet1!$G$2:$M$34",
		Rows:            []string{"Type"},
		Columns:         []string{"Type"},
		Data:            []string{"Sales"},
	}
	table := pivotTable(t, addPivot(t, sourceWorkbook(t), opt))
	const typeIdx = 2
	if table.PivotFields.Field[typeIdx].Axis != "axisRow" {
		t.Fatalf("pivotField %d axis = %q, want axisRow (Rows is checked before Columns)", typeIdx, table.PivotFields.Field[typeIdx].Axis)
	}
	if len(table.RowFields.Field) != 1 || table.RowFields.Field[0].X != typeIdx {
		t.Errorf("rowFields = %+v, want exactly one entry at index %d", table.RowFields.Field, typeIdx)
	}
	if len(table.ColFields.Field) != 1 || table.ColFields.Field[0].X != typeIdx {
		t.Errorf("colFields = %+v, want the same field also cross-listed at index %d despite its axisRow marking", table.ColFields.Field, typeIdx)
	}
}

// TestRowItemsIsAFixedStubIndependentOfFieldCount checks that rowItems is
// the literal two-slot stub addPivotTable builds once in its initial struct
// literal (Count 1, a single <i> with exactly two <x/> children), regardless
// of whether Rows names zero or two fields -- and that colItems never
// appears at all, since ColItems is never assigned anywhere in addPivotTable.
func TestRowItemsIsAFixedStubIndependentOfFieldCount(t *testing.T) {
	noRows := &seedcalc.PivotTableOption{
		DataRange: "Sheet1!$A$1:$E$31", PivotTableRange: "Sheet1!$G$2:$M$34",
		Columns: []string{"Type"}, Data: []string{"Sales"},
	}
	twoRows := &seedcalc.PivotTableOption{
		DataRange: "Sheet1!$A$1:$E$31", PivotTableRange: "Sheet1!$G$2:$M$34",
		Rows: []string{"Month", "Year"}, Columns: []string{"Type"}, Data: []string{"Sales"},
	}
	for _, c := range []struct {
		name string
		opt  *seedcalc.PivotTableOption
	}{
		{"zero row fields", noRows},
		{"two row fields", twoRows},
	} {
		parts := addPivot(t, sourceWorkbook(t), c.opt)
		table := pivotTable(t, parts)
		if table.RowItems.Count != 1 || len(table.RowItems.I) != 1 {
			t.Fatalf("%s: rowItems count = %d with %d <i> elements, want 1 and 1", c.name, table.RowItems.Count, len(table.RowItems.I))
		}
		if len(table.RowItems.I[0].X) != 2 {
			t.Errorf("%s: rowItems <i> holds %d <x/> children, want the fixed 2", c.name, len(table.RowItems.I[0].X))
		}
		if strings.Contains(pivotTableRaw(t, parts), "<colItems") {
			t.Errorf("%s: the pivot table part has a <colItems> element, want none (ColItems is never assigned)", c.name)
		}
	}
}

// TestPageFieldChangesNothing checks that adding a Page field produces a
// byte-for-byte identical pivotTable1.xml part compared to an otherwise
// identical call that omits Page entirely -- Page is declared on
// PivotTableOption but pivotTable.go never reads opt.Page anywhere, so it
// cannot affect the field's axis, data-field marking, or anything else in
// the written definition.
func TestPageFieldChangesNothing(t *testing.T) {
	without := &seedcalc.PivotTableOption{
		DataRange:       "Sheet1!$A$1:$E$31",
		PivotTableRange: "Sheet1!$G$2:$M$34",
		Rows:            []string{"Month", "Year"},
		Columns:         []string{"Type"},
		Data:            []string{"Sales"},
	}
	withPage := &seedcalc.PivotTableOption{
		DataRange:       "Sheet1!$A$1:$E$31",
		PivotTableRange: "Sheet1!$G$2:$M$34",
		Rows:            []string{"Month", "Year"},
		Columns:         []string{"Type"},
		Data:            []string{"Sales"},
		Page:            []string{"Region"},
	}
	rawWithout := pivotTableRaw(t, addPivot(t, sourceWorkbook(t), without))
	rawWith := pivotTableRaw(t, addPivot(t, sourceWorkbook(t), withPage))
	if rawWith != rawWithout {
		t.Errorf("adding a Page field changed xl/pivotTables/pivotTable1.xml:\nwithout Page: %s\nwith Page:    %s", rawWithout, rawWith)
	}
}

// TestDuplicateNameWithinOneListIsNotDeduplicated repeats one field name
// twice within Rows. The per-column pass in addPivotFields naturally never
// duplicates a pivotField, since it walks the header once and the header
// only lists "Month" a single time -- but getPivotFieldsIndex walks the
// requested Rows list itself and appends a match for every occurrence, with
// no deduplication, so rowFields ends up with two entries pointing at the
// very same field index.
func TestDuplicateNameWithinOneListIsNotDeduplicated(t *testing.T) {
	opt := &seedcalc.PivotTableOption{
		DataRange:       "Sheet1!$A$1:$E$31",
		PivotTableRange: "Sheet1!$G$2:$M$34",
		Rows:            []string{"Month", "Month"},
		Columns:         []string{"Type"},
		Data:            []string{"Sales"},
	}
	table := pivotTable(t, addPivot(t, sourceWorkbook(t), opt))
	if table.PivotFields.Count != 5 {
		t.Fatalf("pivotFields count = %d, want 5 (one per source column, not inflated by the duplicate)", table.PivotFields.Count)
	}
	const monthIdx = 0
	if table.PivotFields.Field[monthIdx].Axis != "axisRow" {
		t.Fatalf("pivotField %d axis = %q, want axisRow", monthIdx, table.PivotFields.Field[monthIdx].Axis)
	}
	if table.RowFields.Count != 2 || len(table.RowFields.Field) != 2 {
		t.Fatalf("rowFields count = %d with %d entries, want 2 and 2 (one per Rows list entry, duplicate included)", table.RowFields.Count, len(table.RowFields.Field))
	}
	if table.RowFields.Field[0].X != monthIdx || table.RowFields.Field[1].X != monthIdx {
		t.Errorf("rowFields indices = %d, %d, want both to be %d (the same field listed twice)", table.RowFields.Field[0].X, table.RowFields.Field[1].X, monthIdx)
	}
}
