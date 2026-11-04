# Milestone 05 of 10 — The streaming writer and the drawing registry

You are continuing `seedcalc`, a Go library for OOXML SpreadsheetML documents,
in `/app`. The workspace already holds everything the previous milestones left
behind: their source, the frozen dependency set, the specification corpus under
`/app/specs/`, and the two documents that define the contract this milestone is
specified against. This milestone adds one cumulative capability on top of that.

## 1. Implement this milestone

This milestone introduces the streaming write path — a low-memory alternative to the
buffered cell-by-cell API for writing large worksheets — and a suite of targeted
editing operations on drawings, page margins, defined names, merge regions, and column
visibility.

`NewStreamWriter` returns a `StreamWriter` bound to a named sheet. `SetRow` writes one
row at a time in A1 notation, accepting typed scalars or `Cell` structs that carry an
optional style ID. `AddTable` may be called after all row writes and before `Flush`,
which closes the session and materialises the sheet content. The streamed worksheet part
must be structurally identical to the output of the buffered path for the same data;
worksheet settings established before streaming begins — column widths, merge regions,
sheet views — are preserved across `Flush`.

`AddChart` gains a variadic final parameter so that one or more additional chart format
objects can be combined into one chart part, producing a combo chart while keeping
three-argument calls valid.

Two drawing-deletion methods, `DeleteChart` and `DeletePicture`, remove anchors from a
sheet's drawing part by matching the anchor's `from` address to the given cell. Both
methods are no-ops when no drawing part exists and return a non-nil error for a
syntactically invalid cell reference.

Variadic `SetPageMargins` and `GetPageMargins` take one option per margin, in the
option-pair idiom the library already uses for the sheet-property and page-layout
options, so a call names only the margins it wants and leaves the others alone.
`GetPageMargins` returns the OOXML defaults when no `<pageMargins>` element is present in
the worksheet.

`DeleteDefinedName` removes a workbook- or sheet-scoped defined name. The sharpest
correctness trap here is scope asymmetry: `GetDefinedName` returns `Scope: "Workbook"`
for workbook-level names, but `DeleteDefinedName` computes scope via `GetSheetName` on a
nil `LocalSheetID`, which returns the empty string. Callers deleting a workbook-scoped
name must pass `Scope: ""`.

`UnmergeCell` removes every merge region that geometrically overlaps a given rectangle.
`SetColVisible` now accepts a column range such as `"D:F"`. `CharsetTranscoder` sets a
custom charset reader on the file and returns the file for chaining.

All behaviour from milestones 01–04 remains in scope.

## 2. Read these local references

**Normative semantics:**

- `specs/ecma-376-part1/Ecma Office Open XML Part 1 - Fundamentals And Markup Language Reference.pdf`
  — §18.2.5 `definedName` and §18.2.6 `definedNames` (workbook defined names; the
  `@localSheetId` attribute that scopes a name to one sheet);
  §18.3.1 Worksheet Elements, specifically §18.3.1.4 `c`, §18.3.1.73 `row`,
  §18.3.1.13 `col`, §18.3.1.55 `mergeCells` and §18.3.1.54 `mergeCell`,
  §18.3.1.62 `pageMargins`, §18.3.1.80 `sheetData`; §18.5 Tables (§18.5.1.2 `table`
  element structure, §18.5.1.3 `tableColumn` column name derivation, minimum row
  count); §20.5 SpreadsheetML Drawing (anchor types, `from`
  address, `<c:chart>` and `<xdr:pic>` elements, one drawing part per sheet); §21.2
  DrawingML Charts (multiple chart elements within a single chart part for combo charts).
  Extract a section range with:
  `pdftotext -f <first> -l <last> "specs/ecma-376-part1/Ecma Office Open XML Part 1 - Fundamentals And Markup Language Reference.pdf" -`

- `specs/ecma-376-part2/Ecma Office Open XML Part 2 - Open Packaging Conventions.pdf`
  — §8 Parts and §9 Relationships: content-type registration and relationship targets for
  worksheet-to-drawing and worksheet-to-table part connections; relevant when verifying
  that `Flush` correctly writes relationship entries and `<tableParts>` references.
  Extract with:
  `pdftotext -f <first> -l <last> "specs/ecma-376-part2/Ecma Office Open XML Part 2 - Open Packaging Conventions.pdf" -`

**Additional files:**

- `/app/TEST_INTERFACE.md` §4.05 (the full interface for this milestone, including the
  `StreamWriter` type and all method signatures); also §4.01–4.04 for every inherited
  signature still in scope.
- `/app/WORKSPACE_HANDOFF.md` — describes the state of the workspace at the start of
  this milestone, including the vendor tree, prior source layout, and any traps noted by
  earlier steps.

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
