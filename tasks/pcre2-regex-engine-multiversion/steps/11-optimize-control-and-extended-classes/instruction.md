# Step 11 of 13 — Limits, optimisation control, extended classes

## Implement this milestone

This step is **pcre2-10.45**, absorbing 10.44 as well; it is the largest jump
in this ladder. Three arrivals change the shape of the engine rather than its
vocabulary: an interface for turning individual compile-time optimisations on
and off, an assertion that matches a subpattern against text captured by
another group, and two extended class syntaxes with set operations. Two
earlier behaviours are also reversed, which is where a candidate working from
memory will go wrong: `Lu`, `Ll` and `Lt` become interchangeable under
caseless matching, and `\x` with nothing usable after it becomes an error.

### Caller control over optimisations

* `pcre2_set_optimize()` sets directives in a compile context.
  `PCRE2_OPTIMIZATION_FULL` and `PCRE2_OPTIMIZATION_NONE` enable and disable
  all optional optimisations at once; `PCRE2_AUTO_POSSESS`,
  `PCRE2_DOTSTAR_ANCHOR` and `PCRE2_START_OPTIMIZE`, each with an `_OFF`
  counterpart, control them individually, with results indistinguishable from
  the matching start-of-pattern item (ChangeLog 10.45 item 32; pcre2api.3,
  "PCRE2 CONTEXTS"; pcre2pattern.3, "Disabling start-up optimizations").
* `pcre2test` exposes `optimization_full`, `optimization_none`,
  `auto_possess`, `dotstar_anchor` and `start_optimize`, the last three each
  with an `_off` form (pcre2test.1, "PATTERN MODIFIERS"). The study pass built
  at step 1 thus
  becomes externally observable for the first time — not through the compiled
  form, which the comparison filters out, but through the pattern information
  `pcre2test` prints and through results where a start-up optimisation changes
  the outcome.

### Scan substring assertions

* `(*scan_substring:...)`, abbreviated `(*scs:...)`, is followed by a
  parenthesised list of group numbers (absolute or relative) and/or names in
  single quotes or angle brackets, then a subpattern applied to the captured
  text (ChangeLog 10.45 item 17; pcre2pattern.3, "SCAN SUBSTRING ASSERTIONS";
  pcre2syntax.3, "SUBSTRING SCAN ASSERTION").
* Groups are examined in the order written and the first found to be set is
  scanned; under `PCRE2_DUPNAMES` all groups sharing an ambiguous name are
  checked in numerical order. If none is set, the assertion fails.
* The scan is always anchored at the start of the substring, with no
  bumpalong. End of subject is temporarily reset to the end of the substring
  so `\Z`, `\z` and `$` match there, but start of subject is **not** reset, so
  `^` matches only if the substring is at the start of the main subject, and
  lookbehinds may reach back before it.
* Capturing groups inside the scanned subpattern keep their values for the
  ongoing match if it succeeds. The feature needs capturing, so
  `pcre2_dfa_match()` does not support it.

### Two extended character class syntaxes

* Perl's `(?[...])` is always available: an expression syntax whose atoms are
  escape sequences, character types, POSIX classes and nested ordinary
  classes, with binary operators `&`, `|` or `+`, `-` and `^`, prefix `!`, and
  `PCRE2_EXTENDED_MORE` always in force. Bare literals and ranges are not
  atoms, and `^` is an operator, not a negation (ChangeLog 10.45 item 42;
  pcre2pattern.3, "PERL EXTENDED CHARACTER CLASSES").
* `PCRE2_ALT_EXTENDED_CLASS` instead changes ordinary `[...]` classes to the
  UTS#18 form: `[` becomes a metacharacter inside a class, so a literal one
  must be escaped, and `||`, `&&`, `--` and `~~` denote union, intersection,
  subtraction and symmetric difference (ChangeLog 10.45 item 41;
  pcre2pattern.3, "UTS#18 EXTENDED CHARACTER CLASSES").
* In the UTS#18 form, nesting is allowed and nested classes are themselves
  extended; juxtaposition is an implicit union binding tighter than any
  explicit operator; precedence between explicit operators is undefined, so
  mixing two without brackets is an error. Ordinary class parsing is reworked
  at the same time and must stay correct (ChangeLog 10.45 item 29).

### Two reversals of inherited behaviour

* When caseless matching is in force, the properties `Ll`, `Lt` and `Lu` are
  all treated as `Lc`, so `\p{Lu}` caselessly matches a lower case letter
  (ChangeLog 10.45 item 6; pcre2pattern.3, "The general category property for
  \p and \P").
* `\x` not followed by `{` or a hexadecimal digit is now a compile error; it
  previously meant NUL (ChangeLog 10.45 item 25; pcre2pattern.3, "Non-printing
  characters").

### Replacement strings

* `$&` for the whole match, `` $` `` for the text before it, `$'` for the text
  after it and `$_` for the entire subject; in extended mode `\b` and `\v` are
  backspace and vertical tab (ChangeLog 10.45 item 33; pcre2api.3, "CREATING A
  NEW STRING WITH SUBSTITUTIONS"; pcre2syntax.3, "REPLACEMENT STRINGS").
* The JavaScript `$<name>` and Python `\g<n>` forms, where `n` may be a group
  name or a number (item 30), Python-style backreferences (item 26), named
  captures with non-ASCII names (item 28), octal escapes written `\045` or
  `\111` (item 21), and corrected reporting of over-large octal escapes (27).
* Title casing: `\u` followed immediately by `\L`, or `\l` by `\U`, forces the
  first character one way and the rest the other, so `\u\L$1` turns "heLLo"
  into "Hello" (item 24). `pcre2_set_substitute_case_callout()` lets a caller
  supply its own case transformation (item 36).

### Casing, limits and new option bits

* `PCRE2_EXTRA_TURKISH_CASING` alters the case behaviour of `i`, `I`, U+0130
  and U+0131; `(*TURKISH_CASING)` and `(*CASELESS_RESTRICT)` set it and the
  caseless restriction from within a pattern (ChangeLog 10.45 item 40;
  pcre2pattern.3, "CHARACTERS AND METACHARACTERS", which discusses both verbs;
  pcre2syntax.3 lists them as verb names); and caseless
  backreference matching now honours `PCRE2_EXTRA_CASELESS_RESTRICT` (37).
* The maximum length of a capture group name rises from 32 to 128 code units
  (ChangeLog 10.44 item 3; pcre2pattern.3, "NAMED CAPTURE GROUPS").
  `pcre2limits.3` still prints the old figure; it is the stale page here.
* `pcre2_set_max_pattern_compiled_length()` limits compiled pattern size,
  excluding internal structure overhead, and `pcre2test`'s memory size
  reporting returns to its pre-10.44 form so that 32-bit output matches the
  other widths (ChangeLog 10.44 item 6; ChangeLog 10.45 item 1).
* `PCRE2_EXTRA_NO_BS0` locks out `\0` unless a further octal digit follows,
  `PCRE2_EXTRA_PYTHON_OCTAL` selects Python's rules for backslash followed by
  digits (ChangeLog 10.45 item 31; pcre2pattern.3, "Python rules for non_class
  backslash 1-9"), and `PCRE2_EXTRA_NEVER_CALLOUT` makes a callout-bearing
  pattern fail to compile (item 34).
* The Unicode tables move to 16.0.0 (ChangeLog 10.45 item 35); adopting the
  supplied tables is in scope, deriving them is not. `\X` was matching too
  much: a break must occur between two Extended Pictographic characters unless
  a zero-width joiner intervenes (ChangeLog 10.44 item 7).

### Corrections that change reported results

* A variable-length lookbehind used as a conditional group's condition could
  be treated as true when a branch matched but overran the current position
  (ChangeLog 10.45 item 13); one containing a capture group referenced by
  another lookbehind could be miscompiled (ChangeLog 10.44 item 1); and
  auto-possessification at a lookbehind's end now checks every branch for
  variability (ChangeLog 10.45 item 12).
* Incorrect DFA optimisation code inherited from PCRE1 is removed; it could
  return no match where a partial match was correct, in patterns involving
  `(*F)` (ChangeLog 10.45 item 10). `\Z` now behaves correctly in fragments of
  invalid UTF strings, completing the `\z` fix of the previous step (item 20),
  white space before `^` in `\p{ ^L }` is ignored (item 8), and first/last
  code unit optimisation is fixed with lookaheads (items 16, 18).
* `pcre2test`'s bad-modifier error messages are reworded and reformatted, and
  single-letter modifier sequences are restricted to the first item of a list
  (ChangeLog 10.45 item 11).

### What does not change

* There is no `pcre2_next_match()`, and no way for a subroutine or recursion
  call to return capture values to its caller; both arrive at step 12. `$+` is
  not yet a replacement-string item either; it is added at step 12.
* `pcre2_dfa_match()` gains nothing here: scan substring assertions are
  unavailable to it, and this release's DFA change removes an optimisation.
* The options and items added here are opt-in: with none set, every pattern
  that worked at step 10 must give the same result, the two reversals apart.

## What you inherit, and must not break

Step 10 brought the workspace to **pcre2-10.43**, which added variable-length
lookbehind assertions with a per-branch maximum defaulting to 255 characters,
redefined `\w`, `\b`, `\B` and `[:xdigit:]` in UCP mode, and added the
`PCRE2_EXTRA_ASCII_*` family for restricting them back. All of it stays. The
gate re-checks narrow behaviours from earlier steps end to end, and breaking
one means the step is not satisfied however well the milestone went.

Because this is the widest step in the ladder, the inherited surface at risk
is correspondingly wide. **Ordinary character classes** are the first concern:
their parsing is reworked here to support two new extended syntaxes, and with
neither of those enabled every class that compiled at step 10 must still
behave identically, including POSIX classes, ranges and negation. **Existing
replacement strings** are the second: `$` handling gains many new forms, and a
replacement using none of them must produce byte-identical output.
**Optimisation defaults** are the third: with no directive set,
`pcre2_set_optimize()` must leave auto-possessification, dot-star anchoring
and the start-up optimisations exactly as step 10 left them, so that the
pattern information printed for an untouched pattern does not move.

## Read these local references

Normative specification documents, under `/app/pcre2-doc/`. These are the
PCRE2 10.45 manual pages, copied verbatim; they contain no implementation
and no test expectations.

* **`pcre2pattern.3`** — the normative pattern syntax. The milestone lives in
  "PERL EXTENDED CHARACTER CLASSES", "UTS#18 EXTENDED CHARACTER CLASSES",
  "SCAN SUBSTRING ASSERTIONS", and the two casing verbs discussed under
  "CHARACTERS AND METACHARACTERS".
* **`pcre2api.3`** — the public API. "PCRE2 CONTEXTS" documents
  `pcre2_set_optimize()` and the other new setters; "CREATING A NEW STRING
  WITH SUBSTITUTIONS" the whole replacement vocabulary added here.
* **`pcre2test.1`** — command-line options, modifiers and the shape of the
  default output. This release adds a large block of modifiers; "PATTERN
  MODIFIERS" is the authority on their names and spellings.
* `pcre2syntax.3` — the compact quick reference, with "PERL EXTENDED CHARACTER
  CLASSES", "SUBSTRING SCAN ASSERTION" and a new "REPLACEMENT STRINGS"
  section that summarises the substitution syntax in one table.
* `pcre2unicode.3` — UTF validity, UCP mode, case equivalence including the
  Turkish casing rules, script runs and matching in invalid UTF strings.
* `pcre2matching.3` — how the two matching algorithms differ, which explains
  why scan substring assertions are unavailable to the DFA matcher.
* `pcre2limits.3` — the hard limits. Note that its statement of the maximum
  capture group name length has not been updated at this release.
* `pcre2compat.3` — the catalogue of differences from Perl, several of which
  this release removes.
* `pcre2.3` — the top-level introduction and index to the other pages.
* `pcre2callout.3` — the callout interface and the callout block fields.
* `pcre2partial.3` — partial matching, its restrictions and restarting;
  relevant because disabling start-up optimisations changes partial results.
* `pcre2serialize.3` — saving and restoring compiled patterns.
* `pcre2posix.3` — the POSIX-style wrapper functions.
* `pcre2convert.3` — the experimental glob and POSIX pattern converters.
* `pcre2build.3` — the build-time configuration options and what each enables.
* `pcre2test.txt` — a plain-text rendering of `pcre2test.1`, convenient for
  grepping.

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
