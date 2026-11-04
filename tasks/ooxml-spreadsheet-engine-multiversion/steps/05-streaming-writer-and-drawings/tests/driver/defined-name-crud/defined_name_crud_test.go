// Gate group: defined-name-crud (milestone 05, "The streaming writer and
// the drawing registry").
//
// Regression group: SetDefinedName and GetDefinedName existed in tree-04.
// Tests verify their round-trip behaviour continues to work in tree-05.
//
// Normative sources:
//   * TEST_INTERFACE.md section 4.05 -- "Defined names".
//   * sheet.go SetDefinedName / GetDefinedName: workbook-level names have
//     Scope "Workbook"; sheet-scoped names carry the sheet name as Scope.
package definednamecrud

import (
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

func TestSetAndGetWorkbookScopedDefinedName(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetDefinedName(&seedcalc.DefinedName{
		Name:     "TotalRevenue",
		RefersTo: "Sheet1!$A$1:$A$100",
		Scope:    "Workbook",
	}); err != nil {
		t.Fatalf("SetDefinedName: %v", err)
	}
	names := f.GetDefinedName()
	if len(names) != 1 {
		t.Fatalf("GetDefinedName = %d entries, want 1", len(names))
	}
	dn := names[0]
	if dn.Name != "TotalRevenue" {
		t.Errorf("Name = %q, want %q", dn.Name, "TotalRevenue")
	}
	if dn.RefersTo != "Sheet1!$A$1:$A$100" {
		t.Errorf("RefersTo = %q, want %q", dn.RefersTo, "Sheet1!$A$1:$A$100")
	}
	if dn.Scope != "Workbook" {
		t.Errorf("Scope = %q, want %q", dn.Scope, "Workbook")
	}
}

func TestSetAndGetSheetScopedDefinedName(t *testing.T) {
	f := seedcalc.NewFile()
	f.NewSheet("Sheet2")
	if err := f.SetDefinedName(&seedcalc.DefinedName{
		Name:     "LocalBudget",
		RefersTo: "Sheet2!$B$2",
		Scope:    "Sheet2",
	}); err != nil {
		t.Fatalf("SetDefinedName: %v", err)
	}
	names := f.GetDefinedName()
	if len(names) != 1 {
		t.Fatalf("GetDefinedName = %d entries, want 1", len(names))
	}
	if names[0].Scope != "Sheet2" {
		t.Errorf("Scope = %q, want %q", names[0].Scope, "Sheet2")
	}
}

func TestGetDefinedNameReturnsEmptySliceWhenNoneSet(t *testing.T) {
	f := seedcalc.NewFile()
	names := f.GetDefinedName()
	if len(names) != 0 {
		t.Errorf("GetDefinedName on a fresh workbook = %d entries, want 0", len(names))
	}
}
