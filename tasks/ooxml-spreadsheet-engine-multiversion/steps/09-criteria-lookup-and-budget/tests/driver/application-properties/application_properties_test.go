// Scored group: application-properties.
//
// The extended document properties part and the accessors that read and write
// it. This is the second of the two property parts an OPC package carries: the
// core properties are Dublin Core metadata about the document, while these
// describe the application that produced it. The part is ECMA-376 Part 1
// section 15.2.12.3 "Extended File Properties Part"; its schema is Part 4
// section 22.2.
package applicationproperties

import (
	"bytes"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// reopen serialises f and reads it back, so a property can be shown to survive
// the round trip through the part rather than living only in memory.
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

func TestAFreshWorkbookNamesThisLibraryAsItsApplication(t *testing.T) {
	f := seedcalc.NewFile()
	props, err := f.GetAppProps()
	if err != nil {
		t.Fatalf("GetAppProps: %v", err)
	}
	if props == nil {
		t.Fatal("GetAppProps returned a nil property set")
	}
	if props.Application != "Go Excelize" {
		t.Fatalf("Application = %q, want %q", props.Application, "Go Excelize")
	}
	// Nothing else is claimed by default.
	if props.Company != "" || props.AppVersion != "" {
		t.Fatalf("a fresh workbook already declares Company %q and AppVersion %q", props.Company, props.AppVersion)
	}
	if props.ScaleCrop || props.LinksUpToDate || props.HyperlinksChanged || props.DocSecurity != 0 {
		t.Fatalf("a fresh workbook already sets one of the flags: %+v", props)
	}
}

func TestEveryPropertyRoundTripsThroughTheAccessors(t *testing.T) {
	f := seedcalc.NewFile()
	want := &seedcalc.AppProperties{
		Application:       "Microsoft Excel",
		ScaleCrop:         true,
		DocSecurity:       3,
		Company:           "Company Name",
		LinksUpToDate:     true,
		HyperlinksChanged: true,
		AppVersion:        "16.0000",
	}
	if err := f.SetAppProps(want); err != nil {
		t.Fatalf("SetAppProps: %v", err)
	}
	got, err := f.GetAppProps()
	if err != nil {
		t.Fatalf("GetAppProps: %v", err)
	}
	if *got != *want {
		t.Fatalf("GetAppProps = %+v, want %+v", *got, *want)
	}
}

func TestThePropertiesSurviveAWriteAndReopen(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetAppProps(&seedcalc.AppProperties{
		Application: "Harbor",
		Company:     "Harbor Bench",
		AppVersion:  "1.0000",
		DocSecurity: 1,
	}); err != nil {
		t.Fatalf("SetAppProps: %v", err)
	}
	got, err := reopen(t, f).GetAppProps()
	if err != nil {
		t.Fatalf("GetAppProps after round trip: %v", err)
	}
	if got.Application != "Harbor" || got.Company != "Harbor Bench" || got.AppVersion != "1.0000" || got.DocSecurity != 1 {
		t.Fatalf("GetAppProps after round trip = %+v", *got)
	}
}

// TestSetAppPropsWritesEveryFieldIncludingTheEmptyOnes records that these
// accessors replace rather than merge: the seven fields are copied across
// whatever their value, so a zero field in the argument clears whatever the
// part held before.
func TestSetAppPropsWritesEveryFieldIncludingTheEmptyOnes(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetAppProps(&seedcalc.AppProperties{Application: "First", Company: "Acme"}); err != nil {
		t.Fatalf("SetAppProps: %v", err)
	}
	// Only Application is named this time; Company is not preserved.
	if err := f.SetAppProps(&seedcalc.AppProperties{Application: "Second"}); err != nil {
		t.Fatalf("SetAppProps: %v", err)
	}
	got, err := f.GetAppProps()
	if err != nil {
		t.Fatalf("GetAppProps: %v", err)
	}
	if got.Application != "Second" {
		t.Fatalf("Application = %q, want %q", got.Application, "Second")
	}
	if got.Company != "" {
		t.Fatalf("Company = %q; a field left out of the argument is cleared, not kept", got.Company)
	}
}

func TestThePropertiesAreIndependentOfTheCoreProperties(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetDocProps(&seedcalc.DocProperties{Title: "A Title", Creator: "A Creator"}); err != nil {
		t.Fatalf("SetDocProps: %v", err)
	}
	if err := f.SetAppProps(&seedcalc.AppProperties{Application: "Harbor", Company: "Harbor Bench"}); err != nil {
		t.Fatalf("SetAppProps: %v", err)
	}
	g := reopen(t, f)
	app, err := g.GetAppProps()
	if err != nil {
		t.Fatalf("GetAppProps: %v", err)
	}
	doc, err := g.GetDocProps()
	if err != nil {
		t.Fatalf("GetDocProps: %v", err)
	}
	if app.Application != "Harbor" || app.Company != "Harbor Bench" {
		t.Fatalf("application properties = %+v", *app)
	}
	if doc.Title != "A Title" || doc.Creator != "A Creator" {
		t.Fatalf("core properties = %+v", *doc)
	}
}
