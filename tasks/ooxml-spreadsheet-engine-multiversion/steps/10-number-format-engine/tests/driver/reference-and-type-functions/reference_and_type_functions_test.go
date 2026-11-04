// Scored group: reference-and-type-functions.
//
// The four functions that treat a formula's surroundings as data: INDIRECT,
// which builds a reference out of text; FORMULATEXT, which reads a formula
// back as text; and TYPE and ERROR.TYPE, which classify a value and an error
// respectively by a numeric code.
//
// Semantics follow OpenDocument v1.3 part 4 (OpenFormula) sections 6.14.7
// (INDIRECT), 6.13.15 (FORMULA), 6.13.32 (TYPE) and 6.13.11 (ERROR.TYPE).
package referenceandtypefunctions

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

// sheet holds 1..5 down column A and a formula in Y1 for FORMULATEXT to read.
func sheet(t *testing.T) *seedcalc.File {
	t.Helper()
	f := seedcalc.NewFile()
	for i, axis := range []string{"A1", "A2", "A3", "A4", "A5"} {
		if err := f.SetCellValue("Sheet1", axis, i+1); err != nil {
			t.Fatalf("SetCellValue(%s): %v", axis, err)
		}
	}
	if err := f.SetCellFormula("Sheet1", "Y1", "=1+2"); err != nil {
		t.Fatalf("SetCellFormula(Y1): %v", err)
	}
	return f
}

func TestIndirectResolvesAnA1Reference(t *testing.T) {
	f := sheet(t)
	mustEval(t, f, `=INDIRECT("A3")`, "3")
	mustEval(t, f, `=INDIRECT("A"&"3")`, "3")
	// A range built the same way feeds an aggregate.
	mustEval(t, f, `=SUM(INDIRECT("A1:A5"))`, "15")
	mustEval(t, f, `=COUNT(INDIRECT("A1:A3"))`, "3")
}

func TestIndirectResolvesAnR1C1Reference(t *testing.T) {
	f := sheet(t)
	// A second argument of FALSE switches the notation.
	mustEval(t, f, `=INDIRECT("R3C1",FALSE)`, "3")
	mustEval(t, f, `=SUM(INDIRECT("R1C1:R5C1",FALSE))`, "15")
	// TRUE is the default and keeps A1 notation.
	mustEval(t, f, `=INDIRECT("A3",TRUE)`, "3")
}

func TestIndirectRejectsWhatItCannotResolve(t *testing.T) {
	f := sheet(t)
	mustRejectWith(t, f, `=INDIRECT("nosuch!!")`, "#REF!")
	mustRejectWith(t, f, `=INDIRECT("A3",FALSE)`, "#REF!")
	// A non-boolean second argument is a bad value rather than a bad
	// reference.
	mustRejectWith(t, f, `=INDIRECT("A1","x")`, "#VALUE!")
	mustRejectWith(t, f, `=INDIRECT()`, "INDIRECT requires 1 or 2 arguments")
	// And the working form still works in the same workbook.
	mustEval(t, f, `=INDIRECT("A3")`, "3")
}

func TestIndirectDoesNotCrossSheets(t *testing.T) {
	f := sheet(t)
	f.NewSheet("Second")
	// A sheet-qualified reference is not resolved, even to the sheet the
	// formula is already on.
	mustRejectWith(t, f, `=INDIRECT("Sheet1!A3")`, "#REF!")
	mustRejectWith(t, f, `=INDIRECT("Second!A1")`, "#REF!")
	mustEval(t, f, `=INDIRECT("A3")`, "3")
}

func TestFormulatextReadsAFormulaBack(t *testing.T) {
	f := sheet(t)
	// The text includes the leading equals sign as written.
	mustEval(t, f, `=FORMULATEXT(Y1)`, "=1+2")
	// A cell holding a value rather than a formula yields nothing.
	mustEval(t, f, `=FORMULATEXT(A1)`, "")
	// The argument must be a reference, not text naming one.
	mustRejectWith(t, f, `=FORMULATEXT("A1")`, "#VALUE!")
	mustRejectWith(t, f, `=FORMULATEXT()`, "FORMULATEXT requires 1 argument")
}

func TestTypeClassifiesByPowersOfTwo(t *testing.T) {
	f := sheet(t)
	mustEval(t, f, `=TYPE(1)`, "1")
	mustEval(t, f, `=TYPE("a")`, "2")
	mustEval(t, f, `=TYPE(TRUE)`, "4")
	mustEval(t, f, `=TYPE(NA())`, "16")
	mustEval(t, f, `=TYPE(A1:A5)`, "64")
	mustRejectWith(t, f, `=TYPE()`, "TYPE requires 1 argument")
}

func TestErrorTypeNumbersTheErrorValues(t *testing.T) {
	f := sheet(t)
	// The codes run in the order the error values are listed.
	mustEval(t, f, `=ERROR.TYPE(1/0)`, "2")
	mustEval(t, f, `=ERROR.TYPE(NA())`, "7")
	// A value that is not an error has no code, which is itself the
	// not-available error.
	mustRejectWith(t, f, `=ERROR.TYPE(1)`, "#N/A")
	mustRejectWith(t, f, `=ERROR.TYPE()`, "ERROR.TYPE requires 1 argument")
}
