// Scored group: drawings-and-charts.
//
// Pictures, shapes and charts all reach a worksheet through the DrawingML
// worksheet drawing of ECMA-376 Part 1 clause 20.5: the drawing part holds the
// anchors, embedded media lives under xl/media, and a chart is a separate part
// (clause 21.2) that the drawing reaches through a relationship. The image
// decoders the library needs are registered by blank import, as the standard
// library requires.
package drawingsandcharts

import (
	"archive/zip"
	"bytes"
	"encoding/xml"
	"os"
	"path/filepath"
	"strings"
	"testing"

	_ "image/gif"
	_ "image/jpeg"
	_ "image/png"

	seedcalc "__SEEDCALC_MODULE__"
)

const relNS = "http://schemas.openxmlformats.org/officeDocument/2006/relationships"

func swatch(t *testing.T) string {
	t.Helper()
	dir := os.Getenv("SEEDCALC_ASSETS")
	if dir == "" {
		t.Fatal("SEEDCALC_ASSETS is not set")
	}
	p := filepath.Join(dir, "swatch.png")
	if _, err := os.Stat(p); err != nil {
		t.Fatalf("fixture image: %v", err)
	}
	return p
}

func packageParts(t *testing.T, f *seedcalc.File) map[string][]byte {
	t.Helper()
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	zr, err := zip.NewReader(bytes.NewReader(buf.Bytes()), int64(buf.Len()))
	if err != nil {
		t.Fatalf("written package is not a ZIP container: %v", err)
	}
	out := map[string][]byte{}
	for _, e := range zr.File {
		rc, err := e.Open()
		if err != nil {
			t.Fatalf("open part %s: %v", e.Name, err)
		}
		var b bytes.Buffer
		if _, err := b.ReadFrom(rc); err != nil {
			t.Fatalf("read part %s: %v", e.Name, err)
		}
		rc.Close()
		out[e.Name] = b.Bytes()
	}
	return out
}

func part(t *testing.T, parts map[string][]byte, name string) []byte {
	t.Helper()
	b, ok := parts[name]
	if !ok {
		names := make([]string, 0, len(parts))
		for n := range parts {
			names = append(names, n)
		}
		t.Fatalf("package has no part %s; it has %v", name, names)
	}
	return b
}

type relationships struct {
	Rel []struct {
		ID     string `xml:"Id,attr"`
		Type   string `xml:"Type,attr"`
		Target string `xml:"Target,attr"`
	} `xml:"Relationship"`
}

// element records only the presence and count of an XML element.
type element []struct {
	XMLName xml.Name
}

type drawingPart struct {
	OneCellAnchor []struct {
		Pic           element `xml:"pic"`
		Sp            element `xml:"sp"`
		GraphicFrame  element `xml:"graphicFrame"`
		ClientData    element `xml:"clientData"`
		ExtentPresent element `xml:"ext"`
	} `xml:"oneCellAnchor"`
	TwoCellAnchor []struct {
		Pic          element `xml:"pic"`
		Sp           element `xml:"sp"`
		GraphicFrame element `xml:"graphicFrame"`
	} `xml:"twoCellAnchor"`
}

func (d drawingPart) count(pick func(pic, sp, frame element) element) int {
	n := 0
	for _, a := range d.OneCellAnchor {
		n += len(pick(a.Pic, a.Sp, a.GraphicFrame))
	}
	for _, a := range d.TwoCellAnchor {
		n += len(pick(a.Pic, a.Sp, a.GraphicFrame))
	}
	return n
}

func pics(pic, _, _ element) element  { return pic }
func shapes(_, sp, _ element) element { return sp }
func frames(_, _, fr element) element { return fr }

func TestAddPictureStoresTheImageAsAMediaPart(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.AddPicture("Sheet1", "B2", swatch(t), ""); err != nil {
		t.Fatalf("AddPicture: %v", err)
	}
	parts := packageParts(t, f)

	media := part(t, parts, "xl/media/image1.png")
	onDisk, err := os.ReadFile(swatch(t))
	if err != nil {
		t.Fatalf("read fixture: %v", err)
	}
	if !bytes.Equal(media, onDisk) {
		t.Errorf("the stored media part is %d bytes, the source image is %d", len(media), len(onDisk))
	}

	var d drawingPart
	if err := xml.Unmarshal(part(t, parts, "xl/drawings/drawing1.xml"), &d); err != nil {
		t.Fatalf("the drawing part is not well-formed XML: %v", err)
	}
	if got := d.count(pics); got != 1 {
		t.Errorf("the drawing holds %d picture anchors, want 1", got)
	}

	var rels relationships
	if err := xml.Unmarshal(part(t, parts, "xl/drawings/_rels/drawing1.xml.rels"), &rels); err != nil {
		t.Fatalf("the drawing relationship part is not well-formed XML: %v", err)
	}
	found := false
	for _, r := range rels.Rel {
		if r.Type == relNS+"/image" {
			found = true
			if !strings.HasSuffix(r.Target, "media/image1.png") {
				t.Errorf("the image relationship targets %q, want the image1.png media part", r.Target)
			}
		}
	}
	if !found {
		t.Errorf("the drawing declares no image relationship (%d relationships present)", len(rels.Rel))
	}
}

func TestGetPictureFindsTheImageAnchoredAtACell(t *testing.T) {
	f := seedcalc.NewFile()
	src := swatch(t)
	if err := f.AddPicture("Sheet1", "B2", src, ""); err != nil {
		t.Fatalf("AddPicture: %v", err)
	}
	onDisk, err := os.ReadFile(src)
	if err != nil {
		t.Fatalf("read fixture: %v", err)
	}

	name, raw := f.GetPicture("Sheet1", "B2")
	if name == "" {
		t.Fatal("GetPicture found no picture at the cell the picture was anchored to")
	}
	if !strings.HasSuffix(name, ".png") {
		t.Errorf("GetPicture returned name %q, want the stored media file name", name)
	}
	if !bytes.Equal(raw, onDisk) {
		t.Errorf("GetPicture returned %d bytes, the source image is %d", len(raw), len(onDisk))
	}

	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	g, err := seedcalc.OpenReader(bytes.NewReader(buf.Bytes()))
	if err != nil {
		t.Fatalf("OpenReader: %v", err)
	}
	name2, raw2 := g.GetPicture("Sheet1", "B2")
	if name2 != name || !bytes.Equal(raw2, raw) {
		t.Errorf("after the round trip GetPicture returned (%q, %d bytes), want (%q, %d bytes)", name2, len(raw2), name, len(raw))
	}
	if n, b := g.GetPicture("Sheet1", "H9"); n != "" || len(b) != 0 {
		t.Errorf("GetPicture at a cell with no picture returned (%q, %d bytes), want an empty result", n, len(b))
	}
}

func TestAddPictureRejectsInputItCannotUse(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.AddPicture("Sheet1", "A1", filepath.Join(t.TempDir(), "absent.png"), ""); err == nil {
		t.Error("AddPicture accepted a path that does not exist")
	}
	notAnImage := filepath.Join(t.TempDir(), "notes.txt")
	if err := os.WriteFile(notAnImage, []byte("plain text"), 0o644); err != nil {
		t.Fatalf("write temporary file: %v", err)
	}
	if err := f.AddPicture("Sheet1", "A1", notAnImage, ""); err == nil {
		t.Error("AddPicture accepted a file whose extension is not a supported image type")
	}
	if _, ok := packageParts(t, f)["xl/media/image1.txt"]; ok {
		t.Error("the rejected file was stored as a media part anyway")
	}
}

func TestSetSheetBackgroundStoresTheImageAndPointsTheWorksheetAtIt(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetSheetBackground("Sheet1", swatch(t)); err != nil {
		t.Fatalf("SetSheetBackground: %v", err)
	}
	parts := packageParts(t, f)
	if _, ok := parts["xl/media/image1.png"]; !ok {
		t.Error("the background image was not stored as a media part")
	}

	// Input it cannot use is reported rather than stored.
	if err := f.SetSheetBackground("Sheet1", filepath.Join(t.TempDir(), "absent.png")); err == nil {
		t.Error("SetSheetBackground accepted a path that does not exist")
	}

	var ws struct {
		Picture struct {
			ID string `xml:"http://schemas.openxmlformats.org/officeDocument/2006/relationships id,attr"`
		} `xml:"picture"`
	}
	if err := xml.Unmarshal(part(t, parts, "xl/worksheets/sheet1.xml"), &ws); err != nil {
		t.Fatalf("the worksheet part is not well-formed XML: %v", err)
	}
	if ws.Picture.ID == "" {
		t.Fatal("the worksheet declares no background picture relationship")
	}
	var rels relationships
	if err := xml.Unmarshal(part(t, parts, "xl/worksheets/_rels/sheet1.xml.rels"), &rels); err != nil {
		t.Fatalf("the worksheet relationship part is not well-formed XML: %v", err)
	}
	for _, r := range rels.Rel {
		if r.ID == ws.Picture.ID {
			if r.Type != relNS+"/image" {
				t.Errorf("the background relationship has type %q, want an image relationship", r.Type)
			}
			if !strings.HasSuffix(r.Target, "media/image1.png") {
				t.Errorf("the background relationship targets %q, want the stored media part", r.Target)
			}
			return
		}
	}
	t.Errorf("the picture id %q resolves to no relationship (%d relationships present)", ws.Picture.ID, len(rels.Rel))
}

func TestAddShapeWritesAShapeIntoTheDrawingPart(t *testing.T) {
	f := seedcalc.NewFile()
	f.AddShape("Sheet1", "C3", `{"type":"rect","color":{"line":"#4286F4","fill":"#8EB9FF"},"paragraph":[{"text":"Note"}],"width":180,"height":90}`)
	parts := packageParts(t, f)

	var d drawingPart
	if err := xml.Unmarshal(part(t, parts, "xl/drawings/drawing1.xml"), &d); err != nil {
		t.Fatalf("the drawing part is not well-formed XML: %v", err)
	}
	if got := d.count(shapes); got != 1 {
		t.Errorf("the drawing holds %d shape anchors, want 1", got)
	}
	if got := d.count(pics); got != 0 {
		t.Errorf("the drawing holds %d picture anchors, want 0", got)
	}

	var ws struct {
		Drawing struct {
			ID string `xml:"http://schemas.openxmlformats.org/officeDocument/2006/relationships id,attr"`
		} `xml:"drawing"`
	}
	if err := xml.Unmarshal(part(t, parts, "xl/worksheets/sheet1.xml"), &ws); err != nil {
		t.Fatalf("the worksheet part is not well-formed XML: %v", err)
	}
	if ws.Drawing.ID == "" {
		t.Error("the worksheet declares no drawing relationship for the shape it carries")
	}
	var ct struct {
		Overrides []struct {
			PartName    string `xml:"PartName,attr"`
			ContentType string `xml:"ContentType,attr"`
		} `xml:"Override"`
	}
	if err := xml.Unmarshal(part(t, parts, "[Content_Types].xml"), &ct); err != nil {
		t.Fatalf("[Content_Types].xml is not well-formed XML: %v", err)
	}
	for _, o := range ct.Overrides {
		if o.PartName == "/xl/drawings/drawing1.xml" {
			if o.ContentType != "application/vnd.openxmlformats-officedocument.drawing+xml" {
				t.Errorf("Override for the drawing part has content type %q", o.ContentType)
			}
			return
		}
	}
	t.Error("[Content_Types].xml has no Override for /xl/drawings/drawing1.xml")
}

type chartSpace struct {
	Chart struct {
		PlotArea struct {
			BarChart      element `xml:"barChart"`
			Bar3DChart    element `xml:"bar3DChart"`
			LineChart     element `xml:"lineChart"`
			PieChart      element `xml:"pieChart"`
			Pie3DChart    element `xml:"pie3DChart"`
			DoughnutChart element `xml:"doughnutChart"`
			RadarChart    element `xml:"radarChart"`
			ScatterChart  element `xml:"scatterChart"`
			CatAx         element `xml:"catAx"`
			ValAx         element `xml:"valAx"`
		} `xml:"plotArea"`
	} `xml:"chart"`
}

func seedChartData(f *seedcalc.File) {
	f.SetCellValue("Sheet1", "A1", "Region")
	f.SetCellValue("Sheet1", "B1", "Q1")
	f.SetCellValue("Sheet1", "C1", "Q2")
	f.SetCellValue("Sheet1", "A2", "North")
	f.SetCellValue("Sheet1", "B2", 12)
	f.SetCellValue("Sheet1", "C2", 19)
}

const chartSeries = `"series":[{"name":"Sheet1!$A$2","categories":"Sheet1!$B$1:$C$1","values":"Sheet1!$B$2:$C$2"}]`

func TestEachChartFamilyWritesItsOwnPlotElement(t *testing.T) {
	cases := []struct {
		kind    string
		plot    func(chartSpace) element
		hasAxes bool
	}{
		{seedcalc.Bar, func(c chartSpace) element { return c.Chart.PlotArea.BarChart }, true},
		{seedcalc.Bar3D, func(c chartSpace) element { return c.Chart.PlotArea.Bar3DChart }, true},
		{seedcalc.Line, func(c chartSpace) element { return c.Chart.PlotArea.LineChart }, true},
		{seedcalc.Radar, func(c chartSpace) element { return c.Chart.PlotArea.RadarChart }, true},
		{seedcalc.Scatter, func(c chartSpace) element { return c.Chart.PlotArea.ScatterChart }, true},
		{seedcalc.Pie, func(c chartSpace) element { return c.Chart.PlotArea.PieChart }, false},
		{seedcalc.Pie3D, func(c chartSpace) element { return c.Chart.PlotArea.Pie3DChart }, false},
		{seedcalc.Doughnut, func(c chartSpace) element { return c.Chart.PlotArea.DoughnutChart }, false},
	}
	for _, tc := range cases {
		f := seedcalc.NewFile()
		seedChartData(f)
		f.AddChart("Sheet1", "E1", `{"type":"`+tc.kind+`",`+chartSeries+`}`)

		var c chartSpace
		if err := xml.Unmarshal(part(t, packageParts(t, f), "xl/charts/chart1.xml"), &c); err != nil {
			t.Fatalf("%s: the chart part is not well-formed XML: %v", tc.kind, err)
		}
		if len(tc.plot(c)) != 1 {
			t.Errorf("a %q chart wrote %d plot elements of its own kind, want 1", tc.kind, len(tc.plot(c)))
			continue
		}
		axes := len(c.Chart.PlotArea.CatAx) + len(c.Chart.PlotArea.ValAx)
		if tc.hasAxes && axes == 0 {
			t.Errorf("a %q chart declares no axes", tc.kind)
		}
		if !tc.hasAxes && axes != 0 {
			t.Errorf("a %q chart declares %d axes, want none", tc.kind, axes)
		}
	}
}

func TestAChartIsAPartTheDrawingRelatesTo(t *testing.T) {
	f := seedcalc.NewFile()
	seedChartData(f)
	f.AddChart("Sheet1", "E1", `{"type":"`+seedcalc.Bar+`",`+chartSeries+`}`)
	parts := packageParts(t, f)

	var d drawingPart
	if err := xml.Unmarshal(part(t, parts, "xl/drawings/drawing1.xml"), &d); err != nil {
		t.Fatalf("the drawing part is not well-formed XML: %v", err)
	}
	if got := d.count(frames); got != 1 {
		t.Errorf("the drawing holds %d graphic frames, want 1", got)
	}

	var rels relationships
	if err := xml.Unmarshal(part(t, parts, "xl/drawings/_rels/drawing1.xml.rels"), &rels); err != nil {
		t.Fatalf("the drawing relationship part is not well-formed XML: %v", err)
	}
	found := false
	for _, r := range rels.Rel {
		if r.Type == relNS+"/chart" {
			found = true
			if !strings.HasSuffix(r.Target, "charts/chart1.xml") {
				t.Errorf("the chart relationship targets %q, want the chart1.xml part", r.Target)
			}
		}
	}
	if !found {
		t.Errorf("the drawing declares no chart relationship (%d relationships present)", len(rels.Rel))
	}

	var ct struct {
		Overrides []struct {
			PartName    string `xml:"PartName,attr"`
			ContentType string `xml:"ContentType,attr"`
		} `xml:"Override"`
	}
	if err := xml.Unmarshal(part(t, parts, "[Content_Types].xml"), &ct); err != nil {
		t.Fatalf("[Content_Types].xml is not well-formed XML: %v", err)
	}
	for _, o := range ct.Overrides {
		if o.PartName == "/xl/charts/chart1.xml" {
			if o.ContentType != "application/vnd.openxmlformats-officedocument.drawingml.chart+xml" {
				t.Errorf("Override for the chart part has content type %q", o.ContentType)
			}
			return
		}
	}
	t.Error("[Content_Types].xml has no Override for /xl/charts/chart1.xml")
}

func TestSeveralChartsBecomeSeparatelyNumberedParts(t *testing.T) {
	f := seedcalc.NewFile()
	seedChartData(f)
	f.AddChart("Sheet1", "E1", `{"type":"`+seedcalc.Bar+`",`+chartSeries+`}`)
	f.AddChart("Sheet1", "E20", `{"type":"`+seedcalc.Line+`",`+chartSeries+`}`)
	parts := packageParts(t, f)

	var first, second chartSpace
	if err := xml.Unmarshal(part(t, parts, "xl/charts/chart1.xml"), &first); err != nil {
		t.Fatalf("chart1 is not well-formed XML: %v", err)
	}
	if err := xml.Unmarshal(part(t, parts, "xl/charts/chart2.xml"), &second); err != nil {
		t.Fatalf("chart2 is not well-formed XML: %v", err)
	}
	if len(first.Chart.PlotArea.BarChart) != 1 {
		t.Errorf("the first chart part is not the bar chart that was added first")
	}
	if len(second.Chart.PlotArea.LineChart) != 1 {
		t.Errorf("the second chart part is not the line chart that was added second")
	}

	var d drawingPart
	if err := xml.Unmarshal(part(t, parts, "xl/drawings/drawing1.xml"), &d); err != nil {
		t.Fatalf("the drawing part is not well-formed XML: %v", err)
	}
	if got := d.count(frames); got != 2 {
		t.Errorf("the drawing holds %d graphic frames, want 2", got)
	}

	var rels relationships
	if err := xml.Unmarshal(part(t, parts, "xl/drawings/_rels/drawing1.xml.rels"), &rels); err != nil {
		t.Fatalf("the drawing relationship part is not well-formed XML: %v", err)
	}
	targets := map[string]bool{}
	ids := map[string]bool{}
	for _, r := range rels.Rel {
		if r.Type == relNS+"/chart" {
			targets[r.Target] = true
			if ids[r.ID] {
				t.Errorf("relationship id %q is used twice", r.ID)
			}
			ids[r.ID] = true
		}
	}
	if len(targets) != 2 {
		t.Errorf("the drawing relates to %d distinct chart parts, want 2: %v", len(targets), targets)
	}
}
