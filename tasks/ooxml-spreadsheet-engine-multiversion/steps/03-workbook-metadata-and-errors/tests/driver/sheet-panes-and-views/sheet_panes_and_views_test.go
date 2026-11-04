// Scored group: sheet-panes-and-views (milestone 03 peripheral surface).
//
// Panes, worksheet view options and sheet protection all write into the
// worksheet part that this milestone's page layout and header/footer
// settings also touch. Normative sources: TEST_INTERFACE.md sections 4.02
// and 4.03; ECMA-376 Part 1, 18.3.1.66 pane, 18.3.1.87 sheetView,
// 18.3.1.85 sheetProtection.
package sheetpanesandviews

import (
	"archive/zip"
	"bytes"
	"io/ioutil"
	"strings"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

func sheetXML(t *testing.T, f *seedcalc.File, name string) string {
	t.Helper()
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	zr, err := zip.NewReader(bytes.NewReader(buf.Bytes()), int64(buf.Len()))
	if err != nil {
		t.Fatalf("the saved workbook is not a readable package: %v", err)
	}
	for _, zf := range zr.File {
		if zf.Name != name {
			continue
		}
		rc, err := zf.Open()
		if err != nil {
			t.Fatalf("open %s: %v", name, err)
		}
		defer rc.Close()
		b, err := ioutil.ReadAll(rc)
		if err != nil {
			t.Fatalf("read %s: %v", name, err)
		}
		return string(b)
	}
	t.Fatalf("the saved package has no part named %s", name)
	return ""
}

func TestSetPanesFreezesTheView(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetPanes("Sheet1",
		`{"freeze":true,"split":false,"x_split":1,"y_split":0,"top_left_cell":"B1","active_pane":"topRight",`+
			`"panes":[{"sqref":"K16","active_cell":"K16","pane":"topRight"}]}`); err != nil {
		t.Fatalf("SetPanes: %v", err)
	}
	sheet := sheetXML(t, f, "xl/worksheets/sheet1.xml")
	if !strings.Contains(sheet, "<pane") {
		t.Fatal("the worksheet carries no pane element")
	}
	for _, want := range []string{`xSplit="1"`, `topLeftCell="B1"`, `activePane="topRight"`, `state="frozen"`} {
		if !strings.Contains(sheet, want) {
			t.Errorf("the pane element does not carry %s", want)
		}
	}
	// Removing the panes takes the element away again.
	if err := f.SetPanes("Sheet1", `{"freeze":false,"split":false}`); err != nil {
		t.Fatalf("SetPanes: %v", err)
	}
	if strings.Contains(sheetXML(t, f, "xl/worksheets/sheet1.xml"), "<pane ") {
		t.Error("unfreezing the view left a pane element behind")
	}
}

func TestSheetViewOptions(t *testing.T) {
	f := seedcalc.NewFile()
	var gridLines seedcalc.ShowGridLines
	var headers seedcalc.ShowRowColHeaders
	var zoom seedcalc.ZoomScale
	var rightToLeft seedcalc.RightToLeft

	if err := f.GetSheetViewOptions("Sheet1", 0, &gridLines, &headers, &rightToLeft); err != nil {
		t.Fatalf("GetSheetViewOptions: %v", err)
	}
	if !bool(gridLines) {
		t.Error("ShowGridLines defaults to false, want true")
	}
	if !bool(headers) {
		t.Error("ShowRowColHeaders defaults to false, want true")
	}
	if bool(rightToLeft) {
		t.Error("RightToLeft defaults to true, want false")
	}

	if err := f.SetSheetViewOptions("Sheet1", 0,
		seedcalc.ShowGridLines(false),
		seedcalc.ShowRowColHeaders(false),
		seedcalc.ZoomScale(80),
		seedcalc.RightToLeft(true),
	); err != nil {
		t.Fatalf("SetSheetViewOptions: %v", err)
	}
	if err := f.GetSheetViewOptions("Sheet1", 0, &gridLines, &headers, &zoom, &rightToLeft); err != nil {
		t.Fatalf("GetSheetViewOptions: %v", err)
	}
	if bool(gridLines) || bool(headers) || !bool(rightToLeft) {
		t.Errorf("view options after setting = gridLines %v, headers %v, rightToLeft %v; want false, false, true",
			bool(gridLines), bool(headers), bool(rightToLeft))
	}
	if float64(zoom) != 80 {
		t.Errorf("ZoomScale = %v, want 80", float64(zoom))
	}
	// The last view is addressable as index -1.
	if err := f.GetSheetViewOptions("Sheet1", -1, &gridLines); err != nil {
		t.Errorf("GetSheetViewOptions with view index -1 = %v, want nil", err)
	}
	if err := f.SetSheetViewOptions("Sheet1", 4, seedcalc.ShowGridLines(true)); err == nil {
		t.Error("SetSheetViewOptions with an out-of-range view index returned nil, want a non-nil error")
	}
	if err := f.GetSheetViewOptions("NoSuchSheet", 0, &gridLines); err == nil {
		t.Error("GetSheetViewOptions on an unknown worksheet returned nil, want a non-nil error")
	}
}

func TestProtectAndUnprotectSheet(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.ProtectSheet("Sheet1", &seedcalc.FormatSheetProtection{
		Password:      "password",
		EditScenarios: false,
		AutoFilter:    true,
	}); err != nil {
		t.Fatalf("ProtectSheet: %v", err)
	}
	sheet := sheetXML(t, f, "xl/worksheets/sheet1.xml")
	if !strings.Contains(sheet, "<sheetProtection") {
		t.Fatal("the worksheet carries no sheetProtection element")
	}
	if !strings.Contains(sheet, "password=") {
		t.Error("the sheetProtection element carries no password hash")
	}

	if err := f.UnprotectSheet("Sheet1"); err != nil {
		t.Fatalf("UnprotectSheet: %v", err)
	}
	if strings.Contains(sheetXML(t, f, "xl/worksheets/sheet1.xml"), "<sheetProtection") {
		t.Error("UnprotectSheet left a sheetProtection element behind")
	}
}

func TestActiveSheetSelection(t *testing.T) {
	f := seedcalc.NewFile()
	f.NewSheet("Second")
	f.NewSheet("Third")
	f.SetActiveSheet(2)
	if got := f.GetActiveSheetIndex(); got != 2 {
		t.Errorf("GetActiveSheetIndex = %d, want 2", got)
	}
	if got := f.GetSheetName(f.GetActiveSheetIndex()); got != "Second" {
		t.Errorf("the active worksheet is %q, want %q", got, "Second")
	}
	// An index outside the workbook is ignored rather than accepted.
	f.SetActiveSheet(-1)
	if got := f.GetActiveSheetIndex(); got < 1 {
		t.Errorf("GetActiveSheetIndex after an out-of-range SetActiveSheet = %d, want a valid index", got)
	}
}
