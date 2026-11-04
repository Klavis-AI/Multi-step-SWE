// Scored group: drawings-and-comments (milestone 03 peripheral surface).
//
// Pictures, shapes and comments share the drawing and media machinery that
// this milestone extends with TIFF support. Normative sources:
// TEST_INTERFACE.md sections 4.01, 4.02 and 4.03; ECMA-376 Part 1,
// 20.5 SpreadsheetML Drawing and 18.7 Comments.
package drawingsandcomments

import (
	"archive/zip"
	"bytes"
	"image"
	"image/color"
	"image/png"
	"io/ioutil"
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

// pngBytes builds a small in-memory PNG image.
func pngBytes(t *testing.T) []byte {
	t.Helper()
	img := image.NewRGBA(image.Rect(0, 0, 8, 8))
	for x := 0; x < 8; x++ {
		for y := 0; y < 8; y++ {
			img.Set(x, y, color.RGBA{R: uint8(x * 32), G: uint8(y * 32), B: 128, A: 255})
		}
	}
	var buf bytes.Buffer
	if err := png.Encode(&buf, img); err != nil {
		t.Fatalf("png.Encode: %v", err)
	}
	return buf.Bytes()
}

func TestAddPictureFromBytesCreatesMediaAndDrawing(t *testing.T) {
	f := seedcalc.NewFile()
	data := pngBytes(t)
	if err := f.AddPictureFromBytes("Sheet1", "B2", `{"x_scale":0.5,"y_scale":0.5}`, "picture", ".png", data); err != nil {
		t.Fatalf("AddPictureFromBytes: %v", err)
	}
	parts := savedParts(t, f)
	if _, ok := parts["xl/media/image1.png"]; !ok {
		t.Error("the package has no xl/media/image1.png part")
	}
	if _, ok := parts["xl/drawings/drawing1.xml"]; !ok {
		t.Error("the package has no xl/drawings/drawing1.xml part")
	}
	if !strings.Contains(parts["[Content_Types].xml"], `Extension="png"`) {
		t.Error("[Content_Types].xml declares no default for the png extension")
	}
	name, raw, err := f.GetPicture("Sheet1", "B2")
	if err != nil {
		t.Fatalf("GetPicture: %v", err)
	}
	if name != "image1.png" {
		t.Errorf("GetPicture name = %q, want %q", name, "image1.png")
	}
	if !bytes.Equal(raw, data) {
		t.Errorf("GetPicture returned %d bytes, want the %d bytes that were added", len(raw), len(data))
	}
}

func TestGetPictureOnACellWithoutOne(t *testing.T) {
	f := seedcalc.NewFile()
	name, raw, err := f.GetPicture("Sheet1", "A1")
	if err != nil {
		t.Fatalf("GetPicture on an empty worksheet = %v, want nil", err)
	}
	if name != "" || raw != nil {
		t.Errorf("GetPicture on a cell without a picture = (%q, %d bytes), want (\"\", nil)", name, len(raw))
	}
	if _, _, err := f.GetPicture("Sheet1", "not-a-cell"); err == nil {
		t.Error("GetPicture with a malformed cell reference returned nil, want a non-nil error")
	}
}

func TestAddShape(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.AddShape("Sheet1", "A1",
		`{"type":"rect","color":{"line":"#4286F4","fill":"#8eb9ff"},"paragraph":[{"text":"Rectangle"}],"width":180,"height":40}`); err != nil {
		t.Fatalf("AddShape: %v", err)
	}
	parts := savedParts(t, f)
	drawing, ok := parts["xl/drawings/drawing1.xml"]
	if !ok {
		t.Fatal("AddShape produced no xl/drawings/drawing1.xml part")
	}
	if !strings.Contains(drawing, "Rectangle") {
		t.Error("the drawing does not carry the shape's text")
	}
	if err := f.AddShape("NoSuchSheet", "A1", `{"type":"rect"}`); err == nil {
		t.Error("AddShape on an unknown worksheet returned nil, want a non-nil error")
	}
	if err := f.AddShape("Sheet1", "A1", `{"type":`); err == nil {
		t.Error("AddShape with a malformed format returned nil, want a non-nil error")
	}
}

func TestAddCommentCreatesTheCommentsPart(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.AddComment("Sheet1", "A3",
		`{"author":"Author: ","text":"This is a comment."}`); err != nil {
		t.Fatalf("AddComment: %v", err)
	}
	parts := savedParts(t, f)
	comments, ok := parts["xl/comments1.xml"]
	if !ok {
		t.Fatal("AddComment produced no xl/comments1.xml part")
	}
	if !strings.Contains(comments, "This is a comment.") {
		t.Error("the comments part does not carry the comment text")
	}
	if !strings.Contains(comments, `ref="A3"`) {
		t.Error("the comments part does not anchor the comment at A3")
	}
	if err := f.AddComment("NoSuchSheet", "A3", `{"author":"a","text":"b"}`); err == nil {
		t.Error("AddComment on an unknown worksheet returned nil, want a non-nil error")
	}
}
