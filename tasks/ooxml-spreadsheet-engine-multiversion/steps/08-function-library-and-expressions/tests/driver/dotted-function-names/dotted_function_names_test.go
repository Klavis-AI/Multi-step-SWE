// Scored group: dotted-function-names.
//
// Five functions that milestone 07 registered under a run-together name are
// renamed here to the dotted spelling the specification uses, and the
// statistical additions arrive under dotted names from the start. The dot is
// part of the registered name, so the tokenizer has to carry it through the
// function-name token rather than treating it as punctuation. Names follow
// OpenDocument v1.3 part 4 (OpenFormula) section 6.17 "Rounding Functions" --
// which is where the ceiling and floor pair lives, not 6.16 "Mathematical
// Functions" -- and section 6.18 "Statistical Functions".
//
// What is scored here is therefore the change in the accepted spellings, not
// the arithmetic: the milestone 07 tokenizer discarded the dot before the
// lookup, so the dotted rounding names already evaluated there. The rounding
// arithmetic itself — significance, the sign-valued mode, the untouched plain
// CEILING and FLOOR — is a veto in milestone-07-functions-regression instead.
// Both halves of the rename are asserted here, though, because a rename is a
// replacement and neither half alone says so: the five run-together spellings
// must be gone AND the five dotted spellings must resolve.
package dottedfunctionnames

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

// mustRejectWith is mustReject with the rejection message pinned. An arity
// complaint names the function and the count it wanted; a formula the engine
// has never heard of is refused for an entirely different reason, and the two
// must not be allowed to look alike to this group.
func mustRejectWith(t *testing.T, f *seedcalc.File, formula, wantErr string) {
	t.Helper()
	got, err := eval(t, f, formula)
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

func TestTheRunTogetherSpellingsAreGone(t *testing.T) {
	f := seedcalc.NewFile()
	// These five names resolved at milestone 07. The rename is a replacement,
	// not an addition: the old spelling is no longer registered at all and the
	// dotted one that took its place carries the same signature. Each pair is
	// asserted together so that neither a registry that kept both spellings nor
	// one that dropped the family outright can pass.
	for _, pair := range []struct {
		gone, dotted, want string
	}{
		{"=CEILINGMATH(1.2)", "=CEILING.MATH(1.2)", "2"},
		{"=CEILINGPRECISE(1.2)", "=CEILING.PRECISE(1.2)", "2"},
		{"=FLOORMATH(1.8)", "=FLOOR.MATH(1.8)", "1"},
		{"=FLOORPRECISE(1.8)", "=FLOOR.PRECISE(1.8)", "1"},
		{"=ISOCEILING(1.2)", "=ISO.CEILING(1.2)", "2"},
	} {
		mustReject(t, f, pair.gone)
		mustEval(t, f, pair.dotted, pair.want)
	}
}

func TestTheDottedStatisticalNamesResolve(t *testing.T) {
	f := seedcalc.NewFile()
	mustEval(t, f, "=STDEV.S(1,2,3)", "1")
	mustEval(t, f, "=VAR.P(1,2,3)", "0.666666666666667")
	mustEval(t, f, "=QUARTILE.INC(1,1)", "1")
	mustEval(t, f, "=POISSON.DIST(2,3,TRUE)", "0.423190081126844")
	// STDEV.S is a new name only: there is no run-together STDEVS.
	mustReject(t, f, "=STDEVS(1,2,3)")
}

func TestTheDottedAndUndottedStatisticalNamesCoexist(t *testing.T) {
	f := seedcalc.NewFile()
	// Unlike the ceiling family, the statistical renames are additions: both
	// spellings are registered and they agree.
	mustEval(t, f, "=VARP(1,2,3)", "0.666666666666667")
	mustEval(t, f, "=VAR.P(1,2,3)", "0.666666666666667")
	mustEval(t, f, "=NORMDIST(1,0,1,TRUE)", "0.841344746068543")
	mustEval(t, f, "=NORM.DIST(1,0,1,TRUE)", "0.841344746068543")
	mustEval(t, f, "=NORM.INV(0.5,0,1)", "0")
	mustEval(t, f, "=NORMSINV(0.5)", "0")
	mustEval(t, f, "=NORM.S.INV(0.5)", "0")
}

func TestATwiceDottedNameAndItsOwnArity(t *testing.T) {
	f := seedcalc.NewFile()
	// NORM.S.DIST carries two dots, so the tokenizer's function-name token has
	// to admit more than one. It also differs from its undotted counterpart:
	// NORMSDIST takes the value alone, NORM.S.DIST requires the cumulative
	// flag as well.
	mustEval(t, f, "=NORM.S.DIST(1,TRUE)", "0.841344746068543")
	mustEval(t, f, "=NORMSDIST(1)", "0.841344746068543")
	mustReject(t, f, "=NORM.S.DIST(1)")
	mustReject(t, f, "=NORM.S.DIST(1,TRUE,3)")
}

func TestPERCENTILEINCRequiresBothArguments(t *testing.T) {
	f := seedcalc.NewFile()
	// The complaint spells the dotted name back, which is what distinguishes a
	// function that exists and was miscalled from one the engine cannot find.
	mustRejectWith(t, f, "=PERCENTILE.INC(1,2,3)", "PERCENTILE.INC requires 2 arguments")
}
