# Milestone 04 of 10 — Pivot caches, sparklines and the macro part

You are continuing `seedcalc`, a Go library for OOXML SpreadsheetML documents,
in `/app`. The workspace already holds everything the previous milestones left
behind: their source, the frozen dependency set, the specification corpus under
`/app/specs/`, and the two documents that define the contract this milestone is
specified against. This milestone adds one cumulative capability on top of that.

## 1. Implement this milestone

Milestone 04 adds four independently useful package-level surfaces that no earlier milestone wrote. First, pivot tables: a call to `AddPivotTable` writes a `pivotCacheDefinition` part, a `pivotTableDefinition` part, and their full relationship triple — worksheet-to-table, table-to-cache, and workbook-to-cache — together with the `pivotCaches` element in the workbook and correct content-type overrides. Row, column, data, and filter fields are placed from named source columns. Second, sparklines: `AddSparkline` embeds one sparkline group per call inside the worksheet's future-feature extension list (`extLst`/`ext` with the `ExtURISparklineGroups` URI) using the `x14` vocabulary, with per-sparkline data ranges and location references. Third, the `vbaProject.bin` macro part: `AddVBAProject` stores the binary verbatim, rewrites the workbook's content-type override to the macro-enabled variant, and adds the required `Default` and relationship entries. Fourth, worksheet grouping: `GroupSheets` and `UngroupSheets` set and clear `sheetView/@tabSelected` across named sheets. Alongside these, `SetCellFormula` becomes variadic so callers can attach `@t` and `@ref` formula attributes, `FitToWidth` and `FitToHeight` join the page-layout option pair, `ShowZeros` joins the sheet-view option pair, and font strike is settable in a style descriptor.

The change most likely to break earlier behaviour is the relationship-registry refactor: the three separate exported relationship maps from milestone 03 are replaced by a unified internal registry keyed by part name. Any code that touches the package's `_rels` parts — drawings, charts, comments, hyperlinks, and the new pivot and macro relationships — must still produce well-formed parts with resolved targets and unique relationship IDs after the refactor.

## 2. Read these local references

**Normative semantics:**

- `specs/ecma-376-part1/Ecma Office Open XML Part 1 - Fundamentals And Markup Language Reference.pdf` — sections 12.3.11–12.3.12: Pivot Table Part and Pivot Table Cache Definition Part (part names, content types, and the relationships each part participates in):
  `pdftotext -f 300 -l 700 "specs/ecma-376-part1/Ecma Office Open XML Part 1 - Fundamentals And Markup Language Reference.pdf" -`
- Same Part 1 — sections 18.2.7 (`ext`), 18.2.10 (`extLst`), 18.2.17 (`pivotCache`), 18.2.18 (`pivotCaches`): the workbook elements that register each cache and the extension-list host for sparklines:
  `pdftotext -f 1850 -l 2150 "specs/ecma-376-part1/Ecma Office Open XML Part 1 - Fundamentals And Markup Language Reference.pdf" -`
- Same Part 1 — sections 18.3.1.40 (`f`, formula element and its `@t`/`@ref` attributes), 18.3.1.63 (`pageSetup`, `@fitToWidth`/`@fitToHeight`), 18.3.1.82 (`@codeName`), 18.3.1.87 (`sheetView`, `@tabSelected`/`@showZeros`), 18.3.1.88 (`sheetViews`), 18.18.1 (ST_Axis), 18.18.6 (ST_CellFormulaType):
  `pdftotext -f 2150 -l 2850 "specs/ecma-376-part1/Ecma Office Open XML Part 1 - Fundamentals And Markup Language Reference.pdf" -`
- Same Part 1 — sections 18.4.10 (`strike`), 18.8.2 (`b`, bold), 18.8.22 (`font` record in `xl/styles.xml`), 18.8.10 (`cellXfs`), 18.8.45 (`xf`): font child elements and how a cell format references a font record:
  `pdftotext -f 2850 -l 3600 "specs/ecma-376-part1/Ecma Office Open XML Part 1 - Fundamentals And Markup Language Reference.pdf" -`
- Same Part 1 — section 18.10 (the complete pivot-table vocabulary): `pivotCacheDefinition` 18.10.1.67, `pivotTableDefinition` 18.10.1.73, `cacheSource` 18.10.1.7, `worksheetSource` 18.10.1.95, `cacheField` 18.10.1.3, `cacheFields` 18.10.1.4, `sharedItems` 18.10.1.90, `location` 18.10.1.49, `pivotTableStyleInfo` 18.10.1.74, `pivotFields` 18.10.1.70, `pivotField` 18.10.1.69, `rowFields` 18.10.1.81, `colFields` 18.10.1.14, `dataFields` 18.10.1.23, `dataField` 18.10.1.22, `field` 18.10.1.29, `rowItems` 18.10.1.84, `item` 18.10.1.45, `items` 18.10.1.46:
  `pdftotext -f 3600 -l 4400 "specs/ecma-376-part1/Ecma Office Open XML Part 1 - Fundamentals And Markup Language Reference.pdf" -`
- `specs/ecma-376-part2/Ecma Office Open XML Part 2 - Open Packaging Conventions.pdf` — section 6.4 (resolving relative part references from a base URI), section 6.5.3 (relationship type attribute), sections 7.2.3.2.4–7.2.3.2.5 (`Default` and `Override` content-type elements):
  `pdftotext -f 35 -l 95 "specs/ecma-376-part2/Ecma Office Open XML Part 2 - Open Packaging Conventions.pdf" -`
- `specs/ecma-376-part3/Ecma Office Open XML Part 3 - Markup Compatibility and Extensibility.pdf` — clause 8 (application-defined extension elements; this is the mechanism sparklines use via `extLst`/`ext`):
  `pdftotext -f 1 -l 60 "specs/ecma-376-part3/Ecma Office Open XML Part 3 - Markup Compatibility and Extensibility.pdf" -`

**Additional files:**

- `/app/TEST_INTERFACE.md` section 4.04 (every method signature, constant value, struct field, and behavioural rule for this milestone) and its inherited sections 4.01–4.03 (package and cell model, worksheet features, workbook metadata and error contract — all still in scope)
- `/app/WORKSPACE_HANDOFF.md` — the workspace state at the start of this milestone: source files in place, `go.mod`/`go.sum`/`vendor/` frozen, and any notes from milestone 03 about gaps or partial work

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
