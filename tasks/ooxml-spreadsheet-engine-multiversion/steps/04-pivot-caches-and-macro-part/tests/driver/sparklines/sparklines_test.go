// Scored group: sparklines (milestone 04, "Pivot caches, sparklines and the
// macro part").
//
// Sparklines are not part of the ECMA-376 schema; they are carried in the
// worksheet's future-feature storage area. Normative sources:
//   - TEST_INTERFACE.md section 4.04 -- "Sparkline groups".
//   - ECMA-376 Part 1, 18.2.7 ext (Extension) and 18.2.10 extLst (Future
//     Feature Data Storage Area) -- an ext element carries a uri that
//     identifies the extension, and a consumer that does not understand the
//     uri leaves the content alone.
//   - Oracle v2.0.2 for the x14 sparkline vocabulary carried inside that ext.
//   - Cases cross-checked against the upstream test corpus at tag v2.0.2
//     (sparkline_test.go).
package sparklines

import (
	"archive/zip"
	"bytes"
	"encoding/xml"
	"fmt"
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

type colour struct {
	RGB   string `xml:"rgb,attr"`
	Theme string `xml:"theme,attr"`
	Tint  string `xml:"tint,attr"`
}

type sparklineGroup struct {
	Type                string `xml:"type,attr"`
	DisplayEmptyCellsAs string `xml:"displayEmptyCellsAs,attr"`
	Markers             string `xml:"markers,attr"`
	High                string `xml:"high,attr"`
	Low                 string `xml:"low,attr"`
	First               string `xml:"first,attr"`
	Last                string `xml:"last,attr"`
	Negative            string `xml:"negative,attr"`
	DisplayXAxis        string `xml:"displayXAxis,attr"`
	RightToLeft         string `xml:"rightToLeft,attr"`
	ColorSeries         colour `xml:"http://schemas.microsoft.com/office/spreadsheetml/2009/9/main colorSeries"`
	ColorAxis           colour `xml:"http://schemas.microsoft.com/office/spreadsheetml/2009/9/main colorAxis"`
	Sparklines          struct {
		Sparkline []struct {
			F     string `xml:"http://schemas.microsoft.com/office/excel/2006/main f"`
			Sqref string `xml:"http://schemas.microsoft.com/office/excel/2006/main sqref"`
		} `xml:"http://schemas.microsoft.com/office/spreadsheetml/2009/9/main sparkline"`
	} `xml:"http://schemas.microsoft.com/office/spreadsheetml/2009/9/main sparklines"`
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

func sparklineExt(t *testing.T, f *seedcalc.File) extension {
	t.Helper()
	ws := worksheet(t, f)
	for _, e := range ws.ExtLst.Ext {
		if e.URI == seedcalc.ExtURISparklineGroups {
			return e
		}
	}
	t.Fatalf("the worksheet has no extension with uri %s", seedcalc.ExtURISparklineGroups)
	return extension{}
}

// dataWorkbook seeds a second sheet with two rows of numbers to chart.
func dataWorkbook(t *testing.T) *seedcalc.File {
	t.Helper()
	f := seedcalc.NewFile()
	f.NewSheet("Sheet2")
	for i := 1; i <= 2; i++ {
		row := []int{1, -2, 3, -4, 5, -6, 7, -8, 9, -10}
		if err := f.SetSheetRow("Sheet2", fmt.Sprintf("A%d", i), &row); err != nil {
			t.Fatalf("SetSheetRow: %v", err)
		}
	}
	return f
}

func TestSparklineGroupIsStoredInTheWorksheetExtensionList(t *testing.T) {
	f := dataWorkbook(t)
	err := f.AddSparkline("Sheet1", &seedcalc.SparklineOption{
		Location: []string{"A2"},
		Range:    []string{"Sheet2!A1:J1"},
		Type:     "line",
		Markers:  true,
	})
	if err != nil {
		t.Fatalf("AddSparkline: %v", err)
	}
	ext := sparklineExt(t, f)
	if len(ext.SparklineGroups.Group) != 1 {
		t.Fatalf("the extension holds %d sparkline groups, want 1", len(ext.SparklineGroups.Group))
	}
	g := ext.SparklineGroups.Group[0]
	if g.DisplayEmptyCellsAs != "gap" {
		t.Errorf("displayEmptyCellsAs = %q, want gap", g.DisplayEmptyCellsAs)
	}
	if g.ColorAxis.RGB != "FF000000" {
		t.Errorf("colorAxis rgb = %q, want FF000000", g.ColorAxis.RGB)
	}
	if g.Markers != "true" {
		t.Errorf("markers = %q, want true", g.Markers)
	}
}

func TestSparklineCarriesItsRangeAndLocation(t *testing.T) {
	f := dataWorkbook(t)
	err := f.AddSparkline("Sheet1", &seedcalc.SparklineOption{
		Location: []string{"A2", "A3"},
		Range:    []string{"Sheet2!A1:J1", "Sheet2!A2:J2"},
		Type:     "line",
	})
	if err != nil {
		t.Fatalf("AddSparkline: %v", err)
	}
	g := sparklineExt(t, f).SparklineGroups.Group[0]
	if len(g.Sparklines.Sparkline) != 2 {
		t.Fatalf("the group holds %d sparklines, want 2", len(g.Sparklines.Sparkline))
	}
	wantF := []string{"Sheet2!A1:J1", "Sheet2!A2:J2"}
	wantRef := []string{"A2", "A3"}
	for i, s := range g.Sparklines.Sparkline {
		if s.F != wantF[i] {
			t.Errorf("sparkline %d formula = %q, want %q", i, s.F, wantF[i])
		}
		if s.Sqref != wantRef[i] {
			t.Errorf("sparkline %d reference = %q, want %q", i, s.Sqref, wantRef[i])
		}
	}
}

func TestSparklineTypeNamesMapToGroupTypes(t *testing.T) {
	cases := map[string]string{"line": "line", "column": "column", "win_loss": "stacked"}
	for in, want := range cases {
		f := dataWorkbook(t)
		if err := f.AddSparkline("Sheet1", &seedcalc.SparklineOption{
			Location: []string{"A2"}, Range: []string{"Sheet2!A1:J1"}, Type: in,
		}); err != nil {
			t.Fatalf("AddSparkline(%q): %v", in, err)
		}
		if got := sparklineExt(t, f).SparklineGroups.Group[0].Type; got != want {
			t.Errorf("type %q was written as %q, want %q", in, got, want)
		}
	}
}

func TestUnknownSparklineTypeIsRejected(t *testing.T) {
	f := dataWorkbook(t)
	if err := f.AddSparkline("Sheet1", &seedcalc.SparklineOption{
		Location: []string{"A2"}, Range: []string{"Sheet2!A1:J1"}, Type: "sunburst",
	}); err == nil {
		t.Fatal("AddSparkline accepted an unknown sparkline type")
	}
	for _, e := range worksheet(t, f).ExtLst.Ext {
		if e.URI == seedcalc.ExtURISparklineGroups {
			t.Error("a rejected sparkline still created the sparkline extension")
		}
	}
}

func TestSparklineFeatureFlagsBecomeAttributes(t *testing.T) {
	f := dataWorkbook(t)
	err := f.AddSparkline("Sheet1", &seedcalc.SparklineOption{
		Location: []string{"A2"}, Range: []string{"Sheet2!A1:J1"}, Type: "column",
		High: true, Low: true, First: true, Last: true,
		Negative: true, Axis: true, Reverse: true, Markers: true,
	})
	if err != nil {
		t.Fatalf("AddSparkline: %v", err)
	}
	g := sparklineExt(t, f).SparklineGroups.Group[0]
	for name, got := range map[string]string{
		"high":         g.High,
		"low":          g.Low,
		"first":        g.First,
		"last":         g.Last,
		"negative":     g.Negative,
		"displayXAxis": g.DisplayXAxis,
		"rightToLeft":  g.RightToLeft,
		"markers":      g.Markers,
	} {
		if got != "true" {
			t.Errorf("%s = %q, want true", name, got)
		}
	}

	plain := dataWorkbook(t)
	if err := plain.AddSparkline("Sheet1", &seedcalc.SparklineOption{
		Location: []string{"A2"}, Range: []string{"Sheet2!A1:J1"}, Type: "column",
	}); err != nil {
		t.Fatalf("AddSparkline: %v", err)
	}
	q := sparklineExt(t, plain).SparklineGroups.Group[0]
	if q.High != "" || q.Low != "" || q.Markers != "" || q.RightToLeft != "" {
		t.Errorf("unset flags were written anyway: high=%q low=%q markers=%q rightToLeft=%q",
			q.High, q.Low, q.Markers, q.RightToLeft)
	}
}

func TestSeveralSparklineGroupsShareOneExtension(t *testing.T) {
	f := dataWorkbook(t)
	if err := f.AddSparkline("Sheet1", &seedcalc.SparklineOption{
		Location: []string{"A2"}, Range: []string{"Sheet2!A1:J1"}, Type: "line",
	}); err != nil {
		t.Fatalf("first AddSparkline: %v", err)
	}
	if err := f.AddSparkline("Sheet1", &seedcalc.SparklineOption{
		Location: []string{"A3"}, Range: []string{"Sheet2!A2:J2"}, Type: "column",
	}); err != nil {
		t.Fatalf("second AddSparkline: %v", err)
	}
	ws := worksheet(t, f)
	var n int
	for _, e := range ws.ExtLst.Ext {
		if e.URI == seedcalc.ExtURISparklineGroups {
			n++
		}
	}
	if n != 1 {
		t.Fatalf("the worksheet holds %d sparkline extensions, want exactly 1", n)
	}
	groups := sparklineExt(t, f).SparklineGroups.Group
	if len(groups) != 2 {
		t.Fatalf("the extension holds %d sparkline groups, want 2", len(groups))
	}
	if groups[0].Type != "line" || groups[1].Type != "column" {
		t.Errorf("group types = %q, %q, want line then column in call order", groups[0].Type, groups[1].Type)
	}
}

func TestSparklineStyleRange(t *testing.T) {
	for _, style := range []int{0, 17, 35} {
		f := dataWorkbook(t)
		if err := f.AddSparkline("Sheet1", &seedcalc.SparklineOption{
			Location: []string{"A2"}, Range: []string{"Sheet2!A1:J1"}, Type: "line", Style: style,
		}); err != nil {
			t.Errorf("AddSparkline rejected style %d: %v", style, err)
			continue
		}
		g := sparklineExt(t, f).SparklineGroups.Group[0]
		if g.ColorSeries.RGB == "" && g.ColorSeries.Theme == "" {
			t.Errorf("style %d produced a sparkline group with no series colour", style)
		}
	}
	for _, style := range []int{-1, 36} {
		f := dataWorkbook(t)
		if err := f.AddSparkline("Sheet1", &seedcalc.SparklineOption{
			Location: []string{"A2"}, Range: []string{"Sheet2!A1:J1"}, Type: "line", Style: style,
		}); err == nil {
			t.Errorf("AddSparkline accepted out-of-range style %d", style)
		}
	}
}

func TestSeriesColourOverridesTheStylePalette(t *testing.T) {
	f := dataWorkbook(t)
	if err := f.AddSparkline("Sheet1", &seedcalc.SparklineOption{
		Location: []string{"A2"}, Range: []string{"Sheet2!A1:J1"}, Type: "line",
		SeriesColor: "#0000FF",
	}); err != nil {
		t.Fatalf("AddSparkline: %v", err)
	}
	g := sparklineExt(t, f).SparklineGroups.Group[0]
	if g.ColorSeries.RGB != "FF0000FF" {
		t.Errorf("colorSeries rgb = %q, want FF0000FF", g.ColorSeries.RGB)
	}
	if g.ColorSeries.Theme != "" {
		t.Errorf("colorSeries kept theme %q alongside an explicit colour", g.ColorSeries.Theme)
	}
}

func TestSparklineArgumentValidation(t *testing.T) {
	base := func() *seedcalc.SparklineOption {
		return &seedcalc.SparklineOption{
			Location: []string{"A2"}, Range: []string{"Sheet2!A1:J1"}, Type: "line",
		}
	}
	noLocation := base()
	noLocation.Location = nil
	noRange := base()
	noRange.Range = nil
	mismatch := base()
	mismatch.Location = []string{"A2", "A3"}

	cases := []struct {
		name  string
		sheet string
		opt   *seedcalc.SparklineOption
	}{
		{"a nil option", "Sheet1", nil},
		{"an unknown worksheet", "SheetX", base()},
		{"no location", "Sheet1", noLocation},
		{"no range", "Sheet1", noRange},
		{"unequal location and range counts", "Sheet1", mismatch},
	}
	for _, c := range cases {
		f := dataWorkbook(t)
		if err := f.AddSparkline(c.sheet, c.opt); err == nil {
			t.Errorf("AddSparkline accepted %s", c.name)
		}
	}
}

func TestExtensionAndNamespaceConstants(t *testing.T) {
	for name, got := range map[string]string{
		"ExtURISparklineGroups":             seedcalc.ExtURISparklineGroups,
		"ExtURIConditionalFormattings":      seedcalc.ExtURIConditionalFormattings,
		"ExtURIDataValidations":             seedcalc.ExtURIDataValidations,
		"ExtURISlicerListX14":               seedcalc.ExtURISlicerListX14,
		"NameSpaceSpreadSheetX14":           seedcalc.NameSpaceSpreadSheetX14,
		"NameSpaceSpreadSheetX15":           seedcalc.NameSpaceSpreadSheetX15,
		"NameSpaceSpreadSheetExcel2006Main": seedcalc.NameSpaceSpreadSheetExcel2006Main,
		"NameSpaceMacExcel2008Main":         seedcalc.NameSpaceMacExcel2008Main,
	} {
		if got == "" {
			t.Errorf("%s is empty", name)
		}
	}
	if seedcalc.ExtURISparklineGroups != "{05C60535-1F16-4fd2-B633-F4F36F0B64E0}" {
		t.Errorf("ExtURISparklineGroups = %q", seedcalc.ExtURISparklineGroups)
	}
	if seedcalc.NameSpaceSpreadSheetX14 != "http://schemas.microsoft.com/office/spreadsheetml/2009/9/main" {
		t.Errorf("NameSpaceSpreadSheetX14 = %q", seedcalc.NameSpaceSpreadSheetX14)
	}
	if seedcalc.NameSpaceSpreadSheetExcel2006Main != "http://schemas.microsoft.com/office/excel/2006/main" {
		t.Errorf("NameSpaceSpreadSheetExcel2006Main = %q", seedcalc.NameSpaceSpreadSheetExcel2006Main)
	}
	if seedcalc.NameSpaceSpreadSheetX15 != "http://schemas.microsoft.com/office/spreadsheetml/2010/11/main" {
		t.Errorf("NameSpaceSpreadSheetX15 = %q", seedcalc.NameSpaceSpreadSheetX15)
	}
}
