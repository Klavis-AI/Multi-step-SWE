// Scored group: page-margins (milestone 05, "The streaming writer and
// the drawing registry").
//
// Normative source: TEST_INTERFACE.md section 4.05.4 -- "Page margin variadic
// options". PageMarginBottom/Footer/Header/Left/Right/Top are the six named
// types of the option pair: the value satisfies PageMarginsOptions and its
// address satisfies PageMarginsOptionsPtr. Defaults when the sheet has no
// <pageMargins> element: Bottom=0.75, Footer=0.3, Header=0.3, Left=0.7,
// Right=0.7, Top=0.75.
package pagemargins

import (
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// roundTrip sets the given options on a fresh workbook then reads them back.
func roundTrip(t *testing.T, setOpts []seedcalc.PageMarginsOptions, getOpts []seedcalc.PageMarginsOptionsPtr) {
	t.Helper()
	f := seedcalc.NewFile()
	if err := f.SetPageMargins("Sheet1", setOpts...); err != nil {
		t.Fatalf("SetPageMargins: %v", err)
	}
	if err := f.GetPageMargins("Sheet1", getOpts...); err != nil {
		t.Fatalf("GetPageMargins: %v", err)
	}
}

func TestPageMarginsDefaultsWhenUnset(t *testing.T) {
	f := seedcalc.NewFile()
	var (
		bot    seedcalc.PageMarginBottom
		footer seedcalc.PageMarginFooter
		header seedcalc.PageMarginHeader
		left   seedcalc.PageMarginLeft
		right  seedcalc.PageMarginRight
		top    seedcalc.PageMarginTop
	)
	if err := f.GetPageMargins("Sheet1", &bot, &footer, &header, &left, &right, &top); err != nil {
		t.Fatalf("GetPageMargins on a sheet with no margins set: %v", err)
	}
	defaults := map[string][2]float64{
		"Bottom": {float64(bot), 0.75},
		"Footer": {float64(footer), 0.3},
		"Header": {float64(header), 0.3},
		"Left":   {float64(left), 0.7},
		"Right":  {float64(right), 0.7},
		"Top":    {float64(top), 0.75},
	}
	for name, pair := range defaults {
		if pair[0] != pair[1] {
			t.Errorf("default %s margin = %v, want %v", name, pair[0], pair[1])
		}
	}
}

func TestSetPageMarginsRoundTrip(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetPageMargins("Sheet1",
		seedcalc.PageMarginBottom(1.5),
		seedcalc.PageMarginTop(2.0),
		seedcalc.PageMarginLeft(0.5),
		seedcalc.PageMarginRight(0.5),
	); err != nil {
		t.Fatalf("SetPageMargins: %v", err)
	}
	var bot, top, left, right seedcalc.PageMarginBottom
	var topOut seedcalc.PageMarginTop
	var leftOut seedcalc.PageMarginLeft
	var rightOut seedcalc.PageMarginRight
	if err := f.GetPageMargins("Sheet1", &bot, &topOut, &leftOut, &rightOut); err != nil {
		t.Fatalf("GetPageMargins: %v", err)
	}
	_ = top
	_ = left
	_ = right
	if float64(bot) != 1.5 {
		t.Errorf("Bottom = %v, want 1.5", float64(bot))
	}
	if float64(topOut) != 2.0 {
		t.Errorf("Top = %v, want 2.0", float64(topOut))
	}
	if float64(leftOut) != 0.5 {
		t.Errorf("Left = %v, want 0.5", float64(leftOut))
	}
	if float64(rightOut) != 0.5 {
		t.Errorf("Right = %v, want 0.5", float64(rightOut))
	}
}

func TestSetPageMarginsHeaderFooter(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetPageMargins("Sheet1",
		seedcalc.PageMarginHeader(0.6),
		seedcalc.PageMarginFooter(0.4),
	); err != nil {
		t.Fatalf("SetPageMargins: %v", err)
	}
	var hdr seedcalc.PageMarginHeader
	var ftr seedcalc.PageMarginFooter
	if err := f.GetPageMargins("Sheet1", &hdr, &ftr); err != nil {
		t.Fatalf("GetPageMargins: %v", err)
	}
	if float64(hdr) != 0.6 {
		t.Errorf("Header = %v, want 0.6", float64(hdr))
	}
	if float64(ftr) != 0.4 {
		t.Errorf("Footer = %v, want 0.4", float64(ftr))
	}
}

func TestSetPageMarginsInvalidSheetReturnsError(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetPageMargins("NoSuchSheet", seedcalc.PageMarginTop(1.0)); err == nil {
		t.Error("SetPageMargins on a non-existent sheet must return an error")
	}
}

func TestGetPageMarginsInvalidSheetReturnsError(t *testing.T) {
	f := seedcalc.NewFile()
	var top seedcalc.PageMarginTop
	if err := f.GetPageMargins("NoSuchSheet", &top); err == nil {
		t.Error("GetPageMargins on a non-existent sheet must return an error")
	}
}
