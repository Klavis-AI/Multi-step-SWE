// Gate group: sheet-views-and-page-layout (milestone 04 regression).
//
// Milestone 04 reaches into both of these surfaces: it adds ShowZeros to the
// sheet-view option set and FitToWidth/FitToHeight to the page-layout option
// set, and both are written through the same option-pair machinery earlier
// milestones established. Widening an option family is the classic way to
// break the options already in it -- a default that stops being applied, a
// view index that stops counting backward, a pageSetup element that is
// replaced rather than amended. This group holds that inherited surface still.
//
// Normative sources:
//   - TEST_INTERFACE.md 4.02.5 (worksheet properties and their defaults),
//     4.02.8 (sheet view options, the view index and the ZoomScale interval),
//     4.03.8 (page layout orientation and paper size) and 4.03.9 (headers and
//     footers, including the 255-character limit and removal by nil).
//   - ECMA-376 Part 1, 18.3.1.87 sheetView (Worksheet View) -- a single sheet
//     view definition, carrying showFormulas, showGridLines, showRowColHeaders,
//     defaultGridColor, rightToLeft, topLeftCell and zoomScale.
//   - ECMA-376 Part 1, 18.3.1.88 sheetViews (Sheet Views) -- the collection of
//     sheet views for the worksheet.
//   - ECMA-376 Part 1, 18.3.1.82 sheetPr (Sheet Properties) and 18.3.1.61
//     outlinePr (Outline Properties) -- the worksheet-level property element
//     and the outline summary direction inside it.
//   - ECMA-376 Part 1, 18.3.1.63 pageSetup (Page Setup Settings) -- @orientation
//     and @paperSize.
//   - ECMA-376 Part 1, 18.3.1.46 headerFooter (Header Footer Settings) -- the
//     attributes alignWithMargins, differentFirst, differentOddEven and
//     scaleWithDoc and the oddHeader/oddFooter/evenHeader/evenFooter/
//     firstHeader/firstFooter children.
//   - Cases cross-checked against the upstream test corpus at tag v2.0.2
//     (sheetview_test.go, sheetpr_test.go, sheet_test.go).
package sheetviewsandpagelayout

import (
	"archive/zip"
	"bytes"
	"encoding/xml"
	"io/ioutil"
	"strings"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// savedParts writes the workbook through the documented io.Writer path and
// returns every item of the resulting package.
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

type worksheetDoc struct {
	SheetPr *struct {
		CodeName  string `xml:"codeName,attr"`
		Published string `xml:"published,attr"`
		OutlinePr *struct {
			SummaryBelow string `xml:"summaryBelow,attr"`
		} `xml:"outlinePr"`
	} `xml:"sheetPr"`
	SheetViews struct {
		SheetView []struct {
			TopLeftCell  string `xml:"topLeftCell,attr"`
			ZoomScale    string `xml:"zoomScale,attr"`
			RightToLeft  string `xml:"rightToLeft,attr"`
			ShowFormulas string `xml:"showFormulas,attr"`
		} `xml:"sheetView"`
	} `xml:"sheetViews"`
	PageSetup *struct {
		Orientation string `xml:"orientation,attr"`
		PaperSize   string `xml:"paperSize,attr"`
	} `xml:"pageSetup"`
	HeaderFooter *struct {
		AlignWithMargins string `xml:"alignWithMargins,attr"`
		DifferentFirst   string `xml:"differentFirst,attr"`
		DifferentOddEven string `xml:"differentOddEven,attr"`
		ScaleWithDoc     string `xml:"scaleWithDoc,attr"`
		OddHeader        string `xml:"oddHeader"`
		OddFooter        string `xml:"oddFooter"`
		EvenHeader       string `xml:"evenHeader"`
		EvenFooter       string `xml:"evenFooter"`
		FirstHeader      string `xml:"firstHeader"`
		FirstFooter      string `xml:"firstFooter"`
	} `xml:"headerFooter"`
}

// worksheetPart parses the numbered worksheet part of the saved package.
func worksheetPart(t *testing.T, f *seedcalc.File, n string) worksheetDoc {
	t.Helper()
	name := "xl/worksheets/sheet" + n + ".xml"
	raw, ok := savedParts(t, f)[name]
	if !ok {
		t.Fatalf("the package has no %s part", name)
	}
	var ws worksheetDoc
	if err := xml.Unmarshal([]byte(raw), &ws); err != nil {
		t.Fatalf("the worksheet part is not well-formed: %v", err)
	}
	return ws
}

func TestSheetViewOptionsRoundTrip(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetSheetViewOptions("Sheet1", 0,
		seedcalc.DefaultGridColor(false),
		seedcalc.RightToLeft(true),
		seedcalc.ShowFormulas(true),
		seedcalc.ShowGridLines(false),
		seedcalc.ShowRowColHeaders(false),
		seedcalc.TopLeftCell("B2"),
		seedcalc.ZoomScale(80),
	); err != nil {
		t.Fatalf("SetSheetViewOptions: %v", err)
	}
	var (
		gridColor seedcalc.DefaultGridColor
		rtl       seedcalc.RightToLeft
		formulas  seedcalc.ShowFormulas
		gridLines seedcalc.ShowGridLines
		headers   seedcalc.ShowRowColHeaders
		topLeft   seedcalc.TopLeftCell
		zoom      seedcalc.ZoomScale
	)
	if err := f.GetSheetViewOptions("Sheet1", 0,
		&gridColor, &rtl, &formulas, &gridLines, &headers, &topLeft, &zoom); err != nil {
		t.Fatalf("GetSheetViewOptions: %v", err)
	}
	if bool(gridColor) {
		t.Error("DefaultGridColor read back true after being set to false")
	}
	if !bool(rtl) {
		t.Error("RightToLeft read back false after being set to true")
	}
	if !bool(formulas) {
		t.Error("ShowFormulas read back false after being set to true")
	}
	if bool(gridLines) {
		t.Error("ShowGridLines read back true after being set to false")
	}
	if bool(headers) {
		t.Error("ShowRowColHeaders read back true after being set to false")
	}
	if string(topLeft) != "B2" {
		t.Errorf("TopLeftCell = %q, want B2", string(topLeft))
	}
	if float64(zoom) != 80 {
		t.Errorf("ZoomScale = %v, want 80", float64(zoom))
	}
	ws := worksheetPart(t, f, "1")
	if len(ws.SheetViews.SheetView) != 1 {
		t.Fatalf("the saved worksheet holds %d sheetView elements, want 1", len(ws.SheetViews.SheetView))
	}
	view := ws.SheetViews.SheetView[0]
	if view.TopLeftCell != "B2" {
		t.Errorf("sheetView topLeftCell = %q, want B2", view.TopLeftCell)
	}
	if view.ZoomScale != "80" {
		t.Errorf("sheetView zoomScale = %q, want 80", view.ZoomScale)
	}
	if view.RightToLeft != "true" && view.RightToLeft != "1" {
		t.Errorf("sheetView rightToLeft = %q, want true", view.RightToLeft)
	}
	if view.ShowFormulas != "true" && view.ShowFormulas != "1" {
		t.Errorf("sheetView showFormulas = %q, want true", view.ShowFormulas)
	}
}

func TestSheetViewDefaultsOnAFreshWorksheet(t *testing.T) {
	f := seedcalc.NewFile()
	var (
		gridColor seedcalc.DefaultGridColor
		rtl       seedcalc.RightToLeft
		formulas  seedcalc.ShowFormulas
		gridLines seedcalc.ShowGridLines
		headers   seedcalc.ShowRowColHeaders
		topLeft   seedcalc.TopLeftCell
		zoom      seedcalc.ZoomScale
	)
	if err := f.GetSheetViewOptions("Sheet1", 0,
		&gridColor, &rtl, &formulas, &gridLines, &headers, &topLeft, &zoom); err != nil {
		t.Fatalf("GetSheetViewOptions: %v", err)
	}
	if !bool(gridColor) {
		t.Error("DefaultGridColor defaults to false, want true")
	}
	if bool(rtl) {
		t.Error("RightToLeft defaults to true, want false")
	}
	if bool(formulas) {
		t.Error("ShowFormulas defaults to true, want false")
	}
	if !bool(gridLines) {
		t.Error("ShowGridLines defaults to false, want true")
	}
	if !bool(headers) {
		t.Error("ShowRowColHeaders defaults to false, want true")
	}
	if string(topLeft) != "" {
		t.Errorf("TopLeftCell defaults to %q, want the empty string", string(topLeft))
	}
	if float64(zoom) != 0 {
		t.Errorf("ZoomScale defaults to %v, want 0", float64(zoom))
	}
}

func TestViewIndexCountsBackwardAndIsBounded(t *testing.T) {
	f := seedcalc.NewFile()
	var topLeft seedcalc.TopLeftCell
	if err := f.SetSheetViewOptions("Sheet1", -1, seedcalc.TopLeftCell("C3")); err != nil {
		t.Fatalf("SetSheetViewOptions with index -1: %v", err)
	}
	if err := f.GetSheetViewOptions("Sheet1", 0, &topLeft); err != nil {
		t.Fatalf("GetSheetViewOptions with index 0: %v", err)
	}
	if string(topLeft) != "C3" {
		t.Errorf("index -1 and index 0 addressed different views: TopLeftCell = %q, want C3", string(topLeft))
	}
	for _, idx := range []int{1, -2, 7, -9} {
		if err := f.SetSheetViewOptions("Sheet1", idx, seedcalc.TopLeftCell("D4")); err == nil {
			t.Errorf("SetSheetViewOptions accepted out-of-range view index %d", idx)
		}
		if err := f.GetSheetViewOptions("Sheet1", idx, &topLeft); err == nil {
			t.Errorf("GetSheetViewOptions accepted out-of-range view index %d", idx)
		}
	}
	if err := f.SetSheetViewOptions("NoSuchSheet", 0, seedcalc.TopLeftCell("A1")); err == nil {
		t.Error("SetSheetViewOptions accepted an unknown worksheet")
	}
	if err := f.GetSheetViewOptions("NoSuchSheet", 0, &topLeft); err == nil {
		t.Error("GetSheetViewOptions accepted an unknown worksheet")
	}
	if err := f.GetSheetViewOptions("Sheet1", 0); err != nil {
		t.Errorf("GetSheetViewOptions with no options: %v", err)
	}
	if err := f.SetSheetViewOptions("Sheet1", 0); err != nil {
		t.Errorf("SetSheetViewOptions with no options: %v", err)
	}
}

func TestZoomScaleOutsideItsIntervalIsIgnored(t *testing.T) {
	f := seedcalc.NewFile()
	var zoom seedcalc.ZoomScale
	for _, accepted := range []float64{10, 400, 123} {
		if err := f.SetSheetViewOptions("Sheet1", 0, seedcalc.ZoomScale(accepted)); err != nil {
			t.Fatalf("SetSheetViewOptions with zoom %v: %v", accepted, err)
		}
		if err := f.GetSheetViewOptions("Sheet1", 0, &zoom); err != nil {
			t.Fatalf("GetSheetViewOptions: %v", err)
		}
		if float64(zoom) != accepted {
			t.Errorf("ZoomScale = %v, want the accepted %v", float64(zoom), accepted)
		}
	}
	for _, rejected := range []float64{9, 401, 0, -5} {
		if err := f.SetSheetViewOptions("Sheet1", 0, seedcalc.ZoomScale(rejected)); err != nil {
			t.Fatalf("SetSheetViewOptions with out-of-range zoom %v reported an error: %v", rejected, err)
		}
		if err := f.GetSheetViewOptions("Sheet1", 0, &zoom); err != nil {
			t.Fatalf("GetSheetViewOptions: %v", err)
		}
		if float64(zoom) != 123 {
			t.Errorf("zoom %v was applied: ZoomScale = %v, want the retained 123", rejected, float64(zoom))
		}
	}
}

func TestSheetPropertyOptionsRoundTripAndDefaults(t *testing.T) {
	f := seedcalc.NewFile()
	var (
		codeName   seedcalc.CodeName
		conditions seedcalc.EnableFormatConditionsCalculation
		published  seedcalc.Published
		fitToPage  seedcalc.FitToPage
		autoBreaks seedcalc.AutoPageBreaks
		summary    seedcalc.OutlineSummaryBelow
	)
	if err := f.GetSheetPrOptions("Sheet1",
		&codeName, &conditions, &published, &fitToPage, &autoBreaks, &summary); err != nil {
		t.Fatalf("GetSheetPrOptions: %v", err)
	}
	if string(codeName) != "" {
		t.Errorf("CodeName defaults to %q, want the empty string", string(codeName))
	}
	if !bool(conditions) {
		t.Error("EnableFormatConditionsCalculation defaults to false, want true")
	}
	if !bool(published) {
		t.Error("Published defaults to false, want true")
	}
	if bool(fitToPage) {
		t.Error("FitToPage defaults to true, want false")
	}
	if bool(autoBreaks) {
		t.Error("AutoPageBreaks defaults to true, want false")
	}
	if !bool(summary) {
		t.Error("OutlineSummaryBelow defaults to false, want true")
	}

	if err := f.SetSheetPrOptions("Sheet1",
		seedcalc.CodeName("Sheet_One"),
		seedcalc.EnableFormatConditionsCalculation(false),
		seedcalc.Published(false),
		seedcalc.FitToPage(true),
		seedcalc.AutoPageBreaks(true),
		seedcalc.OutlineSummaryBelow(false),
	); err != nil {
		t.Fatalf("SetSheetPrOptions: %v", err)
	}
	if err := f.GetSheetPrOptions("Sheet1",
		&codeName, &conditions, &published, &fitToPage, &autoBreaks, &summary); err != nil {
		t.Fatalf("GetSheetPrOptions: %v", err)
	}
	if string(codeName) != "Sheet_One" {
		t.Errorf("CodeName = %q, want Sheet_One", string(codeName))
	}
	if bool(conditions) || bool(published) || bool(summary) {
		t.Errorf("the three true-by-default flags did not go false: conditions=%v published=%v summaryBelow=%v",
			bool(conditions), bool(published), bool(summary))
	}
	if !bool(fitToPage) || !bool(autoBreaks) {
		t.Errorf("the two false-by-default flags did not go true: fitToPage=%v autoPageBreaks=%v",
			bool(fitToPage), bool(autoBreaks))
	}

	ws := worksheetPart(t, f, "1")
	if ws.SheetPr == nil {
		t.Fatal("the saved worksheet has no sheetPr element")
	}
	if ws.SheetPr.CodeName != "Sheet_One" {
		t.Errorf("sheetPr codeName = %q, want Sheet_One", ws.SheetPr.CodeName)
	}
	if ws.SheetPr.OutlinePr == nil {
		t.Fatal("the saved sheetPr has no outlinePr element")
	}
	if s := ws.SheetPr.OutlinePr.SummaryBelow; s != "false" && s != "0" {
		t.Errorf("outlinePr summaryBelow = %q, want false", s)
	}
	if err := f.SetSheetPrOptions("NoSuchSheet", seedcalc.Published(false)); err == nil {
		t.Error("SetSheetPrOptions accepted an unknown worksheet")
	}
	if err := f.GetSheetPrOptions("NoSuchSheet", &published); err == nil {
		t.Error("GetSheetPrOptions accepted an unknown worksheet")
	}
}

func TestPageLayoutOrientationAndPaperSizeSurviveTheSave(t *testing.T) {
	f := seedcalc.NewFile()
	var (
		orientation seedcalc.PageLayoutOrientation
		paper       seedcalc.PageLayoutPaperSize
	)
	if err := f.GetPageLayout("Sheet1", &orientation, &paper); err != nil {
		t.Fatalf("GetPageLayout on an untouched worksheet: %v", err)
	}
	if string(orientation) != seedcalc.OrientationPortrait {
		t.Errorf("default orientation = %q, want %q", string(orientation), seedcalc.OrientationPortrait)
	}
	if int(paper) != 1 {
		t.Errorf("default paper size = %d, want 1", int(paper))
	}

	f.NewSheet("Sheet2")
	if err := f.SetPageLayout("Sheet2",
		seedcalc.PageLayoutOrientation(seedcalc.OrientationLandscape),
		seedcalc.PageLayoutPaperSize(9),
	); err != nil {
		t.Fatalf("SetPageLayout: %v", err)
	}
	if err := f.GetPageLayout("Sheet2", &orientation, &paper); err != nil {
		t.Fatalf("GetPageLayout: %v", err)
	}
	if string(orientation) != seedcalc.OrientationLandscape || int(paper) != 9 {
		t.Errorf("read back orientation=%q paperSize=%d, want %q and 9",
			string(orientation), seedcalc.OrientationLandscape, int(paper))
	}
	if err := f.GetPageLayout("Sheet1", &orientation, &paper); err != nil {
		t.Fatalf("GetPageLayout on Sheet1: %v", err)
	}
	if string(orientation) != seedcalc.OrientationPortrait || int(paper) != 1 {
		t.Errorf("setting the layout of Sheet2 changed Sheet1: orientation=%q paperSize=%d",
			string(orientation), int(paper))
	}

	ws := worksheetPart(t, f, "2")
	if ws.PageSetup == nil {
		t.Fatal("the saved worksheet has no pageSetup element")
	}
	if ws.PageSetup.Orientation != seedcalc.OrientationLandscape {
		t.Errorf("pageSetup orientation = %q, want landscape", ws.PageSetup.Orientation)
	}
	if ws.PageSetup.PaperSize != "9" {
		t.Errorf("pageSetup paperSize = %q, want 9", ws.PageSetup.PaperSize)
	}
	if err := f.SetPageLayout("NoSuchSheet", seedcalc.PageLayoutPaperSize(9)); err == nil {
		t.Error("SetPageLayout accepted an unknown worksheet")
	}
	if err := f.GetPageLayout("NoSuchSheet", &paper); err == nil {
		t.Error("GetPageLayout accepted an unknown worksheet")
	}
}

func TestHeaderFooterIsWrittenRemovedAndBounded(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetHeaderFooter("Sheet1", &seedcalc.FormatHeaderFooter{
		AlignWithMargins: true,
		DifferentFirst:   true,
		DifferentOddEven: true,
		ScaleWithDoc:     true,
		OddHeader:        "&L&P",
		OddFooter:        "&C&F",
		EvenHeader:       "&L&P",
		EvenFooter:       "&L&D&R&T",
		FirstHeader:      `&CCenter Bold Header&"-,Regular"`,
		FirstFooter:      "&LFirst",
	}); err != nil {
		t.Fatalf("SetHeaderFooter: %v", err)
	}
	ws := worksheetPart(t, f, "1")
	if ws.HeaderFooter == nil {
		t.Fatal("the saved worksheet has no headerFooter element")
	}
	hf := ws.HeaderFooter
	for _, attr := range []struct{ name, got string }{
		{"alignWithMargins", hf.AlignWithMargins},
		{"differentFirst", hf.DifferentFirst},
		{"differentOddEven", hf.DifferentOddEven},
		{"scaleWithDoc", hf.ScaleWithDoc},
	} {
		if attr.got != "true" && attr.got != "1" {
			t.Errorf("headerFooter %s = %q, want true", attr.name, attr.got)
		}
	}
	for _, child := range []struct{ name, got, want string }{
		{"oddHeader", hf.OddHeader, "&L&P"},
		{"oddFooter", hf.OddFooter, "&C&F"},
		{"evenHeader", hf.EvenHeader, "&L&P"},
		{"evenFooter", hf.EvenFooter, "&L&D&R&T"},
		{"firstFooter", hf.FirstFooter, "&LFirst"},
	} {
		if child.got != child.want {
			t.Errorf("headerFooter %s = %q, want %q", child.name, child.got, child.want)
		}
	}
	if !strings.Contains(hf.FirstHeader, "Center Bold Header") {
		t.Errorf("headerFooter firstHeader = %q, want it to carry the caller's text", hf.FirstHeader)
	}

	if err := f.SetHeaderFooter("Sheet1", nil); err != nil {
		t.Fatalf("SetHeaderFooter(nil): %v", err)
	}
	if ws := worksheetPart(t, f, "1"); ws.HeaderFooter != nil {
		t.Error("SetHeaderFooter(nil) left a headerFooter element behind")
	}

	if err := f.SetHeaderFooter("Sheet1", &seedcalc.FormatHeaderFooter{
		OddHeader: strings.Repeat("A", 255),
	}); err == nil {
		t.Error("SetHeaderFooter accepted a 255-character OddHeader")
	}
	if ws := worksheetPart(t, f, "1"); ws.HeaderFooter != nil {
		t.Error("a rejected header was written to the worksheet anyway")
	}
	if err := f.SetHeaderFooter("Sheet1", &seedcalc.FormatHeaderFooter{
		OddHeader: strings.Repeat("A", 254),
	}); err != nil {
		t.Errorf("SetHeaderFooter rejected a 254-character OddHeader: %v", err)
	}
	if err := f.SetHeaderFooter("NoSuchSheet", &seedcalc.FormatHeaderFooter{}); err == nil {
		t.Error("SetHeaderFooter accepted an unknown worksheet")
	}
}
