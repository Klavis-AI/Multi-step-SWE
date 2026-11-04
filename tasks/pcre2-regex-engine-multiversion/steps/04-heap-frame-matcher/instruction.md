# Step 04 of 13 — The heap-frame engine

## Implement this milestone

This step moves the workspace from PCRE2 10.23 to **PCRE2 10.30**, the
largest behavioural change in the ladder after the compiler rewrite. The
interpreting matcher is rebuilt to keep backtracking state in a vector of
fixed-size frames rather than in C stack frames, and because the recursion
is gone the "match limit recursion" is renamed to the **depth limit**
everywhere a user can see it. Around that sit new compile options, a second
option word on the compile context (`PCRE2_EXTRA_*`), and an experimental
pattern-conversion API. A candidate working from modern PCRE2 gets the era
wrong both ways: the *old* limit names must still work as synonyms, while
`PCRE2_INFO_EXTRAOPTIONS`, script runs, alphabetic assertion names and
"kibibytes" in `-C` output do not exist yet.

### The interpreter stops recursing, and recursion becomes backtrackable

`pcre2_match()` no longer uses recursive function calls, and so the C stack,
to remember backtracking positions (ChangeLog 10.30 item 1). The refactor
itself is invisible to the checker; what is in scope is the behaviour it buys.

* A pattern may now **backtrack into a recursive group call**, as Perl does:
  recursions that behaved as though atomic must retry alternatives inside
  the recursed group (`pcre2pattern.3`, "RECURSIVE PATTERNS"). Marks set in a
  recursion are passed out of it, verbs inside recursions behave as
  documented, and a capture inside a negative assertion used as a condition
  that succeeds via `(*ACCEPT)` is retained (ChangeLog 10.30 item 1).
* The `\=memory` subject modifier reports allocation *sizes* only, with no
  addresses and no `stack_malloc` or `stack_free` lines, and warns when both
  pattern and subject use a null context (ChangeLog 10.30 item 17).
* `PCRE2_INFO_FRAMESIZE` and a `framesize` modifier are added (ChangeLog
  10.30 item 6); the line it prints is deleted by the comparison filter, so
  it is not compared directly but must not make a pattern fail.

### "Match limit recursion" becomes "depth limit", and a heap limit appears

The depth limit still bounds the depth of the search tree; only the name
changes, and the old names stay available (ChangeLog 10.30 item 2). Beside it
an explicit limit on the heap `pcre2_match()` may use is added (ChangeLog
10.30 item 22). Every item below is visible in `pcre2test` output.

* `pcre2test -C` prints `Default heap limit`, `Default match limit` and
  `Default depth limit`, in that order. The 10.23 lines
  `Default recursion depth limit` and `Match recursion uses ...` are gone,
  the second being meaningless once the matcher never recurses
  (`pcre2build.3`, "LIMITING PCRE2 RESOURCE USAGE", for the three limits).
  Everything else in that block is what 10.23 printed, and one line of it is
  easy to lose because nothing in `/app/testdata/` ever shows the block: the
  `  \C is supported` line 10.21 added to it, which sits between the
  `\R matches ...` line and `Internal link size` (`pcre2test.1`, "COMMAND
  LINE OPTIONS", for the `\C` support the line reports). A held-out case
  runs `-C` with no argument and compares the whole block as printed, so
  every line of it is in scope — the version banner (`PCRE2 version 10.30
  2017-08-14`, which `-C` prints even with `-q`), the width lines, the `\C`
  line and the three limit lines alike.
* The `info` modifier prints `Depth limit = N` where 10.23 printed
  `Recursion limit = N`, and `Heap limit = N` too, when a pattern sets them.
  The limit lines sit after the capturing-subpattern count and come in the same
  order as the `-C` block above — heap, then match, then depth — and only the
  limits the pattern sets are printed.
* `(*LIMIT_DEPTH=d)` and `(*LIMIT_HEAP=d)` are added at the start of a
  pattern, with `(*LIMIT_RECURSION=d)` kept as a synonym for the first
  (`pcre2pattern.3`, "Setting match resource limits"). The modifiers are
  `depth_limit=d` and `heap_limit=d`, with `recursion_limit` an obsolete
  synonym (`pcre2test.1`, "Setting heap, match, and depth limits").
* The error texts become `matching depth limit exceeded` and
  `heap limit exceeded`, and `pcre2_config()` answers depth-limit and
  heap-limit queries (`pcre2api.3`, "CHECKING BUILD-TIME OPTIONS").
* `find_limits` reports `Minimum heap limit`, `Minimum match limit` and
  `Minimum depth limit` lines (ChangeLog 10.30 item 15), and works with DFA
  matching, since those limits apply there too (ChangeLog 10.30 item 36). The
  heap limit applies only when the interpreter matches, not to DFA
  (`pcre2pattern.3`, "Setting match resource limits"), so a run that matches
  under DFA prints the `Minimum match limit` and `Minimum depth limit` lines
  only. This release's own test 6 transcript matches under DFA throughout and
  shows the exact line set for both cases.

### PCRE2_ENDANCHORED, PCRE2_EXTENDED_MORE, /xx and (?xx)

`PCRE2_ENDANCHORED` is both a compile and a match option requiring the match
to end at the end of the subject, applies to `pcre2_dfa_match()` too, and is
the `endanchored` modifier on pattern and subject lines (ChangeLog 10.30
item 19; `pcre2api.3`, "COMPILING A PATTERN").
`PCRE2_EXTENDED_MORE` does everything `PCRE2_EXTENDED` does and also ignores
an unescaped space or horizontal tab **inside a character class**
(ChangeLog 10.30 item 29; `pcre2pattern.3`, "INTERNAL OPTION SETTING"). It is
set as `(?xx)`, unset as `(?-xx)`, and is the `extended_more` modifier with
`/xx` as its short spelling; `(?x)` does not imply `(?xx)`.

### Extra compile options on the compile context

A **second option word**, independent of the bits passed to
`pcre2_compile()`, is introduced on the compile context (`pcre2api.3`,
"Extra compile options"), with its first two members:

* `PCRE2_EXTRA_ALLOW_SURROGATE_ESCAPES`, permitting `\x{d800}`-range escapes
  in UTF-8 and UTF-32 mode (ChangeLog 10.30 item 33). Attempting it in
  UTF-16 mode is a compile error with its own message and a new error number
  appended after the existing codes; no existing error number changes value.
* `PCRE2_EXTRA_BAD_ESCAPE_IS_LITERAL`, turning an unrecognized escape into
  the literal character instead of a compile error (ChangeLog 10.30 item 37).

These are the `allow_surrogate_escapes` and `bad_escape_is_literal` pattern
modifiers; nothing reads the extra options back off a compiled pattern yet.

### PCRE2_LITERAL, whole-line and whole-word matching, and POSIX

* `PCRE2_LITERAL` makes the entire pattern a literal string and supports
  POSIX `REG_NOSPEC` (ChangeLog 10.30 item 41); combining it with option bits
  that cannot apply to a literal is a compile error with a new message and
  number appended after the existing codes. The modifiers are `literal` and
  `subject_literal` (ChangeLog 10.30 item 40).
* `PCRE2_EXTRA_MATCH_LINE` and `PCRE2_EXTRA_MATCH_WORD` wrap the pattern so
  it matches only a complete line or word, as `match_line` and `match_word`
  (ChangeLog 10.30 item 42; `pcre2api.3`, "Extra compile options").
* The POSIX wrapper gains `REG_PEND` (ChangeLog 10.30 item 39) and corrects
  offsets returned when `REG_STARTEND` has a non-zero starting offset
  (ChangeLog 10.30 item 38), reached through `posix` and `posix_startend`.

### Experimental pattern conversion

An experimental foreign-pattern conversion facility is added (ChangeLog
10.30 item 50), documented in the new `pcre2convert.3` page: a convert
context with a glob escape and separator, and a conversion function accepting
POSIX basic and extended patterns and glob dialects (`pcre2convert.3`,
"CONVERTING GLOBS" and "CONVERTING POSIX PATTERNS"). `pcre2test` drives it
with `convert`, `convert_length`, `convert_glob_escape` and
`convert_glob_separator`; a failed conversion reports an offset and a message
(`pcre2convert.3`, "THE CONVERSION FUNCTION").

A successful conversion prints the converted pattern before compiling it, so
the *text its converter emits* is part of the transcript and is compared like
any other output. This release's own test 24 and test 25 transcripts
(`/app/testdata/testoutput24` and `testoutput25`) exhibit that text — the
`(?s)\A ... \z` wrapper it builds for a glob, for instance — and are the
calibration for it.

### Smaller syntax and matching changes

* `(?n:` is accepted for `PCRE2_NO_AUTO_CAPTURE` over a group, as in Perl
  (ChangeLog 10.30 item 30; `pcre2pattern.3`, "INTERNAL OPTION SETTING").
* A NUL newline convention is added: `PCRE2_NEWLINE_NUL`, the pattern item
  `(*NUL)`, `NUL` in the `newline` modifier and in `pcre2test -C newline`,
  and `Forced newline is NUL` in `info` output
  (ChangeLog 10.30 item 34; `pcre2pattern.3`, "Newline conventions").
* A hyphen ending a class is a literal (ChangeLog 10.30 item 52), while
  `[\d-X]` is an error as `[[:digit:]-X]` was (ChangeLog 10.30 item 53).
* Auto-anchoring looks past a zero-quantified group or a single-branch false
  condition such as `(?(DEFINE)...)` (ChangeLog 10.30 item 21), and anchored
  patterns record first-code-unit data (ChangeLog 10.30 item 46); see `info`.
* The tables move from Unicode 8.0.0 to 10.0.0 (ChangeLog 10.30 item 47) and
  grapheme breaking follows the current Unicode Standard Annex #29
  (ChangeLog 10.30 item 49): `\p`, `\P`, script names and `\X` all answer
  for the newer rules.

### What does not change

* **Extra options cannot be read back from a compiled pattern.** No
  `PCRE2_INFO_EXTRAOPTIONS`, no `Extra options:` line in `info`; step 05.
* **`PCRE2_EXTENDED` still ignores only the white space its character tables
  recognize**, `\N{U+dddd}` and `(?^)` are still errors, and `(*ACCEPT:ARG)`,
  `(*FAIL:ARG)` and `(*COMMIT:ARG)` still take no argument; all step 05.
* **No script runs and no alphabetic assertion names.** `(*script_run:`,
  `(*sr:`, `(*pla:`, `(*atomic:` and their siblings stay unrecognized until
  step 06, as do `PCRE2_EXTRA_ALT_BSUX` and `PCRE2_EXTRA_ESCAPED_CR_IS_LF`.
* **`pcre2test -C` prints the heap limit as a bare number.** `kibibytes` is
  appended to that line at step 06, not here.

## What you inherit, and must not break

Step 03 left you at PCRE2 10.23: the two-phase compiler that parses a
pattern into an intermediate form before generating code, `pcre2_substitute()`
with its extended replacement syntax, and the `(?(VERSION>=x.y)` condition.
All of that must keep working at 10.30 while the matcher underneath it is
replaced.

The gate re-checks a small number of narrow behaviours from earlier steps end
to end, not just this step's milestone. If any one of them fails, the step is
not satisfied, however complete the heap-frame engine is.

The behaviours most at risk from *this* change are the ones the old recursive
matcher got right by accident of its structure: **backtracking verbs**
(`(*PRUNE)`, `(*SKIP)`, `(*THEN)`, `(*COMMIT)`) and `(*MARK)` names, whose
scope now has to be carried explicitly in frames rather than falling out of
the C call stack; **captures and conditions across recursion**, where a
frame-based engine that forgets to restore capture state on backtracking
gives different `(?(R)...)` and `\1` results; and the **substitution
function**, which drives `pcre2_match()` repeatedly and depends on the
`PCRE2_NOTEMPTY_ATSTART` bumpalong behaviour being unchanged. That list is
where this change is most likely to bite, not the gate's full inventory: the
gate also re-checks each earlier step's headline behaviour end to end — step
2's `REG_NOSUB`/`posix_nosub` semantics and step 3's diagnostics among them —
so re-run the earlier checkpoints too, not only the areas named here.

## Read these local references

Normative specification documents, under `/app/pcre2-doc/`. These are the
PCRE2 10.30 manual pages, copied verbatim; they contain no implementation
and no test expectations.

* **`pcre2api.3`** — the whole native API at 10.30: the compile context and
  its new extra-options word, the match context with the heap limit and the
  renamed depth limit, `pcre2_config()` items, and every
  `pcre2_pattern_info()` item including the new frame size.
* **`pcre2pattern.3`** — pattern syntax: `(*LIMIT_DEPTH)`, `(*LIMIT_HEAP)`,
  `(*NUL)`, `(?xx)`, `(?n)`, and the recursion section that now describes
  backtracking into recursive calls.
* **`pcre2test.1`** — the harness's input grammar and every modifier,
  including the ones new at this release (`depth_limit`, `heap_limit`,
  `endanchored`, `extended_more`, `literal`, `convert*`, `posix_startend`).
* `pcre2test.txt` — the same page rendered as plain text.
* `pcre2convert.3` — new at this release: the experimental glob and POSIX
  pattern conversion API and its convert context.
* `pcre2syntax.3` — the quick syntax summary, including the option-setting
  and newline-convention tables.
* `pcre2unicode.3` — UTF validity rules and the property support that the
  Unicode 10.0.0 tables back.
* `pcre2callout.3` — the callout block and its fields, and the optimizations
  that can suppress callouts.
* `pcre2posix.3` — the POSIX wrapper, now with `REG_NOSPEC` and `REG_PEND`.
* `pcre2matching.3` — the two matching algorithms compared.
* `pcre2partial.3` — partial matching semantics and restarting.
* `pcre2serialize.3` — saving and restoring compiled patterns.
* `pcre2limits.3` — hard limits on pattern and subject sizes and nesting.
* `pcre2compat.3` — where behaviour differs from Perl.
* `pcre2build.3` — build-time configuration and defaults, including the
  default heap and depth limits `pcre2test -C` reports.
* `pcre2.3` — the top-level index of the documentation.

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
