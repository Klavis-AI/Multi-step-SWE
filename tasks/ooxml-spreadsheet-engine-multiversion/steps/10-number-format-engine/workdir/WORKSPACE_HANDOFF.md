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

### Milestone 05 — The streaming writer and the drawing registry

This milestone adds the streaming write path (`NewStreamWriter`, `SetRow`,
`AddTable`, `Flush`), drawing-object deletion (`DeleteChart`, `DeletePicture`),
variadic page-margin accessors, `DeleteDefinedName`, `UnmergeCell`, a column
range extension to `SetColVisible`, a variadic combo parameter on `AddChart`,
and `CharsetTranscoder`.

You inherit everything from milestones 01–04; all prior graded behaviour remains
in scope.

The sharpest trap: `DeleteDefinedName` computes scope as `""` (empty string) for
workbook-level names, but `GetDefinedName` returns `"Workbook"` for the same
names. Passing `Scope: "Workbook"` to `DeleteDefinedName` always returns "no
defined name on the scope". Use `Scope: ""` for workbook-level deletion.

### Milestone 06 — The formula tokenizer and evaluator

This milestone is the largest single delta in the ladder: `calc.go` arrives at 2,632 lines with 68 registered functions, a token stream for the SpreadsheetML formula grammar, and an evaluator over number, text, boolean, empty and the seven error values that implements the additive and multiplicative operators, parenthesised grouping and unary minus — but not exponentiation, percent, comparison or concatenation. Alongside it come the `Style` struct (replacing JSON-only style creation), rich-text cell runs, chart sheets as a new sheet kind, conditional-format and data-validation removal, page breaks, `ExcelDateToTime`, the exported `Stack`, and a migration of `PivotTableOption`'s list fields from `[]string` to `[]PivotTableField`, with `Page` removed in favour of `Filter`.

The agent inherits everything from milestones 01–05: the two write paths (buffered and streaming), the drawing registry, pivot cache parts, data validation, the full OPC and SpreadsheetML model. All prior graded behaviour remains in scope; the evaluator must not disturb it.

Two inherited contracts are re-keyed here. Every integer that names a sheet *position* — in `GetSheetName`, `GetSheetIndex`, `GetActiveSheetIndex`, `SetActiveSheet`, `NewSheet` and `CopySheet` — becomes a zero-based index into `GetSheetList` instead of the `sheetId` attribute, and `GetSheetIndex` answers `-1` for an unknown name where milestone 01 answered `0`. `GetSheetMap` alone keeps its `sheetId` keys.

The three things most likely to trip an implementer at this checkpoint: `SetCellBool` and `SetCellValue(bool)` store `"1"`/`"0"` as raw literals, not `"TRUE"`/`"FALSE"` — `GetCellValue` returns those literals unchanged. `DEGREES(0)` returns `#DIV/0!` rather than `0` — the zero-guard in this checkpoint's implementation fires before the multiply, which is a known bug in this Oracle. And a failed `CalcCellValue` is `("", err)` — the empty string with the `#...!` string carried in `err.Error()` where the failure is one of the seven error conditions — which is also the shape a call with the wrong number of arguments takes. One function departs from that at this milestone: `ARABIC` on a malformed numeral hands `"#VALUE!"` back as a *successful* result, so a nil error is not a promise that the string is a number. `TEST_INTERFACE.md` 4.06.1 has both shapes.

### Milestone 07 — Decryption, iteration limits and the criteria core

This milestone adds workbook decryption (`crypt.go`; MS-OFFCRYPTO names several mechanisms, and only the standard one is graded here, against a frozen fixture), the `Cols` column iterator and its eager companion `GetCols`, the `sheetFormatPr` accessors with their seven option types, twenty new formula functions across the logical, information, criteria, statistical, date and text families, and the exported "Excel specifications and limits" constants.

The agent inherits everything from milestones 01–06: the two write paths, the OPC container, the cell and coordinate model, worksheet features, and the formula tokenizer and evaluator with its 68 inherited functions. All prior graded behaviour remains in scope, but some of it moves here and `TEST_INTERFACE.md` section 4.07.9 is the full list. `StreamWriter.Flush` writes its rows into the serialised package without populating the in-memory worksheet, so `GetRows` on a streamed sheet returns nothing until the workbook has been written and reopened; milestone 06 made those rows visible immediately. The formula evaluator moves in three places, all of which supersede section 4.06.1: a range is resolved from its own top-left corner rather than from `A1` (`=SUM(B1:B3)` sums `B1:B3` alone, not `A1:B3`), the comparison operators `=`, `<`, `>`, `<=` and `>=` evaluate over numeric operands and yield the text `TRUE`/`FALSE`, and `^` exponentiates. `<>`, `%` and `&` are still not evaluated at this checkpoint. One further inherited change is outside the evaluator: `AddVBAProject` still refuses a path that does not exist, but the error it returns no longer satisfies `os.IsNotExist`, so only `err != nil` may be relied on there.

Encryption is one-way here, on purpose. `Decrypt` works; `Encrypt` computes key material and then returns `not support encryption currently` with a nil buffer. Making `Encrypt` succeed is a failure at this checkpoint, not an improvement. Note also that `OpenReader` only consults a password when the stream carries the OLE compound-file identifier, so a wrong password on a plain ZIP workbook is silently irrelevant.

Five of the new functions carry bugs that are graded as behaviour. `OR` assigns instead of accumulating over numeric arguments, so `=OR(1,0)` is `FALSE` and only the last numeric argument matters. `MEDIAN` re-reads the last argument once per scalar argument, so `=MEDIAN(1,2,3)` is `3` — ranges are unaffected and give the true median. `TRIM` does not collapse interior whitespace. `ISNONTEXT` answers "is this operand empty" rather than "is this not text", because every scalar operand arrives as a string. `SUMIF` additionally does not recognise `<>`, which therefore matches nothing rather than everything.

The formula-type options on `SetCellFormula` (`FormulaOpts`, `STCellFormulaTypeArray` and friends) are **inherited from milestone 06**, not new here; they are graded here as an inherited regression gate — unweighted, but it must pass completely — so the new column iterator must not disturb them.

### Milestone 08 — The function library and expression arguments

This milestone takes the registered formula function set from 88 names to 233: 150 names added, five renamed away. Six families arrive at once: logical and lookup (`IF`, `IFERROR`, `NOT`, `TRUE`, `FALSE`, `CHOOSE`, `VLOOKUP`, `HLOOKUP`, `LOOKUP`), a full text library, the descriptive statistics and the normal and Poisson distributions, the engineering family (radix conversion, bitwise operators, complex arithmetic, Bessel), the financial family (annuities, cash flows, depreciation), and the positional functions `ROW`, `COLUMN`, `ROWS`, `COLUMNS`, `SHEET`, `NOW`, `TODAY` and `DATEDIF`. Outside the library it adds `GetCellRichText` and the variadic `HyperlinkOpts` tail on `SetCellHyperLink`.

The agent inherits everything from milestones 01–07: the two write paths, the OPC container, the cell and coordinate model, worksheet features, workbook metadata, the streaming writer, decryption, the column iterator, the `sheetFormatPr` accessors, and the formula tokenizer and evaluator. All prior graded behaviour remains in scope.

**Five functions are renamed, not added.** `CEILINGMATH`, `CEILINGPRECISE`, `FLOORMATH`, `FLOORPRECISE` and `ISOCEILING` become `CEILING.MATH`, `CEILING.PRECISE`, `FLOOR.MATH`, `FLOOR.PRECISE` and `ISO.CEILING`, and the old spellings are gone from the registry. The statistical dotted names work the other way round — `VAR.P` and `VARP` are both registered and agree — so the rename is not a blanket rule. `STDEV.S` is a new name only; there is no `STDEVS`. The dot belongs to the function name, and `NORM.S.DIST` carries two of them.

**Three inherited contracts move, and all three are scored.** A bare `TRUE` or `FALSE` in an argument position now resolves to the nullary function of that name, so `=AND(TRUE,TRUE)` evaluates where milestone 07 rejected it; `=TRUE` on its own is still not a formula. `NA()` is now an error rather than an ordinary result carrying the text `#N/A`, though inside an expression it is still the `#N/A` error value. `MEDIAN` now averages the whole scalar argument list, so `=MEDIAN(1,2,3)` is `2` where milestone 07 gave `3`.

The milestone-07 quirks that did **not** move are still graded as before: `OR` assigns rather than accumulates over numeric arguments, `TRIM` does not collapse interior whitespace, `ISNONTEXT` answers "is this operand empty", `SUMIF` does not recognise `<>`, `DATE` renders a formatted instant rather than a serial number, and `StreamWriter.Flush` writes its rows into the package without populating the in-memory worksheet.

**A calculated number is rendered by a rule of its own at this checkpoint**, and it is not the `SetCellValue` read-back rule: the evaluator prints with Go's `%g` and then, if that form carries more than fifteen digits after the point, rounds to the nearest multiple of `1e-15` and prints again — so `=NORMSDIST(1)` is `0.841344746068543`. `TEST_INTERFACE.md` §4.08 states it in full, and it is stated there for milestone 08; a later milestone may render differently.

**The trailing digits of the financial family are graded nowhere**, at this milestone or at a later one that re-checks this function set. `PMT`, `FV`, `NPER`, `IPMT`, `PPMT`, `CUMIPMT`, `CUMPRINC`, `ISPMT`, `NPV`, `IRR`, `MIRR` and `PDURATION` reach their answers through a bounded search, an accumulation, a power or a logarithm, and a closed form fixes the value rather than the last bits of the double. Where such a result is asserted it is parsed as a number and compared to a tolerance; an exact result — a zero-rate annuity, a whole-number depreciation charge, `=FVSCHEDULE(100,A2:A3)` — is still compared as a string. `TEST_INTERFACE.md` §4.08.7 lists both sides.

The new library carries defects of its own that are graded as behaviour: a `VLOOKUP` miss is a Go error rather than an `#N/A` value; `CONCAT` refuses a range instead of flattening it; `FIXED` has no two-decimal default; `T` returns a non-text argument's string form rather than the empty string; `HARMEAN` reads scalars only; `OCT2DEC` accepts the non-octal digit 8; `SHEET` takes no argument; `RATE` is not registered at all; and `CEILING.MATH`/`FLOOR.MATH` read the mode argument as a sign rather than as the flag OpenFormula §6.17.1 describes, so `=CEILING.MATH(-1.2,1,1)` is `-1` and only a negative mode rounds a negative operand away from zero. Two rough edges are **not** scored and should not be built on: `MID` with a start position of 0 panics, and `GetCellRichText` on a never-written cell leaks `strconv.Atoi: parsing "": invalid syntax`.

### Milestone 09 — Criteria, lookup, shared formulas and the decompression budget

This milestone takes the registered formula function set from 233 names to 329: 96 names added, **none renamed and none removed** — the opposite of milestone 08, which renamed five away. The additions complete the criteria family (`COUNTIF`, `COUNTIFS`, `AVERAGEIF`, `MAXIFS`, `MINIFS`, rebuilding the parser the inherited `SUMIF` shares), the lookup and reference family (`INDEX`, `MATCH`, `XLOOKUP`, `ADDRESS`, `TRANSPOSE`, `SHEETS`, `ISFORMULA`, `ISLOGICAL`, `ISREF`), the date and time family (`DATEVALUE`, `DAY`, `DAYS`, `MONTH`, `YEAR`, `MINUTE`, `TIME`, `WEEKDAY`, `ISOWEEKNUM`, `YEARFRAC`), and the statistical, engineering and financial families. The comparison and concatenation operators learn to handle text operands at the same time, and the conditional functions `IFNA`, `IFS`, `SWITCH` and `XOR` arrive.

Outside the library: shared formulas become functional — the `FormulaOpts` tail milestone 04 added to `SetCellFormula` now populates the whole declared range with a library-assigned `si`, and reading any cell of it returns the formula translated to that cell's position; thirty-six `Err*` sentinels become exported values a caller can branch on with `errors.Is`; the `Options` struct grows `RawCellValue` and the two decompression budgets, and every reading entry point takes it; and `GetCellType`, `GetAppProps`/`SetAppProps`, `SetRowStyle`, `RowOpts`, the iterator position accessors, `Rows.Close`, `File.Close` and `ReadZipReader` as a method fill the remaining gaps; `Pkg` and `Sheet` become `sync.Map`s; and `PivotTableField` gains the `Compact` and `Outline` layout flags.

The agent inherits everything from milestones 01–08: the two write paths, the OPC container, the cell and coordinate model, worksheet features, workbook metadata, the streaming writer, decryption, the iterators, and the formula tokenizer, evaluator and 233-name library. All prior graded behaviour remains in scope.

**Nine inherited contracts move, and all nine are scored.** A numeric formula result is re-rendered to fifteen *significant* digits only when its shortest exact rendering needs more than fifteen digits after the decimal point, so `=SQRT(2)` is `1.4142135623731` but `=PI()` is left at `3.141592653589793` and `=100/7` at `14.285714285714286`; a result below one gains a place rather than losing one, and trailing zeros are dropped so an exact result sheds its accumulated binary tail. A stored numeric cell read back through `GetCellValue` re-renders at a lower threshold, more than ten decimal places. The coordinate helpers accept an absolute reference (`CellNameToCoordinates("$A$1")` is `(1, 1, nil)`), while the other malformed forms are still rejected. `GetSheetIndex` folds case. `DataValidation.SetRange` takes two empty interfaces rather than two `int`s, so a bound can be a whole number, a decimal or formula text, and the two ends are dispatched independently. `SHEET` takes an optional sheet name where milestone 08 rejected any argument. `RATE` is registered, reversing milestone 08's `not support RATE function`. A shared formula populates its range and translates on read, where milestone 04 wrote `@t` and `@ref` onto the master cell alone and returned the untranslated text from every other cell; the `ref` is validated now, and a `dataTable` type is dropped rather than written through. And the XML declaration loses `standalone="yes"` — every part now begins with the Go standard library's `xml.Header`, and the exported `XMLHeader` constant is removed while `XMLHeaderByte` remains.

The milestone-08 quirks that did **not** move are still graded as before: the five dotted ceiling and floor renames, a `VLOOKUP` miss as a Go error, `CONCAT` refusing a range, `FIXED` without a two-decimal default, `T` returning a non-text argument's string form, `HARMEAN` reading scalars only, `OCT2DEC` accepting the digit 8, `OR` assigning over numeric arguments, `TRIM` not collapsing interior whitespace, `ISNONTEXT` answering "is this operand empty", `DATE` rendering a formatted instant, and `StreamWriter.Flush` leaving the in-memory worksheet unpopulated.

The new surface carries defects of its own that are graded as behaviour: a text criterion is an unanchored regular expression, so `=COUNTIF(B1:B4,"ap")` matches anywhere in the value and the wildcards inherit that; `ADDRESS` discards its row argument and builds the reference from the column alone; a mixed comparison never coerces, and a cell reference is classified as numeric before its contents are read; `DATEVALUE` discards a time of day and refuses a serial number; `VALUE` does not strip currency symbols or thousands separators; `WEIBULL` does not range-check a negative `x`; only `AMORDEGRC`, `AMORLINC`, `XIRR` and `XNPV` force text dates; `SetAppProps` replaces rather than merges; `GetCellType` reports `CellTypeUnset` for a numeric cell as well as an empty one; a data-table formula is stored with its type dropped; and several `Options` values are not merged — the last one wins.

One sharp edge is **not** scored and should not be built on: a `FormulaOpts` with `Type` set to `shared` and a nil `Ref` panics, because the library dereferences the pointer without checking. A second one *is* scored, so build it: `File.Close` is not idempotent — a second call tries to remove the already-removed spool files and returns the failure.

### Milestone 10 — The number-format engine

This milestone applies the number format a cell carries. Until now the format was stored and written but never read back over the value. A format engine takes ownership of the read path: it splits a custom format string into up to four sections — positive, negative, zero, text — selects one by the value's sign, and renders the tokens in it. The numeric placeholder is the single character `0`; the date and clock tokens render the workbook's serial numbers, with an elapsed form in square brackets that accumulates past a day; a bracketed hexadecimal LCID selects the language of the month and meridiem names.

The formula function set goes from 329 names to 400: 71 added, **none renamed and none removed**. The additions are the plural criteria aggregates (`SUMIFS`, `AVERAGEIFS`) and the array arithmetic (`SUMPRODUCT`, `SUMX2MY2`, `SUMX2PY2`, `SUMXMY2`, `SERIESSUM`); the probability distributions under both their pre-2010 spellings and their 2010 dotted names; the correlation and hypothesis tests (`CORREL`, `COVAR`, `MODE`, `CHITEST`, `FTEST`, `TTEST`, `GAUSS`, `PHI`); and `INDIRECT`, `FORMULATEXT`, `TYPE`, `ERROR.TYPE`, `HOUR`, `SECOND` and `TIMEVALUE`. Unlike milestone 08's five dotted renames, the dotted statistical names here are **additions**: every original spelling remains registered and the two agree where they denote the same function.

Outside the library: worksheet protection gains the iterated password hash (`FormatSheetProtection.AlgorithmName`, six digests, `spinCount="100000"`, a fresh salt per call) and `UnprotectSheet` grows a variadic password; the workbook part gains `SetWorkbookPrOptions`/`GetWorkbookPrOptions` with `FilterPrivacy` and the existing `CodeName`; the sheet view gains `ShowRuler` and `View`; `SaveAs` accepts exactly five extensions and selects the workbook content type from the one it is given; and four misspelled exported identifiers are corrected, two sentinel messages repaired, `ErrUnsupportEncryptMechanism` renamed to `ErrUnsupportedEncryptMechanism`, and four content-type constants published.

The agent inherits everything from milestones 01–09: the two write paths, the OPC container, the cell and coordinate model, worksheet features, workbook metadata, the streaming writer, decryption, the iterators, shared formulas, the error sentinels, and the formula tokenizer, evaluator and 329-name library. All prior graded behaviour remains in scope.

**Four inherited contracts move, and all four are scored.** A boolean cell *displays* as `TRUE` or `FALSE` where milestone 09 gave `1` and `0`; only the display moves, since the raw value is still the digit and `GetCellType` is still `CellTypeBool`. The fifteen-digit rendering now counts significant figures everywhere and reaches the **stored** value, so `SetCellValue` of `1/3` stores `0.333333333333333` and `=PI()` is `3.14159265358979`. And `ISERR`/`ISERROR` classify the error they are given instead of propagating it, so `=ISERR(1/0)` is `TRUE` where milestone 07 raised `#DIV/0!`. A failed argument expression is an error value in every argument position, where milestone 09 aborted the call on its last one: `=ERROR.TYPE(1/0)` is `2`, and `=IFERROR(1,2/0)` is `1`.

**Five exported names are withdrawn**: the four iterator position accessors `Rows.CurrentRow`, `Rows.TotalRows`, `Cols.CurrentCol` and `Cols.TotalCols`, and `XMLHeaderByte`. The `TotalRows` *constant* (1048576) is a different name and remains; `Rows.Close` and `File.Close` are unaffected.

The milestone-09 quirks that did **not** move are still graded as before: the unanchored regular-expression text criterion, `ADDRESS` discarding its row argument, the non-coercing mixed comparison, `DATEVALUE` refusing a serial number, `DAY(0)` being `0`, `VALUE` not stripping currency symbols, `SetAppProps` replacing rather than merging, `GetCellType` reporting `CellTypeUnset` for a numeric cell, and `File.Close` not being idempotent.

The new surface carries defects of its own that are graded as behaviour. Only the single-character `0` is a numeric placeholder, so `00` is an unsupported token and a section containing one yields the **empty string**; a decimal point is not a token either, so `0.00` falls through unformatted. Rounding is half-to-even. The negative section formats the absolute value without re-emitting the sign, a single-section format covers positive values only, and the zero section never produces its own text. An unsupported token abandons the whole section, discarding the literals around it. `General` is recognised and then declined. The `mm` after an elapsed `[h]` is parsed as the **month**, not the minutes. The language identifier is matched on exact bracketed text, so `[$-de-DE]` and `[$-0409]` are both unrecognised, and an unrecognised one makes the section unsupported rather than falling back to English. `INDIRECT` refuses any sheet-qualified reference, including the containing sheet's own name. `MODE.MULT` renders into a cell as the empty string. `SUMPRODUCT` reports mismatched ranges as `#VALUE!` where the sum-of-squares family reports `#N/A`. `SERIESSUM`'s third argument is the exponent *step*. `GAUSS` and `PHI` word their arity failures differently. `T.INV(0.5,10)` is `0`. And `View` is not validated, so an unknown view mode is stored and written through.

One sharp edge is worth naming because it cannot be undone from outside: a `ProtectSheet` that fails on the algorithm name or the password length **still protects the sheet**. The flags are installed before the password is hashed, so the call returns an error and leaves `<sheetProtection sheet="true">` behind with no password material at all — the sheet is locked and no password will unlock it. `UnprotectSheet` with no argument is the only way out, and it clears unconditionally.
