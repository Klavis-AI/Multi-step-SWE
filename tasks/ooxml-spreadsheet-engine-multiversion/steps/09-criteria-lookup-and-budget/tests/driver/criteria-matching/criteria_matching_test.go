// Scored group: criteria-matching.
//
// The criteria family this milestone completes: COUNTIF, COUNTIFS, AVERAGEIF,
// MAXIFS and MINIFS join the SUMIF that milestone 07 introduced, and the
// criteria parser behind all of them is rebuilt. Two inherited SUMIF contracts
// move as a result and are scored here. Behaviour transcribed from the
// upstream checkpoint this milestone tracks, qax-os/excelize tag v2.5.0
// (calc_test.go), restricted to the public interface. The functions themselves
// are OpenDocument v1.3 part 4 (OpenFormula) sections 6.13.9 (COUNTIF) and
// 6.13.10 (COUNTIFS), which the spec files under Information Functions,
// 6.18.5 (AVERAGEIF) and 6.16.62 (SUMIF). MAXIFS and MINIFS have no
// OpenFormula definition and are graded from TEST_INTERFACE.md 4.09.1 alone.
package criteriamatching

import (
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// fruit builds the grid every test in this group reads:
//
//	     A     B          C
//	1    1    apple       1
//	2    2    banana      2
//	3    3    apricot     2
//	4    4    cherry      3
func fruit(t *testing.T) *seedcalc.File {
	t.Helper()
	f := seedcalc.NewFile()
	for _, c := range []struct {
		axis string
		v    interface{}
	}{
		{"A1", 1}, {"A2", 2}, {"A3", 3}, {"A4", 4},
		{"B1", "apple"}, {"B2", "banana"}, {"B3", "apricot"}, {"B4", "cherry"},
		{"C1", 1}, {"C2", 2}, {"C3", 2}, {"C4", 3},
	} {
		if err := f.SetCellValue("Sheet1", c.axis, c.v); err != nil {
			t.Fatalf("SetCellValue(%s): %v", c.axis, err)
		}
	}
	return f
}

func mustEval(t *testing.T, f *seedcalc.File, formula, want string) {
	t.Helper()
	if err := f.SetCellFormula("Sheet1", "Z1", formula); err != nil {
		t.Fatalf("SetCellFormula(%q): %v", formula, err)
	}
	got, err := f.CalcCellValue("Sheet1", "Z1")
	if err != nil {
		t.Fatalf("CalcCellValue(%q): unexpected error %v", formula, err)
	}
	if got != want {
		t.Fatalf("CalcCellValue(%q) = %q, want %q", formula, got, want)
	}
}

func mustReject(t *testing.T, f *seedcalc.File, formula string) {
	t.Helper()
	if err := f.SetCellFormula("Sheet1", "Z1", formula); err != nil {
		t.Fatalf("SetCellFormula(%q): %v", formula, err)
	}
	got, err := f.CalcCellValue("Sheet1", "Z1")
	if err == nil {
		t.Fatalf("CalcCellValue(%q) = %q, want an error", formula, got)
	}
	if got != "" {
		t.Fatalf("CalcCellValue(%q) returned result %q alongside an error", formula, got)
	}
}

func TestCOUNTIFCountsTheCellsMatchingOneCriterion(t *testing.T) {
	f := fruit(t)
	mustEval(t, f, `=COUNTIF(A1:A4,">2")`, "2")
	mustEval(t, f, `=COUNTIF(A1:A4,">=2")`, "3")
	mustEval(t, f, `=COUNTIF(A1:A4,"<2")`, "1")
	mustEval(t, f, `=COUNTIF(A1:A4,"<=2")`, "2")
	mustEval(t, f, `=COUNTIF(A1:A4,"=2")`, "1")
	// A bare number, quoted or not, is an equality criterion.
	mustEval(t, f, `=COUNTIF(A1:A4,"2")`, "1")
	mustEval(t, f, `=COUNTIF(C1:C4,2)`, "2")
	// Exactly two arguments; a criterion range alone is not enough.
	mustReject(t, f, `=COUNTIF(A1:A4)`)
	mustReject(t, f, `=COUNTIF(A1:A4,">1",C1:C4)`)
}

func TestTheNotEqualOperatorIsRecognisedHere(t *testing.T) {
	f := fruit(t)
	// Milestone 07 matched only the leading "<" of "<>", was left with ">2"
	// as the operand, and so selected nothing. The parser reads "<>" as one
	// operator at this checkpoint.
	mustEval(t, f, `=SUMIF(A1:A4,"<>2")`, "8")
	mustEval(t, f, `=COUNTIF(A1:A4,"<>2")`, "3")
	mustEval(t, f, `=COUNTIF(B1:B4,"<>apple")`, "3")
	// The comparison is made on the string form, so "<>" against a number
	// that is not in the range excludes nothing.
	mustEval(t, f, `=COUNTIF(A1:A4,"<>9")`, "4")
}

func TestATextCriterionIsAnUnanchoredRegularExpression(t *testing.T) {
	f := fruit(t)
	// A text criterion that is not an operator form is compiled as a Go
	// regular expression and matched without anchors, so it succeeds wherever
	// it occurs in the value rather than only at the start or the end.
	mustEval(t, f, `=COUNTIF(B1:B4,"ap")`, "2")
	mustEval(t, f, `=COUNTIF(B1:B4,"rry")`, "1")
	mustEval(t, f, `=COUNTIF(B1:B4,"apple")`, "1")
	// Matching is case sensitive.
	mustEval(t, f, `=COUNTIF(B1:B4,"APPLE")`, "0")
	// Regular-expression syntax is live: "^" anchors, "$" terminates.
	mustEval(t, f, `=COUNTIF(B1:B4,"^ap")`, "2")
	mustEval(t, f, `=COUNTIF(B1:B4,"e$")`, "1")
}

func TestTheWildcardsAreTranslatedIntoThatExpression(t *testing.T) {
	f := fruit(t)
	// "*" becomes ".*" and "?" becomes "." before the match, and the match is
	// still unanchored. Milestone 07 read a leading star as "ends with" and a
	// trailing star as "begins with"; neither survives, because the
	// translated pattern may match anywhere.
	mustEval(t, f, `=SUMIF(B1:B4,"ap*",A1:A4)`, "4")
	mustEval(t, f, `=SUMIF(B1:B4,"*rry",A1:A4)`, "4")
	// "*a" is ".*a", which matches every value containing an "a", not only
	// those ending in one: milestone 07 gave 2 here.
	mustEval(t, f, `=SUMIF(B1:B4,"*a",A1:A4)`, "6")
	mustEval(t, f, `=SUMIF(B1:B4,"a*",A1:A4)`, "6")
	// "?" is any one character, so five of them select every value at least
	// five characters long.
	mustEval(t, f, `=COUNTIF(B1:B4,"?????")`, "4")
	mustEval(t, f, `=COUNTIF(B1:B4,"???????")`, "1")
}

func TestCOUNTIFSPairsEachRangeWithItsOwnCriterion(t *testing.T) {
	f := fruit(t)
	mustEval(t, f, `=COUNTIFS(A1:A4,">1")`, "3")
	mustEval(t, f, `=COUNTIFS(A1:A4,">1",C1:C4,"2")`, "2")
	mustEval(t, f, `=COUNTIFS(A1:A4,">1",C1:C4,"2",B1:B4,"a")`, "2")
	// A first criterion that selects nothing ends the whole match.
	mustEval(t, f, `=COUNTIFS(A1:A4,">9",C1:C4,"1")`, "0")
	// A range without a criterion is #N/A, not a two-argument COUNTIF.
	mustReject(t, f, `=COUNTIFS(A1:A4,">1",C1:C4)`)
	mustReject(t, f, `=COUNTIFS(A1:A4)`)
}

func TestALaterCriterionThatSelectsNothingIsDiscarded(t *testing.T) {
	f := fruit(t)
	// The pairs are applied left to right, each narrowing the surviving set.
	// When a pair after the first narrows that set to nothing, the narrowing
	// is abandoned and the set from the preceding pair is the result, so the
	// count is the one from before the empty criterion rather than zero.
	// ">3" alone selects A4; C4 is 3, so "1" matches nothing, and the answer
	// is 1 rather than the 0 an intersection would give.
	mustEval(t, f, `=COUNTIFS(A1:A4,">3",C1:C4,"1")`, "1")
	mustEval(t, f, `=COUNTIFS(A1:A4,">2",C1:C4,"9")`, "2")
	// The same rule governs MAXIFS and MINIFS, which share the matcher: the
	// aggregate is taken over the set that survived the last non-empty pair.
	mustEval(t, f, `=MAXIFS(A1:A4,A1:A4,">3",C1:C4,"1")`, "4")
	mustEval(t, f, `=MINIFS(A1:A4,A1:A4,">2",C1:C4,"9")`, "3")
}

func TestAVERAGEIFMeansTheSelectedCells(t *testing.T) {
	f := fruit(t)
	mustEval(t, f, `=AVERAGEIF(A1:A4,">2")`, "3.5")
	mustEval(t, f, `=AVERAGEIF(A1:A4,"<>2")`, "2.66666666666667")
	// A third argument moves the averaging to a parallel range.
	mustEval(t, f, `=AVERAGEIF(B1:B4,"ap",A1:A4)`, "2")
	// Selecting nothing is a divide by zero, not an empty average.
	mustReject(t, f, `=AVERAGEIF(A1:A4,">9")`)
	mustReject(t, f, `=AVERAGEIF(A1:A4)`)
}

func TestMAXIFSAndMINIFSTakeTheValueRangeFirst(t *testing.T) {
	f := fruit(t)
	// Unlike AVERAGEIF, the aggregated range comes first and the criterion
	// pairs follow, so at least three arguments are always required.
	mustEval(t, f, `=MAXIFS(A1:A4,B1:B4,"ap")`, "3")
	mustEval(t, f, `=MINIFS(A1:A4,B1:B4,"ap")`, "1")
	mustEval(t, f, `=MAXIFS(A1:A4,C1:C4,"2")`, "3")
	mustEval(t, f, `=MINIFS(A1:A4,C1:C4,"2")`, "2")
	// Selecting nothing gives zero rather than an error.
	mustEval(t, f, `=MAXIFS(A1:A4,B1:B4,"zzz")`, "0")
	mustEval(t, f, `=MINIFS(A1:A4,B1:B4,"zzz")`, "0")
	mustReject(t, f, `=MAXIFS(A1:A4)`)
	mustReject(t, f, `=MINIFS(A1:A4,B1:B4)`)
}
