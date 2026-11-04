# Milestone 03 of 10 — Workbook metadata and the error contract

You are continuing `seedcalc`, a Go library for OOXML SpreadsheetML documents,
in `/app`. The workspace already holds everything the previous milestones left
behind: their source, the frozen dependency set, the specification corpus under
`/app/specs/`, and the two documents that define the contract this milestone is
specified against. This milestone adds one cumulative capability on top of that.

## 1. Implement this milestone

This milestone gives `seedcalc` a uniform failure contract and the workbook
metadata that lives outside the cell grid.

The failure contract is the substance of the step. Every mutating entry point
gains an `error` result and every fallible reader a second `(value, error)`
result; the older result-free spellings are replaced, not kept alongside, so
plan a sweep of the inherited sources first. A call that cannot complete — an
unknown worksheet, a malformed A1 reference, a column name that is not letters
— must say so with a non-nil error and leave the workbook as it found it,
rather than panicking or quietly doing nothing. A short documented set of
calls is exempt, or is a deliberate no-op; `TEST_INTERFACE.md` 4.03 lists both
sides, and a wrong no-op costs as much as a missing error. Row addressing
becomes Excel's own: every row-addressed method takes a 1-based row number,
there is no row 0, and a small family of A1/coordinate helpers becomes the
single definition of a well-formed reference.

The metadata arriving on top of that:

* document core properties in `/docProps/core.xml`, merged rather than replaced
  on write;
* defined names at workbook and worksheet scope, keyed by worksheet name;
* a default-font override applied through the style tree so it persists;
* page layout, and headers and footers with odd, even and first variants;
* per-column styles over a column or a normalised range;
* row duplication to an arbitrary target row, inserting rather than
  overwriting;
* twenty-seven further chart families — cone, pyramid, cylinder, surface, contour,
  bubble — with an error for an unrecognised type, TIFF media, and a diagnostic
  that tells an encrypted package apart from a corrupt one.

Because the sweep touches every call site, the behaviour those sites produce
is what is most likely to drift. Cell values and stored types, formulas,
merged ranges, styles and number formats, tables, filters, validation,
drawings, comments, panes, sheet views and protection must behave as before
through the new result lists; bulk and streaming row access must still agree
row for row; and the saved package must still be a valid OPC container.

## 2. Read these local references

Read a PDF clause with `pdftotext -f <first> -l <last> 'specs/<file>.pdf' -`.
Quote the paths — the ECMA file names contain spaces. The numbers below are
**PDF page numbers**, which in the two ECMA PDFs run ten ahead of the page
number printed on the page.

**Normative semantics:**

* `specs/ecma-376-part1/Ecma Office Open XML Part 1 - Fundamentals And Markup Language Reference.pdf`, 18.17.2.3 *Cell References*, pages 2059–2065 — the `A1-reference` production, which fixes what a well-formed cell name is and therefore what every reference-validating entry point must reject.
* the same PDF, 18.3.1.73 `row` (`@r`), pages 1685–1691 — the Excel row number that the 1-based API mirrors.
* the same PDF, 18.2.5 `definedName` and 18.2.6 `definedNames` (`@name`, `@localSheetId`, `@comment`, element text), pages 1563–1569.
* the same PDF, 18.3.1.13 `col` (`@min`, `@max`, `@style`), pages 1610–1612 — per-column styling and range normalisation.
* the same PDF, 18.3.1.46 `headerFooter` (`@alignWithMargins`, `@differentFirst`, `@differentOddEven`, `@scaleWithDoc` and the six child elements), pages 1646–1649.
* the same PDF, 18.3.1.63 `pageSetup` (`@orientation`, `@paperSize`), pages 1665–1671.
* the same PDF, 18.8.22 `font` and 18.8.29 `name`, pages 1778–1786 — the style-sheet font entry the default-font override rewrites.
* the same PDF, 21.2.2.15 `bar3DChart`, 21.2.2.17 `barDir`, 21.2.2.19 `bubble3D`, 21.2.2.20 `bubbleChart`, pages 3386–3388; 21.2.2.76 `grouping`, page 3405; 21.2.2.177 `shape`, page 3436; 21.2.2.203 `surface3DChart` and 21.2.2.204 `surfaceChart`, page 3445; 21.2.2.230 `wireframe`, page 3451 — the chart elements the new families must emit.
* `specs/ecma-376-part2/Ecma Office Open XML Part 2 - Open Packaging Conventions.pdf`, clause 8 *Core properties* — 8.2 *Core Properties part* and 8.3.4 *Core property elements* — pages 47–53: the Dublin Core and `cp:` vocabulary of `/docProps/core.xml`.
* the same PDF, 7.2.3 *Mapping media types to parts*, pages 37–41 — the `Default` element that a new media extension needs in `[Content_Types].xml`; and, for the package that must stay well formed, 6.2.2 *Part names*, pages 20–23, and 6.5 *Relationships*, pages 29–33.
* `specs/ms-offcrypto/MS-OFFCRYPTO.pdf`, 2.1.5 `DataSpaceVersionInfo` — whose `FeatureIdentifier` is the `Microsoft.Container.DataSpaces` string — and 2.1.6 `DataSpaceMap`, pages 23–26; 2.2, the `\0x06DataSpaces` storages, pages 29–32; 2.3.4 *ECMA-376 Document Encryption*, page 38. Only recognition is needed here, not decryption.
* Schemas, faster than the PDFs for settling an element or attribute question. They are ordinary ZIPs: `unzip -p specs/ecma-376-part1/OfficeOpenXML-XMLSchema-Strict.zip sml.xsd`. Use `sml.xsd` for `CT_Row`, `CT_Col`, `CT_HeaderFooter`, `CT_PageSetup`, `CT_DefinedName` and `CT_Font`; `dml-chart.xsd` in the same archive for `CT_Bar3DChart`, `CT_BubbleChart`, `CT_SurfaceChart` and `CT_Surface3DChart`; and `opc-coreProperties.xsd` and `opc-contentTypes.xsd` in `specs/ecma-376-part2/OpenPackagingConventions-XMLSchema.zip`.

**Additional files:**

* `/app/TEST_INTERFACE.md` section **4.03** — the authority on every signature, constant, default and no-op this milestone adds. Read 4.03.1 first: the rest of the section assumes it.
* `/app/TEST_INTERFACE.md` sections **4.01** and **4.02**, which 4.03 re-signs rather than repeals. In particular 4.01.0 (conventions that govern every signature), 4.01.2 (column titles and A1 references), 4.01.3–4.01.5 (cell values, stored cell types, styles), 4.01.8 (rows, columns and their geometry), 4.01.9 (tables, filters, conditional formats, comments, drawings), 4.01.10 (packages that are not well formed), 4.02.1 (the name-keyed interface), 4.02.2 (the row iterator), 4.02.5–4.02.8 (outline levels and worksheet properties, data validation, protection and search, sheet-view options), 4.02.10–4.02.11 (pictures and comments, charts), 4.02.12 (the corrected column geometry) and 4.02.13 (what is unchanged).
* `/app/WORKSPACE_HANDOFF.md` — the workspace map, the table telling you which specification answers which kind of question, and the log entry written when this milestone started.
* `/app/specs/MANIFEST.md` — every shipped document with its SHA-256 and the URL it came from, so you can confirm you are reading the edition the contract cites.
* The Go sources you inherited at the repository root. The signature change reaches nearly all of them, so enumerate the current exported surface before you add anything: a method you forget to re-sign is a compile error at best and a silently unreported failure at worst.

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
