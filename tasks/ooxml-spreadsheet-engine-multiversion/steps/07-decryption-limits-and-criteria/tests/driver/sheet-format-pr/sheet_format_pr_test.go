// Scored group: sheet-format-pr.
//
// SetSheetFormatPr and GetSheetFormatPr introduced at this milestone.
// These functions accept functional option values conforming to
// SheetFormatPrOptions / SheetFormatPrOptionsPtr. All seven are exercised here:
// BaseColWidth, DefaultColWidth, DefaultRowHeight, CustomHeight, ZeroHeight,
// ThickTop and ThickBottom. A set value is readable back via the corresponding
// getter option. ECMA-376 Part 1 §18.3.1.81 (sheetFormatPr element; §18.3.1.14
// is colBreaks).
package sheetformatpr

import (
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

func TestSetAndGetDefaultRowHeight(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetSheetFormatPr("Sheet1", seedcalc.DefaultRowHeight(24.0)); err != nil {
		t.Fatalf("SetSheetFormatPr DefaultRowHeight: %v", err)
	}
	var got seedcalc.DefaultRowHeight
	if err := f.GetSheetFormatPr("Sheet1", &got); err != nil {
		t.Fatalf("GetSheetFormatPr DefaultRowHeight: %v", err)
	}
	if float64(got) != 24.0 {
		t.Fatalf("DefaultRowHeight = %v, want 24.0", got)
	}
}

func TestSetAndGetDefaultColWidth(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetSheetFormatPr("Sheet1", seedcalc.DefaultColWidth(12.5)); err != nil {
		t.Fatalf("SetSheetFormatPr DefaultColWidth: %v", err)
	}
	var got seedcalc.DefaultColWidth
	if err := f.GetSheetFormatPr("Sheet1", &got); err != nil {
		t.Fatalf("GetSheetFormatPr DefaultColWidth: %v", err)
	}
	if float64(got) != 12.5 {
		t.Fatalf("DefaultColWidth = %v, want 12.5", got)
	}
}

func TestSetAndGetBaseColWidth(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetSheetFormatPr("Sheet1", seedcalc.BaseColWidth(8)); err != nil {
		t.Fatalf("SetSheetFormatPr BaseColWidth: %v", err)
	}
	var got seedcalc.BaseColWidth
	if err := f.GetSheetFormatPr("Sheet1", &got); err != nil {
		t.Fatalf("GetSheetFormatPr BaseColWidth: %v", err)
	}
	if uint8(got) != 8 {
		t.Fatalf("BaseColWidth = %v, want 8", got)
	}
}

func TestSetAndGetCustomHeight(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetSheetFormatPr("Sheet1", seedcalc.CustomHeight(true)); err != nil {
		t.Fatalf("SetSheetFormatPr CustomHeight: %v", err)
	}
	var got seedcalc.CustomHeight
	if err := f.GetSheetFormatPr("Sheet1", &got); err != nil {
		t.Fatalf("GetSheetFormatPr CustomHeight: %v", err)
	}
	if !bool(got) {
		t.Fatal("CustomHeight = false, want true")
	}
}

func TestSetAndGetZeroHeight(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetSheetFormatPr("Sheet1", seedcalc.ZeroHeight(true)); err != nil {
		t.Fatalf("SetSheetFormatPr ZeroHeight: %v", err)
	}
	var got seedcalc.ZeroHeight
	if err := f.GetSheetFormatPr("Sheet1", &got); err != nil {
		t.Fatalf("GetSheetFormatPr ZeroHeight: %v", err)
	}
	if !bool(got) {
		t.Fatal("ZeroHeight = false, want true")
	}
}

func TestMultipleOptionsSetAtOnce(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetSheetFormatPr("Sheet1",
		seedcalc.DefaultRowHeight(18.0),
		seedcalc.DefaultColWidth(9.0),
		seedcalc.CustomHeight(true),
	); err != nil {
		t.Fatalf("SetSheetFormatPr multiple opts: %v", err)
	}
	var rh seedcalc.DefaultRowHeight
	var cw seedcalc.DefaultColWidth
	var ch seedcalc.CustomHeight
	if err := f.GetSheetFormatPr("Sheet1", &rh, &cw, &ch); err != nil {
		t.Fatalf("GetSheetFormatPr multiple opts: %v", err)
	}
	if float64(rh) != 18.0 {
		t.Fatalf("DefaultRowHeight = %v, want 18.0", rh)
	}
	if float64(cw) != 9.0 {
		t.Fatalf("DefaultColWidth = %v, want 9.0", cw)
	}
	if !bool(ch) {
		t.Fatal("CustomHeight = false, want true")
	}
}

func TestGetSheetFormatPrOnAbsentSheetIsAnError(t *testing.T) {
	f := seedcalc.NewFile()
	var rh seedcalc.DefaultRowHeight
	if err := f.GetSheetFormatPr("NoSuch", &rh); err == nil {
		t.Fatal("GetSheetFormatPr on absent sheet returned no error")
	}
}

func TestSetSheetFormatPrOnAbsentSheetIsAnError(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetSheetFormatPr("NoSuch", seedcalc.DefaultRowHeight(15.0)); err == nil {
		t.Fatal("SetSheetFormatPr on absent sheet returned no error")
	}
}

func TestDefaultRowHeightDefaultIsNonZero(t *testing.T) {
	// A freshly created sheet has no sheetFormatPr; GetSheetFormatPr falls back
	// to the Oracle default of 15 points.
	f := seedcalc.NewFile()
	var rh seedcalc.DefaultRowHeight
	if err := f.GetSheetFormatPr("Sheet1", &rh); err != nil {
		t.Fatalf("GetSheetFormatPr on fresh sheet: %v", err)
	}
	if float64(rh) != 15.0 {
		t.Fatalf("DefaultRowHeight default = %v, want 15.0", rh)
	}
}

func TestSetAndGetThickTopAndThickBottom(t *testing.T) {
	// The two remaining option types of the seven. Both are booleans that
	// default to false on a sheet that carries no sheetFormatPr element.
	f := seedcalc.NewFile()
	var top seedcalc.ThickTop
	var bottom seedcalc.ThickBottom
	if err := f.GetSheetFormatPr("Sheet1", &top, &bottom); err != nil {
		t.Fatalf("GetSheetFormatPr on a fresh sheet: %v", err)
	}
	if bool(top) || bool(bottom) {
		t.Fatalf("a fresh sheet reports ThickTop=%v ThickBottom=%v, want both false", bool(top), bool(bottom))
	}
	if err := f.SetSheetFormatPr("Sheet1", seedcalc.ThickTop(true), seedcalc.ThickBottom(true)); err != nil {
		t.Fatalf("SetSheetFormatPr ThickTop/ThickBottom: %v", err)
	}
	if err := f.GetSheetFormatPr("Sheet1", &top, &bottom); err != nil {
		t.Fatalf("GetSheetFormatPr after set: %v", err)
	}
	if !bool(top) || !bool(bottom) {
		t.Fatalf("after setting both, ThickTop=%v ThickBottom=%v, want both true", bool(top), bool(bottom))
	}
}
