
# LTSH - Linear Threshold (OpenType 1.9.1) - Typography | Microsoft Learn

The LTSH table can be used in fonts containing TrueType outlines. There can be noticeable improvements to fonts on the screen when instructions are carefully applied to the sidebearings. The gain in readability is offset by the necessity for the text layout engine to grid fit the glyphs to determine the actual advance widths for the glyphs (since instructions may be moving the sidebearing points). The TrueType outline format already has two mechanisms to sidestep the speed issues. One is the ['hdmx'](hdmx) and [VDMX](vdmx) tables, in which precomputed horizontal and vertical advance widths can be saved for selected ppem sizes. The LTSH table (Linear ThreSHold) is a second, complementary method.

The LTSH table defines the size, on a glyph-by-glyph basis, at which it is reasonable to assume linearly scaled advance widths. This table should *not* be included unless bit 4 of the “flags” field in the 'head' table is set. The criteria for linear scaling are:

- (ppem size is ≥ 50) AND (difference between the rounded linear width and the rounded instructed width ≤ 2% of the rounded linear width); or
- Linear width == Instructed width

The LTSH table records the ppem height for each glyph at which the scaling becomes linear again, despite instructions affecting the advance width. At and above the recorded threshold size, the glyph must remain linear in its scaling. For example, it is not valid to set the threshold for a glyph at 55 ppem if the glyph scaling becomes nonlinear again at 90 ppem.

The format for the table is:

| Type | Name | Description |
| --- | --- | --- |
| uint16 | version | Version number—set to 0. |
| uint16 | numGlyphs | Number of glyphs (from numGlyphs in ['maxp'](maxp) table). |
| uint8 | yPixels[numGlyphs] | The vertical pixel height at which the glyph can be assumed to scale linearly. On a per glyph basis. |

Glyphs that do not have instructions on their sidebearings should have yPixels = 1 (the glyph scales linearly at all sizes).
