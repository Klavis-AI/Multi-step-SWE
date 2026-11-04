# WORKSPACE_HANDOFF.md — what is in this workspace and where it came from

Read this once, then work from `TEST_INTERFACE.md`.

## The workspace

```
/app
├── *.go                     your implementation (you create these)
├── go.mod  go.sum  vendor/  frozen dependency set — do not modify
├── specs/                   the normative specifications (see below)
├── TEST_INTERFACE.md        the graded contract
├── WORKSPACE_HANDOFF.md     this file
└── timer.sh                 prints the time remaining in this milestone
```

Everything under `/app` at the end of a milestone is carried into the next one
exactly as you left it. That includes your source, so a later milestone starts
from your own previous answer, not from a clean tree.

## `specs/` — the normative documents

These are shipped verbatim as published; none has been edited or re-rendered.
`specs/MANIFEST.md` lists every file with its SHA-256 and its source URL.

| Directory | Document | Use it for |
| --- | --- | --- |
| `specs/ecma-376-part1/` | ECMA-376 Part 1, 5th ed. — Fundamentals and Markup Language Reference (PDF) plus the Strict XSD and RELAX NG schema sets | SpreadsheetML and DrawingML element and attribute definitions, built-in number-format IDs, the worksheet/workbook/styles part grammars |
| `specs/ecma-376-part2/` | ECMA-376 Part 2, 5th ed. — Open Packaging Conventions (PDF) plus its schemas | Part naming, `[Content_Types].xml`, relationship parts, package-level rules |
| `specs/ecma-376-part3/` | ECMA-376 Part 3, 5th ed. — Markup Compatibility and Extensibility (PDF) | `mc:AlternateContent`, extension lists, namespace compatibility |
| `specs/openformula/` | OASIS OpenDocument v1.3 Part 4 — Formula (HTML) | Formula grammar, the value lattice, error values, per-function `Semantics` clauses |
| `specs/ms-offcrypto/` | [MS-OFFCRYPTO] (PDF) | ECMA-376 standard encryption: the CFB container, `EncryptionInfo`, `EncryptedPackage`, key derivation |

The PDFs are large. `pdftotext -f <first> -l <last>` is installed; extract the
clauses you need rather than converting a document whole. The schema archives
are ordinary ZIPs and are the fastest way to settle an element or attribute
question.

## Rules

* **Do not modify the dependency set.** `go.mod`, `go.sum` and `vendor/` are
  frozen for the whole ladder. The grader builds with `-mod=vendor` and no
  network.
* **Do not create `/app/ooxmlverify/`.** That path belongs to the grader.
* **Do not delete the seeded documents or `specs/`.**
* **`timer.sh`** prints how much time is left in this milestone. Use it; a
  milestone that runs out of time is graded on whatever is on disk.
* Build with `go build ./...` before you stop. A milestone whose module does
  not compile scores zero on every group, including the ones it finished.

## Milestone log

Each entry below was written when that milestone started.

### Milestone 01 — OPC package and the SpreadsheetML writer

The workspace starts empty, so this milestone builds the whole document layer: a
`File` holding a workbook in memory, the OPC reader and writer around it, and the
worksheet features that package carries — typed cells and shared strings, styles,
merges, row and column geometry, tables and filters, conditional formats,
comments, pictures and charts. Nothing evaluates a formula yet.

Two conventions are easy to get wrong. The `sheet` argument of every worksheet
method names the worksheet *part* (`"Sheet1"`, `"Sheet2"`, in creation order),
not the display name — only `GetSheetName`, `GetSheetMap` and `GetSheetIndex`
deal in those. And almost nothing that mutates the workbook returns an error: a
setter ignores what it cannot act on, and a ZIP that is not a usable package
must open and read as empty rather than fail.

### Milestone 02 — Worksheet features and iteration

A streaming row iterator reads a sheet larger than memory; rows and columns carry outline levels; cells hold booleans and elapsed times; worksheets carry data validation, protection, view options, merged-range enumeration and a regular-expression search; pictures gain positioning and image hyperlinks; charts gain the 3D, stacked and 100%-stacked families and axes gain reversal and fixed bounds.

You inherit the whole of milestone 01 — the OPC container, the typed cell model, styles, row and column geometry — and none of it may change behaviour, with four named exceptions below.

The one thing most likely to be got wrong: **the interface is now keyed by worksheet name, not by `"sheet"` plus an index, and the row height is addressed by the one-based row number the markup itself uses.** `NewSheet` takes the new worksheet's name and returns its index; `SetRowHeight(sheet, 1, h)` must land on the row whose `r` attribute is `1`. The row-outline and row-visibility entry points keep the zero-based row index they already had — only the height moved. Five entry points keep the plain integer they already had — `CopySheet`, `SetActiveSheet`, `GetActiveSheetIndex`, `GetSheetName`, `GetSheetIndex` — because that integer was never the worksheet-identifying key.

The second named exception: a `bool` passed to `SetCellValue` now produces a `t="b"` cell reading back `"1"`/`"0"`, where 4.01 produced a plain string cell reading back `"true"`/`"false"`. Nothing else in the inherited cell, style or container model changes.

The third named exception is the column geometry: milestone 01 widened every row by one trailing empty cell on each `InsertCol` and never reclaimed it, so a three-column sheet read back five columns wide after one insert and four after the inserted column was removed. Here an insert adds exactly one column, repeated inserts do not accumulate, and `InsertCol` and `RemoveCol` are inverses — 4.02.12 has the widths. The matching row identity is inherited and unchanged.

The fourth named exception is a signature change, not a behaviour change, and it will stop your build rather than quietly give a wrong answer: five worksheet mutators you inherit from 4.01 now return an `error` when the JSON format descriptor they are given cannot be parsed. They are `AddChart`, `AddComment`, `AddShape`, `AddTable` and `SetConditionalFormat`, and 4.02.1 gives their new signatures. 4.01's rule that mutating methods do not report errors still holds for everything else it listed.

### Milestone 03 — Workbook metadata and the error contract

This milestone re-shapes the whole public surface: **every mutating method now
returns `error`, every fallible reader returns `(value, error)`, and every
row-addressed method takes a 1-based Excel row number.** Roughly forty
signatures change; the old non-error-returning forms are gone, not deprecated.
Plan for a sweep of your existing code before you add anything new.

On top of that the metadata around the grid arrives: document core properties
(`/docProps/core.xml`), defined names at workbook and worksheet scope, page
layout (orientation, paper size), headers and footers with odd/even/first
variants, per-column styles, a default-font override written through the style
tree, row duplication to an arbitrary target row, twenty-seven further chart
families, TIFF images, and a distinct diagnostic when the package turns out to
be encrypted.

You inherit everything from milestones 01 and 02 — the OPC container, the cell
model, sheets, styles, tables, filters, validation, drawings and comments — and
all of it is still graded, now through the new signatures.

The thing most often got wrong: treating the migration as mechanical. Failure
must actually be *reported*. An unknown worksheet, a malformed cell reference
and a row number below 1 each have to produce a non-nil error at every entry
point that can see them — while the handful of documented no-ops
(`DuplicateRowTo` onto an unusable target, `RemoveRow` past the last row,
`GetRows` on an unknown sheet) must stay `nil`. `TEST_INTERFACE.md` 4.03 lists
both sides; error *text* is never graded, error *identity* sometimes is.

### Milestone 04 — Pivot caches, sparklines and the macro part

This milestone adds three new feature surfaces — pivot tables with their cache
parts, sparkline extension elements, and the `xl/vbaProject.bin` macro part —
plus a variadic extension to `SetCellFormula` and two sheet-grouping methods.

Three things tests punish hardest: first, a substantial set of option fields
on both `PivotTableOption` and `SparklineOption` are **accepted and silently
ignored** — `PivotTableOption.Page` never reaches `pivotTable.go`, and
fourteen `SparklineOption` fields (`DateAxis`, `Max`, `CustMax`, `Min`,
`CustMin`, `Weight`, `Hidden`, `EmptyCells`, and the five colour fields other
than `SeriesColor`) produce byte-identical output whether set or not. Wire them
and your output diverges from the contract. Second, the `rowItems` element is a
fixed two-slot stub regardless of how many row fields are named, and a field
listed in both `Rows` and `Columns` ends up in both `rowFields` and
`colFields` even though its `pivotField/@axis` is `axisRow` only. (The
`cacheId` a cache gets is *not* graded — numbering the first cache `2` on a
fresh file, as this checkpoint's writer does, or `1` is equally fine; only
uniqueness and the match between the workbook entry and the table definition
are contractual.)

Third, outline levels become **bounded** here: `SetRowOutlineLevel` and
`SetColOutlineLevel` accept `1` to `7` and reject `0` or `8`, where milestones
01–03 stored whatever level they were handed.

You inherit everything from milestones 01–03; all prior graded behaviour
remains in scope.
