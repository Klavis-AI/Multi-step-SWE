// Scored group: cell-styles.
//
// Named cell formats: a style is defined once, gets an identifier, and is then
// applied to cells or ranges. The style sheet layout asserted here is the one
// ECMA-376 Part 1 clause 18.8 fixes -- fonts, fills, borders and numFmts are
// collections indexed by a cellXfs/xf record, and a c/@s attribute selects the
// xf. Built-in number format identifiers are from clause 18.8.30.
package cellstyles

import (
	"archive/zip"
	"bytes"
	"encoding/xml"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

type attrVal struct {
	Val string `xml:"val,attr"`
}

type rgbColor struct {
	RGB string `xml:"rgb,attr"`
}

type borderEdge struct {
	Style string    `xml:"style,attr"`
	Color *rgbColor `xml:"color"`
}

type styleSheet struct {
	NumFmts struct {
		NumFmt []struct {
			ID         int    `xml:"numFmtId,attr"`
			FormatCode string `xml:"formatCode,attr"`
		} `xml:"numFmt"`
	} `xml:"numFmts"`
	Fonts struct {
		Font []struct {
			Name  *attrVal  `xml:"name"`
			Sz    *attrVal  `xml:"sz"`
			Color *rgbColor `xml:"color"`
		} `xml:"font"`
	} `xml:"fonts"`
	Fills struct {
		Fill []struct {
			PatternFill *struct {
				PatternType string    `xml:"patternType,attr"`
				FgColor     *rgbColor `xml:"fgColor"`
			} `xml:"patternFill"`
		} `xml:"fill"`
	} `xml:"fills"`
	Borders struct {
		Border []struct {
			Left borderEdge `xml:"left"`
			Top  borderEdge `xml:"top"`
		} `xml:"border"`
	} `xml:"borders"`
	CellXfs struct {
		Xf []struct {
			NumFmtID       int    `xml:"numFmtId,attr"`
			FontID         int    `xml:"fontId,attr"`
			FillID         int    `xml:"fillId,attr"`
			BorderID       int    `xml:"borderId,attr"`
			ApplyAlignment string `xml:"applyAlignment,attr"`
			Alignment      *struct {
				Horizontal   string `xml:"horizontal,attr"`
				Vertical     string `xml:"vertical,attr"`
				WrapText     string `xml:"wrapText,attr"`
				TextRotation string `xml:"textRotation,attr"`
			} `xml:"alignment"`
		} `xml:"xf"`
	} `xml:"cellXfs"`
}

func readPart(t *testing.T, f *seedcalc.File, name string) []byte {
	t.Helper()
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	zr, err := zip.NewReader(bytes.NewReader(buf.Bytes()), int64(buf.Len()))
	if err != nil {
		t.Fatalf("written package is not a ZIP container: %v", err)
	}
	for _, p := range zr.File {
		if p.Name != name {
			continue
		}
		rc, err := p.Open()
		if err != nil {
			t.Fatalf("open %s: %v", name, err)
		}
		defer rc.Close()
		var b bytes.Buffer
		if _, err := b.ReadFrom(rc); err != nil {
			t.Fatalf("read %s: %v", name, err)
		}
		return b.Bytes()
	}
	t.Fatalf("package has no part %s", name)
	return nil
}

func styles(t *testing.T, f *seedcalc.File) styleSheet {
	t.Helper()
	var s styleSheet
	if err := xml.Unmarshal(readPart(t, f, "xl/styles.xml"), &s); err != nil {
		t.Fatalf("xl/styles.xml is not well-formed XML: %v", err)
	}
	return s
}

func newStyle(t *testing.T, f *seedcalc.File, def string) int {
	t.Helper()
	id, err := f.NewStyle(def)
	if err != nil {
		t.Fatalf("NewStyle(%s): %v", def, err)
	}
	return id
}

func TestEachStyleDefinitionGetsItsOwnIdentifier(t *testing.T) {
	f := seedcalc.NewFile()
	seen := map[int]bool{}
	for _, def := range []string{
		`{"number_format":22}`,
		`{"font":{"bold":true}}`,
		`{"fill":{"type":"pattern","color":["#E0EBF5"],"pattern":1}}`,
	} {
		id := newStyle(t, f, def)
		if id == 0 {
			t.Errorf("NewStyle(%s) returned 0, which is the identifier of the default format", def)
		}
		if seen[id] {
			t.Errorf("NewStyle(%s) reused identifier %d", def, id)
		}
		seen[id] = true
	}
	s := styles(t, f)
	if len(s.CellXfs.Xf) != len(seen)+1 {
		t.Errorf("cellXfs holds %d records, want %d (the default plus one per style)", len(s.CellXfs.Xf), len(seen)+1)
	}
}

func TestAMalformedStyleDefinitionIsRejected(t *testing.T) {
	f := seedcalc.NewFile()
	id, err := f.NewStyle(`{"font":`)
	if err == nil {
		t.Fatal("NewStyle accepted a definition that is not well-formed JSON")
	}
	if id != 0 {
		t.Errorf("NewStyle returned identifier %d alongside an error, want 0", id)
	}
	// The rejected definition must not have been recorded.
	s := styles(t, f)
	if len(s.CellXfs.Xf) != 1 {
		t.Errorf("cellXfs holds %d records after a rejected definition, want only the default", len(s.CellXfs.Xf))
	}
}

func TestSetCellStyleAppliesToEveryCellOfTheRange(t *testing.T) {
	f := seedcalc.NewFile()
	id := newStyle(t, f, `{"font":{"bold":true}}`)
	f.SetCellStyle("Sheet1", "B2", "D4", id)

	for _, axis := range []string{"B2", "C3", "D4", "B4", "D2"} {
		if got := f.GetCellStyle("Sheet1", axis); got != id {
			t.Errorf("GetCellStyle(%q) = %d, want %d", axis, got, id)
		}
	}
	for _, axis := range []string{"A1", "E5"} {
		if got := f.GetCellStyle("Sheet1", axis); got != 0 {
			t.Errorf("GetCellStyle(%q) = %d for a cell outside the range, want 0", axis, got)
		}
	}
}

func TestANumberFormatStyleChangesHowTheValueReads(t *testing.T) {
	f := seedcalc.NewFile()
	date := newStyle(t, f, `{"number_format":22}`)
	twoPlaces := newStyle(t, f, `{"number_format":2}`)

	f.SetCellValue("Sheet1", "A1", 42920.5)
	f.SetCellValue("Sheet1", "A2", 1234.5678)
	f.SetCellValue("Sheet1", "A3", 1234.5678)
	f.SetCellStyle("Sheet1", "A1", "A1", date)
	f.SetCellStyle("Sheet1", "A2", "A2", twoPlaces)

	if got := f.GetCellValue("Sheet1", "A1"); got != "7/4/17 12:00" {
		t.Errorf("A1 under built-in format 22 = %q", got)
	}
	if got := f.GetCellValue("Sheet1", "A2"); got != "1234.57" {
		t.Errorf("A2 under built-in format 2 = %q, want %q", got, "1234.57")
	}
	if got := f.GetCellValue("Sheet1", "A3"); got != "1234.5678" {
		t.Errorf("A3 with no style = %q, want the stored value %q", got, "1234.5678")
	}
}

func TestFontFillAndBorderDefinitionsReachTheStyleSheet(t *testing.T) {
	f := seedcalc.NewFile()
	fontID := newStyle(t, f, `{"font":{"bold":true,"family":"Times New Roman","size":13,"color":"#777777"}}`)
	fillID := newStyle(t, f, `{"fill":{"type":"pattern","color":["#E0EBF5"],"pattern":1}}`)
	borderID := newStyle(t, f, `{"border":[{"type":"left","color":"0000FF","style":3},{"type":"top","color":"00FF00","style":4}]}`)
	s := styles(t, f)

	if fontID >= len(s.CellXfs.Xf) || fillID >= len(s.CellXfs.Xf) || borderID >= len(s.CellXfs.Xf) {
		t.Fatalf("cellXfs holds %d records, too few for identifiers %d/%d/%d", len(s.CellXfs.Xf), fontID, fillID, borderID)
	}

	fi := s.CellXfs.Xf[fontID].FontID
	if fi <= 0 || fi >= len(s.Fonts.Font) {
		t.Fatalf("the font style points at fontId %d, outside the %d fonts declared", fi, len(s.Fonts.Font))
	}
	font := s.Fonts.Font[fi]
	if font.Name == nil || font.Name.Val != "Times New Roman" {
		t.Errorf("font %d name = %+v, want Times New Roman", fi, font.Name)
	}
	if font.Sz == nil || font.Sz.Val != "13" {
		t.Errorf("font %d size = %+v, want 13", fi, font.Sz)
	}
	if font.Color == nil || font.Color.RGB != "FF777777" {
		t.Errorf("font %d colour = %+v, want the opaque form of #777777", fi, font.Color)
	}

	gi := s.CellXfs.Xf[fillID].FillID
	if gi <= 0 || gi >= len(s.Fills.Fill) {
		t.Fatalf("the fill style points at fillId %d, outside the %d fills declared", gi, len(s.Fills.Fill))
	}
	fill := s.Fills.Fill[gi]
	if fill.PatternFill == nil || fill.PatternFill.PatternType != "solid" {
		t.Errorf("fill %d = %+v, want a solid patternFill", gi, fill.PatternFill)
	} else if fill.PatternFill.FgColor == nil || fill.PatternFill.FgColor.RGB != "FFE0EBF5" {
		t.Errorf("fill %d foreground = %+v, want the opaque form of #E0EBF5", gi, fill.PatternFill.FgColor)
	}

	bi := s.CellXfs.Xf[borderID].BorderID
	if bi <= 0 || bi >= len(s.Borders.Border) {
		t.Fatalf("the border style points at borderId %d, outside the %d borders declared", bi, len(s.Borders.Border))
	}
	border := s.Borders.Border[bi]
	if border.Left.Style == "" || border.Left.Color == nil || border.Left.Color.RGB != "FF0000FF" {
		t.Errorf("border %d left edge = %+v/%+v, want a styled blue edge", bi, border.Left.Style, border.Left.Color)
	}
	if border.Top.Style == "" || border.Top.Color == nil || border.Top.Color.RGB != "FF00FF00" {
		t.Errorf("border %d top edge = %+v/%+v, want a styled green edge", bi, border.Top.Style, border.Top.Color)
	}
	if border.Left.Style == border.Top.Style {
		t.Errorf("border %d left and top both use line style %q, want the two requested styles to differ", bi, border.Left.Style)
	}
}

func TestAlignmentIsRecordedOnTheCellFormat(t *testing.T) {
	f := seedcalc.NewFile()
	id := newStyle(t, f, `{"alignment":{"horizontal":"center","vertical":"center","wrap_text":true,"text_rotation":45}}`)
	s := styles(t, f)
	if id >= len(s.CellXfs.Xf) {
		t.Fatalf("cellXfs holds %d records, too few for identifier %d", len(s.CellXfs.Xf), id)
	}
	xf := s.CellXfs.Xf[id]
	if xf.ApplyAlignment != "true" && xf.ApplyAlignment != "1" {
		t.Errorf("xf %d applyAlignment = %q, want it set", id, xf.ApplyAlignment)
	}
	if xf.Alignment == nil {
		t.Fatalf("xf %d carries no alignment element", id)
	}
	if xf.Alignment.Horizontal != "center" {
		t.Errorf("xf %d horizontal alignment = %q, want %q", id, xf.Alignment.Horizontal, "center")
	}
	if xf.Alignment.Vertical != "center" {
		t.Errorf("xf %d vertical alignment = %q, want %q", id, xf.Alignment.Vertical, "center")
	}
	if xf.Alignment.WrapText != "true" && xf.Alignment.WrapText != "1" {
		t.Errorf("xf %d wrapText = %q, want it set", id, xf.Alignment.WrapText)
	}
	if xf.Alignment.TextRotation != "45" {
		t.Errorf("xf %d textRotation = %q, want %q", id, xf.Alignment.TextRotation, "45")
	}
}

func TestACustomNumberFormatIsRegisteredWithItsOwnIdentifier(t *testing.T) {
	f := seedcalc.NewFile()
	const code = `[$-380A]dddd;@`
	id := newStyle(t, f, `{"custom_number_format":"`+code+`"}`)
	s := styles(t, f)
	if id >= len(s.CellXfs.Xf) {
		t.Fatalf("cellXfs holds %d records, too few for identifier %d", len(s.CellXfs.Xf), id)
	}
	numFmtID := s.CellXfs.Xf[id].NumFmtID
	// Identifiers below 164 are reserved for the built-in formats.
	if numFmtID < 164 {
		t.Errorf("the custom format was given numFmtId %d, want an identifier at or above 164", numFmtID)
	}
	found := false
	for _, nf := range s.NumFmts.NumFmt {
		if nf.ID == numFmtID {
			found = true
			if nf.FormatCode != code {
				t.Errorf("numFmt %d formatCode = %q, want %q", nf.ID, nf.FormatCode, code)
			}
		}
	}
	if !found {
		t.Errorf("the style sheet declares no numFmt with id %d (%d declared)", numFmtID, len(s.NumFmts.NumFmt))
	}
}
