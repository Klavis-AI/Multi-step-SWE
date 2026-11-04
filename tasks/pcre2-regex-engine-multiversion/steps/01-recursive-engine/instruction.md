# Step 01 of 13 — The recursive engine

## Implement this milestone

PCRE2 **10.20** (2015) is where the ladder starts, and it is the only step
whose workspace arrives part-built. `/app/src/` holds headers, generated data
and a working test driver, and no engine at all: `pcre2.h.generic` and its
`pcre2.h.in` source, `config.h.generic`, `pcre2_internal.h`,
`pcre2_intmodedep.h`, `pcre2_ucp.h`, `pcre2posix.h`, the generated tables
`pcre2_chartables.c.dist` and `pcre2_ucd.c`, and `pcre2test.c` with
`pcre2_printint.c`. `src/SCAFFOLD_MANIFEST.txt` names exactly those files,
with their sizes and hashes, and nothing else under `src/` exists — the
compiler, both matchers, study and auto-possessification, substitution,
serialization, the context, match-data and substring functions, the error
table and the POSIX wrapper are all absent, and together they are this
milestone. The one thing a candidate working from a memory of modern PCRE2
will get wrong is the matcher: at 10.20 `pcre2_match()` is recursive, match
depth is C-stack depth, and the second limit is spelled **recursion limit**,
not depth limit. Implement the heap-frame matcher and every limit name, error
number and error message in this release's transcripts comes out wrong.

### The interpreting matcher recurses, and its limit is the recursion limit

* `pcre2_match()` is implemented by recursion; the page that discusses its
  resource use is written entirely in terms of system stack
  (pcre2stack.3, "PCRE2 DISCUSSION OF STACK USAGE").
* There are exactly two match limits. The second is the **recursion limit**,
  set by `pcre2_set_recursion_limit()`, reported by
  `PCRE2_INFO_RECURSIONLIMIT`, and described as limiting "the depth of
  recursion" (pcre2api.3, "PCRE2 CONTEXTS"). There is no heap limit and no
  depth limit; those arrive at step 4.
* Exceeding them must produce, through `pcre2test`, exactly
  `Failed: error -47: match limit exceeded` and
  `Failed: error -53: recursion limit exceeded`. Both limits are reachable
  from a subject line (pcre2test.1, "Setting match and recursion limits").
* The corresponding start-of-pattern items are `(*LIMIT_MATCH=ddd)` and
  `(*LIMIT_RECURSION=ddd)`, and a value larger than the one already set is
  ignored (pcre2pattern.3, "Setting match and recursion limits").
* The `find_limits` modifier must print `Minimum match limit = n` **and**
  `Minimum recursion limit = n`, in that order, before the match result
  (pcre2test.1, "Finding minimum limits"). The two **lines** are compared; the
  two **numbers** are not, because they count entries to your own matcher's
  recursive step function and so depend on how you decomposed it. See
  `/app/TEST_INTERFACE.md` section 4.
* The recursion limit "is not relevant, and is ignored" for
  `pcre2_dfa_match()` at this release (pcre2api.3, "PCRE2 CONTEXTS"). It
  starts to apply there at step 3.

### The two-pass compiler and its diagnostics

* Compilation is two passes over the pattern, preceded by a pre-pass that
  identifies capturing groups and their names; that pre-pass was refactored at
  this very release (ChangeLog 10.20 item 36).
* Error numbers, error messages and the reported error offset are all compared
  byte for byte. At this release the offset is essentially the point the
  scanner had reached, and `pcre2test` reports failures as
  `Failed: error NN at offset MM: text` (pcre2test.1, "DEFAULT OUTPUT FROM
  pcre2test"). The numbers and their message text are **not** enumerated in
  the manual pages -- pcre2api.3, "COMPILATION ERROR CODES" says only that
  there are over 80 of them and that `pcre2_get_error_message()` returns the
  text. Take the numbers, the messages and the offsets from this release's own
  transcripts in `/app/testdata/`, which show each one in context.
* Simple infinitely-looping recursions are still diagnosed **at compile time**
  at this release; only more complicated cases are left to `pcre2_match()`
  (pcre2api.3, "ERROR RETURNS FROM pcre2_match()"). That split moves at
  step 2.
* An empty subpattern name such as `(?'')` is an error
  (ChangeLog 10.20 item 34), and `(?(?C)` with no assertion condition is
  diagnosed rather than accepted (ChangeLog 10.20 item 3).
* `\8` and `\9` follow current Perl (ChangeLog 10.20 item 24), and
  `PCRE2_ALT_CIRCUMFLEX` exists (ChangeLog 10.20 item 23).
* `PCRE2_NEVER_BACKSLASH_C` exists and makes `\C` a compile error
  (ChangeLog 10.20 item 21; pcre2api.3, "COMPILING A PATTERN").

### Both matchers, all three widths

* `pcre2_match()` is the standard backtracking algorithm and
  `pcre2_dfa_match()` the alternative one; their differing result sets,
  partial-matching behaviour and restrictions are specified in
  (pcre2matching.3, "THE ALTERNATIVE MATCHING ALGORITHM").
* The alternative matcher's output shape differs — it reports a list of
  matches, longest first (pcre2test.1, "OUTPUT FROM THE ALTERNATIVE MATCHING
  FUNCTION").
* One source tree must yield 8-bit, 16-bit and 32-bit libraries, selected by
  `PCRE2_CODE_UNIT_WIDTH`, and `pcre2test -C` must report all three and must
  report that JIT is not available (pcre2api.3, "PCRE2 8-BIT, 16-BIT, AND
  32-BIT LIBRARIES"; pcre2test.1, "COMMAND LINE OPTIONS").
* Partial matching, restarting a partial match with `PCRE2_DFA_RESTART`, and
  the `PCRE2_ERROR_PARTIAL` return must behave as specified
  (pcre2partial.3, "PARTIAL MATCHING USING pcre2_match()"; pcre2test.1,
  "RESTARTING AFTER A PARTIAL MATCH").

### The rest of the API that pcre2test can reach

* Contexts: general, compile and match, with their allocators, the newline and
  `\R` conventions, `parens_nest_limit`, the stack guard and
  `pcre2_set_recursion_memory_management()` (pcre2api.3, "PCRE2 CONTEXTS").
* Every `pcre2_pattern_info()` request `pcre2test` prints under the `info`
  modifier, including the capture count, name table, first and required code
  units, match limits and `PCRE2_INFO_ALLOPTIONS`
  (pcre2api.3, "INFORMATION ABOUT A COMPILED PATTERN").
* Substring extraction by number, by name and as a list, and duplicate names
  (pcre2api.3, "DUPLICATE SUBPATTERN NAMES").
* `pcre2_substitute()` with `PCRE2_SUBSTITUTE_GLOBAL` and this release's
  replacement syntax, which is `$$`, `$<n>` and `${<n>}` and nothing else --
  there is no `PCRE2_SUBSTITUTE_EXTENDED` and no `${name:-str}` form at 10.20
  (pcre2api.3, "CREATING A NEW STRING WITH SUBSTITUTIONS").
* Serialization and the `#save`/`#load` commands
  (pcre2serialize.3, "SAVING COMPILED PATTERNS"; pcre2test.1, "SAVING AND
  RESTORING COMPILED PATTERNS").
* The POSIX wrapper `regcomp()`, `regexec()`, `regerror()` and `regfree()`,
  including the option mapping and the error names
  (pcre2posix.3, "COMPILING A PATTERN"; pcre2posix.3, "ERROR MESSAGES").
* Callouts: numerical and string arguments, both new at this release
  (ChangeLog 10.20 item 1), `pcre2_callout_enumerate()`
  (ChangeLog 10.20 item 4), `PCRE2_AUTO_CALLOUT`, and the places where
  optimisation removes a callout (pcre2callout.3, "MISSING CALLOUTS").
* `pcre2_config()` for every request the release defines
  (pcre2api.3, "CHECKING BUILD-TIME OPTIONS").

### Unicode 7.0.0

* Properties, scripts, `\X`, case folding and the PCRE2-specific properties,
  driven by the supplied `pcre2_ucd.c` and `pcre2_ucp.h`
  (pcre2pattern.3, "Unicode character properties"; pcre2unicode.3, "UNICODE
  PROPERTY SUPPORT").
* UTF validity checking with per-error-kind reporting in all three widths,
  and `PCRE2_NO_UTF_CHECK` (pcre2unicode.3, "VALIDITY OF UTF STRINGS"). The
  exact offsets and error codes are compared.
* `\p` and `\P` used in a library built without Unicode support must be an
  error rather than silently wrong (ChangeLog 10.20 item 45); the build used here
  does have Unicode support, but the option handling must be right.
* Where UTF mode changes the meaning of a construct — `\C`, `\X`, case-less
  matching of wide characters, class ranges — the documented behaviour is
  in (pcre2unicode.3, "WIDE CHARACTERS AND UTF MODES").

### What does not change

* **Nothing here is heap-frame based.** No `PCRE2_ERROR_DEPTHLIMIT`, no
  `pcre2_set_depth_limit()`, no `pcre2_set_heap_limit()`, no
  `(*LIMIT_DEPTH=)` or `(*LIMIT_HEAP=)`, and no `Frame size for
  pcre2_match()` line. All of that arrives at step 4 (10.30).
* `REG_NOSUB` still implies `PCRE2_NO_AUTO_CAPTURE` in the POSIX wrapper, so
  a pattern compiled through `regcomp()` with it cannot use back references.
  That stops being true at step 2.
* The compile-error offset is still the furthest point read, and the error
  numbering is this release's. Both are rewritten at step 3.
* There is no pattern-conversion API, no `PCRE2_ENDANCHORED`, no
  `PCRE2_LITERAL`, no `PCRE2_EXTRA_*` option word, no `/xx`, and no script-run
  construct. Do not add any of them.

## What you inherit

Nothing. This is the first step; `/app` is as the task created it.

## Read these local references

Normative specification documents, under `/app/pcre2-doc/`. These are the
PCRE2 10.20 manual pages, copied verbatim; they contain no implementation
and no test expectations.

* **`pcre2api.3`** — the whole native API: contexts and their limits,
  `pcre2_compile()` and every option it takes, both matching functions, the
  match data block, pattern information, substring extraction, substitution,
  and the full list of compile-time and match-time error codes. This is the
  page most of the milestone is specified in.
* **`pcre2pattern.3`** — the pattern syntax in detail: escapes, classes,
  quantifiers, groups, back references, subroutine calls, recursion,
  conditions, assertions, callouts, the backtracking verbs, and the
  start-of-pattern items including the two limit settings.
* **`pcre2test.1`** — the driver you are handed: its command line, its
  modifier grammar, and, most importantly, the exact shape of every line it
  prints. Output is compared byte for byte, so this page is normative.
* `pcre2.3` — the introduction and the index of the other pages.
* `pcre2build.3` — the build-time options and what each one turns on. The
  supplied `Makefile` already fixes the set this task uses; read it to know
  what a macro means, not to change the build.
* `pcre2callout.3` — the callout interface, the fields passed to a callout,
  callout return values, callout enumeration, and the cases where an
  optimisation means no callout happens.
* `pcre2compat.3` — where this release deliberately differs from Perl.
* `pcre2limits.3` — the hard limits on pattern size, group count, name
  length, lookbehind length and repeat counts at this release.
* `pcre2matching.3` — the two matching algorithms compared: what the
  alternative one returns, and what it cannot do.
* `pcre2partial.3` — partial matching with both functions, hard versus soft,
  and restarting from a partial match.
* `pcre2posix.3` — the POSIX wrapper: which POSIX options map to which PCRE2
  options, what `regexec()` ignores, and the error names.
* `pcre2serialize.3` — serializing compiled patterns and reloading them.
* `pcre2stack.3` — how the recursive matcher uses the system stack, and how
  the recursion limit bounds it. Read this before writing the matcher.
* `pcre2syntax.3` — the quick-reference syntax summary, including the
  complete list of script names and property names this release recognises.
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
