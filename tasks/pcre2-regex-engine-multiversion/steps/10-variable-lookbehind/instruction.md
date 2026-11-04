# Step 10 of 13 — Variable-length lookbehind and four class redefinitions

## Implement this milestone

This step is **pcre2-10.43**. Its centre is the arrival of variable-length
lookbehind assertions in the interpreter, and, beside it, four redefinitions
of what a "word character" and a "hex digit" mean when Unicode property
support is in force. A candidate reproducing current PCRE2 from memory will
reach too far here: extended character classes, caller-controlled optimisation
and the collapse of `Lu`, `Ll` and `Lt` into `Lc` all belong to later releases
and must not appear yet. The redefinitions are the subtle part, because no
syntax changes to announce them — the same pattern text that worked at 10.42
now matches a different set of characters, and only in UCP mode. Read
`### What does not change` as carefully as the rest.

### Variable-length lookbehind assertions

Before this release, every top-level alternative of every lookbehind had to
match a single fixed length. `pcre2_match()` now also supports lookbehinds in
which one or more branches can match more than one length, for example
`(?<=colou?r)` (ChangeLog 10.43 item 24; pcre2pattern.3, "Lookbehind
assertions").

* The maximum matching length of any one branch of a variable-length
  lookbehind is a value set by the calling program, and its **default is 255
  characters** (ChangeLog 10.43 item 24; pcre2syntax.3, "LOOKAHEAD AND
  LOOKBEHIND ASSERTIONS").
* A lookbehind all of whose top-level branches match a fixed length keeps the
  older limit of 65535 characters. The two limits coexist, and which applies
  is decided per assertion (pcre2limits.3, "SIZE AND OTHER LIMITATIONS").
* Unlimited repetition such as `\d*` inside a lookbehind is still rejected at
  compile time, as are `\X` and `\R` anywhere in one and `\C` in a UTF mode.
  Subroutine calls are permitted if the called group matches a limited-length
  string; recursion into an already-active group is not (pcre2pattern.3,
  "Lookbehind assertions").
* `pcre2test`'s `max_varlookbehind=<n>` modifier sets that maximum for
  subsequent compilations, so both the default and reduced values are visible
  in transcripts, together with the compile errors produced when a branch
  exceeds the limit in force (pcre2test.1, "Specifying a maximum for variable
  lookbehinds").
* `pcre2_dfa_match()` gains nothing here: it accepts only fixed-length
  lookbehind branches, so a pattern that compiles for `pcre2_match()` may
  still be refused for DFA matching (pcre2pattern.3, "Lookbehind
  assertions").

### Four redefinitions in one release, with no new syntax

All four apply only when Unicode property support is in force for the pattern;
outside UCP mode nothing here changes.

* `\w`, and therefore `\W` and every synonym, now matches characters whose
  general category is `L` or `N`, plus those in the particular categories `Mn`
  and `Pc`. `Pc` includes underscore, which was previously special-cased
  (ChangeLog 10.43 item 32; pcre2pattern.3, "Generic character types").
* `\b` and `\B` are defined in terms of `\w`, so their meaning moves with it.
  Nothing in the pattern signals this; the word-boundary positions in a
  subject simply change (ChangeLog 10.43 item 35; pcre2pattern.3, "Simple
  assertions").
* `[:xdigit:]` now also matches the fullwidth versions of the hex digits
  (ChangeLog 10.43 item 33; pcre2pattern.3, "POSIX CHARACTER CLASSES").
* The fourth part of the change is that each of these can be restricted back
  to ASCII independently, per class, as the next cluster sets out.

### Restricting the new behaviour back to ASCII

* `PCRE2_EXTRA_ASCII_BSD`, `PCRE2_EXTRA_ASCII_BSS`, `PCRE2_EXTRA_ASCII_BSW`
  and `PCRE2_EXTRA_ASCII_POSIX` force `\d`, `\s`, `\w` and the POSIX classes
  respectively to stay ASCII-only in UCP mode (ChangeLog 10.43 item 9(b);
  pcre2api.3, "COMPILING A PATTERN"). `PCRE2_EXTRA_ASCII_DIGIT` keeps
  `[:digit:]` and `[:xdigit:]` ASCII-only without affecting the other POSIX
  classes (ChangeLog 10.43 item 17).
* Inside a pattern, `(?aD)`, `(?aS)`, `(?aW)`, `(?aP)` and `(?aT)` set the
  corresponding restrictions and the `(?-a...)` forms clear them; `(?aP)` also
  implies `(?aT)`, so `(?-aP)` disables all ASCII restrictions on POSIX
  classes (ChangeLog 10.43 item 36; pcre2pattern.3, "INTERNAL OPTION
  SETTING").
* `PCRE2_EXTRA_CASELESS_RESTRICT`, with the `(?r)` and `(?-r)` in-pattern
  forms, prevents caseless matching from mixing ASCII and non-ASCII characters
  in one equivalence set (ChangeLog 10.43 item 9(a)). The ASCII word-class
  options must still behave correctly when matching an invalid UTF subject
  under `PCRE2_MATCH_INVALID_UTF` (item 18). All of these options have
  `pcre2test` modifiers, so each is directly observable (pcre2test.1, "PATTERN
  MODIFIERS").

### Quantifiers and braced items

* `{,m}` is now a quantifier meaning `{0,m}`, as in Perl. `{,}` is **not** a
  quantifier and remains a literal three-character sequence (ChangeLog 10.43
  item 29; pcre2pattern.3, "REPETITION"; pcre2syntax.3, "QUANTIFIERS").
* Space and horizontal tab are now permitted after `{` and before `}` in
  numeric quantifiers and in the numeric arguments of other braced items.
  `\u{...}`, recognised only in ALT_BSUX mode, is the exception and allows no
  white space (ChangeLog 10.43 item 30; pcre2syntax.3, "BRACED ITEMS").

### Corrections visible through matching and diagnostics

* A NULL pattern pointer with a length of zero is now accepted and treated as
  an empty pattern instead of raising an error (ChangeLog 10.43 item 23), and
  `PCRE2_FIRSTLINE` is now ignored for an anchored pattern (item 37).
* Whole-pattern recursion `(?0)` is reworked and the check that detects a
  recursion which cannot progress is improved, so some patterns that used to
  fail with a recursion-loop error now match (ChangeLog 10.43 items 47, 48).
  `PCRE2_DISABLE_RECURSELOOP_CHECK` and `pcre2test`'s
  `disable_recurseloop_check` modifier turn that check off (item 57), and
  captures set inside a recursive subroutine call are reset correctly when the
  call returns (item 31).
* `\z` and `\Z` behave correctly at the end of an invalid UTF subject
  (ChangeLog 10.43 item 43); the 32-bit library handles a quantifier applied
  to a character above 0x7fffffff and caseless UCP matching of `\x{ffffffff}`
  (items 42, 49 and 50). The computation of the set of possible starting code
  units is improved: the computation itself is not printable, but it changes
  which patterns report a first-code-unit or starting-set optimisation in
  `pcre2test` output (item 53).

### Unicode 15.0.0 and match-data reporting

* The Unicode tables move to 15.0.0, changing property membership for
  characters added or recategorised in that version. The generated tables are
  supplied; adopting them is in scope, deriving them is not (ChangeLog 10.43
  item 10; pcre2unicode.3, "UNICODE PROPERTY SUPPORT").
* `pcre2_get_match_data_heapframes_size()` reports the size of the heap frame
  vector in a match data block and `pcre2test`'s `heapframes_size` modifier
  prints it; unlike the frame-size line, that value is part of the compared
  output (ChangeLog 10.43 item 3; pcre2api.3, "MEMORY USE FOR MATCH DATA
  BLOCKS").

### What does not change

* `{,}` stays literal. It does not become a quantifier here or at any later
  step in this ladder (ChangeLog 10.43 item 29).
* `\x` not followed by `{` or a hexadecimal digit still means a NUL character,
  so `\xz` is NUL followed by `z`. It becomes a compile error at step 11
  (pcre2pattern.3, "Non-printing characters").
* Under caseless matching, `\p{Ll}`, `\p{Lu}` and `\p{Lt}` still match only
  their own general categories; treating all three as `Lc` arrives at step 11
  (pcre2pattern.3, "The general category property for \p and \P").
* Capture group names are still limited to 32 code units, and there is still
  no way for a caller to enable or disable individual compile-time
  optimisations and no extended character class syntax of any kind. All three
  change at step 11 (pcre2limits.3, "SIZE AND OTHER LIMITATIONS").

## What you inherit, and must not break

Step 9 brought the workspace to **pcre2-10.42**, whose visible gain was the
script-property family: `\p{sc:...}` and `\p{scx:...}`, four-letter script
abbreviations, Unicode loose matching of property names, and the silent
redefinition of a bare `\p{name}` from a Script to a Script_Extensions test.
Everything from steps 1 through 9 is still expected to work: both matchers,
serialisation, callouts, partial matching, substitution, the POSIX wrapper,
and `pcre2test` itself. The gate re-checks a small number of narrow earlier
behaviours end to end, and a regression in one of them means the step is not
satisfied however well this milestone went.

Three inherited behaviours are most at risk here. **Fixed-length lookbehind**:
adding a variable-length path must not alter the lengths computed, the 65535
limit or the errors reported for the fixed-length case, and must not make
`pcre2_dfa_match()` accept assertions it used to refuse. **Non-UCP `\w`, `\b`,
`\B` and `[:xdigit:]`**, which keep their ASCII definitions exactly; the four
redefinitions are conditional on Unicode property support being in force.
**`PCRE2_INFO_MAXLOOKBEHIND` and the start-of-match optimisations**, both
recomputed by this step's work and both still reported in existing output.

## Read these local references

Normative specification documents, under `/app/pcre2-doc/`. These are the
PCRE2 10.43 manual pages, copied verbatim; they contain no implementation
and no test expectations.

* **`pcre2pattern.3`** — the normative pattern syntax. "Lookbehind
  assertions", "Generic character types", "Simple assertions", "POSIX
  CHARACTER CLASSES" and "REPETITION" carry the whole milestone.
* **`pcre2test.1`** — every command-line option, pattern modifier and subject
  modifier, and the exact shape of the default output. "Specifying a maximum
  for variable lookbehinds" and the ASCII-restriction modifiers are new here.
* **`pcre2api.3`** — the public API: option bits, context setters, match data
  blocks, error codes. The `PCRE2_EXTRA_*` list and "MEMORY USE FOR MATCH
  DATA BLOCKS" are the relevant parts.
* `pcre2syntax.3` — a compact quick reference to the same syntax, including
  "QUANTIFIERS", "BRACED ITEMS" and the two lookaround length limits. Useful
  as a checklist of what must be accepted.
* `pcre2limits.3` — the hard limits, including the two different lookbehind
  branch limits and the capture-group name length.
* `pcre2unicode.3` — UTF validity, UCP mode, case equivalence, script runs
  and matching in invalid UTF strings.
* `pcre2matching.3` — how the two matching algorithms differ, which is where
  the DFA restriction on lookbehinds comes from.
* `pcre2compat.3` — the catalogue of differences from Perl, worth reading
  because several items in this release exist to reduce that list.
* `pcre2.3` — the top-level introduction and index to the other pages.
* `pcre2callout.3` — the callout interface and the callout block fields.
* `pcre2partial.3` — partial matching, its restrictions and restarting.
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
