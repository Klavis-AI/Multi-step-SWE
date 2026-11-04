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
write/read round trip unchanged.

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
  formula and `GetCellValue` the cached result — or a cell written by
  `SetCellStr`, whose text sits in `v` with no `f` element.

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
`SetDropList` refuses a quoted literal longer than 257 characters, and
`SetRange` refuses when the rule's existing `Formula1` or `Formula2` is already
longer than that.

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
  `42724228`. On failure it returns `-1` and a non-nil error.
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
