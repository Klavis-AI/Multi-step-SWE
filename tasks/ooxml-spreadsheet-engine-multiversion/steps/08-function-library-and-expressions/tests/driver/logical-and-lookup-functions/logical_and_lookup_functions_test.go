// Scored group: logical-and-lookup-functions.
//
// The conditional and lookup families this milestone adds: IF, IFERROR, NOT,
// the TRUE and FALSE nullary constructors, CHOOSE, and the VLOOKUP, HLOOKUP
// and LOOKUP search functions. Semantics follow OpenDocument v1.3 part 4
// (OpenFormula) section 6.15 "Logical Functions" and section 6.14 "Lookup
// Functions", with the deviations this checkpoint exhibits pinned where they
// occur.
//
// The group also carries this milestone's expression-level change, since the
// conditional functions are where it is observed: an argument may be a nested
// call, and the comparison operators gain <> (TEST_INTERFACE.md 4.08.11).
package logicalandlookupfunctions

import (
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// grid builds the fixture every case in this group reads:
//
//	     A     B         C
//	1    1     apple     10
//	2    2     banana    20
//	3    3     cherry    30
//	4    4     date      40
func grid(t *testing.T) *seedcalc.File {
	t.Helper()
	f := seedcalc.NewFile()
	for _, c := range []struct {
		axis string
		v    interface{}
	}{
		{"A1", 1}, {"A2", 2}, {"A3", 3}, {"A4", 4},
		{"B1", "apple"}, {"B2", "banana"}, {"B3", "cherry"}, {"B4", "date"},
		{"C1", 10}, {"C2", 20}, {"C3", 30}, {"C4", 40},
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

func TestIFSelectsABranch(t *testing.T) {
	f := grid(t)
	mustEval(t, f, `=IF(1,"yes","no")`, "yes")
	mustEval(t, f, `=IF(0,"yes","no")`, "no")
	mustEval(t, f, `=IF(A1=1,"one","other")`, "one")
	mustEval(t, f, `=IF(A1=2,"one","other")`, "other")
	// The false branch is optional.
	mustEval(t, f, `=IF(1,"only")`, "only")
}

func TestIFRequiresAtLeastOneArgument(t *testing.T) {
	f := grid(t)
	mustRejectWith(t, f, "=IF()", "IF requires at least 1 argument")
}

func TestIFERRORSubstitutesOnlyWhenTheFirstArgumentFails(t *testing.T) {
	f := grid(t)
	mustEval(t, f, `=IFERROR(1/0,"fallback")`, "fallback")
	mustEval(t, f, `=IFERROR(A1,"fallback")`, "1")
	mustEval(t, f, `=IFERROR(2+2,"fallback")`, "4")
}

func TestIFERRORRequiresExactlyTwoArguments(t *testing.T) {
	f := grid(t)
	mustRejectWith(t, f, "=IFERROR(1)", "IFERROR requires 2 arguments")
	mustRejectWith(t, f, "=IFERROR()", "IFERROR requires 2 arguments")
}

func TestNOTInvertsOnlyAQuotedBoolean(t *testing.T) {
	f := grid(t)
	// Quirk of this checkpoint: although the diagnostic offers "1 boolean or
	// numeric argument", a numeric operand is in fact refused. Only the quoted
	// boolean words reach the function as something it will invert -- the same
	// tokenizer limitation that AND and OR exhibit at milestone 07.
	mustEval(t, f, `=NOT("TRUE")`, "FALSE")
	mustEval(t, f, `=NOT("FALSE")`, "TRUE")
	mustReject(t, f, "=NOT(1)")
	mustReject(t, f, "=NOT(0)")
	mustReject(t, f, "=NOT()")
}

func TestTRUEAndFALSETakeNoArguments(t *testing.T) {
	f := grid(t)
	mustEval(t, f, "=TRUE()", "TRUE")
	mustEval(t, f, "=FALSE()", "FALSE")
	mustReject(t, f, "=TRUE(1)")
	mustReject(t, f, "=FALSE(1)")
}

func TestCHOOSEPicksByIndex(t *testing.T) {
	f := grid(t)
	mustEval(t, f, `=CHOOSE(1,"a","b","c")`, "a")
	mustEval(t, f, `=CHOOSE(2,"a","b","c")`, "b")
	mustEval(t, f, `=CHOOSE(3,"a","b","c")`, "c")
	// An index past the end of the value list is refused, not silently clamped.
	mustReject(t, f, `=CHOOSE(9,"a")`)
	// One argument is an index with nothing to choose from.
	mustReject(t, f, "=CHOOSE(1)")
}

func TestVLOOKUPSearchesTheFirstColumn(t *testing.T) {
	f := grid(t)
	mustEval(t, f, "=VLOOKUP(3,A1:C4,3,FALSE)", "30")
	mustEval(t, f, "=VLOOKUP(1,A1:C4,2,FALSE)", "apple")
	// The range-lookup flag is optional.
	mustEval(t, f, "=VLOOKUP(3,A1:C4,3)", "30")
}

func TestVLOOKUPReportsAMissAsAnError(t *testing.T) {
	f := grid(t)
	// A miss is an error rather than an #N/A value handed back as a result,
	// and it is a different error from the arity complaint below.
	mustRejectWith(t, f, "=VLOOKUP(99,A1:C4,3,FALSE)", "VLOOKUP no result found")
	// Fewer than three arguments is an arity error.
	mustRejectWith(t, f, "=VLOOKUP(3,A1:C4)", "VLOOKUP requires at least 3 arguments")
}

func TestHLOOKUPSearchesTheFirstRow(t *testing.T) {
	f := grid(t)
	// Row 1 of A1:C4 is [1, "apple", 10]; row 2 of that block is [2, ...].
	mustEval(t, f, "=HLOOKUP(1,A1:C4,2,FALSE)", "2")
	mustReject(t, f, "=HLOOKUP(99,A1:C4,2,FALSE)")
	mustReject(t, f, "=HLOOKUP(1,A1:C4)")
}

func TestLOOKUPReadsAResultVectorOrTheSearchedOne(t *testing.T) {
	f := grid(t)
	// With a result vector, the match's position selects from that vector.
	mustEval(t, f, "=LOOKUP(3,A1:A4,C1:C4)", "30")
	mustEval(t, f, "=LOOKUP(1,A1:A4,C1:C4)", "10")
	// Without one, the searched vector is also the result.
	mustEval(t, f, "=LOOKUP(3,A1:A4)", "3")
}

func TestArgumentsAreExpressionsAndNotEqualComparesNumbers(t *testing.T) {
	f := grid(t)
	// A nested call is evaluated before the outer function receives its value.
	mustEval(t, f, `=UPPER(LEFT("abc",2))`, "AB")
	mustEval(t, f, `=LEN(CONCAT("ab","cd"))`, "4")
	mustEval(t, f, `=SUM(LEN("abc"),LEN("de"))`, "5")
	// A reference and an arithmetic expression are arguments too.
	mustEval(t, f, "=IF(A1<A2,C1,C2)", "10")
	// <> is the not-equal comparison over numeric operands. Milestone 07 had no
	// such operator and read =1<>2 as something else entirely.
	mustEval(t, f, "=1<>2", "TRUE")
	mustEval(t, f, "=2<>2", "FALSE")
	mustEval(t, f, `=IF(1<>2,"y","n")`, "y")
	mustEval(t, f, `=IF(2<>2,"y","n")`, "n")
	// Two text operands are not comparable with it.
	mustReject(t, f, `="a"<>"b"`)
}
