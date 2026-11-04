
# vhea — Vertical header table (OpenType 1.9.1) - Typography | Microsoft Learn

## Overview

The vertical header table contains information needed for vertical layout of Chinese, Japanese, Korean (CJK) and other ideographic scripts. In vertical layout, these scripts are written either top to bottom or bottom to top. This table contains information that is general to the font as a whole. Information that pertains to specific glyphs is given in the [vertical metrics ('vmtx') table](vmtx). The formats of these tables are similar to those for horizontal metrics, ['hhea'](hhea) and ['hmtx'](hmtx).

Data in the vertical header table must be consistent with data that appears in the vertical metrics table. The advance height and top sidebearing values in the vertical metrics table must correspond with the maximum advance height and minimum bottom sidebearing values in the vertical header table.

See the section [OpenType CJK Font Guidelines](recom#cjk) for more information about constructing CJK (Chinese, Japanese, and Korean) fonts.

## Table formats

There are two versions of the 'vhea' table, 1.0 and 1.1. The difference between version 1.0 and version 1.1 is the name and definition of the following fields:

- ascender becomes vertTypoAscender
- descender becomes vertTypoDescender
- lineGap becomes vertTypoLineGap

Version 1.0 of the vertical header table format is as follows:

*VheaHeader, version 1.0*

| Type | Name | Description |
| --- | --- | --- |
| Version16Dot16 | version | Version number of the vertical header table; 0x00010000 for version 1.0 |
| FWORD | ascent | Distance in font design units from the centerline to the previous line’s descent. |
| FWORD | descent | Distance in font design units from the centerline to the next line’s ascent. |
| FWORD | lineGap | Reserved; set to 0 |
| UFWORD | advanceHeightMax | The maximum advance height measurement in font design units found in the font. This value must be consistent with the entries in the vertical metrics table. |
| FWORD | minTopSideBearing | The minimum top sidebearing measurement found in the font, in font design units. This value must be consistent with the entries in the vertical metrics table. |
| FWORD | minBottomSideBearing | The minimum bottom sidebearing measurement found in the font, in font design units. This value must be consistent with the entries in the vertical metrics table. |
| FWORD | yMaxExtent | Defined as yMaxExtent = max(tsb + (yMax - yMin)). |
| int16 | caretSlopeRise | The value of the caretSlopeRise field divided by the value of the caretSlopeRun Field determines the slope of the caret. A value of 0 for the rise and a value of 1 for the run specifies a horizontal caret. A value of 1 for the rise and a value of 0 for the run specifies a vertical caret. Intermediate values are desirable for fonts whose glyphs are oblique or italic. For a vertical font, a horizontal caret is best. |
| int16 | caretSlopeRun | See the caretSlopeRise field. Value=1 for nonslanted vertical fonts. |
| int16 | caretOffset | The amount by which the highlight on a slanted glyph needs to be shifted away from the glyph in order to produce the best appearance. Set value equal to 0 for nonslanted fonts. |
| int16 | reserved | Set to 0. |
| int16 | reserved | Set to 0. |
| int16 | reserved | Set to 0. |
| int16 | reserved | Set to 0. |
| int16 | metricDataFormat | Set to 0. |
| uint16 | numOfLongVerMetrics | Number of advance heights in the vertical metrics table. |

Version 1.1 of the vertical header table format is as follows:

*VheaHeader, version 1.1*

| Type | Name | Description |
| --- | --- | --- |
| Version16Dot16 | version | Version number of the vertical header table; 0x00011000 for version 1.1 |
| FWORD | vertTypoAscender | The vertical typographic ascender for this font. It is the distance in font design units from the vertical center baseline to the right edge of the design space for CJK / ideographic glyphs (or “ideographic em box”).It is usually set to (head.unitsPerEm)/2. For example, a font with an em of 1000 design units will set this field to 500. See the Baseline tags section of the OpenType Layout Tag Registry for a description of the [ideographic em-box](baselinetags#ideoembox). |
| FWORD | vertTypoDescender | The vertical typographic descender for this font. It is the distance in font design units from the vertical center baseline to the left edge of the design space for CJK / ideographic glyphs.It is usually set to -(head.unitsPerEm/2). For example, a font with an em of 1000 design units will set this field to -500. |
| FWORD | vertTypoLineGap | The vertical typographic gap for this font. An application can determine the recommended line spacing for single spaced vertical text for an OpenType font by the following expression: ideo embox width + vhea.vertTypoLineGap. |
| UFWORD | advanceHeightMax | The maximum advance height measurement in font design units found in the font. This value must be consistent with the entries in the vertical metrics table. |
| FWORD | minTopSideBearing | The minimum top sidebearing measurement found in the font, in font design units. This value must be consistent with the entries in the vertical metrics table. |
| FWORD | minBottomSideBearing | The minimum bottom sidebearing measurement found in the font, in font design units. This value must be consistent with the entries in the vertical metrics table. |
| FWORD | yMaxExtent | Defined as yMaxExtent = max(tsb + (yMax - yMin)). |
| int16 | caretSlopeRise | The value of the caretSlopeRise field divided by the value of the caretSlopeRun Field determines the slope of the caret. A value of 0 for the rise and a value of 1 for the run specifies a horizontal caret. A value of 1 for the rise and a value of 0 for the run specifies a vertical caret. Intermediate values are desirable for fonts whose glyphs are oblique or italic. For a vertical font, a horizontal caret is best. |
| int16 | caretSlopeRun | See the caretSlopeRise field. Value=1 for nonslanted vertical fonts. |
| int16 | caretOffset | The amount by which the highlight on a slanted glyph needs to be shifted away from the glyph in order to produce the best appearance. Set value equal to 0 for nonslanted fonts. |
| int16 | reserved | Set to 0. |
| int16 | reserved | Set to 0. |
| int16 | reserved | Set to 0. |
| int16 | reserved | Set to 0. |
| int16 | metricDataFormat | Set to 0. |
| uint16 | numOfLongVerMetrics | Number of advance heights in the vertical metrics table. |

## 'vhea' table and OpenType Font Variations

In a variable font, various font-metric values within the 'vhea' table may need to be adjusted for different variation instances. Variation data for 'vhea' entries can be provided in the [metrics variations (MVAR)](mvar) table. Different 'vhea' entries are associated with particular variation data in the MVAR table using value tags, as follows:

| 'vhea' entry | Tag |
| --- | --- |
| ascent, vertTypoAscender | 'vasc' |
| caretOffset | 'vcof' |
| caretSlopeRun | 'vcrn' |
| caretSlopeRise | 'vcrs' |
| descent, vertTypoDescender | 'vdsc' |
| lineGap, vertTypoLineGap | 'vlgp' |

For general information on OpenType Font Variations, see the chapter, [OpenType Font Variations Overview](otvaroverview).

## Vertical header table example

| Offset/length | Value | Name | Comment |
| --- | --- | --- | --- |
| 0/4 | 0x00011000 | version | Version number of the vertical header table is 1.1. |
| 4/2 | 1024 | vertTypoAscender | Half the em-square height. |
| 6/2 | -1024 | vertTypoDescender | Minus half the em-square height. |
| 8/2 | 0 | vertTypoLineGap | Typographic line gap is 0 font design units. |
| 10/2 | 2079 | advanceHeightMax | The maximum advance height measurement found in the font is 2079 font design units. |
| 12/2 | -342 | minTopSideBearing | The minimum top sidebearing measurement found in the font is -342 font design units. |
| 14/2 | -333 | minBottomSideBearing | The minimum bottom sidebearing measurement found in the font is -333 font design units. |
| 16/2 | 2036 | yMaxExtent | max(tsb + (yMax - yMin)) = 2036. |
| 18/2 | 0 | caretSlopeRise | The caret slope rise of 0 and a caret slope run of 1 indicate a horizontal caret for a vertical font. |
| 20/2 | 1 | caretSlopeRun | The caret slope rise of 0 and a caret slope run of 1 indicate a horizontal caret for a vertical font. |
| 22/2 | 0 | caretOffset | Value set to 0 for nonslanted fonts. |
| 24/4 | 0 | reserved | Set to 0. |
| 26/2 | 0 | reserved | Set to 0. |
| 28/2 | 0 | reserved | Set to 0. |
| 30/2 | 0 | reserved | Set to 0. |
| 32/2 | 0 | metricDataFormat | Set to 0. |
| 34/2 | 258 | numOfLongVerMetrics | Number of advance heights in the vertical metrics table is 258. |
