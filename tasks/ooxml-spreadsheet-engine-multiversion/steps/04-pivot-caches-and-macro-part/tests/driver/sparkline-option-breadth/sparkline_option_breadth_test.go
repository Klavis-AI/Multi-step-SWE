// Scored group: sparkline-option-breadth (milestone 04, peripheral).
//
// SparklineOption gained many fields in milestone 04, and AddSparkline is
// easy to special-case by wiring only the handful the obvious example uses
// (Location, Range, Markers). The cases here exercise option fields beyond
// that minimum, checked against what TEST_INTERFACE.md 4.04.9 states about
// them -- not against what their names suggest.
//
// 4.04.9 states two things worth testing directly:
//
//   - Several SparklineOption fields the struct exposes -- DateAxis, Max,
//     CustMax, Min, CustMin, Weight, Hidden, EmptyCells, NegativeColor,
//     MarkersColor, FirstColor, LastColor, HightColor, LowColor -- are never
//     read by AddSparkline or parseFormatAddSparklineSet at all. Setting
//     them has no effect on the written XML: DisplayEmptyCellsAs is always
//     the literal "gap", and the corresponding xlsxX14SparklineGroup fields
//     (dateAxis, manualMax, manualMin, lineWeight, displayHidden,
//     minAxisType, maxAxisType) are never assigned, so they never appear in
//     the marshalled element. This is the opposite of "reaches the written
//     XML" and is exactly the kind of fact a from-the-name implementation
//     would get wrong in either direction.
//   - Of the six auxiliary colour fields on xlsxX14SparklineGroup
//     (colorNegative, colorMarkers, colorFirst, colorLast, colorHigh,
//     colorLow), only colorSeries is reachable through an option field
//     (SeriesColor); the other five are populated solely from the Style
//     palette table in addSparklineGroupByStyle and colorAxis is a hardcoded
//     "FF000000" set unconditionally in AddSparkline, independent of Style.
//
// This surface does not exist before milestone 04: SparklineOption and
// AddSparkline are new symbols with no milestone-03 equivalent, so every
// case here fails to even compile against the milestone-03 predecessor.
package sparklineoptionbreadth

import (
	"archive/zip"
	"bytes"
	"encoding/xml"
	"io/ioutil"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

const x14NS = "http://schemas.microsoft.com/office/spreadsheetml/2009/9/main"

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

type colour struct {
	RGB   string `xml:"rgb,attr"`
	Theme string `xml:"theme,attr"`
}

type sparklineGroup struct {
	ColorSeries   colour `xml:"http://schemas.microsoft.com/office/spreadsheetml/2009/9/main colorSeries"`
	ColorNegative colour `xml:"http://schemas.microsoft.com/office/spreadsheetml/2009/9/main colorNegative"`
	ColorAxis     colour `xml:"http://schemas.microsoft.com/office/spreadsheetml/2009/9/main colorAxis"`
	ColorMarkers  colour `xml:"http://schemas.microsoft.com/office/spreadsheetml/2009/9/main colorMarkers"`
	ColorFirst    colour `xml:"http://schemas.microsoft.com/office/spreadsheetml/2009/9/main colorFirst"`
	ColorLast     colour `xml:"http://schemas.microsoft.com/office/spreadsheetml/2009/9/main colorLast"`
	ColorHigh     colour `xml:"http://schemas.microsoft.com/office/spreadsheetml/2009/9/main colorHigh"`
	ColorLow      colour `xml:"http://schemas.microsoft.com/office/spreadsheetml/2009/9/main colorLow"`
}

type extension struct {
	URI             string `xml:"uri,attr"`
	SparklineGroups struct {
		Group []sparklineGroup `xml:"http://schemas.microsoft.com/office/spreadsheetml/2009/9/main sparklineGroup"`
	} `xml:"http://schemas.microsoft.com/office/spreadsheetml/2009/9/main sparklineGroups"`
}

type worksheetDoc struct {
	ExtLst struct {
		Ext []extension `xml:"ext"`
	} `xml:"extLst"`
}

func worksheetXML(t *testing.T, f *seedcalc.File) string {
	t.Helper()
	raw, ok := savedParts(t, f)["xl/worksheets/sheet1.xml"]
	if !ok {
		t.Fatal("the package has no xl/worksheets/sheet1.xml part")
	}
	return raw
}

func sparklineGroupFrom(t *testing.T, raw string) sparklineGroup {
	t.Helper()
	var ws worksheetDoc
	if err := xml.Unmarshal([]byte(raw), &ws); err != nil {
		t.Fatalf("the worksheet part is not well-formed: %v", err)
	}
	for _, e := range ws.ExtLst.Ext {
		if e.URI == seedcalc.ExtURISparklineGroups {
			if len(e.SparklineGroups.Group) != 1 {
				t.Fatalf("the extension holds %d sparkline groups, want 1", len(e.SparklineGroups.Group))
			}
			return e.SparklineGroups.Group[0]
		}
	}
	t.Fatalf("the worksheet has no extension with uri %s", seedcalc.ExtURISparklineGroups)
	return sparklineGroup{}
}

// baseFile mirrors the sparklines "new"-tier fixture: a second sheet exists
// so Range strings that reference it look realistic, though AddSparkline
// never actually validates that a Range points at real data.
func baseFile(t *testing.T) *seedcalc.File {
	t.Helper()
	f := seedcalc.NewFile()
	f.NewSheet("Sheet2")
	return f
}

// TestUnwiredSparklineOptionFieldsHaveNoEffect sets every SparklineOption
// field 4.04.9 declares accepted and ignored (DateAxis, Max, CustMax, Min,
// CustMin, Weight, Hidden, EmptyCells, and the five colour fields other than
// SeriesColor) and checks that the written worksheet part is byte-for-byte
// identical to a call that omits them. A more "complete" implementation that
// actually wires these fields would fail this by producing different output
// than the contract requires.
func TestUnwiredSparklineOptionFieldsHaveNoEffect(t *testing.T) {
	minimal := &seedcalc.SparklineOption{
		Location: []string{"A2"}, Range: []string{"Sheet2!A1:J1"},
		Type: "column", Style: 12,
		High: true, Low: true, First: true, Last: true, Negative: true,
		Axis: true, Markers: true, Reverse: true, SeriesColor: "#123456",
	}
	loaded := &seedcalc.SparklineOption{
		Location: []string{"A2"}, Range: []string{"Sheet2!A1:J1"},
		Type: "column", Style: 12,
		High: true, Low: true, First: true, Last: true, Negative: true,
		Axis: true, Markers: true, Reverse: true, SeriesColor: "#123456",
		DateAxis: true, Max: 100, CustMax: 200, Min: -100, CustMin: -200,
		Weight: 3.5, Hidden: true, EmptyCells: "span",
		NegativeColor: "#654321", MarkersColor: "#111111",
		FirstColor: "#222222", LastColor: "#333333",
		HightColor: "#444444", LowColor: "#555555",
	}

	a := baseFile(t)
	if err := a.AddSparkline("Sheet1", minimal); err != nil {
		t.Fatalf("AddSparkline (minimal): %v", err)
	}
	b := baseFile(t)
	if err := b.AddSparkline("Sheet1", loaded); err != nil {
		t.Fatalf("AddSparkline (loaded): %v", err)
	}
	wantRaw := worksheetXML(t, a)
	gotRaw := worksheetXML(t, b)
	if gotRaw != wantRaw {
		t.Errorf("setting the unwired option fields changed xl/worksheets/sheet1.xml:\nminimal: %s\nloaded:  %s", wantRaw, gotRaw)
	}
	g := sparklineGroupFrom(t, gotRaw)
	if g.ColorAxis.RGB != "FF000000" {
		t.Errorf("colorAxis rgb = %q, want the hardcoded FF000000 regardless of the unwired fields", g.ColorAxis.RGB)
	}
}

// TestStylePaletteSuppliesTheAuxiliaryColoursAndAxisStaysFixed checks that
// the six auxiliary colour elements (everything besides colorSeries and
// colorAxis) are populated straight from the Style palette table -- style 31
// gives each of them a distinct, exact RGB value taken directly from
// addSparklineGroupByStyle's table -- while colorAxis stays the hardcoded
// FF000000 regardless of which style produced the group.
func TestStylePaletteSuppliesTheAuxiliaryColoursAndAxisStaysFixed(t *testing.T) {
	f := baseFile(t)
	if err := f.AddSparkline("Sheet1", &seedcalc.SparklineOption{
		Location: []string{"A2"}, Range: []string{"Sheet2!A1:J1"}, Type: "line", Style: 31,
	}); err != nil {
		t.Fatalf("AddSparkline: %v", err)
	}
	g := sparklineGroupFrom(t, worksheetXML(t, f))
	want := map[string]struct {
		got  colour
		rgb  string
		name string
	}{
		"colorSeries":   {g.ColorSeries, "FF5F5F5F", "colorSeries"},
		"colorNegative": {g.ColorNegative, "FFFFB620", "colorNegative"},
		"colorMarkers":  {g.ColorMarkers, "FFD70077", "colorMarkers"},
		"colorFirst":    {g.ColorFirst, "FF5687C2", "colorFirst"},
		"colorLast":     {g.ColorLast, "FF359CEB", "colorLast"},
		"colorHigh":     {g.ColorHigh, "FF56BE79", "colorHigh"},
		"colorLow":      {g.ColorLow, "FFFF5055", "colorLow"},
	}
	for _, w := range want {
		if w.got.RGB != w.rgb {
			t.Errorf("style 31 %s rgb = %q, want %q", w.name, w.got.RGB, w.rgb)
		}
	}
	if g.ColorAxis.RGB != "FF000000" {
		t.Errorf("style 31 colorAxis rgb = %q, want the style-independent FF000000", g.ColorAxis.RGB)
	}

	f0 := baseFile(t)
	if err := f0.AddSparkline("Sheet1", &seedcalc.SparklineOption{
		Location: []string{"A2"}, Range: []string{"Sheet2!A1:J1"}, Type: "line", Style: 0,
	}); err != nil {
		t.Fatalf("AddSparkline (style 0): %v", err)
	}
	g0 := sparklineGroupFrom(t, worksheetXML(t, f0))
	if g0.ColorNegative.Theme == "" || g0.ColorNegative.RGB != "" {
		t.Errorf("style 0 colorNegative = %+v, want a theme colour (no rgb), unlike style 31's explicit rgb", g0.ColorNegative)
	}
	if g0.ColorAxis.RGB != "FF000000" {
		t.Errorf("style 0 colorAxis rgb = %q, want the style-independent FF000000", g0.ColorAxis.RGB)
	}
}

// TestSeriesColorOverrideLeavesTheOtherFiveColoursAtTheStyleDefault checks
// that SeriesColor only ever replaces colorSeries: the other five auxiliary
// colour elements are identical whether or not SeriesColor is set, because
// AddSparkline only assigns group.ColorSeries when opt.SeriesColor is
// non-empty and never touches the rest.
func TestSeriesColorOverrideLeavesTheOtherFiveColoursAtTheStyleDefault(t *testing.T) {
	plain := baseFile(t)
	if err := plain.AddSparkline("Sheet1", &seedcalc.SparklineOption{
		Location: []string{"A2"}, Range: []string{"Sheet2!A1:J1"}, Type: "line", Style: 9,
	}); err != nil {
		t.Fatalf("AddSparkline (plain): %v", err)
	}
	overridden := baseFile(t)
	if err := overridden.AddSparkline("Sheet1", &seedcalc.SparklineOption{
		Location: []string{"A2"}, Range: []string{"Sheet2!A1:J1"}, Type: "line", Style: 9,
		SeriesColor: "#00FF00",
	}); err != nil {
		t.Fatalf("AddSparkline (overridden): %v", err)
	}
	p := sparklineGroupFrom(t, worksheetXML(t, plain))
	o := sparklineGroupFrom(t, worksheetXML(t, overridden))

	if o.ColorSeries.RGB != "FF00FF00" {
		t.Errorf("colorSeries rgb = %q, want FF00FF00", o.ColorSeries.RGB)
	}
	if o.ColorSeries == p.ColorSeries {
		t.Error("colorSeries was not actually overridden")
	}
	type named struct {
		name string
		p, o colour
	}
	for _, c := range []named{
		{"colorNegative", p.ColorNegative, o.ColorNegative},
		{"colorMarkers", p.ColorMarkers, o.ColorMarkers},
		{"colorFirst", p.ColorFirst, o.ColorFirst},
		{"colorLast", p.ColorLast, o.ColorLast},
		{"colorHigh", p.ColorHigh, o.ColorHigh},
		{"colorLow", p.ColorLow, o.ColorLow},
	} {
		if c.p != c.o {
			t.Errorf("%s changed when only SeriesColor was set: plain=%+v overridden=%+v", c.name, c.p, c.o)
		}
	}
}

// TestUnknownSheetErrorPrecedesNilOptionCheck checks the order
// parseFormatAddSparklineSet runs its validation in: it resolves the sheet
// (f.workSheetReader) before it checks opt for nil, so calling AddSparkline
// with both an unknown sheet and a nil option returns the sheet-resolution
// error -- and does not panic on the nil option, since opt is never
// dereferenced on that path.
func TestUnknownSheetErrorPrecedesNilOptionCheck(t *testing.T) {
	f := seedcalc.NewFile()
	err := f.AddSparkline("NoSuchSheet", nil)
	if err == nil {
		t.Fatal("AddSparkline accepted an unknown sheet with a nil option")
	}
	if err.Error() == "parameter is required" {
		t.Errorf("AddSparkline reported the nil-option error %q; want the sheet-resolution error to win", err.Error())
	}
}
