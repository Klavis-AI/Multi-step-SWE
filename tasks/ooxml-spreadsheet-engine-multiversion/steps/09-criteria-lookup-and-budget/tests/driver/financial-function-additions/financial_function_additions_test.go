// Scored group: financial-function-additions.
//
// The thirty-three financial functions this milestone registers. Twenty-three
// price or measure a security -- ACCRINT, ACCRINTM, COUPDAYBS, COUPDAYS,
// COUPDAYSNC, COUPNCD, COUPNUM, COUPPCD, DISC, DURATION, INTRATE, MDURATION,
// ODDFPRICE, PRICE, PRICEDISC, PRICEMAT, RECEIVED, TBILLEQ, TBILLPRICE,
// TBILLYIELD, YIELD, YIELDDISC and YIELDMAT -- and ten cover depreciation,
// present value and dated cash flows: AMORDEGRC, AMORLINC, PV, RATE, RRI, SLN,
// SYD, VDB, XIRR and XNPV. Behaviour transcribed from the upstream checkpoint
// this milestone tracks, qax-os/excelize tag v2.5.0 (calc_test.go), restricted
// to the public interface. The functions are OpenDocument v1.3 part 4
// (OpenFormula) section 6.12 "Financial Functions".
package financialfunctionadditions

import (
	"math"
	"strconv"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

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

// mustEvalNear grades a numeric result on its value rather than on its
// rendering, to the relative tolerance the caller names. Two kinds of
// expectation need it, for two different reasons.
//
// The closed-form security functions reach their answers through math.Pow and
// a running summation over a coupon or cash-flow schedule. TEST_INTERFACE.md
// 4.09.7 fixes the quantity each one computes, and 4.09.17 hands a result of
// fifteen or fewer decimal places back exactly as produced, so the final unit
// in the last place reaches the rendered string -- while no document anywhere
// fixes an association order. Summing the XNPV flows from the last date back
// to the first gives 2472.982419245307 where the Oracle's forward sum gives
// 2472.9824192453066; RECEIVED's 1000/(1-0.05) is 1052.6315789473686 where
// 1000*(1/(1-0.05)) is ...683. Both members of each pair are correct readings
// of the same formula, and neither is the more accurate. A relative 1e-9
// accepts every such arrangement and is still orders of magnitude tighter than
// any real formulation error, which is the tolerance 4.08 already states for
// the same hazard in the inherited annuity family. A result that is exact in
// binary -- a whole number, or a zero-rate reduction such as =PV(0,10,-1000)
// -- stays an exact string, because there the string is the value.
//
// RATE needs a looser 1e-6 for a different reason. 4.09.7 fixes RATE's
// residual, its guess default of 0.1 and its |delta| < 1e-6 stop, but not how
// the derivative of the residual is formed -- and a stop that loose leaves the
// iterate short of floating-point accuracy, so the arrangement decides the
// trailing digits. Newton-Raphson on the documented residual with a
// central-difference derivative, same start and same stop, converges in the
// same ten steps to 0.00618341316199073 where the Oracle's arrangement gives
// 0.00618341316212928: a correct reading of the contract, different at the
// eleventh figure. 1e-6 relative still holds the rate to seven figures, which
// a RATE that is actually wrong misses by orders.
func mustEvalNear(t *testing.T, f *seedcalc.File, formula string, want, rel float64) {
	t.Helper()
	if err := f.SetCellFormula("Sheet1", "H1", formula); err != nil {
		t.Fatalf("SetCellFormula(%q): %v", formula, err)
	}
	got, err := f.CalcCellValue("Sheet1", "H1")
	if err != nil {
		t.Fatalf("CalcCellValue(%q): unexpected error %v", formula, err)
	}
	v, perr := strconv.ParseFloat(got, 64)
	if perr != nil {
		t.Fatalf("CalcCellValue(%q) = %q, want a number near %v", formula, got, want)
	}
	if math.Abs(v-want) > rel*math.Abs(want) {
		t.Fatalf("CalcCellValue(%q) = %q, want %v to a relative %v", formula, got, want, rel)
	}
}

func TestACCRINTAccruesInterestUpToSettlement(t *testing.T) {
	f := seedcalc.NewFile()
	// Issue, first interest, settlement, rate, par, frequency and basis.
	mustEvalNear(t, f, `=ACCRINT("2011-01-01","2011-04-01","2014-06-15",0.1,1000,4,0)`, 345.55555555555554, 1e-9)
	// The basis and the calculation-method flag are both optional.
	mustEvalNear(t, f, `=ACCRINT("2011-01-01","2011-04-01","2014-06-15",0.1,1000,4)`, 345.55555555555554, 1e-9)
	mustEvalNear(t, f, `=ACCRINT("2011-01-01","2011-04-01","2014-06-15",0.1,1000,4,0,TRUE)`, 345.55555555555554, 1e-9)
	mustReject(t, f, `=ACCRINT("2011-01-01","2011-04-01","2014-06-15",0.1,1000)`)
	// ACCRINTM is the single-payment form: issue, maturity, rate, par, basis.
	mustEval(t, f, `=ACCRINTM("2011-01-01","2011-04-01",0.1,1000)`, "25")
	mustEvalNear(t, f, `=ACCRINTM("2011-01-01","2011-04-01",0.1,1000,3)`, 24.65753424657534, 1e-9)
	mustReject(t, f, `=ACCRINTM("2011-01-01","2011-04-01",0.1)`)
}

func TestTheCouponFunctionsDivideTheBondSchedule(t *testing.T) {
	f := seedcalc.NewFile()
	// A quarterly bond settling 2011-02-15 and maturing 2017-03-31.
	mustEval(t, f, `=COUPDAYBS("2011-02-15","2017-03-31",4,0)`, "45")
	mustEval(t, f, `=COUPDAYS("2011-02-15","2017-03-31",4,0)`, "90")
	mustEval(t, f, `=COUPDAYSNC("2011-02-15","2017-03-31",4,0)`, "46")
	mustEval(t, f, `=COUPNUM("2011-02-15","2017-03-31",4,0)`, "25")
	// The two dates come back as serial numbers, not formatted dates.
	mustEval(t, f, `=COUPPCD("2011-02-15","2017-03-31",4,0)`, "40543")
	mustEval(t, f, `=COUPNCD("2011-02-15","2017-03-31",4,0)`, "40633")
	// The basis argument is optional; the frequency is not, and it must be
	// one of the recognised payment frequencies.
	mustEval(t, f, `=COUPNUM("2011-02-15","2017-03-31",4)`, "25")
	mustReject(t, f, `=COUPNUM("2011-02-15","2017-03-31",5)`)
	mustReject(t, f, `=COUPNUM("2017-03-31","2011-02-15",4)`)
	mustReject(t, f, `=COUPNUM("2011-02-15","2017-03-31")`)
}

func TestTheDiscountedSecurityFunctions(t *testing.T) {
	f := seedcalc.NewFile()
	// DISC recovers the discount rate from a price, PRICEDISC the price from
	// a rate, and YIELDDISC the yield from the price. These are closed forms,
	// so a result that is not a whole number is graded on its value at the
	// relative 1e-9 of 4.09.7 rather than pinned to the rendered string,
	// whose last digits belong to the association order 4.09.7 does not fix.
	mustEvalNear(t, f, `=DISC("2016-04-01","2021-03-31",95,100)`, 0.01, 1e-9)
	mustEvalNear(t, f, `=DISC("2016-04-01","2021-03-31",95,100,1)`, 0.0100091324200913, 1e-9)
	mustEval(t, f, `=PRICEDISC("2017-04-01","2021-03-31",0.05,100)`, "80")
	mustEvalNear(t, f, `=PRICEDISC("2017-04-01","2021-03-31",0.05,100,1)`, 80.01095290251916, 1e-9)
	mustEvalNear(t, f, `=YIELDDISC("2017-01-01","2021-12-31",95,100)`, 0.0105263157894737, 1e-9)
	mustEvalNear(t, f, `=YIELDDISC("2017-01-01","2021-12-31",95,100,0)`, 0.0105263157894737, 1e-9)
	// The dates must be in order and the money must be positive.
	mustReject(t, f, `=DISC("2021-03-31","2016-04-01",95,100)`)
	mustReject(t, f, `=DISC("2016-04-01","2021-03-31",0,100)`)
	mustReject(t, f, `=PRICEDISC("2017-04-01","2021-03-31",0,100)`)
	mustReject(t, f, `=DISC("2016-04-01","2021-03-31",95)`)
}

func TestPRICEAndYIELDForACouponBond(t *testing.T) {
	f := seedcalc.NewFile()
	mustEvalNear(t, f, `=PRICE("2017-04-01","2021-03-31",0.06,0.05,100,4,0)`, 103.60270778908432, 1e-9)
	mustEvalNear(t, f, `=PRICE("2017-04-01","2021-03-31",0.06,0.05,100,4)`, 103.60270778908432, 1e-9)
	// YIELD solves by iteration, so it is graded on its value, not on its
	// rendered digits (TEST_INTERFACE.md 4.09.7).
	mustEvalNear(t, f, `=YIELD("2017-01-01","2021-12-31",0.05,95,100,2,0)`, 0.0617815124629779, 1e-9)
	// A negative coupon rate has no price.
	mustReject(t, f, `=PRICE("2017-04-01","2021-03-31",-1,0.05,100,4)`)
	mustReject(t, f, `=PRICE("2017-04-01","2021-03-31",0.06,0.05,100)`)
}

func TestTheSecuritiesThatPayOnlyAtMaturity(t *testing.T) {
	f := seedcalc.NewFile()
	// PRICEMAT and YIELDMAT take an issue date as well as settlement and
	// maturity. Both are closed forms, graded on value like the discounted
	// securities above.
	mustEvalNear(t, f, `=PRICEMAT("2017-04-01","2021-03-31","2017-01-01",0.05,0.1)`, 85.35714285714286, 1e-9)
	mustEvalNear(t, f, `=PRICEMAT("2017-04-01","2021-03-31","2017-01-01",0.05,0.1,0)`, 85.35714285714286, 1e-9)
	mustEvalNear(t, f, `=YIELDMAT("2017-01-01","2021-12-31","2016-01-01",0.05,95)`, 0.06, 1e-9)
	mustEvalNear(t, f, `=YIELDMAT("2017-01-01","2021-12-31","2016-01-01",0.05,95,0)`, 0.06, 1e-9)
	// INTRATE and RECEIVED are the two sides of a fully invested security.
	mustEvalNear(t, f, `=INTRATE("2017-01-01","2021-12-31",95,100)`, 0.0105263157894737, 1e-9)
	mustEvalNear(t, f, `=INTRATE("2017-01-01","2021-12-31",95,100,1)`, 0.0105320836337419, 1e-9)
	mustEvalNear(t, f, `=RECEIVED("2011-01-01","2011-12-31",1000,0.05)`, 1052.6315789473686, 1e-9)
	mustEvalNear(t, f, `=RECEIVED("2011-01-01","2011-12-31",1000,0.05,0)`, 1052.6315789473686, 1e-9)
	mustReject(t, f, `=INTRATE("2017-01-01","2021-12-31",0,100)`)
	mustReject(t, f, `=RECEIVED("2011-01-01","2011-12-31",1000,0)`)
}

func TestTheTreasuryBillFunctions(t *testing.T) {
	f := seedcalc.NewFile()
	mustEvalNear(t, f, `=TBILLEQ("2017-01-01","2017-06-30",0.045)`, 0.0466751918158568, 1e-9)
	mustEvalNear(t, f, `=TBILLPRICE("2017-01-01","2017-06-30",0.045)`, 97.75, 1e-9)
	mustEvalNear(t, f, `=TBILLYIELD("2017-01-01","2017-06-30",95)`, 0.105263157894737, 1e-9)
	// All three take exactly three arguments; there is no basis.
	mustReject(t, f, `=TBILLEQ("2017-01-01","2017-06-30")`)
	mustReject(t, f, `=TBILLPRICE("2017-01-01","2017-06-30",0.045,0)`)
}

func TestDURATIONAndMDURATION(t *testing.T) {
	f := seedcalc.NewFile()
	mustEvalNear(t, f, `=DURATION("2018-07-01","2048-07-01",0.08,0.09,2,0)`, 10.953095878170492, 1e-9)
	mustEvalNear(t, f, `=DURATION("2018-07-01","2048-07-01",0.08,0.09,2)`, 10.953095878170492, 1e-9)
	// The modified duration is the Macaulay duration discounted by one
	// period's yield, so it is always the smaller of the two.
	mustEvalNear(t, f, `=MDURATION("2018-07-01","2048-07-01",0.08,0.09,2,0)`, 10.481431462364108, 1e-9)
	mustReject(t, f, `=DURATION("2018-07-01","2048-07-01",0.08,0.09)`)
}

func TestODDFPRICEHandlesAnIrregularFirstPeriod(t *testing.T) {
	f := seedcalc.NewFile()
	// Settlement, maturity, issue, first coupon, rate, yield, redemption,
	// frequency and basis.
	// The coupon arguments are written as plain decimals: the percent sign is
	// tokenised but not evaluated at this checkpoint (TEST_INTERFACE.md 4.06.1,
	// 4.07.9), so a `5.5%` operand would be read as 5.5.
	mustEvalNear(t, f, `=ODDFPRICE("02/01/2017","03/31/2021","12/01/2016","03/31/2017",0.055,0.035,100,4,0)`, 106.77169500099872, 1e-9)
	mustReject(t, f, `=ODDFPRICE("02/01/2017","03/31/2021","12/01/2016","03/31/2017",0.055,0.035,100)`)
}

func TestTheDepreciationFunctions(t *testing.T) {
	f := seedcalc.NewFile()
	// Straight line, sum of years' digits and the variable declining balance.
	mustEval(t, f, "=SLN(10000,1000,5)", "1800")
	mustEval(t, f, "=SYD(10000,1000,5,1)", "3000")
	mustEval(t, f, "=VDB(10000,1000,5,0,1)", "4000")
	mustEval(t, f, "=VDB(10000,1000,5,0,1,2)", "4000")
	mustEval(t, f, "=VDB(10000,1000,5,0,1,2,TRUE)", "4000")
	// A life of zero has no schedule, and the period must be inside it.
	mustReject(t, f, "=SLN(10000,1000,0)")
	mustReject(t, f, "=SYD(10000,1000,0,1)")
	mustReject(t, f, "=SYD(10000,1000,5,0)")
	mustReject(t, f, "=SLN(10000,1000)")
	mustReject(t, f, "=SYD(10000,1000,5)")
	mustReject(t, f, "=VDB(10000,1000,5,0)")
}

func TestTheFrenchDepreciationFunctionsNeedTextDates(t *testing.T) {
	f := seedcalc.NewFile()
	// AMORDEGRC and AMORLINC push their date arguments through DATEVALUE
	// rather than accepting a serial number, so the dates must be written as
	// text that DATEVALUE can parse.
	mustEval(t, f, `=AMORDEGRC(1000,"2020-01-01","2020-03-31",100,1,0.15)`, "340")
	mustEval(t, f, `=AMORDEGRC(1000,"2020-01-01","2020-03-31",100,1,0.15,0)`, "340")
	mustEval(t, f, `=AMORLINC(1000,"2020-01-01","2020-03-31",100,1,0.15)`, "150")
	// AMORDEGRC's coefficient table only covers rates below one half.
	mustReject(t, f, `=AMORDEGRC(1000,"2020-01-01","2020-03-31",100,1,0.6)`)
	// A serial number is not a date here.
	mustReject(t, f, "=AMORLINC(1000,43831,43921,100,1,0.15)")
}

func TestPVRRIAndRATE(t *testing.T) {
	f := seedcalc.NewFile()
	mustEvalNear(t, f, "=PV(0.05,10,-1000)", 7721.734929184813, 1e-9)
	mustEvalNear(t, f, "=PV(0.05,10,-1000,0)", 7721.734929184813, 1e-9)
	// The fifth argument moves the payment to the start of each period.
	mustEvalNear(t, f, "=PV(0.05,10,-1000,0,1)", 8107.821675644054, 1e-9)
	// A zero rate is just the undiscounted total.
	mustEval(t, f, "=PV(0,10,-1000)", "10000")
	mustReject(t, f, "=PV(0.05,10)")
	// RRI is the growth rate implied by a start and an end value. It is a
	// closed form reached through a power, so it is graded on value like the
	// security functions above.
	mustEvalNear(t, f, "=RRI(10,1000,2000)", 0.0717734625362931, 1e-9)
	mustReject(t, f, "=RRI(0,1000,2000)")
	mustReject(t, f, "=RRI(10,0,2000)")
	mustReject(t, f, "=RRI(10,1000,-1)")
	// RATE solves for the periodic interest rate. It is iterative, and the
	// optional guess does not change the answer it converges on here: 0.1 is
	// the default, so all four calls are the same call. The rate is graded on
	// value, not on its rendering -- see mustEvalNear above.
	mustEvalNear(t, f, "=RATE(60,-200,10000)", 0.00618341316212928, 1e-6)
	mustEvalNear(t, f, "=RATE(60,-200,10000,0)", 0.00618341316212928, 1e-6)
	mustEvalNear(t, f, "=RATE(60,-200,10000,0,0)", 0.00618341316212928, 1e-6)
	mustEvalNear(t, f, "=RATE(60,-200,10000,0,0,0.1)", 0.00618341316212928, 1e-6)
	mustReject(t, f, "=RATE(60,-200)")
}

func TestXIRRAndXNPVReadDatedCashFlows(t *testing.T) {
	f := seedcalc.NewFile()
	// Like the French depreciation pair, these two run their dates through
	// DATEVALUE, so the date column has to hold text rather than serials.
	flows := []int{-10000, 2750, 4250, 3250, 2750}
	dates := []string{"2008-01-01", "2008-03-01", "2008-10-30", "2009-02-15", "2009-04-01"}
	for i := range flows {
		axis, err := seedcalc.JoinCellName("D", i+1)
		if err != nil {
			t.Fatalf("JoinCellName: %v", err)
		}
		if err := f.SetCellValue("Sheet1", axis, flows[i]); err != nil {
			t.Fatalf("SetCellValue(%s): %v", axis, err)
		}
		axis, err = seedcalc.JoinCellName("E", i+1)
		if err != nil {
			t.Fatalf("JoinCellName: %v", err)
		}
		if err := f.SetCellStr("Sheet1", axis, dates[i]); err != nil {
			t.Fatalf("SetCellStr(%s): %v", axis, err)
		}
	}
	mustEval(t, f, "=XIRR(D1:D5,E1:E5)", "0.373362533518831")
	mustEvalNear(t, f, "=XNPV(0.05,D1:D5,E1:E5)", 2472.9824192453066, 1e-9)
	// XNPV discounts, so it has nothing to do at a rate of zero.
	mustReject(t, f, "=XNPV(0,D1:D5,E1:E5)")
	mustReject(t, f, "=XIRR(D1:D5)")
	mustReject(t, f, "=XNPV(0.05,D1:D5)")
}
