// Scored group: sheet-view-options.
//
// Per-view worksheet display options. Graded against ECMA-376 Part 1 clause
// 18.3.1.87 (sheetView, and its showFormulas, showGridLines,
// showRowColHeaders, defaultGridColor, rightToLeft, topLeftCell and zoomScale
// attributes; zoomScale is restricted to 10..400) and clause 18.3.1.88
// (sheetViews). Behaviour cases were checked against the upstream release
// this checkpoint tracks (tag v1.4.1).
package sheetviewoptions

import (
	"archive/zip"
	"bytes"
	"encoding/xml"
	"io"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

type xmlSheetView struct {
	DefaultGridColor  *bool   `xml:"defaultGridColor,attr"`
	RightToLeft       *bool   `xml:"rightToLeft,attr"`
	ShowFormulas      *bool   `xml:"showFormulas,attr"`
	ShowGridLines     *bool   `xml:"showGridLines,attr"`
	ShowRowColHeaders *bool   `xml:"showRowColHeaders,attr"`
	TopLeftCell       string  `xml:"topLeftCell,attr"`
	ZoomScale         float64 `xml:"zoomScale,attr"`
}

type worksheet struct {
	SheetViews struct {
		SheetView []xmlSheetView `xml:"sheetView"`
	} `xml:"sheetViews"`
}

func decodeSheet(t *testing.T, f *seedcalc.File) worksheet {
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
		var raw bytes.Buffer
		if _, err := io.Copy(&raw, rc); err != nil {
			t.Fatalf("read %s: %v", name, err)
		}
		var ws worksheet
		if err := xml.Unmarshal(raw.Bytes(), &ws); err != nil {
			t.Fatalf("%s is not well-formed XML: %v", name, err)
		}
		return ws
	}
	t.Fatalf("written package has no part %s", name)
	return worksheet{}
}

func TestSheetViewOptionDefaults(t *testing.T) {
	f := seedcalc.NewFile()
	var (
		gridColor  seedcalc.DefaultGridColor
		rtl        seedcalc.RightToLeft
		formulas   seedcalc.ShowFormulas
		gridLines  seedcalc.ShowGridLines
		rowColHdrs seedcalc.ShowRowColHeaders
		zoom       seedcalc.ZoomScale
		topLeft    seedcalc.TopLeftCell
	)
	if err := f.GetSheetViewOptions("Sheet1", 0, &gridColor, &rtl, &formulas, &gridLines, &rowColHdrs, &zoom, &topLeft); err != nil {
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
	if !bool(rowColHdrs) {
		t.Error("ShowRowColHeaders defaults to false, want true")
	}
	if float64(zoom) != 0 {
		t.Errorf("ZoomScale defaults to %v, want 0 (no explicit zoom)", float64(zoom))
	}
	if string(topLeft) != "" {
		t.Errorf("TopLeftCell defaults to %q, want the empty string", string(topLeft))
	}
}

func TestSheetViewOptionsRoundTrip(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetSheetViewOptions("Sheet1", 0,
		seedcalc.DefaultGridColor(false),
		seedcalc.RightToLeft(true),
		seedcalc.ShowFormulas(true),
		seedcalc.ShowGridLines(false),
		seedcalc.ShowRowColHeaders(false),
		seedcalc.ZoomScale(75),
		seedcalc.TopLeftCell("B2"),
	); err != nil {
		t.Fatalf("SetSheetViewOptions: %v", err)
	}
	var (
		gridColor  seedcalc.DefaultGridColor
		rtl        seedcalc.RightToLeft
		formulas   seedcalc.ShowFormulas
		gridLines  seedcalc.ShowGridLines
		rowColHdrs seedcalc.ShowRowColHeaders
		zoom       seedcalc.ZoomScale
		topLeft    seedcalc.TopLeftCell
	)
	if err := f.GetSheetViewOptions("Sheet1", 0, &gridColor, &rtl, &formulas, &gridLines, &rowColHdrs, &zoom, &topLeft); err != nil {
		t.Fatalf("GetSheetViewOptions: %v", err)
	}
	if bool(gridColor) {
		t.Error("DefaultGridColor reads back true after being set false")
	}
	if !bool(rtl) {
		t.Error("RightToLeft reads back false after being set true")
	}
	if !bool(formulas) {
		t.Error("ShowFormulas reads back false after being set true")
	}
	if bool(gridLines) {
		t.Error("ShowGridLines reads back true after being set false")
	}
	if bool(rowColHdrs) {
		t.Error("ShowRowColHeaders reads back true after being set false")
	}
	if float64(zoom) != 75 {
		t.Errorf("ZoomScale = %v, want 75", float64(zoom))
	}
	if string(topLeft) != "B2" {
		t.Errorf("TopLeftCell = %q, want %q", string(topLeft), "B2")
	}
}

func TestZoomScaleOutsideTheAcceptedRangeIsIgnored(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetSheetViewOptions("Sheet1", 0, seedcalc.ZoomScale(75)); err != nil {
		t.Fatalf("SetSheetViewOptions: %v", err)
	}
	var zoom seedcalc.ZoomScale
	for _, bad := range []float64{5, 9, 401, 500} {
		if err := f.SetSheetViewOptions("Sheet1", 0, seedcalc.ZoomScale(bad)); err != nil {
			t.Fatalf("SetSheetViewOptions(%v): %v", bad, err)
		}
		if err := f.GetSheetViewOptions("Sheet1", 0, &zoom); err != nil {
			t.Fatalf("GetSheetViewOptions: %v", err)
		}
		if float64(zoom) != 75 {
			t.Errorf("ZoomScale = %v after setting %v, want the previous 75 -- only 10..400 is accepted", float64(zoom), bad)
		}
	}
	for _, ok := range []float64{10, 400} {
		if err := f.SetSheetViewOptions("Sheet1", 0, seedcalc.ZoomScale(ok)); err != nil {
			t.Fatalf("SetSheetViewOptions(%v): %v", ok, err)
		}
		if err := f.GetSheetViewOptions("Sheet1", 0, &zoom); err != nil {
			t.Fatalf("GetSheetViewOptions: %v", err)
		}
		if float64(zoom) != ok {
			t.Errorf("ZoomScale = %v after setting %v, want %v -- the bound is inclusive", float64(zoom), ok, ok)
		}
	}
}

func TestSheetViewIndexOutOfRangeIsAnError(t *testing.T) {
	f := seedcalc.NewFile()
	// A fresh worksheet has exactly one view, so index 0 and index -1 both
	// address it and any other index is out of range.
	if err := f.GetSheetViewOptions("Sheet1", 0); err != nil {
		t.Errorf("GetSheetViewOptions at index 0: %v", err)
	}
	if err := f.GetSheetViewOptions("Sheet1", -1); err != nil {
		t.Errorf("GetSheetViewOptions at index -1: %v", err)
	}
	if err := f.GetSheetViewOptions("Sheet1", 1); err == nil {
		t.Error("GetSheetViewOptions accepted index 1 on a worksheet with one view")
	}
	if err := f.GetSheetViewOptions("Sheet1", -2); err == nil {
		t.Error("GetSheetViewOptions accepted index -2 on a worksheet with one view")
	}
	if err := f.SetSheetViewOptions("Sheet1", 1, seedcalc.ZoomScale(50)); err == nil {
		t.Error("SetSheetViewOptions accepted index 1 on a worksheet with one view")
	}
}

func TestSheetViewOptionsReachTheWrittenWorksheet(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetSheetViewOptions("Sheet1", 0,
		seedcalc.RightToLeft(true),
		seedcalc.ShowGridLines(false),
		seedcalc.ZoomScale(120),
		seedcalc.TopLeftCell("C3"),
	); err != nil {
		t.Fatalf("SetSheetViewOptions: %v", err)
	}
	ws := decodeSheet(t, f)
	if len(ws.SheetViews.SheetView) != 1 {
		t.Fatalf("written worksheet has %d sheetView elements, want 1", len(ws.SheetViews.SheetView))
	}
	view := ws.SheetViews.SheetView[0]
	if view.RightToLeft == nil || !*view.RightToLeft {
		t.Errorf("sheetView/@rightToLeft = %v, want true", view.RightToLeft)
	}
	if view.ShowGridLines == nil || *view.ShowGridLines {
		t.Errorf("sheetView/@showGridLines = %v, want false", view.ShowGridLines)
	}
	if view.ZoomScale != 120 {
		t.Errorf("sheetView/@zoomScale = %v, want 120", view.ZoomScale)
	}
	if view.TopLeftCell != "C3" {
		t.Errorf("sheetView/@topLeftCell = %q, want %q", view.TopLeftCell, "C3")
	}
}
