// Scored group: date-time-format-codes.
//
// The date and time half of the number-format engine: the calendar and clock
// tokens, the meridiem token, and the bracketed elapsed tokens that report a
// duration rather than a time of day. The built-in format identifiers that
// route through the same engine are checked alongside the custom codes.
//
// The tokens are ECMA-376 Part 1 section 18.8.30 (numFmt); the built-in
// identifiers are the implied-formatCode table in the same clause.
package datetimeformatcodes

import (
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// noon2022 is 2022-01-01 13:37:37, near enough: the fraction .5678 of a day
// is 13:37:37 and change, which exercises every clock token at once.
const (
	jan1     = 44562.0
	afternoon = 0.5678
)

func display(t *testing.T, style *seedcalc.Style, v interface{}) string {
	t.Helper()
	f := seedcalc.NewFile()
	id, err := f.NewStyle(style)
	if err != nil {
		t.Fatalf("NewStyle: %v", err)
	}
	if err := f.SetCellValue("Sheet1", "A1", v); err != nil {
		t.Fatalf("SetCellValue(%v): %v", v, err)
	}
	if err := f.SetCellStyle("Sheet1", "A1", "A1", id); err != nil {
		t.Fatalf("SetCellStyle: %v", err)
	}
	got, err := f.GetCellValue("Sheet1", "A1")
	if err != nil {
		t.Fatalf("GetCellValue: %v", err)
	}
	return got
}

func custom(t *testing.T, code string, v interface{}, expect string) {
	t.Helper()
	c := code
	if got := display(t, &seedcalc.Style{CustomNumFmt: &c}, v); got != expect {
		t.Fatalf("format %q of %v = %q, want %q", code, v, got, expect)
	}
}

func builtin(t *testing.T, id int, v interface{}, expect string) {
	t.Helper()
	if got := display(t, &seedcalc.Style{NumFmt: id}, v); got != expect {
		t.Fatalf("built-in format %d of %v = %q, want %q", id, v, got, expect)
	}
}

func TestElapsedHoursAccumulateBeyondADay(t *testing.T) {
	// [h] is an elapsed token: it reports the whole duration in hours rather
	// than the hour of the day, so a day and a half is 36 and not 12.
	custom(t, "[h]:mm:ss", 1.5, "36:00:00")
	custom(t, "[h]", 1.5, "36")
	// The unbracketed token still wraps at a day.
	custom(t, "hh:mm:ss", 1.5, "12:00:00")
}

func TestMinutesAfterAnElapsedHourAreReadAsTheMonth(t *testing.T) {
	// mm is ambiguous: minutes next to a clock token, month otherwise. An
	// elapsed hour does not put the parser in a clock context, so the mm
	// that follows one is the month -- 02 for a February serial, 04 for an
	// April one -- and not the minutes, which are zero in both.
	custom(t, "[h]:mm", 44593.5, "1070244:02")
	custom(t, "[h]:mm", 44652.5, "1071660:04")
	// A trailing seconds token resolves the ambiguity the other way.
	custom(t, "[h]:mm:ss", 44593.5, "1070244:00:00")
	// And beside an ordinary hour token it is minutes, as always.
	custom(t, "hh:mm", 44593.5, "12:00")
}

func TestElapsedMinutesAndSecondsAccumulateToo(t *testing.T) {
	custom(t, "[m]:ss", 0.5, "720:00")
	custom(t, "[s]", 0.5, "43200")
	custom(t, "[mm]:ss", 1.5, "2160:00")
	custom(t, "[ss]", 0.25, "21600")
}

func TestTheMeridiemTokenFollowsTheCaseItIsWrittenIn(t *testing.T) {
	// AM/PM and am/pm are distinct tokens; the engine emits the marker in
	// the case the code asked for.
	custom(t, "hh:mm:ss AM/PM", afternoon, "01:37:37 PM")
	custom(t, "h:mm am/pm", afternoon, "1:37 pm")
	custom(t, "h:mm AM/PM", afternoon, "1:37 PM")
	// Without the token the clock is twenty-four hour.
	custom(t, "hh:mm", afternoon, "13:37")
}

func TestTheCalendarNameTokensByLength(t *testing.T) {
	// Three letters abbreviate, four spell out, and five -- for a month --
	// is the single-letter form.
	custom(t, "ddd", jan1, "Sat")
	custom(t, "dddd", jan1, "Saturday")
	custom(t, "mmm", jan1, "Jan")
	custom(t, "mmmm", jan1, "January")
	custom(t, "mmmmm", jan1, "J")
}

func TestTheNumericDateTokensByLength(t *testing.T) {
	custom(t, "yyyy-mm-dd", jan1, "2022-01-01")
	custom(t, "mm/dd/yy", jan1, "01/01/22")
	custom(t, "d/m/yyyy", jan1, "1/1/2022")
	custom(t, "yyyy-mm-dd hh:mm:ss", 44562.5678, "2022-01-01 13:37:37")
	// The same date under a name token, which the engine renders from its
	// own calendar table rather than from the serial's digits.
	custom(t, "yyyy-mmm-ddd", jan1, "2022-Jan-Sat")
}

func TestTheBuiltInTimeFormatsEmitALowercaseMeridiem(t *testing.T) {
	// Built-ins 18 and 19 are h:mm AM/PM and h:mm:ss AM/PM. They route
	// through the engine, which renders their marker in lower case.
	builtin(t, 18, 1234.5678, "1:37 pm")
	builtin(t, 19, 1234.5678, "1:37:37 pm")
	builtin(t, 20, 1234.5678, "13:37")
	builtin(t, 21, 1234.5678, "13:37:37")
}

func TestTheBuiltInElapsedFormat(t *testing.T) {
	// Built-in 46 is [h]:mm:ss, so a serial of more than three thousand days
	// reports tens of thousands of hours.
	builtin(t, 46, 1234.5678, "29630:37:37")
	builtin(t, 45, 1234.5678, "37:37")
}

func TestABuiltInWithAnUnsupportedTokenFallsThrough(t *testing.T) {
	// Built-in 47 is mm:ss.0. The fractional-second token is outside the
	// recognised set, so the section is abandoned and the serial itself is
	// displayed.
	builtin(t, 47, 1234.5678, "1234.5678")
	// Its neighbours, which contain only recognised tokens, still format.
	builtin(t, 46, 44562.5, "1069500:00:00")
}

func TestTheBuiltInDateFormatsAreUnaffected(t *testing.T) {
	// The calendar-only built-ins were already correct and are not moved by
	// the new engine; they are checked here so the group covers the whole
	// range it claims, not only the identifiers that changed.
	builtin(t, 14, 1234.5678, "05-18-03")
	builtin(t, 15, 1234.5678, "18-May-03")
	builtin(t, 16, 1234.5678, "18-May")
	builtin(t, 17, 1234.5678, "May-03")
	builtin(t, 22, 1234.5678, "5/18/03 13:37")
	// Paired with one identifier from the same table that does move.
	builtin(t, 18, 1234.5678, "1:37 pm")
}
