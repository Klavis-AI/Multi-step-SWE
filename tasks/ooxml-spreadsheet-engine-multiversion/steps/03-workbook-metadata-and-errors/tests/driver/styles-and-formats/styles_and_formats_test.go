// Scored group: styles-and-formats (milestone 03 peripheral surface).
//
// Style registration, number formats and conditional formats are adjacent to
// this milestone's per-column styles and default-font override, so they are
// checked here to keep the milestone from being passable by special-casing
// its headline feature. Normative sources: TEST_INTERFACE.md sections 4.01,
// 4.02 and 4.03; ECMA-376 Part 1, 18.8 Styles (18.8.30 numFmt,
// 18.8.45 xf) and 18.3.1.18 conditionalFormatting.
package stylesandformats

import (
	"bytes"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

func TestNewStyleAndSetCellStyle(t *testing.T) {
	f := seedcalc.NewFile()
	style, err := f.NewStyle(`{"font":{"bold":true,"italic":true,"size":14,"color":"#777777"}}`)
	if err != nil {
		t.Fatalf("NewStyle: %v", err)
	}
	if style == 0 {
		t.Fatal("NewStyle returned index 0 for a registered style")
	}
	if err := f.SetCellStyle("Sheet1", "A1", "C3", style); err != nil {
		t.Fatalf("SetCellStyle: %v", err)
	}
	for _, cell := range []string{"A1", "B2", "C3"} {
		got, err := f.GetCellStyle("Sheet1", cell)
		if err != nil {
			t.Fatalf("GetCellStyle(%s): %v", cell, err)
		}
		if got != style {
			t.Errorf("GetCellStyle(%s) = %d, want %d", cell, got, style)
		}
	}
	// A cell outside the styled area is untouched.
	if got, err := f.GetCellStyle("Sheet1", "D4"); err != nil || got != 0 {
		t.Errorf("GetCellStyle(D4) = (%d, %v), want (0, nil)", got, err)
	}

	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	g, err := seedcalc.OpenReader(bytes.NewReader(buf.Bytes()))
	if err != nil {
		t.Fatalf("OpenReader: %v", err)
	}
	if got, err := g.GetCellStyle("Sheet1", "B2"); err != nil || got != style {
		t.Errorf("GetCellStyle(B2) after reopen = (%d, %v), want (%d, nil)", got, err, style)
	}
}

func TestNumberFormatAppliesToTheDisplayedValue(t *testing.T) {
	f := seedcalc.NewFile()
	// Built-in number format 2 is "0.00".
	style, err := f.NewStyle(`{"number_format":2}`)
	if err != nil {
		t.Fatalf("NewStyle: %v", err)
	}
	if err := f.SetCellValue("Sheet1", "A1", 1.5); err != nil {
		t.Fatalf("SetCellValue: %v", err)
	}
	if err := f.SetCellStyle("Sheet1", "A1", "A1", style); err != nil {
		t.Fatalf("SetCellStyle: %v", err)
	}
	got, err := f.GetCellValue("Sheet1", "A1")
	if err != nil {
		t.Fatalf("GetCellValue: %v", err)
	}
	if got != "1.50" {
		t.Errorf("GetCellValue with number format 2 = %q, want %q", got, "1.50")
	}

	// A custom format code is honoured too.
	custom, err := f.NewStyle(`{"custom_number_format":"[$-380A]0.00"}`)
	if err != nil {
		t.Fatalf("NewStyle with a custom format: %v", err)
	}
	if custom == 0 {
		t.Error("registering a custom number format returned index 0")
	}
}

func TestConditionalFormat(t *testing.T) {
	f := seedcalc.NewFile()
	format, err := f.NewConditionalStyle(`{"font":{"color":"#9A0511"},"fill":{"type":"pattern","color":["#FEC7CE"],"pattern":1}}`)
	if err != nil {
		t.Fatalf("NewConditionalStyle: %v", err)
	}
	if err := f.SetConditionalFormat("Sheet1", "A1:A10",
		`[{"type":"cell","criteria":">","format":`+itoa(format)+`,"value":"6"}]`); err != nil {
		t.Fatalf("SetConditionalFormat: %v", err)
	}
	if err := f.SetConditionalFormat("NoSuchSheet", "A1:A10",
		`[{"type":"cell","criteria":">","format":`+itoa(format)+`,"value":"6"}]`); err == nil {
		t.Error("SetConditionalFormat on an unknown worksheet returned nil, want a non-nil error")
	}
}

func TestMalformedStyleDefinitionIsRejected(t *testing.T) {
	f := seedcalc.NewFile()
	for _, def := range []string{`{"font":`, `not json`, `{"font":{"size":`} {
		style, err := f.NewStyle(def)
		if err == nil {
			t.Errorf("NewStyle(%q) returned nil, want a non-nil error", def)
		}
		if style != 0 {
			t.Errorf("NewStyle(%q) = %d on failure, want 0", def, style)
		}
	}
	// A well formed definition still works afterwards.
	if _, err := f.NewStyle(`{"font":{"bold":true}}`); err != nil {
		t.Errorf("NewStyle after a rejected definition = %v, want nil", err)
	}
}

// itoa avoids importing strconv only for the conditional format literal.
func itoa(v int) string {
	if v == 0 {
		return "0"
	}
	neg := v < 0
	if neg {
		v = -v
	}
	var digits []byte
	for v > 0 {
		digits = append([]byte{byte('0' + v%10)}, digits...)
		v /= 10
	}
	if neg {
		return "-" + string(digits)
	}
	return string(digits)
}
