// Scored group: page-setup (milestone 03, "Workbook metadata and the error
// contract").
//
// Normative sources:
//   * TEST_INTERFACE.md section 4.03 -- "Page layout", "Headers and footers".
//   * ECMA-376 Part 1, 18.3.1.63 pageSetup (@orientation, @paperSize) and
//     18.3.1.46 headerFooter (@differentFirst, @differentOddEven,
//     @alignWithMargins, @scaleWithDoc; children oddHeader, oddFooter,
//     evenHeader, evenFooter, firstHeader, firstFooter).
//   * Cases cross-checked against the upstream test corpus at tag v2.0.1
//     (sheet_test.go: TestPageLayoutOption, TestSetHeaderFooter).
package pagesetup

import (
	"archive/zip"
	"bytes"
	"io/ioutil"
	"strings"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// readPart serialises f and returns the named package part as text.
func readPart(t *testing.T, f *seedcalc.File, name string) string {
	t.Helper()
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	zr, err := zip.NewReader(bytes.NewReader(buf.Bytes()), int64(buf.Len()))
	if err != nil {
		t.Fatalf("the saved workbook is not a readable package: %v", err)
	}
	for _, zf := range zr.File {
		if zf.Name != name {
			continue
		}
		rc, err := zf.Open()
		if err != nil {
			t.Fatalf("open %s: %v", name, err)
		}
		defer rc.Close()
		b, err := ioutil.ReadAll(rc)
		if err != nil {
			t.Fatalf("read %s: %v", name, err)
		}
		return string(b)
	}
	t.Fatalf("the saved package has no part named %s", name)
	return ""
}

func TestPageLayoutDefaults(t *testing.T) {
	f := seedcalc.NewFile()

	var orientation seedcalc.PageLayoutOrientation
	var paper seedcalc.PageLayoutPaperSize
	if err := f.GetPageLayout("Sheet1", &orientation, &paper); err != nil {
		t.Fatalf("GetPageLayout: %v", err)
	}
	if string(orientation) != seedcalc.OrientationPortrait {
		t.Errorf("default orientation = %q, want %q", orientation, seedcalc.OrientationPortrait)
	}
	if int(paper) != 1 {
		t.Errorf("default paper size = %d, want 1", paper)
	}

	// The variadic option lists may be empty on both sides.
	if err := f.SetPageLayout("Sheet1"); err != nil {
		t.Errorf("SetPageLayout with no options = %v, want nil", err)
	}
	if err := f.GetPageLayout("Sheet1"); err != nil {
		t.Errorf("GetPageLayout with no options = %v, want nil", err)
	}
}

func TestSetPageLayoutOrientationAndPaperSize(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetPageLayout("Sheet1",
		seedcalc.PageLayoutOrientation(seedcalc.OrientationLandscape),
		seedcalc.PageLayoutPaperSize(10),
	); err != nil {
		t.Fatalf("SetPageLayout: %v", err)
	}

	var orientation seedcalc.PageLayoutOrientation
	var paper seedcalc.PageLayoutPaperSize
	if err := f.GetPageLayout("Sheet1", &orientation, &paper); err != nil {
		t.Fatalf("GetPageLayout: %v", err)
	}
	if string(orientation) != seedcalc.OrientationLandscape {
		t.Errorf("orientation = %q, want %q", orientation, seedcalc.OrientationLandscape)
	}
	if int(paper) != 10 {
		t.Errorf("paper size = %d, want 10", paper)
	}

	// Setting one option leaves the other alone.
	if err := f.SetPageLayout("Sheet1", seedcalc.PageLayoutPaperSize(9)); err != nil {
		t.Fatalf("SetPageLayout: %v", err)
	}
	if err := f.GetPageLayout("Sheet1", &orientation, &paper); err != nil {
		t.Fatalf("GetPageLayout: %v", err)
	}
	if string(orientation) != seedcalc.OrientationLandscape || int(paper) != 9 {
		t.Errorf("after a partial update: orientation = %q, paper = %d; want %q and 9",
			orientation, paper, seedcalc.OrientationLandscape)
	}

	// The settings are written to the worksheet's pageSetup element.
	sheet := readPart(t, f, "xl/worksheets/sheet1.xml")
	if !strings.Contains(sheet, `orientation="landscape"`) {
		t.Errorf("the saved worksheet does not carry orientation=\"landscape\"")
	}
	if !strings.Contains(sheet, `paperSize="9"`) {
		t.Errorf("the saved worksheet does not carry paperSize=\"9\"")
	}
}

func TestPageLayoutSurvivesSaveAndReopen(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetPageLayout("Sheet1",
		seedcalc.PageLayoutOrientation(seedcalc.OrientationLandscape),
		seedcalc.PageLayoutPaperSize(9),
	); err != nil {
		t.Fatalf("SetPageLayout: %v", err)
	}
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	g, err := seedcalc.OpenReader(bytes.NewReader(buf.Bytes()))
	if err != nil {
		t.Fatalf("OpenReader: %v", err)
	}
	var orientation seedcalc.PageLayoutOrientation
	var paper seedcalc.PageLayoutPaperSize
	if err := g.GetPageLayout("Sheet1", &orientation, &paper); err != nil {
		t.Fatalf("GetPageLayout after reopen: %v", err)
	}
	if string(orientation) != seedcalc.OrientationLandscape || int(paper) != 9 {
		t.Errorf("after reopen: orientation = %q, paper = %d; want %q and 9",
			orientation, paper, seedcalc.OrientationLandscape)
	}
}

func TestPageLayoutIsPerWorksheet(t *testing.T) {
	f := seedcalc.NewFile()
	if idx := f.NewSheet("Sheet2"); idx != 2 {
		t.Fatalf("NewSheet(\"Sheet2\") = %d, want 2", idx)
	}
	if err := f.SetPageLayout("Sheet2", seedcalc.PageLayoutOrientation(seedcalc.OrientationLandscape)); err != nil {
		t.Fatalf("SetPageLayout on Sheet2: %v", err)
	}
	var orientation seedcalc.PageLayoutOrientation
	if err := f.GetPageLayout("Sheet1", &orientation); err != nil {
		t.Fatalf("GetPageLayout on Sheet1: %v", err)
	}
	if string(orientation) != seedcalc.OrientationPortrait {
		t.Errorf("Sheet1 orientation = %q, want it untouched at %q", orientation, seedcalc.OrientationPortrait)
	}
	if err := f.GetPageLayout("Sheet2", &orientation); err != nil {
		t.Fatalf("GetPageLayout on Sheet2: %v", err)
	}
	if string(orientation) != seedcalc.OrientationLandscape {
		t.Errorf("Sheet2 orientation = %q, want %q", orientation, seedcalc.OrientationLandscape)
	}
}

func TestSetHeaderFooterWritesEveryVariant(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetHeaderFooter("Sheet1", &seedcalc.FormatHeaderFooter{
		AlignWithMargins: true,
		DifferentFirst:   true,
		DifferentOddEven: true,
		ScaleWithDoc:     true,
		OddHeader:        "&R&P",
		OddFooter:        "&C&F",
		EvenHeader:       "&L&P",
		EvenFooter:       "&L&D&R&T",
		FirstHeader:      "&CFirst page header",
		FirstFooter:      "&LFirst page footer",
	}); err != nil {
		t.Fatalf("SetHeaderFooter: %v", err)
	}

	sheet := readPart(t, f, "xl/worksheets/sheet1.xml")
	for _, want := range []string{
		"<headerFooter", "</headerFooter>",
		`alignWithMargins="true"`, `differentFirst="true"`,
		`differentOddEven="true"`, `scaleWithDoc="true"`,
		"<oddHeader>", "<oddFooter>",
		"<evenHeader>", "<evenFooter>",
		"<firstHeader>", "<firstFooter>",
	} {
		if !strings.Contains(sheet, want) {
			t.Errorf("the saved worksheet does not contain %s", want)
		}
	}
	// The header text itself is carried through, XML-escaped.
	if !strings.Contains(sheet, "First page header") {
		t.Error("the first-page header text was not written")
	}
	if !strings.Contains(sheet, "First page footer") {
		t.Error("the first-page footer text was not written")
	}
}

func TestSetHeaderFooterNilClearsIt(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetHeaderFooter("Sheet1", &seedcalc.FormatHeaderFooter{OddHeader: "&Ctitle"}); err != nil {
		t.Fatalf("SetHeaderFooter: %v", err)
	}
	if !strings.Contains(readPart(t, f, "xl/worksheets/sheet1.xml"), "<headerFooter") {
		t.Fatal("no headerFooter element after setting one")
	}
	if err := f.SetHeaderFooter("Sheet1", nil); err != nil {
		t.Fatalf("SetHeaderFooter(nil): %v", err)
	}
	if strings.Contains(readPart(t, f, "xl/worksheets/sheet1.xml"), "<headerFooter") {
		t.Error("SetHeaderFooter(nil) must remove the headerFooter element")
	}
	// Clearing a worksheet that never had one is still not an error.
	g := seedcalc.NewFile()
	if err := g.SetHeaderFooter("Sheet1", nil); err != nil {
		t.Errorf("SetHeaderFooter(nil) on a fresh worksheet = %v, want nil", err)
	}
}

func TestSetHeaderFooterRejectsOverlongText(t *testing.T) {
	long := strings.Repeat("A", 255)
	ok := strings.Repeat("A", 254)

	rejected := map[string]func(v string) *seedcalc.FormatHeaderFooter{
		"OddHeader":   func(v string) *seedcalc.FormatHeaderFooter { return &seedcalc.FormatHeaderFooter{OddHeader: v} },
		"OddFooter":   func(v string) *seedcalc.FormatHeaderFooter { return &seedcalc.FormatHeaderFooter{OddFooter: v} },
		"EvenHeader":  func(v string) *seedcalc.FormatHeaderFooter { return &seedcalc.FormatHeaderFooter{EvenHeader: v} },
		"EvenFooter":  func(v string) *seedcalc.FormatHeaderFooter { return &seedcalc.FormatHeaderFooter{EvenFooter: v} },
		"FirstFooter": func(v string) *seedcalc.FormatHeaderFooter { return &seedcalc.FormatHeaderFooter{FirstFooter: v} },
	}
	for name, build := range rejected {
		f := seedcalc.NewFile()
		if err := f.SetHeaderFooter("Sheet1", build(long)); err == nil {
			t.Errorf("%s of 255 characters was accepted, want a non-nil error", name)
		}
		// Nothing was written for the rejected settings.
		if strings.Contains(readPart(t, f, "xl/worksheets/sheet1.xml"), "<headerFooter") {
			t.Errorf("%s: a rejected SetHeaderFooter still wrote a headerFooter element", name)
		}
		g := seedcalc.NewFile()
		if err := g.SetHeaderFooter("Sheet1", build(ok)); err != nil {
			t.Errorf("%s of 254 characters = %v, want nil", name, err)
		}
	}
}
