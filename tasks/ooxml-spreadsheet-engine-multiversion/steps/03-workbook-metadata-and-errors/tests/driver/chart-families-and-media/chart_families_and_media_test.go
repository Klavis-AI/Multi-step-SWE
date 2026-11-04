// Scored group: chart-families-and-media (milestone 03, "Workbook metadata
// and the error contract").
//
// Normative sources:
//   * TEST_INTERFACE.md section 4.03 -- "Further chart families",
//     "Per-column styles", "TIFF images".
//   * ECMA-376 Part 1, 21.2.2.16 bar3DChart (@shape: cone / pyramid /
//     cylinder / box), 21.2.2.203 surface3DChart, 21.2.2.201 surfaceChart,
//     21.2.2.20 bubbleChart, 21.2.2.218 wireframe, 21.2.2.19 bubble3D.
//   * ECMA-376 Part 1, 18.3.1.13 col (@min, @max, @style).
//   * ECMA-376 Part 2, 7.2.3  Mapping media types to parts (Default element
//     for part extensions).
//   * Cases cross-checked against the upstream test corpus at tag v2.0.1
//     (chart_test.go, col_test.go TestSetColStyle, picture_test.go).
package chartfamiliesandmedia

import (
	"archive/zip"
	"bytes"
	"image"
	"io"
	"io/ioutil"
	"strings"
	"sync"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// tiffMagic is the little-endian TIFF signature, [MS-OFFCRYPTO] unrelated:
// TIFF 6.0 section 2, "II" followed by the number 42.
const tiffMagic = "II\x2a\x00"

var registerTIFF sync.Once

// packageParts serialises f and returns every part of the resulting package.
func packageParts(t *testing.T, f *seedcalc.File) map[string]string {
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

// chartWorkbook returns a workbook holding a small block of chart data.
func chartWorkbook(t *testing.T) *seedcalc.File {
	t.Helper()
	f := seedcalc.NewFile()
	rows := [][]interface{}{
		{nil, "Apple", "Orange", "Pear"},
		{"Small", 2, 3, 3},
		{"Normal", 5, 2, 4},
		{"Large", 6, 7, 8},
	}
	for i := range rows {
		cell, err := seedcalc.CoordinatesToCellName(1, i+1)
		if err != nil {
			t.Fatalf("CoordinatesToCellName: %v", err)
		}
		if err := f.SetSheetRow("Sheet1", cell, &rows[i]); err != nil {
			t.Fatalf("SetSheetRow: %v", err)
		}
	}
	return f
}

// addChart adds a chart of the given type and returns the chart part.
func addChart(t *testing.T, chartType string) string {
	t.Helper()
	f := chartWorkbook(t)
	format := `{"type":"` + chartType + `","series":[` +
		`{"name":"Sheet1!$A$2","categories":"Sheet1!$B$1:$D$1","values":"Sheet1!$B$2:$D$2"},` +
		`{"name":"Sheet1!$A$3","categories":"Sheet1!$B$1:$D$1","values":"Sheet1!$B$3:$D$3"}],` +
		`"title":{"name":"Chart"}}`
	if err := f.AddChart("Sheet1", "F1", format); err != nil {
		t.Fatalf("AddChart(%q): %v", chartType, err)
	}
	parts := packageParts(t, f)
	chart, ok := parts["xl/charts/chart1.xml"]
	if !ok {
		t.Fatalf("AddChart(%q) produced no xl/charts/chart1.xml part", chartType)
	}
	return chart
}

type shapedChart struct {
	chartType string
	barDir    string
	grouping  string
}

func assertShapedCharts(t *testing.T, shape string, cases []shapedChart) {
	t.Helper()
	for _, c := range cases {
		chart := addChart(t, c.chartType)
		if !strings.Contains(chart, "<bar3DChart>") {
			t.Errorf("%s: chart part has no bar3DChart element", c.chartType)
		}
		if want := `<shape val="` + shape + `"`; !strings.Contains(chart, want) {
			t.Errorf("%s: chart part does not carry %s", c.chartType, want)
		}
		if want := `<barDir val="` + c.barDir + `"`; !strings.Contains(chart, want) {
			t.Errorf("%s: chart part does not carry %s", c.chartType, want)
		}
		if want := `<grouping val="` + c.grouping + `"`; !strings.Contains(chart, want) {
			t.Errorf("%s: chart part does not carry %s", c.chartType, want)
		}
	}
}

func TestConeChartFamily(t *testing.T) {
	assertShapedCharts(t, "cone", []shapedChart{
		{seedcalc.Col3DCone, "col", "standard"},
		{seedcalc.Col3DConeClustered, "col", "clustered"},
		{seedcalc.Col3DConeStacked, "col", "stacked"},
		{seedcalc.Col3DConePercentStacked, "col", "percentStacked"},
		{seedcalc.Bar3DConeClustered, "bar", "clustered"},
		{seedcalc.Bar3DConeStacked, "bar", "stacked"},
		{seedcalc.Bar3DConePercentStacked, "bar", "percentStacked"},
	})
}

func TestPyramidChartFamily(t *testing.T) {
	assertShapedCharts(t, "pyramid", []shapedChart{
		{seedcalc.Col3DPyramid, "col", "standard"},
		{seedcalc.Col3DPyramidClustered, "col", "clustered"},
		{seedcalc.Col3DPyramidStacked, "col", "stacked"},
		{seedcalc.Col3DPyramidPercentStacked, "col", "percentStacked"},
		{seedcalc.Bar3DPyramidClustered, "bar", "clustered"},
		{seedcalc.Bar3DPyramidStacked, "bar", "stacked"},
		{seedcalc.Bar3DPyramidPercentStacked, "bar", "percentStacked"},
	})
}

func TestCylinderChartFamily(t *testing.T) {
	assertShapedCharts(t, "cylinder", []shapedChart{
		{seedcalc.Col3DCylinder, "col", "standard"},
		{seedcalc.Col3DCylinderClustered, "col", "clustered"},
		{seedcalc.Col3DCylinderStacked, "col", "stacked"},
		{seedcalc.Col3DCylinderPercentStacked, "col", "percentStacked"},
		{seedcalc.Bar3DCylinderClustered, "bar", "clustered"},
		{seedcalc.Bar3DCylinderStacked, "bar", "stacked"},
		{seedcalc.Bar3DCylinderPercentStacked, "bar", "percentStacked"},
	})
}

func TestSurfaceAndContourCharts(t *testing.T) {
	cases := []struct {
		chartType string
		element   string
		wireframe bool
	}{
		{seedcalc.Surface3D, "<surface3DChart>", false},
		{seedcalc.WireframeSurface3D, "<surface3DChart>", true},
		{seedcalc.Contour, "<surfaceChart>", false},
		{seedcalc.WireframeContour, "<surfaceChart>", true},
	}
	for _, c := range cases {
		chart := addChart(t, c.chartType)
		if !strings.Contains(chart, c.element) {
			t.Errorf("%s: chart part has no %s element", c.chartType, c.element)
		}
		hasWireframe := strings.Contains(chart, `<wireframe val="true"`)
		if hasWireframe != c.wireframe {
			t.Errorf("%s: wireframe=true present = %v, want %v", c.chartType, hasWireframe, c.wireframe)
		}
	}
	// A 3-D surface and a flat contour are different elements.
	if strings.Contains(addChart(t, seedcalc.Contour), "<surface3DChart>") {
		t.Errorf("%s must not produce a surface3DChart element", seedcalc.Contour)
	}
}

func TestBubbleCharts(t *testing.T) {
	plain := addChart(t, seedcalc.Bubble)
	if !strings.Contains(plain, "<bubbleChart>") {
		t.Errorf("%s: chart part has no bubbleChart element", seedcalc.Bubble)
	}
	if strings.Contains(plain, `<bubble3D val="true"`) {
		t.Errorf("%s must not mark its series as three-dimensional", seedcalc.Bubble)
	}

	threeD := addChart(t, seedcalc.Bubble3D)
	if !strings.Contains(threeD, "<bubbleChart>") {
		t.Errorf("%s: chart part has no bubbleChart element", seedcalc.Bubble3D)
	}
	if !strings.Contains(threeD, `<bubble3D val="true"`) {
		t.Errorf("%s must mark its series as three-dimensional", seedcalc.Bubble3D)
	}
}

func TestAddChartRejectsUnsupportedType(t *testing.T) {
	series := `"series":[{"name":"Sheet1!$A$2","categories":"Sheet1!$B$1:$D$1","values":"Sheet1!$B$2:$D$2"}]`

	f := chartWorkbook(t)
	if err := f.AddChart("Sheet1", "F1", `{"type":"nosuchchart",`+series+`}`); err == nil {
		t.Error("AddChart with an unknown chart type returned nil, want a non-nil error")
	}
	if err := f.AddChart("Sheet1", "F1", `{`+series+`}`); err == nil {
		t.Error("AddChart with no chart type returned nil, want a non-nil error")
	}
	// The rejected charts left no chart part behind.
	if _, ok := packageParts(t, f)["xl/charts/chart1.xml"]; ok {
		t.Error("a rejected AddChart still produced a chart part")
	}
	// A supported type on the same workbook still works.
	if err := f.AddChart("Sheet1", "F1", `{"type":"`+seedcalc.Col3DCone+`",`+series+`}`); err != nil {
		t.Errorf("AddChart with a supported type = %v, want nil", err)
	}
	// Unknown worksheet and malformed cell reference are errors too.
	if err := f.AddChart("NoSuchSheet", "F1", `{"type":"`+seedcalc.Bubble+`",`+series+`}`); err == nil {
		t.Error("AddChart on an unknown worksheet returned nil, want a non-nil error")
	}
	if err := f.AddChart("Sheet1", "", `{"type":"`+seedcalc.Bubble+`",`+series+`}`); err == nil {
		t.Error("AddChart with an empty cell reference returned nil, want a non-nil error")
	}
}

func TestSetColStyle(t *testing.T) {
	f := seedcalc.NewFile()
	style, err := f.NewStyle(`{"font":{"bold":true}}`)
	if err != nil {
		t.Fatalf("NewStyle: %v", err)
	}

	if err := f.SetColStyle("Sheet1", "C:F", style); err != nil {
		t.Fatalf("SetColStyle(\"C:F\"): %v", err)
	}
	if err := f.SetColStyle("Sheet1", "B", style); err != nil {
		t.Fatalf("SetColStyle(\"B\"): %v", err)
	}
	// A reversed range names the same columns and updates the same entry.
	if err := f.SetColStyle("Sheet1", "F:C", style); err != nil {
		t.Fatalf("SetColStyle(\"F:C\"): %v", err)
	}

	sheet := packageParts(t, f)["xl/worksheets/sheet1.xml"]
	if n := strings.Count(sheet, "<col "); n != 2 {
		t.Errorf("the worksheet declares %d col elements, want 2 (C:F and B)", n)
	}
	for _, want := range []string{`min="3"`, `max="6"`, `min="2"`, `max="2"`} {
		if !strings.Contains(sheet, want) {
			t.Errorf("the worksheet's col elements do not carry %s", want)
		}
	}
	if !strings.Contains(sheet, `style="`) {
		t.Error("the worksheet's col elements carry no style attribute")
	}

	// A cell written into a styled column picks the column style up.
	if err := f.SetCellValue("Sheet1", "C1", "x"); err != nil {
		t.Fatalf("SetCellValue: %v", err)
	}
	if got, err := f.GetCellStyle("Sheet1", "C1"); err != nil || got != style {
		t.Errorf("GetCellStyle(C1) = (%d, %v), want (%d, nil)", got, err, style)
	}
	// A cell outside the styled columns keeps the default style.
	if err := f.SetCellValue("Sheet1", "A1", "x"); err != nil {
		t.Fatalf("SetCellValue: %v", err)
	}
	if got, err := f.GetCellStyle("Sheet1", "A1"); err != nil || got != 0 {
		t.Errorf("GetCellStyle(A1) = (%d, %v), want (0, nil)", got, err)
	}

	// A malformed column name is rejected on either side of the range.
	for _, columns := range []string{"-", "1", "", "C:-", "-:F", "C:1"} {
		if err := f.SetColStyle("Sheet1", columns, style); err == nil {
			t.Errorf("SetColStyle(%q) returned nil, want a non-nil error", columns)
		}
	}
}

func TestTIFFImagesAreAccepted(t *testing.T) {
	// The library decodes the image to learn its size; register a decoder
	// for the TIFF signature so the format is available to the test.
	registerTIFF.Do(func() {
		image.RegisterFormat("tiff", tiffMagic,
			func(r io.Reader) (image.Image, error) {
				return image.NewRGBA(image.Rect(0, 0, 32, 32)), nil
			},
			func(r io.Reader) (image.Config, error) {
				return image.Config{ColorModel: image.NewRGBA(image.Rect(0, 0, 1, 1)).ColorModel(), Width: 32, Height: 32}, nil
			})
	})
	data := append([]byte(tiffMagic), make([]byte, 96)...)

	for _, extension := range []string{".tif", ".tiff"} {
		f := seedcalc.NewFile()
		if err := f.AddPictureFromBytes("Sheet1", "A1", "", "tiff picture", extension, data); err != nil {
			t.Fatalf("AddPictureFromBytes(%q): %v", extension, err)
		}
		parts := packageParts(t, f)
		// Both spellings are stored under the canonical .tiff extension.
		if _, ok := parts["xl/media/image1.tiff"]; !ok {
			t.Errorf("%s: the package has no xl/media/image1.tiff part", extension)
		}
		if ct := parts["[Content_Types].xml"]; !strings.Contains(ct, `Extension="tiff"`) {
			t.Errorf("%s: [Content_Types].xml declares no default for the tiff extension", extension)
		}
		name, raw, err := f.GetPicture("Sheet1", "A1")
		if err != nil {
			t.Fatalf("%s: GetPicture: %v", extension, err)
		}
		if name != "image1.tiff" {
			t.Errorf("%s: GetPicture name = %q, want %q", extension, name, "image1.tiff")
		}
		if !bytes.Equal(raw, data) {
			t.Errorf("%s: GetPicture returned %d bytes, want the %d bytes that were added", extension, len(raw), len(data))
		}
	}

	// An extension outside the supported set is still rejected.
	f := seedcalc.NewFile()
	if err := f.AddPictureFromBytes("Sheet1", "A1", "", "picture", ".bmp", data); err == nil {
		t.Error("AddPictureFromBytes with a .bmp extension returned nil, want a non-nil error")
	}
}
