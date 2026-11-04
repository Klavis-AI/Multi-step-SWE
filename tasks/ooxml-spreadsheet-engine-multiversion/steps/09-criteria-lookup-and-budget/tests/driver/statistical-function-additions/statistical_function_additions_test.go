// Scored group: statistical-function-additions.
//
// The twenty-five statistical functions this milestone registers: AVEDEV,
// CHIDIST, CONFIDENCE, CONFIDENCE.NORM, DEVSQ, GEOMEAN, PERCENTILE.EXC,
// PERCENTRANK, PERCENTRANK.EXC, PERCENTRANK.INC, QUARTILE.EXC, RANK, RANK.EQ,
// STANDARDIZE, STDEV.P, STDEVP, TRIMMEAN, VAR, VAR.S, VARA, VARPA, WEIBULL,
// WEIBULL.DIST, Z.TEST and ZTEST. Behaviour transcribed from the upstream
// checkpoint this milestone tracks, qax-os/excelize tag v2.5.0 (calc_test.go),
// restricted to the public interface. The functions are OpenDocument v1.3 part
// 4 (OpenFormula) sections 6.18.2 (AVEDEV), 6.18.11 (LEGACY.CHIDIST, which is
// what the spec calls CHIDIST), 6.18.16 (CONFIDENCE), 6.18.20 (DEVSQ), 6.18.34
// (GEOMEAN), 6.18.57 (PERCENTILE), 6.18.58 (PERCENTRANK), 6.18.64 (QUARTILE),
// 6.18.65 (RANK), 6.18.71 (STANDARDIZE), 6.18.74 (STDEVP), 6.18.80 (TRIMMEAN),
// 6.18.82 (VAR), 6.18.84 (VARP), 6.18.86 (WEIBULL) and 6.18.87 (ZTEST). The
// dotted variants (CONFIDENCE.NORM, PERCENTILE.EXC, PERCENTRANK.EXC,
// PERCENTRANK.INC, QUARTILE.EXC, RANK.EQ, STDEV.P, VAR.S, WEIBULL.DIST, Z.TEST)
// are not spec names; they are graded as aliases of the undotted entries.
package statisticalfunctionadditions

import (
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

// sample builds the workbook every test in this group reads:
//
//	A1:A3 = 1, 2, 3
//	E1:E7 = 1, 2, 3, 4, 5, 6, 7
func sample(t *testing.T) *seedcalc.File {
	t.Helper()
	f := seedcalc.NewFile()
	for i := 1; i <= 3; i++ {
		if err := f.SetCellValue("Sheet1", cellName("A", i), i); err != nil {
			t.Fatalf("SetCellValue(A%d): %v", i, err)
		}
	}
	for i := 1; i <= 7; i++ {
		if err := f.SetCellValue("Sheet1", cellName("E", i), i); err != nil {
			t.Fatalf("SetCellValue(E%d): %v", i, err)
		}
	}
	return f
}

func cellName(col string, row int) string {
	name, err := seedcalc.JoinCellName(col, row)
	if err != nil {
		panic(err)
	}
	return name
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

func TestAVEDEVAndDEVSQMeasureSpreadAboutTheMean(t *testing.T) {
	f := sample(t)
	// AVEDEV is the mean absolute deviation, DEVSQ the sum of squared ones.
	mustEval(t, f, "=AVEDEV(A1:A3)", "2")
	mustEval(t, f, "=AVEDEV(1,2,3,4)", "1")
	mustEval(t, f, "=AVEDEV(1,2,3,4,5)", "1.2")
	mustEval(t, f, "=DEVSQ(A1:A3)", "2")
	mustEval(t, f, "=DEVSQ(1,2,3,4)", "5")
	// Ranges and scalars mix in one argument list.
	mustEval(t, f, "=DEVSQ(A1:A3,4)", "5")
	mustReject(t, f, "=AVEDEV()")
	mustReject(t, f, `=AVEDEV("a")`)
	mustReject(t, f, "=DEVSQ()")
}

func TestGEOMEANMultipliesRatherThanAdds(t *testing.T) {
	f := sample(t)
	mustEval(t, f, "=GEOMEAN(1,2,4)", "2")
	mustEval(t, f, "=GEOMEAN(A1:A3)", "1.81712059283214")
	// The nth root of a product is undefined once any factor is zero or
	// negative.
	mustReject(t, f, "=GEOMEAN(0,2)")
	mustReject(t, f, "=GEOMEAN(-1,2)")
	mustReject(t, f, "=GEOMEAN()")
}

func TestTheVarianceAndDeviationFamily(t *testing.T) {
	f := sample(t)
	// VAR and VAR.S are the sample variance; VARPA is the population one.
	mustEval(t, f, "=VAR(A1:A3)", "1")
	mustEval(t, f, "=VAR.S(A1:A3)", "1")
	mustEval(t, f, "=VARA(A1:A3)", "1")
	mustEval(t, f, "=VARPA(A1:A3)", "0.666666666666667")
	mustEval(t, f, "=VAR(1,2,3,4)", "1.66666666666667")
	mustEval(t, f, "=VARPA(1,2,3,4)", "1.25")
	// STDEVP and STDEV.P are the population standard deviation, the square
	// root of VARPA.
	mustEval(t, f, "=STDEVP(A1:A3)", "0.816496580927726")
	mustEval(t, f, "=STDEV.P(A1:A3)", "0.816496580927726")
	mustEval(t, f, "=STDEVP(1,2,3,4)", "1.118033988749895")
	// A boolean argument counts as its numeric value in all of them.
	mustEval(t, f, "=VARA(TRUE,1,2)", "0.333333333333333")
	mustEval(t, f, "=VARPA(TRUE,1,2)", "0.222222222222222")
	mustReject(t, f, "=VAR()")
	mustReject(t, f, "=VARA()")
	mustReject(t, f, "=VARPA()")
	mustReject(t, f, "=STDEVP()")
}

func TestSTANDARDIZERescalesAValue(t *testing.T) {
	f := sample(t)
	mustEval(t, f, "=STANDARDIZE(5,3,2)", "1")
	mustEval(t, f, "=STANDARDIZE(3,3,2)", "0")
	mustEval(t, f, "=STANDARDIZE(1,3,2)", "-1")
	// A zero standard deviation has no scale to divide by.
	mustReject(t, f, "=STANDARDIZE(5,3,0)")
	mustReject(t, f, "=STANDARDIZE(5,3)")
}

func TestRANKAndRANKEQPositionAValueWithinASet(t *testing.T) {
	f := sample(t)
	// The default order is descending: the largest value ranks first.
	mustEval(t, f, "=RANK(3,A1:A3)", "1")
	mustEval(t, f, "=RANK(1,A1:A3)", "3")
	mustEval(t, f, "=RANK(3,E1:E7)", "5")
	// A non-zero third argument switches to ascending order.
	mustEval(t, f, "=RANK(1,A1:A3,1)", "1")
	mustEval(t, f, "=RANK(3,A1:A3,1)", "3")
	mustEval(t, f, "=RANK(3,E1:E7,1)", "3")
	// RANK.EQ is the same function under its newer name.
	mustEval(t, f, "=RANK.EQ(3,A1:A3)", "1")
	mustEval(t, f, "=RANK.EQ(1,A1:A3,1)", "1")
	// A value that is not in the set has no rank.
	mustReject(t, f, "=RANK(9,A1:A3)")
	mustReject(t, f, "=RANK(2)")
}

func TestPERCENTRANKPlacesAValueOnTheZeroToOneScale(t *testing.T) {
	f := sample(t)
	// The inclusive form puts the smallest value at 0 and the largest at 1.
	mustEval(t, f, "=PERCENTRANK(A1:A3,1)", "0")
	mustEval(t, f, "=PERCENTRANK(A1:A3,2)", "0.5")
	mustEval(t, f, "=PERCENTRANK(A1:A3,3)", "1")
	mustEval(t, f, "=PERCENTRANK.INC(A1:A3,2)", "0.5")
	// The exclusive form leaves room at both ends.
	mustEval(t, f, "=PERCENTRANK.EXC(E1:E7,3)", "0.375")
	mustEval(t, f, "=PERCENTRANK(E1:E7,3)", "0.333")
	// A value between two members is interpolated, and the optional third
	// argument is the number of significant digits kept.
	mustEval(t, f, "=PERCENTRANK(E1:E7,3.5)", "0.416")
	mustEval(t, f, "=PERCENTRANK(E1:E7,3.5,2)", "0.41")
	mustEval(t, f, "=PERCENTRANK(A1:A3,1.5)", "0.25")
	// A value outside the set has no position on the scale.
	mustReject(t, f, "=PERCENTRANK(A1:A3,9)")
	mustReject(t, f, "=PERCENTRANK(A1:A3)")
}

func TestPERCENTILEEXCAndQUARTILEEXCInterpolateInsideTheSet(t *testing.T) {
	f := sample(t)
	// The exclusive percentile interpolates between the sorted values at the
	// position k*(n+1). For seven values that position is an index into the
	// set whenever k lies between 1/8 and 7/8.
	mustEval(t, f, "=PERCENTILE.EXC(E1:E7,0.125)", "1")
	mustEval(t, f, "=PERCENTILE.EXC(E1:E7,0.25)", "2")
	mustEval(t, f, "=PERCENTILE.EXC(E1:E7,0.5)", "4")
	mustEval(t, f, "=PERCENTILE.EXC(E1:E7,0.75)", "6")
	mustEval(t, f, "=PERCENTILE.EXC(E1:E7,0.8)", "6.4")
	mustEval(t, f, "=PERCENTILE.EXC(A1:A3,0.3)", "1.2")
	mustEval(t, f, "=PERCENTILE.EXC(A1:A3,0.6)", "2.4")
	// QUARTILE.EXC is the same computation at k = quart/4.
	mustEval(t, f, "=QUARTILE.EXC(E1:E7,1)", "2")
	mustEval(t, f, "=QUARTILE.EXC(E1:E7,2)", "4")
	mustEval(t, f, "=QUARTILE.EXC(E1:E7,3)", "6")
	// k outside the open unit interval, and a quart outside 1..3, are
	// rejected before any interpolation is attempted.
	mustReject(t, f, "=PERCENTILE.EXC(E1:E7,0)")
	mustReject(t, f, "=PERCENTILE.EXC(E1:E7,1)")
	mustReject(t, f, "=QUARTILE.EXC(E1:E7,0)")
	mustReject(t, f, "=QUARTILE.EXC(E1:E7,4)")
	mustReject(t, f, "=PERCENTILE.EXC(E1:E7)")
}

func TestTRIMMEANAveragesWhatIsLeftAfterTrimming(t *testing.T) {
	f := sample(t)
	mustEval(t, f, "=TRIMMEAN(E1:E7,0.2)", "4")
	mustEval(t, f, "=TRIMMEAN(E1:E7,0.5)", "4")
	mustEval(t, f, "=TRIMMEAN(E1:E7,0)", "4")
	mustEval(t, f, "=TRIMMEAN(A1:A3,0.9)", "2")
	// The trimmed fraction must be below one.
	mustReject(t, f, "=TRIMMEAN(A1:A3,1)")
	mustReject(t, f, "=TRIMMEAN(A1:A3)")
}

func TestTheDistributionFunctions(t *testing.T) {
	f := sample(t)
	// CHIDIST is the right tail of the chi-squared distribution.
	mustEval(t, f, "=CHIDIST(0.5,1)", "0.479500122186953")
	mustEval(t, f, "=CHIDIST(3,2)", "0.22313016014843")
	mustEval(t, f, "=CHIDIST(0,1)", "1")
	mustReject(t, f, "=CHIDIST(-1,1)")
	mustReject(t, f, "=CHIDIST(1)")
	// CONFIDENCE and CONFIDENCE.NORM are the same normal confidence interval.
	mustEval(t, f, "=CONFIDENCE(0.05,2.5,50)", "0.692951912733503")
	mustEval(t, f, "=CONFIDENCE.NORM(0.05,2.5,50)", "0.692951912733503")
	mustReject(t, f, "=CONFIDENCE(0,2.5,50)")
	mustReject(t, f, "=CONFIDENCE(0.05,2.5)")
	// WEIBULL and WEIBULL.DIST share an implementation; the fourth argument
	// chooses the cumulative form over the density.
	mustEval(t, f, "=WEIBULL(1,2,3,TRUE)", "0.10516068318563")
	mustEval(t, f, "=WEIBULL.DIST(1,2,3,TRUE)", "0.10516068318563")
	mustEval(t, f, "=WEIBULL(1,2,3,FALSE)", "0.198853181514304")
	mustEval(t, f, "=WEIBULL(0,2,3,TRUE)", "0")
	// A non-positive shape or scale is rejected. A negative x is not
	// checked at all: it is fed straight into the formula, and because the
	// shape here is even the result is the one for its magnitude.
	mustReject(t, f, "=WEIBULL(1,0,3,TRUE)")
	mustReject(t, f, "=WEIBULL(1,2,0,TRUE)")
	mustEval(t, f, "=WEIBULL(-1,2,3,TRUE)", "0.10516068318563")
	mustReject(t, f, "=WEIBULL(1,2,3)")
}

func TestZTESTAndItsDottedAlias(t *testing.T) {
	f := sample(t)
	// The one-tailed probability that the sample mean exceeds the given one.
	mustEval(t, f, "=Z.TEST(A1:A3,2)", "0.5")
	mustEval(t, f, "=ZTEST(A1:A3,2)", "0.5")
	mustEval(t, f, "=Z.TEST(A1:A3,1)", "0.0416322583317752")
	mustEval(t, f, "=Z.TEST(A1:A3,3)", "0.958367741668225")
	mustEval(t, f, "=Z.TEST(E1:E7,3)", "0.110335680959923")
	// A third argument supplies a known standard deviation.
	mustEval(t, f, "=Z.TEST(A1:A3,2,1)", "0.5")
	mustReject(t, f, "=Z.TEST(A1:A3)")
}
