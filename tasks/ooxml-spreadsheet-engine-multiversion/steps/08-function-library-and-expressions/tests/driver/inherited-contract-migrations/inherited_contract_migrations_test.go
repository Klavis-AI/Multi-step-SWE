// Scored group: inherited-contract-migrations (milestone 08, new behaviour).
//
// The three milestone 07 contracts this milestone deliberately changes,
// asserted in their new form. They are collected here rather than left in
// milestone-07-functions-regression because that group is a gate: behaviour a
// milestone changes on purpose cannot also be the regression it is measured
// against.
//
//   - a bare TRUE or FALSE in an argument position now resolves to the
//     registered function and reaches the connective, where milestone 07
//     rejected the call;
//   - NA() at the top level is now an error rather than a successful call
//     returning the text "#N/A";
//   - MEDIAN over a scalar argument list now takes the whole list as the
//     sample, where milestone 07 read it as the last argument repeated.
//
// ECMA-376 Part 1 clause 18.17.2 fixes the syntax of a formula and clause
// 18.17.7 the function list these three belong to.
package inheritedcontractmigrations

import (
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// sheet builds the fixture:
//
//	     A     B          C        D
//	1    1     apple      10
//	2    2     banana     20       written
//	3    3     apricot    30
//	4    4     cherry     40
func sheet(t *testing.T) *seedcalc.File {
	t.Helper()
	f := seedcalc.NewFile()
	for _, c := range []struct {
		axis string
		v    interface{}
	}{
		{"A1", 1}, {"A2", 2}, {"A3", 3}, {"A4", 4},
		{"B1", "apple"}, {"B2", "banana"}, {"B3", "apricot"}, {"B4", "cherry"},
		{"C1", 10}, {"C2", 20}, {"C3", 30}, {"C4", 40},
		{"D2", "written"},
	} {
		if err := f.SetCellValue("Sheet1", c.axis, c.v); err != nil {
			t.Fatalf("SetCellValue(%s): %v", c.axis, err)
		}
	}
	return f
}

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

func TestTheBareBooleanWordsNowReachTheConnectives(t *testing.T) {
	f := sheet(t)
	// Contract change from milestone 07, where a bare TRUE never reached the
	// function and the call was rejected. TRUE and FALSE are registered
	// functions here, so an unparenthesised occurrence in an argument position
	// resolves to that call and the connective sees a boolean.
	mustEval(t, f, "=AND(TRUE,TRUE)", "TRUE")
	mustEval(t, f, "=AND(TRUE,FALSE)", "FALSE")
	mustEval(t, f, "=OR(FALSE,TRUE)", "TRUE")
	// Only in an argument position: a bare word is still not a whole formula.
	mustReject(t, f, "=TRUE")
	mustReject(t, f, "=FALSE")
}

func TestNAIsNowReportedAsAnError(t *testing.T) {
	f := sheet(t)
	// Contract change from milestone 07, where the call succeeded and handed
	// back "#N/A" as an ordinary result. At the top level it is now an error.
	mustReject(t, f, "=NA()")
	// Inside an expression it is still the #N/A error value, so the functions
	// that inspect errors see exactly what they saw before.
	mustEval(t, f, "=ISNA(NA())", "TRUE")
	mustEval(t, f, "=ISERR(NA())", "FALSE")
	mustEval(t, f, "=ISERROR(NA())", "TRUE")
	mustEval(t, f, `=IFERROR(NA(),"fallback")`, "fallback")
	mustReject(t, f, "=NA(1)")
}

func TestMEDIANNowAveragesTheWholeArgumentList(t *testing.T) {
	f := sheet(t)
	// Contract change from milestone 07, where a scalar argument list was read
	// as the last argument repeated, so the median was always that argument.
	// The whole list is the sample here.
	mustEval(t, f, "=MEDIAN(1,2,3)", "2")
	mustEval(t, f, "=MEDIAN(3,1,2)", "2")
	mustEval(t, f, "=MEDIAN(1,2,3,4)", "2.5")
	// A single argument is its own median either way, and a range was never
	// affected by the old behaviour.
	mustEval(t, f, "=MEDIAN(5)", "5")
	mustEval(t, f, "=MEDIAN(A1:A4)", "2.5")
	mustReject(t, f, "=MEDIAN()")
}
