// Scored group: defined-name-deletion (milestone 05, "The streaming writer and
// the drawing registry").
//
// Normative sources:
//   * TEST_INTERFACE.md section 4.05 -- "Deleting defined names".
//   * sheet.go DeleteDefinedName: matches Name + Scope; scope "" means
//     workbook-level; returns errors.New("no defined name on the scope")
//     when not found.
package definednamedeletion

import (
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

func TestDeleteDefinedNameRemovesWorkbookScopedName(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetDefinedName(&seedcalc.DefinedName{
		Name:     "TotalSales",
		RefersTo: "Sheet1!$A$1:$A$10",
		Scope:    "",
	}); err != nil {
		t.Fatalf("SetDefinedName: %v", err)
	}

	before := f.GetDefinedName()
	if len(before) != 1 {
		t.Fatalf("expected 1 defined name before deletion, got %d", len(before))
	}

	if err := f.DeleteDefinedName(&seedcalc.DefinedName{
		Name:  "TotalSales",
		Scope: "",
	}); err != nil {
		t.Fatalf("DeleteDefinedName: %v", err)
	}

	after := f.GetDefinedName()
	if len(after) != 0 {
		t.Errorf("expected 0 defined names after deletion, got %d: %v", len(after), after)
	}
}

func TestDeleteDefinedNameNotFoundReturnsError(t *testing.T) {
	f := seedcalc.NewFile()
	err := f.DeleteDefinedName(&seedcalc.DefinedName{
		Name:  "NoSuchName",
		Scope: "",
	})
	if err == nil {
		t.Error("DeleteDefinedName for a non-existent name must return an error")
	}
	if err.Error() != "no defined name on the scope" {
		t.Errorf("error message = %q, want %q", err.Error(), "no defined name on the scope")
	}
}

func TestDeleteDefinedNameSheetScoped(t *testing.T) {
	f := seedcalc.NewFile()
	f.NewSheet("Sheet2")
	if err := f.SetDefinedName(&seedcalc.DefinedName{
		Name:     "LocalName",
		RefersTo: "Sheet2!$B$1",
		Scope:    "Sheet2",
	}); err != nil {
		t.Fatalf("SetDefinedName: %v", err)
	}

	// Deleting under the wrong scope must not remove the name.
	if err := f.DeleteDefinedName(&seedcalc.DefinedName{
		Name:  "LocalName",
		Scope: "Sheet1",
	}); err == nil {
		t.Error("DeleteDefinedName with wrong scope must return an error")
	}

	// Deleting under the correct scope must succeed.
	if err := f.DeleteDefinedName(&seedcalc.DefinedName{
		Name:  "LocalName",
		Scope: "Sheet2",
	}); err != nil {
		t.Fatalf("DeleteDefinedName with correct scope: %v", err)
	}
	if names := f.GetDefinedName(); len(names) != 0 {
		t.Errorf("expected 0 names after correct delete, got %v", names)
	}
}

func TestDeleteDefinedNameOnlyMatchingEntry(t *testing.T) {
	f := seedcalc.NewFile()
	for _, n := range []string{"Alpha", "Beta", "Gamma"} {
		if err := f.SetDefinedName(&seedcalc.DefinedName{
			Name:     n,
			RefersTo: "Sheet1!$A$1",
			Scope:    "",
		}); err != nil {
			t.Fatalf("SetDefinedName(%s): %v", n, err)
		}
	}
	if err := f.DeleteDefinedName(&seedcalc.DefinedName{Name: "Beta", Scope: ""}); err != nil {
		t.Fatalf("DeleteDefinedName(Beta): %v", err)
	}
	names := f.GetDefinedName()
	if len(names) != 2 {
		t.Fatalf("expected 2 names after deleting Beta, got %d: %v", len(names), names)
	}
	for _, dn := range names {
		if dn.Name == "Beta" {
			t.Errorf("Beta is still present after deletion")
		}
	}
}
