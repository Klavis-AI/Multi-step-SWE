// Scored group: sheet-grouping (milestone 04, "Pivot caches, sparklines and the
// macro part").
//
// Normative sources:
//   - TEST_INTERFACE.md section 4.04 -- "Grouping worksheets".
//   - ECMA-376 Part 1, 18.3.1.87 sheetView (Worksheet View) -- the tabSelected
//     attribute flags whether this sheet is selected, several sheets may be
//     selected at once, and only one may be active.
//   - ECMA-376 Part 1, 18.3.1.88 sheetViews (Sheet Views).
//   - Cases cross-checked against the upstream test corpus at tag v2.0.2
//     (sheet_test.go: TestGroupSheets, TestUngroupSheets).
package sheetgrouping

import (
	"archive/zip"
	"bytes"
	"encoding/xml"
	"fmt"
	"io/ioutil"
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

type worksheetDoc struct {
	SheetViews struct {
		SheetView []struct {
			TabSelected string `xml:"tabSelected,attr"`
		} `xml:"sheetView"`
	} `xml:"sheetViews"`
}

// tabSelected reports whether the worksheet stored at the given one-based
// position has its first sheet view marked as selected.
func tabSelected(t *testing.T, parts map[string]string, index int) bool {
	t.Helper()
	name := fmt.Sprintf("xl/worksheets/sheet%d.xml", index)
	raw, ok := parts[name]
	if !ok {
		t.Fatalf("the package has no %s part", name)
	}
	var ws worksheetDoc
	if err := xml.Unmarshal([]byte(raw), &ws); err != nil {
		t.Fatalf("%s is not well-formed: %v", name, err)
	}
	if len(ws.SheetViews.SheetView) == 0 {
		t.Fatalf("%s declares no sheet view", name)
	}
	return ws.SheetViews.SheetView[0].TabSelected == "true" || ws.SheetViews.SheetView[0].TabSelected == "1"
}

func threeSheets(t *testing.T) *seedcalc.File {
	t.Helper()
	f := seedcalc.NewFile()
	f.NewSheet("Sheet2")
	f.NewSheet("Sheet3")
	return f
}

func TestGroupSheetsSelectsTheListedTabs(t *testing.T) {
	f := threeSheets(t)
	if err := f.GroupSheets([]string{"Sheet1", "Sheet2"}); err != nil {
		t.Fatalf("GroupSheets: %v", err)
	}
	parts := savedParts(t, f)
	if !tabSelected(t, parts, 1) {
		t.Error("the active sheet lost its selected tab after grouping")
	}
	if !tabSelected(t, parts, 2) {
		t.Error("a grouped sheet did not get a selected tab")
	}
}

func TestGroupSheetsLeavesUnlistedSheetsAlone(t *testing.T) {
	f := threeSheets(t)
	if err := f.GroupSheets([]string{"Sheet1", "Sheet2"}); err != nil {
		t.Fatalf("GroupSheets: %v", err)
	}
	if tabSelected(t, savedParts(t, f), 3) {
		t.Error("a sheet outside the group was selected anyway")
	}
}

func TestGroupSheetsRequiresTheActiveSheet(t *testing.T) {
	f := threeSheets(t)
	if err := f.GroupSheets([]string{"Sheet2", "Sheet3"}); err == nil {
		t.Fatal("GroupSheets accepted a group that excludes the active worksheet")
	}
	if tabSelected(t, savedParts(t, f), 2) {
		t.Error("a rejected grouping selected a tab anyway")
	}
}

func TestGroupSheetsRejectsAnUnknownSheet(t *testing.T) {
	f := threeSheets(t)
	if err := f.GroupSheets([]string{"Sheet1", "SheetX"}); err == nil {
		t.Fatal("GroupSheets accepted a sheet name that does not exist")
	}
}

func TestUngroupSheetsClearsEveryTabButTheActiveOne(t *testing.T) {
	f := threeSheets(t)
	if err := f.GroupSheets([]string{"Sheet1", "Sheet2", "Sheet3"}); err != nil {
		t.Fatalf("GroupSheets: %v", err)
	}
	parts := savedParts(t, f)
	if !tabSelected(t, parts, 2) || !tabSelected(t, parts, 3) {
		t.Fatal("grouping did not select the tabs it was asked to")
	}
	if err := f.UngroupSheets(); err != nil {
		t.Fatalf("UngroupSheets: %v", err)
	}
	parts = savedParts(t, f)
	if tabSelected(t, parts, 2) || tabSelected(t, parts, 3) {
		t.Error("ungrouping left a non-active sheet selected")
	}
	if !tabSelected(t, parts, 1) {
		t.Error("ungrouping cleared the active sheet's own tab")
	}
}

func TestUngroupSheetsWithoutAGroupIsHarmless(t *testing.T) {
	f := threeSheets(t)
	if err := f.UngroupSheets(); err != nil {
		t.Fatalf("UngroupSheets on an ungrouped workbook: %v", err)
	}
	parts := savedParts(t, f)
	if !tabSelected(t, parts, 1) {
		t.Error("the active sheet lost its selected tab")
	}
	if tabSelected(t, parts, 2) || tabSelected(t, parts, 3) {
		t.Error("an inactive sheet is selected after ungrouping")
	}
}
