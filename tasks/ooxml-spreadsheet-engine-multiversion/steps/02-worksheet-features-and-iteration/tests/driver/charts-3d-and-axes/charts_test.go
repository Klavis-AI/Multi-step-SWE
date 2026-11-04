// Scored group: charts-3d-and-axes.
//
// Chart families and axis scaling. Graded against ECMA-376 Part 1 clause
// 21.2.2.16 (barChart), clause 21.2.2.15 (bar3DChart), clause 21.2.2.5
// (areaChart), clause 21.2.2.4 (area3DChart), clause 21.2.2.17 (barDir),
// clause 21.2.2.77 (grouping, the bar grouping: clustered, stacked,
// percentStacked or standard), clause 21.2.2.130 (orientation, whose value is
// minMax or maxMin), clause 21.2.2.160 (scaling), clause 21.2.2.107 (max) and
// clause 21.2.2.108 (min). Behaviour cases were checked against the upstream
// release this checkpoint tracks (tag v1.4.1).
package charts3dandaxes

import (
	"archive/zip"
	"bytes"
	"io"
	"strings"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

func chartPart(t *testing.T, chartType, extra string) string {
	t.Helper()
	f := seedcalc.NewFile()
	f.SetCellValue("Sheet1", "A1", 1)
	f.SetCellValue("Sheet1", "A2", 2)
	f.SetCellValue("Sheet1", "A3", 3)
	format := `{"type":"` + chartType + `","series":[{"name":"Sheet1!$A$1",` +
		`"categories":"Sheet1!$A$1:$A$3","values":"Sheet1!$A$1:$A$3"}]` + extra + `}`
	if err := f.AddChart("Sheet1", "E1", format); err != nil {
		t.Fatalf("AddChart(%s): %v", chartType, err)
	}
	buf, err := f.WriteToBuffer()
	if err != nil {
		t.Fatalf("WriteToBuffer: %v", err)
	}
	b := buf.Bytes()
	zr, err := zip.NewReader(bytes.NewReader(b), int64(len(b)))
	if err != nil {
		t.Fatalf("written package is not a ZIP container: %v", err)
	}
	const name = "xl/charts/chart1.xml"
	for _, fh := range zr.File {
		if fh.Name != name {
			continue
		}
		rc, err := fh.Open()
		if err != nil {
			t.Fatalf("open %s: %v", name, err)
		}
		defer rc.Close()
		var out bytes.Buffer
		if _, err := io.Copy(&out, rc); err != nil {
			t.Fatalf("read %s: %v", name, err)
		}
		return out.String()
	}
	t.Fatalf("a chart was added but the package has no part %s", name)
	return ""
}

func plotElement(t *testing.T, xml string) string {
	t.Helper()
	var found []string
	for _, el := range []string{"bar3DChart", "barChart", "area3DChart", "areaChart"} {
		if strings.Contains(xml, "<c:"+el+">") {
			found = append(found, el)
		}
	}
	if len(found) != 1 {
		t.Fatalf("chart uses %v plot elements, want exactly one", found)
	}
	return found[0]
}

func grouping(t *testing.T, xml string) string {
	t.Helper()
	const open = `<c:grouping val="`
	i := strings.Index(xml, open)
	if i < 0 {
		t.Fatal("chart carries no grouping element")
	}
	rest := xml[i+len(open):]
	j := strings.Index(rest, `"`)
	if j < 0 {
		t.Fatal("grouping element has no value")
	}
	return rest[:j]
}

func TestThreeDimensionalChartsUseTheThreeDPlotElement(t *testing.T) {
	// The exported chart-type constants spell exactly the "type" string the
	// table above requires -- named, not just used as literals elsewhere in
	// this file.
	for _, tc := range []struct{ constant, want string }{
		{seedcalc.Bar, "bar"},
		{seedcalc.BarStacked, "barStacked"},
		{seedcalc.BarPercentStacked, "barPercentStacked"},
		{seedcalc.Col, "col"},
		{seedcalc.ColStacked, "colStacked"},
		{seedcalc.ColPercentStacked, "colPercentStacked"},
		{seedcalc.Bar3DClustered, "bar3DClustered"},
		{seedcalc.Bar3DStacked, "bar3DStacked"},
		{seedcalc.Bar3DPercentStacked, "bar3DPercentStacked"},
		{seedcalc.Col3DClustered, "col3DClustered"},
		{seedcalc.Col3DStacked, "col3DStacked"},
		{seedcalc.Col3DPercentStacked, "col3DPercentStacked"},
		{seedcalc.Area, "area"},
		{seedcalc.AreaStacked, "areaStacked"},
		{seedcalc.AreaPercentStacked, "areaPercentStacked"},
		{seedcalc.Area3DStacked, "area3DStacked"},
		{seedcalc.Area3DPercentStacked, "area3DPercentStacked"},
	} {
		if tc.constant != tc.want {
			t.Errorf("chart-type constant = %q, want %q", tc.constant, tc.want)
		}
	}

	for _, tc := range []struct{ chartType, want string }{
		{"bar3DClustered", "bar3DChart"},
		{"bar3DStacked", "bar3DChart"},
		{"col3DClustered", "bar3DChart"},
		{"col3DPercentStacked", "bar3DChart"},
		{"area3DStacked", "area3DChart"},
		{"barStacked", "barChart"},
		{"colStacked", "barChart"},
		{"areaStacked", "areaChart"},
	} {
		if got := plotElement(t, chartPart(t, tc.chartType, "")); got != tc.want {
			t.Errorf("chart type %q plots into <c:%s>, want <c:%s>", tc.chartType, got, tc.want)
		}
	}
}

func TestGroupingReflectsStackedAndPercentStackedTypes(t *testing.T) {
	for _, tc := range []struct{ chartType, want string }{
		{"bar", "clustered"},
		{"barStacked", "stacked"},
		{"barPercentStacked", "percentStacked"},
		{"colStacked", "stacked"},
		{"colPercentStacked", "percentStacked"},
		{"bar3DStacked", "stacked"},
		{"bar3DPercentStacked", "percentStacked"},
		{"area", "standard"},
		{"areaStacked", "stacked"},
		{"areaPercentStacked", "percentStacked"},
		{"area3DPercentStacked", "percentStacked"},
	} {
		if got := grouping(t, chartPart(t, tc.chartType, "")); got != tc.want {
			t.Errorf("chart type %q has grouping %q, want %q", tc.chartType, got, tc.want)
		}
	}
}

func TestBarAndColumnChartsDifferOnlyInDirection(t *testing.T) {
	for _, tc := range []struct{ chartType, want string }{
		{"barStacked", "bar"},
		{"bar3DStacked", "bar"},
		{"colStacked", "col"},
		{"col3DStacked", "col"},
	} {
		x := chartPart(t, tc.chartType, "")
		if !strings.Contains(x, `<c:barDir val="`+tc.want+`">`) {
			t.Errorf("chart type %q does not declare barDir %q", tc.chartType, tc.want)
		}
	}
	if x := chartPart(t, "areaStacked", ""); strings.Contains(x, "<c:barDir") {
		t.Error("an area chart declares a bar direction")
	}
}

func TestAxisOrientationIsReversedOnRequest(t *testing.T) {
	plain := chartPart(t, "col", "")
	if strings.Contains(plain, `<c:orientation val="maxMin">`) {
		t.Error("an axis is reversed without being asked to be")
	}
	if !strings.Contains(plain, `<c:orientation val="minMax">`) {
		t.Error("the default axis orientation is not minMax")
	}
	if !strings.Contains(plain, `<c:axPos val="b">`) {
		t.Error("the category axis of an unreversed chart is not at the bottom")
	}
	if !strings.Contains(plain, `<c:axPos val="l">`) {
		t.Error("the value axis of an unreversed chart is not on the left")
	}

	reversed := chartPart(t, "col", `,"x_axis":{"reverse_order":true},"y_axis":{"reverse_order":true}`)
	if strings.Contains(reversed, `<c:orientation val="minMax">`) {
		t.Error("an axis is still minMax after both axes were reversed")
	}
	if !strings.Contains(reversed, `<c:orientation val="maxMin">`) {
		t.Error("reversing an axis did not produce a maxMin orientation")
	}
	if !strings.Contains(reversed, `<c:axPos val="t">`) {
		t.Error("a reversed category axis is not moved to the top")
	}
	if !strings.Contains(reversed, `<c:axPos val="r">`) {
		t.Error("a reversed value axis is not moved to the right")
	}
}

func TestAxisMaximumAndMinimumAreWrittenWhenSet(t *testing.T) {
	plain := chartPart(t, "col", "")
	if strings.Contains(plain, "<c:max ") {
		t.Error("a chart with no axis bounds carries a max element")
	}
	if strings.Contains(plain, "<c:min ") {
		t.Error("a chart with no axis bounds carries a min element")
	}

	bounded := chartPart(t, "col", `,"y_axis":{"maximum":7.5,"minimum":0.5}`)
	if !strings.Contains(bounded, `<c:max val="7.5">`) {
		t.Error("the axis maximum was not written")
	}
	if !strings.Contains(bounded, `<c:min val="0.5">`) {
		t.Error("the axis minimum was not written")
	}
}
