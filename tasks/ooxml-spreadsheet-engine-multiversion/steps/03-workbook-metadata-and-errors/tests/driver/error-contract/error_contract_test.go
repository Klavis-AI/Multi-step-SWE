// Scored group: error-contract (milestone 03, "Workbook metadata and the
// error contract").
//
// Normative sources:
//   * TEST_INTERFACE.md section 4.03 -- "The error migration", "Failure
//     reporting on open".
//   * Behaviour cross-checked against the Oracle checkpoint v2.0.1 and the
//     upstream test corpus at tag v2.0.1 (excelize_test.go, TestOpenReader).
//
// Every mutating entry point returns error from this milestone on. Every
// reading entry point that can fail returns (value, error). Nothing here
// asserts on the text of an error: only nil / non-nil and errors.Is identity.
package errorcontract

import (
	"archive/zip"
	"bytes"
	"errors"
	"path/filepath"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// missing is a worksheet name that is never present in a new workbook.
const missing = "NoSuchSheet"

// mutator names one mutating entry point and applies it to a worksheet name.
type mutator struct {
	name string
	call func(f *seedcalc.File, sheet string) error
}

// mutators lists the mutating entry points whose signature changed in this
// milestone to return error. Each one must report an unknown worksheet.
func mutators() []mutator {
	return []mutator{
		{"SetCellValue", func(f *seedcalc.File, s string) error { return f.SetCellValue(s, "A1", "v") }},
		{"SetCellStr", func(f *seedcalc.File, s string) error { return f.SetCellStr(s, "A1", "v") }},
		{"SetCellInt", func(f *seedcalc.File, s string) error { return f.SetCellInt(s, "A1", 7) }},
		{"SetCellBool", func(f *seedcalc.File, s string) error { return f.SetCellBool(s, "A1", true) }},
		{"SetCellFloat", func(f *seedcalc.File, s string) error { return f.SetCellFloat(s, "A1", 1.5, 2, 64) }},
		{"SetCellDefault", func(f *seedcalc.File, s string) error { return f.SetCellDefault(s, "A1", "v") }},
		{"SetCellFormula", func(f *seedcalc.File, s string) error { return f.SetCellFormula(s, "A1", "SUM(B1:B2)") }},
		{"SetCellStyle", func(f *seedcalc.File, s string) error { return f.SetCellStyle(s, "A1", "B2", 0) }},
		{"SetCellHyperLink", func(f *seedcalc.File, s string) error {
			return f.SetCellHyperLink(s, "A1", "https://example.com", "External")
		}},
		{"MergeCell", func(f *seedcalc.File, s string) error { return f.MergeCell(s, "A1", "B2") }},
		{"SetRowHeight", func(f *seedcalc.File, s string) error { return f.SetRowHeight(s, 1, 25) }},
		{"SetRowVisible", func(f *seedcalc.File, s string) error { return f.SetRowVisible(s, 1, false) }},
		{"SetRowOutlineLevel", func(f *seedcalc.File, s string) error { return f.SetRowOutlineLevel(s, 1, 1) }},
		{"InsertRow", func(f *seedcalc.File, s string) error { return f.InsertRow(s, 1) }},
		{"RemoveRow", func(f *seedcalc.File, s string) error { return f.RemoveRow(s, 1) }},
		{"DuplicateRow", func(f *seedcalc.File, s string) error { return f.DuplicateRow(s, 1) }},
		{"DuplicateRowTo", func(f *seedcalc.File, s string) error { return f.DuplicateRowTo(s, 1, 3) }},
		{"SetColWidth", func(f *seedcalc.File, s string) error { return f.SetColWidth(s, "A", "C", 12) }},
		{"SetColVisible", func(f *seedcalc.File, s string) error { return f.SetColVisible(s, "A", false) }},
		{"SetColOutlineLevel", func(f *seedcalc.File, s string) error { return f.SetColOutlineLevel(s, "A", 1) }},
		{"SetColStyle", func(f *seedcalc.File, s string) error { return f.SetColStyle(s, "A:C", 0) }},
		{"InsertCol", func(f *seedcalc.File, s string) error { return f.InsertCol(s, "A") }},
		{"RemoveCol", func(f *seedcalc.File, s string) error { return f.RemoveCol(s, "A") }},
		{"SetPanes", func(f *seedcalc.File, s string) error {
			return f.SetPanes(s, `{"freeze":true,"split":false,"x_split":1,"y_split":0,"top_left_cell":"B1","active_pane":"topRight"}`)
		}},
		{"ProtectSheet", func(f *seedcalc.File, s string) error {
			return f.ProtectSheet(s, &seedcalc.FormatSheetProtection{Password: "pw"})
		}},
		{"UnprotectSheet", func(f *seedcalc.File, s string) error { return f.UnprotectSheet(s) }},
		{"SetHeaderFooter", func(f *seedcalc.File, s string) error {
			return f.SetHeaderFooter(s, &seedcalc.FormatHeaderFooter{OddHeader: "&Ctitle"})
		}},
		{"SetPageLayout", func(f *seedcalc.File, s string) error {
			return f.SetPageLayout(s, seedcalc.PageLayoutOrientation(seedcalc.OrientationLandscape))
		}},
	}
}

func TestMutatorsReportMissingSheet(t *testing.T) {
	for _, m := range mutators() {
		f := seedcalc.NewFile()
		if err := m.call(f, missing); err == nil {
			t.Errorf("%s on an unknown worksheet returned nil, want a non-nil error", m.name)
		}
	}
}

func TestMutatorsReturnNilOnSuccess(t *testing.T) {
	for _, m := range mutators() {
		f := seedcalc.NewFile()
		// InsertRow/RemoveRow/DuplicateRow* need a row to work on.
		if err := f.SetCellValue("Sheet1", "A1", "seed"); err != nil {
			t.Fatalf("SetCellValue: %v", err)
		}
		if err := m.call(f, "Sheet1"); err != nil {
			t.Errorf("%s on an existing worksheet returned %v, want nil", m.name, err)
		}
	}
}

func TestAccessorsReturnValueAndError(t *testing.T) {
	f := seedcalc.NewFile()

	if v, err := f.GetCellValue(missing, "A1"); err == nil || v != "" {
		t.Errorf("GetCellValue = (%q, %v), want (\"\", non-nil)", v, err)
	}
	if v, err := f.GetCellFormula(missing, "A1"); err == nil || v != "" {
		t.Errorf("GetCellFormula = (%q, %v), want (\"\", non-nil)", v, err)
	}
	if v, err := f.GetCellStyle(missing, "A1"); err == nil || v != 0 {
		t.Errorf("GetCellStyle = (%d, %v), want (0, non-nil)", v, err)
	}
	if ok, target, err := f.GetCellHyperLink(missing, "A1"); err == nil || ok || target != "" {
		t.Errorf("GetCellHyperLink = (%v, %q, %v), want (false, \"\", non-nil)", ok, target, err)
	}
	if _, err := f.GetRowHeight(missing, 1); err == nil {
		t.Error("GetRowHeight on an unknown worksheet returned nil error")
	}
	if _, err := f.GetRowVisible(missing, 1); err == nil {
		t.Error("GetRowVisible on an unknown worksheet returned nil error")
	}
	if _, err := f.GetRowOutlineLevel(missing, 1); err == nil {
		t.Error("GetRowOutlineLevel on an unknown worksheet returned nil error")
	}
	if _, err := f.GetColWidth(missing, "A"); err == nil {
		t.Error("GetColWidth on an unknown worksheet returned nil error")
	}
	if _, err := f.GetColVisible(missing, "A"); err == nil {
		t.Error("GetColVisible on an unknown worksheet returned nil error")
	}
	if _, err := f.GetColOutlineLevel(missing, "A"); err == nil {
		t.Error("GetColOutlineLevel on an unknown worksheet returned nil error")
	}
	if cells, err := f.GetMergeCells(missing); err == nil || len(cells) != 0 {
		t.Errorf("GetMergeCells = (%v, %v), want (empty, non-nil)", cells, err)
	}
	if _, raw, err := f.GetPicture(missing, "A1"); err == nil || raw != nil {
		t.Errorf("GetPicture = (%v, %v), want (nil, non-nil)", raw, err)
	}
	if hits, err := f.SearchSheet(missing, "x"); err == nil || len(hits) != 0 {
		t.Errorf("SearchSheet = (%v, %v), want (empty, non-nil)", hits, err)
	}
	if _, err := f.Rows(missing); err == nil {
		t.Error("Rows on an unknown worksheet returned nil error")
	}
	var orientation seedcalc.PageLayoutOrientation
	if err := f.GetPageLayout(missing, &orientation); err == nil {
		t.Error("GetPageLayout on an unknown worksheet returned nil error")
	}
	if err := f.UpdateLinkedValue(); err != nil {
		t.Errorf("UpdateLinkedValue on a healthy workbook = %v, want nil", err)
	}
}

func TestInvalidCellReferenceIsAnError(t *testing.T) {
	f := seedcalc.NewFile()
	bad := []string{"", "A", "1", "1A", "A0", "-A1", "A1:B2", " A1", "$A$1"}
	for _, ref := range bad {
		if err := f.SetCellValue("Sheet1", ref, "v"); err == nil {
			t.Errorf("SetCellValue(%q) returned nil, want a non-nil error", ref)
		}
		if v, err := f.GetCellValue("Sheet1", ref); err == nil || v != "" {
			t.Errorf("GetCellValue(%q) = (%q, %v), want (\"\", non-nil)", ref, v, err)
		}
		if err := f.SetCellStyle("Sheet1", ref, "B2", 0); err == nil {
			t.Errorf("SetCellStyle(%q, ...) returned nil, want a non-nil error", ref)
		}
	}
	// A well formed reference is still accepted.
	if err := f.SetCellValue("Sheet1", "B2", "v"); err != nil {
		t.Errorf("SetCellValue(\"B2\") = %v, want nil", err)
	}
}

func TestOpenFailureYieldsNilFileAndZipFormatError(t *testing.T) {
	f, err := seedcalc.OpenFile(filepath.Join(t.TempDir(), "absent.xlsx"))
	if err == nil {
		t.Fatal("OpenFile on a missing path returned a nil error")
	}
	if f != nil {
		t.Error("OpenFile on a missing path returned a non-nil *File")
	}

	f, err = seedcalc.OpenReader(bytes.NewReader([]byte("this stream is not a zip archive")))
	if err == nil {
		t.Fatal("OpenReader on a non-zip stream returned a nil error")
	}
	if f != nil {
		t.Error("OpenReader on a non-zip stream returned a non-nil *File")
	}
	if !errors.Is(err, zip.ErrFormat) {
		t.Errorf("OpenReader on a non-zip stream: errors.Is(err, zip.ErrFormat) = false, err = %v", err)
	}
}

func TestEncryptedPackageIsReportedDistinctly(t *testing.T) {
	// [MS-OFFCRYPTO] v20181211 3.1: the DataSpaces feature identifier that
	// marks an encrypted OLE-wrapped package.
	identifier := []byte{
		0x3c, 0x00, 0x00, 0x00, 0x4d, 0x00, 0x69, 0x00, 0x63, 0x00, 0x72, 0x00, 0x6f, 0x00, 0x73, 0x00,
		0x6f, 0x00, 0x66, 0x00, 0x74, 0x00, 0x2e, 0x00, 0x43, 0x00, 0x6f, 0x00, 0x6e, 0x00, 0x74, 0x00,
		0x61, 0x00, 0x69, 0x00, 0x6e, 0x00, 0x65, 0x00, 0x72, 0x00, 0x2e, 0x00, 0x44, 0x00, 0x61, 0x00,
		0x74, 0x00, 0x61, 0x00, 0x53, 0x00, 0x70, 0x00, 0x61, 0x00, 0x63, 0x00, 0x65, 0x00, 0x73, 0x00,
		0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
	}
	var buf bytes.Buffer
	buf.Write([]byte{0xd0, 0xcf, 0x11, 0xe0, 0xa1, 0xb1, 0x1a, 0xe1})
	buf.Write(identifier)
	buf.Write(make([]byte, 64))

	f, err := seedcalc.OpenReader(bytes.NewReader(buf.Bytes()))
	if err == nil {
		t.Fatal("OpenReader on an encrypted package returned a nil error")
	}
	if f != nil {
		t.Error("OpenReader on an encrypted package returned a non-nil *File")
	}
	if errors.Is(err, zip.ErrFormat) {
		t.Error("an encrypted package must be reported distinctly, not as zip.ErrFormat")
	}
}
