// Scored group: tables-and-filters (milestone 03 peripheral surface).
//
// Tables, auto filters and data validation sit next to this milestone's
// defined names and page setup and share the worksheet writer path.
// Normative sources: TEST_INTERFACE.md sections 4.01, 4.02 and 4.03;
// ECMA-376 Part 1, 18.5.1.2 table, 18.3.1.2 autoFilter, 18.3.1.33
// dataValidation.
package tablesandfilters

import (
	"archive/zip"
	"bytes"
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

func headerRow(t *testing.T, f *seedcalc.File) {
	t.Helper()
	header := []interface{}{"Column1", "Column2", "Column3"}
	if err := f.SetSheetRow("Sheet1", "A1", &header); err != nil {
		t.Fatalf("SetSheetRow: %v", err)
	}
	for i := 2; i <= 5; i++ {
		row := []interface{}{i, i * 2, i * 3}
		cell, err := seedcalc.CoordinatesToCellName(1, i)
		if err != nil {
			t.Fatalf("CoordinatesToCellName: %v", err)
		}
		if err := f.SetSheetRow("Sheet1", cell, &row); err != nil {
			t.Fatalf("SetSheetRow: %v", err)
		}
	}
}

func TestAddTableCreatesATablePart(t *testing.T) {
	f := seedcalc.NewFile()
	headerRow(t, f)
	if err := f.AddTable("Sheet1", "A1", "C5",
		`{"table_name":"table","table_style":"TableStyleMedium2","show_first_column":true,"show_last_column":true}`); err != nil {
		t.Fatalf("AddTable: %v", err)
	}
	parts := savedParts(t, f)
	if _, ok := parts["xl/tables/table1.xml"]; !ok {
		t.Fatal("AddTable produced no xl/tables/table1.xml part")
	}
	if !strings.Contains(parts["xl/tables/table1.xml"], `ref="A1:C5"`) {
		t.Error("the table part does not declare the range A1:C5")
	}
	if !strings.Contains(parts["xl/worksheets/sheet1.xml"], "tableParts") {
		t.Error("the worksheet does not reference its table parts")
	}
	if !strings.Contains(parts["[Content_Types].xml"], "/xl/tables/table1.xml") {
		t.Error("[Content_Types].xml does not declare the table part")
	}
}

func TestAddTableRejectsAnInvalidRequest(t *testing.T) {
	f := seedcalc.NewFile()
	headerRow(t, f)
	if err := f.AddTable("Sheet1", "", "C5", `{"table_name":"t","table_style":"TableStyleMedium2"}`); err == nil {
		t.Error("AddTable with a malformed start reference returned nil, want a non-nil error")
	}
	if err := f.AddTable("Sheet1", "A1", "", `{"table_name":"t","table_style":"TableStyleMedium2"}`); err == nil {
		t.Error("AddTable with a malformed end reference returned nil, want a non-nil error")
	}
	if err := f.AddTable("Sheet1", "A1", "C5", `{"table_name":`); err == nil {
		t.Error("AddTable with a malformed format returned nil, want a non-nil error")
	}
}

func TestAutoFilterWritesAFilterDefinition(t *testing.T) {
	f := seedcalc.NewFile()
	headerRow(t, f)
	if err := f.AutoFilter("Sheet1", "A1", "C5", `{"column":"B","expression":"x != blanks"}`); err != nil {
		t.Fatalf("AutoFilter: %v", err)
	}
	sheet := savedParts(t, f)["xl/worksheets/sheet1.xml"]
	if !strings.Contains(sheet, "<autoFilter") {
		t.Error("the worksheet carries no autoFilter element")
	}
	if !strings.Contains(sheet, `ref="A1:C5"`) {
		t.Error("the autoFilter element does not declare the range A1:C5")
	}
	if err := f.AutoFilter("Sheet1", "A1", "C5", `{"column":"Z","expression":"x != blanks"}`); err == nil {
		t.Error("AutoFilter with a column outside the range returned nil, want a non-nil error")
	}
	if err := f.AutoFilter("NoSuchSheet", "A1", "C5", ""); err == nil {
		t.Error("AutoFilter on an unknown worksheet returned nil, want a non-nil error")
	}
}

func TestDataValidationIsWrittenToTheWorksheet(t *testing.T) {
	f := seedcalc.NewFile()
	dv := seedcalc.NewDataValidation(true)
	dv.Sqref = "A1:B2"
	if err := dv.SetRange(10, 20, seedcalc.DataValidationTypeWhole, seedcalc.DataValidationOperatorBetween); err != nil {
		t.Fatalf("SetRange: %v", err)
	}
	dv.SetError(seedcalc.DataValidationErrorStyleStop, "title", "body")
	if err := f.AddDataValidation("Sheet1", dv); err != nil {
		t.Fatalf("AddDataValidation: %v", err)
	}

	list := seedcalc.NewDataValidation(true)
	list.SetSqref("C1:C10")
	if err := list.SetDropList([]string{"alpha", "beta"}); err != nil {
		t.Fatalf("SetDropList: %v", err)
	}
	if err := f.AddDataValidation("Sheet1", list); err != nil {
		t.Fatalf("AddDataValidation: %v", err)
	}

	sheet := savedParts(t, f)["xl/worksheets/sheet1.xml"]
	if !strings.Contains(sheet, "<dataValidations") {
		t.Fatal("the worksheet carries no dataValidations element")
	}
	for _, want := range []string{`sqref="A1:B2"`, `sqref="C1:C10"`, `type="whole"`, `operator="between"`, `type="list"`} {
		if !strings.Contains(sheet, want) {
			t.Errorf("the dataValidations element does not carry %s", want)
		}
	}
}
