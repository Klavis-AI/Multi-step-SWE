# Milestone 08 of 10 — The function library and expression arguments

You are continuing `seedcalc`, a Go library for OOXML SpreadsheetML documents,
in `/app`. The workspace already holds everything the previous milestones left
behind: their source, the frozen dependency set, the specification corpus under
`/app/specs/`, and the two documents that define the contract this milestone is
specified against. This milestone adds one cumulative capability on top of that.

## 1. Implement this milestone

This milestone is almost entirely formula library. The registered function set grows from 88 names to 233 — 150 names added, five renamed away — across six families: the logical and lookup functions (`IF`, `IFERROR`, `NOT`, `TRUE`, `FALSE`, `CHOOSE`, `VLOOKUP`, `HLOOKUP`, `LOOKUP`); a full text library (`LEN`, `LEFT`, `RIGHT`, `MID`, `FIND`, `REPLACE`, `SUBSTITUTE`, `REPT`, `CONCAT`, `EXACT`, `CHAR`, `CODE`, `FIXED`, `T`, `N` and their byte-wise and Unicode twins); the descriptive statistics and the normal and Poisson distributions; the engineering family of radix conversions, bitwise operators, complex arithmetic and Bessel functions; the financial family of annuity, cash-flow and depreciation functions; and the positional functions `ROW`, `COLUMN`, `ROWS`, `COLUMNS`, `SHEET`, `NOW`, `TODAY` and `DATEDIF`. Outside the library, `GetCellRichText` completes the rich-text pair milestone 06 opened, and `SetCellHyperLink` gains a variadic `HyperlinkOpts` tail carrying the hyperlink element's optional `@display` and `@tooltip` attributes.

Five functions are **renamed**, not added. `CEILINGMATH`, `CEILINGPRECISE`, `FLOORMATH`, `FLOORPRECISE` and `ISOCEILING` become `CEILING.MATH`, `CEILING.PRECISE`, `FLOOR.MATH`, `FLOOR.PRECISE` and `ISO.CEILING`. The old spellings are removed from the registry and must resolve to `not support <NAME> function`. The dot is part of the registered name, so the tokenizer has to carry it through the function-name token — `NORM.S.DIST` carries two. Keeping the old names registered alongside the new ones is wrong for this milestone.

Several functions depart from their OpenFormula definitions and are required exactly as the Oracle exhibits them. `NOT` inverts only the quoted boolean words, so `=NOT("TRUE")` is `FALSE` while `=NOT(1)` is rejected. A `VLOOKUP` miss is a Go error, not an `#N/A` value. `CONCAT` refuses a range argument rather than flattening it. `FIXED` has no default of two decimals, so `=FIXED(1234.567)` is `"1,234.567"`. `T` hands a non-text argument's string form back rather than the empty string, so `=T(1)` is `"1"`. `HARMEAN` reads scalars only and returns `#N/A` for a range. `OCT2DEC` does not validate its digits, so `=OCT2DEC("8")` is `8`. `SHEET` takes no argument at all. `RATE` is not registered; `=RATE(...)` must be `not support RATE function`.

Function arguments are expressions at this milestone: a cell reference or range, an arithmetic expression, or a nested call whose value the outer function receives. The comparison operators gain `<>`, not-equal, over numeric operands — `=1<>2` is `TRUE` — where milestone 07 had no such operator; `<>` inside a criteria string is still not recognised. `TEST_INTERFACE.md` §4.08.11 has the detail, and §4.08's head has the rule by which a calculated number is turned into the string the checker compares, which is not the same rule `SetCellValue` read-back obeys.

Three inherited contracts move here, and all three are in scope. A bare `TRUE` or `FALSE` in an argument position now resolves to the nullary function of that name, so `=AND(TRUE,TRUE)` evaluates where milestone 07 rejected it — but `=TRUE` as a whole formula is still invalid. `NA()` is now returned as an error rather than handing back the text `#N/A` as an ordinary result; inside an expression it is still the `#N/A` error value, so `=ISNA(NA())` is still `TRUE` and `=IFERROR(NA(),"x")` is still `"x"`. And `MEDIAN` now averages the whole scalar argument list, so `=MEDIAN(1,2,3)` is `2` where milestone 07 gave `3`. Every other inherited contract from milestones 01–07 remains in scope, including the milestone-07 quirks that did *not* move: `OR` still assigns over numeric arguments, `TRIM` still does not collapse interior whitespace, `ISNONTEXT` still answers "is this operand empty", `SUMIF` still does not recognise `<>`, `DATE` still renders a formatted instant rather than a serial, and `StreamWriter.Flush` still leaves the in-memory worksheet unpopulated.

## 2. Read these local references

**Normative semantics:**

- `specs/openformula/OpenDocument-v1.3-os-part4-formula.html` — the section numbers below were read out of this file's own headings, so use them rather than guessing from a title. Section **6.6** Bit operation functions (`BITAND` 6.6.2, `BITLSHIFT` 6.6.3, `BITOR` 6.6.4, `BITRSHIFT` 6.6.5, `BITXOR` 6.6.6); section **6.7** Byte-position text functions (`FINDB` 6.7.2, `LEFTB` 6.7.3, `LENB` 6.7.4, `MIDB` 6.7.5, `REPLACEB` 6.7.6, `RIGHTB` 6.7.7) for the `B`-suffixed twins; section **6.8** Complex Number Functions (`COMPLEX` 6.8.2, `IMABS` 6.8.3) for the complex family; section **6.19** Number Representation Conversion Functions (`BIN2DEC` 6.19.4, `BIN2HEX` 6.19.5, `DEC2BIN` 6.19.7, `HEX2DEC` 6.19.12, `OCT2DEC` 6.19.15) for the radix conversions — **not** section 6.9, which is Database Functions; section **6.10** Date and Time Functions (`DATEDIF` 6.10.3, `NOW` 6.10.15, `TODAY` 6.10.19); section **6.12** Financial Functions (`DB` 6.12.13, `DDB` 6.12.14, `EFFECT` 6.12.19, `FV` 6.12.20, `MIRR` 6.12.27, `NOMINAL` 6.12.28, `NPER` 6.12.29, `NPV` 6.12.30, `PMT` 6.12.36) for the annuity and cash-flow functions; section **6.13** Information Functions (`COLUMN` 6.13.4, `COLUMNS` 6.13.5, `COUNT` 6.13.6, `ISTEXT` 6.13.25, `N` 6.13.26, `ROW` 6.13.29, `ROWS` 6.13.30, `SHEET` 6.13.31); section **6.14** Lookup Functions (`CHOOSE` 6.14.3, `HLOOKUP` 6.14.5, `LOOKUP` 6.14.8, `VLOOKUP` 6.14.12); section **6.15** Logical Functions (`FALSE` 6.15.3, `IF` 6.15.4, `IFERROR` 6.15.5, `NOT` 6.15.7, `TRUE` 6.15.9); section **6.16** Mathematical Functions (`BESSELI` 6.16.12, `BESSELJ` 6.16.13, `GAMMA` 6.16.34, `GAMMALN` 6.16.35); section **6.17** **Rounding** Functions — not Mathematical, despite what the renamed functions look like — for the ceiling and floor pair, `CEILING` 6.17.1 and `FLOOR` 6.17.3, whose Mode paragraph this checkpoint departs from (see `TEST_INTERFACE.md` §4.08.8); section **6.18** Statistical Functions (`AVERAGE` 6.18.3, `HARMEAN` 6.18.36 — not 6.18.35, which is `GROWTH` — `KURT` 6.18.39, `NORMDIST` 6.18.52, `PERCENTILE` 6.18.57, `POISSON` 6.18.62, `QUARTILE` 6.18.64, `SKEW` 6.18.67, `STDEV` 6.18.72, `VARP` 6.18.84); and section **6.20** Text Functions (`CHAR` 6.20.3, `CODE` 6.20.5, `CONCATENATE` 6.20.6 — there is no `CONCAT` clause — `EXACT` 6.20.8, `FIND` 6.20.9, `FIXED` 6.20.10, `LEFT` 6.20.12, `LEN` 6.20.13, `MID` 6.20.15, `REPLACE` 6.20.17, `REPT` 6.20.18, `RIGHT` 6.20.19, `SUBSTITUTE` 6.20.21, `T` 6.20.22, `UNICHAR` 6.20.25, `UNICODE` 6.20.26). `ENCODEURL` appears nowhere in this specification; `TEST_INTERFACE.md` §4.08.4 is its whole definition. Open with a browser or `cat`.
- `specs/ecma-376-part1/Ecma Office Open XML Part 1 - Fundamentals And Markup Language Reference.pdf` — §18.3.1.47 (`hyperlink`: `@ref`, `@display`, `@tooltip`, `@location`, `r:id`); §18.4.4 (`r`, Rich Text Run) and §18.4.8 (`si`, String Item — the shared-string entry the runs hang off) for the runs `GetCellRichText` reads; §18.17.4 (Dates and Times) for the serial-date encoding `NOW`, `TODAY` and `DATEDIF` use, of which §18.17.4.1 is Date Conversion for Serial Date-Times and §18.17.4.2 is Time Conversion for Serial Date-Times — `TODAY` is the whole-day half and `NOW` carries the fraction as well. Read with: `pdftotext -f <first> -l <last> "specs/ecma-376-part1/Ecma Office Open XML Part 1 - Fundamentals And Markup Language Reference.pdf" -`

**Additional files:**

- `/app/TEST_INTERFACE.md` sections 4.01–4.08: section 4.08 defines every required signature for this milestone, family by family, with each in-scope deviation called out; sections 4.01–4.07 define the inherited behaviour that remains in scope.
- `/app/WORKSPACE_HANDOFF.md`: the state left by milestones 01–07, including the module path, vendor directory layout, and Oracle quirks carried into this checkpoint.

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
