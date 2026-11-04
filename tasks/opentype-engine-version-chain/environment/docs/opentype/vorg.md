
# VORG — Vertical origin table (OpenType 1.9.1) - Typography | Microsoft Learn

This optional table specifies the y coordinate of the vertical origin of every glyph in the font.

This table may only be used in CFF or CFF2 OpenType fonts. If present in OpenType fonts containing TrueType outline data, it must be ignored: the Vertical Metrics ('vmtx') and Glyph Data ('glyf') tables provide all the information necessary to accurately calculate the y coordinate of a glyph’s vertical origin. See the “Vertical Origin and Advance Height” section in the ['vmtx'](vmtx) table specification for more details.

The 'vmtx' and Vertical Header ('vhea') tables continue to be required for all OpenType fonts that support vertical writing. Advance heights must continue to be obtained from the 'vmtx' table; that is the only place they are stored.

If a VORG table is present in a CFF or CFF2 OpenType font, the application may choose to obtain the y coordinate of a glyph’s vertical origin either:

1. directly from the VORG table, or
2. by first calculating the top of the glyph’s bounding box from the CFF or CFF2 CharString data and then adding to it the glyph’s top side bearing from the 'vmtx' table.

The former method offers the advantage of increased accuracy and efficiency, since bounding box results calculated from the CFF or CFF2 CharString can differ depending on the rounding decisions and data types of the bounding box algorithm. The latter method provides compatibility for applications that are either unaware of or choose not to support the VORG.

Thus, the VORG table doesn’t add any new font metric values per se; it simply improves accuracy and efficiency for applications, since the intermediate step of calculating bounding boxes from the CFF or CFF2 CharString is rendered unnecessary.

See [OpenType CJK Font Guidelines](recom#cjk) in the Recommendations chapter for more information about constructing CJK (Chinese, Japanese, and Korean) fonts.

## Vertical Origin table format

The VORG table is structured as follows:

*VerticalOrigin table*

| Type | Name | Description |
| --- | --- | --- |
| uint16 | majorVersion | Major version—set to 1. |
| uint16 | minorVersion | Minor version—set to 0. |
| int16 | defaultVertOriginY | The y coordinate of a glyph’s vertical origin, in the font’s design coordinate system, to be used if no entry is present for the glyph in the vertOriginYMetrics array. |
| uint16 | numVertOriginYMetrics | Number of elements in the vertOriginYMetrics array. |
| VertOriginYMetrics | vertOriginYMetrics[numVertOriginYMetrics] | Array of VertOriginYMetrics records, sorted by glyph ID. |

The VertOriginYMetrics record has the following format:

*VertOriginYMetrics record*

| Type | Name | Description |
| --- | --- | --- |
| uint16 | glyphIndex | Glyph index. |
| int16 | vertOriginY | Y coordinate, in the font’s design coordinate system, of the glyph’s vertical origin. |

This array must be sorted by increasing glyphIndex, and must not have more than one element with the same glyphIndex. In a size-optimized implementation, glyphs whose vertical origin’s y coordinate equals defaultVertOriginY will not have an entry in this array.

If all glyphs in a font share the same defaultVertOriginY value, the length of the VORG table will be 8 bytes in a size-optimized implementation, since the vertOriginYMetrics array will be absent.

Typically, only the full-width Latin glyphs in an East Asian font will have entries in the vertOriginYMetrics array. Glyphs rotated for vertical writing, as used in the Vertical Alternates and Rotation ('vrt2') feature, for example, can take advantage of the default value if they are designed appropriately.

In the following example of a complete VORG table for a 1000-unit-em font, every glyph in the font is specified as having a vertOriginY of 880 except for glyphs with glyph indices 10, 12, and 13:

```
majorVersion         =1
minorVersion         =0
defaultVertOriginY   =880
numVertOriginYMetrics=3
--- vertOriginYMetrics[index]=(glyphIndex,vertOriginY)
[0]=(10,889)
[1]=(12,861)
[2]=(13,849)
```
