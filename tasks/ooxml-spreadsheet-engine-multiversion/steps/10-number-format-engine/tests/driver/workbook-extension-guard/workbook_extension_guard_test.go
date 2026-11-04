// Scored group: workbook-extension-guard.
//
// SaveAs stops treating the path as an opaque destination. The extension is
// now read, checked against the five spreadsheet document types, and used to
// pick the content type declared for the workbook part -- so saving under
// .xlsm produces a macro-enabled workbook rather than an ordinary one with
// the wrong name.
//
// The content types are ECMA-376 Part 2 section 7.2.3 (Mapping media types
// to parts), whose 7.2.3.2.4 and 7.2.3.2.5 define the Default and Override
// declarations of [Content_Types].xml; the part names are ECMA-376 Part 1
// section 12.3.23 (Workbook Part).
package workbookextensionguard

import (
	"archive/zip"
	"io"
	"os"
	"path/filepath"
	"strings"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// contentTypes returns the [Content_Types].xml entries of a saved package
// that mention the workbook part or declare a default for an extension.
func contentTypes(t *testing.T, path string) string {
	t.Helper()
	zr, err := zip.OpenReader(path)
	if err != nil {
		t.Fatalf("zip.OpenReader(%s): %v", path, err)
	}
	defer zr.Close()
	for _, zf := range zr.File {
		if zf.Name != "[Content_Types].xml" {
			continue
		}
		rc, err := zf.Open()
		if err != nil {
			t.Fatalf("open [Content_Types].xml: %v", err)
		}
		b, err := io.ReadAll(rc)
		rc.Close()
		if err != nil {
			t.Fatalf("read [Content_Types].xml: %v", err)
		}
		return string(b)
	}
	t.Fatalf("%s has no [Content_Types].xml", path)
	return ""
}

func saveAs(t *testing.T, ext string) (string, error) {
	t.Helper()
	f := seedcalc.NewFile()
	path := filepath.Join(t.TempDir(), "book"+ext)
	return path, f.SaveAs(path)
}

func TestEachSpreadsheetExtensionSelectsItsContentType(t *testing.T) {
	for ext, want := range map[string]string{
		".xlsx": "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml",
		".xlsm": "application/vnd.ms-excel.sheet.macroEnabled.main+xml",
		".xltx": "application/vnd.openxmlformats-officedocument.spreadsheetml.template.main+xml",
		".xltm": "application/vnd.ms-excel.template.macroEnabled.main+xml",
		".xlam": "application/vnd.ms-excel.addin.macroEnabled.main+xml",
	} {
		path, err := saveAs(t, ext)
		if err != nil {
			t.Fatalf("SaveAs(%s): %v", ext, err)
		}
		override := `<Override PartName="/xl/workbook.xml" ContentType="` + want + `"></Override>`
		if got := contentTypes(t, path); !strings.Contains(got, override) {
			t.Fatalf("saving as %s did not declare %s", ext, override)
		}
	}
}

func TestTheFiveContentTypesAreAllDistinct(t *testing.T) {
	seen := map[string]string{}
	for _, ext := range []string{".xlsx", ".xlsm", ".xltx", ".xltm", ".xlam"} {
		path, err := saveAs(t, ext)
		if err != nil {
			t.Fatalf("SaveAs(%s): %v", ext, err)
		}
		body := contentTypes(t, path)
		const marker = `<Override PartName="/xl/workbook.xml" ContentType="`
		i := strings.Index(body, marker)
		if i < 0 {
			t.Fatalf("saving as %s declared no workbook content type", ext)
		}
		rest := body[i+len(marker):]
		ct := rest[:strings.Index(rest, `"`)]
		if prev, ok := seen[ct]; ok {
			t.Fatalf("%s and %s both declare %s", prev, ext, ct)
		}
		seen[ct] = ext
	}
	if len(seen) != 5 {
		t.Fatalf("got %d distinct content types, want 5", len(seen))
	}
}

func TestAnythingElseIsRefused(t *testing.T) {
	for _, ext := range []string{".txt", ".xls", ".zip", ""} {
		path, err := saveAs(t, ext)
		if err == nil || err.Error() != "unsupported workbook extension" {
			t.Fatalf("SaveAs(%q) = %v, want unsupported workbook extension", ext, err)
		}
		// Nothing is written when the extension is refused.
		if _, statErr := os.Stat(path); !os.IsNotExist(statErr) {
			t.Fatalf("SaveAs(%q) left a file behind at %s", ext, path)
		}
	}
}

func TestTheComparisonIsCaseSensitive(t *testing.T) {
	// The table is keyed by the lower-case extension, and the path's own
	// extension is used as it stands.
	for _, ext := range []string{".XLSX", ".Xlsx", ".XLSM"} {
		if _, err := saveAs(t, ext); err == nil || err.Error() != "unsupported workbook extension" {
			t.Fatalf("SaveAs(%q) = %v, want unsupported workbook extension", ext, err)
		}
	}
	if _, err := saveAs(t, ".xlsx"); err != nil {
		t.Fatalf("SaveAs(.xlsx): %v", err)
	}
}

func TestSavingAlsoDeclaresTheProjectExtension(t *testing.T) {
	// Picking a content type by extension also registers the default for the
	// binary VBA project part, whatever the chosen document type.
	const vba = `<Default Extension="bin" ContentType="application/vnd.ms-office.vbaProject"></Default>`
	for _, ext := range []string{".xlsx", ".xlsm", ".xlam"} {
		path, err := saveAs(t, ext)
		if err != nil {
			t.Fatalf("SaveAs(%s): %v", ext, err)
		}
		if got := contentTypes(t, path); !strings.Contains(got, vba) {
			t.Fatalf("saving as %s did not declare the project extension", ext)
		}
	}
}

func TestTheStreamingWriterIsNotGuarded(t *testing.T) {
	// Write takes a stream and has no path to inspect, so it still produces
	// the ordinary workbook content type and does not declare the project
	// extension.
	f := seedcalc.NewFile()
	path := filepath.Join(t.TempDir(), "streamed.xlsx")
	out, err := os.Create(path)
	if err != nil {
		t.Fatalf("create: %v", err)
	}
	if err := f.Write(out); err != nil {
		out.Close()
		t.Fatalf("Write: %v", err)
	}
	if err := out.Close(); err != nil {
		t.Fatalf("close: %v", err)
	}
	body := contentTypes(t, path)
	if !strings.Contains(body, "spreadsheetml.sheet.main+xml") {
		t.Fatalf("Write did not declare the ordinary workbook content type")
	}
	if strings.Contains(body, `Extension="bin"`) {
		t.Fatalf("Write declared the project extension: %s", body)
	}
	// The same workbook down the guarded path does declare it, which is
	// what makes this a difference between the two writers and not a
	// property of the workbook.
	saved, err := saveAs(t, ".xlsx")
	if err != nil {
		t.Fatalf("SaveAs(.xlsx): %v", err)
	}
	if !strings.Contains(contentTypes(t, saved), `Extension="bin"`) {
		t.Fatalf("SaveAs did not declare the project extension")
	}
}
