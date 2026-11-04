
# EBDT - Embedded Bitmap Data Table (OpenType 1.9.1) - Typography | Microsoft Learn

The EBDT table is used to embed monochrome or grayscale bitmap glyph data. It is used together with the [EBLC](eblc) table, which provides embedded bitmap locators, and the [EBSC](ebsc) table, which provides embedded bitmap scaling information.

OpenType embedded bitmaps are also called “sbits” (for “scaler bitmaps”). A set of bitmaps for a face at a given size is called a strike.

The EBLC table identifies the sizes and glyph ranges of the sbits, and has offsets to glyph bitmap data within the EBDT table. The EBDT table then stores the glyph bitmap data, using a number of different possible formats. Glyph metrics information may be stored in either the EBLC or EBDT table, depending upon the formats used in each table. The EBSC table identifies sizes that will be handled by scaling up or scaling down other sbit sizes.

## Table structure

The EBDT table begins with a header containing simply the table version number.

*EBDT Header*

| Type | Name | Description |
| --- | --- | --- |
| uint16 | majorVersion | Major version of the EBDT table, = 2. |
| uint16 | minorVersion | Minor version of the EBDT table, = 0. |

> 
> *Note:* The first version of the EBDT table is 2.0.

The rest of the EBDT table is a collection of bitmap data. The bitmap data can be in a number of possible formats, indicated by information in the EBLC table. Some of the formats contain metric information plus image data, and other formats contain only the image data. Long word alignment is not required for these sub tables; byte alignment is sufficient.

There are also two different formats for glyph metrics: big glyph metrics and small glyph metrics. A BigGlyphMetrics record defines metrics information for both horizontal and vertical layouts. This is important in fonts (such as Kanji) where both types of layout may be used. The SmallGlyphMetrics record defines metrics information for one layout direction only. Which direction applies, horizontal or vertical, is determined by the flags field in the [BitmapSize records](eblc#bitmapsize-record) within the EBLC table.

*BigGlyphMetrics record*

| Type | Name | Description |
| --- | --- | --- |
| uint8 | height | Number of rows in the bitmap. |
| uint8 | width | Number of columns in the bitmap. |
| int8 | horiBearingX | Distance in pixels from the horizontal origin to the left edge of the bitmap. |
| int8 | horiBearingY | Distance in pixels from the horizontal origin to the top edge of the bitmap. |
| uint8 | horiAdvance | Horizontal advance width in pixels. |
| int8 | vertBearingX | Distance in pixels from the vertical origin to the left edge of the bitmap. |
| int8 | vertBearingY | Distance in pixels from the vertical origin to the top edge of the bitmap. |
| uint8 | vertAdvance | Vertical advance width in pixels. |

*SmallGlyphMetrics record*

| Type | Name | Description |
| --- | --- | --- |
| uint8 | height | Number of rows in the bitmap. |
| uint8 | width | Number of columns in the bitmap. |
| int8 | bearingX | Distance in pixels from the horizontal origin to the left edge of the bitmap (for horizontal text); or distance in pixels from the vertical origin to the top edge of the bitmap (for vertical text). |
| int8 | bearingY | Distance in pixels from the horizontal origin to the top edge of the bitmap (for horizontal text); or distance in pixels from the vertical origin to the left edge of the bitmap (for vertical text). |
| uint8 | advance | Horizontal or vertical advance width in pixels. |

Some of the bitmap data formats allow a *composite* bitmap to be defined, using one or more bitmaps as components. The composite formats allow for any number of components, and allow the components to be positioned anywhere in the composite glyph. The components are referenced using an *EbdtComponent* record.

*EbdtComponent record*

| Type | Name | Description |
| --- | --- | --- |
| uint16 | glyphID | Component glyph ID |
| int8 | xOffset | Position of component left |
| int8 | yOffset | Position of component top |

The EbdtComponent record contains the glyph ID of the component, which can be used to look up the location of component glyph data in the EBLC table. The record also contains xOffset and yOffset values that specify where to position the top-left corner of the component within the composite. Nested composites (a composite of composites) are allowed, and the number of nesting levels is determined by implementation stack space.

## Glyph bitmap data formats

The nine different formats currently defined for glyph bitmap data are listed and described below. Different formats are better for different purposes.

In all formats, if the bitDepth is greater than 1, all of a pixel’s bits are stored consecutively in memory, and all of a row’s pixels are stored consecutively.

Each format can contain black/white or grayscale bitmaps depending on the setting of the bitDepth field in the EBLC table. For performance reasons, a byte-aligned format for embedded bitmaps with bitDepth of 8 is recommended.

### Format 1: small metrics, byte-aligned data

*GlyphBitmapFormat1 table*

| Type | Name | Description |
| --- | --- | --- |
| SmallGlyphMetrics | smallMetrics | Metrics information for the glyph |
| uint8 | imageData[*variable*] | Byte-aligned bitmap data |

Glyph bitmap format 1 consists of a small metrics records (either horizontal or vertical depending on the flags field of the BitmapSize record within the EBLC table) followed by byte aligned bitmap data. The bitmap data begins with the most significant bit of the first byte corresponding to the top-left pixel of the bounding box, proceeding through succeeding bits moving left to right. The data for each row is padded to a byte boundary, so the next row begins with the most significant bit of a new byte. 1 bits correspond to black, and 0 bits to white.

### Format 2: small metrics, bit-aligned data

*GlyphBitmapFormat2 table*

| Type | Name | Description |
| --- | --- | --- |
| SmallGlyphMetrics | smallMetrics | Metrics information for the glyph |
| uint8 | imageData[*variable*] | Bit-aligned bitmap data |

Glyph bitmap format 2 is the same as format 1 except that the bitmap data is bit aligned. This means that the data for a new row will begin with the bit immediately following the last bit of the previous row. The start of each glyph must be byte aligned, so the last row of a glyph may require padding. This format takes a little more time to parse, but saves file space compared to format 1.

### Format 3: (obsolete)

This format is not supported in OpenType.

### Format 4: (not supported) metrics in EBLC, compressed data

Glyph bitmap format 4 is a compressed format used by Apple in some of their East Asian fonts. This format is not supported in OpenType.

### Format 5: metrics in EBLC, bit-aligned image data only

*GlyphBitmapFormat5 table*

| Type | Name | Description |
| --- | --- | --- |
| uint8 | imageData[*variable*] | Bit-aligned bitmap data |

Glyph bitmap format 5 is similar to format 2 except that no metrics information is included, just the bit aligned data. This format is for use with EBLC indexSubTable format 2 or format 5, which will contain the metrics information for all glyphs. It works well for Kanji fonts.

In Windows, the rasterizer recalculates sbit metrics for Format 5 bitmap data, allowing correct ABC widths to be reported, even if the bitmaps have white space on either side of the bitmap image. This allows fonts to store monospaced bitmap glyphs in the efficient Format 5 without breaking calls to the GetABCWidths API.

### Format 6: big metrics, byte-aligned data

*GlyphBitmapFormat6 table*

| Type | Name | Description |
| --- | --- | --- |
| BigGlyphMetrics | bigMetrics | Metrics information for the glyph |
| uint8 | imageData[*variable*] | Byte-aligned bitmap data |

Glyph bitmap format 6 is the same as format 1 except that is uses big glyph metrics instead of small.

### Format 7: big metrics, bit-aligned data

*GlyphBitmapFormat7 table*

| Type | Name | Description |
| --- | --- | --- |
| BigGlyphMetrics | bigMetrics | Metrics information for the glyph |
| uint8 | imageData[*variable*] | Bit-aligned bitmap data |

Glyph bitmap format 7 is the same as format 2 except that is uses big glyph metrics instead of small.

### Format 8: small metrics, component data

Format 8 allows definition of composite bitmaps using one or more component bitmaps. See the Table structure section above for a description of the EbdtComponent record.

*GlyphBitmapFormat8 table*

| Type | Name | Description |
| --- | --- | --- |
| SmallGlyphMetrics | smallMetrics | Metrics information for the glyph |
| uint8 | pad | Pad to 16-bit boundary |
| uint16 | numComponents | Number of components |
| EbdtComponent | components[numComponents] | Array of EbdtComponent records |

### Format 9: big metrics, component data

Format 9 allows definition of composite bitmaps using one or more component bitmaps. See the Table structure section above for a description of the EbdtComponent record.

*GlyphBitmapFormat9 table*

| Type | Name | Description |
| --- | --- | --- |
| BigGlyphMetrics | bigMetrics | Metrics information for the glyph |
| uint16 | numComponents | Number of components |
| EbdtComponent | components[numComponents] | Array of EbdtComponent records |
