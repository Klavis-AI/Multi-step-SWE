// Scored group: financial-function-library.
//
// The financial family this milestone adds: the annuity functions and the
// payment split between interest and principal, their cumulative forms, the
// discounted cash-flow measures, the two declining-balance depreciation
// methods, interest-rate conversion, fractional dollar notation and the
// remaining single-purpose helpers. Semantics follow OpenDocument v1.3 part 4
// (OpenFormula) section 6.12 "Financial Functions". Sign conventions are the
// ones this checkpoint uses and are asserted, not assumed.
//
// The trailing digits of this family are deliberately not graded, and
// TEST_INTERFACE.md 4.08.7 says so in terms. A closed form fixes the value; it
// does not fix the last bits of the double an implementation arrives at, which
// depend on how the form was grouped. Those results go through mustEvalApprox.
// The values that are exact -- integers, plain divisions, and the ones the
// rendering rule of 4.08's head lands on its 1e-15 grid -- stay exact strings.
package financialfunctionlibrary

import (
	"math"
	"strconv"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// cashFlow builds A1:A5 = -100, 30, 40, 50, 60: one outlay followed by four
// returns, the shape IRR and MIRR need in order to have a root at all.
func cashFlow(t *testing.T) *seedcalc.File {
	t.Helper()
	f := seedcalc.NewFile()
	// Written as an ordered slice, not a map: IRR, MIRR and FVSCHEDULE all read
	// the series in cell order, and an implementation that stores cells in
	// insertion order would see a different series on every run if this fixture
	// were populated in Go's map-iteration order.
	for _, c := range []struct {
		axis string
		v    int
	}{{"A1", -100}, {"A2", 30}, {"A3", 40}, {"A4", 50}, {"A5", 60}} {
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

// evalNumber evaluates and parses the returned string as a number.
func evalNumber(t *testing.T, f *seedcalc.File, formula string) float64 {
	t.Helper()
	got, err := eval(t, f, formula)
	if err != nil {
		t.Fatalf("CalcCellValue(%q): unexpected error %v", formula, err)
	}
	v, perr := strconv.ParseFloat(got, 64)
	if perr != nil {
		t.Fatalf("CalcCellValue(%q) = %q, which is not a number: %v", formula, got, perr)
	}
	return v
}

// mustEvalApprox grades the mathematics of a result rather than the arithmetic
// route taken to it. The annuity, cash-flow and duration functions reach their
// answer through an accumulation, a power or a logarithm, so the last two or
// three bits of the double -- every digit the rendering prints past the twelfth
// significant figure -- depend on the order the operations were written in and
// not on the closed form. Two conforming implementations disagree there and
// neither is the more accurate one. want is carried to twelve significant
// figures and the tolerance is relative, some three orders of magnitude wider
// than any regrouping of the closed form produces.
func mustEvalApprox(t *testing.T, f *seedcalc.File, formula string, want float64) {
	t.Helper()
	got := evalNumber(t, f, formula)
	if math.Abs(got-want) > 1e-9*math.Abs(want) {
		t.Fatalf("CalcCellValue(%q) = %v, want %.12g to within a relative 1e-9", formula, got, want)
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

func TestPMTComputesTheLevelPayment(t *testing.T) {
	f := cashFlow(t)
	mustEvalApprox(t, f, "=PMT(0.05/12,60,-10000)", 188.712336440)
	// The optional fifth argument is the payment-timing flag: 0 for the end of
	// the period, 1 for the beginning. Paying earlier costs less interest.
	mustEvalApprox(t, f, "=PMT(0.05/12,60,-10000,0,1)", 187.929297700)
	// A zero rate reduces the annuity to plain division, which is exact, so
	// this one is compared as a string.
	mustEval(t, f, "=PMT(0,60,-10000)", "166.66666666666666")
	// The timing flag is exactly those two values.
	mustReject(t, f, "=PMT(0.05/12,60,-10000,0,2)")
}

func TestFVAndNPERCompleteTheAnnuityIdentity(t *testing.T) {
	f := cashFlow(t)
	mustEvalApprox(t, f, "=FV(0.05/12,60,-100)", 6800.60828408)
	// The optional fourth argument is a present value carried forward.
	mustEvalApprox(t, f, "=FV(0.05/12,60,-100,-1000)", 8083.96696259)
	mustEval(t, f, "=FV(0,60,-100)", "6000")
	mustEvalApprox(t, f, "=NPER(0.05/12,-200,10000)", 56.1842907614)
	mustEval(t, f, "=NPER(0,-200,10000)", "50")
	// RATE completes the annuity identity everywhere else, and is deliberately
	// not registered at this checkpoint (TEST_INTERFACE.md 4.08.7): the same
	// arguments NPER accepts must be refused outright rather than solved.
	mustReject(t, f, "=RATE(60,-200,10000)")
}

func TestIPMTAndPPMTSplitOnePayment(t *testing.T) {
	f := cashFlow(t)
	// The first period's interest is the full balance times the period rate;
	// the principal portion is the rest of that period's level payment.
	mustEvalApprox(t, f, "=IPMT(0.05/12,1,60,-10000)", 41.6666666667)
	mustEvalApprox(t, f, "=PPMT(0.05/12,1,60,-10000)", 147.045669773)
	// The split is a property of the annuity rather than of the evaluation
	// route, so it is asserted where the digits are not: the interest and
	// principal halves of one period add back to that period's level payment.
	ipmt := evalNumber(t, f, "=IPMT(0.05/12,1,60,-10000)")
	ppmt := evalNumber(t, f, "=PPMT(0.05/12,1,60,-10000)")
	pmt := evalNumber(t, f, "=PMT(0.05/12,60,-10000)")
	if math.Abs((ipmt+ppmt)-pmt) > 1e-9*math.Abs(pmt) {
		t.Fatalf("IPMT+PPMT = %v, want PMT = %v", ipmt+ppmt, pmt)
	}
	// The period is one-based and must lie inside the term.
	mustReject(t, f, "=IPMT(0.05/12,0,60,-10000)")
	mustReject(t, f, "=IPMT(0.05/12,61,60,-10000)")
}

func TestTheCumulativeForms(t *testing.T) {
	f := cashFlow(t)
	// Interest and principal paid across the first twelve periods of the same
	// loan, reported as outflows.
	mustEvalApprox(t, f, "=CUMIPMT(0.05/12,60,10000,1,12,0)", -458.995507465)
	mustEvalApprox(t, f, "=CUMPRINC(0.05/12,60,10000,1,12,0)", -1805.55252982)
	// Same two-valued timing flag as PMT.
	mustReject(t, f, "=CUMIPMT(0.05/12,60,10000,1,12,2)")
}

func TestNPVDiscountsAScalarSeries(t *testing.T) {
	f := cashFlow(t)
	mustEvalApprox(t, f, "=NPV(0.1,-100,50,50,50)", 22.1296359538)
	// A rate with nothing to discount is an arity error.
	mustReject(t, f, "=NPV(0.1)")
}

func TestIRRAndMIRRSolveForTheRate(t *testing.T) {
	f := cashFlow(t)
	// IRR is solved by a bounded numerical search that stops on a tolerance, so
	// its last digits are the stopping point of one particular search and not a
	// property of the series -- TEST_INTERFACE.md 4.08.7 says in terms that they
	// are not graded, and no exact IRR string is pinned here. What is graded is
	// the contract: a range argument, a guess that only seeds the search, and a
	// refusal on a series with no sign change.
	bare, err := eval(t, f, "=IRR(A1:A5)")
	if err != nil {
		t.Fatalf("CalcCellValue(\"=IRR(A1:A5)\"): unexpected error %v", err)
	}
	if bare == "" {
		t.Fatal("CalcCellValue(\"=IRR(A1:A5)\") returned no result")
	}
	// The guess is optional and defaults to 0.1, so naming it changes nothing.
	guessed, err := eval(t, f, "=IRR(A1:A5,0.1)")
	if err != nil {
		t.Fatalf("CalcCellValue(\"=IRR(A1:A5,0.1)\"): unexpected error %v", err)
	}
	if guessed != bare {
		t.Fatalf("=IRR(A1:A5,0.1) = %q, want the same root as =IRR(A1:A5), %q", guessed, bare)
	}
	// MIRR, by contrast, is a closed form, so the series determines its value
	// and it is asserted -- to a tolerance, because the closed form does not
	// determine which double a particular grouping of it lands on.
	mustEvalApprox(t, f, "=MIRR(A1:A5,0.1,0.1)", 0.194126579381)
	// A series that never changes sign has no internal rate of return.
	mustReject(t, f, "=IRR(A2:A5)")
}

func TestTheDecliningBalanceDepreciations(t *testing.T) {
	f := cashFlow(t)
	mustEval(t, f, "=DB(10000,1000,5,1)", "3690")
	// DB's optional fifth argument is the number of months in the first year;
	// half a year yields half the first period's charge.
	mustEval(t, f, "=DB(10000,1000,5,1,6)", "1845")
	// DDB's optional fifth argument is the rate factor, and two is its default,
	// so naming it explicitly changes nothing.
	mustEval(t, f, "=DDB(10000,1000,5,1)", "4000")
	mustEval(t, f, "=DDB(10000,1000,5,1,2)", "4000")
}

func TestEFFECTAndNOMINALConvertBetweenRates(t *testing.T) {
	f := cashFlow(t)
	// These two are compared as strings and TEST_INTERFACE.md 4.08.7 gives both
	// values: each carries sixteen decimal places, so the rendering correction
	// of 4.08's head lands it on the 1e-15 grid and the last bits drop out.
	mustEval(t, f, "=EFFECT(0.05,4)", "0.050945336914062")
	mustEval(t, f, "=NOMINAL(0.05,4)", "0.049088937716157")
	// There is no such thing as zero compounding periods per year.
	mustReject(t, f, "=EFFECT(0.05,0)")
}

func TestTheDollarFractionPair(t *testing.T) {
	f := cashFlow(t)
	// 1.02 in sixteenths is one and two sixteenths, which is 1.125 decimal;
	// DOLLARFR is the inverse.
	mustEval(t, f, "=DOLLARDE(1.02,16)", "1.125")
	mustEval(t, f, "=DOLLARFR(1.125,16)", "1.02")
	// A zero fraction has nothing to divide by.
	mustReject(t, f, "=DOLLARDE(1.02,0)")
}

func TestFVSCHEDULEAndTheRemainingHelpers(t *testing.T) {
	f := cashFlow(t)
	// FVSCHEDULE compounds by each rate in the range in turn. A2:A3 holds the
	// two rates 30 and 40, so 100 grows to 100 * 31 * 41, an exact integer.
	mustEval(t, f, "=FVSCHEDULE(100,A2:A3)", "127100")
	mustEvalApprox(t, f, "=PDURATION(0.05,100,200)", 14.2066990829)
	// Nothing grows at a zero rate, so no period reaches the target.
	mustReject(t, f, "=PDURATION(0,100,200)")
	mustEvalApprox(t, f, "=ISPMT(0.05/12,1,60,10000)", -40.9722222222)
}
