// Scored group: drawing-deletion (milestone 05, "The streaming writer and
// the drawing registry").
//
// Normative sources:
//   * TEST_INTERFACE.md section 4.05 -- "Deleting drawing anchors".
//   * ECMA-376 Part 1, 20.1.2.2.3 twoCellAnchor and 20.1.2.2.2 oneCellAnchor
//     for the anchor elements that DeleteChart/DeletePicture remove.
//   * drawing.go prepareDrawing/deleteDrawing: shared anchor removal for both
//     "Chart" and "Pic" predicates; returns nil when no Drawing element on
//     the sheet.
package drawingdeletion

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

// swatch returns the minimal 1x1 PNG that AddPicture accepts.
var swatch = func() []byte {
	// 1x1 white PNG, 67 bytes
	return []byte{
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
}()

func TestDeleteChartOnSheetWithNoDrawingIsNoop(t *testing.T) {
	f := seedcalc.NewFile()
	// Sheet1 has no drawing — must return nil without panicking.
	if err := f.DeleteChart("Sheet1", "A1"); err != nil {
		t.Fatalf("DeleteChart on a sheet with no drawing: %v", err)
	}
}

func TestDeletePictureOnSheetWithNoDrawingIsNoop(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.DeletePicture("Sheet1", "A1"); err != nil {
		t.Fatalf("DeletePicture on a sheet with no drawing: %v", err)
	}
}

func TestDeletePictureRemovesAnchorFromDrawingXML(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.AddPictureFromBytes("Sheet1", "B2", `{"x_scale":1,"y_scale":1}`,
		"test.png", ".png", swatch); err != nil {
		t.Fatalf("AddPictureFromBytes: %v", err)
	}

	// Verify drawing part exists before deletion.
	before := packageParts(t, f)
	found := false
	for k := range before {
		if strings.Contains(k, "drawings/drawing") {
			found = true
			break
		}
	}
	if !found {
		t.Fatal("no drawing part present after AddPictureFromBytes — can't test deletion")
	}

	// Delete at the same cell.
	if err := f.DeletePicture("Sheet1", "B2"); err != nil {
		t.Fatalf("DeletePicture: %v", err)
	}

	// Drawing XML should no longer contain a Pic anchor for B2 (col=1, row=1
	// zero-based), i.e. col index 1 and row index 1.
	after := packageParts(t, f)
	for k, v := range after {
		if strings.Contains(k, "drawings/drawing") {
			if strings.Contains(v, "<xdr:pic>") || strings.Contains(v, "<pic:nvPicPr>") {
				t.Errorf("drawing XML still contains a picture element after DeletePicture\npart %s:\n%s", k, v)
			}
		}
	}
}

func TestDeleteChartOnBadCellReturnsError(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.DeleteChart("Sheet1", "BADREF!"); err == nil {
		t.Error("DeleteChart with a bad cell reference must return an error")
	}
}

func TestDeletePictureOnBadCellReturnsError(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.DeletePicture("Sheet1", "BADREF!"); err == nil {
		t.Error("DeletePicture with a bad cell reference must return an error")
	}
}
