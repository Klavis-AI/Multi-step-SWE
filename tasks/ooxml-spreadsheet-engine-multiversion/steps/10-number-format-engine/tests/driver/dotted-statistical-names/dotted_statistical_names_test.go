// Scored group: dotted-statistical-names.
//
// The 2010 spellings of the statistical functions. Most are a new name for a
// distribution that already answers to an older one, and the pair must agree
// to the digit; a few -- the right-tailed and two-tailed variants, and the
// range form of the binomial -- have no older spelling and compute something
// the original could not express.
//
// Semantics follow OpenDocument v1.3 part 4 (OpenFormula) section 6.18.
package dottedstatisticalnames

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

// agree checks that two spellings of one distribution return the same thing,
// which is the point of the renamed forms.
func agree(t *testing.T, f *seedcalc.File, dotted, original, want string) {
	t.Helper()
	mustEval(t, f, dotted, want)
	mustEval(t, f, original, want)
}

func TestTheBetaSpellingsAgree(t *testing.T) {
	f := seedcalc.NewFile()
	// BETA.DIST takes the cumulative flag that BETADIST leaves optional.
	agree(t, f, `=BETA.DIST(0.4,4,5,TRUE)`, `=BETADIST(0.4,4,5)`, "0.4059136")
	agree(t, f, `=BETA.INV(0.2,4,5)`, `=BETAINV(0.2,4,5)`, "0.303225844664082")
	mustEval(t, f, `=BETA.DIST(0.4,4,5,FALSE)`, "2.322432")
}

func TestTheBinomialSpellingsAgree(t *testing.T) {
	f := seedcalc.NewFile()
	agree(t, f, `=BINOM.DIST(3,10,0.3,TRUE)`, `=BINOMDIST(3,10,0.3,TRUE)`, "0.6496107184")
	agree(t, f, `=BINOM.INV(10,0.3,0.5)`, `=CRITBINOM(10,0.3,0.5)`, "3")
	mustRejectWith(t, f, `=BINOM.INV(10,0.3)`, "BINOM.INV requires 3 numeric arguments")
}

func TestTheBinomialRangeFormHasNoOlderSpelling(t *testing.T) {
	f := seedcalc.NewFile()
	// With three arguments it is the mass at that count.
	mustEval(t, f, `=BINOM.DIST.RANGE(10,0.3,3)`, "0.266827932")
	// With four it sums the mass across the closed interval.
	mustEval(t, f, `=BINOM.DIST.RANGE(10,0.3,2,4)`, "0.7004233215")
	mustRejectWith(t, f, `=BINOM.DIST.RANGE(10,0.3)`, "BINOM.DIST.RANGE requires at least 3 arguments")
}

func TestTheChiSquaredSpellingsSplitTheTails(t *testing.T) {
	f := seedcalc.NewFile()
	// The undotted CHIDIST is right-tailed, so CHISQ.DIST.RT matches it and
	// CHISQ.DIST, which is left-tailed, does not.
	agree(t, f, `=CHISQ.DIST.RT(0.5,3)`, `=CHIDIST(0.5,3)`, "0.918891411654676")
	mustEval(t, f, `=CHISQ.DIST(0.5,3,TRUE)`, "0.0811085883453241")
	agree(t, f, `=CHISQ.INV.RT(0.5,3)`, `=CHIINV(0.5,3)`, "2.36597388437534")
	mustEval(t, f, `=CHISQ.INV(0.5,3)`, "2.36597388437534")
	mustRejectWith(t, f, `=CHISQ.DIST(0.5,3)`, "CHISQ.DIST requires 3 arguments")
	mustRejectWith(t, f, `=CHISQ.INV(0.5)`, "CHISQ.INV requires 2 numeric arguments")
}

func TestTheExponentialSpellingsAgree(t *testing.T) {
	f := seedcalc.NewFile()
	agree(t, f, `=EXPON.DIST(0.5,1,TRUE)`, `=EXPONDIST(0.5,1,TRUE)`, "0.393469340287367")
	agree(t, f, `=EXPON.DIST(0.5,1,FALSE)`, `=EXPONDIST(0.5,1,FALSE)`, "0.606530659712633")
}

func TestTheFSpellingsSplitTheTails(t *testing.T) {
	f := seedcalc.NewFile()
	agree(t, f, `=F.DIST.RT(1,2,3)`, `=FDIST(1,2,3)`, "0.46475800154489")
	mustEval(t, f, `=F.DIST(1,2,3,TRUE)`, "0.53524199845511")
	agree(t, f, `=F.INV.RT(0.5,2,3)`, `=FINV(0.5,2,3)`, "0.881101577952299")
	mustEval(t, f, `=F.INV(0.5,2,3)`, "0.881101577952299")
	mustRejectWith(t, f, `=F.DIST(1,2,3)`, "F.DIST requires 4 arguments")
}

func TestTheGammaSpellingsAgree(t *testing.T) {
	f := seedcalc.NewFile()
	agree(t, f, `=GAMMA.DIST(6,9,2,TRUE)`, `=GAMMADIST(6,9,2,TRUE)`, "0.00380299206167596")
	agree(t, f, `=GAMMA.INV(0.5,9,2)`, `=GAMMAINV(0.5,9,2)`, "17.3379023687408")
	// GAMMALN.PRECISE is the log of the gamma function; ln(3!) is ln 6.
	mustEval(t, f, `=GAMMALN.PRECISE(4)`, "1.79175946922805")
	mustRejectWith(t, f, `=GAMMALN.PRECISE(0)`, "#NUM!")
}

func TestTheHypergeometricAndLognormalSpellingsAgree(t *testing.T) {
	f := seedcalc.NewFile()
	// HYPGEOM.DIST takes the cumulative flag the older spelling lacks.
	agree(t, f, `=HYPGEOM.DIST(1,4,8,20,FALSE)`, `=HYPGEOMDIST(1,4,8,20)`, "0.363261093911249")
	mustEval(t, f, `=HYPGEOM.DIST(1,4,8,20,TRUE)`, "0.465428276573787")
	agree(t, f, `=LOGNORM.DIST(1,1,2,TRUE)`, `=LOGNORMDIST(1,1,2)`, "0.308537538725987")
	agree(t, f, `=LOGNORM.INV(0.5,1,2)`, `=LOGINV(0.5,1,2)`, "2.71828182845905")
}

func TestTheNegativeBinomialSpellingsAgree(t *testing.T) {
	f := seedcalc.NewFile()
	agree(t, f, `=NEGBINOM.DIST(3,5,0.3,FALSE)`, `=NEGBINOMDIST(3,5,0.3)`, "0.02917215")
	mustEval(t, f, `=NEGBINOM.DIST(3,5,0.3,TRUE)`, "0.05796765")
}

func TestTheStudentTSpellingsSplitTheTails(t *testing.T) {
	f := seedcalc.NewFile()
	// TDIST's tail count becomes three separate names.
	agree(t, f, `=T.DIST.RT(1,10)`, `=TDIST(1,10,1)`, "0.17044656615103")
	agree(t, f, `=T.DIST.2T(1,10)`, `=TDIST(1,10,2)`, "0.34089313230206")
	mustEval(t, f, `=T.DIST(1,10,TRUE)`, "0.82955343384897")
	// TINV is the two-tailed inverse, so T.INV.2T matches it and T.INV,
	// which is one-tailed, gives the median instead.
	agree(t, f, `=T.INV.2T(0.5,10)`, `=TINV(0.5,10)`, "0.699812061312432")
	mustEval(t, f, `=T.INV(0.5,10)`, "0")
	mustRejectWith(t, f, `=T.INV.2T(0,10)`, "#NUM!")
}

func TestTheConfidenceIntervalHalfWidth(t *testing.T) {
	f := seedcalc.NewFile()
	// CONFIDENCE.T uses the t distribution rather than the normal one.
	mustEval(t, f, `=CONFIDENCE.T(0.05,1,10)`, "0.715356905970665")
	mustRejectWith(t, f, `=CONFIDENCE.T(0.05,1)`, "CONFIDENCE.T requires 3 arguments")
}
