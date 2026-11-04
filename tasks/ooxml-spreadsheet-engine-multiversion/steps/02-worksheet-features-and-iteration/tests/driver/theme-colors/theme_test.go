// Scored group: theme-colors.
//
// Colour conversion between the RGB model the file format stores and the HSL
// model tinting is expressed in. Graded against ECMA-376 Part 1 clause
// 18.3.1.15 (color, whose rgb attribute is an ARGB value) and clause 18.8.19
// (fgColor/@tint, whose tint is applied by converting to HSL, scaling the
// luminance and converting back). Behaviour cases were checked against the
// upstream release this checkpoint tracks (tag v1.4.1).
package themecolors

import (
	"image/color"
	"math"
	"testing"

	seedcalc "__SEEDCALC_MODULE__"
)

func nearly(a, b float64) bool { return math.Abs(a-b) < 1e-9 }

func TestRGBToHSLAndBackIsLossless(t *testing.T) {
	for _, c := range []struct{ r, g, b uint8 }{
		{255, 0, 0},
		{0, 255, 0},
		{0, 0, 255},
		{0, 0, 0},
		{255, 255, 255},
		{18, 52, 86},
		{200, 100, 50},
	} {
		h, s, l := seedcalc.RGBToHSL(c.r, c.g, c.b)
		if h < 0 || h > 1 {
			t.Errorf("RGBToHSL(%d,%d,%d) hue = %v, want it in 0..1", c.r, c.g, c.b, h)
		}
		if s < 0 || s > 1 {
			t.Errorf("RGBToHSL(%d,%d,%d) saturation = %v, want it in 0..1", c.r, c.g, c.b, s)
		}
		if l < 0 || l > 1 {
			t.Errorf("RGBToHSL(%d,%d,%d) luminance = %v, want it in 0..1", c.r, c.g, c.b, l)
		}
		r, g, b := seedcalc.HSLToRGB(h, s, l)
		if r != c.r || g != c.g || b != c.b {
			t.Errorf("HSLToRGB(RGBToHSL(%d,%d,%d)) = (%d,%d,%d), want the original", c.r, c.g, c.b, r, g, b)
		}
	}
}

func TestPrimaryAndAchromaticColoursHaveTheExpectedHSL(t *testing.T) {
	h, s, l := seedcalc.RGBToHSL(255, 0, 0)
	if !nearly(h, 0) || !nearly(s, 1) || !nearly(l, 0.5) {
		t.Errorf("RGBToHSL(255,0,0) = (%v,%v,%v), want (0,1,0.5)", h, s, l)
	}
	h, s, l = seedcalc.RGBToHSL(0, 255, 0)
	if !nearly(h, 1.0/3.0) || !nearly(s, 1) || !nearly(l, 0.5) {
		t.Errorf("RGBToHSL(0,255,0) = (%v,%v,%v), want (1/3,1,0.5)", h, s, l)
	}
	h, s, l = seedcalc.RGBToHSL(128, 128, 128)
	if !nearly(h, 0) || !nearly(s, 0) {
		t.Errorf("RGBToHSL on a grey = (%v,%v,%v), want hue 0 and saturation 0", h, s, l)
	}
	if l <= 0 || l >= 1 {
		t.Errorf("RGBToHSL on a mid grey gave luminance %v, want it strictly between 0 and 1", l)
	}
	if _, _, l := seedcalc.RGBToHSL(0, 0, 0); !nearly(l, 0) {
		t.Errorf("black has luminance %v, want 0", l)
	}
	if _, _, l := seedcalc.RGBToHSL(255, 255, 255); !nearly(l, 1) {
		t.Errorf("white has luminance %v, want 1", l)
	}
}

func TestHSLRGBAMatchesTheEquivalentRGB(t *testing.T) {
	r, g, b, a := seedcalc.HSL{H: 0, S: 1, L: 0.5}.RGBA()
	if r != 0xffff || g != 0 || b != 0 || a != 0xffff {
		t.Errorf("HSL{0,1,0.5}.RGBA() = (%d,%d,%d,%d), want (65535,0,0,65535)", r, g, b, a)
	}
	r, g, b, a = seedcalc.HSL{H: 0, S: 0, L: 0}.RGBA()
	if r != 0 || g != 0 || b != 0 || a != 0xffff {
		t.Errorf("black HSL.RGBA() = (%d,%d,%d,%d), want (0,0,0,65535)", r, g, b, a)
	}
	r, g, b, a = seedcalc.HSL{H: 0, S: 0, L: 1}.RGBA()
	if r != 0xffff || g != 0xffff || b != 0xffff || a != 0xffff {
		t.Errorf("white HSL.RGBA() = (%d,%d,%d,%d), want all channels at 65535", r, g, b, a)
	}
}

func TestHSLModelConvertsAnRGBAColour(t *testing.T) {
	converted := seedcalc.HSLModel.Convert(color.RGBA{R: 0, G: 255, B: 0, A: 255})
	hsl, ok := converted.(seedcalc.HSL)
	if !ok {
		t.Fatalf("HSLModel.Convert returned %T, want seedcalc.HSL", converted)
	}
	if !nearly(hsl.H, 1.0/3.0) || !nearly(hsl.S, 1) || !nearly(hsl.L, 0.5) {
		t.Errorf("HSLModel.Convert(green) = %+v, want H=1/3 S=1 L=0.5", hsl)
	}
	again := seedcalc.HSLModel.Convert(hsl)
	if got, ok := again.(seedcalc.HSL); !ok || got != hsl {
		t.Errorf("converting an HSL colour again gave %v, want it unchanged", again)
	}
}

func TestThemeColorAppliesTheTint(t *testing.T) {
	if got := seedcalc.ThemeColor("000000", 0); got != "FF000000" {
		t.Errorf("ThemeColor with no tint = %q, want the base colour with an opaque alpha %q", got, "FF000000")
	}
	if got := seedcalc.ThemeColor("FF0000", 0); got != "FFFF0000" {
		t.Errorf("ThemeColor with no tint = %q, want %q", got, "FFFF0000")
	}
	if got := seedcalc.ThemeColor("000000", 0.5); got != "FF808080" {
		t.Errorf("ThemeColor(\"000000\", 0.5) = %q, want %q -- a positive tint lightens", got, "FF808080")
	}
	if got := seedcalc.ThemeColor("FFFFFF", -0.5); got != "FF808080" {
		t.Errorf("ThemeColor(\"FFFFFF\", -0.5) = %q, want %q -- a negative tint darkens", got, "FF808080")
	}
	if got := len(seedcalc.ThemeColor("123456", 0.25)); got != 8 {
		t.Errorf("a tinted theme colour is %d characters long, want 8 (AARRGGBB)", got)
	}
}
