# Step 07 of 13 — Invalid UTF and non-atomic lookarounds

## Implement this milestone

This step is PCRE2 **10.34**. Two things arrive together, and both push on
the backtracking frame model that step 4 built: `PCRE2_MATCH_INVALID_UTF`, a
compile-time option that lets `pcre2_match()` run over a subject containing
invalid UTF sequences, and the **non-atomic positive lookaround assertions**
`(*napla:` and `(*naplb:`. The second is the harder: a lookaround that has
succeeded must now leave behind enough state to be backtracked into, where
every earlier step was free to discard that state the moment the assertion
returned true. A candidate writing "modern PCRE2" from memory will reach for
the short synonyms `(?*` and `(?<*`, which do not exist until step 8, will
tend to reject `\K` inside a lookaround, which is still legal here, and will
allow variable-length lookbehind, which does not arrive until step 10.

### Non-atomic positive lookaround assertions

Implement non-atomic positive lookahead and lookbehind (ChangeLog 10.34
item 17), documented in full at (pcre2pattern.3, "NON-ATOMIC ASSERTIONS")
and summarised at (pcre2syntax.3, "NON-ATOMIC LOOKAROUND ASSERTIONS").

* Both spellings must compile: `(*non_atomic_positive_lookahead:` and
  `(*non_atomic_positive_lookbehind:`, and the short forms `(*napla:` and
  `(*naplb:`. Only positive forms exist; there is no non-atomic negative.
* The observable difference from `(*pla:` / `(?=` is that after the assertion
  has succeeded and a later part of the pattern fails, the match re-enters
  the assertion, the capture groups inside it take different contents, and
  matching continues. A pattern whose tail backreferences a group captured
  inside the assertion is the distinguishing case: atomic, it matches only
  the one candidate the assertion first found; non-atomic, it walks the
  alternatives. Both the overall match and the capture values must show it.
* `pcre2_dfa_match()` does not support non-atomic assertions, so a pattern
  that contains one must be rejected at DFA match time rather than matched
  approximately (pcre2pattern.3, "NON-ATOMIC ASSERTIONS").
* An assertion used as the condition of a conditional group must still be
  atomic. A non-atomic assertion in that position is a compile error, and the
  error number, message and offset are all compared, so the diagnostic must
  read `atomic assertion expected after (?( or (?(?C)`.
* Nesting must work: a lookbehind inside a lookahead inside a lookbehind was
  previously mis-processed and must now match (ChangeLog 10.34 item 18).

### Matching in invalid UTF subjects

Add `PCRE2_MATCH_INVALID_UTF` to `pcre2_compile()` for the interpreter
(ChangeLog 10.34 item 4), specified at (pcre2unicode.3, "MATCHING IN INVALID
UTF STRINGS") and listed at (pcre2api.3, "Main compile options"). The points
the transcripts discriminate on:

* Setting it forces `PCRE2_UTF` on and makes `PCRE2_NO_UTF_CHECK` be
  ignored at match time. The pattern itself must still be valid UTF. The
  forced `PCRE2_UTF` is recorded in `PCRE2_INFO_ARGOPTIONS` (not only in
  `PCRE2_INFO_ALLOPTIONS`), so an `I` block for a pattern whose only
  modifier is `match_invalid_utf` prints a single `Options: match_invalid_utf utf`
  line rather than a separate compile/overall pair.
* An invalid code unit sequence in the subject matches nothing at all — not
  `.`, not `\p{Any}`, not a negated class such as `[^X]` — and a lookbehind
  that would have to step back across one fails. The subject behaves as
  valid fragments separated by impassable barriers. Those boundaries are not
  line boundaries (circumflex and dollar do not match there) but they *are*
  word boundaries (`\b` and `\B` behave there as at the ends of the subject).
* A starting offset that lands inside an invalid sequence is advanced to the
  next valid character or the end of the subject, and reported offsets are
  always offsets into the whole subject, including under global matching.
* `pcre2_dfa_match()` does not support this option and must reject the match
  with `PCRE2_ERROR_DFA_UINVALID_UTF`. New error codes are appended, so it
  takes the next free value after 10.33's last one, `-65`
  (`PCRE2_ERROR_INTERNAL_DUPMATCH`), giving `-66`. `pcre2test` must print it
  as `Failed: error -66: PCRE2_MATCH_INVALID_UTF is not supported for DFA matching`.
  `pcre2test` needs the pattern modifier that sets the option, listed at
  (pcre2test.1, "Setting compilation options").

### New compile-time diagnostics

* The documented maximum of 65535 capturing groups was never checked. It is
  now, with the message `too many capturing groups (maximum 65535)`
  (ChangeLog 10.34 item 1).
* The fixed-length lookbehind length check is guarded against integer
  overflow, so an over-long lookbehind is now diagnosed as
  `lookbehind assertion is too long` (ChangeLog 10.34 item 16).
* `{1}` applied to a parenthesized item is no longer discarded when it
  carries a possessive suffix, so `(a){1}+` behaves as an atomic group
  (ChangeLog 10.34 item 12); and `(*ACCEPT)` may now be quantified
  (ChangeLog 10.34 item 8).

### Partial matching, redefined

The definition of a partial match changes in three ways (ChangeLog 10.34
item 20) and (pcre2partial.3, "REQUIREMENTS FOR A PARTIAL MATCH") was
rewritten for it: a pattern containing any lookbehind may now yield an
**empty** partial match; so may a pattern that can match an empty string,
under hard partial matching only; and an empty partial match can be returned
for `\z` and `\Z`. Separately, `allusedtext` now governs the text reported
for a partial match and shows only the string preceding the match point
(ChangeLog 10.34 item 13), and `pcre2_dfa_match()` must give a partial match
when it reaches the end of the subject inside a lookahead, an atomic group or
a recursion (ChangeLog 10.34 item 14).

### Start-up optimisation, and the information `pcre2test` prints

These are performance changes upstream, but each is visible in the `I`
information lines or in whether a match is found at all, so all are in scope.
The `Subject length lower bound`, `First code unit`, `Last code unit` and
`Starting code units` lines are compared exactly.

* A class holding exactly the two cases of one character, such as `[Aa]`, is
  now treated as a single caseless character, so the first and required code
  unit optimisations apply to it (ChangeLog 10.34 item 33); and a starting
  bitmap with only one or two bits is likewise replaced by a single starting
  code unit, caseless when the two are case partners, except where a required
  code unit of the same value would be spoiled (ChangeLog 10.34 item 35).
* For a positive class containing wide characters but no property types in
  UTF-8 mode, only the bytes that actually start those characters are set in
  the starting-byte bitmap (ChangeLog 10.34 item 34); and a non-ASCII first
  character in a starting assertion now gets its casing right under caseless
  matching (ChangeLog 10.34 item 38).
* The reported minimum subject length changes in several ways
  (ChangeLog 10.34 item 9): it is not computed when start-up optimisations
  are disabled, `(*ACCEPT)` suppresses the scan, and a group branch longer
  than 65535 no longer collapses the whole group's minimum to 65535
  (ChangeLog 10.34 item 26).
* The rightmost consulted character now includes characters inspected in a
  trailing negative lookahead (ChangeLog 10.34 item 27); and a branch
  starting with `(*ACCEPT)` is recognised as one that can match
  an empty string, so the `May match empty string` line appears where it did
  not (ChangeLog 10.34 item 21).
* A `(*MARK)` set inside a condition that succeeded is now returned
  (ChangeLog 10.34 item 10).

### Unicode 12.1.0

Adopt the supplied Unicode 12.1.0 tables (ChangeLog 10.34 item 23):
`pcre2test -C` must report the new version, and property, script and
case-folding results must follow the new data.

### What does not change

* `\K` inside a lookaround assertion is still accepted and works as
  documented at (pcre2pattern.3, "Resetting the match start"). It becomes a
  compile error at step 8.
* The short synonyms `(?*` and `(?<*` for the non-atomic assertions do not
  exist yet; they arrive at step 8.
* Lookbehind assertions are still fixed-length. Variable-length lookbehind
  arrives at step 10.
* A capturing group containing a recursive backreference to itself is still
  treated as an atomic group, as documented at (pcre2pattern.3, "Recursive
  backreferences"). That atomicity is withdrawn at step 8, not here.
* `pcre2_get_match_data_size()` is added at this release
  (ChangeLog 10.34 item 19), but nothing `pcre2test` prints reports its
  value, so it changes no transcript.

## What you inherit, and must not break

Step 6 left `/app` at PCRE2 10.33: script runs — `(*script_run:` /
`(*sr:` and the atomic forms — plus `PCRE2_EXTRA_ESCAPED_CR_IS_LF`,
`PCRE2_EXTRA_BAD_ESCAPE_IS_LITERAL`, `\u{hhh}` under `PCRE2_ALT_BSUX`, and
callouts from `pcre2_substitute()`, all on the heap-frame matcher of step 4.

The gate re-checks a few narrow behaviours from *earlier* steps end to end
through `pcre2test`; breaking one means the step is not satisfied however
well the new milestone went. The three most at risk from this step:

* **Ordinary atomic lookarounds must stay atomic.** Making assertions
  backtrackable is the milestone; making `(?=`, `(?!`, `(?<=` and `(?<!`
  backtrackable as a side effect is a regression that shows up as extra
  matches and different captures across the whole corpus.
* **Partial matching that was not empty must stay as it was.** The new
  empty-partial cases are additions; hard and soft partial results for
  patterns with no lookbehind that cannot match empty are unchanged.
* **UTF validity checking without `PCRE2_MATCH_INVALID_UTF`** must still
  reject an invalid subject with the same error number and offset as before;
  the new option is opt-in.

## Read these local references

Normative specification documents, under `/app/pcre2-doc/`. These are the
PCRE2 10.34 manual pages, copied verbatim; they contain no implementation
and no test expectations.

* **`pcre2pattern.3`** — the pattern language. The section "NON-ATOMIC
  ASSERTIONS" is new at this release and is the specification for
  `(*napla:` and `(*naplb:`; "Resetting the match start" still permits `\K`
  inside a lookaround.
* **`pcre2unicode.3`** — Unicode and UTF. "MATCHING IN INVALID UTF STRINGS"
  is new and defines fragment-by-fragment matching under
  `PCRE2_MATCH_INVALID_UTF`; "VALIDITY OF UTF STRINGS" is the default path.
* **`pcre2partial.3`** — partial matching, rewritten this release;
  "REQUIREMENTS FOR A PARTIAL MATCH" gives the new definition, including
  when an empty partial match is returned.
* `pcre2api.3` — every function, option and error code; "Main compile
  options" lists `PCRE2_MATCH_INVALID_UTF`, and "INFORMATION ABOUT A
  COMPILED PATTERN" defines the values `pcre2test` prints with `I`.
* `pcre2syntax.3` — the one-page syntax summary, with "NON-ATOMIC LOOKAROUND
  ASSERTIONS" added.
* `pcre2test.1` — the driver's input grammar, every modifier and the output
  format; the new `match_invalid_utf` modifier is under "Setting
  compilation options".
* `pcre2test.txt` — the same text as `pcre2test.1`, rendered as plain text.
* `pcre2matching.3` — the two matching algorithms and which features the DFA
  does not support, now including invalid-UTF matching and non-atomic
  assertions.
* `pcre2compat.3` — differences from Perl, one item per difference.
* `pcre2callout.3` — the callout interface and what a callout can observe.
* `pcre2limits.3` — the built-in size limits, including the 65535 capture
  group maximum that is now enforced.
* `pcre2serialize.3` — saving and restoring compiled patterns.
* `pcre2convert.3` — foreign pattern conversion.
* `pcre2posix.3` — the POSIX wrapper.
* `pcre2build.3` — build-time options; only the defaults this task fixes are
  relevant.
* `pcre2.3` — the top-level index page.

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
