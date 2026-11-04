// Scored group: cell-model (milestone 03, the error-return migration).
//
// The cell model established by milestones 01 and 02 must keep working once
// every mutator returns error. Normative sources: TEST_INTERFACE.md sections
// 4.01, 4.02 and 4.03 ("The error migration"); ECMA-376 Part 1, 18.3.1.4 c
// (@t cell data types) and 18.3.1.40 f (formula).
package cellmodel

import (
	"bytes"
	"strings"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

func reopen(t *testing.T, f *seedcalc.File) *seedcalc.File {
	t.Helper()
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	g, err := seedcalc.OpenReader(bytes.NewReader(buf.Bytes()))
	if err != nil {
		t.Fatalf("OpenReader: %v", err)
	}
	return g
}

func TestSetAndGetCellValueTypes(t *testing.T) {
	f := seedcalc.NewFile()
	cases := []struct {
		cell  string
		value interface{}
		want  string
	}{
		{"A1", "a string", "a string"},
		{"A2", 42, "42"},
		{"A3", int64(-7), "-7"},
		{"A4", 3.5, "3.5"},
		{"A5", true, "1"},
		{"A6", false, "0"},
		{"A7", nil, ""},
	}
	for _, c := range cases {
		if err := f.SetCellValue("Sheet1", c.cell, c.value); err != nil {
			t.Fatalf("SetCellValue(%s, %v): %v", c.cell, c.value, err)
		}
	}
	g := reopen(t, f)
	for _, c := range cases {
		got, err := g.GetCellValue("Sheet1", c.cell)
		if err != nil {
			t.Errorf("GetCellValue(%s): %v", c.cell, err)
			continue
		}
		if got != c.want {
			t.Errorf("GetCellValue(%s) = %q, want %q", c.cell, got, c.want)
		}
	}
}

func TestTypedCellSetters(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetCellStr("Sheet1", "A1", "text"); err != nil {
		t.Fatalf("SetCellStr: %v", err)
	}
	if err := f.SetCellInt("Sheet1", "A2", 12345); err != nil {
		t.Fatalf("SetCellInt: %v", err)
	}
	if err := f.SetCellBool("Sheet1", "A3", true); err != nil {
		t.Fatalf("SetCellBool: %v", err)
	}
	if err := f.SetCellFloat("Sheet1", "A4", 3.14159265, 3, 64); err != nil {
		t.Fatalf("SetCellFloat: %v", err)
	}
	if err := f.SetCellDefault("Sheet1", "A5", "6.5"); err != nil {
		t.Fatalf("SetCellDefault: %v", err)
	}

	g := reopen(t, f)
	want := map[string]string{"A1": "text", "A2": "12345", "A3": "1", "A4": "3.142", "A5": "6.5"}
	for cell, expect := range want {
		got, err := g.GetCellValue("Sheet1", cell)
		if err != nil {
			t.Errorf("GetCellValue(%s): %v", cell, err)
			continue
		}
		if got != expect {
			t.Errorf("GetCellValue(%s) = %q, want %q", cell, got, expect)
		}
	}
}

func TestCellStringLengthIsCapped(t *testing.T) {
	f := seedcalc.NewFile()
	long := strings.Repeat("x", 40000)
	if err := f.SetCellStr("Sheet1", "A1", long); err != nil {
		t.Fatalf("SetCellStr: %v", err)
	}
	got, err := f.GetCellValue("Sheet1", "A1")
	if err != nil {
		t.Fatalf("GetCellValue: %v", err)
	}
	if len(got) != 32767 {
		t.Errorf("stored string length = %d, want it truncated to 32767", len(got))
	}
}

func TestCellFormula(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetCellValue("Sheet1", "A1", 2); err != nil {
		t.Fatalf("SetCellValue: %v", err)
	}
	if err := f.SetCellValue("Sheet1", "A2", 3); err != nil {
		t.Fatalf("SetCellValue: %v", err)
	}
	if err := f.SetCellFormula("Sheet1", "A3", "SUM(A1:A2)"); err != nil {
		t.Fatalf("SetCellFormula: %v", err)
	}
	got, err := f.GetCellFormula("Sheet1", "A3")
	if err != nil {
		t.Fatalf("GetCellFormula: %v", err)
	}
	if got != "SUM(A1:A2)" {
		t.Errorf("GetCellFormula = %q, want %q", got, "SUM(A1:A2)")
	}
	// A cell without a formula reports the empty string, not an error.
	if got, err := f.GetCellFormula("Sheet1", "A1"); err != nil || got != "" {
		t.Errorf("GetCellFormula on a plain cell = (%q, %v), want (\"\", nil)", got, err)
	}
	// Setting the empty formula removes it.
	if err := f.SetCellFormula("Sheet1", "A3", ""); err != nil {
		t.Fatalf("SetCellFormula(\"\"): %v", err)
	}
	if got, err := f.GetCellFormula("Sheet1", "A3"); err != nil || got != "" {
		t.Errorf("GetCellFormula after clearing = (%q, %v), want (\"\", nil)", got, err)
	}
	if err := f.UpdateLinkedValue(); err != nil {
		t.Errorf("UpdateLinkedValue = %v, want nil", err)
	}
}

func TestMergeCellAndGetMergeCells(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetCellValue("Sheet1", "A1", "merged"); err != nil {
		t.Fatalf("SetCellValue: %v", err)
	}
	if err := f.MergeCell("Sheet1", "A1", "C3"); err != nil {
		t.Fatalf("MergeCell: %v", err)
	}
	// A reversed range names the same area.
	if err := f.MergeCell("Sheet1", "H6", "E4"); err != nil {
		t.Fatalf("MergeCell reversed: %v", err)
	}

	merged, err := f.GetMergeCells("Sheet1")
	if err != nil {
		t.Fatalf("GetMergeCells: %v", err)
	}
	refs := map[string]bool{}
	for _, m := range merged {
		refs[m.GetStartAxis()+":"+m.GetEndAxis()] = true
	}
	if !refs["A1:C3"] {
		t.Errorf("merged ranges = %v, want one of them to be A1:C3", refs)
	}
	if !refs["E4:H6"] {
		t.Errorf("merged ranges = %v, want the reversed range normalised to E4:H6", refs)
	}
	if got := merged[0].GetCellValue(); got != "merged" {
		t.Errorf("the value of the first merged range = %q, want %q", got, "merged")
	}
}

func TestSetSheetRowAndCellHyperLink(t *testing.T) {
	f := seedcalc.NewFile()
	row := []interface{}{"one", nil, 3}
	if err := f.SetSheetRow("Sheet1", "B6", &row); err != nil {
		t.Fatalf("SetSheetRow: %v", err)
	}
	if v, err := f.GetCellValue("Sheet1", "B6"); err != nil || v != "one" {
		t.Errorf("B6 = (%q, %v), want (\"one\", nil)", v, err)
	}
	if v, err := f.GetCellValue("Sheet1", "D6"); err != nil || v != "3" {
		t.Errorf("D6 = (%q, %v), want (\"3\", nil)", v, err)
	}
	// A value that is not a pointer to a slice is rejected.
	if err := f.SetSheetRow("Sheet1", "B6", []interface{}{1}); err == nil {
		t.Error("SetSheetRow with a non-pointer argument returned nil, want a non-nil error")
	}

	if err := f.SetCellHyperLink("Sheet1", "A1", "https://example.com", "External"); err != nil {
		t.Fatalf("SetCellHyperLink: %v", err)
	}
	ok, target, err := f.GetCellHyperLink("Sheet1", "A1")
	if err != nil {
		t.Fatalf("GetCellHyperLink: %v", err)
	}
	if !ok || target != "https://example.com" {
		t.Errorf("GetCellHyperLink = (%v, %q), want (true, %q)", ok, target, "https://example.com")
	}
	if ok, target, err := f.GetCellHyperLink("Sheet1", "B6"); err != nil || ok || target != "" {
		t.Errorf("GetCellHyperLink on a plain cell = (%v, %q, %v), want (false, \"\", nil)", ok, target, err)
	}
}
