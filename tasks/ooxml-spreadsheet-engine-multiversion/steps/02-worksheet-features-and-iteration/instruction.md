# Milestone 02 of 10 — Worksheet features and iteration

You are continuing `seedcalc`, a Go library for OOXML SpreadsheetML documents,
in `/app`. The workspace already holds everything the previous milestones left
behind: their source, the frozen dependency set, the specification corpus under
`/app/specs/`, and the two documents that define the contract this milestone is
specified against. This milestone adds one cumulative capability on top of that.

## 1. Implement this milestone

This milestone turns a worksheet from something you write one cell at a time
into something you can stream, group, constrain, protect, search and draw on,
and it re-keys the library to the worksheet's display name.

- **Streaming row iteration.** An iterator that walks a worksheet row by row
  without materialising it, handing back each row's values positioned by column
  letter and distinguishing a read failure from ordinary exhaustion, plus a
  typed error for a worksheet the workbook does not have. Reading widens too:
  cells whose text lives in the cell itself, and members of a shared-formula
  group that carry only a group index.
- **Outline structure and worksheet properties.** Row and column outline levels
  written on exactly the row or column addressed, and a set-and-read option
  surface over the worksheet's own properties, including the direction a
  group's summary sits in.
- **Data validation.** Drop lists, numeric and decimal ranges with typed
  comparison operators, input and error prompts, and an accumulating reference
  sequence, serialised into the worksheet's validation block.
- **A wider cell model.** Booleans; a `time.Duration` stored as a fraction of a
  day under an elapsed-time format; whole-row writes from a slice; cell-level
  protection carried by a style.
- **Protection, search and a second save path.** Protection whose stored
  password is never the plaintext, its removal, exact and regular-expression
  cell search returning coordinates, merged-range enumeration, and
  `io.WriterTo` beside a buffer writer and a raw container reader.
- **Breadth.** Worksheet view options, RGB/HSL and theme-tint colour
  conversion, the 3D, stacked and 100%-stacked bar, column and area charts with
  axis reversal and fixed bounds, and picture positioning, image hyperlinks and
  comment retrieval.

The migration is what puts inherited behaviour at risk: worksheets are
addressed by the name they display rather than by their part name, sheet
creation takes a name and returns an index, several descriptor-taking mutators
begin returning an error, and row heights move to the one-based row number the
markup uses while row visibility and row outline levels keep their zero-based
index. Everything milestone 01 established must behave exactly as before once
reached by the new key.

## 2. Read these local references

**Normative semantics.** These are under `/app/specs/`. The page numbers below
are physical PDF pages, which is what `pdftotext` counts; they run about ten
ahead of the printed page numbers in the table of contents. Extract a clause
rather than converting a document whole, and quote the path, because the
filenames contain spaces:
`pdftotext -f 1629 -l 1631 "specs/ecma-376-part1/Ecma Office Open XML Part 1 - Fundamentals And Markup Language Reference.pdf" -`

- `specs/ecma-376-part1/Ecma Office Open XML Part 1 - …Reference.pdf` §18.3.1.73 `row` (pp. 1685-1688) and §18.3.1.13 `col` (pp. 1610-1612) — the height, custom-height, hidden and outline-level attributes, and the `min`/`max` span of a column record.
- …Part 1 §18.3.1.53 `is` (p. 1656) and §18.18.11 `ST_CellType` (p. 2451) — where an inline string's text lives and the cell-type token that selects it.
- …Part 1 §18.3.1.40 `f` (pp. 1640-1642) — the formula element, its type attribute, and the `ref` and `si` attributes that define and join a shared-formula group.
- …Part 1 §18.3.1.32 `dataValidation` and §18.3.1.33 `dataValidations` (pp. 1629-1631) — validation types, operators, error styles, prompt attributes, the `sqref` list and the `formula1`/`formula2` children.
- …Part 1 §18.3.1.82 `sheetPr` (pp. 1696-1698) and §18.3.1.61 `outlinePr` (p. 1663) — worksheet properties, the page-setup sub-element and the outline summary direction.
- …Part 1 §18.3.1.85 `sheetProtection` (pp. 1701-1706) — the protection element and the permission attributes it carries.
- …Part 1 §18.3.1.87 `sheetView` (pp. 1708-1712) and §18.3.1.88 `sheetViews` (pp. 1712-1714) — grid, header, formula, right-to-left, top-left-cell and zoom attributes. Note §18.3.1.86 is the *chart-sheet* view and is not the one you want.
- …Part 1 §18.8.33 `protection` (pp. 1802-1803) — the locked/hidden pair that a cell format applies.
- …Part 1 §18.8.19 (p. 1775) for the `tint` attribute's meaning and §18.3.1.15 (pp. 1613-1614) for the ARGB form of a colour value.
- …Part 1 §20.5.2.33 `twoCellAnchor` (pp. 3189-3190) — the anchor and its `editAs` attribute; §21.1.2.3.5 `hlinkClick` (pp. 3251-3253) — the click-hyperlink property on a drawing object.
- …Part 1 chart clauses: §21.2.2.16 `barChart`, §21.2.2.15 `bar3DChart`, §21.2.2.17 `barDir` (p. 3386), §21.2.2.5 `areaChart` (p. 3383), §21.2.2.4 `area3DChart` (p. 3382), §21.2.2.77 `grouping` (pp. 3406-3407), §21.2.2.160 `scaling` (p. 3432), §21.2.2.107 `max` (p. 3414), §21.2.2.108 `min` (p. 3415), §21.2.2.130 `orientation` (pp. 3420-3421).
- `specs/ecma-376-part2/Ecma Office Open XML Part 2 - Open Packaging Conventions.pdf` §6.2 part names (pp. 20-21), §6.5 relationship parts and the `Relationship` element with its target mode (pp. 30-36), §7.2.3 the media-types stream (pp. 37-41) — the container rules a re-keyed worksheet must not disturb, and the external target mode an image hyperlink needs.
- `specs/ecma-376-part1/OfficeOpenXML-XMLSchema-Strict.zip` — an ordinary ZIP and the fastest way to settle an attribute's name, type or required order: `sml.xsd` for the worksheet, validation, protection, view and style types; `dml-spreadsheetDrawing.xsd` for anchors; `dml-chart.xsd` for plot, grouping and scaling elements; `dml-main.xsd` for the hyperlink property. `OfficeOpenXML-RELAXNG-Strict.zip` carries the same grammar in RELAX NG.

**Additional files.**

- `/app/TEST_INTERFACE.md` section 4.02 — the required interface for this milestone. Read 4.02.1 before the rest of it: every other subsection is written in terms of the addressing change it states. 4.02.12 repeals an inherited geometry contract rather than extending it, and 4.02.13 lists what is left untouched.
- `/app/TEST_INTERFACE.md` section 4.01, which is inherited and still in scope in full — in particular 4.01.0, whose two conventions this milestone partly overturns, and 4.01.6 (worksheets), 4.01.8 (row and column geometry, whose column-insert behaviour 4.02.12 corrects), 4.01.1 (opening and saving), 4.01.3 and 4.01.4 (cell values and stored types), 4.01.7 (merged cells), 4.01.9 (drawings and comments) and 4.01.10 (packages that are not well formed).
- `/app/TEST_INTERFACE.md` sections 2 and 3 — the fixed facts about the module and how milestone requirements are organised.
- `/app/WORKSPACE_HANDOFF.md` — the workspace map, the table of what each specification is good for, and the milestone log, whose entry for this milestone names the addressing trap explicitly.
- `/app/specs/MANIFEST.md` — every shipped specification with its SHA-256 and source URL, if you need to confirm which edition you are reading.
- Your own milestone 01 sources in `/app`, and `/app/vendor/modules.txt` for the exact set of packages you are able to import.

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
