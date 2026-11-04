// Scored group: conditional-formats.
//
// Conditional formatting rules and the differential formats they point at:
// the conditionalFormatting/cfRule elements of ECMA-376 Part 1 clause 18.3.1.18
// and 18.3.1.10, the colorScale rule of clause 18.3.1.16, and the dxfs
// collection of clause 18.8.15 that a cfRule's dxfId indexes.
package conditionalformats

import (
	"archive/zip"
	"bytes"
	"encoding/xml"
	"fmt"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

type rgbColor struct {
	RGB string `xml:"rgb,attr"`
}

type dxfList struct {
	Dxfs struct {
		Count int `xml:"count,attr"`
		Dxf   []struct {
			Font *struct {
				Color *rgbColor `xml:"color"`
			} `xml:"font"`
			Fill *struct {
				PatternFill *struct {
					PatternType string    `xml:"patternType,attr"`
					BgColor     *rgbColor `xml:"bgColor"`
				} `xml:"patternFill"`
			} `xml:"fill"`
		} `xml:"dxf"`
	} `xml:"dxfs"`
}

type worksheetRules struct {
	ConditionalFormatting []struct {
		Sqref  string `xml:"sqref,attr"`
		CfRule []struct {
			Type       string `xml:"type,attr"`
			Operator   string `xml:"operator,attr"`
			Priority   string `xml:"priority,attr"`
			DxfID      string `xml:"dxfId,attr"`
			Formula    string `xml:"formula"`
			ColorScale *struct {
				Cfvo []struct {
					Type string `xml:"type,attr"`
					Val  string `xml:"val,attr"`
				} `xml:"cfvo"`
				Color []rgbColor `xml:"color"`
			} `xml:"colorScale"`
		} `xml:"cfRule"`
	} `xml:"conditionalFormatting"`
}

func parts(t *testing.T, f *seedcalc.File) map[string][]byte {
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

func decode(t *testing.T, p map[string][]byte, name string, v interface{}) {
	t.Helper()
	b, ok := p[name]
	if !ok {
		t.Fatalf("package has no part %s", name)
	}
	if err := xml.Unmarshal(b, v); err != nil {
		t.Fatalf("%s is not well-formed XML: %v", name, err)
	}
}

func TestEachConditionalStyleAddsADifferentialFormat(t *testing.T) {
	f := seedcalc.NewFile()
	first, err := f.NewConditionalStyle(`{"font":{"color":"#9A0511"},"fill":{"type":"pattern","color":["#FEC7CE"],"pattern":1}}`)
	if err != nil {
		t.Fatalf("NewConditionalStyle: %v", err)
	}
	second, err := f.NewConditionalStyle(`{"font":{"color":"#09600B"}}`)
	if err != nil {
		t.Fatalf("NewConditionalStyle: %v", err)
	}
	if first == second {
		t.Errorf("two different conditional styles share identifier %d", first)
	}
	if first != 0 {
		t.Errorf("the first differential format got identifier %d, want 0 -- differential format indices start at 0, unlike cell style indices", first)
	}

	var s dxfList
	decode(t, parts(t, f), "xl/styles.xml", &s)
	if len(s.Dxfs.Dxf) != 2 || s.Dxfs.Count != 2 {
		t.Fatalf("the style sheet declares %d differential formats (count=%d), want 2", len(s.Dxfs.Dxf), s.Dxfs.Count)
	}
	if first >= len(s.Dxfs.Dxf) || second >= len(s.Dxfs.Dxf) {
		t.Fatalf("identifiers %d and %d do not index the %d differential formats", first, second, len(s.Dxfs.Dxf))
	}
	d := s.Dxfs.Dxf[first]
	if d.Font == nil || d.Font.Color == nil || d.Font.Color.RGB != "FF9A0511" {
		t.Errorf("differential format %d font colour = %+v, want the opaque form of #9A0511", first, d.Font)
	}
	if d.Fill == nil || d.Fill.PatternFill == nil || d.Fill.PatternFill.BgColor == nil || d.Fill.PatternFill.BgColor.RGB != "FFFEC7CE" {
		t.Errorf("differential format %d fill = %+v, want the opaque form of #FEC7CE", first, d.Fill)
	}
}

func TestAMalformedConditionalStyleIsRejected(t *testing.T) {
	f := seedcalc.NewFile()
	id, err := f.NewConditionalStyle(`{"font":`)
	if err == nil {
		t.Fatal("NewConditionalStyle accepted a definition that is not well-formed JSON")
	}
	if id != 0 {
		t.Errorf("NewConditionalStyle returned %d alongside an error, want 0", id)
	}
	var s dxfList
	decode(t, parts(t, f), "xl/styles.xml", &s)
	if len(s.Dxfs.Dxf) != 0 {
		t.Errorf("the rejected definition left %d differential formats behind", len(s.Dxfs.Dxf))
	}
}

func TestACellRuleReferencesItsDifferentialFormat(t *testing.T) {
	f := seedcalc.NewFile()
	id, err := f.NewConditionalStyle(`{"font":{"color":"#9A0511"}}`)
	if err != nil {
		t.Fatalf("NewConditionalStyle: %v", err)
	}
	f.SetConditionalFormat("Sheet1", "A1:A10", fmt.Sprintf(`[{"type":"cell","criteria":">","format":%d,"value":"6"}]`, id))

	var ws worksheetRules
	decode(t, parts(t, f), "xl/worksheets/sheet1.xml", &ws)
	if len(ws.ConditionalFormatting) != 1 {
		t.Fatalf("the worksheet carries %d conditionalFormatting blocks, want 1", len(ws.ConditionalFormatting))
	}
	block := ws.ConditionalFormatting[0]
	if block.Sqref != "A1:A10" {
		t.Errorf("conditionalFormatting sqref = %q, want %q", block.Sqref, "A1:A10")
	}
	if len(block.CfRule) != 1 {
		t.Fatalf("the block carries %d rules, want 1", len(block.CfRule))
	}
	rule := block.CfRule[0]
	if rule.Type != "cellIs" {
		t.Errorf("cfRule type = %q, want %q", rule.Type, "cellIs")
	}
	if rule.Operator != "greaterThan" {
		t.Errorf("cfRule operator = %q, want %q", rule.Operator, "greaterThan")
	}
	if rule.Formula != "6" {
		t.Errorf("cfRule formula = %q, want %q", rule.Formula, "6")
	}
	if rule.DxfID != fmt.Sprint(id) {
		t.Errorf("cfRule dxfId = %q, want %d", rule.DxfID, id)
	}
	if rule.Priority == "" {
		t.Error("cfRule has no priority")
	}
}

func TestATwoColourScaleWritesTwoStopsAndTwoColours(t *testing.T) {
	f := seedcalc.NewFile()
	f.SetConditionalFormat("Sheet1", "B1:B10",
		`[{"type":"2_color_scale","criteria":"=","min_type":"min","max_type":"max","min_color":"#FFFFFF","max_color":"#638EC6"}]`)

	var ws worksheetRules
	decode(t, parts(t, f), "xl/worksheets/sheet1.xml", &ws)
	if len(ws.ConditionalFormatting) != 1 || len(ws.ConditionalFormatting[0].CfRule) != 1 {
		t.Fatalf("want one rule in one block, got %+v", ws.ConditionalFormatting)
	}
	rule := ws.ConditionalFormatting[0].CfRule[0]
	if rule.Type != "colorScale" {
		t.Fatalf("cfRule type = %q, want %q", rule.Type, "colorScale")
	}
	if rule.ColorScale == nil {
		t.Fatal("the rule carries no colorScale element")
	}
	if len(rule.ColorScale.Cfvo) != 2 {
		t.Errorf("colorScale has %d value objects, want 2", len(rule.ColorScale.Cfvo))
	} else {
		if rule.ColorScale.Cfvo[0].Type != "min" {
			t.Errorf("first cfvo type = %q, want %q", rule.ColorScale.Cfvo[0].Type, "min")
		}
		if rule.ColorScale.Cfvo[1].Type != "max" {
			t.Errorf("second cfvo type = %q, want %q", rule.ColorScale.Cfvo[1].Type, "max")
		}
	}
	want := []string{"FFFFFFFF", "FF638EC6"}
	if len(rule.ColorScale.Color) != len(want) {
		t.Fatalf("colorScale has %d colours, want %d", len(rule.ColorScale.Color), len(want))
	}
	for i, w := range want {
		if rule.ColorScale.Color[i].RGB != w {
			t.Errorf("colorScale colour %d = %q, want %q", i, rule.ColorScale.Color[i].RGB, w)
		}
	}
}

func TestAThreeColourScaleWritesThreeStopsAndThreeColours(t *testing.T) {
	f := seedcalc.NewFile()
	f.SetConditionalFormat("Sheet1", "C1:C10",
		`[{"type":"3_color_scale","criteria":"=","min_type":"min","mid_type":"percentile","max_type":"max","min_color":"#F8696B","mid_color":"#FFEB84","max_color":"#63BE7B"}]`)

	var ws worksheetRules
	decode(t, parts(t, f), "xl/worksheets/sheet1.xml", &ws)
	if len(ws.ConditionalFormatting) != 1 || len(ws.ConditionalFormatting[0].CfRule) != 1 {
		t.Fatalf("want one rule in one block, got %+v", ws.ConditionalFormatting)
	}
	rule := ws.ConditionalFormatting[0].CfRule[0]
	if rule.Type != "colorScale" || rule.ColorScale == nil {
		t.Fatalf("cfRule type = %q with colorScale %v, want a colorScale rule", rule.Type, rule.ColorScale != nil)
	}
	wantStops := []string{"min", "percentile", "max"}
	if len(rule.ColorScale.Cfvo) != len(wantStops) {
		t.Fatalf("colorScale has %d value objects, want 3", len(rule.ColorScale.Cfvo))
	}
	for i, w := range wantStops {
		if rule.ColorScale.Cfvo[i].Type != w {
			t.Errorf("cfvo %d type = %q, want %q", i, rule.ColorScale.Cfvo[i].Type, w)
		}
	}
	if got := rule.ColorScale.Cfvo[1].Val; got != "50" {
		t.Errorf("the midpoint cfvo val = %q, want the 50th percentile", got)
	}
	wantColors := []string{"FFF8696B", "FFFFEB84", "FF63BE7B"}
	if len(rule.ColorScale.Color) != len(wantColors) {
		t.Fatalf("colorScale has %d colours, want 3", len(rule.ColorScale.Color))
	}
	for i, w := range wantColors {
		if rule.ColorScale.Color[i].RGB != w {
			t.Errorf("colorScale colour %d = %q, want %q", i, rule.ColorScale.Color[i].RGB, w)
		}
	}
}
