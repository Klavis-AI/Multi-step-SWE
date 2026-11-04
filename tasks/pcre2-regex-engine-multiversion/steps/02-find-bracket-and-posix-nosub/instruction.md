# Step 02 of 13 — A cycle broken and a POSIX option stops implying another

## Implement this milestone

PCRE2 **10.22** (2016), which on this ladder also carries the whole of the
skipped 10.21. Two named changes give the step its title — the compile/study
dependency cycle is broken, and `REG_NOSUB` stops implying
`PCRE2_NO_AUTO_CAPTURE` — but most of the observable work is a long tail of
new options, new diagnostics and moved error offsets inherited from 10.21. A
candidate working from memory will assume the `posix_nosub` modifier already
exists (it does not) and that infinitely-looping recursions are still caught
at compile time (they are not). Nearly all of this shows up only as an error
number, message or offset, or an `info` line, all compared exactly.

### REG_NOSUB stops implying PCRE2_NO_AUTO_CAPTURE

* `regcomp()` with `REG_NOSUB` must no longer set `PCRE2_NO_AUTO_CAPTURE`,
  because that disabled back references and subroutine calls, which other
  `regcomp()` implementations support under the flag
  (ChangeLog 10.22 item 6; pcre2posix.3, "COMPILING A PATTERN"). What the
  flag still does is unchanged: `regexec()` ignores `nmatch` and `pmatch` and
  returns no captured strings (pcre2posix.3, "MATCHING A PATTERN").
* `pcre2test`'s `no_auto_capture` must therefore be **ignored** when the
  POSIX API is in use, and a new pattern modifier `posix_nosub` must call the
  wrapper and pass `REG_NOSUB` (ChangeLog 10.22 item 7;
  pcre2test.1, "Using the POSIX wrapper API"). `pcre2test.c` is yours from
  this step on; it appears only if you add it.

### Infinite recursion loops are diagnosed at match time

Recursive references such as `(?3)` are no longer converted to a pattern
offset until the whole pattern has been compiled, so none is caught at
compile time any more and all are left to the matcher
(ChangeLog 10.21 item 30). Patterns that used to fail to compile now compile
and then fail while matching, reported by `pcre2test` as
`Failed: error -52: nested recursion at the same subject position`.

Note a conflict in the shipped pages here, and resolve it the way the checker
does. `pcre2api.3`, "ERROR RETURNS FROM pcre2_match()", still says under
`PCRE2_ERROR_RECURSELOOP` that "some simple patterns that might do this are
detected and faulted at compile time". That sentence was not updated for this
change and is stale: upstream left it standing. **ChangeLog 10.21 item 30 is
authoritative** — "detection of all infinitely looping recursions is postponed
till match time. In the past, some easy ones were detected at compile time."
Keep the 10.20 compile-time check and the patterns that should now compile
will not.

Not every looping recursion reaches the matcher, and so not every one is
reported as `-52`. The release runs its start-up optimisations before the
matcher (`pcre2api.3`, "Start-up optimizations"); where one of them
establishes that the subject cannot match — the compiled pattern's required
code unit is absent from it, for instance — the matcher is never entered, the
`PCRE2_ERROR_RECURSELOOP` guard never runs, and the result is plain
`No match`. Both outcomes are part of the behaviour this step reproduces;
which one a given pattern gets follows from the compiled pattern and the
subject it is matched against. `PCRE2_NO_START_OPTIMIZE` is what turns the
scan off, so a pattern compiled with it always reaches the matcher.

Match-limit checking now applies to recursion too (ChangeLog 10.21 item 39).
The limits themselves are unchanged and are gate material, not milestone
work: see `TEST_INTERFACE.md` §7.

### Compile-time diagnostics: the pre-pass, new limits, new offsets

* An unmatched closing parenthesis is diagnosed in the pre-scan, so
  `/(?J)(?'a'))(?'a')/` reports the parenthesis rather than a misleading
  duplicate-name error (ChangeLog 10.22 item 9); missing closing parentheses
  are found there too (ChangeLog 10.22 item 13).
* Two new limits, both in (pcre2limits.3, "SIZE AND OTHER LIMITATIONS"):
  `pcre2_set_max_pattern_length()` caps the pattern length and fails with
  error 188 at offset 0 beyond the cap (ChangeLog 10.21 item 77;
  pcre2test.1, "Limiting the pattern length"), and a lookbehind over 65535
  characters is an error (ChangeLog 10.21 item 74). Names are length-checked
  (ChangeLog 10.21 item 76).
* Error offsets after `\g` and `\k` are corrected (ChangeLog 10.21 item 32),
  `*LIMIT_MATCH` and `*LIMIT_RECURSION` syntax errors report accurate offsets
  (ChangeLog 10.21 item 35), and options set at the *start* of a pattern are
  no longer transferred into `PCRE2_INFO_ALLOPTIONS`
  (ChangeLog 10.21 item 108).
* Newly diagnosed or newly correct: `\987` and similar references to a
  non-existent group (ChangeLog 10.21 item 31); overflow in `(?(<digits>)`
  conditions (ChangeLog 10.21 item 26); `(?R` followed by `-` or `+`
  (ChangeLog 10.21 item 19); an empty `(?#)` comment (ChangeLog 10.21 item
  13); and `(*NO_AUTO_POSSESS)` (ChangeLog 10.21 item 78).

### New API surface, all of it reachable from pcre2test

* `pcre2_set_offset_limit()` with `PCRE2_USE_OFFSET_LIMIT`, and the error for
  a limit set without the option (ChangeLog 10.21 item 53;
  pcre2api.3, "PCRE2 CONTEXTS"; pcre2test.1, "Setting an offset limit").
* For `pcre2_substitute()`: `PCRE2_SUBSTITUTE_EXTENDED`
  (ChangeLog 10.21 item 55), `${*MARK}` (ChangeLog 10.21 item 46),
  `PCRE2_SUBSTITUTE_UNSET_EMPTY` (ChangeLog 10.21 item 110), and
  `PCRE2_SUBSTITUTE_UNKNOWN_UNSET` with `PCRE2_SUBSTITUTE_OVERFLOW_LENGTH`,
  which reports the buffer size that would have been needed
  (ChangeLog 10.21 item 112; pcre2api.3, "CREATING A NEW STRING WITH
  SUBSTITUTIONS").
* `PCRE2_ALT_VERBNAMES` (ChangeLog 10.21 item 48;
  pcre2pattern.3, "BACKTRACKING CONTROL"), `PCRE2_INFO_HASBACKSLASHC`
  (ChangeLog 10.21 item 92), `pcre2_code_copy()` (ChangeLog 10.22 item 15),
  `pcre2_get_error_message()` returning `PCRE2_ERROR_BADDATA` for numbers
  never produced (ChangeLog 10.22 item 36), and `\C` in lookbehinds and in
  DFA matching in UTF-32 (ChangeLog 10.22 item 39).

### Character classes, quantifiers and the UTF check

* Seven class bugs are corrected, each changing which characters a class
  matches: `[[:punct:]b]` (ChangeLog 10.21 item 95); `[:punct:]` in UCP mode
  over 128-255 (ChangeLog 10.21 item 96); `[:^ascii:]` and `[:^xdigit:]` in a
  non-negated class (ChangeLog 10.21 item 97) and a negated one
  (ChangeLog 10.21 item 101); `[\W\p{Any}]` (ChangeLog 10.21 item 56);
  `\p{Any}` in an extended class (ChangeLog 10.21 item 27); `[[:<:]]` and
  `[[:>:]]` in UCP mode (ChangeLog 10.21 item 60).

  One of those needs a note, because the shipped page still describes the
  behaviour being corrected. `pcre2pattern.3`, "[:punct:]", says the class
  includes "those characters with code points less than 256 that have the S
  (Symbol) property". Wrong threshold: the corrected class takes every
  character with the Unicode P (punctuation) property, plus the ASCII
  symbols that Unicode classes as symbols rather than punctuation — `$`, `+`,
  `<`, `=`, `>`, `^`, the backquote, `|`, `~` — for Perl compatibility.
  Symbol characters in 128-255 (U+00B4, U+00D7, …) are therefore *not*
  matched. ChangeLog 10.21 item 96 is authoritative, and this release's own
  transcripts show the corrected behaviour.
* An isolated `\E` (ChangeLog 10.21 item 102), an empty `\Q\E`
  (ChangeLog 10.21 item 105) or a `(?#` comment (ChangeLog 10.21 item 99)
  between an item and its quantifier must disturb neither.
* An invalid UTF pattern reports the offending offset rather than the code
  unit count (ChangeLog 10.21 item 100), and the subject UTF check inspects
  only the part that can be looked at (ChangeLog 10.21 item 40).

### pcre2test itself is part of the milestone

It must gain `#newline_default` (ChangeLog 10.21 item 50), comment lines among
subject strings (ChangeLog 10.21 item 52), `null_context` (ChangeLog 10.21
item 54), pattern replication (ChangeLog 10.21 item 65), `startoffset` as a
synonym for `offset` (ChangeLog 10.21 item 98), modifier lists that split only
at commas (ChangeLog 10.21 item 47), `pushcopy` and `#popcopy` (ChangeLog
10.22 item 15), an `-error` option (ChangeLog 10.22 item 36), literal quoting
inside hexadecimal patterns (ChangeLog 10.22 item 2) and rejection of binary
zeros in input (ChangeLog 10.22 item 3 — part of the release, but no case
used here reaches it; see `TEST_INTERFACE.md` §7). A callout inside a
lookbehind must stop emitting an enormous run of spaces (ChangeLog 10.22
item 11).

The build-time `\C` lockout makes `\C` support a fact `pcre2test` has to
report, in both of the ways the driver reports configuration. A new query,
`-C backslash-C`, joins the table of options that answer `1` for true or `0`
for false and set the exit code to the same value; the manual documents it as
"`\C is supported (not locked out)`". Independently, `-C` with **no**
argument prints a line of its own in the configuration block: two leading
spaces, then exactly `\C is supported`, placed between the `\R matches ...`
line and `Internal link size = N`. In a build that locks `\C` out the same
line reads `  \C is not supported`; the standard build of this task is not
such a build (ChangeLog 10.21 item 58, implemented by the
`--never-backslash-C` configure option; `NEWS`, version 10.21 item 6;
`pcre2test.1`, "COMMAND LINE OPTIONS").

### Changes that pcre2test cannot see

`pcre2_find_bracket()` moves out of `pcre2_compile.c` into its own
compilation unit, breaking the circular dependency with `pcre2_study.c`
(ChangeLog 10.21 item 7); the duplicate `opcode_possessify` table goes
(ChangeLog 10.21 item 3); `memchr()` finds the first character of an
unanchored 8-bit match (ChangeLog 10.21 item 2); and `{1}` is ignored as a
quantifier, greedy, lazy or possessive alike (ChangeLog 10.21 item 10). None
may change a match result, an error or an offset; for the last, what must
hold is that `X{1}`, `X{1}?` and `X{1}+` behave exactly as `X`. Five
fuzzer-found compile-time overflows are closed in the same spirit: no pattern
may crash the compiler or corrupt memory, and each now ends in the diagnosis
this release's own transcripts show for it — some compile where they used to
overflow, some are still rejected (ChangeLog 10.21 items 5, 6, 8 and 11, and
ChangeLog 10.22 item 12). That last item is about the compiling workspace,
which is a fixed-size buffer: a sufficiently deeply nested pattern still
exhausts it and is diagnosed as `regular expression is too complicated`, at
the offset where the workspace fills — test 2's transcript shows that
diagnosis for two of its patterns and is the calibration for the offset.

### Unicode 8.0.0

The tables move from 7.0.0 to 8.0.0 (ChangeLog 10.21 item 12). This release's
generated tables are supplied under `/app/unicode-data/`; adopting them is
the work. The script names it recognises are listed in
(pcre2syntax.3, "SCRIPT NAMES FOR \p AND \P"); property lookups, `\X` and
case folding must follow the new data.

### What does not change

* The matcher is still recursive and its second limit is still the
  **recursion limit**, with `PCRE2_ERROR_RECURSIONLIMIT` and
  `(*LIMIT_RECURSION=)`. Heap frames and a depth limit are step 4.
* Compile-error offsets are still, in general, the right-most character read,
  and the error numbering is still broadly 10.20's. Both are rewritten
  wholesale at step 3.
* `pcre2_dfa_match()` still ignores the recursion limit, automatic callouts
  are still inserted around explicit callouts, back references are still not
  permitted inside lookbehinds, and `\g{+n}` is still not recognised. All
  four change at step 3.

## What you inherit, and must not break

Step 1 built the PCRE2 **10.20** engine from a skeleton: the two-pass
compiler, the recursion-based interpreting matcher, the DFA matcher, study,
substitution, serialization, the POSIX wrapper and all three code-unit
widths. All of it is in `/app` now and all of it is yours to modify,
including `src/pcre2test.c` and `src/pcre2_printint.c`, which were supplied
at step 1 and are never supplied again.

Besides the build and `pcre2test -C`, the gate runs a small number of narrow
end-to-end checks on behaviour introduced by *earlier* steps. If any part of
the gate fails, the step is not satisfied, however well the new milestone went.

Three inherited behaviours are most at risk here. The two match limits: this
step changes when recursion loops are detected and extends match-limit
checking into recursion, so it is easy to rename the second limit or to make
one control raise the error belonging to the other while in that code. The
gate drives `(*LIMIT_MATCH=)`, `(*LIMIT_RECURSION=)`, `match_limit=` and
`recursion_limit=` and compares `error -47` and `error -53` literally; the
counts `find_limits` prints are implementation-defined and are not part of
the requirement (`TEST_INTERFACE.md` §7). The compile-error
offsets step 1 got right: this step moves a specific handful, and every other
one must stay where it was. And the POSIX wrapper as a whole: only the
`REG_NOSUB` implication changes, while every other option mapping, the
`regexec()` behaviour and the `regerror()` messages stay put.

## Read these local references

Normative specification documents, under `/app/pcre2-doc/`. These are the
PCRE2 10.22 manual pages, copied verbatim; they contain no implementation
and no test expectations.

* **`pcre2posix.3`** — the POSIX wrapper, and the page that records this
  step's headline change: which POSIX flags map to which PCRE2 options, what
  `REG_NOSUB` does and no longer does, what `regexec()` ignores, and the
  `regerror()` messages.
* **`pcre2test.1`** — the driver, which is now yours. Its command line, its
  modifier grammar and the exact shape of every line it prints. Several
  modifiers and one command-line option are added at this release, and the
  page documents them as they must behave.
* **`pcre2api.3`** — the native API, including the options, context settings,
  substitution flags and `pcre2_pattern_info()` requests added here, and the
  full list of compile-time and match-time error codes.
* `pcre2.3` — the introduction and the index of the other pages.
* `pcre2build.3` — the build-time options and what each one turns on.
* `pcre2callout.3` — the callout interface, the fields passed to a callout,
  and the cases where an optimisation means no callout happens.
* `pcre2compat.3` — where this release deliberately differs from Perl.
* `pcre2limits.3` — the size limits, including the two that are new at this
  release: the maximum lookbehind length and the caller-imposed maximum
  pattern length.
* `pcre2matching.3` — the two matching algorithms compared.
* `pcre2partial.3` — partial matching with both functions, and restarting.
* `pcre2pattern.3` — the pattern syntax in detail: escapes, classes,
  quantifiers, groups, conditions, assertions, recursion, the backtracking
  verbs and the start-of-pattern items.
* `pcre2serialize.3` — serializing compiled patterns and reloading them.
* `pcre2stack.3` — how the recursive matcher uses the system stack and how
  the recursion limit bounds it.
* `pcre2syntax.3` — the syntax quick reference, including the list of script
  names this release recognises under Unicode 8.0.0.
* `pcre2unicode.3` — UTF and UCP modes, property support, and the UTF
  validity rules and their error codes in all three widths.
* `pcre2test.txt` — the plain-text rendering of `pcre2test.1`, for grepping.

Additional files:

* `/app/pcre2-doc/README.md` — how the manual pages are organised and which one
  answers which kind of question.
* `/app/pcre2-doc/PROFILE.md` — the subset of PCRE2 this task's milestone
  covers, and what is out of scope at every step.
* `/app/pcre2-doc/ChangeLog.transition` — upstream's own numbered change
  entries for exactly the release(s) this step covers, copied verbatim. This is
  the authoritative list of things that must become true during this step;
  where the prose above cites an item number, it is an item in this file.
* `/app/pcre2-doc/NEWS` — upstream's shorter release narrative, useful for
  orientation before reading the ChangeLog entries.
* `/app/TEST_INTERFACE.md` — the public contract: layout, build command,
  binary name, invocation grammar, output comparison rule, and what is
  explicitly **out of scope**. Read it before you write anything.
* `/app/WORKSPACE_HANDOFF.md` — what `/app` is, what is preserved between
  steps, and the end-of-step checkpoint.
* `/app/testdata/` — this release's own test corpus, both the `testinput*`
  files and the `testoutput*` transcripts upstream expects from them. Use them
  to check yourself. The corpus used here is larger than this: it uses these files
  byte for byte **and** held-out inputs you never see — see the section "What
  you can see, and what you cannot" in `/app/TEST_INTERFACE.md`.

## Meta requirements

* Your budget for this step is 2 hours. `bash /app/timer.sh` prints the
  remaining time; the configured timeout is authoritative and the snapshot is
  taken when it expires, not when you stop.
* `/app` is a cumulative workspace: everything under it, `build/` included,
  carries forward to the next step, which begins in a fresh conversation with
  no memory of this one. The checker runs `make` over the tree as you leave
  it — it does not clean first — so keep a left-over `build/` consistent with
  `src/`, or remove it. Leave it building.
* **This environment has no network access.** The toolchain and everything the
  step needs are already installed, and nothing can be fetched, cloned or looked
  up — there is no dependency to install and no documentation to reach. The
  reference material and fixtures shipped with the task are the whole of what you
  are given, and the artifact you deliver must be your own implementation of the
  contract: not a copy, fork, vendored or reconstructed source tree from any
  project, including PCRE2 itself, and not any task-specific answer from
  elsewhere.
* Do not inspect `/tests`, any later step, or any solution material.
* The checker rebuilds your tree from source with `make` and runs the binary it
  produces. It does not read your own tests, your build logs, or any file you
  generate for your own use.
