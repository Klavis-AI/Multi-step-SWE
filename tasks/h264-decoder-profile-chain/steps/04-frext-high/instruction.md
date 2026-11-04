# H.264/AVC decoder — the High profiles (FRExt)

## 1. Implement this milestone

Extend the decoder in this workspace so that it decodes the **fidelity range
extension** profiles of ITU-T H.264 in addition to everything it already
decodes: **High** (`profile_idc` 100), **High 10** (110), **High 4:2:2** (122),
**High 4:4:4 Predictive** (244) and **CAVLC 4:4:4 Intra** (44).

These profiles add the 8x8 transform and Intra_8x8 prediction, scaling
matrices, chroma formats other than 4:2:0, bit depths above 8, lossless
transform bypass, independent colour planes, and a second chroma QP offset.

`/app/SPEC/04-frext.md` lists what this milestone requires, each against its
clause in ITU-T H.264.

**Everything already specified in `/app/SPEC/01-baseline.md`,
`/app/SPEC/02-main.md` and `/app/SPEC/03-extended.md` must keep working.** The
bitstreams this milestone targets are entirely High-family: every one declares
a High-profile SPS, and the inherited tools are exercised inside those streams
rather than by separate Baseline or Main bitstreams.

The interface is unchanged and is defined in `/app/TEST_INTERFACE.md`:

```
bin/ldecod -i <input.264> -o <output.yuv> -s
```

Each bitstream must be decoded within **420 seconds**; a decode that takes longer is stopped where it got to. That is a cap on one stream, not an allowance every stream may spend: the decoder must get through 100 bitstreams inside a whole-run budget of 2700 seconds across 7 workers, so the figure to design against is a **mean of about 189 seconds** per bitstream. A decoder that uses the full 420 seconds on every stream does not finish within that budget.

`/app/bin/ldecod` is itself a **Node script**: a text file whose first line is
a `#!` naming `node`, for example `#!/usr/bin/env node`. A shell script is
not acceptable — including one whose only statement is `exec node ...`. This is
not a style preference: nothing but a Node script at that path is run, so a
decoder behind a wrapper is never reached, no matter how good the decoder is.
Put your logic in JavaScript that this script imports.

Note that the output layout `/app/TEST_INTERFACE.md` specifies already covers
what these profiles produce: plane sizes follow `chroma_format_idc`, a
monochrome stream is written as 4:2:0 with both chroma planes at
`1 << (BitDepthC − 1)`, and a bit depth above 8 makes every sample two bytes,
little-endian.

What matters is the **decoded samples**, frame by frame.

## 2. Read these local references

**Normative semantics:**
- `/app/SPEC/04-frext.md` — the tools this milestone adds, by ITU-T H.264 clause.
- `/app/SPEC/03-extended.md`, `/app/SPEC/02-main.md`, `/app/SPEC/01-baseline.md`
  — the tools that must keep working.
- ITU-T Rec. H.264, the normative text the SPEC files cite by clause: local
  copy of the 04/2017 edition at `/opt/docs/itu-t-h264/T-REC-H.264-201704.pdf`,
  with a text conversion beside it as `T-REC-H.264-201704.txt`.
  `/opt/docs` is reference material only; everything the decoder needs must be
  in `/app`.

**Additional files:**
- `/app/TEST_INTERFACE.md` — invocation, output byte layout, implementation
  constraints. Unchanged.
- `/app/WORKSPACE_HANDOFF.md` — what this workspace holds and what the previous
  session left behind.
- `/app/tests/visible/` — sample bitstreams with expected per-frame digests:

  ```
  node tests/visible/run_visible.mjs
  ```

  They are a sample, not the full range of bitstreams the decoder must
  handle.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Continue in the cumulative `/app` workspace. Preserve the inherited source,
  documents and handoff files; do not create a prose answer or a replacement
  entry point.
- Do not search for, download, copy, wrap, or invoke
  an online implementation or task-specific answer; work only from this
  workspace, its documentation and `/opt/docs`, and do not search the system for
  other implementations or answers.
- Run the documented local checks and leave a reproducible handoff.
