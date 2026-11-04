// Scored group: rich-text-read.
//
// Milestone 06 could write a cell's rich-text runs; this one can read them
// back. GetCellRichText returns the same []RichTextRun that SetCellRichText
// accepted, including each run's font, both from the in-memory workbook and
// from a package that has been written and reopened. The underlying parts are
// ECMA-376 Part 1 section 18.4.8 "si (String Item)" and section 18.4.4
// "r (Rich Text Run)".
package richtextread

import (
	"bytes"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// runs is the fixture written by every case here: one unstyled run followed by
// one carrying every font attribute the struct exposes.
func runs() []seedcalc.RichTextRun {
	return []seedcalc.RichTextRun{
		{Text: "plain "},
		{Text: "bold", Font: &seedcalc.Font{
			Bold:      true,
			Italic:    true,
			Underline: "single",
			Family:    "Times New Roman",
			Size:      14,
			Color:     "2354E8",
		}},
	}
}

func checkRuns(t *testing.T, got []seedcalc.RichTextRun, where string) {
	t.Helper()
	if len(got) != 2 {
		t.Fatalf("%s: GetCellRichText returned %d runs, want 2", where, len(got))
	}
	if got[0].Text != "plain " {
		t.Fatalf("%s: first run text = %q, want %q", where, got[0].Text, "plain ")
	}
	// The unstyled run carries no font at all, rather than a zero-valued one.
	if got[0].Font != nil {
		t.Fatalf("%s: first run carries a font, want none", where)
	}
	if got[1].Text != "bold" {
		t.Fatalf("%s: second run text = %q, want %q", where, got[1].Text, "bold")
	}
	font := got[1].Font
	if font == nil {
		t.Fatalf("%s: second run carries no font", where)
	}
	if !font.Bold || !font.Italic {
		t.Fatalf("%s: second run Bold=%v Italic=%v, want both true", where, font.Bold, font.Italic)
	}
	if font.Underline != "single" {
		t.Fatalf("%s: second run Underline = %q, want %q", where, font.Underline, "single")
	}
	if font.Family != "Times New Roman" {
		t.Fatalf("%s: second run Family = %q, want %q", where, font.Family, "Times New Roman")
	}
	if font.Size != 14 {
		t.Fatalf("%s: second run Size = %v, want 14", where, font.Size)
	}
	if font.Color != "2354E8" {
		t.Fatalf("%s: second run Color = %q, want %q", where, font.Color, "2354E8")
	}
}

func TestRichTextRunsAreReadBackInMemory(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetCellRichText("Sheet1", "A1", runs()); err != nil {
		t.Fatalf("SetCellRichText: %v", err)
	}
	got, err := f.GetCellRichText("Sheet1", "A1")
	if err != nil {
		t.Fatalf("GetCellRichText: %v", err)
	}
	checkRuns(t, got, "in memory")
}

func TestRichTextRunsSurviveARoundTrip(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetCellRichText("Sheet1", "A1", runs()); err != nil {
		t.Fatalf("SetCellRichText: %v", err)
	}
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	g, err := seedcalc.OpenReader(bytes.NewReader(buf.Bytes()))
	if err != nil {
		t.Fatalf("OpenReader: %v", err)
	}
	got, err := g.GetCellRichText("Sheet1", "A1")
	if err != nil {
		t.Fatalf("GetCellRichText after round trip: %v", err)
	}
	checkRuns(t, got, "after round trip")
}

func TestTheCellValueIsTheConcatenatedRuns(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetCellRichText("Sheet1", "A1", runs()); err != nil {
		t.Fatalf("SetCellRichText: %v", err)
	}
	// The plain accessor still works on a rich cell and sees the runs joined,
	// with no markup and no styling.
	got, err := f.GetCellValue("Sheet1", "A1")
	if err != nil {
		t.Fatalf("GetCellValue: %v", err)
	}
	if got != "plain bold" {
		t.Fatalf("GetCellValue on a rich-text cell = %q, want %q", got, "plain bold")
	}
}

func TestAPlainCellHasNoRuns(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetCellValue("Sheet1", "C1", "plain text"); err != nil {
		t.Fatalf("SetCellValue: %v", err)
	}
	got, err := f.GetCellRichText("Sheet1", "C1")
	if err != nil {
		t.Fatalf("GetCellRichText on a plain cell: %v", err)
	}
	// A cell written as a plain string is not one run of rich text; it has none.
	if len(got) != 0 {
		t.Fatalf("GetCellRichText on a plain cell returned %d runs, want none", len(got))
	}
}

func TestGetCellRichTextRejectsWhatItCannotAddress(t *testing.T) {
	f := seedcalc.NewFile()
	if _, err := f.GetCellRichText("NoSuchSheet", "A1"); err == nil {
		t.Fatal("GetCellRichText on an absent sheet returned no error")
	}
	if _, err := f.GetCellRichText("Sheet1", "ZZZ"); err == nil {
		t.Fatal("GetCellRichText at a malformed reference returned no error")
	}
}
