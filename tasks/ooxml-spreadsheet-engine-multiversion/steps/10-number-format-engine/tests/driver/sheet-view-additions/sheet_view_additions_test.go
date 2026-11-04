// Scored group: sheet-view-additions.
//
// Two options join the sheet-view interface: the ruler toggle, and the view
// mode that chooses between the ordinary grid, the page-layout preview and
// the page-break preview.
//
// The attributes are ECMA-376 Part 1 section 18.3.1.87 (sheetView):
// showRuler and view.
package sheetviewadditions

import (
	"archive/zip"
	"bytes"
	"io"
	"strings"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// element returns the first occurrence of the named start tag in a part of
// the written package, or the empty string when the tag is absent.
func element(t *testing.T, f *seedcalc.File, part, tag string) string {
	t.Helper()
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	zr, err := zip.NewReader(bytes.NewReader(buf.Bytes()), int64(buf.Len()))
	if err != nil {
		t.Fatalf("zip.NewReader: %v", err)
	}
	for _, zf := range zr.File {
		if zf.Name != part {
			continue
		}
		rc, err := zf.Open()
		if err != nil {
			t.Fatalf("open %s: %v", part, err)
		}
		b, err := io.ReadAll(rc)
		rc.Close()
		if err != nil {
			t.Fatalf("read %s: %v", part, err)
		}
		for _, chunk := range strings.Split(strings.ReplaceAll(string(b), ">", ">\n"), "\n") {
			if strings.HasPrefix(chunk, tag) {
				return chunk
			}
		}
		return ""
	}
	t.Fatalf("%s is missing from the package", part)
	return ""
}

func sheetView(t *testing.T, f *seedcalc.File) string {
	t.Helper()
	return element(t, f, "xl/worksheets/sheet1.xml", "<sheetView ")
}

func TestTheRulerDefaultsToShownAndCanBeHidden(t *testing.T) {
	f := seedcalc.NewFile()
	var ruler seedcalc.ShowRuler
	if err := f.GetSheetViewOptions("Sheet1", 0, &ruler); err != nil {
		t.Fatalf("GetSheetViewOptions: %v", err)
	}
	if !bool(ruler) {
		t.Fatalf("ShowRuler defaults to %v, want true", bool(ruler))
	}
	if err := f.SetSheetViewOptions("Sheet1", 0, seedcalc.ShowRuler(false)); err != nil {
		t.Fatalf("SetSheetViewOptions: %v", err)
	}
	if err := f.GetSheetViewOptions("Sheet1", 0, &ruler); err != nil {
		t.Fatalf("GetSheetViewOptions: %v", err)
	}
	if bool(ruler) {
		t.Fatalf("ShowRuler = %v, want false", bool(ruler))
	}
	if el := sheetView(t, f); !strings.Contains(el, `showRuler="false"`) {
		t.Fatalf("sheetView = %q, want showRuler=\"false\"", el)
	}
}

func TestTheViewModeDefaultsToNormal(t *testing.T) {
	f := seedcalc.NewFile()
	var view seedcalc.View
	if err := f.GetSheetViewOptions("Sheet1", 0, &view); err != nil {
		t.Fatalf("GetSheetViewOptions: %v", err)
	}
	if string(view) != "normal" {
		t.Fatalf("View defaults to %q, want %q", string(view), "normal")
	}
	// Which is the schema default, so nothing is written for it.
	if el := sheetView(t, f); strings.Contains(el, "view=") {
		t.Fatalf("sheetView = %q, want no view attribute", el)
	}
}

func TestEachNamedViewModeIsWritten(t *testing.T) {
	for _, mode := range []string{"pageLayout", "pageBreakPreview", "normal"} {
		f := seedcalc.NewFile()
		if err := f.SetSheetViewOptions("Sheet1", 0, seedcalc.View(mode)); err != nil {
			t.Fatalf("SetSheetViewOptions(%s): %v", mode, err)
		}
		var view seedcalc.View
		if err := f.GetSheetViewOptions("Sheet1", 0, &view); err != nil {
			t.Fatalf("GetSheetViewOptions(%s): %v", mode, err)
		}
		if string(view) != mode {
			t.Fatalf("View = %q, want %q", string(view), mode)
		}
		if el := sheetView(t, f); !strings.Contains(el, `view="`+mode+`"`) {
			t.Fatalf("sheetView = %q, want view=%q", el, mode)
		}
	}
}

func TestTheViewModeIsNotValidated(t *testing.T) {
	// The option is a plain string: an unrecognised mode is stored and
	// written as given rather than refused.
	f := seedcalc.NewFile()
	if err := f.SetSheetViewOptions("Sheet1", 0, seedcalc.View("nope")); err != nil {
		t.Fatalf("SetSheetViewOptions(nope): %v", err)
	}
	var view seedcalc.View
	if err := f.GetSheetViewOptions("Sheet1", 0, &view); err != nil {
		t.Fatalf("GetSheetViewOptions: %v", err)
	}
	if string(view) != "nope" {
		t.Fatalf("View = %q, want %q", string(view), "nope")
	}
	if el := sheetView(t, f); !strings.Contains(el, `view="nope"`) {
		t.Fatalf("sheetView = %q, want view=\"nope\"", el)
	}
}

func TestTheTwoNewOptionsCombineWithTheInheritedOnes(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetSheetViewOptions("Sheet1", 0,
		seedcalc.ShowRuler(false),
		seedcalc.View("pageLayout"),
		seedcalc.ShowGridLines(false),
		seedcalc.ZoomScale(120),
	); err != nil {
		t.Fatalf("SetSheetViewOptions: %v", err)
	}
	var ruler seedcalc.ShowRuler
	var view seedcalc.View
	var grid seedcalc.ShowGridLines
	var zoom seedcalc.ZoomScale
	if err := f.GetSheetViewOptions("Sheet1", 0, &ruler, &view, &grid, &zoom); err != nil {
		t.Fatalf("GetSheetViewOptions: %v", err)
	}
	if bool(ruler) || string(view) != "pageLayout" || bool(grid) || float64(zoom) != 120 {
		t.Fatalf("got ruler=%v view=%q grid=%v zoom=%v", bool(ruler), string(view), bool(grid), float64(zoom))
	}
}

func TestTheNewOptionsReportTheSameFailuresAsTheOldOnes(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetSheetViewOptions("Nope", 0, seedcalc.ShowRuler(false)); err == nil || err.Error() != "sheet Nope is not exist" {
		t.Fatalf("SetSheetViewOptions(missing sheet) = %v, want sheet Nope is not exist", err)
	}
	if err := f.SetSheetViewOptions("Sheet1", 5, seedcalc.View("normal")); err == nil || err.Error() != "view index 5 out of range" {
		t.Fatalf("SetSheetViewOptions(bad index) = %v, want view index 5 out of range", err)
	}
	var view seedcalc.View
	if err := f.GetSheetViewOptions("Sheet1", 5, &view); err == nil || err.Error() != "view index 5 out of range" {
		t.Fatalf("GetSheetViewOptions(bad index) = %v, want view index 5 out of range", err)
	}
}
