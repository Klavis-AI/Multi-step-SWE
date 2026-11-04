// Scored group: package-robustness.
//
// What the reader does with input that is not a well-formed workbook. A ZIP
// container that fails ECMA-376 Part 2 clause 9 (no [Content_Types].xml, no
// reachable workbook part, a relationship whose target is absent) must be
// reported or degrade to a defined empty result -- never crash the caller and
// never invent content.
package packagerobustness

import (
	"archive/zip"
	"bytes"
	"os"
	"path/filepath"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

func assetPath(t *testing.T, name string) string {
	t.Helper()
	dir := os.Getenv("SEEDCALC_ASSETS")
	if dir == "" {
		t.Fatal("SEEDCALC_ASSETS is not set")
	}
	return filepath.Join(dir, name)
}

func assetBytes(t *testing.T, name string) []byte {
	t.Helper()
	b, err := os.ReadFile(assetPath(t, name))
	if err != nil {
		t.Fatalf("read fixture %s: %v", name, err)
	}
	return b
}

func TestOpenFileRejectsAPathThatDoesNotExist(t *testing.T) {
	missing := filepath.Join(t.TempDir(), "absent.xlsx")
	if _, err := seedcalc.OpenFile(missing); err == nil {
		t.Error("OpenFile returned no error for a path that does not exist")
	}
}

func TestOpenReaderRejectsInputThatIsNotAZipContainer(t *testing.T) {
	whole := assetBytes(t, "typed-cells.xlsx")
	cases := map[string][]byte{
		"an empty stream":       {},
		"arbitrary bytes":       []byte("not a package at all"),
		"a truncated container": whole[:len(whole)/2],
		"a local header alone":  []byte("PK\x03\x04 and nothing else"),
	}
	for why, body := range cases {
		if _, err := seedcalc.OpenReader(bytes.NewReader(body)); err == nil {
			t.Errorf("OpenReader accepted %s", why)
		}
	}
}

func TestAZipWithoutTheOpcDeclarationsHasNoWorksheets(t *testing.T) {
	f, err := seedcalc.OpenReader(bytes.NewReader(assetBytes(t, "no-content-types.xlsx")))
	if err != nil {
		t.Fatalf("OpenReader on a ZIP with no OPC declarations: %v", err)
	}
	if got := f.GetSheetMap(); len(got) != 0 {
		t.Errorf("GetSheetMap = %v, want no worksheets", got)
	}
	if got := f.GetSheetName(1); got != "" {
		t.Errorf("GetSheetName(1) = %q, want the empty string", got)
	}
	if got := f.GetSheetIndex("Sheet1"); got != 0 {
		t.Errorf("GetSheetIndex(\"Sheet1\") = %d, want 0", got)
	}
}

func TestAWorkbookWhoseWorksheetPartIsMissingReadsAsEmpty(t *testing.T) {
	// The fixture declares Sheet1 in xl/workbook.xml and relates it to
	// worksheets/sheet1.xml, but that part is not in the container.
	f, err := seedcalc.OpenReader(bytes.NewReader(assetBytes(t, "dangling-relationship.xlsx")))
	if err != nil {
		t.Fatalf("OpenReader on a package with a dangling worksheet relationship: %v", err)
	}
	m := f.GetSheetMap()
	if len(m) != 1 {
		t.Fatalf("GetSheetMap = %v, want the one sheet the workbook declares", m)
	}
	if got := f.GetCellValue("Sheet1", "A1"); got != "" {
		t.Errorf("GetCellValue on a sheet with no part = %q, want the empty string", got)
	}
	if got := f.GetRows("Sheet1"); len(got) != 0 {
		t.Errorf("GetRows on a sheet with no part = %v, want no rows", got)
	}
}

func TestReadZipReaderExposesThePartsOfTheContainer(t *testing.T) {
	raw := assetBytes(t, "typed-cells.xlsx")
	zr, err := zip.NewReader(bytes.NewReader(raw), int64(len(raw)))
	if err != nil {
		t.Fatalf("the fixture is not a ZIP container: %v", err)
	}
	parts, sheets, err := seedcalc.ReadZipReader(zr)
	if err != nil {
		t.Fatalf("ReadZipReader: %v", err)
	}
	for _, want := range []string{
		"[Content_Types].xml",
		"_rels/.rels",
		"xl/workbook.xml",
		"xl/_rels/workbook.xml.rels",
		"xl/worksheets/sheet1.xml",
		"xl/sharedStrings.xml",
	} {
		if _, ok := parts[want]; !ok {
			t.Errorf("ReadZipReader did not report part %s", want)
		}
	}
	if sheets != 1 {
		t.Errorf("ReadZipReader reported %d worksheets, want 1", sheets)
	}
}

func TestSaveNeedsAPathAndSaveAsSuppliesOne(t *testing.T) {
	f := seedcalc.NewFile()
	f.SetCellValue("Sheet1", "A1", "kept")

	if err := f.Save(); err == nil {
		t.Error("Save on a workbook that was never associated with a path returned no error")
	}

	path := filepath.Join(t.TempDir(), "out.xlsx")
	if err := f.SaveAs(path); err != nil {
		t.Fatalf("SaveAs: %v", err)
	}
	g, err := seedcalc.OpenFile(path)
	if err != nil {
		t.Fatalf("OpenFile on the package SaveAs wrote: %v", err)
	}
	if got := g.GetCellValue("Sheet1", "A1"); got != "kept" {
		t.Errorf("A1 in the saved package = %q, want %q", got, "kept")
	}
	// A workbook that was opened from a path can be saved back to it.
	if err := g.Save(); err != nil {
		t.Errorf("Save on a workbook opened from a path: %v", err)
	}
}
