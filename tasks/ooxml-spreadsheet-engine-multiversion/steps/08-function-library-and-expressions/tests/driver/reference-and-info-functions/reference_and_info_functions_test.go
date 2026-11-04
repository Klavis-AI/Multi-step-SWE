// Scored group: reference-and-info-functions.
//
// Functions whose answer depends on where they are rather than on what they
// are given: the position of the containing cell, the shape of a range, the
// index of the containing sheet, the current clock and the calendar difference
// between two serial dates. Semantics follow OpenDocument v1.3 part 4
// (OpenFormula) section 6.13 "Information Functions" and section 6.14 "Lookup
// Functions"; the serial-date encoding is ECMA-376 Part 1 section 18.17.4
// "Dates and Times", whose 18.17.4.1 is Date Conversion for Serial Date-Times
// and whose 18.17.4.2 is Time Conversion for Serial Date-Times.
package referenceandinfofunctions

import (
	"math"
	"strconv"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// evalAt evaluates formula in a named cell, because these functions read the
// address they are written at.
func evalAt(t *testing.T, f *seedcalc.File, sheet, axis, formula string) (string, error) {
	t.Helper()
	if err := f.SetCellFormula(sheet, axis, formula); err != nil {
		t.Fatalf("SetCellFormula(%s!%s, %q): %v", sheet, axis, formula, err)
	}
	return f.CalcCellValue(sheet, axis)
}

func mustEvalAt(t *testing.T, f *seedcalc.File, sheet, axis, formula, want string) {
	t.Helper()
	got, err := evalAt(t, f, sheet, axis, formula)
	if err != nil {
		t.Fatalf("CalcCellValue(%s!%s, %q): unexpected error %v", sheet, axis, formula, err)
	}
	if got != want {
		t.Fatalf("CalcCellValue(%s!%s, %q) = %q, want %q", sheet, axis, formula, got, want)
	}
}

func mustEval(t *testing.T, f *seedcalc.File, formula, want string) {
	t.Helper()
	mustEvalAt(t, f, "Sheet1", "Z1", formula, want)
}

func mustReject(t *testing.T, f *seedcalc.File, formula string) {
	t.Helper()
	got, err := evalAt(t, f, "Sheet1", "Z1", formula)
	if err == nil {
		t.Fatalf("CalcCellValue(%q) = %q, want an error", formula, got)
	}
	if got != "" {
		t.Fatalf("CalcCellValue(%q) returned result %q alongside an error", formula, got)
	}
}

// mustRejectWith is mustReject with the rejection message pinned. An arity
// complaint names the function and the count it wanted; a formula the engine
// has never heard of is refused for an entirely different reason, and the two
// must not be allowed to look alike to this group.
func mustRejectWith(t *testing.T, f *seedcalc.File, formula, wantErr string) {
	t.Helper()
	got, err := evalAt(t, f, "Sheet1", "Z1", formula)
	if err == nil {
		t.Fatalf("CalcCellValue(%q) = %q, want an error", formula, got)
	}
	if err.Error() != wantErr {
		t.Fatalf("CalcCellValue(%q) failed with %q, want %q", formula, err, wantErr)
	}
	if got != "" {
		t.Fatalf("CalcCellValue(%q) returned result %q alongside an error", formula, got)
	}
}

func TestROWAndCOLUMNDefaultToTheContainingCell(t *testing.T) {
	f := seedcalc.NewFile()
	// With no argument each reports its own address, so the answer depends on
	// which cell the formula was written into. Z1 is column 26, row 1.
	mustEvalAt(t, f, "Sheet1", "Z1", "=ROW()", "1")
	mustEvalAt(t, f, "Sheet1", "Z1", "=COLUMN()", "26")
	mustEvalAt(t, f, "Sheet1", "D7", "=ROW()", "7")
	mustEvalAt(t, f, "Sheet1", "D7", "=COLUMN()", "4")
}

func TestROWAndCOLUMNOfANamedReference(t *testing.T) {
	f := seedcalc.NewFile()
	mustEval(t, f, "=ROW(B7)", "7")
	mustEval(t, f, "=COLUMN(C3)", "3")
	// Given a range, each reports the top-left corner rather than a list.
	mustEval(t, f, "=ROW(A1:A5)", "1")
	mustEval(t, f, "=COLUMN(A1:C1)", "1")
	// A number is not a reference.
	mustReject(t, f, "=ROW(1)")
	mustReject(t, f, "=COLUMN(1)")
}

func TestROWSAndCOLUMNSMeasureARange(t *testing.T) {
	f := seedcalc.NewFile()
	mustEval(t, f, "=ROWS(A1:A5)", "5")
	mustEval(t, f, "=COLUMNS(A1:C1)", "3")
	mustEval(t, f, "=ROWS(A1:C3)", "3")
	mustEval(t, f, "=COLUMNS(A1:C3)", "3")
	// A single cell is a one-by-one range.
	mustEval(t, f, "=ROWS(A1)", "1")
	mustEval(t, f, "=COLUMNS(A1)", "1")
}

func TestROWSTakesExactlyOneArgument(t *testing.T) {
	f := seedcalc.NewFile()
	// Unlike the aggregation functions, these do not accept a list.
	mustRejectWith(t, f, "=ROWS()", "ROWS requires 1 argument")
	mustRejectWith(t, f, "=ROWS(A1:A5,B1)", "ROWS requires 1 argument")
	mustRejectWith(t, f, "=COLUMNS()", "COLUMNS requires 1 argument")
	mustRejectWith(t, f, "=COLUMNS(A1:C1,B1)", "COLUMNS requires 1 argument")
}

func TestSHEETReportsTheContainingSheetsIndex(t *testing.T) {
	f := seedcalc.NewFile()
	f.NewSheet("Second")
	// The index is one-based and follows the workbook's sheet order.
	mustEvalAt(t, f, "Sheet1", "A1", "=SHEET()", "1")
	mustEvalAt(t, f, "Second", "A1", "=SHEET()", "2")
}

func TestSHEETTakesNoArgument(t *testing.T) {
	f := seedcalc.NewFile()
	// Deviation from OpenFormula, which lets SHEET name a sheet to look up: at
	// this checkpoint it reports the containing sheet and nothing else, so any
	// argument at all is refused.
	mustRejectWith(t, f, `=SHEET("Sheet1")`, "SHEET accepts no arguments")
}

func TestNOWAndTODAYReturnSerialDates(t *testing.T) {
	f := seedcalc.NewFile()
	today, err := evalAt(t, f, "Sheet1", "Z1", "=TODAY()")
	if err != nil {
		t.Fatalf("CalcCellValue(=TODAY()): %v", err)
	}
	now, err := evalAt(t, f, "Sheet1", "Z2", "=NOW()")
	if err != nil {
		t.Fatalf("CalcCellValue(=NOW()): %v", err)
	}
	// Read TODAY again on the far side of NOW, so that a run which happens to
	// straddle midnight has a second whole day to agree with rather than
	// failing on the clock.
	todayAfter, err := evalAt(t, f, "Sheet1", "Z3", "=TODAY()")
	if err != nil {
		t.Fatalf("CalcCellValue(=TODAY()) after NOW: %v", err)
	}
	// Both are serial numbers, not formatted dates.
	todayN, err := strconv.ParseFloat(today, 64)
	if err != nil {
		t.Fatalf("TODAY() = %q, which is not a serial number: %v", today, err)
	}
	nowN, err := strconv.ParseFloat(now, 64)
	if err != nil {
		t.Fatalf("NOW() = %q, which is not a serial number: %v", now, err)
	}
	// TODAY is a whole day; NOW carries the time of day as its fraction, and
	// the two name the same day.
	if todayN != math.Trunc(todayN) {
		t.Fatalf("TODAY() = %v, want a whole serial day", todayN)
	}
	todayAfterN, err := strconv.ParseFloat(todayAfter, 64)
	if err != nil {
		t.Fatalf("TODAY() = %q, which is not a serial number: %v", todayAfter, err)
	}
	if math.Trunc(nowN) != todayN && math.Trunc(nowN) != todayAfterN {
		t.Fatalf("NOW() = %v names neither the day TODAY() gave before it (%v) nor after it (%v)", nowN, todayN, todayAfterN)
	}
	// Both sit well inside the serial epoch, which began at 1899-12-30.
	if todayN < 40000 || todayN > 100000 {
		t.Fatalf("TODAY() = %v is outside any plausible serial-date range", todayN)
	}
}

func TestNOWAndTODAYTakeNoArguments(t *testing.T) {
	f := seedcalc.NewFile()
	mustRejectWith(t, f, "=NOW(1)", "NOW accepts no arguments")
	mustRejectWith(t, f, "=TODAY(1)", "TODAY accepts no arguments")
}

func TestDATEDIFCountsWholeUnitsBetweenSerialDates(t *testing.T) {
	f := seedcalc.NewFile()
	// 43831 is 2020-01-01 and 44270 is 2021-03-15.
	mustEval(t, f, `=DATEDIF(43831,44270,"Y")`, "1")
	mustEval(t, f, `=DATEDIF(43831,44270,"M")`, "14")
	mustEval(t, f, `=DATEDIF(43831,44270,"D")`, "439")
}

func TestDATEDIFPartialUnitsAndItsDomain(t *testing.T) {
	f := seedcalc.NewFile()
	// The compound units drop the larger component. MD is the day difference
	// ignoring months and years, taken in the end date's month; YM is the whole
	// months past the last full year. YD is the one that needs its reference
	// year stated, and TEST_INTERFACE.md 4.08.9 states it: the end date's month
	// and day are transplanted into the *start* date's year and the days from
	// the start date are counted, so 2020-01-01 to 2021-03-15 counts
	// 2020-01-01 to 2020-03-15. 2020 is a leap year, so that is 31 + 29 + 14 =
	// 74 days, not the 73 that counting from the 2021 anniversary would give.
	mustEval(t, f, `=DATEDIF(43831,44270,"MD")`, "14")
	mustEval(t, f, `=DATEDIF(43831,44270,"YM")`, "2")
	mustEval(t, f, `=DATEDIF(43831,44270,"YD")`, "74")
	// The unit is matched without regard to case.
	mustEval(t, f, `=DATEDIF(43831,44270,"y")`, "1")
	// The interval runs forwards only, and the unit set is closed.
	mustReject(t, f, `=DATEDIF(44270,43831,"D")`)
	mustReject(t, f, `=DATEDIF(43831,44270,"Q")`)
	// DATEDIF reads serial numbers, and DATE does not produce one: it renders
	// a formatted instant (see the milestone 07 function set), which DATEDIF
	// cannot parse. The two do not compose at this checkpoint.
	mustReject(t, f, `=DATEDIF(DATE(2020,1,1),DATE(2021,1,1),"D")`)
}
