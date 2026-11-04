// Scored group: engineering-and-text-additions.
//
// The eight engineering functions this milestone registers -- BESSELK, BESSELY,
// DELTA, ERF, ERF.PRECISE, ERFC, ERFC.PRECISE and GESTEP -- and the two text
// ones, TEXTJOIN and VALUE. BESSELK and BESSELY complete the Bessel set begun
// by the inherited BESSELI and BESSELJ. Behaviour transcribed from the upstream
// checkpoint this milestone tracks, qax-os/excelize tag v2.5.0 (calc_test.go),
// restricted to the public interface. The functions are OpenDocument v1.3 part
// 4 (OpenFormula) sections 6.16.14 (BESSELK), 6.16.15 (BESSELY), 6.16.26
// (DELTA), 6.16.27 (ERF), 6.16.28 (ERFC), 6.16.37 (GESTEP) and 6.13.34 (VALUE).
// Section 6.16 is titled "Mathematical Functions"; OpenFormula has no separate
// engineering section. TEXTJOIN has no OpenFormula definition and is graded
// from TEST_INTERFACE.md 4.09.6 alone. The Bessel values below are those of the
// Abramowitz & Stegun polynomial approximations the Oracle uses, which agree
// with the true functions to about eight significant digits and no further --
// grade against the approximation, not against the function.
package engineeringandtextadditions

import (
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// grid gives every test the block
//
//	     A     B      C
//	1    1    10   text
//	2    2    20
//	3    3    30
//
// where C2 and C3 are left empty so that TEXTJOIN's empty-cell flag has
// something to act on.
func grid(t *testing.T) *seedcalc.File {
	t.Helper()
	f := seedcalc.NewFile()
	for axis, v := range map[string]int{
		"A1": 1, "A2": 2, "A3": 3,
		"B1": 10, "B2": 20, "B3": 30,
	} {
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

func TestBESSELKAndBESSELYTakeAnOrderAndAnArgument(t *testing.T) {
	f := grid(t)
	mustEval(t, f, "=BESSELK(1,0)", "0.421024441682191")
	mustEval(t, f, "=BESSELK(1,1)", "0.601907227223457")
	mustEval(t, f, "=BESSELK(2,1)", "0.13986588")
	mustEval(t, f, "=BESSELY(1,0)", "0.0882569713977081")
	mustEval(t, f, "=BESSELY(1,1)", "-0.78121282095312")
	mustEval(t, f, "=BESSELY(2,1)", "-0.10703243170956")
	// Both are singular at zero, so a non-positive argument and a negative
	// order are refused.
	mustReject(t, f, "=BESSELK(-1,0)")
	mustReject(t, f, "=BESSELK(1,-1)")
	mustReject(t, f, "=BESSELY(-1,0)")
	mustReject(t, f, "=BESSELY(1,-1)")
	mustReject(t, f, "=BESSELK(1)")
}

func TestDELTATestsTwoNumbersForEquality(t *testing.T) {
	f := grid(t)
	mustEval(t, f, "=DELTA(1,1)", "1")
	mustEval(t, f, "=DELTA(1,2)", "0")
	mustEval(t, f, "=DELTA(2.5,2.5)", "1")
	// With one argument the comparison is against zero.
	mustEval(t, f, "=DELTA(0)", "1")
	mustEval(t, f, "=DELTA(1)", "0")
	mustReject(t, f, "=DELTA(1,2,3)")
}

func TestGESTEPComparesAgainstAThreshold(t *testing.T) {
	f := grid(t)
	// One where the value reaches the step, zero below it. The comparison is
	// inclusive.
	mustEval(t, f, "=GESTEP(2,1)", "1")
	mustEval(t, f, "=GESTEP(1,1)", "1")
	mustEval(t, f, "=GESTEP(0,1)", "0")
	// Without a threshold the step is at zero.
	mustEval(t, f, "=GESTEP(1)", "1")
	mustEval(t, f, "=GESTEP(0)", "1")
	mustEval(t, f, "=GESTEP(-1)", "0")
	mustReject(t, f, "=GESTEP()")
}

func TestERFIntegratesBetweenItsLimits(t *testing.T) {
	f := grid(t)
	mustEval(t, f, "=ERF(1)", "0.842700792949715")
	mustEval(t, f, "=ERF(0)", "0")
	// A second argument is the upper limit, the first becoming the lower one,
	// so ERF(0,x) is the same as ERF(x).
	mustEval(t, f, "=ERF(0,1)", "0.842700792949715")
	mustEval(t, f, "=ERF(1,2)", "0.152621472069238")
	// ERF.PRECISE is the single-limit form only.
	mustEval(t, f, "=ERF.PRECISE(1)", "0.842700792949715")
	mustEval(t, f, "=ERF.PRECISE(0)", "0")
	mustReject(t, f, "=ERF.PRECISE(1,2)")
}

func TestERFCIsTheComplementOfERF(t *testing.T) {
	f := grid(t)
	mustEval(t, f, "=ERFC(1)", "0.157299207050285")
	mustEval(t, f, "=ERFC(0)", "1")
	mustEval(t, f, "=ERFC.PRECISE(1)", "0.157299207050285")
	mustEval(t, f, "=ERFC.PRECISE(0)", "1")
	// Neither form takes a second limit.
	mustReject(t, f, "=ERFC(1,2)")
	mustReject(t, f, "=ERFC.PRECISE(1,2)")
}

func TestTEXTJOINConcatenatesWithASeparator(t *testing.T) {
	f := grid(t)
	mustEval(t, f, `=TEXTJOIN("-",TRUE,"a","b")`, "a-b")
	mustEval(t, f, `=TEXTJOIN("",TRUE,"a","b")`, "ab")
	mustEval(t, f, `=TEXTJOIN("-",TRUE,A1:A3)`, "1-2-3")
	// The second argument says whether empty cells are skipped. The block
	// A1:C3 has empty cells at C2 and C3; keeping them puts a separator
	// where each one was, including a trailing one for the last cell.
	mustEval(t, f, `=TEXTJOIN("-",TRUE,A1:C3)`, "1-10-text-2-20-3-30")
	mustEval(t, f, `=TEXTJOIN("-",FALSE,A1:C3)`, "1-10-text-2-20--3-30-")
	// A separator and a flag are not enough; there must be something to join.
	mustReject(t, f, `=TEXTJOIN("-")`)
	mustReject(t, f, `=TEXTJOIN("-",TRUE)`)
}

func TestVALUEReadsANumberOutOfText(t *testing.T) {
	f := grid(t)
	mustEval(t, f, `=VALUE("1.5")`, "1.5")
	mustEval(t, f, `=VALUE("-2")`, "-2")
	// A trailing per-cent sign scales the number, and exponent notation is
	// understood.
	mustEval(t, f, `=VALUE("1.5%")`, "0.015")
	mustEval(t, f, `=VALUE("1E3")`, "1000")
	// A number that is already a number passes through.
	mustEval(t, f, "=VALUE(5)", "5")
	// Currency symbols and thousands separators are not stripped.
	mustReject(t, f, `=VALUE("$1,000")`)
	mustReject(t, f, `=VALUE("abc")`)
	mustReject(t, f, `=VALUE("")`)
	mustReject(t, f, `=VALUE("1.5","x")`)
}
