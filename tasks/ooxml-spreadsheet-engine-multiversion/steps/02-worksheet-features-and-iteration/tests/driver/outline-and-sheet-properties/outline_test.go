// Scored group: outline-and-sheet-properties.
//
// Row and column outline (grouping) levels and the worksheet-level properties
// that go with them, including the direction in which a group's summary row
// sits. Graded against ECMA-376 Part 1 clause 18.3.1.73 (row/@outlineLevel),
// clause 18.3.1.13 (col/@outlineLevel), clause 18.3.1.61 (outlinePr, whose
// summaryBelow attribute defaults to true) and clause 18.3.1.82 (sheetPr).
// Behaviour cases were checked against the upstream release this checkpoint
// tracks (tag v1.4.1).
package outlineandsheetproperties

import (
	"archive/zip"
	"bytes"
	"encoding/xml"
	"io"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

type wsRow struct {
	R            int   `xml:"r,attr"`
	OutlineLevel uint8 `xml:"outlineLevel,attr"`
}

type wsCol struct {
	Min          int   `xml:"min,attr"`
	Max          int   `xml:"max,attr"`
	OutlineLevel uint8 `xml:"outlineLevel,attr"`
}

type wsOutlinePr struct {
	SummaryBelow *bool `xml:"summaryBelow,attr"`
}

type wsPageSetUpPr struct {
	FitToPage      bool `xml:"fitToPage,attr"`
	AutoPageBreaks bool `xml:"autoPageBreaks,attr"`
}

type wsSheetPr struct {
	CodeName                          string         `xml:"codeName,attr"`
	EnableFormatConditionsCalculation *bool          `xml:"enableFormatConditionsCalculation,attr"`
	Published                         *bool          `xml:"published,attr"`
	PageSetUpPr                       *wsPageSetUpPr `xml:"pageSetUpPr"`
	OutlinePr                         *wsOutlinePr   `xml:"outlinePr"`
}

type worksheet struct {
	SheetPr *wsSheetPr `xml:"sheetPr"`
	Cols    []wsCol    `xml:"cols>col"`
	Rows    []wsRow    `xml:"sheetData>row"`
}

func sheetPart(t *testing.T, f *seedcalc.File) []byte {
	t.Helper()
	buf, err := f.WriteToBuffer()
	if err != nil {
		t.Fatalf("WriteToBuffer: %v", err)
	}
	b := buf.Bytes()
	zr, err := zip.NewReader(bytes.NewReader(b), int64(len(b)))
	if err != nil {
		t.Fatalf("written package is not a ZIP container: %v", err)
	}
	const name = "xl/worksheets/sheet1.xml"
	for _, fh := range zr.File {
		if fh.Name != name {
			continue
		}
		rc, err := fh.Open()
		if err != nil {
			t.Fatalf("open %s: %v", name, err)
		}
		defer rc.Close()
		var out bytes.Buffer
		if _, err := io.Copy(&out, rc); err != nil {
			t.Fatalf("read %s: %v", name, err)
		}
		return out.Bytes()
	}
	t.Fatalf("written package has no part %s", name)
	return nil
}

func decodeSheet(t *testing.T, f *seedcalc.File) worksheet {
	t.Helper()
	var ws worksheet
	if err := xml.Unmarshal(sheetPart(t, f), &ws); err != nil {
		t.Fatalf("worksheet part is not well-formed XML: %v", err)
	}
	return ws
}

func TestRowOutlineLevelRoundTrips(t *testing.T) {
	f := seedcalc.NewFile()
	f.SetRowOutlineLevel("Sheet1", 2, 4)

	if got := f.GetRowOutlineLevel("Sheet1", 2); got != 4 {
		t.Errorf("GetRowOutlineLevel(sheet, 2) = %d, want 4", got)
	}
	if got := f.GetRowOutlineLevel("Sheet1", 1); got != 0 {
		t.Errorf("GetRowOutlineLevel(sheet, 1) = %d, want 0 -- only the addressed row is grouped", got)
	}
	if got := f.GetRowOutlineLevel("Sheet1", 99); got != 0 {
		t.Errorf("GetRowOutlineLevel for a row that was never written = %d, want 0", got)
	}
}

func TestRowOutlineLevelReachesTheRowElement(t *testing.T) {
	f := seedcalc.NewFile()
	f.SetRowOutlineLevel("Sheet1", 2, 4)

	ws := decodeSheet(t, f)
	var grouped []wsRow
	for _, r := range ws.Rows {
		if r.OutlineLevel != 0 {
			grouped = append(grouped, r)
		}
	}
	if len(grouped) != 1 {
		t.Fatalf("%d rows carry an outlineLevel, want exactly 1: %+v", len(grouped), ws.Rows)
	}
	if grouped[0].OutlineLevel != 4 {
		t.Errorf("row outlineLevel = %d, want 4", grouped[0].OutlineLevel)
	}
	// The row index is zero-based; row index 2 is the row whose r attribute is 3.
	if grouped[0].R != 3 {
		t.Errorf("outlineLevel landed on the row with r=%d, want r=3 for row index 2", grouped[0].R)
	}
}

func TestColumnOutlineLevelRoundTrips(t *testing.T) {
	f := seedcalc.NewFile()
	f.SetColOutlineLevel("Sheet1", "D", 2)

	if got := f.GetColOutlineLevel("Sheet1", "D"); got != 2 {
		t.Errorf("GetColOutlineLevel(sheet, \"D\") = %d, want 2", got)
	}
	if got := f.GetColOutlineLevel("Sheet1", "A"); got != 0 {
		t.Errorf("GetColOutlineLevel(sheet, \"A\") = %d, want 0 -- only the addressed column is grouped", got)
	}

	f.SetColOutlineLevel("Sheet1", "D", 5)
	if got := f.GetColOutlineLevel("Sheet1", "D"); got != 5 {
		t.Errorf("GetColOutlineLevel after a second SetColOutlineLevel = %d, want 5", got)
	}
}

func TestColumnOutlineLevelReachesTheColElement(t *testing.T) {
	f := seedcalc.NewFile()
	f.SetColOutlineLevel("Sheet1", "D", 2)

	ws := decodeSheet(t, f)
	var grouped []wsCol
	for _, c := range ws.Cols {
		if c.OutlineLevel != 0 {
			grouped = append(grouped, c)
		}
	}
	if len(grouped) != 1 {
		t.Fatalf("%d col elements carry an outlineLevel, want exactly 1: %+v", len(grouped), ws.Cols)
	}
	if grouped[0].OutlineLevel != 2 {
		t.Errorf("col outlineLevel = %d, want 2", grouped[0].OutlineLevel)
	}
	if grouped[0].Min != 4 || grouped[0].Max != 4 {
		t.Errorf("col span is min=%d max=%d, want the single column D (4..4)", grouped[0].Min, grouped[0].Max)
	}
}

func TestOutlineSummaryDirectionDefaultsToBelow(t *testing.T) {
	f := seedcalc.NewFile()
	var below seedcalc.OutlineSummaryBelow
	if err := f.GetSheetPrOptions("Sheet1", &below); err != nil {
		t.Fatalf("GetSheetPrOptions: %v", err)
	}
	if !bool(below) {
		t.Error("OutlineSummaryBelow defaults to false, want true")
	}
}

func TestOutlineSummaryDirectionCanBeReversed(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetSheetPrOptions("Sheet1", seedcalc.OutlineSummaryBelow(false)); err != nil {
		t.Fatalf("SetSheetPrOptions: %v", err)
	}
	var below seedcalc.OutlineSummaryBelow
	if err := f.GetSheetPrOptions("Sheet1", &below); err != nil {
		t.Fatalf("GetSheetPrOptions: %v", err)
	}
	if bool(below) {
		t.Error("OutlineSummaryBelow reads back true after being set false")
	}

	ws := decodeSheet(t, f)
	if ws.SheetPr == nil || ws.SheetPr.OutlinePr == nil || ws.SheetPr.OutlinePr.SummaryBelow == nil {
		t.Fatal("written worksheet carries no sheetPr/outlinePr/@summaryBelow")
	}
	if *ws.SheetPr.OutlinePr.SummaryBelow {
		t.Error("sheetPr/outlinePr/@summaryBelow is true in the written worksheet, want false")
	}
}

func TestSheetPropertyDefaultsAndRoundTrip(t *testing.T) {
	f := seedcalc.NewFile()
	var (
		codeName seedcalc.CodeName
		enable   seedcalc.EnableFormatConditionsCalculation
		pub      seedcalc.Published
		fit      seedcalc.FitToPage
		breaks   seedcalc.AutoPageBreaks
	)
	if err := f.GetSheetPrOptions("Sheet1", &codeName, &enable, &pub, &fit, &breaks); err != nil {
		t.Fatalf("GetSheetPrOptions: %v", err)
	}
	if string(codeName) != "" {
		t.Errorf("CodeName defaults to %q, want the empty string", string(codeName))
	}
	if !bool(enable) {
		t.Error("EnableFormatConditionsCalculation defaults to false, want true")
	}
	if !bool(pub) {
		t.Error("Published defaults to false, want true")
	}
	if bool(fit) {
		t.Error("FitToPage defaults to true, want false")
	}
	if bool(breaks) {
		t.Error("AutoPageBreaks defaults to true, want false")
	}

	if err := f.SetSheetPrOptions("Sheet1",
		seedcalc.CodeName("Budget"),
		seedcalc.EnableFormatConditionsCalculation(false),
		seedcalc.Published(false),
		seedcalc.FitToPage(true),
		seedcalc.AutoPageBreaks(true),
	); err != nil {
		t.Fatalf("SetSheetPrOptions: %v", err)
	}
	if err := f.GetSheetPrOptions("Sheet1", &codeName, &enable, &pub, &fit, &breaks); err != nil {
		t.Fatalf("GetSheetPrOptions after set: %v", err)
	}
	if string(codeName) != "Budget" {
		t.Errorf("CodeName = %q, want %q", string(codeName), "Budget")
	}
	if bool(enable) {
		t.Error("EnableFormatConditionsCalculation reads back true after being set false")
	}
	if bool(pub) {
		t.Error("Published reads back true after being set false")
	}
	if !bool(fit) {
		t.Error("FitToPage reads back false after being set true")
	}
	if !bool(breaks) {
		t.Error("AutoPageBreaks reads back false after being set true")
	}

	// The five options above reach their own markup locations, not just the
	// getter: sheetPr/@codeName, sheetPr/@enableFormatConditionsCalculation,
	// sheetPr/@published and sheetPr/pageSetUpPr/@fitToPage,@autoPageBreaks.
	ws := decodeSheet(t, f)
	if ws.SheetPr == nil {
		t.Fatal("written worksheet carries no sheetPr element")
	}
	if ws.SheetPr.CodeName != "Budget" {
		t.Errorf("sheetPr/@codeName = %q, want %q", ws.SheetPr.CodeName, "Budget")
	}
	if ws.SheetPr.EnableFormatConditionsCalculation == nil || *ws.SheetPr.EnableFormatConditionsCalculation {
		t.Error("sheetPr/@enableFormatConditionsCalculation is missing or true, want false")
	}
	if ws.SheetPr.Published == nil || *ws.SheetPr.Published {
		t.Error("sheetPr/@published is missing or true, want false")
	}
	if ws.SheetPr.PageSetUpPr == nil {
		t.Fatal("written worksheet carries no sheetPr/pageSetUpPr element")
	}
	if !ws.SheetPr.PageSetUpPr.FitToPage {
		t.Error("sheetPr/pageSetUpPr/@fitToPage = false, want true")
	}
	if !ws.SheetPr.PageSetUpPr.AutoPageBreaks {
		t.Error("sheetPr/pageSetUpPr/@autoPageBreaks = false, want true")
	}
}
