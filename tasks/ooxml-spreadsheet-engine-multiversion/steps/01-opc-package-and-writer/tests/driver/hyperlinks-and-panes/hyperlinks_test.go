// Scored group: hyperlinks-and-panes.
//
// SetCellHyperLink/GetCellHyperLink -- the worksheet's hyperlinks collection
// (ECMA-376 Part 1 clause 18.3.1.47 hyperlink, 18.3.1.48 hyperlinks) -- and
// SetPanes -- the sheetView's pane and selection children (clauses 18.3.1.66
// and 18.3.1.78). Neither setter returns an error at this milestone.
package hyperlinksandpanes

import (
	"archive/zip"
	"bytes"
	"encoding/xml"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

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

type worksheetHyperlinks struct {
	Hyperlinks struct {
		Hyperlink []struct {
			Ref      string `xml:"ref,attr"`
			Location string `xml:"location,attr"`
			RID      string `xml:"http://schemas.openxmlformats.org/officeDocument/2006/relationships id,attr"`
		} `xml:"hyperlink"`
	} `xml:"hyperlinks"`
}

func readHyperlinks(t *testing.T, f *seedcalc.File) worksheetHyperlinks {
	t.Helper()
	var ws worksheetHyperlinks
	if err := xml.Unmarshal(part(t, packageParts(t, f), "xl/worksheets/sheet1.xml"), &ws); err != nil {
		t.Fatalf("the worksheet part is not well-formed XML: %v", err)
	}
	return ws
}

type worksheetRels struct {
	Rel []struct {
		ID     string `xml:"Id,attr"`
		Target string `xml:"Target,attr"`
	} `xml:"Relationship"`
}

func readSheetRels(t *testing.T, f *seedcalc.File) worksheetRels {
	t.Helper()
	var rels worksheetRels
	parts := packageParts(t, f)
	b, ok := parts["xl/worksheets/_rels/sheet1.xml.rels"]
	if !ok {
		return rels // no relationships part at all: zero relationships.
	}
	if err := xml.Unmarshal(b, &rels); err != nil {
		t.Fatalf("the worksheet relationships part is not well-formed XML: %v", err)
	}
	return rels
}

type worksheetPane struct {
	ActivePane  string  `xml:"activePane,attr"`
	State       string  `xml:"state,attr"`
	TopLeftCell string  `xml:"topLeftCell,attr"`
	XSplit      float64 `xml:"xSplit,attr"`
	YSplit      float64 `xml:"ySplit,attr"`
}

type worksheetSelection struct {
	ActiveCell string `xml:"activeCell,attr"`
	Pane       string `xml:"pane,attr"`
	SQRef      string `xml:"sqref,attr"`
}

func readFirstSheetView(t *testing.T, f *seedcalc.File) (pane *worksheetPane, selections []worksheetSelection) {
	t.Helper()
	var ws struct {
		SheetViews struct {
			SheetView []struct {
				Pane      *worksheetPane       `xml:"pane"`
				Selection []worksheetSelection `xml:"selection"`
			} `xml:"sheetView"`
		} `xml:"sheetViews"`
	}
	if err := xml.Unmarshal(part(t, packageParts(t, f), "xl/worksheets/sheet1.xml"), &ws); err != nil {
		t.Fatalf("the worksheet part is not well-formed XML: %v", err)
	}
	if len(ws.SheetViews.SheetView) == 0 {
		t.Fatal("the worksheet declares no sheetView")
	}
	sv := ws.SheetViews.SheetView[len(ws.SheetViews.SheetView)-1]
	return sv.Pane, sv.Selection
}

func TestExternalHyperlinkAddsARelationshipAndAppearsOnTheWorksheet(t *testing.T) {
	f := seedcalc.NewFile()
	f.SetCellHyperLink("Sheet1", "A3", "https://example.com/docs", "External")

	if ok, target := f.GetCellHyperLink("Sheet1", "A3"); !ok || target != "https://example.com/docs" {
		t.Errorf("GetCellHyperLink(A3) = (%v, %q), want (true, %q)", ok, target, "https://example.com/docs")
	}

	ws := readHyperlinks(t, f)
	if len(ws.Hyperlinks.Hyperlink) != 1 {
		t.Fatalf("worksheet has %d hyperlinks, want 1", len(ws.Hyperlinks.Hyperlink))
	}
	h := ws.Hyperlinks.Hyperlink[0]
	if h.Ref != "A3" {
		t.Errorf("hyperlink ref = %q, want %q", h.Ref, "A3")
	}
	if h.RID == "" {
		t.Fatal("an External hyperlink has no r:id relationship reference")
	}
	if h.Location != "" {
		t.Errorf("an External hyperlink has location = %q, want empty (the target lives on the relationship)", h.Location)
	}

	rels := readSheetRels(t, f)
	var target string
	for _, r := range rels.Rel {
		if r.ID == h.RID {
			target = r.Target
		}
	}
	if target != "https://example.com/docs" {
		t.Errorf("relationship %s targets %q, want %q", h.RID, target, "https://example.com/docs")
	}
}

func TestLocationHyperlinkStoresTheTargetWithNoRelationship(t *testing.T) {
	f := seedcalc.NewFile()
	f.SetCellHyperLink("Sheet1", "B5", "Sheet2!A1", "Location")

	if ok, target := f.GetCellHyperLink("Sheet1", "B5"); !ok || target != "Sheet2!A1" {
		t.Errorf("GetCellHyperLink(B5) = (%v, %q), want (true, %q)", ok, target, "Sheet2!A1")
	}

	ws := readHyperlinks(t, f)
	if len(ws.Hyperlinks.Hyperlink) != 1 {
		t.Fatalf("worksheet has %d hyperlinks, want 1", len(ws.Hyperlinks.Hyperlink))
	}
	h := ws.Hyperlinks.Hyperlink[0]
	if h.Ref != "B5" {
		t.Errorf("hyperlink ref = %q, want %q", h.Ref, "B5")
	}
	if h.Location != "Sheet2!A1" {
		t.Errorf("hyperlink location = %q, want %q", h.Location, "Sheet2!A1")
	}
	if h.RID != "" {
		t.Errorf("a Location hyperlink has r:id = %q, want empty (no relationship is created)", h.RID)
	}

	rels := readSheetRels(t, f)
	if len(rels.Rel) != 0 {
		t.Errorf("a Location-only workbook has %d worksheet relationships, want 0", len(rels.Rel))
	}
}

func TestUnknownLinkTypeIsANoOpAndAnUnlinkedAxisReportsNoLink(t *testing.T) {
	f := seedcalc.NewFile()
	if ok, target := f.GetCellHyperLink("Sheet1", "C7"); ok || target != "" {
		t.Errorf("GetCellHyperLink on a fresh sheet = (%v, %q), want (false, \"\")", ok, target)
	}

	f.SetCellHyperLink("Sheet1", "C7", "ignored", "Bogus")
	if ok, target := f.GetCellHyperLink("Sheet1", "C7"); ok || target != "" {
		t.Errorf("GetCellHyperLink(C7) after an unrecognised linkType = (%v, %q), want (false, \"\")", ok, target)
	}

	ws := readHyperlinks(t, f)
	if len(ws.Hyperlinks.Hyperlink) != 0 {
		t.Errorf("an unrecognised linkType appended %d hyperlinks, want 0", len(ws.Hyperlinks.Hyperlink))
	}
}

func TestHyperlinkOnAMergedCellRedirectsToTheAnchor(t *testing.T) {
	f := seedcalc.NewFile()
	f.MergeCell("Sheet1", "A1", "B2")
	f.SetCellHyperLink("Sheet1", "B2", "https://example.com/anchor", "External")

	for _, axis := range []string{"A1", "B2"} {
		if ok, target := f.GetCellHyperLink("Sheet1", axis); !ok || target != "https://example.com/anchor" {
			t.Errorf("GetCellHyperLink(%s) = (%v, %q), want (true, %q)", axis, ok, target, "https://example.com/anchor")
		}
	}

	ws := readHyperlinks(t, f)
	if len(ws.Hyperlinks.Hyperlink) != 1 {
		t.Fatalf("worksheet has %d hyperlinks, want 1", len(ws.Hyperlinks.Hyperlink))
	}
	if got := ws.Hyperlinks.Hyperlink[0].Ref; got != "A1" {
		t.Errorf("hyperlink ref = %q, want %q (the merged range's anchor, not the cell SetCellHyperLink was called on)", got, "A1")
	}
}

func TestFrozenPanesWriteStateFrozenWithSplitCoordinatesAndSelection(t *testing.T) {
	f := seedcalc.NewFile()
	f.SetPanes("Sheet1", `{"freeze":true,"split":false,"x_split":0,"y_split":9,"top_left_cell":"A10","active_pane":"bottomLeft","panes":[{"sqref":"A10","active_cell":"A10","pane":"bottomLeft"}]}`)

	pane, selections := readFirstSheetView(t, f)
	if pane == nil {
		t.Fatal("freeze:true wrote no pane element")
	}
	if pane.State != "frozen" {
		t.Errorf("pane state = %q, want %q", pane.State, "frozen")
	}
	if pane.YSplit != 9 {
		t.Errorf("pane ySplit = %v, want 9", pane.YSplit)
	}
	if pane.TopLeftCell != "A10" {
		t.Errorf("pane topLeftCell = %q, want %q", pane.TopLeftCell, "A10")
	}
	if pane.ActivePane != "bottomLeft" {
		t.Errorf("pane activePane = %q, want %q", pane.ActivePane, "bottomLeft")
	}
	if len(selections) != 1 || selections[0].SQRef != "A10" || selections[0].ActiveCell != "A10" || selections[0].Pane != "bottomLeft" {
		t.Errorf("selections = %+v, want one {sqref:A10 activeCell:A10 pane:bottomLeft}", selections)
	}
}

func TestSplitPanesWriteAPaneWithoutFrozenState(t *testing.T) {
	f := seedcalc.NewFile()
	f.SetPanes("Sheet1", `{"freeze":false,"split":true,"x_split":3270,"y_split":1800,"top_left_cell":"N57","active_pane":"topRight","panes":[{"sqref":"G33","active_cell":"G33","pane":"topRight"}]}`)

	pane, selections := readFirstSheetView(t, f)
	if pane == nil {
		t.Fatal("split:true wrote no pane element")
	}
	if pane.State == "frozen" {
		t.Error("split:true with freeze:false wrote state=\"frozen\"")
	}
	if pane.XSplit != 3270 || pane.YSplit != 1800 {
		t.Errorf("pane split = (%v, %v), want (3270, 1800)", pane.XSplit, pane.YSplit)
	}
	if pane.TopLeftCell != "N57" {
		t.Errorf("pane topLeftCell = %q, want %q", pane.TopLeftCell, "N57")
	}
	if len(selections) != 1 || selections[0].Pane != "topRight" {
		t.Errorf("selections = %+v, want one pane=topRight", selections)
	}
}

func TestClearingFreezeAndSplitRemovesThePaneButKeepsSelections(t *testing.T) {
	f := seedcalc.NewFile()
	f.SetPanes("Sheet1", `{"freeze":true,"split":false,"y_split":5,"top_left_cell":"A6","active_pane":"bottomLeft"}`)
	f.SetPanes("Sheet1", `{"freeze":false,"split":false,"panes":[{"sqref":"D4","active_cell":"D4","pane":"bottomRight"}]}`)

	pane, selections := readFirstSheetView(t, f)
	if pane != nil {
		t.Errorf("freeze:false,split:false left a pane element: %+v, want it removed entirely", pane)
	}
	if len(selections) != 1 || selections[0].SQRef != "D4" || selections[0].ActiveCell != "D4" || selections[0].Pane != "bottomRight" {
		t.Errorf("selections = %+v, want one {sqref:D4 activeCell:D4 pane:bottomRight} (a later call's panes are written even when the pane element is removed)", selections)
	}
}
