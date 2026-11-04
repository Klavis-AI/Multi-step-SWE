# TEST_INTERFACE — step 08 of 13 (PCRE2 10.39)

The public contract between your workspace and the checker. Everything the
checker needs — a path, a name, a command, an argument grammar, an exit status,
a comparison rule — is either in this file or in a manual page under
`/app/pcre2-doc/`. Nothing else is assumed, and no hidden test introduces an
interface that is not named here.

## 1. Workspace layout

| Path | Owner | Meaning |
| --- | --- | --- |
| `/app/Makefile` | you | The build. Must work with no arguments. |
| `/app/src/` | you | The library and the supplied scaffolding. |
| `/app/build/` | you, generated | Build output. **Not** excluded from the snapshot — it carries forward with the rest of `/app`, and the checker runs `make` over the tree as it stands, without cleaning first. Never treat a built artefact as the deliverable. |
| `/app/testdata/` | overlay | This release's own test corpus: the `testinput*` files **and** the `testoutput*` transcripts upstream expects from them. Refreshed every step. |
| `/app/unicode-data/<v>/` | overlay | This release's **generated** Unicode tables (`pcre2_ucd.c`, `pcre2_ucp.h`, and `pcre2_ucptables*` where the release has them), at an inert path. Reference data: nothing includes it, and the checker never reads it. Generating such tables from the UCD is out of scope; adopting them into `src/` is your business. |
| `/app/pcre2-doc/` | overlay | This release's manual pages verbatim, its `COPYING` and `LICENCE`, the top-level `NEWS`, a `ChangeLog.transition` holding every change entry between the previous step's release and this one, a `PROFILE.md`, and a `README.md` index. |
| `/app/TEST_INTERFACE.md`, `/app/WORKSPACE_HANDOFF.md`, `/app/timer.sh` | overlay | Refreshed every step. |

You may add files and directories anywhere else under `/app`.

## 2. The build

The checker runs, as an unprivileged user, in a container that has **only** a C
compiler, `make`, `binutils` and the C library — no autotools, no CMake, no
Perl, no Python, no network:

```sh
cd /app && make -j<n>
```

It must exit 0 and produce an executable at **`/app/build/pcre2test`**. That is
the only artefact the checker looks for. How you lay out objects, libraries or
intermediate targets is yours; `make` with no target must do the whole job from
a clean tree, and must not require a network.

A tree that does not build fails the step, regardless of how much of the
milestone is implemented. Reach a compiling state early.

## 3. The binary the checker runs

`/app/build/pcre2test` is the process boundary. The checker invokes it exactly
as upstream's own test harness does:

```sh
/app/build/pcre2test -q -8  <input-file> <output-file>
/app/build/pcre2test -q -16 <input-file> <output-file>
/app/build/pcre2test -q -32 <input-file> <output-file>
```

* `-q` suppresses the version banner. `-8`, `-16` and `-32` select the
  code-unit width.
* Two positional arguments: the input file and the output file, in that order.
* It must exit **0** on a run in which every pattern and subject was processed,
  whatever the match results were.
* `/app/build/pcre2test -C` must exit 0 and report support for all three
  code-unit widths. The checker looks for the three width strings.
* For one file in scope the checker runs a second invocation
  `pcre2test -q -<w> -error <n,m,...>` and appends its output to the first
  before comparing. The `-error` option is documented in
  `/app/pcre2-doc/pcre2test.1`. This appendix is invisible in the input file;
  the expected transcript in `/app/testdata/` already includes it.
* The working directory during checking is a writable copy of the directory
  holding the inputs in scope, so a relative path in an input file resolves
  against it. Nothing else is placed there: an input that reads an
  auxiliary file by name only works at a step whose release ships that file
  (from 10.39 on, `testbtables`).

The checker uses its own copies of every input and expected file. It never
reads anything from `/app/testdata/`, so editing that directory changes
nothing about what is checked.

`pcre2test`'s input grammar and output format are documented in full in
`/app/pcre2-doc/pcre2test.1`. A working `src/pcre2test.c` and
`src/pcre2_printint.c` were supplied at step 1; from step 2 on they are yours
like the rest of `src/`, and keeping the harness compiling and printing
correctly as the library changes is part of the work.

## 4. How output is compared

For each (test file, width, mode) in scope the checker runs the command above
and compares its output file against the release's expected output **byte for
byte**, after both sides pass through the same normalising filter. A
requirement group is one or more (test-file, width, mode) comparisons that pass
or fail together; it passes only when every comparison in it passes. Every
comparison inside a group is required. Most groups hold exactly one comparison,
but a group covering width-dependent behaviour may span all three widths.

The normalising filter deletes, from both sides equally:

* the byte-code listing emitted by the `B`, `bincode`, `fullbincode` and
  `debug` modifiers — everything between a line of 60 or more `-` characters
  and the next such line;
* `Memory allocation (code space): N` and `Memory allocation (JIT code): N`;
* `Frame size for pcre2_match(): N`;
* the integers on the `Minimum heap limit = N`, `Minimum match limit = N` and
  `Minimum depth limit = N` lines printed by the `find_limits` modifier. Those
  numbers count entries to the matcher's own frame accounting, so they depend
  on how a matcher decomposes the search rather than on anything the manual
  pages specify. **The lines themselves are still required and still
  compared** — only the numbers on them are replaced.

Those are the quantities that depend on the internal encoding of a compiled
pattern or on the matcher's own step accounting, neither of which is part of
this contract. **Everything else is compared
exactly**: match and no-match results, capture numbers and values, mark and
callout output, substitution results, error numbers, error messages and error
offsets, and every value reported through `pcre2_pattern_info()`.

### What you can see, and what you cannot

`/app/testdata/` contains this release's own `testinput*` **and**
`testoutput*` files, exactly as upstream ships them. Those are the release's
published conformance data, and you are meant to use them: run your build
against an input, diff against the matching expected transcript, and iterate.
That is what upstream's own `RunTest` does.

**The corpus in scope is larger than your workspace.** Some cases use
inputs that are not in `/app/testdata/` and whose expected transcripts you
never see. They exercise the behaviour this step introduces, and they are
written against the manual pages — so a solution that reproduces the visible
transcripts without implementing the documented behaviour fails them. Derive
behaviour from `/app/pcre2-doc/`; use `/app/testdata/` to check yourself.

## 5. Scope

The **gate** is pass/fail. It is the build and `pcre2test -C` above, plus —
from step 2 onward — a small set of narrow end-to-end checks on behaviour
introduced by *earlier* steps. Breaking something you inherited means the
step is not satisfied.

The independent groups cover *this* step's milestone: above all the new
capability itself, tested through inputs that are not in your workspace;
reproducing this release's own 8-bit conformance transcripts; and peripheral
breadth — the other code-unit widths. Alternative-matcher (`-dfa`) runs are in
scope wherever the release's own tests use them.

## 6. Out of scope

* Your source layout, file names, object file names, symbol naming, internal
  headers, and the internal encoding of a compiled pattern.
* Comments, formatting, and any documentation you write.
* Your own tests, scripts, build logs and scratch files.
* Compile time, run time, memory use, allocation counts and instruction counts,
  except where a documented limit is exceeded. The checker does impose
  wall-clock ceilings as runaway guards — 900 seconds for the build and 20
  seconds per invocation — and an invocation that does not finish counts as a
  failure, so a correct but pathologically slow implementation can still fail
  the group it ran.
* `warning`- or `note`-level compiler diagnostics. The build must exit 0; it
  need not be warning-free.
* Anything outside the release's own behaviour: JIT (`pcre2_jit_*`, `sljit`),
  `pcre2grep`, the fuzzer harness, EBCDIC and z/OS configurations, Windows
  builds, and the maintainer utilities under `maint/`.
* Deriving the generated Unicode tables from the Unicode Character Database.
  Each release's own generated tables are handed to you under
  `/app/unicode-data/`; what is checked is the matching behaviour they
  support, not their derivation.
* Locale-dependent behaviour beyond the C locale. No case in scope selects a locale other than `C`, but the `locale` modifier itself must keep working; files in scope include patterns with `locale=C`.

## 7. Step-specific notes

* The gate re-runs step 05's DFA heap case end to end, so read it as a
  requirement rather than as history. `pcre2_dfa_match()` takes heap beyond
  a small reserved block, and the workspace a pattern with a couple of levels
  of internal recursion needs is larger than 4 KiB: for
  `/^(\((?:[a-z]|(?1))*\))$/` on `((q(w))(e))`, a run that sends it to the
  DFA matcher (`\=dfa` on the subject line) must report
  `Failed: error -63: heap limit exceeded` at `heap_limit=0` **and** at
  `heap_limit=4` — the modifiers count kibibytes, so four of them — while
  `heap_limit=4000` matches. A DFA whose heap workspace for that pattern
  fits inside 4 KiB fails the gate, and the step is not satisfied.
* A binary character-tables file first appears alongside the test data at
  this release, and section 3's note about the checking working directory is
  what makes a relative `#loadtables` command resolve — re-read it.
  `pcre2test` must implement that command and the fourth `tables` value,
  and must emit the documented complaint when tables 3 are selected before
  any tables have been loaded.
* `pcre2test` also gains, this step, the pattern modifier for the `\K`
  opt-out and three substitution modifiers. The `#perltest` directive is
  present in input in scope; the stricter delimiter rule it triggers is part
  of the milestone, but no `#perltest` section in scope uses a non-`/`
  delimiter, so that specific restriction is not directly exercised by any
  requirement group.
* No new numbered test file appears at 10.39. The set of files in scope and
  the widths at which each is run are chosen per step; the selection here
  is 8-bit: tests 1, 2, 4, 5, 9, 10, 14; 16- and 32-bit: tests 1, 2, 4,
  5, 11, 12, 14.
* The byte-code listing is filtered out of both sides, so opcodes that
  vanish from `B` output because a group stopped being made atomic are not
  themselves checked. What is checked is the match result and the capture
  values that the change produces.
