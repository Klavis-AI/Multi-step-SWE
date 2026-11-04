// Scored group: workbook-metadata (milestone 03, "Workbook metadata and the
// error contract").
//
// Normative sources:
//   * TEST_INTERFACE.md section 4.03 -- "Document core properties",
//     "Defined names", "The default font".
//   * ECMA-376 Part 2, 8.  Core properties (Dublin Core / Open Packaging
//     Conventions metadata: dc:title, dc:creator, cp:keywords, ...).
//   * ECMA-376 Part 1, 18.2.5 definedNames and 18.2.6 definedName
//     (@name, @localSheetId, @comment, element text = the reference).
//   * Cases cross-checked against the upstream test corpus at tag v2.0.1
//     (docProps_test.go, sheet_test.go TestDefinedName, styles_test.go
//     TestSetDefaultFont).
package workbookmetadata

import (
	"bytes"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// reopen serialises f and reads it back, so that assertions see what was
// actually persisted rather than the in-memory model.
func reopen(t *testing.T, f *seedcalc.File) *seedcalc.File {
	t.Helper()
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	g, err := seedcalc.OpenReader(bytes.NewReader(buf.Bytes()))
	if err != nil {
		t.Fatalf("OpenReader: %v", err)
	}
	return g
}

func fullProps() seedcalc.DocProperties {
	return seedcalc.DocProperties{
		Category:       "category",
		ContentStatus:  "Draft",
		Created:        "2019-06-04T22:00:10Z",
		Creator:        "Go Seedcalc",
		Description:    "This file created by Go Seedcalc",
		Identifier:     "xlsx",
		Keywords:       "Spreadsheet",
		Language:       "en-US",
		LastModifiedBy: "Go Author",
		Modified:       "2019-06-04T22:00:10Z",
		Revision:       "0",
		Subject:        "Test Subject",
		Title:          "Test Title",
		Version:        "1.0.0",
	}
}

func diffProps(t *testing.T, what string, got, want seedcalc.DocProperties) {
	t.Helper()
	fields := []struct {
		name       string
		got, want  string
	}{
		{"Category", got.Category, want.Category},
		{"ContentStatus", got.ContentStatus, want.ContentStatus},
		{"Created", got.Created, want.Created},
		{"Creator", got.Creator, want.Creator},
		{"Description", got.Description, want.Description},
		{"Identifier", got.Identifier, want.Identifier},
		{"Keywords", got.Keywords, want.Keywords},
		{"Language", got.Language, want.Language},
		{"LastModifiedBy", got.LastModifiedBy, want.LastModifiedBy},
		{"Modified", got.Modified, want.Modified},
		{"Revision", got.Revision, want.Revision},
		{"Subject", got.Subject, want.Subject},
		{"Title", got.Title, want.Title},
		{"Version", got.Version, want.Version},
	}
	for _, fl := range fields {
		if fl.got != fl.want {
			t.Errorf("%s: %s = %q, want %q", what, fl.name, fl.got, fl.want)
		}
	}
}

func TestGetDocPropsOnANewWorkbook(t *testing.T) {
	f := seedcalc.NewFile()
	props, err := f.GetDocProps()
	if err != nil {
		t.Fatalf("GetDocProps: %v", err)
	}
	if props == nil {
		t.Fatal("GetDocProps returned a nil *DocProperties for a new workbook")
	}
	// A new workbook already carries a core properties part with the
	// creation and modification timestamps filled in.
	if props.Created == "" {
		t.Error("Created is empty on a new workbook, want a timestamp")
	}
	if props.Modified == "" {
		t.Error("Modified is empty on a new workbook, want a timestamp")
	}
	// Nothing the caller never set is invented.
	if props.Title != "" || props.Subject != "" || props.Keywords != "" || props.Category != "" {
		t.Errorf("unset properties are populated on a new workbook: %+v", *props)
	}
}

func TestSetDocPropsRoundTrip(t *testing.T) {
	f := seedcalc.NewFile()
	want := fullProps()
	if err := f.SetDocProps(&want); err != nil {
		t.Fatalf("SetDocProps: %v", err)
	}
	got, err := f.GetDocProps()
	if err != nil {
		t.Fatalf("GetDocProps: %v", err)
	}
	diffProps(t, "after SetDocProps", *got, want)
}

func TestSetDocPropsOnlyOverwritesNonEmptyFields(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetDocProps(&seedcalc.DocProperties{Title: "First title", Creator: "First creator"}); err != nil {
		t.Fatalf("first SetDocProps: %v", err)
	}
	// A second call that leaves Title and Creator empty must not clear them.
	if err := f.SetDocProps(&seedcalc.DocProperties{Subject: "A subject"}); err != nil {
		t.Fatalf("second SetDocProps: %v", err)
	}
	got, err := f.GetDocProps()
	if err != nil {
		t.Fatalf("GetDocProps: %v", err)
	}
	if got.Title != "First title" {
		t.Errorf("Title = %q, want it preserved as %q", got.Title, "First title")
	}
	if got.Creator != "First creator" {
		t.Errorf("Creator = %q, want it preserved as %q", got.Creator, "First creator")
	}
	if got.Subject != "A subject" {
		t.Errorf("Subject = %q, want %q", got.Subject, "A subject")
	}

	// Overwriting with a non-empty value does replace.
	if err := f.SetDocProps(&seedcalc.DocProperties{Title: "Second title"}); err != nil {
		t.Fatalf("third SetDocProps: %v", err)
	}
	got, err = f.GetDocProps()
	if err != nil {
		t.Fatalf("GetDocProps: %v", err)
	}
	if got.Title != "Second title" {
		t.Errorf("Title = %q, want %q", got.Title, "Second title")
	}
}

func TestDocPropsSurviveSaveAndReopen(t *testing.T) {
	f := seedcalc.NewFile()
	want := fullProps()
	if err := f.SetDocProps(&want); err != nil {
		t.Fatalf("SetDocProps: %v", err)
	}
	g := reopen(t, f)
	got, err := g.GetDocProps()
	if err != nil {
		t.Fatalf("GetDocProps after reopen: %v", err)
	}
	diffProps(t, "after save and reopen", *got, want)
}

func TestDefinedNameAtWorkbookScope(t *testing.T) {
	f := seedcalc.NewFile()
	if names := f.GetDefinedName(); len(names) != 0 {
		t.Fatalf("a new workbook has %d defined names, want 0", len(names))
	}
	if err := f.SetDefinedName(&seedcalc.DefinedName{
		Name:     "Amount",
		RefersTo: "Sheet1!$A$2:$D$5",
		Comment:  "defined name comment",
	}); err != nil {
		t.Fatalf("SetDefinedName: %v", err)
	}
	names := f.GetDefinedName()
	if len(names) != 1 {
		t.Fatalf("GetDefinedName returned %d entries, want 1", len(names))
	}
	if names[0].Name != "Amount" {
		t.Errorf("Name = %q, want %q", names[0].Name, "Amount")
	}
	if names[0].RefersTo != "Sheet1!$A$2:$D$5" {
		t.Errorf("RefersTo = %q, want %q", names[0].RefersTo, "Sheet1!$A$2:$D$5")
	}
	if names[0].Comment != "defined name comment" {
		t.Errorf("Comment = %q, want %q", names[0].Comment, "defined name comment")
	}
	// An omitted scope means the whole workbook, and is reported as such.
	if names[0].Scope != "Workbook" {
		t.Errorf("Scope = %q, want %q", names[0].Scope, "Workbook")
	}

	g := reopen(t, f)
	after := g.GetDefinedName()
	if len(after) != 1 || after[0].Name != "Amount" || after[0].RefersTo != "Sheet1!$A$2:$D$5" || after[0].Scope != "Workbook" {
		t.Errorf("defined names after save and reopen = %+v, want the workbook-scoped Amount", after)
	}
}

func TestDefinedNameAtWorksheetScope(t *testing.T) {
	f := seedcalc.NewFile()
	if idx := f.NewSheet("Sheet2"); idx != 2 {
		t.Fatalf("NewSheet(\"Sheet2\") = %d, want 2", idx)
	}
	if err := f.SetDefinedName(&seedcalc.DefinedName{Name: "Amount", RefersTo: "Sheet1!$A$1:$B$2"}); err != nil {
		t.Fatalf("workbook-scoped SetDefinedName: %v", err)
	}
	// The same name may be defined again when it is scoped to a worksheet.
	if err := f.SetDefinedName(&seedcalc.DefinedName{
		Name: "Amount", RefersTo: "Sheet2!$C$3:$D$4", Scope: "Sheet2",
	}); err != nil {
		t.Fatalf("worksheet-scoped SetDefinedName: %v", err)
	}
	// A print area is an ordinary worksheet-scoped defined name.
	if err := f.SetDefinedName(&seedcalc.DefinedName{
		Name: "_xlnm.Print_Area", RefersTo: "Sheet1!$A$1:$Z$100", Scope: "Sheet1",
	}); err != nil {
		t.Fatalf("print area SetDefinedName: %v", err)
	}

	byScope := map[string]seedcalc.DefinedName{}
	for _, dn := range f.GetDefinedName() {
		byScope[dn.Scope+"/"+dn.Name] = dn
	}
	if len(byScope) != 3 {
		t.Fatalf("GetDefinedName returned %+v, want three distinct scope/name pairs", f.GetDefinedName())
	}
	if got := byScope["Workbook/Amount"]; got.RefersTo != "Sheet1!$A$1:$B$2" {
		t.Errorf("workbook-scoped Amount RefersTo = %q, want %q", got.RefersTo, "Sheet1!$A$1:$B$2")
	}
	if got := byScope["Sheet2/Amount"]; got.RefersTo != "Sheet2!$C$3:$D$4" {
		t.Errorf("Sheet2-scoped Amount RefersTo = %q, want %q", got.RefersTo, "Sheet2!$C$3:$D$4")
	}
	if got := byScope["Sheet1/_xlnm.Print_Area"]; got.RefersTo != "Sheet1!$A$1:$Z$100" {
		t.Errorf("print area RefersTo = %q, want %q", got.RefersTo, "Sheet1!$A$1:$Z$100")
	}
}

func TestDefinedNameRejectsDuplicateWithinAScope(t *testing.T) {
	f := seedcalc.NewFile()
	f.NewSheet("Sheet2")
	if err := f.SetDefinedName(&seedcalc.DefinedName{Name: "Amount", RefersTo: "Sheet1!$A$1"}); err != nil {
		t.Fatalf("SetDefinedName: %v", err)
	}
	if err := f.SetDefinedName(&seedcalc.DefinedName{Name: "Amount", RefersTo: "Sheet1!$B$1"}); err == nil {
		t.Error("redefining a workbook-scoped name returned nil, want a non-nil error")
	}
	if err := f.SetDefinedName(&seedcalc.DefinedName{Name: "Amount", RefersTo: "Sheet2!$A$1", Scope: "Sheet2"}); err != nil {
		t.Fatalf("worksheet-scoped SetDefinedName: %v", err)
	}
	if err := f.SetDefinedName(&seedcalc.DefinedName{Name: "Amount", RefersTo: "Sheet2!$B$1", Scope: "Sheet2"}); err == nil {
		t.Error("redefining a worksheet-scoped name returned nil, want a non-nil error")
	}
	// The rejected calls left the workbook with exactly the two accepted names.
	if names := f.GetDefinedName(); len(names) != 2 {
		t.Errorf("GetDefinedName returned %d entries, want 2: %+v", len(names), names)
	}
}

func TestDefaultFontOverride(t *testing.T) {
	f := seedcalc.NewFile()
	if got := f.GetDefaultFont(); got != "Calibri" {
		t.Errorf("GetDefaultFont on a new workbook = %q, want %q", got, "Calibri")
	}
	f.SetDefaultFont("Ariel")
	if got := f.GetDefaultFont(); got != "Ariel" {
		t.Errorf("GetDefaultFont after SetDefaultFont = %q, want %q", got, "Ariel")
	}
	// The override goes through the workbook style tree, so it is persisted.
	g := reopen(t, f)
	if got := g.GetDefaultFont(); got != "Ariel" {
		t.Errorf("GetDefaultFont after save and reopen = %q, want %q", got, "Ariel")
	}
	// Registering a style that names no font family does not disturb it.
	if _, err := g.NewStyle(`{"font":{"bold":true}}`); err != nil {
		t.Fatalf("NewStyle: %v", err)
	}
	if got := g.GetDefaultFont(); got != "Ariel" {
		t.Errorf("GetDefaultFont after NewStyle = %q, want %q", got, "Ariel")
	}
}

func TestNamespaceConstants(t *testing.T) {
	cases := []struct {
		name, got, want string
	}{
		{"NameSpaceDublinCore", seedcalc.NameSpaceDublinCore, "http://purl.org/dc/elements/1.1/"},
		{"NameSpaceDublinCoreTerms", seedcalc.NameSpaceDublinCoreTerms, "http://purl.org/dc/terms/"},
		{"NameSpaceDublinCoreMetadataIntiative", seedcalc.NameSpaceDublinCoreMetadataIntiative, "http://purl.org/dc/dcmitype/"},
		{"NameSpaceXMLSchemaInstance", seedcalc.NameSpaceXMLSchemaInstance, "http://www.w3.org/2001/XMLSchema-instance"},
	}
	for _, c := range cases {
		if c.got != c.want {
			t.Errorf("%s = %q, want %q", c.name, c.got, c.want)
		}
	}
}
