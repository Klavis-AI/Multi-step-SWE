# Step 08 of 13 — The atomicity withdrawal and the \K story

## Implement this milestone

This step is PCRE2 **10.39**, and it swallows four upstream releases —
10.35, 10.36, 10.37 and 10.38. `/app/pcre2-doc/ChangeLog.transition` holds
all four sets of entries, so every citation below names the release its item
belongs to. Two changes define the step, and both **take something away**.
`\K` inside a lookaround assertion, legal at every step so far, becomes a
compile error unless the caller opts back in. And the documented rule that a
capturing group containing a recursive backreference to itself is made
atomic is withdrawn, so captures that used to be frozen after such a group
now change under backtracking. Upstream reached the `\K` end state over
several releases; you must land on **10.39's** behaviour in one move, not on
any intermediate state. Nothing later belongs here either: `\p{sc:...}`,
variable-length lookbehind and `PCRE2_EXTRA_CASELESS_RESTRICT` are all still
in the future.

### `\K` inside a lookaround is a compile error

Perl locked `\K` out of lookarounds, and PCRE2 follows by default
(ChangeLog 10.38 item 6). The rule and its opt-out are documented at
(pcre2pattern.3, "Resetting the match start").

* By default, `\K` anywhere inside a lookahead or lookbehind assertion,
  positive or negative, fails to compile. Error number, message and offset
  are all compared byte for byte; the message names the option below, as
  `\K is not allowed in lookarounds (but see ...)`.
* The check is made **after the whole pattern has been parsed**, so the
  reported offset is the end of the pattern — the pattern's length — not the
  position at which the `\K` itself sits. That is the opposite of the engine's
  ordinary convention, under which a compile diagnostic is reported where the
  scanner detected the problem rather than at the end of the pattern; this
  release's own transcripts show both conventions side by side. Reproduce the
  end-of-pattern offset for this error rather than reporting the `\K`'s own
  position.
* `PCRE2_EXTRA_ALLOW_LOOKAROUND_BSK` restores the previous behaviour
  (pcre2api.3, "Extra compile options"). Under it, `\K` is **acted upon**
  inside positive assertions and **ignored** inside negative ones. That
  asymmetry is part of the specification, not an implementation detail.
* With the opt-out set, a pattern such as `(?=ab\K)` can report a match
  whose start is greater than its end, and `\K` in a lookbehind at the start
  of a pattern can report a match starting earlier than the offset matching
  began at. Both must be reproduced, not defended against.
* `\K` outside an assertion is untouched: it still resets the reported start
  without disturbing captures.
* `pcre2test` needs the pattern modifier that sets the extra option
  (pcre2test.1, "Setting compilation options").

### Recursive-backreference groups are no longer atomic

A capturing group that contains a backreference to itself used to be turned
into an atomic group, a behaviour inherited from PCRE1 and documented as
such. The refactoring done for 10.30 made it unnecessary and it is now
removed (ChangeLog 10.35 item 4); (pcre2pattern.3, "Recursive
backreferences") now states that backtracking into such groups occurs as
normal. Note that the version number in that sentence is upstream's own
slip; the ChangeLog entry is the authority for when the change happened.

The observable effect is twofold and both halves are in scope. Patterns of the
shape `(a\1?){n}` that previously could not match now match, because the
group is re-entered and the backreference takes a different value; and the
reported contents of such a capture group can differ, being the value from
the iteration that was backtracked into rather than from the final
repetition.

### Assertion synonyms and assertion repetition

* `(?*` and `(?<*` are added as synonyms for `(*napla:` and `(*naplb:`
  (ChangeLog 10.35 item 8), listed at (pcre2pattern.3, "NON-ATOMIC
  ASSERTIONS") beside the note that a non-atomic assertion containing
  `(*ACCEPT)` is treated as atomic.
* A repeated assertion is no longer clamped to a maximum of one repetition.
  The only restriction now imposed is that an **unlimited** maximum is
  changed to one more than the minimum (ChangeLog 10.35 item 9), so `(?=a)*`
  and `(?=a){2,}` compile and behave as documented at (pcre2pattern.3,
  "ASSERTIONS"). Repetition of an assertion matters when the assertion
  contains capturing groups.

### Substitution gains three options

`pcre2_substitute()` gains `PCRE2_SUBSTITUTE_LITERAL` (ChangeLog 10.35
item 5), `PCRE2_SUBSTITUTE_MATCHED` (ChangeLog 10.35 item 7) and
`PCRE2_SUBSTITUTE_REPLACEMENT_ONLY` (ChangeLog 10.35 item 11), all specified
at (pcre2api.3, "CREATING A NEW STRING WITH SUBSTITUTIONS"). The literal
option suppresses all interpretation of the replacement string; the matched
option reuses a match data block that has already been filled by an external
`pcre2_match()` call with the same pattern and subject; the
replacement-only option returns just the replacement text instead of the
whole modified subject. `pcre2test` needs one modifier per option
(pcre2test.1, "Testing the substitution function").

### Loadable character tables

The character-tables handling is extended (ChangeLog 10.35 item 21):
`pcre2_config()` answers `PCRE2_CONFIG_TABLES_LENGTH` so a caller can save
tables in binary. The visible consequence is in `pcre2test`: a fourth tables
setting selects tables loaded from a binary file by a new `#loadtables`
command, and selecting it beforehand must produce the documented complaint
rather than a match (pcre2test.1, "Using alternative character tables").

### Compile-time and match-time corrections

Each of these changes an error, a match or a reported offset, so each is
compared:

* Over-large numbers now behave as Perl does (ChangeLog 10.37 item 4): a
  sequence such as `\214748364` is the octal escape `\214` followed by
  literal digits rather than an overflow error, and a `{65536` with no
  closing brace is not a quantifier at all and so raises no
  quantifier-too-big error.
* A pattern ending in `(?(VERSION=n.d` no longer reads past the end of the
  pattern, and is diagnosed (ChangeLog 10.36 item 8).
* `(?(DEFINE)` groups are skipped when a lookbehind's fixed length is
  computed, so patterns that wrongly reported a non-fixed-length lookbehind
  now compile (ChangeLog 10.35 item 14), and a DEFINE group immediately
  preceding a lookbehind no longer mis-compiles (ChangeLog 10.35 item 20).
* The effect of `\K` now propagates back from a full-pattern recursion, so
  `/a\K.(?0)*/` against "abac" yields "c" (ChangeLog 10.37 item 6).
* The lookbehind-length loop counter is no longer reset when a lookahead is
  processed (ChangeLog 10.38 item 8).
* The UTF-16 validity checker reports the correct error offset when a high
  surrogate is not followed by a valid low surrogate, which also corrects
  matching under `PCRE2_MATCH_INVALID_UTF` (ChangeLog 10.35 item 19); and
  the 8-bit start-of-match optimisation now resets correctly between
  fragments when `PCRE2_MATCH_INVALID_UTF` and `PCRE2_CASELESS` are both
  set (ChangeLog 10.36 item 13).

### Unicode properties without UTF, and Unicode 14.0.0

When `PCRE2_UCP` is set but `PCRE2_UTF` is not, Unicode character properties
are now used for upper- and lower-case computations on code points above 127
(ChangeLog 10.35 item 18). This is visible in 8-bit non-UTF caseless
matching of characters in the 128-255 range.

Adopt Unicode 14.0.0: ChangeLog 10.35 item 23 moves to 13.0.0 and
ChangeLog 10.39 item 3 moves to 14.0.0, and the end state is 14.0.0. The
generated tables are supplied, and `pcre2test -C` must report the version.

### What does not change

* Lookbehind assertions are still fixed-length; variable-length lookbehind
  and its maximum-setter arrive at step 10.
* Bare `\p{Name}` for a script still means that character's own script, not
  its script extensions. `\p{sc:...}` and `\p{scx:...}` do not exist yet and
  the redefinition happens at step 9.
* `\K` outside a lookaround is unaffected, and the non-atomic assertions
  added at step 7 keep their `(*napla:` and `(*naplb:` spellings; the new
  `(?*` forms are additions, not replacements.
* The POSIX wrapper's behaviour is unchanged. The duplicate entry points
  under their original names are removed from the library
  (ChangeLog 10.37 item 8), which nothing `pcre2test` prints can detect.

## What you inherit, and must not break

Step 7 left `/app` at PCRE2 10.34: matching in invalid UTF subjects under
`PCRE2_MATCH_INVALID_UTF`, the non-atomic positive lookarounds `(*napla:`
and `(*naplb:`, the redefined partial match, and Unicode 12.1.0.

The gate re-checks a few narrow behaviours from *earlier* steps end to end
through `pcre2test`; breaking one means the step is not satisfied however
well the new milestone went. The three most at risk here:

* **`\K` outside a lookaround.** The lockout is narrow. `foo\Kbar`,
  `(foo)\Kbar` and `\K` inside an ordinary group must behave exactly as
  before, including leaving captures alone. Rejecting `\K` too widely is
  worse than not implementing the lockout at all.
* **Atomic groups and possessive quantifiers.** Withdrawing one piece of
  automatic atomicity must not loosen the explicit forms `(?>...)`, `*+`,
  `++` or `(*atomic:`, nor the atomicity of lookarounds other than the
  non-atomic ones from step 7.
* **Invalid-UTF matching from step 7.** Two of this step's corrections touch
  that path, so the step 7 fragment model must still hold in full: no match
  across a barrier, offsets into the whole subject, `\b` at fragment edges.

## Read these local references

Normative specification documents, under `/app/pcre2-doc/`. These are the
PCRE2 10.39 manual pages, copied verbatim; they contain no implementation
and no test expectations.

* **`pcre2pattern.3`** — the pattern language. "Resetting the match start"
  states the new `\K` rule and its opt-out; "Recursive backreferences" now
  says backtracking into such groups happens normally; "NON-ATOMIC
  ASSERTIONS" lists the new `(?*` and `(?<*` spellings; "REPETITION" covers
  the new rules for repeating an assertion.
* **`pcre2api.3`** — every function, option and error code. "Extra compile
  options" defines `PCRE2_EXTRA_ALLOW_LOOKAROUND_BSK`; "CREATING A NEW
  STRING WITH SUBSTITUTIONS" defines the three new substitution options.
* **`pcre2test.1`** — the driver's input grammar, every modifier and the
  output format. New here: the modifier that sets the `\K` opt-out, three
  substitution modifiers, the fourth `tables` value and the `#loadtables`
  command under "Using alternative character tables", and the tightened
  rules for patterns that follow `#perltest`.
* `pcre2test.txt` — the same text as `pcre2test.1`, rendered as plain text.
* `pcre2unicode.3` — Unicode and UTF support, including matching in invalid
  UTF strings and the script-run rules.
* `pcre2syntax.3` — the one-page syntax summary; the quickest way to check
  which spellings of an assertion exist at this release.
* `pcre2matching.3` — the two matching algorithms and what the DFA does not
  support.
* `pcre2compat.3` — differences from Perl, one item per difference; the `\K`
  change is a move towards Perl, not away from it.
* `pcre2partial.3` — partial matching and the multi-segment patterns.
* `pcre2callout.3` — the callout interface, including callouts from
  `pcre2_substitute()`.
* `pcre2limits.3` — the built-in size limits.
* `pcre2serialize.3` — saving and restoring compiled patterns.
* `pcre2convert.3` — foreign pattern conversion.
* `pcre2posix.3` — the POSIX wrapper and its entry-point names.
* `pcre2build.3` — build-time options; only the defaults this task fixes
  are relevant.
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
