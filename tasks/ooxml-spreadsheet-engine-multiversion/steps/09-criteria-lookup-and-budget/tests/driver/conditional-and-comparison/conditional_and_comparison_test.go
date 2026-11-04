// Scored group: conditional-and-comparison.
//
// Two related changes. The comparison and concatenation operators learn to
// handle text operands, which until this milestone made the whole formula
// invalid; and the conditional functions IFNA, IFS, SWITCH and XOR join the
// library. Behaviour transcribed from the upstream checkpoint this milestone
// tracks, qax-os/excelize tag v2.5.0 (calc_test.go), restricted to the public
// interface. The operators are OpenDocument v1.3 part 4 (OpenFormula) section
// 5.5 "Operators". Of the four functions only two are in that spec: IFNA is
// section 6.15.6 and XOR is section 6.15.10. IFS and SWITCH have no
// OpenFormula definition at all and are graded from TEST_INTERFACE.md 4.09.3
// alone.
package conditionalandcomparison

import (
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// grid gives every test A1:A3 = 1, 2, 3 and C1 = "text".
func grid(t *testing.T) *seedcalc.File {
	t.Helper()
	f := seedcalc.NewFile()
	for axis, v := range map[string]int{"A1": 1, "A2": 2, "A3": 3} {
		if err := f.SetCellValue("Sheet1", axis, v); err != nil {
			t.Fatalf("SetCellValue(%s): %v", axis, err)
		}
	}
	if err := f.SetCellValue("Sheet1", "C1", "text"); err != nil {
		t.Fatalf("SetCellValue(C1): %v", err)
	}
	return f
}

func mustEval(t *testing.T, f *seedcalc.File, formula, want string) {
	t.Helper()
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

func mustReject(t *testing.T, f *seedcalc.File, formula string) {
	t.Helper()
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

func TestTwoTextLiteralsCompareLexicographically(t *testing.T) {
	f := grid(t)
	// The ordering is a plain byte comparison of the two literals, so it is
	// case sensitive and every upper-case letter sorts before every
	// lower-case one.
	mustEval(t, f, `="a"<"b"`, "TRUE")
	mustEval(t, f, `="b"<"a"`, "FALSE")
	mustEval(t, f, `="a"<="a"`, "TRUE")
	mustEval(t, f, `="a">="a"`, "TRUE")
	mustEval(t, f, `="apple"<"banana"`, "TRUE")
	mustEval(t, f, `="A"<"a"`, "TRUE")
	mustEval(t, f, `="apple">"Apple"`, "TRUE")
	mustEval(t, f, `="a">"B"`, "TRUE")
	mustEval(t, f, `=""<"a"`, "TRUE")
	// The result is an ordinary boolean, usable wherever one is expected.
	mustEval(t, f, `=IF("a"<"b","yes","no")`, "yes")
	mustEval(t, f, `=IF("b"<"a","yes","no")`, "no")
	mustEval(t, f, `=NOT("a"<"b")`, "FALSE")
	mustEval(t, f, `=AND("a"<"b",1<2)`, "TRUE")
}

func TestEveryNumberSortsBeforeEveryPieceOfText(t *testing.T) {
	f := grid(t)
	// A mixed comparison never coerces. The numeric side is simply treated as
	// the smaller one, whatever the two values are.
	mustEval(t, f, `=1<"a"`, "TRUE")
	mustEval(t, f, `=1<="a"`, "TRUE")
	mustEval(t, f, `=1>"a"`, "FALSE")
	mustEval(t, f, `=1>="a"`, "FALSE")
	mustEval(t, f, `="a"<1`, "FALSE")
	mustEval(t, f, `="a"<=1`, "FALSE")
	mustEval(t, f, `="a">1`, "TRUE")
	mustEval(t, f, `="a">=1`, "TRUE")
	// Numeric comparisons are unchanged.
	mustEval(t, f, "=1<2", "TRUE")
	mustEval(t, f, "=2<=2", "TRUE")
	mustEval(t, f, "=3>2", "TRUE")
	mustEval(t, f, "=2>=3", "FALSE")
}

func TestOnlyALiteralCountsAsTextInAComparison(t *testing.T) {
	f := grid(t)
	// A cell reference is classified as a numeric operand before its contents
	// are looked at, so a text-valued cell takes the number side of a mixed
	// comparison rather than being compared as text. C1 holds "text", which
	// would sort after both of these literals if it were read as one.
	mustEval(t, f, `=C1<"u"`, "TRUE")
	mustEval(t, f, `=C1<"a"`, "TRUE")
	mustEval(t, f, `=C1>"a"`, "FALSE")
	mustEval(t, f, `=A1<"a"`, "TRUE")
}

func TestEqualityComparesTheOperandsAsTheyWereWritten(t *testing.T) {
	f := grid(t)
	mustEval(t, f, `="abc"="abc"`, "TRUE")
	mustEval(t, f, `="abc"="abd"`, "FALSE")
	mustEval(t, f, `="abc"<>"abd"`, "TRUE")
	// The two sides are matched as text, without a numeric reading, so a
	// number equals its own spelling but not a different spelling of itself.
	mustEval(t, f, `=1="1"`, "TRUE")
	mustEval(t, f, `=1<>"1"`, "FALSE")
	mustEval(t, f, "=1.0=1", "FALSE")
}

func TestConcatenationJoinsTextOperands(t *testing.T) {
	f := grid(t)
	mustEval(t, f, `="a"&"b"`, "ab")
	mustEval(t, f, `="a"&"b"&"c"`, "abc")
	mustEval(t, f, `="n="&1`, "n=1")
	mustEval(t, f, `=A1&"x"`, "1x")
	// Joining two numbers, which already worked, is unchanged.
	mustEval(t, f, "=1&2", "12")
}

func TestIFNASubstitutesOnlyForNotAvailable(t *testing.T) {
	f := grid(t)
	mustEval(t, f, `=IFNA(NA(),"x")`, "x")
	mustEval(t, f, `=IFNA(5,"x")`, "5")
	mustEval(t, f, `=IFNA("v","x")`, "v")
	mustEval(t, f, `=IFNA(MATCH(9,A1:A3,0),"missing")`, "missing")
	// Any other error propagates rather than being replaced.
	mustReject(t, f, `=IFNA(1/0,"x")`)
	// Exactly two arguments.
	mustReject(t, f, `=IFNA(NA())`)
	mustReject(t, f, `=IFNA(1,2,3)`)
}

func TestIFSReturnsTheFirstSatisfiedBranch(t *testing.T) {
	f := grid(t)
	mustEval(t, f, `=IFS(TRUE,"a")`, "a")
	mustEval(t, f, `=IFS(FALSE,"a",TRUE,"b")`, "b")
	mustEval(t, f, `=IFS(1>2,"a",2>1,"b",3>2,"c")`, "b")
	mustEval(t, f, `=IFS(A1>0,"pos",A1<0,"neg")`, "pos")
	// No satisfied condition is #N/A rather than an empty result.
	mustReject(t, f, `=IFS(FALSE,"a",FALSE,"b")`)
	// Evaluation stops at the first satisfied condition, so a trailing
	// argument after it is never examined -- not because a bare condition is
	// tolerated in general, but because scanning never reaches it.
	mustEval(t, f, `=IFS(TRUE,"a",FALSE)`, "a")
	mustReject(t, f, `=IFS(TRUE)`)
}

func TestSWITCHMatchesTheExpressionAgainstEachValue(t *testing.T) {
	f := grid(t)
	mustEval(t, f, `=SWITCH(2,1,"one",2,"two")`, "two")
	mustEval(t, f, `=SWITCH(2,1,"one",2,"two",3,"three")`, "two")
	mustEval(t, f, `=SWITCH("b","a",1,"b",2)`, "2")
	// An odd trailing argument is the default for an unmatched expression.
	mustEval(t, f, `=SWITCH(9,1,"one",2,"two","other")`, "other")
	mustEval(t, f, `=SWITCH(1,1,"one",2,"two","other")`, "one")
	// Without that default, no match is #N/A.
	mustReject(t, f, `=SWITCH(9,1,"one",2,"two")`)
	mustReject(t, f, `=SWITCH(1)`)
}

func TestXORIsTrueForAnOddNumberOfTruths(t *testing.T) {
	f := grid(t)
	mustEval(t, f, "=XOR(TRUE,FALSE)", "TRUE")
	mustEval(t, f, "=XOR(TRUE,TRUE)", "FALSE")
	mustEval(t, f, "=XOR(FALSE,FALSE)", "FALSE")
	mustEval(t, f, "=XOR(TRUE,TRUE,TRUE)", "TRUE")
	mustEval(t, f, "=XOR(TRUE)", "TRUE")
	// Numbers count as true when they are non-zero.
	mustEval(t, f, "=XOR(1,0)", "TRUE")
	mustEval(t, f, "=XOR(0,0,1,1)", "FALSE")
	// A range contributes each of its cells; A1:A3 is three non-zero values.
	mustEval(t, f, "=XOR(A1:A3)", "TRUE")
	// Text is not a truth value.
	mustReject(t, f, `=XOR("a")`)
	mustReject(t, f, "=XOR()")
}
