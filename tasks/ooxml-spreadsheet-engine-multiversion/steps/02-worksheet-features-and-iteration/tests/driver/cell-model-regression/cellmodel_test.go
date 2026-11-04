// Gate group: cell-model-regression.
//
// The cell model established earlier must be untouched by this milestone's
// move to name-keyed worksheets: typed values, formulas, hyperlinks, styles
// and the A1 coordinate helpers all behave as before and survive a write and
// read. Graded against ECMA-376 Part 1 clause 18.3.1.4 (c), clause 18.3.1.40
// (f), clause 18.3.1.47 (hyperlink) and clause 18.18.62 (ST_Ref, the A1
// reference style used by cell references).
package cellmodelregression

import (
	"bytes"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

func roundTrip(t *testing.T, f *seedcalc.File) *seedcalc.File {
	t.Helper()
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	g, err := seedcalc.OpenReader(bytes.NewReader(buf.Bytes()))
	if err != nil {
		t.Fatalf("OpenReader on a package this library wrote: %v", err)
	}
	return g
}

func TestTypedCellValuesReadBackAsText(t *testing.T) {
	f := seedcalc.NewFile()
	f.SetCellValue("Sheet1", "A1", "plain text")
	f.SetCellValue("Sheet1", "A2", 42)
	f.SetCellValue("Sheet1", "A3", -7)
	f.SetCellValue("Sheet1", "A4", 3.5)
	f.SetCellValue("Sheet1", "A5", "")

	for _, tc := range []struct{ axis, want string }{
		{"A1", "plain text"},
		{"A2", "42"},
		{"A3", "-7"},
		{"A4", "3.5"},
		{"A5", ""},
		{"Z99", ""},
	} {
		if got := f.GetCellValue("Sheet1", tc.axis); got != tc.want {
			t.Errorf("%s = %q, want %q", tc.axis, got, tc.want)
		}
	}
}

func TestCellFormulaRoundTrips(t *testing.T) {
	f := seedcalc.NewFile()
	f.SetCellValue("Sheet1", "A1", 2)
	f.SetCellValue("Sheet1", "A2", 3)
	f.SetCellFormula("Sheet1", "A3", "SUM(A1:A2)")

	if got := f.GetCellFormula("Sheet1", "A3"); got != "SUM(A1:A2)" {
		t.Errorf("GetCellFormula = %q, want %q", got, "SUM(A1:A2)")
	}
	if got := f.GetCellFormula("Sheet1", "A1"); got != "" {
		t.Errorf("GetCellFormula on a cell with no formula = %q, want the empty string", got)
	}
	if got := roundTrip(t, f).GetCellFormula("Sheet1", "A3"); got != "SUM(A1:A2)" {
		t.Errorf("GetCellFormula after a write and read = %q, want %q", got, "SUM(A1:A2)")
	}
}

func TestCellHyperLinkRoundTrips(t *testing.T) {
	f := seedcalc.NewFile()
	f.SetCellHyperLink("Sheet1", "A1", "https://example.com/spec", "External")
	f.SetCellHyperLink("Sheet1", "A2", "Sheet1!A40", "Location")

	linked, target := f.GetCellHyperLink("Sheet1", "A1")
	if !linked || target != "https://example.com/spec" {
		t.Errorf("A1 hyperlink = (%v, %q), want (true, %q)", linked, target, "https://example.com/spec")
	}
	linked, target = f.GetCellHyperLink("Sheet1", "A2")
	if !linked || target != "Sheet1!A40" {
		t.Errorf("A2 hyperlink = (%v, %q), want (true, %q)", linked, target, "Sheet1!A40")
	}
	linked, target = f.GetCellHyperLink("Sheet1", "B9")
	if linked || target != "" {
		t.Errorf("an unlinked cell reports (%v, %q), want (false, \"\")", linked, target)
	}
}

func TestCoordinateHelpersAreInverses(t *testing.T) {
	for _, tc := range []struct {
		n    int
		name string
	}{
		{0, "A"},
		{1, "B"},
		{25, "Z"},
		{26, "AA"},
		{51, "AZ"},
		{52, "BA"},
		{701, "ZZ"},
	} {
		if got := seedcalc.ToAlphaString(tc.n); got != tc.name {
			t.Errorf("ToAlphaString(%d) = %q, want %q", tc.n, got, tc.name)
		}
		if got := seedcalc.TitleToNumber(tc.name); got != tc.n {
			t.Errorf("TitleToNumber(%q) = %d, want %d", tc.name, got, tc.n)
		}
	}
}

func TestCellStyleAppliesAndReadsBack(t *testing.T) {
	f := seedcalc.NewFile()
	styleID, err := f.NewStyle(`{"font":{"bold":true}}`)
	if err != nil {
		t.Fatalf("NewStyle: %v", err)
	}
	if styleID == 0 {
		t.Fatal("NewStyle returned the default style for a non-empty descriptor")
	}
	f.SetCellStyle("Sheet1", "A1", "B2", styleID)

	for _, axis := range []string{"A1", "A2", "B1", "B2"} {
		if got := f.GetCellStyle("Sheet1", axis); got != styleID {
			t.Errorf("GetCellStyle(%s) = %d, want %d", axis, got, styleID)
		}
	}
	if got := f.GetCellStyle("Sheet1", "C3"); got == styleID {
		t.Errorf("GetCellStyle(C3) = %d; the style was applied to A1:B2 only", got)
	}
	if _, err := f.NewStyle(`{`); err == nil {
		t.Error("NewStyle accepted a malformed style descriptor")
	}
}

func TestTheWholeCellModelSurvivesAWriteAndRead(t *testing.T) {
	f := seedcalc.NewFile()
	styleID, err := f.NewStyle(`{"font":{"italic":true}}`)
	if err != nil {
		t.Fatalf("NewStyle: %v", err)
	}
	f.SetCellValue("Sheet1", "A1", "text")
	f.SetCellValue("Sheet1", "A2", 12.25)
	f.SetCellFormula("Sheet1", "A3", "A2*2")
	f.SetCellHyperLink("Sheet1", "A4", "https://example.com/", "External")
	f.SetCellStyle("Sheet1", "A1", "A1", styleID)

	g := roundTrip(t, f)
	if got := g.GetCellValue("Sheet1", "A1"); got != "text" {
		t.Errorf("A1 = %q, want %q", got, "text")
	}
	if got := g.GetCellValue("Sheet1", "A2"); got != "12.25" {
		t.Errorf("A2 = %q, want %q", got, "12.25")
	}
	if got := g.GetCellFormula("Sheet1", "A3"); got != "A2*2" {
		t.Errorf("A3 formula = %q, want %q", got, "A2*2")
	}
	if linked, target := g.GetCellHyperLink("Sheet1", "A4"); !linked || target != "https://example.com/" {
		t.Errorf("A4 hyperlink = (%v, %q) after a round trip", linked, target)
	}
	if got := g.GetCellStyle("Sheet1", "A1"); got != styleID {
		t.Errorf("A1 style = %d after a round trip, want %d", got, styleID)
	}
}
