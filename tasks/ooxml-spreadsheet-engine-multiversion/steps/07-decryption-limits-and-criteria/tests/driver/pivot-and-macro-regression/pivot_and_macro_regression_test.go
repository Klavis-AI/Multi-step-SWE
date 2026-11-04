// Gate group: pivot-and-macro-regression.
//
// Milestone 04's package surface -- the pivot cache and pivot table parts, the
// sparkline extension, the VBA macro part and worksheet grouping -- which this
// milestone's patch touches (pivotTable.go, sparkline.go, xmlPivotCache.go,
// xmlPivotTable.go) and which no other group here exercises. A candidate that
// breaks pivot-table or sparkline writing while adding decryption is charged
// for it.
//
// Everything asserted here is inherited and unchanged at this checkpoint, so
// the group passes on milestone 06 as well as on 07. It deliberately asserts
// only structure: which parts appear, which content types are declared, which
// extension URI carries the sparkline vocabulary, and that a rejected call
// leaves nothing behind. The one diagnostic string asserted below --
// `unsupported VBA project extension` -- is the one TEST_INTERFACE.md 4.04.10
// states; no other rejection here has a documented message.
//
// TEST_INTERFACE.md 4.04.3 (relationship types and the sparkline extension
// URI), 4.04.4/4.04.5 (pivot tables and their parts), 4.04.7 (sparkline
// groups), 4.04.10 (the macro part) and 4.04.11 (grouping); the field shapes
// are 4.06.8. The pivot parts are ECMA-376 Part 1 sections 12.3.11 (Pivot
// Table Part) and 12.3.12 (Pivot Table Cache Definition Part); the extension
// list is section 18.2.10 (extLst) over section 18.2.7 (ext); grouping is the
// tabSelected attribute of section 18.3.1.87 (sheetView).
package pivotandmacroregression

import (
	"archive/zip"
	"bytes"
	"io"
	"os"
	"strings"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// parts writes the workbook and returns its part names plus the text of
// [Content_Types].xml.
func parts(t *testing.T, f *seedcalc.File) ([]string, string) {
	t.Helper()
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	zr, err := zip.NewReader(bytes.NewReader(buf.Bytes()), int64(buf.Len()))
	if err != nil {
		t.Fatalf("the written workbook is not a ZIP package: %v", err)
	}
	var names []string
	var types string
	for _, item := range zr.File {
		names = append(names, item.Name)
		if item.Name == "[Content_Types].xml" {
			types = read(t, item)
		}
	}
	return names, types
}

func read(t *testing.T, item *zip.File) string {
	t.Helper()
	rc, err := item.Open()
	if err != nil {
		t.Fatalf("open %s: %v", item.Name, err)
	}
	defer rc.Close()
	b, err := io.ReadAll(rc)
	if err != nil {
		t.Fatalf("read %s: %v", item.Name, err)
	}
	return string(b)
}

func partText(t *testing.T, f *seedcalc.File, name string) string {
	t.Helper()
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	zr, err := zip.NewReader(bytes.NewReader(buf.Bytes()), int64(buf.Len()))
	if err != nil {
		t.Fatalf("the written workbook is not a ZIP package: %v", err)
	}
	for _, item := range zr.File {
		if item.Name == name {
			return read(t, item)
		}
	}
	t.Fatalf("the written package has no part %s", name)
	return ""
}

func has(names []string, want string) bool {
	for _, n := range names {
		if n == want {
			return true
		}
	}
	return false
}

func anyPivotPart(names []string) string {
	for _, n := range names {
		if strings.Contains(n, "xl/pivotTables/") || strings.Contains(n, "xl/pivotCache/") {
			return n
		}
	}
	return ""
}

// source builds the two-sheet workbook the pivot cases share: a header row and
// four data rows on Sheet1, and an empty Pivot sheet to place the table on.
func source(t *testing.T) *seedcalc.File {
	t.Helper()
	f := seedcalc.NewFile()
	for i, row := range [][]interface{}{
		{"Month", "Region", "Sales"},
		{"Jan", "North", 10},
		{"Feb", "North", 20},
		{"Jan", "South", 30},
		{"Feb", "South", 40},
	} {
		axis, err := seedcalc.CoordinatesToCellName(1, i+1)
		if err != nil {
			t.Fatalf("CoordinatesToCellName: %v", err)
		}
		values := row
		if err := f.SetSheetRow("Sheet1", axis, &values); err != nil {
			t.Fatalf("SetSheetRow(%s): %v", axis, err)
		}
	}
	f.NewSheet("Pivot")
	return f
}

func TestAPivotTableWritesItsCacheAndTableParts(t *testing.T) {
	f := source(t)
	if err := f.AddPivotTable(&seedcalc.PivotTableOption{
		DataRange:       "Sheet1!$A$1:$C$5",
		PivotTableRange: "Pivot!$A$1:$E$20",
		Rows:            []seedcalc.PivotTableField{{Data: "Month"}},
		Columns:         []seedcalc.PivotTableField{{Data: "Region"}},
		Data:            []seedcalc.PivotTableField{{Data: "Sales", Name: "Sum of Sales", Subtotal: "Sum"}},
	}); err != nil {
		t.Fatalf("AddPivotTable: %v", err)
	}
	names, types := parts(t, f)
	for _, want := range []string{
		"xl/pivotCache/pivotCacheDefinition1.xml",
		"xl/pivotTables/pivotTable1.xml",
		"xl/pivotTables/_rels/pivotTable1.xml.rels",
	} {
		if !has(names, want) {
			t.Fatalf("the written package has no %s; parts = %v", want, names)
		}
	}
	// One AddPivotTable call declares both new part types.
	if !strings.Contains(types, "pivotTable") || !strings.Contains(types, "pivotCache") {
		t.Fatalf("[Content_Types].xml declares neither pivot part type: %s", types)
	}
	// The table definition places the named source columns on their axes.
	table := partText(t, f, "xl/pivotTables/pivotTable1.xml")
	for _, want := range []string{"axisRow", "axisCol", "dataField"} {
		if !strings.Contains(table, want) {
			t.Fatalf("pivotTable1.xml carries no %s entry: %s", want, table)
		}
	}
	// The package is still readable after all that.
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	g, err := seedcalc.OpenReader(bytes.NewReader(buf.Bytes()))
	if err != nil {
		t.Fatalf("OpenReader on a workbook carrying a pivot table: %v", err)
	}
	if got := g.GetSheetList(); len(got) != 2 {
		t.Fatalf("reopened sheet list = %v, want both sheets", got)
	}
}

func TestARejectedPivotTableLeavesNoPartBehind(t *testing.T) {
	// 4.04.4's rejection list. The message is not contract, so only the
	// refusal and the absence of a half-written part are asserted.
	for _, c := range []struct {
		name string
		opt  *seedcalc.PivotTableOption
	}{
		{"a nil option", nil},
		{"no ranges at all", &seedcalc.PivotTableOption{}},
		{"an unqualified range", &seedcalc.PivotTableOption{
			DataRange: "$A$1:$C$5", PivotTableRange: "Pivot!$A$1:$E$20"}},
		{"a range naming an absent worksheet", &seedcalc.PivotTableOption{
			DataRange: "NoSuchSheet!$A$1:$C$5", PivotTableRange: "Pivot!$A$1:$E$20"}},
		{"a single-cell data range", &seedcalc.PivotTableOption{
			DataRange: "Sheet1!$A$1:$A$1", PivotTableRange: "Pivot!$A$1:$E$20"}},
		{"a row number of zero", &seedcalc.PivotTableOption{
			DataRange: "Sheet1!$A$0:$C$5", PivotTableRange: "Pivot!$A$1:$E$20"}},
	} {
		f := source(t)
		if err := f.AddPivotTable(c.opt); err == nil {
			t.Fatalf("AddPivotTable accepted %s", c.name)
		}
		names, types := parts(t, f)
		if leaked := anyPivotPart(names); leaked != "" {
			t.Fatalf("AddPivotTable left %s behind after refusing %s", leaked, c.name)
		}
		if strings.Contains(types, "pivot") {
			t.Fatalf("[Content_Types].xml declares a pivot type after refusing %s", c.name)
		}
	}
}

func TestASparklineGroupReachesTheWorksheetExtension(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.AddSparkline("Sheet1", &seedcalc.SparklineOption{
		Location: []string{"F2"},
		Range:    []string{"Sheet1!A2:C2"},
		Type:     "column",
		Style:    12,
	}); err != nil {
		t.Fatalf("AddSparkline: %v", err)
	}
	sheet := partText(t, f, "xl/worksheets/sheet1.xml")
	for _, want := range []string{
		"extLst",
		seedcalc.ExtURISparklineGroups,
		"sparklineGroup",
		"Sheet1!A2:C2", // the data reference, copied verbatim into xm:f
		"F2",           // the location, copied verbatim into xm:sqref
		`type="column"`,
	} {
		if !strings.Contains(sheet, want) {
			t.Fatalf("sheet1.xml does not carry %q: %s", want, sheet)
		}
	}

	// A rejected call writes no extension at all.
	for _, c := range []struct {
		name  string
		sheet string
		opt   *seedcalc.SparklineOption
	}{
		{"an unknown type", "Sheet1", &seedcalc.SparklineOption{
			Location: []string{"F2"}, Range: []string{"Sheet1!A2:C2"}, Type: "bogus"}},
		{"a style outside 0-35", "Sheet1", &seedcalc.SparklineOption{
			Location: []string{"F2"}, Range: []string{"Sheet1!A2:C2"}, Style: 36}},
		{"mismatched location and range counts", "Sheet1", &seedcalc.SparklineOption{
			Location: []string{"F2", "F3"}, Range: []string{"Sheet1!A2:C2"}}},
		{"a nil option", "Sheet1", nil},
		{"an absent worksheet", "NoSuchSheet", nil},
	} {
		g := seedcalc.NewFile()
		if err := g.AddSparkline(c.sheet, c.opt); err == nil {
			t.Fatalf("AddSparkline accepted %s", c.name)
		}
		if got := partText(t, g, "xl/worksheets/sheet1.xml"); strings.Contains(got, "sparkline") {
			t.Fatalf("AddSparkline wrote a sparkline after refusing %s", c.name)
		}
	}
}

func TestTheMacroPartIsWiredOnlyWhenItIsAdded(t *testing.T) {
	// A workbook that never calls AddVBAProject declares none of it.
	names, types := parts(t, seedcalc.NewFile())
	for _, n := range names {
		if strings.HasSuffix(n, ".bin") {
			t.Fatalf("a workbook with no macro carries %s", n)
		}
	}
	if strings.Contains(types, "macroEnabled") || strings.Contains(types, "vbaProject") {
		t.Fatalf("a workbook with no macro declares a macro content type: %s", types)
	}

	dir := t.TempDir()
	bin := dir + "/macros.bin"
	if err := os.WriteFile(bin, []byte("MACROBYTES"), 0o600); err != nil {
		t.Fatalf("WriteFile: %v", err)
	}
	notBin := dir + "/macros.txt"
	if err := os.WriteFile(notBin, []byte("MACROBYTES"), 0o600); err != nil {
		t.Fatalf("WriteFile: %v", err)
	}

	f := seedcalc.NewFile()
	// Existence is checked before the extension, and both refusals change
	// nothing. The absent-path message is not asserted: it is the file
	// system's own at milestone 06 and the library's own at 07.
	if err := f.AddVBAProject(dir + "/absent.bin"); err == nil {
		t.Fatal("AddVBAProject accepted a path that does not exist")
	}
	if err := f.AddVBAProject(notBin); err == nil {
		t.Fatal("AddVBAProject accepted a path whose extension is not .bin")
	} else if err.Error() != "unsupported VBA project extension" {
		t.Fatalf("AddVBAProject on a non-.bin path = %q, want %q", err, "unsupported VBA project extension")
	}
	// Existence is checked first, so a path that is missing AND wrongly
	// named is reported as missing, never as an unsupported extension.
	if err := f.AddVBAProject(dir + "/absent.txt"); err == nil {
		t.Fatal("AddVBAProject accepted a path that is neither present nor .bin")
	} else if err.Error() == "unsupported VBA project extension" {
		t.Fatal("AddVBAProject checked the extension before the path existed")
	}
	if names, types := parts(t, f); strings.Contains(types, "macroEnabled") {
		t.Fatalf("a refused AddVBAProject wired the macro content type; parts = %v", names)
	}

	// A successful call stores the bytes at the fixed part name, whatever the
	// source file was called.
	if err := f.AddVBAProject(bin); err != nil {
		t.Fatalf("AddVBAProject: %v", err)
	}
	names, types = parts(t, f)
	if !has(names, "xl/vbaProject.bin") {
		t.Fatalf("the written package has no xl/vbaProject.bin; parts = %v", names)
	}
	if has(names, "xl/macros.bin") {
		t.Fatalf("the macro part was stored under the source file's own name; parts = %v", names)
	}
	if !strings.Contains(types, "macroEnabled") {
		t.Fatalf("the workbook part is not macro-enabled: %s", types)
	}
	if !strings.Contains(types, "vnd.ms-office.vbaProject") {
		t.Fatalf("[Content_Types].xml declares no bin default media type: %s", types)
	}
	if got := partText(t, f, "xl/vbaProject.bin"); got != "MACROBYTES" {
		t.Fatalf("xl/vbaProject.bin = %q, want the source bytes verbatim", got)
	}
}

func TestGroupSheetsValidatesBeforeItSelects(t *testing.T) {
	f := seedcalc.NewFile()
	f.NewSheet("Sheet2")
	f.NewSheet("Sheet3")
	// The active worksheet must be named in the list, so an empty or nil list
	// fails the same way.
	if err := f.GroupSheets([]string{"Sheet2", "Sheet3"}); err == nil {
		t.Fatal("GroupSheets grouped a list that does not name the active worksheet")
	}
	if err := f.GroupSheets(nil); err == nil {
		t.Fatal("GroupSheets accepted an empty list")
	}
	// Every name must exist.
	if err := f.GroupSheets([]string{"Sheet1", "SheetX"}); err == nil {
		t.Fatal("GroupSheets accepted a name that is not a worksheet")
	}
	if err := f.GroupSheets([]string{"Sheet1", "Sheet2"}); err != nil {
		t.Fatalf("GroupSheets on a list holding the active worksheet: %v", err)
	}
	// UngroupSheets takes no arguments and never fails, grouped or not.
	if err := f.UngroupSheets(); err != nil {
		t.Fatalf("UngroupSheets: %v", err)
	}
	if err := f.UngroupSheets(); err != nil {
		t.Fatalf("UngroupSheets on a workbook that is not grouped: %v", err)
	}
}
