// Scored group: date-and-time-functions.
//
// The date and time functions this milestone registers: DATEVALUE, DAY, DAYS,
// MONTH, YEAR, MINUTE, TIME, WEEKDAY, ISOWEEKNUM and YEARFRAC. They all work in
// the serial-number representation the workbook stores dates in, where 1 is
// 1900-01-01 and the fractional part is the time of day. Behaviour transcribed
// from the upstream checkpoint this milestone tracks, qax-os/excelize tag
// v2.5.0 (calc_test.go), restricted to the public interface. The functions are
// OpenDocument v1.3 part 4 (OpenFormula) sections 6.10.4 (DATEVALUE), 6.10.5
// (DAY), 6.10.6 (DAYS), 6.10.11 (ISOWEEKNUM), 6.10.12 (MINUTE), 6.10.13
// (MONTH), 6.10.17 (TIME), 6.10.20 (WEEKDAY), 6.10.23 (YEAR) and 6.10.24
// (YEARFRAC).
package dateandtimefunctions

import (
	"math"
	"strconv"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// The serial numbers used throughout: 2020-01-01 and 2021-03-04, which is a
// Thursday in ISO week 9.
const (
	jan2020 = "43831"
	mar2021 = "44259"
)

func mustEval(t *testing.T, formula, want string) {
	t.Helper()
	f := seedcalc.NewFile()
	if err := f.SetCellFormula("Sheet1", "H1", formula); err != nil {
		t.Fatalf("SetCellFormula(%q): %v", formula, err)
	}
	got, err := f.CalcCellValue("Sheet1", "H1")
	if err != nil {
		t.Fatalf("CalcCellValue(%q): unexpected error %v", formula, err)
	}
	if got != want {
		t.Fatalf("CalcCellValue(%q) = %q, want %q", formula, got, want)
	}
}

// mustEvalNear grades a numeric result on its value rather than on its
// rendering, to a relative 1e-9. It exists for YEARFRAC basis 1 and nothing
// else in this group. That is the one date result here whose exact rendering
// fits in fifteen decimal places, so TEST_INTERFACE.md 4.09.17 hands it back
// unrounded at sixteen significant figures and the final unit in the last
// place reaches the string. 4.09.4 fixes the day-count convention and nothing
// about how the quotient is formed, so an implementation that divides the day
// count by the averaged year length in a different order lands on the
// neighbouring double and renders differently. The other five bases render at
// fifteen significant figures or fewer, which absorbs an ULP, so they stay
// exact strings. 1e-9 relative is far tighter than a wrong basis, a wrong day
// count or an off-by-one on either endpoint.
func mustEvalNear(t *testing.T, formula string, want float64) {
	t.Helper()
	f := seedcalc.NewFile()
	if err := f.SetCellFormula("Sheet1", "H1", formula); err != nil {
		t.Fatalf("SetCellFormula(%q): %v", formula, err)
	}
	got, err := f.CalcCellValue("Sheet1", "H1")
	if err != nil {
		t.Fatalf("CalcCellValue(%q): unexpected error %v", formula, err)
	}
	v, perr := strconv.ParseFloat(got, 64)
	if perr != nil {
		t.Fatalf("CalcCellValue(%q) = %q, want a number near %v", formula, got, want)
	}
	if math.Abs(v-want) > 1e-9*math.Abs(want) {
		t.Fatalf("CalcCellValue(%q) = %q, want %v to a relative 1e-9", formula, got, want)
	}
}

func mustReject(t *testing.T, formula string) {
	t.Helper()
	f := seedcalc.NewFile()
	if err := f.SetCellFormula("Sheet1", "H1", formula); err != nil {
		t.Fatalf("SetCellFormula(%q): %v", formula, err)
	}
	got, err := f.CalcCellValue("Sheet1", "H1")
	if err == nil {
		t.Fatalf("CalcCellValue(%q) = %q, want an error", formula, got)
	}
	if got != "" {
		t.Fatalf("CalcCellValue(%q) returned result %q alongside an error", formula, got)
	}
}

func TestDATEVALUEConvertsTextToASerialNumber(t *testing.T) {
	mustEval(t, `=DATEVALUE("2021-03-04")`, mar2021)
	mustEval(t, `=DATEVALUE("03/04/2021")`, mar2021)
	mustEval(t, `=DATEVALUE("2020-01-01")`, jan2020)
	// A time of day in the text is parsed and then discarded: DATEVALUE
	// yields the whole-day serial only.
	mustEval(t, `=DATEVALUE("2021-03-04 12:00:00")`, mar2021)
	// The argument must be text. A serial number, which is already the
	// answer, is not accepted as input.
	mustReject(t, `=DATEVALUE(44259)`)
	mustReject(t, `=DATEVALUE("4-Mar-2021")`)
	mustReject(t, `=DATEVALUE("not a date")`)
	mustReject(t, `=DATEVALUE("2021-03-04","x")`)
}

func TestDAYMONTHAndYEARSplitADate(t *testing.T) {
	// Each accepts either a serial number or a parseable date string.
	mustEval(t, "=DAY(44259)", "4")
	mustEval(t, `=DAY("2021-03-04")`, "4")
	mustEval(t, "=MONTH(44259)", "3")
	mustEval(t, `=MONTH("2021-03-04")`, "3")
	mustEval(t, "=YEAR(44259)", "2021")
	mustEval(t, `=YEAR("2021-03-04")`, "2021")
	// Serial zero is the day before the epoch, 1899-12-30, and each part
	// reports that date rather than refusing it.
	mustEval(t, "=DAY(0)", "0")
	mustEval(t, "=MONTH(0)", "12")
	mustEval(t, "=YEAR(0)", "1899")
	// A negative serial has no date, and the argument count is exact.
	mustReject(t, "=DAY(-1)")
	mustReject(t, `=DAY("nonsense")`)
	mustReject(t, "=DAY(44259,1)")
}

func TestDAYSCountsTheIntervalBetweenTwoDates(t *testing.T) {
	// The end date comes first, so the natural ordering gives a positive
	// count and the reverse gives a negative one.
	mustEval(t, "=DAYS(44259,43831)", "428")
	mustEval(t, `=DAYS("2021-03-04","2020-01-01")`, "428")
	mustEval(t, "=DAYS(43831,44259)", "-428")
	mustEval(t, "=DAYS(44259,44259)", "0")
	mustReject(t, "=DAYS(44259)")
}

func TestTIMEBuildsTheFractionOfADay(t *testing.T) {
	mustEval(t, "=TIME(0,0,0)", "0")
	mustEval(t, "=TIME(12,0,0)", "0.5")
	mustEval(t, "=TIME(6,0,0)", "0.25")
	mustEval(t, "=TIME(12,30,45)", "0.521354166666667")
	// Hours beyond a day are not folded back, so the result exceeds 1.
	mustEval(t, "=TIME(25,0,0)", "1.04166666666667")
	// Text arguments that read as numbers are accepted.
	mustEval(t, `=TIME("12","30","45")`, "0.521354166666667")
	// A negative component has no time, and all three are required.
	mustReject(t, "=TIME(-1,0,0)")
	mustReject(t, "=TIME(0,0,-1)")
	mustReject(t, "=TIME(12,30)")
}

func TestMINUTEReadsTheMinuteOutOfATime(t *testing.T) {
	mustEval(t, `=MINUTE("15:30:00")`, "30")
	mustEval(t, `=MINUTE("15:00:00")`, "0")
	// From a serial number, only the fractional part matters; half a day is
	// noon exactly, so its minute is zero whichever day it falls on.
	mustEval(t, "=MINUTE(0.5)", "0")
	mustEval(t, "=MINUTE(44259.5)", "0")
	mustReject(t, "=MINUTE(-1)")
	mustReject(t, `=MINUTE("nope")`)
}

func TestWEEKDAYNumbersTheDayUnderTheChosenScheme(t *testing.T) {
	// 2021-03-04 is a Thursday.
	mustEval(t, "=WEEKDAY(44259)", "5")   // default: Sunday is 1
	mustEval(t, "=WEEKDAY(44259,1)", "5") // the same scheme, named
	mustEval(t, "=WEEKDAY(44259,2)", "4") // Monday is 1
	mustEval(t, "=WEEKDAY(44259,3)", "3") // Monday is 0
	mustEval(t, "=WEEKDAY(44259,11)", "4")
	mustEval(t, `=WEEKDAY("2021-03-04")`, "5")
	// An unrecognised scheme number is refused.
	mustReject(t, "=WEEKDAY(44259,99)")
	mustReject(t, "=WEEKDAY()")
}

func TestISOWEEKNUMNumbersTheWeek(t *testing.T) {
	mustEval(t, "=ISOWEEKNUM(44259)", "9")
	mustEval(t, `=ISOWEEKNUM("2021-03-04")`, "9")
	mustEval(t, "=ISOWEEKNUM(43831)", "1")
	mustReject(t, `=ISOWEEKNUM("nope")`)
	mustReject(t, "=ISOWEEKNUM(44259,1)")
}

func TestYEARFRACMeasuresTheIntervalInYears(t *testing.T) {
	// The optional third argument is the day-count basis. Basis 0, the
	// default, is the 30/360 US convention.
	mustEval(t, "=YEARFRAC(43831,44259)", "1.175")
	mustEval(t, "=YEARFRAC(43831,44259,0)", "1.175")
	mustEvalNear(t, "=YEARFRAC(43831,44259,1)", 1.170998632010944) // actual/actual
	mustEval(t, "=YEARFRAC(43831,44259,2)", "1.18888888888889")    // actual/360
	mustEval(t, "=YEARFRAC(43831,44259,3)", "1.17260273972603")    // actual/365
	mustEval(t, "=YEARFRAC(43831,44259,4)", "1.175")               // 30/360 European
	mustEval(t, `=YEARFRAC("2020-01-01","2021-03-04")`, "1.175")
	// The same date twice is zero regardless of basis.
	mustEval(t, "=YEARFRAC(44259,44259,1)", "0")
	mustReject(t, "=YEARFRAC(43831,44259,5)")
	mustReject(t, "=YEARFRAC(43831)")
}

func TestTheDateFunctionsComposeWithEachOther(t *testing.T) {
	// DATEVALUE feeds the extractors, and the extractors agree with the
	// direct serial-number form.
	mustEval(t, `=DAY(DATEVALUE("2021-03-04"))`, "4")
	mustEval(t, `=MONTH(DATEVALUE("2021-03-04"))`, "3")
	mustEval(t, `=YEAR(DATEVALUE("2021-03-04"))`, "2021")
	mustEval(t, `=WEEKDAY(DATEVALUE("2021-03-04"),2)`, "4")
	mustEval(t, `=DAYS(DATEVALUE("2021-03-04"),DATEVALUE("2020-01-01"))`, "428")
	mustEval(t, "=MINUTE(TIME(15,30,0))", "30")
}

func TestDateResultsAreOrdinaryNumbers(t *testing.T) {
	f := seedcalc.NewFile()
	if err := f.SetCellValue("Sheet1", "A1", 44259); err != nil {
		t.Fatalf("SetCellValue: %v", err)
	}
	for axis, formula := range map[string]string{
		"B1": "=YEAR(A1)+MONTH(A1)+DAY(A1)",
		"B2": "=DAYS(A1,A1-10)",
		"B3": "=SUM(WEEKDAY(A1),ISOWEEKNUM(A1))",
	} {
		if err := f.SetCellFormula("Sheet1", axis, formula); err != nil {
			t.Fatalf("SetCellFormula(%s): %v", axis, err)
		}
	}
	for axis, want := range map[string]string{
		"B1": "2028", // 2021 + 3 + 4
		"B2": "10",
		"B3": "14", // 5 + 9
	} {
		got, err := f.CalcCellValue("Sheet1", axis)
		if err != nil {
			t.Fatalf("CalcCellValue(%s): %v", axis, err)
		}
		if got != want {
			t.Fatalf("CalcCellValue(%s) = %q, want %q", axis, got, want)
		}
	}
}
