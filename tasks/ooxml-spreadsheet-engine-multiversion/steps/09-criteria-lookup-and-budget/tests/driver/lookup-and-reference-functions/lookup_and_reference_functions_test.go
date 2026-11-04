// Scored group: lookup-and-reference-functions.
//
// The lookup, reference and information functions this milestone registers:
// INDEX, MATCH, XLOOKUP, ADDRESS, TRANSPOSE, SHEETS, ISFORMULA, ISLOGICAL and
// ISREF. Behaviour transcribed from the upstream checkpoint this milestone
// tracks, qax-os/excelize tag v2.5.0 (calc_test.go), restricted to the public
// interface. The functions are OpenDocument v1.3 part 4 (OpenFormula) sections
// 6.14.6 (INDEX), 6.14.9 (MATCH), 6.14.2 (ADDRESS), 6.5.6 (TRANSPOSE, which the
// spec files under Matrix Functions rather than Lookup), 6.13.18 (ISFORMULA),
// 6.13.19 (ISLOGICAL), 6.13.24 (ISREF) and 6.13.32 (SHEETS). XLOOKUP has no
// OpenFormula definition and is graded from TEST_INTERFACE.md 4.09.2 alone.
package lookupandreferencefunctions

import (
	"bytes"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// grid builds the workbook every test in this group reads:
//
//	     A     B      C      D
//	1    1    10   text      3
//	2    2    20             2
//	3    3    30             1
//
// with Z1 holding a formula, so that a formula cell can be told from a
// literal one.
func grid(t *testing.T) *seedcalc.File {
	t.Helper()
	f := seedcalc.NewFile()
	for axis, v := range map[string]int{
		"A1": 1, "A2": 2, "A3": 3,
		"B1": 10, "B2": 20, "B3": 30,
		"D1": 3, "D2": 2, "D3": 1,
	} {
		if err := f.SetCellValue("Sheet1", axis, v); err != nil {
			t.Fatalf("SetCellValue(%s): %v", axis, err)
		}
	}
	if err := f.SetCellValue("Sheet1", "C1", "text"); err != nil {
		t.Fatalf("SetCellValue(C1): %v", err)
	}
	if err := f.SetCellFormula("Sheet1", "Z1", "=1+1"); err != nil {
		t.Fatalf("SetCellFormula(Z1): %v", err)
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

func TestINDEXNeedsBothCoordinatesToNameOneCell(t *testing.T) {
	f := grid(t)
	mustEval(t, f, "=INDEX(A1:A3,2,1)", "2")
	mustEval(t, f, "=INDEX(A1:B3,2,2)", "20")
	mustEval(t, f, "=INDEX(A1:B3,3,1)", "3")
	// Omitting the column argument selects a whole row rather than a cell.
	// The result is a matrix, and a matrix rendered into a single cell is the
	// empty string, so the two-argument form yields nothing visible.
	mustEval(t, f, "=INDEX(A1:A3,2)", "")
	// Row zero means "every row", which is a matrix for the same reason.
	mustEval(t, f, "=INDEX(A1:A3,0,1)", "")
	// Out-of-range coordinates are reported separately for each axis.
	mustReject(t, f, "=INDEX(A1:A3,9)")
	mustReject(t, f, "=INDEX(A1:A3,1,9)")
	mustReject(t, f, "=INDEX(A1:A3)")
}

func TestMATCHReturnsAPositionWithinAOneColumnRange(t *testing.T) {
	f := grid(t)
	// The default match type is 1: the position of the largest value that is
	// not greater than the sought one, assuming ascending order.
	mustEval(t, f, "=MATCH(2,A1:A3)", "2")
	mustEval(t, f, "=MATCH(2.5,A1:A3)", "2")
	mustEval(t, f, "=MATCH(3,A1:A3)", "3")
	// Match type 0 is exact only.
	mustEval(t, f, "=MATCH(2,A1:A3,0)", "2")
	mustEval(t, f, "=MATCH(\"text\",C1:C3,0)", "1")
	mustReject(t, f, "=MATCH(2.5,A1:A3,0)")
	// Match type -1 expects descending order, so it finds nothing in an
	// ascending range but works on D1:D3.
	mustReject(t, f, "=MATCH(2,A1:A3,-1)")
	mustEval(t, f, "=MATCH(2,D1:D3,-1)", "2")
	mustEval(t, f, "=MATCH(2.5,D1:D3,-1)", "1")
	// A match type that is neither 0 nor -1 is silently taken as 1.
	mustEval(t, f, "=MATCH(2,A1:A3,7)", "2")
	mustReject(t, f, "=MATCH(2)")
}

func TestMATCHOnlySearchesASingleColumn(t *testing.T) {
	f := grid(t)
	// The same search over one column succeeds, so the refusals below are the
	// shape check doing its work and not MATCH being absent -- an engine that
	// has never heard of MATCH fails this first line.
	mustEval(t, f, "=MATCH(2,A1:A3,0)", "2")
	// The lookup array must be one column wide; a wider range and a scalar are
	// both refused. Only the refusal is graded -- 4.09 says the wording of a
	// diagnostic is not part of the contract, so the message is not pinned.
	mustReject(t, f, "=MATCH(2,A1:B3,0)")
	mustReject(t, f, "=MATCH(2,A1,0)")
	mustReject(t, f, "=MATCH(2,7,0)")
}

func TestXLOOKUPSearchesOneVectorAndAnswersFromAnother(t *testing.T) {
	f := grid(t)
	mustEval(t, f, "=XLOOKUP(2,A1:A3,B1:B3)", "20")
	mustEval(t, f, "=XLOOKUP(3,A1:A3,B1:B3)", "30")
	// The default match is exact, so a value that is not present is #N/A
	// unless a fourth argument supplies a replacement.
	mustReject(t, f, "=XLOOKUP(99,A1:A3,B1:B3)")
	mustEval(t, f, "=XLOOKUP(99,A1:A3,B1:B3,\"none\")", "none")
	// The fifth argument is the match mode. Mode 1 takes the next larger
	// entry; mode -1 is served by the descending search, so an ascending
	// vector with no exact match answers from the first entry of the result
	// vector -- 4.09.2's Oracle quirk, and the graded value.
	mustEval(t, f, "=XLOOKUP(2.5,A1:A3,B1:B3,\"none\",-1)", "10")
	mustEval(t, f, "=XLOOKUP(2.5,A1:A3,B1:B3,\"none\",1)", "30")
	// The sixth is the search mode; reverse search finds the same single hit.
	mustEval(t, f, "=XLOOKUP(2,A1:A3,B1:B3,\"none\",0,-1)", "20")
	mustReject(t, f, "=XLOOKUP(2,A1:A3,B1:B3,\"none\",3)")
	mustReject(t, f, "=XLOOKUP(2,A1:A3)")
}

func TestADDRESSBuildsItsReferenceFromTheColumnArgumentAlone(t *testing.T) {
	f := grid(t)
	// The row argument is range checked and then discarded: the column number
	// supplies both halves of the address. ADDRESS(2,3) is therefore $C$3 and
	// not the $C$2 the argument order suggests.
	mustEval(t, f, "=ADDRESS(2,3)", "$C$3")
	mustEval(t, f, "=ADDRESS(99,3)", "$C$3")
	// A row argument of zero is accepted for the same reason; only the upper
	// bound is enforced, and it excludes the last row of the grid.
	mustEval(t, f, "=ADDRESS(0,3)", "$C$3")
	mustReject(t, f, "=ADDRESS(1048576,3)")
	mustReject(t, f, "=ADDRESS(2)")
	mustReject(t, f, "=ADDRESS(2,3,1,TRUE,\"Sheet1\",9)")
}

func TestTheADDRESSAbsoluteModeAndReferenceStyle(t *testing.T) {
	f := grid(t)
	// The third argument selects which halves carry a dollar sign.
	mustEval(t, f, "=ADDRESS(2,3,1)", "$C$3")
	mustEval(t, f, "=ADDRESS(2,3,2)", "C$3")
	mustEval(t, f, "=ADDRESS(2,3,3)", "$C3")
	mustEval(t, f, "=ADDRESS(2,3,4)", "C3")
	mustReject(t, f, "=ADDRESS(2,3,5)")
	mustReject(t, f, "=ADDRESS(2,3,0)")
	// A false fourth argument switches to R1C1 style, where the absolute
	// mode decides whether the offsets are bracketed.
	mustEval(t, f, "=ADDRESS(2,3,1,FALSE)", "R3C3")
	mustEval(t, f, "=ADDRESS(2,3,4,FALSE)", "R[3]C[3]")
	// A fifth argument prefixes the sheet name.
	mustEval(t, f, "=ADDRESS(2,3,1,TRUE,\"Sheet1\")", "Sheet1!$C$3")
}

func TestTRANSPOSEIsOnlyUsefulInsideAnAggregate(t *testing.T) {
	f := grid(t)
	// Like the matrix form of INDEX, a transposed range has no single-cell
	// rendering, but it is still a usable argument to a function that
	// consumes a range.
	mustEval(t, f, "=TRANSPOSE(A1:A3)", "")
	mustEval(t, f, "=SUM(TRANSPOSE(A1:A3))", "6")
	mustEval(t, f, "=COUNT(TRANSPOSE(A1:B3))", "6")
	mustEval(t, f, "=MAX(TRANSPOSE(B1:B3))", "30")
	mustReject(t, f, "=TRANSPOSE(A1:A3,1)")
}

func TestSHEETSCountsWorksheets(t *testing.T) {
	f := grid(t)
	// With no argument, the number of sheets in the workbook.
	mustEval(t, f, "=SHEETS()", "1")
	// With a reference, the number of distinct sheets that reference spans.
	mustEval(t, f, "=SHEETS(A1:A3)", "1")
	mustEval(t, f, "=SHEETS(Sheet1!A1)", "1")
	// A value that is not a reference cannot span any sheet.
	mustReject(t, f, "=SHEETS(\"A1\")")
	mustReject(t, f, "=SHEETS(A1:A3,B1)")
	f.NewSheet("Second")
	mustEval(t, f, "=SHEETS()", "2")
	mustEval(t, f, "=SHEETS(A1:A3)", "1")
}

func TestISREFDistinguishesAReferenceFromAValue(t *testing.T) {
	f := grid(t)
	mustEval(t, f, "=ISREF(A1)", "TRUE")
	mustEval(t, f, "=ISREF(A1:A3)", "TRUE")
	mustEval(t, f, "=ISREF(Y9)", "TRUE")
	// A cell name in quotes is text, not a reference.
	mustEval(t, f, "=ISREF(\"A1\")", "FALSE")
	mustEval(t, f, "=ISREF(1)", "FALSE")
	mustReject(t, f, "=ISREF()")
}

func TestISFORMULAAndISLOGICALInspectTheOperand(t *testing.T) {
	f := grid(t)
	// ISFORMULA asks whether the referenced cell stores a formula, not
	// whether it holds a computed value.
	mustEval(t, f, "=ISFORMULA(Z1)", "TRUE")
	mustEval(t, f, "=ISFORMULA(A1)", "FALSE")
	mustEval(t, f, "=ISFORMULA(Y9)", "FALSE")
	mustEval(t, f, "=ISFORMULA(\"A1\")", "FALSE")
	mustReject(t, f, "=ISFORMULA(A1,1)")
	// ISLOGICAL tests the operand's own type. A number is not a boolean even
	// where a boolean would coerce to it.
	mustEval(t, f, "=ISLOGICAL(TRUE)", "TRUE")
	mustEval(t, f, "=ISLOGICAL(FALSE)", "TRUE")
	mustEval(t, f, "=ISLOGICAL(1)", "FALSE")
	mustEval(t, f, "=ISLOGICAL(C1)", "FALSE")
	mustReject(t, f, "=ISLOGICAL()")
}

func TestLookupResultsSurviveAWriteAndReopen(t *testing.T) {
	f := grid(t)
	for axis, formula := range map[string]string{
		"H1": "=INDEX(A1:B3,2,2)",
		"H2": "=MATCH(3,A1:A3,0)",
		"H3": "=XLOOKUP(1,A1:A3,B1:B3)",
		"H4": "=ADDRESS(2,3)",
	} {
		if err := f.SetCellFormula("Sheet1", axis, formula); err != nil {
			t.Fatalf("SetCellFormula(%s): %v", axis, err)
		}
	}
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	g, err := seedcalc.OpenReader(bytes.NewReader(buf.Bytes()))
	if err != nil {
		t.Fatalf("OpenReader: %v", err)
	}
	for axis, want := range map[string]string{
		"H1": "20", "H2": "3", "H3": "10", "H4": "$C$3",
	} {
		got, err := g.CalcCellValue("Sheet1", axis)
		if err != nil {
			t.Fatalf("CalcCellValue(%s) after round trip: %v", axis, err)
		}
		if got != want {
			t.Fatalf("CalcCellValue(%s) after round trip = %q, want %q", axis, got, want)
		}
	}
}
