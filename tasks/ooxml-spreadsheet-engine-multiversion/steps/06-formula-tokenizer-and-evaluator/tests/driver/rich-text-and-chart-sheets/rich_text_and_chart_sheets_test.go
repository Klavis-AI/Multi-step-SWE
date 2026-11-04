// Scored group: rich-text-and-chart-sheets.
//
// Two new kinds of content: a cell whose string is a sequence of independently
// formatted runs, and a sheet in the workbook that carries a chart instead of a
// cell grid. Behaviour transcribed from the upstream checkpoint this milestone
// tracks, qax-os/excelize tag v2.2.0 (cell_test.go, chart_test.go), restricted
// to the public interface. Rich text is ECMA-376 Part 1 section 18.4.8 "si
// (String Item)" over section 18.4.4 "r (Rich Text Run)"; a chart sheet is
// section 18.3.1.12 "chartsheet (Chart Sheet)".
package richtextandchartsheets

import (
	"archive/zip"
	"bytes"
	"strings"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// chartFormat is a minimal but complete chart description over Sheet1!A1:A3.
const chartFormat = `{"type":"col","series":[{"name":"Sheet1!$A$1","categories":"Sheet1!$A$1:$A$3","values":"Sheet1!$A$1:$A$3"}],"title":{"name":"Chart"}}`

// comboFormat is a second chart description, of the same JSON form, passed
// through AddChartSheet's variadic combo parameter.
const comboFormat = `{"type":"line","series":[{"name":"Sheet1!$A$2","categories":"Sheet1!$A$1:$A$3","values":"Sheet1!$A$1:$A$3"}],"title":{"name":"Chart"}}`

// withData returns a workbook holding three numbers in column A.
func withData(t *testing.T) *seedcalc.File {
	t.Helper()
	f := seedcalc.NewFile()
	for axis, v := range map[string]int{"A1": 1, "A2": 2, "A3": 3} {
		if err := f.SetCellValue("Sheet1", axis, v); err != nil {
			t.Fatalf("SetCellValue(%s): %v", axis, err)
		}
	}
	return f
}

// partNames returns every part name in the written package.
func partNames(t *testing.T, f *seedcalc.File) []string {
	t.Helper()
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	zr, err := zip.NewReader(bytes.NewReader(buf.Bytes()), int64(buf.Len()))
	if err != nil {
		t.Fatalf("written package is not a ZIP container: %v", err)
	}
	names := make([]string, 0, len(zr.File))
	for _, zf := range zr.File {
		names = append(names, zf.Name)
	}
	return names
}

func TestRichTextRunsConcatenateIntoTheCellString(t *testing.T) {
	f := seedcalc.NewFile()
	runs := []seedcalc.RichTextRun{
		{Text: "bold", Font: &seedcalc.Font{Bold: true, Color: "2354e8"}},
		{Text: " and "},
		{Text: "italic", Font: &seedcalc.Font{Italic: true, Family: "Times New Roman", Size: 12}},
	}
	if err := f.SetCellRichText("Sheet1", "A1", runs); err != nil {
		t.Fatalf("SetCellRichText: %v", err)
	}
	got, err := f.GetCellValue("Sheet1", "A1")
	if err != nil {
		t.Fatalf("GetCellValue: %v", err)
	}
	if got != "bold and italic" {
		t.Fatalf("GetCellValue = %q, want %q: the cell's value is the concatenation of its runs", got, "bold and italic")
	}
}

func TestASingleRunAndAnEmptyRunListAreBothAccepted(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetCellRichText("Sheet1", "A1", []seedcalc.RichTextRun{{Text: "plain"}}); err != nil {
		t.Fatalf("SetCellRichText with one run: %v", err)
	}
	got, err := f.GetCellValue("Sheet1", "A1")
	if err != nil {
		t.Fatalf("GetCellValue: %v", err)
	}
	if got != "plain" {
		t.Fatalf("GetCellValue = %q, want %q", got, "plain")
	}
	if err := f.SetCellRichText("Sheet1", "A2", []seedcalc.RichTextRun{}); err != nil {
		t.Fatalf("SetCellRichText with no runs: %v", err)
	}
	got, err = f.GetCellValue("Sheet1", "A2")
	if err != nil {
		t.Fatalf("GetCellValue: %v", err)
	}
	if got != "" {
		t.Fatalf("GetCellValue of a cell set from no runs = %q, want the empty string", got)
	}
}

func TestRichTextSurvivesTheWriteReadRoundTrip(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetCellRichText("Sheet1", "A1", []seedcalc.RichTextRun{
		{Text: "one", Font: &seedcalc.Font{Bold: true}},
		{Text: "two", Font: &seedcalc.Font{Underline: "single"}},
	}); err != nil {
		t.Fatalf("SetCellRichText: %v", err)
	}
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	g, err := seedcalc.OpenReader(bytes.NewReader(buf.Bytes()))
	if err != nil {
		t.Fatalf("OpenReader: %v", err)
	}
	got, err := g.GetCellValue("Sheet1", "A1")
	if err != nil {
		t.Fatalf("GetCellValue after round trip: %v", err)
	}
	if got != "onetwo" {
		t.Fatalf("GetCellValue after round trip = %q, want %q", got, "onetwo")
	}
}

func TestSetCellRichTextRejectsAnUnknownSheetOrMalformedCell(t *testing.T) {
	f := seedcalc.NewFile()
	runs := []seedcalc.RichTextRun{{Text: "x"}}
	if err := f.SetCellRichText("NoSuchSheet", "A1", runs); err == nil {
		t.Fatal("SetCellRichText on an absent sheet returned no error")
	}
	if err := f.SetCellRichText("Sheet1", "ZZZ", runs); err == nil {
		t.Fatal("SetCellRichText at a malformed cell reference returned no error")
	}
}

func TestAddChartSheetAppendsASheetToTheWorkbook(t *testing.T) {
	f := withData(t)
	before := len(f.GetSheetList())
	if err := f.AddChartSheet("Chart1", chartFormat); err != nil {
		t.Fatalf("AddChartSheet: %v", err)
	}
	list := f.GetSheetList()
	if len(list) != before+1 {
		t.Fatalf("workbook has %d sheets after AddChartSheet, want %d", len(list), before+1)
	}
	if list[len(list)-1] != "Chart1" {
		t.Fatalf("last sheet is %q, want the chart sheet %q appended at the end", list[len(list)-1], "Chart1")
	}
	if idx := f.GetSheetIndex("Chart1"); idx != before {
		t.Fatalf("GetSheetIndex(Chart1) = %d, want %d", idx, before)
	}
	if name := f.GetSheetName(before); name != "Chart1" {
		t.Fatalf("GetSheetName(%d) = %q, want %q", before, name, "Chart1")
	}

	// The variadic combo parameter carries further chart descriptions plotted
	// into the same chart, so a chart sheet given one is still one sheet with
	// one chart part.
	g := withData(t)
	if err := g.AddChartSheet("Combo", chartFormat, comboFormat); err != nil {
		t.Fatalf("AddChartSheet with a combo description: %v", err)
	}
	if list := g.GetSheetList(); len(list) != before+1 || list[len(list)-1] != "Combo" {
		t.Fatalf("sheets after AddChartSheet with a combo description = %v, want one appended chart sheet %q", list, "Combo")
	}
	charts := 0
	for _, n := range partNames(t, g) {
		if strings.HasPrefix(n, "xl/charts/chart") {
			charts++
		}
	}
	if charts != 1 {
		t.Fatalf("written package holds %d chart parts, want 1: a combo description is plotted into the same chart", charts)
	}
}

func TestAddChartSheetRejectsADuplicateNameAndAMalformedFormat(t *testing.T) {
	f := withData(t)
	if err := f.AddChartSheet("Sheet1", chartFormat); err == nil {
		t.Fatal("AddChartSheet reusing an existing sheet name returned no error")
	}
	if err := f.AddChartSheet("Chart1", "not json"); err == nil {
		t.Fatal("AddChartSheet with a malformed format description returned no error")
	}
	if err := f.AddChartSheet("Chart2", chartFormat, "not json"); err == nil {
		t.Fatal("AddChartSheet with a malformed combo description returned no error")
	}
}

func TestAChartSheetHasNoCellGrid(t *testing.T) {
	f := withData(t)
	if err := f.AddChartSheet("Chart1", chartFormat); err != nil {
		t.Fatalf("AddChartSheet: %v", err)
	}
	got, err := f.GetCellValue("Chart1", "A1")
	if err == nil {
		t.Fatalf("GetCellValue on a chart sheet = %q, want an error: a chart sheet carries no cells", got)
	}
	if got != "" {
		t.Fatalf("GetCellValue on a chart sheet returned %q alongside an error", got)
	}
	if err := f.SetCellValue("Chart1", "A1", 1); err == nil {
		t.Fatal("SetCellValue on a chart sheet returned no error")
	}
}

func TestAChartSheetIsWrittenAsItsOwnPart(t *testing.T) {
	f := withData(t)
	if err := f.AddChartSheet("Chart1", chartFormat); err != nil {
		t.Fatalf("AddChartSheet: %v", err)
	}
	var chartsheet, chart bool
	for _, n := range partNames(t, f) {
		// The workbook already has a worksheet (sheet1.xml), so the chart sheet
		// is assigned the next sheet index and appears as sheet2.xml.
		if n == "xl/chartsheets/sheet2.xml" {
			chartsheet = true
		}
		if n == "xl/charts/chart1.xml" {
			chart = true
		}
	}
	if !chartsheet {
		t.Fatal("written package has no xl/chartsheets/sheet2.xml part")
	}
	if !chart {
		t.Fatal("written package has no xl/charts/chart1.xml part")
	}
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	g, err := seedcalc.OpenReader(bytes.NewReader(buf.Bytes()))
	if err != nil {
		t.Fatalf("OpenReader: %v", err)
	}
	list := g.GetSheetList()
	if len(list) != 2 || list[1] != "Chart1" {
		t.Fatalf("sheet list after round trip = %v, want [Sheet1 Chart1]", list)
	}
}
