// Gate group: drawing-and-link-relationships (milestone 04 regression).
//
// This milestone replaces the three separate relationship caches an earlier
// checkpoint kept for the workbook, the worksheets and the drawings with a
// single part-keyed registry. Everything that reaches a part through a
// relationship must keep working across that change.
//
// Normative sources:
//   - TEST_INTERFACE.md sections 4.01, 4.02, 4.03 and 4.04.
//   - ECMA-376 Part 2, 6.5.2 Relationships part -- the relationships for a
//     part named /a/b.xml live in /a/_rels/b.xml.rels.
//   - ECMA-376 Part 2, 6.5.3 Relationship markup -- Id, Type, Target and the
//     TargetMode that distinguishes an internal target from an external one.
//   - ECMA-376 Part 2, 6.4 Resolving relative references.
package drawingandlinkrelationships

import (
	"archive/zip"
	"bytes"
	"encoding/xml"
	"image"
	"image/color"
	"image/png"
	"io/ioutil"
	"path"
	"path/filepath"
	"strings"
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

type relationship struct {
	ID         string `xml:"Id,attr"`
	Type       string `xml:"Type,attr"`
	Target     string `xml:"Target,attr"`
	TargetMode string `xml:"TargetMode,attr"`
}

type relationships struct {
	Rel []relationship `xml:"Relationship"`
}

func parseRels(t *testing.T, parts map[string]string, name string) relationships {
	t.Helper()
	raw, ok := parts[name]
	if !ok {
		t.Fatalf("the package has no %s part", name)
	}
	var r relationships
	if err := xml.Unmarshal([]byte(raw), &r); err != nil {
		t.Fatalf("%s is not well-formed relationship markup: %v", name, err)
	}
	return r
}

// checkInternalTargetsResolve verifies that every internal relationship in the
// named relationship part points at a package item that exists.
func checkInternalTargetsResolve(t *testing.T, parts map[string]string, relsName string) int {
	t.Helper()
	base := path.Dir(path.Dir(relsName))
	if base == "." {
		base = ""
	}
	rels := parseRels(t, parts, relsName)
	for _, r := range rels.Rel {
		if r.TargetMode == "External" {
			continue
		}
		target := path.Clean(path.Join(base, r.Target))
		if _, ok := parts[target]; !ok {
			t.Errorf("%s: relationship %s targets %q, which resolves to a missing part %q", relsName, r.ID, r.Target, target)
		}
	}
	return len(rels.Rel)
}

func pngBytes(t *testing.T) []byte {
	t.Helper()
	img := image.NewRGBA(image.Rect(0, 0, 8, 8))
	for x := 0; x < 8; x++ {
		for y := 0; y < 8; y++ {
			img.Set(x, y, color.RGBA{R: uint8(x * 32), G: uint8(y * 32), B: 96, A: 255})
		}
	}
	var buf bytes.Buffer
	if err := png.Encode(&buf, img); err != nil {
		t.Fatalf("png.Encode: %v", err)
	}
	return buf.Bytes()
}

func TestWorkbookRelationshipsResolveToWorksheets(t *testing.T) {
	f := seedcalc.NewFile()
	f.NewSheet("Second")
	f.NewSheet("Third")
	parts := savedParts(t, f)
	if n := checkInternalTargetsResolve(t, parts, "xl/_rels/workbook.xml.rels"); n < 3 {
		t.Errorf("the workbook declares %d relationships, want at least one per sheet plus styles and theme", n)
	}
	checkInternalTargetsResolve(t, parts, "_rels/.rels")
}

func TestPictureRelationshipsResolve(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.AddPictureFromBytes("Sheet1", "B2", "", "swatch", ".png", pngBytes(t)); err != nil {
		t.Fatalf("AddPictureFromBytes: %v", err)
	}
	parts := savedParts(t, f)
	if _, ok := parts["xl/media/image1.png"]; !ok {
		t.Error("the package has no xl/media/image1.png part")
	}
	if _, ok := parts["xl/drawings/drawing1.xml"]; !ok {
		t.Error("the package has no xl/drawings/drawing1.xml part")
	}
	checkInternalTargetsResolve(t, parts, "xl/drawings/_rels/drawing1.xml.rels")
	checkInternalTargetsResolve(t, parts, "xl/worksheets/_rels/sheet1.xml.rels")
}

func TestChartRelationshipsResolve(t *testing.T) {
	f := seedcalc.NewFile()
	categories := []string{"Apple", "Banana", "Cherry"}
	values := []int{3, 5, 8}
	if err := f.SetSheetRow("Sheet1", "A1", &categories); err != nil {
		t.Fatalf("SetSheetRow: %v", err)
	}
	if err := f.SetSheetRow("Sheet1", "A2", &values); err != nil {
		t.Fatalf("SetSheetRow: %v", err)
	}
	format := `{"type":"col","series":[{"name":"Sheet1!$A$2","categories":"Sheet1!$A$1:$C$1","values":"Sheet1!$A$2:$C$2"}],"title":{"name":"Fruit"}}`
	if err := f.AddChart("Sheet1", "E1", format); err != nil {
		t.Fatalf("AddChart: %v", err)
	}
	parts := savedParts(t, f)
	if _, ok := parts["xl/charts/chart1.xml"]; !ok {
		t.Error("the package has no xl/charts/chart1.xml part")
	}
	checkInternalTargetsResolve(t, parts, "xl/drawings/_rels/drawing1.xml.rels")
	checkInternalTargetsResolve(t, parts, "xl/worksheets/_rels/sheet1.xml.rels")
}

func TestCommentRelationshipsResolve(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.AddComment("Sheet1", "B2", `{"author":"Author: ","text":"a note"}`); err != nil {
		t.Fatalf("AddComment: %v", err)
	}
	parts := savedParts(t, f)
	var comments, vml bool
	for name := range parts {
		if strings.HasPrefix(name, "xl/comments") {
			comments = true
		}
		if strings.HasPrefix(name, "xl/drawings/vmlDrawing") {
			vml = true
		}
	}
	if !comments {
		t.Error("the package has no comments part")
	}
	if !vml {
		t.Error("the package has no VML drawing part for the comment anchors")
	}
	checkInternalTargetsResolve(t, parts, "xl/worksheets/_rels/sheet1.xml.rels")
}

func TestExternalHyperlinkKeepsItsTargetMode(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetCellHyperLink("Sheet1", "A3", "https://example.invalid/report", "External"); err != nil {
		t.Fatalf("SetCellHyperLink: %v", err)
	}
	parts := savedParts(t, f)
	rels := parseRels(t, parts, "xl/worksheets/_rels/sheet1.xml.rels")
	var found bool
	for _, r := range rels.Rel {
		if r.Target == "https://example.invalid/report" {
			found = true
			if r.TargetMode != "External" {
				t.Errorf("the hyperlink relationship has TargetMode %q, want External", r.TargetMode)
			}
		}
	}
	if !found {
		t.Error("the worksheet declares no relationship for the external hyperlink")
	}
	ok, link, err := f.GetCellHyperLink("Sheet1", "A3")
	if err != nil {
		t.Fatalf("GetCellHyperLink: %v", err)
	}
	if !ok || link != "https://example.invalid/report" {
		t.Errorf("GetCellHyperLink = %v, %q", ok, link)
	}
}

func TestRelationshipIdentifiersAreUniqueWithinEachPart(t *testing.T) {
	f := seedcalc.NewFile()
	f.NewSheet("Second")
	if err := f.AddPictureFromBytes("Sheet1", "B2", "", "swatch", ".png", pngBytes(t)); err != nil {
		t.Fatalf("AddPictureFromBytes: %v", err)
	}
	if err := f.SetCellHyperLink("Sheet1", "A3", "https://example.invalid/a", "External"); err != nil {
		t.Fatalf("SetCellHyperLink: %v", err)
	}
	parts := savedParts(t, f)
	for name := range parts {
		if !strings.HasSuffix(name, ".rels") {
			continue
		}
		seen := map[string]bool{}
		for _, r := range parseRels(t, parts, name).Rel {
			if seen[r.ID] {
				t.Errorf("%s declares relationship id %s twice", name, r.ID)
			}
			seen[r.ID] = true
		}
	}
}

func TestRelationshipsSurviveSaveAndReopen(t *testing.T) {
	f := seedcalc.NewFile()
	f.NewSheet("Second")
	if err := f.AddPictureFromBytes("Sheet1", "B2", "", "swatch", ".png", pngBytes(t)); err != nil {
		t.Fatalf("AddPictureFromBytes: %v", err)
	}
	out := filepath.Join(t.TempDir(), "book.xlsx")
	if err := f.SaveAs(out); err != nil {
		t.Fatalf("SaveAs: %v", err)
	}
	g, err := seedcalc.OpenFile(out)
	if err != nil {
		t.Fatalf("OpenFile: %v", err)
	}
	parts := savedParts(t, g)
	checkInternalTargetsResolve(t, parts, "xl/_rels/workbook.xml.rels")
	checkInternalTargetsResolve(t, parts, "xl/drawings/_rels/drawing1.xml.rels")
	name, raw, err := g.GetPicture("Sheet1", "B2")
	if err != nil {
		t.Fatalf("GetPicture: %v", err)
	}
	if name == "" || len(raw) == 0 {
		t.Errorf("GetPicture after a reopen returned %q and %d bytes", name, len(raw))
	}
}
