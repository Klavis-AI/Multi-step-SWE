// Scored group: sheet-names-and-visibility.
//
// Worksheet naming and the hidden state. ECMA-376 Part 1 clause 18.2.19
// (ST_SheetState) fixes the state attribute of a workbook sheet element; the
// sheet name restrictions -- the forbidden characters and the 31 character
// limit -- come from the application's naming rules the format is written for,
// and the limit counts characters, not bytes.
package sheetnamesandvisibility

import (
	"bytes"
	"strings"
	"testing"
	"unicode/utf8"

	seedcalc "__SEEDCALC_MODULE__"
)

func roundTrip(t *testing.T, f *seedcalc.File) *seedcalc.File {
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

// nameAt returns the display name the workbook records at an index.
func nameAt(t *testing.T, f *seedcalc.File, index int) string {
	t.Helper()
	m := f.GetSheetMap()
	name, ok := m[index]
	if !ok {
		t.Fatalf("no sheet at index %d: %v", index, m)
	}
	return name
}

func TestForbiddenCharactersAreDroppedFromSheetNames(t *testing.T) {
	f := seedcalc.NewFile()
	f.NewSheet(2, `A:B\C/D?E*F[G]H`)

	got := nameAt(t, f, 2)
	if got != "ABCDEFGH" {
		t.Errorf("sheet name = %q, want %q with every forbidden character removed", got, "ABCDEFGH")
	}
	for _, r := range `:\/?*[]` {
		if strings.ContainsRune(got, r) {
			t.Errorf("sheet name %q still contains the forbidden character %q", got, r)
		}
	}
}

func TestSheetNamesAreTruncatedAtThirtyOneCharacters(t *testing.T) {
	f := seedcalc.NewFile()
	// Forty characters that are three bytes each in UTF-8: truncating by byte
	// count would give a different -- and possibly invalid -- result.
	long := strings.Repeat("字", 40)
	f.NewSheet(2, long)

	got := nameAt(t, f, 2)
	if n := utf8.RuneCountInString(got); n != 31 {
		t.Errorf("sheet name has %d characters, want 31", n)
	}
	if !utf8.ValidString(got) {
		t.Errorf("sheet name %q is not valid UTF-8", got)
	}
	if got != strings.Repeat("字", 31) {
		t.Errorf("sheet name = %q, want the first 31 characters of the requested name", got)
	}

	short := "Fits"
	f.NewSheet(3, short)
	if got := nameAt(t, f, 3); got != short {
		t.Errorf("a short name was altered: %q, want %q", got, short)
	}
}

func TestSetSheetNameAppliesTheSameRules(t *testing.T) {
	f := seedcalc.NewFile()
	f.SetSheetName("Sheet1", "Re:na/med")
	if got := nameAt(t, f, 1); got != "Renamed" {
		t.Errorf("renamed sheet = %q, want %q", got, "Renamed")
	}

	// Renaming a sheet that is not there leaves the workbook alone.
	before := f.GetSheetMap()
	f.SetSheetName("No Such Sheet", "Ignored")
	after := f.GetSheetMap()
	if len(after) != len(before) || after[1] != before[1] {
		t.Errorf("renaming an unknown sheet changed the workbook: %v then %v", before, after)
	}
}

func TestHidingASheetIsRecordedAndSurvivesTheRoundTrip(t *testing.T) {
	f := seedcalc.NewFile()
	f.NewSheet(2, "Beta")

	if !f.GetSheetVisible("Beta") {
		t.Fatal("a newly created sheet is not visible")
	}
	f.SetSheetVisible("Beta", false)
	if f.GetSheetVisible("Beta") {
		t.Error("GetSheetVisible still reports the hidden sheet as visible")
	}
	if !f.GetSheetVisible("Sheet1") {
		t.Error("hiding one sheet also hid another")
	}

	g := roundTrip(t, f)
	if g.GetSheetVisible("Beta") {
		t.Error("the hidden state was lost when the package was written and read back")
	}
	if !g.GetSheetVisible("Sheet1") {
		t.Error("a visible sheet came back hidden")
	}
}

func TestShowingAHiddenSheetRestoresIt(t *testing.T) {
	f := seedcalc.NewFile()
	f.NewSheet(2, "Beta")
	f.SetSheetVisible("Beta", false)
	f.SetSheetVisible("Beta", true)

	if !f.GetSheetVisible("Beta") {
		t.Error("a sheet that was hidden and then shown is still reported hidden")
	}
	if f.GetSheetVisible("No Such Sheet") {
		t.Error("GetSheetVisible reported an unknown sheet as visible")
	}
}
