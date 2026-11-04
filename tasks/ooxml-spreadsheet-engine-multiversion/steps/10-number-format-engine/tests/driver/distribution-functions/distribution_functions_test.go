// Scored group: distribution-functions.
//
// The probability distributions under the spellings they were given before
// the 2010 renaming: the beta, binomial, chi-squared, exponential, F, gamma,
// hypergeometric, lognormal, negative binomial and Student's t families,
// each with its cumulative form and, where one exists, its inverse. The
// dotted spellings of the same distributions are a separate group.
//
// Semantics follow OpenDocument v1.3 part 4 (OpenFormula) section 6.18,
// "Statistical Functions".
package distributionfunctions

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

func TestTheBetaDistribution(t *testing.T) {
	f := seedcalc.NewFile()
	mustEval(t, f, `=BETADIST(0.4,4,5)`, "0.4059136")
	mustEval(t, f, `=BETAINV(0.2,4,5)`, "0.303225844664082")
	// x must lie inside the support, which defaults to the unit interval.
	mustRejectWith(t, f, `=BETADIST(2,4,5)`, "#NUM!")
	mustRejectWith(t, f, `=BETAINV(2,4,5)`, "#NUM!")
	mustRejectWith(t, f, `=BETADIST(0.4,4)`, "BETADIST requires at least 3 arguments")
	mustRejectWith(t, f, `=BETAINV(0.2,4)`, "BETAINV requires at least 3 arguments")
}

func TestTheBinomialDistribution(t *testing.T) {
	f := seedcalc.NewFile()
	// The fourth argument chooses the mass function or the cumulative one.
	mustEval(t, f, `=BINOMDIST(3,10,0.3,FALSE)`, "0.266827932")
	mustEval(t, f, `=BINOMDIST(3,10,0.3,TRUE)`, "0.6496107184")
	// More successes than trials is out of range.
	mustRejectWith(t, f, `=BINOMDIST(11,10,0.3,FALSE)`, "#NUM!")
	mustRejectWith(t, f, `=BINOMDIST(3,10,0.3)`, "BINOMDIST requires 4 arguments")
	// CRITBINOM inverts the cumulative form over the integers.
	mustEval(t, f, `=CRITBINOM(10,0.3,0.5)`, "3")
	mustRejectWith(t, f, `=CRITBINOM(-1,0.3,0.5)`, "#NUM!")
	mustRejectWith(t, f, `=CRITBINOM(10,0.3)`, "CRITBINOM requires 3 numeric arguments")
}

func TestTheChiSquaredInverse(t *testing.T) {
	f := seedcalc.NewFile()
	// CHIDIST predates this milestone; its inverse arrives with it, and the
	// two are checked together so the pair is shown to agree.
	mustEval(t, f, `=CHIDIST(0.5,3)`, "0.918891411654676")
	// Round-tripping the value back through the inverse lands on the input
	// to the last digit the fifteen-significant-figure rendering keeps.
	mustEval(t, f, `=CHIINV(0.918891411654676,3)`, "0.499999999999999")
	mustEval(t, f, `=CHIINV(0.5,3)`, "2.36597388437534")
	// The probability argument is a probability.
	mustRejectWith(t, f, `=CHIINV(2,3)`, "#NUM!")
	mustRejectWith(t, f, `=CHIINV(0.5)`, "CHIINV requires 2 numeric arguments")
}

func TestTheExponentialDistribution(t *testing.T) {
	f := seedcalc.NewFile()
	mustEval(t, f, `=EXPONDIST(0.5,1,TRUE)`, "0.393469340287367")
	mustEval(t, f, `=EXPONDIST(0.5,1,FALSE)`, "0.606530659712633")
	// The support starts at zero.
	mustRejectWith(t, f, `=EXPONDIST(-1,1,TRUE)`, "#NUM!")
	mustRejectWith(t, f, `=EXPONDIST(0.5,1)`, "EXPONDIST requires 3 arguments")
}

func TestTheFDistribution(t *testing.T) {
	f := seedcalc.NewFile()
	// FDIST is the right-tailed form, so it and its inverse are a pair.
	mustEval(t, f, `=FDIST(1,2,3)`, "0.46475800154489")
	mustEval(t, f, `=FINV(0.5,2,3)`, "0.881101577952299")
	mustEval(t, f, `=FINV(0.46475800154489,2,3)`, "1")
	mustRejectWith(t, f, `=FDIST(-1,2,3)`, "#NUM!")
	mustRejectWith(t, f, `=FDIST(1,2)`, "FDIST requires 3 arguments")
	mustRejectWith(t, f, `=FINV(0.5,2)`, "FINV requires 3 arguments")
}

func TestTheGammaDistribution(t *testing.T) {
	f := seedcalc.NewFile()
	mustEval(t, f, `=GAMMADIST(6,9,2,TRUE)`, "0.00380299206167596")
	mustEval(t, f, `=GAMMADIST(6,9,2,FALSE)`, "0.00405075589734072")
	mustEval(t, f, `=GAMMAINV(0.5,9,2)`, "17.3379023687408")
	mustRejectWith(t, f, `=GAMMADIST(-1,9,2,TRUE)`, "#NUM!")
	mustRejectWith(t, f, `=GAMMADIST(6,9,2)`, "GAMMADIST requires 4 arguments")
	mustRejectWith(t, f, `=GAMMAINV(0.5,9)`, "GAMMAINV requires 3 arguments")
}

func TestTheHypergeometricDistribution(t *testing.T) {
	f := seedcalc.NewFile()
	// One success in a sample of four, from eight successes in twenty.
	mustEval(t, f, `=HYPGEOMDIST(1,4,8,20)`, "0.363261093911249")
	mustEval(t, f, `=HYPGEOMDIST(0,4,8,20)`, "0.102167182662539")
	mustRejectWith(t, f, `=HYPGEOMDIST(1,4,8)`, "HYPGEOMDIST requires 4 numeric arguments")
}

func TestTheLognormalDistribution(t *testing.T) {
	f := seedcalc.NewFile()
	mustEval(t, f, `=LOGNORMDIST(1,1,2)`, "0.308537538725987")
	// The median of a lognormal with log-mean 1 is e.
	mustEval(t, f, `=LOGINV(0.5,1,2)`, "2.71828182845905")
	mustRejectWith(t, f, `=LOGNORMDIST(-1,1,2)`, "#NUM!")
	mustRejectWith(t, f, `=LOGNORMDIST(1,1)`, "LOGNORMDIST requires 3 arguments")
	mustRejectWith(t, f, `=LOGINV(0.5,1)`, "LOGINV requires 3 arguments")
}

func TestTheNegativeBinomialDistribution(t *testing.T) {
	f := seedcalc.NewFile()
	mustEval(t, f, `=NEGBINOMDIST(3,5,0.3)`, "0.02917215")
	mustEval(t, f, `=NEGBINOMDIST(0,5,0.3)`, "0.00243")
	mustRejectWith(t, f, `=NEGBINOMDIST(3,5)`, "NEGBINOMDIST requires 3 arguments")
}

func TestTheStudentTDistribution(t *testing.T) {
	f := seedcalc.NewFile()
	// The third argument is the number of tails, one or two.
	mustEval(t, f, `=TDIST(1,10,1)`, "0.17044656615103")
	mustEval(t, f, `=TDIST(1,10,2)`, "0.34089313230206")
	mustEval(t, f, `=TINV(0.5,10)`, "0.699812061312432")
	mustRejectWith(t, f, `=TDIST(1,10,3)`, "#NUM!")
	mustRejectWith(t, f, `=TDIST(-1,10,1)`, "#NUM!")
	mustRejectWith(t, f, `=TDIST(1,10)`, "TDIST requires 3 arguments")
	mustRejectWith(t, f, `=TINV(0.5)`, "TINV requires 2 arguments")
}
