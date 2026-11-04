# PDF 1.0 through PDF 1.3

Normative text: `/opt/pdfspec/ISO-32000-1-2008.txt` (ISO 32000-1:2008). Clause
numbers below are that document's. Where it describes a feature introduced after
PDF 1.3, that feature is not part of this milestone.

The utilities must read any file whose header declares version 1.0, 1.1, 1.2 or 1.3,
and must not fail on a file that merely declares a later version in its header when
everything it actually uses is within this milestone.

Constructs that ISO 32000-1 introduced after PDF 1.3 are outside this milestone: they
are not required here.

## 1. Lexical conventions and objects — clauses 7.2, 7.3

The full object grammar: booleans, integers and reals, literal and hexadecimal
strings, names, arrays, dictionaries, streams, null, and indirect references and
definitions.

* White space and delimiter characters as clause 7.2.2 and 7.2.3 define them, and
  comments (`%` to end of line) wherever white space may appear.
* Literal strings (7.3.4.2): balanced unescaped parentheses, the `\n \r \t \b \f
  \( \) \\` escapes, `\ddd` octal escapes of one to three digits, a reverse solidus
  at end of line as a line continuation, and an end-of-line inside a string.
* Hexadecimal strings (7.3.4.3), including an odd final digit.
* Names (7.3.5), including `#xx` escapes.
* Streams (7.3.8): the required end-of-line after the `stream` keyword, `/Length`
  including the case where it is an indirect reference, and `endstream`.

## 2. File structure — clause 7.5

* The header (7.5.2), including a file whose header is not at byte offset 0, in
  which case every offset in the file is relative to the header's position.
* The cross-reference table (7.5.4): subsections, the 20-byte entry format, in-use
  and free entries, and the free-entry linked list; the trailer dictionary (7.5.5)
  and `startxref`.
* Object generation numbers other than 0.
* **Recovery.** A conforming reader reconstructs the document when the
  cross-reference information is unusable: `startxref` missing or pointing at
  nonsense, a truncated or corrupt table, offsets that do not land on the object they
  claim, a `/Length` that is wrong in either direction, or a missing `endstream`.
  Reconstruction scans the file for object definitions and uses what is actually
  there. A file that can be recovered must be read, not rejected.

## 3. Filters — clause 7.4

`ASCIIHexDecode` (7.4.2), `ASCII85Decode` (7.4.3), `LZWDecode` (7.4.4) including
`/EarlyChange` 0 and 1, `FlateDecode` (7.4.4) and `RunLengthDecode` (7.4.5).

Only the filters listed above are decoded. A stream whose chain contains any other
filter keeps its encoded bytes; the stream must still be located, its `/Length`
resolved, and its bytes returned by `--raw-stream-data`.

Filters may be chained: `/Filter` is a name or an array of names, with `/DecodeParms`
a dictionary, an array, or absent.

The `LZWDecode` and `FlateDecode` predictors of table 10 — TIFF predictor 2 and the
PNG predictors — with `/Colors`, `/BitsPerComponent` and `/Columns`.

A stream whose data is truncated or corrupt yields the bytes that could be decoded;
it does not abort the document.

## 4. Document structure — clause 7.7

The document catalog (7.7.2); the page tree (7.7.3) including intermediate nodes,
the inheritable attributes of table 30 (`/Resources`, `/MediaBox`, `/CropBox`,
`/Rotate`) and a `/Count` that disagrees with the tree; resource dictionaries
(7.8.3); the document information dictionary (14.3.3); name and number trees where
the features above reach them.

`/MediaBox` and `/CropBox` are rectangles in the sense of 7.9.5 — the two corners may
be given in either order. `/Rotate` is a multiple of 90 and may be negative.

## 5. Content streams and the graphics state — clauses 7.8.2, 8.4

The content stream is a sequence of operands followed by an operator. This milestone
requires the operators the text machinery depends on: the graphics state stack (`q`,
`Q`), the current transformation matrix (`cm`), the text operators of clause 9, and
correct skipping of everything else, including inline images (`BI … ID … EI`, 8.9.7)
whose binary data must not be interpreted as operators.

Form XObjects (8.10) are traversed for their text, with their own `/Resources` and
`/Matrix`.

## 6. Text — clause 9

* Text state parameters (9.3): `Tc`, `Tw`, `Tz`, `TL`, `Tf`, `Ts`, `Tr`.
* Text objects and positioning (9.4.1, 9.4.2): `BT`, `ET`, `Td`, `TD`, `Tm`, `T*`.
* Text-showing operators (9.4.3): `Tj`, `TJ`, `'`, `"`, and the glyph displacement
  rules of 9.4.4 — in particular that word spacing applies to a single-byte code 32
  and never to a two-byte code (9.3.3).
* Simple fonts (9.6): Type 1 including the standard 14 (9.6.2.2) and their metrics,
  TrueType (9.6.3), and Type 3 (9.6.5) with `/FontMatrix` and `/CharProcs`.
* Encodings (9.6.6 and Annex D): `StandardEncoding`, `WinAnsiEncoding`,
  `MacRomanEncoding`, a font's built-in encoding, and `/Differences`.
* Composite fonts (9.7) to the extent PDF 1.3 defines them: Type 0 fonts with a CMap
  and a CIDFont descendant, `/W` and `/DW`.
* Extraction of text content (9.10), including `/ToUnicode` CMaps (9.10.3) with
  `bfchar` and `bfrange`, an array destination, and a destination string that maps one
  code to several characters.

Text drawn under render mode 3 is still text for extraction purposes.

## 7. What the utilities must report

The interfaces, their option spellings and the comparison rules are in
`/app/TEST_INTERFACE.md`.

* `pdftotext` — the text of the requested pages.
* `pdfinfo` — the document information dictionary fields, the page count, the page
  size and rotation of the first page in the range, the file size, whether the file
  is encrypted, linearized ("Optimized"), tagged, and the version.
* `pdffonts` — one row per distinct font reachable from the requested pages, with its
  base name, subtype, encoding, and whether it is embedded, subset, and has a
  `/ToUnicode` mapping.

  A font counts as embedded when its glyph descriptions travel with the file. That
  covers the obvious case -- a descriptor carrying `/FontFile`, `/FontFile2` or
  `/FontFile3` -- and it also covers **Type 3**, whose glyphs ARE the content streams
  in its `/CharProcs` dictionary: there is no external font to resolve, so a Type 3
  font is always embedded. A standard-14 font named without a descriptor is not.
* `qpdf --show-object=N --filtered-stream-data` — the fully decoded bytes of one
  stream object; `--raw-stream-data` — its bytes before any filter is applied.
