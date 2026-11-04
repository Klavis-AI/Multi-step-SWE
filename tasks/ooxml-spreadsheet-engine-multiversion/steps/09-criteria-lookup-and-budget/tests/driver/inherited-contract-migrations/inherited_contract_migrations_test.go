// Scored group: inherited-contract-migrations.
//
// Six of the eight contracts that earlier milestones established and that this
// one deliberately moves. TEST_INTERFACE.md 4.09.17 states all eight; the two
// that are not here are scored where their own capability is scored -- RATE's
// registration in financial-function-additions, and the shared formula that
// now populates its range in shared-formulas -- so no moving contract is
// ungraded. They are scored, not gated, for the reason the two halves
// of the reward exist at all: a gate group asserts behaviour a candidate is
// obliged to preserve, and none of these is preserved here. Asserting a moved
// contract in the gate would demand that the workspace this step starts from --
// the previous milestone's own output -- already behave like this milestone,
// which nothing can satisfy.
//
// What moves:
//
//   - a numeric formula result is re-rendered to fifteen significant digits
//   - the coordinate helpers accept a reference marked absolute with $
//   - DataValidation.SetRange takes its bounds as empty interfaces
//   - the XML declaration on every part drops its standalone pseudo-attribute
//   - worksheet lookup by name folds case
//   - SHEET takes an optional argument
//
// The surrounding behaviour each one sits in stays where it was, in the gate
// group that owns it: this group asserts the delta and nothing else.
//
// The XML declaration is the one the W3C XML 1.0 specification section 2.8
// defines, carried into OPC by ECMA-376 Part 2 section 10.1.1; the validation
// rule and its bounds are ECMA-376 Part 1 sections 18.3.1.32
// "dataValidation (Data Validation)" and 18.3.1.44 "formula1 (Formula 1)";
// SHEET is OpenDocument v1.3 part 4 (OpenFormula) section 6.13.31.
package inheritedcontractmigrations

import (
	"archive/zip"
	"bytes"
	"io"
	"strings"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// ---------------------------------------------------------------- helpers ---

// ramp is the fixture the statistical cases read: A1:A5 holding 1 through 5.
func ramp(t *testing.T) *seedcalc.File {
	t.Helper()
	f := seedcalc.NewFile()
	for i, axis := range []string{"A1", "A2", "A3", "A4", "A5"} {
		if err := f.SetCellValue("Sheet1", axis, i+1); err != nil {
			t.Fatalf("SetCellValue(%s): %v", axis, err)
		}
	}
	return f
}

func evalAt(t *testing.T, f *seedcalc.File, sheetName, axis, formula string) (string, error) {
	t.Helper()
	if err := f.SetCellFormula(sheetName, axis, formula); err != nil {
		t.Fatalf("SetCellFormula(%q): %v", formula, err)
	}
	return f.CalcCellValue(sheetName, axis)
}

func mustEvalAt(t *testing.T, f *seedcalc.File, sheetName, axis, formula, want string) {
	t.Helper()
	got, err := evalAt(t, f, sheetName, axis, formula)
	if err != nil {
		t.Fatalf("CalcCellValue(%q): unexpected error %v", formula, err)
	}
	if got != want {
		t.Fatalf("CalcCellValue(%q) = %q, want %q", formula, got, want)
	}
}

func mustEval(t *testing.T, f *seedcalc.File, formula, want string) {
	t.Helper()
	mustEvalAt(t, f, "Sheet1", "Z1", formula, want)
}

func mustReject(t *testing.T, f *seedcalc.File, formula string) {
	t.Helper()
	got, err := evalAt(t, f, "Sheet1", "Z1", formula)
	if err == nil {
		t.Fatalf("CalcCellValue(%q) = %q, want an error", formula, got)
	}
	if got != "" {
		t.Fatalf("CalcCellValue(%q) returned result %q alongside an error", formula, got)
	}
}

// pack writes f and returns a reader over the package.
func pack(t *testing.T, f *seedcalc.File) *zip.Reader {
	t.Helper()
	var buf bytes.Buffer
	if err := f.Write(&buf); err != nil {
		t.Fatalf("Write: %v", err)
	}
	if buf.Len() == 0 {
		t.Fatal("Write produced an empty package")
	}
	zr, err := zip.NewReader(bytes.NewReader(buf.Bytes()), int64(buf.Len()))
	if err != nil {
		t.Fatalf("written package is not a readable ZIP container: %v", err)
	}
	return zr
}

// readPart returns the bytes of a named part.
func readPart(t *testing.T, zr *zip.Reader, name string) []byte {
	t.Helper()
	for _, zf := range zr.File {
		if zf.Name != name {
			continue
		}
		rc, err := zf.Open()
		if err != nil {
			t.Fatalf("open %s: %v", name, err)
		}
		defer rc.Close()
		b, err := io.ReadAll(rc)
		if err != nil {
			t.Fatalf("read %s: %v", name, err)
		}
		return b
	}
	t.Fatalf("written package has no part %s", name)
	return nil
}

// populated returns a workbook with a second sheet, some values and a formula.
func populated(t *testing.T) *seedcalc.File {
	t.Helper()
	f := seedcalc.NewFile()
	f.NewSheet("Second")
	if err := f.SetCellValue("Sheet1", "A1", 1); err != nil {
		t.Fatalf("SetCellValue: %v", err)
	}
	if err := f.SetCellValue("Sheet1", "A2", "text"); err != nil {
		t.Fatalf("SetCellValue: %v", err)
	}
	if err := f.SetCellFormula("Sheet1", "A3", "=SUM(A1,1)"); err != nil {
		t.Fatalf("SetCellFormula: %v", err)
	}
	return f
}

// sheetPart returns the first worksheet part of a written workbook.
func sheetPart(t *testing.T, f *seedcalc.File) string {
	t.Helper()
	return string(readPart(t, pack(t, f), "xl/worksheets/sheet1.xml"))
}

// ------------------------------------------------------------------ cases ---

func TestNumericResultsAreNowRenderedToFifteenSignificantDigits(t *testing.T) {
	// A result whose shortest exact rendering needs more than fifteen places
	// after the decimal point is re-rendered to fifteen significant digits,
	// with trailing zeros dropped.
	f := ramp(t)
	mustEval(t, f, "=SQRT(2)", "1.4142135623731")
	mustEval(t, f, "=10/3", "3.33333333333333")
	// Fifteen places or fewer is left exactly as it was.
	mustEval(t, f, "=PI()", "3.141592653589793")
	mustEval(t, f, "=EXP(1)", "2.718281828459045")
	mustEval(t, f, "=100/7", "14.285714285714286")
	// Nothing changes for the results that were already short.
	mustEval(t, f, "=0.1+0.2", "0.3")
	mustEval(t, f, "=1.1*1.1", "1.21")
	mustEval(t, f, "=1/3*3", "1")
	// The rounding reaches the function library, not just the arithmetic. The
	// kurtosis of A1:A5 is exactly -1.2, and the previous milestone printed the
	// accumulated binary error after it; the rounding absorbs that tail.
	mustEval(t, f, "=KURT(A1:A5)", "-1.2")
	mustEval(t, f, "=HARMEAN(1,2,4)", "1.71428571428571")
	// Fifteen *significant* digits, not fifteen decimal places: a result below
	// one keeps a digit the previous milestone dropped.
	mustEval(t, f, "=EFFECT(0.05,4)", "0.0509453369140624")
	mustEval(t, f, "=NOMINAL(0.05,4)", "0.0490889377161574")
	// A result the evaluator renders in exponent form is exempt: the trigger
	// counts digits after a decimal point and does not read such a string as
	// numeric at all, so it comes back with its full mantissa.
	mustEval(t, f, "=2^70", "1.1805916207174113e+21")
	mustEval(t, f, "=100000000/7", "1.4285714285714285e+07")
}

func TestTheCoordinateHelpersNowAcceptAnAbsoluteReference(t *testing.T) {
	// A dollar sign marking a reference absolute is stripped and the rest of
	// the name parsed, where the previous checkpoint rejected the whole name.
	for _, name := range []string{"$A$1", "$A1", "A$1"} {
		col, row, err := seedcalc.SplitCellName(name)
		if err != nil {
			t.Fatalf("SplitCellName(%q): %v", name, err)
		}
		if col != "A" || row != 1 {
			t.Fatalf("SplitCellName(%q) = (%q, %d), want (A, 1)", name, col, row)
		}
		c, r, err := seedcalc.CellNameToCoordinates(name)
		if err != nil {
			t.Fatalf("CellNameToCoordinates(%q): %v", name, err)
		}
		if c != 1 || r != 1 {
			t.Fatalf("CellNameToCoordinates(%q) = (%d, %d), want (1, 1)", name, c, r)
		}
	}
	// The whole public surface goes through those helpers, so an absolute
	// reference now addresses a cell.
	f := seedcalc.NewFile()
	if err := f.SetCellValue("Sheet1", "$B$2", 7); err != nil {
		t.Fatalf("SetCellValue($B$2): %v", err)
	}
	got, err := f.GetCellValue("Sheet1", "B2")
	if err != nil {
		t.Fatalf("GetCellValue(B2): %v", err)
	}
	if got != "7" {
		t.Fatalf("GetCellValue(B2) = %q, want %q after writing to $B$2", got, "7")
	}
	// A name that is malformed for any other reason is still rejected.
	for _, bad := range []string{"", "A", "1", "A0", "1A", "$", "A1$"} {
		if _, _, err := seedcalc.SplitCellName(bad); err == nil {
			t.Fatalf("SplitCellName(%q) returned no error", bad)
		}
	}
}

func TestTheValidationRangeNowTakesThreeOperandTypes(t *testing.T) {
	// SetRange used to take a pair of numbers of one fixed Go type. It now
	// takes a pair of empty interfaces and dispatches on what is in them, so
	// the same rule can be expressed as whole numbers, as decimals, or as
	// formula text.
	whole := seedcalc.NewDataValidation(true)
	if err := whole.SetRange(10, 20, seedcalc.DataValidationTypeWhole, seedcalc.DataValidationOperatorBetween); err != nil {
		t.Fatalf("SetRange with a pair of ints: %v", err)
	}
	// An int is rendered exactly, with no fractional part appended.
	if whole.Formula1 != "<formula1>10</formula1>" || whole.Formula2 != "<formula2>20</formula2>" {
		t.Fatalf("SetRange(10, 20) left Formula1 = %q, Formula2 = %q", whole.Formula1, whole.Formula2)
	}
	if whole.Type != "whole" || whole.Operator != "between" {
		t.Fatalf("SetRange(10, 20) left Type = %q, Operator = %q, want whole/between", whole.Type, whole.Operator)
	}

	dec := seedcalc.NewDataValidation(true)
	if err := dec.SetRange(1.5, 2.5, seedcalc.DataValidationTypeDecimal, seedcalc.DataValidationOperatorBetween); err != nil {
		t.Fatalf("SetRange with a pair of float64s: %v", err)
	}
	if dec.Formula1 != "<formula1>1.5</formula1>" || dec.Formula2 != "<formula2>2.5</formula2>" {
		t.Fatalf("SetRange(1.5, 2.5) left Formula1 = %q, Formula2 = %q", dec.Formula1, dec.Formula2)
	}
	if dec.Type != "decimal" {
		t.Fatalf("SetRange with the decimal type left Type = %q", dec.Type)
	}

	// A string goes through verbatim, which is how a bound is expressed as a
	// reference rather than a literal.
	ref := seedcalc.NewDataValidation(true)
	if err := ref.SetRange("=A1", "=A2", seedcalc.DataValidationTypeWhole, seedcalc.DataValidationOperatorBetween); err != nil {
		t.Fatalf("SetRange with a pair of strings: %v", err)
	}
	if ref.Formula1 != "<formula1>=A1</formula1>" || ref.Formula2 != "<formula2>=A2</formula2>" {
		t.Fatalf("SetRange(=A1, =A2) left Formula1 = %q, Formula2 = %q", ref.Formula1, ref.Formula2)
	}

	// The two ends are dispatched independently, so they need not agree.
	mixed := seedcalc.NewDataValidation(true)
	if err := mixed.SetRange(10, "=A2", seedcalc.DataValidationTypeWhole, seedcalc.DataValidationOperatorBetween); err != nil {
		t.Fatalf("SetRange with one int and one string: %v", err)
	}
	if mixed.Formula1 != "<formula1>10</formula1>" || mixed.Formula2 != "<formula2>=A2</formula2>" {
		t.Fatalf("SetRange(10, =A2) left Formula1 = %q, Formula2 = %q", mixed.Formula1, mixed.Formula2)
	}

	// Those three are the whole of it: any other dynamic type is refused, and
	// a refused call changes nothing on the rule.
	for _, tc := range []struct {
		what   string
		f1, f2 interface{}
	}{
		{"a pair of int64s", int64(3), int64(4)},
		{"a pair of bools", true, false},
		{"a pair of nils", nil, nil},
	} {
		d := seedcalc.NewDataValidation(true)
		if err := d.SetRange(tc.f1, tc.f2, seedcalc.DataValidationTypeWhole, seedcalc.DataValidationOperatorBetween); err == nil {
			t.Fatalf("SetRange with %s returned no error", tc.what)
		}
		if d.Formula1 != "" || d.Formula2 != "" || d.Type != "" || d.Operator != "" {
			t.Fatalf("SetRange with %s was refused but still wrote to the rule: %q %q %q %q",
				tc.what, d.Formula1, d.Formula2, d.Type, d.Operator)
		}
	}

	// The rule still reaches the worksheet part the same way it always did.
	f := seedcalc.NewFile()
	whole.SetSqref("A1:B2")
	f.AddDataValidation("Sheet1", whole)
	part := sheetPart(t, f)
	// Each attribute is checked on its own. The order Go writes them in, and
	// which of them are emitted at their zero value, are marshalling details
	// that have moved between milestones without the rule itself changing;
	// 4.02.6 fixes the spellings and that is what is asserted here.
	if !strings.Contains(part, "<dataValidation ") {
		t.Fatalf("worksheet part carries no dataValidation element for the rule: %s", part)
	}
	for _, want := range []string{
		`allowBlank="true"`, `operator="between"`, `sqref="A1:B2"`, `type="whole"`,
		"<formula1>10</formula1>", "<formula2>20</formula2>",
	} {
		if !strings.Contains(part, want) {
			t.Fatalf("worksheet part does not contain %s: %s", want, part)
		}
	}
}

func TestTheXMLDeclarationNoLongerClaimsToBeStandalone(t *testing.T) {
	// Every part is still introduced by an XML declaration, but the standalone
	// pseudo-attribute is gone from it.
	zr := pack(t, populated(t))
	const want = `<?xml version="1.0" encoding="UTF-8"?>` + "\n"
	for _, name := range []string{
		"[Content_Types].xml",
		"_rels/.rels",
		"xl/workbook.xml",
		"xl/_rels/workbook.xml.rels",
		"xl/worksheets/sheet1.xml",
		"docProps/core.xml",
		"docProps/app.xml",
	} {
		b := readPart(t, zr, name)
		if !bytes.HasPrefix(b, []byte(want)) {
			head := b
			if len(head) > 64 {
				head = head[:64]
			}
			t.Fatalf("part %s begins %q, want the declaration %q", name, string(head), want)
		}
	}
	// The exported declaration is the one the parts are written with.
	if got := string(seedcalc.XMLHeaderByte); got != want {
		t.Fatalf("XMLHeaderByte = %q, want %q", got, want)
	}
}

func TestGetSheetIndexNowMatchesTheNameWithoutRegardToCase(t *testing.T) {
	// Worksheet lookup by name folds case, where the previous checkpoint
	// compared the name exactly.
	f := seedcalc.NewFile()
	f.NewSheet("Second")
	for _, name := range []string{"Sheet1", "sheet1", "SHEET1", "ShEeT1"} {
		if got := f.GetSheetIndex(name); got != 0 {
			t.Fatalf("GetSheetIndex(%q) = %d, want 0", name, got)
		}
	}
	for _, name := range []string{"Second", "second", "SECOND"} {
		if got := f.GetSheetIndex(name); got != 1 {
			t.Fatalf("GetSheetIndex(%q) = %d, want 1", name, got)
		}
	}
	// A name that matches no worksheet at all is still reported as absent.
	if got := f.GetSheetIndex("NoSuchSheet"); got != -1 {
		t.Fatalf("GetSheetIndex on an absent sheet = %d, want -1", got)
	}
	// The sheet list itself still reports the names as they were written.
	names := f.GetSheetList()
	if len(names) != 2 || names[0] != "Sheet1" || names[1] != "Second" {
		t.Fatalf("GetSheetList = %v, want [Sheet1 Second]", names)
	}
}

func TestSHEETNowAcceptsASheetName(t *testing.T) {
	f := ramp(t)
	f.NewSheet("Second")
	// The nullary form is unchanged: the sheet holding the formula.
	mustEvalAt(t, f, "Sheet1", "A10", "=SHEET()", "1")
	mustEvalAt(t, f, "Second", "A10", "=SHEET()", "2")
	// Named, it reports that sheet's one-based position wherever it is called.
	mustEval(t, f, `=SHEET("Sheet1")`, "1")
	mustEval(t, f, `=SHEET("Second")`, "2")
	mustEvalAt(t, f, "Second", "B10", `=SHEET("Sheet1")`, "1")
	// A reference argument names the sheet the formula is on.
	mustEval(t, f, "=SHEET(A1)", "1")
	// A name that is not a sheet is #N/A, which surfaces as an error.
	mustReject(t, f, `=SHEET("Nope")`)
}
