// Scored group: cell-value-model.
//
// The typed cell model on the writing side: how a Go value handed to the
// library becomes the text of a SpreadsheetML c element (ECMA-376 Part 1
// clause 18.3.1.4) and what comes back out of it. Cases derived from the
// behaviour of the checkpoint the group is graded against; the built-in number
// format identifiers are those of ECMA-376 Part 1 clause 18.8.30.
package cellvaluemodel

import (
	"bytes"
	"testing"
	"time"

	seedcalc "__SEEDCALC_MODULE__"
)

// roundTrip writes f to memory and reads it back as a fresh package.
func roundTrip(t *testing.T, f *seedcalc.File) *seedcalc.File {
	t.Helper()
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	g, err := seedcalc.OpenReader(bytes.NewReader(buf.Bytes()))
	if err != nil {
		t.Fatalf("OpenReader on a package this library wrote: %v", err)
	}
	return g
}

func TestStringValuesArePreservedExactly(t *testing.T) {
	f := seedcalc.NewFile()
	values := map[string]string{
		"A1": "plain",
		"A2": " leading and trailing ",
		"A3": "markup < & > \" chars",
		"A4": "unicode é中文",
		"A5": "",
	}
	for axis, v := range values {
		f.SetCellValue("Sheet1", axis, v)
	}
	for axis, want := range values {
		if got := f.GetCellValue("Sheet1", axis); got != want {
			t.Errorf("in memory GetCellValue(%q) = %q, want %q", axis, got, want)
		}
	}
	g := roundTrip(t, f)
	for axis, want := range values {
		if got := g.GetCellValue("Sheet1", axis); got != want {
			t.Errorf("after round trip GetCellValue(%q) = %q, want %q", axis, got, want)
		}
	}
}

func TestBooleanValuesReadBackAsTheirTextForm(t *testing.T) {
	f := seedcalc.NewFile()
	f.SetCellValue("Sheet1", "A1", true)
	f.SetCellValue("Sheet1", "A2", false)

	g := roundTrip(t, f)
	if got := g.GetCellValue("Sheet1", "A1"); got != "true" {
		t.Errorf("A1 = %q, want %q", got, "true")
	}
	if got := g.GetCellValue("Sheet1", "A2"); got != "false" {
		t.Errorf("A2 = %q, want %q", got, "false")
	}
}

func TestNumericValuesReadBackAsDecimalText(t *testing.T) {
	f := seedcalc.NewFile()
	f.SetCellValue("Sheet1", "A1", 42)
	f.SetCellValue("Sheet1", "A2", -7)
	f.SetCellValue("Sheet1", "A3", 3.14159)
	f.SetCellValue("Sheet1", "A4", int64(9007199254740993))
	f.SetCellValue("Sheet1", "A5", uint8(200))
	f.SetCellValue("Sheet1", "A6", float32(1.5))

	g := roundTrip(t, f)
	for axis, want := range map[string]string{
		"A1": "42",
		"A2": "-7",
		"A3": "3.14159",
		"A4": "9007199254740993",
		"A5": "200",
		"A6": "1.5",
	} {
		if got := g.GetCellValue("Sheet1", axis); got != want {
			t.Errorf("%s = %q, want %q", axis, got, want)
		}
	}
}

func TestTimeValuesBecomeFormattedSerialDates(t *testing.T) {
	f := seedcalc.NewFile()
	f.SetCellValue("Sheet1", "A1", time.Date(2017, time.July, 4, 12, 0, 0, 0, time.UTC))

	// Built-in number format 22 is "m/d/yy h:mm".
	if got := f.GetCellValue("Sheet1", "A1"); got != "7/4/17 12:00" {
		t.Errorf("A1 = %q, want the value rendered through the date format", got)
	}
	if got := f.GetCellStyle("Sheet1", "A1"); got == 0 {
		t.Error("a time value was stored without a number format; GetCellStyle returned the default format")
	}
	g := roundTrip(t, f)
	if got := g.GetCellValue("Sheet1", "A1"); got != "7/4/17 12:00" {
		t.Errorf("after round trip A1 = %q, want the value rendered through the date format", got)
	}
}

func TestTypedSettersWriteTheValueTheyAreGiven(t *testing.T) {
	f := seedcalc.NewFile()
	f.SetCellInt("Sheet1", "A1", 11)
	f.SetCellStr("Sheet1", "A2", " padded ")
	f.SetCellDefault("Sheet1", "A3", "raw text")

	g := roundTrip(t, f)
	for axis, want := range map[string]string{
		"A1": "11",
		"A2": " padded ",
		"A3": "raw text",
	} {
		if got := g.GetCellValue("Sheet1", axis); got != want {
			t.Errorf("%s = %q, want %q", axis, got, want)
		}
	}
}

func TestAFormulaIsHeldSeparatelyFromTheCellValue(t *testing.T) {
	f := seedcalc.NewFile()
	f.SetCellValue("Sheet1", "A1", 3)
	f.SetCellFormula("Sheet1", "B1", "A1*2")

	if got := f.GetCellFormula("Sheet1", "B1"); got != "A1*2" {
		t.Errorf("GetCellFormula(B1) = %q, want %q", got, "A1*2")
	}
	// No engine evaluates the formula at this milestone, so the cell carries no
	// cached result until one is supplied.
	if got := f.GetCellValue("Sheet1", "B1"); got != "" {
		t.Errorf("GetCellValue(B1) = %q, want the empty string", got)
	}
	if got := f.GetCellFormula("Sheet1", "A1"); got != "" {
		t.Errorf("GetCellFormula(A1) = %q, want the empty string for a cell with no formula", got)
	}

	g := roundTrip(t, f)
	if got := g.GetCellFormula("Sheet1", "B1"); got != "A1*2" {
		t.Errorf("after round trip GetCellFormula(B1) = %q, want %q", got, "A1*2")
	}
}

func TestUpdateLinkedValueDiscardsCachedFormulaResults(t *testing.T) {
	f := seedcalc.NewFile()
	f.SetCellValue("Sheet1", "A1", 3)
	f.SetCellFormula("Sheet1", "B1", "A1*2")
	f.SetCellDefault("Sheet1", "B1", "6")
	f.SetCellValue("Sheet1", "C1", "not a formula")

	// The cached result of a formula belongs to whoever last calculated the
	// workbook, so it is dropped on the workbook as read back from a package.
	g := roundTrip(t, f)
	if got := g.GetCellValue("Sheet1", "B1"); got != "6" {
		t.Fatalf("GetCellValue(B1) = %q before the cache is dropped, want %q", got, "6")
	}
	g.UpdateLinkedValue()
	if got := g.GetCellValue("Sheet1", "B1"); got != "" {
		t.Errorf("GetCellValue(B1) = %q after UpdateLinkedValue, want the empty string", got)
	}
	if got := g.GetCellFormula("Sheet1", "B1"); got != "A1*2" {
		t.Errorf("GetCellFormula(B1) = %q after UpdateLinkedValue, want the formula to survive", got)
	}
	if got := g.GetCellValue("Sheet1", "C1"); got != "not a formula" {
		t.Errorf("GetCellValue(C1) = %q after UpdateLinkedValue, want a non-formula cell to be untouched", got)
	}
}

func TestANilValueYieldsAnEmptyCell(t *testing.T) {
	f := seedcalc.NewFile()
	f.SetCellValue("Sheet1", "A1", "was set")
	f.SetCellValue("Sheet1", "A1", nil)
	if got := f.GetCellValue("Sheet1", "A1"); got != "" {
		t.Errorf("GetCellValue(A1) after SetCellValue(nil) overwriting a value = %q, want the empty string", got)
	}

	f.SetCellValue("Sheet1", "A2", nil)
	if got := f.GetCellValue("Sheet1", "A2"); got != "" {
		t.Errorf("GetCellValue(A2) = %q, want the empty string for a cell set directly to nil", got)
	}

	g := roundTrip(t, f)
	if got := g.GetCellValue("Sheet1", "A1"); got != "" {
		t.Errorf("after round trip GetCellValue(A1) = %q, want the empty string", got)
	}
}

func TestCellsThatWereNeverSetReadAsEmpty(t *testing.T) {
	f := seedcalc.NewFile()
	f.SetCellValue("Sheet1", "A1", "only cell")

	for _, axis := range []string{"A2", "B1", "Z26", "C4"} {
		if got := f.GetCellValue("Sheet1", axis); got != "" {
			t.Errorf("GetCellValue(%q) = %q, want the empty string", axis, got)
		}
		if got := f.GetCellFormula("Sheet1", axis); got != "" {
			t.Errorf("GetCellFormula(%q) = %q, want the empty string", axis, got)
		}
	}
}
