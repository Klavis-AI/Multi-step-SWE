
# hdmx - Horizontal Device Metrics (OpenType 1.9.1) - Typography | Microsoft Learn

The 'hdmx' table can be used in fonts with TrueType outlines to store integer advance widths scaled to particular pixel sizes. This allows the text layout engine to build integer width tables without calling the scaler for each glyph. Typically, this table will contain widths for only certain pixel sizes.

For non-square pixel grids, the em square width (in pixels) is used to determine which device record to use. For example, a 12 point em square on a device with a resolution of 72x96 would be 12 pixels high and 16 pixels wide. The 'hdmx' device record for 16 pixels per em would be used.

If bit 4 of the flags field in the ['head' table](head) is not set, then it is assumed that the font scales linearly; in this case an 'hdmx' table is not necessary and should not be built. If bit 4 of the flag field is set, then one or more glyphs in the font are assumed to scale nonlinearly. In this case, performance can be improved by including the 'hdmx' table with width data for important sizes. Please see the chapter [“Recommendations for OpenType Fonts”](recom#hdmx) for more detail.

The table begins as follows:

*HdmxHeader*

| Type | Name | Description |
| --- | --- | --- |
| uint16 | version | Table version number—set to 0. |
| uint16 | numRecords | Number of device records. |
| uint32 | sizeDeviceRecord | Size of a device record, 32-bit aligned. |
| DeviceRecord | records[numRecords] | Array of device records. |

The records array must be sorted in increasing pixel size order. The DeviceRecord format is as follows:

*DeviceRecord*

| Type | Name | Description |
| --- | --- | --- |
| uint8 | pixelSize | Pixel size for following widths (as ppem). |
| uint8 | maxWidth | Maximum width. |
| uint8 | widths[numGlyphs] | Array of widths (numGlyphs is from the 'maxp' table). |

Each DeviceRecord is padded with 0s to make it 32-bit aligned.

Each value in the widths array is the width of the particular glyph, in pixels, at the pixels per em (ppem) size given in the pixelSize field.

The ppem sizes are measured along the y axis.
