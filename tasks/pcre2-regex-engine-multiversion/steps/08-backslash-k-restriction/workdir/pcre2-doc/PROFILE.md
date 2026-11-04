# PROFILE — what this task covers, and what it does not

This file is the same at every step except for the last section. It defines the
product. `TEST_INTERFACE.md` defines how the product is graded; the manual
pages in this directory define how it must behave.

## The product

A **PCRE2 regular-expression engine**: a C library that compiles a pattern into
an internal form and matches it against a subject, plus the `pcre2test` driver
that exercises it. The task is not "a regex engine of your own design" — it is
*this* engine, at a specific published release, observable through `pcre2test`.

Thirteen steps walk a single workspace forward along thirteen real PCRE2
releases, from 10.20 (2015) to 10.48 (2026). Each step names one release. The
milestone of a step is the behaviour that release introduced relative to the
previous step's release, and nothing else.

## In scope

* The pattern compiler: syntax, escapes, classes, quantifiers, groups,
  assertions, conditions, backreferences, subroutine calls, recursion, verbs,
  options and their interactions, and the compile-time diagnostics — error
  numbers, error messages and error offsets — exactly as documented.
* The interpreting matcher (`pcre2_match()`), including its backtracking model,
  its limits, and how they are reported.
* The alternative DFA matcher (`pcre2_dfa_match()`).
* The Unicode layer: properties, scripts, case folding, `\X`, UTF validity
  checking, and UTF and UCP mode in all three widths.
* The surrounding API: match data, substring extraction, `pcre2_substitute()`,
  pattern information, serialization, callouts, the POSIX wrapper, and the
  configuration query — to the extent `pcre2test` can reach them.
* All three code-unit widths: 8-bit, 16-bit and 32-bit, from one source tree.
* `pcre2test` itself, from step 2 onward: it is supplied working at step 1, and
  keeping it compiling and printing correctly as the library changes is part of
  the work from then on.

## Out of scope at every step

* **The JIT compiler in its entirety**, and the `sljit` backend under it.
  `SUPPORT_JIT` is never defined; the build must not require `sljit`, which
  from release 10.45 is not even in the upstream repository. `pcre2test -C`
  must report that JIT is unavailable.
* `pcre2grep` and its fixtures.
* The fuzzer harness.
* EBCDIC, z/OS and Windows configurations.
* The maintainer utilities: generating the Unicode tables, the chartables or
  the manual pages from their sources. Every generated artefact this engine
  needs is supplied to you as data.
* Build-system portability. There is exactly one supported build — `make` in
  `/app`, producing `/app/build/pcre2test` — and one supported platform, the
  container you are in. No autotools, no CMake, no cross-compilation, no shared
  libraries, no installation.
* Performance. Nothing is timed beyond the per-case timeout, and nothing is
  measured in allocations or instructions.

## How the thirteen steps run

| Step | Release | Subject |
| --- | --- | --- |
| 1 | 10.20 | The recursive engine |
| 2 | 10.22 | A cycle broken and a POSIX option stops implying another |
| 3 | 10.23 | The compile-path restructure |
| 4 | 10.30 | The heap-frame engine |
| 5 | 10.32 | De-recursing the DFA and persisting extra options |
| 6 | 10.33 | Script runs |
| 7 | 10.34 | Invalid UTF and non-atomic lookarounds |
| 8 | 10.39 | The atomicity withdrawal and the `\K` story |
| 9 | 10.42 | Script properties and the silent redefinition |
| 10 | 10.43 | Variable-length lookbehind and four class redefinitions |
| 11 | 10.45 | Limits, optimisation control, extended classes |
| 12 | 10.47 | Iteration and a security fix |
| 13 | 10.48 | Unicode 17 and the harness rewrite |

A step's documentation is *its own release's* documentation. It describes that
release's behaviour and no later release's. Where your recollection of PCRE2
disagrees with the manual page in this directory, the manual page is right and
your recollection is about a different release — several constructs in this
ladder mean one thing early and something else later, and reproducing the
modern meaning early is a scored failure, not a rounding error.

## Where you are

This is step 8 of 13, release 10.39, and it stands in for four upstream
releases: 10.35, 10.36, 10.37 and 10.38. Step 7 left a 10.34 engine with
invalid-UTF matching and non-atomic lookarounds. This step makes `\K` inside
a lookaround a compile error with an opt-out extra option, withdraws the
automatic atomicity of capturing groups that contain a recursive
backreference to themselves, adds `(?*` and `(?<*`, relaxes the repetition
of assertions, adds three `pcre2_substitute()` options and a loadable set of
character tables, and moves to Unicode 14.0.0. Step 9 will assume all of
that and will rework `\p` and `\P` on top of it.

How 10.39 differs from current PCRE2: a script name written bare, as
`\p{Greek}`, still means only that character's own script; `\p{sc:...}`,
`\p{scx:...}`, the four-letter script abbreviations and loose property-name
matching do not exist; lookbehinds are fixed-length; and the caseless and
ASCII restriction options of later releases are absent.
