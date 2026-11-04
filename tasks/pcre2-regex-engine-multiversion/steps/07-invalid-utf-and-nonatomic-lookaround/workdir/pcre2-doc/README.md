# `/app/pcre2-doc` — the documentation for step 07 (PCRE2 10.34)

This directory is refreshed at the start of every step to the documentation
that upstream shipped with **PCRE2 10.34** (`pcre2-10.34`). Everything here
except this file and `PROFILE.md` is upstream's own text, byte for byte. It is
the specification: where this documentation and any other description of the
task disagree, the documentation and `../TEST_INTERFACE.md` win.

## Where to look first

| If you need | Read |
| --- | --- |
| what this step must implement | `PROFILE.md` (written for this step), then `../instruction.md` |
| what changed since the previous step | `ChangeLog.transition` — upstream's own numbered items for exactly the releases this step spans |
| pattern syntax and semantics | `pcre2pattern.3` — the largest and most important page |
| a quick syntax index | `pcre2syntax.3` |
| the C API: functions, options, info requests, error codes | `pcre2api.3` |
| how a pattern is matched, and the two matchers | `pcre2matching.3` |
| Unicode: properties, scripts, case folding, `\X`, UTF validity | `pcre2unicode.3` |
| documented limits and their defaults | `pcre2limits.3` |
| the build-time configuration macros | `pcre2build.3` |
| partial matching | `pcre2partial.3` |
| callouts | `pcre2callout.3` |
| serialization | `pcre2serialize.3` |
| pattern conversion (POSIX and glob syntaxes) | `pcre2convert.3`, where the release has it |
| the POSIX compatibility wrapper | `pcre2posix.3` |
| differences from Perl | `pcre2compat.3` |
| **the test harness: its input grammar and its exact output format** | `pcre2test.1`, and `pcre2test.txt` where the release ships the rendered form |
| the licence | `LICENCE` or `LICENCE.md`, plus `COPYING` |
| upstream's release narrative | `NEWS` |

## A note on format

The `.3` and `.1` files are nroff manual-page sources, shipped exactly as
upstream wrote them. They read perfectly well as plain text once you ignore the
`.` directives at the start of a line; `.\"` is a comment, `.SH` a section
heading, `.rs`/`.sp` vertical spacing, and `\fB`…`\fP` bold. Nothing has been
reformatted, because a reformatted specification is no longer the
specification.

## What is *not* here

Upstream's ~80 one-function manual pages (`pcre2_compile.3` and friends) are
omitted: each is a short stub that refers the reader to `pcre2api.3`, which is
present in full. The generated HTML tree and the concatenated `pcre2.txt` are
omitted for the same reason — they are other renderings of these same files.
