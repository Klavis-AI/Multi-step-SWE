# PDF 1.5

Normative text: `/opt/pdfspec/ISO-32000-1-2008.txt` (ISO 32000-1:2008). Clause
numbers below are that document's. Where it describes a feature introduced after
PDF 1.5, that feature is not part of this milestone.

The previous milestones' clauses, in `/app/SPEC/pdf13.md` and `/app/SPEC/pdf14.md`,
still apply in full. This document adds to them and replaces none of them.

The header version is not a gate on which structures may appear. A file declaring
1.3 or 1.4 that uses every structure below shall be read exactly as one declaring
1.5, and a file declaring 1.5 that uses none of them is an ordinary earlier file.
Nothing in this milestone requires *writing* any of these structures.

## 1. Cross-reference streams — clause 7.5.8

### 1.1 General — 7.5.8.1

Cross-reference information may be stored in a cross-reference stream instead of a
cross-reference table. A cross-reference stream is an ordinary stream object
(7.3.8) and carries, for one cross-reference section, the information a
cross-reference table (7.5.4) and a trailer (7.5.5) carried between them: the
trailer's entries in the stream dictionary, the table's entries in the stream data.

The value following `startxref` is the byte offset of the cross-reference stream
object rather than of an `xref` keyword. In a file that uses cross-reference
streams throughout, the keywords `xref` and `trailer` shall no longer appear, so
that apart from the header, `startxref`, `%%EOF` and comments the file may be
entirely a sequence of objects. A reader that requires either keyword to be present
cannot open such a file.

### 1.2 The dictionary — 7.5.8.2, table 17

In addition to the entries common to all streams (table 5) and to trailer
dictionaries (table 15):

* `/Type` — required, `/XRef`.
* `/Size` — required, one greater than the highest object number in this section or
  in any section this one updates; equivalent to a trailer's `/Size`.
* `/Index` — optional, an array of integer pairs, one pair per subsection, each
  giving a first object number and a count. Sorted ascending; subsections shall not
  overlap. **Default `[0 Size]`** — a stream without `/Index` describes objects 0
  through `/Size` - 1.
* `/Prev` — the byte offset of the previous cross-reference stream, with the same
  function as a trailer's `/Prev`. Present only when the file has more than one
  cross-reference stream.
* `/W` — required, an array of integers giving the width in bytes of each field of
  an entry. For PDF 1.5 it always holds three. **A width of zero means the field is
  not present in the data and its default is used**; a zero first element means the
  type field is absent and every entry defaults to type 1. The sum of the widths is
  the length of one entry, which with `/Index` fixes where each subsection starts.
  Different cross-reference streams in one file may use different `/W` values.

Restrictions that follow from the reader needing this dictionary before it can
resolve anything: every entry in table 17 shall be a direct object, including every
element of the `/Index` and `/W` arrays, and so shall `/Filter` and `/DecodeParms`
if the stream is encoded. Other entries — `/Root` among them — may be, and usually
are, indirect.

### 1.3 The data — 7.5.8.3, table 18

Each entry has one or more fields, the first designating the entry's type. Fields
are written in increasing field order, each exactly as many bytes as `/W` says, and
a field of more than one byte is stored **high-order byte first**. In PDF 1.5
through 1.7 only types 0, 1 and 2 are defined; any other value shall be interpreted
as a reference to the null object, so that new types may be added later.

| type | field 2 | field 3 |
| --- | --- | --- |
| 0 — free | object number of the next free object | generation to use if reused |
| 1 — in use, not compressed | byte offset of the object from the start of the file | generation number, default 0 |
| 2 — compressed | object number of the object stream holding it | index of this object within that stream |

Type 1 corresponds to a table's `n` entry and type 0 to its `f` entry. Type 2 is
new and is the only route to an object stored in an object stream.

Being a stream, a cross-reference stream is itself an indirect object and shall have
an entry — normally in itself.

The stream data may be encoded like any other stream; `/FlateDecode` with a PNG
predictor is the ordinary case, and the predictor applies to the entry data as rows
of `/W`-summed width. The filters and predictors of the first milestone are
unchanged here.

### 1.4 Hybrid-reference files — 7.5.8.4, table 19

A file may carry both a classic cross-reference table and a cross-reference stream,
so that a reader that knows nothing of cross-reference streams can still open it. In
such a file an update section's trailer may carry `/XRefStm`, an integer byte offset
of a cross-reference stream. The normative search order is: look in the standard
cross-reference section; if the object is not found there, look in the
cross-reference stream named by `/XRefStm`; only then follow `/Prev` to the previous
section. `/XRefStm` shall appear only in an update section's trailer, never in the
main one, and `/Size` shall cover the objects the stream defines.

`/XRefStm` shall be read and shall not cause the file to be rejected. **No
case depends on it.** The reference tools do not agree on it — one ignores the entry
entirely, the other prefers a valid classic entry — with the consequence that a file
carrying it and the same file without it are indistinguishable through every command
in `/app/TEST_INTERFACE.md` §2. It is specified here for completeness and for files
you may encounter, and nothing you do with it can be measured.

## 2. Object streams — clause 7.5.7

An object stream is a stream object in which a sequence of indirect objects is
stored, instead of their being stored at the outermost level of the file. The term
*compressed object* is used for an object stored this way whether or not the stream
is actually encoded with a compression filter.

### 2.1 The dictionary — table 16

* `/Type` — required, `/ObjStm`.
* `/N` — required, the number of indirect objects stored in the stream.
* `/First` — required, the byte offset **in the decoded stream** of the first stored
  object.
* `/Extends` — optional, a reference to another object stream of which this one is
  an extension; the `/Extends` links of a collection form a directed acyclic graph.

### 2.2 The data

The decoded stream begins with `/N` pairs of integers separated by white space: for
each stored object, its object number and then the byte offset of its value,
**relative to `/First`**. The offsets are in increasing order, but the objects need
not be in object-number order. After that header the `/N` object values are stored
consecutively; only the values appear, and the `obj` and `endobj` keywords shall not
be used. A conforming writer places the first object immediately after the last byte
offset, but **a conforming reader shall rely on `/First`**, not on where the pair
header appears to end. A compressed dictionary or array may contain indirect
references. An object in an object stream shall not consist solely of an object
reference.

### 2.3 What may not be compressed

Stream objects shall not be stored in an object stream, nor objects with a
generation number other than zero, nor the object that is the value of an object
stream's own `/Length`. In a linearized file the document catalogue, the
linearization parameter dictionary and page objects shall not appear in one. The
generation number of an object stream and of every compressed object shall be zero.

### 2.4 Resolution — the point of the clause

An indirect reference to a compressed object uses ordinary syntax (`14 0 R`); there
is nothing at the reference site to say where the object lives. **The type-2
cross-reference entry is the only thing that says so, and it is what decides which
definition of an object is in force.** Two consequences are normative here:

* Where an object number has a definition both at the outermost level of the file
  and inside an object stream, the cross-reference entry selects between them. A
  reader that scans the file for `N 0 obj` and takes what it finds will select the
  wrong one whenever the entry says type 2, and will do so silently.
* An object that exists *only* inside an object stream is not present in the file's
  bytes in any form a scan can recognise. A document whose page-tree root is
  compressed contains no `/Type /Pages` to be found; it is readable through the
  cross-reference data and by no other route. Recovery by scanning, which the first
  milestone requires and which still applies to everything else, cannot reconstruct
  such a document, and shall report the failure rather than invent a document.

`/Extends` shall be read and reported as written. It is not followed and not
validated at this milestone: an `/Extends` naming an object that is not an object
stream is not an error, and no case depends on it.

## 3. Optional content — clause 8.11

Optional content is content that may be selectively shown or hidden. The structures
below determine, for a document as it is first opened, which content a conforming
reader treats as visible. **That visibility decides what this milestone's text
extraction emits**: content in a group that is off is not emitted.

### 3.1 Optional content groups — 8.11.2.1, table 98

An optional content group (OCG) is a dictionary with `/Type /OCG` required and a
required `/Name` text string for presentation in a user interface. `/Intent` is
optional, a name or an array of names, default `/View`; `/Usage` is optional.

A group has a state, ON or OFF. States are not part of the document: they are
initialized from the default configuration dictionary when the document is opened
(3.4 below). Content belonging to a group is visible when the group is ON and
invisible when it is OFF.

### 3.2 Membership dictionaries — 8.11.2.2, table 99

To express a policy over more than one group, content declares membership in an
optional content membership dictionary (OCMD) instead:

* `/Type` — required, `/OCMD`.
* `/OCGs` — optional, a group dictionary or an array of them. **Null values and
  references to deleted objects shall be ignored.** If the entry is absent, is an
  empty array, or contains only null or deleted references, the membership
  dictionary has no effect on the visibility of any content.
* `/P` — optional, the visibility policy, one of `/AllOn` (visible only if all the
  groups in `/OCGs` are ON), `/AnyOn` (visible if any is ON), `/AnyOff` (visible if
  any is OFF) or `/AllOff` (visible only if all are OFF). **Default `/AnyOn`.**

`/VE`, the visibility expression, is PDF 1.6 and is outside this milestone; where it
is present it shall be readable and reported like any other array, and `/OCGs` with
`/P` govern visibility.

`/Intent` — 8.11.2.3 — has two defined names, `/View` and `/Design`. A group takes
part in determining visibility when one of its intents is in the current
configuration's intent set; if there is no match the group has no effect on
visibility, and if the configuration's `/Intent` is an empty array no group does, so
all content is visible. The default on both sides is `/View`, which is the case
every corpus file exercises.

### 3.3 Making content optional — 8.11.3

**In content streams — 8.11.3.2.** A section of a content stream is made optional by
enclosing it between `BDC` and `EMC` (14.6) with the tag `/OC`. The property list
operand shall be a name looked up in the `/Properties` subdictionary of the current
resource dictionary (14.6.2), resolving to an OCG or an OCMD. The tag shall be `/OC`
*and* the operand shall resolve to a valid group or membership dictionary; marked
content tagged `/OC` whose operand is neither is not optional content. This applies
in any content stream — a page's, a form or pattern's, a Type 3 glyph description,
an annotation's appearance.

Sections may nest. **Nested optional content is visible only if every enclosing
level is visible**: if the outer level is hidden, the inner level is hidden whatever
its own configuration says.

**In XObjects — 8.11.3.3.** A form or image XObject may carry an `/OC` entry, an OCG
or an OCMD, making the whole XObject optional. Its visibility is that of the group
or policy *together with* the visibility in force where the `Do` operator appears.

**What hiding means — 8.11.3.1.** Hidden content is not drawn, but graphics state
operations still apply, and the side effects of drawing operators still apply — in
particular **the current text position is still updated for hidden text**. Graphics
state after an `EMC` is the same whether the section was visible or not. An XObject
or annotation made optional may be skipped entirely, because nothing inside it
persists beyond its own content stream.

### 3.4 Configuration — 8.11.4.2 and 8.11.4.3, tables 100 and 101

The catalogue's optional `/OCProperties` entry holds the optional content properties
dictionary: `/OCGs`, a required array of indirect references to **all** the optional
content groups in the document, in any order; `/D`, the required default
configuration dictionary; and `/Configs`, an optional array of alternates.

**If `/OCProperties` is absent, a conforming reader shall ignore any optional
content structures in the document** — a `/OC` marked-content section in a file with
no `/OCProperties` is ordinary content, and its text is emitted. This is a rule about
the catalogue, not about the content stream, and it is required.

The `/D` dictionary sets the initial states. Of table 101 the entries that decide
visibility are:

* `/BaseState` — a name initializing every group at once: `/ON`, `/OFF`, or
  `/Unchanged`. **Default `/ON`.**
* `/ON` — an array of groups set ON after `/BaseState` is applied.
* `/OFF` — an array of groups set OFF after `/BaseState` is applied.

**The order is normative and is the whole of the algorithm**: apply `/BaseState` to
every group in `/OCGs`, then process `/ON` and `/OFF`, which override it. A file
with `/BaseState /OFF` and a group listed in `/ON` has that group ON. A file with
neither `/ON` nor `/OFF` has every group ON by the `/BaseState` default, so content
in a group that no array mentions is visible.

`/Name`, `/Creator`, `/Intent`, `/AS`, `/Order`, `/ListMode` and `/RBGroups` shall
be readable and reported as written. `/Order` and `/ListMode` describe presentation
in a user interface and `/AS` describes automatic state changes from external
factors; none of them changes the initial visibility this milestone computes, and no
case depends on them. `/Locked` is PDF 1.6 and is outside this milestone.

## 4. The JPXDecode filter — clause 7.4.9

`JPXDecode` (PDF 1.5) names data encoded by the JPEG2000 method. It applies only to
image XObjects, never to inline images (8.9). The filter expects to read a complete
JPX file structure, which carries its own colour space, bit depth and dimensions.

**This milestone carries such a stream through; it does not apply the filter.** What
is required is exactly what the first milestone already required of a filter it does
not implement, and it is required precisely:

* the stream shall be located and its `/Length` resolved like any other stream, and
  the document containing it shall read normally — a page carrying a JPX image is an
  ordinary page, and the text on it extracts as usual;
* the stream's **raw bytes shall be returned exactly as stored**, byte for byte, and
  no inspection of the codestream shall gate this. A payload that is not valid
  JPEG2000 data at all is still returned unchanged and is still not an error: nothing
  in this milestone validates a codestream, and a reader that rejects a stream
  because its contents do not parse as JPEG2000 is wrong;
* a request for the **decoded** contents of such a stream shall be refused, and the
  refusal shall produce no output rather than partial output. Where `JPXDecode` is
  chained with a filter this implementation does support, the refusal covers the
  whole chain: applying the supported stage and returning its result is a wrong
  answer. `/app/TEST_INTERFACE.md` §2 fixes the exit status and §4.1 the comparison.

The image dictionary entries 7.4.9 constrains — `/Width` and `/Height` matching the
JPEG2000 data, `/ColorSpace` optional because the data carries its own and
overriding it when present, `/SMaskInData` selecting packaged opacity, `/Decode`
ignored unless `/ImageMask` is true — shall be readable and reported as written.
None of them is validated against the codestream here, and no case requires
their interpretation.

## 5. What the utilities must report

The interfaces, their option spellings and the comparison rules are in
`/app/TEST_INTERFACE.md`.

* `pdftotext` — the text of the requested pages, with content in an optional content
  group that is OFF omitted and content in a group that is ON included, resolved
  through whatever combination of cross-reference streams and object streams the
  file uses.
* `pdfinfo` — the fields of the previous milestones. `PDF version` is the header's
  value; it is not a statement about which structures the file contains.
* `pdffonts` — one row per distinct font reachable from the requested pages, the
  font being whichever definition the cross-reference data selects.
* `qpdf --show-object=N` — one object, whether it is stored plainly or inside an
  object stream; `--show-object=trailer` — the trailer dictionary, which for a file
  using cross-reference streams is that stream's dictionary, `/W`, `/Index` and
  `/Prev` included.
* `qpdf --show-xref` — the cross-reference data in human-readable form: for each
  object, the revision that won, and for a compressed object the stream that holds
  it and its index within that stream.
* `qpdf --show-pages` — the page list and each page's content streams.
* `qpdf --check` — the structural report, any warnings, and the exit status that
  goes with them.
* `qpdf --raw-stream-data` — a stream's bytes before any filter is applied;
  `--filtered-stream-data` — its decoded bytes, or a refusal where this milestone
  does not decode the chain.
