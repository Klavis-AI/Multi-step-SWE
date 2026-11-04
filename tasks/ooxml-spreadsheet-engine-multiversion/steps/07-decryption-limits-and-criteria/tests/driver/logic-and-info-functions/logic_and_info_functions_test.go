// Scored group: logic-and-info-functions.
//
// The logical functions AND and OR and the IS-family information functions,
// reached through File.CalcCellValue. Semantics follow OpenDocument v1.3 part 4
// (OpenFormula) section 6.15 "Logical Functions" and section 6.13 "Information
// Functions", with the deviations this checkpoint actually exhibits pinned
// below rather than glossed over.
//
// Two properties of the evaluator shape every test here. First, every scalar
// operand reaching a function is carried as a string, so the IS-family
// functions can only distinguish "empty" from "not empty" -- they do not see
// the Go type behind a cell. Second, a formula error aborts evaluation at the
// point it arises, so ISERR and ISERROR can only be handed an error that is a
// *value*, which at this checkpoint means NA().
package logicandinfofunctions

import (
	"strings"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// sheet builds a workbook with A1=1, A2=2, A3=3, B1="x" and B2 set to the
// empty string. Z9 is never written and so is genuinely absent.
func sheet(t *testing.T) *seedcalc.File {
	t.Helper()
	f := seedcalc.NewFile()
	for _, c := range []struct {
		axis string
		v    interface{}
	}{
		{"A1", 1}, {"A2", 2}, {"A3", 3}, {"B1", "x"}, {"B2", ""},
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

func TestANDConjoinsItsArguments(t *testing.T) {
	f := sheet(t)
	// AND is a true conjunction: every argument must be non-zero.
	mustEval(t, f, "=AND(1,1)", "TRUE")
	mustEval(t, f, "=AND(1,0)", "FALSE")
	mustEval(t, f, "=AND(0,1)", "FALSE")
	mustEval(t, f, "=AND(0,0)", "FALSE")
	mustEval(t, f, "=AND(1)", "TRUE")
	mustEval(t, f, "=AND(1,1,1,1,1)", "TRUE")
}

func TestANDAndORReturnUpperCaseBooleanText(t *testing.T) {
	f := sheet(t)
	// The result is the Go boolean rendered in upper case, not "1"/"0" and not
	// a boolean-typed cell value.
	for _, formula := range []string{"=AND(1,1)", "=OR(1,1)"} {
		got, err := eval(t, f, formula)
		if err != nil {
			t.Fatalf("CalcCellValue(%q): %v", formula, err)
		}
		if got != "TRUE" {
			t.Fatalf("CalcCellValue(%q) = %q, want the literal text %q", formula, got, "TRUE")
		}
	}
	mustEval(t, f, "=AND(0,0)", "FALSE")
}

func TestANDAndORAcceptTheBooleanWordsAsText(t *testing.T) {
	f := sheet(t)
	// A quoted TRUE/FALSE is recognised by name. A bare TRUE is not a token the
	// tokenizer produces at this checkpoint, so it never reaches the function.
	mustEval(t, f, `=AND("TRUE","TRUE")`, "TRUE")
	mustEval(t, f, `=AND("TRUE","FALSE")`, "FALSE")
	mustEval(t, f, `=OR("TRUE","FALSE")`, "TRUE")
	mustEval(t, f, `=OR("FALSE","FALSE")`, "FALSE")
	mustReject(t, f, "=AND(TRUE,TRUE)")
}

func TestORKeepsOnlyTheLastNumericArgument(t *testing.T) {
	f := sheet(t)
	// Quirk of this checkpoint: for numeric arguments OR *assigns* rather than
	// accumulates, so the last numeric argument alone decides the result. This
	// is not the OpenFormula disjunction and must be reproduced exactly.
	mustEval(t, f, "=OR(1,0)", "FALSE")
	mustEval(t, f, "=OR(0,1)", "TRUE")
	mustEval(t, f, "=OR(1,1)", "TRUE")
	mustEval(t, f, "=OR(0,0)", "FALSE")
	mustEval(t, f, "=OR(1,1,1,0)", "FALSE")
}

func TestANDAndORRequireAtLeastOneArgument(t *testing.T) {
	f := sheet(t)
	mustRejectWith(t, f, "=AND()", "AND requires at least 1 argument")
	mustRejectWith(t, f, "=OR()", "OR requires at least 1 argument")
}

func TestISBLANKDistinguishesEmptyFromWritten(t *testing.T) {
	f := sheet(t)
	mustEval(t, f, "=ISBLANK(B2)", "TRUE")
	mustEval(t, f, "=ISBLANK(Z9)", "TRUE")
	mustEval(t, f, "=ISBLANK(A1)", "FALSE")
	mustEval(t, f, "=ISBLANK(B1)", "FALSE")
}

func TestISNUMBERTestsWhetherTheOperandParsesAsANumber(t *testing.T) {
	f := sheet(t)
	mustEval(t, f, "=ISNUMBER(A1)", "TRUE")
	mustEval(t, f, "=ISNUMBER(B1)", "FALSE")
	mustEval(t, f, "=ISNUMBER(Z9)", "FALSE")
}

func TestISNONTEXTAnswersWhetherTheOperandIsEmpty(t *testing.T) {
	f := sheet(t)
	// Quirk of this checkpoint: every scalar operand arrives as a string, so
	// ISNONTEXT reports TRUE only for an operand with no text at all. A numeric
	// cell is therefore reported as text, the opposite of the spreadsheet
	// function it is named after.
	mustEval(t, f, "=ISNONTEXT(A1)", "FALSE")
	mustEval(t, f, "=ISNONTEXT(B1)", "FALSE")
	mustEval(t, f, "=ISNONTEXT(Z9)", "TRUE")
}

func TestISEVENAndISODDClassifyIntegers(t *testing.T) {
	f := sheet(t)
	mustEval(t, f, "=ISEVEN(A2)", "TRUE")
	mustEval(t, f, "=ISEVEN(A1)", "FALSE")
	mustEval(t, f, "=ISODD(A1)", "TRUE")
	mustEval(t, f, "=ISODD(A2)", "FALSE")
	mustEval(t, f, "=ISEVEN(0)", "TRUE")
	// A non-numeric operand is the documented #VALUE! error, handed back as
	// an error rather than as FALSE or as a result.
	mustRejectWith(t, f, `=ISEVEN("x")`, "#VALUE!")
	mustRejectWith(t, f, `=ISODD("x")`, "#VALUE!")
}

func TestNAProducesTheNotAvailableErrorAsAValue(t *testing.T) {
	f := sheet(t)
	// NA() is the one error this evaluator hands back as an ordinary result:
	// the call succeeds and the result is the error text.
	got, err := eval(t, f, "=NA()")
	if err != nil {
		t.Fatalf("CalcCellValue(=NA()): unexpected error %v", err)
	}
	if got != "#N/A" {
		t.Fatalf("CalcCellValue(=NA()) = %q, want %q", got, "#N/A")
	}
}

func TestISNARecognisesOnlyTheNotAvailableError(t *testing.T) {
	f := sheet(t)
	mustEval(t, f, "=ISNA(NA())", "TRUE")
	mustEval(t, f, "=ISNA(1)", "FALSE")
	mustEval(t, f, "=ISNA(A1)", "FALSE")
	mustEval(t, f, `=ISNA("text")`, "FALSE")
}

func TestISERRExcludesNotAvailableAndISERRORIncludesIt(t *testing.T) {
	f := sheet(t)
	// This is the whole difference between the two functions: #N/A is an error
	// for ISERROR but deliberately not for ISERR.
	mustEval(t, f, "=ISERR(NA())", "FALSE")
	mustEval(t, f, "=ISERROR(NA())", "TRUE")
	mustEval(t, f, "=ISERR(1)", "FALSE")
	mustEval(t, f, "=ISERROR(1)", "FALSE")
}

func TestAnErrorRaisedMidExpressionAbortsEvaluation(t *testing.T) {
	f := sheet(t)
	// Both predicates answer an ordinary argument first, so the rejections
	// below are the engine refusing a raised error and not the engine failing
	// to find the function at all.
	mustEval(t, f, "=ISERR(1)", "FALSE")
	mustEval(t, f, "=ISERROR(1)", "FALSE")
	// A division by zero is raised, not returned, so it never reaches the
	// enclosing ISERR/ISERROR call: the whole formula is rejected, and the
	// message that comes back is the division's, not the predicate's.
	mustRejectWith(t, f, "=ISERR(1/0)", "#DIV/0!")
	mustRejectWith(t, f, "=ISERROR(1/0)", "#DIV/0!")
}

func TestTheInformationFunctionsTakeExactlyOneArgument(t *testing.T) {
	f := sheet(t)
	for _, name := range []string{"ISBLANK", "ISNUMBER", "ISNONTEXT", "ISNA", "ISERR", "ISERROR", "ISEVEN", "ISODD"} {
		mustRejectWith(t, f, "="+name+"()", name+" requires 1 argument")
		mustRejectWith(t, f, "="+name+"(1,2)", name+" requires 1 argument")
	}
}

func TestANDAndORAcceptAtMostThirtyArguments(t *testing.T) {
	// The documented ceiling. Thirty arguments evaluate; a thirty-first is
	// refused by name before any of them is looked at.
	f := sheet(t)
	args := func(n int) string {
		parts := make([]string, n)
		for i := range parts {
			parts[i] = "1"
		}
		return strings.Join(parts, ",")
	}
	for _, name := range []string{"AND", "OR"} {
		mustEval(t, f, "="+name+"("+args(30)+")", "TRUE")
		mustReject(t, f, "="+name+"("+args(31)+")")
	}
}
