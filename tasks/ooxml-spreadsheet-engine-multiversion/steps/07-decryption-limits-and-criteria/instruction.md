# Milestone 07 of 10 — Decryption, iteration limits and the criteria core

You are continuing `seedcalc`, a Go library for OOXML SpreadsheetML documents,
in `/app`. The workspace already holds everything the previous milestones left
behind: their source, the frozen dependency set, the specification corpus under
`/app/specs/`, and the two documents that define the contract this milestone is
specified against. This milestone adds one cumulative capability on top of that.

## 1. Implement this milestone

This milestone adds workbook decryption and the password-carrying `Options` value; the `Cols` column iterator and its eager companion `GetCols`; the worksheet formatting-properties accessors `SetSheetFormatPr` and `GetSheetFormatPr` with their seven option types; twenty new formula functions across the logical, information, criteria, statistical, date and text families; and the exported "Excel specifications and limits" constants together with the file-name ceiling `SaveAs` now enforces.

Encryption is deliberately one-way at this checkpoint. `Decrypt` reads an OLE compound file and dispatches on the mechanism its `EncryptionInfo` stream names; only the **standard** mechanism is in scope here, against a frozen standard-encrypted fixture, and no in-scope case presents an agile package; `Encrypt` is declared, computes its key material, and then returns the error `not support encryption currently` rather than emitting a package a host application could not open. An implementation that makes `Encrypt` succeed is wrong for this milestone.

Five of the new functions depart from their OpenFormula definitions, and the departures are required exactly as the Oracle exhibits them. `OR` assigns rather than accumulates over numeric arguments, so only the last numeric argument decides the result: `=OR(1,0)` is `FALSE`. `MEDIAN` re-reads the *last* argument once per scalar argument, so over literals it always returns the final one: `=MEDIAN(1,2,3)` is `3`, while `=MEDIAN(A1:A3)` over a range is the true median. `TRIM` strips leading and trailing whitespace but does not collapse interior runs. `ISNONTEXT` reports `TRUE` only for an operand with no text at all, because every scalar operand reaches a function as a string — a numeric cell is therefore reported as text. `SUMIF` does not recognise the `<>` operator; the parser consumes the leading `<` and is left with an unparseable operand, so such a criterion selects nothing.

All behaviour from milestones 01–06 remains in scope: the two write paths, the OPC container, the cell and coordinate model, worksheet features, and the formula engine and its inherited function library. Some of it moves at this checkpoint, and `TEST_INTERFACE.md` section 4.07.9 is the list. The streaming writer's `Flush` serialises its rows into the package but no longer populates the in-memory worksheet, so a sheet written through `NewStreamWriter` reads back through `GetRows` only after the file has been written and reopened. In the formula evaluator three things change: a range is now resolved from its own top-left corner rather than from `A1`, so `=SUM(B1:B3)` is the sum of `B1:B3` alone; the comparison operators `=`, `<`, `>`, `<=` and `>=` now evaluate over numeric operands and give the text `TRUE`/`FALSE`; and `^` now exponentiates, so `=2^3` is `8`. `<>`, `%` and `&` still do not evaluate here. Section 4.07.9 supersedes section 4.06.1 on those three points and gives the worked values. It also records two changes outside the evaluator: the distinct diagnostic an encrypted package used to get is withdrawn, and `AddVBAProject`'s refusal of a path that does not exist no longer satisfies `os.IsNotExist`.

## 2. Read these local references

**Normative semantics:**

- `specs/openformula/OpenDocument-v1.3-os-part4-formula.html` — section 6.13 (Information Functions) for the IS-family, section 6.15 (Logical Functions) for `AND` and `OR`, section 6.16 (Mathematical Functions) for `SUMIF`, section 6.18 (Statistical Functions) for `MEDIAN` — `COUNTA` is filed under section 6.13 (Information Functions), not Statistical — section 6.10 (Date and Time) for `DATE`, and section 6.20 (Text Functions) for `TRIM`, `CLEAN`, `LOWER`, `UPPER` and `PROPER`. Open with a browser or `cat`.
- `specs/ecma-376-part1/Ecma Office Open XML Part 1 - Fundamentals And Markup Language Reference.pdf` — §18.3.1.81 (`sheetFormatPr`: `@baseColWidth`, `@defaultColWidth`, `@defaultRowHeight`, `@customHeight`, `@zeroHeight`, `@thickTop`, `@thickBottom`); §18.3.1.13 (`col`) for the column model the iterator walks; clause 2 "Conformance" (§2.1, Document Conformance) for the document conformance classes — note that the shipped edition defines the **Strict** class only (SML Strict and its siblings) and no longer defines a Transitional one; §12.3.23 ("Workbook Part", the "Source Relationship" row) for the `officeDocument` relationship type. The shipped edition tabulates the Strict form only; the Transitional URI is given verbatim in `TEST_INTERFACE.md` section 4.07.8. Read with: `pdftotext -f <first> -l <last> "specs/ecma-376-part1/Ecma Office Open XML Part 1 - Fundamentals And Markup Language Reference.pdf" -`
- `specs/ms-offcrypto/` — sections 2.3.4.5 (standard encryption) and 2.3.4.10 (agile encryption) for the `EncryptionInfo` stream `Decrypt` parses, and sections 2.3.4.7–2.3.4.9 for the standard mechanism's key and verifier derivation — 2.3.4.7 ("ECMA-376 Document Encryption Key Generation") is where the 50,000-iteration hash spin that yields the decryption key is specified, and 2.3.4.8–2.3.4.9 cover the password verifier, which this checkpoint does **not** check (`TEST_INTERFACE.md` section 4.07.1). Only the standard mechanism is in scope here. The compound-file container format itself is **not** in this document — MS-OFFCRYPTO defers it to `[MS-CFB]`, which the corpus does not ship; the vendored `github.com/richardlehane/mscfb` already parses it, so no part of that format has to be reimplemented.

**Additional files:**

- `/app/TEST_INTERFACE.md` sections 4.01–4.07: section 4.07 defines every required signature for this milestone; sections 4.01–4.06 define the inherited behaviour that remains in scope.
- `/app/WORKSPACE_HANDOFF.md`: the state left by milestones 01–06, including the module path, vendor directory layout, and Oracle quirks carried into this checkpoint.

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
