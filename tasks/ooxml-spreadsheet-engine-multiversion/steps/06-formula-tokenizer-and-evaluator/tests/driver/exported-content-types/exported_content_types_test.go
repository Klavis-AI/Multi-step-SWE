// Scored group: exported-content-types.
//
// The twelve media-type constants and the three relationship-type constants
// this milestone exports, and the parts of a written package that carry them. A content type is the only thing in an OPC
// package that says what a part IS -- the part name is a hint and the
// relationship type is a role, but the consumer dispatches on the media type --
// so exporting them is what lets a caller reason about a package without
// hard-coding strings.
//
// The media-types (content-types) stream is ECMA-376 Part 2 section 7.2.3
// "Mapping media types to parts"; a Default maps a file extension (section
// 7.2.3.2.4) and an Override names one part (section 7.2.3.2.5). Relationship
// types are ECMA-376 Part 2 section 6.5.3 "Relationship markup". The
// SpreadsheetML media types themselves are ECMA-376 Part 1 section 12.3 "Part
// Summary", and the chart and drawing types are ECMA-376 Part 1 section 14.2
// "Part Summary" (DrawingML).
package exportedcontenttypes

import (
	"archive/zip"
	"bytes"
	"encoding/xml"
	"io"
	"strings"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// contentTypes mirrors the [Content_Types].xml stream.
type contentTypes struct {
	Defaults []struct {
		Extension   string `xml:"Extension,attr"`
		ContentType string `xml:"ContentType,attr"`
	} `xml:"Default"`
	Overrides []struct {
		PartName    string `xml:"PartName,attr"`
		ContentType string `xml:"ContentType,attr"`
	} `xml:"Override"`
}

// stream writes f and returns its content-types stream, indexed two ways:
// by lower-cased extension for the Defaults and by part name for the Overrides.
func stream(t *testing.T, f *seedcalc.File) (defaults, overrides map[string]string) {
	t.Helper()
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	zr, err := zip.NewReader(bytes.NewReader(buf.Bytes()), int64(buf.Len()))
	if err != nil {
		t.Fatalf("written package is not a readable ZIP container: %v", err)
	}
	var raw []byte
	for _, zf := range zr.File {
		if zf.Name != "[Content_Types].xml" {
			continue
		}
		rc, err := zf.Open()
		if err != nil {
			t.Fatalf("open [Content_Types].xml: %v", err)
		}
		raw, err = io.ReadAll(rc)
		rc.Close()
		if err != nil {
			t.Fatalf("read [Content_Types].xml: %v", err)
		}
	}
	if raw == nil {
		t.Fatal("written package has no [Content_Types].xml")
	}
	var ct contentTypes
	if err := xml.Unmarshal(raw, &ct); err != nil {
		t.Fatalf("[Content_Types].xml does not parse: %v", err)
	}
	defaults, overrides = map[string]string{}, map[string]string{}
	for _, d := range ct.Defaults {
		defaults[strings.ToLower(d.Extension)] = d.ContentType
	}
	for _, o := range ct.Overrides {
		overrides[o.PartName] = o.ContentType
	}
	return defaults, overrides
}

// relationships mirrors a .rels part.
type relationships struct {
	Relationship []struct {
		ID     string `xml:"Id,attr"`
		Type   string `xml:"Type,attr"`
		Target string `xml:"Target,attr"`
	} `xml:"Relationship"`
}

// workbookRels writes f and returns the relationships of xl/_rels/workbook.xml.rels
// keyed by Target.
func workbookRels(t *testing.T, f *seedcalc.File) map[string]string {
	t.Helper()
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	zr, err := zip.NewReader(bytes.NewReader(buf.Bytes()), int64(buf.Len()))
	if err != nil {
		t.Fatalf("written package is not a readable ZIP container: %v", err)
	}
	var raw []byte
	for _, zf := range zr.File {
		if zf.Name != "xl/_rels/workbook.xml.rels" {
			continue
		}
		rc, err := zf.Open()
		if err != nil {
			t.Fatalf("open xl/_rels/workbook.xml.rels: %v", err)
		}
		raw, err = io.ReadAll(rc)
		rc.Close()
		if err != nil {
			t.Fatalf("read xl/_rels/workbook.xml.rels: %v", err)
		}
	}
	if raw == nil {
		t.Fatal("written package has no xl/_rels/workbook.xml.rels")
	}
	var rels relationships
	if err := xml.Unmarshal(raw, &rels); err != nil {
		t.Fatalf("xl/_rels/workbook.xml.rels does not parse: %v", err)
	}
	byTarget := map[string]string{}
	for _, r := range rels.Relationship {
		byTarget[r.Target] = r.Type
	}
	return byTarget
}

func wantOverride(t *testing.T, overrides map[string]string, part, want string) {
	t.Helper()
	got, ok := overrides[part]
	if !ok {
		t.Fatalf("[Content_Types].xml has no Override for %s", part)
	}
	if got != want {
		t.Fatalf("content type of %s = %q, want %q", part, got, want)
	}
}

func TestTheExportedContentTypeConstantsHoldTheirMediaTypes(t *testing.T) {
	for _, tc := range []struct{ got, want, what string }{
		{seedcalc.ContentTypeSpreadSheetMLWorksheet,
			"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml", "worksheet"},
		{seedcalc.ContentTypeSpreadSheetMLChartsheet,
			"application/vnd.openxmlformats-officedocument.spreadsheetml.chartsheet+xml", "chart sheet"},
		{seedcalc.ContentTypeSpreadSheetMLComments,
			"application/vnd.openxmlformats-officedocument.spreadsheetml.comments+xml", "comments"},
		{seedcalc.ContentTypeSpreadSheetMLTable,
			"application/vnd.openxmlformats-officedocument.spreadsheetml.table+xml", "table"},
		{seedcalc.ContentTypeSpreadSheetMLSharedStrings,
			"application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml", "shared strings"},
		{seedcalc.ContentTypeSpreadSheetMLPivotTable,
			"application/vnd.openxmlformats-officedocument.spreadsheetml.pivotTable+xml", "pivot table"},
		{seedcalc.ContentTypeSpreadSheetMLPivotCacheDefinition,
			"application/vnd.openxmlformats-officedocument.spreadsheetml.pivotCacheDefinition+xml", "pivot cache definition"},
		{seedcalc.ContentTypeDrawing,
			"application/vnd.openxmlformats-officedocument.drawing+xml", "drawing"},
		{seedcalc.ContentTypeDrawingML,
			"application/vnd.openxmlformats-officedocument.drawingml.chart+xml", "chart"},
		{seedcalc.ContentTypeVML,
			"application/vnd.openxmlformats-officedocument.vmlDrawing", "VML drawing"},
		{seedcalc.ContentTypeMacro,
			"application/vnd.ms-excel.sheet.macroEnabled.main+xml", "macro-enabled workbook"},
		{seedcalc.ContentTypeVBA,
			"application/vnd.ms-office.vbaProject", "VBA project"},
	} {
		if tc.got != tc.want {
			t.Errorf("the %s content type is %q, want %q", tc.what, tc.got, tc.want)
		}
	}
	// The three relationship-type constants that arrive alongside them. These
	// are the values of the Type attribute of a Relationship element, not
	// media types, and they live under the officeDocument relationship
	// namespace rather than the package one.
	for _, tc := range []struct{ got, want, what string }{
		{seedcalc.SourceRelationshipChartsheet,
			"http://schemas.openxmlformats.org/officeDocument/2006/relationships/chartsheet", "chart sheet"},
		{seedcalc.SourceRelationshipDialogsheet,
			"http://schemas.openxmlformats.org/officeDocument/2006/relationships/dialogsheet", "dialog sheet"},
		{seedcalc.SourceRelationshipSharedStrings,
			"http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings", "shared strings"},
	} {
		if tc.got != tc.want {
			t.Errorf("the %s relationship type is %q, want %q", tc.what, tc.got, tc.want)
		}
	}
}

func TestThePackageDeclaresItsPartsWithTheExportedTypes(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetCellValue("Sheet1", "A1", "text"); err != nil {
		t.Fatalf("SetCellValue: %v", err)
	}
	if err := f.AddTable("Sheet1", "A1", "B2",
		`{"table_name":"T","table_style":"TableStyleMedium2"}`); err != nil {
		t.Fatalf("AddTable: %v", err)
	}
	if err := f.AddComment("Sheet1", "B7", `{"author":"A ","text":"hi"}`); err != nil {
		t.Fatalf("AddComment: %v", err)
	}
	f.NewSheet("Second")

	defaults, overrides := stream(t, f)
	wantOverride(t, overrides, "/xl/worksheets/sheet1.xml", seedcalc.ContentTypeSpreadSheetMLWorksheet)
	wantOverride(t, overrides, "/xl/worksheets/sheet2.xml", seedcalc.ContentTypeSpreadSheetMLWorksheet)
	wantOverride(t, overrides, "/xl/tables/table1.xml", seedcalc.ContentTypeSpreadSheetMLTable)
	wantOverride(t, overrides, "/xl/comments1.xml", seedcalc.ContentTypeSpreadSheetMLComments)
	// A comment carries an anchor drawn in VML, and every .vml part in the
	// package shares one media type, so that one is a Default rather than an
	// Override.
	if got := defaults["vml"]; got != seedcalc.ContentTypeVML {
		t.Fatalf("Default for the vml extension = %q, want %q", got, seedcalc.ContentTypeVML)
	}
}

func TestTheChartAndChartsheetTypesAppearWhenThosePartsDo(t *testing.T) {
	const chart = `{"type":"col","series":[{"name":"S","categories":"Sheet1!$A$1:$A$2","values":"Sheet1!$A$1:$A$2"}]}`
	f := seedcalc.NewFile()
	for axis, v := range map[string]int{"A1": 1, "A2": 2} {
		if err := f.SetCellValue("Sheet1", axis, v); err != nil {
			t.Fatalf("SetCellValue(%s): %v", axis, err)
		}
	}
	// An embedded chart is a chart part plus the drawing that anchors it.
	if err := f.AddChart("Sheet1", "E1", chart); err != nil {
		t.Fatalf("AddChart: %v", err)
	}
	// A chart sheet is a sheet in its own right, with its own chart part.
	if err := f.AddChartSheet("ChartOne", chart); err != nil {
		t.Fatalf("AddChartSheet: %v", err)
	}

	_, overrides := stream(t, f)
	wantOverride(t, overrides, "/xl/charts/chart1.xml", seedcalc.ContentTypeDrawingML)
	wantOverride(t, overrides, "/xl/charts/chart2.xml", seedcalc.ContentTypeDrawingML)
	wantOverride(t, overrides, "/xl/drawings/drawing1.xml", seedcalc.ContentTypeDrawing)
	wantOverride(t, overrides, "/xl/drawings/drawing2.xml", seedcalc.ContentTypeDrawing)
	wantOverride(t, overrides, "/xl/chartsheets/sheet2.xml", seedcalc.ContentTypeSpreadSheetMLChartsheet)
	// The chart sheet did not displace the worksheet it was added beside.
	wantOverride(t, overrides, "/xl/worksheets/sheet1.xml", seedcalc.ContentTypeSpreadSheetMLWorksheet)

	// The content type says what the part is; the relationship type says what
	// role it plays in the workbook. A chart sheet is reached through a
	// relationship whose Type is the exported constant, so the constant is
	// what a caller matches on rather than a hard-coded URI.
	rels := workbookRels(t, f)
	if got := rels["chartsheets/sheet2.xml"]; got != seedcalc.SourceRelationshipChartsheet {
		t.Fatalf("relationship type of chartsheets/sheet2.xml = %q, want %q",
			got, seedcalc.SourceRelationshipChartsheet)
	}
	// The worksheet beside it keeps the worksheet relationship type, so the
	// chart-sheet constant identifies exactly one of the two sheets.
	if got := rels["worksheets/sheet1.xml"]; got == seedcalc.SourceRelationshipChartsheet {
		t.Fatal("the worksheet is related as a chart sheet")
	}
}
