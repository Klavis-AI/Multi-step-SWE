
# kern - Kerning (OpenType 1.9.1) - Typography | Microsoft Learn

The kerning table contains values that control inter-character spacing for the glyphs in a font. Fonts containing CFF outlines are not supported by the 'kern' table and require use of the GPOS table to provide kerning.

> 
> *Note:* Apple specifications define a version of the 'kern' table with additional functionality. Those extensions are not supported in OpenType.

A 'kern' table has one or more subtables. Each subtable varies in format and can contain information for vertical or horizontal text. A subtable can contain kerning values or minimum values. Kerning values are used to adjust inter-character spacing, and minimum values are used to limit the amount of adjustment that the scaler applies by the combination of kerning and tracking. Because the adjustments are additive, the order of the subtables containing kerning values is not important. However, tables containing minimum values should usually be placed last, so that they can be used to limit the total effect of other subtables.

The kerning table has a header that contains the format number and the number of subtables present.

*KernHeader*

| Type | Field | Description |
| --- | --- | --- |
| uint16 | version | Table version number—set to 0. |
| uint16 | nTables | Number of subtables in the kerning table. |

The kerning subtables follow immediately after the KernHeader. There are different subtable formats that share the same header. The subtable header is used to identify the format of the subtable and the kind of information it contains:

| Type | Field | Description |
| --- | --- | --- |
| uint16 | version | Kern subtable version number—set to 0. |
| uint16 | length | Length of the subtable, in bytes (including this header). |
| uint16 | coverage | What type of information is contained in this table. |

The coverage field is divided into the following sub-fields, with sizes given in bits:

| Sub-field | Bit(s) | Description |
| --- | --- | --- |
| horizontal | 0 | 1 if table has horizontal data, 0 if vertical. |
| minimum | 1 | If this bit is set to 1, the table has minimum values. If set to 0, the table has kerning values. |
| cross-stream | 2 | If set to 1, kerning is perpendicular to the flow of the text.  If the text is normally written horizontally, kerning will be done in the up and down directions. If kerning values are positive, the text will be kerned upwards; if they are negative, the text will be kerned downwards.  If the text is normally written vertically, kerning will be done in the left and right directions. If kerning values are positive, the text will be kerned to the right; if they are negative, the text will be kerned to the left.  The value 0x8000 in the kerning data resets the cross-stream kerning back to 0. |
| override | 3 | If this bit is set to 1 the value in this table should replace the value currently being accumulated. |
| reserved1 | 4-7 | Reserved—set to zero. |
| format | 8-15 | Format of the subtable. Only formats 0 and 2 have been defined. Formats 1 and 3 through 255 are reserved for future use. |

Remaining subtable data follows the subtable header according to the format specified in the coverage field. The length field in the subtable header indicates the total length of the subtable, including the header and the format-specific data that follows it.

## Format 0

Format 0 is the only subtable format supported by Windows.

This subtable is a sorted list of kerning pairs and values. The list is preceded by information which makes it possible to make an efficient binary search of the list:

*KernSubtableFormat0*

| Type | Field | Description |
| --- | --- | --- |
| uint16 | nPairs | This gives the number of kerning pairs in the table. |
| uint16 | searchRange | The largest power of two less than or equal to the value of nPairs, multiplied by the size in bytes of an entry in the table. |
| uint16 | entrySelector | This is calculated as log2 of the largest power of two less than or equal to the value of nPairs. This value indicates how many iterations of the search loop will have to be made. (For example, in a list of eight items, there would have to be three iterations of the loop). |
| uint16 | rangeShift | The value of nPairs minus the largest power of two less than or equal to nPairs, and then multiplied by the size in bytes of an entry in the table. |
| KernPair | kernPairs[nPairs] | Array of KernPair records. |

The kernPairs array provides a list of kerning pairs and values. Each KernPair record has the following format:

*KernPair record*

| Type | Field | Description |
| --- | --- | --- |
| uint16 | left | The glyph index for the left-hand glyph in the kerning pair. |
| uint16 | right | The glyph index for the right-hand glyph in the kerning pair. |
| FWORD | value | The kerning value for the above pair, in font design units. If this value is greater than zero, the characters will be moved apart. If this value is less than zero, the character will be moved closer together. |

The KernPair records must be ordered by combining the left and right values to form an unsigned 32-bit integer (left as the high-order word), then ordering records numerically using these combined values.

A binary search is most efficiently coded if the search range is a power of two. The search range can be reduced by half by shifting instead of dividing. In general, the number of kerning pairs, nPairs, will not be a power of two. The value of the search range, searchRange, should be the largest power of two less than or equal to nPairs. The number of pairs not covered by searchRange (that is, nPairs - searchRange) is the value rangeShift.

## Format 2

This subtable is a two-dimensional array of kerning values. The glyphs are mapped to classes, using a different mapping for left- and right-hand glyphs. This allows glyphs that have similar right- or left-side shapes to be handled together. Each similar right- or left-hand shape is said to be single class.

Each row in the kerning array represents one left-hand glyph class, each column represents one right-hand glyph class, and each cell contains a kerning value. Row and column 0 always represent glyphs that do not kern and contain all zeros.

The values in the right class table are stored pre-multiplied by the number of bytes in a single kerning value, and the values in the left class table are stored pre-multiplied by the number of bytes in one row. This eliminates a need to multiply the row and column values together to determine the location of the kerning value. The array can be indexed by doing the right- and left-hand class mappings, adding the class values to the address of the array, and fetching the kerning value to which the new address points.

The header for the simple array has the following format:

*KernSubtableFormat2*

| Type | Field | Description |
| --- | --- | --- |
| uint16 | rowWidth | The width, in bytes, of a row in the table. |
| Offset16 | leftClassOffset | Offset from beginning of this subtable to left-hand class table. |
| Offset16 | rightClassOffset | Offset from beginning of this subtable to right-hand class table. |
| Offset16 | kerningArrayOffset | Offset from beginning of this subtable to the start of the kerning array. |

Each class table has the following header:

| Type | Field | Description |
| --- | --- | --- |
| uint16 | firstGlyph | First glyph in class range. |
| uint16 | nGlyphs | Number of glyph in class range. |

This header is followed by nGlyphs number of class values, which are in uint16 format. Entries for glyphs that don’t participate in kerning should point to the row or column at position zero.

The array itself is a left by right array of kerning values, which are FWORDs, where left is the number of left-hand classes and R is the number of right-hand classes. The array is stored by row.

> 
> *Note:* This format is the quickest to process since each lookup requires only a few index operations. The table can be quite large since it will contain the number of cells equal to the product of the number of right-hand classes and the number of left-hand classes, even though many of these classes do not kern with each other.
