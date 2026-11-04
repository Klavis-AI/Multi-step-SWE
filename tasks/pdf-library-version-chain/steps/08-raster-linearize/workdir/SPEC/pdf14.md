# PDF 1.4

Normative text: `/opt/pdfspec/ISO-32000-1-2008.txt` (ISO 32000-1:2008). Clause
numbers below are that document's. Where it describes a feature introduced after
PDF 1.4, that feature is not part of this milestone.

The previous milestone's clauses, in `/app/SPEC/pdf13.md`, still apply in full.
This document adds to them and replaces none of them. The utilities must read
any file whose header declares version 1.0 through 1.4, and must not fail on a
file that merely declares a later version in its header when everything it
actually uses is within this milestone.

Clause 11 also specifies how the constructs in sections 1 to 3 are composited
onto a page. This milestone requires them to be parsed, carried and reported;
the compositing computation itself is not required here.

## 1. Transparency groups — clauses 11.6.6, 8.10.3, 11.4.7

A *group attributes dictionary* (table 96) has an optional `/Type` of `/Group`
and a required `/S` giving the group subtype. The only subtype the standard
defines is `/Transparency`, whose additional entries are table 147:

* `/CS` — the group colour space, a name or an array. Sometimes required:
  11.6.5.2 requires it when the group is the source of a luminosity soft mask.
* `/I` — the isolated flag, a boolean, default `false`.
* `/K` — the knockout flag, a boolean, default `false`.

Such a dictionary is reached two ways:

* as a page object's `/Group` entry (table 30, optional, PDF 1.4), which makes
  it the *page group* of 11.4.7;
* as a form XObject's `/Group` entry (table 95), which makes the form a
  *transparency group XObject* — the form retains its own `/Resources`,
  `/Matrix` and `/BBox` and is still traversed for text as before.

No validation obligation attaches to any of this. An `/S` that is not
`/Transparency`, or a `/CS` naming a colour space that does not exist, is not an
error in this milestone and shall not be rejected, diagnosed or repaired: the
dictionary shall be reported as the file writes it. How it is reported is fixed
by `/app/TEST_INTERFACE.md` §4.1.

## 2. Soft masks — clause 11.6.5

The `/SMask` entry of a graphics state parameter dictionary (table 58, PDF 1.4)
is either the name `/None` or a soft-mask dictionary. The three cases — `/None`,
a dictionary, and the entry being absent — are distinct and must stay distinct.

A soft-mask dictionary is table 144:

* `/Type` — optional, `/Mask` if present.
* `/S` — required, `/Alpha` or `/Luminosity`, selecting which of the two
  derivations of 11.5.2 and 11.5.3 the mask values come from.
* `/G` — required, a transparency group XObject supplying the alpha or colour
  values. When `/S` is `/Luminosity`, that group's attributes dictionary is
  required to carry a `/CS`.
* `/BC` — optional, an array of component values for the backdrop colour,
  consulted only when `/S` is `/Luminosity`, with as many numbers as the group's
  `/CS` has components.
* `/TR` — optional, a transfer function or the name `/Identity`, which is also
  the default.

An image XObject's own `/SMask` entry (11.6.5.3) is a different key with a
different meaning — a soft-mask image — and is read as the stream it is.

Neither the subtype nor the group is validated in this milestone. A `/S` that is
neither `/Alpha` nor `/Luminosity`, a `/G` whose indirect reference resolves to
nothing, and a `/G` form that carries no `/Group` are all things a file may
contain; the reader shall carry on and report what is there. Note that a key
whose indirect reference does not resolve has a reported form of its own — see
`/app/TEST_INTERFACE.md` §4.1, which fixes it exactly.

## 3. Blend modes and constant alpha — clauses 11.3.5, 11.6.3, 11.6.4.4

`/BM` in a graphics state parameter dictionary (table 58, PDF 1.4) is a name or
an array of names. The standard separable modes of table 136 are `/Normal`,
`/Multiply`, `/Screen`, `/Overlay`, `/Darken`, `/Lighten`, `/ColorDodge`,
`/ColorBurn`, `/HardLight`, `/SoftLight`, `/Difference` and `/Exclusion`, with
`/Compatible` equivalent to `/Normal`; the nonseparable modes of table 137 are
`/Hue`, `/Saturation`, `/Color` and `/Luminosity`.

Clause 11.6.3 gives the array rule: the reader uses the first name in the array
that it recognizes, or `/Normal` if it recognizes none. A name that is not a
standard mode is therefore not an error — the fallback *is* the defined
behaviour — and neither the name nor the array shall be rewritten, reordered or
normalized on the way to being reported.

The remaining PDF 1.4 entries of table 58 belong here too: `/CA` and `/ca`, the
stroking and nonstroking constant alpha of 11.6.4.4, numbers in the range 0 to
1 with default 1; `/AIS`, the alpha-source flag, default `false`; and `/TK`, the
text knockout flag.

Numbers are preserved **as written**. `.25`, `0.25` and `0.250` are the same
value and three different tokens, and the token is what is reported. A reader
that re-formats reals on output will disagree with the reference on files that
are otherwise correct.

## 4. Incremental updates — clause 7.5.6

A file may be updated by appending to it, leaving the original bytes intact.
Each update adds a body update, its own cross-reference section, its own trailer
and its own `%%EOF` (figure 3), and a file updated several times therefore
carries several trailers and may carry several copies of one object identifier.

* An update's cross-reference section contains entries only for the objects that
  were changed, replaced or deleted. A deleted object is left in the file and
  marked deleted by its cross-reference entry.
* The added trailer carries every entry of the previous trailer except that
  trailer's `/Prev`, and adds a `/Prev` giving the location of the previous
  cross-reference section (table 15).
* **The most recent copy of each object is the one accessed.** Cross-reference
  information shall be built so that a later section's entry overrides an
  earlier one, at any depth of chain; three or more generations of update are
  ordinary and must resolve.

Everything an update can change must be visible in what the utilities report:
the content stream of a page; the document information dictionary; a page's
`/MediaBox`; the page tree, whether the update adds a page or removes one by
rewriting `/Kids` and `/Count`; and the trailer's `/Root`, which an update may
replace with a different catalogue and a different page tree altogether. An
object that a later section marks free is gone, even though its bytes remain.

Two consequences are worth stating because they are jointly observable. A page
removed from `/Kids` is absent from the page count, from the page list and from
extracted text even though its objects are still present and still resolvable in
the file; what counts is the page tree, not the presence of objects. And a
`/MediaBox` that an update shrinks changes the reported page size *and* removes
from extracted text any text that now falls outside the box — one edit, two
independent observable effects, both required.

A `/Prev` that points at no cross-reference section does not end the document.
The recovery the previous milestone already requires — scanning the file for the
object definitions that are actually there — applies unchanged, and the damage
shall be reported rather than passed over in silence. `/app/TEST_INTERFACE.md`
§4.1 fixes the form of that report and the exit status that goes with it.

Clause 7.5.6 also allows an update to add a `/Version` entry to the document
catalogue. The entry shall be readable and reported like any other catalogue
entry; which version each utility then prints is not compared in this milestone.

## 5. Linearized files — Annex F

Reading only. Nothing in this milestone requires writing a file in this form.

* **F.3.1.** The indirect objects fall into two groups — first-page and
  document-level objects in one, everything else in the other — indexed by
  exactly two cross-reference sections, with all generation numbers 0. The file
  is described as eleven parts, in order.
* **F.3.3, table F.1.** The first object in the body is the *linearization
  parameter dictionary*: all values direct, nothing in the document referring to
  it, and the whole dictionary within the first 1024 bytes of the file so that a
  reader can decide cheaply. Its entries are `/Linearized`, `/L`, `/H`, `/O`,
  `/E`, `/N`, `/T` and the optional `/P`.
* **`/L` is the gate.** Table F.1 requires `/L` to be exactly equal to the
  actual length of the file, and says that a mismatch indicates the file is not
  linearized and shall be treated as ordinary PDF, ignoring the linearization
  information. A file whose `/L` is wrong therefore reports as not linearized,
  no matter how well-formed the rest of its linearization data is, and a file
  whose `/L` is right is taken at its word even when the rest is wrong.
* **F.3.4.** The first-page cross-reference table and trailer are a valid
  cross-reference table that is not at the end of the file, in a single
  subsection with no free entries, carrying entries for the linearization
  parameter dictionary and for the primary hint stream.
* **F.3.6, F.4.** The hint tables live in the primary hint stream. F.4 treats
  the stream as a bit stream, high-order bit first, divided into fields of
  arbitrary width without regard to byte boundaries, each table beginning on a
  byte boundary. The page offset hint table (F.4.1, tables F.3 and F.4) is first
  and begins at offset 0; the shared object hint table (F.4.2) follows. Offsets
  within the hint tables are interpreted as if the primary hint stream were not
  present: a position greater than the hint stream offset has the hint stream
  length added to it to give the real offset.

A file that claims linearization with a correct `/L` but whose hint stream or
parameters do not agree with its contents is still read, and the discrepancies
are reported as warnings rather than errors — the behaviour the qpdf manual
describes for `--check-linearization`, and what `--check` does with the same
data.

## 6. What the utilities must report

The interfaces, their option spellings and the comparison rules are in
`/app/TEST_INTERFACE.md`.

* `pdftotext` — the text of the requested pages, taken from the newest revision
  of every object the pages reach.
* `pdfinfo` — the fields of the previous milestone, over the newest revision.
  `Optimized:` is `yes` when the file carries a linearization parameter
  dictionary whose `/L` equals the file's real length, and `no` otherwise; it
  does not depend on the hint tables being correct, and does not become `no`
  because they are not.
* `pdffonts` — one row per distinct font reachable from the requested pages of
  the newest revision.
* `qpdf --show-object=N` — one object, in its newest revision;
  `--show-object=trailer` — the trailer dictionary of the newest cross-reference
  section, `/Prev` included.
* `qpdf --show-xref` — the cross-reference data in human-readable form: for each
  object, the revision that won.
* `qpdf --show-pages` — the page list, and the content streams of each page,
  after every update has been applied.
* `qpdf --check` — the structural report, its linearization verdict, any
  warnings, and the exit status that goes with them.
* `qpdf --show-linearization` — the linearization parameter data and the hint
  tables, decoded.
