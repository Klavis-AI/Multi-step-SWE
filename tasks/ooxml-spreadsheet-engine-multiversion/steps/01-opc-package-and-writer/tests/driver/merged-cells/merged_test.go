// Scored group: merged-cells.
//
// The mergeCells collection of a worksheet (ECMA-376 Part 1 clause 18.3.1.55).
// A merged range is written as one mergeCell whose ref is the normalised
// top-left:bottom-right reference, and every cell of the range answers with the
// value held at the anchor.
package mergedcells

import (
	"archive/zip"
	"bytes"
	"encoding/xml"
	"sort"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

func writePackage(t *testing.T, f *seedcalc.File) map[string][]byte {
	t.Helper()
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	zr, err := zip.NewReader(bytes.NewReader(buf.Bytes()), int64(buf.Len()))
	if err != nil {
		t.Fatalf("written package is not a ZIP container: %v", err)
	}
	parts := map[string][]byte{}
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
		parts[e.Name] = b.Bytes()
	}
	return parts
}

// mergeRefs returns the ref of every mergeCell on the first worksheet, sorted.
func mergeRefs(t *testing.T, f *seedcalc.File) []string {
	t.Helper()
	parts := writePackage(t, f)
	body, ok := parts["xl/worksheets/sheet1.xml"]
	if !ok {
		t.Fatal("the package has no first worksheet part")
	}
	var ws struct {
		MergeCells struct {
			MergeCell []struct {
				Ref string `xml:"ref,attr"`
			} `xml:"mergeCell"`
		} `xml:"mergeCells"`
	}
	if err := xml.Unmarshal(body, &ws); err != nil {
		t.Fatalf("the worksheet part is not well-formed XML: %v", err)
	}
	refs := []string{}
	for _, m := range ws.MergeCells.MergeCell {
		refs = append(refs, m.Ref)
	}
	sort.Strings(refs)
	return refs
}

func TestAMergedRangeIsWrittenAsANormalisedReference(t *testing.T) {
	f := seedcalc.NewFile()
	// The corners are given bottom-right first; the reference must still come
	// out top-left first.
	f.MergeCell("Sheet1", "C3", "A1")

	got := mergeRefs(t, f)
	if len(got) != 1 || got[0] != "A1:C3" {
		t.Errorf("mergeCells = %v, want exactly one range %q", got, "A1:C3")
	}
}

func TestCellsInsideAMergedRangeReadTheAnchorValue(t *testing.T) {
	f := seedcalc.NewFile()
	f.SetCellValue("Sheet1", "A1", "anchor")
	f.MergeCell("Sheet1", "A1", "C3")

	for _, axis := range []string{"A1", "B2", "C3", "A3", "C1"} {
		if got := f.GetCellValue("Sheet1", axis); got != "anchor" {
			t.Errorf("GetCellValue(%q) inside the merge = %q, want %q", axis, got, "anchor")
		}
	}
	if got := f.GetCellValue("Sheet1", "D4"); got != "" {
		t.Errorf("GetCellValue(D4) outside the merge = %q, want the empty string", got)
	}
}

func TestWritingThroughAMergedCellUpdatesTheAnchor(t *testing.T) {
	f := seedcalc.NewFile()
	f.SetCellValue("Sheet1", "A1", "before")
	f.MergeCell("Sheet1", "A1", "C3")

	f.SetCellValue("Sheet1", "B2", "after")
	if got := f.GetCellValue("Sheet1", "A1"); got != "after" {
		t.Errorf("the anchor A1 = %q after writing to B2, want %q", got, "after")
	}
	if got := f.GetCellValue("Sheet1", "B2"); got != "after" {
		t.Errorf("B2 = %q, want %q", got, "after")
	}
}

func TestSeveralMergedRangesCoexist(t *testing.T) {
	f := seedcalc.NewFile()
	f.SetCellValue("Sheet1", "A1", "first")
	f.SetCellValue("Sheet1", "E5", "second")
	f.MergeCell("Sheet1", "A1", "B2")
	f.MergeCell("Sheet1", "E5", "G7")

	got := mergeRefs(t, f)
	want := []string{"A1:B2", "E5:G7"}
	if len(got) != len(want) {
		t.Fatalf("mergeCells = %v, want %v", got, want)
	}
	for i := range want {
		if got[i] != want[i] {
			t.Errorf("mergeCells = %v, want %v", got, want)
			break
		}
	}
	if v := f.GetCellValue("Sheet1", "B2"); v != "first" {
		t.Errorf("B2 = %q, want the first range's anchor", v)
	}
	if v := f.GetCellValue("Sheet1", "F6"); v != "second" {
		t.Errorf("F6 = %q, want the second range's anchor", v)
	}
}

func TestMergedRangesSurviveTheRoundTrip(t *testing.T) {
	f := seedcalc.NewFile()
	f.SetCellValue("Sheet1", "A1", "anchor")
	f.MergeCell("Sheet1", "A1", "C3")

	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	g, err := seedcalc.OpenReader(bytes.NewReader(buf.Bytes()))
	if err != nil {
		t.Fatalf("OpenReader: %v", err)
	}
	if got := mergeRefs(t, g); len(got) != 1 || got[0] != "A1:C3" {
		t.Errorf("after the round trip mergeCells = %v, want %q", got, "A1:C3")
	}
	if got := g.GetCellValue("Sheet1", "B2"); got != "anchor" {
		t.Errorf("after the round trip B2 = %q, want %q", got, "anchor")
	}
}
