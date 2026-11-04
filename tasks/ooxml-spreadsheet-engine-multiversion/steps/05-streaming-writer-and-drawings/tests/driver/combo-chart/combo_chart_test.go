// Scored group: combo-chart (milestone 05, "The streaming writer and
// the drawing registry").
//
// Normative sources:
//   * TEST_INTERFACE.md section 4.05 -- "Combo charts".
//   * chart.go AddChart v2.1.0: accepts variadic combo ...string so a
//     second chart format can be overlaid. The v2.0.x predecessor accepted
//     only (sheet, cell, format string) with no combo parameter.
//   * ECMA-376 Part 1, 21.2 DrawingML -- Charts for plotArea containing
//     multiple chart type sub-elements.
package combochart

import (
	"archive/zip"
	"bytes"
	"io/ioutil"
	"strings"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

func packageParts(t *testing.T, f *seedcalc.File) map[string]string {
	t.Helper()
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	zr, err := zip.NewReader(bytes.NewReader(buf.Bytes()), int64(buf.Len()))
	if err != nil {
		t.Fatalf("zip.NewReader: %v", err)
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

const barFmt = `{"type":"bar","series":[{"name":"Sheet1!$A$1","categories":"Sheet1!$B$1:$D$1","values":"Sheet1!$B$2:$D$2"}],"title":{"name":"Sales"}}`
const lineFmt = `{"type":"line","series":[{"name":"Sheet1!$A$1","categories":"Sheet1!$B$1:$D$1","values":"Sheet1!$B$3:$D$3"}]}`

func setupData(t *testing.T) *seedcalc.File {
	t.Helper()
	f := seedcalc.NewFile()
	data := [][]interface{}{
		{"Category", "Q1", "Q2", "Q3"},
		{"Bars", 10, 20, 30},
		{"Line", 15, 25, 35},
	}
	for r, row := range data {
		for c, v := range row {
			axis, err := seedcalc.CoordinatesToCellName(c+1, r+1)
			if err != nil {
				t.Fatalf("CoordinatesToCellName: %v", err)
			}
			if err := f.SetCellValue("Sheet1", axis, v); err != nil {
				t.Fatalf("SetCellValue: %v", err)
			}
		}
	}
	return f
}

func TestAddChartWithNoComboProducesChartPart(t *testing.T) {
	f := setupData(t)
	if err := f.AddChart("Sheet1", "E1", barFmt); err != nil {
		t.Fatalf("AddChart: %v", err)
	}
	parts := packageParts(t, f)
	found := false
	for k := range parts {
		if strings.Contains(k, "xl/charts/chart") {
			found = true
			break
		}
	}
	if !found {
		t.Error("no chart part found after AddChart")
	}
}

func TestAddChartWithComboProducesSingleChartPart(t *testing.T) {
	f := setupData(t)
	if err := f.AddChart("Sheet1", "E1", barFmt, lineFmt); err != nil {
		t.Fatalf("AddChart with combo: %v", err)
	}
	parts := packageParts(t, f)
	chartCount := 0
	for k := range parts {
		if strings.Contains(k, "xl/charts/chart") && strings.HasSuffix(k, ".xml") {
			chartCount++
		}
	}
	if chartCount != 1 {
		t.Errorf("combo chart produced %d chart parts, want 1", chartCount)
	}
}

func TestAddChartComboXMLContainsBothChartTypes(t *testing.T) {
	f := setupData(t)
	if err := f.AddChart("Sheet1", "E1", barFmt, lineFmt); err != nil {
		t.Fatalf("AddChart with combo: %v", err)
	}
	parts := packageParts(t, f)
	for k, v := range parts {
		if strings.Contains(k, "xl/charts/chart") && strings.HasSuffix(k, ".xml") {
			if !strings.Contains(v, "barChart") {
				t.Errorf("combo chart XML is missing barChart\npart: %s", k)
			}
			if !strings.Contains(v, "lineChart") {
				t.Errorf("combo chart XML is missing lineChart\npart: %s", k)
			}
			return
		}
	}
	t.Error("no chart part found")
}
