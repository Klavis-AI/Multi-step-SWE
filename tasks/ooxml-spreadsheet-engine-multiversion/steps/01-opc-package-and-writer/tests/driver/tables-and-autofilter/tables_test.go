// Scored group: tables-and-autofilter.
//
// Table parts and worksheet auto-filters. A table lives in its own part
// (ECMA-376 Part 1 clause 18.5.1.2), is declared in [Content_Types].xml, is
// related from the worksheet that hosts it, and is referenced back from the
// worksheet's tableParts collection (clause 18.3.1.95). An auto-filter is an
// autoFilter element on the worksheet itself (clause 18.3.1.2).
package tablesandautofilter

import (
	"archive/zip"
	"bytes"
	"encoding/xml"
	"sort"
	"strings"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

func writePackage(t *testing.T, f *seedcalc.File) map[string][]byte {
	t.Helper()
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	zr, err := zip.NewReader(bytes.NewReader(buf.Bytes()), int64(buf.Len()))
	if err != nil {
		t.Fatalf("written package is not a ZIP container: %v", err)
	}
	parts := map[string][]byte{}
	for _, e := range zr.File {
		rc, err := e.Open()
		if err != nil {
			t.Fatalf("open part %s: %v", e.Name, err)
		}
		var b bytes.Buffer
		if _, err := b.ReadFrom(rc); err != nil {
			t.Fatalf("read part %s: %v", e.Name, err)
		}
		rc.Close()
		parts[e.Name] = b.Bytes()
	}
	return parts
}

func mustPart(t *testing.T, parts map[string][]byte, name string) []byte {
	t.Helper()
	b, ok := parts[name]
	if !ok {
		keys := []string{}
		for k := range parts {
			keys = append(keys, k)
		}
		sort.Strings(keys)
		t.Fatalf("package has no part %s; it has %v", name, keys)
	}
	return b
}

type tablePart struct {
	Ref          string `xml:"ref,attr"`
	DisplayName  string `xml:"displayName,attr"`
	ID           string `xml:"id,attr"`
	TableColumns struct {
		Count  int `xml:"count,attr"`
		Column []struct {
			ID   string `xml:"id,attr"`
			Name string `xml:"name,attr"`
		} `xml:"tableColumn"`
	} `xml:"tableColumns"`
	TableStyleInfo struct {
		Name             string `xml:"name,attr"`
		ShowFirstColumn  string `xml:"showFirstColumn,attr"`
		ShowRowStripes   string `xml:"showRowStripes,attr"`
		ShowColumnStripe string `xml:"showColumnStripes,attr"`
	} `xml:"tableStyleInfo"`
}

func seedTable(f *seedcalc.File) {
	f.SetCellValue("Sheet1", "A1", "name")
	f.SetCellValue("Sheet1", "B1", "count")
	f.SetCellValue("Sheet1", "A2", "alpha")
	f.SetCellValue("Sheet1", "B2", 1)
}

func TestATableIsWrittenAsItsOwnDeclaredPart(t *testing.T) {
	f := seedcalc.NewFile()
	seedTable(f)
	f.AddTable("Sheet1", "A1", "B2", "")
	parts := writePackage(t, f)

	var tbl tablePart
	if err := xml.Unmarshal(mustPart(t, parts, "xl/tables/table1.xml"), &tbl); err != nil {
		t.Fatalf("the table part is not well-formed XML: %v", err)
	}
	if tbl.Ref != "A1:B2" {
		t.Errorf("table ref = %q, want %q", tbl.Ref, "A1:B2")
	}
	if tbl.DisplayName == "" || tbl.ID == "" {
		t.Errorf("table has displayName %q and id %q, want both set", tbl.DisplayName, tbl.ID)
	}

	if !strings.Contains(string(mustPart(t, parts, "[Content_Types].xml")),
		"application/vnd.openxmlformats-officedocument.spreadsheetml.table+xml") {
		t.Error("[Content_Types].xml does not declare the table part's content type")
	}
}

func TestTableColumnsAreNamedFromTheHeaderRow(t *testing.T) {
	f := seedcalc.NewFile()
	seedTable(f)
	f.AddTable("Sheet1", "A1", "B2", "")
	parts := writePackage(t, f)

	var tbl tablePart
	if err := xml.Unmarshal(mustPart(t, parts, "xl/tables/table1.xml"), &tbl); err != nil {
		t.Fatalf("the table part is not well-formed XML: %v", err)
	}
	if tbl.TableColumns.Count != 2 || len(tbl.TableColumns.Column) != 2 {
		t.Fatalf("table declares %d columns (%d elements), want 2", tbl.TableColumns.Count, len(tbl.TableColumns.Column))
	}
	for i, want := range []string{"name", "count"} {
		if got := tbl.TableColumns.Column[i].Name; got != want {
			t.Errorf("table column %d is named %q, want %q taken from the header row", i, got, want)
		}
		if tbl.TableColumns.Column[i].ID == "" {
			t.Errorf("table column %d has no id", i)
		}
	}
}

func TestTheWorksheetRelatesToAndReferencesItsTable(t *testing.T) {
	f := seedcalc.NewFile()
	seedTable(f)
	f.AddTable("Sheet1", "A1", "B2", "")
	parts := writePackage(t, f)

	var rels struct {
		Relationship []struct {
			ID     string `xml:"Id,attr"`
			Type   string `xml:"Type,attr"`
			Target string `xml:"Target,attr"`
		} `xml:"Relationship"`
	}
	if err := xml.Unmarshal(mustPart(t, parts, "xl/worksheets/_rels/sheet1.xml.rels"), &rels); err != nil {
		t.Fatalf("the worksheet relationship part is not well-formed XML: %v", err)
	}
	const tableType = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/table"
	rid := ""
	for _, r := range rels.Relationship {
		if r.Type == tableType {
			rid = r.ID
			if r.Target != "../tables/table1.xml" {
				t.Errorf("the table relationship targets %q, want %q", r.Target, "../tables/table1.xml")
			}
		}
	}
	if rid == "" {
		t.Fatal("the worksheet has no relationship of the table type")
	}

	var ws struct {
		TableParts struct {
			Count     int `xml:"count,attr"`
			TablePart []struct {
				ID string `xml:"http://schemas.openxmlformats.org/officeDocument/2006/relationships id,attr"`
			} `xml:"tablePart"`
		} `xml:"tableParts"`
	}
	if err := xml.Unmarshal(mustPart(t, parts, "xl/worksheets/sheet1.xml"), &ws); err != nil {
		t.Fatalf("the worksheet part is not well-formed XML: %v", err)
	}
	if ws.TableParts.Count != 1 || len(ws.TableParts.TablePart) != 1 {
		t.Fatalf("the worksheet declares %d tableParts, want 1", len(ws.TableParts.TablePart))
	}
	if got := ws.TableParts.TablePart[0].ID; got != rid {
		t.Errorf("tablePart references %q, but the table relationship is %q", got, rid)
	}
}

func TestSeveralTablesAreNumberedAndAccumulate(t *testing.T) {
	f := seedcalc.NewFile()
	seedTable(f)
	f.AddTable("Sheet1", "A1", "B2", "")
	f.SetCellValue("Sheet1", "D1", "other")
	f.SetCellValue("Sheet1", "D2", 9)
	f.AddTable("Sheet1", "D1", "D2", "")
	parts := writePackage(t, f)

	for _, name := range []string{"xl/tables/table1.xml", "xl/tables/table2.xml"} {
		mustPart(t, parts, name)
	}
	var second tablePart
	if err := xml.Unmarshal(mustPart(t, parts, "xl/tables/table2.xml"), &second); err != nil {
		t.Fatalf("the second table part is not well-formed XML: %v", err)
	}
	if second.Ref != "D1:D2" {
		t.Errorf("the second table ref = %q, want %q", second.Ref, "D1:D2")
	}
	var first tablePart
	if err := xml.Unmarshal(mustPart(t, parts, "xl/tables/table1.xml"), &first); err != nil {
		t.Fatalf("the first table part is not well-formed XML: %v", err)
	}
	if first.DisplayName == second.DisplayName {
		t.Errorf("both tables are called %q, want distinct display names", first.DisplayName)
	}
	if first.ID == second.ID {
		t.Errorf("both tables have id %q, want distinct identifiers", first.ID)
	}
}

func TestTableStyleOptionsAreRecorded(t *testing.T) {
	f := seedcalc.NewFile()
	seedTable(f)
	f.AddTable("Sheet1", "A1", "B2", `{"table_style":"TableStyleMedium2","show_first_column":true,"show_row_stripes":false}`)
	parts := writePackage(t, f)

	var tbl tablePart
	if err := xml.Unmarshal(mustPart(t, parts, "xl/tables/table1.xml"), &tbl); err != nil {
		t.Fatalf("the table part is not well-formed XML: %v", err)
	}
	if tbl.TableStyleInfo.Name != "TableStyleMedium2" {
		t.Errorf("tableStyleInfo name = %q, want %q", tbl.TableStyleInfo.Name, "TableStyleMedium2")
	}
	if v := tbl.TableStyleInfo.ShowFirstColumn; v != "true" && v != "1" {
		t.Errorf("showFirstColumn = %q, want it set", v)
	}
	if v := tbl.TableStyleInfo.ShowRowStripes; v != "false" && v != "0" {
		t.Errorf("showRowStripes = %q, want it cleared", v)
	}
}

func TestAutoFilterRecordsItsRangeAndRejectsWhatItCannotApply(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.AutoFilter("Sheet1", "A1", "D4", `{"column":"B","expression":"x != blanks"}`); err != nil {
		t.Fatalf("AutoFilter over A1:D4 filtering column B: %v", err)
	}
	parts := writePackage(t, f)
	var ws struct {
		SheetPr struct {
			FilterMode string `xml:"filterMode,attr"`
		} `xml:"sheetPr"`
		AutoFilter struct {
			Ref string `xml:"ref,attr"`
		} `xml:"autoFilter"`
	}
	if err := xml.Unmarshal(mustPart(t, parts, "xl/worksheets/sheet1.xml"), &ws); err != nil {
		t.Fatalf("the worksheet part is not well-formed XML: %v", err)
	}
	if ws.AutoFilter.Ref != "A1:D4" {
		t.Errorf("autoFilter ref = %q, want %q", ws.AutoFilter.Ref, "A1:D4")
	}
	if v := ws.SheetPr.FilterMode; v != "true" && v != "1" {
		t.Errorf("sheetPr filterMode = %q, want it set once a filter criterion is applied", v)
	}

	g := seedcalc.NewFile()
	if err := g.AutoFilter("Sheet1", "A1", "D4", `{"column":"Z","expression":"x != blanks"}`); err == nil {
		t.Error("AutoFilter accepted a filter column outside the filtered range")
	}
	if err := g.AutoFilter("Sheet1", "A1", "D4", `{"column":"B","expression":"x >"}`); err == nil {
		t.Error("AutoFilter accepted an incomplete filter expression")
	}
}
