# PDF/A-1 and PDF/A-2

Normative text: **ISO 19005-1:2005** (PDF/A-1) and **ISO 19005-2:2011** (PDF/A-2).
Clause numbers below are those documents'. Both are *restrictions* on an underlying
PDF specification, not replacements for it: ISO 19005-1 restricts PDF 1.4, and
ISO 19005-2 restricts ISO 32000-1 (PDF 1.7), whose text is at
`/opt/pdfspec/ISO-32000-1-2008.txt`. Everything the underlying standard requires still
holds; this document adds what the archiving profiles forbid, require in addition, or
constrain further.

The previous milestones' clauses, in `/app/SPEC/pdf13.md`, `pdf14.md`, `pdf15.md`,
`pdf16-17.md` and `pdf20.md`, still apply in full. A conforming file is first a
readable file.

**The two parts number their clauses differently, and the difference is a trap.**
ISO 19005-1 puts fonts at clause 6.3 and metadata at 6.7; ISO 19005-2 moves fonts
under graphics at 6.2.11 and metadata to 6.6. The same requirement therefore carries
two clause numbers depending on the part. Where both are given below they are written
`19005-1 6.3.5 / 19005-2 6.2.11.4.2`.

## 1. Conformance levels

A conformance claim names a **part** (1 or 2) and a **level**. Level **B** ("basic")
requires that the document's visual appearance be reliably reproducible over the long
term. Level **U**, defined only for part 2, is level B plus the requirement that all
text have a Unicode mapping. Level **A** ("accessible") is the most demanding.

This document specifies levels **1b**, **2b** and **2u** in full. Levels `1a` and `2a`
impose further requirements beyond level B that this document does not specify, and no
case at those flavours depends on them: `/app/TEST_INTERFACE.md` §4 limits the
corpus at `1a` and `2a` to the character-map clauses of §4.6 and the identification
clauses of §6.4 below, both of which are specified here in full.

A file's *claim* and a file's *conformance* are different things. The XMP
identification of §6.4 states what the file claims; validation decides whether the
claim is true. A file claiming nothing is validated against the flavour requested on
the command line, and a file whose claim disagrees with the requested flavour is
validated against the requested one.

## 2. File structure — 19005-1 6.1 / 19005-2 6.1

### 2.1 Encryption is prohibited — 19005-1 6.1.3 / 19005-2 6.1.3

**No file conforming to either part shall be encrypted.** The trailer dictionary shall
contain no `/Encrypt` key, and no string or stream in the document shall be encrypted
by any means. This is absolute: there is no permitted algorithm, no permitted key
length, and no "empty user password" exemption. A document that decrypts perfectly
well with an empty password is still non-conforming, because the prohibition is on the
presence of the encryption dictionary, not on the difficulty of reading through it.

This is the single cheapest check in the profile and one of the most commonly failed,
because encryption is routinely applied to set permission flags rather than to keep
anyone out. ISO 19005-2 6.1.12 restates the consequence from the other direction: the
permission machinery that would have carried those flags may not be used to restrict
what a conforming reader may do with the document.

### 2.2 Header — 19005-1 6.1.2 / 19005-2 6.1.2

The file shall begin with the PDF header. The second line shall be a comment
containing at least four bytes whose values are greater than 127, marking the file as
binary. A header line that is missing, that is not at the start of the file, or whose
binary comment is absent or contains only bytes below 128, is non-conforming.

ISO 19005-1 requires a `%PDF-1.4` or earlier header. ISO 19005-2 permits headers
through `%PDF-1.7`.

### 2.3 Trailer and cross-reference — 19005-1 6.1.3, 6.1.4 / 19005-2 6.1.3, 6.1.4

The trailer shall contain `/Root` and, for a file with more than one cross-reference
section, `/ID`. The `/ID` entry shall be present in the trailer of every conforming
file under ISO 19005-2. `startxref` shall address real cross-reference information and
the file shall end with `%%EOF`.

The cross-reference information shall be complete and correct: every object in use
shall have an entry, and the offsets shall be right. **A file that a reader can only
open by reconstructing its cross-reference table is non-conforming**, even though the
previous milestones require your reader to perform that reconstruction. Reading
damaged files and certifying damaged files are different jobs, and this is the clause
where they separate.

ISO 19005-1 permits only a classic cross-reference table. ISO 19005-2 permits a
cross-reference stream as well; see §7.

### 2.4 Objects, strings and filters — 19005-1 6.1.6, 6.1.8, 6.1.10 / 19005-2 6.1.6-6.1.10

An indirect object's `obj`/`endobj` keywords shall be present and correctly paired,
and a stream's `/Length` shall be correct — an indirect `/Length` is permitted but its
value shall be right. A stream shall not carry an `/F` entry naming an external file:
the data shall be in the file.

`LZWDecode` shall not be used in either part. `Crypt` filters other than `/Identity`
shall not be used, which follows from §2.1. ISO 19005-2 additionally constrains inline
image dictionaries (6.1.10) to the abbreviations the underlying standard defines.

Hexadecimal strings shall contain an even number of digits. Name objects shall not
exceed 127 bytes.

### 2.5 Implementation limits — 19005-1 6.1.12 / 19005-2 6.1.13

The profiles adopt the underlying standard's architectural limits as normative
requirements rather than guidance: the integer, real and string magnitudes, the
maximum nesting of `q`/`Q` pairs, the maximum number of indirect objects, and the
limit on `/DeviceN` colourant count. A file exceeding one of them is non-conforming
even if every reader handles it.

## 3. Colour — 19005-1 6.2.2, 6.2.3 / 19005-2 6.2.3, 6.2.4

### 3.1 The rule

**All colour shall be specified in a way that is independent of any particular
device**, either directly or by supplying the information a reader needs to interpret
device colour. This is the profile's central graphics requirement and the one that
most often makes conversion real work.

A colour space is device-independent if it is `CalGray`, `CalRGB`, `Lab` or
`ICCBased`. `DeviceGray`, `DeviceRGB` and `DeviceCMYK` are device-dependent and are
permitted **only** when the document supplies an `/OutputIntent` whose destination
profile is for the corresponding device class, so that the device colour has a defined
meaning. A document using `DeviceRGB` with a CMYK-only output intent, or with no
output intent at all, is non-conforming.

### 3.2 Output intents — 19005-1 6.2.2 / 19005-2 6.2.3

The catalogue's `/OutputIntents` array holds output intent dictionaries. For a
conforming file, at most one of them shall have `/S` equal to `/GTS_PDFA1`, and that
one shall carry a `/DestOutputProfile` whose value is a stream containing a valid
embedded ICC profile. The profile is **embedded**, not referenced: a
`/DestOutputProfile` that names an external profile, or an
`/OutputConditionIdentifier` naming a registered condition without an embedded
profile, does not satisfy the clause.

Where more than one output intent with `/S /GTS_PDFA1` is present, all of them shall
have the same `/DestOutputProfile`. ISO 19005-2 extends this to page-level output
intents where the underlying standard permits them: they shall not conflict with the
document-level one.

### 3.3 Alternates and special spaces — 19005-1 6.2.3.4 / 19005-2 6.2.4.4, 6.2.4.5

`Separation` and `DeviceN` colour spaces shall have an alternate space that is itself
permitted by §3.1 and a tint transform that is a valid function. Two `Separation`
spaces in one document that name the same colourant shall have the same alternate
space and tint transform; `/All` and `/None` keep their defined meanings.

ISO 19005-2 6.2.4.5 adds that an `Indexed` space's base shall be permitted, and that a
`Pattern` space's underlying space shall be permitted.

### 3.4 Rendering intents — 19005-1 6.2.9 / 19005-2 6.2.6

Where a rendering intent is named, by the `ri` operator or by an extended graphics
state's `/RI`, it shall be one of `/RelativeColorimetric`, `/AbsoluteColorimetric`,
`/Perceptual` or `/Saturation`. Any other name is non-conforming.

### 3.5 Overprint mode with an ICC-based CMYK space — 19005-2 6.2.4.2 (part 2 only)

Under ISO 19005-2, an `ICCBased` colour space whose profile is a CMYK profile (four
components) shall not be painted with overprinting on unless the overprint mode is 0.
Where such a space is used for stroking while the stroking overprint flag (`/OP` in the
extended graphics state in force) is true, or for filling while the non-stroking overprint
flag (`/op`) is true, the overprint mode (`/OPM` in that graphics state) shall not be 1. A
file that sets `/OPM 1` in either situation is non-conforming. ISO 19005-1 has no such
rule.

## 4. Fonts — 19005-1 6.3 / 19005-2 6.2.11

### 4.1 Embedding is required — 19005-1 6.3.4 / 19005-2 6.2.11.4.1

**Every font used to show text in the document shall be embedded.** There is no
exemption for the standard 14, none for fonts the reader is likely to have, and none
for fonts used only in an annotation's appearance. A font program that is not embedded
makes the file non-conforming however common the typeface.

The embedded program shall be a legal font program of the type its descriptor claims,
and it shall be consistent with the font dictionary that names it. A font that is
present in the resources but never used to show any text is not required to be
embedded, because the requirement attaches to use, not to presence.

### 4.2 Subsets — 19005-1 6.3.5 / 19005-2 6.2.11.4.2

A subset font's `/BaseFont` name shall carry the six-uppercase-letter tag followed by
`+`, and the tag shall be used consistently: two different subsets of the same font in
one document shall not share a tag, and one subset shall not be given two.

### 4.3 Metrics — 19005-1 6.3.6 / 19005-2 6.2.11.5

The widths in the font dictionary shall be consistent with the widths in the embedded
font program. A glyph whose declared `/Widths` entry disagrees with the width the
program actually defines makes the file non-conforming, because text laid out from the
dictionary would not match text laid out from the program.

### 4.4 Composite fonts — 19005-1 6.3.3.2 / 19005-2 6.2.11.3

A `Type0` font's CMap shall be one of the predefined CMaps the underlying standard
names, or an embedded CMap stream that is legal. The `/CIDSystemInfo` of the CMap and
of the descendant `CIDFont` shall agree. A `CIDFontType2` with a non-`Identity` CMap
shall carry a `/CIDToGIDMap` that is `/Identity` or a legal stream.

ISO 19005-1 requires a `/CIDSet` in the descendant font descriptor of an embedded
subset, and requires it to be complete and correct. ISO 19005-2 relaxes this; see §7.

### 4.5 Encodings and `.notdef` — 19005-1 6.3.7 / 19005-2 6.2.11.6, 6.2.11.8

A non-symbolic `TrueType` font shall use `/WinAnsiEncoding` or `/MacRomanEncoding`
with no `/Differences`, or shall be given an `/Encoding` the clause permits. A symbolic
`TrueType` font shall not carry an `/Encoding` entry at all, and its embedded program
shall carry exactly one of the character maps the clause allows.

Text shall not reference the `.notdef` glyph. This is explicit in ISO 19005-2
6.2.11.8 and follows in ISO 19005-1 from the requirement that every shown character
have a defined glyph.

### 4.6 Unicode character maps — 19005-1 6.3.8 / 19005-2 6.2.11.7

For level **U** (flavour `2u`), every font used to show text shall permit every
character it shows to be mapped to Unicode, through a `/ToUnicode` CMap or through an
encoding and font program from which the mapping is unambiguous. A font with no
`/ToUnicode` and a non-standard encoding fails the level even though it passes level B.

This clause is also what levels `1a` and `2a` require of fonts, and it is the part of
those levels the corpus exercises.

## 5. Annotations, actions and forms

### 5.1 Annotations — 19005-1 6.5 / 19005-2 6.3

An annotation's `/Subtype` shall be one of the types the part permits. Under ISO 19005-1
those are `/Text`, `/Link`, `/FreeText`, `/Line`, `/Square`, `/Circle`, `/Highlight`,
`/Underline`, `/Squiggly`, `/StrikeOut`, `/Stamp`, `/Ink`, `/Popup`, `/Widget`,
`/PrinterMark` and `/TrapNet` — PDF 1.4's types without `/FileAttachment`, `/Sound` and
`/Movie`. Under ISO 19005-2 the list adds `/Polygon`, `/PolyLine`, `/Caret`, `/Redact`,
`/Watermark` and `/FileAttachment`: the types ISO 32000-1 defines without `/3D`, `/Sound`,
`/Screen` and `/Movie`. A type ISO 32000-1 does not define, such as `/RichMedia`, is
permitted by neither part. An annotation dictionary's
`/F` flags shall have `Print` set and `Hidden`, `NoView` and `Invisible` clear, so that
what is archived is what is seen. Every annotation other than `/Popup` and `/Link`
shall have a normal appearance stream in its `/AP` dictionary, and where `/AS` is
present the appearance selected by it shall exist. An annotation's `/C` and `/IC`
colour entries are subject to §3.

### 5.2 Actions are restricted — 19005-1 6.6.1 / 19005-2 6.5.1

**Only these action types may appear in a conforming file:** `/GoTo`, `/GoToR`,
`/Thread`, `/URI`, `/Named` and `/SubmitForm`, and under ISO 19005-2 also `/GoToE`. Every
other type is non-conforming wherever it appears — among them `/JavaScript`, `/Launch`,
`/Movie`, `/Sound`, `/ResetForm`, `/ImportData`, `/Hide`, `/SetOCGState`, `/Rendition`,
`/Trans`, `/GoTo3DView`, and the deprecated set-state and no-op actions. A `/Named` action
shall be one of `/NextPage`, `/PrevPage`, `/FirstPage` or `/LastPage`; any other named
action is non-conforming.

The prohibition is on presence, not on reachability. An action dictionary of a
forbidden type in an object that nothing refers to still makes the file
non-conforming, and so does one attached to an annotation that is never shown. The
catalogue shall also carry no `/JavaScript` name tree and no `/AA` additional-actions
dictionary; ISO 19005-1 6.6.2 and ISO 19005-2 6.5.2 forbid the document-level,
page-level and annotation-level trigger dictionaries that would fire such an action.

### 5.3 Interactive forms — 19005-1 6.9 / 19005-2 6.4

A form field's value and its appearance shall agree: the appearance stream shall
represent the field's current value. `/NeedAppearances` shall be absent, or present
with the value `false` — a file that asks the reader to build its appearances is
non-conforming, because the archived appearance would then depend on the reader.
ISO 19005-2 6.4.2 additionally forbids XFA: the interactive form dictionary shall
carry no `/XFA` entry.

## 6. Metadata — 19005-1 6.7 / 19005-2 6.6

### 6.1 The metadata stream — 19005-1 6.7.2 / 19005-2 6.6.2.1

The catalogue shall carry a `/Metadata` entry whose value is a stream containing
well-formed XMP. The stream shall not be filtered in a way that makes it unreadable
to a processor that does not decode filters: it shall carry no `/Filter` at all, so
that the metadata is recoverable from the bytes of the file directly.

Every `/Metadata` stream elsewhere in the document is subject to the same
well-formedness requirement.

### 6.2 Schemas and extension schemas — 19005-1 6.7.8 / 19005-2 6.6.2.3

Every property in the XMP packet shall belong to a schema that is either one of the
predefined schemas the part lists, or is described by an extension schema included in
the packet itself. An extension schema description shall be complete: the namespace
URI, the preferred prefix, and for each property its name, value type and category.
A property from an unknown namespace with no extension schema describing it makes the
file non-conforming, which is the clause that most often catches metadata copied in
from another producer.

### 6.3 Agreement with the document information dictionary — 19005-1 6.7.3 (part 1 only)

**This rule is ISO 19005-1's alone.** ISO 19005-2 has no counterpart: at `2b`, `2u` and
`2a` an information dictionary whose entries disagree with the XMP packet, or have no XMP
counterpart at all, does not make the file non-conforming.

Under ISO 19005-1, each of these document information dictionary entries, **where the
entry is present**, shall be equivalent to its analogous XMP property:

| information dictionary | XMP property |
| --- | --- |
| `/Title` | `dc:title`, its `x-default` alternative |
| `/Author` | `dc:creator`, which shall then hold exactly one entry |
| `/Subject` | `dc:description`, its `x-default` alternative |
| `/Keywords` | `pdf:Keywords` |
| `/Creator` | `xmp:CreatorTool` |
| `/Producer` | `pdf:Producer` |
| `/CreationDate` | `xmp:CreateDate` |
| `/ModDate` | `xmp:ModifyDate` |

An entry present in the information dictionary whose analogue is absent from the XMP
packet does not satisfy the rule; an XMP property with no information-dictionary entry is
not constrained by it. Text values are compared exactly, character for character. The two
dates are compared as the instants they denote, not as strings: `D:20150310171921+01'00'`
and `2015-03-10T16:19:21+00:00` are equivalent, and two values one second apart are not.

### 6.4 Version and conformance level identification — 19005-1 6.7.11 / 19005-2 6.6.4

The XMP packet shall contain the PDF/A identification schema, in the namespace
`http://www.aiim.org/pdfa/ns/id/`, carrying:

* `pdfaid:part` — the integer part number: `1` for ISO 19005-1, `2` for ISO 19005-2;
* `pdfaid:conformance` — the level, as the single uppercase letter `A`, `B` or `U`.
  `U` is defined only for part 2.

A file with no identification claims nothing. A file whose identification is present
but malformed — a part that is not an integer, a conformance that is not one of the
letters, the properties in the wrong namespace — is non-conforming at every level,
because the claim itself is part of what the profile requires.

ISO 19005-2 6.6.6 adds that where the file records provenance, it does so in the
`xmpMM` schema with a valid `xmpMM:VersionID` and `xmpMM:DocumentID`.

## 7. What ISO 19005-2 permits that ISO 19005-1 forbids

This section is the practical difference between the two parts, and between validating
at `1b` and at `2b`. Everything below is **non-conforming under ISO 19005-1 and
conforming under ISO 19005-2**, subject to the constraint stated with it. The same
file will therefore quite properly produce `FAIL` at `1b` and `PASS` at `2b`, and a
validator that shares one rule table between the two parts will get these wrong.

* **Transparency** — 19005-1 6.4 forbids it outright: no transparency group, no
  `/SMask` in an extended graphics state, no blend mode other than `/Normal` or
  `/Compatible`, and no `/CA` or `/ca` other than `1.0`. ISO 19005-2 6.2.10 permits
  all of it, on the condition that a page containing transparency have a defined
  blending colour space: either the page group's `/CS`, or an output intent from
  which one follows.
* **JPEG2000** — 19005-1 permits no `JPXDecode` filter at all, since the filter
  postdates PDF 1.4. ISO 19005-2 6.2.8.3 permits it, requiring the codestream to be
  one the clause allows, its colour space to be permitted by §3, and its bit depth and
  channel count to agree with the image dictionary.
* **Object streams and cross-reference streams** — forbidden under ISO 19005-1, whose
  base is PDF 1.4. ISO 19005-2 permits both, since its base is ISO 32000-1; §2.3's
  requirement that the cross-reference information be complete and correct applies to
  a cross-reference stream exactly as to a table.
* **Optional content** — forbidden under ISO 19005-1. ISO 19005-2 6.9 permits it,
  requiring that `/OCProperties` be present and correct when any optional content
  exists, that every group be listed in it, that the default configuration carry no
  `/AS`, and that no `/SetOCGState` action appear (§5.2 permits none).
* **Embedded files** — ISO 19005-1 6.1.11 forbids `/EmbeddedFiles` in the name tree
  entirely. ISO 19005-2 permits embedded files **on the condition that each embedded
  file is itself a file conforming to ISO 19005-1 or ISO 19005-2**. An embedded file
  of any other kind makes the containing file non-conforming, so the check is
  recursive.
* **`Indexed` and `Pattern` colour spaces** — constrained but permitted under
  ISO 19005-2 6.2.4.5, as §3.3 sets out.
* **`/CIDSet`** — required and required to be complete under ISO 19005-1 6.3.3.2. ISO
  19005-2 does not require it; where it is present it shall be correct.
* **Digital signatures** — ISO 19005-2 permits a signature, requiring its
  `/SubFilter` to be one the clause names and its appearance to follow §5.1.
* **Compression of the document** — ISO 19005-2's larger base permits compression
  constructs PDF 1.4 has no syntax for. None of them changes any requirement above.

Two things are **not** relaxed and are worth stating because implementations
routinely assume otherwise: **encryption is prohibited in both parts** (§2.1), and
**font embedding is required in both parts** (§4.1). There is no level and no part in
this milestone at which either becomes optional.

## 8. What the utilities must report

The interfaces, their option spellings and the comparison rules are in
`/app/TEST_INTERFACE.md`.

* `verapdf -f FLAVOUR --format text FILE...` — one line per operand: the verdict, the
  operand exactly as given, and the flavour, for each file in the batch. The verdict
  is conformance at the requested flavour and nothing else; the clause that failed is
  not reported by this output format and is not compared. A file that cannot be parsed
  is named as unparsable rather than declared non-conforming, and the two are
  different answers.

* The earlier milestones' utilities — unchanged. A file within this milestone's scope
  is still an ordinary PDF file to them, and everything they reported before they
  still report.
