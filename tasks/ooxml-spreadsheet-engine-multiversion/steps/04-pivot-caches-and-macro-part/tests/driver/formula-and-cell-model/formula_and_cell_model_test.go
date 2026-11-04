// Gate group: formula-and-cell-model (milestone 04 regression).
//
// Milestone 04 re-spells SetCellFormula as a variadic function and adds the
// f element's t and ref attributes. The cell model underneath it -- typed
// cells, the shared string table, merged cells, the plain three-argument
// formula call and the error contract earlier milestones established -- must
// come through that change unchanged.
//
// Normative sources:
//   - TEST_INTERFACE.md 4.01 (the typed cell model), 4.03.1 (the error
//     contract) and 4.03.3 (A1-style references).
//   - ECMA-376 Part 1, 18.3.1.4 c (Cell) -- a cell carries its reference in r,
//     its style index in s, its type in t and its value in v.
//   - ECMA-376 Part 1, 18.18.11 ST_CellType (Cell Type) -- the enumeration is
//     b, d, e, inlineStr, n, s and str; b is boolean, n (the default, written
//     by omitting t) is numeric and str is a string.
//   - W3C XML 1.0 section 2.4 -- the characters & and < shall be escaped in
//     character data, so text carrying them survives a round trip.
//   - ECMA-376 Part 1, 18.3.1.40 f (Formula) -- the expression is the
//     character content of the element.
//   - ECMA-376 Part 1, 18.3.1.54 mergeCell (Merged Cell) and 18.3.1.55
//     mergeCells (Merge Cells).
//   - ECMA-376 Part 1, 18.17.2.3.1 A1-Style Cell References.
package formulaandcellmodel

import (
	"archive/zip"
	"bytes"
	"encoding/xml"
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

type cellRecord struct {
	R string `xml:"r,attr"`
	T string `xml:"t,attr"`
	F *struct {
		T       string `xml:"t,attr"`
		Ref     string `xml:"ref,attr"`
		Content string `xml:",chardata"`
	} `xml:"f"`
	Value string `xml:"v"`
}

type worksheetDoc struct {
	SheetData struct {
		Row []struct {
			R int          `xml:"r,attr"`
			C []cellRecord `xml:"c"`
		} `xml:"row"`
	} `xml:"sheetData"`
	MergeCells struct {
		Count int `xml:"count,attr"`
		Cell  []struct {
			Ref string `xml:"ref,attr"`
		} `xml:"mergeCell"`
	} `xml:"mergeCells"`
}

func worksheet(t *testing.T, parts map[string]string) worksheetDoc {
	t.Helper()
	raw, ok := parts["xl/worksheets/sheet1.xml"]
	if !ok {
		t.Fatal("the package has no xl/worksheets/sheet1.xml part")
	}
	var ws worksheetDoc
	if err := xml.Unmarshal([]byte(raw), &ws); err != nil {
		t.Fatalf("the worksheet part is not well-formed: %v", err)
	}
	return ws
}

func storedCell(t *testing.T, parts map[string]string, axis string) *cellRecord {
	t.Helper()
	ws := worksheet(t, parts)
	for _, row := range ws.SheetData.Row {
		for i, c := range row.C {
			if c.R == axis {
				return &row.C[i]
			}
		}
	}
	return nil
}

func cellValue(t *testing.T, f *seedcalc.File, axis string) string {
	t.Helper()
	got, err := f.GetCellValue("Sheet1", axis)
	if err != nil {
		t.Fatalf("GetCellValue(%q): %v", axis, err)
	}
	return got
}

func TestCellValuesRoundTripByType(t *testing.T) {
	f := seedcalc.NewFile()
	for _, c := range []struct {
		axis  string
		value interface{}
		want  string
	}{
		{"A1", "Region", "Region"},
		{"A2", 42, "42"},
		{"A3", 3.5, "3.5"},
		{"A4", -17, "-17"},
		{"A5", float32(1.25), "1.25"},
		{"A6", "", ""},
	} {
		if err := f.SetCellValue("Sheet1", c.axis, c.value); err != nil {
			t.Fatalf("SetCellValue(%q, %v): %v", c.axis, c.value, err)
		}
		if got := cellValue(t, f, c.axis); got != c.want {
			t.Errorf("GetCellValue(%q) = %q, want %q", c.axis, got, c.want)
		}
	}
	// An untouched cell is empty rather than an error.
	if got := cellValue(t, f, "Z99"); got != "" {
		t.Errorf("GetCellValue on an untouched cell = %q, want the empty string", got)
	}
	// Overwriting replaces the value rather than accumulating.
	if err := f.SetCellValue("Sheet1", "A1", 7); err != nil {
		t.Fatalf("overwriting A1: %v", err)
	}
	if got := cellValue(t, f, "A1"); got != "7" {
		t.Errorf("after overwriting, A1 = %q, want 7", got)
	}
}

func TestTypedCellSettersWriteTheDeclaredCellType(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetCellStr("Sheet1", "A1", "text"); err != nil {
		t.Fatalf("SetCellStr: %v", err)
	}
	if err := f.SetCellInt("Sheet1", "B1", 12); err != nil {
		t.Fatalf("SetCellInt: %v", err)
	}
	if err := f.SetCellBool("Sheet1", "C1", true); err != nil {
		t.Fatalf("SetCellBool: %v", err)
	}
	if err := f.SetCellFloat("Sheet1", "D1", 1.0/3.0, 4, 64); err != nil {
		t.Fatalf("SetCellFloat: %v", err)
	}
	if err := f.SetCellDefault("Sheet1", "E1", "99"); err != nil {
		t.Fatalf("SetCellDefault: %v", err)
	}
	parts := savedParts(t, f)
	for _, c := range []struct{ axis, wantType string }{
		{"A1", "str"},
		{"B1", ""},
		{"C1", "b"},
		{"D1", ""},
		{"E1", ""},
	} {
		got := storedCell(t, parts, c.axis)
		if got == nil {
			t.Fatalf("the worksheet has no cell %s", c.axis)
		}
		if got.T != c.wantType {
			t.Errorf("cell %s has type %q, want %q", c.axis, got.T, c.wantType)
		}
	}
	if got := cellValue(t, f, "D1"); got != "0.3333" {
		t.Errorf("SetCellFloat with precision 4 reads back as %q, want 0.3333", got)
	}
	if got := cellValue(t, f, "B1"); got != "12" {
		t.Errorf("SetCellInt reads back as %q, want 12", got)
	}
	if got := cellValue(t, f, "E1"); got != "99" {
		t.Errorf("SetCellDefault reads back as %q, want 99", got)
	}
	if got := cellValue(t, f, "C1"); got != "1" {
		t.Errorf("SetCellBool(true) reads back as %q, want the stored 1", got)
	}
}

func TestTextIsEscapedAndSurvivesAReopen(t *testing.T) {
	f := seedcalc.NewFile()
	samples := map[string]string{
		"A1": "Tom & Jerry <b>",
		"A2": "quote \" and apostrophe '",
		"A3": "tab\tand newline\nhere",
		"A4": "\u00e9\u4e2d\u6587 \u2014 unicode",
		"A5": "   leading and trailing   ",
	}
	for axis, text := range samples {
		if err := f.SetCellValue("Sheet1", axis, text); err != nil {
			t.Fatalf("SetCellValue(%q): %v", axis, err)
		}
	}
	// The worksheet part is still well-formed XML with the text escaped.
	parts := savedParts(t, f)
	if raw := parts["xl/worksheets/sheet1.xml"]; strings.Contains(raw, "Tom & Jerry") {
		t.Error("an ampersand reached the worksheet part unescaped")
	}
	for axis, want := range samples {
		if got := cellValue(t, f, axis); got != want {
			t.Errorf("GetCellValue(%q) = %q, want %q", axis, got, want)
		}
	}

	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	g, err := seedcalc.OpenReader(bytes.NewReader(buf.Bytes()))
	if err != nil {
		t.Fatalf("OpenReader: %v", err)
	}
	for axis, want := range samples {
		got, err := g.GetCellValue("Sheet1", axis)
		if err != nil {
			t.Fatalf("after reopening, GetCellValue(%q): %v", axis, err)
		}
		if got != want {
			t.Errorf("after reopening, %s = %q, want %q", axis, got, want)
		}
	}
}

func TestPlainFormulaCallIsUnchangedByTheVariadicSignature(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetCellValue("Sheet1", "A1", 2); err != nil {
		t.Fatalf("SetCellValue: %v", err)
	}
	if err := f.SetCellValue("Sheet1", "A2", 3); err != nil {
		t.Fatalf("SetCellValue: %v", err)
	}
	// The three-argument call is the form every earlier milestone used.
	if err := f.SetCellFormula("Sheet1", "A3", "=SUM(A1:A2)"); err != nil {
		t.Fatalf("SetCellFormula: %v", err)
	}
	got, err := f.GetCellFormula("Sheet1", "A3")
	if err != nil {
		t.Fatalf("GetCellFormula: %v", err)
	}
	if got != "=SUM(A1:A2)" {
		t.Errorf("GetCellFormula = %q, want =SUM(A1:A2)", got)
	}
	cell := storedCell(t, savedParts(t, f), "A3")
	if cell == nil || cell.F == nil {
		t.Fatal("A3 has no f element")
	}
	if cell.F.Content != "=SUM(A1:A2)" {
		t.Errorf("the stored formula is %q", cell.F.Content)
	}
	// Replacing a formula replaces it rather than appending a second one.
	if err := f.SetCellFormula("Sheet1", "A3", "=A1*A2"); err != nil {
		t.Fatalf("replacing the formula: %v", err)
	}
	if got, err = f.GetCellFormula("Sheet1", "A3"); err != nil || got != "=A1*A2" {
		t.Errorf("after replacement GetCellFormula = %q (err %v), want =A1*A2", got, err)
	}
	// A cell that holds a formula reports no formula on a different cell.
	if got, err = f.GetCellFormula("Sheet1", "B9"); err != nil || got != "" {
		t.Errorf("GetCellFormula on a cell with no formula = %q (err %v), want the empty string", got, err)
	}
}

func TestMergedCellsKeepTheirRangeAndAnchorValue(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetCellValue("Sheet1", "A1", "anchor"); err != nil {
		t.Fatalf("SetCellValue: %v", err)
	}
	if err := f.MergeCell("Sheet1", "A1", "C3"); err != nil {
		t.Fatalf("MergeCell: %v", err)
	}
	if err := f.MergeCell("Sheet1", "E1", "F2"); err != nil {
		t.Fatalf("MergeCell: %v", err)
	}
	ws := worksheet(t, savedParts(t, f))
	if len(ws.MergeCells.Cell) != 2 {
		t.Fatalf("the worksheet declares %d merged ranges, want 2", len(ws.MergeCells.Cell))
	}
	merged, err := f.GetMergeCells("Sheet1")
	if err != nil {
		t.Fatalf("GetMergeCells: %v", err)
	}
	if len(merged) != 2 {
		t.Fatalf("GetMergeCells reports %d ranges, want 2", len(merged))
	}
	var found bool
	for _, m := range merged {
		if m.GetStartAxis() == "A1" && m.GetEndAxis() == "C3" {
			found = true
			if m.GetCellValue() != "anchor" {
				t.Errorf("the merged range A1:C3 reports the value %q, want anchor", m.GetCellValue())
			}
		}
	}
	if !found {
		t.Errorf("GetMergeCells does not report the range A1:C3: %v", merged)
	}
	if got := cellValue(t, f, "A1"); got != "anchor" {
		t.Errorf("the merge anchor A1 = %q, want anchor", got)
	}
}

func TestCellOperationsReportBadSheetsAndReferences(t *testing.T) {
	f := seedcalc.NewFile()
	for _, axis := range []string{"", "A", "1", "A0", "1A", "A1:B2", " A1"} {
		if err := f.SetCellValue("Sheet1", axis, 1); err == nil {
			t.Errorf("SetCellValue accepted the malformed reference %q", axis)
		}
		if _, err := f.GetCellValue("Sheet1", axis); err == nil {
			t.Errorf("GetCellValue accepted the malformed reference %q", axis)
		}
	}
	for _, call := range []struct {
		name string
		err  error
	}{
		{"SetCellValue", f.SetCellValue("SheetX", "A1", 1)},
		{"SetCellStr", f.SetCellStr("SheetX", "A1", "x")},
		{"SetCellInt", f.SetCellInt("SheetX", "A1", 1)},
		{"SetCellBool", f.SetCellBool("SheetX", "A1", true)},
		{"SetCellDefault", f.SetCellDefault("SheetX", "A1", "1")},
		{"SetCellFormula", f.SetCellFormula("SheetX", "A1", "=A1")},
		{"MergeCell", f.MergeCell("SheetX", "A1", "B2")},
	} {
		if call.err == nil {
			t.Errorf("%s accepted a worksheet that does not exist", call.name)
		}
	}
	if _, err := f.GetCellValue("SheetX", "A1"); err == nil {
		t.Error("GetCellValue accepted a worksheet that does not exist")
	}
	if _, err := f.GetCellFormula("SheetX", "A1"); err == nil {
		t.Error("GetCellFormula accepted a worksheet that does not exist")
	}
	if _, err := f.GetMergeCells("SheetX"); err == nil {
		t.Error("GetMergeCells accepted a worksheet that does not exist")
	}
	// A rejected call leaves the workbook as it found it.
	if got := cellValue(t, f, "A1"); got != "" {
		t.Errorf("a rejected call wrote %q into Sheet1!A1", got)
	}
}
