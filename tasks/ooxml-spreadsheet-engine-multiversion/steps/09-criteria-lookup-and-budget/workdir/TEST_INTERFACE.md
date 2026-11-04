# TEST_INTERFACE.md — the test interface of `seedcalc`

This document is the contract the automated checker holds you to. Everything the
checker asserts is stated here or in a specification shipped under `specs/`. If
the checker appears to require something this document does not state, the
document is the defect — but the document as written is what governs.

## 1. What you are building

`seedcalc` is a Go library implementing an **OOXML SpreadsheetML document and
formula engine**: it reads and writes `.xlsx`-family packages, models their
parts, and — from milestone 06 onward — evaluates formulas in them.

It is a **library**, not a program. There is no command-line entry point at any
milestone and none is in scope. Everything observable is reached through exported
Go identifiers in the package at the repository root.

## 2. Fixed facts about the workspace

| Fact | Value |
| --- | --- |
| Module path | `github.com/harbor-bench/seedcalc` (declared in `/app/go.mod`) |
| Package name | `seedcalc`, one package, at the repository root |
| Go toolchain | the image's `go`; `GOTOOLCHAIN=local`, `GOPROXY=off` |
| Dependency set | frozen; `/app/vendor/` plus `/app/go.mod` and `/app/go.sum` |
| Build command the checker runs | `go build ./...` with `-mod=vendor` |
| Build the checker must see succeed | the whole module, before any test runs |

**The dependency set is frozen for the entire ladder.** `go.mod`, `go.sum` and
`vendor/` are seeded at milestone 01 and no milestone changes them. Every
package you are permitted to import is already vendored; `go mod tidy`,
`go get` and any edit to `go.mod` will break the build, because the checker runs
with the network off. If a dependency you want is not in `vendor/`, the answer
is to write the code yourself.

Do not delete `vendor/`, `go.mod`, `go.sum`, `TEST_INTERFACE.md`,
`WORKSPACE_HANDOFF.md` or `specs/`. Do not create a directory named
`ooxmlverify/` at the repository root — the checker plants its own there and
will remove yours first.

## 3. How this milestone is checked

Each milestone is checked on its own, by a container that sees only `/app` as
you left it. Each group is a set of Go test cases compiled against your library
through its public interface, and within a group every case must pass. A group
whose case count differs from the number the checker expects — because your
library failed to compile, panicked, timed out, or made a case unreachable —
fails as a whole.

Groups come in two kinds.

**Gate groups add no requirement of their own; a failure there means the
milestone is not satisfied.** The whole-module build is always one of them, and
from milestone 02 onward so is every *inherited-regression* group — the
behaviour earlier milestones established, which this milestone's changes are
most likely to break. Every case in every gate group must pass.

**Requirement groups carry this milestone's new work.** They are this
milestone's *new capability* together with the *peripheral breadth* that keeps
the milestone from being passable by special-casing its headline feature. Every
case in every requirement group must pass too.

The gate takes precedence and is all-or-nothing: breaking a single inherited
behaviour means the milestone is not satisfied, however much of the new
capability you finished. Conversely, preserving every inherited behaviour while
adding nothing new satisfies none of the new work.

Milestones are cumulative. Everything an earlier milestone required is still
required in every later milestone — but it is enforced there as a gate rather
than counted as new work.

## 4. Per-milestone interface

Each subsection below fixes the interface that arrives at that milestone. A
subsection is added when its milestone starts and its text is not rewritten
afterwards, so section `4.NN` always describes milestone `NN` and nothing
later. Where a later milestone supersedes a statement, that subsection states
the change and the earlier text may also carry a marked note naming it; either
way the superseding subsection governs.

Types and functions named here must exist with the spelling, receiver, arity
and result shape given. Where a signature names a spec clause, that clause is
in `specs/` and is normative.

Errors are checked in one of three ways, and which one applies is decided by how
this document states the error, never by the checker's taste:

- **By identity**, where a subsection names a defined error *value* — one of the
  `Err*` sentinels milestone 09 exports, or a standard-library sentinel it says
  is wrapped, such as `archive/zip`'s `ErrFormat`. What is checked is what
  `errors.Is` answers. The message text is not checked at all.
- **By its whole message**, where a subsection quotes one: `err.Error()`
  must equal the quoted string exactly. That is how the seven spreadsheet
  error strings work from milestone 06 onward — §4.06.1 makes `=1/0` return an
  error whose message *is* `#DIV/0!` — and how a scattering of specific
  diagnostics work, each quoted in the subsection that introduces it.
- **By a phrase it must report**, where a subsection quotes a phrase rather
  than a whole message: the message has to contain it, and the rest of the
  wording is yours.

An error this document describes only in prose, with no value named and nothing
quoted, is checked on being non-nil and on nothing else.

### 4.01 OPC package and the SpreadsheetML writer

Milestone 01 is the whole document layer: an in-memory workbook model, a reader
and a writer for the `.xlsx` package around it, and the worksheet features —
cells, styles, geometry, merges, tables, filters, conditional formats, comments
and drawings — that a spreadsheet document can carry. Nothing evaluates a
formula at this milestone; a formula is stored text with an optional cached
result beside it.

#### 4.01.0 Two conventions that govern every signature below

**Worksheets are addressed by their part name, not their display name.** Every
method whose first parameter is `sheet string` resolves it to the worksheet
part `xl/worksheets/<sheet>.xml`, lower-cased. The first worksheet is therefore
`"Sheet1"`, the second `"Sheet2"`, and so on, in the order the parts were
created, whatever the sheets are *called*. `GetSheetName`, `GetSheetMap` and
`GetSheetIndex` deal in display names and are the only place display names
appear. A sheet renamed to `"Summary"` is still reached as `"Sheet1"`. (Superseded from §4.02.1 on: from that milestone every entry point whose `sheet` argument identifies a worksheet is keyed by that worksheet's **name** — the name `NewSheet` or `SetSheetName` gave it — and the part-name resolution described here no longer applies.)

**Mutating methods do not report errors.** With the exceptions listed
explicitly below (`NewStyle`, `NewConditionalStyle`, `AddPicture`, `AutoFilter`,
`CopySheet`, `SetSheetBackground`, `Write`, `Save`, `SaveAs`), a method that
changes the workbook returns nothing. An argument it cannot act on is ignored,
not rejected, and the workbook is left as it was. No method may panic on any
input described here. (Superseded from §4.03.1 on: from that milestone every
mutating entry point returns an `error` and the ignore-what-you-cannot-act-on
rule gives way to reporting it.)

#### 4.01.1 Opening, writing and saving a package

```go
type File struct{ /* unexported */ }

func NewFile() *File
func OpenFile(name string) (*File, error)
func OpenReader(r io.Reader) (*File, error)
func ReadZipReader(r *zip.Reader) (map[string]string, int, error)

func (f *File) Write(w io.Writer) error
func (f *File) Save() error
func (f *File) SaveAs(name string) error
```

`NewFile` returns a workbook holding exactly one worksheet: display name
`Sheet1`, part `xl/worksheets/sheet1.xml`, and one entry in `GetSheetMap`.

`Write` serialises the workbook as an OPC package — a ZIP container built per
ECMA-376 Part 2 §6.2.2/§6.2.3 (part names and media types), §6.5 (package
relationships, including `/_rels/.rels` and `/xl/_rels/workbook.xml.rels`),
§7.2.3 (the `[Content_Types].xml` `Default`/`Override` markup) and §7.3 (the
mapping onto a ZIP file). Every package this library writes carries at least

* `[Content_Types].xml`, `_rels/.rels`, `xl/workbook.xml`,
  `xl/_rels/workbook.xml.rels`, one `xl/worksheets/sheetN.xml` per worksheet,
  and the two document-property parts `docProps/app.xml` and
  `docProps/core.xml`. Both property parts are written from this milestone
  onwards, so a package that omits one is incomplete even though nothing in
  this contract reads their contents;
* in `[Content_Types].xml`, a `Default` for the `rels` extension with content
  type `application/vnd.openxmlformats-package.relationships+xml` and a
  `Default` for the `xml` extension with content type `application/xml`; an
  `Override` for `/xl/workbook.xml` with
  `application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml`,
  an `Override` for each worksheet part with
  `application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml`,
  an `Override` for `/xl/styles.xml` with
  `application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml`, one
  for `/xl/theme/theme1.xml` with
  `application/vnd.openxmlformats-officedocument.theme+xml`, and one for each
  document-property part — `/docProps/core.xml` with
  `application/vnd.openxmlformats-package.core-properties+xml` and
  `/docProps/app.xml` with
  `application/vnd.openxmlformats-officedocument.extended-properties+xml`;
* in `_rels/.rels`, a relationship of type
  `http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument`
  with a non-empty `Id` whose `Target` is a part that exists in the package,
  and a relationship to `docProps/core.xml` of type
  `http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties`;
* in `xl/_rels/workbook.xml.rels`, one relationship per worksheet of type
  `http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet`
  whose `Target` is that worksheet's part, a relationship to `xl/styles.xml` of
  type
  `http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles`,
  and one to `xl/theme/theme1.xml` of type
  `http://schemas.openxmlformats.org/officeDocument/2006/relationships/theme`.

Those strings are written out in full deliberately. Every content type and
relationship type this document names is compared literally, character for
character, wherever a test inspects the package, so an abbreviation or a
plausible synonym fails. The same holds for the constants named in 4.01.9
below. This list is the whole set for a workbook with no tables, comments or
drawings; each feature that adds a part names its own constants where that
feature is described.

Two further parts, `xl/styles.xml` and `xl/theme/theme1.xml`, are written into
every package as well. They are named here so that a reader who lists a written
package and finds nine parts against a required list of seven is not left
wondering which of the two lists is wrong: neither appears in the required
list above, but both are written into every package this library produces, and
a written package that omits either is incomplete. `xl/styles.xml` is read
wherever a style definition — or a rejected definition — is checked. Whatever a
package does contain must still be covered by the content-type stream — an
`Override` for the part or a `Default` for its extension, with no part left
uncovered — so a package that writes a part this list does not name takes on
the obligation to declare it.

Every XML part this library writes opens with an XML declaration naming
version 1.0 and encoding UTF-8 — the `<?xml version="1.0" encoding="UTF-8"?>`
form — with whatever further pseudo-attributes this milestone's writer chooses
beside them; the declaration is part of the written form, not an optional
header.

Everything the workbook holds survives `Write` followed by `OpenReader`.

`OpenReader` returns an error for input that is not a ZIP container: empty
input, arbitrary text, a truncated archive, or a stream that begins with the
ZIP local-file signature but ends there. `OpenFile` returns an error for a path
that does not exist. Neither is required to validate the OPC declarations
inside a well-formed ZIP; see 4.01.10.

`ReadZipReader` reports the container's parts as a map from part name to part
text, together with the number of worksheets it found and an error. On a
package this library wrote it returns no error, names `[Content_Types].xml`,
`_rels/.rels`, `xl/workbook.xml`, `xl/_rels/workbook.xml.rels` and
`xl/worksheets/sheet1.xml` among its keys, and reports one worksheet.

`Save` writes the workbook back to the path it was opened from, and returns an
error when there is no such path — a workbook from `NewFile` has none.
`SaveAs` writes to the named path; the package it produces reads back through
`OpenFile` with its values intact, and the workbook that `OpenFile` returns can
then be `Save`d.

#### 4.01.2 Column titles and A1 references

```go
func ToAlphaString(value int) string
func TitleToNumber(s string) int
```

The bijection of ECMA-376 Part 1 clause 18.17.2.3.1 between a **zero-based** column
ordinal and its bicameral base-26 title. `ToAlphaString(0) == "A"`,
`25 → "Z"`, `26 → "AA"`, `27 → "AB"`, `51 → "AZ"`, `52 → "BA"`, `701 → "ZZ"`,
`702 → "AAA"`, `16383 → "XFD"`. `TitleToNumber` is its inverse over upper-case
titles, so `TitleToNumber(ToAlphaString(i)) == i` for every ordinal from 0
upward. A negative ordinal yields the empty title; the empty title yields `-1`.

A cell is named by an A1-style axis, `"A1"`, `"B2"`, `"AA5"`. Row numbers in an
axis are one-based; the column ordinals `TitleToNumber` produces are zero-based
and index `GetRows`' inner slice directly.

#### 4.01.3 Cell values

```go
func (f *File) SetCellValue(sheet, axis string, value interface{})
func (f *File) SetCellInt(sheet, axis string, value int)
func (f *File) SetCellStr(sheet, axis, value string)
func (f *File) SetCellDefault(sheet, axis, value string)
func (f *File) SetCellFormula(sheet, axis, formula string)
func (f *File) GetCellValue(sheet, axis string) string
func (f *File) GetCellFormula(sheet, axis string) string
func (f *File) GetRows(sheet string) [][]string
```

`GetCellValue` always returns text — the cell's value rendered through its
number format (4.01.4). A cell that was never written reads as the empty
string, and so does its formula.

`SetCellValue` accepts, and coerces as follows:

| Go value | Stored as | `GetCellValue` |
| --- | --- | --- |
| `string` | a string cell, byte-for-byte, including leading and trailing spaces, XML metacharacters and non-ASCII text | the same string |
| `bool` | a cell written so that it reads back as the boolean's text (see the note below the table) | `"true"` / `"false"` |
| any signed or unsigned integer type | a numeric cell in decimal, exactly — `int64(9007199254740993)` does not lose its last digit | the decimal text |
| `float32`, `float64` | a numeric cell in the shortest decimal form that round-trips | e.g. `"1.5"`, `"3.14159"` |
| `time.Time` | the serial date, with a style whose number format is the built-in 22 | the date rendered through that format, e.g. `"7/4/17 12:00"` |
| `nil` | an empty cell | `""` |

A `bool` is stored so that the round trip above holds: reading a cell this
library wrote for a `bool` gives `"true"` or `"false"`, never `"1"` or `"0"`.
Which `t` attribute the writer chooses for it is this milestone's business;
4.01.4's rule for `t="b"` describes how a cell that carries that attribute is
read, not a shape a written boolean has to take. (**Superseded from §4.02.4
on**: from that milestone a `bool` is the `t="b"` cell 4.01.4 describes,
carrying `1` or `0`, so neither this note nor the `bool` row above it holds.)

`SetCellInt` and `SetCellStr` write a number and a string respectively;
`SetCellStr` stores its text as the `t="str"` cell 4.01.4 defines — the string
goes into `v` with no `f` element, not into the shared string table — and
`SetCellDefault` writes raw cell text with no type attribute. All survive the
write/read round trip unchanged. (**Superseded from §4.07.9 on**: from that
milestone the writer keeps a shared string table, and a written string is a
`t="s"` cell whose `v` is the text's zero-based index into
`xl/sharedStrings.xml` — the shape 4.07.9 records.)

`SetCellFormula` stores the formula text and nothing else: `GetCellFormula`
returns it, `GetCellValue` on that cell returns the empty string until a cached
result is supplied — `SetCellDefault` on the same cell supplies one — and the
formula survives the round trip. A cell with no formula returns the empty
string from `GetCellFormula`.

```go
func (f *File) UpdateLinkedValue()
```

Drops the cached result of every formula cell in the workbook, leaving the
formula in place: after it, such a cell reads as the empty string while
`GetCellFormula` still returns the formula. Cells with no formula are
untouched. It operates over the worksheets the workbook knows about, so it
applies to a workbook read back from a package.

`GetRows` returns the worksheet as a rectangle of rendered text, outer index by
row in order and inner index by zero-based column ordinal, with gaps filled by
empty strings.

#### 4.01.4 Stored cell types

A cell's `t` attribute (ECMA-376 Part 1 clause 18.18.11) selects how its `v` is
read:

* `t="s"` — `v` is a zero-based index into the shared string table of
  `xl/sharedStrings.xml` (clause 18.4). The `si` at that index supplies the
  text, with `xml:space="preserve"` honoured so insignificant whitespace is
  kept. An `si` built from several `r` runs reads as its runs concatenated in
  order when the worksheet is read through `GetRows`.
* no `t` — `v` is a number.
* `t="b"` — `v` is the stored boolean literal (`"1"` / `"0"`) and is returned
  as stored.
* `t="e"` — `v` is an error literal such as `#DIV/0!` and is returned as
  stored.
* `t="str"` — a string held in `v` rather than in the shared string table:
  either a formula cell's cached string result — `GetCellFormula` returns the
  formula and `GetCellValue` the cached result — or a string cell written
  inline rather than as a shared-string index, which is the shape every written
  string had before §4.07.9.

Whatever the type, a cell whose `s` attribute selects a `cellXfs` record with a
non-default `numFmtId` is rendered through that number format, using the
built-in format codes of clause 18.8.30: format 22 (`m/d/yy h:mm`) renders the
serial `42920.5` as `7/4/17 12:00`, format 2 (`0.00`) renders `1234.5678` as
`1234.57`. A cell with style 0 is returned exactly as stored.

#### 4.01.5 Styles

```go
func (f *File) NewStyle(style string) (int, error)
func (f *File) SetCellStyle(sheet, hcell, vcell string, styleID int)
func (f *File) GetCellStyle(sheet, axis string) int
```

`NewStyle` takes a JSON style definition, appends the records it needs to
`xl/styles.xml`, and returns the index of the new `cellXfs` record. Index 0 is
the default format and is never returned for a valid definition; each distinct
definition gets its own index, and `cellXfs` ends up holding the default plus
one record per accepted definition. A definition that is not well-formed JSON
is rejected: the call returns `(0, err)` and leaves `xl/styles.xml` unchanged.

The same reservation applies one level down, in the collections `NewStyle`
draws its records from. A freshly created workbook's `xl/styles.xml` already
holds one `font` (the workbook default, `Calibri`), one `border` (all edges
unset) and two `fill`s (the required `none` and `gray125` presets) before any
style is defined, so `fonts/font[0]`, `borders/border[0]` and the first `fill`
a caller-visible definition can occupy are never index 0 for a definition
`NewStyle` accepts: a `font`, `fill` or `border` a style definition adds gets
an index of 1 or greater, parallel to `cellXfs`'s own reserved index 0.

The keys this milestone fixes:

| Key | Effect on `xl/styles.xml` |
| --- | --- |
| `{"number_format": n}` | the `xf`'s `numFmtId` is the built-in format `n` (clause 18.8.30), and values in cells carrying the style are rendered through it |
| `{"custom_number_format": "…"}` | a `numFmt` is registered with an id at or above 164 (the first id clause 18.8.30 leaves to applications) and the given `formatCode`, and the `xf` points at it |
| `{"font": {"family":…, "size":…, "color":"#RRGGBB", "bold":…}}` | a `font` record whose `name/@val`, `sz/@val` and `color/@rgb` carry the requested family, size and colour; a `#RRGGBB` colour becomes the opaque eight-digit form `FFRRGGBB` |
| `{"fill": {"type":"pattern","color":["#RRGGBB"],"pattern":1}}` | a `fill` record holding a `patternFill` with `patternType="solid"` and that foreground colour |
| `{"border": [{"type":"left","color":"RRGGBB","style":n}, …]}` | a `border` record whose named edge carries a line style selected by `n` and that colour written in the opaque eight-digit form — `"0000FF"` becomes `FF0000FF`, exactly as the font and fill colours do; different `style` numbers select different line styles |
| `{"alignment": {"horizontal":…, "vertical":…, "wrap_text":…, "text_rotation":…}}` | an `alignment` child on the `xf`, with `applyAlignment` set, carrying `horizontal`, `vertical`, `wrapText` and `textRotation` |

`SetCellStyle` applies a style identifier to every cell of the rectangle whose
opposite corners are `hcell` and `vcell`, inclusive. `GetCellStyle` returns the
identifier in force at a cell, and `0` for a cell outside every styled range.

#### 4.01.6 Worksheets

```go
func (f *File) NewSheet(index int, name string)
func (f *File) GetSheetName(index int) string
func (f *File) GetSheetIndex(name string) int
func (f *File) GetSheetMap() map[int]string
func (f *File) SetSheetName(oldName, newName string)
func (f *File) CopySheet(from, to int) error
func (f *File) DeleteSheet(name string)
func (f *File) SetActiveSheet(index int)
func (f *File) GetActiveSheetIndex() int
func (f *File) SetSheetVisible(name string, visible bool)
func (f *File) GetSheetVisible(name string) bool
```

**`NewSheet` is keyed by index.** The caller supplies the one-based position the
new sheet takes in the workbook, and its display name. Creating it adds, in one
step: the part `xl/worksheets/sheetN.xml` (N being the index the caller
supplied, so a call for the second sheet is `sheet2.xml`), its `Override` in
`[Content_Types].xml`, a worksheet relationship in `xl/_rels/workbook.xml.rels`
whose `Target` resolves to `worksheets/sheetN.xml`, and a `sheet` element in
`xl/workbook.xml` naming it and carrying that relationship id.

`GetSheetMap` maps one-based index to display name and agrees with
`GetSheetName` and `GetSheetIndex` in both directions. `GetSheetIndex` returns
`0` for a name the workbook does not have; `GetSheetName` returns the empty
string for an index it does not have.

Sheet names are sanitised on the way in, by both `NewSheet` and `SetSheetName`.
The characters `:` `\` `/` `?` `*` `[` `]` are removed, and what remains is
truncated to **31 characters** — counted in characters, not bytes, so a name of
31 multi-byte characters survives whole and is still valid UTF-8. A name that
is already short and clean is stored unaltered. `SetSheetName` on a name the
workbook does not have changes nothing.

`CopySheet(from, to)` copies the content of the worksheet at one-based index
`from` onto the one at index `to`; afterwards the destination holds the
source's cell values and the two are independent, so writing to one does not
change the other. It returns an error, and changes nothing, when either index
is below 1, when the two are equal, or when either does not name a sheet.

`DeleteSheet(name)` removes the named sheet from `xl/workbook.xml` and drops
its worksheet relationship from `xl/_rels/workbook.xml.rels`; afterwards the
name appears in neither `GetSheetMap` nor `GetSheetIndex`, the remaining sheets
are still reachable by name, and the workbook lists one fewer sheet. It is a
no-op for a name the workbook does not have, and a no-op when it would leave
the workbook with no worksheet at all.

`SetActiveSheet(index)` records the one-based index as the active tab: the
`workbookView` element of `xl/workbook.xml` carries `activeTab` set to the
zero-based equivalent, the chosen worksheet's `sheetView` is marked
`tabSelected`, and every other worksheet's is not.

`GetActiveSheetIndex` reports the one-based index of whichever worksheet's
`sheetView` currently carries `tabSelected`, by reading that flag back out of
the worksheet parts — it does not consult `workbookView`/`activeTab`. A
workbook from `NewFile` reports **1**: its single worksheet template already
carries `sheetView tabSelected="1"` before any call to `SetActiveSheet`. If no
worksheet's `sheetView` carries `tabSelected` — which cannot happen to a
workbook produced by this library, but can happen to one read from an
arbitrary package — it returns `0`.

Every sheet starts visible. `GetSheetVisible` is `true` for the sheet a new
workbook is created with and for every sheet added afterwards, without anything
having to set it; a test checks that a freshly added sheet does not report
itself hidden, so the default is a requirement and not an implementation
detail. `SetSheetVisible(name, false)` hides a sheet and `GetSheetVisible` then
reports it hidden while its siblings stay visible; the hidden state survives
the write/read round trip, and setting it back to `true` restores it. A hidden
sheet is still a member of the workbook and still appears in `GetSheetMap`.
`GetSheetVisible` reports a name the workbook does not have as not visible,
which is the same answer it gives for a hidden one — the two cases are not
distinguishable through this call.

#### 4.01.7 Merged cells

```go
func (f *File) MergeCell(sheet, hcell, vcell string)
```

Merges the rectangle whose opposite corners are `hcell` and `vcell`. The range
is written to the worksheet's `mergeCells` (clause 18.3.1.55) as a single
normalised reference — top-left first — whichever order the corners were given
in, so `MergeCell(sheet, "C3", "A1")` yields `A1:C3`. Every cell inside a merged
range reads the anchor's value, and writing to any cell inside the range writes
to the anchor, so the whole range reads back as the new value. Cells outside
are unaffected. Several merged ranges coexist in the order they were added, and
all of this survives the round trip.

#### 4.01.8 Rows, columns and their geometry

```go
func (f *File) SetColWidth(sheet, startcol, endcol string, width float64)
func (f *File) GetColWidth(sheet, column string) float64
func (f *File) SetRowHeight(sheet string, rowIndex int, height float64)
func (f *File) GetRowHeight(sheet string, row int) float64
func (f *File) SetColVisible(sheet, column string, visible bool)
func (f *File) GetColVisible(sheet, column string) bool
func (f *File) SetRowVisible(sheet string, rowIndex int, visible bool)
func (f *File) GetRowVisible(sheet string, rowIndex int) bool
func (f *File) InsertRow(sheet string, row int)
func (f *File) RemoveRow(sheet string, row int)
func (f *File) InsertCol(sheet, column string)
func (f *File) RemoveCol(sheet, column string)
```

**The `row` parameter of these methods is a zero-based row index**, unlike the
one-based row number inside an A1 axis. Columns are named by title.
(Superseded from §4.03.2 on: from that milestone every row-addressed entry
point takes the one-based Excel row number.)

A default worksheet reports row height `20` and column width `64`. `SetColWidth`
applies to every column of the inclusive range and to no column outside it;
`SetRowHeight` applies to exactly one row. Both are recorded in the worksheet
(`cols/col` and `row`, clauses 18.3.1.13 and 18.3.1.73) and survive the round
trip.

`SetColVisible` / `SetRowVisible` hide and show one column or row; the state is
readable back, does not spill onto neighbours, survives the round trip, and is
reversible.

`InsertRow(sheet, n)` inserts a blank row before the row at zero-based index
`n`, shifting that row and everything below it down one; `RemoveRow(sheet, n)`
deletes it and shifts everything below up one. `InsertCol` and `RemoveCol` do
the same for the named column and the columns to its right. Cell contents move
with their row or column.

#### 4.01.9 Tables, filters, conditional formats, comments and drawings

```go
func (f *File) AddTable(sheet, hcell, vcell, format string)
func (f *File) AutoFilter(sheet, hcell, vcell, format string) error
```

`AddTable` writes the range as a table part, `xl/tables/tableN.xml`, numbered
from 1 in the order tables are added. The part carries a non-empty `displayName`
and `id`, both distinct between tables, the `ref` of the range, and a `tableColumns` whose count matches the range, whose members carry a non-empty `id` counting from 1, and whose column names are taken from the range's header row. The worksheet gains a relationship of type
`http://schemas.openxmlformats.org/officeDocument/2006/relationships/table`
targeting `../tables/tableN.xml` and a `tableParts` element carrying `count` equal to the number of tables on the worksheet and a `tablePart` entry with that relationship id, and `[Content_Types].xml` gains an `Override` declaring
`application/vnd.openxmlformats-officedocument.spreadsheetml.table+xml`.
The format keys `table_style`, `show_first_column` and `show_row_stripes` are
recorded on the part's `tableStyleInfo`, whose four boolean flags
`showFirstColumn`, `showLastColumn`, `showRowStripes` and `showColumnStripes`
are written on every table whether or not the format set named them, in the Go
boolean spelling: a table written from `{"show_row_stripes":false}` carries
`showRowStripes="false"` on the part, and one whose format set is silent about a
flag carries that flag as `"false"` too — the attributes are never omitted.

`AutoFilter` records an auto filter over the range: the worksheet's `autoFilter`
carries the `ref` and the `sheetPr` element gains `filterMode="true"`. The format
is `{"column":"<title>","expression":"<expr>"}`; a descriptor that names both
also writes a `filterColumn` for the named column. A descriptor that names no
column or no expression — the empty string among them — is not an error: it
records the range with no `filterColumn` and returns `nil`, so
`AutoFilter(sheet, "A1", "C1", "")` leaves the range recorded and the sheet
unfiltered. The call fails when the named column lies outside the filtered
range, and when the expression is present but not a complete criterion (its
token count is neither 3 nor 7); a malformed descriptor is not otherwise
validated.

```go
func (f *File) NewConditionalStyle(style string) (int, error)
func (f *File) SetConditionalFormat(sheet, area, formatSet string)
```

`NewConditionalStyle` registers a differential format in the `dxfs` collection
of `xl/styles.xml` (clause 18.8.15) and returns its index. **Differential format
indices start at 0**, unlike cell style indices; distinct definitions get
distinct indices, and `dxfs`' `count` matches the number registered. The font
and fill keys are those of 4.01.5, with `#RRGGBB` again becoming `FFRRGGBB`; the
fill's colour lands on the `patternFill`'s `bgColor`. A definition that is not
well-formed JSON is rejected with `(0, err)` and registers nothing.

`SetConditionalFormat` writes a `conditionalFormatting` element (clause
18.3.1.18) whose `sqref` is the given area, holding one `cfRule` per entry of
the JSON array:

* `{"type":"cell","criteria":">","format":<dxf>,"value":"6"}` — a `cfRule` of
  type `cellIs` with `operator="greaterThan"`, a non-empty `priority`, a
  `dxfId` equal to the identifier `NewConditionalStyle` returned, and a
  `formula` child holding the value.
* `{"type":"2_color_scale", "min_type":…, "max_type":…, "min_color":…,
  "max_color":…}` — a `cfRule` of type `colorScale` (clause 18.3.1.16) holding
  a `colorScale` with two `cfvo` of the requested types and the two colours in minimum-then-maximum order, each written in the opaque eight-digit form 4.01.5 gives (`#RRGGBB` becomes `FFRRGGBB`).
* `{"type":"3_color_scale", …, "mid_type":"percentile", "mid_color":…}` — the same with three `cfvo` — the midpoint one carrying `val="50"` — and three colours in minimum, midpoint, maximum order, written in the same eight-digit form.

```go
func (f *File) AddComment(sheet, cell, format string)
```

Adds a comment, `{"author":"…","text":"…"}`, and with it both parts a comment
needs. `xl/commentsN.xml` (clause 18.7), one per worksheet that has comments and
numbered in worksheet order, lists the author in `authors` and the comment in
`commentList` with the cell's `ref`, an `authorId`, and text runs that carry
both the author and the comment text. `xl/drawings/vmlDrawingN.vml` holds the
comment's legacy shape, with client data of `ObjectType="Note"`. The worksheet gains relationships to both parts — one of type `http://schemas.openxmlformats.org/officeDocument/2006/relationships/vmlDrawing` resolving to the VML part, one of type `http://schemas.openxmlformats.org/officeDocument/2006/relationships/comments` resolving to the comments part — and a `legacyDrawing` element (Annex L.5.5.2,
the worked SpreadsheetML Comments example — this edition's Strict schema
dropped the VML shape, so the annex is the reference) whose relationship id
resolves to the VML part, and
`[Content_Types].xml` gains a `Default` for the `vml` extension with content
type `application/vnd.openxmlformats-officedocument.vmlDrawing` and an
`Override` for the comments part declaring
`application/vnd.openxmlformats-officedocument.spreadsheetml.comments+xml`.

```go
func (f *File) AddPicture(sheet, cell, picture, format string) error
func (f *File) GetPicture(sheet, cell string) (string, []byte)
func (f *File) AddShape(sheet, cell, format string)
func (f *File) AddChart(sheet, cell, format string)
func (f *File) SetSheetBackground(sheet, picture string) error
```

Pictures, shapes and charts share the worksheet drawing of ECMA-376 Part 1
clause 20.5: `xl/drawings/drawingN.xml` holds one anchor per object — a `pic`,
an `sp` or a `graphicFrame` — the worksheet points at it through a `drawing`
relationship, and `[Content_Types].xml` declares it as
`application/vnd.openxmlformats-officedocument.drawing+xml`.

`AddPicture` copies the image file into `xl/media/imageN.<ext>` byte for byte and relates the drawing to it with a relationship of the image type, `http://schemas.openxmlformats.org/officeDocument/2006/relationships/image`. It returns
an error, storing nothing, for a path that does not exist and for a file whose
extension is not a supported image type. The supported extensions, matched
case-sensitively against the source path's suffix, are exactly `.png`, `.jpg`,
`.jpeg` and `.gif`; anything else (including the same extensions upper-cased)
is unsupported. It decodes the image to size the anchor, so the caller is
expected to have registered the relevant decoders — in Go that means
blank-importing `image/png` and friends. `format` follows the same JSON shape
as `AddShape`'s below, minus `type`/`color`/`paragraph`: `{"print_obj": bool,
"lock_aspect_ratio": bool, "locked": bool, "x_offset": n, "y_offset": n,
"x_scale": x, "y_scale": y}`, every key optional and defaulting to `true`/
`false`/`0`/`1.0` as shown for `AddShape` below.

`GetPicture` returns the stored file name and the image bytes for the cell a
picture was anchored to, both before and after the round trip, and `("", nil)`
for a cell with no picture.

`AddShape` draws one DrawingML autoshape (an `sp`) anchored at `cell`. Its
`format` JSON:

| Key | Effect | Default when the key is omitted |
| --- | --- | --- |
| `{"type": "…"}` | the preset geometry name, written verbatim to `prstGeom/@prst`; the library does not validate it against the DrawingML preset list | the empty string (still emits an `sp`, with an empty preset name) |
| `{"width": n, "height": n}` | the shape's extent in points, before the scale below is applied | `160`, `160` |
| `{"format": {"print_obj":…, "locked":…, "lock_aspect_ratio":…, "x_offset":…, "y_offset":…, "x_scale":…, "y_scale":…}}` | the same offset/scale/print/lock keys `AddPicture` takes at its top level | `true`, `false`, `false`, `0`, `0`, `1.0`, `1.0` |
| `{"color": {"line": "#RRGGBB", "fill": "#RRGGBB", "effect": "…"}}` | the shape's outline and fill colour | unset — no `ln` or `solidFill` is written |
| `{"paragraph": [{"text": "…", "font": {"bold":…, "italic":…, "underline":…, "family":…, "size":…, "color":"#RRGGBB"}}]}` | one text run per array entry, inside the shape | no text |

`width` and `height` are multiplied by `x_scale`/`y_scale` before conversion
to EMU for the anchor extent.

`SetSheetBackground` stores the image as a media part under `xl/media/imageN.<ext>` — the convention `AddPicture` follows — and relates the worksheet to it with a relationship of the image type, `http://schemas.openxmlformats.org/officeDocument/2006/relationships/image`, adding a `picture` element to the worksheet carrying that relationship id. It returns an error for input it cannot use.

`AddChart` writes the chart as its own part, `xl/charts/chartN.xml`, numbered
from 1 in the order charts are added and declared in `[Content_Types].xml` as
`application/vnd.openxmlformats-officedocument.drawingml.chart+xml`. The drawing holds one `graphicFrame` per chart and relates to each chart part under a distinct relationship id of type `http://schemas.openxmlformats.org/officeDocument/2006/relationships/chart`. The chart's `plotArea` holds the plot element of
its family. The families and their constants:

```go
const (
    Bar      = "bar"      // c:barChart
    Bar3D    = "bar3D"    // c:bar3DChart
    Line     = "line"     // c:lineChart
    Radar    = "radar"    // c:radarChart
    Scatter  = "scatter"  // c:scatterChart
    Pie      = "pie"      // c:pieChart
    Pie3D    = "pie3D"    // c:pie3DChart
    Doughnut = "doughnut" // c:doughnutChart
)
```

The format is `{"type":"<family>","series":[{"name":…,"categories":…,"values":…}]}`.
`Bar`, `Bar3D`, `Line`, `Radar` and `Scatter` charts declare axes (`c:catAx`
and `c:valAx`); `Pie`, `Pie3D` and `Doughnut` charts declare none.

#### 4.01.10 Packages that are not well formed

Reading must be total: a ZIP container that is not a usable SpreadsheetML
package opens without error and without panicking, and simply has nothing in
it.

* A ZIP with no `[Content_Types].xml` and no workbook part opens successfully;
  `GetSheetMap` is empty, `GetSheetName(1)` is the empty string, and
  `GetSheetIndex` finds nothing.
* A package whose workbook declares a worksheet but whose worksheet part is
  missing — a dangling relationship — opens successfully and reports that one
  sheet in `GetSheetMap`; reading a cell of it returns the empty string and
  `GetRows` returns no rows.

Malformed input is rejected only where 4.01.1 says it is: at the container
level, by `OpenFile` and `OpenReader`.

#### 4.01.11 Hyperlinks and pane views

```go
func (f *File) SetCellHyperLink(sheet, axis, link, linkType string)
func (f *File) GetCellHyperLink(sheet, axis string) (bool, string)
func (f *File) SetPanes(sheet, panes string)
```

At this milestone neither setter returns an error; both are unconditional
functions that either store something or silently do nothing. (A later
milestone gives `SetCellHyperLink` and `SetPanes` an `error` return and adds a
capacity limit on hyperlinks — neither is true of this checkpoint, and neither
is documented here.)

A `hyperlink` element (ECMA-376 Part 1 clause 18.3.1.47) is appended to the
worksheet's `hyperlinks` collection (clause 18.3.1.48). `SetCellHyperLink`'s
`linkType` is a closed, case-sensitive enum of exactly two members:

| `linkType` | Effect on the worksheet |
| --- | --- |
| `"External"` | a relationship to `link` is added to the worksheet's part-relationships, and a `hyperlink` element is appended to the worksheet's `hyperlinks` collection with `ref` set to `axis` and `r:id` pointing at that relationship; `link` is stored as-is and never validated as a URL |
| `"Location"` | a `hyperlink` element is appended with `ref` set to `axis` and `location` set to `link` verbatim (typically an in-workbook reference such as `"Sheet2!A1"`, but nothing enforces that shape); no relationship is created |

Any other `linkType` value is a no-op: nothing is appended, no relationship is
created, and the call returns nothing to signal it. (**Superseded from §4.03.1
on**: from that milestone a call the library cannot act on reports it — an
unknown `linkType` is a non-nil error, `invalid link type "…"`, and nothing is
appended either way.) An `axis` inside a merged
range is redirected to the range's anchor first, the same redirection 4.01.7
describes for a write to any cell of a merged range.

`GetCellHyperLink(sheet, axis)` applies the same merged-range redirection `SetCellHyperLink` does, so an `axis` inside a merged range is read at the range's anchor and answers for the anchor's hyperlink. It reports `(true, target)` for an `axis` that matches some stored hyperlink's `ref` — `target` is the `location` value for a
`"Location"` link, and the relationship's resolved `Target` for an
`"External"` one — and `(false, "")` when `axis` matches no stored hyperlink
or the worksheet has none.

`SetPanes(sheet, panes)` takes a JSON descriptor and replaces the worksheet's
current pane/selection state outright — it does not merge with a prior call. It
writes the `sheetView`'s `pane` child (clause 18.3.1.66) and `selection`
children (clause 18.3.1.78). The keys:

| Key | Effect |
| --- | --- |
| `{"freeze": bool}` | when `true`, the `pane` element carries `state="frozen"` |
| `{"split": bool}` | when `true` (and `freeze` is `false`), a `pane` element is still written but without `state="frozen"` |
| `{"x_split": n, "y_split": n}` | the `pane`'s `xSplit`/`ySplit` attributes |
| `{"top_left_cell": "…"}` | the `pane`'s `topLeftCell` attribute, an A1 axis |
| `{"active_pane": "…"}` | the `pane`'s `activePane` attribute |
| `{"panes": [{"sqref": "…", "active_cell": "…", "pane": "…"}, …]}` | one `selection` element per array entry, carrying `sqref`, `activeCell` and `pane` respectively |

`active_pane` and each `panes[].pane` are a closed, case-sensitive enum of
four members: `"topLeft"`, `"topRight"`, `"bottomLeft"`, `"bottomRight"` —
naming which quadrant of a frozen or split view the pane occupies. When both
`freeze` and `split` are `false`, the `pane` element is removed entirely (so a
prior call's freeze/split is undone), but any `panes` selections in the same
call are still written.

### 4.02 Worksheet features and iteration

This milestone turns a worksheet from something you write one cell at a time
into something you can stream, group, constrain, protect, search and draw on.
It also makes one change to the shape of the interface itself, stated first
because everything else in this section depends on it.

#### 4.02.1 The migration: the interface is keyed by worksheet name

Every entry point whose parameter *identifies which worksheet to act on* now
takes that worksheet's **name** in place of the `"sheet"`-plus-index shape
4.01 used for the same purpose — most visibly, `NewSheet(index int, name
string)` from 4.01.6 becomes `NewSheet(name string) int` below.

```go
func (f *File) NewSheet(name string) int
```

`NewSheet` takes the name of the worksheet to create and **returns** the index
that worksheet then has. The returned index is positive and satisfies
`f.GetSheetName(idx) == name` and `f.GetSheetIndex(name) == idx`, and
`f.GetSheetMap()[idx] == name`. Calling `NewSheet` with a name that already
exists does **not** create a second worksheet: the workbook still has the same
number of worksheets, the existing worksheet keeps its index and its cell
values, and the returned index is the existing one.

**This changes only the worksheet-identifying key; it does not remove every
integer parameter from the interface.** Five entry points from 4.01.6 keep
exactly the index they already had, because that index was never the
worksheet-identifying key: `CopySheet(from, to int) error` still copies by
one-based worksheet index; `SetActiveSheet(index int)` and
`GetActiveSheetIndex() int` still set and read the active tab by that same
index; `GetSheetName(index int) string` and `GetSheetIndex(name string) int`
still convert between index and name in both directions. None of the five is
renamed, re-keyed, or removed. A `viewIndex`, such as
`SetSheetViewOptions`'s (4.02.8), selects one of a *named* worksheet's own
views and is a different thing again — it is not a worksheet number.

Row heights move to the same one-based numbering the markup uses:

```go
func (f *File) SetRowHeight(sheet string, row int, height float64)
func (f *File) GetRowHeight(sheet string, row int) float64
```

`row` is the one-based row number. `SetRowHeight(sheet, 1, 50)` sets the height
of the row whose `r` attribute is `1` — it writes `ht="50"` and
`customHeight="true"` on that row and on no other — and `GetRowHeight(sheet, 1)`
reads it back. Setting a height for row 3 leaves rows 1 and 2 without one.

**Only the height moved.** `SetRowVisible`, `GetRowVisible`,
`SetRowOutlineLevel` and `GetRowOutlineLevel` keep the zero-based `rowIndex`
they already had: `rowIndex` 2 is the row whose `r` attribute is `3`. Column
entry points continue to take a column title (`"D"`), not a number.

These worksheet mutators return an `error` when the JSON format descriptor they
are given cannot be parsed: `AddChart(sheet, cell, format string) error`,
`AddComment(sheet, cell, format string) error`,
`AddShape(sheet, cell, format string) error`,
`AddTable(sheet, hcell, vcell, format string) error`,
`SetConditionalFormat(sheet, area, formatSet string) error`. An empty `format`
is not one of those: it is the empty descriptor, the same as `"{}"`, and it
selects the documented defaults rather than returning an error. `AutoFilter`
takes a descriptor too but is not validated as JSON at all — no descriptor it is
given is a parse error — and its empty form is the case §4.02.1 records for it.

#### 4.02.2 The row iterator

```go
type Rows struct{ ... }
func (f *File) Rows(sheet string) (*Rows, error)
func (rows *Rows) Next() bool
func (rows *Rows) Columns() []string
func (rows *Rows) Error() error
```

`Rows` returns an iterator positioned **before** the first row: `Columns()`
returns an empty slice until `Next()` has been called at least once. `Next()`
advances to the next row and reports whether there is one; it returns `false`
at the end of the worksheet and keeps returning `false` on every later call.
Reaching the end is not a failure — `Error()` returns `nil` after exhaustion —
and `Error()` reports only a failure that occurred while reading.

`Columns()` returns the current row's values **positioned by column**: a value
sits at the index its column letter implies, and columns with no cell are the
empty string. A row with values in A and C is `[]string{"a", "", "c"}`. The
slice is not padded beyond the rightmost cell that has a value. Rows are
visited in worksheet order.

`Rows` on a worksheet that does not exist returns a `nil` iterator and an
error:

```go
type ErrSheetNotExist struct{ SheetName string }
func (err ErrSheetNotExist) Error() string
```

The error either is, or unwraps to (`errors.As`), an `ErrSheetNotExist` whose
`SheetName` field is the name that was asked for. Its message text is not
checked; its identity and its `SheetName` field are.

#### 4.02.3 Inline strings and shared formulas

Two cell shapes this library does not itself write must now be **read**.

`GetCellValue` on a cell whose type is `inlineStr` returns the text held in the
cell's own `is` element, and the row iterator returns the same value. ECMA-376
Part 1 clause 18.18.11 (`ST_CellType`) fixes `inlineStr` as the cell type, and
clause 18.3.1.53 (`is`) fixes where the text lives.

A shared formula (ECMA-376 Part 1 clause 18.3.1.40, `f` with `t="shared"`) is
written once on the cell that defines it, carrying `ref` and `si`; every other
member of the group carries only `si`. `GetCellFormula` returns the defining
cell's formula text for **every** member of the group, not only for the cell
that spells it out. The constant `STCellFormulaTypeShared` has the value
`"shared"`.

#### 4.02.4 What a cell can hold

```go
func (f *File) SetCellBool(sheet, axis string, value bool)
```

A boolean cell carries the cell type `b` and the value `1` or `0`;
`GetCellValue` returns `"1"` or `"0"`. `SetCellValue` accepts a `bool` and
produces exactly the same cell. **This is a behaviour change from 4.01.3**,
whose coercion table gave a `bool` as a plain string cell rendering `"true"` /
`"false"` — 4.01 had no `SetCellBool` and no `t="b"` cell type for a
Go-level boolean. From this milestone on, a `bool` is stored the way 4.01.4
already described a `t="b"` cell being read, and `"true"`/`"false"` no longer
appear for a `bool` value.

`SetCellValue` also accepts a `time.Duration`, which is stored as a **number**
— a fraction of a day — with an elapsed-time number format applied. Four hours
thirty minutes is stored as `0.1875`, the cell carries no `t` attribute, the
cell's style index is non-zero, and `GetCellValue` returns `"04:30:00"`.

```go
func (f *File) SetSheetRow(sheet, axis string, slice interface{})
```

`SetSheetRow` writes consecutive cells across one row, starting at `axis` and
moving right, one cell per element. `slice` must be a **pointer to a slice**;
anything else — including a slice passed by value — is ignored and writes
nothing, without panicking. A `nil` element leaves its cell empty. Only the
addressed row is touched, and the row is only as wide as the slice.

Cell-level protection is carried by a style: `NewStyle` accepts a
`"protection"` object with `"locked"` and `"hidden"` members, and the resulting
entry in `cellXfs` sets `applyProtection` together with a `protection` element
carrying those two attributes (ECMA-376 Part 1 clause 18.8.33).

#### 4.02.5 Outline levels and worksheet properties

```go
func (f *File) SetRowOutlineLevel(sheet string, rowIndex int, level uint8)
func (f *File) GetRowOutlineLevel(sheet string, rowIndex int) uint8
func (f *File) SetColOutlineLevel(sheet, column string, level uint8)
func (f *File) GetColOutlineLevel(sheet, column string) uint8
```

A row's outline level is written as `row/@outlineLevel` on exactly one row —
the row whose `r` attribute is `rowIndex + 1` — and on no other. A column's
outline level is written as `col/@outlineLevel` on exactly one `col` element
whose `min` and `max` are both that column's number (`"D"` gives `min="4"
max="4"`). Both read back the level that was set, including after being set a
second time; a row or column that was never grouped reads back `0`, and so does
a row that was never written at all.

```go
type SheetPrOption interface{ ... }
type SheetPrOptionPtr interface{ ... }
func (f *File) SetSheetPrOptions(name string, opts ...SheetPrOption) error
func (f *File) GetSheetPrOptions(name string, opts ...SheetPrOptionPtr) error
```

The options are named types, each of which satisfies `SheetPrOption` by value
and `SheetPrOptionPtr` by pointer, so the same type both sets and reads:

| Option | Underlying type | Default | Markup |
| --- | --- | --- | --- |
| `CodeName` | `string` | `""` | `sheetPr/@codeName` |
| `EnableFormatConditionsCalculation` | `bool` | `true` | `sheetPr/@enableFormatConditionsCalculation` |
| `Published` | `bool` | `true` | `sheetPr/@published` |
| `FitToPage` | `bool` | `false` | `sheetPr/pageSetUpPr/@fitToPage` |
| `AutoPageBreaks` | `bool` | `false` | `sheetPr/pageSetUpPr/@autoPageBreaks` |
| `OutlineSummaryBelow` | `bool` | `true` | `sheetPr/outlinePr/@summaryBelow` |

`OutlineSummaryBelow` is the group summary direction: it reads back `true` on a
fresh worksheet, and setting it to `false` writes `summaryBelow="false"` on
`sheetPr/outlinePr` in the saved worksheet (ECMA-376 Part 1 clauses 18.3.1.61
`outlinePr` and 18.3.1.82 `sheetPr`). Every option round-trips through
`SetSheetPrOptions` then `GetSheetPrOptions`.

#### 4.02.6 Data validation

```go
type DataValidationType int
type DataValidationOperator int
type DataValidationErrorStyle int

type DataValidation struct {
    AllowBlank       bool
    ShowDropDown     bool
    ShowInputMessage bool
    ShowErrorMessage bool
    Type             string
    Operator         string
    Formula1         string
    Formula2         string
    Sqref            string
    ErrorStyle       *string
    ErrorTitle       *string
    Error            *string
    PromptTitle      *string
    Prompt           *string
}

func NewDataValidation(allowBlank bool) *DataValidation
func (dd *DataValidation) SetDropList(keys []string) error
func (dd *DataValidation) SetRange(f1, f2 int, t DataValidationType, o DataValidationOperator) error
func (dd *DataValidation) SetInput(title, msg string)
func (dd *DataValidation) SetError(style DataValidationErrorStyle, title, msg string)
func (dd *DataValidation) SetSqref(sqref string)
func (dd *DataValidation) SetSqrefDropList(sqref string, isCurrentSheet bool) error
func (f *File) AddDataValidation(sheet string, dv *DataValidation)
```

`DataValidationType`, `DataValidationOperator` and `DataValidationErrorStyle`
are each their own defined type over `int`, not an alias — code outside this
package can declare named constants of these types but cannot pass a bare
`int` where one is expected. `ErrorStyle`, `ErrorTitle`, `Error`,
`PromptTitle` and `Prompt` are the five optional string pointers: all five are
`nil` until a setter below fills them in.

`NewDataValidation(b)` sets `AllowBlank` to `b` and leaves both
`ShowInputMessage` and `ShowErrorMessage` false, and `Sqref` empty.

`SetDropList` sets `Type` to `"list"` and `Formula1` to the members joined by
commas inside **one** quoted literal: `[]string{"red","green","blue"}` gives
`"red,green,blue"` (fourteen characters of payload between two `"`
characters — one literal, the members joined by commas).

`SetRange` sets `Formula1` and `Formula2` to the decimal forms of `f1` and
`f2`, `Type` to the name of the type and `Operator` to the name of the
operator. The type constants are `DataValidationTypeCustom`,
`DataValidationTypeDate`, `DataValidationTypeDecimal`,
`DataValidationTypeTextLeng`, `DataValidationTypeTime`,
`DataValidationTypeWhole`, spelt in the markup as `custom`, `date`, `decimal`,
`textLength`, `time`, `whole`. The operator constants are
`DataValidationOperatorBetween`, `…Equal`, `…GreaterThan`,
`…GreaterThanOrEqual`, `…LessThan`, `…LessThanOrEqual`, `…NotBetween`,
`…NotEqual`, spelt `between`, `equal`, `greaterThan`, `greaterThanOrEqual`,
`lessThan`, `lessThanOrEqual`, `notBetween`, `notEqual`. A validation formula
is limited to 255 characters plus the two enclosing quotes. Both setters that
produce one return an error, and change nothing, when the limit is exceeded:
`SetDropList` refuses a quoted literal longer than 257 characters, and `SetRange` refuses when the rule's existing `Formula1` or `Formula2` is already longer than that. (**Superseded from §4.09.17 on**: the two bounds become empty interfaces dispatched on what each end holds, and `Formula1` and `Formula2` then carry the wrapped `<formula1>…</formula1>` fragment §4.09.17 describes rather than a bare decimal.)

`SetInput` records `PromptTitle` and `Prompt` and sets `ShowInputMessage`.
`SetError` records `ErrorStyle`, `ErrorTitle` and `Error` and sets
`ShowErrorMessage`; the style constants `DataValidationErrorStyleStop`,
`DataValidationErrorStyleWarning` and `DataValidationErrorStyleInformation` are
spelt `stop`, `warning` and `information`.

`SetSqref` **accumulates**: each call appends the new reference to `Sqref`
separated by a single space, so three calls give `"A1:B2 D4 F6:F9"`.
`SetSqrefDropList` takes a cell range to use as the list source; with
`isCurrentSheet` true it sets `Type` to `"list"` and `Formula1` to the range,
and with `isCurrentSheet` false it returns an error and leaves `Formula1`
unset, because a source on another worksheet is not supported.

`AddDataValidation` appends the rule to the worksheet. The saved worksheet
carries one `dataValidations` element whose `count` attribute equals the number
of rules, each rule written as a `dataValidation` with its `type`, `operator`,
`sqref`, `allowBlank`, `showInputMessage`, `showErrorMessage`, `errorStyle`,
`errorTitle`, `error`, `promptTitle` and `prompt` attributes and its `formula1`
and `formula2` child elements (ECMA-376 Part 1 clauses 18.3.1.32 and 18.3.1.33).

#### 4.02.7 Worksheet protection, search, merged ranges and saving

```go
type FormatSheetProtection struct {
    AutoFilter, DeleteColumns, DeleteRows, EditObjects, EditScenarios,
    FormatCells, FormatColumns, FormatRows, InsertColumns, InsertHyperlinks,
    InsertRows, PivotTables, SelectLockedCells, SelectUnlockedCells,
    Sort bool
    Password string
}
func (f *File) ProtectSheet(sheet string, settings *FormatSheetProtection)
func (f *File) UnprotectSheet(sheet string)
```

`ProtectSheet` writes a `sheetProtection` element (ECMA-376 Part 1 clause
18.3.1.85) whose `sheet` attribute is always `true` and whose remaining attributes mirror the requested fields, in the lower-camel spellings of clause 18.3.1.85 — the field's name with its first letter lower-cased, except that `EditObjects` is written `objects` and `EditScenarios` is written `scenarios`; a field that was not requested is `false`. `ProtectSheet(sheet, nil)` applies the default restriction set, in
which `objects`, `scenarios` and `selectLockedCells` are true and the
structural permissions such as `deleteRows` are false. A non-empty `Password`
is stored **hashed**, never as the plaintext and never containing it, and the
same password always produces the same stored value; an empty `Password` writes
no password attribute. `UnprotectSheet` removes the `sheetProtection` element
entirely, and is harmless on a worksheet that was never protected.

```go
func (f *File) SearchSheet(sheet, value string, reg ...bool) []string
```

`SearchSheet` returns the coordinates (`"A1"`, `"C3"`) of every cell that
matches. By default the match is **exact and whole-value**: `"appl"` does not
find `"apple"`. Passing `true` for the optional `reg` argument makes `value` a
regular expression, so `"^ap"` finds `"apple"` and `"apricot"` and `"an+a"`
finds `"banana"`. A value that matches nothing returns nothing.

```go
type MergeCell []string
func (f *File) GetMergeCells(sheet string) []MergeCell
func (m MergeCell) GetStartAxis() string
func (m MergeCell) GetEndAxis() string
func (m MergeCell) GetCellValue() string
```

Both signature blocks above are the milestone-02 forms; **§4.03.1 supersedes
them** — from that milestone `SearchSheet` returns `([]string, error)` and
`GetMergeCells` returns `([]MergeCell, error)`, while `MergeCell` itself is
unchanged.

`MergeCell` is its own defined type, not an alias for `[]string` — only a
defined type can carry the three methods above.

`GetMergeCells` returns one entry per merged range on the worksheet, each
reporting the range's start axis, its end axis and the value of its upper-left
cell. An unknown worksheet returns nothing rather than failing — **superseded
by §4.03.1**, which makes an unknown worksheet a non-nil error for this method
like the rest of the surface it lists; the returned slice is still empty.

```go
func (f *File) WriteTo(w io.Writer) (int64, error)
func (f *File) WriteToBuffer() (*bytes.Buffer, error)
```

`WriteTo` makes `*File` an `io.WriterTo`: it writes the package to `w` and
returns the number of bytes written, which equals the number of bytes `w`
received. `WriteToBuffer` returns the same package in a new buffer, the same
length as `WriteTo` produced, and both results are packages `OpenReader` reads
back to the values that were written.

```go
func ReadZipReader(r *zip.Reader) (map[string][]byte, int, error)
```

**This supersedes the `ReadZipReader` signature 4.01.1 declared** —
`map[string]string, int, error` — which was milestone 01's shape and is no
longer the one a later milestone's build must satisfy; 4.01.1's text stands
only as the historical record of what v1.1.0 returned. From this milestone on,
`ReadZipReader` returns every part of an open OPC container keyed by part
name, as raw bytes, together with the number of worksheets the workbook
declares.

This is also why `File` gains one exported field this milestone, superseding
4.01.1's `type File struct{ /* unexported */ }` for this field alone — every
other member of `File` stays unexported:

```go
type File struct {
    XLSX map[string][]byte // part name -> part content
    /* every other member remains unexported */
}
```

The map `ReadZipReader` returns is the same content a `*File` opened from the
same bytes holds in that `XLSX` field.

#### 4.02.8 Sheet view options

```go
type SheetViewOption interface{ ... }
type SheetViewOptionPtr interface{ ... }
func (f *File) SetSheetViewOptions(name string, viewIndex int, opts ...SheetViewOption) error
func (f *File) GetSheetViewOptions(name string, viewIndex int, opts ...SheetViewOptionPtr) error
```

`viewIndex` selects one of the worksheet's views. It may be negative, counting
back from the last view, so on a worksheet with a single view both `0` and `-1`
address it and any other index — `1`, `-2` — is an error from both functions.

| Option | Underlying type | Default | Markup |
| --- | --- | --- | --- |
| `DefaultGridColor` | `bool` | `true` | `sheetView/@defaultGridColor` |
| `RightToLeft` | `bool` | `false` | `sheetView/@rightToLeft` |
| `ShowFormulas` | `bool` | `false` | `sheetView/@showFormulas` |
| `ShowGridLines` | `bool` | `true` | `sheetView/@showGridLines` |
| `ShowRowColHeaders` | `bool` | `true` | `sheetView/@showRowColHeaders` |
| `TopLeftCell` | `string` | `""` | `sheetView/@topLeftCell` |
| `ZoomScale` | `float64` | `0` | `sheetView/@zoomScale` |

Every option round-trips, and a value that is set reaches the saved worksheet's
single `sheetView` element (ECMA-376 Part 1 clauses 18.3.1.87 and 18.3.1.88).
`ZoomScale` is restricted to the closed interval **10..400**: a value outside
it is **silently ignored** — `SetSheetViewOptions` still returns `nil` and the
previous zoom is retained — while `10` and `400` themselves are accepted.

#### 4.02.9 Theme colours

```go
type HSL struct{ H, S, L float64 }
var HSLModel color.Model
func RGBToHSL(r, g, b uint8) (h, s, l float64)
func HSLToRGB(h, s, l float64) (r, g, b uint8)
func (c HSL) RGBA() (uint32, uint32, uint32, uint32)
func ThemeColor(baseColor string, tint float64) string
```

`RGBToHSL` returns hue, saturation and luminance each in `0..1`. Pure red is
`(0, 1, 0.5)`, pure green is `(1/3, 1, 0.5)`, a grey has saturation `0` and hue
`0`, black has luminance `0` and white has luminance `1`. `HSLToRGB` is its
exact inverse for every 8-bit triple: `HSLToRGB(RGBToHSL(r,g,b))` returns the
original bytes.

`HSL` implements `color.Color`: `RGBA()` returns alpha-premultiplied 16-bit
channels with an opaque alpha, so `HSL{0,1,0.5}.RGBA()` is
`(0xffff, 0, 0, 0xffff)`. `HSLModel` is the matching `color.Model`: it converts
a `color.Color` to an `HSL` value and leaves an `HSL` unchanged.

`ThemeColor(base, tint)` applies a tint to a six-character RGB colour and
returns the result as **eight** hex characters, `AARRGGBB`, with an opaque
alpha. A tint of `0` returns the base colour unchanged (`"000000"` gives
`"FF000000"`). A positive tint lightens towards white and a negative tint
darkens towards black, by scaling the luminance in the HSL model: `("000000",
0.5)` and `("FFFFFF", -0.5)` both give `"FF808080"`. This is the conversion
ECMA-376 Part 1 clause 18.8.19 describes for a colour's `tint` attribute, over
the ARGB value of clause 18.3.1.15.

#### 4.02.10 Pictures and comments

```go
func (f *File) AddPictureFromBytes(sheet, cell, format, name, extension string, file []byte) error
func (f *File) GetPicture(sheet, cell string) (string, []byte)
```

`AddPictureFromBytes` adds an image held in memory. `extension` selects the
image type and must be one of `.png`, `.jpg`, `.jpeg`, `.gif`; any other
extension is an error, as is a `format` descriptor that is not valid JSON. A successful call stores the bytes **verbatim** as a part under `xl/media/` keeping the extension, and creates `xl/drawings/drawing1.xml` together with `xl/drawings/_rels/drawing1.xml.rels`. `[Content_Types].xml` gains a `Default` for each of the four image extensions — `jpeg`, `png`, `gif`, `tiff` — that it does not already declare, so the media part is covered by its extension. `GetPicture` returns the stored file
name and those same bytes for a cell that carries a picture, and an empty name
with no bytes for a cell that does not.

The `format` descriptor's `"positioning"` member records how the image moves
with the cells beneath it, as the `editAs` attribute of the anchor (ECMA-376
Part 1 clause 20.5.2.33, `twoCellAnchor`). `"oneCell"` writes
`editAs="oneCell"` and `"absolute"` writes `editAs="absolute"`; the default
positioning writes **no** `editAs` attribute at all. Every picture is anchored
with a `twoCellAnchor` element regardless of positioning, and the drawing part
serialises it with the spreadsheet-drawing namespace prefix — the written form
is `<xdr:twoCellAnchor …>`, as it is for every other anchor in that part.

The descriptor's `"hyperlink"` and `"hyperlink_type"` members attach a link to
the image: the drawing carries an `hlinkClick` element (ECMA-376 Part 1
clause 21.1.2.3.5, the hyperlink-on-click property of a non-visual drawing
property set) and the drawing's relationship part gains a relationship of the
hyperlink type whose `Target` is the given URL and whose `TargetMode` is
`External`.

```go
type Comment struct { Author string; AuthorID int; Ref string; Text string }
func (f *File) GetComments() (comments map[string][]Comment)
```

`GetComments` returns the comments of the whole workbook keyed by **worksheet
name**, one map entry per worksheet that has at least one comment, and an empty
map for a workbook with none. Each `Comment` reports the cell it is attached to
in `Ref`, the author in `Author`, and the comment body in `Text`; `Text`
contains the text that was supplied. `AuthorID` is the comment's index into
that worksheet's own `authors` list — the list of the comments part the comment
is stored in — so with a single author on a worksheet it is `0`.

#### 4.02.11 Charts: 3D, stacked and axis scaling

`AddChart` accepts these further chart types. The plot element and the
`c:grouping` value each type produces are fixed (ECMA-376 Part 1 clauses
21.2.2.16 `barChart`, 21.2.2.15 `bar3DChart`, 21.2.2.5 `areaChart`, 21.2.2.4
`area3DChart`, 21.2.2.17 `barDir`, 21.2.2.77 `grouping`):

| `type` | Plot element | `grouping` | `barDir` |
| --- | --- | --- | --- |
| `bar`, `barStacked`, `barPercentStacked` | `c:barChart` | `clustered`, `stacked`, `percentStacked` | `bar` |
| `col`, `colStacked`, `colPercentStacked` | `c:barChart` | `clustered`, `stacked`, `percentStacked` | `col` |
| `bar3DClustered`, `bar3DStacked`, `bar3DPercentStacked` | `c:bar3DChart` | `clustered`, `stacked`, `percentStacked` | `bar` |
| `col3DClustered`, `col3DStacked`, `col3DPercentStacked` | `c:bar3DChart` | `clustered`, `stacked`, `percentStacked` | `col` |
| `area`, `areaStacked`, `areaPercentStacked` | `c:areaChart` | `standard`, `stacked`, `percentStacked` | — |
| `area3DStacked`, `area3DPercentStacked` | `c:area3DChart` | `stacked`, `percentStacked` | — |

Bar and column charts differ only in `c:barDir`; an area chart declares no
`c:barDir` at all.

**`Bar3D = "bar3D"`, which frozen 4.01.9 declared, is removed at this
milestone** — it is one of the ten symbols v1.4.1 drops relative to v1.1.0 —
and is superseded by the `bar3DClustered`/`bar3DStacked`/`bar3DPercentStacked`
row of the table above; nothing in this checkpoint accepts `"bar3D"` as a
`type` value any longer. The v1.4.1 checkpoint also exports two bare
constants, `Col3D = "col3D"` and `Area3D = "area3D"` (with no `Clustered`,
`Stacked` or `PercentStacked` suffix), that name no distinct plot of their
own — they are not one of the `type` values in the table above — and are out
of scope for what this milestone covers; only the seventeen `type` strings the
table lists are chart types in scope.

Each of those seventeen `type` strings is also exported as a constant of the
same name with its first letter capitalised, and those identifiers are part of
the contract — a test may name any of them:
`Bar = "bar"`, `BarStacked = "barStacked"`, `BarPercentStacked`,
`Col = "col"`, `ColStacked`, `ColPercentStacked`,
`Bar3DClustered = "bar3DClustered"`, `Bar3DStacked`, `Bar3DPercentStacked`,
`Col3DClustered = "col3DClustered"`, `Col3DStacked`, `Col3DPercentStacked`,
`Area = "area"`, `AreaStacked`, `AreaPercentStacked`,
`Area3DStacked = "area3DStacked"`, `Area3DPercentStacked`.

The `format` descriptor's `"x_axis"` and `"y_axis"` objects carry the axis
settings. `"reverse_order": true` reverses that axis: its `c:orientation`
becomes `maxMin` instead of the default `minMax`, and its position moves — the
category axis from `c:axPos val="b"` to `val="t"`, the value axis from
`val="l"` to `val="r"` (clause 21.2.2.130, `orientation`). `"maximum"` and
`"minimum"` fix the axis bounds, written as `c:max` and `c:min` inside
`c:scaling` (clauses 21.2.2.160, 21.2.2.107 and 21.2.2.108); a chart with no
bounds set carries neither element.

#### 4.02.12 Column geometry: inserting a column no longer widens the sheet

```go
func (f *File) InsertCol(sheet, column string)
func (f *File) RemoveCol(sheet, column string)
```

These two are inherited from 4.01.8 unchanged in signature, and one of the
three places where this milestone deliberately changes an inherited
behaviour.

Milestone 01 shifted the named column and everything to its right, and then
widened every row by one further trailing empty cell that it never reclaimed:
one `InsertCol("Sheet1", "B")` on a three-column sheet produced rows of five
cells rather than four, and removing the inserted column afterwards left
rows of four. **At this milestone an insert adds exactly one column.** On a
three-column, two-row sheet, `InsertCol("Sheet1", "B")` leaves the row count
at two, gives every row exactly four cells, and reads back as
`[a1, "", b1, c1]`: the blank is where B was, and there is nothing past the
old last column. Inserting before the first column behaves the same way —
`InsertCol("Sheet1", "A")` on that sheet gives two rows of four with `A1`
empty and the old `A1`, `B1`, `C1` now at `B1`, `C1`, `D1`.

The correction is not a one-off adjustment applied to the first insert only:
the extra cell is never appended, so repeated inserts do not accumulate.
Three consecutive `InsertCol("Sheet1", "B")` calls on the same three-column
sheet give rows of six, reading `[a1, "", "", "", b1, c1]`, and not rows of
nine.

Because the width is now exact, **`InsertCol` and `RemoveCol` are inverses**:
inserting a column and then removing the same column restores the grid
`GetRows` returns, cell for cell and width for width. The restored width is a
property of the worksheet and not of the in-memory grid alone, so it survives
serialisation — an insert, a matching remove, a `WriteToBuffer` and an
`OpenReader` return the grid the workbook started with.

The row identity inherited from 4.01.8 — insert a row, remove the same row,
get the values back — is unaffected and is still checked as inherited
behaviour; it is the column identity that is new here.

Checked against ECMA-376 Part 1 clause 18.3.1.73 (`row`, and the `spans`
attribute that records how wide a row is) and clause 18.3.1.4 (`c`, and the
`r` attribute that fixes a cell's column).

#### 4.02.13 What is unchanged

Everything milestone 01 established still holds, now reached by worksheet name:
the OPC container and its content types and relationships, the typed cell model
with its values, formulas, hyperlinks and styles, the A1 coordinate helpers,
row and column geometry and visibility apart from the column-insert
correction of §4.02.12, merged cells, the sheet lifecycle and
the 31-character sheet-name limit counted in runes. In particular, a workbook
must keep at least one visible worksheet: a request to hide the only worksheet
in a workbook leaves it visible.

**Out of scope at this milestone, though present in the v1.4.1 checkpoint:**
`(f *File) DuplicateRow(sheet string, row int)` is added at v1.4.1, but row
duplication is in scope starting at the milestone that documents its reference-
adjustment behaviour (§2 assigns "row duplication to an arbitrary target row"
to milestone 03); nothing here should be read as specifying `DuplicateRow`'s
behaviour, and a workdir that leaves it unimplemented until milestone 03 is
conforming. `XMLHeaderByte`, `File.Theme *xlsxTheme`,
`StrictNameSpaceSpreadSheet` and the four `StrictSourceRelationship*`
constants (`StrictSourceRelationship`, `StrictSourceRelationshipChart`,
`StrictSourceRelationshipComments`, `StrictSourceRelationshipImage`), and
`STCellFormulaTypeArray`/`STCellFormulaTypeDataTable`/`STCellFormulaTypeNormal`
are likewise new exported symbols at this checkpoint that no milestone-02
entry point exercises; the symbols themselves are out of scope here, and nothing
calls them by name. (`XMLHeaderByte` holds the XML declaration every written
part opens with — §4.01.1 — but the declaration is checked as part of the
written part, not through that symbol.)

### 4.03 Workbook metadata and the error contract

This milestone changes how the whole library reports failure, and it adds the
metadata that lives outside the cell grid: document core properties, defined
names, page setup, headers and footers, per-column styles, a default-font
override, row duplication, twenty-seven further chart families and TIFF media.

The signature change is the substance of the milestone. Read it first; almost
every other clause below depends on it.

#### 4.03.1 The error migration

**Every mutating entry point returns `error`.** A call that completes returns
`nil`; a call that cannot complete returns a non-nil error and leaves the
workbook as it found it. Nothing in this library reports failure by panicking,
by silently doing nothing, or by writing to a log.

**Every reading entry point that can fail returns `(value, error)`,** with the
value at its zero value whenever the error is non-nil, with one documented
exception: `GetRowHeight` on a non-positive row (4.03.2).

These are the signatures after the migration. Methods marked `*` existed at an
earlier milestone with the same name and parameters but no `error` result (or,
for readers, with the value alone); the spelling of the parameters is
unchanged, only the result list.

```go
// Cells
func (f *File) SetCellValue(sheet, axis string, value interface{}) error          // *
func (f *File) SetCellStr(sheet, axis, value string) error                        // *
func (f *File) SetCellInt(sheet, axis string, value int) error                    // *
func (f *File) SetCellBool(sheet, axis string, value bool) error                  // *
func (f *File) SetCellFloat(sheet, axis string, value float64, prec, bitSize int) error // *
func (f *File) SetCellDefault(sheet, axis, value string) error                    // *
func (f *File) SetCellFormula(sheet, axis, formula string) error                  // *
func (f *File) SetCellStyle(sheet, hcell, vcell string, styleID int) error        // *
func (f *File) SetCellHyperLink(sheet, axis, link, linkType string) error         // *
func (f *File) SetSheetRow(sheet, axis string, slice interface{}) error           // *
func (f *File) MergeCell(sheet, hcell, vcell string) error                        // *
func (f *File) GetCellValue(sheet, axis string) (string, error)                   // *
func (f *File) GetCellFormula(sheet, axis string) (string, error)                 // *
func (f *File) GetCellStyle(sheet, axis string) (int, error)                      // *
func (f *File) GetCellHyperLink(sheet, axis string) (bool, string, error)         // *
func (f *File) GetMergeCells(sheet string) ([]MergeCell, error)                   // *

// Rows
func (f *File) SetRowHeight(sheet string, row int, height float64) error          // *
func (f *File) SetRowVisible(sheet string, row int, visible bool) error           // *
func (f *File) SetRowOutlineLevel(sheet string, row int, level uint8) error       // *
func (f *File) InsertRow(sheet string, row int) error                             // *
func (f *File) RemoveRow(sheet string, row int) error                             // *
func (f *File) GetRowHeight(sheet string, row int) (float64, error)               // *
func (f *File) GetRowVisible(sheet string, row int) (bool, error)                 // *
func (f *File) GetRowOutlineLevel(sheet string, row int) (uint8, error)           // *
func (f *File) GetRows(sheet string) ([][]string, error)                          // *
func (rows *Rows) Columns() ([]string, error)                                     // *

// Columns
func (f *File) SetColWidth(sheet, startcol, endcol string, width float64) error   // *
func (f *File) SetColVisible(sheet, col string, visible bool) error               // *
func (f *File) SetColOutlineLevel(sheet, col string, level uint8) error           // *
func (f *File) InsertCol(sheet, col string) error                                 // *
func (f *File) RemoveCol(sheet, col string) error                                 // *
func (f *File) GetColWidth(sheet, col string) (float64, error)                    // *
func (f *File) GetColVisible(sheet, col string) (bool, error)                     // *
func (f *File) GetColOutlineLevel(sheet, col string) (uint8, error)               // *

// Worksheet-level
func (f *File) SetSheetVisible(name string, visible bool) error                   // *
func (f *File) SetPanes(sheet, panes string) error                                // *
func (f *File) ProtectSheet(sheet string, settings *FormatSheetProtection) error  // *
func (f *File) UnprotectSheet(sheet string) error                                 // *
func (f *File) AddDataValidation(sheet string, dv *DataValidation) error          // *
func (f *File) SearchSheet(sheet, value string, reg ...bool) ([]string, error)    // *
func (f *File) GetPicture(sheet, cell string) (string, []byte, error)             // *
func (f *File) UpdateLinkedValue() error                                          // *
```

The general contract these share:

* **Unknown worksheet.** Every method above that takes a `sheet` name returns
  a non-nil error when no worksheet of that name exists, with two documented
  exceptions: `GetRows` reports no rows and a `nil` error, and `SetSheetVisible`
  reports a `nil` error. Do not add errors to those two; do not omit the error
  from any of the others. The same obligation holds outside this block, for the
  methods whose signatures this milestone leaves alone or adds elsewhere:
  `DuplicateRow` and `DuplicateRowTo` (4.03.4), `AddChart`, `AddComment`,
  `AddShape`, `SetConditionalFormat` and `AutoFilter` (4.02.1/4.01.9),
  `SetSheetPrOptions` (4.02.5), and `SetSheetViewOptions` and
  `GetSheetViewOptions` (4.02.8) all report an unknown worksheet with a
  non-nil error. The only other documented no-ops on a name that does not
  exist are `SetSheetName` and `DeleteSheet` (4.01.6).
* **Malformed cell reference.** Any parameter that names a cell — `axis`,
  `hcell` and `vcell` above, and the `cell` of `AddChart` (4.02.1) and
  `GetPicture` (4.02.10) — that is not a well-formed A1-style reference (see
  4.03.3) is a non-nil error. `""`, `"A"`,
  `"1"`, `"1A"`, `"A0"`, `"-A1"`, `"A1:B2"`, `" A1"` and `"$A$1"` are all
  malformed: a bare column, a bare row, a row of zero, a range, a sign, leading
  space and absolute-reference `$` markers are all rejected.
* **Malformed column name.** Any `col`, `startcol` or `endcol` that is not one
  or more ASCII letters is a non-nil error.
* **A rejected call writes nothing.** After an error the workbook must be
  serialisable and must not contain the element the rejected call would have
  written.
* **Success is `nil`, not a sentinel.** A successful mutator returns `nil`; a
  successful reader returns `nil` alongside its value.

Errors are checked on **identity**, never on message text. The only error
identity this milestone fixes is `zip.ErrFormat` (4.03.12). Beyond that, a
caller may only rely on nil versus non-nil, so you are free to choose your own
wording — except where a later section prints a message as part of the
behaviour it states: that message is then the contract, and is checked as
written.

#### 4.03.2 1-based row addressing

Every row-addressed entry point takes an **Excel row number**, counting from
`1`, matching `CT_Row/@r` in ECMA-376 Part 1, 18.3.1.73. There is no row `0`.

**This supersedes §4.02.1 and §4.02.5 for the outline-level entry points.**
Milestone 02 recorded `SetRowOutlineLevel` and `GetRowOutlineLevel` as keeping
a zero-based `rowIndex`: §4.02.5 puts the level on "the row whose `r` attribute
is `rowIndex + 1`", and §4.02.1 says in as many words that only the height
moved. That was true at milestone 02 and it stops being true here — the two
outline-level calls join every other row entry point on 1-based addressing.
`SetRowOutlineLevel(sheet, 2, 5)` writes `outlineLevel="5"` on `<row r="2">`,
where at milestone 02 the same call wrote it on `<row r="3">`, and `rowIndex`
`0`, the first row under the old numbering, is now the error `invalid row
number 0`. Read §4.02.1's "only the height moved" as a statement about
milestone 02 alone. The two **column** outline-level calls are unaffected: they
take a column title rather than an index, and §4.02.5 still describes them.

`SetRowHeight`, `SetRowVisible`, `SetRowOutlineLevel`, `InsertRow`,
`RemoveRow`, `GetRowHeight`, `GetRowVisible`, `GetRowOutlineLevel`,
`DuplicateRow` and `DuplicateRowTo` all return a non-nil error for any row
number less than `1`, and accept `1` as the first legal row. The check on the
row number happens before the worksheet is looked up, so a non-positive row is
an error even on a worksheet that exists.

On that same non-positive-row error, `GetRowVisible` and `GetRowOutlineLevel`
report their zero values (`false` and `0`), matching the general rule in
4.03.1. `GetRowHeight` does not: it reports its default value, `20`, alongside
the non-nil error, never `0`. This is the one reading entry point in this
milestone whose value is not the zero value when the error is non-nil.

A row number **past the end of the used range is not an error**: the sheet is
treated as if the row existed but were untouched.

* `GetRowHeight` of an untouched or out-of-range row is `20` points — the
  default row height — with a `nil` error.
* `GetRowVisible` of a row past the end is `false` with a `nil` error, and of a row inside the used range that was never hidden it is `true` with a `nil` error — only a row that was explicitly hidden reports `false`.
* `GetRowOutlineLevel` of a row past the end is `0` with a `nil` error.
* `RemoveRow` of a row past the end is a no-op with a `nil` error.

`InsertRow(sheet, n)` inserts one empty row so that the row that was at `n`
becomes row `n+1`. `RemoveRow(sheet, n)` deletes row `n` and moves every row
below it up by one. Both renumber the surviving rows; a later `GetRows`
observes the new positions.

#### 4.03.3 Cell and column name conversion

Six package-level functions convert between A1-style names and numeric
coordinates. They are the single definition of what a well-formed reference
is, and the mutators in 4.03.1 validate through them.

```go
func SplitCellName(cell string) (string, int, error)
func JoinCellName(col string, row int) (string, error)
func ColumnNameToNumber(name string) (int, error)
func ColumnNumberToName(num int) (string, error)
func CellNameToCoordinates(cell string) (int, int, error)
func CoordinatesToCellName(col, row int) (string, error)
```

Contract, following ECMA-376 Part 1, 18.17.2.3 (an A1 reference is one or
more letters naming the column followed by a 1-based row number):

* **`SplitCellName`** splits `"AB42"` into `("AB", 42, nil)`. The column part
  is returned with the case the caller used: `"a1"` splits to `("a", 1)`. On
  failure it returns `("", -1, non-nil)`.
* **`JoinCellName`** is the inverse and **upper-cases the column**:
  `JoinCellName("zz", 7)` is `"ZZ7"`. A column that is not all letters, or a
  row below `1`, is an error, and the returned name is `""`.
* **`ColumnNameToNumber`** maps `"A"`→1, `"Z"`→26, `"AA"`→27, `"BA"`→53. It is
  case-insensitive. It has **no upper bound** at this milestone: a long name is
  converted arithmetically rather than rejected, so `"Column"` converts to
  `42724228`. On failure it returns `-1` and a non-nil error. (**Superseded from
  §4.07.7 on**: the coordinate helpers agree with the sheet's dimension limits
  there, and a name beyond `TotalColumns` is refused — `"Column"` returns `-1`
  and a non-nil error, not `42724228`.)
* **`ColumnNumberToName`** is the inverse: 1→`"A"`, 27→`"AA"`, 16384→`"XFD"`.
  On failure it returns `""` and a non-nil error.
* **`CellNameToCoordinates`** returns `(column, row)` in that order — column
  first — so `"C5"` is `(3, 5)`. On failure it returns `(-1, -1, non-nil)`.
* **`CoordinatesToCellName`** takes `(col, row)` in that order and returns the
  upper-case name: `(3, 5)` is `"C5"`. A coordinate below `1` on either axis is
  an error, and the returned name is `""`.

Every failure returns the documented zero value *as well as* the error; a
caller that ignores the error must not see a plausible-looking result.

#### 4.03.4 Row duplication

```go
func (f *File) DuplicateRow(sheet string, row int) error
func (f *File) DuplicateRowTo(sheet string, row, row2 int) error
```

`DuplicateRow(sheet, row)` is exactly `DuplicateRowTo(sheet, row, row+1)`.

`DuplicateRowTo(sheet, row, row2)` copies row `row` — its cell values and its
cell styles — and **inserts** the copy at row `row2`, pushing the row that was
at `row2` and everything below it down by one. It is an insertion, not an
overwrite.

* Duplicating downward: on rows `A1..A4`, `DuplicateRowTo(1, 3)` yields
  `A1, A2, A1, A3, A4`.
* Duplicating upward: on rows `A1..A4`, `DuplicateRowTo(4, 2)` yields
  `A1, A4, A2, A3, A4` — the copy is taken from the source row's position
  before the insertion shifts it.
* A target beyond the last used row extends the sheet, leaving the rows in
  between empty: on `A1..A4`, `DuplicateRowTo(2, 7)` yields
  `A1, A2, A3, A4, (empty), (empty), A2`.

Cases that are a **no-op with a `nil` error**, not an error:

* `row2 == row` — copying a row onto itself changes nothing;
* `row2 < 1` — there is no such target;
* `row` is past the last used row — there is nothing to copy;
* the worksheet has no rows at all.

`row < 1` is an error, as 4.03.2 requires, and the worksheet is left unchanged.

The copy carries the source cells' style indices, and they survive a save and
a reopen: a cell duplicated from a bold cell is still bold when read back.

#### 4.03.5 Document core properties

```go
type DocProperties struct {
    Category       string
    ContentStatus  string
    Created        string
    Creator        string
    Description    string
    Identifier     string
    Keywords       string
    Language       string
    LastModifiedBy string
    Modified       string
    Revision       string
    Subject        string
    Title          string
    Version        string
}

func (f *File) SetDocProps(docProperties *DocProperties) error
func (f *File) GetDocProps() (*DocProperties, error)
```

These map onto the OPC **core properties part**, `/docProps/core.xml`
(ECMA-376 Part 2, clause 8), whose vocabulary is Dublin Core plus the
`cp:` extensions: `dc:title`, `dc:subject`, `dc:creator`, `cp:keywords`,
`dc:description`, `cp:lastModifiedBy`, `cp:revision`, `dcterms:created`,
`dcterms:modified`, `cp:category`, `cp:contentStatus`, `dc:identifier`,
`dc:language`, `cp:version`. `Created` and `Modified` are the
`dcterms:W3CDTF`-typed timestamps and are round-tripped as the caller wrote
them.

Four namespace constants are exported for writing that part:

```go
const NameSpaceDublinCore                    = "http://purl.org/dc/elements/1.1/"
const NameSpaceDublinCoreTerms               = "http://purl.org/dc/terms/"
const NameSpaceDublinCoreMetadataIntiative   = "http://purl.org/dc/dcmitype/"
const NameSpaceXMLSchemaInstance             = "http://www.w3.org/2001/XMLSchema-instance"
```

(The third name is spelled `Intiative`, without the second `i`. That spelling
is part of the interface.)

Behaviour:

* A workbook created by `NewFile` already has a core properties part, with
  `Created` and `Modified` filled in. Every other field is empty — a new
  workbook invents no title, subject, keywords or category.
* `GetDocProps` never returns a nil `*DocProperties` alongside a nil error.
* **`SetDocProps` merges; it does not replace.** Only the fields that are
  non-empty in the argument are written. A second call that leaves `Title`
  empty must not clear a `Title` set by the first call; a second call that
  gives a new non-empty `Title` does replace it.
* The properties survive a save and a reopen unchanged.

#### 4.03.6 Defined names

```go
type DefinedName struct {
    Name     string
    Comment  string
    RefersTo string
    Scope    string
}

func (f *File) SetDefinedName(definedName *DefinedName) error
func (f *File) GetDefinedName() []DefinedName
```

These correspond to `definedNames`/`definedName` in the workbook part,
ECMA-376 Part 1, 18.2.5 and 18.2.6: `@name`, `@localSheetId`, `@comment`, and
the element text, which is the reference the name stands for.

* `Scope` is the **name of a worksheet** for a worksheet-scoped name, and is left empty for a workbook-scoped name. Internally a worksheet scope is the worksheet's `@localSheetId`; the API works in names. A non-empty `Scope` that names no worksheet is **not** an error: it creates a workbook-level name, exactly as `Scope: ""` does, and that name reads back with `Scope` set to the literal string `"Workbook"` — which is what the required round trip of `Scope: "Workbook"` rests on. (The duplicate check compares the requested `Scope` string against each stored name's scope as reported, so two calls that are both spelt `Scope: "Workbook"` are not duplicates of each other.)
* A new workbook has **no** defined names: `GetDefinedName()` returns an empty
  slice.
* `GetDefinedName` reports a workbook-scoped name with `Scope` set to the
  literal string `"Workbook"`. Round-tripping is therefore not symmetric:
  you set `Scope: ""` and read back `Scope: "Workbook"`.
* `Name`, `RefersTo` and `Comment` are reported exactly as they were set.
* **Uniqueness is per scope.** Defining a name that already exists in the same
  scope is a non-nil error and leaves the existing definition in place. The
  same `Name` may be defined once at workbook scope and once per worksheet.
* `_xlnm.Print_Area` and the other built-in names are ordinary worksheet-scoped
  defined names; nothing special-cases them.
* Defined names survive a save and a reopen, scope included.

`GetDefinedName` returns no error: reading the list of names cannot fail.

#### 4.03.7 The default font

```go
func (f *File) SetDefaultFont(fontName string)
func (f *File) GetDefaultFont() string
```

Neither returns an error. `GetDefaultFont` on a new workbook is `"Calibri"`.

`SetDefaultFont` rewrites the **first font entry of the style sheet** —
`xl/styles.xml`, `fonts/font[0]/name/@val` (ECMA-376 Part 1, 18.8.22 `font`,
18.8.29 `name`) — so the change is part of the workbook's style tree and is
persisted: after a save and a reopen `GetDefaultFont` still reports the
override. Registering further styles that do not name a font family must not
disturb it.

#### 4.03.8 Page layout

```go
type PageLayoutOrientation string
type PageLayoutPaperSize   int

const OrientationPortrait  = "portrait"
const OrientationLandscape = "landscape"

type PageLayoutOption interface {
    setPageLayout(layout *xlsxPageSetUp)
}
type PageLayoutOptionPtr interface {
    PageLayoutOption
    getPageLayout(layout *xlsxPageSetUp)
}

func (f *File) SetPageLayout(sheet string, opts ...PageLayoutOption) error
func (f *File) GetPageLayout(sheet string, opts ...PageLayoutOptionPtr) error
```

`PageLayoutOrientation` is a defined type over `string` and
`PageLayoutPaperSize` a defined type over `int`. Each has a value-receiver
`setPageLayout` and a pointer-receiver `getPageLayout`, so the value satisfies
`PageLayoutOption` and its address satisfies `PageLayoutOptionPtr`: a caller
passes `PageLayoutOrientation(OrientationLandscape)` to `SetPageLayout` and
`&orientation` to `GetPageLayout`. A conversion is needed in both directions
(`string(orientation)`, `int(paper)`).

This follows the option-pair shape already used for sheet properties and sheet
views: the same value type is a setter when passed by value and a getter when
passed by pointer. The `setPageLayout`/`getPageLayout` methods are unexported,
which means **no code outside this package can add a page-layout option** —
that is intentional and is part of the contract.

The settings land in the worksheet's `pageSetup` element, ECMA-376 Part 1,
18.3.1.63: `@orientation` and `@paperSize`.

* Defaults on a worksheet whose page setup was never touched: orientation
  `"portrait"`, paper size `1` (US Letter). `GetPageLayout` reports these
  without an error even though no `pageSetup` element has been written yet.
* Both option lists may be empty: `SetPageLayout(sheet)` and
  `GetPageLayout(sheet)` with no options are `nil`.
* Setting one option leaves the others as they were.
* The values are per worksheet: setting the orientation of `Sheet2` does not
  change `Sheet1`.
* The values are written to the saved package (`orientation="landscape"`,
  `paperSize="9"`) and are read back after a reopen.
* An unknown worksheet is an error on both methods.

#### 4.03.9 Headers and footers

```go
type FormatHeaderFooter struct {
    AlignWithMargins bool
    DifferentFirst   bool
    DifferentOddEven bool
    ScaleWithDoc     bool
    OddHeader        string
    OddFooter        string
    EvenHeader       string
    EvenFooter       string
    FirstFooter      string
    FirstHeader      string
}

func (f *File) SetHeaderFooter(sheet string, settings *FormatHeaderFooter) error
```

This writes the worksheet's `headerFooter` element, ECMA-376 Part 1,
18.3.1.46: the attributes `@alignWithMargins`, `@differentFirst`,
`@differentOddEven`, `@scaleWithDoc`, and the child elements `oddHeader`,
`oddFooter`, `evenHeader`, `evenFooter`, `firstHeader`, `firstFooter`.

* A settings value with every field populated produces a `headerFooter`
  element carrying all four attributes and all six child elements, with the
  caller's text — including the `&`-prefixed formatting codes — carried
  through, XML-escaped where necessary.
* **`SetHeaderFooter(sheet, nil)` removes the element.** Calling it on a
  worksheet that never had a header or footer is a no-op with a `nil` error.
* **A header or footer string of 255 characters or more is rejected** with a
  non-nil error, and nothing is written — the worksheet still has no
  `headerFooter` element afterwards. 254 characters is accepted. The limit is
  checked on `OddHeader`, `OddFooter`, `EvenHeader`, `EvenFooter` and
  `FirstFooter`.
* An unknown worksheet is an error.

#### 4.03.10 Per-column styles

```go
func (f *File) SetColStyle(sheet, columns string, styleID int) error
```

`columns` is either a single column name (`"B"`) or a range written with a
colon (`"C:F"`). The style is recorded on the worksheet's `col` elements,
ECMA-376 Part 1, 18.3.1.13: `@min`, `@max`, `@style`.

* A range is stored as one `col` entry spanning it: `"C:F"` becomes a single
  element with `min="3"` and `max="6"`, not four elements.
* **A reversed range is normalised**, and setting `"F:C"` after `"C:F"`
  updates the existing entry rather than appending a second one. After
  `"C:F"`, `"B"` and `"F:C"` the worksheet has exactly two `col` elements.
* A cell written into a styled column inherits the column's style: after
  `SetColStyle("Sheet1", "C:F", s)`, `GetCellStyle("Sheet1", "C1")` is `s`. A
  cell outside the styled columns keeps style `0`.
* A malformed column name on either side of the range is an error: `"-"`,
  `"1"`, `""`, `"C:-"`, `"-:F"` and `"C:1"` are all rejected.
* An unknown worksheet is an error.

#### 4.03.11 Further chart families and TIFF media

Twenty-seven further chart type constants are added. `AddChart`'s `type` field
accepts them and `AddChart` returns a non-nil error for any type it does not
recognise, including an omitted type; a rejected chart leaves no chart part in
the package.

```go
// bar3DChart with <shape val="cone"|"pyramid"|"cylinder">
const Col3DCone, Col3DConeClustered, Col3DConeStacked, Col3DConePercentStacked
const Bar3DConeClustered, Bar3DConeStacked, Bar3DConePercentStacked
const Col3DPyramid, Col3DPyramidClustered, Col3DPyramidStacked, Col3DPyramidPercentStacked
const Bar3DPyramidClustered, Bar3DPyramidStacked, Bar3DPyramidPercentStacked
const Col3DCylinder, Col3DCylinderClustered, Col3DCylinderStacked, Col3DCylinderPercentStacked
const Bar3DCylinderClustered, Bar3DCylinderStacked, Bar3DCylinderPercentStacked
// surface and contour
const Surface3D, WireframeSurface3D, Contour, WireframeContour
// bubble
const Bubble, Bubble3D
```

Each constant's value is its own name with a lower-case first letter
(`Col3DCone = "col3DCone"`, `Bar3DConePercentStacked =
"bar3DConePercentStacked"`, `WireframeContour = "wireframeContour"`, and so
on), because the value is what a caller writes into the `type` field of the
JSON format string.

What each family must emit in `xl/charts/chart1.xml` (ECMA-376 Part 1, 21.2):

* **Cone, pyramid and cylinder** are `bar3DChart` (21.2.2.15) with
  `<shape val="cone">`, `<shape val="pyramid">` or `<shape val="cylinder">`.
  The `Col3D…` constants carry `<barDir val="col">` and the `Bar3D…`
  constants `<barDir val="bar">`. `<grouping val="…">` is `standard` for the
  unsuffixed `Col3DCone`, `Col3DPyramid` and `Col3DCylinder`, and otherwise
  `clustered`, `stacked` or `percentStacked` to match the constant's suffix.
* **`Surface3D` and `WireframeSurface3D`** are `surface3DChart` (21.2.2.203);
  **`Contour` and `WireframeContour`** are `surfaceChart` (21.2.2.204). A
  contour must not emit a `surface3DChart` element. The two wireframe variants
  carry `<wireframe val="true">`; the other two do not.
* **`Bubble` and `Bubble3D`** are `bubbleChart` (21.2.2.20). `Bubble3D` marks
  its series with `<bubble3D val="true">`; `Bubble` does not.

The chart part is written with a default namespace, so these elements appear
unprefixed in the saved XML.

**TIFF images.** `AddPictureFromBytes` accepts `.tif` and `.tiff` in addition
to the extensions earlier milestones allowed. Both spellings are stored under
the canonical extension: the media part is `xl/media/image1.tiff` and
`GetPicture` reports the name `image1.tiff`. An empty `format` string is not
rejected as invalid JSON: it is treated the same as `"{}"` and selects the
documented defaults, for `AddPictureFromBytes` as for every other JSON-format
argument in this library. `[Content_Types].xml` gains a
`<Default Extension="tiff" …/>` entry (ECMA-376 Part 2, 7.2.3). The byte
stream is stored verbatim and `GetPicture` returns it unchanged. An extension
outside the supported set — `.bmp`, for instance — is still a non-nil error.

#### 4.03.12 Failure reporting on open

`OpenFile` and `OpenReader` return `(nil, non-nil)` when they cannot open the
package: a nil `*File` together with the error, never a usable-looking
`*File`.

* A stream that is not a ZIP archive must report an error for which
  **`errors.Is(err, archive/zip.ErrFormat)` is true.** This is the one error
  identity this milestone fixes; wrap or return `zip.ErrFormat` so that
  `errors.Is` finds it.
* **An encrypted package must be reported distinctly.** ECMA-376 standard
  encryption wraps the package in an OLE compound file whose DataSpaces
  structure begins with the `Microsoft.Container.DataSpaces` identifier
  ([MS-OFFCRYPTO], 2.1.5 and the `\006DataSpaces` storage; the specification
  is under `specs/ms-offcrypto/`). When that identifier is present in the stream's leading bytes — an OLE compound file begins with its own eight-byte signature, so the identifier follows it at offset 8 — `OpenReader` must return a diagnostic saying the package is encrypted, and `errors.Is(err, zip.ErrFormat)` must be **false** for it. A
  user handed an encrypted workbook must not be told it is corrupt.

#### 4.03.13 Behaviour the migration must preserve

The new signatures change the result lists and nothing else. These observable
behaviours are unchanged, and are restated because the migration touches every
call site that produces them:

* `SetCellValue` with a `bool` stores a boolean cell, and `GetCellValue` reads
  it back as the string `"1"` for true and `"0"` for false — not `"TRUE"` and
  `"FALSE"`. `SetCellBool` behaves the same way. A `nil` value reads back as
  `""`.
* `SetCellStr` truncates its argument to **32767** characters, the
  SpreadsheetML cell-text limit; it does not return an error for a longer
  string.
* `SetCellFloat(sheet, axis, value, prec, bitSize)` formats the value to
  `prec` decimal places: `SetCellFloat(…, 3.14159265, 3, 64)` reads back as
  `"3.142"`.
* `NewStyle` and `NewConditionalStyle` return `(0, non-nil)` for a definition
  that is not well-formed JSON, and a well-formed definition still registers
  afterwards. Style index `0` is the default style, which is also what
  `GetCellStyle` reports for a cell that was never styled.
* `GetCellFormula` of a cell with no formula is `("", nil)`. Setting the empty
  formula removes the formula from the cell.
* `MergeCell` normalises a reversed range: `MergeCell(sheet, "H6", "E4")` is
  the range `E4:H6`. `GetMergeCells` returns `[]MergeCell`, and each element
  answers `GetStartAxis()`, `GetEndAxis()` and `GetCellValue()`.
* `SetSheetRow` requires a **pointer to a slice**; a plain slice is a non-nil
  error. A `nil` element leaves that cell empty.
* `GetRows` returns the used range only: trailing empty rows are not reported,
  and an empty worksheet yields no rows. On an unknown worksheet it yields no
  rows and a `nil` error (see 4.03.1). `Rows` on an unknown worksheet, by
  contrast, returns a nil iterator and a non-nil error.
* The row iterator and `GetRows` agree row for row and cell for cell.
* `SearchSheet(sheet, value)` matches whole cell values; with a third argument
  of `true` the value is a regular expression.
* Worksheet-scoped page setup, headers, footers, panes, protection, tables,
  auto filters, data validation, drawings and comments all continue to be
  written into the parts earlier milestones established, and the saved package
  is still a valid OPC container: part names carry no leading slash, every
  non-default part is declared in `[Content_Types].xml`, and `_rels/.rels`
  relates the package to `xl/workbook.xml` and `docProps/core.xml`.
* `AddShape`'s JSON descriptor takes a `paragraph` array; each entry's `text`
  key is the run of text drawn inside the shape (`"paragraph":[{"text":"Rectangle"}]`
  produces a shape whose body carries the literal text `"Rectangle"`). A
  descriptor with no `paragraph` entries still produces a shape, with a single
  blank paragraph.
* `SetActiveSheet` has no error result. An index below `1` is not rejected and
  does not leave the previous selection alone: it is clamped up to `1`, so
  `SetActiveSheet(-1)` behaves as `SetActiveSheet(1)`, and `GetActiveSheetIndex`
  reports that valid, clamped index afterward.

### 4.04 Pivot caches, sparklines and the macro part

This milestone adds three package parts that no earlier milestone wrote — the
pivot cache definition, the pivot table definition and the VBA project binary —
together with sparkline groups, which are not ECMA markup at all but a
future-feature extension carried inside the worksheet. It also adds worksheet
grouping, fit-to-page counts, zero-value display, struck-through text, and the
one signature change of the milestone: `SetCellFormula` becomes variadic so a
formula can carry its type and its range.

Nothing from 4.01–4.03 is withdrawn except the old three-argument
`SetCellFormula`. Everything else that milestone 03 established still holds and
is still observed.

#### 4.04.1 The signature change: formula options

One method changes shape and one is removed:

```go
// removed at this milestone
func (f *File) SetCellFormula(sheet, axis, formula string) error

// added at this milestone
func (f *File) SetCellFormula(sheet, axis, formula string, opts ...FormulaOpts) error

type FormulaOpts struct {
    Type *string // ST_CellFormulaType: the f element's t attribute
    Ref  *string // the f element's ref attribute
}
```

The change is **source-compatible for every existing call**: the three-argument
form is still legal Go and still means exactly what it meant at milestone 03 —
store the formula text on the cell, write no `t` and no `ref`. Callers that
never pass an option see no behavioural difference at all.

The contract of the new parameter:

* Each field is a **pointer**, and `nil` means "do not write this attribute".
  `FormulaOpts{}` is legal and is a no-op.
* `Type` becomes the `<f>` element's `@t` attribute, verbatim; `Ref` becomes
  its `@ref` attribute, verbatim. Neither is validated against the source
  range, the cell, or the enumeration, and neither is rewritten.
* **With no options, neither attribute is written.** The element is a bare
  `<f>` holding the formula text, exactly as at milestone 03.
* Options **merge across arguments**, and a later argument wins over an earlier
  one for the field it sets. `SetCellFormula(sheet, axis, formula,
  FormulaOpts{}, FormulaOpts{Type: &t}, FormulaOpts{Ref: &r})` produces an `<f>`
  element carrying both `t` and `ref`. A field left `nil` by every argument is
  left alone.
* The empty formula still removes the formula from the cell: `SetCellFormula(
  sheet, axis, "")` drops the `<f>` element and `GetCellFormula` then reports
  `("", nil)`. Options passed alongside an empty formula have nothing to attach
  to.
* Validation is unchanged: an unknown worksheet and a malformed cell reference
  (`"1C"`) are each a non-nil error, whether or not options are supplied.

A second, internal change comes with the milestone and is worth stating so it
is not mistaken for a hole: v2.0.1's three exported relationship maps —
`WorkBookRels *xlsxWorkbookRels`, `WorkSheetRels map[string]*xlsxWorkbookRels`
and `DrawingRels map[string]*xlsxWorkbookRels` — are **replaced by a single
field**, `Relationships map[string]*xlsxRelationships`, keyed by the full
relationship part name (`"xl/_rels/workbook.xml.rels"`,
`"xl/worksheets/_rels/sheet1.xml.rels"`, `"xl/drawings/_rels/drawing1.xml.rels"`,
and now `"xl/pivotTables/_rels/pivotTable1.xml.rels"`). The map's value type is
unexported, so the field carries no usable API: it is an implementation detail
that lets one code path serve every `_rels` part in the package, including the
new ones. **No observable behaviour depends on how the registry is spelled** —
relationships are observed, as they always were, through the `_rels` parts of
the saved package (4.04.5, 4.04.10, and the relationship contract of 4.01).

#### 4.04.2 Formula type and range attributes

ECMA-376 Part 1, 18.3.1.40 `f` (Formula): the formula expression is the
character content of the element; `@t` is its type (ST_CellFormulaType,
18.18.6) and `@ref` is the "range of cells which the formula applies to … only
required for shared formula, array formula or data table" and is "only written
on the master formula". This milestone exposes both attributes and leaves the
choice to the caller.

The four type names of 18.18.6 are available as constants — `STCellFormulaTypeShared`
existed at milestone 02; the other three are usable from this milestone on:

```go
const STCellFormulaTypeArray     = "array"
const STCellFormulaTypeDataTable = "dataTable"
const STCellFormulaTypeNormal    = "normal"
const STCellFormulaTypeShared    = "shared"
```

Each constant is exactly the ST_CellFormulaType token it names, and each is
written through to `@t` unchanged:

* A shared formula: `t := STCellFormulaTypeShared; ref := "C1:C5"` with
  `FormulaOpts{Type: &t, Ref: &ref}` on `C1` writes
  `<f t="shared" ref="C1:C5">=A1+B1</f>`.
* An array formula: `STCellFormulaTypeArray` with `ref := "E1:E1"` writes
  `<f t="array" ref="E1:E1">…</f>`. A single-cell `ref` is accepted as written.
* `STCellFormulaTypeDataTable` writes `t="dataTable"`. The library does not
  treat it specially and does not require the data-table attributes of
  18.3.1.40 to be present.
* The formula text is stored and read back verbatim, leading `=` included:
  `GetCellFormula` of the cell above is `("=A1+B1", nil)`.

#### 4.04.3 New constants: extension URIs, namespaces and relationship types

Sparklines, and the future-feature area in general, are identified by GUID
strings rather than by element names, and the pivot and macro parts are wired
by relationship type. **These are compared literally**, so they are given here
with their exact values, punctuation and letter case included (note the
lower-case `fd2` inside `ExtURISparklineGroups` and the lower-case letters in
`ExtURISlicerCachesListX14`, `ExtURISlicerListX15` and `ExtURITimelineRefs` —
the spelling is not uniformly upper-case).

Extension URIs, the `uri` attribute of an `<ext>` element (18.2.7):

```go
const ExtURIConditionalFormattings  = "{78C0D931-6437-407D-A8EE-F0AAD7539E65}"
const ExtURIDataValidations         = "{CCE6A557-97BC-4B89-ADB6-D9C93CAAB3DF}"
const ExtURIDrawingBlip             = "{28A0092B-C50C-407E-A947-70E740481C1C}"
const ExtURIIgnoredErrors           = "{01252117-D84E-4E92-8308-4BE1C098FCBB}"
const ExtURIMacExcelMX              = "{64002731-A6B0-56B0-2670-7721B7C09600}"
const ExtURIProtectedRanges         = "{FC87AEE6-9EDD-4A0A-B7FB-166176984837}"
const ExtURISlicerCachesListX14     = "{BBE1A952-AA13-448e-AADC-164F8A28A991}"
const ExtURISlicerListX14           = "{A8765BA9-456A-4DAB-B4F3-ACF838C121DE}"
const ExtURISlicerListX15           = "{3A4CF648-6AED-40f4-86FF-DC5316D8AED3}"
const ExtURISparklineGroups         = "{05C60535-1F16-4fd2-B633-F4F36F0B64E0}"
const ExtURITimelineRefs            = "{7E03D99C-DC04-49d9-9315-930204A7B6E9}"
const ExtURIWebExtensions           = "{F7C9EE02-42E1-4005-9D12-6889AFFD525C}"
```

Namespace names:

```go
const NameSpaceMacExcel2008Main          = "http://schemas.microsoft.com/office/mac/excel/2008/main"
const NameSpaceSpreadSheetExcel2006Main  = "http://schemas.microsoft.com/office/excel/2006/main"
const NameSpaceSpreadSheetX14            = "http://schemas.microsoft.com/office/spreadsheetml/2009/9/main"
const NameSpaceSpreadSheetX15            = "http://schemas.microsoft.com/office/spreadsheetml/2010/11/main"
```

Relationship types (the `Type` attribute of a `<Relationship>` element,
ECMA-376 Part 2, 6.5.3):

```go
const SourceRelationshipPivotCache  = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/pivotCacheDefinition"
const SourceRelationshipPivotTable  = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/pivotTable"
const SourceRelationshipVBAProject  = "http://schemas.microsoft.com/office/2006/relationships/vbaProject"
```

`NameSpaceSpreadSheetX14` is the namespace of the sparkline vocabulary,
`NameSpaceSpreadSheetExcel2006Main` the namespace of the `xm:f` and `xm:sqref`
elements inside it, and `NameSpaceSpreadSheetX15` and `NameSpaceMacExcel2008Main`
are declared on the worksheet root alongside them. The two pivot relationship
types are the standard ECMA identifiers under
`http://schemas.openxmlformats.org/officeDocument/2006/relationships`;
`SourceRelationshipVBAProject` is a Microsoft identifier, because ECMA-376
defines no macro part.

#### 4.04.4 Pivot tables

```go
type PivotTableOption struct {
    DataRange       string
    PivotTableRange string
    Rows            []string
    Columns         []string
    Data            []string
    Page            []string
}

func (f *File) AddPivotTable(opt *PivotTableOption) error
```

`DataRange` is the source data, header row included; `PivotTableRange` is where
the pivot table is placed. Both are **sheet-qualified ranges** of the form
`Sheet1!$A$1:$E$31`: a worksheet name, `!`, then two cell references. The `$`
signs are optional and are stripped before the reference is parsed, so
`Sheet1!A1:E31` means the same thing.

`Rows`, `Columns` and `Data` name **source columns by their header text** — the
text of the corresponding cell in the first row of `DataRange` — not by letter
or index. `Page` is accepted; see 4.04.9.

Both ranges are normalised before use: the dollar signs are removed, and a
range given in reverse (`Sheet1!$E$31:$A$1`) is rewritten so the top-left
corner comes first. A reversed `DataRange` of `Sheet1!$E$31:$A$1` yields the
source reference `A1:E31`, and a reversed `PivotTableRange` of
`Sheet1!$U$34:$O$2` yields the location reference `O2:U34`.

`AddPivotTable` rejects, with a non-nil error and **without leaving a single
`xl/pivotTables/…` or `xl/pivotCache/…` part behind**:

* a `nil` option;
* an option with no ranges at all (an empty `DataRange` is an error before
  anything else is looked at);
* a range with no sheet qualifier (`$A$1:$E$31`), or with more than one `!`;
* a range naming a worksheet that does not exist, for either `DataRange` or
  `PivotTableRange`;
* a range that is a single cell (`Sheet1!$A$1:$A$1`), for either range — a
  pivot table needs an area;
* a range with a row number of `0` (`Sheet1!$A$0:$E$31`); row numbers are
  1-based.

The worksheet holding the source data must exist as a worksheet, and the sheet
named by `PivotTableRange` must be in the workbook's sheet map. Neither range
is checked for overlap, and the source area is not checked for having any data
below its header row.

#### 4.04.5 The pivot cache and pivot table parts

One `AddPivotTable` call writes three new parts, ECMA-376 Part 1, 12.3.11
(Pivot Table Part) and 12.3.12 (Pivot Table Cache Definition Part):

| Part | Written by |
| --- | --- |
| `xl/pivotCache/pivotCacheDefinition1.xml` | the cache definition, 18.10.1.67 |
| `xl/pivotTables/pivotTable1.xml` | the table definition, 18.10.1.73 |
| `xl/pivotTables/_rels/pivotTable1.xml.rels` | the table's relationship part |

**The numbering is per workbook and starts at 1**, counting the pivot parts
already in the package: the first call writes `pivotCacheDefinition1.xml` and
`pivotTable1.xml`, the second writes `pivotCacheDefinition2.xml`,
`pivotTable2.xml` and `xl/pivotTables/_rels/pivotTable2.xml.rels`. There is one
cache definition per table — caches are never shared between tables, even when
two tables read the same source range. The cache records part (12.3.13) is
**not** written at this milestone, and the cache definition does not relate to
one.

Both new parts are declared in `[Content_Types].xml` with an `Override`
(Part 2, 7.2.3.2.5), by part name with a leading slash:

* `/xl/pivotTables/pivotTable1.xml` →
  `application/vnd.openxmlformats-officedocument.spreadsheetml.pivotTable+xml`
* `/xl/pivotCache/pivotCacheDefinition1.xml` →
  `application/vnd.openxmlformats-officedocument.spreadsheetml.pivotCacheDefinition+xml`

Three relationships tie the parts together. Each `Target` is relative to the
base of the part that owns the relationship part (Part 2, 6.4), so each must be
resolved against that part's directory before it names a package item:

* **Worksheet → pivot table.** In `xl/worksheets/_rels/sheetN.xml.rels` for the
  sheet named by `PivotTableRange`, a relationship of type
  `SourceRelationshipPivotTable` whose target resolves to
  `xl/pivotTables/pivotTable1.xml` (written as
  `../pivotTables/pivotTable1.xml`). This is 12.3.11's implicit relationship
  from the worksheet. A worksheet carrying two pivot tables has two such
  relationships.
* **Pivot table → cache definition.** In
  `xl/pivotTables/_rels/pivotTable1.xml.rels`, a relationship of type
  `SourceRelationshipPivotCache` whose target resolves to
  `xl/pivotCache/pivotCacheDefinition1.xml` (written as
  `../pivotCache/pivotCacheDefinition1.xml`).
* **Workbook → cache definition.** In `xl/_rels/workbook.xml.rels`, a
  relationship of type `SourceRelationshipPivotCache` whose target resolves to
  `xl/pivotCache/pivotCacheDefinition1.xml` (written as
  `pivotCache/pivotCacheDefinition1.xml`, relative to `xl/`). This is 12.3.12's
  explicit relationship from the workbook part.

The workbook part also gains a `pivotCaches` element (18.2.18) holding one
`pivotCache` (18.2.17) per cache:

* `@cacheId` is an unsigned integer that is **unique within the workbook**, and
  **the `cacheId` attribute of the pivot table definition repeats the same
  value** — that pairing is how a consumer finds a table's cache. (The first
  cache in a workbook is numbered `2` and the next `3`; only the uniqueness and
  the match are contractual.)
* `@r:id` is the relationship id of the workbook → cache-definition
  relationship above, in the relationships namespace
  (`http://schemas.openxmlformats.org/officeDocument/2006/relationships`), and
  it must resolve in `xl/_rels/workbook.xml.rels` to that relationship.

The cache definition (18.10.1.67) carries `saveData` and `refreshOnLoad`
attributes — XML booleans, whose spelling (`"0"`/`"false"`, `"1"`/`"true"`) is
this milestone's own business — and describes the source with a `cacheSource`
(18.10.1.7)
of `type="worksheet"` holding a `worksheetSource` (18.10.1.95) whose `@sheet`
is the worksheet named in `DataRange` and whose `@ref` is the normalised,
dollar-free reference (`A1:E31`). Its `cacheFields` (18.10.1.4) carries a
`@count` and one `cacheField` (18.10.1.3) per source column, in source order,
whose `@name` is the header text of that column; each cache field is written
with `numFmtId="0"` and an empty `sharedItems` (18.10.1.90) of `count="0"`. A
blank header cell yields a cache field with an empty `@name`, not a skipped or
generated one.

The table definition (18.10.1.73) carries `@name` — **unique per table**, spelled
`Pivot Table1`, `Pivot Table2` — `@cacheId`, a
`dataCaption="Values"`, a `location` (18.10.1.49) whose `@ref` is the
normalised pivot range (`G2:M34`) and whose `@firstHeaderRow`, `@firstDataRow`
and `@firstDataCol` are all `1` regardless of how many fields were placed, and
a `pivotTableStyleInfo` (18.10.1.74) named `PivotStyleLight16` with
`showRowHeaders`, `showColHeaders` and `showLastColumn` set.

#### 4.04.6 Placing fields on the row, column and data axes

Field placement is written **twice**, in two independent passes, and the two
passes do not agree in every case. Both are observable, so both are contract.

**Pass one — one `pivotField` per source column.** `pivotFields` (18.10.1.70)
carries `@count` and exactly one `pivotField` (18.10.1.69) per column of
`DataRange`, in source order, matching the cache fields one for one. Each
column is examined once and gets **at most one axis**, testing `Rows` first,
then `Columns`, then `Data`:

* named in `Rows` → `axis="axisRow"` (ST_Axis, 18.18.1), plus an `items`
  (18.10.1.46) of `count="1"` holding a single `item` (18.10.1.45) with
  `t="default"`;
* otherwise named in `Columns` → `axis="axisCol"`, with the same `items` stub;
* otherwise named in `Data` → `dataField="true"` and **no** `axis` attribute;
* otherwise an empty `<pivotField/>`: an unplaced column still gets its
  positional entry, with neither `axis` nor `dataField`.

**Pass two — one entry per option-list entry.** `rowFields` (18.10.1.81),
`colFields` (18.10.1.14) and `dataFields` (18.10.1.23) are each built by
walking `Rows`, `Columns` and `Data` respectively and looking each name up in
the header row. Each list carries `@count` and its entries in the order the
option list gave them:

* `rowFields`/`colFields` hold `field` elements (18.10.1.29) whose `@x` is the
  **zero-based index of the source column**, not a position within the option
  list. For the header `Month, Year, Type, Sales, Region` with
  `Rows: ["Month","Year"]`, `Columns: ["Type"]`, `Data: ["Sales"]`, `rowFields`
  is `count="2"` with `x="0"` and `x="1"`, `colFields` is `count="1"` with
  `x="2"`, and `dataFields` is `count="1"` with a `dataField` (18.10.1.22)
  whose `@fld` is `3`.
* **A name that matches no header is silently skipped** — not an error, and not
  an entry. `Rows: ["Nowhere"]` produces `rowFields` with `count="0"` and no
  entries, while the other lists are still placed normally.
* **A duplicate within one list is not deduplicated.** `Rows: ["Month","Month"]`
  produces `rowFields` with `count="2"` and both entries `x="0"`, while
  `pivotFields` still has exactly one entry per source column (`count="5"`,
  column 0 marked `axisRow` once).
* **A name in both `Rows` and `Columns` is cross-listed.** Pass one gives that
  column `axis="axisRow"` only (`Rows` is tested first), but pass two lists its
  index under **both** `rowFields` and `colFields`, because each list is built
  from its own option list with no knowledge of the other.

`rowItems` (18.10.1.84) is **a fixed stub, not derived from the fields**: it is
always `count="1"` with exactly one `<i>` (18.10.1.44) holding exactly two
empty `<x/>` children (18.10.1.96), whether `Rows` named zero, one or two
fields. `colItems` is **never written at all** — a `<colItems>` element must
not appear in the part. Neither element is computed from the source data; this
milestone writes the pivot structure, not pivot results, and a consumer is
expected to refresh the table on load (`refreshOnLoad`).

#### 4.04.7 Sparkline groups

Sparklines are not in the SpreadsheetML schema. They travel in the worksheet's
future-feature storage area: `extLst` (ECMA-376 Part 1, 18.2.10) holding an
`ext` (18.2.7) whose `@uri` identifies the extension, with vocabulary from
another namespace inside it — the arrangement ECMA-376 Part 3 describes for
application-defined extension elements (Part 3, clause 8). A consumer that does
not recognise the `uri` leaves the content alone.

```go
type SparklineOption struct {
    Location      []string // cell reference per sparkline, e.g. "A2"
    Range         []string // data reference per sparkline, e.g. "Sheet2!A1:J1"
    Max           int
    CustMax       int
    Min           int
    CustMin       int
    Type          string   // "line" (default), "column" or "win_loss"
    Weight        float64
    DateAxis      bool
    Markers       bool
    High          bool
    Low           bool
    First         bool
    Last          bool
    Negative      bool
    Axis          bool
    Hidden        bool
    Reverse       bool
    Style         int      // 0-35
    SeriesColor   string   // "#RRGGBB"
    NegativeColor string
    MarkersColor  string
    FirstColor    string
    LastColor     string
    HightColor    string   // spelled "Hight", not "High"
    LowColor      string
    EmptyCells    string
}

func (f *File) AddSparkline(sheet string, opt *SparklineOption) error
```

One call adds **one sparkline group** holding one sparkline per `Location`
entry. `Location[i]` is where sparkline *i* is drawn and `Range[i]` is the data
it charts; the two slices are positional and must be the same length.

What a call writes into `xl/worksheets/sheetN.xml`:

* an `<extLst>` containing an `<ext>` whose `uri` is `ExtURISparklineGroups`;
* inside it an `<x14:sparklineGroups>` element in the `NameSpaceSpreadSheetX14`
  namespace, carrying `xmlns:xm="` + `NameSpaceSpreadSheetExcel2006Main`;
* inside that one `<x14:sparklineGroup>` per call;
* inside each group an `<x14:sparklines>` holding one `<x14:sparkline>` per
  location, each with an `<xm:f>` child holding `Range[i]` verbatim and an
  `<xm:sqref>` child holding `Location[i]` verbatim, both in the
  `NameSpaceSpreadSheetExcel2006Main` namespace.

The worksheet root element carries the namespace declarations these elements
need (`x14`, `x15`, `xm`, `mc`, `mx` and the rest) when the part is written, so
the extension content is namespace-resolvable in the saved package rather than
only well-formed.

`Range` is **not validated**: it is copied into `xm:f` as given, may name any
worksheet, and is never checked for existing or for matching the shape of the
location.

Group attributes:

* `@type` is the mapped name, not the option string: `"line"` → `line`,
  `"column"` → `column`, `"win_loss"` → **`stacked`**. An empty `Type` means
  `line`. Any other value is a non-nil error and **no extension is written** —
  a rejected call must leave the worksheet with no sparkline `ext` at all.
* `@displayEmptyCellsAs` is **always the literal `gap`**.
* The boolean options become attributes **only when true**, and are omitted
  entirely when false — never written as `false`: `Markers` → `@markers`,
  `High` → `@high`, `Low` → `@low`, `First` → `@first`, `Last` → `@last`,
  `Negative` → `@negative`, `Axis` → **`@displayXAxis`**, `Reverse` →
  **`@rightToLeft`**. They are written in the Go boolean spelling, `"true"`.

Several groups share one extension: a second `AddSparkline` on the same
worksheet **appends its group to the existing `ext`** rather than adding a
second one. After two calls the worksheet holds exactly one `ext` with
`ExtURISparklineGroups` and two `x14:sparklineGroup` children, in call order.

Validation, in the order the checks run — the order matters because the first
failure is the one reported:

1. the worksheet is resolved first, so **an unknown worksheet is reported even
   when `opt` is `nil`**; the call must not panic on that path;
2. a `nil` option is an error, reported as exactly `parameter is required`.
   A test distinguishes the two failures by that string, so check 1 winning
   over check 2 is observable: `AddSparkline("NoSuchSheet", nil)` must report
   the worksheet, not the option;
3. an empty (or absent) `Location` is an error;
4. an empty (or absent) `Range` is an error;
5. `len(Location) != len(Range)` is an error;
6. `Style` outside `0`–`35` inclusive is an error; `-1` and `36` are rejected,
   `0`, `17` and `35` are accepted.

#### 4.04.8 Sparkline colours

A sparkline group carries up to eight colour children —
`x14:colorSeries`, `x14:colorNegative`, `x14:colorAxis`, `x14:colorMarkers`,
`x14:colorFirst`, `x14:colorLast`, `x14:colorHigh`, `x14:colorLow` — and at
this milestone they come from exactly two places:

* **`Style` selects a palette entry** that supplies seven of them
  (`colorSeries`, `colorNegative`, `colorMarkers`, `colorFirst`, `colorLast`,
  `colorHigh`, `colorLow`). The palette is a fixed table indexed by `Style`
  (the table has an unreachable 37th entry; only indices `0`-`35` are
  accepted). Low style numbers use theme colours — style `0` writes
  `colorNegative` as a theme reference with a `theme` attribute and **no `rgb`
  attribute** — while the high styles use explicit RGB. Style `31`, for
  example, writes `colorSeries="FF5F5F5F"`, `colorNegative="FFFFB620"`,
  `colorMarkers="FFD70077"`, `colorFirst="FF5687C2"`, `colorLast="FF359CEB"`,
  `colorHigh="FF56BE79"` and `colorLow="FFFF5055"` as `@rgb` values. Every
  accepted style produces a `colorSeries` with either an `@rgb` or a `@theme`.
* **`colorAxis` is hardcoded**: always `rgb="FF000000"`, for every style, and
  independent of every option.

`SeriesColor` is the only colour an option can set, and it replaces
`colorSeries` alone. The value is an `#RRGGBB` string; it is uppercased, the
`#` is dropped and `FF` is prefixed, so `"#0000FF"` is written as
`rgb="FF0000FF"` and `"#00FF00"` as `rgb="FF00FF00"`. Setting it **discards the
palette's theme/tint for that element** — the written `colorSeries` carries
`rgb` and no `theme` — and leaves the other six palette colours and the fixed
`colorAxis` exactly as they were.

#### 4.04.9 Option fields that are accepted and ignored

Two option structs declare more fields than this milestone writes. **These
fields are accepted and ignored: the written part is byte-for-byte identical
with or without them.** They are part of the contract, not an accident to be
repaired — wiring them up produces a different package and is wrong at this
milestone.

On `SparklineOption`, every one of these is read by nothing:

`DateAxis`, `Max`, `CustMax`, `Min`, `CustMin`, `Weight`, `Hidden`,
`EmptyCells`, `NegativeColor`, `MarkersColor`, `FirstColor`, `LastColor`,
`HightColor`, `LowColor`.

Consequently:

* `@dateAxis`, `@manualMax`, `@manualMin`, `@lineWeight`, `@displayHidden`,
  `@minAxisType` and `@maxAxisType` are **never written** on a sparkline group,
  whatever the options say;
* `@displayEmptyCellsAs` is always `gap`; `EmptyCells: "span"` does not change
  it;
* the six auxiliary colour names (`NegativeColor`, `MarkersColor`, `FirstColor`, `LastColor`, `HightColor`, `LowColor`) never reach
  `colorNegative`, `colorMarkers`, `colorFirst`, `colorLast`, `colorHigh` or
  `colorLow` — **those six come from the `Style` palette only** — and
  `colorAxis` is always `FF000000` (4.04.8).

Two calls that differ only in those fields must produce an identical worksheet
part. `SeriesColor`, `Type`, `Style`, `Location`, `Range`, `Markers`, `High`,
`Low`, `First`, `Last`, `Negative`, `Axis` and `Reverse` are the fields that do
reach the XML.

On `PivotTableOption`, `Page` is the same kind of field: it is accepted,
`AddPivotTable` returns `nil` for an option that sets it, and **the written
`xl/pivotTables/pivotTable1.xml` is identical to the same call without it**. No
`pageFields` (18.10.1.63) or `pageField` (18.10.1.62) element is written, no
field is given `axis="axisPage"`, and a column named only in `Page` stays an
empty `<pivotField/>` like any other unplaced column. A five-column source with
`Page: ["Region"]` still produces `pivotFields` with `count="5"`.
(**Superseded by §4.06.8**, which removes the `Page` field entirely and gives
filter fields their own `Filter []PivotTableField`, which *does* write
`pageFields`/`pageField` and `axis="axisPage"`.)

#### 4.04.10 The macro part

```go
func (f *File) AddVBAProject(bin string) error
```

`bin` is a path **in the local file system**, not a part name: the file's bytes
become the workbook's VBA project part. ECMA-376 does not define a macro part,
so both the media type and the relationship type are Microsoft's (4.04.3).

A successful call:

* stores the file's bytes **verbatim** at the fixed part name
  `xl/vbaProject.bin`, whatever the source file was called. A source named
  `quite-a-different-name.bin` still becomes `xl/vbaProject.bin`, and no part
  is created under the source's own basename;
* changes the `[Content_Types].xml` `Override` for `/xl/workbook.xml` from
  `application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml`
  to `application/vnd.ms-excel.sheet.macroEnabled.main+xml` (Part 2,
  7.2.3.2.5). There is still exactly one override for that part name;
* adds a `Default` for the `bin` extension with the media type
  `application/vnd.ms-office.vbaProject` (Part 2, 7.2.3.2.4: a default matches
  any part whose name ends with a period followed by the extension), so the
  macro part needs no override of its own;
* adds one relationship in `xl/_rels/workbook.xml.rels` of type
  `SourceRelationshipVBAProject` whose `Target` is the literal
  `vbaProject.bin` — relative to `xl/`, and again not derived from the source
  path.

Checks, in the order they run:

1. **existence first.** A path that does not exist is rejected with an error
   for which `os.IsNotExist` reports true. This check runs *before* the
   extension check, so a missing path with the wrong extension is reported as
   missing, never as an unsupported extension.
2. **extension second.** A path whose extension is not `.bin` is rejected with
   the error text `unsupported VBA project extension`.

A rejected call changes nothing: no `xl/vbaProject.bin` part, no `bin` default
media type, no macro-enabled content type on the workbook part, and no macro
relationship.

Two further behaviours are contract:

* **A path that exists but is not a readable file still succeeds.** The
  content-type and relationship wiring happens before the file is read, and the
  read error is discarded, so `AddVBAProject` on a *directory* named
  `vbaProject.bin` returns `nil`, wires the workbook override, the `bin`
  default and the relationship exactly as a real macro would, and stores a
  **zero-length** `xl/vbaProject.bin` part.
* **A second call overwrites the stored bytes** — the part holds whatever the
  latest call read — while the relationship stays singular (deduplicated by
  target and type, not by input path) and the `bin` default stays singular
  (deduplicated by extension). After two calls there is exactly one macro
  relationship and exactly one `bin` default.

The macro part is an ordinary package item from there on: saving to a `.xlsm`
path and reopening with `OpenFile` yields the same bytes at the same part name,
and a sheet code name set through `SetSheetPrOptions(sheet, CodeName(…))`
(4.02) comes back from `GetSheetPrOptions` after the round trip (`@codeName`,
18.3.1.82). The file extension is not otherwise significant — `AddVBAProject`
does not require the workbook to be saved as `.xlsm`.

**A workbook that never calls `AddVBAProject` declares none of this**: no `bin`
default media type, no part whose name ends in `.bin`, no content type
containing `macroEnabled`, and no workbook relationship of the vbaProject type.
The same holds for the pivot parts: no `xl/pivotCache/…` or `xl/pivotTables/…`
part, no content type containing `pivot`, and no pivotCacheDefinition
relationship, unless `AddPivotTable` was called.

#### 4.04.11 Grouping worksheets

```go
func (f *File) GroupSheets(sheets []string) error
func (f *File) UngroupSheets() error
```

Grouping is selection, and selection is `sheetView/@tabSelected` (18.3.1.87):
the flag says whether a sheet is selected, several sheets may be selected at
once, and only one of them is the active sheet.

`GroupSheets` validates before it mutates:

* **the active worksheet must be named in the list.** Grouping
  `["Sheet2","Sheet3"]` while `Sheet1` is active is a non-nil error and
  **selects nothing** — the workbook is left exactly as it was. An empty or
  `nil` list fails the same way, since the active sheet cannot be in it;
* **every name must exist.** `["Sheet1","SheetX"]` is a non-nil error.

On success every sheet view of every listed worksheet gets `tabSelected` set,
and **worksheets outside the list are left alone** — an unlisted sheet that was
not selected stays unselected.

`UngroupSheets` takes no arguments and never fails. It clears `tabSelected` on
every worksheet **except the active one**, which keeps its selection. Calling
it on a workbook that was never grouped is harmless: the active sheet stays
selected, the others stay unselected.

The relevant starting state, from earlier milestones: a new workbook's first
worksheet is active and selected, and a worksheet added with `NewSheet` gets a
sheet view (18.3.1.88) with no `tabSelected` attribute, i.e. unselected. Every
worksheet part written by this library declares at least one sheet view.

#### 4.04.12 Fitting a sheet to a page count

Two option types join the page-layout option pair of 4.03.8:

```go
type FitToWidth  int   // pageSetup/@fitToWidth
type FitToHeight int   // pageSetup/@fitToHeight
```

They behave like every other member of the pair: the value satisfies
`PageLayoutOption` and its address satisfies `PageLayoutOptionPtr`, so
`SetPageLayout("Sheet1", FitToWidth(2), FitToHeight(3))` sets and
`GetPageLayout("Sheet1", &width, &height)` reads, with an `int(...)` conversion
in both directions.

18.3.1.63 `pageSetup`: `@fitToWidth` is the "number of horizontal pages to fit
on" and `@fitToHeight` the number of vertical pages; both are `unsignedInt`.

* **Both default to `1`** on a worksheet whose page setup was never touched.
  `GetPageLayout` reports `1` for each without an error, even though no
  `pageSetup` element has been written yet, and reports `1` for an element
  whose attribute is absent or `0`.
* Set values are written to the worksheet's `pageSetup`:
  `fitToWidth="2" fitToHeight="3"`, and read back as `4` and `5` after
  `SetPageLayout("Sheet1", FitToWidth(4), FitToHeight(5))`.
* **A non-positive count is silently ignored.** `FitToWidth(0)` and
  `FitToHeight(-1)` return `nil` and leave the previously stored values in
  place; they neither clear the attribute nor report an error. After setting
  `2`/`3` and then `0`/`-1`, the sheet still reports `2` and `3`.

These are page-*setup* attributes and are independent of the `FitToPage`
worksheet property of 4.02 (`sheetPr/pageSetUpPr/@fitToPage`, 18.3.1.65), which
is what tells a consumer to honour them; setting the counts does not set that
flag.

#### 4.04.13 Suppressing zero values

```go
type ShowZeros bool   // sheetView/@showZeros
```

A new member of the sheet-view option pair of 4.02.8, used through
`SetSheetViewOptions(sheet, viewIndex, ShowZeros(false))` and
`GetSheetViewOptions(sheet, viewIndex, &zeros)`.

18.3.1.87 `sheetView`: `@showZeros` flags "whether the window should show 0
(zero) in cells containing zero value"; when false, such cells appear blank.

* **The default is `true`**, and a fresh worksheet writes **no `showZeros`
  attribute at all** — the default is the schema's, not an explicit `true`.
  `GetSheetViewOptions` still reports `true`.
* `ShowZeros(false)` writes the attribute explicitly (`showZeros="false"`;
  `"0"` is the equivalent spelling) and reads back as `false`.
* Setting it back to `true` reads back as `true` again.

#### 4.04.14 Struck-through text

The style descriptor's `font` object gains a `strike` key:

```go
style, err := f.NewStyle(`{"font":{"strike":true,"bold":true}}`)
```

18.4.10 `strike` (Strike Through) draws a line through the middle of the text;
its `@val` defaults to true when omitted, which is why the bare element is
what gets written.

* `"strike": true` adds a `<strike/>` child to the `font` record (18.8.22) in
  `xl/styles.xml` that the new cell format points at — that is, the record at
  index `xf/@fontId` (18.8.45) of the `cellXfs` entry (18.8.10) whose index
  `NewStyle` returned.
* The other font properties of the same descriptor are unaffected: a font that
  also asked for `"bold": true` still carries its `<b/>` (18.8.2).
* **`"strike": false`, and a descriptor that omits the key, write no `strike`
  element at all** — not `<strike val="false"/>`.
* The returned style index is an index into `cellXfs`, and after
  `SetCellStyle(sheet, "A1", "A1", style)`, `GetCellStyle(sheet, "A1")` returns
  that same index.

#### 4.04.15 Behaviour this milestone must preserve

New parts, new relationships and a new `_rels` registry (4.04.1) touch the
package plumbing that everything else rests on. The following were established by 4.01–4.03 and are unchanged — the one exception is named as a change in its own bullet; they are listed because this milestone is the one most likely to break them, and because two of them have never been spelled out:

* **Outline levels are bounded — and the bound is new here.** Milestones 01
  to 03 stored whatever level they were handed; from this milestone on
  `SetRowOutlineLevel` and `SetColOutlineLevel` accept levels `1` to `7`, and
  `0` and `8` are each a non-nil error. A row or column that was never grouped
  reads back `0`.
* **A column width of exactly `0` reads back as the default.** `GetColWidth`
  reports a stored width only when it is non-zero, so after
  `SetColWidth(sheet, "E", "E", 0)` the column reports `64`, not `0`. Row
  height `20` and column width `64` remain the defaults, a row past the end of
  the sheet's row data is not visible (`false`, `nil`), a materialised row that
  was never hidden is, and an unset column is unconditionally visible.
* `InsertRow` shifts the row at the insertion point and everything below it
  down one and leaves a blank row behind; `RemoveRow` of that blank row shifts
  them back, restoring the original values.
* **Every relationship part stays well formed.** Ids are unique within their
  own part, every `Target` resolves to a part that exists in the package once
  it is interpreted relative to the owning part's base (Part 2, 6.4), a
  hyperlink to an external address keeps `TargetMode="External"` while internal
  relationships have no `TargetMode`, and every relationship survives a save
  and reopen. The pivot and macro relationships of this milestone are added
  alongside the existing drawing, chart, comment and hyperlink relationships in
  the very same parts, and must not disturb them.
* **The package still declares everything it contains.** Every part that is not
  covered by a `Default` extension mapping has an `Override` in
  `[Content_Types].xml`, part names carry no leading slash inside the zip,
  `_rels/.rels` still relates the package to `xl/workbook.xml` and
  `docProps/core.xml`, `ReadZipReader` still reports the package's items and
  worksheet count, and a written package reopens with its content intact.
* The cell model of 4.01 and the error contract of 4.03.1 are untouched: typed
  setters still write their declared cell type, text is still escaped and comes
  back verbatim, merged ranges still normalise and keep their anchor value, and
  an unknown worksheet or malformed reference is still a non-nil error at every
  entry point that can see one.
* Sheet-view and page-layout behaviour of 4.02.8 and 4.03.8 is unchanged by the
  two new option types: the other options keep their defaults, the view index
  still counts backward from the last view and is still bounded, a zoom scale
  outside its interval is still ignored, and orientation, paper size, headers
  and footers still survive the save.

### 4.05 The streaming writer and the drawing registry

This milestone introduces the streaming write path, two drawing-object deletion
methods, the variadic page-margin API, `DeleteDefinedName`, `UnmergeCell`, a
range syntax extension to `SetColVisible`, and a variadic second-chart parameter
for `AddChart`. The `CharsetTranscoder` method is also added at this checkpoint.

Nothing from 4.01–4.04 is withdrawn. The one signature change of the milestone
is `AddChart`, which gains a variadic `combo` parameter; existing three-argument
calls remain valid.

#### 4.05.1 The streaming writer

A new `StreamWriter` type exposes a low-memory write path for large sheets.

```go
func (f *File) NewStreamWriter(sheet string) (*StreamWriter, error)

func (sw *StreamWriter) SetRow(axis string, values []interface{}) error
func (sw *StreamWriter) AddTable(hcell, vcell, format string) error
func (sw *StreamWriter) Flush() error
```

`NewStreamWriter` returns a `*StreamWriter` bound to the named sheet; the sheet
must already exist (it is created by default on a new file as `"Sheet1"`).
Calling it on an unknown sheet name returns a non-nil error and no writer.

The binding is observable: the writer exposes the workbook it was made from in
its `File` field, the sheet name in `Sheet`, and the worksheet's index — the
value `GetSheetIndex` reports for that name — in `SheetID`.

`SetRow` writes one row at a time. `axis` is the top-left cell of the row in
A1 notation (`"A1"`, `"A2"`, …). Each element of `values` may be a scalar — any signed or unsigned integer type, `float32`/`float64`, `string`, `bool`, `time.Time`, `time.Duration` — coerced as §4.01.3's table and §4.02.4 coerce the same value for `SetCellValue`, or a `Cell` struct that carries an optional style ID alongside a value:

```go
type Cell struct {
    StyleID int
    Value   interface{}
}
```

An element may also be a `*Cell` — the same struct by pointer — and a `[]byte`,
which is stored as its text (`[]byte("bytes")` reads back as `"bytes"`); a
`nil` element leaves its cell empty.

Streamed rows are written in the library's own cell markup, the same shapes the
buffered path writes: a Go string becomes `<c r="A1" t="str"><v>Name</v></c>`,
a number `<c r="B1"><v>42</v></c>` with no `t` attribute, a boolean
`<c r="D1" t="b"><v>1</v></c>`, and a styled cell keeps its `s` attribute
between `r` and `t`. A string with a leading or trailing space carries
`xml:space="preserve"`. A streamed row the writer was given no options for is
exactly `<row r="1">`: the element carries the `r` attribute and nothing else,
with no `spans` attribute and no other decoration.

`Flush` closes the streaming session and replaces the streamed content of the
worksheet with what the writer was given — the cell data is the streamed rows,
not a merge with anything written before — while the worksheet-level settings
listed below survive it. Until `Flush` is called the sheet is not updated.
After `Flush` the `StreamWriter` must not be reused.

**Parity with the buffered path.** The streamed worksheet XML is structurally
identical to the output of the buffered (`SetCellValue`) path for the same data.
Both paths emit the same `<sheetData>` element. Worksheet-level settings made
before streaming begins — column widths, merge regions, sheet views — are
preserved across `Flush` and appear in the final worksheet element exactly as
they do in the buffered path. One cell shape departs from that parity from
§4.07.9 on: the buffered path writes a string as a `t="s"` shared-string cell
there, where the streamed path keeps the `t="str"` shape above.

**`AddTable` on the streaming path.** `AddTable` may be called after all
`SetRow` calls and before `Flush`. It writes `xl/tables/tableN.xml`, a
relationship entry in `xl/worksheets/_rels/sheet1.xml.rels`, and a `<tablePart r:id="…">` reference inside a `<tableParts count="1">` element in the worksheet part — the element carries the `count` attribute the buffered path writes. The `<tableParts>` element is serialised after `</sheetData>`. Column names are
taken verbatim from the cells in the first streamed row of the table area. A
table whose area spans only one row is automatically expanded to two rows
(header plus one blank data row) because OOXML requires at least one data row
below the header. The `format` argument takes the buffered `AddTable`'s three
keys — `table_style`, `show_first_column` and `show_row_stripes` — and three
more: `table_name`, written as both the part's `@name` and its `@displayName`;
`show_last_column`, written as `showLastColumn`; and `show_column_stripes`,
written as `showColumnStripes`. All four of the `tableStyleInfo` booleans are
written on the part whether or not the format set names them, in the Go boolean
spelling — `showFirstColumn="true"` for a flag set true and
`showRowStripes="false"` for one set false or not mentioned at all, never an
omitted attribute.

#### 4.05.2 `AddChart` — variadic combo parameter

`AddChart` gains a variadic final parameter:

```go
// tree-04 (removed)
func (f *File) AddChart(sheet, cell, format string) error

// tree-05
func (f *File) AddChart(sheet, cell, format string, combo ...string) error
```

The three-argument form is still legal and still means "no combo chart". When
one or more `combo` strings are supplied, each is parsed as a chart format JSON
object and its chart element is appended to the same drawing anchor that the
primary chart occupies. The result is a single chart part containing multiple
chart elements — a combo chart. The part still lives at one path
(`xl/charts/chartN.xml`) and is referenced by one relationship.

#### 4.05.3 Drawing deletion

Two new methods remove anchors from a sheet's drawing part:

```go
func (f *File) DeleteChart(sheet, cell string) error
func (f *File) DeletePicture(sheet, cell string) error
```

Both methods search the sheet's drawing part for an anchor whose `from` address
matches `cell`. `DeleteChart` matches anchors that contain a `<c:chart>`
element; `DeletePicture` matches anchors that contain a `<xdr:pic>` element.
The matching anchor element is removed from the drawing XML.

**No-drawing no-op.** If the sheet has no drawing part at all, both methods
return `nil` without error — there is nothing to remove.

**Bad cell reference.** Passing a syntactically invalid cell reference returns a
non-nil error before any drawing lookup is attempted.

#### 4.05.4 Page margin variadic options

```go
func (f *File) SetPageMargins(sheet string, opts ...PageMarginsOptions) error
func (f *File) GetPageMargins(sheet string, opts ...PageMarginsOptionsPtr) error
```

Both methods accept zero or more option arguments, one per margin. The
margins are named types in the option-pair idiom this library uses elsewhere
(4.02.5's `SheetPrOption`/`SheetPrOptionPtr`, 4.03.8's
`PageLayoutOption`/`PageLayoutOptionPtr`): a **defined** type over `float64`
for each margin, carrying a value-receiver setter and a pointer-receiver
getter, so the value satisfies `PageMarginsOptions` and its address satisfies
`PageMarginsOptionsPtr`:

```go
type (
    PageMarginBottom float64
    PageMarginFooter float64
    PageMarginHeader float64
    PageMarginLeft   float64
    PageMarginRight  float64
    PageMarginTop    float64
)

// each of the six carries the pair's two unexported methods: a value-receiver
// setter and a pointer-receiver getter

type PageMarginsOptions interface { /* the setter */ }
type PageMarginsOptionsPtr interface { /* the getter */ }
```

`SetPageMargins("Sheet1", PageMarginBottom(1.5), PageMarginTop(2.0))` sets
those two margins and leaves the others as they were; `GetPageMargins("Sheet1",
&bot, &top)` reads them back, and each value converts to and from `float64`
(`float64(bot)`). The method names of the two interfaces are not part of the
contract; the shape is — an alias for `float64` cannot carry the pair.

When no `<pageMargins>` element is present in the worksheet, `GetPageMargins`
returns the OOXML defaults: `Bottom=0.75`, `Footer=0.3`, `Header=0.3`,
`Left=0.7`, `Right=0.7`, `Top=0.75`. Both methods return a non-nil error for
an unknown sheet name.

#### 4.05.5 `DeleteDefinedName` — scope matching

```go
func (f *File) DeleteDefinedName(definedName *DefinedName) error
```

`DeleteDefinedName` removes the first `xlsxDefinedName` entry whose `Name` and
computed scope match the argument. If no match is found it returns
`errors.New("no defined name on the scope")`.

**Scope matching is asymmetric with `GetDefinedName`.** `GetDefinedName` returns
`Scope: "Workbook"` for workbook-level names (those with a nil `LocalSheetID`
in the XML). `DeleteDefinedName` computes the comparison scope as the empty
string `""` for the same entries (because `GetSheetName` returns `""` when
`LocalSheetID` is `nil`). Callers that delete a workbook-scoped name must pass
`Scope: ""`, not `Scope: "Workbook"`:

```go
// correct
f.DeleteDefinedName(&DefinedName{Name: "Revenue", Scope: ""})

// wrong — will always return "no defined name on the scope"
f.DeleteDefinedName(&DefinedName{Name: "Revenue", Scope: "Workbook"})
```

Sheet-scoped names use the sheet name as the scope string in both methods, so
no asymmetry exists for them.

#### 4.05.6 `UnmergeCell`

```go
func (f *File) UnmergeCell(sheet, hcell, vcell string) error
```

`UnmergeCell` removes every `<mergeCell>` element whose range overlaps the
rectangle from `hcell` to `vcell` (inclusive). Overlap is tested geometrically:
a stored merge region is removed if it shares at least one cell with the
argument range.

If the sheet has no merge regions (`MergeCells == nil`) the method returns `nil`
without modifying anything. An unknown sheet name returns a non-nil error.

#### 4.05.7 `SetColVisible` range syntax

```go
func (f *File) SetColVisible(sheet, columns string, visible bool) error
```

`columns` now accepts a range like `"D:F"` in addition to a single column
letter. The implementation splits the string on `":"` and applies the visibility
flag to every column from the first to the last of the pair inclusive. A single
column name (no colon) is treated as a range whose start and end are identical.

An invalid column name — including a range where either endpoint is not a valid
column — returns a non-nil error.

#### 4.05.8 `CharsetTranscoder`

```go
func (f *File) CharsetTranscoder(fn func(charset string, input io.Reader) (io.Reader, error)) *File
```

Sets `f.CharsetReader` to `fn` and returns `f` for chaining. The stored
function is called whenever the XML decoder encounters a charset declaration
other than UTF-8 or UTF-16.

### 4.06 The formula tokenizer and evaluator

This milestone adds the formula engine (`calc.go`, 2,632 lines), the `Style` struct, rich-text cell runs, chart sheets, conditional-format and data-validation removal, page breaks, pivot-table filter fields and the `PivotTableField` struct, two utility helpers (`ExcelDateToTime` and `Stack`), and a set of new content-type and relationship constants. Oracle checkpoint: `qax-os/excelize` tag `v2.2.0` (`0feb819d`).

#### 4.06.1 Formula evaluation

```go
func (f *File) CalcCellValue(sheet, cell string) (result string, err error)
```

Evaluates the formula stored in `cell` on `sheet` and returns the result as a string. The function library at this milestone registers these 68 function identifiers:

```
ABS  ACOS  ACOSH  ACOT  ACOTH  ARABIC  ASIN  ASINH
ATAN  ATAN2  ATANH  BASE  CEILING  CEILINGMATH  CEILINGPRECISE  COMBIN
COMBINA  COS  COSH  COT  COTH  CSC  CSCH  DECIMAL
DEGREES  EVEN  EXP  FACT  FACTDOUBLE  FLOOR  FLOORMATH  FLOORPRECISE
GCD  INT  ISOCEILING  LCM  LN  LOG  LOG10  MDETERM
MOD  MROUND  MULTINOMIAL  MUNIT  ODD  PI  POWER  PRODUCT
QUOTIENT  RADIANS  RAND  RANDBETWEEN  ROMAN  ROUND  ROUNDDOWN  ROUNDUP
SEC  SECH  SIGN  SIN  SINH  SQRT  SQRTPI  SUM
SUMSQ  TAN  TANH  TRUNC
```

**A name is normalised before it is looked up.** The evaluator deletes the
prefix `_xlfn` and *every* `.` from the function-name token and matches what is
left against the identifiers above. (**Superseded from §4.08.8 on**: from that
milestone the dot is part of the registered name for the five renamed
functions, so `STDEV.S` is its own identifier rather than a spelling of
`STDEVS`.) The list is therefore the set of registered
*identifiers*, not the set of accepted spellings: at this milestone
`CEILING.MATH`, `_xlfn.CEILING.MATH` and `CEILINGMATH` all reach the one
registered `CEILINGMATH`, and likewise `CEILING.PRECISE`/`CEILINGPRECISE`,
`ISO.CEILING`/`ISOCEILING`, `FLOOR.MATH`/`FLOORMATH` and
`FLOOR.PRECISE`/`FLOORPRECISE`. A name that does not reduce to one of the 68
after that stripping is refused rather than evaluated: `=NOSUCHFN(1)` is an
error, not a value.

The match against the surviving identifier is **case-sensitive**: `=SUM(1,2)`
evaluates, `=sum(1,2)` and `=Sum(1,2)` are refused. That is a deliberate
deviation from OpenFormula §5.6 "Functions and Function Parameters", which
requires function names to be matched case-insensitively; an implementation of
this milestone reproduces the Oracle, not the standard.

**What a caller receives.** A formula that evaluates successfully returns its
value as a string and a nil `error`. A formula that fails returns the **empty
string** together with a non-nil `error`; the error value is never handed back
as the result, so a caller must test `err` rather than inspect the string.
Where the failure is one of the seven spreadsheet error conditions, the
`error`'s message *is* that error string — `=1/0` returns `("", err)` with
`err.Error()` equal to `#DIV/0!`, and `=SQRT(-1)` returns `("", err)` with
`err.Error()` equal to `#NUM!`. The seven strings are `#DIV/0!`, `#VALUE!`,
`#REF!`, `#NAME?`, `#NUM!`, `#N/A` and `#NULL!`. Other refusals — an
unregistered name, a formula that cannot be tokenised, an operand that will not
convert to a number — carry an ordinary descriptive message instead.

Two shapes sit either side of that rule:

- `CalcCellValue` on a cell that holds **no formula** is `("", nil)`, not an
  error. A literal number, a literal string and an empty cell all come back as
  the empty string with a nil error, because there is nothing to tokenise. The
  call itself fails only when `sheet` names no sheet, or `cell` is not a valid
  A1 reference.
- At this milestone a function may return a spreadsheet error string as a
  **successful result**:
  `=ARABIC("VV")` is `("#VALUE!", nil)`, because `ARABIC` reports a malformed
  numeral through its return value and not through `err`. A nil `error` is
  therefore not a guarantee that the result string is a number.

**A wrong number of arguments is an ordinary failure, not an error value.**
Sixty-three of the 68 identifiers above are OpenFormula functions and accept
the argument counts their own clause gives them; the per-function `Syntax:`
lines live in §6.16 "Mathematical Functions", §6.17 "Rounding Functions",
§6.19 "Number Representation Conversion Functions" and §6.5 "Matrix
Functions", and instruction §2 points at all four. A call whose argument count
falls outside what the function accepts returns `("", err)` — the empty string
and a non-nil `error` carrying an ordinary descriptive message, never a `#...!`
string as a successful result. `=ABS()`, `=ABS(1,2)`, `=INT()`, `=SIGN()` and
`=QUOTIENT(1)` all fail that way. Optional parameters are genuinely optional
where the clause makes them so: `=CEILING(2.1)` is `"3"`.

**The remaining five identifiers have no OpenFormula clause at all**, so their
accepted counts are fixed here instead of by Part 4. `CEILINGMATH`,
`CEILINGPRECISE`, `FLOORMATH`, `FLOORPRECISE` and `ISOCEILING` are
spreadsheet-application extensions: searching OpenDocument v1.3 part 4 for
them, dotted or undotted, finds nothing. At this milestone each requires **at
least one** argument and needs nothing beyond it; the later parameters are
optional, with `CEILINGMATH` and `FLOORMATH` accepting **at most three** and
`CEILINGPRECISE`, `FLOORPRECISE` and `ISOCEILING` **at most two**. A call
outside those bounds returns `("", err)` like any other arity failure. Given
one argument each rounds to an integer, the `CEILING` names upwards and the
`FLOOR` names downwards, so
`=CEILINGMATH(2.1)`, `=CEILINGPRECISE(2.1)` and `=ISOCEILING(2.1)` are all
`"3"` while `=FLOORMATH(-2.1)` and `=FLOORPRECISE(-2.1)` are both `"-3"`.

Three departures from the OpenFormula §6.16 and §6.17 clauses are called out
below — two of argument count and one of value — and the checkpoint's
behaviour is what is checked. They are not an exhaustive list of this
milestone's divergences from part 4: the case-sensitive name matching above,
and the `DEGREES`, `COTH` and range-clamp entries below, are more of them.
Treat this section as governing wherever it and a clause disagree.

- `=ROUND(2.5)` is **refused**. §6.17.5 writes `ROUND( Number X [ ; Number
  Digits = 0 ] )`, making the digit count optional with a default of 0; this
  implementation requires both arguments, so `=ROUND(2.5,0)` is `"3"` and
  `=ROUND(2.5)` returns `("", err)`.
- `=SUM()` is **accepted and returns `"0"`**. §6.16.61 writes `SUM( {
  NumberSequenceList N }+ )` — one or more parameters — under the constraint
  `N != {}`, while adding that evaluators may evaluate expressions that do not
  meet that constraint. This implementation takes that latitude: a `SUM` with
  no arguments at all is a successful empty sum, not an error.
- **`CEILING` and `FLOOR` round the magnitude when `Significance` is
  negative.** §6.17.1 says that with `Mode` omitted or zero "rounding is
  toward positive infinity; the number is rounded to the smallest multiple of
  significance that is equal-to or greater than N", and §6.17.3 that it
  "rounds toward negative infinity, and the result is the largest multiple of
  Significance that is less than or equal to N". The plain two-argument
  `CEILING` and `FLOOR` here behave instead the way those two clauses' own
  Notes describe the common spreadsheet forms — "as if there was a non-zero
  Mode value". When `N` and `Significance` are both negative, the absolute
  value of `N` is rounded to a multiple of the absolute value of
  `Significance` — away from zero for `CEILING`, toward zero for `FLOOR` —
  and the sign is applied afterwards. So `=CEILING(-2.5,-1)` is `"-3"`, not
  the `"-2"` §6.17.1 gives, and `=FLOOR(-2.5,-1)` is `"-2"`, not the `"-3"`
  §6.17.3 gives. Where `N` and `Significance` are both positive the clause and
  this checkpoint agree, and the clause is what to implement:
  `=CEILING(2.1,1)` and `=CEILING(2.5,1)` are `"3"`, `=FLOOR(2.9,1)` and
  `=FLOOR(3.7,2)` are `"2"`. This is about the plain two-argument names only;
  the `CEILING`/`FLOOR` extension family of the previous paragraph is a
  separate contract.

Key contracts:

- **Only part of the operator table is implemented.** What works at this milestone is the additive and multiplicative arithmetic set (`+`, `-`, `*`, `/`), parenthesised grouping and unary minus, with the usual precedence: `=4+2*3` is `"10"`, `=(1+2)*3` is `"9"`, `=1-2-3` is `"-4"`, `=2*-3` is `"-6"`, `=10/4` is `"2.5"`. The rest of the OpenFormula §5.5 "Operators" table must not be assumed at this checkpoint. Exponentiation, percent and the comparison operators are tokenised but not evaluated here, and silently yield the right-hand operand instead of a result — `=2^3` is `"3"`, `=50%` is `"50"`, `=1<>2` is `"2"` — while string concatenation is refused outright: `="a"&"b"` returns an error. The range operator `:` works inside a function argument; the union and intersection operators do not (`=SUM(A1:A3 A2:A3)` fails with `#VALUE!`).
- Cell references (absolute and relative, sheet-qualified) and range references are resolved at call time.
- **A call in argument position is evaluated before the outer function receives its
  value.** A function argument may itself be a call, and the value the inner call
  produces is what the outer function is given: `=SUM(1,2,SUM(3,4))` is `"10"`.
- **A range is not a value.** The range operator `:` is meaningful only inside a function's argument list, where a function consumes the rectangle it names. A formula whose own result would be a range is refused: `=A1:A3` returns `("", err)` with `err.Error()` equal to `#VALUE!`, and so does `=A1:A3+1`, however the cells inside the range are populated. `=SUM(A1:A3)` is the shape that works.
- **Evaluation is single-cell, and it does not recurse.** A reference operand resolves to the value *stored* in the cell it names, never to the result of that cell's own formula, and `CalcCellValue` writes nothing back into the cell it evaluates. With `A1` = 1, `A2` = 2 and `D1` holding the formula `=A1+A2` but no stored value, `=D1` is `("", nil)` and `=D1+1` is refused; `=D1+1` yields `"4"` only once `D1` itself holds the literal `3`. Evaluating `D1` beforehand does not help, because the computed value is returned to the caller and not written into the cell.
- **At this milestone a range is resolved from `A1`, not from its own top-left corner.** The rectangle the evaluator reads is seeded at `A1` and only ever widened to cover the reference, so `B1:B3` is read as `A1:B3` and `X1:X9` as `A1:X9`. On a sheet holding 1, 2, 3 in `A1:A3` and 10, 20, 30 in `B1:B3`, `=SUM(B1:B3)` is therefore `"66"`, not `"60"`, while every range that already starts at `A1` behaves as written. This is a defect of this checkpoint, reproduced faithfully.
- That milestone-06 clamp is also why a range far from the data is not simply empty. `=SUM(X1:X9)` returns `"0"` on an otherwise empty sheet and `"1"` on a sheet whose only value is `A1` = 1, and it fails with `#VALUE!` as soon as a text cell — say `C1` holding `"text"` — falls inside the rectangle actually read. A single blank reference is still zero: `=SUM(X1)` is `"0"`.
- The tokenizer tolerates an unbalanced parenthesis around a bare sub-expression, and a surplus closing one: `=(1+2` evaluates to `"3"`, and so does `=SUM(1,2))`. It does **not** tolerate an unclosed function call: `=SUM(1,2` is refused, because the call is only evaluated when its closing parenthesis is seen. `=1+` and `=*3` are refused as well. These are quirks of this checkpoint.
- `DEGREES(0)` returns `#DIV/0!` at this checkpoint because the implementation guards on `val == 0` before the multiply; this is a known Oracle quirk and callers must not assume `DEGREES(0)` is 0.
- `COTH` at this checkpoint is implemented as `math.Tanh`, not `1/math.Tanh`; `COTH(x)` therefore returns the same number as `TANH(x)`.
- `SetCellFormula` stores the formula and no cached result, so a formula cell is written as `<c><f>…</f></c>` with no `<v>` until something puts a value there. ECMA-376 Part 1 §18.3.1.4 (`c` element: `@f` the formula, `@v` the cached value).
- `UpdateLinkedValue` is how a host is asked to recalculate on next open. It clears the cached result of every cell that carries **both** a formula and a value — that cell keeps its `<f>` and loses its `<v>` and its `@t`, while a literal cell is left untouched — and it **removes the `<calcPr>` element from `xl/workbook.xml` altogether**. `NewFile` ships `<calcPr calcId="122211"/>` in its workbook template, so the removal is observable: after `UpdateLinkedValue` the written `xl/workbook.xml` contains no `calcPr` element at all. ECMA-376 Part 1 §18.2.2 (`calcPr`).

#### 4.06.2 The Style struct

```go
type Style struct {
    Border        []Border
    Fill          Fill
    Font          *Font
    Alignment     *Alignment
    Protection    *Protection
    NumFmt        int
    DecimalPlaces int
    CustomNumFmt  *string
    Lang          string
    NegRed        bool
}
type Alignment struct {
    Horizontal      string
    Indent          int
    JustifyLastLine bool
    ReadingOrder    uint64
    RelativeIndent  int
    ShrinkToFit     bool
    TextRotation    int
    Vertical        string
    WrapText        bool
}
type Border struct{ Type, Color string; Style int }
type Fill struct{ Type string; Pattern int; Color []string; Shading int }
type Font struct {
    Bold, Italic, Strike bool
    Underline, Family, Color string
    Size    float64
}
type Protection struct{ Hidden, Locked bool }
```

```go
func (f *File) NewStyle(style interface{}) (int, error)
```

**This supersedes the `NewStyle` signature 4.01.5 declared** — `NewStyle(style
string) (int, error)` — which was the shape through milestones 01 to 05 and is
no longer the one a later milestone's build must satisfy; 4.01.5's text stands
only as the historical record of what those checkpoints accepted. From this
milestone on the parameter is `interface{}`.

`NewStyle` accepts either a JSON string (as in earlier milestones) or a pointer to a `Style` struct, and nothing else: an argument of any other type — an `int`, a `float64`, a `bool`, a slice, or `nil` — returns `(0, err)`. When passed a `*Style` value the struct is marshalled internally and follows the same contract as the JSON path. The `Font.Underline` field accepts the string `"single"` for a single underline. `Border.Style` is an integer index into the border-style enumeration defined in ECMA-376 Part 1 §18.18.3.

#### 4.06.3 Rich-text cell runs

```go
type RichTextRun struct {
    Font *Font
    Text string
}
func (f *File) SetCellRichText(sheet, cell string, runs []RichTextRun) error
```

Stores `runs` as an ECMA-376 `si`/`r` (string item / rich-text run) sequence in the shared-string table. ECMA-376 Part 1 §18.4.8 (`si`), §18.4.4 (`r`).

- `GetCellValue` on a rich-text cell returns the concatenation of all runs' `Text` fields in order.
- An empty `runs` slice stores an empty string.
- Returns a non-nil error when `sheet` does not exist or `cell` is not a valid A1 reference.
- Each run's `Font` pointer may be nil, in which case no font element is written for that run and the run inherits the cell's base formatting.

#### 4.06.4 Chart sheets

```go
func (f *File) AddChartSheet(sheet, format string, combo ...string) error
```

Appends a new sheet of kind `chartsheet` to the workbook. ECMA-376 Part 1 §18.3.1.12.

- Returns a non-nil error when `sheet` duplicates an existing sheet name or when `format` is not valid JSON.
- `GetSheetList` includes chart sheets; `GetSheetIndex` and `GetSheetName` work across both sheet kinds.
- The chart sheet's part is written as `xl/chartsheets/sheetN.xml` where N is the next available sequential sheet index across both worksheets and chart sheets. On a workbook that already has one worksheet (`sheet1.xml`), the first chart sheet lands at `xl/chartsheets/sheet2.xml`.
- A `chartsheet` part requires a drawing (§18.3.1.12 `CT_Chartsheet`), so the chart sheet also owns a drawing part — `xl/drawings/drawingN.xml`, N the next available drawing number, declared in `[Content_Types].xml` with the drawing content type, the same one an embedded chart's drawing carries.
- `GetCellValue` and `SetCellValue` on a chart-sheet name return a non-nil error; a chart sheet carries no cell grid.
- The variadic `combo` parameter carries further chart descriptions, each in the same JSON form as `format`. They are plotted into the *same* chart, so a chart sheet created with `combo` entries is still one sheet with one `xl/charts/chartN.xml` part. A `combo` entry that is not valid JSON is rejected exactly as a malformed `format` is.

#### 4.06.5 Sheet positions become zero-based

```go
func (f *File) GetSheetList() (list []string)
func (f *File) GetSheetName(index int) (name string)
func (f *File) GetSheetIndex(name string) int
func (f *File) GetActiveSheetIndex() (index int)
func (f *File) SetActiveSheet(index int)
func (f *File) NewSheet(name string) int
func (f *File) CopySheet(from, to int) error
func (f *File) GetSheetMap() map[int]string
```

**This milestone re-keys every integer that identifies a sheet, and it
supersedes 4.01.6 and 4.02.1 for all of the entry points named here.** 4.02.1
says of `CopySheet`, `SetActiveSheet`, `GetActiveSheetIndex`, `GetSheetName`
and `GetSheetIndex` that "None of the five is renamed, re-keyed, or removed";
that was true at milestone 02 and is no longer true at this one. Two things
change at once, and only the first is visible in the signatures:

1. The integer is now a **position in `GetSheetList()`**, not the `sheetId`
   attribute of the `sheet` element that earlier milestones used.
2. Positions are **zero-based**, so the first sheet in the workbook is `0`.

The two are separable and both matter. Deleting a middle sheet closes the
positions up but leaves a hole in the `sheetId` values, so after adding
`Alpha` and `Beta` to a new workbook and deleting `Alpha`, `GetSheetList()` is
`["Sheet1", "Beta"]` and `GetSheetName(1)` is `"Beta"` — the sheet whose
`sheetId` is 3.

- `GetSheetList` returns every sheet name — worksheet and chart sheet — in
  definition order. It *defines* the numbering: position `i` of the returned
  slice is the index every other entry point in this list means.
- `GetSheetName(i)` is the name at position `i`, and the empty string for any
  `i` the list does not have, negative ones included.
- `GetSheetIndex(name)` is the position of `name`, so `GetSheetIndex("Sheet1")`
  on a workbook from `NewFile` is **`0`**, not `1`. A name the workbook does
  not have returns **`-1`**, not `0`: 4.01.6's "`GetSheetIndex` returns `0` for
  a name the workbook does not have" no longer holds, because `0` is now a
  valid position. At this milestone the comparison is exact, so `"sheet1"`
  does not find `Sheet1` and also returns `-1`.
- `NewSheet(name)` returns the zero-based position of the sheet it added, so
  the first sheet added to a workbook from `NewFile` returns `1`.
- `GetActiveSheetIndex` is the position of the active sheet, so a workbook from
  `NewFile` reports **`0`**; 4.01.6's "A workbook from `NewFile` reports **1**"
  no longer holds. It reads the `activeTab` attribute of `workbookView` and
  maps it back to a position, rather than scanning the worksheet parts for
  `tabSelected` as 4.01.6 describes. ECMA-376 Part 1 §18.2.1 (`bookViews`).
- `SetActiveSheet(index)` selects by position and **clamps a negative index to
  `0`**. An index past the end of the list matches nothing: the call is not an
  error, and `GetActiveSheetIndex` still reports whichever sheet was active
  before it.
- `CopySheet(from, to)` copies by position, so `CopySheet(0, 1)` copies the
  first sheet onto the second and succeeds. It returns an error and changes
  nothing when either index is negative, when the two are equal, or when
  either names no sheet — 4.01.6's "either index is below 1" becomes "below
  `0`".
- `DeleteSheet(name)` and `SetSheetName(old, new)` are keyed by name, not by
  position, and are unchanged.
- **`GetSheetMap` is the exception.** It is keyed by the sheet's `sheetId`
  attribute, exactly as before, and this milestone does not touch it. On a
  workbook this library built and has not deleted from, `GetSheetMap()[1]` and
  `GetSheetList()[0]` are the same sheet, which is the only sense in which the
  two numberings line up.

These signatures replace the ID-based accessors from earlier milestones.

#### 4.06.6 Page breaks

```go
func (f *File) InsertPageBreak(sheet, cell string) (err error)
func (f *File) RemovePageBreak(sheet, cell string) (err error)
```

Inserts or removes a manual page break at the row *above* and the column *left
of* the cell named by `cell`. Both return a non-nil error when `sheet` names no
sheet or `cell` is not a valid A1 reference.

The breaks live in two collections of the worksheet part — `<rowBreaks>`
(ECMA-376 Part 1 §18.3.1.74 "rowBreaks (Horizontal Page Breaks (Row))") and
`<colBreaks>` (§18.3.1.14 "colBreaks (Vertical Page Breaks)") — each holding
`<brk>` children (§18.3.1.3 "brk (Break)").

- One call records up to two breaks. `InsertPageBreak(sheet, "C2")` writes a
  row break with `@id="1"` — the zero-based index of the row the break sits
  above, so row 2 gives 1 — and a column break with `@id="2"`, the zero-based
  index of column C.
- Every break this library writes is manual and spans the sheet: `@man="true"`
  on each `<brk>`, `@max="16383"` (the last column index) on a row break and
  `@max="1048575"` (the last row index) on a column break.
- Each collection carries `@count` and `@manualBreakCount`, both equal to the
  number of `<brk>` children it holds.
- A break in row 1 has nothing above it and a break in column A has nothing to
  its left, so `"A5"` records a row break only, `"D1"` a column break only, and
  `"A1"` records nothing at all.
- Inserting the same break twice adds nothing; the collections hold one entry
  per row and per column, and `@count` stays at 1.
- `RemovePageBreak(sheet, cell)` drops the row break and the column break that
  `cell` implies, leaving the collections empty. Removing a break that was
  never inserted succeeds and changes nothing.

#### 4.06.7 Conditional-format and data-validation removal

```go
func (f *File) UnsetConditionalFormat(sheet, area string) error
func (f *File) DeleteDataValidation(sheet, sqref string) error
```

`UnsetConditionalFormat` removes the conditional-format rule covering `area` on
`sheet`. Removal is by whole element: the `<conditionalFormatting>` entry whose
`@sqref` is `area`, together with the `<cfRule>` it contains, disappears from
the worksheet part, and a sheet that had exactly one rule is left with no
`<conditionalFormatting>` element at all. If no rule exists for `area` the call
succeeds silently and the existing rules are left intact. ECMA-376 Part 1
§18.3.1.18 (`conditionalFormatting`) over §18.3.1.10 (`cfRule`).

`DeleteDataValidation` removes the data-validation entry whose `sqref` equals
the given value; the matching `<dataValidation>` child goes, and with it the
`<dataValidations>` parent once it is empty. If no validation with that `sqref`
exists the call succeeds silently and changes nothing. Returns a non-nil error
when `sheet` does not exist. ECMA-376 Part 1 §18.3.1.33 (`dataValidations`)
over §18.3.1.32 (`dataValidation`).

#### 4.06.8 Pivot-table filter fields and the PivotTableField struct

```go
type PivotTableField struct {
    Data     string
    Name     string
    Subtotal string
}
```

`PivotTableOption.Columns`, `.Rows` and `.Data` are now `[]PivotTableField`
instead of `[]string`, and **`PivotTableOption.Page []string` from milestone 04
is gone**, replaced by `Filter []PivotTableField`. That is a removal, not a
rename with a compatible shim: code carrying the old `Page` field forward does
not compile against this checkpoint.

The `Data` field names the source column; `Name` gives the field a display
label; `Subtotal` accepts subtotal-function strings such as `"Sum"`. In
`xl/pivotTables/pivotTable1.xml` the three placements reach the definition as
follows.

- A field named by `Filter` is written as a `<pageField>` inside `<pageFields>`
  (ECMA-376 Part 1 §18.10.1.63 `pageFields`, §18.10.1.62 `pageField`), carrying
  `@fld`, the zero-based ordinal of that column in the data range.
  `<pageFields>` carries `@count`, the number of `pageField` children, exactly
  as the other field lists carry theirs. Its entry in `<pivotFields>` carries
  `axis="axisPage"` (§18.10.1.69 `pivotField`).
- `Rows` and `Columns` produce `axis="axisRow"` and `axis="axisCol"` entries in
  the same `<pivotFields>` list, with `<rowFields>`/`<colFields>` naming their
  ordinals.
- A field named by `Data` becomes a `<dataField>` inside `<dataFields>`
  (§18.10.1.23, §18.10.1.22). `Name` is written to its `@name` verbatim and
  `Subtotal` to its `@subtotal`, in the token that subtotal function has —
  `Subtotal: "Sum"` is stored as `subtotal="sum"`. Its `<pivotFields>` entry carries `dataField="true"`
  rather than an `axis`.

#### 4.06.9 ExcelDateToTime and the Stack type

```go
func ExcelDateToTime(excelDate float64, use1904Format bool) (time.Time, error)
```

Converts a serial date float to a `time.Time` in UTC. When `use1904Format` is false the epoch is 1900-01-00 (the standard 1900 date system, with the historical leap-year quirk for serial 60). When it is true the epoch is 1904-01-01 (the 1904 date system), so serial 0 is 1904-01-01 and serial 1 is 1904-01-02. Returns a non-nil error when `excelDate` is negative. Serial values 0 and 1 are valid. Serial 44000 is 2020-06-18 with `use1904Format = false` and 2024-06-19 with `use1904Format = true`.

```go
func NewStack() *Stack
func (s *Stack) Push(value interface{})
func (s *Stack) Pop() interface{}
func (s *Stack) Peek() interface{}
func (s *Stack) Len() int
func (s *Stack) Empty() bool
```

A generic LIFO stack used internally by the evaluator and exported at this milestone. `Pop` and `Peek` on an empty stack return `nil`. `Push` accepts any `interface{}` value.

#### 4.06.10 New content-type and relationship constants

```go
const ContentTypeSpreadSheetMLChartsheet = "application/vnd.openxmlformats-officedocument.spreadsheetml.chartsheet+xml"
const ContentTypeSpreadSheetMLComments   = "application/vnd.openxmlformats-officedocument.spreadsheetml.comments+xml"
const ContentTypeSpreadSheetMLPivotCacheDefinition = "application/vnd.openxmlformats-officedocument.spreadsheetml.pivotCacheDefinition+xml"
const ContentTypeSpreadSheetMLPivotTable = "application/vnd.openxmlformats-officedocument.spreadsheetml.pivotTable+xml"
const ContentTypeSpreadSheetMLSharedStrings = "application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml"
const ContentTypeSpreadSheetMLTable      = "application/vnd.openxmlformats-officedocument.spreadsheetml.table+xml"
const ContentTypeSpreadSheetMLWorksheet  = "application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"
const ContentTypeDrawing                 = "application/vnd.openxmlformats-officedocument.drawing+xml"
const ContentTypeDrawingML               = "application/vnd.openxmlformats-officedocument.drawingml.chart+xml"
const ContentTypeMacro                   = "application/vnd.ms-excel.sheet.macroEnabled.main+xml"
const ContentTypeVBA                     = "application/vnd.ms-office.vbaProject"
const ContentTypeVML                     = "application/vnd.openxmlformats-officedocument.vmlDrawing"
const SourceRelationshipChartsheet       = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/chartsheet"
const SourceRelationshipDialogsheet      = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/dialogsheet"
const SourceRelationshipSharedStrings    = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings"
```

These constants are exported for use in OPC content-type and relationship
assertions. The three `SourceRelationship*` values are the relationship-type
URIs that appear as the `Type` attribute of a `Relationship` element: adding a
chart sheet writes a `Relationship` into `xl/_rels/workbook.xml.rels` whose
`Type` is exactly `SourceRelationshipChartsheet`, and whose `Target` is
`chartsheets/sheetN.xml`. ECMA-376 Part 2 §6.5.3 "Relationship markup"; the
media-types (content-types) stream is §7.2.3 "Mapping media types to parts".

#### 4.06.11 Boolean cell value contract (inherited, clarified)

`SetCellBool` and `SetCellValue(bool)` both store the raw literal `"1"` (true) or `"0"` (false) in the cell's `@v` attribute with type `"b"`. `GetCellValue` returns the stored literal, so callers receive `"1"` or `"0"`, not `"TRUE"` or `"FALSE"`. This is the Oracle's behaviour at this checkpoint; callers that need a display form must apply their own mapping.

### 4.07 Decryption, iteration limits and the criteria core

This milestone adds workbook decryption (`crypt.go`), the `Cols` column iterator, the `sheetFormatPr` accessors, twenty new formula functions, and the exported specification-limit constants. Oracle checkpoint: `qax-os/excelize` tag `v2.3.2`.

**What this milestone does not cover.** The release this checkpoint tracks moves
a good deal of surface that nothing here asserts, and no group reads or writes
any of it. None of the following is in scope at this checkpoint, and an
implementation that omits all of it loses nothing:

* concurrent cell writes and the mutex `File` embeds to serialise them — no case
  writes from more than one goroutine, and no safety boundary is stated;
* the migration of the namespace and relationship declarations
  (`NameSpaceSpreadSheet`, `NameSpaceDrawingML`, `SourceRelationship`,
  `SourceRelationshipCompatibility` and their nine companions) from
  `const string` to `var xml.Attr`. The two constants 4.07.8 *does* cover,
  `SourceRelationshipOfficeDocument` and
  `StrictSourceRelationshipOfficeDocument`, are new `const string` at this
  milestone and are unaffected;
* `DataValidation.SetRange` widening its bounds from `int` to `float64`;
* the argument-kind constants `ArgType`, `ArgString`, `ArgMatrix` and
  `ArgUnknown`;
* the eight exported encryption structures — `Encryption`, `KeyData`,
  `KeyEncryptors`, `KeyEncryptor`, `EncryptedKey`, `DataIntegrity`,
  `StandardEncryptionHeader` and `StandardEncryptionVerifier`. `Decrypt` and
  `Encrypt` are checked through their own signatures in 4.07.1; how you model
  what they parse is yours;
* the thirteen new `PivotTableOption` fields and `PivotTableField.DefaultSubtotal`;
* custom date-time number-format codes honoured when reading a cell, defined
  names resolved during evaluation, and chart marker types.

`NOT` does not exist at this checkpoint: the library registers eighty-eight
functions, and `NOT` is not among them. Adding it is not required and is not
checked here.

#### 4.07.1 Options and workbook decryption

```go
type Options struct {
    Password string
}

func Decrypt(raw []byte, opt *Options) (packageBuf []byte, err error)
func Encrypt(raw []byte, opt *Options) (packageBuf []byte, err error)

func OpenFile(filename string, opt ...Options) (*File, error)
func OpenReader(r io.Reader, opt ...Options) (*File, error)
func (f *File) SaveAs(name string, opt ...Options) error
```

`Options` carries a single field, the password used to decrypt a protected workbook. It is accepted variadically by `OpenFile`, `OpenReader` and `SaveAs`; passing none is legal and is the common case, and a non-empty password handed to `SaveAs` is accepted and changes nothing about the package written — only acceptance is checked there.

`Decrypt` takes the bytes of an OLE compound file, reads its `EncryptionInfo` and `EncryptedPackage` streams, and returns the decrypted ZIP package — bytes beginning with the local-header signature `PK\x03\x04`, which `OpenReader` then reads with no password at all. Key derivation follows MS-OFFCRYPTO: the spin iterator and the integer block keys are serialised as 32-bit **little-endian** values before they are hashed. `Decrypt` dispatches on the mechanism named in the `EncryptionInfo` stream. **Only the `standard` mechanism is in scope at this checkpoint**: the frozen fixture the decryption cases read is a standard-encrypted workbook, and no case here presents an `agile`, `extensible` or unknown-mechanism package. The Oracle carries an agile branch too, but nothing in this milestone exercises it, so an implementation that supports the standard mechanism alone loses nothing here. Input that is not a compound file is an error. On any failure `Decrypt` returns **no bytes at all** alongside its error — a nil `packageBuf`, never a partly deciphered package — the same shape 4.07.1 states for `Encrypt`.

`Encrypt` is declared but **not implemented at this checkpoint**. It derives its key material and then returns the error `not support encryption currently` with a nil buffer. This is the required behaviour, not a defect to repair.

`OpenReader` consults the password only when the stream carries the OLE compound-file identifier **and at least one `Options` is passed**; with no options at all an encrypted workbook is handed to the ZIP reader undeciphered. A plain ZIP workbook therefore opens whether or not a password is supplied, and an incorrect password on an unencrypted workbook is simply ignored. When `Decrypt` itself fails — a malformed compound file, an unreadable `EncryptionInfo` — `OpenReader` reports `decrypted file failed`. A merely *wrong* password is not one of those cases: the standard mechanism's verifier hash is not checked at this checkpoint, so the wrong key deciphers the package into rubbish, `Decrypt` returns that rubbish without complaint, and the error the caller sees comes from the ZIP reader afterwards.

This **withdraws the second bullet of 4.03.12** for this milestone. Milestones 01–06 required an encrypted package to be reported distinctly, by an error that named encryption and for which `errors.Is(err, zip.ErrFormat)` was false. That check is gone from this checkpoint: with no `Options` passed, an encrypted compound file is handed to the ZIP reader like any other stream, and the diagnostic the caller gets is the ZIP reader's own. The first bullet of 4.03.12 — the `zip.ErrFormat` identity for a stream that is not an archive — is unchanged and still checked.

`SaveAs` rejects a `name` longer than `MaxFileNameLength` (207) with the error `file name length exceeds maximum limit`, before opening anything, so no file is created. The check applies to the whole path string, not to the base name.

That ceiling is not the only way `SaveAs` refuses a path. A `name` that is within the ceiling but that the file system will not open — a parent directory that does not exist, or a parent that is a regular file rather than a directory — is refused too, and `SaveAs` propagates the file system's own error rather than the length message. The two refusals are distinct and are ordered: the length check runs first and before anything is opened, and anything that survives it can still fail when the file is created.

#### 4.07.2 The column iterator

```go
func (f *File) Cols(sheet string) (*Cols, error)
func (cols *Cols) Next() bool
func (cols *Cols) Error() error
func (cols *Cols) Rows() ([]string, error)

func (f *File) GetCols(sheet string) ([][]string, error)
```

`Cols` walks a worksheet one column at a time, mirroring the existing `Rows` iterator. `Next` reports whether a further column is available; `Rows` returns the current column's cell values top to bottom; `Error` reports any error encountered during traversal. `GetCols` is the eager form, returning every column at once as a slice of string slices. A sheet that exists but holds no cells has no columns: `GetCols` returns an empty slice with a nil error, and `Next` is `false` on the first call rather than yielding one empty column. Both name the sheet case-sensitively and return an error for an absent sheet.

#### 4.07.3 Worksheet formatting properties

```go
func (f *File) SetSheetFormatPr(sheet string, opts ...SheetFormatPrOptions) error
func (f *File) GetSheetFormatPr(sheet string, opts ...SheetFormatPrOptionsPtr) error

type SheetFormatPrOptions interface{ /* unexported method */ }
type SheetFormatPrOptionsPtr interface{ SheetFormatPrOptions; /* unexported method */ }

type BaseColWidth      uint8
type DefaultColWidth   float64
type DefaultRowHeight  float64
type CustomHeight      bool
type ZeroHeight        bool
type ThickTop          bool
type ThickBottom       bool
```

Both interfaces are **sealed**: their method sets are unexported, so only types declared inside the library can satisfy them and a caller cannot supply an option of its own. `SheetFormatPrOptionsPtr` embeds `SheetFormatPrOptions`, so every readable option is also a settable one. The method names are not part of the contract and nothing here checks them; what is checked is that the seven types below are accepted where the interfaces are, by value for `SetSheetFormatPr` and by pointer for `GetSheetFormatPr`. Each option type is a named scalar implementing `SheetFormatPrOptions`; the pointer form implements `SheetFormatPrOptionsPtr` and is what `GetSheetFormatPr` writes through. Several options may be set or read in a single call. These map to the `sheetFormatPr` element attributes of ECMA-376 Part 1 §18.3.1.81.

A worksheet with no `sheetFormatPr` element reports `DefaultRowHeight` as `15` — the one default that is not the zero value. Every other option reads back as its type's zero on such a sheet: `BaseColWidth` `0`, `DefaultColWidth` `0`, and `CustomHeight`, `ZeroHeight`, `ThickTop` and `ThickBottom` all `false`. Both functions return an error for an absent sheet.

#### 4.07.4 Logical and information functions

```
AND  OR  ISBLANK  ISERR  ISERROR  ISEVEN  ISNA  ISNONTEXT  ISNUMBER  ISODD  NA
```

`AND` and `OR` return the literal text `"TRUE"` or `"FALSE"`, not `1`/`0` and not a boolean-typed value. Both require at least one argument and accept at most 30. A quoted `"TRUE"`/`"FALSE"` is recognised by name. A bare `TRUE` or `FALSE` is not a token this tokenizer produces: it reaches the function as **no argument at all** rather than as a false one. So it is silently dropped from a longer list — `=OR(TRUE,1)` is `"TRUE"` and `=AND(1,TRUE)` is `"TRUE"`, each evaluating the one numeric argument that survived — and when it is the only argument the call **fails** the arity check below, so `=AND(TRUE)`, `=AND(TRUE,TRUE)` and `=OR(TRUE)` all come back as the empty string with the error `AND requires at least 1 argument` (respectively `OR requires at least 1 argument`). Write `=AND("TRUE")` to pass the literal.

- `AND` is a true conjunction over its arguments.
- **`OR` assigns rather than accumulates** over numeric arguments, so the last numeric argument alone decides the result: `=OR(1,0)` is `FALSE`, `=OR(0,1)` is `TRUE`, `=OR(1,1,1,0)` is `FALSE`. A quoted `"TRUE"` does stick.
- `ISBLANK` is `TRUE` for an operand with no value — an unwritten cell or one holding the empty string.
- `ISNUMBER` is `TRUE` when the operand parses as a number.
- **`ISNONTEXT` reports `TRUE` only for an operand with no text at all.** Every scalar operand reaches a function as a string, so a numeric cell is reported as text (`FALSE`), the opposite of the spreadsheet function it is named after.
- `ISEVEN` and `ISODD` classify integers and return `#VALUE!` for a non-numeric operand. That `#VALUE!` is a **refusal, not a result**: the call comes back in the ordinary failure shape 4.06.1 states — the empty string together with a non-nil `error` whose message is `#VALUE!` — so a caller must test `err`. This is the opposite of `NA()` below and of `ARABIC`, which hand their error string back as a successful value with a nil `error`.
- `NA()` succeeds and returns the string `#N/A` as an ordinary result, with a nil error. It is not the only error string this evaluator hands back as a value — 4.06.1 records that `ARABIC` reports a malformed numeral the same way, as `("#VALUE!", nil)` — but it is the only one where the error string is the function's *success* result rather than its way of refusing an argument.
- `ISNA` is `TRUE` only for `#N/A`. `ISERR` excludes `#N/A`; `ISERROR` includes it. So `=ISERR(NA())` is `FALSE` and `=ISERROR(NA())` is `TRUE`.
- An error raised mid-expression aborts evaluation rather than becoming a value, so `=ISERR(1/0)` is rejected outright and never returns `TRUE`. (**Superseded in part from §4.08.10 on**: an argument expression that fails is handed to the function as an **error value** whenever another argument follows it, and only the last argument aborts the call — which is why `=IFERROR(1/0,"fallback")` is `"fallback"`; the sentence above holds for a lone argument and for the last one.)

Each IS-family function takes exactly one argument, and passing any other
number is refused with a message naming the function.

Every arity complaint in this section is an exact string and is checked as
written. `AND` and `OR` use the *at least* form, because they accept up to 30:

```
AND requires at least 1 argument
OR requires at least 1 argument
```

The eight IS-family functions use the fixed-count form, on one template with
the function's own name substituted — `<NAME> requires 1 argument`, singular,
with no *at least*:

```
ISBLANK requires 1 argument
ISERR requires 1 argument
ISERROR requires 1 argument
ISEVEN requires 1 argument
ISNA requires 1 argument
ISNONTEXT requires 1 argument
ISNUMBER requires 1 argument
ISODD requires 1 argument
```

All ten are in scope, not merely the two `at least` forms. `NA` is the exception:
it takes no arguments and raises no arity complaint of its own.

#### 4.07.5 Criteria, statistical and date functions

```
SUMIF  MEDIAN  COUNTA  DATE
```

`SUMIF(range, criteria[, sum_range])` sums `sum_range` where `range` satisfies `criteria`, or sums `range` itself when no third argument is given. It requires at least two arguments. Supported criteria forms:

- A bare value or `=value` — equality.
- `>`, `>=`, `<`, `<=` followed by a number.
- A trailing `*` — prefix match (`"ap*"`). A leading `*` — suffix match (`"*rry"`).
- **`<>` is not recognised.** The parser matches the leading `<` and is left with `>value`, which is not a number, so such a criterion selects nothing: `=SUMIF(A1:A4,"<>2")` is `0`.

`MEDIAN` requires at least one argument. Over a **range** it is the true median — the middle value of an odd sample, the mean of the middle pair of an even one. Over **scalar arguments** it re-reads the last argument once per argument, so the sample is that value repeated and the result is always the final argument: `=MEDIAN(1,2,3)` is `3`, `=MEDIAN(3,1,2)` is `2`.

`COUNTA` counts non-empty cells, text as readily as numbers. An empty argument list returns `0` rather than an error.

`DATE(year, month, day)` requires exactly three numeric arguments and returns the constructed instant rendered in full — for example `2020-05-11 00:00:00 +0000 UTC` — not a serial number. An argument that is not numeric — `=DATE(2020,5,"x")` — fails with the section's DATE arity string below, not with an ordinary descriptive error.

The three arity complaints of this section are exact strings and are checked as
written. Two of them are ungrammatical, and the wording is the contract rather
than the grammar: `SUMIF requires at least 2 argument` (singular),
`MEDIAN requires at least 1 argument`, and
`DATE requires 3 number arguments` (`number`, not `numeric`).

#### 4.07.6 Text functions

```
TRIM  CLEAN  LOWER  UPPER  PROPER
```

Each takes exactly one argument.

- **`TRIM` strips leading and trailing whitespace only.** Unlike the OpenFormula function it does not collapse interior runs: `=TRIM("  hello  world  ")` is `"hello  world"`.
- `CLEAN` removes non-printing control characters and leaves printable text, spaces included, untouched.
- `LOWER` and `UPPER` convert case and are Unicode-aware (`=UPPER("ärger")` is `"ÄRGER"`).
- `PROPER` capitalises the first letter of each word and lowers the rest. Any non-letter is a word boundary, so `=PROPER("o'neil mcdonald")` is `"O'Neil Mcdonald"` and `=PROPER("123abc def")` is `"123Abc Def"`.

A numeric operand is seen as the string it is stored as and returned unchanged.

The five arity complaints of this section are exact strings and are checked as
written. They follow the same fixed-count template as the IS-family in 4.07.4 —
`<NAME> requires 1 argument`, singular, with no *at least*:

```
CLEAN requires 1 argument
LOWER requires 1 argument
PROPER requires 1 argument
TRIM requires 1 argument
UPPER requires 1 argument
```

#### 4.07.7 Specification limits

```go
const MaxFontFamilyLength  = 31
const MaxFontSize          = 409
const MaxFileNameLength    = 207
const MaxColumnWidth       = 255
const MaxRowHeight         = 409
const TotalRows            = 1048576
const TotalColumns         = 16384
const TotalSheetHyperlinks = 65529
const TotalCellChars       = 32767
```

`SetColWidth` rejects a width above `MaxColumnWidth`; `SetRowHeight` rejects a height above `MaxRowHeight`; both accept the limit value itself. `SaveAs` enforces `MaxFileNameLength` as described in 4.07.1. `TotalRows` and `TotalColumns` agree with the coordinate helpers: `XFD1048576` is the last addressable cell, and a row beyond `TotalRows` cannot be addressed.

#### 4.07.8 Conformance-class relationship constants

```go
const SourceRelationshipOfficeDocument       = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument"
const StrictSourceRelationshipOfficeDocument = "http://purl.oclc.org/ooxml/officeDocument/relationships/officeDocument"
```

The two forms of the `officeDocument` relationship type. The Transitional form is what a package written by this library declares in `_rels/.rels`; the Strict form is recognised on read and translated to the Transitional namespace. The relationship type is ECMA-376 Part 1 §12.3.23 "Workbook Part", whose "Source Relationship" row carries the Strict form; the conformance classes themselves are Part 1 clause 2 "Conformance" (§2.1, Document Conformance). The shipped edition of Part 1 tabulates the Strict form only, so the Transitional URI above is given verbatim rather than cited.

#### 4.07.9 Inherited behaviour that changes here

```go
func (sw *StreamWriter) Flush() error
```

`Flush` serialises the streamed rows into the worksheet part of the package. It
does **not** populate the in-memory worksheet, so `GetRows`, `GetCols` and the
cell accessors report a streamed sheet as empty until the workbook has been
written and reopened. At milestone 06 those rows were visible immediately.

The second change is in 4.07.1: the distinct diagnostic 4.03.12 required for
an encrypted package is withdrawn here.

The third is in `AddVBAProject`. 4.04.10's first check is unchanged in every
other respect — a path that does not exist is still refused, still before the
extension is looked at, and a refused call still changes nothing — but the
error is no longer the file system's own value. It is rebuilt as text, so
**`os.IsNotExist(err)` is `false` here** where 4.04.10 says it is true. Only
`err != nil` may be relied on for that path; the wording of the message is not
contract and nothing here checks it. The `.bin` extension check is untouched
and still reports the exact text `unsupported VBA project extension`.

The fourth is in `SetCellStr`, and it is the writer's alone: a written string is
no longer stored in the cell. From this milestone the call keeps a shared string
table, writes the text to `xl/sharedStrings.xml` — which the package now carries,
with its content type — and stores a `t="s"` cell whose `v` is the text's
zero-based index there. `SetCellValue` with a `string` or a `[]byte` arrives by
the same path. Milestones 01–06 put the text itself into `v` under `t="str"`;
4.01.3 says so and this milestone supersedes that shape for written cells.
Reading is unchanged: 4.01.4 already says how a `t="s"` cell is read, and a
`t="str"` cell written by an earlier milestone is still read as its `v`. The
streaming path is not affected — `StreamWriter.SetRow` keeps the shapes 4.05.1
states, `t="str"` among them — so 4.05.1's parity claim no longer covers string
cells, which 4.05.1 now records.

The remaining three are in the formula evaluator, and each **supersedes** a
statement 4.06.1 makes. 4.06.1 describes milestone 06 correctly for milestone
06; on these three points it is superseded from this milestone onward, and
where the two sections disagree this one governs.

**A range is resolved from its own top-left corner.** This supersedes 4.06.1's
two range bullets — the `A1` clamp and the far-range consequences it draws from
it. Milestone 06 seeded the rectangle it read at `A1` and only widened it to
cover the reference, so `B1:B3` was read as `A1:B3`. Here the reference is read
as written. On a sheet holding 1, 2, 3 in `A1:A3` and 10, 20, 30 in `B1:B3`,
`=SUM(B1:B3)` is `"60"` where milestone 06 gave `"66"`, `=SUM(B2:B3)` is `"50"`
where it gave `"66"`, and `=SUM(B1:B1)` is `"10"` where it gave `"11"`. A range
away from the data is now genuinely empty rather than a window onto the
top-left of the sheet: `=SUM(X1:X9)` is `"0"` on that sheet, and `"0"` — not
`"1"` — on a sheet whose only value is `A1` = 1. A text cell the reference does
not cover no longer spoils the sum either: with `C1` holding `"text"`,
`=SUM(X1:X9)` is `"0"` rather than the `#VALUE!` milestone 06 produced. A text
cell genuinely inside the reference is still `#VALUE!`, so `=SUM(C1:C1)` and
`=SUM(B1:C1)` both still fail that way.

This is a property of reference resolution, not of `SUM`. It reaches **every**
range argument of every function that takes one, including the ones new at this
milestone, so the examples above are not a special case to be handled inside
`SUM`. On the same sheet `=COUNTA(B1:B3)` is `"3"` — it counts `B1:B3` and
nothing else, where reading from `A1` would have counted six cells — and
`=SUMIF(A1:A3,">1",B1:B3)` is `"50"`, the sum of `B2` and `B3`, where a sum
range seeded at `A1` would have given `"5"`. Each reference argument is resolved
independently and exactly as written; a criteria range and a sum range in the
same call do not influence one another's extent.

**Five comparison operators evaluate.** This supersedes 4.06.1's statement that
the comparison operators are tokenised but not evaluated and silently yield the
right-hand operand. `=`, `<`, `>`, `<=` and `>=` compare **numeric** operands
here and produce the literal text `"TRUE"` or `"FALSE"` — the same convention
4.07.4 states for `AND` and `OR`, not `1`/`0` and not a boolean-typed value:

```
=1=1   "TRUE"      =1=2   "FALSE"
=1<2   "TRUE"      =2<1   "FALSE"
=2>1   "TRUE"      =1>2   "FALSE"
=1<=1  "TRUE"      =1>=2  "FALSE"
```

Cell references work in the same position — with `A1` = 1 and `A2` = 2, `=A1<A2`
is `"TRUE"` and `=A2<A1` is `"FALSE"` — and a comparison composes with
arithmetic, so `=1+2=3` is `"TRUE"`.

**Exponentiation evaluates.** This supersedes 4.06.1's statement that `^` yields
its right-hand operand. `=2^3` is `"8"` where milestone 06 gave `"3"`; `=2^2` is
`"4"`, `=2^-1` is `"0.5"`, `=(1+1)^3` is `"8"` and `=2^3+1` is `"9"`.

**What has not moved.** The rest of 4.06.1's operator restriction stands at this
milestone. The table is **not** complete here, and an implementation must not
assume the operators below work:

- `<>` is still not evaluated and still yields its right-hand operand: `=1<>2`
  is `"2"`, not a boolean. (That it is also unrecognised inside a `SUMIF`
  criteria string is a separate matter, 4.07.5.)
- `%` is still not evaluated: `=50%` is `"50"`.
- String concatenation is still refused outright: `="a"&"b"` is
  `formula not valid`.
- The five comparisons that do evaluate still require numeric operands. Two
  text operands make the whole formula invalid, so `="a"="a"` is
  `formula not valid`.
- Everything else 4.06.1 states about the evaluator is unchanged: evaluation is
  still single-cell and does not recurse, the range operator still works only
  inside a function argument while union and intersection do not, and the
  tokenizer's tolerance quirks are as they were.

Apart from the seven changes in this section, every inherited contract
from milestones 01–06 is unchanged.

### 4.08 The function library and expression arguments

This milestone is almost entirely formula library: the registered function set grows from 88 names to 233 — 150 names added, five renamed away (see 4.08.8). Alongside it, `GetCellRichText` completes the rich-text pair and `SetCellHyperLink` gains a variadic options tail. Oracle checkpoint: `qax-os/excelize` tag `v2.4.0`.

The required signatures outside the function library are few, so they come first; the function families follow, each with the behaviour that is actually checked.

**How a calculated number is rendered, at this checkpoint.** `CalcCellValue` hands back a string, and every numeric expectation in this section is that string, compared exactly — with two declared exceptions: where a result of §4.08.7's annuity, cash-flow and duration functions carries trailing digits, it is compared as a *number*, to a tolerance, because those digits are out of scope; and `KURT`'s value on a sample, whose last digit follows the arithmetic route taken to the fourth moment, is compared as a number in the same way. At milestone 08 the evaluator renders a number with Go's `%g` verb — the shortest decimal form that round-trips — and then applies one correction: if that form is a plain decimal (an optional leading `-`, digits, at most one `.`, no exponent) carrying **more than fifteen digits after the point**, the value is multiplied by `1e15`, rounded to the nearest whole number, divided by `1e15` and rendered with `%g` again. So `=NORMSDIST(1)` is `0.841344746068543` and not the round-trip form `0.8413447460685429`, and `=1/3` is `0.333333333333333`; a shorter form is left alone, so `=PMT(0,60,-10000)` keeps all fourteen decimals of `166.66666666666666`. A result that `%g` renders in exponent form is never corrected, and neither is a text or boolean result. This is the rule for *calculated* values only: the `SetCellValue` read-back of §4.01.3 is the round-trip form with no correction. It is stated here as milestone 08's rendering — where a later section of this document states a rendering rule for its own checkpoint, that later section governs there.

**What this milestone does not cover.** The checkpoint adds surface this milestone deliberately leaves out of scope, named here so it is not mistaken for a hidden requirement. (a) `Cell.Formula` and formulas written through `StreamWriter.SetRow` — the streaming writer is checked exactly as milestone 05 left it. (b) The appearance aliases `BlackAndWhite`, `FirstPageNumber`, `PageLayoutScale` and `TabColor`, and the field `PivotTableOption.ShowError`: they exist at this checkpoint but no required behaviour reaches them. (c) The exported evaluator constants `ArgEmpty`, `ArgError`, `ArgList` and `ArgNumber`. (d) Argument *evaluation order* and error short-circuiting: arguments are expressions (§4.08.11), but no case in scope can observe the order in which they are evaluated, so nothing is required of it. (e) The trailing digits of a result the financial family of §4.08.7 reaches through a bounded numerical search, or through an accumulation, a power or a logarithm over a non-exact operand: `PMT`, `FV`, `NPER`, `IPMT`, `PPMT`, `CUMIPMT`, `CUMPRINC`, `ISPMT`, `NPV`, `IRR`, `MIRR` and `PDURATION`. *No* case pins the trailing digits of such a result, for any of those twelve — neither a case in scope nor one of the unweighted regression gates, at this checkpoint or at a later one that re-checks this function set. Arguments that reduce one of the twelve to an exact or single-operation result are a different matter and *are* pinned as strings — the zero-rate `=PMT(0,60,-10000)` above is one. See §4.08.7 for what is required in their place, and for the values in that section that are pinned exactly.

#### 4.08.1 Reading rich text back

```go
func (f *File) GetCellRichText(sheet, cell string) ([]RichTextRun, error)
```

The read half of the pair milestone 06 opened with `SetCellRichText`. It returns the runs of a rich-text cell in order, each with the `*Font` it was written with, and it round-trips: the runs read from a package that has been written and reopened equal the runs read from the in-memory workbook.

A run written without a font carries a nil `Font`, not a zero-valued one. A cell written as a plain string has no runs at all — `GetCellRichText` returns an empty slice and no error, rather than one run holding the whole string. `GetCellValue` on a rich cell still works and returns the runs concatenated with no markup.

An absent sheet and a malformed cell reference are both errors.

> **Oracle quirk.** On a cell that was never written, `GetCellRichText` returns no runs together with the error `strconv.Atoi: parsing "": invalid syntax` — an internal parse failure surfacing rather than a clean "nothing here" answer. This is out of scope; do not build behaviour on it.

#### 4.08.2 Optional hyperlink attributes

```go
type HyperlinkOpts struct {
    Display *string
    Tooltip *string
}

func (f *File) SetCellHyperLink(sheet, axis, link, linkType string, opts ...HyperlinkOpts) error
```

`SetCellHyperLink` gains a variadic tail carrying the two optional attributes of the `hyperlink` element (ECMA-376 Part 1 §18.3.1.47): `@display`, the text shown in place of the cell value, and `@tooltip`, the text shown on hover.

Both fields are pointers so that either can be set without the other: a nil field writes no attribute at all, rather than an empty one. Several `HyperlinkOpts` values may be passed; they are applied in order and each contributes only the fields it sets, so two single-field values have the same effect as one value holding both.

The options are write-only. `GetCellHyperLink` reports the same `(bool, string)` pair it always did and never surfaces either attribute; they are observable only in the written worksheet part. Options apply to a `Location` link as well as an `External` one.

Everything else about hyperlinks is unchanged: the link type is still restricted to `External` and `Location`, an unknown type is still `invalid link type "..."`, a malformed reference is still rejected, and an unlinked cell still reports `(false, "")` with no error.

#### 4.08.3 The logical and lookup functions

```
IF  IFERROR  NOT  TRUE  FALSE  CHOOSE  VLOOKUP  HLOOKUP  LOOKUP
```

`IF` selects a branch; the false branch is optional, so `=IF(1,"only")` is `"only"`. It requires at least one argument (`IF requires at least 1 argument`). `IFERROR` requires exactly two (`IFERROR requires 2 arguments`) and substitutes the second only when the first evaluates to an error; an argument expression that fails is handed to the function as an error **value** when another argument follows it, which is why `=IFERROR(1/0,"fallback")` is `"fallback"` — see §4.08.10 for the last-argument case that aborts instead.

`TRUE` and `FALSE` are nullary functions returning the text `TRUE` and `FALSE`; either with an argument is an error. Because they are registered names, a bare `TRUE` in an *argument* position now resolves to that call — see §4.08.10.

`CHOOSE(index, v1, ...)` selects by one-based index. An index past the end of the value list is the error `index_num should be <= to the number of values`, not a clamp.

`VLOOKUP` searches the first column of its range, `HLOOKUP` the first row, and each takes an optional range-lookup flag. `LOOKUP(needle, vector)` returns from the searched vector; `LOOKUP(needle, vector, result)` returns the correspondingly-positioned entry of the result vector.

`VLOOKUP` takes at least three arguments; two is `VLOOKUP requires at least 3 arguments`.

> **Deviation from OpenFormula §6.14.** A lookup that finds nothing is reported as a Go error (`VLOOKUP no result found`), not as an `#N/A` *value* handed back as the result. Callers cannot wrap the miss in `IFERROR` at the top level.

> **Deviation from OpenFormula §6.15.** `NOT` inverts only the quoted boolean words: `=NOT("TRUE")` is `FALSE`, but `=NOT(1)` and `=NOT(0)` are both rejected with `NOT expects 1 boolean or numeric argument` despite what that message says. This is the same tokenizer limitation `AND` and `OR` exhibit.

#### 4.08.4 The text function library

```
LEN  LENB  LEFT  LEFTB  RIGHT  RIGHTB  MID  MIDB  FIND  FINDB
REPLACE  REPLACEB  SUBSTITUTE  REPT  CONCAT  CONCATENATE  EXACT
CHAR  CODE  UNICHAR  UNICODE  FIXED  T  N  ISTEXT  ENCODEURL
```

`LEFT` and `RIGHT` take an optional count defaulting to one; a count past the end of the string yields the whole string, and a negative count is an error. `MID` has no optional argument — all three are required — and a start position past the end yields the empty string.

The `B`-suffixed names (`LENB`, `LEFTB`, `RIGHTB`, `MIDB`, `FINDB`, `REPLACEB`) are separately registered and agree with the names they shadow on single-byte text.

`FIND` is case-sensitive and reports a miss as an error. The optional third argument is not a skip: the scan always begins at the first character of the subject and the start position enters only the running count that is reported, so the position it returns is the found character's true one-based position **raised by `ceil((start−1)/2)`** — the true position when `start` is omitted or `1`, and not otherwise. `=FIND("c","abcabc",4)` is `5`, where the character found is the `c` at position 3, and `=FIND("c","abc",4)` is `5` although the subject is three characters long. An empty needle is answered with the start position itself: `=FIND("","abc")` is `1` and `=FIND("","abc",3)` is `3`. `REPLACE` splices by one-based position; position zero is out of range. `SUBSTITUTE` replaces by content, with an optional one-based occurrence selector; occurrence zero is the error `instance_num should be > 0`, not "all occurrences".

> **Deviation from OpenFormula §6.20.9.** The clause returns the position at which the search text is found *when the search is started from character position `Start`*; here the start position skips nothing and inflates the reported position instead (above).

`REPT` requires a non-negative count. `CONCAT` and `CONCATENATE` are both registered and agree on scalar arguments. `EXACT` compares string forms without folding case, so `=EXACT(1,"1")` is `TRUE`.

`CHAR`/`CODE` cover the single-byte range; `UNICHAR`/`UNICODE` the Unicode code points. Their edges differ: `=CHAR(256)` is an error, `=UNICHAR(0)` is an error, and `=CODE("")` is `0` where `=UNICODE("")` is an error. `CODE` and `UNICODE` read the first character of a longer string.

`N` maps an operand to a number: a number is itself, a boolean is `1` for true and `0` for false, and text that is not a number is `0`, so `=N(1)` is `1`, `=N(TRUE())` is `1` and `=N("abc")` is `0`. `ISTEXT` answers whether its operand is text rather than a number, and reads a cell reference as the cell's stored type: `=ISTEXT("a")` is `TRUE`, `=ISTEXT(1)` is `FALSE`.

`ENCODEURL` percent-escapes a string for use in a **query string**, with one departure from that convention: a space is encoded as `%20` and not as `+`. Everything else follows query-string escaping, so a reserved character becomes its `%XX` form and an unreserved one is left alone — `=ENCODEURL("a b&c")` is `"a%20b%26c"`. `ENCODEURL` has no OpenFormula clause; this paragraph is its whole specification.

> **Deviation from OpenFormula §6.20.6 (`CONCATENATE`).** `CONCAT` itself has no OpenFormula clause — the specification registers only `CONCATENATE`, at §6.20.6, and this checkpoint's `CONCAT` is read against it. `CONCAT` does not flatten a range argument; `=CONCAT(A1:A3)` is the error `CONCAT requires arguments to be strings`.

> **Deviation from OpenFormula §6.20.10.** `FIXED`'s decimals argument has no default of 2. Omitted, the value is rendered unrounded: `=FIXED(1234.567)` is `"1,234.567"`. Given, it rounds and may be negative (`=FIXED(1234.567,-1)` is `"1,230"`). The optional third argument suppresses the thousands separators.

> **Deviation from OpenFormula §6.20.22.** `T` of a non-text argument is not the empty string; the argument's string form is handed straight back, so `=T(1)` is `"1"`.

> **Oracle defect — do not exercise.** `MID` with a start position of 0 panics with a slice-bounds error rather than returning a value or an error. No case in scope reaches it.

#### 4.08.5 The statistical function library

```
AVERAGE  AVERAGEA  COUNT  COUNTBLANK  MAX  MAXA  MIN  MINA
LARGE  SMALL  STDEV  STDEVA  STDEV.S  VARP  VAR.P
PERCENTILE  PERCENTILE.INC  QUARTILE  QUARTILE.INC
KURT  SKEW  HARMEAN  PERMUT  PERMUTATIONA
NORMDIST  NORM.DIST  NORMSDIST  NORM.S.DIST
NORMINV  NORM.INV  NORMSINV  NORM.S.INV
POISSON  POISSON.DIST  FISHER  FISHERINV  GAMMA  GAMMALN
```

`AVERAGE` over no arguments is `AVERAGE divide by zero`. The `-A` variants count text as zero rather than skipping it, so `=AVERAGEA(B1:B3)` over three text cells is `0`. `COUNT` counts numbers only; `COUNTBLANK` counts never-written cells in a range.

`MAX` and `MIN` report `0` for an all-text range rather than an error, under both the plain and the `-A` name. `MAX()` with no arguments is `MAX requires at least 1 argument`.

`LARGE` and `SMALL` take a one-based `k` bounded by the array length at both ends: `k should be > 0` and `k should be <= length of array`.

`STDEV`/`STDEVA`/`STDEV.S` are the sample standard deviation, `VARP`/`VAR.P` the population variance. A single observation has no sample deviation and is `#DIV/0!`. `PERCENTILE` takes a fraction and `QUARTILE` an integer in 0..4; each is refused outside its domain (`#N/A` and `#NUM!` respectively).

`NORMINV`/`NORMSINV` at the median are exactly `0`, and that is what is checked: `=NORMINV(0.5,0,1)` and `=NORMSINV(0.5)` both render `0`, not a value a search stopped just short of.

> **The inverse normal is a rational approximation, and it is the one named here.** `NORMINV` — and with it `NORMSINV`, the dotted pair, and every later result built on it, `CONFIDENCE`'s half-width included (§4.09.5) — inverts the cumulative normal by Acklam's rational approximation rather than by an exact inverse. An implementation that inverts the normal *more* accurately renders different digits, and those digits are wrong here: the formulation is the one below, not a better one. With `pLow = 0.02425`, `pHigh = 1 − pLow`, and

> ```
> a = -3.969683028665376e+01  2.209460984245205e+02  -2.759285104469687e+02
>      1.383577518672690e+02  -3.066479806614716e+01  2.506628277459239e+00
> b = -5.447609879822406e+01  1.615858368580409e+02  -1.556989798598866e+02
>      6.680131188771972e+01  -1.328068155288572e+01
> c = -7.784894002430293e-03  -3.223964580411365e-01  -2.400758277161838e+00
>     -2.549732539343734e+00  4.374664141464968e+00  2.938163982698783e+00
> d =  7.784695709041462e-03  3.224671290700398e-01  2.445134137142996e+00
>      3.754408661907416e+00
> ```

> the value of `NORMSINV(p)` is, in the three regions, with the polynomials taken left to right (Horner form, highest coefficient first):
> for `0 < p < pLow`, `q = √(−2·ln p)` and `(((((c₁q+c₂)q+c₃)q+c₄)q+c₅)q+c₆) / ((((d₁q+d₂)q+d₃)q+d₄)q+1)`;
> for `pLow ≤ p ≤ pHigh`, `q = p − 0.5`, `r = q²` and `(((((a₁r+a₂)r+a₃)r+a₄)r+a₅)r+a₆)·q / (((((b₁r+b₂)r+b₃)r+b₄)r+b₅)r+1)`;
> for `pHigh < p < 1`, `q = √(−2·ln(1−p))` and the negative of the lower-region form.
> A probability of `0` or `1` has no finite answer and is `#NUM!`; a probability outside the closed unit interval is `#N/A`.

Numeric results are compared as the strings the evaluator returns, rendered by the rule at the head of §4.08, `KURT` excepted. Representative values on the sample 1..5: `STDEV` is `1.58113883008419`, `VARP` is `2`, `KURT` is `-1.199999999999999`, `SKEW` is `0`. Five more, off that sample: `=GAMMALN(4)` is `1.791759469228055`, the natural logarithm of `Γ(4) = 6`; `=FISHER(0.5)` is `0.549306144334055` and `=FISHERINV(0.5)` is `0.46211715726001`; the cumulative `=POISSON(2,3,TRUE)` is `0.423190081126844`; and `=VARP(1,2,3)`, the population variance of the three-point sample 1, 2, 3 rather than of 1..5, is `0.666666666666667`, which `VAR.P` matches (§4.08.8). All of these are compared as exact strings except `KURT`, which is compared as a number: the value listed above is one correct grouping's rendering of the fourth moment, while a different but equally correct grouping lands on the neighbouring double and renders `-1.2`. For the values carrying more than fifteen digits after the point, the correction at the head of §4.08 puts the result on the `1e-15` grid before it is rendered, and the routes a conforming implementation might take converge there: term summation, Horner and a recurrence all give `POISSON` this string; `atanh` and half the natural logarithm of 3 both give `FISHER` this string; and both population-variance formulations give `VARP` this string.

> **Deviation from OpenFormula §6.18.36.** `HARMEAN` reads scalar arguments only. `=HARMEAN(1,2,4)` is `1.714285714285714`, but handed a range it returns `#N/A`.

#### 4.08.6 The engineering function library

```
BIN2DEC  BIN2HEX  BIN2OCT  DEC2BIN  DEC2HEX  DEC2OCT
HEX2BIN  HEX2DEC  HEX2OCT  OCT2BIN  OCT2DEC  OCT2HEX
BITAND  BITOR  BITXOR  BITLSHIFT  BITRSHIFT
COMPLEX  IMABS  IMREAL  IMAGINARY  IMCONJUGATE  IMARGUMENT
IMSUM  IMSUB  IMPRODUCT  IMDIV  IMPOWER  IMSQRT
IMEXP  IMLN  IMLOG10  IMLOG2
IMSIN  IMCOS  IMTAN  IMSEC  IMCSC  IMCOT
IMSINH  IMCOSH  IMSECH  IMCSCH
BESSELI  BESSELJ
```

The radix conversions take an optional second argument that zero-pads the result to a fixed width. The binary field is **ten bits in two's complement**, so `=BIN2DEC("1111111111")` is `-1` and `DEC2BIN`'s argument is bounded at −512..511 on both sides (`#NUM!` outside). A digit outside the source radix is refused.

The bitwise operators take non-negative integers; a negative operand is `#NUM!`.

`COMPLEX(real, imag)` builds the textual form and drops a zero part from it: `=COMPLEX(3,0)` is `"3"` and `=COMPLEX(0,1)` is `"1i"`. The optional third argument names the imaginary unit and admits only `"i"` and `"j"`.

The `IM*` functions parse that textual form and render their results in it too — a
zero part is left out of a result the same way it is left out of `COMPLEX`'s output,
so `=IMEXP("0")` is `"1"` and not `"1+0i"`; a string that is not a complex literal
is an error. `IMCSC` and `IMCSCH` are `#NUM!` at the origin. The accessors read the part they name — `=IMABS("3+4i")` is `5`, `=IMREAL("3+4i")` is `3`, `=IMAGINARY("3+4i")` is `4` and `=IMCONJUGATE("3+4i")` is `3-4i` — and `IMARGUMENT` reports the principal angle in radians, so `=IMARGUMENT("3+4i")` is `0.927295218001612`: `atan2(4, 3)` put on the `1e-15` grid by the correction at the head of §4.08, which is why reaching it as `atan(4/3)` instead makes no difference to the string.

`BESSELI(x, n)` and `BESSELJ(x, n)` take the value first and the order second: `=BESSELI(1,0)` is `1.266065877752008`, `=BESSELJ(1,0)` is `0.765197686557967`.

> **Deviation from OpenFormula §6.19.15.** `OCT2DEC` does not validate its operand's digits. `=OCT2DEC("8")` is accepted and yields `8`, although 8 is not an octal digit. The other five conversions do validate.

#### 4.08.7 The financial function library

```
PMT  FV  NPER  IPMT  PPMT  CUMIPMT  CUMPRINC  ISPMT
NPV  IRR  MIRR  FVSCHEDULE  PDURATION
DB  DDB  EFFECT  NOMINAL  DOLLARDE  DOLLARFR
```

The annuity functions use the usual sign convention: a negative present value yields a positive payment. `PMT`, `CUMIPMT` and `CUMPRINC` take a payment-timing flag of exactly 0 (end of period) or 1 (beginning); any other value is `#N/A`. A zero rate reduces `PMT` and `NPER` to plain division.

`IPMT` and `PPMT` split one period's payment; the period is one-based and must lie inside the term (`#N/A` otherwise). `DB`'s optional fifth argument is the number of months in the first year; `DDB`'s optional fifth argument is the rate factor, defaulting to 2.

`NPV(rate, value1, [value2], …)` requires a rate and at least one further argument, and discounts the **arguments** it was handed rather than the cells behind them: each argument contributes its own numeric value, and a *range* argument is read as the number `0`, taking its place in the series like any other argument — the same treatment `AVEDEV` gives a range (§4.09.5) — so `=NPV(0.1,A1:A5)` is `0` however many numbers the range holds, and the range still occupies a period of the series. The comma-separated form is the one that discounts: cash flow *k* is divided by `(1+rate)^k` with *k* counted from 1, so `=NPV(0.1,-100,50,50,50)` is about `22.129635953828`. `IRR` takes a range and an optional guess; the guess only seeds the search and does not select between roots, and passing the default (`0.1`) explicitly changes nothing. A series that never changes sign is `#NUM!`. `EFFECT` and `NOMINAL` convert between rate conventions and reject zero compounding periods. `DOLLARDE` and `DOLLARFR` are inverses over a fraction denominator; a zero denominator is `#DIV/0!`. `PDURATION` at a zero rate is `#NUM!`.

> **Deviation from OpenFormula §6.12.30.** The clause requires range and array arguments to be read row-wise from the top left; here a range argument is read as the number `0` (above), so the range's members never enter the series.

> **The trailing digits of this family are out of scope.** `IRR` is found by a bounded search that stops on a tolerance rather than at the exact root, so the digits past about the eighth decimal are the stopping point of whatever search an implementation runs. The rest of the family has the opposite problem and the same consequence, and with `IRR` they make twelve — `PMT`, `FV`, `NPER`, `IPMT`, `PPMT`, `CUMIPMT`, `CUMPRINC`, `ISPMT`, `NPV`, `IRR`, `MIRR`, `PDURATION`. The other eleven do have closed forms, but a closed form fixes the *value*, not the last two or three bits of the `float64` you arrive at — and those bits are every digit the rendering rule at the head of §4.08 prints past the twelfth significant figure. Two correct implementations that group the same closed form differently, or that reach it through `math.Pow` rather than through `math.Exp`∘`math.Log`, disagree there, and neither answer is the more accurate one. **No case of any kind pins the trailing digits of those twelve results**: not a case in scope, and not one of the unweighted regression gates, at this checkpoint or at a later milestone re-checking this function set. Wherever such a result is asserted, it is parsed as a number and compared to a tolerance wide enough to absorb any regrouping of the closed form — a **relative `1e-9`** at this checkpoint; a later milestone that re-renders these results states its own. A result that every conforming route computes as the same `float64` is still compared as a string, and the zero-rate rows of this family are exactly that case: a zero, a whole number, or the single division a zero rate reduces `PMT` and `NPER` to. The reason is not that the decimal is exact — `10000/60` is not — but that IEEE-754 fixes the result of one correctly-rounded division, so every conforming implementation of the reduced form lands on the same double and prints the same string. What governs this section is *exact-or-route-invariant versus not*, never *closed form versus iterative*. The distinction between a case in scope and a gate matters because failing a gate means the step is not satisfied, so "no case in scope pins them" would not have been a strong enough promise.

> **What is required in their place.** Everything the closed form does fix. The sign convention. The zero-rate reductions, which are a plain division or a plain multiplication and *are* compared as exact strings: `=PMT(0,60,-10000)` is `166.66666666666666`, `=FV(0,60,-100)` is `6000`, `=NPER(0,-200,10000)` is `50`. That the two halves of one period add back to that period's level payment, `IPMT + PPMT = PMT`. The arity refusals and the two-valued timing flag. And, for `IRR` specifically, that it takes a range and an optional guess, that passing the default guess (`0.1`) explicitly yields the same string as omitting it, and that a series which never changes sign is `#NUM!`. A solver that brackets the root on a different schedule, and therefore prints different trailing digits, is correct here.

> **The results this section does pin exactly**, so that none of them has to be guessed. The depreciation and schedule functions are exact integers: `=DB(10000,1000,5,1)` is `3690` and `=DB(10000,1000,5,1,6)` is `1845`; `=DDB(10000,1000,5,1)` is `4000`, and naming the default rate factor changes nothing, so `=DDB(10000,1000,5,1,2)` is `4000` too; `FVSCHEDULE` compounding `100` by the rates `30` and `40` in turn is `127100`. The fractional-dollar pair is exact to three decimals: `=DOLLARDE(1.02,16)` is `1.125` and `=DOLLARFR(1.125,16)` is `1.02`. `EFFECT` and `NOMINAL` are the two `math.Pow`-shaped results that survive as strings, because each carries sixteen digits after the point and the head-of-§4.08 correction therefore lands it on the `1e-15` grid, where the last bits drop out: `=EFFECT(0.05,4)` is `0.050945336914062` and `=NOMINAL(0.05,4)` is `0.049088937716157`. Compute those two from the definitions the clause gives — `(1 + rate/n)^n − 1` and `n · ((1 + rate)^(1/n) − 1)` — rather than through a `log1p`/`expm1` rewriting, which lands on the far side of that grid point and renders one digit differently.

> **Not implemented.** `RATE` is **not registered** at this checkpoint. `=RATE(60,-200,10000)` is `not support RATE function`. The annuity family is otherwise complete; do not add a rate solver.

#### 4.08.8 The dotted function names

Five functions milestone 07 registered under a run-together name are **renamed** here to the dotted spelling:

| Milestone 07 | Milestone 08 |
| --- | --- |
| `CEILINGMATH` | `CEILING.MATH` |
| `CEILINGPRECISE` | `CEILING.PRECISE` |
| `FLOORMATH` | `FLOOR.MATH` |
| `FLOORPRECISE` | `FLOOR.PRECISE` |
| `ISOCEILING` | `ISO.CEILING` |

This is a replacement, not an addition: the five old spellings are no longer registered and resolve to `not support <NAME> function`. Signatures carry across intact, including the optional significance and mode arguments. The plain two-argument `CEILING` and `FLOOR` were never part of the rename and are untouched.

> **Deviation from OpenFormula §6.17.1 (`CEILING`) and §6.17.3 (`FLOOR`).** Those two clause numbers are the *analogous* clauses, not clauses for these function names: OpenFormula registers no `CEILING.MATH`, `CEILING.PRECISE`, `FLOOR.MATH`, `FLOOR.PRECISE` or `ISO.CEILING` in any spelling, so there is nothing to cite for them directly and the dotted forms are read against the undotted clause. A reader sweeping for "which clause governs `CEILING.MATH`" should stop here rather than conclude one exists. The mode argument is read as a **sign**, not as the on/off flag the clause describes. §6.17.1 says that "if Mode is given and not equal to zero, the absolute value of N is rounded away from zero to a multiple of the absolute value of Significance and then the sign applied", which would make `=CEILING.MATH(-1.2,1,1)` equal `-2`. At this checkpoint only a **negative** mode rounds a negative operand away from zero; a zero or positive mode rounds it toward zero. So `=CEILING.MATH(-1.2,1,1)` is `-1` and `=CEILING.MATH(-1.2,1,0)` is `-1`, while `=CEILING.MATH(-1.2,1,-1)` is `-2`. `FLOOR.MATH` mirrors it: `=FLOOR.MATH(-1.2,1,1)` is `-2` and `=FLOOR.MATH(-1.2,1,-1)` is `-1`. `CEILING.PRECISE` and `FLOOR.PRECISE` take no mode at all and ignore the sign of the significance, so `=CEILING.PRECISE(-1.2,1)` is `-1`. This deviation is in scope, and it is checked in an inherited-regression group, so getting it wrong means the milestone is not satisfied rather than one case failing.

The statistical renames of §4.08.5 work the other way: `VAR.P` and `VARP`, `NORM.DIST` and `NORMDIST`, `NORM.S.INV` and `NORMSINV` are all registered and agree. `STDEV.S` is a new name only — there is no `STDEVS`.

The dot is part of the registered name, so the tokenizer must carry it through the function-name token; `NORM.S.DIST` carries two. That name also differs in arity from its undotted counterpart: `NORMSDIST` takes the value alone, `NORM.S.DIST` requires the cumulative flag as well (`NORM.S.DIST requires 2 numeric arguments`). A dotted name carries the dot into its own diagnostic too: `PERCENTILE.INC requires 2 arguments`.

#### 4.08.9 Reference and information functions

```
ROW  COLUMN  ROWS  COLUMNS  SHEET  NOW  TODAY  DATEDIF
```

`ROW` and `COLUMN` with no argument report the address of the cell the formula lives in, so the answer depends on where the formula was written. Given a range, each reports the top-left corner. A number is not a reference (`invalid reference`).

`ROWS` and `COLUMNS` measure a range and take exactly one argument — not a list, and the complaint says `argument` in the singular: `ROWS requires 1 argument`, `COLUMNS requires 1 argument`. A single cell is a one-by-one range.

`SHEET` reports the one-based index of the containing sheet in workbook order.

`NOW` and `TODAY` return **serial numbers**, not formatted dates: `TODAY` is a whole serial day and `NOW` carries the time of day as its fraction. Neither takes an argument, and each says so in its own words: `NOW accepts no arguments`, `TODAY accepts no arguments` — the `accepts no` phrasing `SHEET` uses below, not the `requires` phrasing of the functions that want a fixed count.

`DATEDIF(start, end, unit)` counts whole units between two serial dates. Units are `Y`, `M`, `D`, `MD`, `YM`, `YD`, matched without regard to case; anything else is `DATEDIF has invalid unit`. The interval runs forwards only — a start after the end is `start_date > end_date`.

The three compound units need their reference year stated, because the two natural readings of "ignoring years" differ by a day whenever a leap February falls between the dates. **`YD` is measured inside the *start* date's own year**: the end date's month and day are transplanted into the start date's year — advanced by one year only when that month-and-day falls before the start's — and the days from the start date to that transplanted date are counted. So `=DATEDIF(43831,44270,"YD")`, from 2020-01-01 to 2021-03-15, counts 2020-01-01 to 2020-03-15 and is **`74`**: 2020 is a leap year, so February contributes 29 days. It is not `73`, which is what counting forward from the 2021 anniversary gives. `MD` is the other way round — the day difference is taken in the *end* date's month, so `=DATEDIF(43831,44270,"MD")` is `14` — and `YM` is the month difference alone, backed off by one when the end's day-of-month precedes the start's, so `=DATEDIF(43831,44270,"YM")` is `2`. Neither of those two turns on the leap day.

> **Deviation from OpenFormula §6.13.31.** `SHEET` does not accept a sheet name to look up. Any argument at all is `SHEET accepts no arguments`.

> **Note.** `DATEDIF` takes serial numbers. It cannot consume the output of `DATE`, which renders a formatted instant (see §4.08.10) rather than a serial.

#### 4.08.10 Inherited behaviour that changes here

Four contracts from milestone 07 move at this checkpoint: they are the first four bold entries below. A fifth bold entry follows them — `CoordinatesToCellName`, which *widens* without moving, answering every call the inherited contract describes exactly as it did before — so read the count as four moves and one compatible widening. Everything else inherited from milestones 01–07 is unchanged — and "inherited from milestones 01–07" means the contract **as milestone 07 leaves it**, not as the earliest section to mention a behaviour described it. Where §4.07.9 supersedes a statement §4.06.1 makes, §4.07.9 is what this milestone inherits and nothing here restores the superseded reading. The evaluator is the case that matters: read §4.06.1's range and operator bullets through §4.07.9 before assuming anything about how a range resolves or which operators evaluate.

**Bare boolean words now reach the connectives.** At milestone 07 a bare `TRUE` was not a token the tokenizer produced, so `=AND(TRUE,TRUE)` was rejected. `TRUE` and `FALSE` are registered functions here, so an unparenthesised occurrence in an argument position resolves to that call: `=AND(TRUE,TRUE)` is `TRUE`, `=IF(FALSE,"y","n")` is `"n"`. This applies in argument position only — `=TRUE` as a whole formula is still `formula not valid`.

**`NA()` is now reported as an error.** At milestone 07 the call succeeded and returned the text `#N/A` as an ordinary result. Here it is returned as an error. Inside an expression it is still the `#N/A` error value, so `=ISNA(NA())` is still `TRUE`, `=ISERROR(NA())` is `TRUE`, `=ISERR(NA())` is `FALSE` and `=IFERROR(NA(),"fallback")` is `"fallback"`.

**`MEDIAN` now averages the whole argument list.** At milestone 07 a scalar argument list was read as the last argument repeated, so `=MEDIAN(1,2,3)` was `3`. Here the whole list is the sample: `=MEDIAN(1,2,3)` is `2` and `=MEDIAN(1,2,3,4)` is `2.5`. Ranges were never affected and still give the true median.

**A failed argument expression reaches the function as an error value.** At milestone 07 any error raised while a function's argument was evaluated aborted the whole call. Here the failure is packaged as an error **value** and handed to the function whenever the expression is closed by another argument — that is, for every argument but the last — and only the last argument is still evaluated by the path that aborts the call. `=IFERROR(1/0,"fallback")` is `"fallback"` and `=SUM(1/0,1)` is `"1"`, both of which milestone 07 refused, while `=SUM(1,1/0)`, whose failing expression is the last, is still refused; `=ISERR(1/0)`, one argument and therefore also the last, remains `#DIV/0!` as §4.07.4 says.

**`CoordinatesToCellName` widens.** The helper declared in §4.03 gains a variadic tail at this checkpoint: `func CoordinatesToCellName(col, row int, abs ...bool) (string, error)`. Called with two arguments it answers exactly as before (`(1, 1)` is `"A1"`), so the inherited contract is unchanged; a trailing `true` asks for the absolute form, `"$A$1"`. No case in scope passes the third argument.

The milestone-07 quirks that do **not** move are still checked as they were: `OR` still assigns rather than accumulates over numeric arguments (`=OR(1,0)` is `FALSE`); `TRIM` still does not collapse interior whitespace; `ISNONTEXT` still answers "is this operand empty"; `SUMIF` still does not recognise `<>`; `DATE` still renders the constructed instant (`=DATE(2020,5,11)` is `2020-05-11 00:00:00 +0000 UTC`) rather than a serial number; and `StreamWriter.Flush` still writes rows into the package without populating the in-memory worksheet.

#### 4.08.11 Expressions in argument position

An argument is an expression, not only a literal. A cell reference or a range (`=AVERAGE(A1:A5)`, `=IRR(A1:A5)`), an arithmetic expression (`=PMT(0.05/12,60,-10000)`) and a nested call are all accepted, and a nested call is evaluated before the outer function receives its value: `=UPPER(LEFT("abc",2))` is `"AB"`, `=LEN(CONCAT("ab","cd"))` is `4`, `=SUM(LEN("abc"),LEN("de"))` is `5`.

The comparison operators gain **`<>`**, not-equal, over numeric operands: `=1<>2` is `TRUE` and `=2<>2` is `FALSE`, and it composes like the five comparisons §4.07.9 already evaluates, so `=IF(1<>2,"y","n")` is `"y"`. This is a change rather than an addition to an untouched grammar: `<>` is the one comparison §4.07.9 lists as still unevaluated at milestone 07, where it yielded its right-hand operand and `=1<>2` was `"2"`. It joins the other five here. Two text operands are not comparable with it: `="a"<>"b"` is `formula not valid`. `<>` inside a *criteria string* is a separate matter and is still not recognised there, as §4.08.10 says of `SUMIF`.

> **Oracle defect — do not exercise.** `IF` does not always accept a nested call in its condition position. `=IF(ISNA(NA()),"y","n")` is `"y"`, but `=IF(TRUE(),"y","n")` and `=IF(ISTEXT("a"),"y","n")` are the empty string with no error rather than a branch, where the bare words work (`=IF(FALSE,"y","n")` is `"n"`, §4.08.10) and a numeric condition works (`=IF(1,"only")` is `"only"`, §4.08.3). No case in scope reaches the broken form.

### 4.09 Criteria, lookup, shared formulas and the decompression budget

The registered function set grows from 233 names to 329 — 96 added, **none renamed and none removed**, a deliberate contrast with milestone 08. The additions complete the criteria family, the lookup and reference family, the date and time family, and the statistical, engineering and financial families; the comparison and concatenation operators learn to handle text operands at the same time.

Outside the library, six things arrive. Shared formulas become functional: the `FormulaOpts` tail milestone 04 added to `SetCellFormula` now populates the whole declared range, and reading any cell of it returns the formula translated to that cell's position. The library's diagnostics become exported sentinel values a caller can branch on with `errors.Is`, phrased in terms of exported limit constants. The `Options` struct grows three fields — a raw-value flag and the two decompression budgets — and every reading entry point accepts it. And a scattering of accessors fills gaps: `GetCellType`, `GetAppProps`/`SetAppProps`, `SetRowStyle`, `RowOpts` on the streaming writer, the iterator position accessors, `File.Close`, and `ReadZipReader` as a method on `*File`. `File`'s `Drawings`, `Sheet`, `Relationships` and `Pkg` fields become `sync.Map`s, `Pkg` being a rename of milestone 08's `XLSX` parts map (§4.09.14). And `PivotTableField` gains the `Compact` and `Outline` layout flags (§4.09.16).

Oracle checkpoint: `qax-os/excelize` tag `v2.5.0`.

**How an error outcome is returned.** Throughout §4.09, saying that a formula "is `#N/A`" or "is `#NUM!`" names the *kind* of failure, not the return value. `CalcCellValue` reports a failed evaluation as an **empty result string together with a non-nil `error`** — never as the `#…` text in the result position. So `=XLOOKUP("zz",A1:A3,B1:B3)` returns `("", err)` with `err != nil`, and a caller that tests `result == "#N/A"` sees `""` and concludes the call succeeded. The error's message carries the `#…` code where the failure has one and a descriptive sentence otherwise; neither the message text nor the error's identity is checked, only that the result is empty and the error is non-nil.

#### 4.09.1 Matching a criterion

```
COUNTIF  COUNTIFS  AVERAGEIF  MAXIFS  MINIFS
```

These join the `SUMIF` milestone 07 introduced, and the criteria parser behind all of them is rebuilt. A criterion is either an operator form — `>`, `>=`, `<`, `<=`, `=`, `<>` followed by an operand — or a bare value, which is an equality test. `COUNTIF` takes exactly two arguments; a third is an error, and so is a range with no criterion.

`COUNTIFS` pairs each range with its own criterion and applies the pairs left to right, each narrowing the surviving set. A trailing range without a criterion is `#N/A`, not a two-argument `COUNTIF`.

> **Oracle quirk.** The narrowing is abandoned rather than carried through when a pair *after the first* leaves nothing: the set from the preceding pair stands as the result, so `=COUNTIFS(A1:A4,">3",C1:C4,"1")` over a grid where `>3` selects one row and `1` selects none of it is `1`, not the `0` an intersection would give. Only a **first** criterion that selects nothing ends the match at zero. `MAXIFS` and `MINIFS` share the matcher and so share the rule: the aggregate is taken over the set that survived the last non-empty pair, and an aggregate over a set that comes out empty is `0` rather than an error — `=MAXIFS(A1:A4,B1:B4,"zzz")` is `0`.

`AVERAGEIF(range, criterion [, average_range])` and `SUMIF(range, criterion [, sum_range])` aggregate the tested range when no third range is given. `MAXIFS` and `MINIFS` take the **value range first**, then criterion pairs. `AVERAGEIF` refuses a call whose criterion is missing, and refuses a criterion that selects nothing — a mean over an empty selection has no value to report.

> **Deviation from OpenFormula §6.18.5.** A text criterion is compiled as a Go regular expression and matched **without anchors**, so it succeeds wherever it occurs in the value: `=COUNTIF(B1:B4,"ap")` counts every value containing `ap`, not only those equal to it. Matching is case sensitive, and regular-expression syntax is live — `^` anchors and `$` terminates. The wildcards are translated into that expression (`*` becomes `.*`, `?` becomes `.`) and the result is still unanchored, so `"*a"` is `.*a` and matches every value *containing* an `a`.

> **Oracle quirk.** A `<>` comparison is made on the string form, so `=COUNTIF(A1:A4,"<>9")` against a range holding 1–4 excludes nothing and counts all four.

Two inherited `SUMIF` contracts move as a result of the rebuild; both are in scope here. The `<>` operator is now read as one token, so `=SUMIF(A1:A4,"<>2")` selects the other values where milestone 07 matched only the leading `<` and selected nothing. And the wildcard forms change meaning: milestone 07 read a leading star as "ends with" and a trailing star as "begins with", and neither survives the move to an unanchored regular expression.

#### 4.09.2 Lookup, reference and information functions

```
INDEX  MATCH  XLOOKUP  ADDRESS  TRANSPOSE  SHEETS  ISFORMULA  ISLOGICAL  ISREF
```

`INDEX(range, row, column)` names one cell. Omitting the column selects a whole row, and a row of zero means every row; both are matrices, and a matrix rendered into a single cell is the empty string, so the two-argument form yields nothing visible rather than an error. Out-of-range coordinates are reported separately for each axis.

`MATCH(needle, range [, type])` returns a one-based position. The default type is 1 — the largest value not greater than the needle, assuming ascending order; 0 is exact only; −1 expects descending order. Neither non-zero type is a direction *check*: each walks the array in order, returns the first element equal to the needle, and stops at the first element **strictly past** it on the side it searches — the first element **greater** than the needle for type 1, the first element **less** than it for type −1 — reporting the position *before* that element, or `#N/A` when it is the first. So type −1 over an **ascending** array is refused: `=MATCH(2,A1:A3,-1)` over 1, 2, 3 is `#N/A`, where the mirror of the type-1 rule would find position 2. A type that is neither 0 nor −1 is silently taken as 1. The lookup array must be exactly one column wide: a wider range, a single cell and a scalar are all refused, all three for the shape of the array rather than for the value being sought. As everywhere in §4.09, the wording of the diagnostic is not part of the contract — only that the call is refused.

`XLOOKUP(needle, lookup, result [, if_missing [, match_mode [, search_mode]]])` searches one vector and answers from another. The default match is exact, so a missing value is `#N/A` unless the fourth argument supplies a replacement. The match mode is a closed set — `0` exact (the default), `1` next larger, `−1` next smaller and `2` wildcard — and a value outside it is refused; the search mode is `1` linear (the default), `−1` reverse linear and `2`/`−2` binary, refused outside that set too.

> **Oracle quirk.** Mode `−1` is served by the descending-order search, so on an ascending vector with no exact match it answers from the **first** entry of the result vector rather than from the next smaller one: over an ascending `A1:A3` of `1,2,3` with `B1:B3` of `10,20,30`, `=XLOOKUP(2.5,A1:A3,B1:B3,"none",-1)` is `"10"`, and `"10"` is the expected value. Mode `1` does behave as described — the same call with `1` is `"30"`.

`TRANSPOSE` yields a matrix and is only useful inside an aggregate. `SHEETS` with no argument counts the workbook's worksheets, and with a reference counts the distinct sheets that reference spans; a value that is not a reference spans none and is refused. `ISREF` distinguishes a reference from a value, and `ISFORMULA` and `ISLOGICAL` inspect their operand.

> **Oracle quirk.** `ADDRESS(row, column)` range-checks the row argument and then **discards it**: the column number supplies both halves of the address, so `=ADDRESS(2,3)` is `$C$3`, not `$C$2`. A row of zero is accepted for the same reason; only the upper bound is enforced, and it excludes the last row of the grid.

`ADDRESS` takes three further optional arguments. The third is the absolute mode, one of 1 (both halves pinned), 2 (the row only), 3 (the column only) or 4 (neither); it defaults to 1, and a value outside 1–4 — including 0 — is an error. The fourth is the reference style: true or omitted is A1, and false is R1C1, where the absolute mode decides whether the offsets are bracketed, so `=ADDRESS(2,3,1,FALSE)` is `R3C3` and `=ADDRESS(2,3,4,FALSE)` is `R[3]C[3]`. The fifth is a sheet name, prefixed to the reference with a `!`: `=ADDRESS(2,3,1,TRUE,"Sheet1")` is `Sheet1!$C$3`. Every form is built from the column argument alone, as the quirk above describes.

#### 4.09.3 Text operands in comparisons, and the conditional functions

```
IFNA  IFS  SWITCH  XOR
```

The comparison and concatenation operators learn to handle text operands, which until this milestone made the whole formula invalid. Two text literals compare as a plain byte ordering, so the comparison is case sensitive and every upper-case letter sorts before every lower-case one. The result is an ordinary boolean, usable wherever one is expected. Concatenation with `&` joins text operands and mixes text with numbers.

`IFNA` substitutes only for `#N/A`; any other error propagates.

`IFS(condition, value, …)` scans its condition/value pairs in order and returns the value belonging to the first condition that holds. Scanning stops there, so arguments after the satisfied pair are never examined. If no condition holds the result is `#N/A`. Fewer than two arguments is an error.

`SWITCH(expression, value, result, …)` compares the expression against each value in turn and returns the matching result. An odd trailing argument is the default, returned when nothing matched; with no such default an unmatched expression is `#N/A`. Fewer than three arguments is an error.

`XOR` is true for an odd number of truths. A number counts when it is non-zero, and a range contributes each of its cells. A text argument counts only if the text itself reads as a boolean or a number, so `=XOR(TRUE,"TRUE")` is `FALSE` — the `"TRUE"` is counted, making two truths — while `=XOR(TRUE,"a")` is `TRUE`, the `"a"` having been passed over. `#VALUE!` is reached only when *no* argument yielded a truth value at all, as in `=XOR("a")`. Calling it with no arguments is an error.

> **Oracle quirk.** A mixed comparison never coerces: the numeric side is simply treated as the smaller one, whatever the two values are, so `=1<"a"` is `TRUE` and `="a"<1` is `FALSE`.

> **Oracle quirk.** A cell reference is classified as a numeric operand *before its contents are looked at*, so a text-valued cell takes the number side of a mixed comparison rather than being compared as text. With `C1` holding `"text"`, `=C1<"a"` is `TRUE`.

> **Oracle quirk.** Equality compares the operands as they were written, without a numeric reading. `=1="1"` is `TRUE` but `=1.0=1` is `FALSE`.

#### 4.09.4 Date and time functions

```
DATEVALUE  DAY  DAYS  MONTH  YEAR  MINUTE  TIME  WEEKDAY  ISOWEEKNUM  YEARFRAC
```

All of these work in the serial-number representation the workbook stores dates in, where 1 is 1900-01-01 and the fractional part is the time of day. `DAY`, `MONTH` and `YEAR` each accept either a serial number or a parseable date string; serial zero is accepted rather than refused, and it sits the day before the epoch, 1899-12-30 — but only two of the three parts say so: `YEAR(0)` is 1899 and `MONTH(0)` is 12, while `DAY(0)` is **0**, not the 30 that date would give. A negative serial has no date.

`DAYS(end, start)` takes the end date first, so the natural ordering gives a positive count and the reverse a negative one. `TIME(h, m, s)` builds the fraction of a day and does not fold hours beyond a day back, so the result may exceed 1. It refuses a **negative total** — `=TIME(-1,0,0)` and `=TIME(0,0,-1)` are both `#NUM!` — but the three components are summed before the sign is read, so `=TIME(-1,60,0)` is `0` and is accepted. `MINUTE` reads the minute out of the fractional part alone and refuses a negative operand outright. Both also accept their arguments as numeric text: `=TIME("12","30","45")` is `0.521354166666667`, and `=MINUTE("15:30:00")` is `30`. `WEEKDAY` numbers the day under the chosen scheme and refuses an unrecognised scheme number. `YEARFRAC` takes an optional day-count basis, defaulting to basis 0, the 30/360 US convention.

> **Deviation from OpenFormula §6.10.17 and §6.10.12.** Neither clause's "may be any number" holds here. `TIME` sums its three components and refuses a negative total, so `=TIME(-1,0,0)` and `=TIME(0,0,-1)` are `#NUM!` where the clause's formula would give a negative fraction of a day; `MINUTE` refuses any negative argument outright, where the clause's `INT(HourFraction × 60)` gives `0` for `-1`.

Basis 1, actual/actual, divides the day count by the averaged length of the years the interval spans, and it is the one result in this section whose exact rendering fits in fifteen decimal places and is therefore handed back by §4.09.17 **unrounded**, at sixteen significant figures: `=YEARFRAC(43831,44259,1)` is `1.170998632010944`. Nothing here fixes the order in which that quotient is formed, so — exactly as for the closed-form financial results of §4.09.7 — **its trailing digits are out of scope**: it is parsed as a number and compared to a relative `1e-9`, which is far tighter than a wrong basis, a wrong day count or an off-by-one at either endpoint. That licence covers exactly the one value above. Every other result in this section is compared as an exact string, the other four bases included: basis 0 and basis 4 give `1.175`, basis 2 gives `1.18888888888889` and basis 3 gives `1.17260273972603`. A basis-1 result that is genuinely exact is compared as a string too, because there the string *is* the value — the degenerate `=YEARFRAC(44259,44259,1)`, whose endpoints coincide, is `0`.

`DATEVALUE(text)` accepts four written forms and no others. A **slashed** form is read month first, so `"03/04/2021"` and `"3/4/2021"` are both 4 March 2021 and `"04/03/2021"` is 3 April; there is no day-first reading. A **hyphenated all-digit** form is read year first when its leading field is a four-digit year from 1900 on, `"2021-03-04"`; otherwise the leading field must be a month number and the form is month-day-year, so `"3-4-2021"` is also 4 March. The two **named-month** forms are `d-month-y` and `month d, y`, as in `"4-mar-2021"`, `"4-march-2021"` and `"march 4, 2021"`; either the three-letter abbreviation or the full name will do. Any of the four may be followed by a space and a time of day.

> **Oracle quirk.** A month name is matched **in lower case only**. `"4-mar-2021"` parses and `"4-Mar-2021"` is `#VALUE!`; `"march 4, 2021"` parses and `"March 4, 2021"` is `#VALUE!`. Slashes are only ever month-first, so a year-first slashed date has no reading at all and `"2021/03/04"` is refused.

> **Oracle quirk.** `DATEVALUE` parses a time of day in its argument and then discards it, yielding the whole-day serial only. Its argument must be text — a serial number, which is already the answer, is not accepted as input.

#### 4.09.5 The statistical additions

```
AVEDEV  CHIDIST  CONFIDENCE  CONFIDENCE.NORM  DEVSQ  GEOMEAN  PERCENTILE.EXC
PERCENTRANK  PERCENTRANK.EXC  PERCENTRANK.INC  QUARTILE.EXC  RANK  RANK.EQ
STANDARDIZE  STDEV.P  STDEVP  TRIMMEAN  VAR  VAR.S  VARA  VARPA  WEIBULL
WEIBULL.DIST  ZTEST  Z.TEST
```

`VAR`, `VAR.S` and `VARA` are the sample variance; `VARPA`, `STDEVP` and `STDEV.P` are the population forms. A boolean argument counts as its numeric value in all of them. `GEOMEAN` is undefined once any factor is zero or negative. `STANDARDIZE` has no scale to divide by at a zero standard deviation.

`RANK` and `RANK.EQ` are the same function; the default order is descending, and a non-zero third argument switches to ascending. A value not in the set has no rank. `PERCENTRANK.INC` puts the smallest value at 0 and the largest at 1; `PERCENTRANK.EXC` places the *k*-th of *n* members at `k/(n+1)`, leaving room at both ends — so with `E1:E7` holding 1 to 7, the member `3` is the third and `=PERCENTRANK.EXC(E1:E7,3)` is `0.375`. The optional third argument is the significance, defaulting to **3** (§6.18.58), and it must be at least 1. It is applied as `floor(position × 10^significance) / 10^significance` — so it counts digits **after the decimal point** rather than significant figures as §6.18.58 reads it, and the kept digits are **truncated toward zero, never rounded**. With `E1:E7` holding 1 to 7, the position of `3.5` is `0.41666…`, and `=PERCENTRANK(E1:E7,3.5)` is `0.416`, not the `0.417` that rounding gives; `=PERCENTRANK(E1:E7,3.5,2)` is `0.41` and `=PERCENTRANK(E1:E7,3.5,9)` is `0.416666666`. `PERCENTILE.EXC` interpolates at position `k*(n+1)` and `QUARTILE.EXC` is the same computation at `k = quart/4`; a `k` outside the open unit interval, and a quart outside 1–3, are rejected before any interpolation.

`WEIBULL` and `WEIBULL.DIST` share an implementation whose fourth argument chooses the cumulative form over the density. `ZTEST` and `Z.TEST` are the one-tailed probability that the sample mean exceeds the given one, with an optional known standard deviation. The formulation required is OpenFormula **§6.18.87**'s, `z = (x̄ − Mean) / (Sigma / √n)` with the upper tail of the standard normal; the part of it that decides the answer is the sentence "if `Sigma` is omitted, it is estimated from `Sample`, using **`STDEV`**" — the *sample* deviation, dividing by `n − 1`. Over `A1:A3` = 1, 2, 3 that gives `=Z.TEST(A1:A3,1)` = `0.0416322583317752`, where estimating with the population form `STDEVP` would give about `0.01695`.

`CHIDIST(x, degrees_freedom)` takes **exactly two** arguments and is the **right** tail of the chi-squared distribution, `1 − γ(df/2, x/2) / Γ(df/2)`. It is not the two-tailed test and it is not the inverse; `=CHIDIST(0,1)` is `1`, the whole mass lying to the right of zero, and a negative `x` is refused. The lower incomplete gamma is evaluated as a **fixed thirty-three-term series**, `γ(a,x) = x^a · e^(−x) · Σ(n=0..32) xⁿ / (a(a+1)···(a+n))`, which is what fixes the trailing digits of the answer: the term count is part of the required behaviour, not a convergence criterion.

`CONFIDENCE` and `CONFIDENCE.NORM` take exactly three arguments, `(alpha, standard_deviation, size)`, and return the **two-tailed** half-width `−NORMSINV(alpha/2) × sd / √size`, built on the inherited `NORMINV`. Alpha must lie strictly inside the unit interval, the deviation must be positive, and the size must be at least one.

> **Note on trailing digits.** `CHIDIST`, `CONFIDENCE` and `ZTEST` reach their answers through series and rational approximations rather than a closed form, so their last digits belong to the approximation and not to the distribution. Each is checked against the full rendered result of **the formulation named for it above** — the thirty-three-term series for `CHIDIST`, the `NORMSINV` half-width for `CONFIDENCE` — whose inverse normal §4.08.5 pins to the digit — and OpenFormula §6.18.87's sample-`STDEV` form for `ZTEST`. There is no tolerance on any of the three, so the formulation has to be the one described and not a better one.

> **Oracle quirk.** `AVEDEV` averages the absolute deviations over the **arguments** it was handed, not over the values behind them, and a *range* argument contributes `0` to that sum instead of its members. The mean it deviates from is still `AVERAGE` of everything, so a lone range argument comes back as the mean itself: over `A1:A3` = 1, 2, 3, `=AVEDEV(A1:A3)` is `2` — the same answer as `=AVERAGE(A1:A3)` — where §6.18.2's mean absolute deviation is `0.666666666666667`. Over 1, 2, 6 it is likewise `3`. The comma-separated form is correct, `=AVEDEV(1,2,3)` being `0.666666666666667`, and mixing the two shows the coercion directly: `=AVEDEV(A1:A3,0)` is `1.5`, the mean of 1, 2, 3 and 0 deviated from twice. `DEVSQ`, `STDEV`, `SUM`, `MAX` and `COUNT` all read the same range correctly. `NPV` coerces a range argument the same way `AVEDEV` does — the one other function that does, and §4.08.7 records it — and apart from those two this is not a general treatment of ranges. It arrives at this milestone and is still the behaviour at the next one, so it is not a transitional state to be corrected later.

> **Oracle quirk.** `WEIBULL` rejects a non-positive shape or scale but does **not** check a negative `x`: it is fed straight into the formula.

#### 4.09.6 The engineering and text additions

```
BESSELK  BESSELY  DELTA  ERF  ERF.PRECISE  ERFC  ERFC.PRECISE  GESTEP
TEXTJOIN  VALUE
```

`BESSELK(x, n)` and `BESSELY(x, n)` complete the Bessel set begun by the inherited `BESSELI` and `BESSELJ` — the argument first, the order second. Both are singular at zero, so a non-positive argument and a negative order are refused. A fractional order is floored.

> **Oracle quirk.** Unlike `BESSELI` and `BESSELJ`, which are computed to double precision, `BESSELK` and `BESSELY` are the classic **Abramowitz & Stegun polynomial approximations** — the `bessk0`/`bessk1`/`bessy0`/`bessy1` form reproduced in *Numerical Recipes*, with a small-argument branch for `x ≤ 2` and an asymptotic branch above it, orders above one built by upward recurrence from the order-0 and order-1 values. They are accurate to roughly **eight significant digits and no further**: `BESSELK(1,0)` is checked against `0.421024441682191` where the true K₀(1) is `0.42102443824070833`, and `BESSELY(1,0)` against `0.0882569713977081` where the true Y₀(1) is `0.08825696421567696`. Both diverge from the exact function at the ninth significant digit, and the digits past that point are artifacts of the polynomial. **Check against the approximation, not against the function.** The six expected values are:
>
> | Call | Value |
> | --- | --- |
> | `=BESSELK(1,0)` | `0.421024441682191` |
> | `=BESSELK(1,1)` | `0.601907227223457` |
> | `=BESSELK(2,1)` | `0.13986588` |
> | `=BESSELY(1,0)` | `0.0882569713977081` |
> | `=BESSELY(1,1)` | `-0.78121282095312` |
> | `=BESSELY(2,1)` | `-0.10703243170956` |
>
> `=BESSELK(2,1)` is short for a reason rather than truncated: at `x = 2` the small-argument branch's logarithmic term vanishes and its polynomial argument is exactly 1, so the result is the plain sum of the coefficients and its shortest exact rendering really is eight digits. `DELTA` tests two numbers for equality, comparing against zero when given one argument. `GESTEP` compares against a threshold inclusively, the step standing at zero when no threshold is given.

`ERF` with two arguments integrates between the limits, so `ERF(0,x)` is the same as `ERF(x)`; `ERF.PRECISE` is the single-limit form only. `ERFC` and `ERFC.PRECISE` are the complement and take no second limit.

`TEXTJOIN(separator, skip_empty, ...)` concatenates; the second argument says whether empty cells are skipped, and keeping them puts a separator where each one was, including a trailing one for a trailing empty cell.

> **Oracle quirk.** `VALUE` reads a number out of text, understanding a trailing per-cent sign and exponent notation, but does **not** strip currency symbols or thousands separators.

#### 4.09.7 The financial additions

```
ACCRINT  ACCRINTM  COUPDAYBS  COUPDAYS  COUPDAYSNC  COUPNCD  COUPNUM  COUPPCD
DISC  DURATION  INTRATE  MDURATION  ODDFPRICE  PRICE  PRICEDISC  PRICEMAT
RECEIVED  TBILLEQ  TBILLPRICE  TBILLYIELD  YIELD  YIELDDISC  YIELDMAT
AMORDEGRC  AMORLINC  PV  RATE  RRI  SLN  SYD  VDB  XIRR  XNPV
```

Twenty-three of these price or measure a security and ten cover depreciation, present value and dated cash flows. The coupon functions divide a bond's payment schedule; `COUPNCD` and `COUPPCD` return their dates as **serial numbers**, not formatted dates. The basis argument is optional throughout; the frequency is not, and it must be one of the recognised payment frequencies. The discounted-security functions require the dates in order and the money positive, and the coupon functions require the same order — `settlement` before `maturity`, a settlement on or after maturity being refused. The three treasury-bill functions take exactly three arguments and have no basis. `MDURATION` is `DURATION` discounted by one period's yield, so it is always the smaller of the two.

`AMORDEGRC(cost, purchased, first_period, salvage, period, rate [, basis])` is the French declining-balance schedule, and `AMORLINC` the straight-line one. Both take their two dates as text (see the quirk below).

The degressive coefficient is **not** an argument: it is looked up from the implied asset life, `1/rate`, on a four-band table, and the rate is multiplied by it before any charge is worked out.

| implied life `1/rate` | coefficient |
| --- | --- |
| under 3 | 1 |
| 3 or more, under 5 | 1.5 |
| 5 or more, up to and including 6 | 2 |
| over 6 | 2.5 |

The bands are compared against `1/rate` and not against the rate, so they do not fall where reading them off the rate would suggest, and the coefficient jumps at the boundary. With cost `1000`, no salvage, period `0` and a first period one quarter of a year away, `rate` `0.3333334` gives `83` and `0.3333333` gives `125`: the two rates differ in the seventh decimal place, but the first has a life a shade under 3 and the second a shade over, so the coefficient moves from 1 to 1.5. `0.1666667` gives `83` and `0.1666666` gives `104` at the other boundary, where the life crosses 6 and the coefficient moves from 2 to 2.5.

A rate of one half or more has no band and is `#NUM!`; the table stops there because a life below 2 has no entry.

Each period's charge is `cost x rate x coefficient` **rounded to the nearest whole number, halves away from zero**, and then subtracted from the cost the next period works from, so the charges are whole numbers throughout. Period `0` is prorated by `YEARFRAC(purchased, first_period, basis)` rather than being a full period. With `=AMORDEGRC(1000,"2020-01-01","2020-03-31",100,p,0.15)` — where the year fraction is `0.25` and the coefficient is `2.5` — the schedule runs `94`, `340`, `212`, `133` for `p` of 0 to 3. The basis argument is optional and defaults to 0, so passing `0` explicitly is the same call.

> **ODDFPRICE — the computation the clause does not give.** OpenFormula §6.12.31 defines `ODDFPRICE(settlement, maturity, issue, first_coupon, rate, yld, redemption, frequency [, basis])`, its parameters and its constraints, and **no arithmetic at all**; what is required here is Excel's two-case form for a security whose first coupon period is irregular, written out in full. Write `m` for the frequency, `x` for `1 + yld/m`, and `days(a, b)` for the day count from `a` to `b` under the basis — the count the `COUP*` functions of this section divide. `E` is `COUPDAYS(settlement, maturity, m, basis)`, the length of the coupon period containing the settlement date; `N(a, b)` is `COUPNUM(a, b, m)`, which is `ceil(YEARFRAC(a, b, 0) × m)`; and the **quasi-coupon dates** are the first coupon date stepped backwards by `12/m` months at a time, each step keeping the day of the month and taking the month's last day when that day does not exist.

> `DFC = days(issue, first_coupon)` decides the case, and `settlement > issue`, `first_coupon > settlement` and `maturity > first_coupon` are required of both.
>
> **Short first period**, when `DFC < E`:
>
> ```
> DSC = days(settlement, first_coupon)      A = days(issue, settlement)
> price = redemption / x^(N(settlement,maturity)−1+DSC/E)
>       + 100·rate/m · (DFC/E) / x^(DSC/E)
>       + Σ over k = 2…N(settlement,maturity) of 100·rate/m / x^(k−1+DSC/E)
>       − 100·rate/m · (A/E)
> ```
>
> **Long first period**, otherwise:
>
> ```
> N = N(first_coupon, maturity)
> Nq = the number of quasi-coupon dates after the settlement date and not later than the first coupon date
> DSC = days(settlement, COUPNCD(settlement, first_coupon, m, basis))          when the basis is 2 or 3
>     = E − days(COUPPCD(settlement, first_coupon, m, basis), settlement)      otherwise
> DCNL, ANL = the sums over the NC = N(issue, first_coupon) quasi-periods that end at the first
>             coupon date or earlier, taken in any order; per quasi-period, with `start` and `end`
>             its bounds and NL = E (or days(start, end) on basis 1):
>     DCNL adds days(max(issue, start), end) / NL
>     ANL  adds days(max(issue, start), min(settlement, end)) / NL
> price = redemption / x^(N+Nq+DSC/E)
>       + 100·rate/m · DCNL / x^(Nq+DSC/E)
>       + Σ over k = 1…N of 100·rate/m / x^(k+Nq+DSC/E)
>       − 100·rate/m · ANL
> ```
>
> The one case in scope: settlement `2017-02-01`, maturity `2021-03-31`, issue `2016-12-01`, first coupon `2017-03-31`, rate `0.055`, yield `0.035`, redemption `100`, frequency `4`, basis `0`. `E = 90` and `DFC = 120`, so the long case applies; `NC = 2` and the quasi-periods are `2016-09-30…2016-12-31` and `2016-12-31…2017-03-31`, giving `DCNL = 1 + 1/3` and `ANL = (31 + 30)/90`; `Nq = 1`, `DSC = 90 − 31 = 59` and `N = 16`. `=ODDFPRICE("02/01/2017","03/31/2021","12/01/2016","03/31/2017",0.055,0.035,100,4,0)` prices it at `106.77169500099872` — a *value*, compared to a relative `1e-9` like the rest of the closed forms above, so the digits above that tolerance are what is checked and the rendering is not.

> **Deviation from OpenFormula §6.12.2.** `ACCRINT` neither enforces the clause's `Issue < First < Settlement` constraint nor reads a coupon schedule: the accrued interest is `par × rate ×` the year fraction of §4.09.4 between **issue and settlement** on the given basis, and the first-interest date and the calculation-method flag are taken for their arity and then ignored. An issue date equal to the first-interest date is therefore accepted, and with `E1` and `E2` a common year apart on basis 0, `=ACCRINT(E1,E1,E2,0.1,1000,1,0,FALSE)` is `100` — `1000 × 0.1 × 1`, the year fraction of a whole common year. `ACCRINTM` is the same product over its two dates, with `settlement` required to be no earlier than `issue`, and it never consults the coupon schedule either.

`SLN`, `SYD` and `VDB` are the depreciation schedules: a life of zero has no schedule, and the period must lie inside it. `PV` moves the payment to the start of each period with its fifth argument and reduces to the undiscounted total at a zero rate. `RRI` is the growth rate implied by a start and an end value. `XIRR` and `XNPV` read a column of cash flows against a column of dates; `XNPV` discounts, so a rate of zero leaves it nothing to do and is refused. `XIRR` needs flows of both signs and refuses a set that has only one.

`RATE`, `XIRR` and `YIELD` are the **iterative** functions here. All three return the iterate they stop on rather than a refined root, so it is the stopping rule that decides whether the rendered answer is reproducible at all — and on that they differ, which is why they are not checked the same way.

- `RATE(nper, pmt, pv, [fv], [type], [guess])` starts at the guess, which **defaults to 0.1**, and steps until `|Δ| < 1e-6` or 100 iterations have passed, whichever comes first. A stop that loose leaves the iterate well short of floating-point accuracy, so its trailing digits belong to the path rather than to the root: they move with the guess, and they move with how the residual and its derivative are arranged — which this contract does not fix and will not. **`RATE`'s rendered digits are therefore out of scope: not in a requirement group, not in a gate, not at this milestone and not at the next one.** What is checked is the rate itself, to a relative tolerance, together with the shape of the call. `=RATE(60,-200,10000)` is about `0.0061834132` and `=RATE(24,-800,10000)` about `0.060435578`; the guess defaults to `0.1`, so passing `0.1` explicitly is the same call as omitting it, and that is the case this milestone checks. A *different* guess does move the answer, within that tolerance — `=RATE(24,-800,10000,0,0,0.5)` agrees with the default only to about the tenth figure — which is the mark of an iterate rather than a root.
- `YIELD(settlement, maturity, rate, price, redemption, frequency, [basis])` also solves by iteration, and is checked on its **value** rather than on its rendered digits: `=YIELD("2017-01-01","2021-12-31",0.05,95,100,2,0)` is compared to a relative `1e-9` around `0.0617815124629779`.
- `XIRR(values, dates, [guess])` measures each date as `(dᵢ − d₀)/365` years, steps until either the step or the residual is within `1e-10`, and gives up after 50 iterations with `#NUM!`. The **guess defaults to `0`** — not to the `0.1` OpenFormula §6.12.51 states, which is the default of the *periodic* `RATE` above — and the default is value-affecting: the iteration stops on a tolerance and starts from a different point, so the rendered result moves with the guess. A guess of `-1` or below is refused. That stop is tight enough to leave the iterate at floating-point accuracy, so unlike `RATE` its rendering is reproducible, and `XIRR` **is** checked against the full rendered result.

> **The trailing digits of the closed-form results named below are out of scope.** §4.08 made this promise for the twelve annuity and cash-flow functions milestone 08 added, and it extends here, in the same terms, to those of the closed-form functions this section adds. A closed form fixes the *value*, not the last two or three bits of the `float64` an implementation arrives at — and because §4.09.17 returns a result of fifteen or fewer decimal places **exactly as produced**, those bits reach the rendered string. Two correct implementations that group a discounted-coupon summation differently, that accumulate a dated cash-flow series from the far end rather than the near one, or that reach a discount factor through `math.Exp`∘`math.Log` rather than `math.Pow`, disagree there, and neither answer is the more accurate one: summing the `XNPV` flows in reverse gives `2472.982419245307` where the forward sum gives `2472.9824192453066`, and `RECEIVED`'s `1000/0.95` is `1052.6315789473686` where the same quantity reached as `1000 × (1/0.95)` is `1052.6315789473683`. **No case of any kind pins the trailing digits of `ACCRINT`, `ACCRINTM`, `PRICE`, `RECEIVED`, `TBILLPRICE`, `DURATION`, `MDURATION`, `ODDFPRICE`, `PV` or `XNPV`** — not a case in scope, and not one of the unweighted regression gates, at this checkpoint or at a later milestone re-checking this function set. Wherever such a result is asserted it is parsed as a number and compared to a tolerance wide enough to absorb any regrouping of the closed form: a **relative `1e-9`**, the same tolerance §4.08 states for the same hazard. A result that is genuinely exact is still compared as a string, because there the string *is* the value — a whole number such as `=ACCRINTM("2011-01-01","2011-04-01",0.1,1000)` = `25`, or a zero-rate reduction such as `=PV(0,10,-1000)` = `10000`. `XIRR` keeps its full rendering, for the reason given in the bullet above; `RATE` is compared to a relative `1e-6`, its digits being out of scope entirely.

> **Oracle quirk.** Only four functions force text dates. `AMORDEGRC`, `AMORLINC`, `XIRR` and `XNPV` push their date arguments through `DATEVALUE` rather than accepting a serial number, so those dates must be written as text `DATEVALUE` can parse — a serial number is not a date there. Every other function in the family accepts serial numbers.

> **Note.** `RATE` reverses the milestone-08 contract; see §4.09.17.

#### 4.09.8 The exported limits and error sentinels

The library's diagnostics become exported sentinel values rather than strings assembled at the point of failure, so a caller can branch on the kind of failure with `errors.Is`. Thirty-six `Err*` values are exported; milestone 08 exported none. Thirty of them are the pairings listed below, and those thirty are what this milestone requires. The remaining six — `ErrAttrValBool`, `ErrCoordinates`, `ErrEncrypt`, `ErrTotalSheetHyperlinks`, `ErrUnknownEncryptMechanism` and `ErrUnsupportEncryptMechanism` — sit on paths this milestone does not reach through the public interface, and neither their presence nor what returns them is checked here. The sentinels are phrased in terms of the limit constants, of which `MaxFieldLength`, `StreamChunkSize` and `UnzipSizeLimit` are new here and the rest carry over:

```go
MaxColumnWidth  MaxFieldLength  MaxFileNameLength  MaxFontFamilyLength
MaxFontSize     MaxRowHeight    StreamChunkSize    TotalCellChars
TotalColumns    TotalRows       UnzipSizeLimit
```

`MaxFieldLength` is **255** — the same 255 as the data-validation formula limit of §4.02.6, which is one of the several places the library applies it. `StreamChunkSize` and `UnzipSizeLimit` get their values in §4.09.10; the other eight are unchanged from §4.08.

Which entry point returns which sentinel is in scope; the mapping is not mechanical, and these are the pairings that matter:

- Coordinates and geometry — `ErrColumnNumber`, `ErrColumnWidth`, `ErrMaxRows`, `ErrMaxRowHeight`, `ErrOutlineLevel`. Inside the limits there is no error at all.
- Worksheets — `ErrSheetIdx` (a negative index, an index naming no worksheet, or a `CopySheet` whose source and destination are the same index), `ErrExistsWorksheet` (a chartsheet taking the name of an existing worksheet), `ErrGroupSheets` (a group that omits the active sheet).
- Defined names — `ErrDefinedNameScope`, `ErrDefinedNameduplicate` (spelled as shown).
- Styles — `ErrCustomNumFmt`, `ErrFontLength`, `ErrFontSize`, `ErrParameterInvalid` for a `NewStyle` argument that is neither JSON text nor a `*Style`.
- Data validation — `ErrDataValidationRange`, `ErrDataValidationFormulaLenth` (spelled as shown).
- Sparklines — `ErrParameterRequired` (a **nil** `*SparklineOption`, which is refused before any field is looked at), then `ErrSparklineLocation`, `ErrSparklineRange`, `ErrSparkline` (a location count and a range count that disagree), `ErrSparklineStyle` and `ErrSparklineType` for a non-nil option missing or mis-setting that particular field.
- Files and pictures — `ErrSave` (a workbook built in memory has no path to save back over), `ErrImgExt`, `ErrMaxFileNameLength`, `ErrAddVBAProject`.
- Cells and formulas — `ErrCellCharsLength`, which caps a rich-text cell at the same length as any other cell value, and `ErrInvalidFormula`, reported whatever it was the tokenizer could not make sense of.
- The streaming writer and the options — `ErrStreamSetColWidth`, `ErrOptionsUnzipSizeLimit`.

> **Note.** `ErrImgExt` is reported before the image bytes are decoded, but the entry points that take a *path* check that the file exists first, so provoking the extension error through them needs a real file on disk.

#### 4.09.9 Shared formulas

```go
type FormulaOpts struct {
    Type *string // Formula type
    Ref  *string // Shared formula ref
}

func (f *File) SetCellFormula(sheet, axis, formula string, opts ...FormulaOpts) error
```

The signature and the four `STCellFormulaType*` constants are unchanged from §4.04.2; what they do changes. At milestone 04 the option was a pair of attributes the caller wrote by hand onto one cell, and reading any other cell of the range gave back the master's text unchanged. Here `SetCellFormula` with `Type` set to `shared` **populates the whole range**, giving every cell in it `t="shared"` and an index the library assigns itself, counting up from zero per worksheet.

A shared formula is therefore written once and read back everywhere in its range: the master cell carries the text, a `ref` and an index, serialised as `<f t="shared" ref="B1:B3" si="0">`; the other cells of the range carry the index and no text. `GetCellFormula` on any of them returns the formula **translated to that cell's position** — both the row and the column offset are applied, both ends of a range argument move, a sheet qualifier stays attached to the reference it qualifies, and text inside the formula is left exactly as written. A `$` pins its half of the reference so only the free half moves.

The offset is measured from wherever the master sits, so a master in the middle of the range translates backwards as well as forwards. The range is normalised, so naming its corners the other way round makes no difference. Each shared range gets its own index, and neither group translates the other's text. Setting the master cell's formula to the empty string removes it, and the rest of the range then resolves to nothing.

The `ref` is now validated, where milestone 04 stored whatever it was given. `ErrParameterInvalid` is the sentinel for a `ref` that does not name two corners (`"F1"`, `""`, `"notaref"`). A `ref` with two corners but a corner that is not a cell name is reported by whichever corner failed to parse.

The array and data-table types travel through the same option. An array formula keeps its own range and is not translated anywhere. The normal type is the default and is written out explicitly.

> **Oracle quirk — do not build on this.** A `FormulaOpts` with `Type` set to `shared` and a nil `Ref` **panics**: the library dereferences the pointer without checking. A shared formula must always carry a `Ref`.

> **Oracle quirk.** Declaring a range that does not surround the master is accepted, but translating back to it can run off the left edge of the sheet, in which case the cells in the range resolve to nothing at all.

> **Oracle quirk.** A data-table formula is accepted and stored, but the type is dropped: the option returns as soon as it recognises the type.

#### 4.09.10 The open options and the decompression budgets

```go
type Options struct {
    Password          string
    RawCellValue      bool
    UnzipSizeLimit    int64
    UnzipXMLSizeLimit int64
}

const (
    StreamChunkSize = 1 << 24
    UnzipSizeLimit  = 1000 << 24
)
```

An OPC package is a ZIP archive whose declared sizes are not trustworthy (ECMA-376 Part 2 §7.3 and Annex B.2), so decompression is bounded. `UnzipSizeLimit` caps the whole package and `UnzipXMLSizeLimit` caps a single XML part, above which that part is spooled to a temporary file instead of being held in memory. Opening without options still works and takes the two exported constants as defaults.

The two budgets are reconciled rather than taken literally. A zero `UnzipSizeLimit` becomes the default but is widened to `UnzipXMLSizeLimit` if that is larger; a zero `UnzipXMLSizeLimit` becomes `StreamChunkSize` but is narrowed to `UnzipSizeLimit` if that is smaller. An explicit pair with the XML budget above the overall one is `ErrOptionsUnzipSizeLimit`. A package that exceeds the overall budget is refused with a message naming the budget in bytes.

`OpenFile` takes the same options tail as `OpenReader`.

> **Oracle quirk.** Several `Options` values may be passed, but they are not merged: the **last** one wins outright.

#### 4.09.11 Reading the stored value instead of the formatted one

```go
func (f *File) GetCellValue(sheet, axis string, opts ...Options) (string, error)
func (f *File) GetRows(sheet string, opts ...Options) ([][]string, error)
func (f *File) GetCols(sheet string, opts ...Options) ([][]string, error)
func (rows *Rows) Columns(opts ...Options) ([]string, error)
func (cols *Cols) Rows(opts ...Options) ([]string, error)
```

Every reading entry point takes the same `Options` tail, and `RawCellValue` selects the literal stored in the cell over the value the number format would display. A cell holding `1` under built-in format 2 (`0.00`) reads back as `1.00` by default and as `1` with the flag set. On a cell carrying no number format the flag is inert.

On the two iterators the option is per call, not per iterator, so a single walk can read some rows formatted and others raw.

#### 4.09.12 The cell type enumeration

```go
type CellType byte

const (
    CellTypeUnset CellType = iota
    CellTypeBool
    CellTypeDate
    CellTypeError
    CellTypeNumber
    CellTypeString
)

func (f *File) GetCellType(sheet, axis string) (CellType, error)
```

The enumeration is consecutive from zero in the order shown. A cell written with `SetCellBool` reports `CellTypeBool` and one written with `SetCellStr` reports `CellTypeString`.

> **Oracle quirk.** A number is the format's default, so a numeric cell carries no type attribute at all and reports `CellTypeUnset` rather than `CellTypeNumber`. An empty cell reports `CellTypeUnset` too, so the accessor cannot tell a number from a blank.

> **Note.** Every error path returns `CellTypeUnset` alongside the error — an absent worksheet, a malformed reference, an empty reference, and a row of zero.

#### 4.09.13 The extended document properties

```go
type AppProperties struct {
    Application       string
    ScaleCrop         bool
    DocSecurity       int
    Company           string
    LinksUpToDate     bool
    HyperlinksChanged bool
    AppVersion        string
}

func (f *File) SetAppProps(appProperties *AppProperties) error
func (f *File) GetAppProps() (*AppProperties, error)
```

The extended-properties part (ECMA-376 Part 1 §15.2.12.3, *Extended File Properties Part*; that is the only part of the citation seeded into `specs/`), distinct from the core properties `SetDocProps` and `GetDocProps` reach. A fresh workbook reports `Application: "Go Excelize"` and the zero value for everything else. All seven fields round-trip and survive a write and reopen. The two accessor pairs are independent: setting app properties does not disturb doc properties, and the reverse.

The part's variant-type namespace arrives as an exported variable alongside the accessors, `NameSpaceDocumentPropertiesVariantTypes`, an `xml.Attr` binding the prefix `vt` to `http://schemas.openxmlformats.org/officeDocument/2006/docPropsVTypes`. It is one of the names this milestone adds and **the variable itself is out of scope**: no case in scope and no gate names it, in the same way the six sentinels of §4.09.8 and the removal of §4.09.17's `XMLHeader` constant are declared but out of scope.

> **Oracle quirk.** `SetAppProps` **replaces rather than merges**: the seven fields are copied across whatever their value, so a zero field in the argument clears whatever the part held before — including the default `Application`.

#### 4.09.14 Iterator position, closing a workbook, and reading an archive

```go
func (rows *Rows) CurrentRow() int
func (rows *Rows) TotalRows() int
func (rows *Rows) Close() error
func (cols *Cols) CurrentCol() int
func (cols *Cols) TotalCols() int
func (f *File) Close() error
func (f *File) ReadZipReader(r *zip.Reader) (map[string][]byte, int, error)

type File struct {
    Drawings      sync.Map
    Sheet         sync.Map
    Relationships sync.Map
    Pkg           sync.Map
    // ... no other exported field changes type
}
```

`TotalRows` and `TotalCols` are known **before** the first `Next()`, because the extent is read when the iterator is opened. `CurrentRow` and `CurrentCol` are 0 until then and advance one per successful `Next()`; `CurrentRow` matches the row actually read. Both iterators reject an absent worksheet.

`Rows.Close` removes whatever that iterator spooled and returns nil when nothing was. `File.Close` does the same for the workbook — the parts an oversized `UnzipXMLSizeLimit` pushed to temporary files. A spooled worksheet is still readable in the ordinary way before the close.

`ReadZipReader` becomes a method on `*File`, where milestone 08 had it as a package-level function. It returns the part map, the number of worksheets found, and an error.

Four exported map fields become `sync.Map`s at this checkpoint — `Drawings`,
`Sheet`, `Relationships` and `Pkg` — where milestone 08 held all four as plain
Go maps: the lookup is `Load` and the traversal is `Range`. `Pkg` is also a
**rename**: milestone 08 called the parts map `XLSX map[string][]byte`, and that
field no longer exists. Only `Pkg` and `Sheet` are exercised by this
milestone's requirement groups; `Drawings` and `Relationships` are listed because
the type change is real and inherited code that indexes them will not compile.
`Pkg` is keyed by part name and holds the part content as `[]byte`. A workbook
from `NewFile()` already holds the nine template parts — `[Content_Types].xml`,
`_rels/.rels`, `docProps/app.xml`, `docProps/core.xml`,
`xl/_rels/workbook.xml.rels`, `xl/styles.xml`, `xl/theme/theme1.xml`,
`xl/workbook.xml` and `xl/worksheets/sheet1.xml` — and `Load` of a part that is
not there reports `false` rather than an empty slice.

`Pkg` is the package **as it was read or templated**, not a live serialisation:
writing a cell does not change the bytes under `xl/worksheets/sheet1.xml`.
Those bytes only reflect the edit once the workbook has been written and
reopened. `Sheet` is the separate map of *parsed* worksheets, keyed by part
path rather than by sheet name, and it is filled lazily — a workbook that has
only been opened has parsed nothing, and the first `GetCellValue` against a
worksheet is what puts its entry there.

> **Oracle quirk.** The unzip budget travels with the **receiver**, not with the archive, so the same `*zip.Reader` that one workbook reads may be refused by another whose budget is smaller.

> **Oracle quirk.** `File.Close` is **not idempotent**, and that is in scope. It removes the files it spooled but does not forget that it spooled them, so a second call tries to remove them again and fails, reporting `remove <path>: no such file or directory`. What is checked is that the second call returns a non-nil error; the message names a temporary path and is not itself asserted.

#### 4.09.15 Row styles and the streaming writer options

```go
func (f *File) SetRowStyle(sheet string, start, end, styleID int) error

type RowOpts struct {
    Height  float64
    Hidden  bool
    StyleID int
}

func (sw *StreamWriter) SetRow(axis string, values []interface{}, opts ...RowOpts) error
func (sw *StreamWriter) SetColWidth(min, max int, width float64) error
func (sw *StreamWriter) MergeCell(hcell, vcell string) error
```

`SetRowStyle` applies one style to every row in an inclusive range, writing `<row r="N" s="ID" customFormat="true">` for each (ECMA-376 Part 1 §18.3.1.73). Rows below the range are materialised so the range can be addressed but carry no style of their own, appearing as `<row r="1"></row>`. A reversed range is normalised. Row 0 is rejected, a row past `TotalRows` is `ErrMaxRows`, a negative style ID is rejected, and an absent sheet is rejected.

`StreamWriter.SetRow` gains a variadic `RowOpts` tail. The attributes are emitted only for non-zero and true fields, in a fixed order: `s` and `customFormat`, then `ht` and `customHeight`, then `hidden` — each boolean written as the literal `"true"`. An optionless row is exactly `<row r="1">`. A height above `MaxRowHeight` is `ErrMaxRowHeight`.

`StreamWriter.SetColWidth` writes into the sheet header, so it must be called before the first row: afterwards it is `ErrStreamSetColWidth`. `StreamWriter.MergeCell` accumulates into a single `<mergeCells count="N">` element (§18.3.1.55) and rejects a malformed corner.

#### 4.09.16 The pivot field layout flags

```go
type PivotTableField struct {
    Compact bool
    Outline bool
    // ... Data, Name, DefaultSubtotal and Subtotal are unchanged
}
```

`PivotTableField` gains two boolean members controlling how a field is laid out
in the rendered report. They are written straight through to `pivotField`
(ECMA-376 Part 1 §18.10.1.69) as its `@compact` and `@outline` attributes, and
both attributes are **always emitted, spelt out**: a zero-valued axis field
serialises as `<pivotField axis="axisRow" compact="false" outline="false"`, not
with the attributes omitted. The two are independent — `Compact` alone gives
`compact="true" outline="false"`.

Only a field placed on an axis carries them. A field in `Data` is written as a
bare `<pivotField dataField="true" showAll="false">`, and setting either member
there changes the serialised part not at all.

The rest of the struct is unchanged from milestone 06. `Name` renames the field
in place and `DefaultSubtotal` rides on the same element as the two new
members, in the attribute order `name`, `axis`, `compact`, `outline`,
`showAll`, `defaultSubtotal`; a row field with `DefaultSubtotal` set keeps its
`<items count="1"><item t="default"></item></items>` list. `Subtotal` and
`Name` on a data field still reach `dataField`, with the subtotal name
the token that subtotal function has: `<dataField name="Total" fld="2" subtotal="sum">`.

#### 4.09.17 Inherited behaviour that changes here

Nine contracts from milestones 02–08 move at this checkpoint. Everything else inherited from milestones 01–08 is unchanged and remains in scope.

**A numeric formula result is re-rendered to fifteen significant digits — but only when its exact rendering does not fit in fifteen decimal places.** The rule is conditional, and the condition is the part a reader has to get right. `CalcCellValue` first produces the **shortest decimal string that reproduces the double exactly**, and re-renders that string **only if it carries more than fifteen digits after the decimal point**. Re-rendering is to fifteen *significant* digits, with trailing zeros dropped. A string with fifteen or fewer digits after the point is returned **exactly as produced, unrounded**, even where that is sixteen or seventeen significant digits.

| Formula | Exact rendering | Places after the point | Expected result |
| --- | --- | --- | --- |
| `=SQRT(2)` | `1.4142135623730951` | 16 | `1.4142135623731` — re-rendered |
| `=10/3` | `3.3333333333333335` | 16 | `3.33333333333333` — re-rendered |
| `=1/7` | `0.14285714285714285` | 17 | `0.142857142857143` — re-rendered |
| `=PI()` | `3.141592653589793` | 15 | `3.141592653589793` — left alone, sixteen figures |
| `=EXP(1)` | `2.718281828459045` | 15 | `2.718281828459045` — left alone, sixteen figures |
| `=100/7` | `14.285714285714286` | 15 | `14.285714285714286` — left alone, **seventeen** figures |

**Do not round a result that already fits in fifteen decimal places.** Expectations carrying sixteen or seventeen significant digits are checked in several of this milestone's groups, including one of the inherited-regression gates, so rounding them is the most expensive mistake available here.

Because the trigger counts decimal *places* while the rounding is to significant *digits*, a result below one gains a place rather than losing one: `=EFFECT(0.05,4)` is `0.0509453369140624` — sixteen places, fifteen figures — where milestone 08 gave `0.050945336914062`. `=HARMEAN(1,2,4)` is superseded the same way and is checked at its new rendering here: it is `1.71428571428571`, where §4.08.5 records `1.714285714285714`. `=NOMINAL(0.05,4)` is `0.0490889377161574`. Neither function changed; only the last digits of the rendering moved. Both digit strings depend on the route taken to the value, and the route is fixed in §4.08.7: compute them from `(1 + rate/n)^n − 1` and `n · ((1 + rate)^(1/n) − 1)`, not through a `log1p`/`expm1` rewriting, which renders the last figure differently. That instruction is two milestones back and these renderings are pinned exactly in an inherited-regression group at the next checkpoint, so missing it means a whole step is not satisfied rather than one case failing. Trailing zeros are dropped, so a result that is exact once rounded sheds its accumulated binary tail and can finish well short of fifteen figures: the kurtosis of a linear sample is `-1.2` rather than `-1.199999999999999`, and `=SQRT(2)/1000` is `0.0014142135623731`. A result the evaluator renders in **exponent form** is never re-rendered at all, because the trigger does not treat such a string as numeric: `=2^70` is `1.1805916207174113e+21` and `=100000000/7` is `1.4285714285714285e+07`.

What moves at this checkpoint is the re-rendering, not the trigger. Milestone 08 fired on the same "more than fifteen decimal places" condition but rounded to the nearest multiple of 1e-15 instead, which is a different answer: there `=SQRT(2)` was `1.414213562373095` and `=10/3` was `3.333333333333334`. Results the trigger never touched read the same at both checkpoints — `=PI()`, `=100/7`, `=0.1+0.2` as `0.3` and `=1.1*1.1` as `1.21`.

Reading a **stored** numeric cell picks up a re-rendering of its own, at a lower threshold. `GetCellValue`, `GetRows`, `GetCols` and the two iterators apply the same fifteen-significant-digit rendering to an unstyled numeric cell whose stored text carries more than **ten** digits after the decimal point; milestone 08 applied none at all. A `float64` of `100/7` written with `SetCellValue` therefore reads back as `14.2857142857143`, while the same value computed by `=100/7` is `14.285714285714286`: the two call paths do not agree, and that is the Oracle's behaviour rather than an oversight. `Options{RawCellValue: true}` bypasses it and hands back the stored text `14.285714285714286` (§4.09.11).

**The coordinate helpers accept an absolute reference.** `SplitCellName("$A$1")` is `("A", 1, nil)` and `CellNameToCoordinates("$A$1")` is `(1, 1, nil)`, where milestone 08 rejected both. The other malformed forms are still rejected: `""`, `"A"`, `"1"`, `"A0"`, `"1A"`, `"$"`, `"A1$"`.

**`GetSheetIndex` folds case.** `"sheet1"`, `"SHEET1"` and `"ShEeT1"` all resolve to the index of `Sheet1`. A name that matches no sheet is still −1.

**`SHEET` takes an optional sheet name.** At milestone 08 it was nullary and any argument at all was `SHEET accepts no arguments`. It now accepts a name or a reference: `=SHEET("Second")` is `2`, `=SHEET()` is still the containing sheet, and a name that matches nothing is `#N/A`. A **reference** answers with the sheet it points into, not with the position of the cell — so from `Sheet1`, `=SHEET(A1)` and `=SHEET(C1)` are both `1` whatever those cells hold, and a qualified reference crosses over: `=SHEET(Second!A1)` is `2`.

**`RATE` is registered.** Milestone 08 had no rate solver and answered `not support RATE function`; the function is part of the financial family here (§4.09.7).

**A shared formula now populates its range and translates on read.** At milestone 04 (§4.04.2) `FormulaOpts{Type: &shared, Ref: &ref}` wrote `<f t="shared" ref="C1:C5">=A1+B1</f>` onto the master cell alone, and `GetCellFormula` on any other cell of the range returned that text unchanged. Here the whole range is materialised — each cell gets `t="shared"` and a library-assigned `si` counting up from zero per worksheet — and reading translates the formula to each cell's position. The `ref` is validated rather than stored verbatim, and a `dataTable` type is now dropped instead of written through to `@t`.

**`DataValidation.SetRange` takes two empty interfaces.** Milestone 02 (§4.02.6) declared it as `SetRange(f1, f2 int, t DataValidationType, o DataValidationOperator) error` and could express a bound only as a whole number. It is now `SetRange(f1, f2 interface{}, t DataValidationType, o DataValidationOperator) error` and dispatches on what each end holds, independently of the other. An `int` is rendered exactly, with no fractional part appended, so `SetRange(10, 20, ...)` leaves `Formula1` as `<formula1>10</formula1>`. A `float64` keeps its fraction: `<formula1>1.5</formula1>`. A `string` goes through verbatim, which is how a bound is expressed as a reference rather than a literal: `SetRange("=A1", "=A2", ...)` leaves `<formula1>=A1</formula1>`. The two ends need not agree, so `SetRange(10, "=A2", ...)` is accepted. Those three dynamic types are the whole of it, and the two ways a call can be refused have **different sentinels**. A `float64` end is additionally bounded by *single* precision: an end whose magnitude exceeds `math.MaxFloat32` is `ErrDataValidationRange`, so `SetRange(1e39, 10.0, ...)` is refused on the value. Anything that is none of the three types — an `int64`, a `bool`, a nil — is `ErrParameterInvalid`, so `SetRange(true, 10, ...)` is refused on the type. Either way the refused call leaves `Formula1`, `Formula2`, `Type` and `Operator` all untouched. §4.07's list of changes it does not cover records a widening of these bounds from `int` to `float64`, and that widening is real: the pair is `int` from milestone 02 through milestone 06, `float64` at milestones 07 and 08, and `interface{}` here. The `float64` form was declared out of scope where it arrived, so the `interface{}` form described above is the only one this ladder covers, and it is the form to implement at this checkpoint.

**The XML declaration no longer claims to be standalone.** Every written part now begins with the Go standard library's `xml.Header` — `<?xml version="1.0" encoding="UTF-8"?>` — where milestone 08 wrote `standalone="yes"` as well. The exported `XMLHeader` constant is removed along with it; `XMLHeaderByte` remains.

The milestone-08 quirks that do **not** move are still checked as they were, including `VLOOKUP`'s miss being a Go error, `CONCAT` refusing a range, `FIXED` having no two-decimal default, `T` returning a non-text argument's string form, `HARMEAN` reading scalars only, `OCT2DEC` accepting the digit 8, the five dotted ceiling and floor renames of §4.08.8, `OR` assigning over numeric arguments, `TRIM` not collapsing interior whitespace, `ISNONTEXT` answering "is this operand empty", `DATE` rendering a formatted instant rather than a serial, and `StreamWriter.Flush` leaving the in-memory worksheet unpopulated.
