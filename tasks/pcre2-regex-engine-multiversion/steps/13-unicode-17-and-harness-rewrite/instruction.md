# Step 13 of 13 — Unicode 17 and the harness rewrite

## Implement this milestone

This step is **pcre2-10.48**, the last in the ladder. Its centre of gravity is
data rather than syntax: Unicode moves to 17.0.0 and every generated table is
replaced, so the answers to property, script and grapheme queries change
wholesale while the pattern language barely moves. Around that sit possessive
repeats of backreferences, partial-match support in substitution, and a small
set of matching and compilation fixes. A candidate reaching for the newest
feature list will look for syntax that is not here, and most of what remains
in the release notes is JIT, `pcre2grep` and build work that is out of scope.

### Unicode 17.0.0

* Unicode support moves to 17.0, including new scripts, new characters, new
  property values, new case mappings and new emoji data (ChangeLog 10.48 item
  3). Every table is regenerated; the tables themselves are supplied. The
  Unicode version string reported through configuration enquiry must be the
  new one (pcre2api.3, "CHECKING BUILD-TIME OPTIONS").
* This reaches every construct consulting them: `\p` and `\P` for categories,
  scripts, script extensions and binary properties; `\X` and its grapheme
  cluster rules; script runs; and caseless matching (pcre2unicode.3, "UNICODE
  PROPERTY SUPPORT"; pcre2unicode.3, "UNICODE CASE-EQUIVALENCE").
* The property and script-extension conformance transcripts are themselves
  generated from the Unicode Character Database, so they change by hundreds of
  lines with no change in engine logic: code points unassigned at 16.0 acquire
  scripts and categories, and some existing ones move. Adopting the tables but
  leaving one derived structure stale — a case-folding list, a
  script-extension list, a grapheme-break table — scatters wrong answers
  rather than producing one failure.

### Character classes across internal range boundaries

* Classes combining characters from more than one internal range were compiled
  and matched incorrectly: those mixing characters at or below U+00FF with
  characters at U+0100 and at U+8000 or above (ChangeLog 10.48 item 6;
  pcre2pattern.3, "SQUARE BRACKETS AND CHARACTER CLASSES"). Explicit lists and
  ranges spanning the boundary are both affected, and the failure is a wrong
  match result, not an error.
* Two compilation failures involving extended, Boolean character classes are
  fixed: such a class could be used neither before a lookbehind nor inside a
  lookaround (ChangeLog 10.48 item 34; pcre2pattern.3, "PERL EXTENDED
  CHARACTER CLASSES"; pcre2pattern.3, "UTS#18 EXTENDED CHARACTER CLASSES").
  Patterns that failed to compile must now compile and match.

### Possessive repeats of backreferences

* A backreference may be repeated possessively — `\1*+`, `\1++`, `\1?+`,
  `\1{2,}+`, `\1{3,5}+` — with direct support rather than an internal atomic
  wrapper; an exact repeat such as `\1{3}` is possessive in any case
  (ChangeLog 10.48 item 8; pcre2pattern.3, "BACKREFERENCES"; pcre2pattern.3,
  "ATOMIC GROUPING AND POSSESSIVE QUANTIFIERS").
* The change of internal representation is not observable: the compiled form
  is removed before comparison. What is observable is the match results, which
  must be those of a greedy repeat inside an atomic group — the two have the
  same meaning.

### Substitution with partial matches

* `pcre2_substitute()` supports partial matching, with limits. A match that
  succeeds only partially returns `PCRE2_ERROR_PARTIAL`. When
  `PCRE2_PARTIAL_HARD` or `PCRE2_PARTIAL_SOFT` is passed,
  `PCRE2_SUBSTITUTE_REPLACEMENT_ONLY` must be set too, or
  `PCRE2_ERROR_BADOPTION` is returned (ChangeLog 10.48 item 10; pcre2api.3,
  "CREATING A NEW STRING WITH SUBSTITUTIONS").
* The replacement items `$'` and `$_` cannot be expanded for a partial match,
  so they return the new `PCRE2_ERROR_PARTIALSUBS` whenever partial matching
  is requested — even if the match found is complete. The code is `-76`, its
  message `replacement $' or $_ not supported with partial match`, reported
  with an offset within the replacement.
* `PCRE2_NO_UTF_CHECK` may now differ between an existing match and a call to
  `pcre2_substitute()` with `PCRE2_SUBSTITUTE_MATCHED`, instead of being
  rejected with `PCRE2_ERROR_DIFFSUBSOPTIONS` (ChangeLog 10.48 item 5). The
  other consistency requirements step 12 introduced stay in force; only this
  option is exempted.
* A regression in extended replacement strings is fixed: repeated backslash
  escapes were mishandled, so more than one escaped character in sequence
  produced the wrong text (ChangeLog 10.48 item 9).

### Compile-time diagnostics and offsets

* An incomplete `\x{`, `\o{` or `\N{U+` escape at the very end of a pattern
  reported an error offset one past the end of it; the offset now equals the
  pattern length — the position immediately after the final character, one
  position left of where 10.47 put it (ChangeLog 10.48 item 28;
  pcre2pattern.3, "Non-printing characters"). Message and offset are both
  compared, so the offset alone can lose a case.
* Bounds checks are added for several integer overflows during compilation
  that could under-allocate and then write out of bounds, described as
  affecting 32-bit CPUs (ChangeLog 10.48 item 36). A pattern that overflows a
  size computation must report a compile error.

### Invalid UTF and DFA workspace limits

* Backward scans through invalid UTF data must not read before the start of
  the checked subject: a lower buffer bound fixes two out-of-bounds reads
  while scanning backwards with `PCRE2_MATCH_INVALID_UTF` (ChangeLog 10.48
  item 37; pcre2unicode.3, "MATCHING IN INVALID UTF STRINGS"). It is visible
  through lookbehinds, `\X` and `\b` used just after an invalid sequence, in
  UTF-8 and UTF-16 mode alike, where the answer is often no match.
* An out-of-bounds write in DFA matching under a heap limit is fixed, with
  integer overflows that could under-allocate the workspace (ChangeLog 10.48
  item 35; pcre2pattern.3, "Setting match resource limits"). A deeply nested
  pattern run under `pcre2_dfa_match()` with a small `(*LIMIT_HEAP=)` must
  report the heap limit error, the workspace resizing and being re-used, not
  overrun.

### Interface details, and what the checked binary cannot show

* `pcre2test`'s colour option gains an environment override: with no colour
  option given the default is `auto` unless `NO_COLOR` is defined and
  non-empty, and the option with no specifier means `always` (pcre2test.1,
  "COMMAND LINE OPTIONS"). Output to a file carries no escapes either way.
* Per-frame match memory is reduced by shrinking temporary fields (ChangeLog
  10.48 item 7). The frame size line is removed before comparison, so the
  reduction is not required; what matters is that the heapframes sizes
  `pcre2test` reports, which are compared, are unchanged from the previous
  release.
* Upstream rewrites its own test harness script here, so that output goes to
  per-configuration directories and a failure no longer stops the run. That
  script plays no part in checking; the binary that is checked is invoked
  directly, as at step 12.
* The rest of the release is outside this task: the JIT fixes (ChangeLog 10.48
  items 12, 20, 22, 25, 27), the `pcre2grep` fixes (items 13, 23, 30, 31), the
  pattern-conversion overflow where `size_t` is 32 bits (item 26), the CMake,
  Zig and `pcre2-config` work, the branch rename (item 4) and the
  support-lifecycle policy (item 21). Two memory-management fixes change no
  printed line either: the leak and stale-pointer free in
  `pcre2_deserialize_decode()` (item 16) and the uninitialised serialization
  padding (item 1). All appear in `ChangeLog.transition`, and none can be
  seen through the binary that is checked.

### What does not change

* No new pattern syntax arrives with the Unicode update: `\p`, `\P` and `\X`
  are spelled exactly as at step 12, and only the answers change.
* `pcre2_next_match()`, the capture-returning recursion syntax, the scan
  substring assertion, the `-E` option and the `#if ... #endif` input commands
  are all inherited from step 12 unchanged.
* Existing substitution behaviour is untouched where partial matching is not
  requested: `$'` and `$_` work normally for a complete match with no partial
  option set, and every step 12 replacement string gives the same result.
* The tables are handed over generated: deriving them from the Unicode
  Character Database is out of scope, adopting them is the requirement.

## What you inherit, and must not break

Step 12 brought the workspace to **pcre2-10.47**, which fixed the buffer
over-read in scan substring assertions, added `pcre2_next_match()`, let a
recursion or subroutine call return chosen captures, and added `$+`, the
effective link size enquiry and `pcre2test`'s `-E` and `#if` machinery. All of
it stays. The gate re-checks a small number of narrow behaviours from earlier
steps end to end, and breaking one means the step is not satisfied however
well the milestone went.

The Unicode table swap makes this step's risk unusual: nearly everything at
risk is inherited, and the danger is a partial update. **Every construct that
reads a Unicode table** is exposed — `\p`, `\P`, `\X`, script runs, `\w` and
`[:xdigit:]` in UCP mode from step 10, the script properties from step 9, and
caseless matching including step 11's `Lc` treatment. Tables adopted for
categories but not for case folding leave half the ladder wrong. **Character
class compilation** is the second: the fix for classes spanning internal
ranges touches the same code as step 11's extended classes, which must keep
working. **Substitution** is the third: with no partial option set, every
replacement string from steps 11 and 12 must still give byte-identical output.

## Read these local references

Normative specification documents, under `/app/pcre2-doc/`. These are the
PCRE2 10.48 manual pages, copied verbatim; they contain no implementation
and no test expectations.

* **`pcre2unicode.3`** — the Unicode reference. "UNICODE PROPERTY SUPPORT"
  lists the property syntaxes the new tables feed; "UNICODE CASE-EQUIVALENCE"
  and "MATCHING IN INVALID UTF STRINGS" cover the rest this release touches.
* **`pcre2pattern.3`** — the normative pattern syntax. "BACKREFERENCES" with
  "ATOMIC GROUPING AND POSSESSIVE QUANTIFIERS" define possessive repeats; the
  three class sections define what the class fixes must produce.
* **`pcre2api.3`** — the public API. "CREATING A NEW STRING WITH
  SUBSTITUTIONS" is where partial matching, `PCRE2_ERROR_PARTIALSUBS` and the
  relaxed option check are specified.
* `pcre2test.1` — command-line options, modifiers and the exact shape of the
  default output, including the colour option and its environment override.
* `pcre2partial.3` — partial matching, its requirements and multi-segment use;
  its guidance on `PCRE2_DFA_RESTART` and retained subject data is expanded at
  this release.
* `pcre2syntax.3` — the compact quick reference to every construct, useful for
  checking a spelling against the long page.
* `pcre2matching.3` — how the two matching algorithms differ, and which
  features the DFA matcher does not have.
* `pcre2limits.3` — the hard limits on pattern size, nesting depth, group
  count and name length.
* `pcre2serialize.3` — saving and restoring compiled patterns, and the checks
  made when a serialized block is decoded.
* `pcre2callout.3` — the callout interface and the fields of the callout
  block.
* `pcre2compat.3` — the catalogue of differences from Perl.
* `pcre2.3` — the top-level introduction and index to the other pages.
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
