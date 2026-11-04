# Step 03 of 13 — The compile-path restructure

## Implement this milestone

This step targets **PCRE2 10.23**. Upstream re-factored the compiler so that
most syntax checking now happens in the pre-pass that identifies capturing
groups (ChangeLog 10.23 item 3). The re-factoring is not behaviour-preserving
and the requirement is on behaviour, not structure: error numbers and messages are
reassigned, some patterns that used to be rejected now compile, some that
used to compile are rejected, and -- the change that touches the most output
lines -- the offset reported with a compile error is no longer necessarily
the right-most character read. Reproducing 10.22's diagnostics behind a
tidier compiler is out of scope here, and so is reproducing current
PCRE2's, whose error table was renumbered again after this release.

### The compile-error offset is no longer the right-most character read

The offset reported with a compile error is now "an indication of where in
the pattern the error occurred. It is not necessarily the furthest point in
the pattern that was read" (pcre2api.3, "COMPILING A PATTERN"), and
`pcre2test` prints it for every failing pattern. The case the page and the
change entry single out is a lookbehind that is not of fixed length: the
offset now points to the start of the failing assertion, not to where the
compiler found the problem (ChangeLog 10.23 item 3), though that error's
number and message are unchanged. Other diagnostics still report where the
offending item was read, and an invalid UTF string reports its first bad unit.

### Error numbers and messages are reassigned

Some compile-error numbers now denote different errors and some messages are
reworded (ChangeLog 10.23 item 3); there are over 80 positive codes
(pcre2api.3, "COMPILATION ERROR CODES"). `pcre2test` prints the numeric code
as well as the text, so keeping 10.22's numbering fails every line that
mentions a changed code even where the wording is right. The `-error` option
prints the message for a given number, so it too depends on the numbering
(pcre2test.1, "COMMAND LINE OPTIONS").

The visible transcripts do not reach every entry in the table. One they never
print: an unknown property name after `\p` or `\P`, as in `\p{Nonsuch}`, is
**error 147**, `unknown property name after \P or \p`.

### Syntax checking moves into the pre-pass, and these cases change

Each item below is listed in ChangeLog 10.23 item 3.

* `\Q\E` in the middle of a quantifier such as `A+\Q\E+` is ignored instead
  of being an invalid-quantifier error, and `{0}` may follow a group inside a
  lookbehind, where it used to give "assertion is not fixed length".
* `(?(DEFINE)` is always a define group even if a group is named `DEFINE`;
  `(?(R2)...)` must refer to an existing group; and `(?(R)...)` must work
  when a group's name begins with `R` (pcre2pattern.3, "CONDITIONAL
  SUBPATTERNS").
* A hyphen immediately after a POSIX character class, as in `[[:ascii:]-z]`,
  is now an error, as it already was after a character type escape
  (pcre2pattern.3, "SQUARE BRACKETS AND CHARACTER CLASSES").

### Callouts: what is passed, and where automatic callouts appear

* With PCRE2_AUTO_CALLOUT, callouts are inserted before each pattern item
  "except immediately before or after a callout in the pattern"
  (pcre2callout.3, DESCRIPTION; ChangeLog 10.23 item 7).
* The length of the next pattern item passed to a callout changes: before an
  opening parenthesis it is the length of the parenthesis item alone,
  including meta characters after it, so before `(?=ab)` it is 3; for an
  alternation bar or closing parenthesis it is one, plus any following
  quantifier; and zero only at the end of the pattern (pcre2callout.3, "THE
  CALLOUT INTERFACE"; ChangeLog 10.23 item 3).
* The caret marking the subject position in callout output must be correct
  after an escape for a code point above `\x{ff}` (ChangeLog 10.23 item 8).

### Back references, classes and lifted limits

* Back references are permitted in lookbehinds when PCRE2_MATCH_UNSET_BACKREF
  is not set, `(?|` is not used, and a reference by name is to a unique name;
  the referenced group must be of fixed length (ChangeLog 10.23 item 4;
  pcre2pattern.3, ASSERTIONS). `\g{+n}` is added as a forward back reference
  (ChangeLog 10.23 item 6; pcre2pattern.3, "BACK REFERENCES").
* In a wide-character mode without PCRE2_UCP, a negative type such as `\D` in
  a positive class makes every character above 255 match, and adding a
  property item, as in `[\D\P{Nd}]`, must not suppress that (ChangeLog 10.23
  item 2). This one is in scope on its own, through inputs written for it and
  run at all three code-unit widths.
* With PCRE2_UCP but not PCRE2_UTF, a class that needs every wide character to
  match, such as `[\s[:^ascii:]]`, must compile correctly (ChangeLog 10.23
  item 39). **That fix alone** is not separately in scope: its only occurrence
  in an in-scope input carries a `B` dump, which the normalising filter removes,
  so it is invisible to a byte-diff checker.
* Groups with more than about 1100 branches no longer fail as too complicated
  (ChangeLog 10.23 item 37), because possible emptiness is now tracked as the
  pattern is compiled instead of by scanning the compiled group.

### Start-up optimisations, anchoring and the DFA recursion limit

With PCRE2_NO_START_OPTIMIZE unset, `(?=.*X)X$` must not be treated as needing
two `X` characters, and a leading assertion starting with `.*` is no longer
taken to mean the match begins at the subject start or after a newline
(ChangeLog 10.23 item 15); with PCRE2_DOTALL but not PCRE2_NO_DOTSTAR_ANCHOR,
a pattern starting with `.*` inside a positive lookahead is not implicitly
anchored (ChangeLog 10.23 item 18). Both show as matches 10.22 missed and in
the first-code-unit and anchoring lines printed for a pattern (pcre2api.3,
"INFORMATION ABOUT A COMPILED PATTERN"). Separately, `pcre2_dfa_match()` now
takes note of the recursion limit for its internal recursive calls and returns
PCRE2_ERROR_RECURSIONLIMIT when it is exceeded (ChangeLog 10.23 item 36;
pcre2api.3, "PCRE2 CONTEXTS"), where earlier copies of that page said it was
ignored.

### New in pcre2test, and one new library function

* `utf8_input` treats the pattern and following subject lines as UTF-8 so
  wide characters can be given in non-UTF mode; it excludes `utf`, works only
  in 16-bit and 32-bit mode, makes a value above 0xffff an error in 16-bit
  mode, and in 32-bit mode adds 0x80000000 to a character preceded by the
  byte 0xff (ChangeLog 10.23 item 1; pcre2test.1, "INPUT ENCODING").
* `use_length` passes the pattern by length rather than zero-terminated, as
  `hex` already does (ChangeLog 10.23 item 22); `callout_error` makes a
  callout return PCRE2_ERROR_CALLOUT, so a match can fail with that error
  (ChangeLog 10.23 item 40); and `-ac` is a synonym for `-pattern
  auto_callout` (ChangeLog 10.23 item 38; pcre2test.1, "PATTERN MODIFIERS").
  `-ac` is a command-line global flag that cannot be passed from inside an
  input file; the checker exercises `auto_callout` via pattern modifiers in
  the inputs used here, so `-ac` the option is not separately in scope.
* Four smaller fixes are also visible: offset information for errors in
  hexadecimal patterns, an off-by-one in the name list for `get` and `copy`,
  the `offset` modifier being ignored under the POSIX API as documented, and
  `(?=(a\K))`, whose match can end before it starts, reported correctly there
  (ChangeLog 10.23 items 16, 23, 27 and 49).
* `pcre2_code_copy_with_tables()` copies the compiled code and its character
  tables, and `pcre2test` gains a command that pushes such a copy onto its
  pattern stack (pcre2api.3, "COMPILING A PATTERN"; ChangeLog 10.23 item 28).

### Fixes that show only as the absence of a failure

Partial matching of `/./` against an empty subject under a CRLF newline
convention must not read out of bounds (ChangeLog 10.23 item 30); `\p`, `\P`
and `\X` in a substitution string with PCRE2_SUBSTITUTE_EXTENDED must not
crash (ChangeLog 10.23 item 32); a start offset past the subject length in
`pcre2_substitute()` must be safe (ChangeLog 10.23 item 33); a locale set in
this or an earlier pattern must not crash on a wide character (ChangeLog 10.23
item 31); and PCRE2_CASELESS is ignored for `\h`, `\H`, `\v` and `\V` in
classes, which changes the compiled form only, never a result (ChangeLog 10.23
item 48). These safety fixes are **not separately in scope**: they are only
observable as the absence of a crash or out-of-bounds read, which a
byte-diff checker cannot distinguish from a working implementation.

### What does not change

* No compile or match option is added, and the only new function is
  `pcre2_code_copy_with_tables()` (pcre2api.3, "COMPILING A PATTERN"). There
  is no PCRE2_ENDANCHORED, no PCRE2_LITERAL and no extra-options word.
* The matcher is still the recursive one: the recursion limit,
  PCRE2_ERROR_RECURSIONLIMIT and the counts `find_limits` reports stay as
  they are; the heap frame vector, the depth limit and the heap limit arrive
  at step 4.
* Unicode stays at 8.0.0: no script or property is added (pcre2syntax.3,
  "SCRIPT NAMES FOR \p AND \P").
* The POSIX wrapper keeps step 2's behaviour: `REG_NOSUB` still does not
  imply PCRE2_NO_AUTO_CAPTURE, and the option mappings and `regerror()`
  messages are untouched (pcre2posix.3, "ERROR MESSAGES").

## What you inherit, and must not break

Step 2 brought the workspace to **PCRE2 10.22**: `REG_NOSUB` stopped implying
PCRE2_NO_AUTO_CAPTURE, infinite recursion loops became a match-time error
rather than a compile-time diagnostic, many compile-time diagnostics and
limits were tightened, and the tables moved to Unicode 8.0.0.

Besides the build and `pcre2test -C`, the gate runs a small number of narrow
end-to-end checks on behaviour introduced by *earlier* steps. If any part of
the gate fails, the step is not satisfied, however well the new milestone went.

Three inherited behaviours are most exposed by a compile-path restructure.
The UTF validity checks come first: the negative error codes for malformed
UTF-8, UTF-16 and UTF-32 input, and the offsets reported with them, do not
change at this release, and a compiler that reads the pattern differently
must still report them exactly as before (pcre2unicode.3, "VALIDITY OF UTF
STRINGS"). Second, every pattern the new compiler produces must behave under
`pcre2_dfa_match()` as well as under `pcre2_match()`, including the Unicode
property and lookaround cases; the recursion limit is the only thing that
changes there. Third, the `jit` and `jitverify` modifiers and the
`pcre2test -C` output must keep reporting a build without JIT support.

## Read these local references

Normative specification documents, under `/app/pcre2-doc/`. These are the
PCRE2 10.23 manual pages, copied verbatim; they contain no implementation
and no test expectations.

* **`pcre2api.3`** — the native API, and the page that records this step's
  headline change: "COMPILING A PATTERN" now says the error offset is not
  necessarily the furthest point in the pattern that was read. Also the
  option list, the context settings, the error-code overview and the
  `pcre2_pattern_info()` requests.
* **`pcre2pattern.3`** — the pattern syntax in detail, and the definitive
  statement of the syntax rules that change here: conditions and recursion
  conditions, back references in lookbehinds, the relative reference forms,
  and what is and is not allowed after a POSIX character class.
* **`pcre2callout.3`** — the callout interface: where automatic callouts are
  and are not inserted, and the length of the next pattern item passed to a
  callout, which this release redefines.
* `pcre2.3` — the introduction and the index of the other pages.
* `pcre2build.3` — the build-time options and what each one turns on.
* `pcre2compat.3` — where this release deliberately differs from Perl.
* `pcre2limits.3` — the size limits; two published limits change here.
* `pcre2matching.3` — the two matching algorithms compared.
* `pcre2partial.3` — partial matching with both functions, and restarting.
* `pcre2posix.3` — the POSIX wrapper: the option mappings, what `regexec()`
  ignores, and the `regerror()` messages.
* `pcre2serialize.3` — serializing compiled patterns and reloading them.
* `pcre2stack.3` — how the recursive matcher uses the system stack and how
  the recursion limit bounds it, which now applies to both matchers.
* `pcre2syntax.3` — the syntax quick reference, including the escape,
  condition and back-reference forms this release adds.
* `pcre2test.1` — the driver: its command line, its modifier grammar and the
  exact shape of every line it prints, including the modifiers and the
  command-line option added at this release.
* `pcre2unicode.3` — UTF and UCP modes, property support, and the UTF
  validity rules and their error codes in all three widths.
* `pcre2test.txt` — the plain-text rendering of `pcre2test.1`, for grepping.

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
