# WORKSPACE_HANDOFF — step 10 of 13 (PCRE2 10.43)

This file explains how the workspace travels between steps. It does not define
the product — `pcre2-doc/PROFILE.md` and `TEST_INTERFACE.md` do that.

## What `/app` is

`/app` is a **cumulative workspace**. Each step runs in a fresh conversation
with no memory of the previous one, but on the *same* `/app` tree, with that
step's documentation overlaid on top. Everything you write under `/app` is what
the next step starts from, so the workspace is the handoff.

At the start of this step `/app` contains:

* the implementation you (or a previous step) wrote: `Makefile`, `src/`, and
  anything else you added;
* `pcre2-doc/` — refreshed to the PCRE2 10.43 manual pages for this step,
  with `README.md` (an index), `PROFILE.md` (this step's scope), `NEWS` and
  `ChangeLog.transition` (upstream's own change entries for this step);
* `testdata/` — refreshed to this release's own test corpus: the `testinput*`
  files **and** the `testoutput*` transcripts upstream expects from them, so
  you can check yourself against the release's published conformance data;
* `unicode-data/15.0.0/` — **the Unicode version changes this step**: this release
  moves to Unicode 15.0.0, and the generated tables (`pcre2_ucd.c`, `pcre2_ucp.h`, `pcre2_ucptables.c`)
  are replaced wholesale, at an inert path outside the build.
  Nothing includes them; they are reference data. Generating such tables from
  the Unicode Character Database is **out of scope** — adopting them into your
  own `src/` in whatever form your tree needs is not.
* `TEST_INTERFACE.md` — refreshed for this step;
* this file and `timer.sh`, both refreshed for this step.

The overlay **replaces** files with the same name. Documentation, the profile,
the corpus inputs, this file and `timer.sh` are overlay-owned and will be
replaced again next step; everything else is yours and is preserved exactly as
you leave it.

## Preservation rules

1. **Do not delete or move `pcre2-doc/`, `testdata/`, `TEST_INTERFACE.md`,
   `WORKSPACE_HANDOFF.md` or `timer.sh`.** Later steps and the checkpoint
   assume they are present at these paths.
2. **Keep the build working at every point you might be interrupted.** The
   snapshot is taken when the step's time runs out, not when you decide you are
   finished, so a workspace that does not build is a workspace that scores
   nothing — reach a compiling state early and improve it in place.
3. `build/` is **not** excluded from the snapshot: it carries forward with the
   rest of `/app`, and the grader runs `make` over the tree as you leave it,
   without cleaning first, so a `build/` tree you leave behind is reused
   rather than rebuilt from scratch. Never treat a built artefact as the
   deliverable; the checkpoint below rebuilds from a clean tree, which is the
   state worth leaving behind.
4. Do not add generated code that cannot be rebuilt offline from the workspace,
   and do not add a dependency the image does not already have. In particular
   the grading environment has a C compiler, `make` and the C library, and
   nothing else: no autotools, no CMake, no Perl, no Python.
5. Leave the workspace in the layout `TEST_INTERFACE.md` §1 describes.

## Checkpoint

Before the step ends, confirm the checkpoint by hand. These commands use only
what `TEST_INTERFACE.md` already promises, and cost about a minute:

```sh
cd /app
rm -rf build && make -j"$(nproc)"      # must succeed
test -x build/pcre2test

./build/pcre2test -C                   # must list 8-bit, 16-bit and 32-bit
                                       # support, and must NOT list JIT

cd /app/testdata                       # run from here: relative paths in an
                                       # input resolve against the cwd
for w in 8 16 32; do
  /app/build/pcre2test -q -$w testinput1 /tmp/out1.$w || echo "RUN FAILED -$w"
  diff -q testoutput1 /tmp/out1.$w     || echo "DIFF FAILED -$w"
done
```

That loop is the grader's own comparison, on one file. Run it across the whole
of `testdata/` while you work — it is the fastest signal you have. But note
that passing every visible transcript is **not** the whole score: the graded
corpus also contains inputs that are not in your workspace, aimed at the
behaviour this step introduces. Use the manual pages in `pcre2-doc/` to decide
what a pattern should produce, and `testdata/` to confirm you got it right.

`pcre2test -q <input> <output>` is exactly the invocation the grader uses, so a
run that crashes, hangs or exits non-zero here scores nothing there.

## Restoring this workspace elsewhere

If you are continuing this task in a new environment rather than inside the
step sequence, restore the complete `/app` artifact from the previous step
before starting — not just `src/`. The build is driven by your own `Makefile`,
which lives at `/app/Makefile`, and the corpus the checkpoint reads lives at
`/app/testdata/`. A partial restore that omits either will look like a
regression you did not cause.
