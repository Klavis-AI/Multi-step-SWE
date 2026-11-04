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
appear. A sheet renamed to `"Summary"` is still reached as `"Sheet1"`.

**Mutating methods do not report errors.** With the exceptions listed
explicitly below (`NewStyle`, `NewConditionalStyle`, `AddPicture`, `AutoFilter`,
`CopySheet`, `SetSheetBackground`, `Write`, `Save`, `SaveAs`), a method that
changes the workbook returns nothing. An argument it cannot act on is ignored,
not rejected, and the workbook is left as it was. No method may panic on any
input described here.

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
read, not a shape a written boolean has to take.

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
created, and the call returns nothing to signal it. An `axis` inside a merged
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
