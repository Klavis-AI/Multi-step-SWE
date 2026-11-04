// Gate group: drawing-registry (milestone 05, "The streaming writer and
// the drawing registry").
//
// Normative sources:
//   * TEST_INTERFACE.md section 4.05 -- "The drawing registry".
//   * drawing.go prepareDrawing: shared drawing XML allocation used by both
//     chart and picture paths.  A second chart on the same sheet reuses the
//     same drawing part rather than creating a second one.
//   * ECMA-376 Part 1, 20.1 DrawingML -- Spreadsheet for the twoCellAnchor
//     element that holds each embedded object.
package drawingregistry

import (
	"archive/zip"
	"bytes"
	"io/ioutil"
	"strings"
	"testing"
	_ "image/png"

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

var swatch = []byte{
	0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
	0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
	0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
	0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53,
	0xde, 0x00, 0x00, 0x00, 0x0c, 0x49, 0x44, 0x41,
	0x54, 0x08, 0xd7, 0x63, 0xf8, 0xcf, 0xc0, 0x00,
	0x00, 0x00, 0x02, 0x00, 0x01, 0xe2, 0x21, 0xbc,
	0x33, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e,
	0x44, 0xae, 0x42, 0x60, 0x82,
}

func countDrawingParts(parts map[string]string) int {
	n := 0
	for k := range parts {
		if strings.Contains(k, "xl/drawings/drawing") {
			n++
		}
	}
	return n
}

func TestTwoPicturesOnSameSheetShareOneDrawingPart(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.AddPictureFromBytes("Sheet1", "A1", `{"x_scale":1,"y_scale":1}`,
		"a.png", ".png", swatch); err != nil {
		t.Fatalf("AddPictureFromBytes A1: %v", err)
	}
	if err := f.AddPictureFromBytes("Sheet1", "C3", `{"x_scale":1,"y_scale":1}`,
		"b.png", ".png", swatch); err != nil {
		t.Fatalf("AddPictureFromBytes C3: %v", err)
	}
	parts := packageParts(t, f)
	n := countDrawingParts(parts)
	if n != 1 {
		t.Errorf("two pictures on the same sheet produced %d drawing parts, want exactly 1", n)
	}
}

func TestPicturesOnDifferentSheetsGetSeparateDrawingParts(t *testing.T) {
	f := seedcalc.NewFile()
	f.NewSheet("Sheet2")
	if err := f.AddPictureFromBytes("Sheet1", "A1", `{"x_scale":1,"y_scale":1}`,
		"a.png", ".png", swatch); err != nil {
		t.Fatalf("AddPictureFromBytes Sheet1!A1: %v", err)
	}
	if err := f.AddPictureFromBytes("Sheet2", "A1", `{"x_scale":1,"y_scale":1}`,
		"b.png", ".png", swatch); err != nil {
		t.Fatalf("AddPictureFromBytes Sheet2!A1: %v", err)
	}
	parts := packageParts(t, f)
	n := countDrawingParts(parts)
	if n != 2 {
		t.Errorf("pictures on two different sheets produced %d drawing parts, want 2", n)
	}
}

func TestDrawingPartHasRelationshipFromWorksheet(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.AddPictureFromBytes("Sheet1", "B2", `{"x_scale":1,"y_scale":1}`,
		"x.png", ".png", swatch); err != nil {
		t.Fatalf("AddPictureFromBytes: %v", err)
	}
	parts := packageParts(t, f)
	rels := parts["xl/worksheets/_rels/sheet1.xml.rels"]
	if !strings.Contains(rels, "drawings/drawing") {
		t.Errorf("worksheet rels do not reference the drawing part\ngot: %s", rels)
	}
}
