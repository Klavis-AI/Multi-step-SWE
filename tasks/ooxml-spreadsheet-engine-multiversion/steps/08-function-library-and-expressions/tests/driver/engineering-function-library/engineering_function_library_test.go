// Scored group: engineering-function-library.
//
// The engineering family this milestone adds: conversion between the binary,
// octal, decimal and hexadecimal radices; the bitwise operators; the complex
// number constructor, its accessors, its arithmetic and its transcendental
// functions; and the modified and unmodified Bessel functions. Semantics follow
// OpenDocument v1.3 part 4 (OpenFormula) section 6.8 "Complex Number
// Functions" and section 6.19 "Number Representation Conversion Functions"
// -- 6.19, not 6.9, which is Database Functions --
// with this checkpoint's departures from that text asserted where they occur.
package engineeringfunctionlibrary

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

func TestBinaryConversionsAndTheirTwosComplementRange(t *testing.T) {
	f := seedcalc.NewFile()
	mustEval(t, f, `=BIN2DEC("1010")`, "10")
	mustEval(t, f, `=BIN2OCT("1010")`, "12")
	mustEval(t, f, `=BIN2HEX("1010")`, "A")
	// The optional second argument pads the result to a fixed width.
	mustEval(t, f, `=BIN2HEX("1010",4)`, "000A")
	// The binary operand is ten bits in two's complement, so a leading 1 in the
	// tenth position is a negative value, not a large positive one.
	mustEval(t, f, `=BIN2DEC("1111111111")`, "-1")
	// A digit outside the radix is refused.
	mustReject(t, f, `=BIN2DEC("2")`)
}

func TestDecimalConversionsAndTheirDomain(t *testing.T) {
	f := seedcalc.NewFile()
	mustEval(t, f, "=DEC2BIN(10)", "1010")
	mustEval(t, f, "=DEC2HEX(255)", "FF")
	mustEval(t, f, "=DEC2OCT(8)", "10")
	mustEval(t, f, "=DEC2BIN(10,8)", "00001010")
	// DEC2BIN's result is that same ten-bit two's complement field, so its
	// argument is bounded at -512..511 on both sides.
	mustReject(t, f, "=DEC2BIN(512)")
	mustReject(t, f, "=DEC2BIN(-513)")
}

func TestHexadecimalAndOctalConversions(t *testing.T) {
	f := seedcalc.NewFile()
	mustEval(t, f, `=HEX2DEC("FF")`, "255")
	mustEval(t, f, `=HEX2BIN("A")`, "1010")
	mustEval(t, f, `=HEX2OCT("A")`, "12")
	mustEval(t, f, `=OCT2DEC("17")`, "15")
	mustEval(t, f, `=OCT2BIN("7")`, "111")
	mustEval(t, f, `=OCT2HEX("17")`, "F")
	// A digit outside the hexadecimal alphabet is refused.
	mustReject(t, f, `=HEX2DEC("ZZ")`)
	// Deviation worth reproducing exactly: OCT2DEC does not validate its
	// operand's digits. The digit 8 is not octal, but it is accepted and
	// carried through as the decimal 8 rather than rejected.
	mustEval(t, f, `=OCT2DEC("8")`, "8")
}

func TestTheBitwiseOperators(t *testing.T) {
	f := seedcalc.NewFile()
	// 12 is 1100, 10 is 1010.
	mustEval(t, f, "=BITAND(12,10)", "8")
	mustEval(t, f, "=BITOR(12,10)", "14")
	mustEval(t, f, "=BITXOR(12,10)", "6")
	mustEval(t, f, "=BITLSHIFT(3,2)", "12")
	mustEval(t, f, "=BITRSHIFT(12,2)", "3")
	// The operands are non-negative integers; a negative one is out of domain.
	mustReject(t, f, "=BITAND(-1,1)")
}

func TestCOMPLEXBuildsTheTextualForm(t *testing.T) {
	f := seedcalc.NewFile()
	mustEval(t, f, "=COMPLEX(3,4)", "3+4i")
	// The optional suffix selects the imaginary unit's name.
	mustEval(t, f, `=COMPLEX(3,4,"j")`, "3+4j")
	// A zero part is dropped from the rendered form rather than written out.
	mustEval(t, f, "=COMPLEX(3,0)", "3")
	mustEval(t, f, "=COMPLEX(0,1)", "1i")
	// Only i and j are suffixes.
	mustReject(t, f, `=COMPLEX(3,4,"x")`)
}

func TestTheComplexAccessors(t *testing.T) {
	f := seedcalc.NewFile()
	mustEval(t, f, `=IMABS("3+4i")`, "5")
	mustEval(t, f, `=IMREAL("3+4i")`, "3")
	mustEval(t, f, `=IMAGINARY("3+4i")`, "4")
	mustEval(t, f, `=IMCONJUGATE("3+4i")`, "3-4i")
	mustEval(t, f, `=IMARGUMENT("3+4i")`, "0.927295218001612")
	// A string that is not a complex literal is refused.
	mustReject(t, f, `=IMABS("notacomplex")`)
}

func TestComplexArithmetic(t *testing.T) {
	f := seedcalc.NewFile()
	mustEval(t, f, `=IMSUM("1+2i","3+4i")`, "4+6i")
	mustEval(t, f, `=IMSUB("3+4i","1+2i")`, "2+2i")
	mustEval(t, f, `=IMPRODUCT("1+2i","3+4i")`, "-5+10i")
	mustEval(t, f, `=IMDIV("3+4i","1+2i")`, "2.2-0.4i")
	mustEval(t, f, `=IMPOWER("2",2)`, "4")
	mustEval(t, f, `=IMSQRT("4")`, "2")
}

func TestComplexExponentialsAndLogarithms(t *testing.T) {
	f := seedcalc.NewFile()
	mustEval(t, f, `=IMEXP("0")`, "1")
	mustEval(t, f, `=IMLN("1")`, "0")
	mustEval(t, f, `=IMLOG10("10")`, "1")
	mustEval(t, f, `=IMLOG2("2")`, "1")
}

func TestComplexTrigonometryAndItsPoles(t *testing.T) {
	f := seedcalc.NewFile()
	mustEval(t, f, `=IMSIN("0")`, "0")
	mustEval(t, f, `=IMCOS("0")`, "1")
	mustEval(t, f, `=IMTAN("0")`, "0")
	mustEval(t, f, `=IMSEC("0")`, "1")
	mustEval(t, f, `=IMSINH("0")`, "0")
	mustEval(t, f, `=IMCOSH("0")`, "1")
	mustEval(t, f, `=IMSECH("0")`, "1")
	// The two reciprocal functions with a pole at the origin are refused there
	// rather than returning an infinity.
	mustReject(t, f, `=IMCSC("0")`)
	mustReject(t, f, `=IMCSCH("0")`)
}

func TestTheBesselFunctions(t *testing.T) {
	f := seedcalc.NewFile()
	// Order zero at x = 1, modified and unmodified.
	mustEval(t, f, "=BESSELI(1,0)", "1.266065877752008")
	mustEval(t, f, "=BESSELJ(1,0)", "0.765197686557967")
}
