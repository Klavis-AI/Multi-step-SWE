// Scored group: exported-error-values.
//
// The library's diagnostics are now exported sentinel values rather than
// strings assembled at the point of failure, so a caller can branch on the
// kind of failure with errors.Is. This group pins the sentinel that each
// public entry point returns, and the exported limit constants those
// diagnostics are phrased in terms of. Behaviour transcribed from the upstream
// checkpoint this milestone tracks, qax-os/excelize tag v2.5.0. Sentinel
// comparison is the errors.Is contract from the Go standard library; the
// limits themselves are ECMA-376 Part 1 section 18.3.1.73 "row (Row)",
// section 18.3.1.13 "col (Column Width & Formatting)" and section 18.4.12
// "t (Text)".
package exportederrorvalues

import (
	"bytes"
	"errors"
	"os"
	"path/filepath"
	"strings"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// wants fails unless err is the sentinel target.
func wants(t *testing.T, what string, err, target error) {
	t.Helper()
	if err == nil {
		t.Fatalf("%s returned no error, want %v", what, target)
	}
	if !errors.Is(err, target) {
		t.Fatalf("%s returned %v, want %v", what, err, target)
	}
}

func TestTheLimitConstantsAreExported(t *testing.T) {
	for _, tc := range []struct {
		name string
		got  int
		want int
	}{
		{"TotalRows", seedcalc.TotalRows, 1048576},
		{"TotalColumns", seedcalc.TotalColumns, 16384},
		{"TotalCellChars", seedcalc.TotalCellChars, 32767},
		{"MaxColumnWidth", seedcalc.MaxColumnWidth, 255},
		{"MaxRowHeight", seedcalc.MaxRowHeight, 409},
		{"MaxFontSize", seedcalc.MaxFontSize, 409},
		{"MaxFontFamilyLength", seedcalc.MaxFontFamilyLength, 31},
		{"MaxFileNameLength", seedcalc.MaxFileNameLength, 207},
		{"MaxFieldLength", seedcalc.MaxFieldLength, 255},
	} {
		if tc.got != tc.want {
			t.Fatalf("%s = %d, want %d", tc.name, tc.got, tc.want)
		}
	}
	if seedcalc.StreamChunkSize != 1<<24 {
		t.Fatalf("StreamChunkSize = %d, want %d", seedcalc.StreamChunkSize, 1<<24)
	}
	if seedcalc.UnzipSizeLimit != 1000<<24 {
		t.Fatalf("UnzipSizeLimit = %d, want %d", seedcalc.UnzipSizeLimit, 1000<<24)
	}
}

func TestTheCoordinateLimitsReportTheirOwnSentinel(t *testing.T) {
	// One column past XFD and one row past the last.
	_, err := seedcalc.ColumnNameToNumber("XFE")
	wants(t, `ColumnNameToNumber("XFE")`, err, seedcalc.ErrColumnNumber)
	_, err = seedcalc.ColumnNumberToName(seedcalc.TotalColumns + 1)
	wants(t, "ColumnNumberToName past the last column", err, seedcalc.ErrColumnNumber)
	_, _, err = seedcalc.CellNameToCoordinates("A1048577")
	wants(t, `CellNameToCoordinates("A1048577")`, err, seedcalc.ErrMaxRows)
	// Inside the limits there is no error at all.
	if _, err := seedcalc.ColumnNameToNumber("XFD"); err != nil {
		t.Fatalf(`ColumnNameToNumber("XFD"): %v`, err)
	}
	if _, _, err := seedcalc.CellNameToCoordinates("A1048576"); err != nil {
		t.Fatalf(`CellNameToCoordinates("A1048576"): %v`, err)
	}
}

func TestTheRowAndColumnFormattingLimits(t *testing.T) {
	f := seedcalc.NewFile()
	wants(t, "SetRowHeight past MaxRowHeight",
		f.SetRowHeight("Sheet1", 1, seedcalc.MaxRowHeight+1), seedcalc.ErrMaxRowHeight)
	wants(t, "SetColWidth past MaxColumnWidth",
		f.SetColWidth("Sheet1", "A", "B", seedcalc.MaxColumnWidth+1), seedcalc.ErrColumnWidth)
	wants(t, "SetRowStyle past the last row",
		f.SetRowStyle("Sheet1", 1, seedcalc.TotalRows+1, 0), seedcalc.ErrMaxRows)
	// The outline level is 1 to 7 on both axes.
	wants(t, "SetColOutlineLevel(8)", f.SetColOutlineLevel("Sheet1", "D", 8), seedcalc.ErrOutlineLevel)
	wants(t, "SetColOutlineLevel(0)", f.SetColOutlineLevel("Sheet1", "D", 0), seedcalc.ErrOutlineLevel)
	wants(t, "SetRowOutlineLevel(8)", f.SetRowOutlineLevel("Sheet1", 2, 8), seedcalc.ErrOutlineLevel)
	if err := f.SetRowOutlineLevel("Sheet1", 2, 7); err != nil {
		t.Fatalf("SetRowOutlineLevel at the top of the range: %v", err)
	}
}

func TestTheWorksheetSentinels(t *testing.T) {
	f := seedcalc.NewFile()
	// CopySheet rejects an index that names no worksheet, and refuses to copy
	// a sheet onto itself.
	wants(t, "CopySheet(0, 9)", f.CopySheet(0, 9), seedcalc.ErrSheetIdx)
	wants(t, "CopySheet(0, 0)", f.CopySheet(0, 0), seedcalc.ErrSheetIdx)
	wants(t, "CopySheet(-1, 0)", f.CopySheet(-1, 0), seedcalc.ErrSheetIdx)
	// A chartsheet cannot take the name of an existing worksheet.
	wants(t, `AddChartSheet("Sheet1", ...)`,
		f.AddChartSheet("Sheet1", `{"type":"col"}`), seedcalc.ErrExistsWorksheet)
	// A group of worksheets must include the active one.
	f.NewSheet("Second")
	wants(t, "GroupSheets without the active sheet",
		f.GroupSheets([]string{"Second"}), seedcalc.ErrGroupSheets)
	if err := f.GroupSheets([]string{"Sheet1", "Second"}); err != nil {
		t.Fatalf("GroupSheets including the active sheet: %v", err)
	}
}

func TestTheDefinedNameSentinels(t *testing.T) {
	f := seedcalc.NewFile()
	name := seedcalc.DefinedName{Name: "Amount", RefersTo: "Sheet1!$A$2:$D$5", Scope: "Sheet1"}
	if err := f.SetDefinedName(&name); err != nil {
		t.Fatalf("SetDefinedName: %v", err)
	}
	wants(t, "SetDefinedName twice on one scope",
		f.SetDefinedName(&name), seedcalc.ErrDefinedNameduplicate)
	wants(t, "DeleteDefinedName for a name that is not there",
		f.DeleteDefinedName(&seedcalc.DefinedName{Name: "NoSuchName", Scope: "Sheet1"}),
		seedcalc.ErrDefinedNameScope)
	if err := f.DeleteDefinedName(&name); err != nil {
		t.Fatalf("DeleteDefinedName: %v", err)
	}
}

func TestTheStyleSentinels(t *testing.T) {
	f := seedcalc.NewFile()
	_, err := f.NewStyle(&seedcalc.Style{
		Font: &seedcalc.Font{Family: strings.Repeat("x", seedcalc.MaxFontFamilyLength+1)},
	})
	wants(t, "NewStyle with an over-long font family", err, seedcalc.ErrFontLength)
	_, err = f.NewStyle(&seedcalc.Style{Font: &seedcalc.Font{Size: seedcalc.MaxFontSize + 1}})
	wants(t, "NewStyle with an over-large font size", err, seedcalc.ErrFontSize)
	empty := ""
	_, err = f.NewStyle(&seedcalc.Style{CustomNumFmt: &empty})
	wants(t, "NewStyle with an empty custom number format", err, seedcalc.ErrCustomNumFmt)
	// NewStyle takes JSON text or a *Style; anything else is the wrong kind of
	// parameter rather than a malformed one.
	_, err = f.NewStyle(42)
	wants(t, "NewStyle(42)", err, seedcalc.ErrParameterInvalid)
}

func TestTheDataValidationSentinels(t *testing.T) {
	dv := seedcalc.NewDataValidation(true)
	long := make([]string, 0, 64)
	for i := 0; i < 64; i++ {
		long = append(long, "abcdefgh")
	}
	wants(t, "SetDropList past MaxFieldLength",
		dv.SetDropList(long), seedcalc.ErrDataValidationFormulaLenth)
	if err := dv.SetDropList([]string{"a", "b", "c"}); err != nil {
		t.Fatalf("SetDropList: %v", err)
	}
	// A float outside the single-precision range cannot be written into the
	// validation formula.
	wants(t, "SetRange with an out-of-range float",
		dv.SetRange(1e39, 10.0, seedcalc.DataValidationTypeWhole, seedcalc.DataValidationOperatorBetween),
		seedcalc.ErrDataValidationRange)
	wants(t, "SetRange with an unsupported argument type",
		dv.SetRange(true, 10, seedcalc.DataValidationTypeWhole, seedcalc.DataValidationOperatorBetween),
		seedcalc.ErrParameterInvalid)
	if err := dv.SetRange(1, 10, seedcalc.DataValidationTypeWhole, seedcalc.DataValidationOperatorBetween); err != nil {
		t.Fatalf("SetRange: %v", err)
	}
}

func TestTheSparklineSentinels(t *testing.T) {
	f := seedcalc.NewFile()
	wants(t, "AddSparkline(nil)", f.AddSparkline("Sheet1", nil), seedcalc.ErrParameterRequired)
	wants(t, "AddSparkline with no location",
		f.AddSparkline("Sheet1", &seedcalc.SparklineOption{Range: []string{"Sheet1!A1:C1"}}),
		seedcalc.ErrSparklineLocation)
	wants(t, "AddSparkline with no range",
		f.AddSparkline("Sheet1", &seedcalc.SparklineOption{Location: []string{"E1"}}),
		seedcalc.ErrSparklineRange)
	wants(t, "AddSparkline with mismatched location and range",
		f.AddSparkline("Sheet1", &seedcalc.SparklineOption{
			Location: []string{"E1", "E2"},
			Range:    []string{"Sheet1!A1:C1"},
		}), seedcalc.ErrSparkline)
	wants(t, "AddSparkline with a style outside 0-35",
		f.AddSparkline("Sheet1", &seedcalc.SparklineOption{
			Location: []string{"E1"},
			Range:    []string{"Sheet1!A1:C1"},
			Style:    36,
		}), seedcalc.ErrSparklineStyle)
	wants(t, "AddSparkline with an unrecognised type",
		f.AddSparkline("Sheet1", &seedcalc.SparklineOption{
			Location: []string{"E1"},
			Range:    []string{"Sheet1!A1:C1"},
			Type:     "pie",
		}), seedcalc.ErrSparklineType)
	if err := f.AddSparkline("Sheet1", &seedcalc.SparklineOption{
		Location: []string{"E1"},
		Range:    []string{"Sheet1!A1:C1"},
		Type:     "column",
	}); err != nil {
		t.Fatalf("AddSparkline: %v", err)
	}
}

func TestTheFileAndPictureSentinels(t *testing.T) {
	f := seedcalc.NewFile()
	// A workbook built in memory has no path to save back over.
	wants(t, "Save on a workbook with no path", f.Save(), seedcalc.ErrSave)
	wants(t, "SaveAs with an over-long name",
		f.SaveAs(strings.Repeat("x", seedcalc.MaxFileNameLength+1)+".xlsx"),
		seedcalc.ErrMaxFileNameLength)
	// The picture extension is checked before the bytes are decoded.
	wants(t, "AddPictureFromBytes with an unsupported extension",
		f.AddPictureFromBytes("Sheet1", "A1", "", "logo", ".txt", []byte("not an image")),
		seedcalc.ErrImgExt)
	// The entry points that take a path check that the file is there before
	// they look at its extension, so both of these need a real file.
	stub := filepath.Join(t.TempDir(), "stub.txt")
	if err := os.WriteFile(stub, []byte("not an image"), 0o600); err != nil {
		t.Fatalf("WriteFile: %v", err)
	}
	wants(t, "SetSheetBackground with an unsupported extension",
		f.SetSheetBackground("Sheet1", stub), seedcalc.ErrImgExt)
	wants(t, "AddPicture with an unsupported extension",
		f.AddPicture("Sheet1", "A1", stub, ""), seedcalc.ErrImgExt)
	// A VBA project has to be a .bin.
	wants(t, "AddVBAProject on a file that is not a .bin",
		f.AddVBAProject(stub), seedcalc.ErrAddVBAProject)
}

func TestTheCellAndFormulaSentinels(t *testing.T) {
	f := seedcalc.NewFile()
	// A rich-text cell is capped at the same number of characters as any
	// other cell value.
	wants(t, "SetCellRichText past TotalCellChars",
		f.SetCellRichText("Sheet1", "A1", []seedcalc.RichTextRun{
			{Text: strings.Repeat("x", seedcalc.TotalCellChars+1)},
		}), seedcalc.ErrCellCharsLength)
	if err := f.SetCellRichText("Sheet1", "A1", []seedcalc.RichTextRun{{Text: "ok"}}); err != nil {
		t.Fatalf("SetCellRichText: %v", err)
	}
	// A formula the tokenizer cannot make sense of reports the one sentinel,
	// whatever it was that went wrong.
	if err := f.SetCellFormula("Sheet1", "B1", "=1+"); err != nil {
		t.Fatalf("SetCellFormula: %v", err)
	}
	_, err := f.CalcCellValue("Sheet1", "B1")
	wants(t, `CalcCellValue("=1+")`, err, seedcalc.ErrInvalidFormula)
}

func TestTheStreamWriterAndOptionSentinels(t *testing.T) {
	f := seedcalc.NewFile()
	sw, err := f.NewStreamWriter("Sheet1")
	if err != nil {
		t.Fatalf("NewStreamWriter: %v", err)
	}
	wants(t, "StreamWriter.SetColWidth past the last column",
		sw.SetColWidth(1, seedcalc.TotalColumns+1, 20), seedcalc.ErrColumnNumber)
	wants(t, "StreamWriter.SetColWidth at column 0",
		sw.SetColWidth(0, 3, 20), seedcalc.ErrColumnNumber)
	wants(t, "StreamWriter.SetColWidth past MaxColumnWidth",
		sw.SetColWidth(1, 3, seedcalc.MaxColumnWidth+1), seedcalc.ErrColumnWidth)
	if err := sw.SetColWidth(1, 3, 20); err != nil {
		t.Fatalf("StreamWriter.SetColWidth: %v", err)
	}
	// Column widths belong to the sheet header, so they can no longer be set
	// once a row has been written.
	if err := sw.SetRow("A1", []interface{}{1, 2, 3}); err != nil {
		t.Fatalf("StreamWriter.SetRow: %v", err)
	}
	wants(t, "StreamWriter.SetColWidth after SetRow",
		sw.SetColWidth(1, 3, 20), seedcalc.ErrStreamSetColWidth)
	// The two unzip limits have to be consistent with each other.
	var buf bytes.Buffer
	if err := seedcalc.NewFile().Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	_, err = seedcalc.OpenReader(bytes.NewReader(buf.Bytes()), seedcalc.Options{
		UnzipSizeLimit:    1 << 10,
		UnzipXMLSizeLimit: 1 << 20,
	})
	wants(t, "OpenReader with UnzipXMLSizeLimit above UnzipSizeLimit", err, seedcalc.ErrOptionsUnzipSizeLimit)
}
