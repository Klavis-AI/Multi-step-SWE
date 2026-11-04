/* shaper-indic.js -- Unicode Indic character properties, shared by the
 * Universal Shaping Engine (shaper-use.js) and the Indic shaper.
 *
 * S2's unicode.js carries every Unicode property the shapers need EXCEPT the
 * two Indic ones, so they are generated here from the same Unicode 17.0.0
 * release HarfBuzz 14.4.0 is built against:
 *
 *   ../ucd/IndicSyllabicCategory.txt   -> UISC
 *   ../ucd/IndicPositionalCategory.txt -> UIPC
 *
 * The tables below are mechanically derived from those files (adjacent ranges
 * with the same value merged) by the generator recorded in
 * NOTES-S1-scripts.md; they are data, not hand-written.
 *
 * Defaults, per the UCD: a code point absent from IndicSyllabicCategory.txt is
 * UISC = Other, and one absent from IndicPositionalCategory.txt is UIPC = NA.
 * Both lookups return -1 for "absent" so the caller can tell the default apart
 * from an explicit value.
 */


/* UISC -- 811 ranges, 36 values, generated from ../ucd/IndicSyllabicCategory.txt */
export const UISC_NAMES = [
  'Avagraha', 'Bindu', 'Brahmi_Joining_Number', 'Cantillation_Mark',
  'Consonant', 'Consonant_Dead', 'Consonant_Final',
  'Consonant_Head_Letter', 'Consonant_Initial_Postfixed',
  'Consonant_Killer', 'Consonant_Medial', 'Consonant_Placeholder',
  'Consonant_Preceding_Repha', 'Consonant_Prefixed', 'Consonant_Subjoined',
  'Consonant_Succeeding_Repha', 'Consonant_With_Stacker',
  'Gemination_Mark', 'Invisible_Stacker', 'Joiner', 'Modifying_Letter',
  'Non_Joiner', 'Nukta', 'Number', 'Number_Joiner', 'Pure_Killer',
  'Register_Shifter', 'Reordering_Killer', 'Syllable_Modifier',
  'Tone_Letter', 'Tone_Mark', 'Virama', 'Visarga', 'Vowel',
  'Vowel_Dependent', 'Vowel_Independent'
];
const UISC_R = [
  0x2D,0x2D,11, 0x30,0x39,23, 0xA0,0xA0,11, 0xB2,0xB3,28, 0xD7,0xD7,11,
  0x900,0x902,1, 0x903,0x903,32, 0x904,0x914,35, 0x915,0x939,4,
  0x93A,0x93B,34, 0x93C,0x93C,22, 0x93D,0x93D,0, 0x93E,0x94C,34,
  0x94D,0x94D,31, 0x94E,0x94F,34, 0x951,0x952,3, 0x955,0x957,34,
  0x958,0x95F,4, 0x960,0x961,35, 0x962,0x963,34, 0x966,0x96F,23,
  0x972,0x977,35, 0x978,0x97F,4, 0x980,0x980,11, 0x981,0x982,1,
  0x983,0x983,32, 0x985,0x98C,35, 0x98F,0x990,35, 0x993,0x994,35,
  0x995,0x9A8,4, 0x9AA,0x9B0,4, 0x9B2,0x9B2,4, 0x9B6,0x9B9,4,
  0x9BC,0x9BC,22, 0x9BD,0x9BD,0, 0x9BE,0x9C4,34, 0x9C7,0x9C8,34,
  0x9CB,0x9CC,34, 0x9CD,0x9CD,31, 0x9CE,0x9CE,5, 0x9D7,0x9D7,34,
  0x9DC,0x9DD,4, 0x9DF,0x9DF,4, 0x9E0,0x9E1,35, 0x9E2,0x9E3,34,
  0x9E6,0x9EF,23, 0x9F0,0x9F1,4, 0x9FC,0x9FC,1, 0x9FE,0x9FE,28,
  0xA01,0xA02,1, 0xA03,0xA03,32, 0xA05,0xA0A,35, 0xA0F,0xA10,35,
  0xA13,0xA14,35, 0xA15,0xA28,4, 0xA2A,0xA30,4, 0xA32,0xA33,4,
  0xA35,0xA36,4, 0xA38,0xA39,4, 0xA3C,0xA3C,22, 0xA3E,0xA42,34,
  0xA47,0xA48,34, 0xA4B,0xA4C,34, 0xA4D,0xA4D,31, 0xA51,0xA51,3,
  0xA59,0xA5C,4, 0xA5E,0xA5E,4, 0xA66,0xA6F,23, 0xA70,0xA70,1,
  0xA71,0xA71,17, 0xA72,0xA73,11, 0xA75,0xA75,10, 0xA81,0xA82,1,
  0xA83,0xA83,32, 0xA85,0xA8D,35, 0xA8F,0xA91,35, 0xA93,0xA94,35,
  0xA95,0xAA8,4, 0xAAA,0xAB0,4, 0xAB2,0xAB3,4, 0xAB5,0xAB9,4,
  0xABC,0xABC,22, 0xABD,0xABD,0, 0xABE,0xAC5,34, 0xAC7,0xAC9,34,
  0xACB,0xACC,34, 0xACD,0xACD,31, 0xAE0,0xAE1,35, 0xAE2,0xAE3,34,
  0xAE6,0xAEF,23, 0xAF9,0xAF9,4, 0xAFA,0xAFA,3, 0xAFB,0xAFB,17,
  0xAFC,0xAFC,3, 0xAFD,0xAFF,22, 0xB01,0xB02,1, 0xB03,0xB03,32,
  0xB05,0xB0C,35, 0xB0F,0xB10,35, 0xB13,0xB14,35, 0xB15,0xB28,4,
  0xB2A,0xB30,4, 0xB32,0xB33,4, 0xB35,0xB39,4, 0xB3C,0xB3C,22,
  0xB3D,0xB3D,0, 0xB3E,0xB44,34, 0xB47,0xB48,34, 0xB4B,0xB4C,34,
  0xB4D,0xB4D,31, 0xB55,0xB57,34, 0xB5C,0xB5D,4, 0xB5F,0xB5F,4,
  0xB60,0xB61,35, 0xB62,0xB63,34, 0xB66,0xB6F,23, 0xB71,0xB71,4,
  0xB82,0xB82,1, 0xB83,0xB83,20, 0xB85,0xB8A,35, 0xB8E,0xB90,35,
  0xB92,0xB94,35, 0xB95,0xB95,4, 0xB99,0xB9A,4, 0xB9C,0xB9C,4,
  0xB9E,0xB9F,4, 0xBA3,0xBA4,4, 0xBA8,0xBAA,4, 0xBAE,0xBB9,4,
  0xBBE,0xBC2,34, 0xBC6,0xBC8,34, 0xBCA,0xBCC,34, 0xBCD,0xBCD,31,
  0xBD7,0xBD7,34, 0xBE6,0xBEF,23, 0xC00,0xC02,1, 0xC03,0xC03,32,
  0xC04,0xC04,1, 0xC05,0xC0C,35, 0xC0E,0xC10,35, 0xC12,0xC14,35,
  0xC15,0xC28,4, 0xC2A,0xC39,4, 0xC3C,0xC3C,22, 0xC3D,0xC3D,0,
  0xC3E,0xC44,34, 0xC46,0xC48,34, 0xC4A,0xC4C,34, 0xC4D,0xC4D,31,
  0xC55,0xC56,34, 0xC58,0xC5A,4, 0xC5D,0xC5D,5, 0xC60,0xC61,35,
  0xC62,0xC63,34, 0xC66,0xC6F,23, 0xC80,0xC82,1, 0xC83,0xC83,32,
  0xC85,0xC8C,35, 0xC8E,0xC90,35, 0xC92,0xC94,35, 0xC95,0xCA8,4,
  0xCAA,0xCB3,4, 0xCB5,0xCB9,4, 0xCBC,0xCBC,22, 0xCBD,0xCBD,0,
  0xCBE,0xCC4,34, 0xCC6,0xCC8,34, 0xCCA,0xCCC,34, 0xCCD,0xCCD,31,
  0xCD5,0xCD6,34, 0xCDD,0xCDD,5, 0xCDE,0xCDE,4, 0xCE0,0xCE1,35,
  0xCE2,0xCE3,34, 0xCE6,0xCEF,23, 0xCF1,0xCF2,16, 0xCF3,0xCF3,1,
  0xD00,0xD02,1, 0xD03,0xD03,32, 0xD04,0xD04,1, 0xD05,0xD0C,35,
  0xD0E,0xD10,35, 0xD12,0xD14,35, 0xD15,0xD3A,4, 0xD3B,0xD3C,25,
  0xD3D,0xD3D,0, 0xD3E,0xD44,34, 0xD46,0xD48,34, 0xD4A,0xD4C,34,
  0xD4D,0xD4D,31, 0xD4E,0xD4E,12, 0xD54,0xD56,5, 0xD57,0xD57,34,
  0xD5F,0xD61,35, 0xD62,0xD63,34, 0xD66,0xD6F,23, 0xD7A,0xD7F,5,
  0xD81,0xD82,1, 0xD83,0xD83,32, 0xD85,0xD96,35, 0xD9A,0xDB1,4,
  0xDB3,0xDBB,4, 0xDBD,0xDBD,4, 0xDC0,0xDC6,4, 0xDCA,0xDCA,31,
  0xDCF,0xDD4,34, 0xDD6,0xDD6,34, 0xDD8,0xDDF,34, 0xDE6,0xDEF,23,
  0xDF2,0xDF3,34, 0xE01,0xE2E,4, 0xE30,0xE39,34, 0xE3A,0xE3A,25,
  0xE40,0xE45,34, 0xE47,0xE47,34, 0xE48,0xE4B,30, 0xE4C,0xE4C,9,
  0xE4D,0xE4D,1, 0xE4E,0xE4E,25, 0xE50,0xE59,23, 0xE81,0xE82,4,
  0xE84,0xE84,4, 0xE86,0xE8A,4, 0xE8C,0xEA3,4, 0xEA5,0xEA5,4,
  0xEA7,0xEAE,4, 0xEB0,0xEB9,34, 0xEBA,0xEBA,25, 0xEBB,0xEBB,34,
  0xEBC,0xEBD,10, 0xEC0,0xEC4,34, 0xEC8,0xECB,30, 0xECD,0xECD,1,
  0xECE,0xECE,28, 0xED0,0xED9,23, 0xEDC,0xEDF,4, 0xF20,0xF33,23,
  0xF35,0xF35,28, 0xF37,0xF37,28, 0xF39,0xF39,22, 0xF40,0xF47,4,
  0xF49,0xF6C,4, 0xF71,0xF7D,34, 0xF7E,0xF7E,1, 0xF7F,0xF7F,32,
  0xF80,0xF81,34, 0xF82,0xF83,1, 0xF84,0xF84,25, 0xF85,0xF85,0,
  0xF88,0xF8C,7, 0xF8D,0xF97,14, 0xF99,0xFBC,14, 0xFC6,0xFC6,28,
  0x1000,0x1020,4, 0x1021,0x102A,35, 0x102B,0x1035,34, 0x1036,0x1036,1,
  0x1037,0x1037,30, 0x1038,0x1038,32, 0x1039,0x1039,18, 0x103A,0x103A,25,
  0x103B,0x103E,10, 0x103F,0x103F,4, 0x1040,0x1049,23, 0x104B,0x104B,11,
  0x104E,0x104E,11, 0x1050,0x1051,4, 0x1052,0x1055,35, 0x1056,0x1059,34,
  0x105A,0x105D,4, 0x105E,0x1060,10, 0x1061,0x1061,4, 0x1062,0x1062,34,
  0x1063,0x1064,30, 0x1065,0x1066,4, 0x1067,0x1068,34, 0x1069,0x106D,30,
  0x106E,0x1070,4, 0x1071,0x1074,34, 0x1075,0x1081,4, 0x1082,0x1082,10,
  0x1083,0x1086,34, 0x1087,0x108D,30, 0x108E,0x108E,4, 0x108F,0x108F,30,
  0x1090,0x1099,23, 0x109A,0x109B,30, 0x109C,0x109D,34, 0x1700,0x1702,35,
  0x1703,0x1711,4, 0x1712,0x1713,34, 0x1714,0x1715,25, 0x171F,0x171F,4,
  0x1720,0x1722,35, 0x1723,0x1731,4, 0x1732,0x1733,34, 0x1734,0x1734,25,
  0x1740,0x1742,35, 0x1743,0x1751,4, 0x1752,0x1753,34, 0x1760,0x1762,35,
  0x1763,0x176C,4, 0x176E,0x1770,4, 0x1772,0x1773,34, 0x1780,0x17A2,4,
  0x17A3,0x17B3,35, 0x17B6,0x17C5,34, 0x17C6,0x17C6,1, 0x17C7,0x17C7,32,
  0x17C8,0x17C8,34, 0x17C9,0x17CA,26, 0x17CB,0x17CB,28, 0x17CC,0x17CC,15,
  0x17CD,0x17CD,9, 0x17CE,0x17D0,28, 0x17D1,0x17D1,25, 0x17D2,0x17D2,18,
  0x17D3,0x17D3,28, 0x17DC,0x17DC,0, 0x17DD,0x17DD,28, 0x17E0,0x17E9,23,
  0x1900,0x191E,4, 0x1920,0x1928,34, 0x1929,0x192B,14, 0x1930,0x1931,6,
  0x1932,0x1932,1, 0x1933,0x1939,6, 0x193A,0x193A,34, 0x193B,0x193B,28,
  0x1946,0x194F,23, 0x1950,0x1962,4, 0x1963,0x196D,33, 0x1970,0x1974,29,
  0x1980,0x19AB,4, 0x19B0,0x19C0,34, 0x19C1,0x19C7,6, 0x19C8,0x19C9,30,
  0x19D0,0x19DA,23, 0x1A00,0x1A16,4, 0x1A17,0x1A1B,34, 0x1A20,0x1A4C,4,
  0x1A4D,0x1A52,35, 0x1A53,0x1A54,4, 0x1A55,0x1A56,10, 0x1A57,0x1A57,14,
  0x1A58,0x1A59,6, 0x1A5A,0x1A5A,8, 0x1A5B,0x1A5E,14, 0x1A60,0x1A60,18,
  0x1A61,0x1A73,34, 0x1A74,0x1A74,1, 0x1A75,0x1A79,30, 0x1A7A,0x1A7A,25,
  0x1A7B,0x1A7C,28, 0x1A7F,0x1A7F,28, 0x1A80,0x1A89,23, 0x1A90,0x1A99,23,
  0x1B00,0x1B02,1, 0x1B03,0x1B03,6, 0x1B04,0x1B04,32, 0x1B05,0x1B12,35,
  0x1B13,0x1B33,4, 0x1B34,0x1B34,22, 0x1B35,0x1B43,34, 0x1B44,0x1B44,31,
  0x1B45,0x1B4C,4, 0x1B50,0x1B59,23, 0x1B80,0x1B80,1, 0x1B81,0x1B81,6,
  0x1B82,0x1B82,32, 0x1B83,0x1B89,35, 0x1B8A,0x1BA0,4, 0x1BA1,0x1BA3,14,
  0x1BA4,0x1BA9,34, 0x1BAA,0x1BAA,25, 0x1BAB,0x1BAB,18, 0x1BAC,0x1BAD,14,
  0x1BAE,0x1BAF,4, 0x1BB0,0x1BB9,23, 0x1BBA,0x1BBA,0, 0x1BBB,0x1BBD,4,
  0x1BBE,0x1BBF,6, 0x1BC0,0x1BE3,4, 0x1BE4,0x1BE5,35, 0x1BE6,0x1BE6,22,
  0x1BE7,0x1BEF,34, 0x1BF0,0x1BF1,6, 0x1BF2,0x1BF3,27, 0x1C00,0x1C23,4,
  0x1C24,0x1C25,14, 0x1C26,0x1C2C,34, 0x1C2D,0x1C33,6, 0x1C34,0x1C35,1,
  0x1C36,0x1C36,28, 0x1C37,0x1C37,22, 0x1C40,0x1C49,23, 0x1C4D,0x1C4F,4,
  0x1CD0,0x1CD2,3, 0x1CD4,0x1CE1,3, 0x1CF2,0x1CF3,5, 0x1CF4,0x1CF4,3,
  0x1CF5,0x1CF6,16, 0x1CF7,0x1CF9,3, 0x1CFA,0x1CFA,11, 0x1DFB,0x1DFB,28,
  0x200C,0x200C,21, 0x200D,0x200D,19, 0x2010,0x2014,11, 0x2074,0x2074,28,
  0x2082,0x2084,28, 0x20F0,0x20F0,3, 0x25CC,0x25CC,11, 0xA800,0xA801,35,
  0xA802,0xA802,34, 0xA803,0xA805,35, 0xA806,0xA806,31, 0xA807,0xA80A,4,
  0xA80B,0xA80B,1, 0xA80C,0xA822,4, 0xA823,0xA827,34, 0xA82C,0xA82C,25,
  0xA840,0xA85D,4, 0xA85E,0xA861,33, 0xA862,0xA865,4, 0xA866,0xA866,33,
  0xA867,0xA868,14, 0xA869,0xA870,4, 0xA871,0xA871,14, 0xA872,0xA872,4,
  0xA873,0xA873,1, 0xA880,0xA880,1, 0xA881,0xA881,32, 0xA882,0xA891,35,
  0xA892,0xA8B3,4, 0xA8B4,0xA8B4,10, 0xA8B5,0xA8C3,34, 0xA8C4,0xA8C4,31,
  0xA8C5,0xA8C5,1, 0xA8D0,0xA8D9,23, 0xA8E0,0xA8F1,3, 0xA8F2,0xA8F3,1,
  0xA8FE,0xA8FE,35, 0xA8FF,0xA8FF,34, 0xA900,0xA909,23, 0xA90A,0xA921,4,
  0xA922,0xA92A,33, 0xA92B,0xA92D,30, 0xA930,0xA946,4, 0xA947,0xA94E,34,
  0xA94F,0xA952,6, 0xA953,0xA953,25, 0xA980,0xA981,1, 0xA982,0xA982,6,
  0xA983,0xA983,32, 0xA984,0xA988,35, 0xA989,0xA98B,4, 0xA98C,0xA98E,35,
  0xA98F,0xA9B2,4, 0xA9B3,0xA9B3,22, 0xA9B4,0xA9BC,34, 0xA9BD,0xA9BF,10,
  0xA9C0,0xA9C0,31, 0xA9D0,0xA9D9,23, 0xA9E0,0xA9E4,4, 0xA9E5,0xA9E5,34,
  0xA9E7,0xA9EF,4, 0xA9F0,0xA9F9,23, 0xA9FA,0xA9FE,4, 0xAA00,0xAA05,35,
  0xAA06,0xAA28,4, 0xAA29,0xAA32,34, 0xAA33,0xAA36,10, 0xAA40,0xAA4D,6,
  0xAA50,0xAA59,23, 0xAA60,0xAA6F,4, 0xAA71,0xAA73,4, 0xAA74,0xAA76,11,
  0xAA7A,0xAA7A,4, 0xAA7B,0xAA7D,30, 0xAA7E,0xAAAF,4, 0xAAB0,0xAABE,34,
  0xAABF,0xAABF,30, 0xAAC0,0xAAC0,29, 0xAAC1,0xAAC1,30, 0xAAC2,0xAAC2,29,
  0xAAE0,0xAAE1,35, 0xAAE2,0xAAEA,4, 0xAAEB,0xAAEF,34, 0xAAF5,0xAAF5,32,
  0xAAF6,0xAAF6,18, 0xABC0,0xABCD,4, 0xABCE,0xABCF,35, 0xABD0,0xABD0,4,
  0xABD1,0xABD1,35, 0xABD2,0xABDA,4, 0xABDB,0xABE2,6, 0xABE3,0xABEA,34,
  0xABEC,0xABEC,30, 0xABED,0xABED,25, 0xABF0,0xABF9,23, 0x10A00,0x10A00,4,
  0x10A01,0x10A03,34, 0x10A05,0x10A06,34, 0x10A0C,0x10A0D,34,
  0x10A0E,0x10A0E,1, 0x10A0F,0x10A0F,32, 0x10A10,0x10A13,4,
  0x10A15,0x10A17,4, 0x10A19,0x10A35,4, 0x10A38,0x10A3A,22,
  0x10A3F,0x10A3F,18, 0x10A40,0x10A48,23, 0x11000,0x11001,1,
  0x11002,0x11002,32, 0x11003,0x11004,16, 0x11005,0x11012,35,
  0x11013,0x11037,4, 0x11038,0x11045,34, 0x11046,0x11046,31,
  0x11052,0x11065,2, 0x11066,0x1106F,23, 0x11070,0x11070,25,
  0x11071,0x11072,35, 0x11073,0x11074,34, 0x11075,0x11075,4,
  0x1107F,0x1107F,24, 0x11080,0x11081,1, 0x11082,0x11082,32,
  0x11083,0x1108C,35, 0x1108D,0x110AF,4, 0x110B0,0x110B8,34,
  0x110B9,0x110B9,31, 0x110BA,0x110BA,22, 0x110C2,0x110C2,34,
  0x11100,0x11101,1, 0x11102,0x11102,32, 0x11103,0x11106,35,
  0x11107,0x11126,4, 0x11127,0x11132,34, 0x11133,0x11133,18,
  0x11134,0x11134,25, 0x11136,0x1113F,23, 0x11144,0x11144,4,
  0x11145,0x11146,34, 0x11147,0x11147,4, 0x11150,0x11154,33,
  0x11155,0x11172,4, 0x11173,0x11173,22, 0x11180,0x11181,1,
  0x11182,0x11182,32, 0x11183,0x11190,35, 0x11191,0x111B2,4,
  0x111B3,0x111BF,34, 0x111C0,0x111C0,31, 0x111C1,0x111C1,0,
  0x111C2,0x111C3,13, 0x111C9,0x111C9,28, 0x111CA,0x111CA,22,
  0x111CB,0x111CC,34, 0x111CE,0x111CE,34, 0x111CF,0x111CF,1,
  0x111D0,0x111D9,23, 0x111E1,0x111F4,23, 0x11200,0x11207,35,
  0x11208,0x11211,4, 0x11213,0x1122B,4, 0x1122C,0x11233,34,
  0x11234,0x11234,1, 0x11235,0x11235,31, 0x11236,0x11236,22,
  0x11237,0x11237,17, 0x1123E,0x1123E,3, 0x1123F,0x1123F,4,
  0x11240,0x11240,35, 0x11241,0x11241,34, 0x11280,0x11283,35,
  0x11284,0x11286,4, 0x11288,0x11288,4, 0x1128A,0x1128D,4,
  0x1128F,0x1129D,4, 0x1129F,0x112A8,4, 0x112B0,0x112B9,35,
  0x112BA,0x112DE,4, 0x112DF,0x112DF,1, 0x112E0,0x112E8,34,
  0x112E9,0x112E9,22, 0x112EA,0x112EA,25, 0x112F0,0x112F9,23,
  0x11300,0x11302,1, 0x11303,0x11303,32, 0x11305,0x1130C,35,
  0x1130F,0x11310,35, 0x11313,0x11314,35, 0x11315,0x11328,4,
  0x1132A,0x11330,4, 0x11332,0x11333,4, 0x11335,0x11339,4,
  0x1133B,0x1133C,22, 0x1133D,0x1133D,0, 0x1133E,0x11344,34,
  0x11347,0x11348,34, 0x1134B,0x1134C,34, 0x1134D,0x1134D,31,
  0x11357,0x11357,34, 0x1135E,0x1135F,1, 0x11360,0x11361,35,
  0x11362,0x11363,34, 0x11366,0x1136C,3, 0x11370,0x11374,3,
  0x11380,0x11389,35, 0x1138B,0x1138B,35, 0x1138E,0x1138E,35,
  0x11390,0x11391,35, 0x11392,0x113B5,4, 0x113B7,0x113B7,0,
  0x113B8,0x113C0,34, 0x113C2,0x113C2,34, 0x113C5,0x113C5,34,
  0x113C7,0x113C9,34, 0x113CA,0x113CA,1, 0x113CC,0x113CC,1,
  0x113CD,0x113CD,32, 0x113CE,0x113CF,25, 0x113D0,0x113D0,18,
  0x113D1,0x113D1,12, 0x113D2,0x113D2,17, 0x113E1,0x113E2,3,
  0x11400,0x1140D,35, 0x1140E,0x11434,4, 0x11435,0x11441,34,
  0x11442,0x11442,31, 0x11443,0x11444,1, 0x11445,0x11445,32,
  0x11446,0x11446,22, 0x11447,0x11447,0, 0x11450,0x11459,23,
  0x1145E,0x1145E,28, 0x1145F,0x1145F,1, 0x11460,0x11461,16,
  0x11481,0x1148E,35, 0x1148F,0x114AF,4, 0x114B0,0x114BE,34,
  0x114BF,0x114C0,1, 0x114C1,0x114C1,32, 0x114C2,0x114C2,31,
  0x114C3,0x114C3,22, 0x114C4,0x114C4,0, 0x114D0,0x114D9,23,
  0x11580,0x1158D,35, 0x1158E,0x115AE,4, 0x115AF,0x115B5,34,
  0x115B8,0x115BB,34, 0x115BC,0x115BD,1, 0x115BE,0x115BE,32,
  0x115BF,0x115BF,31, 0x115C0,0x115C0,22, 0x115D8,0x115DB,35,
  0x115DC,0x115DD,34, 0x11600,0x1160D,35, 0x1160E,0x1162F,4,
  0x11630,0x1163C,34, 0x1163D,0x1163D,1, 0x1163E,0x1163E,32,
  0x1163F,0x1163F,31, 0x11640,0x11640,34, 0x11650,0x11659,23,
  0x11680,0x11689,35, 0x1168A,0x116AA,4, 0x116AB,0x116AB,1,
  0x116AC,0x116AC,32, 0x116AD,0x116B5,34, 0x116B6,0x116B6,31,
  0x116B7,0x116B7,22, 0x116B8,0x116B8,4, 0x116C0,0x116C9,23,
  0x116D0,0x116E3,23, 0x11700,0x1171A,4, 0x1171D,0x1171F,10,
  0x11720,0x1172A,34, 0x1172B,0x1172B,25, 0x11730,0x1173B,23,
  0x11740,0x11746,4, 0x11800,0x11809,35, 0x1180A,0x1182B,4,
  0x1182C,0x11836,34, 0x11837,0x11837,1, 0x11838,0x11838,32,
  0x11839,0x11839,31, 0x1183A,0x1183A,22, 0x11900,0x11906,35,
  0x11909,0x11909,35, 0x1190C,0x11913,4, 0x11915,0x11916,4,
  0x11918,0x1192F,4, 0x11930,0x11935,34, 0x11937,0x11938,34,
  0x1193B,0x1193C,1, 0x1193D,0x1193D,25, 0x1193E,0x1193E,18,
  0x1193F,0x1193F,13, 0x11940,0x11940,10, 0x11941,0x11941,12,
  0x11942,0x11942,10, 0x11943,0x11943,22, 0x11950,0x11959,23,
  0x119A0,0x119A7,35, 0x119AA,0x119AD,35, 0x119AE,0x119D0,4,
  0x119D1,0x119D7,34, 0x119DA,0x119DD,34, 0x119DE,0x119DE,1,
  0x119DF,0x119DF,32, 0x119E0,0x119E0,31, 0x119E1,0x119E1,0,
  0x119E4,0x119E4,34, 0x11A00,0x11A00,4, 0x11A01,0x11A0A,34,
  0x11A0B,0x11A32,4, 0x11A33,0x11A33,28, 0x11A34,0x11A34,25,
  0x11A35,0x11A38,1, 0x11A39,0x11A39,32, 0x11A3A,0x11A3A,16,
  0x11A3B,0x11A3E,10, 0x11A3F,0x11A3F,11, 0x11A45,0x11A45,11,
  0x11A47,0x11A47,18, 0x11A50,0x11A50,4, 0x11A51,0x11A5B,34,
  0x11A5C,0x11A83,4, 0x11A84,0x11A85,13, 0x11A86,0x11A86,12,
  0x11A87,0x11A89,13, 0x11A8A,0x11A95,6, 0x11A96,0x11A96,1,
  0x11A97,0x11A97,32, 0x11A98,0x11A98,17, 0x11A99,0x11A99,18,
  0x11A9D,0x11A9D,0, 0x11B60,0x11B67,34, 0x11C00,0x11C08,35,
  0x11C0A,0x11C0D,35, 0x11C0E,0x11C2E,4, 0x11C2F,0x11C36,34,
  0x11C38,0x11C3B,34, 0x11C3C,0x11C3D,1, 0x11C3E,0x11C3E,32,
  0x11C3F,0x11C3F,31, 0x11C40,0x11C40,0, 0x11C50,0x11C6C,23,
  0x11C72,0x11C8F,4, 0x11C92,0x11CA7,14, 0x11CA9,0x11CAF,14,
  0x11CB0,0x11CB4,34, 0x11CB5,0x11CB6,1, 0x11D00,0x11D06,35,
  0x11D08,0x11D09,35, 0x11D0B,0x11D0B,35, 0x11D0C,0x11D30,4,
  0x11D31,0x11D36,34, 0x11D3A,0x11D3A,34, 0x11D3C,0x11D3D,34,
  0x11D3F,0x11D3F,34, 0x11D40,0x11D40,1, 0x11D41,0x11D41,32,
  0x11D42,0x11D42,22, 0x11D43,0x11D43,34, 0x11D44,0x11D44,25,
  0x11D45,0x11D45,18, 0x11D46,0x11D46,12, 0x11D47,0x11D47,10,
  0x11D50,0x11D59,23, 0x11D60,0x11D65,35, 0x11D67,0x11D68,35,
  0x11D6A,0x11D6B,35, 0x11D6C,0x11D89,4, 0x11D8A,0x11D8E,34,
  0x11D90,0x11D91,34, 0x11D93,0x11D94,34, 0x11D95,0x11D95,1,
  0x11D96,0x11D96,32, 0x11D97,0x11D97,18, 0x11DA0,0x11DA9,23,
  0x11EE0,0x11EF1,4, 0x11EF2,0x11EF2,11, 0x11EF3,0x11EF6,34,
  0x11F00,0x11F01,1, 0x11F02,0x11F02,12, 0x11F03,0x11F03,32,
  0x11F04,0x11F10,35, 0x11F12,0x11F33,4, 0x11F34,0x11F3A,34,
  0x11F3E,0x11F40,34, 0x11F41,0x11F41,25, 0x11F42,0x11F42,18,
  0x11F50,0x11F59,23, 0x11F5A,0x11F5A,22, 0x16100,0x16100,35,
  0x16101,0x1611D,4, 0x1611E,0x16129,34, 0x1612A,0x1612C,10,
  0x1612D,0x1612D,1, 0x1612E,0x1612E,10, 0x1612F,0x1612F,25,
  0x16130,0x16139,23, 0x16D40,0x16D41,1, 0x16D42,0x16D42,32,
  0x16D43,0x16D62,4, 0x16D63,0x16D6A,34, 0x16D6B,0x16D6C,25,
  0x16D70,0x16D79,23
];

/* UIPC -- 650 ranges, 15 values, generated from ../ucd/IndicPositionalCategory.txt */
export const UIPC_NAMES = [
  'Bottom', 'Bottom_And_Left', 'Bottom_And_Right', 'Left',
  'Left_And_Right', 'Overstruck', 'Right', 'Top', 'Top_And_Bottom',
  'Top_And_Bottom_And_Left', 'Top_And_Bottom_And_Right', 'Top_And_Left',
  'Top_And_Left_And_Right', 'Top_And_Right', 'Visual_Order_Left'
];
const UIPC_R = [
  0x900,0x902,7, 0x903,0x903,6, 0x93A,0x93A,7, 0x93B,0x93B,6,
  0x93C,0x93C,0, 0x93E,0x93E,6, 0x93F,0x93F,3, 0x940,0x940,6,
  0x941,0x944,0, 0x945,0x948,7, 0x949,0x94C,6, 0x94D,0x94D,0,
  0x94E,0x94E,3, 0x94F,0x94F,6, 0x951,0x951,7, 0x952,0x952,0,
  0x955,0x955,7, 0x956,0x957,0, 0x962,0x963,0, 0x981,0x981,7,
  0x982,0x983,6, 0x9BC,0x9BC,0, 0x9BE,0x9BE,6, 0x9BF,0x9BF,3,
  0x9C0,0x9C0,6, 0x9C1,0x9C4,0, 0x9C7,0x9C8,3, 0x9CB,0x9CC,4,
  0x9CD,0x9CD,0, 0x9D7,0x9D7,6, 0x9E2,0x9E3,0, 0x9FE,0x9FE,7,
  0xA01,0xA02,7, 0xA03,0xA03,6, 0xA3C,0xA3C,0, 0xA3E,0xA3E,6,
  0xA3F,0xA3F,3, 0xA40,0xA40,6, 0xA41,0xA42,0, 0xA47,0xA48,7,
  0xA4B,0xA4C,7, 0xA4D,0xA4D,0, 0xA51,0xA51,0, 0xA70,0xA71,7,
  0xA75,0xA75,0, 0xA81,0xA82,7, 0xA83,0xA83,6, 0xABC,0xABC,0,
  0xABE,0xABE,6, 0xABF,0xABF,3, 0xAC0,0xAC0,6, 0xAC1,0xAC4,0,
  0xAC5,0xAC5,7, 0xAC7,0xAC8,7, 0xAC9,0xAC9,13, 0xACB,0xACC,6,
  0xACD,0xACD,0, 0xAE2,0xAE3,0, 0xAFA,0xAFF,7, 0xB01,0xB01,7,
  0xB02,0xB03,6, 0xB3C,0xB3C,0, 0xB3E,0xB3E,6, 0xB3F,0xB3F,7,
  0xB40,0xB40,6, 0xB41,0xB44,0, 0xB47,0xB47,3, 0xB48,0xB48,11,
  0xB4B,0xB4B,4, 0xB4C,0xB4C,12, 0xB4D,0xB4D,0, 0xB55,0xB56,7,
  0xB57,0xB57,13, 0xB62,0xB63,0, 0xB82,0xB82,7, 0xBBE,0xBBF,6,
  0xBC0,0xBC0,7, 0xBC1,0xBC2,6, 0xBC6,0xBC8,3, 0xBCA,0xBCC,4,
  0xBCD,0xBCD,7, 0xBD7,0xBD7,6, 0xC00,0xC00,7, 0xC01,0xC03,6,
  0xC04,0xC04,7, 0xC3C,0xC3C,0, 0xC3E,0xC40,7, 0xC41,0xC44,6,
  0xC46,0xC47,7, 0xC48,0xC48,8, 0xC4A,0xC4D,7, 0xC55,0xC55,7,
  0xC56,0xC56,0, 0xC62,0xC63,0, 0xC81,0xC81,7, 0xC82,0xC83,6,
  0xCBC,0xCBC,0, 0xCBE,0xCBE,6, 0xCBF,0xCBF,7, 0xCC0,0xCC0,13,
  0xCC1,0xCC4,6, 0xCC6,0xCC6,7, 0xCC7,0xCC8,13, 0xCCA,0xCCB,13,
  0xCCC,0xCCD,7, 0xCD5,0xCD6,6, 0xCE2,0xCE3,0, 0xCF3,0xCF3,6,
  0xD00,0xD01,7, 0xD02,0xD03,6, 0xD3B,0xD3C,7, 0xD3E,0xD40,6,
  0xD41,0xD44,0, 0xD46,0xD48,3, 0xD4A,0xD4C,4, 0xD4D,0xD4E,7,
  0xD57,0xD57,6, 0xD62,0xD63,0, 0xD81,0xD81,7, 0xD82,0xD83,6,
  0xDCA,0xDCA,7, 0xDCF,0xDD1,6, 0xDD2,0xDD3,7, 0xDD4,0xDD4,0,
  0xDD6,0xDD6,0, 0xDD8,0xDD8,6, 0xDD9,0xDD9,3, 0xDDA,0xDDA,11,
  0xDDB,0xDDB,3, 0xDDC,0xDDC,4, 0xDDD,0xDDD,12, 0xDDE,0xDDE,4,
  0xDDF,0xDDF,6, 0xDF2,0xDF3,6, 0xE30,0xE30,6, 0xE31,0xE31,7,
  0xE32,0xE33,6, 0xE34,0xE37,7, 0xE38,0xE3A,0, 0xE40,0xE44,14,
  0xE45,0xE45,6, 0xE47,0xE4E,7, 0xEB0,0xEB0,6, 0xEB1,0xEB1,7,
  0xEB2,0xEB3,6, 0xEB4,0xEB7,7, 0xEB8,0xEBA,0, 0xEBB,0xEBB,7,
  0xEBC,0xEBC,0, 0xEC0,0xEC4,14, 0xEC8,0xECE,7, 0xF18,0xF19,0,
  0xF35,0xF35,0, 0xF37,0xF37,0, 0xF39,0xF39,7, 0xF3E,0xF3E,6,
  0xF3F,0xF3F,3, 0xF71,0xF71,0, 0xF72,0xF72,7, 0xF73,0xF73,8,
  0xF74,0xF75,0, 0xF76,0xF79,8, 0xF7A,0xF7E,7, 0xF7F,0xF7F,6,
  0xF80,0xF80,7, 0xF81,0xF81,8, 0xF82,0xF83,7, 0xF84,0xF84,0,
  0xF86,0xF87,7, 0xF8D,0xF97,0, 0xF99,0xFBC,0, 0xFC6,0xFC6,0,
  0x102B,0x102C,6, 0x102D,0x102E,7, 0x102F,0x1030,0, 0x1031,0x1031,3,
  0x1032,0x1036,7, 0x1037,0x1037,0, 0x1038,0x1038,6, 0x103A,0x103A,7,
  0x103B,0x103B,6, 0x103C,0x103C,9, 0x103D,0x103E,0, 0x1056,0x1057,6,
  0x1058,0x1059,0, 0x105E,0x1060,0, 0x1062,0x1064,6, 0x1067,0x106D,6,
  0x1071,0x1074,7, 0x1082,0x1082,0, 0x1083,0x1083,6, 0x1084,0x1084,3,
  0x1085,0x1086,7, 0x1087,0x108C,6, 0x108D,0x108D,0, 0x108F,0x108F,6,
  0x109A,0x109C,6, 0x109D,0x109D,7, 0x1712,0x1712,7, 0x1713,0x1714,0,
  0x1715,0x1715,6, 0x1732,0x1732,7, 0x1733,0x1733,0, 0x1734,0x1734,6,
  0x1752,0x1752,7, 0x1753,0x1753,0, 0x1772,0x1772,7, 0x1773,0x1773,0,
  0x17B6,0x17B6,6, 0x17B7,0x17BA,7, 0x17BB,0x17BD,0, 0x17BE,0x17BE,11,
  0x17BF,0x17BF,12, 0x17C0,0x17C0,4, 0x17C1,0x17C3,3, 0x17C4,0x17C5,4,
  0x17C6,0x17C6,7, 0x17C7,0x17C8,6, 0x17C9,0x17D1,7, 0x17D3,0x17D3,7,
  0x17DD,0x17DD,7, 0x1920,0x1921,7, 0x1922,0x1922,0, 0x1923,0x1924,6,
  0x1925,0x1926,13, 0x1927,0x1928,7, 0x1929,0x192B,6, 0x1930,0x1931,6,
  0x1932,0x1932,0, 0x1933,0x1938,6, 0x1939,0x1939,0, 0x193A,0x193A,7,
  0x193B,0x193B,0, 0x19B0,0x19B4,6, 0x19B5,0x19B7,14, 0x19B8,0x19B9,6,
  0x19BA,0x19BA,14, 0x19BB,0x19C0,6, 0x19C8,0x19C9,6, 0x1A17,0x1A17,7,
  0x1A18,0x1A18,0, 0x1A19,0x1A19,3, 0x1A1A,0x1A1A,6, 0x1A1B,0x1A1B,7,
  0x1A55,0x1A55,3, 0x1A56,0x1A56,0, 0x1A57,0x1A57,6, 0x1A58,0x1A5A,7,
  0x1A5B,0x1A5E,0, 0x1A61,0x1A61,6, 0x1A62,0x1A62,7, 0x1A63,0x1A64,6,
  0x1A65,0x1A68,7, 0x1A69,0x1A6A,0, 0x1A6B,0x1A6B,7, 0x1A6C,0x1A6C,0,
  0x1A6D,0x1A6D,6, 0x1A6E,0x1A72,3, 0x1A73,0x1A7C,7, 0x1A7F,0x1A7F,0,
  0x1B00,0x1B03,7, 0x1B04,0x1B04,6, 0x1B34,0x1B34,7, 0x1B35,0x1B35,6,
  0x1B36,0x1B37,7, 0x1B38,0x1B3A,0, 0x1B3B,0x1B3B,2, 0x1B3C,0x1B3C,8,
  0x1B3D,0x1B3D,10, 0x1B3E,0x1B3F,3, 0x1B40,0x1B41,4, 0x1B42,0x1B42,7,
  0x1B43,0x1B43,13, 0x1B44,0x1B44,6, 0x1B6B,0x1B6B,7, 0x1B6C,0x1B6C,0,
  0x1B6D,0x1B73,7, 0x1B80,0x1B81,7, 0x1B82,0x1B82,6, 0x1BA1,0x1BA1,6,
  0x1BA2,0x1BA3,0, 0x1BA4,0x1BA4,7, 0x1BA5,0x1BA5,0, 0x1BA6,0x1BA6,3,
  0x1BA7,0x1BA7,6, 0x1BA8,0x1BA9,7, 0x1BAA,0x1BAA,6, 0x1BAC,0x1BAD,0,
  0x1BE6,0x1BE6,7, 0x1BE7,0x1BE7,6, 0x1BE8,0x1BE9,7, 0x1BEA,0x1BEC,6,
  0x1BED,0x1BED,7, 0x1BEE,0x1BEE,6, 0x1BEF,0x1BF1,7, 0x1BF2,0x1BF3,6,
  0x1C24,0x1C26,6, 0x1C27,0x1C28,3, 0x1C29,0x1C29,11, 0x1C2A,0x1C2B,6,
  0x1C2C,0x1C2C,0, 0x1C2D,0x1C33,7, 0x1C34,0x1C35,3, 0x1C36,0x1C36,7,
  0x1C37,0x1C37,0, 0x1CD0,0x1CD2,7, 0x1CD4,0x1CD4,5, 0x1CD5,0x1CD9,0,
  0x1CDA,0x1CDB,7, 0x1CDC,0x1CDF,0, 0x1CE0,0x1CE0,7, 0x1CE1,0x1CE1,6,
  0x1CE2,0x1CE8,5, 0x1CED,0x1CED,0, 0x1CF4,0x1CF4,7, 0x1CF7,0x1CF7,6,
  0x1DFB,0x1DFB,7, 0x20F0,0x20F0,7, 0xA802,0xA802,7, 0xA806,0xA806,7,
  0xA80B,0xA80B,7, 0xA823,0xA824,6, 0xA825,0xA825,0, 0xA826,0xA826,7,
  0xA827,0xA827,6, 0xA82C,0xA82C,0, 0xA880,0xA881,6, 0xA8B4,0xA8C3,6,
  0xA8C4,0xA8C4,0, 0xA8C5,0xA8C5,7, 0xA8E0,0xA8F1,7, 0xA8FF,0xA8FF,7,
  0xA926,0xA92A,7, 0xA92B,0xA92D,0, 0xA947,0xA949,0, 0xA94A,0xA94A,7,
  0xA94B,0xA94E,0, 0xA94F,0xA951,7, 0xA952,0xA953,6, 0xA980,0xA982,7,
  0xA983,0xA983,6, 0xA9B3,0xA9B3,7, 0xA9B4,0xA9B5,6, 0xA9B6,0xA9B7,7,
  0xA9B8,0xA9B9,0, 0xA9BA,0xA9BB,3, 0xA9BC,0xA9BC,7, 0xA9BD,0xA9BD,0,
  0xA9BE,0xA9BE,2, 0xA9BF,0xA9BF,1, 0xA9C0,0xA9C0,2, 0xA9E5,0xA9E5,7,
  0xAA29,0xAA2C,7, 0xAA2D,0xAA2D,0, 0xAA2E,0xAA2E,7, 0xAA2F,0xAA30,3,
  0xAA31,0xAA31,7, 0xAA32,0xAA32,0, 0xAA33,0xAA33,6, 0xAA34,0xAA34,3,
  0xAA35,0xAA36,0, 0xAA43,0xAA43,7, 0xAA4C,0xAA4C,7, 0xAA4D,0xAA4D,6,
  0xAA7B,0xAA7B,6, 0xAA7C,0xAA7C,7, 0xAA7D,0xAA7D,6, 0xAAB0,0xAAB0,7,
  0xAAB1,0xAAB1,6, 0xAAB2,0xAAB3,7, 0xAAB4,0xAAB4,0, 0xAAB5,0xAAB6,14,
  0xAAB7,0xAAB8,7, 0xAAB9,0xAAB9,14, 0xAABA,0xAABA,6, 0xAABB,0xAABC,14,
  0xAABD,0xAABD,6, 0xAABE,0xAABF,7, 0xAAC1,0xAAC1,7, 0xAAEB,0xAAEB,3,
  0xAAEC,0xAAEC,0, 0xAAED,0xAAED,7, 0xAAEE,0xAAEE,3, 0xAAEF,0xAAEF,6,
  0xAAF5,0xAAF5,6, 0xABE3,0xABE4,6, 0xABE5,0xABE5,7, 0xABE6,0xABE7,6,
  0xABE8,0xABE8,0, 0xABE9,0xABEA,6, 0xABEC,0xABEC,6, 0xABED,0xABED,0,
  0x10A01,0x10A01,5, 0x10A02,0x10A03,0, 0x10A05,0x10A05,7,
  0x10A06,0x10A06,5, 0x10A0C,0x10A0E,0, 0x10A0F,0x10A0F,7,
  0x10A38,0x10A38,7, 0x10A39,0x10A3A,0, 0x11000,0x11000,6,
  0x11001,0x11001,7, 0x11002,0x11002,6, 0x11038,0x1103B,7,
  0x1103C,0x11041,0, 0x11042,0x11046,7, 0x11070,0x11070,7,
  0x11073,0x11074,7, 0x11080,0x11081,7, 0x11082,0x11082,6,
  0x110B0,0x110B0,6, 0x110B1,0x110B1,3, 0x110B2,0x110B2,6,
  0x110B3,0x110B4,0, 0x110B5,0x110B6,7, 0x110B7,0x110B8,6,
  0x110B9,0x110BA,0, 0x110C2,0x110C2,0, 0x11100,0x11102,7,
  0x11127,0x11129,7, 0x1112A,0x1112B,0, 0x1112C,0x1112C,3,
  0x1112D,0x1112D,7, 0x1112E,0x1112F,8, 0x11130,0x11130,7,
  0x11131,0x11132,0, 0x11134,0x11134,7, 0x11145,0x11146,6,
  0x11173,0x11173,0, 0x11180,0x11181,7, 0x11182,0x11182,6,
  0x111B3,0x111B3,6, 0x111B4,0x111B4,3, 0x111B5,0x111B5,6,
  0x111B6,0x111BB,0, 0x111BC,0x111BE,7, 0x111BF,0x111BF,13,
  0x111C0,0x111C0,6, 0x111C2,0x111C3,7, 0x111C9,0x111CA,0,
  0x111CB,0x111CB,7, 0x111CC,0x111CC,0, 0x111CE,0x111CE,3,
  0x111CF,0x111CF,7, 0x1122C,0x1122E,6, 0x1122F,0x1122F,0,
  0x11230,0x11231,7, 0x11232,0x11233,13, 0x11234,0x11234,7,
  0x11235,0x11235,6, 0x11236,0x11237,7, 0x1123E,0x1123E,7,
  0x11241,0x11241,0, 0x112DF,0x112DF,7, 0x112E0,0x112E0,6,
  0x112E1,0x112E1,3, 0x112E2,0x112E2,6, 0x112E3,0x112E4,0,
  0x112E5,0x112E8,7, 0x112E9,0x112EA,0, 0x11300,0x11301,7,
  0x11302,0x11303,6, 0x1133B,0x1133C,0, 0x1133E,0x1133F,6,
  0x11340,0x11340,7, 0x11341,0x11344,6, 0x11347,0x11348,3,
  0x1134B,0x1134C,4, 0x1134D,0x1134D,6, 0x11357,0x11357,6,
  0x11362,0x11363,6, 0x11366,0x1136C,7, 0x11370,0x11374,7,
  0x113B8,0x113B8,6, 0x113B9,0x113BA,13, 0x113BB,0x113C0,0,
  0x113C2,0x113C2,3, 0x113C5,0x113C5,3, 0x113C7,0x113C8,4,
  0x113C9,0x113CA,6, 0x113CC,0x113CD,6, 0x113CE,0x113CE,7,
  0x113CF,0x113CF,6, 0x113D1,0x113D1,7, 0x113D2,0x113D2,0,
  0x113E1,0x113E1,7, 0x113E2,0x113E2,0, 0x11435,0x11435,6,
  0x11436,0x11436,3, 0x11437,0x11437,6, 0x11438,0x1143D,0,
  0x1143E,0x1143F,7, 0x11440,0x11441,6, 0x11442,0x11442,0,
  0x11443,0x11444,7, 0x11445,0x11445,6, 0x11446,0x11446,0,
  0x1145E,0x1145E,7, 0x114B0,0x114B0,6, 0x114B1,0x114B1,3,
  0x114B2,0x114B2,6, 0x114B3,0x114B8,0, 0x114B9,0x114B9,3,
  0x114BA,0x114BA,7, 0x114BB,0x114BB,11, 0x114BC,0x114BC,4,
  0x114BD,0x114BD,6, 0x114BE,0x114BE,4, 0x114BF,0x114C0,7,
  0x114C1,0x114C1,6, 0x114C2,0x114C3,0, 0x115AF,0x115AF,6,
  0x115B0,0x115B0,3, 0x115B1,0x115B1,6, 0x115B2,0x115B5,0,
  0x115B8,0x115B8,3, 0x115B9,0x115B9,11, 0x115BA,0x115BA,4,
  0x115BB,0x115BB,12, 0x115BC,0x115BD,7, 0x115BE,0x115BE,6,
  0x115BF,0x115C0,0, 0x115DC,0x115DD,0, 0x11630,0x11632,6,
  0x11633,0x11638,0, 0x11639,0x1163A,7, 0x1163B,0x1163C,6,
  0x1163D,0x1163D,7, 0x1163E,0x1163E,6, 0x1163F,0x1163F,0,
  0x11640,0x11640,7, 0x116AB,0x116AB,7, 0x116AC,0x116AC,6,
  0x116AD,0x116AD,7, 0x116AE,0x116AE,3, 0x116AF,0x116AF,6,
  0x116B0,0x116B1,0, 0x116B2,0x116B5,7, 0x116B6,0x116B6,6,
  0x116B7,0x116B7,0, 0x1171D,0x1171D,0, 0x1171E,0x1171E,9,
  0x1171F,0x1171F,7, 0x11720,0x11721,6, 0x11722,0x11723,7,
  0x11724,0x11725,0, 0x11726,0x11726,3, 0x11727,0x11727,7,
  0x11728,0x11728,0, 0x11729,0x1172B,7, 0x1182C,0x1182C,6,
  0x1182D,0x1182D,3, 0x1182E,0x1182E,6, 0x1182F,0x11832,0,
  0x11833,0x11837,7, 0x11838,0x11838,6, 0x11839,0x1183A,0,
  0x11930,0x11934,6, 0x11935,0x11935,3, 0x11937,0x11937,3,
  0x11938,0x11938,4, 0x1193B,0x1193C,7, 0x1193D,0x1193D,6,
  0x1193F,0x1193F,7, 0x11940,0x11940,6, 0x11941,0x11941,7,
  0x11942,0x11942,2, 0x11943,0x11943,0, 0x119D1,0x119D1,6,
  0x119D2,0x119D2,3, 0x119D3,0x119D3,6, 0x119D4,0x119D7,0,
  0x119DA,0x119DB,7, 0x119DC,0x119DF,6, 0x119E0,0x119E0,0,
  0x119E4,0x119E4,3, 0x11A01,0x11A01,7, 0x11A02,0x11A03,0,
  0x11A04,0x11A09,7, 0x11A0A,0x11A0A,0, 0x11A33,0x11A34,0,
  0x11A35,0x11A38,7, 0x11A39,0x11A39,6, 0x11A3B,0x11A3E,0,
  0x11A51,0x11A51,7, 0x11A52,0x11A53,0, 0x11A54,0x11A56,7,
  0x11A57,0x11A58,6, 0x11A59,0x11A5B,0, 0x11A84,0x11A89,7,
  0x11A8A,0x11A95,0, 0x11A96,0x11A96,7, 0x11A97,0x11A97,6,
  0x11A98,0x11A98,7, 0x11B60,0x11B60,7, 0x11B61,0x11B61,6,
  0x11B62,0x11B63,0, 0x11B64,0x11B64,7, 0x11B65,0x11B65,6,
  0x11B66,0x11B66,7, 0x11B67,0x11B67,6, 0x11C2F,0x11C2F,6,
  0x11C30,0x11C31,7, 0x11C32,0x11C36,0, 0x11C38,0x11C3D,7,
  0x11C3E,0x11C3E,6, 0x11C3F,0x11C3F,0, 0x11C92,0x11CA7,0,
  0x11CA9,0x11CA9,6, 0x11CAA,0x11CB0,0, 0x11CB1,0x11CB1,3,
  0x11CB2,0x11CB2,0, 0x11CB3,0x11CB3,7, 0x11CB4,0x11CB4,6,
  0x11CB5,0x11CB6,7, 0x11D31,0x11D35,7, 0x11D36,0x11D36,0,
  0x11D3A,0x11D3A,7, 0x11D3C,0x11D3D,7, 0x11D3F,0x11D41,7,
  0x11D42,0x11D42,0, 0x11D43,0x11D43,7, 0x11D44,0x11D44,0,
  0x11D46,0x11D46,6, 0x11D47,0x11D47,0, 0x11D8A,0x11D8E,6,
  0x11D90,0x11D91,7, 0x11D93,0x11D94,6, 0x11D95,0x11D95,7,
  0x11D96,0x11D96,6, 0x11EF3,0x11EF3,7, 0x11EF4,0x11EF4,0,
  0x11EF5,0x11EF5,3, 0x11EF6,0x11EF6,6, 0x11F00,0x11F02,7,
  0x11F03,0x11F03,6, 0x11F34,0x11F35,6, 0x11F36,0x11F37,7,
  0x11F38,0x11F3A,0, 0x11F3E,0x11F3F,3, 0x11F40,0x11F40,7,
  0x11F41,0x11F41,6, 0x11F5A,0x11F5A,7, 0x1611E,0x16129,7,
  0x1612A,0x1612B,3, 0x1612C,0x1612C,6, 0x1612D,0x1612D,7,
  0x1612E,0x1612F,0, 0x16D40,0x16D42,6, 0x16D63,0x16D6C,6
];

function rangeLookup( R, cp ) {
  let lo = 0, hi = ( R.length / 3 ) - 1;
  while ( lo <= hi ) {
    const mid = ( lo + hi ) >> 1, p = mid * 3;
    if ( cp < R[p] ) hi = mid - 1;
    else if ( cp > R[p + 1] ) lo = mid + 1;
    else return R[p + 2];
  }
  return -1;
}

/* UISC default is Other; UIPC default is NA. */
export function uisc( cp ) { const i = rangeLookup( UISC_R, cp ); return i < 0 ? -1 : i; }
export function uipc( cp ) { const i = rangeLookup( UIPC_R, cp ); return i < 0 ? -1 : i; }
export function uiscName( cp ) { const i = uisc( cp ); return i < 0 ? 'Other' : UISC_NAMES[i]; }
export function uipcName( cp ) { const i = uipc( cp ); return i < 0 ? 'NA' : UIPC_NAMES[i]; }

/* Numeric ids for the UISC values the shapers actually branch on.  Resolved
 * once from the generated name table so a change in the UCD cannot silently
 * shift an index out from under the algorithms. */
const U = ( n ) => { const i = UISC_NAMES.indexOf( n ); return i; };
export const UISC = {
  Avagraha: U( 'Avagraha' ), Bindu: U( 'Bindu' ),
  Brahmi_Joining_Number: U( 'Brahmi_Joining_Number' ),
  Cantillation_Mark: U( 'Cantillation_Mark' ), Consonant: U( 'Consonant' ),
  Consonant_Dead: U( 'Consonant_Dead' ), Consonant_Final: U( 'Consonant_Final' ),
  Consonant_Head_Letter: U( 'Consonant_Head_Letter' ),
  Consonant_Initial_Postfixed: U( 'Consonant_Initial_Postfixed' ),
  Consonant_Killer: U( 'Consonant_Killer' ),
  Consonant_Medial: U( 'Consonant_Medial' ),
  Consonant_Placeholder: U( 'Consonant_Placeholder' ),
  Consonant_Preceding_Repha: U( 'Consonant_Preceding_Repha' ),
  Consonant_Prefixed: U( 'Consonant_Prefixed' ),
  Consonant_Subjoined: U( 'Consonant_Subjoined' ),
  Consonant_Succeeding_Repha: U( 'Consonant_Succeeding_Repha' ),
  Consonant_With_Stacker: U( 'Consonant_With_Stacker' ),
  Gemination_Mark: U( 'Gemination_Mark' ),
  Invisible_Stacker: U( 'Invisible_Stacker' ), Joiner: U( 'Joiner' ),
  Modifying_Letter: U( 'Modifying_Letter' ), Non_Joiner: U( 'Non_Joiner' ),
  Nukta: U( 'Nukta' ), Number: U( 'Number' ), Number_Joiner: U( 'Number_Joiner' ),
  Pure_Killer: U( 'Pure_Killer' ), Register_Shifter: U( 'Register_Shifter' ),
  Reordering_Killer: U( 'Reordering_Killer' ),
  Syllable_Modifier: U( 'Syllable_Modifier' ), Tone_Letter: U( 'Tone_Letter' ),
  Tone_Mark: U( 'Tone_Mark' ), Virama: U( 'Virama' ), Visarga: U( 'Visarga' ),
  Vowel: U( 'Vowel' ), Vowel_Dependent: U( 'Vowel_Dependent' ),
  Vowel_Independent: U( 'Vowel_Independent' ),
  Other: -1,
};

const P = ( n ) => UIPC_NAMES.indexOf( n );
export const UIPC = {
  Bottom: P( 'Bottom' ), Bottom_And_Left: P( 'Bottom_And_Left' ),
  Bottom_And_Right: P( 'Bottom_And_Right' ), Left: P( 'Left' ),
  Left_And_Right: P( 'Left_And_Right' ), Overstruck: P( 'Overstruck' ),
  Right: P( 'Right' ), Top: P( 'Top' ), Top_And_Bottom: P( 'Top_And_Bottom' ),
  Top_And_Bottom_And_Left: P( 'Top_And_Bottom_And_Left' ),
  Top_And_Bottom_And_Right: P( 'Top_And_Bottom_And_Right' ),
  Top_And_Left: P( 'Top_And_Left' ),
  Top_And_Left_And_Right: P( 'Top_And_Left_And_Right' ),
  Top_And_Right: P( 'Top_And_Right' ),
  Visual_Order_Left: P( 'Visual_Order_Left' ),
  NA: -1,
};

/* The two range tables above are the RAW Unicode properties.  The shapers no
 * longer read them at run time: each engine needs a category that is a
 * FUNCTION of these properties and of General_Category, Joining_Type and
 * Default_Ignorable_Code_Point, and that function is evaluated once, offline,
 * into the two derived tables further down (OT_TABLE here, USE_TABLE in
 * shaper-use.js).  They are kept, and exported as uisc() / uipc() /
 * uiscName() / uipcName(), because they are what those derived tables are
 * derived FROM: a reader who wants to check a single character's USE class
 * against the specification needs its Indic_Syllabic_Category and
 * Indic_Positional_Category, and this is where they are. */

/* ==================================================================== */
/*  Imports.  This module must NOT import shape-plan.js: shape-plan.js    */
/*  imports the shapers, and a module cycle through its `await import`    */
/*  at module scope would leave these bindings uninitialised.             */
/* ==================================================================== */

import { GC, script as scriptOfCp } from './unicode.js';
import { mergeClusterRange, makeInfo, makePos,
         SCRATCH_HAS_BROKEN_SYLLABLE } from './buffer.js';


/* ==================================================================== */
/*  A cluster-grammar scanner, shared by the four syllabic shapers       */
/* ==================================================================== */

/* Every one of the scripts with a dedicated engine -- Devanagari and its
 * relatives, Khmer, Myanmar, and the Universal Shaping Engine -- describes
 * its clusters as a REGULAR GRAMMAR over character categories, and every one
 * of those grammars has overlapping alternatives: in Khmer a HALANT both
 * continues a cluster and, alone, forms one; in Myanmar a medial group is a
 * prefix of a longer one.  The engine therefore has to keep every live
 * alternative and take the LONGEST accepting prefix, which is what an NFA
 * does and what recursive descent with committed choices does not.  All four
 * shapers build their rule list with the combinators below and scan with
 * makeScanner().
 *
 * Each combinator returns a THUNK rather than a fragment so that a
 * sub-grammar named twice (Khmer's xgroup appears three times in one rule)
 * gets its own states at each use.
 */
let _nfaStates = 0;
function _st() { return { id: _nfaStates++, eps: [], tr: null, any: null }; }

export function gLit( c ) {
  return () => { const a = _st(), b = _st(); a.tr = [ c, b ]; return { s: a, e: b }; };
}
export function gAny() {
  return () => { const a = _st(), b = _st(); a.any = b; return { s: a, e: b }; };
}
export function gSeq( ...fs ) {
  return () => {
    let first = null, prev = null;
    for ( const f of fs ) {
      const fr = f();
      if ( !first ) first = fr.s; else prev.eps.push( fr.s );
      prev = fr.e;
    }
    if ( !first ) { const a = _st(); return { s: a, e: a }; }
    return { s: first, e: prev };
  };
}
export function gAlt( ...fs ) {
  return () => {
    const a = _st(), b = _st();
    for ( const f of fs ) { const fr = f(); a.eps.push( fr.s ); fr.e.eps.push( b ); }
    return { s: a, e: b };
  };
}
export function gStar( f ) {
  return () => { const a = _st(); const fr = f(); a.eps.push( fr.s ); fr.e.eps.push( a );
                 return { s: a, e: a }; };
}
export function gOpt( f ) {
  return () => { const a = _st(), b = _st(); const fr = f();
                 a.eps.push( fr.s ); a.eps.push( b ); fr.e.eps.push( b );
                 return { s: a, e: b }; };
}
export function gPlus( f ) { return gSeq( f, gStar( f ) ); }

/* One scanner per shaper, built once at module load.  `rules` is an ordered
 * list of { f, t }: the ragel scanner's semantics are longest match wins and,
 * on a tie, the rule listed first. */
export function makeScanner( rules ) {
  const frags = rules.map( ( r ) => r.f() );
  const types = rules.map( ( r ) => r.t );
  let stamp = new Int32Array( _nfaStates );
  let gen = 0;

  const closure = ( list, g ) => {
    for ( let i = 0; i < list.length; i++ ) {
      const eps = list[ i ].eps;
      for ( let k = 0; k < eps.length; k++ ) {
        const t = eps[ k ];
        if ( stamp[ t.id ] !== g ) { stamp[ t.id ] = g; list.push( t ); }
      }
    }
  };

  const longest = ( frag, cats, i, n ) => {
    if ( stamp.length < _nfaStates ) {
      const s = new Int32Array( _nfaStates ); s.set( stamp ); stamp = s;
    }
    if ( gen > 0x7FFFFF00 ) { stamp.fill( 0 ); gen = 0; }
    let g = ++gen;
    let cur = [ frag.s ];
    stamp[ frag.s.id ] = g;
    closure( cur, g );
    let last = stamp[ frag.e.id ] === g ? 0 : -1;
    for ( let k = i; k < n; k++ ) {
      const c = cats[ k ];
      const ng = ++gen;
      const nxt = [];
      for ( let x = 0; x < cur.length; x++ ) {
        const s = cur[ x ];
        if ( s.any && stamp[ s.any.id ] !== ng ) { stamp[ s.any.id ] = ng; nxt.push( s.any ); }
        if ( s.tr && s.tr[ 0 ] === c ) {
          const t = s.tr[ 1 ];
          if ( stamp[ t.id ] !== ng ) { stamp[ t.id ] = ng; nxt.push( t ); }
        }
      }
      if ( !nxt.length ) break;
      closure( nxt, ng );
      cur = nxt; g = ng;
      if ( stamp[ frag.e.id ] === g ) last = k + 1 - i;
    }
    return last;
  };

  /* The longest match at `i`, as { len, type }.  A rule that matches nothing
   * (every ragel scanner has an `other = any` catch-all, so this cannot be
   * reached from a well-formed rule list) falls back to one token. */
  return function scan( cats, i, n, fallbackType ) {
    let bestLen = 0, bestType = fallbackType;
    for ( let r = 0; r < frags.length; r++ ) {
      const len = longest( frags[ r ], cats, i, n );
      if ( len > bestLen ) { bestLen = len; bestType = types[ r ]; }
    }
    if ( bestLen <= 0 ) return { len: 1, type: fallbackType };
    return { len: bestLen, type: bestType };
  };
}


/* ==================================================================== */
/*  The Indic character table, shared by Indic, Khmer and Myanmar        */
/* ==================================================================== */

/* The three dedicated engines take their categories from ONE table, keyed by
 * Indic_Syllabic_Category and Indic_Positional_Category, restricted to the
 * blocks those engines own (the nine Indic blocks, Khmer, Myanmar and their
 * extensions, plus Vedic Extensions, the punctuation and superscript blocks
 * the specs name, and U+00A0 and U+25CC).  A code point outside them is X /
 * POS_END, which is what makes a stray Latin letter a non-cluster.
 *
 * The derivation is mechanical and is described by the script-development
 * chapters:  the syllabic category chooses the letter (Consonant -> C,
 * Virama -> H, Vowel_Dependent -> M, ...), the positional category chooses
 * the visual slot, a matra's slot is then moved to where that script actually
 * reorders it (sd-devanagari.txt l.301-339 and the equivalent tables in the
 * other chapters -- Telugu and Kannada place theirs BEFORE the subjoined
 * forms, Bengali and Malayalam AFTER the post-base ones), and Khmer and
 * Myanmar keep the slot IN the category instead (VPre / VAbv / VBlw / VPst)
 * because their grammars sequence matras by position.  A short list of
 * characters the chapters call out by hand -- the RA of each script, the
 * Khmer Robatic / Xgroup / Ygroup signs, the Myanmar medials and tones --
 * overrides the derived value. */
export const OT = {
  X: 0, C: 1, V: 2, N: 3, H: 4, ZWNJ: 5, ZWJ: 6, M: 7, SM: 8, A: 9, VD: 9,
  PLACEHOLDER: 10, DOTTEDCIRCLE: 11, RS: 12, MPst: 13, Repha: 14, Ra: 15,
  CM: 16, Symbol: 17, CS: 18, SMPst: 57,
  VAbv: 20, VBlw: 21, VPre: 22, VPst: 23,
  Robatic: 25, Xgroup: 26, Ygroup: 27,
  IV: 2, DB: 3, GB: 10,
  As: 32, MH: 35, MR: 36, MW: 37, MY: 38, PT: 39, VS: 40, ML: 41,
};

/* ot_position_t: the visual slots of a syllable, left to right. */
export const OTPOS = {
  START: 0, RA_TO_BECOME_REPH: 1, PRE_M: 2, PRE_C: 3, BASE_C: 4,
  AFTER_MAIN: 5, ABOVE_C: 6, BEFORE_SUB: 7, BELOW_C: 8, AFTER_SUB: 9,
  BEFORE_POST: 10, POST_C: 11, AFTER_POST: 12, SMVD: 13, END: 14,
};

/* start, end, (category | position << 8) triples, ascending. */
const OT_TABLE = [
  0x2D,0x2D,0x40A, 0x30,0x39,0x40A, 0xA0,0xA0,0x40A, 0xB2,0xB3,0xD39,
  0xD7,0xD7,0x40A, 0x900,0x903,0xD08, 0x904,0x914,0x402, 0x915,0x92F,0x401,
  0x930,0x930,0x40F, 0x931,0x939,0x401, 0x93A,0x93B,0x907,
  0x93C,0x93C,0xE03, 0x93D,0x93D,0xD11, 0x93E,0x93E,0x907,
  0x93F,0x93F,0x207, 0x940,0x94C,0x907, 0x94D,0x94D,0x804,
  0x94E,0x94E,0x207, 0x94F,0x94F,0x907, 0x951,0x952,0xD09,
  0x953,0x954,0xD08, 0x955,0x957,0x907, 0x958,0x95F,0x401,
  0x960,0x961,0x402, 0x962,0x963,0x907, 0x966,0x96F,0x40A,
  0x972,0x977,0x402, 0x978,0x97F,0x401, 0x980,0x980,0x40A,
  0x981,0x983,0xD08, 0x985,0x98C,0x402, 0x98F,0x990,0x402,
  0x993,0x994,0x402, 0x995,0x9A8,0x401, 0x9AA,0x9AF,0x401,
  0x9B0,0x9B0,0x40F, 0x9B2,0x9B2,0x401, 0x9B6,0x9B9,0x401,
  0x9BC,0x9BC,0xE03, 0x9BD,0x9BD,0xD11, 0x9BE,0x9BE,0xC07,
  0x9BF,0x9BF,0x207, 0x9C0,0x9C0,0xC07, 0x9C1,0x9C4,0x907,
  0x9C7,0x9C8,0x207, 0x9CB,0x9CC,0xC07, 0x9CD,0x9CD,0x804,
  0x9CE,0x9CE,0x401, 0x9D7,0x9D7,0xC07, 0x9DC,0x9DD,0x401,
  0x9DF,0x9DF,0x401, 0x9E0,0x9E1,0x402, 0x9E2,0x9E3,0x907,
  0x9E6,0x9EF,0x40A, 0x9F0,0x9F0,0x40F, 0x9F1,0x9F1,0x401,
  0x9FC,0x9FC,0x40A, 0x9FE,0x9FE,0xD08, 0xA01,0xA03,0xD08,
  0xA05,0xA0A,0x402, 0xA0F,0xA10,0x402, 0xA13,0xA14,0x402,
  0xA15,0xA28,0x401, 0xA2A,0xA2F,0x401, 0xA30,0xA30,0x40F,
  0xA32,0xA33,0x401, 0xA35,0xA36,0x401, 0xA38,0xA39,0x401,
  0xA3C,0xA3C,0xE03, 0xA3E,0xA3E,0xC07, 0xA3F,0xA3F,0x207,
  0xA40,0xA40,0xC0D, 0xA41,0xA42,0xC07, 0xA47,0xA48,0xC07,
  0xA4B,0xA4C,0xC07, 0xA4D,0xA4D,0x804, 0xA51,0xA51,0x807,
  0xA59,0xA5C,0x401, 0xA5E,0xA5E,0x401, 0xA66,0xA6F,0x40A,
  0xA70,0xA71,0xD08, 0xA72,0xA73,0x401, 0xA75,0xA75,0x410,
  0xA81,0xA83,0xD08, 0xA85,0xA8D,0x402, 0xA8F,0xA91,0x402,
  0xA93,0xA94,0x402, 0xA95,0xAA8,0x401, 0xAAA,0xAAF,0x401,
  0xAB0,0xAB0,0x40F, 0xAB2,0xAB3,0x401, 0xAB5,0xAB9,0x401,
  0xABC,0xABC,0xE03, 0xABD,0xABD,0xD11, 0xABE,0xABE,0xC07,
  0xABF,0xABF,0x207, 0xAC0,0xAC4,0xC07, 0xAC5,0xAC5,0x907,
  0xAC7,0xAC8,0x907, 0xAC9,0xAC9,0xC07, 0xACB,0xACC,0xC07,
  0xACD,0xACD,0x804, 0xAE0,0xAE1,0x402, 0xAE2,0xAE3,0xC07,
  0xAE6,0xAEF,0x40A, 0xAF9,0xAF9,0x401, 0xAFA,0xAFA,0xD09,
  0xAFB,0xAFB,0xE03, 0xAFC,0xAFC,0xD09, 0xAFD,0xAFF,0xE03,
  0xB01,0xB01,0x708, 0xB02,0xB03,0xD08, 0xB05,0xB0C,0x402,
  0xB0F,0xB10,0x402, 0xB13,0xB14,0x402, 0xB15,0xB28,0x401,
  0xB2A,0xB2F,0x401, 0xB30,0xB30,0x40F, 0xB32,0xB33,0x401,
  0xB35,0xB39,0x401, 0xB3C,0xB3C,0xE03, 0xB3D,0xB3D,0xD11,
  0xB3E,0xB3E,0xC07, 0xB3F,0xB3F,0x507, 0xB40,0xB40,0xC07,
  0xB41,0xB44,0x907, 0xB47,0xB47,0x207, 0xB48,0xB48,0x507,
  0xB4B,0xB4C,0xC07, 0xB4D,0xB4D,0x804, 0xB55,0xB55,0xE03,
  0xB56,0xB56,0x507, 0xB57,0xB57,0xC07, 0xB5C,0xB5D,0x401,
  0xB5F,0xB5F,0x401, 0xB60,0xB61,0x402, 0xB62,0xB63,0x907,
  0xB66,0xB6F,0x40A, 0xB71,0xB71,0x401, 0xB82,0xB82,0xD08,
  0xB85,0xB8A,0x402, 0xB8E,0xB90,0x402, 0xB92,0xB94,0x402,
  0xB95,0xB95,0x401, 0xB99,0xB9A,0x401, 0xB9C,0xB9C,0x401,
  0xB9E,0xB9F,0x401, 0xBA3,0xBA4,0x401, 0xBA8,0xBAA,0x401,
  0xBAE,0xBAF,0x401, 0xBB0,0xBB0,0x40F, 0xBB1,0xBB9,0x401,
  0xBBE,0xBBF,0xC07, 0xBC0,0xBC0,0x907, 0xBC1,0xBC2,0xC07,
  0xBC6,0xBC8,0x207, 0xBCA,0xBCC,0xC07, 0xBCD,0xBCD,0x604,
  0xBD7,0xBD7,0xC07, 0xBE6,0xBEF,0x40A, 0xC00,0xC04,0xD08,
  0xC05,0xC0C,0x402, 0xC0E,0xC10,0x402, 0xC12,0xC14,0x402,
  0xC15,0xC28,0x401, 0xC2A,0xC2F,0x401, 0xC30,0xC30,0x40F,
  0xC31,0xC39,0x401, 0xC3C,0xC3C,0xE03, 0xC3D,0xC3D,0xD11,
  0xC3E,0xC42,0x707, 0xC43,0xC44,0x907, 0xC46,0xC48,0x707,
  0xC4A,0xC4C,0x707, 0xC4D,0xC4D,0x604, 0xC55,0xC56,0x707,
  0xC58,0xC5A,0x401, 0xC5D,0xC5D,0x401, 0xC60,0xC61,0x402,
  0xC62,0xC63,0x707, 0xC66,0xC6F,0x40A, 0xC80,0xC80,0x40A,
  0xC81,0xC83,0xD08, 0xC85,0xC8C,0x402, 0xC8E,0xC90,0x402,
  0xC92,0xC94,0x402, 0xC95,0xCA8,0x401, 0xCAA,0xCAF,0x401,
  0xCB0,0xCB0,0x40F, 0xCB1,0xCB3,0x401, 0xCB5,0xCB9,0x401,
  0xCBC,0xCBC,0xE03, 0xCBD,0xCBD,0xD11, 0xCBE,0xCC2,0x707,
  0xCC3,0xCC4,0x907, 0xCC6,0xCC6,0x707, 0xCC7,0xCC8,0x907,
  0xCCA,0xCCB,0x907, 0xCCC,0xCCC,0x707, 0xCCD,0xCCD,0x604,
  0xCD5,0xCD6,0x907, 0xCDD,0xCDE,0x401, 0xCE0,0xCE1,0x402,
  0xCE2,0xCE3,0x707, 0xCE6,0xCEF,0x40A, 0xCF1,0xCF2,0x412,
  0xCF3,0xCF3,0xD08, 0xD00,0xD03,0xD08, 0xD04,0xD04,0x40A,
  0xD05,0xD0C,0x402, 0xD0E,0xD10,0x402, 0xD12,0xD14,0x402,
  0xD15,0xD2F,0x401, 0xD30,0xD30,0x40F, 0xD31,0xD3A,0x401,
  0xD3B,0xD3C,0x907, 0xD3D,0xD3D,0xD11, 0xD3E,0xD44,0xC07,
  0xD46,0xD48,0x207, 0xD4A,0xD4C,0xC07, 0xD4D,0xD4D,0x604,
  0xD4E,0xD4E,0xE0E, 0xD54,0xD56,0x401, 0xD57,0xD57,0xC07,
  0xD5F,0xD61,0x402, 0xD62,0xD63,0xC07, 0xD66,0xD6F,0x40A,
  0xD7A,0xD7F,0x401, 0x1000,0x1003,0x401, 0x1004,0x1004,0x40F,
  0x1005,0x101A,0x401, 0x101B,0x101B,0x40F, 0x101C,0x1020,0x401,
  0x1021,0x102A,0x402, 0x102B,0x102C,0xB17, 0x102D,0x102E,0x614,
  0x102F,0x1030,0x815, 0x1031,0x1031,0x316, 0x1032,0x1032,0xD09,
  0x1033,0x1035,0x614, 0x1036,0x1036,0xD09, 0x1037,0x1037,0xE03,
  0x1038,0x1038,0xD08, 0x1039,0x1039,0xE04, 0x103A,0x103A,0xE20,
  0x103B,0x103B,0xE26, 0x103C,0x103C,0xE24, 0x103D,0x103D,0xE25,
  0x103E,0x103E,0xE23, 0x103F,0x103F,0x401, 0x1040,0x104B,0x40A,
  0x104E,0x104E,0x401, 0x1050,0x1051,0x401, 0x1052,0x1055,0x402,
  0x1056,0x1057,0xB17, 0x1058,0x1059,0x815, 0x105A,0x105A,0x40F,
  0x105B,0x105D,0x401, 0x105E,0x105F,0xE26, 0x1060,0x1060,0xE29,
  0x1061,0x1061,0x401, 0x1062,0x1062,0xB17, 0x1063,0x1064,0xE27,
  0x1065,0x1066,0x401, 0x1067,0x1068,0xB17, 0x1069,0x106D,0xE27,
  0x106E,0x1070,0x401, 0x1071,0x1074,0x614, 0x1075,0x1081,0x401,
  0x1082,0x1082,0xE25, 0x1083,0x1083,0xB17, 0x1084,0x1084,0x316,
  0x1085,0x1086,0x614, 0x1087,0x108D,0xD08, 0x108E,0x108E,0x401,
  0x108F,0x108F,0xD08, 0x1090,0x1099,0x40A, 0x109A,0x109C,0xD08,
  0x109D,0x109D,0x614, 0x1780,0x1799,0x401, 0x179A,0x179A,0x40F,
  0x179B,0x17A2,0x401, 0x17A3,0x17B3,0x402, 0x17B6,0x17B6,0xB17,
  0x17B7,0x17BA,0x614, 0x17BB,0x17BD,0x815, 0x17BE,0x17BE,0x614,
  0x17BF,0x17C0,0xB17, 0x17C1,0x17C3,0x316, 0x17C4,0x17C5,0xB17,
  0x17C6,0x17C6,0xE1A, 0x17C7,0x17C8,0xE1B, 0x17C9,0x17CA,0xE19,
  0x17CB,0x17CB,0xE1A, 0x17CC,0x17CC,0xE19, 0x17CD,0x17D1,0xE1A,
  0x17D2,0x17D2,0xE04, 0x17D3,0x17D3,0xE1B, 0x17D9,0x17D9,0x40A,
  0x17DC,0x17DC,0xD11, 0x17DD,0x17DD,0xE1B, 0x17E0,0x17E9,0x40A,
  0x1CD0,0x1CD2,0xD09, 0x1CD4,0x1CE8,0xD09, 0x1CE9,0x1CEC,0xD11,
  0x1CED,0x1CED,0xD09, 0x1CEE,0x1CF1,0xD11, 0x1CF2,0x1CF3,0x401,
  0x1CF4,0x1CF4,0xD09, 0x1CF5,0x1CF6,0x412, 0x1CF7,0x1CF9,0xD09,
  0x1CFA,0x1CFA,0x40A, 0x200C,0x200C,0xE05, 0x200D,0x200D,0xE06,
  0x2010,0x2015,0x40A, 0x2022,0x2022,0x40A, 0x2074,0x2074,0xD39,
  0x2082,0x2084,0xD39, 0x25CC,0x25CC,0x40B, 0x25FB,0x25FE,0x40A,
  0xA8E0,0xA8F1,0xD09, 0xA8F2,0xA8F7,0xD11, 0xA8FE,0xA8FE,0x402,
  0xA8FF,0xA8FF,0x907, 0xA9E0,0xA9E4,0x401, 0xA9E5,0xA9E5,0x614,
  0xA9E7,0xA9EF,0x401, 0xA9F0,0xA9F9,0x40A, 0xA9FA,0xA9FE,0x401,
  0xAA60,0xAA6F,0x401, 0xAA71,0xAA73,0x401, 0xAA74,0xAA76,0x40A,
  0xAA7A,0xAA7A,0x401, 0xAA7B,0xAA7B,0xE27, 0xAA7C,0xAA7D,0xE03,
  0xAA7E,0xAA7F,0x401, 0xFE00,0xFE0F,0xE28, 0x11301,0x11303,0xD08,
  0x1133B,0x1133C,0xE03, 0x116D0,0x116E3,0x40A
];
const OT_DEFAULT = OT.X | ( OTPOS.END << 8 );

/* hb_indic_get_categories */
export function otCategories( cp ) {
  let lo = 0, hi = ( OT_TABLE.length / 3 ) - 1;
  while ( lo <= hi ) {
    const mid = ( lo + hi ) >> 1, k = mid * 3;
    if ( cp < OT_TABLE[ k ] ) hi = mid - 1;
    else if ( cp > OT_TABLE[ k + 1 ] ) lo = mid + 1;
    else return OT_TABLE[ k + 2 ];
  }
  return OT_DEFAULT;
}
export const otCategory = ( cp ) => otCategories( cp ) & 0xFF;
export const otPosition = ( cp ) => ( otCategories( cp ) >> 8 ) & 0xFF;

/* hb_syllabic_insert_dotted_circles, the shared half of what every syllabic
 * shaper does with a BROKEN verdict: one U+25CC per broken cluster, placed
 * after any repha the cluster starts with, carrying that cluster's cluster
 * number, mask and syllable so the run does not split.  Runs in a GSUB pause,
 * so `codepoint` is a glyph id and the record carries no unicode or glyph
 * properties -- exactly the reference's `hb_glyph_info_t dottedcircle = {0}`. */
export function insertDottedCircles( buffer, font, brokenType, circleCat,
                                     rephaCat, circlePos, catField, posField ) {
  const g = ( font && font.getNominalGlyph ) ? ( font.getNominalGlyph( 0x25CC ) || 0 ) : 0;
  if ( !g ) return false;
  const info = buffer.info;
  let lastSyllable = 0;
  let i = 0;
  let any = false;
  while ( i < info.length ) {
    const syl = info[ i ].syllable;
    if ( lastSyllable !== syl && ( syl & 0x0F ) === brokenType ) {
      lastSyllable = syl;
      let at = i;
      if ( rephaCat >= 0 )
        while ( at < info.length && info[ at ].syllable === syl &&
                info[ at ][ catField ] === rephaCat ) at++;
      const rec = makeInfo( g, info[ i ].cluster );
      rec.mask = info[ i ].mask;
      rec.syllable = syl;
      rec[ catField ] = circleCat;
      if ( posField && circlePos >= 0 ) rec[ posField ] = circlePos;
      info.splice( at, 0, rec );
      if ( buffer.pos ) buffer.pos.splice( at, 0, makePos() );
      if ( buffer.len !== undefined ) buffer.len += 1;
      any = true;
      i = at + 1;
    } else i++;
  }
  return any;
}

/* hb_ot_map_t::get_1_mask.  A feature the FONT does not list has no mask at
 * all, so every pass keyed on it is inert -- which is the difference between
 * "this font has no 'rphf'" and "this font's 'rphf' matched nothing". */
export function featureMask( plan, tag ) {
  const fm = plan && plan.featureMap && plan.featureMap.get( tag );
  if ( !fm ) return 0;
  if ( plan.gsubFound && !plan.gsubFound.has( tag ) ) return 0;
  return ( ( ( 1 << fm.shift ) >>> 0 ) & fm.mask ) >>> 0;
}

/* foreach_syllable: contiguous runs of equal info[i].syllable. */
export function forEachSyllable( buffer, fn ) {
  const info = buffer.info;
  const n = Math.min( info.length, buffer.len === undefined ? info.length
                                                           : Math.max( buffer.len, 0 ) ) || info.length;
  let start = 0;
  for ( let i = 1; i <= n; i++ )
    if ( i === n || info[ i ].syllable !== info[ start ].syllable ) {
      fn( start, i );
      start = i;
    }
}

/* hb_buffer_t::sort -- a stable insertion sort that MERGES the clusters of
 * everything an element moves past.  buffer.sort() cannot be called directly
 * from a GSUB pause: its cluster merge walks out from the range using
 * buffer.len and buffer.idx, both of which are stale mid-stage. */
export function sortRange( buffer, start, end, cmp ) {
  const info = buffer.info;
  if ( typeof buffer.sort !== 'function' ) {
    const part = info.slice( start, end );
    part.sort( cmp );
    for ( let i = 0; i < part.length; i++ ) info[ start + i ] = part[ i ];
    return;
  }
  const savedLen = buffer.len, savedIdx = buffer.idx;
  buffer.len = info.length; buffer.idx = 0;
  buffer.sort( start, end, cmp );
  buffer.len = savedLen; buffer.idx = savedIdx;
}

export function reverseRange( buffer, start, end ) {
  const info = buffer.info;
  for ( let i = start, j = end - 1; i < j; i++, j-- ) {
    const t = info[ i ]; info[ i ] = info[ j ]; info[ j ] = t;
  }
}

/* ==================================================================== */
/*  Prohibited vowel sequences -- ucd/IndicShapingInvalidCluster.txt      */
/* ==================================================================== */

/* sd-use.txt l.673-675 and sd-devanagari.txt l.463: an INDEPENDENT vowel
 * followed by a DEPENDENT vowel that together look like a different
 * independent vowel is a spoof, and the engine breaks it with a dotted
 * circle.  Which sequences those are is not derivable from any character
 * property; the UCD publishes them as a list, transcribed below in file
 * order as  length, code points...  repeated.
 *
 * THE LIST IS PER SCRIPT, keyed by the script of its FIRST code point, and
 * only the entries belonging to the RUN's script apply.  Applying the whole
 * list to every run -- which is what this engine did before -- makes a
 * Telugu-looking pair inside a Tai Tham run collect a circle that no shaper
 * emits.  Both shapers run this pass: it is preprocess_text for the Indic
 * shaper and for USE alike.
 *
 * The circle goes immediately BEFORE the LAST character of the prohibited
 * sequence and inherits that character's cluster, mask and unicode
 * properties minus the continuation bit.  Scanning resumes after the
 * sequence, so overlapping sequences take at most one circle; where a
 * prohibited sequence is a prefix of a longer one the SHORTER wins, which is
 * why the candidate lists are sorted by length. */
const INVALID_SEQ = [
  2, 0x0905, 0x0946, 2, 0x0905, 0x093E, 3, 0x0930, 0x094D, 0x0907, 2,
  0x0909, 0x0941, 2, 0x090F, 0x0945, 2, 0x090F, 0x0946, 2, 0x090F, 0x0947,
  2, 0x0905, 0x0949, 2, 0x0906, 0x0945, 2, 0x0905, 0x094A, 2, 0x0906,
  0x0946, 2, 0x0905, 0x094B, 2, 0x0906, 0x0947, 2, 0x0905, 0x094C, 2,
  0x0906, 0x0948, 2, 0x0905, 0x0945, 2, 0x0905, 0x093A, 2, 0x0905, 0x093B,
  2, 0x0906, 0x093A, 2, 0x0905, 0x094F, 2, 0x0905, 0x0956, 2, 0x0905,
  0x0957, 2, 0x0985, 0x09BE, 2, 0x098B, 0x09C3, 2, 0x098C, 0x09E2, 2,
  0x0A05, 0x0A3E, 2, 0x0A72, 0x0A3F, 2, 0x0A72, 0x0A40, 2, 0x0A73, 0x0A41,
  2, 0x0A73, 0x0A42, 2, 0x0A72, 0x0A47, 2, 0x0A05, 0x0A48, 2, 0x0A73,
  0x0A4B, 2, 0x0A05, 0x0A4C, 2, 0x0A85, 0x0ABE, 2, 0x0A85, 0x0AC5, 2,
  0x0A85, 0x0AC7, 2, 0x0A85, 0x0AC8, 2, 0x0A85, 0x0AC9, 2, 0x0A85, 0x0ACB,
  3, 0x0A85, 0x0ABE, 0x0AC5, 2, 0x0A85, 0x0ACC, 3, 0x0A85, 0x0ABE, 0x0AC8,
  2, 0x0AC5, 0x0ABE, 2, 0x0B05, 0x0B3E, 2, 0x0B0F, 0x0B57, 2, 0x0B13,
  0x0B57, 2, 0x0B85, 0x0BC2, 2, 0x0C12, 0x0C55, 2, 0x0C12, 0x0C4C, 2,
  0x0C3F, 0x0C55, 2, 0x0C46, 0x0C55, 2, 0x0C4A, 0x0C55, 2, 0x0C89, 0x0CBE,
  2, 0x0C92, 0x0CCC, 2, 0x0C8B, 0x0CBE, 2, 0x0D07, 0x0D57, 2, 0x0D09,
  0x0D57, 2, 0x0D0E, 0x0D46, 2, 0x0D12, 0x0D3E, 2, 0x0D12, 0x0D57, 2,
  0x0D85, 0x0DCF, 2, 0x0D85, 0x0DD0, 2, 0x0D85, 0x0DD1, 2, 0x0D8B, 0x0DDF,
  2, 0x0D8D, 0x0DD8, 2, 0x0D8F, 0x0DDF, 2, 0x0D91, 0x0DCA, 2, 0x0D91,
  0x0DD9, 2, 0x0D91, 0x0DDA, 2, 0x0D91, 0x0DDC, 2, 0x0D91, 0x0DDD, 2,
  0x0D91, 0x0DDE, 2, 0x0D94, 0x0DDF, 2, 0x11005, 0x11038, 2, 0x1100B,
  0x1103E, 2, 0x1100F, 0x11042, 2, 0x11680, 0x116AD, 2, 0x11686, 0x116B2,
  2, 0x11680, 0x116B4, 2, 0x11680, 0x116B5, 2, 0x11200, 0x1122C, 2,
  0x11240, 0x1122E, 2, 0x11206, 0x1122C, 2, 0x11200, 0x11231, 2, 0x11200,
  0x11233, 3, 0x11200, 0x1122C, 0x11231, 2, 0x1122C, 0x11230, 2, 0x1122C,
  0x11231, 2, 0x112B0, 0x112E0, 2, 0x112B0, 0x112E5, 2, 0x112B0, 0x112E6,
  2, 0x112B0, 0x112E7, 2, 0x112B0, 0x112E8, 2, 0x11481, 0x114B0, 2,
  0x114AA, 0x114B5, 2, 0x114AA, 0x114B6, 2, 0x1148B, 0x114BA, 2, 0x1148D,
  0x114BA, 2, 0x11600, 0x11639, 2, 0x11600, 0x1163A, 2, 0x11601, 0x11639,
  2, 0x11601, 0x1163A
];

/* Mirrors shape-plan.js's unicode_props packing. */
const UPROPS_MASK_CONTINUATION = 0x0080;

let VC_BY_SCRIPT = null;
function vcByScript() {
  if ( VC_BY_SCRIPT ) return VC_BY_SCRIPT;
  const byScript = new Map();
  for ( let i = 0; i < INVALID_SEQ.length; ) {
    const n = INVALID_SEQ[ i++ ];
    const seq = INVALID_SEQ.slice( i, i + n );
    i += n;
    const sc = scriptOfCp( seq[ 0 ] );
    let m = byScript.get( sc );
    if ( !m ) { m = new Map(); byScript.set( sc, m ); }
    let list = m.get( seq[ 0 ] );
    if ( !list ) { list = []; m.set( seq[ 0 ], list ); }
    list.push( seq );
  }
  for ( const m of byScript.values() )
    for ( const v of m.values() ) v.sort( ( a, b ) => a.length - b.length );
  VC_BY_SCRIPT = byScript;
  return byScript;
}

/* Runs while `codepoint` is still a CHARACTER and before normalization, so
 * the record it splices in is the character U+25CC and the normalizer and
 * glyph mapping handle it like any other input character. */
export function preprocessVowelConstraints( plan, buffer, font ) {
  if ( !buffer || !buffer.info ) return false;
  const firsts = vcByScript().get( scriptCodeOf( plan ) );
  if ( !firsts ) return false;
  const info = buffer.info;
  let n = buffer.len === undefined ? info.length : buffer.len;
  let changed = false;
  let i = 0;
  while ( i + 1 < n ) {
    const cands = firsts.get( info[ i ].codepoint );
    let hit = 0;
    if ( cands ) {
      for ( const seq of cands ) {
        if ( i + seq.length > n ) continue;
        let ok = true;
        for ( let k = 1; k < seq.length; k++ )
          if ( info[ i + k ].codepoint !== seq[ k ] ) { ok = false; break; }
        if ( ok ) { hit = seq.length; break; }
      }
    }
    if ( !hit ) { i++; continue; }
    const at = i + hit - 1;
    const rec = makeInfo( 0x25CC, info[ at ].cluster );
    rec.mask = info[ at ].mask;
    rec.unicodeProps = ( info[ at ].unicodeProps || 0 ) & ~UPROPS_MASK_CONTINUATION;
    info.splice( at, 0, rec );
    if ( buffer.pos ) buffer.pos.splice( at, 0, makePos() );
    if ( buffer.len !== undefined ) buffer.len += 1;
    n++;
    changed = true;
    i = at + 2;
  }
  return changed;
}

/* ==================================================================== */
/*  The Indic shaper -- sd-devanagari.txt, plus the per-script           */
/*  reordering tables from sd-{bengali,gujarati,gurmukhi,kannada,        */
/*  malayalam,oriya,tamil,telugu}.txt.                                  */
/* ==================================================================== */

import { selectScript, selectLangSys, featureWouldSubstitute } from './ot-layout.js';

/* Mirrors shape-plan.js's F_* (no import: shape-plan.js imports us). */
const F_NONE = 0, F_GLOBAL = 1, F_MANUAL_ZWNJ = 4, F_MANUAL_ZWJ = 8,
      F_PER_SYLLABLE = 64;
const F_MANUAL_JOINERS = F_MANUAL_ZWNJ | F_MANUAL_ZWJ;
/* Mirrors ot-layout.js's GlyphProps bits. */
const GP_SUBSTITUTED = 0x0010, GP_LIGATED = 0x0020, GP_MULTIPLIED = 0x0040;
/* Mirrors shape-plan.js's unicode_props packing. */
const UPROPS_MASK_GEN_CAT = 0x001F;

const ligated = ( inf ) => !!( inf.props & GP_LIGATED );
const multiplied = ( inf ) => !!( inf.props & GP_MULTIPLIED );
const ligatedAndDidntMultiply = ( inf ) => ligated( inf ) && !multiplied( inf );

/* "If it ligated, all bets are off": a glyph a ligature consumed no longer
 * belongs to any of the categories the reorderer branches on. */
const isOneOf = ( inf, set ) => !ligated( inf ) && set.has( inf.indicCategory );

/* Vowels and placeholders are treated as consonants.  A vowel cannot occur in
 * a consonant syllable, so the two can share one reordering routine. */
const CONSONANTS = new Set( [ OT.C, OT.CS, OT.Ra, OT.CM, OT.V, OT.PLACEHOLDER,
                              OT.DOTTEDCIRCLE ] );
const JOINERS = new Set( [ OT.ZWJ, OT.ZWNJ ] );
const HALANTS = new Set( [ OT.H ] );
const MATRAS = new Set( [ OT.M, OT.MPst ] );
const RA_ONLY = new Set( [ OT.Ra ] );
const ZWJ_ONLY = new Set( [ OT.ZWJ ] );
const N_OR_H = new Set( [ OT.N, OT.H ] );
const MATRA_OR_HALANT = new Set( [ OT.M, OT.MPst, OT.H ] );
/* The marks that carry no position of their own and travel with whatever
 * precedes them. */
const TRAVELS = new Set( [ OT.ZWJ, OT.ZWNJ, OT.N, OT.RS, OT.CM, OT.H ] );

const isConsonant = ( inf ) => isOneOf( inf, CONSONANTS );
const isJoiner = ( inf ) => isOneOf( inf, JOINERS );
const isHalant = ( inf ) => isOneOf( inf, HALANTS );


/* -------------------------------------------------------------------- */
/*  Per-script configuration -- the reph position and spec generation     */
/* -------------------------------------------------------------------- */

/* Where each script puts the reph, from the "Reordering" section of each
 * chapter; which sequence forms one (an implicit Ra + Halant, an explicit
 * Ra + Halant + ZWJ in Telugu, or an encoded Repha in Malayalam); and whether
 * the below-forms feature reaches pre-base consonants as well as post-base
 * ones. */
const REPH_POS = {
  AFTER_MAIN: OTPOS.AFTER_MAIN, BEFORE_SUB: OTPOS.BEFORE_SUB,
  AFTER_SUB: OTPOS.AFTER_SUB, BEFORE_POST: OTPOS.BEFORE_POST,
  AFTER_POST: OTPOS.AFTER_POST,
};
const REPH_IMPLICIT = 0, REPH_EXPLICIT = 1, REPH_LOG_REPHA = 2;
const BLWF_PRE_AND_POST = 0, BLWF_POST_ONLY = 1;

const INDIC_CONFIGS = {
  DFLT: { oldSpec: false, virama: 0, rephPos: REPH_POS.BEFORE_POST,
          rephMode: REPH_IMPLICIT, blwfMode: BLWF_PRE_AND_POST },
  Deva: { oldSpec: true, virama: 0x094D, rephPos: REPH_POS.BEFORE_POST,
          rephMode: REPH_IMPLICIT, blwfMode: BLWF_PRE_AND_POST },
  Beng: { oldSpec: true, virama: 0x09CD, rephPos: REPH_POS.AFTER_SUB,
          rephMode: REPH_IMPLICIT, blwfMode: BLWF_PRE_AND_POST },
  Guru: { oldSpec: true, virama: 0x0A4D, rephPos: REPH_POS.BEFORE_SUB,
          rephMode: REPH_IMPLICIT, blwfMode: BLWF_PRE_AND_POST },
  Gujr: { oldSpec: true, virama: 0x0ACD, rephPos: REPH_POS.BEFORE_POST,
          rephMode: REPH_IMPLICIT, blwfMode: BLWF_PRE_AND_POST },
  Orya: { oldSpec: true, virama: 0x0B4D, rephPos: REPH_POS.AFTER_MAIN,
          rephMode: REPH_IMPLICIT, blwfMode: BLWF_PRE_AND_POST },
  Taml: { oldSpec: true, virama: 0x0BCD, rephPos: REPH_POS.AFTER_POST,
          rephMode: REPH_IMPLICIT, blwfMode: BLWF_PRE_AND_POST },
  Telu: { oldSpec: true, virama: 0x0C4D, rephPos: REPH_POS.AFTER_POST,
          rephMode: REPH_EXPLICIT, blwfMode: BLWF_POST_ONLY },
  Knda: { oldSpec: true, virama: 0x0CCD, rephPos: REPH_POS.AFTER_POST,
          rephMode: REPH_IMPLICIT, blwfMode: BLWF_POST_ONLY },
  Mlym: { oldSpec: true, virama: 0x0D4D, rephPos: REPH_POS.AFTER_MAIN,
          rephMode: REPH_LOG_REPHA, blwfMode: BLWF_PRE_AND_POST },
};

export function scriptCodeOf( plan ) {
  const t = plan && plan.props && plan.props.script;
  if ( typeof t === 'string' ) return t;
  if ( !t ) return 'Deva';
  return String.fromCharCode( ( t >>> 24 ) & 0xFF, ( t >>> 16 ) & 0xFF,
                              ( t >>> 8 ) & 0xFF, t & 0xFF );
}


/* -------------------------------------------------------------------- */
/*  The cluster grammar -- sd-devanagari.txt l.201-292                    */
/* -------------------------------------------------------------------- */

export const SYLL = {
  CONSONANT: 0, VOWEL: 1, STANDALONE: 2, SYMBOL: 3, BROKEN: 4, NON_INDIC: 5,
};

const i_c = gAlt( gLit( OT.C ), gLit( OT.Ra ) );
const i_n = gSeq( gOpt( gSeq( gOpt( gLit( OT.ZWNJ ) ), gLit( OT.RS ) ) ),
                  gOpt( gSeq( gLit( OT.N ), gOpt( gLit( OT.N ) ) ) ) );
const i_z = gAlt( gLit( OT.ZWJ ), gLit( OT.ZWNJ ) );
const i_reph = gAlt( gSeq( gLit( OT.Ra ), gLit( OT.H ) ), gLit( OT.Repha ) );
const i_sm = gAlt( gLit( OT.SM ), gLit( OT.SMPst ) );
const i_cn = gSeq( i_c, gOpt( gLit( OT.ZWJ ) ), gOpt( i_n ) );
const i_symbol = gSeq( gLit( OT.Symbol ), gOpt( gLit( OT.N ) ) );
const i_matra_group = gSeq( gStar( i_z ),
  gAlt( gLit( OT.M ), gSeq( gOpt( i_sm ), gLit( OT.MPst ) ) ),
  gOpt( gLit( OT.N ) ), gOpt( gLit( OT.H ) ) );
const i_syllable_tail = gSeq(
  gOpt( gSeq( gOpt( i_z ), i_sm, gOpt( i_sm ), gOpt( gLit( OT.ZWNJ ) ) ) ),
  gStar( gLit( OT.A ) ) );
const i_halant_group = gSeq( gOpt( i_z ), gLit( OT.H ),
                             gOpt( gSeq( gLit( OT.ZWJ ), gOpt( gLit( OT.N ) ) ) ) );
const i_final_halant_group = gAlt( i_halant_group,
                                   gSeq( gLit( OT.H ), gLit( OT.ZWNJ ) ) );
const i_medial_group = gOpt( gLit( OT.CM ) );
const i_halant_or_matra_group = gAlt( i_final_halant_group,
                                      gStar( i_matra_group ) );
const i_complex_tail = gSeq( gStar( gSeq( i_halant_group, i_cn ) ),
                             i_medial_group, i_halant_or_matra_group,
                             i_syllable_tail );

const i_consonant_syllable = gSeq(
  gOpt( gAlt( gLit( OT.Repha ), gLit( OT.CS ) ) ), i_cn, i_complex_tail );
const i_vowel_syllable = gSeq( gOpt( i_reph ), gLit( OT.V ), gOpt( i_n ),
                               gAlt( gLit( OT.ZWJ ), i_complex_tail ) );
const i_standalone_cluster = gSeq(
  gAlt( gSeq( gOpt( gAlt( gLit( OT.Repha ), gLit( OT.CS ) ) ),
              gLit( OT.PLACEHOLDER ) ),
        gSeq( gOpt( i_reph ), gLit( OT.DOTTEDCIRCLE ) ) ),
  gOpt( i_n ), i_complex_tail );
const i_symbol_cluster = gSeq( i_symbol, i_syllable_tail );
const i_broken_cluster = gSeq( gOpt( i_reph ), gOpt( i_n ), i_complex_tail );

const scanIndic = makeScanner( [
  { f: i_consonant_syllable, t: SYLL.CONSONANT },
  { f: i_vowel_syllable, t: SYLL.VOWEL },
  { f: i_standalone_cluster, t: SYLL.STANDALONE },
  { f: i_symbol_cluster, t: SYLL.SYMBOL },
  { f: gLit( OT.SMPst ), t: SYLL.NON_INDIC },
  { f: i_broken_cluster, t: SYLL.BROKEN },
  { f: gAny(), t: SYLL.NON_INDIC },
] );

export function findIndicSyllables( buffer ) {
  const info = buffer.info;
  const n = Math.min( info.length, buffer.len === undefined ? info.length
                                                           : Math.max( buffer.len, 0 ) ) || info.length;
  if ( !n ) return;
  const cats = new Int16Array( n );
  for ( let i = 0; i < n; i++ )
    cats[ i ] = info[ i ].indicCategory === undefined ? OT.X : info[ i ].indicCategory;
  let serial = 1;
  let i = 0;
  while ( i < n ) {
    const hit = scanIndic( cats, i, n, SYLL.NON_INDIC );
    const val = ( ( serial << 4 ) | hit.type ) & 0xFF;
    for ( let k = i; k < i + hit.len; k++ ) info[ k ].syllable = val;
    if ( hit.type === SYLL.BROKEN && buffer.scratchFlags !== undefined )
      buffer.scratchFlags |= SCRATCH_HAS_BROKEN_SYLLABLE;
    serial++;
    if ( serial === 16 ) serial = 1;
    i += hit.len;
  }
}

export const indicSyllableType = ( inf ) => inf.syllable & 0x0F;
export const eachIndicSyllable = forEachSyllable;


/* -------------------------------------------------------------------- */
/*  Features -- sd-devanagari.txt l.371-411                               */
/* -------------------------------------------------------------------- */

/* Applied IN ORDER, one at a time, after the initial reordering, scoped to
 * the syllable -- hence a pause after each.  The four with a mask of their
 * own (rphf, pref, blwf, abvf, half, pstf) are the ones the reorderer aims at
 * particular characters; the rest are global and apply everywhere. */
const BASIC_FEATURES = [
  [ 'nukt', F_GLOBAL ], [ 'akhn', F_GLOBAL ], [ 'rphf', F_NONE ],
  [ 'rkrf', F_GLOBAL ], [ 'pref', F_NONE ], [ 'blwf', F_NONE ],
  [ 'abvf', F_NONE ], [ 'half', F_NONE ], [ 'pstf', F_NONE ],
  [ 'vatu', F_GLOBAL ], [ 'cjct', F_GLOBAL ],
];
const OTHER_FEATURES = [
  [ 'init', F_NONE ], [ 'pres', F_GLOBAL ], [ 'abvs', F_GLOBAL ],
  [ 'blws', F_GLOBAL ], [ 'psts', F_GLOBAL ], [ 'haln', F_GLOBAL ],
];

function indicCollectFeatures( b ) {
  b.addGsubPause( setupSyllables );

  b.enableFeature( 'locl', F_PER_SYLLABLE );
  /* The chapters do not require ccmp, but where a font uses it, it is
   * typically at the very beginning. */
  b.enableFeature( 'ccmp', F_PER_SYLLABLE );

  b.addGsubPause( initialReorder );
  for ( const [ t, g ] of BASIC_FEATURES ) {
    b.addFeature( t, g | F_MANUAL_JOINERS | F_PER_SYLLABLE );
    b.addGsubPause( null );
  }
  b.addGsubPause( finalReorder );
  for ( const [ t, g ] of OTHER_FEATURES )
    b.addFeature( t, g | F_MANUAL_JOINERS | F_PER_SYLLABLE );
}

/* 'liga' is not an Indic feature and its ligatures cut across the shaping
 * ones, so the generic collector's global 'liga' is switched off again. */
function indicOverrideFeatures( b ) {
  b.enableFeature( 'liga', F_NONE, 0 );
  b.addGsubPause( null );
}


/* -------------------------------------------------------------------- */
/*  Dynamic consonant properties -- sd-devanagari.txt l.159-179           */
/* -------------------------------------------------------------------- */

/* "Each of the features above is applied together with 'locl' to input
 * sequences consisting of two characters: for 'rphf' and 'half', features are
 * applied to Consonant + Halant combinations; for 'pref', 'blwf' and 'pstf',
 * features are applied to Halant + Consonant.  If these two glyphs form a
 * ligature, with no additional glyphs in context, this means the consonant
 * has the corresponding form."
 *
 * Both orders are probed for every feature, not just the one the chapter
 * names: fonts built for the old specification were widely copied into
 * new-specification fonts without swapping the pairs, and the reference
 * engine follows those fonts rather than the text. */
function indicPlanOf( plan ) {
  if ( plan._indicPlan ) return plan._indicPlan;

  const scriptTag = scriptCodeOf( plan );
  const config = INDIC_CONFIGS[ scriptTag ] || INDIC_CONFIGS.DFLT;

  const table = plan.layout && plan.layout.gsub;
  let ls = null;
  if ( table ) {
    const si = selectScript( table,
      ( plan.scriptTags || [] ).concat( [ 'DFLT', 'dflt' ] ) );
    if ( si >= 0 ) ls = selectLangSys( table, si, plan.languageTags || [] );
  }

  /* A font declares the SECOND specification by carrying a v2 script tag
   * (`dev2`, `bng2`, ...); anything else -- a v1 tag, a fallback tag, or no
   * GSUB script at all -- is the old specification, whose fonts expect the
   * engine to move a post-base halant and to merge the whole post-base run
   * into one cluster. */
  const gs = plan.gsubScript;
  const isOldSpec = !!config.oldSpec &&
    !( typeof gs === 'string' && gs.length === 4 && gs.charCodeAt( 3 ) === 0x32 );

  /* hb_indic_would_substitute_feature_t::init's zero_context.  A zero-context
   * probe insists the lookup match the two glyphs with NOTHING around them; a
   * non-zero-context one lets a contextual lookup's backtrack and lookahead
   * count as satisfied.  The reference uses zero context for the new
   * specification of every script EXCEPT Malayalam, where testing showed both
   * specifications allow context.  Do not widen this without evidence: it is
   * the one place the reference's own comment says "DON'T TOUCH OTHERWISE". */
  const zeroContext = !isOldSpec && scriptTag !== 'Mlym';

  const cache = new Map();
  const ask = ( tag, glyphs ) => {
    if ( !table || !ls ) return false;
    const key = tag + ':' + glyphs.join( ',' );
    let v = cache.get( key );
    if ( v === undefined ) {
      v = featureWouldSubstitute( table, ls, tag, glyphs, zeroContext );
      cache.set( key, v );
    }
    return v;
  };

  const mask = ( tag, global ) => ( global ? 0 : featureMask( plan, tag ) );
  const masks = {};
  for ( const [ t, g ] of BASIC_FEATURES ) masks[ t ] = mask( t, g & F_GLOBAL );
  for ( const [ t, g ] of OTHER_FEATURES ) masks[ t ] = mask( t, g & F_GLOBAL );

  const ip = {
    config, isOldSpec, ask, masks,
    viramaGlyph: -1,
    loadVirama( font ) {
      if ( this.viramaGlyph === -1 )
        this.viramaGlyph = ( config.virama && font && font.getNominalGlyph )
          ? ( font.getNominalGlyph( config.virama ) || 0 ) : 0;
      return this.viramaGlyph;
    },
  };
  plan._indicPlan = ip;
  return ip;
}

/* consonant_position_from_face: which of the three post-base forms this
 * consonant has, decided by asking the font. */
function consonantPositionFromFace( ip, consonant, virama ) {
  const a = [ virama, consonant ], b = [ consonant, virama ];
  if ( ip.ask( 'blwf', a ) || ip.ask( 'blwf', b ) ||
       ip.ask( 'vatu', a ) || ip.ask( 'vatu', b ) ) return OTPOS.BELOW_C;
  if ( ip.ask( 'pstf', a ) || ip.ask( 'pstf', b ) ) return OTPOS.POST_C;
  if ( ip.ask( 'pref', a ) || ip.ask( 'pref', b ) ) return OTPOS.POST_C;
  return OTPOS.BASE_C;
}

function updateConsonantPositions( plan, buffer, font ) {
  const ip = indicPlanOf( plan );
  const virama = ip.loadVirama( font );
  if ( !virama ) return;
  const info = buffer.info;
  for ( let i = 0; i < info.length; i++ )
    if ( info[ i ].indicPosition === OTPOS.BASE_C )
      info[ i ].indicPosition =
        consonantPositionFromFace( ip, info[ i ].codepoint, virama );
}

function setupMasks( plan, buffer /* , font */ ) {
  const info = buffer.info;
  const n = Math.min( info.length, buffer.len === undefined ? info.length
                                                           : Math.max( buffer.len, 0 ) ) || info.length;
  for ( let i = 0; i < n; i++ ) {
    const v = otCategories( info[ i ].codepoint );
    info[ i ].indicCategory = v & 0xFF;
    info[ i ].indicPosition = ( v >> 8 ) & 0xFF;
  }
}

function setupSyllables( plan, buffer /* , font */ ) {
  findIndicSyllables( buffer );
  if ( typeof buffer.unsafeToBreak === 'function' )
    forEachSyllable( buffer, ( s, e ) => buffer.unsafeToBreak( s, e ) );
}


/* -------------------------------------------------------------------- */
/*  Initial reordering -- sd-devanagari.txt l.293-369                     */
/* -------------------------------------------------------------------- */

function moveLeft( info, from, to ) {        /* info[from] -> info[to], to > from */
  const t = info[ from ];
  for ( let i = from; i < to; i++ ) info[ i ] = info[ i + 1 ];
  info[ to ] = t;
}
function moveRight( info, from, to ) {       /* info[from] -> info[to], to < from */
  const t = info[ from ];
  for ( let i = from; i > to; i-- ) info[ i ] = info[ i - 1 ];
  info[ to ] = t;
}

function initialReorderSyllable( plan, buffer, font, start, end ) {
  const ip = indicPlanOf( plan );
  const info = buffer.info;
  const scriptTag = scriptCodeOf( plan );
  const cfg = ip.config;
  const M = ip.masks;

  /* For compatibility with legacy usage in Kannada, Ra + H + ZWJ must behave
   * like Ra + ZWJ + H. */
  if ( scriptTag === 'Knda' && start + 3 <= end &&
       isOneOf( info[ start ], RA_ONLY ) &&
       isOneOf( info[ start + 1 ], HALANTS ) &&
       isOneOf( info[ start + 2 ], ZWJ_ONLY ) ) {
    mergeClusterRange( buffer, start + 1, start + 3 );
    const t = info[ start + 1 ]; info[ start + 1 ] = info[ start + 2 ];
    info[ start + 2 ] = t;
  }

  /* 1. Find the base consonant.  Starting from the end of the syllable, move
   *    backwards until a consonant is found that has neither a below-base nor
   *    a post-base form (post-base forms have to follow below-base forms), or
   *    arrive at the first consonant.  A syllable that starts with Ra + Halant
   *    in a script that has a reph excludes that Ra from the candidates. */
  let base = end;
  let hasReph = false;
  let limit = start;

  if ( M.rphf && start + 3 <= end &&
       ( ( cfg.rephMode === REPH_IMPLICIT && !isJoiner( info[ start + 2 ] ) ) ||
         ( cfg.rephMode === REPH_EXPLICIT &&
           info[ start + 2 ].indicCategory === OT.ZWJ ) ) ) {
    const g = [ info[ start ].codepoint, info[ start + 1 ].codepoint ];
    const g3 = g.concat( [ info[ start + 2 ].codepoint ] );
    if ( ip.ask( 'rphf', g ) ||
         ( cfg.rephMode === REPH_EXPLICIT && ip.ask( 'rphf', g3 ) ) ) {
      limit += 2;
      while ( limit < end && isJoiner( info[ limit ] ) ) limit++;
      base = start;
      hasReph = true;
    }
  } else if ( cfg.rephMode === REPH_LOG_REPHA &&
              info[ start ].indicCategory === OT.Repha ) {
    limit += 1;
    while ( limit < end && isJoiner( info[ limit ] ) ) limit++;
    base = start;
    hasReph = true;
  }

  {
    let i = end;
    let seenBelow = false;
    do {
      i--;
      if ( isConsonant( info[ i ] ) ) {
        if ( info[ i ].indicPosition !== OTPOS.BELOW_C &&
             ( info[ i ].indicPosition !== OTPOS.POST_C || seenBelow ) ) {
          base = i;
          break;
        }
        if ( info[ i ].indicPosition === OTPOS.BELOW_C ) seenBelow = true;
        /* A pre-base-reordering Ra is marked POST_C and is skipped above. */
        base = i;
      } else if ( start < i && info[ i ].indicCategory === OT.ZWJ &&
                  info[ i - 1 ].indicCategory === OT.H ) {
        /* A ZWJ after a Halant stops the base search and requests an explicit
         * half form; a ZWJ BEFORE a Halant asks for a subjoined form, so the
         * search continues -- which is what makes Bengali Ra,H,Ya subjoin. */
        break;
      }
    } while ( i > limit );

    /* Only for an unforced reph: with no other consonant the reph is not
     * formed and the Ra becomes the base after all. */
    if ( hasReph && base === start && limit - base <= 2 ) hasReph = false;
  }

  /* 2. Matras were decomposed by the normalizer and 3. the marks were put in
   *    canonical order there too, so both steps are already done. */

  for ( let i = start; i < base; i++ )
    info[ i ].indicPosition = Math.min( OTPOS.PRE_C, info[ i ].indicPosition );
  if ( base < end ) info[ base ].indicPosition = OTPOS.BASE_C;
  if ( hasReph ) info[ start ].indicPosition = OTPOS.RA_TO_BECOME_REPH;

  /* For old-specification fonts, move the first post-base Halant after the
   * last consonant.  Kannada is blocked: there the reference does it only
   * when there is not already a Halant after the last consonant. */
  if ( ip.isOldSpec ) {
    const disallowDouble = scriptTag === 'Knda';
    for ( let i = base + 1; i < end; i++ )
      if ( info[ i ].indicCategory === OT.H ) {
        let j;
        for ( j = end - 1; j > i; j-- )
          if ( isConsonant( info[ j ] ) ||
               ( disallowDouble && info[ j ].indicCategory === OT.H ) ) break;
        if ( info[ j ].indicCategory !== OT.H && j > i ) moveLeft( info, i, j );
        break;
      }
  }

  /* Attach the marks that have no position of their own to whatever precedes
   * them, so they travel with it. */
  {
    let lastPos = OTPOS.START;
    for ( let i = start; i < end; i++ ) {
      if ( TRAVELS.has( info[ i ].indicCategory ) ) {
        info[ i ].indicPosition = lastPos;
        if ( info[ i ].indicCategory === OT.H &&
             info[ i ].indicPosition === OTPOS.PRE_M ) {
          /* The reference does not move a Halant with a left matra.
           * TEST: U+092B,U+093F,U+094D */
          for ( let j = i; j > start; j-- )
            if ( info[ j - 1 ].indicPosition !== OTPOS.PRE_M ) {
              info[ i ].indicPosition = info[ j - 1 ].indicPosition;
              break;
            }
        }
      } else if ( info[ i ].indicPosition !== OTPOS.SMVD ) {
        if ( info[ i ].indicCategory === OT.MPst &&
             i > start && info[ i - 1 ].indicCategory === OT.SM )
          info[ i - 1 ].indicPosition = info[ i ].indicPosition;
        lastPos = info[ i ].indicPosition;
      }
    }
  }
  /* A post-base consonant owns everything between it and the last consonant
   * or matra before it. */
  {
    let last = base;
    for ( let i = base + 1; i < end; i++ )
      if ( isConsonant( info[ i ] ) ) {
        for ( let j = last + 1; j < i; j++ )
          if ( info[ j ].indicPosition < OTPOS.SMVD )
            info[ j ].indicPosition = info[ i ].indicPosition;
        last = i;
      } else if ( MATRAS.has( info[ i ].indicCategory ) ) last = i;
  }

  {
    /* The syllable field doubles as a permutation record across the sort, so
     * that the clusters of everything that actually moved can be merged
     * afterwards -- and ONLY those.  Merging the whole syllable here would
     * over-merge: the final reordering still moves pre-base matras closer to
     * the base, and its own merges have to interlock with these. */
    const syllable = info[ start ].syllable;
    for ( let i = start; i < end; i++ ) info[ i ].syllable = i - start;

    const part = info.slice( start, end );
    part.sort( ( a, b ) => a.indicPosition - b.indicPosition );
    for ( let i = 0; i < part.length; i++ ) info[ start + i ] = part[ i ];

    /* Find the base again, and flip any left-matra sequence. */
    let firstLeft = end, lastLeft = end;
    base = end;
    for ( let i = start; i < end; i++ ) {
      if ( info[ i ].indicPosition === OTPOS.BASE_C ) { base = i; break; }
      if ( info[ i ].indicPosition === OTPOS.PRE_M ) {
        if ( firstLeft === end ) firstLeft = i;
        lastLeft = i;
      }
    }
    if ( firstLeft < lastLeft ) {
      reverseRange( buffer, firstLeft, lastLeft + 1 );
      let a = firstLeft;
      for ( let j = a; j <= lastLeft; j++ )
        if ( MATRAS.has( info[ j ].indicCategory ) ) {
          reverseRange( buffer, a, j + 1 );
          a = j + 1;
        }
    }

    if ( ip.isOldSpec || end - start > 127 ) {
      mergeClusterRange( buffer, base, end );
    } else {
      for ( let i = base; i < end; i++ )
        if ( info[ i ].syllable !== 255 ) {
          let mn = i, mx = i;
          let j = start + info[ i ].syllable;
          while ( j !== i ) {
            mn = Math.min( mn, j ); mx = Math.max( mx, j );
            const next = start + info[ j ].syllable;
            info[ j ].syllable = 255;
            j = next;
          }
          mergeClusterRange( buffer, Math.max( base, mn ), mx + 1 );
        }
    }
    for ( let i = start; i < end; i++ ) info[ i ].syllable = syllable;
  }

  /* Masks. */
  {
    for ( let i = start; i < end && info[ i ].indicPosition === OTPOS.RA_TO_BECOME_REPH; i++ )
      info[ i ].mask = ( info[ i ].mask | M.rphf ) >>> 0;

    let mask = M.half;
    if ( !ip.isOldSpec && cfg.blwfMode === BLWF_PRE_AND_POST ) mask |= M.blwf;
    for ( let i = start; i < base; i++ )
      info[ i ].mask = ( info[ i ].mask | mask ) >>> 0;

    const post = ( M.blwf | M.abvf | M.pstf ) >>> 0;
    for ( let i = base + 1; i < end; i++ )
      info[ i ].mask = ( info[ i ].mask | post ) >>> 0;
  }

  /* Old-specification Devanagari: the eye-lash Ra.  "The feature 'below-base
   * form' is applied to consonants having below-base forms and following the
   * base consonant.  The exception is vattu, which may appear below half
   * forms as well as below the base glyph."  Ra,Halant,ZWJ is the request for
   * the eyelash form, so that sequence is left alone. */
  if ( ip.isOldSpec && scriptTag === 'Deva' ) {
    for ( let i = start; i + 1 < base; i++ )
      if ( info[ i ].indicCategory === OT.Ra &&
           info[ i + 1 ].indicCategory === OT.H &&
           ( i + 2 === base || info[ i + 2 ].indicCategory !== OT.ZWJ ) ) {
        info[ i ].mask = ( info[ i ].mask | M.blwf ) >>> 0;
        info[ i + 1 ].mask = ( info[ i + 1 ].mask | M.blwf ) >>> 0;
      }
  }

  /* Mark a Halant, Ra sequence after the base for pre-base reordering. */
  if ( M.pref && base + 2 < end ) {
    for ( let i = base + 1; i + 1 < end; i++ )
      if ( ip.ask( 'pref', [ info[ i ].codepoint, info[ i + 1 ].codepoint ] ) ) {
        info[ i ].mask = ( info[ i ].mask | M.pref ) >>> 0;
        info[ i + 1 ].mask = ( info[ i + 1 ].mask | M.pref ) >>> 0;
        break;
      }
  }

  /* ZWJ and ZWNJ effects.  Both disable 'cjct' simply by being present (the
   * feature is collected with F_MANUAL_ZWJ, so the lookups do not skip them);
   * a ZWNJ additionally disables 'half' back to the preceding consonant. */
  for ( let i = start + 1; i < end; i++ )
    if ( isJoiner( info[ i ] ) ) {
      const nonJoiner = info[ i ].indicCategory === OT.ZWNJ;
      let j = i;
      do {
        j--;
        if ( nonJoiner ) info[ j ].mask = ( info[ j ].mask & ~M.half ) >>> 0;
      } while ( j > start && !isConsonant( info[ j ] ) );
    }
}

export function initialReorder( plan, buffer, font ) {
  updateConsonantPositions( plan, buffer, font );
  if ( buffer.scratchFlags === undefined ||
       ( buffer.scratchFlags & SCRATCH_HAS_BROKEN_SYLLABLE ) )
    insertDottedCircles( buffer, font, SYLL.BROKEN, OT.DOTTEDCIRCLE, OT.Repha,
                         OTPOS.END, 'indicCategory', 'indicPosition' );
  forEachSyllable( buffer, ( start, end ) => {
    const t = buffer.info[ start ].syllable & 0x0F;
    /* The vowels were made to look like consonants and the broken clusters
     * have just been given their dotted circle, so all four types that
     * reorder at all take the one routine. */
    if ( t === SYLL.CONSONANT || t === SYLL.VOWEL ||
         t === SYLL.STANDALONE || t === SYLL.BROKEN )
      initialReorderSyllable( plan, buffer, font, start, end );
  } );
}


/* -------------------------------------------------------------------- */
/*  Final reordering -- sd-devanagari.txt l.313-341                       */
/* -------------------------------------------------------------------- */

function finalReorderSyllable( plan, buffer, start, end ) {
  const ip = indicPlanOf( plan );
  const info = buffer.info;
  const scriptTag = scriptCodeOf( plan );
  const cfg = ip.config;
  const M = ip.masks;

  /* Lots of ligation happened between the two reorderings and may have lost a
   * Halant's class.  Recover the one case we can be sure of: a glyph that is
   * still the virama glyph but is marked as both ligated and multiplied. */
  const virama = ip.viramaGlyph > 0 ? ip.viramaGlyph : 0;
  if ( virama ) {
    for ( let i = start; i < end; i++ )
      if ( info[ i ].codepoint === virama && ligated( info[ i ] ) &&
           multiplied( info[ i ] ) ) {
        info[ i ].indicCategory = OT.H;
        info[ i ].props = ( info[ i ].props & ~( GP_LIGATED | GP_MULTIPLIED ) ) >>> 0;
      }
  }

  let tryPref = !!M.pref;

  /* Find the base again. */
  let base;
  findBase:
  for ( base = start; base < end; base++ ) {
    if ( info[ base ].indicPosition < OTPOS.BASE_C ) continue;
    if ( tryPref && base + 1 < end ) {
      for ( let i = base + 1; i < end; i++ )
        if ( ( info[ i ].mask & M.pref ) !== 0 ) {
          if ( !( ( info[ i ].props & GP_SUBSTITUTED ) &&
                  ligatedAndDidntMultiply( info[ i ] ) ) ) {
            /* A 'pref' candidate that formed nothing: the base is around here
             * after all. */
            base = i;
            while ( base < end && isHalant( info[ base ] ) ) base++;
            if ( base < end ) info[ base ].indicPosition = OTPOS.BASE_C;
            tryPref = false;
          }
          break;
        }
      if ( base === end ) break findBase;
    }
    /* For Malayalam, skip over below-base forms that did not form -- but not
     * post-base ones. */
    if ( scriptTag === 'Mlym' ) {
      for ( let i = base + 1; i < end; i++ ) {
        while ( i < end && isJoiner( info[ i ] ) ) i++;
        if ( i === end || !isHalant( info[ i ] ) ) break;
        i++;
        while ( i < end && isJoiner( info[ i ] ) ) i++;
        if ( i < end && isConsonant( info[ i ] ) &&
             info[ i ].indicPosition === OTPOS.BELOW_C ) {
          base = i;
          info[ base ].indicPosition = OTPOS.BASE_C;
        }
      }
    }
    if ( start < base && info[ base ].indicPosition > OTPOS.BASE_C ) base--;
    break;
  }
  if ( base === end && start < base &&
       isOneOf( info[ base - 1 ], ZWJ_ONLY ) ) base--;
  if ( base < end )
    while ( start < base &&
            isOneOf( info[ base ], N_OR_H ) ) base--;

  /* Reorder matras: a pre-base matra that was moved to the head of the
   * syllable before the basic features ran can now move closer to the base --
   * "after the last standalone halant glyph, after the initial matra position
   * and before the main consonant".  Measured against the reference: a ZWJ
   * after that halant STOPS the move, a ZWNJ does not. */
  if ( start + 1 < end && start < base ) {
    let newPos = base === end ? base - 2 : base - 1;
    /* Malayalam and Tamil have no half forms and no explicit virama forms:
     * what 'half' makes there is a chillu or a ligated virama, and the matra
     * belongs after it. */
    if ( scriptTag !== 'Mlym' && scriptTag !== 'Taml' ) {
      for ( ;; ) {
        while ( newPos > start && !isOneOf( info[ newPos ], MATRA_OR_HALANT ) ) newPos--;
        if ( isHalant( info[ newPos ] ) &&
             info[ newPos ].indicPosition !== OTPOS.PRE_M ) {
          if ( newPos + 1 < end &&
               info[ newPos + 1 ].indicCategory === OT.ZWJ && newPos > start ) {
            newPos--;
            continue;                /* keep searching */
          }
        } else {
          newPos = start;            /* no move */
        }
        break;
      }
    }

    if ( start < newPos && info[ newPos ].indicPosition !== OTPOS.PRE_M ) {
      for ( let i = newPos; i > start; i-- )
        if ( info[ i - 1 ].indicPosition === OTPOS.PRE_M ) {
          const oldPos = i - 1;
          if ( oldPos < base && base <= newPos ) base--;
          moveLeft( info, oldPos, newPos );
          /* Deliberately AFTER the move: Indic matra reordering is special. */
          mergeClusterRange( buffer, newPos, Math.min( end, base + 1 ) );
          newPos--;
        }
    } else {
      for ( let i = start; i < base; i++ )
        if ( info[ i ].indicPosition === OTPOS.PRE_M ) {
          mergeClusterRange( buffer, i, Math.min( end, base + 1 ) );
          break;
        }
    }
  }

  /* Reorder the reph.  Its original position is the head of the syllable; it
   * is moved according to what the basic forms actually produced.  Where the
   * reph is a Ra + Halant sequence it moves only if it LIGATED; where it is an
   * encoded Repha it moves only if it did NOT. */
  if ( start + 1 < end &&
       info[ start ].indicPosition === OTPOS.RA_TO_BECOME_REPH &&
       ( ( info[ start ].indicCategory === OT.Repha ) !==
         ligatedAndDidntMultiply( info[ start ] ) ) ) {
    let newRephPos = -1;
    const rephPos = cfg.rephPos;

    const step2 = () => {
      let p = start + 1;
      while ( p < base && !isHalant( info[ p ] ) ) p++;
      if ( p < base && isHalant( info[ p ] ) ) {
        if ( p + 1 < base && isJoiner( info[ p + 1 ] ) ) p++;
        return p;
      }
      return -1;
    };

    if ( rephPos !== REPH_POS.AFTER_POST ) {
      /* 2. after the first explicit halant between the first post-reph
       *    consonant and the last main consonant. */
      newRephPos = step2();
      /* 3. after the main consonant. */
      if ( newRephPos < 0 && rephPos === REPH_POS.AFTER_MAIN ) {
        newRephPos = base;
        while ( newRephPos + 1 < end &&
                info[ newRephPos + 1 ].indicPosition <= OTPOS.AFTER_MAIN )
          newRephPos++;
        if ( newRephPos >= end ) newRephPos = -1;
      }
      /* 4. before the first post-base consonant. */
      if ( newRephPos < 0 && rephPos === REPH_POS.AFTER_SUB ) {
        newRephPos = base;
        while ( newRephPos + 1 < end &&
                !( info[ newRephPos + 1 ].indicPosition === OTPOS.POST_C ||
                   info[ newRephPos + 1 ].indicPosition === OTPOS.AFTER_POST ||
                   info[ newRephPos + 1 ].indicPosition === OTPOS.SMVD ) )
          newRephPos++;
        if ( newRephPos >= end ) newRephPos = -1;
      }
    }
    /* 5. the same search as step 2, for the after-post class. */
    if ( newRephPos < 0 ) newRephPos = step2();
    /* 6. otherwise, the end of the syllable. */
    if ( newRephPos < 0 ) {
      newRephPos = end - 1;
      while ( newRephPos > start && info[ newRephPos ].indicPosition === OTPOS.SMVD )
        newRephPos--;
      /* If the reph would land after a Matra, Halant sequence, put it before
       * that Halant so it can interact with the matra -- but not for a plain
       * Consonant, Halant.  TEST: U+0930,U+094D,U+0915,U+094B,U+094D */
      if ( isHalant( info[ newRephPos ] ) )
        for ( let i = base + 1; i < newRephPos; i++ )
          if ( MATRAS.has( info[ i ].indicCategory ) ) newRephPos--;
    }

    mergeClusterRange( buffer, start, newRephPos + 1 );
    moveLeft( info, start, newRephPos );
    if ( start < base && base <= newRephPos ) base--;
  }

  /* Reorder a pre-base-reordering consonant: only one produced by 'pref', and
   * only if it ligated. */
  if ( tryPref && base + 1 < end ) {
    for ( let i = base + 1; i < end; i++ )
      if ( ( info[ i ].mask & M.pref ) !== 0 ) {
        if ( ligatedAndDidntMultiply( info[ i ] ) ) {
          let newPos = base;
          if ( scriptTag !== 'Mlym' && scriptTag !== 'Taml' ) {
            while ( newPos > start && !isOneOf( info[ newPos - 1 ], MATRA_OR_HALANT ) ) newPos--;
          }
          if ( newPos > start && isHalant( info[ newPos - 1 ] ) ) {
            if ( newPos < end && isJoiner( info[ newPos ] ) ) newPos++;
          }
          const oldPos = i;
          mergeClusterRange( buffer, newPos, oldPos + 1 );
          moveRight( info, oldPos, newPos );
          if ( newPos <= base && base < oldPos ) base++;
        }
        break;
      }
  }

  /* 'init' applies to a left matra that starts a word. */
  if ( info[ start ].indicPosition === OTPOS.PRE_M ) {
    /* "Word start" is the whole general-category RANGE from Cf to Mn, which
     * is every letter as well as the formats and marks -- not just the marks.
     * Reading it as the marks alone gives the initial form to every left
     * matra in a run, not only to the first. */
    const prevCat = start ? ( info[ start - 1 ].unicodeProps & UPROPS_MASK_GEN_CAT ) : -1;
    const midWord = prevCat >= GC.Cf && prevCat <= GC.Mn;
    if ( !start || !midWord )
      info[ start ].mask = ( info[ start ].mask | M.init ) >>> 0;
    else if ( typeof buffer.unsafeToBreak === 'function' )
      buffer.unsafeToBreak( start - 1, start + 1 );
  }
}

export function finalReorder( plan, buffer, font ) {
  if ( !buffer.info.length ) return;
  forEachSyllable( buffer, ( s, e ) => finalReorderSyllable( plan, buffer, s, e ) );
}


/* -------------------------------------------------------------------- */
/*  Normalization                                                         */
/* -------------------------------------------------------------------- */

/* Four letters whose canonical decomposition the shaper must NOT take: they
 * are letters in their own right in these scripts, not a base plus a nukta. */
const NO_DECOMPOSE = new Set( [ 0x0931, 0x09DC, 0x09DD, 0x0B94 ] );
function decompose( shaper, ab ) {
  /* `false` means "this character has no decomposition", which is not the
   * same as "I have nothing to say": the canonical decomposition of these
   * four is a base plus a nukta, and taking it turns a letter into a
   * different letter. */
  return NO_DECOMPOSE.has( ab ) ? false : null;
}


export const indicShaper = {
  name: 'indic',
  collectFeatures: indicCollectFeatures,
  overrideFeatures: indicOverrideFeatures,
  preprocessText: preprocessVowelConstraints,
  setupMasks,
  decompose,
  normalizationPreference: 'no-short-circuit',
  zeroWidthMarks: 'none',
  fallbackPosition: false,
  gposTag: null,
};

const INDIC_SCRIPTS = new Set( [
  'Deva', 'Beng', 'Guru', 'Gujr', 'Orya', 'Taml', 'Telu', 'Knda', 'Mlym',
] );

/* hb_ot_shaper_categorize(): "If the designer designed the font for the
 * 'DFLT' script (or we ended up arbitrarily picking 'latn'), use the default
 * shaper.  Otherwise use the specific shaper.  If it's an indy3 tag, send to
 * USE."  So the Indic shaper is chosen by what the FONT declares, not by the
 * text: a Devanagari font whose GSUB names 'dev3' is shaped by the Universal
 * engine, and one that names only 'DFLT' is not complex-shaped at all. */
export function shaperForScript( scriptTag, ctx ) {
  if ( !INDIC_SCRIPTS.has( scriptTag ) ) return null;
  /* W19_NO_GSUBSCRIPT ignores the tag the font declares. */
  const gs = process.env.W19_NO_GSUBSCRIPT ? null : ( ctx && ctx.gsubScript );
  if ( gs === 'DFLT' || gs === 'latn' ) return null;
  if ( typeof gs === 'string' && gs.length === 4 && gs.charCodeAt( 3 ) === 0x33 )
    return null;                     /* a v3 tag: shaper-use.js takes it */
  return indicShaper;
}

export default indicShaper;
