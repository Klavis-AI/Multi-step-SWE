// Scored group: hyperlink-opts.
//
// SetCellHyperLink gains a variadic HyperlinkOpts tail carrying the two
// optional attributes of the hyperlink element: the text shown in place of the
// cell value and the text shown on hover. Neither is readable through
// GetCellHyperLink, so both are checked where they are actually recorded --
// the hyperlink element of the written worksheet part, ECMA-376 Part 1 section
// 18.3.1.47 "hyperlink (Hyperlink)".
package hyperlinkopts

import (
	"archive/zip"
	"bytes"
	"encoding/xml"
	"io"
	"sort"
	"strings"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// sheetXML returns the first worksheet part of the written package.
func sheetXML(t *testing.T, f *seedcalc.File) string {
	t.Helper()
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	zr, err := zip.NewReader(bytes.NewReader(buf.Bytes()), int64(buf.Len()))
	if err != nil {
		t.Fatalf("written package is not a ZIP container: %v", err)
	}
	for _, zf := range zr.File {
		if zf.Name != "xl/worksheets/sheet1.xml" {
			continue
		}
		rc, err := zf.Open()
		if err != nil {
			t.Fatalf("open worksheet part: %v", err)
		}
		defer rc.Close()
		b, err := io.ReadAll(rc)
		if err != nil {
			t.Fatalf("read worksheet part: %v", err)
		}
		return string(b)
	}
	t.Fatal("written package has no xl/worksheets/sheet1.xml")
	return ""
}

// hyperlinkFor returns the hyperlink element whose ref attribute names axis,
// re-rendered with its attributes in a fixed order. The part is parsed rather
// than string-searched: attribute order carries no meaning in XML and nothing
// in the contract pins it, so a writer that emits @ref after @display must
// still be found here.
func hyperlinkFor(t *testing.T, part, axis string) string {
	t.Helper()
	dec := xml.NewDecoder(strings.NewReader(part))
	for {
		tok, err := dec.Token()
		if err == io.EOF {
			break
		}
		if err != nil {
			t.Fatalf("worksheet part is not well-formed XML: %v", err)
		}
		el, ok := tok.(xml.StartElement)
		if !ok || el.Name.Local != "hyperlink" {
			continue
		}
		var ref string
		attrs := make([]string, 0, len(el.Attr))
		for _, a := range el.Attr {
			if a.Name.Local == "ref" {
				ref = a.Value
			}
			attrs = append(attrs, a.Name.Local+`="`+a.Value+`"`)
		}
		if ref != axis {
			continue
		}
		sort.Strings(attrs)
		return "<hyperlink " + strings.Join(attrs, " ") + ">"
	}
	t.Fatalf("worksheet part has no hyperlink for %s", axis)
	return ""
}

func TestAHyperlinkWithoutOptionsCarriesNeitherAttribute(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetCellHyperLink("Sheet1", "A1", "https://example.com", "External"); err != nil {
		t.Fatalf("SetCellHyperLink: %v", err)
	}
	el := hyperlinkFor(t, sheetXML(t, f), "A1")
	if strings.Contains(el, "display=") || strings.Contains(el, "tooltip=") {
		t.Fatalf("hyperlink written without options = %s, want neither display nor tooltip", el)
	}
}

func TestDisplayAndTooltipAreRecordedOnTheElement(t *testing.T) {
	f := seedcalc.NewFile()
	display, tooltip := "shown", "hovered"
	err := f.SetCellHyperLink("Sheet1", "A2", "https://example.org", "External",
		seedcalc.HyperlinkOpts{Display: &display, Tooltip: &tooltip})
	if err != nil {
		t.Fatalf("SetCellHyperLink with options: %v", err)
	}
	el := hyperlinkFor(t, sheetXML(t, f), "A2")
	if !strings.Contains(el, `display="shown"`) {
		t.Fatalf("hyperlink = %s, want a display attribute", el)
	}
	if !strings.Contains(el, `tooltip="hovered"`) {
		t.Fatalf("hyperlink = %s, want a tooltip attribute", el)
	}
}

func TestEachOptionIsIndependentlyOptional(t *testing.T) {
	f := seedcalc.NewFile()
	display := "shown"
	// The fields are pointers precisely so that one can be set without the
	// other: a nil field writes no attribute rather than an empty one.
	err := f.SetCellHyperLink("Sheet1", "A3", "https://example.net", "External",
		seedcalc.HyperlinkOpts{Display: &display})
	if err != nil {
		t.Fatalf("SetCellHyperLink with one option: %v", err)
	}
	el := hyperlinkFor(t, sheetXML(t, f), "A3")
	if !strings.Contains(el, `display="shown"`) {
		t.Fatalf("hyperlink = %s, want a display attribute", el)
	}
	if strings.Contains(el, "tooltip=") {
		t.Fatalf("hyperlink = %s, want no tooltip attribute", el)
	}
}

func TestSeveralOptionValuesAreMergedInOrder(t *testing.T) {
	f := seedcalc.NewFile()
	display, tooltip := "shown", "hovered"
	// The tail is variadic, and each value contributes only the fields it sets,
	// so two single-field values have the same effect as one holding both.
	err := f.SetCellHyperLink("Sheet1", "A4", "https://example.com", "External",
		seedcalc.HyperlinkOpts{Display: &display},
		seedcalc.HyperlinkOpts{Tooltip: &tooltip})
	if err != nil {
		t.Fatalf("SetCellHyperLink with two option values: %v", err)
	}
	el := hyperlinkFor(t, sheetXML(t, f), "A4")
	if !strings.Contains(el, `display="shown"`) || !strings.Contains(el, `tooltip="hovered"`) {
		t.Fatalf("hyperlink = %s, want both attributes", el)
	}
}

func TestOptionsApplyToALocationLinkToo(t *testing.T) {
	f := seedcalc.NewFile()
	tooltip := "hovered"
	err := f.SetCellHyperLink("Sheet1", "B1", "Sheet1!A10", "Location",
		seedcalc.HyperlinkOpts{Tooltip: &tooltip})
	if err != nil {
		t.Fatalf("SetCellHyperLink(Location) with options: %v", err)
	}
	el := hyperlinkFor(t, sheetXML(t, f), "B1")
	// An internal link records its destination inline rather than through a
	// relationship, and still carries the optional attributes.
	if !strings.Contains(el, `location="Sheet1!A10"`) {
		t.Fatalf("hyperlink = %s, want a location attribute", el)
	}
	if !strings.Contains(el, `tooltip="hovered"`) {
		t.Fatalf("hyperlink = %s, want a tooltip attribute", el)
	}
}

func TestTheInheritedHyperlinkContractIsUnchanged(t *testing.T) {
	f := seedcalc.NewFile()
	display := "shown"
	if err := f.SetCellHyperLink("Sheet1", "A1", "https://example.com", "External",
		seedcalc.HyperlinkOpts{Display: &display}); err != nil {
		t.Fatalf("SetCellHyperLink: %v", err)
	}
	// The options change nothing about what GetCellHyperLink reports.
	ok, target, err := f.GetCellHyperLink("Sheet1", "A1")
	if err != nil {
		t.Fatalf("GetCellHyperLink: %v", err)
	}
	if !ok || target != "https://example.com" {
		t.Fatalf("GetCellHyperLink = (%v, %q), want (true, %q)", ok, target, "https://example.com")
	}
	// An unlinked cell is still reported as such without an error.
	ok, target, err = f.GetCellHyperLink("Sheet1", "C9")
	if err != nil {
		t.Fatalf("GetCellHyperLink on an unlinked cell: %v", err)
	}
	if ok || target != "" {
		t.Fatalf("GetCellHyperLink on an unlinked cell = (%v, %q), want (false, \"\")", ok, target)
	}
	// The link type is still closed, and the reference still validated.
	if err := f.SetCellHyperLink("Sheet1", "A5", "x", "Nope"); err == nil {
		t.Fatal("SetCellHyperLink with an unknown link type returned no error")
	}
	if err := f.SetCellHyperLink("Sheet1", "ZZZ", "x", "External"); err == nil {
		t.Fatal("SetCellHyperLink at a malformed reference returned no error")
	}
}
