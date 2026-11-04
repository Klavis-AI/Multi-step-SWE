// Gate group: doc-props (milestone 05, "The streaming writer and
// the drawing registry").
//
// Normative sources:
//   * TEST_INTERFACE.md section 4.05 -- "Document core properties".
//   * docProps.go SetDocProps / GetDocProps: v2.1.0 changes the SetDocProps
//     return to (err error) via named return, and GetDocProps returns
//     (ret *DocProperties, err error) -- both match the v2.0.x signatures on
//     the wire.
package docprops

import (
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

func TestSetAndGetDocPropsRoundTrip(t *testing.T) {
	f := seedcalc.NewFile()
	want := &seedcalc.DocProperties{
		Title:         "Quarterly Report",
		Subject:       "Finance",
		Creator:       "Acme Corp",
		Keywords:      "finance,quarterly",
		Description:   "Q1 report",
		LastModifiedBy: "test-runner",
		Language:      "en-US",
		Revision:      "3",
		ContentStatus: "Final",
		Category:      "Report",
		Version:       "1.0",
	}
	if err := f.SetDocProps(want); err != nil {
		t.Fatalf("SetDocProps: %v", err)
	}
	got, err := f.GetDocProps()
	if err != nil {
		t.Fatalf("GetDocProps: %v", err)
	}
	check := func(field, g, w string) {
		t.Helper()
		if g != w {
			t.Errorf("DocProperties.%s = %q, want %q", field, g, w)
		}
	}
	check("Title", got.Title, want.Title)
	check("Subject", got.Subject, want.Subject)
	check("Creator", got.Creator, want.Creator)
	check("Keywords", got.Keywords, want.Keywords)
	check("Description", got.Description, want.Description)
	check("LastModifiedBy", got.LastModifiedBy, want.LastModifiedBy)
	check("Language", got.Language, want.Language)
	check("Revision", got.Revision, want.Revision)
	check("ContentStatus", got.ContentStatus, want.ContentStatus)
	check("Category", got.Category, want.Category)
	check("Version", got.Version, want.Version)
}

func TestGetDocPropsOnFreshWorkbookReturnsNilError(t *testing.T) {
	f := seedcalc.NewFile()
	props, err := f.GetDocProps()
	if err != nil {
		t.Fatalf("GetDocProps on a fresh workbook: %v", err)
	}
	if props == nil {
		t.Fatal("GetDocProps returned a nil *DocProperties")
	}
}

func TestSetDocPropsPreservesUnspecifiedFields(t *testing.T) {
	f := seedcalc.NewFile()
	// First pass: set Creator.
	if err := f.SetDocProps(&seedcalc.DocProperties{Creator: "FirstAuthor"}); err != nil {
		t.Fatalf("first SetDocProps: %v", err)
	}
	// Second pass: set Title only; Creator must survive.
	if err := f.SetDocProps(&seedcalc.DocProperties{Title: "ATitle"}); err != nil {
		t.Fatalf("second SetDocProps: %v", err)
	}
	got, err := f.GetDocProps()
	if err != nil {
		t.Fatalf("GetDocProps: %v", err)
	}
	if got.Title != "ATitle" {
		t.Errorf("Title = %q, want %q", got.Title, "ATitle")
	}
	if got.Creator != "FirstAuthor" {
		t.Errorf("Creator = %q, want %q after partial update", got.Creator, "FirstAuthor")
	}
}
