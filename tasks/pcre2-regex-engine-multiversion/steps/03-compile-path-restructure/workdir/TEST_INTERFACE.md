# TEST_INTERFACE — step 03 of 13 (PCRE2 10.23)

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

The set of numbered test files in `/app/testdata/` is the same as at step 2;
what changes at this release is their expected content, heavily. Most of the
churn is in compile diagnostics, so a single wrong error number, message or
offset costs one line, but a modifier `pcre2test` does not recognise costs a
whole case: the driver reports the unknown modifier instead of compiling the
pattern, and every line that follows in that file shifts out of alignment.
Three modifiers and one command-line option are added at this release and are
listed in `pcre2test.1`; a fourth modifier is not, and is the subject of the
next paragraph. The 16-bit and 32-bit cases in particular depend on the new
input encoding modifier being accepted exactly where the manual page says it
is allowed.

One `pcre2test` **pattern modifier** used by this release's expected
transcripts is described only in `/app/pcre2-doc/ChangeLog.transition`, not in
`pcre2test.1`: the modifier that pushes a copy of a compiled pattern taken
together with its character tables onto the pattern stack. Read the change
entry for the new copy-with-tables function to see what it must do.

As at step 2, the harness runs `pcre2test` a second time with `-error` for
three cases in scope and appends that output before comparing, so the option
must keep working. The normalising filter of section 4 still deletes the byte-code
listing, which means the internal effects of the compile restructure are
invisible and only the diagnostics, the match results and the
`pcre2_pattern_info()` values it reports are compared.
