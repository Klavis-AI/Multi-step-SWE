# Segment the corpus into lines (UAX-14)

## 1. Implement this milestone

`/app/stringzilla` is StringZilla at a pinned upstream tag with the optimized
backends of one or more kernel families removed. The library is complete and
correct: it builds, it passes its own test suite, and every public entry point
produces exactly the output it produced before. It is simply slow in the places
whose backends are missing — every one of those entry points now runs the
portable implementation that upstream ships beside them.

**This milestone: make UAX-14 line-break segmentation faster, without changing a single output byte.**

It covers `sz_utf8_linebreaks` — every position at which a line may break, per the Unicode line-breaking algorithm: a table-driven state machine over character classes, not a scan for `\n`.

The family currently has no optimized backend: its backend files are gone and
the dispatch falls through to the portable implementation, which is the
reference for both correctness and speed. Those entry points live in
`include/stringzilla/utf8_linebreaks.h` and `include/stringzilla/utf8_linebreaks/`, whose `tables.h` holds the property tables the portable implementation reads too. Optimize it however you judge
best — a new backend header of your own beside the portable one, a rewritten
portable implementation, whatever makes the entry points faster. You are
measured on the entry points themselves, through the library's own dispatch, so
any route that makes them faster is acceptable.

Earlier milestones must keep working. This workspace is cumulative: work you
have already done is still here, still part of this step's baseline, and still
re-checked for correctness, so a regression breaks both the earlier work and
this step's baseline.

**What success means here.**

1. **The library must behave exactly as it does now.** This is an optimization
   task, not a behaviour-change task: every public entry point must produce
   byte-identical output to the tree you started from, for every input and every
   configuration — not merely for the cases you happen to test. An
   approximation, a fast path that is right "almost always", or a reordering
   that changes rounding is a failure, however fast it is. The portable
   implementations that are in the tree define the correct output; do not
   weaken them into a faster wrong answer.
2. **This family must get faster on the workload described below.** Only this
   family is in scope. Optimizing some other part of the library is out of
   scope for this milestone, and so is effort that does not show up in the
   measurement.

`/app/BENCHMARK.md` gives the exact commands for both — the behaviour checks and
the timing — and names the entry points this milestone is timed on, so you can
confirm where you stand before finishing. The same checks are re-run against a
workload you cannot see.

**The workload is generated, fixed and not disclosed.** It is a multilingual
text corpus built deterministically by the checker, and the entry points above
are timed on it individually. You are not told its draw: optimize the family for
the general case, because a kernel that only wins on one shape of input does not
satisfy this milestone.

Long lines of mixed-script prose with the punctuation, digits, brackets and multi-byte characters that UAX-14 classifies separately, so the answer is a property-dependent sequence of positions rather than one per newline.

**The capability envelope.** The target host is x86-64, and both your tree
and the reference tree it is compared against are compiled with the same fixed
command line (`-mavx2 -mbmi -mbmi2 -maes -mpclmul -msse4.2 -mlzcnt -mpopcnt`).
The reference's kernels stop at AVX2 — 256 bits. The host has no AVX-512 at
all, and although its CPU lists SHA-NI, neither that command line nor any
reference kernel enables it: reaching for either would measure a machine the
reference had no access to. So it is checked — the code this milestone looks
after, its own files and anything their headers pull in, is scanned for it, and
a tree whose kernels use a wider tier is not acceptable. The measurement is
single-threaded for the same reason: the reference creates no threads, so the
entry points in scope must not create them, or child processes, either — every
run of your tree under this measurement is capped at one process, and a
kernel that needs threads will not get them here.

The property tables are the contract: keep the same classes and the same carried state and the positions are identical. The work is in evaluating the same transitions over many characters at once.

## 2. Read these local references

**Normative references, all local:**

- `include/stringzilla/utf8_linebreaks.h` and `include/stringzilla/utf8_linebreaks/`, whose `tables.h` holds the property tables the portable implementation reads too.
- `/app/BENCHMARK.md` — the exact behaviour and timing commands, and the entry
  points this milestone is timed on.
- `/app/tools/measure` — a timing tool for individual entry points
  (`/app/tools/measure <entry-point>`, lower is better). It is compiled against
  your headers, so rebuild it with `/app/tools/build.sh` after every change; the
  source is `/app/tools/measure.c`.
- `/app/WORKSPACE_HANDOFF.md` — what earlier milestones changed, and how to
  build and measure locally.

The other kernel families in this tree are not your concern: some are already
optimized, some are not, and none of them is in scope here. Leave their behaviour
alone, and remember that the whole library's suite has to keep passing.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace. Keep `/app/stringzilla` as the source
  tree, keep the upstream build system working
  (`cmake -S /app/stringzilla -B <dir> -DSTRINGZILLA_BUILD_TEST=ON`), and
  preserve the handoff documents.
- You do not have network access. **Do not fetch, copy, vendor, or transcribe
  StringZilla's own optimized implementation of this family, or any other
  project's, from the internet or a package mirror.** The optimization must be
  yours. Do not inspect `/tests`, later-step directories, or solution material.
- Do not weaken, filter, rename, or disable tests.
- Leave a reproducible handoff in `/app/WORKSPACE_HANDOFF.md`: what you changed,
  the speedup you measured locally, and how to rebuild.
