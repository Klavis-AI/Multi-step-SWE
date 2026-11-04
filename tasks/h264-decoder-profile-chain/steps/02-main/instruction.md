# H.264/AVC decoder — Main profile

## 1. Implement this milestone

Extend the decoder in this workspace so that it decodes **ITU-T H.264 Main
profile** bitstreams (`profile_idc` 77) in addition to everything it already
decodes.

Main adds three tool families over the profile the decoder already supports:
**CABAC** entropy coding, **B slices** with both direct prediction modes, and
**interlaced coding** — field pictures, complementary field pairs, and
macroblock-adaptive frame/field coding. It also adds **weighted prediction**,
explicit and implicit.

`/app/SPEC/02-main.md` lists what this milestone requires, each against its
clause in ITU-T H.264.

**Everything already specified in `/app/SPEC/01-baseline.md` must keep
working.** A Main profile stream still uses I and P slices, the same transforms,
the same deblocking filter and the same reference-picture management, and Main
profile streams (`profile_idc` 77) that stay on those inherited tools must
decode correctly just as the streams that need this milestone's new tools must,
so a regression there is as serious as missing the new work.

The interface is unchanged and is defined in `/app/TEST_INTERFACE.md`:

```
bin/ldecod -i <input.264> -o <output.yuv> -s
```

Each bitstream must be decoded within **120 seconds**. A decode that takes longer is stopped where it got to, so this is a budget to design against, not only a limit to stay under.

`/app/bin/ldecod` is itself a **Node script**: a text file whose first line is
a `#!` naming `node`, for example `#!/usr/bin/env node`. A shell script is
not acceptable — including one whose only statement is `exec node ...`. This is
not a style preference: nothing but a Node script at that path is run, so a
decoder behind a wrapper is never reached, no matter how good the decoder is.
Put your logic in JavaScript that this script imports.

What matters is the **decoded samples**: for each bitstream, the output, split
into frames, must match frame by frame the decoded output of the reference
decoder for that bitstream: the decoded YUV shipped with the ITU-T H.264.1
conformance suite where there is one, and the output of the JM reference
decoder (ITU-T H.264.2) for bitstreams encoded for this milestone.

## 2. Read these local references

**Normative semantics:**
- `/app/SPEC/02-main.md` — the tools this milestone adds, by ITU-T H.264 clause.
- `/app/SPEC/01-baseline.md` — the tools that must keep working.
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
- `/app/tests/visible/` — sample bitstreams with expected per-frame digests.
  Check progress with:

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
