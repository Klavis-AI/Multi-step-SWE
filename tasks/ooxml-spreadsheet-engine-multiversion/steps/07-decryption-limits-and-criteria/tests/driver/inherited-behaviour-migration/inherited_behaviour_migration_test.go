// Scored group: inherited-behaviour-migration.
//
// TEST_INTERFACE.md section 4.07.9, "Inherited behaviour that changes here",
// end to end. Everything this group asserts is behaviour an earlier milestone
// described differently and this checkpoint supersedes, so each case is new
// capability even though the API it exercises is inherited:
//
//   - a range reference resolves from its own top-left corner, not from A1 --
//     a property of reference resolution, so it reaches every range argument
//     of every function rather than just SUM;
//   - the comparison operators =, <, >, <= and >= evaluate over numeric
//     operands and produce the literal text "TRUE"/"FALSE";
//   - ^ exponentiates;
//   - <>, % and & have NOT moved and are asserted as the negatives 4.07.9
//     documents them to be, so an implementation that "finishes the job" is
//     wrong for this milestone;
//   - StreamWriter.Flush serialises its rows without populating the in-memory
//     worksheet.
//
// The operator grammar is OpenDocument v1.3 part 4 (OpenFormula) section 5.5
// "Operators" and reference resolution is its section 5.8 "References"; the
// part the streaming writer serialises into is ECMA-376 Part 1 section
// 18.3.1.99 (worksheet).
package inheritedbehaviourmigration

import (
	"bytes"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// grid is 4.07.9's own worked fixture: 1, 2, 3 down column A, 10, 20, 30 down
// column B, and a text cell at C1 that a clamped range would sweep up.
func grid(t *testing.T) *seedcalc.File {
	t.Helper()
	f := seedcalc.NewFile()
	for _, c := range []struct {
		axis string
		v    interface{}
	}{
		{"A1", 1}, {"A2", 2}, {"A3", 3},
		{"B1", 10}, {"B2", 20}, {"B3", 30},
		{"C1", "text"},
	} {
		if err := f.SetCellValue("Sheet1", c.axis, c.v); err != nil {
			t.Fatalf("SetCellValue(%s): %v", c.axis, err)
		}
	}
	return f
}

func eval(t *testing.T, f *seedcalc.File, formula string) (string, error) {
	t.Helper()
	if err := f.SetCellFormula("Sheet1", "H1", formula); err != nil {
		t.Fatalf("SetCellFormula(%q): %v", formula, err)
	}
	return f.CalcCellValue("Sheet1", "H1")
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

func mustReject(t *testing.T, f *seedcalc.File, formula string) {
	t.Helper()
	got, err := eval(t, f, formula)
	if err == nil {
		t.Fatalf("CalcCellValue(%q) = %q, want an error", formula, got)
	}
	if got != "" {
		t.Fatalf("CalcCellValue(%q) returned result %q alongside an error", formula, got)
	}
}

func TestARangeIsResolvedFromItsOwnTopLeftCorner(t *testing.T) {
	f := grid(t)
	// Milestone 06 seeded the rectangle at A1 and widened it to cover the
	// reference, so B1:B3 was read as A1:B3 and summed to 66. Here the
	// reference is read as written.
	mustEval(t, f, "=SUM(B1:B3)", "60")
	mustEval(t, f, "=SUM(B2:B3)", "50")
	mustEval(t, f, "=SUM(B1:B1)", "10")
	// Exact resolution is a property of reference resolution rather than of
	// SUM, so it reaches every range argument of every function.
	mustEval(t, f, "=COUNTA(B1:B3)", "3")
	mustEval(t, f, `=SUMIF(A1:A3,">1",B1:B3)`, "50")
}

func TestARangeAwayFromTheDataIsGenuinelyEmpty(t *testing.T) {
	f := grid(t)
	// A window onto the top-left of the sheet would have swept up C1's text
	// and failed with #VALUE!, which is exactly what milestone 06 did.
	mustEval(t, f, "=SUM(X1:X9)", "0")
	mustEval(t, f, "=COUNTA(X1:X9)", "0")

	lone := seedcalc.NewFile()
	if err := lone.SetCellValue("Sheet1", "A1", 1); err != nil {
		t.Fatalf("SetCellValue: %v", err)
	}
	mustEval(t, lone, "=SUM(X1:X9)", "0")

	// A text cell genuinely inside the reference still spoils the sum.
	mustReject(t, f, "=SUM(C1:C1)")
	mustReject(t, f, "=SUM(B1:C1)")
}

func TestTheFiveComparisonOperatorsEvaluate(t *testing.T) {
	f := grid(t)
	// The result is the literal text TRUE/FALSE -- the convention 4.07.4
	// states for AND and OR -- not 1/0 and not a boolean-typed value.
	for _, c := range []struct{ formula, want string }{
		{"=1=1", "TRUE"}, {"=1=2", "FALSE"},
		{"=1<2", "TRUE"}, {"=2<1", "FALSE"},
		{"=2>1", "TRUE"}, {"=1>2", "FALSE"},
		{"=1<=1", "TRUE"}, {"=1>=2", "FALSE"},
		// Cell references work in the same position, and a comparison
		// composes with arithmetic.
		{"=A1<A2", "TRUE"}, {"=A2<A1", "FALSE"},
		{"=1+2=3", "TRUE"},
	} {
		mustEval(t, f, c.formula, c.want)
	}
}

func TestExponentiationEvaluates(t *testing.T) {
	f := grid(t)
	// Milestone 06 yielded the right-hand operand, so =2^3 was "3".
	mustEval(t, f, "=2^3", "8")
	mustEval(t, f, "=2^2", "4")
	mustEval(t, f, "=2^-1", "0.5")
	mustEval(t, f, "=(1+1)^3", "8")
	mustEval(t, f, "=2^3+1", "9")
}

func TestTheOperatorSetThatMovedIsExactlyTheDocumentedOne(t *testing.T) {
	f := grid(t)
	// 4.07.9's negatives, each paired with the neighbour that did move, so
	// that the boundary is asserted and not just the gap. The table is
	// deliberately incomplete at this milestone and an implementation must
	// not widen it: "<" evaluates, "<>" does not.
	mustEval(t, f, "=1<2", "TRUE")
	mustEval(t, f, "=1<>2", "2")
	mustEval(t, f, "=2<>2", "2")
	// Exponentiation moved; the postfix percent sign did not, and still
	// yields its operand unscaled.
	mustEval(t, f, "=2^3", "8")
	mustEval(t, f, "=50%", "50")
	// Concatenation is refused outright, and the five comparisons that do
	// evaluate still require numeric operands.
	mustReject(t, f, `="a"&"b"`)
	mustReject(t, f, `="a"="a"`)
}

func TestFlushDoesNotPopulateTheInMemoryWorksheet(t *testing.T) {
	stream := func(t *testing.T) *seedcalc.File {
		t.Helper()
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
		return f
	}

	// Flush serialises the rows into the worksheet part. It does not populate
	// the worksheet struct the accessors read, so the sheet is still empty in
	// memory. Milestone 06 made those rows visible immediately.
	f := stream(t)
	rows, err := f.GetRows("Sheet1")
	if err != nil {
		t.Fatalf("GetRows after Flush: %v", err)
	}
	if len(rows) != 0 {
		t.Fatalf("GetRows after Flush returned %d rows, want none: %v", len(rows), rows)
	}
	got, err := f.GetCellValue("Sheet1", "A1")
	if err != nil {
		t.Fatalf("GetCellValue after Flush: %v", err)
	}
	if got != "" {
		t.Fatalf("GetCellValue(A1) after Flush = %q, want the empty string", got)
	}

	// The rows are in the package all the same: a workbook that was not read
	// in memory first writes and reopens with all five.
	g := stream(t)
	var buf bytes.Buffer
	if err := g.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	h, err := seedcalc.OpenReader(bytes.NewReader(buf.Bytes()))
	if err != nil {
		t.Fatalf("OpenReader: %v", err)
	}
	back, err := h.GetRows("Sheet1")
	if err != nil {
		t.Fatalf("GetRows after the round trip: %v", err)
	}
	if len(back) != 5 {
		t.Fatalf("GetRows after the round trip returned %d rows, want 5", len(back))
	}
	if back[4][0] != "5" || back[0][1] != "row" {
		t.Fatalf("streamed grid = %v, want the values written", back)
	}
}
