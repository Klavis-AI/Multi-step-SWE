// Scored group: stream-writer-table (milestone 05, "The streaming writer and
// the drawing registry").
//
// Normative sources:
//   * TEST_INTERFACE.md section 4.05 -- "Tables on the streaming path".
//   * ECMA-376 Part 1, 18.5.1.2 table (CT_Table/@ref, @name, @displayName)
//     and 18.5.1.4 tableColumn (CT_TableColumn/@id, @name) for the table part,
//     18.3.1.95 tableParts / 18.3.1.94 tablePart for the worksheet element
//     that points at it, and 18.3.1.99 worksheet for its position after
//     sheetData.
//   * ECMA-376 Part 2, 9.3 (Relationship Markup) for the sheet-level
//     relationship the table is reached through.
//   * Cases cross-checked against the upstream test corpus at tag v2.1.0
//     (stream_test.go).
package streamwritertable

import (
	"archive/zip"
	"bytes"
	"io/ioutil"
	"strings"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// packageParts serialises f and returns every part of the saved package.
func packageParts(t *testing.T, f *seedcalc.File) map[string]string {
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

func mustPart(t *testing.T, parts map[string]string, name string) string {
	t.Helper()
	s, ok := parts[name]
	if !ok {
		t.Fatalf("the saved package has no part named %s", name)
	}
	return s
}

// relationshipID returns the Id of the relationship in a .rels part whose
// Target is target, or "" when there is none. The id is the library's to
// allocate; what is graded is that the worksheet's tablePart names the
// relationship the table is reached through (TEST_INTERFACE.md 4.05.1).
func relationshipID(rels, target string) string {
	for _, rel := range strings.Split(rels, "<Relationship") {
		if !strings.Contains(rel, `Target="`+target+`"`) {
			continue
		}
		i := strings.Index(rel, `Id="`)
		if i < 0 {
			continue
		}
		rest := rel[i+len(`Id="`):]
		if j := strings.Index(rest, `"`); j >= 0 {
			return rest[:j]
		}
	}
	return ""
}

// headerAndBody streams a three-row block into a fresh workbook.
func headerAndBody(t *testing.T) (*seedcalc.File, *seedcalc.StreamWriter) {
	t.Helper()
	f := seedcalc.NewFile()
	sw, err := f.NewStreamWriter("Sheet1")
	if err != nil {
		t.Fatalf("NewStreamWriter: %v", err)
	}
	if err := sw.SetRow("A1", []interface{}{"Region", "Units", "Value"}); err != nil {
		t.Fatalf("SetRow: %v", err)
	}
	if err := sw.SetRow("A2", []interface{}{"North", 1, 2}); err != nil {
		t.Fatalf("SetRow: %v", err)
	}
	if err := sw.SetRow("A3", []interface{}{"South", 3, 4}); err != nil {
		t.Fatalf("SetRow: %v", err)
	}
	return f, sw
}

func TestStreamAddTableWritesTablePartAndRelationship(t *testing.T) {
	f, sw := headerAndBody(t)
	if err := sw.AddTable("A1", "C3", ``); err != nil {
		t.Fatalf("AddTable: %v", err)
	}
	if err := sw.Flush(); err != nil {
		t.Fatalf("Flush: %v", err)
	}
	parts := packageParts(t, f)

	table := mustPart(t, parts, "xl/tables/table1.xml")
	if !strings.Contains(table, `ref="A1:C3"`) {
		t.Errorf("table part does not carry ref=\"A1:C3\"\ngot: %s", table)
	}
	for _, want := range []string{`name="Region"`, `name="Units"`, `name="Value"`} {
		if !strings.Contains(table, want) {
			t.Errorf("table columns are not taken from the first streamed row: %s missing\ngot: %s", want, table)
		}
	}

	rels := mustPart(t, parts, "xl/worksheets/_rels/sheet1.xml.rels")
	if !strings.Contains(rels, "../tables/table1.xml") {
		t.Errorf("the worksheet relationship part does not target the table\ngot: %s", rels)
	}
	if !strings.Contains(rels, "/relationships/table") {
		t.Errorf("the table relationship does not carry the table relationship type\ngot: %s", rels)
	}

	types := mustPart(t, parts, "[Content_Types].xml")
	if !strings.Contains(types, "/xl/tables/table1.xml") {
		t.Errorf("[Content_Types].xml does not declare the table part\ngot: %s", types)
	}
}

func TestStreamAddTableLinksTablePartsAfterSheetData(t *testing.T) {
	f, sw := headerAndBody(t)
	if err := sw.AddTable("A1", "C3", ``); err != nil {
		t.Fatalf("AddTable: %v", err)
	}
	if err := sw.Flush(); err != nil {
		t.Fatalf("Flush: %v", err)
	}
	parts := packageParts(t, f)
	sheet := mustPart(t, parts, "xl/worksheets/sheet1.xml")

	if !strings.Contains(sheet, `<tableParts count="1">`) {
		t.Fatalf("the streamed worksheet has no tableParts element\ngot: %s", sheet)
	}
	// The relationship id's spelling is the library's own; the reference is
	// graded against the id the sheet's relationship part actually carries.
	rels := mustPart(t, parts, "xl/worksheets/_rels/sheet1.xml.rels")
	id := relationshipID(rels, "../tables/table1.xml")
	if id == "" {
		t.Fatalf("the worksheet has no relationship targeting the table\ngot: %s", rels)
	}
	if !strings.Contains(sheet, `<tablePart r:id="`+id+`">`) {
		t.Errorf("tableParts does not reference the table relationship %q\ngot: %s", id, sheet)
	}
	closeData := strings.Index(sheet, "</sheetData>")
	tableParts := strings.Index(sheet, "<tableParts")
	if closeData < 0 || tableParts < 0 || tableParts < closeData {
		t.Errorf("tableParts must be emitted after sheetData is closed (sheetData ends at %d, tableParts at %d)", closeData, tableParts)
	}
}

func TestStreamAddTableExpandsSingleRowArea(t *testing.T) {
	f := seedcalc.NewFile()
	sw, err := f.NewStreamWriter("Sheet1")
	if err != nil {
		t.Fatalf("NewStreamWriter: %v", err)
	}
	if err := sw.SetRow("A1", []interface{}{"Region", "Units"}); err != nil {
		t.Fatalf("SetRow: %v", err)
	}
	// A table needs a header row plus at least one data row.
	if err := sw.AddTable("A1", "B1", ``); err != nil {
		t.Fatalf("AddTable: %v", err)
	}
	if err := sw.Flush(); err != nil {
		t.Fatalf("Flush: %v", err)
	}
	table := mustPart(t, packageParts(t, f), "xl/tables/table1.xml")
	if !strings.Contains(table, `ref="A1:B2"`) {
		t.Errorf("a one-row area must be widened to two rows; table ref is not A1:B2\ngot: %s", table)
	}
}

func TestStreamAddTableHonoursFormatSetAndRejectsBadInput(t *testing.T) {
	f, sw := headerAndBody(t)
	if err := sw.AddTable("A1", "C3", `{"table_name":"Sales","table_style":"TableStyleMedium2","show_first_column":true,"show_last_column":true,"show_row_stripes":false,"show_column_stripes":true}`); err != nil {
		t.Fatalf("AddTable with a format set: %v", err)
	}
	if err := sw.Flush(); err != nil {
		t.Fatalf("Flush: %v", err)
	}
	table := mustPart(t, packageParts(t, f), "xl/tables/table1.xml")
	for _, want := range []string{
		`name="Sales"`,
		`displayName="Sales"`,
		`name="TableStyleMedium2"`,
		`showFirstColumn="true"`,
		`showLastColumn="true"`,
		`showRowStripes="false"`,
		`showColumnStripes="true"`,
	} {
		if !strings.Contains(table, want) {
			t.Errorf("the table part does not reflect the format set: %s missing\ngot: %s", want, table)
		}
	}

	_, bad := headerAndBody(t)
	if err := bad.AddTable("A1", "C3", `{`); err == nil {
		t.Error("AddTable with a malformed format set returned a nil error")
	}
	if err := bad.AddTable("", "C3", ``); err == nil {
		t.Error("AddTable with a malformed top-left reference returned a nil error")
	}
	if err := bad.AddTable("A1", "", ``); err == nil {
		t.Error("AddTable with a malformed bottom-right reference returned a nil error")
	}
}
