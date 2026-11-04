// Scored group: cell-types.
//
// The CellType enumeration and the GetCellType accessor: the library's own
// name for the type attribute a cell carries in the file. The attribute itself
// is ECMA-376 Part 1 section 18.18.11 "ST_CellType", whose tokens are b, d, e,
// inlineStr, n, s and str; the enumeration folds the three string spellings
// together and reserves a distinct value for a cell that declares no type at
// all.
package celltypes

import (
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

func TestTheEnumerationIsExportedAndOrdered(t *testing.T) {
	// The zero value is the unset type, so a CellType variable that has never
	// been assigned reports the absence of a type rather than some real one.
	var zero seedcalc.CellType
	if zero != seedcalc.CellTypeUnset {
		t.Fatalf("the zero CellType = %d, want CellTypeUnset", zero)
	}
	for i, ct := range []seedcalc.CellType{
		seedcalc.CellTypeUnset,
		seedcalc.CellTypeBool,
		seedcalc.CellTypeDate,
		seedcalc.CellTypeError,
		seedcalc.CellTypeNumber,
		seedcalc.CellTypeString,
	} {
		if int(ct) != i {
			t.Fatalf("constant at position %d has value %d; the enumeration must be consecutive from zero", i, ct)
		}
	}
}

func TestABooleanCellReportsTheBooleanType(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetCellBool("Sheet1", "A1", true); err != nil {
		t.Fatalf("SetCellBool: %v", err)
	}
	ct, err := f.GetCellType("Sheet1", "A1")
	if err != nil {
		t.Fatalf("GetCellType: %v", err)
	}
	if ct != seedcalc.CellTypeBool {
		t.Fatalf("GetCellType on a boolean cell = %d, want CellTypeBool", ct)
	}
}

func TestATextCellReportsTheStringType(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetCellStr("Sheet1", "A1", "hello"); err != nil {
		t.Fatalf("SetCellStr: %v", err)
	}
	if err := f.SetCellValue("Sheet1", "A2", "world"); err != nil {
		t.Fatalf("SetCellValue: %v", err)
	}
	// Both spellings of a string cell fold to the one enumeration value.
	for _, axis := range []string{"A1", "A2"} {
		ct, err := f.GetCellType("Sheet1", axis)
		if err != nil {
			t.Fatalf("GetCellType(%s): %v", axis, err)
		}
		if ct != seedcalc.CellTypeString {
			t.Fatalf("GetCellType(%s) = %d, want CellTypeString", axis, ct)
		}
	}
}

// TestANumericCellDeclaresNoType pins a quirk worth knowing before relying on
// this accessor: a number is the format's default, so a numeric cell carries
// no type attribute at all and reports CellTypeUnset rather than
// CellTypeNumber.
func TestANumericCellDeclaresNoType(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetCellInt("Sheet1", "A1", 42); err != nil {
		t.Fatalf("SetCellInt: %v", err)
	}
	if err := f.SetCellFloat("Sheet1", "A2", 1.5, 2, 64); err != nil {
		t.Fatalf("SetCellFloat: %v", err)
	}
	for _, axis := range []string{"A1", "A2"} {
		ct, err := f.GetCellType("Sheet1", axis)
		if err != nil {
			t.Fatalf("GetCellType(%s): %v", axis, err)
		}
		if ct != seedcalc.CellTypeUnset {
			t.Fatalf("GetCellType(%s) = %d, want CellTypeUnset: a number carries no type attribute", axis, ct)
		}
	}
}

func TestAnEmptyCellDeclaresNoType(t *testing.T) {
	f := seedcalc.NewFile()
	ct, err := f.GetCellType("Sheet1", "Q17")
	if err != nil {
		t.Fatalf("GetCellType on an unset cell: %v", err)
	}
	if ct != seedcalc.CellTypeUnset {
		t.Fatalf("GetCellType on an unset cell = %d, want CellTypeUnset", ct)
	}
}

func TestGetCellTypeReportsTheUnsetTypeAlongsideItsErrors(t *testing.T) {
	f := seedcalc.NewFile()
	for _, tc := range []struct{ sheet, axis string }{
		{"NoSuchSheet", "A1"},
		{"Sheet1", "ZZZ"},
		{"Sheet1", ""},
		{"Sheet1", "A0"},
	} {
		ct, err := f.GetCellType(tc.sheet, tc.axis)
		if err == nil {
			t.Fatalf("GetCellType(%q, %q) returned no error", tc.sheet, tc.axis)
		}
		if ct != seedcalc.CellTypeUnset {
			t.Fatalf("GetCellType(%q, %q) = %d alongside an error, want CellTypeUnset", tc.sheet, tc.axis, ct)
		}
	}
}
