/* shaper-use.js -- the Universal Shaping Engine.
 *
 * Implemented from ../otspec/txt/sd-use.txt (Microsoft's script-development
 * chapter for USE) and the Unicode character databases shipped in
 * ../ucd, which between them define every input this engine needs:
 *
 *   - the CLUSTER GRAMMAR of sd-use.txt l.567-700, transcribed below as a
 *     regular grammar and run as a longest-match scanner, which is what the
 *     chapter's "the engine matches the longest valid cluster" means in
 *     operational terms;
 *   - the CHARACTER CLASSES of l.95-540, which are a pure function of
 *     Indic_Syllabic_Category, Indic_Positional_Category, General_Category,
 *     Joining_Type and Default_Ignorable_Code_Point.  They are derived from
 *     those five properties by the rules at the head of USE_TABLE below and
 *     baked into a range table, because the derivation is a fixed function of
 *     a fixed data release and re-deriving it per code point at run time is
 *     pure cost.
 *
 * A NOTE ON WHAT REPLACED WHAT.  The first version of this file read the
 * chapter's prose and matched clusters with a hand-written recursive
 * descent over FLAT classes (one V, one VM, one F) plus a separately
 * computed "position".  That is not the same language: the chapter's tail
 * is  (VPre)* (VAbv)* (VBlw)* (VPst)*  -- an ordered sequence of FOUR
 * distinct classes -- and no amount of watermarking a shared V class
 * reproduces it, because the same character may be VAbv in one script and
 * VBlw in another, and because  VAbv VPre  must FAIL while  VPre VAbv  must
 * succeed.  The measured symptom was the whole missing-dotted-circle class:
 * a defective cluster is exactly one the grammar rejects, so a grammar that
 * accepts too much never marks one broken and never inserts the circle the
 * reference does.  Positional classes are therefore the primary categories
 * here, and the flat questions ("is this some kind of vowel?") are asked as
 * predicates over them.
 *
 * This module deliberately does NOT import shape-plan.js: shape-plan.js
 * imports the shapers, and an ES-module cycle through an `await import` at
 * module scope would leave our bindings uninitialised.  The feature flags are
 * therefore mirrored locally; they must stay in step with shape-plan.js.
 */

import { generalCategory, GC } from './unicode.js';
import { preprocessVowelConstraints, gLit as L, gAny, gSeq as SEQ,
         gAlt as ALT, gStar as STAR, gOpt as OPT, gPlus as PLUS,
         makeScanner, insertDottedCircles, forEachSyllable,
         featureMask as oneMask } from './shaper-indic.js';
import { mergeClusterRange, SCRATCH_HAS_BROKEN_SYLLABLE } from './buffer.js';

/* Mirrors shape-plan.js's F_* -- see the note above. */
const F_NONE = 0, F_GLOBAL = 1, F_MANUAL_ZWNJ = 4, F_MANUAL_ZWJ = 8,
      F_PER_SYLLABLE = 64;

/* Mirrors ot-layout.js's GlyphProps bits, for the same reason. */
const GP_SUBSTITUTED = 0x0010, GP_LIGATED = 0x0020;
/* Mirrors shape-plan.js's unicode_props packing: the low five bits are the
 * general category, cached while `codepoint` was still a character. */
const UPROPS_MASK_GEN_CAT = 0x001F;


/* ------------------------------------------------------------------ */
/*  Character classification -- sd-use.txt l.95-540                     */
/* ------------------------------------------------------------------ */

/* The USE classes.  The positional suffixes (Abv/Blw/Pst/Pre) are part of the
 * class, not a separate axis: l.621-627 sequences them by position, so the
 * grammar needs them as distinct terminals.  The numbering is the order the
 * classes appear in l.95-540 with the positional families grouped; nothing
 * outside this file depends on the values. */
export const U = {
  O: 0, B: 1, N: 4, GB: 5, CGJ: 6, SUB: 11, H: 12, HN: 13, ZWNJ: 14, WJ: 16,
  R: 18, CS: 43, IS: 44, Sk: 48, G: 49, J: 50, SB: 51, SE: 52, HVM: 53,
  HM: 54, HR: 55, RK: 56,
  FAbv: 24, FBlw: 25, FPst: 26,
  MAbv: 27, MBlw: 28, MPst: 29, MPre: 30,
  CMAbv: 31, CMBlw: 32,
  VAbv: 33, VBlw: 34, VPst: 35, VPre: 22,
  VMAbv: 37, VMBlw: 38, VMPst: 39, VMPre: 23,
  SMAbv: 41, SMBlw: 42,
  FMAbv: 45, FMBlw: 46, FMPst: 47,
};

/* Kept under its old name because the module used to export it; `C` is now an
 * alias of the positional table.  Nothing outside this file reads it. */
export const C = U;

/* USE_TABLE -- the class of every code point that has one, as
 * start, end, class triples in ascending order.  Derived from the five
 * properties named at the head of this file by the rules of sd-use.txt
 * l.95-540 (BASE is a Number / Consonant / Consonant_Head_Letter /
 * Tone_Letter / Vowel_Independent, or an Arabic-joining letter, or a
 * UGC = Lo character whose Indic_Syllabic_Category would otherwise make it a
 * mark; HALANT is a Virama; SAKOT is U+1A60; and so on), then split by
 * Indic_Positional_Category into the four cardinal positions for the F, M,
 * CM, V, VM, SM and FM families.  Scripts with their own shaper (Arabic,
 * Lao, Samaritan, Syriac, Thai) are omitted and fall to OTHER, as USE never
 * sees them.  Absent code points are OTHER. */
const USE_TABLE = [
  0x2D,0x2D,5, 0x30,0x39,1, 0x5B,0x5B,51, 0x5D,0x5D,52, 0x7B,0x7B,51,
  0x7D,0x7D,52, 0xA0,0xA0,5, 0xAD,0xAD,16, 0xB2,0xB3,47, 0xD7,0xD7,5,
  0x34F,0x34F,6, 0x640,0x640,1, 0x7CA,0x7EA,1, 0x7EB,0x7F3,37, 0x7FA,0x7FA,1,
  0x7FD,0x7FD,37, 0x840,0x858,1, 0x859,0x85B,32, 0x900,0x902,37,
  0x903,0x903,39, 0x904,0x939,1, 0x93A,0x93A,33, 0x93B,0x93B,35,
  0x93C,0x93C,32, 0x93D,0x93D,1, 0x93E,0x93E,35, 0x93F,0x93F,22,
  0x940,0x940,35, 0x941,0x944,34, 0x945,0x948,33, 0x949,0x94C,35,
  0x94D,0x94D,12, 0x94E,0x94E,22, 0x94F,0x94F,35, 0x951,0x951,37,
  0x952,0x952,38, 0x955,0x955,33, 0x956,0x957,34, 0x958,0x961,1,
  0x962,0x963,34, 0x966,0x96F,1, 0x972,0x97F,1, 0x980,0x980,5,
  0x981,0x981,37, 0x982,0x983,39, 0x985,0x98C,1, 0x98F,0x990,1,
  0x993,0x9A8,1, 0x9AA,0x9B0,1, 0x9B2,0x9B2,1, 0x9B6,0x9B9,1, 0x9BC,0x9BC,32,
  0x9BD,0x9BD,1, 0x9BE,0x9BE,35, 0x9BF,0x9BF,22, 0x9C0,0x9C0,35,
  0x9C1,0x9C4,34, 0x9C7,0x9C8,22, 0x9CB,0x9CC,22, 0x9CD,0x9CD,12,
  0x9D7,0x9D7,35, 0x9DC,0x9DD,1, 0x9DF,0x9E1,1, 0x9E2,0x9E3,34,
  0x9E6,0x9F1,1, 0x9FC,0x9FC,1, 0x9FE,0x9FE,45, 0xA01,0xA02,37,
  0xA03,0xA03,39, 0xA05,0xA0A,1, 0xA0F,0xA10,1, 0xA13,0xA28,1, 0xA2A,0xA30,1,
  0xA32,0xA33,1, 0xA35,0xA36,1, 0xA38,0xA39,1, 0xA3C,0xA3C,32,
  0xA3E,0xA3E,35, 0xA3F,0xA3F,22, 0xA40,0xA40,35, 0xA41,0xA42,34,
  0xA47,0xA48,33, 0xA4B,0xA4C,33, 0xA4D,0xA4D,12, 0xA51,0xA51,38,
  0xA59,0xA5C,1, 0xA5E,0xA5E,1, 0xA66,0xA6F,1, 0xA70,0xA70,37,
  0xA71,0xA71,31, 0xA72,0xA73,5, 0xA75,0xA75,28, 0xA81,0xA82,37,
  0xA83,0xA83,39, 0xA85,0xA8D,1, 0xA8F,0xA91,1, 0xA93,0xAA8,1, 0xAAA,0xAB0,1,
  0xAB2,0xAB3,1, 0xAB5,0xAB9,1, 0xABC,0xABC,32, 0xABD,0xABD,1,
  0xABE,0xABE,35, 0xABF,0xABF,22, 0xAC0,0xAC0,35, 0xAC1,0xAC4,34,
  0xAC5,0xAC5,33, 0xAC7,0xAC9,33, 0xACB,0xACC,35, 0xACD,0xACD,12,
  0xAE0,0xAE1,1, 0xAE2,0xAE3,34, 0xAE6,0xAEF,1, 0xAF9,0xAF9,1,
  0xAFA,0xAFA,37, 0xAFB,0xAFB,31, 0xAFC,0xAFC,37, 0xAFD,0xAFF,31,
  0xB01,0xB01,37, 0xB02,0xB03,39, 0xB05,0xB0C,1, 0xB0F,0xB10,1,
  0xB13,0xB28,1, 0xB2A,0xB30,1, 0xB32,0xB33,1, 0xB35,0xB39,1, 0xB3C,0xB3C,32,
  0xB3D,0xB3D,1, 0xB3E,0xB3E,35, 0xB3F,0xB3F,33, 0xB40,0xB40,35,
  0xB41,0xB44,34, 0xB47,0xB48,22, 0xB4B,0xB4C,22, 0xB4D,0xB4D,12,
  0xB55,0xB57,33, 0xB5C,0xB5D,1, 0xB5F,0xB61,1, 0xB62,0xB63,34,
  0xB66,0xB6F,1, 0xB71,0xB71,1, 0xB82,0xB82,37, 0xB85,0xB8A,1, 0xB8E,0xB90,1,
  0xB92,0xB95,1, 0xB99,0xB9A,1, 0xB9C,0xB9C,1, 0xB9E,0xB9F,1, 0xBA3,0xBA4,1,
  0xBA8,0xBAA,1, 0xBAE,0xBB9,1, 0xBBE,0xBBF,35, 0xBC0,0xBC0,33,
  0xBC1,0xBC2,35, 0xBC6,0xBC8,22, 0xBCA,0xBCC,22, 0xBCD,0xBCD,12,
  0xBD7,0xBD7,35, 0xBE6,0xBEF,1, 0xC00,0xC00,37, 0xC01,0xC03,39,
  0xC04,0xC04,37, 0xC05,0xC0C,1, 0xC0E,0xC10,1, 0xC12,0xC28,1, 0xC2A,0xC39,1,
  0xC3C,0xC3C,32, 0xC3D,0xC3D,1, 0xC3E,0xC40,33, 0xC41,0xC44,35,
  0xC46,0xC48,33, 0xC4A,0xC4C,33, 0xC4D,0xC4D,12, 0xC55,0xC55,33,
  0xC56,0xC56,34, 0xC58,0xC5A,1, 0xC60,0xC61,1, 0xC62,0xC63,34,
  0xC66,0xC6F,1, 0xC80,0xC80,1, 0xC81,0xC81,37, 0xC82,0xC83,39,
  0xC85,0xC8C,1, 0xC8E,0xC90,1, 0xC92,0xCA8,1, 0xCAA,0xCB3,1, 0xCB5,0xCB9,1,
  0xCBC,0xCBC,32, 0xCBD,0xCBD,1, 0xCBE,0xCBE,35, 0xCBF,0xCC0,33,
  0xCC1,0xCC4,35, 0xCC6,0xCC8,33, 0xCCA,0xCCC,33, 0xCCD,0xCCD,12,
  0xCD5,0xCD6,35, 0xCDE,0xCDE,1, 0xCE0,0xCE1,1, 0xCE2,0xCE3,34,
  0xCE6,0xCEF,1, 0xCF1,0xCF2,43, 0xCF3,0xCF3,39, 0xD00,0xD01,37,
  0xD02,0xD03,39, 0xD04,0xD0C,1, 0xD0E,0xD10,1, 0xD12,0xD3A,1,
  0xD3B,0xD3C,33, 0xD3D,0xD3D,1, 0xD3E,0xD40,35, 0xD41,0xD44,34,
  0xD46,0xD48,22, 0xD4A,0xD4C,22, 0xD4D,0xD4D,12, 0xD4E,0xD4E,18,
  0xD57,0xD57,35, 0xD5F,0xD61,1, 0xD62,0xD63,34, 0xD66,0xD6F,1,
  0xD81,0xD81,37, 0xD82,0xD83,39, 0xD85,0xD96,1, 0xD9A,0xDB1,1,
  0xDB3,0xDBB,1, 0xDBD,0xDBD,1, 0xDC0,0xDC6,1, 0xDCA,0xDCA,53,
  0xDCF,0xDD1,35, 0xDD2,0xDD3,33, 0xDD4,0xDD4,34, 0xDD6,0xDD6,34,
  0xDD8,0xDD8,35, 0xDD9,0xDDE,22, 0xDDF,0xDDF,35, 0xDE6,0xDEF,1,
  0xDF2,0xDF3,35, 0xF00,0xF01,1, 0xF04,0xF06,1, 0xF18,0xF19,34,
  0xF20,0xF33,1, 0xF35,0xF35,46, 0xF37,0xF37,46, 0xF39,0xF39,31,
  0xF3E,0xF3E,35, 0xF3F,0xF3F,22, 0xF40,0xF47,1, 0xF49,0xF6C,1,
  0xF71,0xF71,32, 0xF72,0xF72,34, 0xF73,0xF74,33, 0xF75,0xF75,34,
  0xF76,0xF79,33, 0xF7A,0xF7D,34, 0xF7E,0xF7E,37, 0xF80,0xF80,34,
  0xF81,0xF81,33, 0xF82,0xF83,37, 0xF84,0xF84,34, 0xF86,0xF87,37,
  0xF88,0xF8C,1, 0xF8D,0xF97,11, 0xF99,0xFBC,11, 0xFC6,0xFC6,46,
  0x1000,0x102A,1, 0x102B,0x102C,35, 0x102D,0x102E,33, 0x102F,0x1030,34,
  0x1031,0x1031,22, 0x1032,0x1035,33, 0x1036,0x1036,37, 0x1037,0x1037,38,
  0x1038,0x1038,39, 0x1039,0x1039,44, 0x103A,0x103A,33, 0x103B,0x103B,29,
  0x103C,0x103C,30, 0x103D,0x103E,28, 0x103F,0x1049,1, 0x104B,0x104B,5,
  0x104E,0x104E,5, 0x1050,0x1055,1, 0x1056,0x1057,35, 0x1058,0x1059,34,
  0x105A,0x105D,1, 0x105E,0x1060,28, 0x1061,0x1061,1, 0x1062,0x1062,35,
  0x1063,0x1064,39, 0x1065,0x1066,1, 0x1067,0x1068,35, 0x1069,0x106D,39,
  0x106E,0x1070,1, 0x1071,0x1074,33, 0x1075,0x1081,1, 0x1082,0x1082,28,
  0x1083,0x1083,35, 0x1084,0x1084,22, 0x1085,0x1086,33, 0x1087,0x108C,39,
  0x108D,0x108D,38, 0x108E,0x108E,1, 0x108F,0x108F,39, 0x1090,0x1099,1,
  0x109A,0x109B,39, 0x109C,0x109C,35, 0x109D,0x109D,33, 0x1700,0x1711,1,
  0x1712,0x1712,33, 0x1713,0x1714,34, 0x1715,0x1715,35, 0x171F,0x1731,1,
  0x1732,0x1732,33, 0x1733,0x1733,34, 0x1734,0x1734,35, 0x1740,0x1751,1,
  0x1752,0x1752,33, 0x1753,0x1753,34, 0x1760,0x176C,1, 0x176E,0x1770,1,
  0x1772,0x1772,33, 0x1773,0x1773,34, 0x1780,0x17B3,1, 0x17B4,0x17B5,6,
  0x17B6,0x17B6,35, 0x17B7,0x17BA,33, 0x17BB,0x17BD,34, 0x17BE,0x17C5,22,
  0x17C6,0x17C6,37, 0x17C7,0x17C7,39, 0x17C8,0x17C8,35, 0x17C9,0x17CA,37,
  0x17CB,0x17CB,45, 0x17CC,0x17CC,24, 0x17CD,0x17CD,31, 0x17CE,0x17CE,45,
  0x17CF,0x17CF,37, 0x17D0,0x17D0,45, 0x17D1,0x17D1,33, 0x17D2,0x17D2,44,
  0x17D3,0x17D3,45, 0x17DC,0x17DC,1, 0x17DD,0x17DD,45, 0x17E0,0x17E9,1,
  0x1800,0x1800,1, 0x1807,0x1807,1, 0x180A,0x180A,1, 0x180B,0x180D,6,
  0x180E,0x180E,16, 0x180F,0x180F,6, 0x1820,0x1878,1, 0x1880,0x1884,5,
  0x1885,0x1886,31, 0x1887,0x18A8,1, 0x18A9,0x18A9,32, 0x18AA,0x18AA,1,
  0x1900,0x191E,1, 0x1920,0x1921,33, 0x1922,0x1922,34, 0x1923,0x1924,35,
  0x1925,0x1928,33, 0x1929,0x192B,11, 0x1930,0x1931,26, 0x1932,0x1932,38,
  0x1933,0x1938,26, 0x1939,0x1939,25, 0x193A,0x193A,37, 0x193B,0x193B,46,
  0x1946,0x196D,1, 0x1970,0x1974,1, 0x1980,0x19AB,1, 0x19B0,0x19C7,1,
  0x19C8,0x19C9,39, 0x19D0,0x19DA,1, 0x1A00,0x1A16,1, 0x1A17,0x1A18,33,
  0x1A19,0x1A19,22, 0x1A1A,0x1A1A,35, 0x1A1B,0x1A1B,33, 0x1A20,0x1A54,1,
  0x1A55,0x1A55,30, 0x1A56,0x1A56,28, 0x1A57,0x1A57,11, 0x1A58,0x1A59,24,
  0x1A5A,0x1A5A,27, 0x1A5B,0x1A5E,11, 0x1A60,0x1A60,48, 0x1A61,0x1A61,35,
  0x1A62,0x1A62,33, 0x1A63,0x1A64,35, 0x1A65,0x1A68,33, 0x1A69,0x1A6A,34,
  0x1A6B,0x1A6B,33, 0x1A6C,0x1A6C,34, 0x1A6D,0x1A6D,35, 0x1A6E,0x1A72,22,
  0x1A73,0x1A73,33, 0x1A74,0x1A79,37, 0x1A7A,0x1A7A,33, 0x1A7B,0x1A7C,37,
  0x1A7F,0x1A7F,38, 0x1A80,0x1A89,1, 0x1A90,0x1A99,1, 0x1B00,0x1B02,37,
  0x1B03,0x1B03,24, 0x1B04,0x1B04,39, 0x1B05,0x1B33,1, 0x1B34,0x1B34,31,
  0x1B35,0x1B35,35, 0x1B36,0x1B37,33, 0x1B38,0x1B3B,34, 0x1B3C,0x1B3D,33,
  0x1B3E,0x1B41,22, 0x1B42,0x1B43,33, 0x1B44,0x1B44,12, 0x1B45,0x1B4C,1,
  0x1B50,0x1B59,1, 0x1B6B,0x1B73,41, 0x1B80,0x1B80,37, 0x1B81,0x1B81,24,
  0x1B82,0x1B82,39, 0x1B83,0x1BA0,1, 0x1BA1,0x1BA3,11, 0x1BA4,0x1BA4,33,
  0x1BA5,0x1BA5,34, 0x1BA6,0x1BA6,22, 0x1BA7,0x1BA7,35, 0x1BA8,0x1BA9,33,
  0x1BAA,0x1BAA,35, 0x1BAB,0x1BAB,44, 0x1BAC,0x1BAD,11, 0x1BAE,0x1BE5,1,
  0x1BE6,0x1BE6,31, 0x1BE7,0x1BE7,35, 0x1BE8,0x1BE9,33, 0x1BEA,0x1BEC,35,
  0x1BED,0x1BED,33, 0x1BEE,0x1BEE,35, 0x1BEF,0x1BEF,33, 0x1BF0,0x1BF1,24,
  0x1BF2,0x1BF3,56, 0x1C00,0x1C23,1, 0x1C24,0x1C25,11, 0x1C26,0x1C26,35,
  0x1C27,0x1C29,22, 0x1C2A,0x1C2B,35, 0x1C2C,0x1C2C,34, 0x1C2D,0x1C33,24,
  0x1C34,0x1C35,23, 0x1C36,0x1C36,45, 0x1C37,0x1C37,32, 0x1C40,0x1C49,1,
  0x1C4D,0x1C4F,1, 0x1CD0,0x1CD2,37, 0x1CD4,0x1CD9,38, 0x1CDA,0x1CDB,37,
  0x1CDC,0x1CDF,38, 0x1CE0,0x1CE0,37, 0x1CE1,0x1CE1,39, 0x1CE2,0x1CE8,38,
  0x1CED,0x1CED,38, 0x1CF4,0x1CF4,37, 0x1CF5,0x1CF6,43, 0x1CF7,0x1CF7,39,
  0x1CF8,0x1CF9,37, 0x1CFA,0x1CFA,5, 0x1DFB,0x1DFB,45, 0x200B,0x200B,16,
  0x200C,0x200C,14, 0x200D,0x200D,6, 0x200E,0x200F,16, 0x2010,0x2014,5,
  0x202A,0x202E,16, 0x2060,0x206F,16, 0x2074,0x2074,47, 0x2082,0x2084,47,
  0x20F0,0x20F0,37, 0x25CC,0x25CC,1, 0x27E6,0x27E6,51, 0x27E7,0x27E7,52,
  0x27E8,0x27E8,51, 0x27E9,0x27E9,52, 0x2D30,0x2D67,1, 0x2D6F,0x2D6F,1,
  0x2D7F,0x2D7F,12, 0x2E22,0x2E22,51, 0x2E23,0x2E23,52, 0x2E24,0x2E24,51,
  0x2E25,0x2E25,52, 0xA800,0xA801,1, 0xA802,0xA802,33, 0xA803,0xA805,1,
  0xA806,0xA806,12, 0xA807,0xA80A,1, 0xA80B,0xA80B,37, 0xA80C,0xA822,1,
  0xA823,0xA824,35, 0xA825,0xA825,34, 0xA826,0xA826,33, 0xA827,0xA827,35,
  0xA82C,0xA82C,34, 0xA840,0xA873,1, 0xA880,0xA881,39, 0xA882,0xA8B3,1,
  0xA8B4,0xA8B4,29, 0xA8B5,0xA8C3,35, 0xA8C4,0xA8C4,12, 0xA8C5,0xA8C5,37,
  0xA8D0,0xA8D9,1, 0xA8E0,0xA8F1,37, 0xA8F2,0xA8F3,1, 0xA8FE,0xA8FE,1,
  0xA8FF,0xA8FF,33, 0xA900,0xA925,1, 0xA926,0xA92A,33, 0xA92B,0xA92D,38,
  0xA930,0xA946,1, 0xA947,0xA949,34, 0xA94A,0xA94A,33, 0xA94B,0xA94E,34,
  0xA94F,0xA951,24, 0xA952,0xA952,26, 0xA953,0xA953,35, 0xA980,0xA981,37,
  0xA982,0xA982,24, 0xA983,0xA983,39, 0xA984,0xA9B2,1, 0xA9B3,0xA9B3,31,
  0xA9B4,0xA9B5,35, 0xA9B6,0xA9B7,33, 0xA9B8,0xA9B9,34, 0xA9BA,0xA9BB,22,
  0xA9BC,0xA9BC,33, 0xA9BD,0xA9BD,28, 0xA9BE,0xA9BE,29, 0xA9BF,0xA9BF,28,
  0xA9C0,0xA9C0,12, 0xA9D0,0xA9D9,1, 0xA9E0,0xA9E4,1, 0xA9E5,0xA9E5,33,
  0xA9E7,0xA9FE,1, 0xAA00,0xAA28,1, 0xAA29,0xAA29,37, 0xAA2A,0xAA2C,33,
  0xAA2D,0xAA2D,34, 0xAA2E,0xAA2E,33, 0xAA2F,0xAA30,22, 0xAA31,0xAA31,33,
  0xAA32,0xAA32,34, 0xAA33,0xAA33,29, 0xAA34,0xAA34,30, 0xAA35,0xAA35,27,
  0xAA36,0xAA36,28, 0xAA40,0xAA42,1, 0xAA43,0xAA43,24, 0xAA44,0xAA4B,1,
  0xAA4C,0xAA4C,24, 0xAA4D,0xAA4D,26, 0xAA50,0xAA59,1, 0xAA60,0xAA6F,1,
  0xAA71,0xAA73,1, 0xAA74,0xAA76,5, 0xAA7A,0xAA7A,1, 0xAA7B,0xAA7B,39,
  0xAA7C,0xAA7C,37, 0xAA7D,0xAA7D,39, 0xAA7E,0xAAAF,1, 0xAAB0,0xAAB0,34,
  0xAAB1,0xAAB1,1, 0xAAB2,0xAAB3,34, 0xAAB4,0xAAB4,33, 0xAAB5,0xAAB6,1,
  0xAAB7,0xAAB8,34, 0xAAB9,0xAABD,1, 0xAABE,0xAABE,34, 0xAABF,0xAABF,37,
  0xAAC0,0xAAC0,1, 0xAAC1,0xAAC1,37, 0xAAC2,0xAAC2,1, 0xAAE0,0xAAEA,1,
  0xAAEB,0xAAEB,22, 0xAAEC,0xAAEC,34, 0xAAED,0xAAED,33, 0xAAEE,0xAAEE,22,
  0xAAEF,0xAAEF,35, 0xAAF5,0xAAF5,39, 0xAAF6,0xAAF6,44, 0xABC0,0xABE2,1,
  0xABE3,0xABE4,35, 0xABE5,0xABE5,33, 0xABE6,0xABE7,35, 0xABE8,0xABE8,34,
  0xABE9,0xABEA,35, 0xABEC,0xABEC,39, 0xABED,0xABED,34, 0xABF0,0xABF9,1,
  0xFE00,0xFE0F,6, 0xFEFF,0xFEFF,16, 0xFFF0,0xFFF8,16, 0x10570,0x1057A,1,
  0x1057C,0x1058A,1, 0x1058C,0x10592,1, 0x10594,0x10595,1, 0x10597,0x105A1,1,
  0x105A3,0x105B1,1, 0x105B3,0x105B9,1, 0x105BB,0x105BC,1, 0x10A00,0x10A00,1,
  0x10A01,0x10A03,34, 0x10A05,0x10A05,33, 0x10A06,0x10A06,34,
  0x10A0C,0x10A0C,35, 0x10A0D,0x10A0E,38, 0x10A0F,0x10A0F,37,
  0x10A10,0x10A13,1, 0x10A15,0x10A17,1, 0x10A19,0x10A35,1,
  0x10A38,0x10A3A,32, 0x10A3F,0x10A3F,44, 0x10A40,0x10A48,1,
  0x10AC0,0x10AC7,1, 0x10AC9,0x10AE4,1, 0x10AE5,0x10AE6,32,
  0x10AEB,0x10AEF,1, 0x10B80,0x10B91,1, 0x10BA9,0x10BAE,1, 0x10D00,0x10D23,1,
  0x10D24,0x10D26,37, 0x10D27,0x10D27,31, 0x10D30,0x10D39,1,
  0x10D4A,0x10D65,1, 0x10D69,0x10D6D,33, 0x10D6F,0x10D85,1,
  0x10E80,0x10EA9,1, 0x10EAB,0x10EAC,33, 0x10EB0,0x10EB1,1,
  0x10F30,0x10F45,1, 0x10F46,0x10F50,38, 0x10F51,0x10F54,1,
  0x10F70,0x10F81,1, 0x10F82,0x10F85,32, 0x10FB0,0x10FB0,1,
  0x10FB2,0x10FB6,1, 0x10FB8,0x10FBF,1, 0x10FC1,0x10FC4,1, 0x10FC9,0x10FCB,1,
  0x11000,0x11000,39, 0x11001,0x11001,37, 0x11002,0x11002,39,
  0x11003,0x11004,43, 0x11005,0x11037,1, 0x11038,0x1103B,33,
  0x1103C,0x11041,34, 0x11042,0x11045,33, 0x11046,0x11046,12,
  0x11052,0x11065,4, 0x11066,0x1106F,1, 0x11070,0x11070,33,
  0x11071,0x11072,1, 0x11073,0x11074,33, 0x11075,0x11075,1,
  0x1107F,0x1107F,13, 0x11080,0x11081,37, 0x11082,0x11082,39,
  0x11083,0x110AF,1, 0x110B0,0x110B0,35, 0x110B1,0x110B1,22,
  0x110B2,0x110B2,35, 0x110B3,0x110B4,34, 0x110B5,0x110B6,33,
  0x110B7,0x110B8,35, 0x110B9,0x110B9,12, 0x110BA,0x110BA,32,
  0x110C2,0x110C2,34, 0x11100,0x11102,37, 0x11103,0x11126,1,
  0x11127,0x11129,34, 0x1112A,0x1112B,33, 0x1112C,0x1112C,22,
  0x1112D,0x1112D,34, 0x1112E,0x1112F,33, 0x11130,0x11130,34,
  0x11131,0x11132,33, 0x11133,0x11133,44, 0x11134,0x11134,31,
  0x11136,0x1113F,1, 0x11144,0x11144,1, 0x11145,0x11146,35,
  0x11147,0x11147,1, 0x11150,0x11172,1, 0x11173,0x11173,32,
  0x11180,0x11181,37, 0x11182,0x11182,39, 0x11183,0x111B2,1,
  0x111B3,0x111B3,35, 0x111B4,0x111B4,22, 0x111B5,0x111B5,35,
  0x111B6,0x111BB,34, 0x111BC,0x111BF,33, 0x111C0,0x111C0,12,
  0x111C1,0x111C1,1, 0x111C2,0x111C3,18, 0x111C9,0x111C9,46,
  0x111CA,0x111CA,32, 0x111CB,0x111CB,33, 0x111CC,0x111CC,34,
  0x111CE,0x111CE,22, 0x111CF,0x111CF,37, 0x111D0,0x111DA,1,
  0x111E1,0x111F4,1, 0x11200,0x11211,1, 0x11213,0x1122B,1,
  0x1122C,0x1122E,35, 0x1122F,0x1122F,34, 0x11230,0x11233,33,
  0x11234,0x11234,37, 0x11235,0x11235,12, 0x11236,0x11237,31,
  0x1123E,0x1123E,37, 0x1123F,0x11240,1, 0x11241,0x11241,34,
  0x11280,0x11286,1, 0x11288,0x11288,1, 0x1128A,0x1128D,1, 0x1128F,0x1129D,1,
  0x1129F,0x112A8,1, 0x112B0,0x112DE,1, 0x112DF,0x112DF,37,
  0x112E0,0x112E0,35, 0x112E1,0x112E1,22, 0x112E2,0x112E2,35,
  0x112E3,0x112E4,34, 0x112E5,0x112E8,33, 0x112E9,0x112E9,32,
  0x112EA,0x112EA,34, 0x112F0,0x112F9,1, 0x11300,0x11303,37,
  0x11305,0x1130C,1, 0x1130F,0x11310,1, 0x11313,0x11328,1, 0x1132A,0x11330,1,
  0x11332,0x11333,1, 0x11335,0x11339,1, 0x1133B,0x1133C,32,
  0x1133D,0x1133D,1, 0x1133E,0x1133F,35, 0x11340,0x11340,33,
  0x11341,0x11344,35, 0x11347,0x11348,22, 0x1134B,0x1134C,22,
  0x1134D,0x1134D,12, 0x11357,0x11357,35, 0x1135E,0x11361,1,
  0x11362,0x11363,35, 0x11366,0x1136C,37, 0x11370,0x11374,37,
  0x11380,0x11389,1, 0x1138B,0x1138B,1, 0x1138E,0x1138E,1, 0x11390,0x113B5,1,
  0x113B7,0x113B7,1, 0x113B8,0x113B8,35, 0x113B9,0x113BA,33,
  0x113BB,0x113C0,34, 0x113C2,0x113C2,22, 0x113C5,0x113C5,22,
  0x113C7,0x113C8,22, 0x113C9,0x113C9,35, 0x113CA,0x113CA,39,
  0x113CC,0x113CD,39, 0x113CE,0x113CE,37, 0x113CF,0x113CF,32,
  0x113D0,0x113D0,44, 0x113D1,0x113D1,18, 0x113D2,0x113D2,32,
  0x113E1,0x113E1,37, 0x113E2,0x113E2,38, 0x11400,0x11434,1,
  0x11435,0x11435,35, 0x11436,0x11436,22, 0x11437,0x11437,35,
  0x11438,0x1143D,34, 0x1143E,0x1143F,33, 0x11440,0x11441,35,
  0x11442,0x11442,12, 0x11443,0x11444,37, 0x11445,0x11445,39,
  0x11446,0x11446,32, 0x11447,0x11447,1, 0x11450,0x11459,1,
  0x1145E,0x1145E,45, 0x1145F,0x1145F,1, 0x11460,0x11461,43,
  0x11481,0x114AF,1, 0x114B0,0x114B0,35, 0x114B1,0x114B1,22,
  0x114B2,0x114B2,35, 0x114B3,0x114B8,34, 0x114B9,0x114B9,22,
  0x114BA,0x114BA,33, 0x114BB,0x114BC,22, 0x114BD,0x114BD,35,
  0x114BE,0x114BE,22, 0x114BF,0x114C1,37, 0x114C2,0x114C2,12,
  0x114C3,0x114C3,32, 0x114C4,0x114C4,1, 0x114D0,0x114D9,1,
  0x11580,0x115AE,1, 0x115AF,0x115AF,35, 0x115B0,0x115B0,22,
  0x115B1,0x115B1,35, 0x115B2,0x115B5,34, 0x115B8,0x115BB,22,
  0x115BC,0x115BD,37, 0x115BE,0x115BE,39, 0x115BF,0x115BF,12,
  0x115C0,0x115C0,32, 0x115D8,0x115DB,1, 0x115DC,0x115DD,34,
  0x11600,0x1162F,1, 0x11630,0x11632,35, 0x11633,0x11638,34,
  0x11639,0x1163A,33, 0x1163B,0x1163C,35, 0x1163D,0x1163D,37,
  0x1163E,0x1163E,39, 0x1163F,0x1163F,12, 0x11640,0x11640,33,
  0x11650,0x11659,1, 0x11680,0x116AA,1, 0x116AB,0x116AB,37,
  0x116AC,0x116AC,39, 0x116AD,0x116AD,33, 0x116AE,0x116AE,22,
  0x116AF,0x116AF,35, 0x116B0,0x116B1,34, 0x116B2,0x116B5,33,
  0x116B6,0x116B6,12, 0x116B7,0x116B7,32, 0x116B8,0x116B8,1,
  0x116C0,0x116C9,1, 0x116D0,0x116E3,1, 0x11700,0x1171A,1,
  0x1171D,0x1171D,28, 0x1171E,0x1171E,30, 0x1171F,0x1171F,27,
  0x11720,0x11721,35, 0x11722,0x11723,33, 0x11724,0x11725,34,
  0x11726,0x11726,22, 0x11727,0x11727,33, 0x11728,0x11728,34,
  0x11729,0x1172B,33, 0x11730,0x1173B,1, 0x11740,0x11746,1,
  0x11800,0x1182B,1, 0x1182C,0x1182C,35, 0x1182D,0x1182D,22,
  0x1182E,0x1182E,35, 0x1182F,0x11832,34, 0x11833,0x11836,33,
  0x11837,0x11837,37, 0x11838,0x11838,39, 0x11839,0x11839,12,
  0x1183A,0x1183A,32, 0x11900,0x11906,1, 0x11909,0x11909,1,
  0x1190C,0x11913,1, 0x11915,0x11916,1, 0x11918,0x1192F,1,
  0x11930,0x11934,35, 0x11935,0x11935,22, 0x11937,0x11938,22,
  0x1193B,0x1193C,37, 0x1193D,0x1193D,35, 0x1193E,0x1193E,44,
  0x1193F,0x1193F,18, 0x11940,0x11940,29, 0x11941,0x11941,18,
  0x11942,0x11942,29, 0x11943,0x11943,32, 0x11950,0x11959,1,
  0x119A0,0x119A7,1, 0x119AA,0x119D0,1, 0x119D1,0x119D1,35,
  0x119D2,0x119D2,22, 0x119D3,0x119D3,35, 0x119D4,0x119D7,34,
  0x119DA,0x119DB,33, 0x119DC,0x119DD,35, 0x119DE,0x119DF,39,
  0x119E0,0x119E0,12, 0x119E1,0x119E1,1, 0x119E4,0x119E4,22,
  0x11A00,0x11A00,1, 0x11A01,0x11A01,33, 0x11A02,0x11A03,34,
  0x11A04,0x11A09,33, 0x11A0A,0x11A0A,34, 0x11A0B,0x11A32,1,
  0x11A33,0x11A33,46, 0x11A34,0x11A34,34, 0x11A35,0x11A38,37,
  0x11A39,0x11A39,39, 0x11A3A,0x11A3A,43, 0x11A3B,0x11A3E,11,
  0x11A3F,0x11A3F,5, 0x11A45,0x11A45,5, 0x11A47,0x11A47,44,
  0x11A50,0x11A50,1, 0x11A51,0x11A51,33, 0x11A52,0x11A53,34,
  0x11A54,0x11A56,33, 0x11A57,0x11A58,35, 0x11A59,0x11A5B,34,
  0x11A5C,0x11A83,1, 0x11A84,0x11A89,18, 0x11A8A,0x11A95,25,
  0x11A96,0x11A96,37, 0x11A97,0x11A97,39, 0x11A98,0x11A98,31,
  0x11A99,0x11A99,44, 0x11A9D,0x11A9D,1, 0x11B60,0x11B60,33,
  0x11B61,0x11B61,35, 0x11B62,0x11B63,34, 0x11B64,0x11B64,33,
  0x11B65,0x11B65,35, 0x11B66,0x11B66,33, 0x11B67,0x11B67,35,
  0x11C00,0x11C08,1, 0x11C0A,0x11C2E,1, 0x11C2F,0x11C2F,35,
  0x11C30,0x11C31,33, 0x11C32,0x11C36,34, 0x11C38,0x11C3B,33,
  0x11C3C,0x11C3D,37, 0x11C3E,0x11C3E,39, 0x11C3F,0x11C3F,12,
  0x11C40,0x11C40,1, 0x11C50,0x11C6C,1, 0x11C72,0x11C8F,1,
  0x11C92,0x11CA7,11, 0x11CA9,0x11CAF,11, 0x11CB0,0x11CB0,34,
  0x11CB1,0x11CB1,22, 0x11CB2,0x11CB2,34, 0x11CB3,0x11CB3,33,
  0x11CB4,0x11CB4,35, 0x11CB5,0x11CB6,37, 0x11D00,0x11D06,1,
  0x11D08,0x11D09,1, 0x11D0B,0x11D30,1, 0x11D31,0x11D35,33,
  0x11D36,0x11D36,34, 0x11D3A,0x11D3A,33, 0x11D3C,0x11D3D,33,
  0x11D3F,0x11D3F,33, 0x11D40,0x11D41,37, 0x11D42,0x11D42,32,
  0x11D43,0x11D43,33, 0x11D44,0x11D44,34, 0x11D45,0x11D45,44,
  0x11D46,0x11D46,18, 0x11D47,0x11D47,28, 0x11D50,0x11D59,1,
  0x11D60,0x11D65,1, 0x11D67,0x11D68,1, 0x11D6A,0x11D89,1,
  0x11D8A,0x11D8E,35, 0x11D90,0x11D91,33, 0x11D93,0x11D94,35,
  0x11D95,0x11D95,37, 0x11D96,0x11D96,39, 0x11D97,0x11D97,44,
  0x11DA0,0x11DA9,1, 0x11EE0,0x11EF1,1, 0x11EF2,0x11EF2,5,
  0x11EF3,0x11EF3,33, 0x11EF4,0x11EF4,34, 0x11EF5,0x11EF5,22,
  0x11EF6,0x11EF6,35, 0x11F00,0x11F01,37, 0x11F02,0x11F02,18,
  0x11F03,0x11F03,39, 0x11F04,0x11F10,1, 0x11F12,0x11F33,1,
  0x11F34,0x11F35,35, 0x11F36,0x11F37,33, 0x11F38,0x11F3A,34,
  0x11F3E,0x11F3F,22, 0x11F40,0x11F40,33, 0x11F41,0x11F41,35,
  0x11F42,0x11F42,44, 0x11F50,0x11F59,1, 0x11F5A,0x11F5A,31,
  0x13000,0x1342F,49, 0x13430,0x13436,50, 0x13437,0x13437,51,
  0x13438,0x13438,52, 0x13439,0x1343B,50, 0x1343C,0x1343F,49,
  0x13440,0x13440,55, 0x13441,0x13446,49, 0x13447,0x13455,54,
  0x13460,0x143FA,49, 0x16100,0x1611D,1, 0x1611E,0x16129,33,
  0x1612A,0x1612B,30, 0x1612C,0x1612C,29, 0x1612D,0x1612D,37,
  0x1612E,0x1612E,28, 0x1612F,0x1612F,34, 0x16130,0x16139,1,
  0x16AC0,0x16AC9,1, 0x16B00,0x16B2F,1, 0x16B30,0x16B36,37,
  0x16D40,0x16D42,39, 0x16D43,0x16D6A,1, 0x16D6B,0x16D6C,35,
  0x16D70,0x16D79,1, 0x16F00,0x16F4A,1, 0x16F4F,0x16F4F,32,
  0x16F51,0x16F87,34, 0x16F8F,0x16F92,38, 0x16FE4,0x16FE4,1,
  0x18B00,0x18CD5,1, 0x18CFF,0x18CFF,1, 0x1BC00,0x1BC6A,1, 0x1BC70,0x1BC7C,1,
  0x1BC80,0x1BC88,1, 0x1BC90,0x1BC99,1, 0x1BC9D,0x1BC9E,32,
  0x1D173,0x1D17A,16, 0x1E100,0x1E12C,1, 0x1E130,0x1E136,37,
  0x1E137,0x1E13D,1, 0x1E140,0x1E149,1, 0x1E14E,0x1E14F,1, 0x1E290,0x1E2AD,1,
  0x1E2AE,0x1E2AE,37, 0x1E2C0,0x1E2EB,1, 0x1E2EC,0x1E2EF,37,
  0x1E2F0,0x1E2F9,1, 0x1E4D0,0x1E4EB,1, 0x1E4EC,0x1E4EF,33,
  0x1E4F0,0x1E4F9,1, 0x1E5D0,0x1E5ED,1, 0x1E5EE,0x1E5EF,34,
  0x1E5F0,0x1E5FA,1, 0x1E900,0x1E943,1, 0x1E944,0x1E94A,31,
  0x1E94B,0x1E94B,1, 0x1E950,0x1E959,1, 0xE0000,0xE00FF,16,
  0xE0100,0xE01EF,6, 0xE01F0,0xE0FFF,16
];

/* Binary search over USE_TABLE's triples. */
export function useCategory( cp ) {
  let lo = 0, hi = ( USE_TABLE.length / 3 ) - 1;
  while ( lo <= hi ) {
    const mid = ( lo + hi ) >> 1, k = mid * 3;
    if ( cp < USE_TABLE[ k ] ) hi = mid - 1;
    else if ( cp > USE_TABLE[ k + 1 ] ) lo = mid + 1;
    else return USE_TABLE[ k + 2 ];
  }
  return U.O;
}

/* The flat questions the rest of the engine asks.  Each is the union of one
 * positional family. */
const isV  = ( c ) => c === U.VAbv || c === U.VBlw || c === U.VPst || c === U.VPre;
const isVM = ( c ) => c === U.VMAbv || c === U.VMBlw || c === U.VMPst || c === U.VMPre;
const isF  = ( c ) => c === U.FAbv || c === U.FBlw || c === U.FPst;
const isFM = ( c ) => c === U.FMAbv || c === U.FMBlw || c === U.FMPst;
const isM  = ( c ) => c === U.MAbv || c === U.MBlw || c === U.MPst || c === U.MPre;


/* ------------------------------------------------------------------ */
/*  The cluster grammar -- sd-use.txt l.567-700                         */
/* ------------------------------------------------------------------ */

export const SYLL = {
  VIRAMA_TERMINATED: 0, SAKOT_TERMINATED: 1, STANDARD: 2,
  NUMBER_JOINER_TERMINATED: 3, NUMERAL: 4, SYMBOL: 5, HIEROGLYPH: 6,
  BROKEN: 7, NON_CLUSTER: 8,
};

/* The grammar is regular and is scanned with the shared NFA scanner in
 * shaper-indic.js; see the note there for why a backtracking descent over
 * these rules is not equivalent. */

/* l.593:  h = HALANT | HALANT_OR_VOWEL_MODIFIER | INVISIBLE_STACKER | SAKOT */
const g_h = ALT( L( U.H ), L( U.HVM ), L( U.IS ), L( U.Sk ) );
const g_consonant_modifiers = SEQ(
  STAR( L( U.CMAbv ) ), STAR( L( U.CMBlw ) ),
  STAR( SEQ( ALT( SEQ( g_h, L( U.B ) ), L( U.SUB ) ),
             STAR( L( U.CMAbv ) ), STAR( L( U.CMBlw ) ) ) ) );
const g_medial_consonants = SEQ( OPT( L( U.MPre ) ), OPT( L( U.MAbv ) ),
                                 OPT( L( U.MBlw ) ), OPT( L( U.MPst ) ) );
const g_dependent_vowels = ALT(
  SEQ( STAR( L( U.VPre ) ), STAR( L( U.VAbv ) ), STAR( L( U.VBlw ) ),
       STAR( L( U.VPst ) ) ),
  L( U.H ) );
const g_vowel_modifiers = SEQ( OPT( L( U.HVM ) ), STAR( L( U.VMPre ) ),
                               STAR( L( U.VMAbv ) ), STAR( L( U.VMBlw ) ),
                               STAR( L( U.VMPst ) ) );
const g_final_consonants = SEQ( STAR( L( U.FAbv ) ), STAR( L( U.FBlw ) ),
                                STAR( L( U.FPst ) ) );
const g_final_modifiers = ALT( SEQ( STAR( L( U.FMAbv ) ), STAR( L( U.FMBlw ) ) ),
                               OPT( L( U.FMPst ) ) );

const g_start = SEQ( OPT( ALT( L( U.R ), L( U.CS ) ) ), ALT( L( U.B ), L( U.GB ) ) );
const g_middle = SEQ( g_consonant_modifiers, g_medial_consonants,
                      g_dependent_vowels, g_vowel_modifiers,
                      STAR( SEQ( L( U.Sk ), L( U.B ) ) ) );
const g_tail_complex = SEQ( g_middle, g_final_consonants, g_final_modifiers );
const g_njt_tail = SEQ( STAR( SEQ( L( U.HN ), L( U.N ) ) ), L( U.HN ) );
const g_numeral_tail = PLUS( SEQ( L( U.HN ), L( U.N ) ) );
const g_symbol_tail = ALT( SEQ( PLUS( L( U.SMAbv ) ), STAR( L( U.SMBlw ) ) ),
                           PLUS( L( U.SMBlw ) ) );
const g_virama_tail = SEQ( g_consonant_modifiers, ALT( L( U.IS ), L( U.RK ) ) );
const g_sakot_tail = SEQ( g_middle, L( U.Sk ) );
const g_tail = ALT( g_tail_complex, g_sakot_tail, g_symbol_tail, g_virama_tail );

const g_virama_cluster = SEQ( g_start, g_virama_tail );
const g_sakot_cluster = SEQ( g_start, g_sakot_tail );
const g_standard_cluster = SEQ( g_start, g_tail_complex );
const g_broken_cluster = SEQ( OPT( L( U.R ) ),
                              ALT( g_tail, g_njt_tail, g_numeral_tail ) );
const g_njt_cluster = SEQ( L( U.N ), g_njt_tail );
const g_numeral_cluster = SEQ( L( U.N ), OPT( g_numeral_tail ) );
const g_symbol_cluster = SEQ( ALT( L( U.O ), L( U.GB ), L( U.SB ) ), OPT( g_tail ) );
const g_hieroglyph_group = SEQ( L( U.G ), OPT( L( U.HR ) ), OPT( L( U.HM ) ),
                                STAR( L( U.SE ) ) );
const g_hieroglyph_cluster = SEQ( STAR( L( U.SB ) ), g_hieroglyph_group,
  STAR( SEQ( L( U.J ), STAR( L( U.SB ) ), OPT( g_hieroglyph_group ) ) ) );

/* The scanner's alternatives, in the order l.567-700 lists them.  Longest
 * match wins; on a tie the earlier rule wins, which is what makes a bare
 * SAKOT-terminated cluster lose to the standard cluster that swallows it. */
const RULES = [
  { f: SEQ( g_virama_cluster, OPT( L( U.ZWNJ ) ) ), t: SYLL.VIRAMA_TERMINATED },
  { f: SEQ( g_sakot_cluster, OPT( L( U.ZWNJ ) ) ), t: SYLL.SAKOT_TERMINATED },
  { f: SEQ( g_standard_cluster, OPT( L( U.ZWNJ ) ) ), t: SYLL.STANDARD },
  { f: SEQ( g_njt_cluster, OPT( L( U.ZWNJ ) ) ), t: SYLL.NUMBER_JOINER_TERMINATED },
  { f: SEQ( g_numeral_cluster, OPT( L( U.ZWNJ ) ) ), t: SYLL.NUMERAL },
  { f: SEQ( g_symbol_cluster, OPT( L( U.ZWNJ ) ) ), t: SYLL.SYMBOL },
  { f: SEQ( g_hieroglyph_cluster, OPT( L( U.ZWNJ ) ) ), t: SYLL.HIEROGLYPH },
  { f: L( U.FMPst ), t: SYLL.NON_CLUSTER },
  { f: SEQ( g_broken_cluster, OPT( L( U.ZWNJ ) ) ), t: SYLL.BROKEN },
  { f: gAny(), t: SYLL.NON_CLUSTER },
];
const scanUse = makeScanner( RULES );

const isUnicodeMark = ( inf ) => {
  const g = inf.unicodeProps !== undefined
    ? ( inf.unicodeProps & UPROPS_MASK_GEN_CAT )
    : generalCategory( inf.codepoint );
  return g === GC.Mn || g === GC.Mc || g === GC.Me;
};

/* Divide the buffer into clusters and record the type and a running index in
 * info[i].syllable, exactly as the reference packs it: (index << 4) | type.
 *
 * Two classes of character are INVISIBLE to the grammar (l.677-687) and are
 * filtered out before the scanner runs rather than given rules of their own:
 *   - CGJ, which "may occur anywhere in a cluster with no effect", and which
 *     by l.133/l.381 also covers ZWJ and the variation selectors;
 *   - a ZWNJ whose next visible neighbour IS a combining mark, because l.683
 *     only breaks the cluster after a ZWNJ when what follows is not a mark.
 *     A ZWNJ that survives the filter is the terminator the rules spell as
 *     the trailing `ZWNJ?`.
 * A filtered character still belongs to the cluster that surrounds it: the
 * ranges below are in ORIGINAL buffer indices, so the syllable a cluster
 * stamps covers the invisible characters inside it. */
export function findSyllables( buffer ) {
  const info = buffer.info;
  /* NEVER use buffer.len alone here.  A GSUB pause runs in the middle of the
   * substitution stage, and ot-layout.js splices buffer.info as lookups add
   * and delete glyphs; buffer.len is only resynchronised when S3 returns from
   * the stage.  Taking the smaller of the two is the only safe reading. */
  const n = Math.min( info.length, buffer.len === undefined ? info.length
                                                           : Math.max( buffer.len, 0 ) ) || info.length;
  if ( !n ) return;
  const catOf = ( k ) => ( info[ k ].useCat === undefined ? U.O : info[ k ].useCat );

  const idx = [];
  const cats = [];
  for ( let i = 0; i < n; i++ ) {
    const c = catOf( i );
    if ( c === U.CGJ ) continue;
    if ( c === U.ZWNJ ) {
      let keep = true;
      for ( let k = i + 1; k < n; k++ ) {
        if ( catOf( k ) === U.CGJ ) continue;
        keep = !isUnicodeMark( info[ k ] );
        break;
      }
      if ( !keep ) continue;
    }
    idx.push( i );
    cats.push( c );
  }
  const m = idx.length;
  if ( !m ) {
    for ( let i = 0; i < n; i++ ) info[ i ].syllable = 0;
    return;
  }

  let serial = 1;
  let k = 0;
  while ( k < m ) {
    const hit = scanUse( cats, k, m, SYLL.NON_CLUSTER );
    const bestLen = hit.len, bestType = hit.type;
    const s = k === 0 ? 0 : idx[ k ];
    const e = k + bestLen < m ? idx[ k + bestLen ] : n;
    const val = ( ( serial << 4 ) | bestType ) & 0xFF;
    for ( let x = s; x < e; x++ ) info[ x ].syllable = val;
    if ( bestType === SYLL.BROKEN && buffer.scratchFlags !== undefined )
      buffer.scratchFlags |= SCRATCH_HAS_BROKEN_SYLLABLE;
    serial++;
    if ( serial === 16 ) serial = 1;
    k += bestLen;
  }
}

export const syllableType = ( inf ) => inf.syllable & 0x0F;
export const syllableIndex = ( inf ) => inf.syllable >> 4;

const foreachSyllable = forEachSyllable;

/* ------------------------------------------------------------------ */
/*  Features -- sd-use.txt l.701-930                                    */
/* ------------------------------------------------------------------ */

/* Orthographic unit shaping, per cluster (l.737-765). */
const BASIC_FEATURES = [ 'rkrf', 'abvf', 'blwf', 'half', 'pstf', 'vatu', 'cjct' ];
/* Topographical, per cluster (l.838-850). */
const TOPO_FEATURES = [ 'isol', 'init', 'medi', 'fina' ];
/* Standard typographic presentation, whole run (l.854-880). */
const OTHER_FEATURES = [ 'abvs', 'blws', 'haln', 'pres', 'psts' ];

function collectFeatures( b ) {
  /* Do this before any lookups have been applied. */
  b.addGsubPause( setupSyllables );

  /* "Default glyph pre-processing group" (l.705-721). */
  b.enableFeature( 'locl', F_PER_SYLLABLE );
  b.enableFeature( 'ccmp', F_PER_SYLLABLE );
  b.enableFeature( 'nukt', F_PER_SYLLABLE );
  b.enableFeature( 'akhn', F_MANUAL_ZWJ | F_PER_SYLLABLE );

  /* "Reordering group" (l.723-735): rphf then pref, each on its own so that
   * the output of each can be reordered.  The two record passes read the
   * SUBSTITUTED bit, which is why each is preceded by a pass that clears it:
   * without that, every glyph any earlier feature touched would be taken for
   * this feature's output. */
  b.addGsubPause( clearSubstitutionFlags );
  b.addFeature( 'rphf', F_MANUAL_ZWJ | F_PER_SYLLABLE );
  b.addGsubPause( recordRphf );
  b.addGsubPause( clearSubstitutionFlags );
  b.enableFeature( 'pref', F_MANUAL_ZWJ | F_PER_SYLLABLE );
  b.addGsubPause( recordPref );

  /* "Orthographic unit shaping group" (l.737-765). */
  for ( const t of BASIC_FEATURES )
    b.enableFeature( t, F_MANUAL_ZWJ | F_PER_SYLLABLE );

  b.addGsubPause( reorderUse );
  b.addGsubPause( clearSyllables );

  /* "Topographical features" (l.838-850). */
  for ( const t of TOPO_FEATURES ) b.addFeature( t );
  b.addGsubPause( null );

  /* "Standard typographic presentation" (l.854-880). */
  for ( const t of OTHER_FEATURES ) b.enableFeature( t, F_MANUAL_ZWJ );
}

/* USE leaves the common/horizontal features to the generic collector; nothing
 * needs disabling. */
function overrideFeatures( /* b */ ) {}

function setupRphfMask( plan, buffer ) {
  const mask = oneMask( plan, 'rphf' );
  if ( !mask ) return;
  const info = buffer.info;
  foreachSyllable( buffer, ( start, end ) => {
    const limit = info[ start ].useCat === U.R ? 1 : Math.min( 3, end - start );
    for ( let i = start; i < start + limit; i++ )
      info[ i ].mask = ( info[ i ].mask | mask ) >>> 0;
  } );
}

/* l.838-850: the topographical forms are chosen by where a cluster sits in
 * the RUN, not by the Unicode joining types -- USE clusters join to each
 * other.  The first cluster is isolated; the next makes it initial and takes
 * the final form itself; each further cluster makes its predecessor medial.
 *
 * Deriving these from joining_type() instead -- which is what this file did
 * before -- gives every Tai Tham cluster the isolated form, because Tai Tham
 * has no joining types at all. */
const JF_ISOL = 0, JF_INIT = 1, JF_MEDI = 2, JF_FINA = 3, JF_NONE = 4;

function setupTopographicalMasks( plan, buffer ) {
  if ( plan && plan.useArabicJoining ) return;
  const masks = [ 0, 0, 0, 0 ];
  let all = 0;
  const globalMask = plan && plan.globalMask !== undefined ? plan.globalMask : 0;
  for ( let i = 0; i < 4; i++ ) {
    let mk = oneMask( plan, TOPO_FEATURES[ i ] );
    if ( mk === globalMask ) mk = 0;
    masks[ i ] = mk;
    all |= mk;
  }
  if ( !all ) return;
  const other = ( ~all ) >>> 0;
  const info = buffer.info;
  let lastStart = 0;
  let lastForm = JF_NONE;
  foreachSyllable( buffer, ( start, end ) => {
    const t = info[ start ].syllable & 0x0F;
    if ( t === SYLL.HIEROGLYPH || t === SYLL.NON_CLUSTER ) {
      lastForm = JF_NONE;
    } else {
      const join = lastForm === JF_FINA || lastForm === JF_ISOL;
      if ( join ) {
        lastForm = lastForm === JF_FINA ? JF_MEDI : JF_INIT;
        for ( let i = lastStart; i < start; i++ )
          info[ i ].mask = ( ( info[ i ].mask & other ) | masks[ lastForm ] ) >>> 0;
      }
      lastForm = join ? JF_FINA : JF_ISOL;
      for ( let i = start; i < end; i++ )
        info[ i ].mask = ( ( info[ i ].mask & other ) | masks[ lastForm ] ) >>> 0;
    }
    lastStart = start;
  } );
}

function setupSyllables( plan, buffer /* , font */ ) {
  findSyllables( buffer );
  if ( typeof buffer.unsafeToBreak === 'function' )
    foreachSyllable( buffer, ( s, e ) => buffer.unsafeToBreak( s, e ) );
  setupRphfMask( plan, buffer );
  setupTopographicalMasks( plan, buffer );
}

function clearSubstitutionFlags( plan, buffer ) {
  const info = buffer.info;
  for ( let i = 0; i < info.length; i++ )
    info[ i ].props = ( info[ i ].props & ~GP_SUBSTITUTED ) >>> 0;
}

/* "Mark a substituted repha as USE(R)."  One per cluster, and only inside the
 * window setupRphfMask actually gave the feature. */
function recordRphf( plan, buffer ) {
  const mask = oneMask( plan, 'rphf' );
  if ( !mask ) return;
  const info = buffer.info;
  foreachSyllable( buffer, ( start, end ) => {
    for ( let i = start; i < end && ( info[ i ].mask & mask ); i++ )
      if ( info[ i ].props & GP_SUBSTITUTED ) { info[ i ].useCat = U.R; break; }
  } );
}

/* "Mark a substituted pref as VPre, as they behave the same way."  That one
 * line is the whole of the pre-base reordering of l.806: the reordering pass
 * below moves VPre, so a glyph 'pref' produced rides along with it. */
function recordPref( plan, buffer ) {
  const info = buffer.info;
  foreachSyllable( buffer, ( start, end ) => {
    for ( let i = start; i < end; i++ )
      if ( info[ i ].props & GP_SUBSTITUTED ) { info[ i ].useCat = U.VPre; break; }
  } );
}

function clearSyllables( /* plan, buffer */ ) {}


/* ------------------------------------------------------------------ */
/*  Glyph reordering -- sd-use.txt l.767-834                            */
/* ------------------------------------------------------------------ */

/* l.783: "If a font developer wants the reordering behavior to not be blocked
 * by an explicit virama, they can substitute the virama for an alternate
 * glyph" -- so a virama a ligating lookup consumed is no longer explicit and
 * no longer blocks. */
const isHalantUse = ( inf ) => {
  const c = inf.useCat;
  return ( c === U.H || c === U.HVM || c === U.IS ) && !( inf.props & GP_LIGATED );
};

const POST_BASE = new Set( [ U.FAbv, U.FBlw, U.FPst, U.FMAbv, U.FMBlw, U.FMPst,
                             U.MAbv, U.MBlw, U.MPst, U.MPre, U.VAbv, U.VBlw,
                             U.VPst, U.VPre, U.VMAbv, U.VMBlw, U.VMPst, U.VMPre ] );

function reorderSyllable( buffer, start, end ) {
  const info = buffer.info;
  const pos = buffer.pos;
  const t = info[ start ].syllable & 0x0F;
  if ( t !== SYLL.VIRAMA_TERMINATED && t !== SYLL.SAKOT_TERMINATED &&
       t !== SYLL.STANDARD && t !== SYLL.SYMBOL && t !== SYLL.BROKEN ) return;

  /* rotate info[a..b] left by one, carrying pos with it */
  const rotateLeft = ( a, b ) => {
    const g = info[ a ];
    for ( let i = a; i < b; i++ ) info[ i ] = info[ i + 1 ];
    info[ b ] = g;
    if ( pos && pos.length > b ) {
      const q = pos[ a ];
      for ( let i = a; i < b; i++ ) pos[ i ] = pos[ i + 1 ];
      pos[ b ] = q;
    }
  };
  const rotateRight = ( a, b ) => {
    const g = info[ b ];
    for ( let i = b; i > a; i-- ) info[ i ] = info[ i - 1 ];
    info[ a ] = g;
    if ( pos && pos.length > b ) {
      const q = pos[ b ];
      for ( let i = b; i > a; i-- ) pos[ i ] = pos[ i - 1 ];
      pos[ a ] = q;
    }
  };

  /* Move things forward: a repha reorders towards the end of its cluster but
   * before the first post-base glyph (l.801). */
  if ( info[ start ].useCat === U.R && end - start > 1 ) {
    for ( let i = start + 1; i < end; i++ ) {
      const isPostBase = POST_BASE.has( info[ i ].useCat ) || isHalantUse( info[ i ] );
      if ( isPostBase || i === end - 1 ) {
        const to = isPostBase ? i - 1 : i;
        mergeClusterRange( buffer, start, to + 1 );
        rotateLeft( start, to );
        break;
      }
    }
  }

  /* Move things back: every pre-base vowel (and the pre-base glyph 'pref'
   * produced, which recordPref relabelled VPre) moves to the head of the
   * cluster, or to just after the last explicit virama (l.779, l.825-829). */
  let j = start;
  for ( let i = start; i < end; i++ ) {
    if ( isHalantUse( info[ i ] ) ) {
      j = i + 1;
    } else if ( ( info[ i ].useCat === U.VPre || info[ i ].useCat === U.VMPre ) &&
                /* Only move the first component of a MultipleSubst. */
                !info[ i ].ligComp && j < i ) {
      mergeClusterRange( buffer, j, i + 1 );
      rotateRight( j, i );
    }
  }
}

/* hb_syllabic_insert_dotted_circles, as reorder_use calls it with
 * (use_broken_cluster, USE(B), USE(R)).
 *
 * l.693-695: "A sequence of marks without a valid base forms separate
 * clusters for each mark ... USE inserts a dotted circle glyph (U+25CC) to
 * indicate a broken cluster."  The grammar above is what decides which runs
 * those are; this pass only acts on the verdict, one circle per broken
 * cluster, placed after any repha the cluster starts with.
 *
 * The record is the reference's `hb_glyph_info_t dottedcircle = {0}` with the
 * glyph id filled in: no unicode props and no glyph props, because this runs
 * in a GSUB pause, after substitute_start has already classified everything
 * else.  It takes the cluster, mask and syllable of the glyph it displaces so
 * the syllable stays one run and the cluster numbering does not move. */
function insertBrokenClusterCircles( buffer, font ) {
  if ( buffer.scratchFlags !== undefined &&
       !( buffer.scratchFlags & SCRATCH_HAS_BROKEN_SYLLABLE ) ) return false;
  return insertDottedCircles( buffer, font, SYLL.BROKEN, U.B, U.R, -1,
                              'useCat', null );
}

function reorderUse( plan, buffer, font ) {
  insertBrokenClusterCircles( buffer, font );
  foreachSyllable( buffer, ( s, e ) => reorderSyllable( buffer, s, e ) );
}

function setupMasks( plan, buffer, font ) {
  const info = buffer.info;
  const n = Math.min( info.length, buffer.len === undefined ? info.length
                                                           : Math.max( buffer.len, 0 ) ) || info.length;
  for ( let i = 0; i < n; i++ )
    info[ i ].useCat = useCategory( info[ i ].codepoint );
}

/* l.673-675 carves the prohibited independent-vowel + dependent-vowel
 * sequences out into IndicShapingInvalidCluster.txt; shaper-indic.js owns
 * that table because the Indic shaper needs the same pass. */
function preprocessText( plan, buffer, font ) {
  preprocessVowelConstraints( plan, buffer, font );
}

function postprocessGlyphs( /* plan, buffer, font */ ) {}

/* USE does its own mark ordering through the cluster model, so the generic
 * canonical reordering must not run. */
function reorderMarks( /* plan, buffer, start, end */ ) {}


/* ------------------------------------------------------------------ */
/*  The shaper object and the script table                              */
/* ------------------------------------------------------------------ */

export const useShaper = {
  name: 'use',
  collectFeatures,
  overrideFeatures,
  preprocessText,
  setupMasks,
  postprocessGlyphs,
  reorderMarks,
  /* USE validates clusters on the decomposed form (l.559). */
  normalizationPreference: 'decomposed',
  zeroWidthMarks: 'by-gdef-early',
  fallbackPosition: false,
  gposTag: null,
};

/* The scripts USE owns, as ISO 15924 codes -- shape-plan.js passes
 * tagToString(props.script).  From the registered-tag table at l.1017-1358,
 * restricted to the scripts Unicode 17 actually encodes.
 *
 * hb_ot_shaper_categorize() is a switch, so a script has exactly ONE shaper:
 * the cursive scripts it sends to the Arabic shaper (Adlm, Chrs, Mand, Mani,
 * Mong, Nkoo, Ougr, Phag, Phlp, Rohg, Sogd, Gara) are therefore NOT USE
 * scripts, even though the registered-tag table lists them.  They are handled
 * in shaper-misc.js. */
const USE_SCRIPTS = new Set( [
  'Ahom', 'Bali', 'Batk', 'Bhks', 'Brah', 'Bugi', 'Buhd', 'Cakm',
  'Cham', 'Cpmn', 'Diak', 'Dogr', 'Dupl', 'Egyp', 'Elym',
  'Gong', 'Gonm', 'Gran', 'Gukh', 'Hano', 'Hmng', 'Hmnp', 'Java', 'Kali',
  'Kawi', 'Khar', 'Khoj', 'Kits', 'Krai', 'Kthi', 'Lana', 'Lepc', 'Limb',
  'Mahj', 'Maka', 'Marc', 'Medf', 'Modi', 'Mtei',
  'Mult', 'Nagm', 'Nand', 'Newa', 'Onao',
  'Plrd', 'Rjng', 'Saur', 'Shrd', 'Sidd', 'Sind', 'Sogo',
  'Soyo', 'Sund', 'Sunu', 'Sylo', 'Tagb', 'Takr', 'Tale', 'Tavt', 'Tglg',
  'Tibt', 'Tfng', 'Tirh', 'Tnsa', 'Todr', 'Toto', 'Tutg', 'Vith', 'Wcho',
  'Yezi', 'Zanb',
] );

/* The nine Indic scripts have their own engine, but only for a font that
 * declares an Indic v1 or v2 script tag.  A font that declares a v3 tag
 * ('dev3', 'knd3', 'tel3', ...) is asking for the Universal engine instead --
 * that is what the third generation of the tags MEANS -- and a font that
 * declares only 'DFLT' or 'latn' is asking for no complex shaping at all. */
const INDIC_SCRIPTS = new Set( [
  'Deva', 'Beng', 'Guru', 'Gujr', 'Orya', 'Taml', 'Telu', 'Knda', 'Mlym',
] );

/* W19_NO_GSUBSCRIPT ignores the tag the font declares, restoring the
 * script-only routing this engine had before. */
export function shaperForScript( scriptTag, ctx ) {
  const gs = process.env.W19_NO_GSUBSCRIPT ? null : ( ctx && ctx.gsubScript );
  const v3 = typeof gs === 'string' && gs.length === 4 && gs.charCodeAt( 3 ) === 0x33;
  if ( INDIC_SCRIPTS.has( scriptTag ) )
    return ( v3 && gs !== 'DFLT' && gs !== 'latn' ) ? useShaper : null;
  /* Sinhala.  spec/sd-sinhala.txt:39 -- "NOTE: Starting in Windows 10, Sinhala
   * will be supported by the Universal Shaping Engine rather than a
   * stand-alone shaping engine.  Moving forward, developers should refer to
   * this new specification."  That chapter is shipped and says so in its own
   * opening note, and :119 -- "the engine verifies that the run consists of
   * valid clusters and inserts a placeholder glyph (U+25CC) wherever combining
   * marks occur without a valid base" -- is exactly USE's cluster validation. */
  if ( scriptTag !== 'Sinh' && !USE_SCRIPTS.has( scriptTag ) ) return null;
  if ( gs === 'DFLT' || gs === 'latn' ) return null;
  return useShaper;
}

export default useShaper;
