// Scored group: pictures-and-comments.
//
// Images added from memory, how they are anchored and linked, and the
// comments a worksheet carries. Graded against ECMA-376 Part 1 clause 20.5.2.33
// (twoCellAnchor, whose editAs attribute records how the picture moves with
// the cells beneath it), clause 21.1.2.3.5 (hlinkClick), clause 18.7.3
// (comment) and clause 18.7.6 (comments), together with ECMA-376 Part 2
// clause 9.3 (relationship parts, and the External target mode). Behaviour
// cases were checked against the upstream release this checkpoint tracks
// (tag v1.4.1).
package picturesandcomments

import (
	"archive/zip"
	"bytes"
	"encoding/xml"
	"image"
	"image/color"
	"image/png"
	"strings"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

func tinyPNG(t *testing.T) []byte {
	t.Helper()
	img := image.NewRGBA(image.Rect(0, 0, 2, 2))
	img.Set(0, 0, color.RGBA{R: 255, A: 255})
	img.Set(1, 1, color.RGBA{B: 255, A: 255})
	var buf bytes.Buffer
	if err := png.Encode(&buf, img); err != nil {
		t.Fatalf("encode png: %v", err)
	}
	return buf.Bytes()
}

func packageParts(t *testing.T, f *seedcalc.File) map[string][]byte {
	t.Helper()
	buf, err := f.WriteToBuffer()
	if err != nil {
		t.Fatalf("WriteToBuffer: %v", err)
	}
	b := buf.Bytes()
	zr, err := zip.NewReader(bytes.NewReader(b), int64(len(b)))
	if err != nil {
		t.Fatalf("written package is not a ZIP container: %v", err)
	}
	out := make(map[string][]byte, len(zr.File))
	for _, fh := range zr.File {
		rc, err := fh.Open()
		if err != nil {
			t.Fatalf("open %s: %v", fh.Name, err)
		}
		var raw bytes.Buffer
		if _, err := raw.ReadFrom(rc); err != nil {
			rc.Close()
			t.Fatalf("read %s: %v", fh.Name, err)
		}
		rc.Close()
		out[fh.Name] = raw.Bytes()
	}
	return out
}

func TestAddPictureFromBytesStoresTheMediaPart(t *testing.T) {
	raw := tinyPNG(t)
	f := seedcalc.NewFile()
	if err := f.AddPictureFromBytes("Sheet1", "A1", "", "Logo", ".png", raw); err != nil {
		t.Fatalf("AddPictureFromBytes: %v", err)
	}
	parts := packageParts(t, f)

	var media []string
	for name := range parts {
		if strings.HasPrefix(name, "xl/media/") {
			media = append(media, name)
		}
	}
	if len(media) != 1 {
		t.Fatalf("package carries %d media parts, want 1: %v", len(media), media)
	}
	if !strings.HasSuffix(media[0], ".png") {
		t.Errorf("media part %q does not keep the image extension", media[0])
	}
	if !bytes.Equal(parts[media[0]], raw) {
		t.Error("the stored media part is not the image that was supplied")
	}
	if _, ok := parts["xl/drawings/drawing1.xml"]; !ok {
		t.Error("adding a picture produced no drawing part")
	}
	if _, ok := parts["xl/drawings/_rels/drawing1.xml.rels"]; !ok {
		t.Error("the drawing part has no relationships part")
	}
}

func TestGetPictureReturnsTheStoredImage(t *testing.T) {
	raw := tinyPNG(t)
	f := seedcalc.NewFile()
	if err := f.AddPictureFromBytes("Sheet1", "B3", "", "Logo", ".png", raw); err != nil {
		t.Fatalf("AddPictureFromBytes: %v", err)
	}
	name, content := f.GetPicture("Sheet1", "B3")
	if name == "" {
		t.Fatal("GetPicture returned no file name for a cell that carries a picture")
	}
	if !strings.HasSuffix(name, ".png") {
		t.Errorf("GetPicture returned the name %q, want a .png file", name)
	}
	if !bytes.Equal(content, raw) {
		t.Errorf("GetPicture returned %d bytes, want the %d bytes that were added", len(content), len(raw))
	}
	emptyName, emptyContent := f.GetPicture("Sheet1", "Z9")
	if emptyName != "" || len(emptyContent) != 0 {
		t.Errorf("GetPicture on a cell with no picture returned (%q, %d bytes)", emptyName, len(emptyContent))
	}
}

func TestAddPictureRejectsInputItCannotStore(t *testing.T) {
	raw := tinyPNG(t)
	f := seedcalc.NewFile()
	if err := f.AddPictureFromBytes("Sheet1", "A1", "", "Logo", ".bmp", raw); err == nil {
		t.Error("AddPictureFromBytes accepted an unsupported image extension")
	}
	if err := f.AddPictureFromBytes("Sheet1", "A1", `{`, "Logo", ".png", raw); err == nil {
		t.Error("AddPictureFromBytes accepted a malformed format descriptor")
	}
	for _, ext := range []string{".png", ".jpg", ".jpeg", ".gif"} {
		g := seedcalc.NewFile()
		if err := g.AddPictureFromBytes("Sheet1", "A1", "", "Logo", ext, raw); err != nil {
			t.Errorf("AddPictureFromBytes rejected the supported extension %q: %v", ext, err)
		}
	}
}

func TestPicturePositioningIsRecordedOnTheAnchor(t *testing.T) {
	raw := tinyPNG(t)
	f := seedcalc.NewFile()
	if err := f.AddPictureFromBytes("Sheet1", "A1", `{"positioning":"oneCell"}`, "One", ".png", raw); err != nil {
		t.Fatalf("AddPictureFromBytes: %v", err)
	}
	if err := f.AddPictureFromBytes("Sheet1", "C5", `{"positioning":"absolute"}`, "Two", ".png", raw); err != nil {
		t.Fatalf("AddPictureFromBytes: %v", err)
	}
	if err := f.AddPictureFromBytes("Sheet1", "E9", "", "Three", ".png", raw); err != nil {
		t.Fatalf("AddPictureFromBytes: %v", err)
	}

	drawing := string(packageParts(t, f)["xl/drawings/drawing1.xml"])
	if drawing == "" {
		t.Fatal("package has no drawing part")
	}
	if !strings.Contains(drawing, `editAs="oneCell"`) {
		t.Error("a picture positioned oneCell did not record editAs=\"oneCell\"")
	}
	if !strings.Contains(drawing, `editAs="absolute"`) {
		t.Error("a picture positioned absolute did not record editAs=\"absolute\"")
	}
	if n := strings.Count(drawing, "<xdr:twoCellAnchor"); n != 3 {
		t.Errorf("drawing carries %d anchors, want 3", n)
	}
	if n := strings.Count(drawing, "editAs="); n != 2 {
		t.Errorf("drawing carries %d editAs attributes, want 2 -- the default positioning writes none", n)
	}
}

func TestImageHyperlinkBecomesADrawingRelationship(t *testing.T) {
	raw := tinyPNG(t)
	f := seedcalc.NewFile()
	format := `{"hyperlink":"https://example.com/target","hyperlink_type":"External"}`
	if err := f.AddPictureFromBytes("Sheet1", "A1", format, "Linked", ".png", raw); err != nil {
		t.Fatalf("AddPictureFromBytes: %v", err)
	}
	parts := packageParts(t, f)

	drawing := string(parts["xl/drawings/drawing1.xml"])
	if !strings.Contains(drawing, "hlinkClick") {
		t.Error("a picture with a hyperlink carries no hlinkClick element")
	}

	var rels struct {
		Rel []struct {
			ID         string `xml:"Id,attr"`
			Type       string `xml:"Type,attr"`
			Target     string `xml:"Target,attr"`
			TargetMode string `xml:"TargetMode,attr"`
		} `xml:"Relationship"`
	}
	if err := xml.Unmarshal(parts["xl/drawings/_rels/drawing1.xml.rels"], &rels); err != nil {
		t.Fatalf("drawing relationships part is not well-formed XML: %v", err)
	}
	found := false
	for _, r := range rels.Rel {
		if !strings.HasSuffix(r.Type, "/hyperlink") {
			continue
		}
		found = true
		if r.Target != "https://example.com/target" {
			t.Errorf("hyperlink relationship targets %q, want %q", r.Target, "https://example.com/target")
		}
		if r.TargetMode != "External" {
			t.Errorf("hyperlink relationship TargetMode = %q, want %q", r.TargetMode, "External")
		}
	}
	if !found {
		t.Errorf("the drawing declares no hyperlink relationship: %+v", rels.Rel)
	}
}

func TestGetCommentsReturnsCommentsPerWorksheet(t *testing.T) {
	f := seedcalc.NewFile()
	f.NewSheet("Second")
	if err := f.AddComment("Sheet1", "B2", `{"author":"Reviewer: ","text":"look here"}`); err != nil {
		t.Fatalf("AddComment: %v", err)
	}
	if err := f.AddComment("Second", "C3", `{"author":"Reviewer: ","text":"and here"}`); err != nil {
		t.Fatalf("AddComment: %v", err)
	}
	if err := f.AddComment("Sheet1", "D4", `{`); err == nil {
		t.Error("AddComment accepted a malformed format descriptor")
	}

	comments := f.GetComments()
	if len(comments) != 2 {
		t.Fatalf("GetComments returned %d worksheets, want 2: %v", len(comments), comments)
	}
	first := comments["Sheet1"]
	if len(first) != 1 {
		t.Fatalf("Sheet1 carries %d comments, want 1: %v", len(first), first)
	}
	if first[0].Ref != "B2" {
		t.Errorf("comment ref = %q, want %q", first[0].Ref, "B2")
	}
	if first[0].Author != "Reviewer: " {
		t.Errorf("comment author = %q, want %q", first[0].Author, "Reviewer: ")
	}
	if first[0].AuthorID != 0 {
		t.Errorf("comment AuthorID = %d, want 0 -- each worksheet's comments part carries a single author entry", first[0].AuthorID)
	}
	if !strings.Contains(first[0].Text, "look here") {
		t.Errorf("comment text = %q, want it to contain %q", first[0].Text, "look here")
	}
	second := comments["Second"]
	if len(second) != 1 || second[0].Ref != "C3" {
		t.Errorf("Second carries %v, want one comment on C3", second)
	}
	if len(second) == 1 && second[0].AuthorID != 0 {
		t.Errorf("comment AuthorID = %d, want 0", second[0].AuthorID)
	}
	if got := seedcalc.NewFile().GetComments(); len(got) != 0 {
		t.Errorf("a workbook with no comments returned %v", got)
	}
}
