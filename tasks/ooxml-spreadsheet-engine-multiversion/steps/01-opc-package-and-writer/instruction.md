# Milestone 01 of 10 — OPC package and the SpreadsheetML writer

You are continuing `seedcalc`, a Go library for OOXML SpreadsheetML documents,
in `/app`. The workspace already holds everything the previous milestones left
behind: their source, the frozen dependency set, the specification corpus under
`/app/specs/`, and the two documents that define the contract this milestone is
specified against. This milestone adds one cumulative capability on top of that.

## 1. Implement this milestone

This is the first milestone: the workspace holds no Go source yet, so the whole
document layer arrives here. When you are done, `seedcalc` can build a
SpreadsheetML workbook in memory, serialise it as an OPC package, and read one
back. Three layers land together.

* **The package.** A ZIP container carrying a content-types stream with its
  default and override declarations, a package relationships part and per-part
  `_rels` parts with allocated relationship identifiers, and the SpreadsheetML
  parts themselves — workbook, worksheets, shared strings, styles — under their
  conventional part names. Reading is deliberately total: a ZIP that is not a
  usable SpreadsheetML package opens as an empty workbook rather than failing,
  and a dangling worksheet relationship is survivable; only input that is not a
  ZIP container at all is refused.
* **The document model.** A typed cell — number, shared string, boolean, error,
  and formula text with an optional cached result — addressed by
  an A1-style axis, plus conversion in both directions between a column ordinal
  and its base-26 column title. Values are rendered through the number format
  their style selects, so a read returns the text a spreadsheet application
  would display.
* **The worksheet features a document carries.** Named styles over fonts, fills,
  borders, alignment and number formats, applied to cell ranges; merged ranges;
  row heights, column widths, visibility, and row and column insertion and
  deletion; tables with their own parts and auto filters; conditional formats
  including two- and three-colour scales; comments with their legacy VML
  drawing; pictures, shapes and the first chart families with their DrawingML
  parts; cell hyperlinks (external and in-workbook) and a worksheet's frozen or
  split pane state with its per-pane selections; and sheet creation, renaming,
  copying, hiding and deletion with the active tab recomputed.

Nothing evaluates a formula at this milestone — a formula is stored text.

Because everything is new, the behaviour most at risk is internal to the
milestone: each part you add later must leave the package still readable, with
every earlier value intact, and must not disturb the content-type and
relationship declarations the workbook already depends on. Treat the
write-then-read round trip as the invariant you re-check after every feature.

## 2. Read these local references

**Normative semantics:**

* `specs/ecma-376-part2/Ecma Office Open XML Part 2 - Open Packaging Conventions.pdf`
  (5th ed., 95 pages) — the package. §6.2.2 part names and §6.2.3 media types
  (pp. 20–23); §6.5 relationships, where §6.5.2 fixes the package relationships
  part name `/_rels/.rels` and derives `/xl/_rels/workbook.xml.rels` from
  `/xl/workbook.xml`, and §6.5.3 fixes the `Relationships`/`Relationship` markup
  with its `Id`, `Type` and `Target` attributes (pp. 29–36); §7.2.3 the Media
  Types stream — the `Types`, `Default` and `Override` elements (pp. 37–41);
  §7.3 the mapping onto a ZIP file, with §7.3.7 fixing the
  `[Content_Types].xml` item name (pp. 43–46). This edition renumbered the
  standard: its clauses 9 and 10 are Thumbnails and Digital Signatures, neither
  of which applies here.
* `specs/ecma-376-part1/Ecma Office Open XML Part 1 - Fundamentals And Markup Language Reference.pdf`
  (5th ed., 5,039 pages) — SpreadsheetML. Use these clauses:
  * §12.2 package structure and §12.3 part summary give each part's content
    type and relationship type: §12.3.3 Comments, §12.3.8 Drawings, §12.3.15
    Shared String Table, §12.3.20 Styles, §12.3.21 Table Definition, §12.3.23
    Workbook, §12.3.24 Worksheet (pp. 70–108). §14.2.1 Chart Part (p. 133) and
    §15.2.14 Image Part (p. 163) cover the rest.
  * Workbook part: §18.2.27 `workbook`, §18.2.20 `sheets`, §18.2.19 `sheet`,
    and §18.2.30 `workbookView`, which is where `activeTab` lives (pp.
    1551–1600).
  * Worksheet part, all under §18.3.1 (pp. 1600–1725): §18.3.1.99 `worksheet`
    for the child-element order, §18.3.1.4 `c` with its `r`, `s` and `t`
    attributes, §18.3.1.73 `row`, §18.3.1.17 `cols` and §18.3.1.13 `col`,
    §18.3.1.55 `mergeCells`, §18.3.1.2 `autoFilter`, §18.3.1.18
    `conditionalFormatting` with §18.3.1.10 `cfRule`, §18.3.1.11 `cfvo` and
    §18.3.1.16 `colorScale`, §18.3.1.87 `sheetView` for `tabSelected`, §18.3.1.47 `hyperlink`
    and §18.3.1.48 `hyperlinks`, §18.3.1.66 `pane` and §18.3.1.78 `selection`,
    §18.3.1.95 `tableParts`, §18.3.1.36 `drawing`, §18.3.1.67 `picture` for a
    sheet background.
  * Cell types and references: §18.18.11 `ST_CellType` enumerates the `t` values
    (p. 2451) and §18.18.7 `ST_CellRef` the cell reference. §18.17.2.3.1,
    A1-Style Cell References (pp. 2061–2064), defines the column-letter sequence
    A–Z, AA–AZ, …, ZZ, AAA–… and the one-based row number; that is the rule the
    column-title conversion implements.
  * Shared strings: §18.4 Shared String Table — §18.4.9 `sst`, §18.4.8 `si`,
    §18.4.4 `r` and §18.4.12 `t` for runs and preserved whitespace (pp.
    1725–1735).
  * Styles: §18.8 (pp. 1760–1810) — §18.8.39 `styleSheet`, §18.8.10 `cellXfs`,
    §18.8.45 `xf`, §18.8.22 `font`, §18.8.32 `patternFill`, §18.8.4 `border`,
    §18.8.1 `alignment`, §18.8.14 `dxf` and §18.8.15 `dxfs`. §18.8.30 `numFmt`
    carries the table of built-in format identifiers and their codes.
  * Tables: §18.5.1.2 `table`, §18.5.1.4 `tableColumns`, §18.5.1.5
    `tableStyleInfo` (pp. 1734–1747). Comments: §18.7 — §18.7.2 `authors`,
    §18.7.4 `commentList`, §18.7.3 `comment` (pp. 1753–1761).
  * Drawings: §20.5, DrawingML — SpreadsheetML Drawing (pp. 3165–3194), with
    §20.5.2 covering `twoCellAnchor`, `pic`, `sp` and `graphicFrame`.
  * The VML shape behind a comment was dropped from this edition's Strict
    schema, so the worked example in Annex L.5.5.2, SpreadsheetML Comments (pp.
    4986–4988), is the reference: it shows the worksheet's `legacyDrawing`
    element and its `legacyDrawingN.vml` target. Annex L.5 (from p. 4974)
    introduces VML.
  * Annex L.2, Introduction to SpreadsheetML (pp. 4643–4690), is informative but
    much the fastest orientation: L.2.1.2 the minimum workbook, L.2.2.1 the
    minimum worksheet, L.2.2.13 merged cells, L.2.2.14 conditional formatting,
    L.2.3 the shared string table, L.2.4 tables.
* Read a page range rather than a whole document:
  `pdftotext -f <first> -l <last> "specs/<dir>/<file>.pdf" -` . The page numbers
  above are PDF pages, which run about ten ahead of the printed page numbers in
  Part 1.
* `specs/ecma-376-part1/OfficeOpenXML-XMLSchema-Strict.zip` — `sml.xsd` settles
  a child-element order or attribute question far faster than the PDF;
  `dml-spreadsheetDrawing.xsd` is the anchor grammar and `dml-chart.xsd` the
  plot elements. `unzip -p <zip> sml.xsd` reads one entry without extracting.
* `specs/ecma-376-part2/OpenPackagingConventions-XMLSchema.zip` —
  `opc-contentTypes.xsd` and `opc-relationships.xsd`.
* Both 5th-edition PDFs and the Strict schemas print namespace and
  relationship-type URIs under `purl.oclc.org`. The packages you must produce
  use the transitional `schemas.openxmlformats.org` forms; `TEST_INTERFACE.md`
  gives the exact strings and those are the ones required.
* `specs/MANIFEST.md` identifies every shipped document and its source.
  `specs/openformula/`, `specs/ms-offcrypto/` and ECMA-376 Part 3 govern later
  milestones; nothing in this one needs them.

**Additional files:**

* `/app/TEST_INTERFACE.md` — §4.01 is this milestone's contract. Read §4.01.0
  first: it states two conventions that govern every signature in the section,
  and both are easy to get wrong. Sections §1–§3 (what you are building, the
  fixed workspace facts, and how milestone requirements are organised) apply to
  every milestone and are worth reading once; there are no earlier `4.NN` sections to inherit.
* `/app/WORKSPACE_HANDOFF.md` — the workspace layout, what each `specs/`
  directory is for, the standing rules, and the milestone log to append to.
* `/app/go.mod` and `/app/vendor/` — the module path and the exact frozen
  dependency set. Anything not vendored there you write yourself.
* `/app/timer.sh` — time remaining in this milestone.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Continue cumulatively in `/app`. Everything earlier milestones required is
  still required — and from milestone 02 onward it is enforced as a **gate**:
  breaking one inherited behaviour means this whole milestone is not satisfied,
  however much of the new capability you finished, while preserving all of them
  is a prerequisite rather than the milestone's work. `TEST_INTERFACE.md` §3 has the
  model. Preserve the inherited source, `go.mod`, `go.sum`, `vendor/`,
  `specs/`, `TEST_INTERFACE.md` and `WORKSPACE_HANDOFF.md`; append to the
  handoff rather than rewriting it.
- The dependency set is frozen. Do not edit `go.mod` or `go.sum`, do not run
  `go get` or `go mod tidy`, and do not add a vendored package: the checker
  builds with `-mod=vendor` and the network off, so any change here fails the
  whole-module build, and every requirement in this milestone depends on that
  build.
- `go build ./...` must succeed before you stop. The whole-module build is a
  gate: it satisfies nothing on its own, and no other requirement can be met
  while it fails.
- Do not create a directory named `ooxmlverify/` at the repository root; the
  checker plants its own there.
- This environment has no network access, and everything the build needs is already installed. The upstream project's source, its releases, mirrors and package registries are unreachable, so a copy cannot be fetched by any route. Do not search for, download, copy, wrap, or
  invoke an online implementation or task-specific answer; do not inspect
  `/tests`, later-step directories, or solution material.
- Do not edit the supplied documents (`TEST_INTERFACE.md`, `specs/`) or weaken, filter, rename, or disable
  tests. Your own tests are out of scope and are not required; if you write
  any, keep them building.
