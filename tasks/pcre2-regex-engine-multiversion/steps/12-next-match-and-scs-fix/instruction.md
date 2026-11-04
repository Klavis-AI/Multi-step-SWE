# Step 12 of 13 — Iteration and a security fix

## Implement this milestone

This step is **pcre2-10.47**, and it also absorbs 10.46. Two things dominate.
The first is a security fix: CVE-2025-58050, a read past the end of the
buffer, of arbitrary length, reachable when `(*ACCEPT)` and `(*scs:)` are used
together — a hole opened by the scan substring assertion that the previous
step introduced, and present in no release before it. The second is iteration:
a public function that supplies the parameters for the next match attempt, and
a recursion syntax that lets a subroutine call hand capture values back to its
caller. Native EBCDIC and z/OS support and an AArch64 SIMD JIT also land at
this release, but both are out of scope here. A candidate who treats the scan
substring assertion as finished business at step 11 will carry the
vulnerability forward.

### The scan substring security fix

* An attacker-controlled pattern that uses `(*ACCEPT)` inside a scan substring
  assertion could read past the end of the buffer by an arbitrary length.
  Crafted subject text alone cannot trigger it; the pattern is the vector
  (ChangeLog 10.46 item 1).
* The fix is a behaviour change, not a refactor: the combination must be
  handled correctly rather than rejected out of hand, so patterns that pair
  the two features have to produce the right match results. Releases 10.44 and
  earlier are not affected, which means the defect was introduced by the
  previous step's own feature (ChangeLog 10.46 item 1; pcre2pattern.3, "SCAN
  SUBSTRING ASSERTIONS").

### Iterating over all matches

* `pcre2_next_match()` is called after a match attempt and supplies the start
  offset and the extra options for the next attempt. It returns 1 if another
  attempt should be made — which does not imply another match exists — and 0
  if there is no need (ChangeLog 10.47 item 11; pcre2api.3, "ITERATING OVER
  ALL MATCHES").
* Its guarantee is that the start offset advances, so a loop built on it
  terminates: either the function returns 0, or the returned start offset is
  strictly greater than the previous one, or the previous match was an empty
  match and the returned offset equals the previous `ovector[1]` with
  `PCRE2_NOTEMPTY_ATSTART` set in the returned options.
* Unless `PCRE2_EXTRA_ALLOW_LOOKAROUND_BSK` is set, each match satisfies
  `start_offset <= ovector[0] <= ovector[1]`, and the matches produced by an
  iteration are sorted, non-overlapping and free of duplicates. With that
  option the guarantees are weaker: only the start offset is promised to
  advance.
* `pcre2test`'s own repeated-match handling changes with it. After an empty
  match, the next attempt is made with `PCRE2_NOTEMPTY_ATSTART` alone; it no
  longer also sets `PCRE2_ANCHORED`, and it no longer advances the start
  offset itself, whether by one character or by two for CRLF (pcre2api.3,
  "ITERATING OVER ALL MATCHES"). This changes the sequence of matches
  reported for some `/g` subjects, so it is directly visible.

### Recursion and subroutine calls that return captures

* A recursion or subroutine call may be followed by a parenthesised list of
  capture groups to retain when the call returns: `(?R(grouplist))`,
  `(?n(grouplist))`, `(?+n(grouplist))`, `(?-n(grouplist))`,
  `(?&name(grouplist))` and `(?P>name(grouplist))`. The list is
  comma-separated and may mix absolute and relative group numbers with names
  in single quotes or angle brackets (ChangeLog 10.47 item 9; pcre2pattern.3,
  "Recursion and subroutines with returned capture groups").
* Only the groups named in the list are retained; every other capture reverts
  to the calling context's value, as it did before. This is a PCRE2 extension
  that Perl does not have, and it is not available through the Oniguruma
  `\g<...>` and `\g'...'` subroutine syntax.

### Substitution and configuration

* `$+` inserts the highest-numbered capture group that matched (ChangeLog
  10.47 item 18; pcre2api.3, "CREATING A NEW STRING WITH SUBSTITUTIONS").
* Input validation for `pcre2_substitute()` with `PCRE2_SUBSTITUTE_MATCHED` is
  tightened: the match data must come from a prior external match on the same
  pattern, subject pointer, effective subject length, start offset and match
  options, and changing any of them is an error rather than undefined
  behaviour (ChangeLog 10.47 item 17; pcre2api.3, "CREATING A NEW STRING WITH
  SUBSTITUTIONS"). `pcre2test` gains a `substitute_subject` modifier
  specifically to exercise a client that mutates the subject buffer between
  the match and the substitution (pcre2test.1, "SUBJECT MODIFIERS").
* `PCRE2_CONFIG_EFFECTIVE_LINKSIZE` reports the link size the library actually
  uses for internal linkage in compiled patterns. It is derived from the
  build-time value, which `PCRE2_CONFIG_LINKSIZE` continues to report and
  which can be lower, because the actual size depends on the width of the code
  units the library supports (ChangeLog 10.47 item 15; pcre2api.3, "CHECKING
  BUILD-TIME OPTIONS"). `pcre2test -C linksize` still reports the configured
  value, not the effective one (pcre2test.1, "COMMAND LINE OPTIONS").
* A crash in `pcre2_callout_enumerate()`, easily reachable on any pattern
  containing a Unicode character class, is fixed (ChangeLog 10.47 item 16).

### Diagnostics and compilation

* Error offsets and diagnostics for syntax errors during pattern compilation
  are improved (ChangeLog 10.47 item 13). Compile errors are part of the
  compared output, so both the message and the offset matter. A failed
  pattern is followed by a `here:` line that marks the offset by inserting
  arrows into a copy of the pattern, as this release's own transcripts show
  (for example `here: a** |<--| b` for `/a**b/`). Two offsets are worth
  stating because the release does not place them where the marker alone
  would suggest. A numeric condition naming a **positive** group that does
  not exist reports the offset two code units before the end of the number,
  so `(?(1)a)` gives `error 115 at offset 2` while `(?(10))` gives offset 3 —
  the two-digit case is visible in this release's transcripts, the one-digit
  case is not. A numeric entry in a scan substring assertion's capture list —
  `(*scs:` or `(*scan_substring:` — reports the offset of the number's
  **first** character instead: `(*scs:(9)x)` gives offset 7, and this
  release's transcripts show the same position for `/(*scs:(1)a|b)/`. A zero
  or negative number in either construct is reported at the end of the
  number.
* Named capture groups are looked up through a hash table during compilation.
  The mechanism is not visible through `pcre2test`; what is visible is that
  patterns with many named groups must still compile to identical results and
  report identical name tables (ChangeLog 10.47 item 7).
* `pcre2test` gains a `-malloc` argument and more detailed internal assertions
  (ChangeLog 10.47 item 8), a `-E` preprocess-only mode that processes `#if
  ... #endif` commands and prints all other lines verbatim, and `#if CONDITION
  ... #endif` input commands whose condition is any condition testable by `-C`
  and may be negated with `!` (pcre2test.1, "COMMAND LINE OPTIONS";
  pcre2test.1, "COMMAND LINES").

### Out of scope at this release

Three of this release's headline items are out of scope, and they
appear in `ChangeLog.transition` where they will be visible: full z/OS and
native EBCDIC support (ChangeLog 10.47 item 6), the new AArch64 SIMD code
generation in the JIT (item 19), and symbol versioning for the dynamic
libraries (item 10). None is required, and none can be seen through the binary
that is checked.

Four further items from the same list are documented, but no case used here
reaches them, so they are not separately in scope at this step:
`PCRE2_CONFIG_EFFECTIVE_LINKSIZE` (the `-C` check reads the width lines and
the JIT line only), `pcre2_callout_enumerate()` (nothing the binary that is
checked prints calls it), and `pcre2test`'s `-malloc` and `-E` command-line
arguments (no case used here passes either one). The exported
`pcre2_next_match()` function is in scope through its observable effect — the
iteration behaviour of `pcre2test`'s global matching — rather than through a
call to that function by name. Implement all of them as documented; the point
of saying so is that no transcript depends on them here.

The `#if CONDITION … #endif` input commands are **not** in that list, and
`-E` is not a precondition for them: they are in scope at this step, both by
this release's own numbered transcripts and by a held-out case whose whole
input is `#if`/`#endif` blocks. A true condition prints the `#if` line and
goes on to process the block as normal, printing its commandlines too; a
false one prints the `#if` line and then the `#endif` line with nothing
between them, the lines inside the block never reaching the output
(`pcre2test.1`, "COMMAND LINES"). The conditions are drawn from the `-C`
queries and may be negated, so `#if !pcre2-16` must skip its block in a
build that has the 16-bit library while `#if backslash-C` must not. They
are commands of an ordinary run; implementing them only under `-E` fails
every transcript that contains one.

### What does not change

* Unicode support stays at 16.0.0. It moves to 17.0.0 at step 13.
* Possessive repeats of backreferences are not supported here; they arrive at
  step 13, as does `PCRE2_ERROR_PARTIALSUBS` and partial matching under
  `PCRE2_SUBSTITUTE_REPLACEMENT_ONLY`.
* `pcre2_dfa_match()` gains no new pattern features. Scan substring assertions
  and capture-returning recursion both require capturing and remain
  unavailable to it.
* The error numbers reported by `pcre2test` are not renumbered at this
  release. A memory failure in substitution is `-48` here as it was at step
  11; only the manual page example was stale before.

## What you inherit, and must not break

Step 11 brought the workspace to **pcre2-10.45**, the widest step in the
ladder: control over individual compile-time optimisations, scan substring
assertions, two extended class syntaxes, the modern replacement-string
vocabulary and two reversals of older behaviour. All of it stays. The gate
re-checks a small number of narrow behaviours from earlier steps end to end,
and breaking one means the step is not satisfied however well the milestone
went.

Three inherited behaviours are most exposed. **Scan substring assertions** are
the obvious one: this step fixes a buffer over-read in them, and the fix must
not be a rejection. Every `(*scs:)` pattern that matched correctly at step 11,
anchoring rules and inner captures included, must still match the same way.
**Repeated matching over a subject** is the second: the retry after an empty
match is reworked, so every subject whose matches are all non-empty must still
report exactly the sequence step 11 reported. **Recursion and subroutine
calls** are the third: a plain `(?R)`, `(?1)` or `(?&name)` with no group list
must go on discarding the callee's captures on return, because the new syntax
adds to that rule rather than replacing it.

## Read these local references

Normative specification documents, under `/app/pcre2-doc/`. These are the
PCRE2 10.47 manual pages, copied verbatim; they contain no implementation
and no test expectations.

* **`pcre2api.3`** — the public API. "ITERATING OVER ALL MATCHES" specifies
  `pcre2_next_match()` and the guarantees a loop may rely on; "CHECKING
  BUILD-TIME OPTIONS" covers the two link-size enquiries.
* **`pcre2pattern.3`** — the normative pattern syntax. Re-read "SCAN SUBSTRING
  ASSERTIONS" before the security fix; "Recursion and subroutines with
  returned capture groups" specifies the new call forms.
* **`pcre2test.1`** — command-line options, modifiers and the exact shape of
  the default output. "COMMAND LINES" documents the new `#if ... #endif` input
  commands; "DEFAULT OUTPUT FROM pcre2test" the reworked empty-match retry.
* `pcre2syntax.3` — the compact quick reference, including "SUBROUTINE
  REFERENCES (POSSIBLY RECURSIVE)", "SUBSTRING SCAN ASSERTION" and
  "REPLACEMENT STRINGS".
* `pcre2matching.3` — how the two matching algorithms differ, and so which of
  this step's features the DFA matcher cannot have.
* `pcre2partial.3` — partial matching, its restrictions and how to restart
  after one.
* `pcre2unicode.3` — UTF validity, UCP mode, case equivalence, script runs and
  matching in invalid UTF strings.
* `pcre2callout.3` — the callout interface and the callout block fields,
  including the enumeration function fixed here.
* `pcre2limits.3` — the hard limits on pattern size, nesting depth, group
  count and name length.
* `pcre2compat.3` — the catalogue of differences from Perl.
* `pcre2.3` — the top-level introduction and index to the other pages.
* `pcre2serialize.3` — saving and restoring compiled patterns.
* `pcre2posix.3` — the POSIX-style wrapper functions.
* `pcre2convert.3` — the experimental glob and POSIX pattern converters.
* `pcre2build.3` — the build-time configuration options, the link size among
  them.
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
