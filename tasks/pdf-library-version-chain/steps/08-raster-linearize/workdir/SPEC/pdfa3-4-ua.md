# PDF/A-3, PDF/A-4 and PDF/UA-1

Normative texts: **ISO 19005-3:2012** (PDF/A-3), **ISO 19005-4:2020** (PDF/A-4) and
**ISO 14289-1:2014** (PDF/UA-1). Clause numbers below are those documents' own.
`/opt/pdfspec/ISO-32000-1-2008.txt` holds the published ISO 32000-1:2008 text, which all
three restrict rather than replace and which ISO 14289-1 names for the tagging model.

The previous milestones' clauses still apply in full, and this document adds to them and
replaces none of them. `/app/SPEC/pdfa1-2.md` matters most: ISO 19005-3 is ISO 19005-2
with one permission added, so nearly every requirement a PDF/A-3 file must meet is stated
there and is not restated here. `/app/SPEC/pdf20.md` matters next: ISO 19005-4 is built on
PDF 2.0, so the constructs that milestone added are the ones part 4 constrains.

**Three standards, three numberings.** ISO 19005-3 numbers as ISO 19005-2 does — clause 6,
with 6.8 for embedded files. ISO 19005-4 renumbers within clause 6 and refers to
ISO 32000-2 throughout. ISO 14289-1 puts its identification requirement in clause 5 and
its file requirements in clause 7. A conformance report names the standard alongside the
clause for exactly this reason, and a clause number alone is ambiguous across the three.

**Conformance levels.** ISO 19005-3 keeps part 2's three: `3b` (level B, appearance),
`3a` (level A, which adds the tagging and language requirements) and `3u` (level U, which
adds the requirement that all text map to Unicode). ISO 19005-4 has no level letters at
all: there is plain `4`, and the two extended profiles `4e` and `4f`. ISO 14289-1 has one
level; `ua1` names it.

---

## 1. PDF/A-3 — ISO 19005-3:2012

### 1.1 What part 3 is

A PDF/A-3 file is a PDF/A-2 file that is additionally permitted to carry embedded files of
**any** format. ISO 19005-2 admits an embedded file only where that file is itself a
conforming PDF/A file; ISO 19005-3 lifts that restriction, so that a document may carry
the source data it was generated from — a spreadsheet, an XML invoice, a CAD model —
alongside the archival rendition of it, and remain conforming.

Everything else is part 2's, unchanged: the file structure rules, the prohibition on
encryption, the font embedding requirements, the colour and output-intent requirements,
the prohibited actions and triggers, the transparency and extended-graphics-state
constraints, and the metadata requirements. They are in `/app/SPEC/pdfa1-2.md` and are not
restated here. **A file that fails any part 2 requirement fails at `3b` for the same
reason**, reported against the ISO 19005-3 clause that restates it.

### 1.2 Embedded files — 6.8

The permission and its conditions. A conforming file may contain embedded file streams
subject to the requirements of this clause, and those requirements are what a checker
actually tests:

* **Every** embedded file's file specification dictionary shall contain an
  `/AFRelationship` entry, a name identifying the relationship between the embedded file
  and the content of the document. The defined values are `/Source`, `/Data`,
  `/Alternative`, `/Supplement`, `/EncryptedPayload`, `/FormData`, `/Schema` and
  `/Unspecified`; a name outside that set shall be treated as `/Unspecified` rather than
  as an error, but the entry shall be present. This is the single requirement that most
  distinguishes a part 3 file from a part 2 file carrying an attachment, and it is the one
  a converter most often omits.
* The embedded file stream dictionary shall carry a `/Subtype` whose value is a valid MIME
  type for the embedded data. Where the type is not known, `application/octet-stream`
  shall be used — the entry is not optional, and an absent `/Subtype` is a defect even
  where the data's type is genuinely unknown.
* The file specification dictionary shall contain both `/F` and `/UF`, and neither shall
  be empty. `/F` is the file name in the PDFDocEncoded form and `/UF` the Unicode form;
  a conforming file carries both so that a processor unable to read one can use the other.
* The additional information provided for an associated file, and the usage requirements
  for it, shall together indicate the relationship between the embedded file and the
  document or the part of the document it is associated with.

### 1.3 Associated files — the `/AF` array

An *associated file* is an embedded file reached through an `/AF` entry, whose value is an
array of file specification dictionaries. Part 3 admits `/AF` in three places, and the
place carries meaning:

* on the **document catalogue**, where the attachment relates to the document as a whole;
* on a **page** dictionary, where it relates to that page;
* on a **structure element** dictionary, where it relates to the content that element
  tags.

`/AF` is a PDF 2.0 construct that ISO 19005-3 adopts ahead of PDF 2.0's publication; the
previous milestone's `/app/SPEC/pdf20.md` defines its syntax, and nothing about that
syntax changes here. What changes is that it becomes normative: an embedded file reachable
only through the catalogue's `/Names` `/EmbeddedFiles` name tree, and not through any
`/AF`, is still an embedded file and still subject to 6.8, and a file specification
appearing in an `/AF` array without an `/AFRelationship` is a defect wherever it appears.

### 1.4 Identification

The XMP identification carries the part number and the level, exactly as part 2's does:
`pdfaid:part` shall be `3` and `pdfaid:conformance` shall be `A`, `B` or `U` according to
the level claimed. The properties shall carry the `pdfaid` namespace prefix. A file whose
content satisfies every part 3 requirement but whose `pdfaid:part` says `2` is **not** a
conforming PDF/A-3 file, and a checker asked for `3b` shall say so: the identification is
a requirement, not a label, and this is the most common reason a file that "looks right"
fails at a flavour it was not written for.

### 1.5 Levels A and U

`3u` adds part 2's level-U requirement: every character code used to show text shall map
to a Unicode value, through `/ToUnicode` or through a font construction that determines it
without one. `3a` adds level U's requirement and the tagging requirements on top — a
structure hierarchy, a natural language specification, and the alternative-text
requirements that go with them. Those requirements overlap the PDF/UA-1 requirements in
§3 but are **not** identical to them, and a file conforming at `3a` is not thereby
conforming at `ua1`, nor the reverse.

---

## 2. PDF/A-4 — ISO 19005-4:2020

### 2.1 The base standard is PDF 2.0

Parts 1 through 3 restrict ISO 32000-1. **Part 4 restricts ISO 32000-2**, and every
internal reference in it is to that document. Three consequences run through everything
below:

* the constructs PDF 2.0 added are available to a conforming file and are constrained,
  not prohibited, where earlier parts had no need to mention them at all;
* the constructs PDF 2.0 deprecated are the ones part 4 refuses;
* the previous milestone's `/app/SPEC/pdf20.md` is the definition of those constructs, and
  this document constrains rather than redefines them.

Part 4 also drops the level letters. There is no `4a`, `4b` or `4u`: tagging is no longer
a conformance level of its own, and the Unicode requirement is folded into the base. What
exist are plain **`4`**, **`4e`** (which additionally permits engineering content) and
**`4f`** (which additionally permits embedded files of any format, the permission part 3
gave at its own level).

### 2.2 File structure — 6.1

* **6.1.2, the file header.** It shall begin at byte zero and consist of `%PDF-2.n`
  followed by a single EOL marker, where `n` is a single decimal digit. A file whose
  header says `%PDF-1.7` is not a PDF/A-4 file however well-formed its contents; this is
  the most mechanical difference between part 4 and every earlier part, and it is checked
  first.
* **6.1.3, the file trailer**, and **6.1.4, the cross-reference table** — the trailer shall
  not contain `/Encrypt`; the `xref` keyword and the cross-reference subsection header
  shall be separated by a single EOL marker; the file shall be complete, with no
  unresolved reference and no object outside the cross-reference information. A document
  information dictionary, where one is present, shall contain nothing but a `/ModDate`
  entry, and the trailer shall carry `/Info` only when the catalogue has a `/PieceInfo`
  entry.
* **6.1.5, string objects** — a string shall not exceed the implementation limit the base
  standard states, and the text string types PDF 2.0 defines shall be used as it defines
  them. The previous milestone's UTF-8 text strings are the case that matters here.
* **6.1.6, encryption** — a conforming file shall not be encrypted. Both subclauses are
  checked: there shall be no `/Encrypt` entry, and no crypt filter shall appear.
* **6.1.7 to 6.1.9** — the constraints on the document's object structure that follow from
  the above: no file may rely on a construct ISO 32000-2 deprecates, and the `/Length` of
  every stream shall be correct as written.
* **6.1.11, permissions** — the permission bits shall not deny the operations an archival
  reader needs.
* **6.1.12, the document catalogue** — the entries a conforming catalogue shall and shall
  not carry.

### 2.3 Graphics — 6.2

The subclauses with enforced requirements, in the standard's order:

* **6.2.2, content streams** — a content stream that references other objects, such as
  images and fonts, shall have an explicitly associated `/Resources` dictionary as
  ISO 32000-2 7.8.3 describes, and that dictionary shall define **all** named resources
  the stream references. An inherited or implied resource dictionary is not enough.
* **6.2.3, the output intent** — where more than one entry appears in an `/OutputIntents`
  array, every entry carrying a `/DestOutputProfile` shall give the **same indirect
  object** as its value, and that object shall be a valid ICC profile stream. This is what
  makes a file simultaneously conforming to this part and to another standard well-defined
  rather than contradictory. PDF 2.0's page-level output intents, which the previous
  milestone introduced, are permitted and are subject to the same rule.
* **6.2.4, colour spaces** — device colour shall be made determinate, either by a
  device-independent default colour space in force where the device space is used, or by
  a PDF/A output intent whose destination profile matches the device space's kind.
  `/DeviceRGB` requires an RGB-based resolution, `/DeviceCMYK` a CMYK one, `/DeviceGray` a
  grey or either of the others. This is the single largest source of non-conformance in
  the corpus at this level, and the largest single reason a naive conversion fails.
* **6.2.5, the extended graphics state** — part 2's constraints, restated. They are in
  `/app/SPEC/pdfa1-2.md` and are not repeated here.
* **6.2.7 and 6.2.8, images and their metadata** — the image dictionary entries a
  conforming file may carry, and the metadata that shall accompany them.
* **6.2.9** — the constraint on the content stream operators a conforming file may use.
* **6.2.10, fonts** — the largest subclause, and the one nearest the earlier milestones'
  work:
  * **6.2.10.3**, composite fonts: for a Type 0 font, the `/CIDSystemInfo` of the CIDFont
    and of the encoding shall agree — freely where the encoding is `/Identity-H` or
    `/Identity-V`, and by identical `/Registry` and `/Ordering` with a `/Supplement` no
    greater than the CMap's otherwise. Every embedded Type 2 CIDFont shall carry a
    `/CIDToGIDMap` that is either a stream or the name `/Identity`. Every CMap used, except
    those ISO 32000-2 9.7.5.2 Table 116 lists, shall be embedded; an embedded CMap's
    `/WMode` shall equal the `/WMode` in the stream; and a CMap shall not reference another
    CMap outside that table.
  * **6.2.10.4, embedding** — the font programs for all fonts used to show text shall be
    embedded. A font referenced solely with text rendering mode 3 shows nothing and is
    exempt. An embedded font shall define every glyph the file selects from it, and for a
    TrueType font the character codes shall map to glyphs by ISO 32000-2 9.6.5 without a
    non-standard mapping.
  * **6.2.10.5, metrics** — for every embedded font, the glyph width in the font dictionary
    and the width in the embedded font program shall agree for every glyph the file selects.
    Glyphs referenced only with rendering mode 3 are exempt.
  * **6.2.10.6, encodings** — every non-symbolic TrueType font shall name `MacRomanEncoding`
    or `WinAnsiEncoding` as its `/Encoding` or as the `/BaseEncoding` inside it, and shall
    not define a `/Differences` array unless every glyph name in it is in the Adobe Glyph
    List and the embedded program carries at least the Microsoft Unicode (3,1) `cmap`
    subtable.
  * **6.2.10.7 to 6.2.10.9** — the character-to-Unicode requirement that in earlier parts
    was level U's, now unconditional, and the `.notdef` and character-set constraints that
    go with it.

### 2.4 Annotations — 6.3

* **6.3.1** — the annotation subtypes a conforming file may contain. At plain `4` they are
  `/Text`, `/Link`, `/FreeText`, `/Line`, `/Square`, `/Circle`, `/Polygon`, `/PolyLine`,
  `/Highlight`, `/Underline`, `/Squiggly`, `/StrikeOut`, `/Caret`, `/Stamp`, `/Ink`,
  `/Popup`, `/Widget`, `/PrinterMark`, `/TrapNet`, `/Watermark`, `/Redact` and
  `/Projection`; every other subtype — among them `/Sound`, `/Screen`, `/Movie`, `/3D`,
  `/RichMedia` and `/FileAttachment` — is non-conforming. `4e` adds `/3D` and `/RichMedia`,
  and `4f` adds `/FileAttachment` (§2.9); `/Sound`, `/Screen` and `/Movie` are permitted at
  none of the three.
* **6.3.2** — except for annotation dictionaries whose `/Subtype` is `/Popup`, every
  annotation dictionary shall contain the `/F` flags entry.
* **6.3.3** — every annotation, including a `/Widget` used for a form field, shall have at
  least one appearance dictionary, with exactly two exceptions: an annotation whose `/Rect`
  is degenerate (the value at index 1 equal to the value at index 3 and the value at index
  2 equal to the value at index 4), and an annotation whose `/Subtype` is `/Popup`, `/Link`
  or `/Projection`.

### 2.5 Interactive forms — 6.4 — and actions — 6.6

* **6.4.1 and 6.4.2** — a conforming file shall not contain a dynamic XFA form, and the
  `/NeedAppearances` flag shall not be true: the appearance stored in the file is the
  appearance, because an archival file may not depend on a processor regenerating one.
* **6.6.1** — the actions a conforming file may contain. At plain `4` an action's `/S`
  shall be one of `/GoTo`, `/GoToR`, `/GoToE`, `/GoToDp`, `/Thread`, `/URI`, `/Named`,
  `/SubmitForm`, `/JavaScript` and `/RichMediaExecute`. Every other type is prohibited
  wherever it appears, together with any trigger that would run it — among them `/Launch`,
  `/Sound`, `/Movie`, `/ResetForm`, `/ImportData`, `/Hide`, `/Rendition`, `/Trans`,
  `/SetOCGState`, `/GoTo3DView` and the deprecated set-state and no-op actions. `4e`
  additionally permits `/SetOCGState` and `/GoTo3DView` (§2.9). Unlike parts 1 to 3, part 4
  does not prohibit `/JavaScript`. A `/Named` action shall be `/NextPage`, `/PrevPage`,
  `/FirstPage` or `/LastPage`.
* **6.6.3** — the constraints on the actions that remain, and on the additional-actions
  dictionaries that may hold them.

### 2.6 Metadata and identification — 6.7

* **6.7.2, metadata streams** — the catalogue shall carry a `/Metadata` entry whose value
  is a metadata stream; an XMP packet's header shall not use the `bytes` attribute and
  shall not use the `encoding` attribute; every metadata stream in the file shall be
  well-formed XMP.
* **6.7.3, version identification** — the part 4 shape, and the place where a file written
  for an earlier part fails:
  * `pdfaid:part` shall be `4`, being the part number of ISO 19005 the file conforms to;
  * `pdfaid:rev` shall be `2020`. This property is part 4's and has no counterpart in
    parts 1 to 3;
  * `pdfaid:conformance` shall be `E` for a PDF/A-4e file and `F` for a PDF/A-4f file, and
    **a file conforming to neither shall not provide any `pdfaid:conformance` at all**. A
    plain PDF/A-4 file carrying `pdfaid:conformance` `B` — the shape every earlier part
    used — is non-conforming for that reason alone;
  * each of `part`, `rev` and `conformance` shall carry the `pdfaid` namespace prefix.
* **The document information dictionary** — part 4 has no counterpart of ISO 19005-1's
  rule that its entries agree with the XMP (`/app/SPEC/pdfa1-2.md` §6.3). It is constrained
  by 6.1.3 above instead: where present, it holds only `/ModDate`.

### 2.7 Embedded files — 6.9

Plain PDF/A-4 keeps part 2's restriction: **all embedded files shall themselves be
compliant with ISO 19005-1, ISO 19005-2 or ISO 19005-4.** The conditions of §1.2 apply
here too and are restated by part 4 in its own terms — every embedded file's file
specification dictionary shall contain an `/AFRelationship` key describing how the file
relates to the document's content; the stream dictionary shall carry a valid MIME type in
`/Subtype`, falling back to `application/octet-stream`; and the file specification shall
contain both `/F` and `/UF`.

### 2.8 Optional content — 6.10 — alternate presentations — 6.11 — requirements — 6.12

* **6.10** — every optional content configuration dictionary, whether it is the value of
  `/D` or an element of `/Configs`, shall carry a `/Name`; and the `/AS` entry, which makes
  visibility depend on the processor's state, shall not appear.
* **6.11** — a conforming file shall contain no alternate presentation and no page
  transition: what the file shows shall not depend on a presentation mode.
* **6.12** — a conforming file shall not contain a `/Requirements` entry: it shall not
  declare that a processor must implement something in order to show it correctly.

### 2.9 The extended levels — 4e and 4f

Each admits what plain `4` refuses for its own purpose, and announces itself in the
identification. Everything else in §2.2 to §2.8 applies at all three.

* **PDF/A-4e** — engineering content. It permits the `/3D` and `/RichMedia` annotation
  subtypes (6.3.1) and the `/SetOCGState` and `/GoTo3DView` actions (6.6.1), all of which
  plain `4` prohibits. It constrains what it admits: the `/Subtype` in a 3D stream
  dictionary (ISO 32000-2 13.6.3) shall be `U3D` or `PRC`. Plain `4`'s requirement that
  every embedded file itself conform (§2.7) is not applied at `4e`. `pdfaid:conformance`
  shall be `E`.
* **PDF/A-4f** — embedded files of any format, as part 3 permits at its own level, and the
  `/FileAttachment` annotation subtype (6.3.1). A conforming file shall contain an
  `/EmbeddedFiles` key in the name dictionary of the document catalogue — the permission is
  not merely tolerated, it is announced. `pdfaid:conformance` shall be `F`.

A file validated at the wrong one of these three fails on the identification rule before
anything else is reached, which is why the flavour is named on every invocation.

---

## 3. PDF/UA-1 — ISO 14289-1:2014

A PDF/UA-1 file is not an archival file. The standard says nothing about whether the file
will still be readable in fifty years and everything about whether its content is
*described* — whether a processor that cannot see the page can still recover what the page
says, in what order, and in what role. The requirement is therefore about the structure
hierarchy of ISO 32000-1 14.7 and 14.8, and about the places where content escapes it.

**This clause set is the one that most needs §3.12 read alongside it.** ISO 14289-1
contains requirements that no program can decide. They are stated here because they are
the standard's, and §3.12 says exactly which of them a conformance checker tests and which
it cannot — and nothing a checker cannot test is required in this workspace.

### 3.1 Identification — clause 5

The PDF/UA version and conformance level shall be specified using the PDF/UA
identification extension schema in the document's XMP: `pdfuaid:part` shall be the part
number of the standard the file conforms to, and `part` — like the optional `amd` and
`corr` properties that name an amendment and a corrigendum — shall carry the `pdfuaid`
namespace prefix. A file with a perfect structure tree and no `pdfuaid:part` is not a
PDF/UA-1 file.

### 3.2 General — 7.1

The clause that carries the standard, and the one most files fail:

* the logical structure of a conforming file **shall be described by a structure hierarchy
  rooted in the `/StructTreeRoot` entry of the document catalogue**, as ISO 32000-1 14.7
  describes. This is the root requirement; a file without one fails here and, in
  consequence, almost everywhere in §3.3 onward;
* **content shall be marked as an artifact or tagged as real content.** Every mark on the
  page is one or the other. Content that is neither — text emitted outside any marked
  content sequence in a document that claims a structure tree — is the defect this rule
  names, and it is the most common one in the corpus;
* content marked as an artifact shall not appear inside tagged content, and tagged content
  shall not appear inside content marked as an artifact. The two are exclusive at every
  level of nesting, not merely at the top;
* every non-standard structure type shall be mapped, in the role map of the structure tree
  root, to the nearest functionally equivalent standard type as ISO 32000-1 14.8.4 defines
  them. The mapping may be indirect — a non-standard type may map to another non-standard
  type — but it **shall terminate at a standard type**, and a circular mapping shall not
  exist. A standard type shall not be remapped to anything;
* the file shall have a `/Suspects` value of false in its mark information dictionary
  (ISO 32000-1 Table 321) — a document that declares its own tagging suspect is not
  conforming;
* the catalogue shall contain a `/Metadata` key whose value is a metadata stream, with
  `/Type` `/Metadata` and `/Subtype` `/XML`;
* that metadata stream shall contain a `dc:title` entry that clearly identifies the
  document, `dc` being the Dublin Core prefix;
* the catalogue shall include a `/ViewerPreferences` dictionary containing
  `/DisplayDocTitle` with the value **true** — so that a processor shows the document's
  title rather than its file name.

### 3.3 Text, and the structural nesting that lives with it — 7.2

Clause 7.2 is where the standard's text requirements and the tagging model's well-
formedness rules meet, and it holds more distinct requirements than any other clause.

**Natural language.** The language of text shall be *determinable* — for the page content,
for a `/Lang` on the catalogue or on a structure element, for the text in an `/Alt`, an
`/ActualText` or an `/E` attribute (including inside a `/Span` marked-content sequence),
for the `/Contents` of an annotation, for the `/TU` of a form field, for outline entries,
and for the document metadata. Separately, **where a `/Lang` entry is present** — on the
catalogue, on a structure element dictionary or in a property list — its value shall be a
language identifier as ISO 32000-1 14.9.2 describes, which is to say a Language-Tag as
RFC 3066 defines one. Note the shape of these two requirements: one asks that a language
be determinable at all, the other that the tag be well formed. Neither asks, or could ask,
whether the tag names the language actually used.

**Tables.** A `/Table` element may contain only `/TR`, `/THead`, `/TBody`, `/TFoot` and
`/Caption` elements; it may contain at most one `/Caption`, which shall be its first or
last child; it shall contain zero or one `/THead` and zero or one `/TFoot`; and where it
contains a `/THead` or a `/TFoot` it shall contain one or more `/TBody`. A `/THead`,
`/TBody` or `/TFoot` may contain only `/TR` and shall be contained in a `/Table`. A `/TR`
may contain only `/TH` and `/TD` and shall be contained in a `/Table`, `/THead`, `/TBody`
or `/TFoot`. A `/TH` or `/TD` shall be contained in a `/TR`. **Table rows shall have the
same number of columns, counting column spans.**

**Lists.** An `/L` element may contain only `/L`, `/LI` and `/Caption` elements, and may
contain a `/Caption` only as its first child. An `/LI` may contain only `/Lbl` and
`/LBody`, and shall be contained in an `/L`. An `/LBody` shall be contained in an `/LI`.

**Tables of contents.** A `/TOC` may contain only `/TOC`, `/TOCI` and `/Caption` elements,
and a `/Caption` only as its first child. A `/TOCI` shall be contained in a `/TOC`.

### 3.4 Graphics — 7.3

A `/Figure` tag shall include an alternative representation or replacement text
representing the content it marks — an `/Alt` or an `/ActualText` — as ISO 32000-1 14.7.2
Table 323 provides. This is the requirement PDF/UA is best known for and the one whose
*satisfaction* a program can see and whose *adequacy* it cannot; see §3.12.

### 3.5 Headings — 7.4

* **7.4.2, numbered headings.** For a document that is not strongly structured as
  ISO 32000-1 14.8.4.3.5 describes, heading tags shall be used so that: if any heading tag
  is used, `H1` is the first; a level may be repeated as the content requires; a descending
  sequence proceeds in strict numerical order without skipping a level; and a document may
  increment its heading sequence without restarting at `H1`.
* **7.4.4, unnumbered headings.** Each node in the tag tree shall contain at most one child
  `H` tag, and a document shall be **either strongly or weakly structured, but not both** —
  it shall not mix the numbered `H1`…`H6` scheme with the unnumbered `H` scheme.

### 3.6 Tables — 7.5

Where a table's structure is not determinable through `/Headers` and `/ID` attributes,
structure elements of type `/TH` shall have a `/Scope` attribute. The nesting requirements
that go with this are in §3.3 above, where the standard's own rule set places them.

### 3.7 Formulae, notes, optional content, embedded files, XFA and security

* **7.7, mathematical expressions** — every mathematical expression shall be enclosed in a
  `/Formula` tag as ISO 32000-1 14.8.4.5 describes, and shall have an `/Alt` or an
  `/ActualText` attribute.
* **7.9, notes and references** — a `/Note` tag shall have an `/ID` entry, and each
  `/Note` tag's `/ID` shall be unique within the document.
* **7.10, optional content** — every optional content configuration dictionary that is the
  value of `/D` or an element of `/Configs` shall contain a `/Name` key, and the `/AS` key
  shall not appear in any of them. Content whose visibility depends on processor state
  cannot be described reliably, which is why this matches part 4's 6.10.
* **7.11, embedded files** — the file specification dictionary for an embedded file shall
  contain non-empty `/F` and `/UF` keys.
* **7.15, XFA** — dynamic XFA forms shall not be used.
* **7.16, security** — an encrypted conforming file shall contain a `/P` key in its
  encryption dictionary (ISO 32000-1 7.6.3.2, Table 21) whose **10th bit position is
  true**: a document may be encrypted, but it shall not use encryption to deny extraction
  for accessibility.

### 3.8 Annotations — 7.18

* **7.18.1, general** — an annotation, excluding `/Widget`, `/PrinterMark` and `/Link`,
  shall be nested within an `/Annot` tag. An annotation — except a `/Widget`, a hidden
  annotation, or one whose rectangle lies outside the crop box — shall have either a
  `/Contents` key or an `/Alt` entry in its enclosing structure element. A form field shall
  have a `/TU` key, or all of its `/Widget` annotations shall have alternative descriptions
  through an `/Alt` in the enclosing structure elements.
* **7.18.2, types** — annotations of subtype `/TrapNet` shall not be permitted.
* **7.18.3, tab order** — every page carrying an annotation shall contain `/Tabs` in its
  page dictionary with the value `/S`, so that tab order follows the structure order.
* **7.18.4, forms** — a `/Widget` annotation shall be nested within a `/Form` tag, per
  ISO 32000-1 14.8.4.5 Table 340.
* **7.18.5, links** — links shall be tagged as ISO 32000-1 14.8.4.4.2 requires, and shall
  carry an alternate description in their `/Contents` key as 14.9.3 describes.
* **7.18.6, media** — in a media clip data dictionary the `/CT` and `/Alt` keys, optional
  in ISO 32000-1 13.2.4.2 Table 274, are **required**.
* **7.18.8, printer marks** — `/PrinterMark` annotations, where present, shall be treated
  as incidental artifacts, as if they were hidden page elements under ISO 32000-1
  14.8.2.2.3.

### 3.9 XObjects — 7.20

A conforming file shall contain no reference XObjects. The content of a Form XObject shall
be incorporated into structure elements as ISO 32000-1 14.7.2 describes — content does not
escape the structure tree by being placed in an XObject.

### 3.10 Fonts — 7.21

Largely the requirements the archival parts make, for a different reason: a processor that
cannot see the page must be able to recover the characters.

* **7.21.3, composite fonts** — the `/CIDSystemInfo` agreement between a Type 0 font's
  CIDFont and its encoding, exactly as §2.3 states it for part 4 but against
  ISO 32000-1 9.7.5.2 Table 118; the `/CIDToGIDMap` requirement on embedded Type 2
  CIDFonts; CMap embedding, the `/WMode` agreement, and the prohibition on a CMap
  referencing another outside Table 118.
* **7.21.4, embedding** — the font programs for all fonts used to show text shall be
  embedded. Where an embedded Type 1 font's `/FontDescriptor` carries a `/CharSet`, it
  shall list the character names of **all** glyphs in the program, used or not; where an
  embedded CID font's carries a `/CIDSet`, it shall identify all CIDs in the program,
  used or not.
* **7.21.5, metrics** — the glyph widths in the font dictionary and in the embedded program
  shall agree for every glyph used.
* **7.21.6, encodings** — the non-symbolic TrueType encoding requirement of §2.3, and:
  **symbolic TrueType fonts shall not contain an `/Encoding` entry** in the font dictionary.
* **7.21.7, Unicode maps** — the font dictionary of every font shall define the map from
  all used character codes to Unicode values, through `/ToUnicode` or another mechanism
  this clause allows. The Unicode values in a `/ToUnicode` CMap shall all be greater than
  zero and shall be neither `U+FEFF` nor `U+FFFE`.
* **7.21.8, `.notdef`** — a conforming document shall not reference the `.notdef` glyph
  from any text-showing operator, in any text rendering mode, in any content stream. A
  character with no glyph is a character the reader loses.

### 3.11 Subclauses with no mechanically enforced requirement

ISO 14289-1 clause 7 also contains **7.6 Lists**, **7.8 Page headers and footers**,
**7.12 Article threads**, **7.13 Digital signatures**, **7.14 Non-interactive forms**,
**7.17 Navigation** and **7.19 Actions**. A conformance checker built for this standard
carries no rule filed under any of them.

That is not the same as saying they impose nothing. Two distinct reasons are mixed here
and it is worth separating them:

* **The requirement is enforced, but filed elsewhere.** The list structure requirements of
  7.6 are real and are checked — as the `/L`, `/LI`, `/Lbl` and `/LBody` nesting rules of
  §3.3, which the checker files under 7.2. A conformance report naming clause 7.2 for a
  broken list is not a mistake.
* **The requirement is not decidable.** Whether a running header has been correctly marked
  as an artifact rather than tagged as content (7.8), whether article threads follow the
  order a reader would take (7.12), whether navigation aids actually aid navigation (7.17)
  — these are requirements about intent, and no rule expresses them.

### 3.12 What a conformance checker can decide, and what it cannot

**This subclause is normative for what is required.** The requirements in §3.1 through §3.10
are the standard's; the requirements a program can test are a strict subset of them; and
**nothing outside that subset is required anywhere in this workspace.**

**What a PDF/UA checker decides.** Everything §3.1 to §3.10 states as a property of the
file's objects: the presence and rootedness of the structure hierarchy; the exclusivity of
artifact and tagged content; the termination, acyclicity and non-remapping of the role
map; the `/Suspects`, `/Metadata`, `dc:title` and `/DisplayDocTitle` entries; the
*presence* of `/Alt` or `/ActualText` on a `/Figure` and on a `/Formula`; the *syntactic
well-formedness* of a `/Lang` value against RFC 3066 and the *determinability* of a
natural language for each kind of text; the nesting and cardinality rules for tables,
lists and tables of contents, and the equal-column-count rule; the heading-sequence rules
and the strongly-or-weakly-structured exclusivity; the `/Scope` requirement on a `/TH`
whose table is not determinable through `/Headers` and `/ID`; `/Tabs /S`; every annotation
requirement in §3.8; and every font requirement in §3.10.

**What is not decided, and is therefore required nowhere.** These are requirements of
ISO 14289-1, and a file can satisfy every checkable rule and still fail them for a human
reader:

* **Reading order.** The standard requires that the structure hierarchy reflect the order
  in which the document is meant to be read. No rule tests it, and none could: the order a
  structure tree expresses is well defined, but whether it is the *intended* order is a
  fact about the document's meaning. A file whose paragraphs are tagged in reverse order is
  fully conforming to every rule in §3.2 and §3.3.
* **Whether alternative text is adequate.** §3.4 and §3.7 require that a `/Figure` or
  `/Formula` have an `/Alt` or `/ActualText`. That the entry exists and is a text string is
  decidable; that the string *describes* the figure is not. `/Alt (image)` satisfies every
  rule.
* **Whether a language tag is correct.** §3.3 requires a `/Lang` to be a well-formed
  Language-Tag and requires a language to be determinable. Whether the text is actually in
  that language is not decidable, and a document of German tagged `en-US` conforms.
* **Whether a tag is the right tag.** That a role map terminates at a standard type is
  decidable; that the standard type it terminates at is the *functionally equivalent* one
  the standard asks for is not. Marking a heading as `/P`, or body text as `/H1`, breaks no
  rule.
* **Whether artifact marking is honest.** That content is either an artifact or tagged is
  decidable; whether the content marked as an artifact really is decoration rather than
  meaning is not. Marking an entire page as an artifact satisfies §3.2.
* **Whether a table's headers describe its layout.** That a `/TH` has a `/Scope`, and that
  rows have equal column counts, are decidable; whether the scope names the cells the
  header actually governs is not.

The practical consequence, and the reason this subclause exists: **do not attempt to make,
or to implement a check for, a judgement no program makes.** Your `verapdf` at `ua1` is
asked for exactly one thing — the verdict a conformance checker reaches from the rules
above — and a file that a human would call inaccessible while every rule passes shall be
reported `PASS`, because that is what the rule set says. Reporting what a standard *means*
rather than what it *tests* is a wrong answer here.

---

## 4. What the utilities must report

The interfaces, their option spellings and the comparison rules are in
`/app/TEST_INTERFACE.md`.

* `verapdf -f 3b|3a|3u` — for each operand, whether the file conforms to ISO 19005-3 at
  that level, under §1 together with the part 2 requirements part 3 inherits.
* `verapdf -f 4|4e|4f` — for each operand, whether the file conforms to ISO 19005-4 at
  that profile, under §2. `4e` and `4f` differ from `4` by the permissions §2.9 lists and
  by the identification value each requires, and a file conforming at one of the three
  generally fails at the other two.
* `verapdf -f ua1` — for each operand, whether the file conforms to ISO 14289-1, under §3
  and **only** to the extent §3.12 allows.

* The earlier milestones' utilities — unchanged. A PDF/A-3, PDF/A-4 or PDF/UA-1 file is an
  ordinary PDF file to all of them, and every one of their reports over such a file is
  what its own milestone fixed.
