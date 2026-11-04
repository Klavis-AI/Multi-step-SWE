// Gate group: workbook-metadata-regression.
//
// Inherited workbook-level behaviour that the new sheet-index addressing and
// the new evaluator must not disturb: document properties, defined names,
// sheet visibility, the streaming writer and full-text search over a sheet.
// Core document properties are ECMA-376 Part 2 clause 8 "Core Properties";
// defined names are ECMA-376 Part 1 section 18.2.5 "definedName (Defined Name)"
// inside section 18.2.6 "definedNames (Defined Names)"; sheet visibility is the
// state attribute of section 18.2.19 "sheet (Sheet Information)", whose
// vocabulary is section 18.18.68 "ST_SheetState".
package workbookmetadataregression

import (
	"bytes"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

func TestDocumentPropertiesRoundTrip(t *testing.T) {
	f := seedcalc.NewFile()
	props := &seedcalc.DocProperties{
		Category:       "category",
		ContentStatus:  "Draft",
		Created:        "2020-05-11T00:00:00Z",
		Creator:        "seedcalc",
		Description:    "description",
		Identifier:     "xlsx",
		Keywords:       "spreadsheet",
		LastModifiedBy: "seedcalc",
		Modified:       "2020-05-11T00:00:00Z",
		Revision:       "1",
		Subject:        "subject",
		Title:          "title",
		Language:       "en-GB",
		Version:        "1.0.0",
	}
	if err := f.SetDocProps(props); err != nil {
		t.Fatalf("SetDocProps: %v", err)
	}
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	g, err := seedcalc.OpenReader(bytes.NewReader(buf.Bytes()))
	if err != nil {
		t.Fatalf("OpenReader: %v", err)
	}
	got, err := g.GetDocProps()
	if err != nil {
		t.Fatalf("GetDocProps: %v", err)
	}
	if got.Title != props.Title || got.Creator != props.Creator || got.Subject != props.Subject {
		t.Fatalf("GetDocProps = %+v, want title/creator/subject preserved from %+v", got, props)
	}
	if got.Language != props.Language || got.Version != props.Version {
		t.Fatalf("GetDocProps lost language or version: %+v", got)
	}
}

func TestDefinedNamesAreAddedListedAndDeleted(t *testing.T) {
	f := seedcalc.NewFile()
	name := &seedcalc.DefinedName{
		Name:     "Amount",
		RefersTo: "Sheet1!$A$2:$D$5",
		Comment:  "defined name comment",
	}
	if err := f.SetDefinedName(name); err != nil {
		t.Fatalf("SetDefinedName: %v", err)
	}
	if err := f.SetDefinedName(name); err == nil {
		t.Fatal("SetDefinedName accepted a duplicate name")
	}
	names := f.GetDefinedName()
	if len(names) != 1 {
		t.Fatalf("GetDefinedName returned %d entries, want 1", len(names))
	}
	if names[0].Name != "Amount" || names[0].RefersTo != "Sheet1!$A$2:$D$5" {
		t.Fatalf("GetDefinedName = %+v, want Amount -> Sheet1!$A$2:$D$5", names[0])
	}
	if err := f.DeleteDefinedName(name); err != nil {
		t.Fatalf("DeleteDefinedName: %v", err)
	}
	if got := f.GetDefinedName(); len(got) != 0 {
		t.Fatalf("GetDefinedName after delete returned %d entries, want 0", len(got))
	}
	if err := f.DeleteDefinedName(name); err == nil {
		t.Fatal("DeleteDefinedName accepted a name that is not defined")
	}
}

func TestSheetVisibility(t *testing.T) {
	f := seedcalc.NewFile()
	f.NewSheet("Hidden")
	if !f.GetSheetVisible("Hidden") {
		t.Fatal("a newly added sheet reports itself hidden")
	}
	if err := f.SetSheetVisible("Hidden", false); err != nil {
		t.Fatalf("SetSheetVisible: %v", err)
	}
	if f.GetSheetVisible("Hidden") {
		t.Fatal("GetSheetVisible = true after the sheet was hidden")
	}
	// A hidden sheet is still part of the workbook's sheet map. Read through
	// GetSheetMap, which every milestone since 02 has carried, rather than
	// through the GetSheetList that arrives at this one.
	found := false
	for _, n := range f.GetSheetMap() {
		if n == "Hidden" {
			found = true
		}
	}
	if !found {
		t.Fatal("a hidden sheet disappeared from the sheet map")
	}
	if err := f.SetSheetVisible("Hidden", true); err != nil {
		t.Fatalf("SetSheetVisible: %v", err)
	}
	if !f.GetSheetVisible("Hidden") {
		t.Fatal("GetSheetVisible = false after the sheet was shown again")
	}
}

func TestTheStreamingWriterWritesRows(t *testing.T) {
	f := seedcalc.NewFile()
	sw, err := f.NewStreamWriter("Sheet1")
	if err != nil {
		t.Fatalf("NewStreamWriter: %v", err)
	}
	for row := 1; row <= 5; row++ {
		axis, err := seedcalc.CoordinatesToCellName(1, row)
		if err != nil {
			t.Fatalf("CoordinatesToCellName: %v", err)
		}
		if err := sw.SetRow(axis, []interface{}{row, "row"}); err != nil {
			t.Fatalf("SetRow: %v", err)
		}
	}
	if err := sw.Flush(); err != nil {
		t.Fatalf("Flush: %v", err)
	}
	rows, err := f.GetRows("Sheet1")
	if err != nil {
		t.Fatalf("GetRows: %v", err)
	}
	if len(rows) != 5 {
		t.Fatalf("GetRows returned %d rows, want 5", len(rows))
	}
	if rows[4][0] != "5" || rows[0][1] != "row" {
		t.Fatalf("streamed grid = %v, want the values written", rows)
	}
	if _, err := f.NewStreamWriter("NoSuchSheet"); err == nil {
		t.Fatal("NewStreamWriter on an absent sheet returned no error")
	}
}

func TestSearchSheetFindsTheCellsHoldingAValue(t *testing.T) {
	f := seedcalc.NewFile()
	for axis, v := range map[string]string{"A1": "alpha", "B2": "beta", "C3": "alpha"} {
		if err := f.SetCellValue("Sheet1", axis, v); err != nil {
			t.Fatalf("SetCellValue(%s): %v", axis, err)
		}
	}
	hits, err := f.SearchSheet("Sheet1", "alpha")
	if err != nil {
		t.Fatalf("SearchSheet: %v", err)
	}
	if len(hits) != 2 {
		t.Fatalf("SearchSheet found %v, want two cells", hits)
	}
	seen := map[string]bool{}
	for _, h := range hits {
		seen[h] = true
	}
	if !seen["A1"] || !seen["C3"] {
		t.Fatalf("SearchSheet found %v, want A1 and C3", hits)
	}
	none, err := f.SearchSheet("Sheet1", "gamma")
	if err != nil {
		t.Fatalf("SearchSheet: %v", err)
	}
	if len(none) != 0 {
		t.Fatalf("SearchSheet for an absent value found %v, want nothing", none)
	}
	if _, err := f.SearchSheet("NoSuchSheet", "alpha"); err == nil {
		t.Fatal("SearchSheet on an absent sheet returned no error")
	}
}

func TestTheRowIteratorWalksTheGrid(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetSheetRow("Sheet1", "A1", &[]interface{}{"a", "b"}); err != nil {
		t.Fatalf("SetSheetRow: %v", err)
	}
	if err := f.SetSheetRow("Sheet1", "A2", &[]interface{}{"c", "d"}); err != nil {
		t.Fatalf("SetSheetRow: %v", err)
	}
	rows, err := f.Rows("Sheet1")
	if err != nil {
		t.Fatalf("Rows: %v", err)
	}
	var collected [][]string
	for rows.Next() {
		cols, err := rows.Columns()
		if err != nil {
			t.Fatalf("Columns: %v", err)
		}
		collected = append(collected, cols)
	}
	if err := rows.Error(); err != nil {
		t.Fatalf("Rows iteration error: %v", err)
	}
	if len(collected) != 2 {
		t.Fatalf("the iterator produced %d rows, want 2", len(collected))
	}
	if collected[0][0] != "a" || collected[1][1] != "d" {
		t.Fatalf("iterated grid = %v, want [[a b] [c d]]", collected)
	}
	if _, err := f.Rows("NoSuchSheet"); err == nil {
		t.Fatal("Rows on an absent sheet returned no error")
	}
}
