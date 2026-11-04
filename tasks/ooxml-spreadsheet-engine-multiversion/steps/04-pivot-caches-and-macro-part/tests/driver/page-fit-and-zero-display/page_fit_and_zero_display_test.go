// Scored group: page-fit-and-zero-display (milestone 04, "Pivot caches,
// sparklines and the macro part").
//
// Normative sources:
//   - TEST_INTERFACE.md section 4.04 -- "Fitting a sheet to a page count",
//     "Suppressing zero values", "Struck-through text".
//   - ECMA-376 Part 1, 18.3.1.63 pageSetup (Page Setup Settings) -- fitToWidth
//     is the number of horizontal pages to fit on and fitToHeight the number
//     of vertical pages, both unsignedInt.
//   - ECMA-376 Part 1, 18.3.1.87 sheetView (Worksheet View) -- showZeros
//     flags whether the window shows 0 in cells holding a zero value; when
//     false such cells appear blank.
//   - ECMA-376 Part 1, 18.4.10 strike (Strike Through) -- the element draws a
//     line through the middle of the text and its val attribute defaults to
//     true when omitted; 18.8.10 cellXfs and 18.8.23 fonts for the indexed
//     records the cell format points at.
//   - Cases cross-checked against the upstream test corpus at tag v2.0.2
//     (sheet_test.go, sheetview_test.go, styles_test.go).
package pagefitandzerodisplay

import (
	"archive/zip"
	"bytes"
	"encoding/xml"
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

type worksheetDoc struct {
	SheetViews struct {
		SheetView []struct {
			ShowZeros string `xml:"showZeros,attr"`
		} `xml:"sheetView"`
	} `xml:"sheetViews"`
	PageSetup struct {
		FitToWidth  string `xml:"fitToWidth,attr"`
		FitToHeight string `xml:"fitToHeight,attr"`
	} `xml:"pageSetup"`
}

func worksheet(t *testing.T, f *seedcalc.File) worksheetDoc {
	t.Helper()
	raw, ok := savedParts(t, f)["xl/worksheets/sheet1.xml"]
	if !ok {
		t.Fatal("the package has no xl/worksheets/sheet1.xml part")
	}
	var ws worksheetDoc
	if err := xml.Unmarshal([]byte(raw), &ws); err != nil {
		t.Fatalf("the worksheet part is not well-formed: %v", err)
	}
	return ws
}

type boolProperty struct{}

type styleSheet struct {
	Fonts struct {
		Font []struct {
			Strike *boolProperty `xml:"strike"`
			Bold   *boolProperty `xml:"b"`
		} `xml:"font"`
	} `xml:"fonts"`
	CellXfs struct {
		Xf []struct {
			FontID int `xml:"fontId,attr"`
		} `xml:"xf"`
	} `xml:"cellXfs"`
}

func styles(t *testing.T, f *seedcalc.File) styleSheet {
	t.Helper()
	raw, ok := savedParts(t, f)["xl/styles.xml"]
	if !ok {
		t.Fatal("the package has no xl/styles.xml part")
	}
	var s styleSheet
	if err := xml.Unmarshal([]byte(raw), &s); err != nil {
		t.Fatalf("the style part is not well-formed: %v", err)
	}
	return s
}

func TestFitToPageDefaultsToOnePageEachWay(t *testing.T) {
	f := seedcalc.NewFile()
	var width seedcalc.FitToWidth
	var height seedcalc.FitToHeight
	if err := f.GetPageLayout("Sheet1", &width, &height); err != nil {
		t.Fatalf("GetPageLayout: %v", err)
	}
	if int(width) != 1 {
		t.Errorf("default FitToWidth = %d, want 1", int(width))
	}
	if int(height) != 1 {
		t.Errorf("default FitToHeight = %d, want 1", int(height))
	}
}

func TestFitToWidthAndHeightAreWrittenToPageSetup(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetPageLayout("Sheet1", seedcalc.FitToWidth(2), seedcalc.FitToHeight(3)); err != nil {
		t.Fatalf("SetPageLayout: %v", err)
	}
	ws := worksheet(t, f)
	if ws.PageSetup.FitToWidth != "2" {
		t.Errorf("pageSetup fitToWidth = %q, want 2", ws.PageSetup.FitToWidth)
	}
	if ws.PageSetup.FitToHeight != "3" {
		t.Errorf("pageSetup fitToHeight = %q, want 3", ws.PageSetup.FitToHeight)
	}
}

func TestFitToPageValuesReadBack(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetPageLayout("Sheet1", seedcalc.FitToWidth(4), seedcalc.FitToHeight(5)); err != nil {
		t.Fatalf("SetPageLayout: %v", err)
	}
	var width seedcalc.FitToWidth
	var height seedcalc.FitToHeight
	if err := f.GetPageLayout("Sheet1", &width, &height); err != nil {
		t.Fatalf("GetPageLayout: %v", err)
	}
	if int(width) != 4 || int(height) != 5 {
		t.Errorf("read back FitToWidth=%d FitToHeight=%d, want 4 and 5", int(width), int(height))
	}
}

func TestNonPositivePageCountsAreIgnored(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetPageLayout("Sheet1", seedcalc.FitToWidth(2), seedcalc.FitToHeight(3)); err != nil {
		t.Fatalf("SetPageLayout: %v", err)
	}
	if err := f.SetPageLayout("Sheet1", seedcalc.FitToWidth(0), seedcalc.FitToHeight(-1)); err != nil {
		t.Fatalf("SetPageLayout with non-positive counts: %v", err)
	}
	var width seedcalc.FitToWidth
	var height seedcalc.FitToHeight
	if err := f.GetPageLayout("Sheet1", &width, &height); err != nil {
		t.Fatalf("GetPageLayout: %v", err)
	}
	if int(width) != 2 || int(height) != 3 {
		t.Errorf("after non-positive counts FitToWidth=%d FitToHeight=%d, want the previous 2 and 3", int(width), int(height))
	}
}

func TestZeroValuesAreShownByDefault(t *testing.T) {
	f := seedcalc.NewFile()
	var zeros seedcalc.ShowZeros
	if err := f.GetSheetViewOptions("Sheet1", 0, &zeros); err != nil {
		t.Fatalf("GetSheetViewOptions: %v", err)
	}
	if !bool(zeros) {
		t.Error("ShowZeros defaults to false, want true")
	}
	if got := worksheet(t, f).SheetViews.SheetView[0].ShowZeros; got != "" {
		t.Errorf("a default worksheet writes showZeros=%q, want the attribute omitted", got)
	}
}

func TestZeroValuesCanBeSuppressed(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetSheetViewOptions("Sheet1", 0, seedcalc.ShowZeros(false)); err != nil {
		t.Fatalf("SetSheetViewOptions: %v", err)
	}
	if got := worksheet(t, f).SheetViews.SheetView[0].ShowZeros; got != "false" && got != "0" {
		t.Errorf("sheetView showZeros = %q, want false", got)
	}
	var zeros seedcalc.ShowZeros
	if err := f.GetSheetViewOptions("Sheet1", 0, &zeros); err != nil {
		t.Fatalf("GetSheetViewOptions: %v", err)
	}
	if bool(zeros) {
		t.Error("ShowZeros read back as true after being set to false")
	}
	if err := f.SetSheetViewOptions("Sheet1", 0, seedcalc.ShowZeros(true)); err != nil {
		t.Fatalf("SetSheetViewOptions: %v", err)
	}
	if err := f.GetSheetViewOptions("Sheet1", 0, &zeros); err != nil {
		t.Fatalf("GetSheetViewOptions: %v", err)
	}
	if !bool(zeros) {
		t.Error("ShowZeros did not go back to true")
	}
}

func TestStrikeStyleReachesTheFontRecord(t *testing.T) {
	f := seedcalc.NewFile()
	style, err := f.NewStyle(`{"font":{"strike":true,"bold":true}}`)
	if err != nil {
		t.Fatalf("NewStyle: %v", err)
	}
	if err := f.SetCellStyle("Sheet1", "A1", "A1", style); err != nil {
		t.Fatalf("SetCellStyle: %v", err)
	}
	s := styles(t, f)
	if style < 0 || style >= len(s.CellXfs.Xf) {
		t.Fatalf("NewStyle returned index %d but the sheet holds %d cell formats", style, len(s.CellXfs.Xf))
	}
	fontID := s.CellXfs.Xf[style].FontID
	if fontID < 0 || fontID >= len(s.Fonts.Font) {
		t.Fatalf("cell format %d points at font %d but the sheet holds %d fonts", style, fontID, len(s.Fonts.Font))
	}
	font := s.Fonts.Font[fontID]
	if font.Strike == nil {
		t.Error("the font record the style points at has no strike element")
	}
	if font.Bold == nil {
		t.Error("the font record the style points at lost its bold element")
	}
	got, err := f.GetCellStyle("Sheet1", "A1")
	if err != nil {
		t.Fatalf("GetCellStyle: %v", err)
	}
	if got != style {
		t.Errorf("GetCellStyle = %d, want %d", got, style)
	}
}

func TestFontsWithoutStrikeDoNotGetTheElement(t *testing.T) {
	f := seedcalc.NewFile()
	style, err := f.NewStyle(`{"font":{"bold":true}}`)
	if err != nil {
		t.Fatalf("NewStyle: %v", err)
	}
	s := styles(t, f)
	fontID := s.CellXfs.Xf[style].FontID
	if s.Fonts.Font[fontID].Strike != nil {
		t.Error("a font that did not ask for strike was written with a strike element")
	}
	plain, err := f.NewStyle(`{"font":{"strike":false}}`)
	if err != nil {
		t.Fatalf("NewStyle: %v", err)
	}
	s = styles(t, f)
	if s.Fonts.Font[s.CellXfs.Xf[plain].FontID].Strike != nil {
		t.Error("an explicit strike:false was written as a strike element")
	}
}
