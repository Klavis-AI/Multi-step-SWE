// Gate group: milestone-08-functions-regression.
//
// The function library milestone 08 added -- the conditional and lookup
// family, the text family, the statistical and distribution family, the radix
// and complex-number engineering family, the financial family, the dotted
// function names and the reference functions -- checked against the far larger
// library that arrives here. Every one of them must behave exactly as milestone
// 08 left it. The three contracts in this family that v2.5.0 does move -- SHEET
// gaining an argument, and the fifteen-significant-digit rendering of KURT,
// HARMEAN, EFFECT and NOMINAL -- are scored in inherited-contract-migrations
// instead, so that nothing in this gate group asks the previous milestone's own
// output to behave like this one. Semantics follow OpenDocument v1.3 part 4
// (OpenFormula) sections 6.8 through 6.20.
package milestone08functionsregression

import (
	"math"
	"strconv"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// sheet builds the fixture every case in this group reads:
//
//	     A     B         C      D
//	1    1     apple     10
//	2    2     banana    20
//	3    3     cherry    30
//	4    4     date      40
//	5    5
//
// Column D is left blank on purpose, so COUNTBLANK has something to count.
func sheet(t *testing.T) *seedcalc.File {
	t.Helper()
	f := seedcalc.NewFile()
	for _, c := range []struct {
		axis string
		v    interface{}
	}{
		{"A1", 1}, {"A2", 2}, {"A3", 3}, {"A4", 4}, {"A5", 5},
		{"B1", "apple"}, {"B2", "banana"}, {"B3", "cherry"}, {"B4", "date"},
		{"C1", 10}, {"C2", 20}, {"C3", 30}, {"C4", 40},
	} {
		if err := f.SetCellValue("Sheet1", c.axis, c.v); err != nil {
			t.Fatalf("SetCellValue(%s): %v", c.axis, err)
		}
	}
	return f
}

// cashFlow builds A1:A5 = -100, 30, 40, 50, 60: the one outlay followed by four
// returns that IRR and MIRR need in order to have a root at all.
func cashFlow(t *testing.T) *seedcalc.File {
	t.Helper()
	f := seedcalc.NewFile()
	for axis, v := range map[string]int{"A1": -100, "A2": 30, "A3": 40, "A4": 50, "A5": 60} {
		if err := f.SetCellValue("Sheet1", axis, v); err != nil {
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

// mustEvalNear grades a numeric result on its value rather than on its last
// bits. The financial functions reach their answers through chains of pow, log
// and running summation whose association order no document fixes, so two
// implementations that both compute the documented quantity correctly can
// differ in the final unit in the last place -- and because milestone 09
// returns a result of fifteen or fewer decimal places exactly as produced
// (TEST_INTERFACE.md 4.09.17), that difference reaches the rendered string.
// This is a gate group, so a string comparison would veto the whole step over
// it. A relative tolerance of 1e-9 is many orders tighter than any real
// reformulation moves these answers and many orders looser than one ulp.
// Assertions that are genuinely exact -- a zero-rate annuity, a whole-number
// depreciation charge, a refusal -- are still compared as strings.
func mustEvalNear(t *testing.T, f *seedcalc.File, formula string, want float64) {
	t.Helper()
	got, err := evalAt(t, f, "Sheet1", "Z1", formula)
	if err != nil {
		t.Fatalf("CalcCellValue(%q): unexpected error %v", formula, err)
	}
	v, perr := strconv.ParseFloat(got, 64)
	if perr != nil {
		t.Fatalf("CalcCellValue(%q) = %q, want a number near %v", formula, got, want)
	}
	if math.Abs(v-want) > 1e-9*math.Abs(want) {
		t.Fatalf("CalcCellValue(%q) = %q, want %v to a relative 1e-9", formula, got, want)
	}
}

func TestTheConditionalFunctionsAreUnchanged(t *testing.T) {
	f := sheet(t)
	mustEval(t, f, `=IF(1,"yes","no")`, "yes")
	mustEval(t, f, `=IF(0,"yes","no")`, "no")
	mustEval(t, f, `=IF(A1=1,"one","other")`, "one")
	mustEval(t, f, `=IF(A1=2,"one","other")`, "other")
	mustEval(t, f, `=IF(1,"only")`, "only")
	mustEval(t, f, `=IFERROR(1/0,"fallback")`, "fallback")
	mustEval(t, f, `=IFERROR(A1,"fallback")`, "1")
	mustEval(t, f, `=IFERROR(2+2,"fallback")`, "4")
	// NOT still reads the bare boolean words out of a text argument.
	mustEval(t, f, `=NOT("TRUE")`, "FALSE")
	mustEval(t, f, `=NOT("FALSE")`, "TRUE")
	mustEval(t, f, "=TRUE()", "TRUE")
	mustEval(t, f, "=FALSE()", "FALSE")
}

func TestTheLookupFunctionsAreUnchanged(t *testing.T) {
	f := sheet(t)
	mustEval(t, f, `=CHOOSE(1,"a","b","c")`, "a")
	mustEval(t, f, `=CHOOSE(2,"a","b","c")`, "b")
	mustEval(t, f, `=CHOOSE(3,"a","b","c")`, "c")
	mustEval(t, f, "=VLOOKUP(3,A1:C4,3,FALSE)", "30")
	mustEval(t, f, "=VLOOKUP(1,A1:C4,2,FALSE)", "apple")
	// The range-lookup argument is still optional.
	mustEval(t, f, "=VLOOKUP(3,A1:C4,3)", "30")
	mustEval(t, f, "=HLOOKUP(1,A1:C4,2,FALSE)", "2")
	mustEval(t, f, "=LOOKUP(3,A1:A4,C1:C4)", "30")
	mustEval(t, f, "=LOOKUP(1,A1:A4,C1:C4)", "10")
	// With no result vector LOOKUP still returns the matched value itself.
	mustEval(t, f, "=LOOKUP(3,A1:A4)", "3")
}

func TestTheTextFunctionsAreUnchanged(t *testing.T) {
	f := sheet(t)
	mustEval(t, f, `=LEN("hello")`, "5")
	mustEval(t, f, "=LEN(B1)", "5")
	mustEval(t, f, `=LENB("hello")`, "5")
	mustEval(t, f, `=LEFT("abcdef",2)`, "ab")
	mustEval(t, f, `=RIGHT("abcdef",2)`, "ef")
	// The count argument still defaults to one.
	mustEval(t, f, `=LEFT("abcdef")`, "a")
	mustEval(t, f, `=RIGHT("abcdef")`, "f")
	mustEval(t, f, `=MID("abcdef",2,3)`, "bcd")
	mustEval(t, f, `=FIND("c","abcdef")`, "3")
	mustEval(t, f, `=REPLACE("abcdef",2,3,"XY")`, "aXYef")
	mustEval(t, f, `=SUBSTITUTE("aXbXc","X","-")`, "a-b-c")
	mustEval(t, f, `=REPT("ab",3)`, "ababab")
	mustEval(t, f, `=CONCAT("a","b","c")`, "abc")
	mustEval(t, f, "=CONCAT(A1,A2)", "12")
	mustEval(t, f, `=EXACT("abc","abc")`, "TRUE")
	mustEval(t, f, `=EXACT("abc","ABC")`, "FALSE")
}

func TestTheCharacterAndCoercionFunctionsAreUnchanged(t *testing.T) {
	f := sheet(t)
	mustEval(t, f, "=CHAR(65)", "A")
	mustEval(t, f, "=UNICHAR(65)", "A")
	mustEval(t, f, `=CODE("A")`, "65")
	mustEval(t, f, `=UNICODE("A")`, "65")
	// CODE still reports the first character only, and is still defined on the
	// empty string where the reference implementation raises #VALUE!.
	mustEval(t, f, `=CODE("abc")`, "97")
	mustEval(t, f, `=CODE("")`, "0")
	mustEval(t, f, "=FIXED(1234.567)", "1,234.567")
	mustEval(t, f, "=FIXED(1234.567,1)", "1,234.6")
	mustEval(t, f, "=FIXED(1234.567,1,TRUE)", "1234.6")
	mustEval(t, f, "=N(1)", "1")
	mustEval(t, f, `=N("abc")`, "0")
	mustEval(t, f, "=N(TRUE())", "1")
	mustEval(t, f, `=T("abc")`, "abc")
	mustEval(t, f, "=T(1)", "1")
	mustEval(t, f, `=ISTEXT("a")`, "TRUE")
	mustEval(t, f, "=ISTEXT(B1)", "TRUE")
	mustEval(t, f, "=ISTEXT(A1)", "FALSE")
	mustEval(t, f, `=ENCODEURL("a b&c")`, "a%20b%26c")
}

func TestTheStatisticalFunctionsAreUnchanged(t *testing.T) {
	f := sheet(t)
	mustEval(t, f, "=AVERAGE(A1:A5)", "3")
	mustEval(t, f, "=AVERAGE(1,2,3)", "2")
	mustEval(t, f, "=AVERAGEA(A1:A5)", "3")
	mustEval(t, f, "=COUNT(A1:A5)", "5")
	mustEval(t, f, "=COUNTBLANK(A1:A5)", "0")
	mustEval(t, f, "=COUNTBLANK(D1:D5)", "5")
	mustEval(t, f, "=MAX(A1:A5)", "5")
	mustEval(t, f, "=MIN(A1:A5)", "1")
	mustEval(t, f, "=MAXA(A1:A5)", "5")
	mustEval(t, f, "=MINA(A1:A5)", "1")
	mustEval(t, f, "=LARGE(A1:A5,1)", "5")
	mustEval(t, f, "=LARGE(A1:A5,5)", "1")
	mustEval(t, f, "=SMALL(A1:A5,1)", "1")
	mustEval(t, f, "=MEDIAN(A1:A5)", "3")
	mustEval(t, f, "=PERCENTILE(A1:A5,0.5)", "3")
	mustEval(t, f, "=QUARTILE(A1:A5,1)", "2")
}

func TestTheDispersionAndDistributionFunctionsAreUnchanged(t *testing.T) {
	f := sheet(t)
	mustEval(t, f, "=STDEV(A1:A5)", "1.58113883008419")
	mustEval(t, f, "=STDEVA(A1:A5)", "1.58113883008419")
	mustEval(t, f, "=VARP(A1:A5)", "2")
	mustEval(t, f, "=SKEW(A1:A5)", "0")
	// KURT and HARMEAN are the two values in this family whose rendering moves
	// at this checkpoint; they are scored in inherited-contract-migrations
	// rather than asserted here, because the workspace this step starts from
	// cannot produce the new form.
	mustEval(t, f, "=PERMUT(5,2)", "20")
	mustEval(t, f, "=PERMUTATIONA(5,2)", "25")
	mustEval(t, f, "=NORMDIST(1,0,1,TRUE)", "0.841344746068543")
	mustEval(t, f, "=NORMSDIST(1)", "0.841344746068543")
	mustEval(t, f, "=NORMINV(0.5,0,1)", "0")
	mustEval(t, f, "=NORMSINV(0.5)", "0")
	mustEval(t, f, "=POISSON(2,3,TRUE)", "0.423190081126844")
	mustEval(t, f, "=FISHER(0.5)", "0.549306144334055")
	mustEval(t, f, "=FISHERINV(0.5)", "0.46211715726001")
	mustEval(t, f, "=GAMMA(4)", "6")
	mustEval(t, f, "=GAMMALN(4)", "1.791759469228055")
}

func TestTheRadixAndBitwiseFunctionsAreUnchanged(t *testing.T) {
	f := sheet(t)
	mustEval(t, f, `=BIN2DEC("1010")`, "10")
	mustEval(t, f, `=BIN2OCT("1010")`, "12")
	mustEval(t, f, `=BIN2HEX("1010")`, "A")
	mustEval(t, f, `=BIN2HEX("1010",4)`, "000A")
	// Ten-bit binary is still read as two's complement.
	mustEval(t, f, `=BIN2DEC("1111111111")`, "-1")
	mustEval(t, f, "=DEC2BIN(10)", "1010")
	mustEval(t, f, "=DEC2HEX(255)", "FF")
	mustEval(t, f, "=DEC2OCT(8)", "10")
	mustEval(t, f, `=HEX2DEC("FF")`, "255")
	mustEval(t, f, `=HEX2BIN("A")`, "1010")
	mustEval(t, f, `=HEX2OCT("A")`, "12")
	mustEval(t, f, `=OCT2DEC("17")`, "15")
	mustEval(t, f, `=OCT2BIN("7")`, "111")
	mustEval(t, f, `=OCT2HEX("17")`, "F")
	mustEval(t, f, "=BITAND(12,10)", "8")
	mustEval(t, f, "=BITOR(12,10)", "14")
	mustEval(t, f, "=BITXOR(12,10)", "6")
	mustEval(t, f, "=BITLSHIFT(3,2)", "12")
	mustEval(t, f, "=BITRSHIFT(12,2)", "3")
}

func TestTheComplexNumberFunctionsAreUnchanged(t *testing.T) {
	f := sheet(t)
	mustEval(t, f, "=COMPLEX(3,4)", "3+4i")
	mustEval(t, f, `=COMPLEX(3,4,"j")`, "3+4j")
	mustEval(t, f, "=COMPLEX(3,0)", "3")
	mustEval(t, f, "=COMPLEX(0,1)", "1i")
	mustEval(t, f, `=IMABS("3+4i")`, "5")
	mustEval(t, f, `=IMREAL("3+4i")`, "3")
	mustEval(t, f, `=IMAGINARY("3+4i")`, "4")
	mustEval(t, f, `=IMCONJUGATE("3+4i")`, "3-4i")
	mustEval(t, f, `=IMARGUMENT("3+4i")`, "0.927295218001612")
	mustEval(t, f, `=IMSUM("1+2i","3+4i")`, "4+6i")
	mustEval(t, f, `=IMSUB("3+4i","1+2i")`, "2+2i")
	mustEval(t, f, `=IMPRODUCT("1+2i","3+4i")`, "-5+10i")
	mustEval(t, f, `=IMDIV("3+4i","1+2i")`, "2.2-0.4i")
	mustEval(t, f, `=IMPOWER("2",2)`, "4")
	mustEval(t, f, `=IMSQRT("4")`, "2")
	mustEval(t, f, `=IMEXP("0")`, "1")
	mustEval(t, f, `=IMLN("1")`, "0")
	mustEval(t, f, `=IMLOG10("10")`, "1")
	mustEval(t, f, `=IMLOG2("2")`, "1")
}

func TestTheAnnuityFunctionsAreUnchanged(t *testing.T) {
	f := sheet(t)
	// Every annuity answer at a non-zero rate goes through pow, so it is
	// graded on value (mustEvalNear). The zero-rate arms are a single
	// division and a single multiplication, exact in every formulation, so
	// they stay pinned to the string.
	mustEvalNear(t, f, "=PMT(0.05/12,60,-10000)", 188.71233644010874)
	// The type argument still moves the payment to the start of the period.
	mustEvalNear(t, f, "=PMT(0.05/12,60,-10000,0,1)", 187.92929769969334)
	mustEval(t, f, "=PMT(0,60,-10000)", "166.66666666666666")
	mustEvalNear(t, f, "=FV(0.05/12,60,-100)", 6800.608284084337)
	mustEval(t, f, "=FV(0,60,-100)", "6000")
	mustEvalNear(t, f, "=NPER(0.05/12,-200,10000)", 56.184290761431996)
	mustEval(t, f, "=NPER(0,-200,10000)", "50")
	mustEvalNear(t, f, "=IPMT(0.05/12,1,60,-10000)", 41.666666666666664)
	mustEvalNear(t, f, "=PPMT(0.05/12,1,60,-10000)", 147.04566977344209)
	mustEvalNear(t, f, "=CUMIPMT(0.05/12,60,10000,1,12,0)", -458.99550746532793)
	mustEvalNear(t, f, "=CUMPRINC(0.05/12,60,10000,1,12,0)", -1805.552529815977)
	mustEvalNear(t, f, "=ISPMT(0.05/12,1,60,10000)", -40.97222222222222)
}

func TestTheCashFlowAndDepreciationFunctionsAreUnchanged(t *testing.T) {
	f := cashFlow(t)
	mustEvalNear(t, f, "=NPV(0.1,-100,50,50,50)", 22.12963595382827)
	// IRR is solved by a bounded numerical search that stops on a tolerance,
	// so its last digits are the stopping point of one particular search
	// rather than a property of the series. TEST_INTERFACE.md 4.08.7 says in
	// terms that they are not graded, so no exact IRR string is pinned here.
	// What is graded is the contract milestone 08 stated: a range argument, an
	// optional guess that changes nothing when it is the default 0.1, and a
	// refusal on a series that never changes sign.
	bare, err := evalAt(t, f, "Sheet1", "Z1", "=IRR(A1:A5)")
	if err != nil {
		t.Fatalf("CalcCellValue(\"=IRR(A1:A5)\"): unexpected error %v", err)
	}
	if bare == "" {
		t.Fatal("CalcCellValue(\"=IRR(A1:A5)\") returned no result")
	}
	guessed, err := evalAt(t, f, "Sheet1", "Z1", "=IRR(A1:A5,0.1)")
	if err != nil {
		t.Fatalf("CalcCellValue(\"=IRR(A1:A5,0.1)\"): unexpected error %v", err)
	}
	if guessed != bare {
		t.Fatalf("=IRR(A1:A5,0.1) = %q, want the same answer as =IRR(A1:A5), %q", guessed, bare)
	}
	mustReject(t, f, "=IRR(A2:A5)")
	mustEvalNear(t, f, "=MIRR(A1:A5,0.1,0.1)", 0.194126579381225)
	mustEval(t, f, "=DB(10000,1000,5,1)", "3690")
	mustEval(t, f, "=DB(10000,1000,5,1,6)", "1845")
	mustEval(t, f, "=DDB(10000,1000,5,1)", "4000")
	// EFFECT and NOMINAL both return a value below one, so both keep a digit
	// at this checkpoint that milestone 08 dropped. That rendering change is
	// scored in inherited-contract-migrations, not asserted here.
	mustEval(t, f, "=DOLLARDE(1.02,16)", "1.125")
	mustEval(t, f, "=DOLLARFR(1.125,16)", "1.02")
	mustEvalNear(t, f, "=PDURATION(0.05,100,200)", 14.206699082890445)
}

func TestTheDottedFunctionNamesAreUnchanged(t *testing.T) {
	f := sheet(t)
	mustEval(t, f, "=CEILING.MATH(1.2)", "2")
	mustEval(t, f, "=CEILING.PRECISE(1.2)", "2")
	mustEval(t, f, "=ISO.CEILING(1.2)", "2")
	mustEval(t, f, "=FLOOR.MATH(1.8)", "1")
	mustEval(t, f, "=FLOOR.PRECISE(1.8)", "1")
	// The undotted forms still need their significance argument.
	mustEval(t, f, "=CEILING(1.2,1)", "2")
	mustEval(t, f, "=FLOOR(1.8,1)", "1")
	// A dotted name and its legacy spelling still agree.
	mustEval(t, f, "=VARP(1,2,3)", "0.666666666666667")
	mustEval(t, f, "=VAR.P(1,2,3)", "0.666666666666667")
	mustEval(t, f, "=STDEV.S(1,2,3)", "1")
	mustEval(t, f, "=NORM.DIST(1,0,1,TRUE)", "0.841344746068543")
	mustEval(t, f, "=NORM.S.DIST(1,TRUE)", "0.841344746068543")
	mustEval(t, f, "=NORM.INV(0.5,0,1)", "0")
	mustEval(t, f, "=NORM.S.INV(0.5)", "0")
	mustEval(t, f, "=QUARTILE.INC(1,1)", "1")
	mustEval(t, f, "=POISSON.DIST(2,3,TRUE)", "0.423190081126844")
}

func TestTheReferenceAndDateFunctionsAreUnchanged(t *testing.T) {
	f := sheet(t)
	// With no argument ROW and COLUMN still report the cell holding the formula.
	mustEvalAt(t, f, "Sheet1", "Z1", "=ROW()", "1")
	mustEvalAt(t, f, "Sheet1", "Z1", "=COLUMN()", "26")
	mustEvalAt(t, f, "Sheet1", "D7", "=ROW()", "7")
	mustEvalAt(t, f, "Sheet1", "D7", "=COLUMN()", "4")
	mustEval(t, f, "=ROW(B7)", "7")
	mustEval(t, f, "=COLUMN(C3)", "3")
	// Given an area they still report its top-left corner.
	mustEval(t, f, "=ROW(A1:A5)", "1")
	mustEval(t, f, "=COLUMN(A1:C1)", "1")
	mustEval(t, f, "=ROWS(A1:A5)", "5")
	mustEval(t, f, "=COLUMNS(A1:C1)", "3")
	mustEval(t, f, "=ROWS(A1)", "1")
	mustEval(t, f, `=DATEDIF(43831,44270,"Y")`, "1")
	mustEval(t, f, `=DATEDIF(43831,44270,"M")`, "14")
	mustEval(t, f, `=DATEDIF(43831,44270,"D")`, "439")
	// The unit is still matched without regard to case.
	mustEval(t, f, `=DATEDIF(43831,44270,"y")`, "1")
}

// TestSHEETNowAcceptsASheetName pins the one milestone-08 contract that moves
// here. At that checkpoint SHEET was nullary: any argument at all was rejected
// with "SHEET accepts no arguments". It now takes an optional argument.

func TestSUMIFIsUnchanged(t *testing.T) {
	f := sheet(t)
	// The criterion may test the summed range itself or a separate one.
	mustEval(t, f, `=SUMIF(A1:A4,">2")`, "7")
	mustEval(t, f, `=SUMIF(A1:A4,"2")`, "2")
	mustEval(t, f, `=SUMIF(B1:B4,"apple",A1:A4)`, "1")
	mustEval(t, f, `=SUMIF(B1:B4,"absent",A1:A4)`, "0")
	mustReject(t, f, `=SUMIF(A1:A4)`)
}
