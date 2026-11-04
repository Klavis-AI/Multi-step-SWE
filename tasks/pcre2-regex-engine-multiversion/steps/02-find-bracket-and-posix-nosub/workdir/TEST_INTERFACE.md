# TEST_INTERFACE — step 02 of 13 (PCRE2 10.22)

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
* the integers on `Minimum match limit = N` and `Minimum recursion limit = N`,
  printed by the `find_limits` modifier. Those count entries to the matcher's
  own recursive step function, so they depend on how a matcher decomposes the
  search rather than on anything the manual pages specify. **The two lines
  themselves are still required and still compared** — only the numbers on
  them are replaced.

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

From this step onwards nothing under `/app/src/` and no `Makefile` is
supplied. `src/pcre2test.c` and `src/pcre2_printint.c` were scaffolding at
step 1 and are now ordinary parts of the codebase: every modifier, option and
output line this release's `pcre2test` documents has to be there because you
put it there. In particular the new POSIX **pattern** modifier `posix_nosub`
— `pcre2test.1` lists it with the pattern modifiers, and says it is not
allowed on a subject line or after compilation — and the new `-error`
command-line option are part of the milestone, not free.

For one of the cases in scope the harness runs `pcre2test` a second time with
`-error` and a list of numbers, appending that output to the first run's
output before comparing. This mirrors what upstream's own test script does.
`pcre2test.1` documents what `-error` is for but not the shape of the lines it
prints; the authority for the shape is this release's own expected transcript
for test 2, which is in `/app/testdata/` and is itself in scope. Reproduce its
`-error` appendix and you have the format.

The file `testbtables` is **not present** at this release and no input uses
`#loadtables` at this step. That directive and its file arrive later; `§3`
says only files the inputs in scope name for themselves appear in the working
directory.

Four numbered test files exist in `/app/testdata/` at this release that did
not at step 1, and one existing test's expected output splits into one file
per code-unit width. Some expected files are now named for the link size as
well; only the default link size of 2 is built, which is the value
`config.h.generic` sets and the one `pcre2test -C` must report.

The two match limits are inherited, and this step extends match-limit checking
into recursion, so they are easy to disturb. They are required by the
**gate**, not by one of the comparison groups: an input outside your workspace
drives `(*LIMIT_MATCH=)`, `(*LIMIT_RECURSION=)`, `match_limit=` and
`recursion_limit=` at recursive patterns and compares
`Failed: error -47: match limit exceeded` and
`Failed: error -53: recursion limit exceeded` literally, including which of
the two a given control produces. The *counts* are a different matter: the
`Minimum match limit` and `Minimum recursion limit` lines that `find_limits`
prints depend on how many times your matcher happens to re-enter itself, so
they are **not** checked anywhere, and neither is `testinput15`, the file
built around them.

Two more things this release introduces that no case in scope reaches, listed
so you do not spend time on them. `pcre2test` now rejects a binary zero in its
input (ChangeLog 10.22 item 3) — that path abandons the run and exits
non-zero, and §3 covers only runs in which every pattern and subject was
processed, so no input in scope contains a binary zero. And whether
`pcre2test -C` prints a JIT line is not compared as text; what is checked is
the behaviour, through this release's own test 16, which asks for JIT on a
pattern and must answer `JIT support is not available in this version of
PCRE2`.

Section 4 lists what the normalising filter deletes. Note what it does not:
every `pcre2_pattern_info()` value and every error offset are compared
literally.
