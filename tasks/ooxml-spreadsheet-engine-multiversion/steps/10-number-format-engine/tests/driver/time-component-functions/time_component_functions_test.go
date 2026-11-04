// Scored group: time-component-functions.
//
// The clock half of the date functions: HOUR and SECOND, which take a serial
// number apart, and TIMEVALUE, which builds a fractional day out of text.
// MINUTE, which sits between them, arrived earlier and is checked here only
// to show the three agree on the same instant.
//
// Semantics follow OpenDocument v1.3 part 4 (OpenFormula) sections 6.10.10
// (HOUR), 6.10.16 (SECOND) and 6.10.19 (TIMEVALUE).
package timecomponentfunctions

import (
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

func eval(t *testing.T, f *seedcalc.File, formula string) (string, error) {
	t.Helper()
	if err := f.SetCellFormula("Sheet1", "Z1", formula); err != nil {
		t.Fatalf("SetCellFormula(%q): %v", formula, err)
	}
	return f.CalcCellValue("Sheet1", "Z1")
}

func mustEval(t *testing.T, f *seedcalc.File, formula, want string) {
	t.Helper()
	got, err := eval(t, f, formula)
	if err != nil {
		t.Fatalf("CalcCellValue(%q): unexpected error %v", formula, err)
	}
	if got != want {
		t.Fatalf("CalcCellValue(%q) = %q, want %q", formula, got, want)
	}
}

// mustRejectWith insists on the exact diagnostic, not merely on failure: a
// function that is absent altogether reports that it is unsupported, which
// is a different sentence from the one a present function uses to turn down
// a malformed call.
func mustRejectWith(t *testing.T, f *seedcalc.File, formula, wantErr string) {
	t.Helper()
	got, err := eval(t, f, formula)
	if err == nil {
		t.Fatalf("CalcCellValue(%q) = %q, want error %q", formula, got, wantErr)
	}
	if err.Error() != wantErr {
		t.Fatalf("CalcCellValue(%q) error = %q, want %q", formula, err.Error(), wantErr)
	}
}

func TestHourReadsTheHourOfTheDay(t *testing.T) {
	f := seedcalc.NewFile()
	// Three quarters of a day is eighteen hours.
	mustEval(t, f, `=HOUR(0.75)`, "18")
	mustEval(t, f, `=HOUR(0.5)`, "12")
	mustEval(t, f, `=HOUR("13:37:00")`, "13")
	// The whole-day part of a serial is discarded.
	mustEval(t, f, `=HOUR(44562.75)`, "18")
}

func TestSecondReadsTheSecondOfTheMinute(t *testing.T) {
	f := seedcalc.NewFile()
	mustEval(t, f, `=SECOND("13:37:45")`, "45")
	mustEval(t, f, `=SECOND(0.5)`, "0")
	mustEval(t, f, `=SECOND(44562.75)`, "0")
}

func TestTimevalueBuildsAFractionOfADay(t *testing.T) {
	f := seedcalc.NewFile()
	mustEval(t, f, `=TIMEVALUE("13:37:00")`, "0.567361111111111")
	// The twelve-hour spelling of the same instant gives the same fraction.
	mustEval(t, f, `=TIMEVALUE("1:37 PM")`, "0.567361111111111")
	mustEval(t, f, `=TIMEVALUE("12:00:00")`, "0.5")
	mustEval(t, f, `=TIMEVALUE("00:00:00")`, "0")
}

func TestTheThreeComponentsAgreeOnOneInstant(t *testing.T) {
	f := seedcalc.NewFile()
	mustEval(t, f, `=HOUR(TIMEVALUE("13:37:45"))`, "13")
	mustEval(t, f, `=MINUTE(TIMEVALUE("13:37:45"))`, "37")
	mustEval(t, f, `=SECOND(TIMEVALUE("13:37:45"))`, "45")
}

func TestTheClockFunctionsRefuseWhatTheyCannotRead(t *testing.T) {
	f := seedcalc.NewFile()
	// A serial number is a count forward from the epoch, so it cannot be
	// negative; the two functions say so in the same words.
	mustRejectWith(t, f, `=HOUR(-1)`, "HOUR only accepts positive argument")
	mustRejectWith(t, f, `=SECOND(-1)`, "SECOND only accepts positive argument")
	// Text that is not a time is a bad value.
	mustRejectWith(t, f, `=HOUR("nope")`, "#VALUE!")
	mustRejectWith(t, f, `=SECOND("nope")`, "#VALUE!")
	mustRejectWith(t, f, `=TIMEVALUE("nope")`, "#VALUE!")
	// TIMEVALUE reads text only; a number is not a time expression.
	mustRejectWith(t, f, `=TIMEVALUE(1)`, "#VALUE!")
}

func TestTheArityOfTheClockFunctions(t *testing.T) {
	f := seedcalc.NewFile()
	mustRejectWith(t, f, `=HOUR()`, "HOUR requires exactly 1 argument")
	mustRejectWith(t, f, `=SECOND()`, "SECOND requires exactly 1 argument")
	mustRejectWith(t, f, `=TIMEVALUE()`, "TIMEVALUE requires exactly 1 argument")
}
