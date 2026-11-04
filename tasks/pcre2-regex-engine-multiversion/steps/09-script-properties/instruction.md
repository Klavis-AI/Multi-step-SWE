# Step 09 of 13 — Script properties and the silent redefinition

## Implement this milestone

This step is PCRE2 **10.42**, standing in for 10.40, 10.41 and 10.42
together; `/app/pcre2-doc/ChangeLog.transition` carries all three sets of
entries, and every citation below names the release its item belongs to.
The subject is `\p` and `\P`. Script matching gains three syntaxes, the
standard four-letter script abbreviations, and Unicode's loose matching
rules for property names; Bidi_Class and a set of binary properties are
added. Underneath all of that sits one **silent redefinition**: a bare
script name, written `\p{Greek}`, used to mean "this character's own
script" and now means "this character's script extensions", which is a
strictly larger set. No option restores the old meaning, no diagnostic
marks the change, and every pattern in the earlier steps that used a bare
script name now matches more. That is the single most likely thing to get
wrong, in either direction: implementing the new syntaxes but leaving the
bare form meaning `sc:`, or applying the new meaning to `\p{sc:...}` too.

### Script matching: three syntaxes, one redefinition

The specification is (pcre2pattern.3, "Script properties for \ep and \eP"),
summarised at (pcre2syntax.3, "SCRIPT MATCHING WITH \ep AND \eP"), and the
change list is (ChangeLog 10.40 item 17).

* Each character has one basic script and, optionally, a list of other
  scripts it is commonly used with. `\p{sc:Adlam}` matches characters whose
  **basic** script is Adlam. `\p{scx:Adlam}` matches those *and* characters
  that have Adlam in their extensions list (ChangeLog 10.40 item 17(b)).
* The long property-type names `script` and `script_extensions` are
  synonyms for `sc` and `scx`, and an equals sign is an alternative to the
  colon, so all of `\p{script:Adlam}`, `\p{script=Adlam}`,
  `\p{script_extensions:Adlam}` and `\p{scx=Adlam}` are accepted.
* **A script name given with no property type now means `scx:`**
  (ChangeLog 10.40 item 17(c)). `\p{Adlam}` is `\p{scx:Adlam}`, matching
  Perl from its release 5.26. This is the flip: a character whose own
  script is Inherited or Common but which lists a script in its extensions
  now matches the bare form where it did not before.
* The standard Unicode four-letter script abbreviations are recognised
  wherever a full script name is (ChangeLog 10.40 item 17(d)), so `Latn`,
  `Grek` and `Hani` work in every one of the forms above.
* Unicode's loose matching rules apply to property names: spaces, hyphens
  and underscores in the name are ignored and what remains is matched
  independent of case (ChangeLog 10.40 item 17(e)). `\p{ bidi_control }`,
  `\p{bidicontrol}` and `\p{Bidi_Control}` are the same property, and
  `\p{script extensions:katakana}` is `\p{scx:Katakana}`.
* Unassigned characters, and in non-UTF 32-bit mode code points above
  0x10FFFF, are in the "Unknown" script; characters not part of an
  identified script are "Common".
* A name that is not a recognised script or property is still a compile
  error with the existing number and offset, but the message text changes
  this release: it loses the word "name". Where step 8 produced
  `unknown property name after \P or \p`, this release produces
  `unknown property after \P or \p`. A general category name is still not
  a script name, so `\p{sc:L}` must fail with that message.
* Script extensions are now held as a bitmap rather than a list
  (ChangeLog 10.40 item 17(a)), and three maintainer scripts now generate a
  third table file that the tables unit includes (ChangeLog 10.40 item 18).
  Neither is visible in any transcript; what is visible is that a character
  can be tested against every script in its extensions list at once.

### Bidi_Class and the binary properties

`\p` and `\P` gain the Bidi_Class property and a number of binary
(yes/no) properties, Bidi_Control among them (ChangeLog 10.40 item 14).

* `\p{Bidi_Class:<class>}` and its abbreviation `\p{BC:<class>}` match a
  character with the given bidirectional class; an equals sign may replace
  the colon, class names are case-insensitive, and **only the short class
  names** in (pcre2pattern.3, "The Bidi_Class property for \ep and \eP")
  are recognised. The same table appears at (pcre2syntax.3, "THE
  BIDI_CLASS PROPERTY FOR \ep AND \eP").
* The binary properties and their abbreviations are those the release's own
  tables define; (pcre2pattern.3, "Binary (yes/no) properties for \ep and
  \eP") points at the `pcre2test` listing option that enumerates them, and
  the supplied generated tables are the authority for the membership of
  each one.
* All of these must work identically in a class, negated with `\P` or with
  a leading circumflex, quantified greedily, lazily and possessively, and
  under the DFA matcher as well as the interpreter.
* `pcre2test` gains two listing command-line options that write the
  recognised properties and the recognised script names to standard output
  and exit (ChangeLog 10.40 item 20; pcre2test.1, "COMMAND LINE OPTIONS").

### A NULL subject or replacement is an empty string

Four related changes (ChangeLog 10.40 items 10, 11, 12 and 13):

* For the subject of `pcre2_match()`, `pcre2_dfa_match()` and
  `pcre2_substitute()`, and for the replacement of the last, a NULL pointer
  with a length of zero is treated as an empty string (pcre2api.3,
  "MATCHING A PATTERN: THE TRADITIONAL FUNCTION"). A NULL pointer with a
  non-zero length is an error, and its message is compared exactly.
* `pcre2_substitute()` checks for a NULL replacement, and its error return
  is documented at (pcre2api.3, "Substitution errors"); the POSIX
  `regexec()` wrapper checks for a NULL subject and returns its own code.
* `pcre2test` needs a subject modifier for each of the two NULL cases
  (pcre2test.1, "Passing a NULL context, subject, or replacement").

### Match frames live on the heap

The backtracking frame vector no longer starts on the system stack; it is
always allocated on the heap, remembered in the match data block, reused if
that block is reused, and freed with it (ChangeLog 10.41 item 9). The
consequences are all observable:

* `pcre2_match()` now always needs some heap memory, so a heap limit of
  zero — however it is set, including `(*LIMIT_HEAP=0)` in the pattern —
  guarantees a heap-limit error rather than a match (pcre2api.3, "The match
  context"). This changes results under the POSIX wrapper too.
* Minimum-limit searching is split in two: the existing modifier now finds
  the heap limit as well as the match and depth limits, and a second
  modifier omits the heap limit because its minimum varies between systems
  (ChangeLog 10.41 items 10 and 11; pcre2test.1, "Finding minimum limits").
  Both must exist and print the lines the manual describes.

### Smaller corrections that change output

* An item such as `[Aa]` is optimised into a caseless single character. When
  it was quantified and was also the last literal item in the pattern, the
  "must be present for a match" check was not flagged caseless, so matches
  that should have succeeded failed (ChangeLog 10.40 item 22). Both the
  match results and the `Last code unit` information line change.
* An ovector count above 65535 passed to `pcre2_match_data_create()` is
  silently capped at 65535 instead of crashing (ChangeLog 10.42 item 4;
  pcre2api.3, "THE MATCH DATA BLOCK").
* A negative repeat value in a `pcre2test` subject line is now diagnosed
  instead of looping forever (ChangeLog 10.41 item 20).

### What does not change

* The Unicode version stays at 14.0.0. No table in this step encodes a
  newer Unicode; the next Unicode upgrade is at step 10.
* There is no opt-out for the bare-script-name redefinition. Do not invent
  an option, a pattern modifier or an extra compile flag to restore `sc:`
  semantics for `\p{Name}`.
* Perl's long property synonyms such as `\p{Letter}`, and the `Is` prefix,
  are still not supported, and caseless matching still does not affect
  `\p` and `\P` (pcre2pattern.3, "The general category property for \ep and
  \eP").
* Lookbehind assertions are still fixed-length; variable-length lookbehind
  arrives at step 10.
* `\K` inside a lookaround is still a compile error with the same opt-out
  as step 8, and the non-atomic assertions keep all four spellings.

## What you inherit, and must not break

Step 8 left `/app` at PCRE2 10.39: `\K` inside a lookaround is a compile
error with `PCRE2_EXTRA_ALLOW_LOOKAROUND_BSK` as the opt-out, capturing
groups holding a recursive backreference to themselves are no longer
atomic, and Unicode is at 14.0.0. The gate re-checks a few narrow
behaviours from *earlier* steps end to end through `pcre2test`; breaking
one means the step is not satisfied however well the milestone went. The
three most at risk here:

* **Script runs, from step 6.** `(*script_run:` and `(*sr:` are defined in
  terms of script identity, and this step rewrites how script data is
  stored and looked up. Script runs keep their own rules, are not redefined
  in terms of `scx:`, and their Han and digit special cases still hold.
* **General category and PCRE2's own properties.** `\p{Lu}`, `\p{Xan}`,
  `\p{Xwd}`, `\p{Any}` and the rest are untouched. Loose matching must not
  turn a mistyped category into a silent success, and unknown-property
  errors keep their number and offset — the message text is the one thing
  that changes, as described above.
* **Everything that reads the heap limit.** Moving the frame vector onto
  the heap changes when a limit is hit, but limits set high enough still
  give the same matches, and depth and match limits are unchanged.

## Read these local references

Normative specification documents, under `/app/pcre2-doc/`. These are the
PCRE2 10.42 manual pages, copied verbatim; they contain no implementation
and no test expectations.

* **`pcre2pattern.3`** — the pattern language. Four sections carry this
  step: "Unicode character properties" states the loose matching rule,
  "Script properties for \ep and \eP" defines `sc`, `scx` and what a bare
  script name now means, "Binary (yes/no) properties for \ep and \eP" and
  "The Bidi_Class property for \ep and \eP" define the new property
  families.
* **`pcre2syntax.3`** — the one-page syntax summary, reorganised this
  release: "SCRIPT MATCHING WITH \ep AND \eP", "BINARY PROPERTIES FOR \ep
  AND \eP" and "THE BIDI_CLASS PROPERTY FOR \ep AND \eP" are the quickest
  check of which spellings are accepted.
* **`pcre2api.3`** — every function, option and error code. "The match
  context" states that matching now always needs heap memory; "THE MATCH
  DATA BLOCK" gives the ovector maximum; "MATCHING A PATTERN: THE
  TRADITIONAL FUNCTION" and "Substitution errors" cover NULL arguments.
* `pcre2test.1` — the driver's input grammar, every modifier and the
  output format; new here are the two listing options under "COMMAND LINE
  OPTIONS", the second minimum-limit modifier under "Finding minimum
  limits", and the NULL subject and replacement modifiers.
* `pcre2test.txt` — the same text as `pcre2test.1`, rendered as plain text.
* `pcre2unicode.3` — Unicode and UTF support: UTF validity, matching in
  invalid UTF strings, case equivalence, and the script-run rules, which
  are separate from the `\p` script properties.
* `pcre2matching.3` — the two matching algorithms and what the DFA does
  not support.
* `pcre2compat.3` — differences from Perl, one item per difference.
* `pcre2limits.3` — the built-in size limits.
* `pcre2partial.3` — partial matching and multi-segment matching.
* `pcre2callout.3` — the callout interface.
* `pcre2serialize.3` — saving and restoring compiled patterns.
* `pcre2convert.3` — foreign pattern conversion.
* `pcre2posix.3` — the POSIX wrapper, which gains a NULL subject check.
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
