# Step 05 of 13 — De-recursing the DFA and persisting extra options

## Implement this milestone

This step moves the workspace from PCRE2 10.30 to **PCRE2 10.32**, absorbing
the intervening 10.31 release, so `/app/pcre2-doc/ChangeLog.transition`
carries **two** release headings and item numbers restart under the second.
The centrepiece is the alternative matcher: `pcre2_dfa_match()` stops using
the C stack for its internal recursion, which brings it under the heap limit
added at step 04. Around that, the extra compile options become part of the
compiled pattern and become retrievable, `pcre2test -C` corrects two of its
answers, and pattern constructs a candidate assumes have always existed —
`\N{U+dddd}`, `(?^)`, `(*ACCEPT:ARG)` — arrive here, not earlier.

### pcre2_dfa_match() is de-recursed

`pcre2_dfa_match()` is refactored so that its internal recursive calls no
longer use the stack for local workspace and local ovectors: an initial block
of stack is reserved and heap memory used beyond it (ChangeLog 10.32-RC1
item 12). The refactor is invisible to the checker; its consequence is not.

* **The heap limit now applies to `pcre2_dfa_match()`.** A `-dfa` run of a
  pattern setting `(*LIMIT_HEAP=d)` or `heap_limit` low enough must report
  `heap limit exceeded`, where 10.30 ignored the setting
  (`pcre2api.3`, "The match context"). The reserved block is small, so the
  limit is observable through short inputs too: a pattern with internal
  recursion a couple of levels deep, such as `^(\((?:[a-z]|(?1))*\))$/`
  matched against `((q(w))(e))`, must already need heap beyond the block and
  so report `heap limit exceeded` at `heap_limit=0`, while a pattern with no
  internal recursion — which needs no heap at all — still matches at that
  limit. That need is not marginal, and a limit of zero is not the only one
  that stops it: the modifiers count kibibytes, and at `heap_limit=4` — four
  kibibytes — the same pattern must **still** report
  `heap limit exceeded`, while a limit of `heap_limit=4000` lets the match
  complete. The workspace this pattern needs is therefore larger than 4 KiB,
  and a DFA that completes it on 4 KiB of heap or less fails that check.
* A `find_limits` test of DFA matching that overflows the ovector still
  displays the matches found, where 10.30 displayed none
  (ChangeLog 10.32-RC1 item 13).

### Extra compile options are stored in the compiled pattern

The extra options set in a compile context were not previously saved with the
pattern. They are now, `PCRE2_INFO_EXTRAOPTIONS` is added to retrieve them,
and `pcre2test` shows them (ChangeLog 10.31 item 21; `pcre2api.3`,
"INFORMATION ABOUT A COMPILED PATTERN"). In output that means the `info`
modifier prints an `Extra options:` line naming the options set for that
pattern, and nothing where none are set. This line did not exist at step 04.

Note a conflict in the shipped pages here, and resolve it the way the checker
does. `pcre2api.3`, "Extra compile options", still opens with "Unlike the main
compile-time options, the extra options are not saved with the compiled
pattern". That sentence is stale — upstream did not update it along with the
change — while the same page's `PCRE2_INFO_EXTRAOPTIONS` entry states the new
behaviour. **ChangeLog 10.31 item 21 and the `PCRE2_INFO_EXTRAOPTIONS` entry
are authoritative**; the "not saved" sentence describes the earlier releases
this step is replacing.

### New pattern syntax

* `\N{U+dddd}` is supported, but **only** in Unicode mode; outside it the
  pattern is an error with its own new message (ChangeLog 10.32-RC1 item 29;
  `pcre2pattern.3`, "Non-printing characters"). Two existing texts shift with
  it: the unsupported-escape message names `\N{name}` alongside `\F`, `\L`,
  `\l`, `\U`, `\u`, and "digits missing" names `\N{U+}` too.
* `(?^)` unsets all of `imnsx` at once and letters may follow it to set
  options again, so `(?^)` is equivalent to `(?-imnsx)` (ChangeLog 10.32-RC1
  item 30); a malformed option setting such as a stray hyphen now has its own
  compile error message.
* `(*ACCEPT:ARG)`, `(*FAIL:ARG)` and `(*COMMIT:ARG)` are now supported
  (ChangeLog 10.32-RC1 item 27; `pcre2pattern.3`, "Verbs that act
  immediately"), and a `(*MARK)` name is passed back for a positive assertion
  terminated by `(*ACCEPT)` (ChangeLog 10.32-RC1 item 28). The compile error
  that used to reject an argument becomes an obsolete slot: **it keeps its
  number**, so every later error code keeps its value, and `pcre2test -error`
  reports a placeholder message for it rather than the old text.

### PCRE2_EXTENDED and Unicode Pattern White Space

With Unicode support compiled, `PCRE2_EXTENDED` (and so `PCRE2_EXTENDED_MORE`)
discards U+0085, U+200E, U+200F, U+2028 and U+2029 as well as the white space
its tables recognize (ChangeLog 10.32-RC1 item 31; `pcre2api.3`,
"COMPILING A PATTERN" under PCRE2_EXTENDED).

### pcre2test -C answers correctly, and gains two options

* `-C` reports the compiled code-unit widths from one new configuration
  query and whether `\C` is supported from another (ChangeLog 10.31 item 4;
  `pcre2api.3`, "CHECKING BUILD-TIME OPTIONS").
* `-C` used to claim `\R matches CR, LF, or CRLF only` whatever the build
  said; it must now print `\R matches all Unicode newlines` in the default
  configuration, and `-C bsr` must be able to produce `ANY`
  (ChangeLog 10.32-RC1 item 3).
* `-LM` lists the available pattern and subject modifiers and exits, and `-C`
  complains about a bad option only when the following argument does not
  start with a hyphen (ChangeLog 10.31 item 10). `-AC` behaves as `-ac` plus
  `callout_extra`, and `-S` is described in `mebibytes`
  (`pcre2test.1`, "COMMAND LINE OPTIONS").

### Callout flags and the callout_extra modifier

Callout blocks gain a `callout_flags` field with `PCRE2_CALLOUT_STARTMATCH`
and `PCRE2_CALLOUT_BACKTRACK`, set by `pcre2_match()` but not by
`pcre2_dfa_match()` (ChangeLog 10.31 item 22; `pcre2callout.3`, "Fields for
all callouts"). The **only** new `pcre2test` modifier at this release is
`callout_extra`, which makes those bits visible: a callout beginning a match
attempt prints `New match attempt`, one reached by backtracking prints
`Backtrack`, and one that is both prints `Backtrack`, `No other matching
paths`, `New match attempt`. Under DFA matching it has no effect
(`pcre2test.1`, "Testing callouts").

### Matching and compile corrections

* `(*ACCEPT)` in an assertion closes only the groups started within that
  assertion, not every open group (ChangeLog 10.31 item 12).
* Auto-possessification no longer applies to an iterator at the end of a
  capture group when the pattern contains any recursion, since what follows
  the group differs between its inline and recursed uses
  (ChangeLog 10.31 item 31).
* First-code-unit deduction is corrected for a leading backreference with a
  zero minimum (ChangeLog 10.31 item 17) and for a group inside a leading
  assertion (ChangeLog 10.31 item 18); both are visible in `info` output.
* `PCRE2_FIRSTLINE` without `PCRE2_NO_START_OPTIMIZE` no longer fails when
  the match starts with the first code unit of a newline sequence
  (ChangeLog 10.31 item 25).
* A lookbehind whose lookaheads carry quantifiers is no longer wrongly
  rejected as not fixed length (ChangeLog 10.32-RC1 item 22), a `VERSION`
  condition reads fractional version numbers correctly
  (ChangeLog 10.32-RC1 item 23), and option settings such as `/((?i)A)(?m)B/`
  are scoped correctly again (ChangeLog 10.32-RC1 item 32).
* Backtracking no longer re-enters the first of two adjacent atomic groups
  (ChangeLog 10.32-RC1 item 25), a repeated negative class is no longer
  wrongly auto-possessified (ChangeLog 10.32-RC1 item 35), and conditional
  groups are re-examined for anchoring (ChangeLog 10.32-RC1 item 39;
  ChangeLog 10.32-RC1 item 40).
* Global matching of a pattern that matches empty only away from the start
  offset, such as `/(?<=\G.)/`, follows Perl in `pcre2test` and
  `pcre2_substitute()` (ChangeLog 10.32-RC1 item 21), and `altglobal` with
  `replace` warns and is ignored (ChangeLog 10.32-RC1 item 20).

### POSIX, serialization and Unicode 11.0.0

`REG_STARTEND` with a non-zero starting offset returns unset groups that
precede a captured one as unset (ChangeLog 10.32-RC1 item 1), and `pcre2test`
shows leading unset POSIX groups as `<unset>` as for native matching
(ChangeLog 10.32-RC1 item 2). Serializing a pattern zeroes its pointer fields
so the byte stream is reproducible (ChangeLog 10.32-RC1 item 34), and
`pcre2_pattern_convert()` reports offset zero for errors found before any
output (ChangeLog 10.32-RC1 item 9). The tables move to Unicode 11.0.0
(ChangeLog 10.32-RC1 item 24), changing property, script and
grapheme-cluster answers (`pcre2pattern.3`, "Extended grapheme clusters").

### What does not change

* **No script runs, no alphabetic assertion names.** `(*script_run:`,
  `(*sr:`, `(*pla:` and `(*atomic:` remain unrecognized until step 06, and so
  do `PCRE2_EXTRA_ALT_BSUX` and `PCRE2_EXTRA_ESCAPED_CR_IS_LF`.
* **`pcre2_substitute()` still does no callouts**; that is step 06.
* **`info` still says "Capturing subpattern count" and "Named capturing
  subpatterns".** That wording changes at step 06.
* **`pcre2test -C` still prints the heap limit as a bare number**, without
  `kibibytes`, which is added at step 06.
* **The depth limit keeps its 10.30 names and synonyms.** `recursion_limit`
  and `(*LIMIT_RECURSION)` must still be accepted here.

## What you inherit, and must not break

Step 04 left you at PCRE2 10.30: a `pcre2_match()` that keeps its
backtracking state in heap-allocated frames instead of on the C stack, the
depth limit and heap limit with their old names kept as synonyms, the
`PCRE2_EXTRA_*` compile-context options, `PCRE2_ENDANCHORED`,
`PCRE2_EXTENDED_MORE`, `PCRE2_LITERAL` and the pattern-conversion API. All of
that must still work while the alternative matcher is rebuilt underneath it.

The gate re-checks a small number of narrow behaviours from earlier steps end
to end. Breaking one means the step is not satisfied, however well the
new milestone went.

Three inherited behaviours are most at risk here. **Backtracking into
recursive group calls**, won at step 04, is easy to lose when this step's
auto-possessification change touches iterators at the ends of capture groups.
**DFA matching of the constructs it already supports** must not regress while
its workspace moves to the heap: partial matches, restarts and the
`\=find_limits` and ovector-overflow paths all run through the code being
changed. And the **depth and heap limits on `pcre2_match()`** must keep
reporting the same minima through `find_limits` as they did at 10.30, even
though the heap limit now also governs the DFA matcher.

## Read these local references

Normative specification documents, under `/app/pcre2-doc/`. These are the
PCRE2 10.32 manual pages, copied verbatim; they contain no implementation
and no test expectations.

* **`pcre2api.3`** — the native API at 10.32, including
  `PCRE2_INFO_EXTRAOPTIONS`, the two new `pcre2_config()` items for the
  compiled code-unit widths and `\C` support, and the heap limit as it
  now applies to both matching functions.
* **`pcre2pattern.3`** — pattern syntax: `\N{U+dddd}`, `(?^)`, the verbs
  that now take an argument, and what `PCRE2_EXTENDED` discards when
  Unicode support is compiled.
* **`pcre2callout.3`** — the callout block, now carrying `callout_flags`
  with the start-match and backtrack bits, and which matcher sets them.
* `pcre2test.1` — the harness's input grammar and every modifier, including
  `callout_extra` and the `-LM` and `-AC` command-line options.
* `pcre2test.txt` — the same page rendered as plain text.
* `pcre2syntax.3` — the quick syntax summary, including the option-setting
  and escape tables.
* `pcre2unicode.3` — UTF validity rules and the property support behind the
  Unicode 11.0.0 tables.
* `pcre2matching.3` — the two matching algorithms compared, and what the
  alternative one does not support.
* `pcre2convert.3` — the experimental glob and POSIX conversion API.
* `pcre2posix.3` — the POSIX wrapper, including `REG_STARTEND`.
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
