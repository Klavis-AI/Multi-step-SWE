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
