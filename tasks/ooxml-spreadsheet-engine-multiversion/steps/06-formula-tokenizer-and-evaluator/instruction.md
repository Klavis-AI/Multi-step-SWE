# Milestone 06 of 10 — The formula tokenizer and evaluator

You are continuing `seedcalc`, a Go library for OOXML SpreadsheetML documents,
in `/app`. The workspace already holds everything the previous milestones left
behind: their source, the frozen dependency set, the specification corpus under
`/app/specs/`, and the two documents that define the contract this milestone is
specified against. This milestone adds one cumulative capability on top of that.

## 1. Implement this milestone

This milestone installs the formula engine: a tokenizer that classifies the SpreadsheetML formula grammar into operands, operators, functions, subexpressions, and range components; a parser that resolves the additive and multiplicative operators, parenthesised grouping, and unary minus, with the usual precedence; and an evaluator whose value lattice covers number, text, boolean, empty, and the seven error values (`#DIV/0!`, `#VALUE!`, `#REF!`, `#NAME?`, `#NUM!`, `#N/A`, `#NULL!`). `CalcCellValue` evaluates the formula stored in a cell and returns its result as a string, or the empty string and a non-nil error when the formula cannot be evaluated. The initial function library covers 68 functions across arithmetic, rounding, aggregation, trigonometric, combinatorial, and unit-conversion families; `UpdateLinkedValue` drops every cached result from the serialized cells and removes the workbook's `calcPr` element, so a host spreadsheet application recalculates on next open.

Alongside the evaluator, this milestone adds: the `Style` struct accepted by `NewStyle` alongside the existing JSON path; rich-text cell runs stored as shared-string `si`/`r` sequences; chart sheets as a distinct sheet kind with their own part path; manual page breaks inserted and removed per row or column; `UnsetConditionalFormat` and `DeleteDataValidation`; `ExcelDateToTime` for serial-date conversion; the exported `Stack`; and a migration of `PivotTableOption`'s list fields from `[]string` to `[]PivotTableField`, with `Page` removed in favour of `Filter`.

Two changes are most likely to surface failures in inherited behaviour. The sheet-index API stops keying on the `sheetId` attribute and starts keying on the zero-based position in `GetSheetList`: `GetSheetName`, `GetSheetIndex`, `GetActiveSheetIndex`, `SetActiveSheet`, `NewSheet` and `CopySheet` all move with it, and `GetSheetIndex` now answers `-1`, not `0`, for a name the workbook does not have, so any code using the old ID-based accessors must switch. And `PivotTableOption.Columns`, `.Rows` and `.Data` are now `[]PivotTableField` while `.Page []string` is replaced by `.Filter []PivotTableField`; callers that built those slices from bare strings must wrap each entry in a `PivotTableField{Data: name}` value. All two-write-path, OPC-container, cell-model, worksheet-feature, and workbook-metadata behaviour from milestones 01–05 remains in scope.

## 2. Read these local references

**Normative semantics:**

- `specs/openformula/OpenDocument-v1.3-os-part4-formula.html` — section 3 "Formula Processing Model" (3.2 "Expression Evaluation"), section 4 "Types" (the value lattice, including 4.6 "Error" and 4.7 "Empty Cell"), section 5 "Expression Syntax" (5.5 "Operators" and its precedence table, 5.6 "Functions and Function Parameters", 5.8 "References", 5.12 "Constant Errors") and section 6.3 "Implicit Conversion Operators" for coercion. The per-function semantics and argument counts of **63 of the 68 names** are in section 6.16 "Mathematical Functions", section 6.17 "Rounding Functions", section 6.19 "Number Representation Conversion Functions" and section 6.5 "Matrix Functions" — each function's `Syntax:` line gives the arity `TEST_INTERFACE.md` 4.06.1 says is enforced, and `TEST_INTERFACE.md` 4.06.1 also calls out three departures from those clauses — two of argument count (`ROUND` and `SUM`) and one of value: the plain two-argument `CEILING` and `FLOOR` given a negative significance round the magnitude rather than toward an infinity, so `=CEILING(-2.5,-1)` is `"-3"` where §6.17.1 gives `"-2"`, and `=FLOOR(-2.5,-1)` is `"-2"` where §6.17.3 gives `"-3"`. That list is not exhaustive and 4.06.1 is not only a list of arities, so read the whole of it alongside these clauses: where 4.06.1 and a clause disagree, 4.06.1 governs. The other five — `CEILINGMATH`, `CEILINGPRECISE`, `FLOORMATH`, `FLOORPRECISE` and `ISOCEILING` — are spreadsheet-application extensions with no clause anywhere in part 4, dotted or undotted, so do not go looking for them: `TEST_INTERFACE.md` 4.06.1 states their accepted argument counts and results directly. Read 5.6 with care: it requires function names to be matched case-insensitively and this milestone deliberately does not, so follow `TEST_INTERFACE.md` 4.06.1 where the two disagree. Open with a browser or `cat`.
- `specs/ecma-376-part1/Ecma Office Open XML Part 1 - Fundamentals And Markup Language Reference.pdf` — §18.2.2 (`calcPr`, the calculation properties `UpdateLinkedValue` removes); §18.3.1.4 (`c` element: `@t` type attribute, `@v` cached value, `@f` formula); §18.3.1.74 (`rowBreaks`) and §18.3.1.14 (`colBreaks`), each over §18.3.1.3 (`brk`), for page breaks; §18.3.1.12 (chartsheet element); §18.4.4 (`r`, rich-text run) and §18.4.8 (`si`, String Item — the shared-string entry the runs hang off); §18.10.1.62 (`pageField`) inside §18.10.1.63 (`pageFields`) in the pivot table definition; §18.18.3 (border-style integer enumeration). Read with: `pdftotext -f <first> -l <last> "specs/ecma-376-part1/Ecma Office Open XML Part 1 - Fundamentals And Markup Language Reference.pdf" -`

**Additional files:**

- `/app/TEST_INTERFACE.md` sections 4.01–4.06: section 4.06 defines every required signature for this milestone; sections 4.01–4.05 define the inherited behaviour that remains in scope.
- `/app/WORKSPACE_HANDOFF.md`: the state left by milestones 01–05, including the module path, vendor directory layout, and Oracle quirks carried into this checkpoint.

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
