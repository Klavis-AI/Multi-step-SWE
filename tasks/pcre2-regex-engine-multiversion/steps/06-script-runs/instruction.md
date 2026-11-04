# Step 06 of 13 — Script runs

## Implement this milestone

This step moves the workspace from PCRE2 10.32 to **PCRE2 10.33**. Its
centrepiece is the script run: a group that succeeds or fails on a property
of the text it has just matched — which Unicode scripts its characters
belong to — rather than on the text itself. Around it come Perl 5.28's
alphabetic names for lookarounds and atomic groups, more extra compile
options, substitution callouts and rewordings of `pcre2test`'s own output.
A candidate working from memory will read `(*sr:...)` as "all characters
share one Script property"; the real rule is a Script Extensions rule with
named exceptions, set out in this release's `pcre2unicode.3`.

### Script runs

* `(*script_run:...)` and its short form `(*sr:...)` match whatever the
  enclosed pattern matches, then **fail** if the characters matched are not
  a script run; normal backtracking follows the failure (ChangeLog 10.33
  item 8; `pcre2pattern.3`, "SCRIPT RUNS").
* `(*atomic_script_run:...)` and `(*asr:...)` are defined as exactly
  `(*sr:(?>...))`: the atomic group is **inside** the script run. Outside it
  would not stop backtracking into the check, and the two nestings give
  different results (`pcre2pattern.3` and `pcre2syntax.3`, "SCRIPT RUNS").
* Script runs are **not supported by `pcre2_dfa_match()`**, because they use
  the same mechanism as capturing parentheses; under `-dfa` such a pattern
  gives the existing item-not-supported-for-DFA error (`pcre2pattern.3`,
  "SCRIPT RUNS"; `pcre2api.3`, "Error returns from pcre2_dfa_match()").
* `(*ACCEPT)` inside a script run leaves the group at once and bypasses the
  check; that is documented, not a defect (`pcre2pattern.3`, "SCRIPT RUNS").
  Without Unicode support the constructs are a compile error with a new
  message, reachable here only through `pcre2test -error`; its slot at the
  end of the error table must exist.

Whether matched text is a script run is decided by the rules below, from
`pcre2unicode.3`, "SCRIPT RUNS"; the script-set data they need is in this
release's `unicode-data/11.0.0/` tables:

* Fewer than two characters is always a script run, and is the only case in
  which a character of script Unknown qualifies ("Basic script run rules").
* Longer strings are checked against the **Script Extensions** property
  only, never the basic Script property. A Script Extension of the single
  value Inherited is always accepted, and so is Common subject to the digit
  rule; the rest must have a non-empty intersection of Script Extension sets
  ("Basic script run rules").
* Han with Hiragana and Katakana, Han with Hangul, and Han with Bopomofo are
  virtual scripts: a run may mix within one but not across two, so Hangul
  with Bopomofo with Han is not a run ("The Chinese Han script").
* Decimal digits in a run must all come from one set of ten adjacent
  characters, whatever the script check concluded ("Decimal digits").

### Alphabetic names for assertions and atomic groups

Perl 5.28's experimental alphabetic forms are accepted (ChangeLog 10.33
item 7): `(*atomic:` for `(?>` (`pcre2pattern.3`, "ATOMIC GROUPING AND
POSSESSIVE QUANTIFIERS"), and `(*positive_lookahead:` or `(*pla:`,
`(*negative_lookahead:` or `(*nla:`, `(*positive_lookbehind:` or `(*plb:`,
`(*negative_lookbehind:` or `(*nlb:` (`pcre2pattern.3`, "Alphabetic
assertion names"). A lower-case letter directly after `(*` marks these off
from the control verbs; any other name of that form gets its own new compile
error, not the verb-not-recognised one. Both spellings behave identically.

### Extra compile options: two added, one narrowed

* `PCRE2_EXTRA_ESCAPED_CR_IS_LF` converts `\r` in a pattern to `\n`, so it
  matches LF; a literal CR and an explicit `\x{0D}` are unaffected
  (ChangeLog 10.33 item 6). `PCRE2_EXTRA_ALT_BSUX` has the effect of
  `PCRE2_ALT_BSUX` and also recognises ECMAScript 6's `\u{hhh..}` (ChangeLog
  10.33 item 31). Both are in `pcre2api.3`, "Extra compile options", get
  modifiers `escaped_cr_is_lf` and `extra_alt_bsux` (`pcre2test.1`, "Setting
  compilation options"), and show in the `Extra options:` `info` line.
* `PCRE2_EXTRA_BAD_ESCAPE_IS_LITERAL`, yours since 10.30, now applies only
  to unrecognised or malformed escapes: an escape valid in a class but not
  as a range end, such as `[_-\s]`, gives the invalid-range error with or
  without the option (ChangeLog 10.33 item 26, the second of the two items
  so numbered), and a known escape such as `\e` used invalidly in a class is
  no longer made literal (ChangeLog 10.33 item 27).

### Callouts from the substitution function

`pcre2_substitute()` gains a callout, registered on the match context and
called after each substitution is processed (ChangeLog 10.33 item 3;
`pcre2api.3`, "Substitution callouts"). Zero accepts the replacement; a
positive return rejects it and continues under `PCRE2_SUBSTITUTE_GLOBAL`; a
negative return rejects it and ends the call, copying the rest of the
subject through. In `pcre2test` this is `substitute_callout`, plus
`substitute_skip=<n>` and `substitute_stop=<n>`, which return +1 and -1 for
match n and imply the first (`pcre2test.1`, "Testing substitute callouts").
Each call prints the match count, the ovector pairs set, and the offsets and
text of the old and replacement substrings, flagging a skipped or stopped
replacement. With `null_context` the modifier is refused.

### The matched subject, and the whole ovector

* `PCRE2_COPY_MATCHED_SUBJECT` makes a successful call to either matching
  function copy the subject into the match data instead of pointing at it;
  on failure the subject field is set to NULL (ChangeLog 10.33 item 10;
  `pcre2api.3`, "Option bits for pcre2_match()"). The `pcre2test` subject
  modifier is `copy_matched_subject`.
* `allvector` shows the entire ovector whatever the outcome — failure,
  partial match, and under `-dfa` — printing `<unchanged>` for a pair still
  holding the value written before the attempt (ChangeLog 10.33 item 1;
  `pcre2test.1`, "Showing the entire ovector, for all outcomes").

### Group names, \p{Any}, and matching corrections

* With `PCRE2_UTF` set, group names may contain any Unicode letter or
  decimal digit, and must still start with a non-digit; outside UTF mode the
  ASCII rule stands (ChangeLog 10.33 item 30; `pcre2pattern.3`, "NAMED
  CAPTURE GROUPS" gives both rules as patterns).
* `\p{Any}` compiles as `.` does in DOTALL mode, so `\p{Any}*` at the start
  of a pattern is auto-anchored and `info` reports `anchored` (ChangeLog
  10.33 item 32; `pcre2pattern.3`, "Unicode character properties").
* `info` shows no first-code-unit requirement for a pattern starting with a
  subroutine call quantified with a minimum of zero (ChangeLog 10.33 item
  11), and shows `anchored` for one starting with a verb such as `(*MARK)`
  or `(*COMMIT)` followed by `^` (ChangeLog 10.33 item 22).
* `pcre2_dfa_match()` must not report a spurious heap-limit error for a very
  large limit (ChangeLog 10.33 item 12), and must handle a true
  `(?(VERSION>=n.m)` condition (ChangeLog 10.33 item 29).

### Output wording, and a rename you cannot see

* The pattern information output says `Capture group count =` and heads the
  name table `Named capture groups:` where 10.32 said "subpattern"
  (`pcre2test.1`, "Information about a pattern"). The compile *error* texts
  are not rewritten by that change: they still say "subpattern" — `reference
  to non-existent subpattern`, `two named subpatterns have the same name` —
  and their numbers do not move either.
* `pcre2test -C`, which the gate reads, prints the default heap limit
  followed by `kibibytes` (ChangeLog 10.33 item 13; `pcre2test.1`,
  "COMMAND LINE OPTIONS").
* The POSIX wrappers are renamed `pcre2_regcomp()` and so on, with the
  standard names defined as macros in `pcre2posix.h` and kept in the library
  for older programs (ChangeLog 10.33 item 4; `pcre2posix.3`, "DESCRIPTION").
  **None of that is visible through `pcre2test`**; its `posix` modifier
  behaves exactly as at 10.32.

### What does not change

* Unicode stays at 11.0.0; the upgrade to 12.1.0 comes at step 07.
* `PCRE2_MATCH_INVALID_UTF` arrives at step 07; UTF checking is as at 10.32.
* The default heap, match and depth limits and their `find_limits` minima
  are unchanged; only the `-C` wording of the heap limit moves.
* Backtracking verbs, arguments on `(*ACCEPT)` and `(?^)` are as at 10.32.

## What you inherit, and must not break

Step 05 left you at PCRE2 10.32: a `pcre2_dfa_match()` that no longer
recurses on the C stack and so honours the heap limit, extra compile options
stored in the compiled pattern and shown on the `Extra options:` line,
`\N{U+dddd}`, `(?^)`, and arguments on `(*ACCEPT)`, `(*FAIL)` and
`(*COMMIT)`. All of that must survive while group machinery is reused.

The gate re-checks a small number of narrow behaviours from earlier steps
end to end. Breaking one means the step is not satisfied, however well
the new milestone went.

Three inherited behaviours are most at risk here. **Backtracking into and
out of groups** — captures, atomic groups and recursion on the heap frames
won at step 04 — is exactly the machinery script runs borrow, so frame
handling for the existing group types must not change when the new one is
added. **The control verbs** `(*MARK)`, `(*COMMIT)`, `(*PRUNE)`, `(*SKIP)`
and `(*THEN)` share the `(*` opener with the new alphabetic names, and the
parser change that tells them apart by a following lower-case letter must
leave every verb, with and without an argument, parsed as before. And
**`pcre2_substitute()` with `/g`**, its bumpalong on empty matches and its
overflow-length handling, must be untouched by the callout threaded through.

## Read these local references

Normative specification documents, under `/app/pcre2-doc/`. These are the
PCRE2 10.33 manual pages, copied verbatim; they contain no implementation
and no test expectations.

* **`pcre2pattern.3`** — pattern syntax at 10.33: the "SCRIPT RUNS" section
  for `(*script_run:` and `(*atomic_script_run:` and their short forms, the
  alphabetic assertion and atomic-group names, and group names in UTF mode.
* **`pcre2unicode.3`** — the "SCRIPT RUNS" section with the rules a matched
  string must satisfy: Script Extensions, Inherited and Common, the three
  Han combinations, and the decimal-digit constraint.
* **`pcre2api.3`** — the native API at 10.33: the extra compile options
  including `PCRE2_EXTRA_ESCAPED_CR_IS_LF` and `PCRE2_EXTRA_ALT_BSUX`,
  `PCRE2_COPY_MATCHED_SUBJECT`, and the substitution callout block.
* `pcre2test.1` — the harness's input grammar and every modifier, including
  `allvector`, `substitute_callout`, `substitute_skip`, `substitute_stop`,
  `escaped_cr_is_lf` and `extra_alt_bsux`.
* `pcre2test.txt` — the same page rendered as plain text.
* `pcre2syntax.3` — the quick syntax summary, with the script-run and
  alphabetic-name forms in their tables.
* `pcre2callout.3` — the pattern callout block; unchanged this step but the
  model the substitution callout follows.
* `pcre2matching.3` — the two matching algorithms compared, and what the
  alternative one does not support.
* `pcre2convert.3` — the experimental glob and POSIX conversion API.
* `pcre2posix.3` — the POSIX wrapper, now described in terms of the
  `pcre2_reg*()` names and the macros that map the standard names onto them.
* `pcre2partial.3` — partial matching semantics and restarting.
* `pcre2serialize.3` — saving and restoring compiled patterns.
* `pcre2limits.3` — hard limits on pattern and subject sizes and nesting.
* `pcre2compat.3` — where behaviour differs from Perl.
* `pcre2build.3` — build-time configuration and the defaults `-C` reports.
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
