// Scored group: cell-model-extensions.
//
// What a cell can now hold and how a whole row is written at once: boolean
// cells, elapsed-time values, a row written from a slice, shared formulas read
// back from their defining cell, and cell-level protection carried by a style.
// Graded against ECMA-376 Part 1 clause 18.3.1.4 (c, and its t attribute),
// clause 18.18.11 (ST_CellType, whose "b" value is the boolean cell type),
// clause 18.3.1.40 (f, and its t="shared" / si / ref attributes) and clause
// 18.8.33 (protection, with its locked and hidden attributes). Behaviour cases
// were checked against the upstream release this checkpoint tracks (tag
// v1.4.1).
package cellmodelextensions

import (
	"archive/zip"
	"bytes"
	"encoding/xml"
	"io"
	"testing"
	"time"

	seedcalc "__SEEDCALC_MODULE__"
)

type xmlCell struct {
	R string `xml:"r,attr"`
	T string `xml:"t,attr"`
	S int    `xml:"s,attr"`
	V string `xml:"v"`
}

type xmlRow struct {
	R int       `xml:"r,attr"`
	C []xmlCell `xml:"c"`
}

type worksheet struct {
	Rows []xmlRow `xml:"sheetData>row"`
}

type xmlProtection struct {
	Hidden bool `xml:"hidden,attr"`
	Locked bool `xml:"locked,attr"`
}

type xmlXf struct {
	ApplyProtection bool           `xml:"applyProtection,attr"`
	Protection      *xmlProtection `xml:"protection"`
}

type stylesheet struct {
	CellXfs struct {
		Xf []xmlXf `xml:"xf"`
	} `xml:"cellXfs"`
}

func packageBytes(t *testing.T, f *seedcalc.File) []byte {
	t.Helper()
	buf, err := f.WriteToBuffer()
	if err != nil {
		t.Fatalf("WriteToBuffer: %v", err)
	}
	return buf.Bytes()
}

func readPart(t *testing.T, pkg []byte, name string) []byte {
	t.Helper()
	zr, err := zip.NewReader(bytes.NewReader(pkg), int64(len(pkg)))
	if err != nil {
		t.Fatalf("package is not a ZIP container: %v", err)
	}
	for _, fh := range zr.File {
		if fh.Name != name {
			continue
		}
		rc, err := fh.Open()
		if err != nil {
			t.Fatalf("open %s: %v", name, err)
		}
		defer rc.Close()
		var out bytes.Buffer
		if _, err := io.Copy(&out, rc); err != nil {
			t.Fatalf("read %s: %v", name, err)
		}
		return out.Bytes()
	}
	t.Fatalf("package has no part %s", name)
	return nil
}

func rebuildPackage(t *testing.T, src []byte, replace map[string]string) []byte {
	t.Helper()
	zr, err := zip.NewReader(bytes.NewReader(src), int64(len(src)))
	if err != nil {
		t.Fatalf("source package is not a ZIP container: %v", err)
	}
	var out bytes.Buffer
	zw := zip.NewWriter(&out)
	for _, fh := range zr.File {
		w, err := zw.Create(fh.Name)
		if err != nil {
			t.Fatalf("create part %s: %v", fh.Name, err)
		}
		if s, ok := replace[fh.Name]; ok {
			if _, err := w.Write([]byte(s)); err != nil {
				t.Fatalf("write part %s: %v", fh.Name, err)
			}
			continue
		}
		rc, err := fh.Open()
		if err != nil {
			t.Fatalf("open part %s: %v", fh.Name, err)
		}
		if _, err := io.Copy(w, rc); err != nil {
			rc.Close()
			t.Fatalf("copy part %s: %v", fh.Name, err)
		}
		rc.Close()
	}
	if err := zw.Close(); err != nil {
		t.Fatalf("close rebuilt package: %v", err)
	}
	return out.Bytes()
}

func cellOf(t *testing.T, f *seedcalc.File, axis string) xmlCell {
	t.Helper()
	var ws worksheet
	if err := xml.Unmarshal(readPart(t, packageBytes(t, f), "xl/worksheets/sheet1.xml"), &ws); err != nil {
		t.Fatalf("worksheet part is not well-formed XML: %v", err)
	}
	for _, r := range ws.Rows {
		for _, c := range r.C {
			if c.R == axis {
				return c
			}
		}
	}
	t.Fatalf("written worksheet has no cell %s", axis)
	return xmlCell{}
}

func TestSetCellBoolWritesABooleanCell(t *testing.T) {
	f := seedcalc.NewFile()
	f.SetCellBool("Sheet1", "A1", true)
	f.SetCellBool("Sheet1", "A2", false)

	if got := f.GetCellValue("Sheet1", "A1"); got != "1" {
		t.Errorf("GetCellValue on a true cell = %q, want %q", got, "1")
	}
	if got := f.GetCellValue("Sheet1", "A2"); got != "0" {
		t.Errorf("GetCellValue on a false cell = %q, want %q", got, "0")
	}

	a1 := cellOf(t, f, "A1")
	if a1.T != "b" {
		t.Errorf("cell A1 t = %q, want %q -- a boolean cell carries the b cell type", a1.T, "b")
	}
	if a1.V != "1" {
		t.Errorf("cell A1 v = %q, want %q", a1.V, "1")
	}
	a2 := cellOf(t, f, "A2")
	if a2.T != "b" {
		t.Errorf("cell A2 t = %q, want %q", a2.T, "b")
	}
	if a2.V != "0" {
		t.Errorf("cell A2 v = %q, want %q", a2.V, "0")
	}
}

func TestSetCellValueAcceptsABool(t *testing.T) {
	f := seedcalc.NewFile()
	f.SetCellValue("Sheet1", "A1", true)
	f.SetCellValue("Sheet1", "B1", false)

	if got := f.GetCellValue("Sheet1", "A1"); got != "1" {
		t.Errorf("SetCellValue(true) reads back %q, want %q", got, "1")
	}
	if got := f.GetCellValue("Sheet1", "B1"); got != "0" {
		t.Errorf("SetCellValue(false) reads back %q, want %q", got, "0")
	}
	if got := cellOf(t, f, "A1").T; got != "b" {
		t.Errorf("SetCellValue(true) produced cell type %q, want %q", got, "b")
	}
}

func TestSetCellValueAcceptsADuration(t *testing.T) {
	f := seedcalc.NewFile()
	f.SetCellValue("Sheet1", "A1", 4*time.Hour+30*time.Minute)

	if got := f.GetCellValue("Sheet1", "A1"); got != "04:30:00" {
		t.Errorf("a 4h30m duration reads back as %q, want %q", got, "04:30:00")
	}
	if got := f.GetCellStyle("Sheet1", "A1"); got == 0 {
		t.Error("a duration cell was written without an elapsed-time number format")
	}
	c := cellOf(t, f, "A1")
	if c.T != "" {
		t.Errorf("cell A1 t = %q, want it absent -- a duration is stored as a number", c.T)
	}
	if c.V != "0.1875" {
		t.Errorf("cell A1 v = %q, want %q -- a duration is stored as a fraction of a day", c.V, "0.1875")
	}
}

func TestSetSheetRowWritesConsecutiveCellsFromTheAxis(t *testing.T) {
	f := seedcalc.NewFile()
	values := []interface{}{"one", nil, 3}
	f.SetSheetRow("Sheet1", "B6", &values)

	if got := f.GetCellValue("Sheet1", "B6"); got != "one" {
		t.Errorf("B6 = %q, want %q", got, "one")
	}
	if got := f.GetCellValue("Sheet1", "C6"); got != "" {
		t.Errorf("C6 = %q, want the empty string for a nil element", got)
	}
	if got := f.GetCellValue("Sheet1", "D6"); got != "3" {
		t.Errorf("D6 = %q, want %q", got, "3")
	}
	if got := f.GetCellValue("Sheet1", "E6"); got != "" {
		t.Errorf("E6 = %q, want the empty string -- the row is only as wide as the slice", got)
	}
	if got := f.GetCellValue("Sheet1", "B5"); got != "" {
		t.Errorf("B5 = %q, want the empty string -- only the addressed row is written", got)
	}
}

func TestSetSheetRowIgnoresAValueThatIsNotAPointerToASlice(t *testing.T) {
	f := seedcalc.NewFile()
	values := []interface{}{"one", "two"}
	f.SetSheetRow("Sheet1", "B6", values)
	if got := f.GetCellValue("Sheet1", "B6"); got != "" {
		t.Errorf("B6 = %q after passing a slice by value, want nothing written", got)
	}

	f.SetSheetRow("Sheet1", "B6", "not a slice")
	if got := f.GetCellValue("Sheet1", "B6"); got != "" {
		t.Errorf("B6 = %q after passing a string, want nothing written", got)
	}

	// The documented form still works on the same worksheet afterwards.
	f.SetSheetRow("Sheet1", "B6", &values)
	if got := f.GetCellValue("Sheet1", "B6"); got != "one" {
		t.Errorf("B6 = %q after passing a pointer to the slice, want %q", got, "one")
	}
}

func TestSharedFormulaResolvesFromTheDefiningCell(t *testing.T) {
	const sharedSheet = `<?xml version="1.0" encoding="UTF-8" standalone="yes"?>` +
		`<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">` +
		`<dimension ref="A1:B3"/><sheetViews><sheetView workbookViewId="0"/></sheetViews>` +
		`<sheetData>` +
		`<row r="1"><c r="B1"><v>1</v></c></row>` +
		`<row r="2"><c r="A2"><f t="shared" ref="A2:A3" si="0">B1+1</f><v>2</v></c></row>` +
		`<row r="3"><c r="A3"><f t="shared" si="0"/><v>3</v></c></row>` +
		`</sheetData></worksheet>`

	seed := seedcalc.NewFile()
	seed.SetCellValue("Sheet1", "A1", "placeholder")
	b := rebuildPackage(t, packageBytes(t, seed), map[string]string{
		"xl/worksheets/sheet1.xml": sharedSheet,
	})
	f, err := seedcalc.OpenReader(bytes.NewReader(b))
	if err != nil {
		t.Fatalf("OpenReader on a package carrying a shared formula: %v", err)
	}
	if got := f.GetCellFormula("Sheet1", "A2"); got != "B1+1" {
		t.Errorf("the defining cell of the shared formula reads back %q, want %q", got, "B1+1")
	}
	if got := f.GetCellFormula("Sheet1", "A3"); got != "B1+1" {
		t.Errorf("a cell that only carries si reads back %q, want the shared formula %q", got, "B1+1")
	}
	if seedcalc.STCellFormulaTypeShared != "shared" {
		t.Errorf("STCellFormulaTypeShared = %q, want %q", seedcalc.STCellFormulaTypeShared, "shared")
	}
}

func TestCellProtectionIsCarriedByTheStyle(t *testing.T) {
	f := seedcalc.NewFile()
	styleID, err := f.NewStyle(`{"protection":{"hidden":true,"locked":true}}`)
	if err != nil {
		t.Fatalf("NewStyle: %v", err)
	}
	if styleID == 0 {
		t.Fatal("NewStyle returned the default style for a protection descriptor")
	}
	f.SetCellStyle("Sheet1", "A1", "A1", styleID)
	if got := f.GetCellStyle("Sheet1", "A1"); got != styleID {
		t.Errorf("GetCellStyle = %d, want the style that was applied (%d)", got, styleID)
	}

	var sheet stylesheet
	if err := xml.Unmarshal(readPart(t, packageBytes(t, f), "xl/styles.xml"), &sheet); err != nil {
		t.Fatalf("styles part is not well-formed XML: %v", err)
	}
	if styleID >= len(sheet.CellXfs.Xf) {
		t.Fatalf("cellXfs has %d entries, style id %d is out of range", len(sheet.CellXfs.Xf), styleID)
	}
	xf := sheet.CellXfs.Xf[styleID]
	if !xf.ApplyProtection {
		t.Error("the cell format does not set applyProtection")
	}
	if xf.Protection == nil {
		t.Fatal("the cell format carries no protection element")
	}
	if !xf.Protection.Hidden {
		t.Error("protection/@hidden = false, want true")
	}
	if !xf.Protection.Locked {
		t.Error("protection/@locked = false, want true")
	}
}
