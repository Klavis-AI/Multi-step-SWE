# H.264/AVC decoder — Baseline profile

## 1. Implement this milestone

Build an H.264/AVC video decoder that decodes **ITU-T H.264 Baseline profile**
bitstreams (`profile_idc` 66) and writes the decoded pictures as raw planar YUV.

The decoder is a program at `/app/bin/ldecod`, driven exactly like the JM
reference decoder:

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

It is written in **JavaScript and run by Node.js**, with no third-party
packages.

`/app/SPEC/01-baseline.md` lists the coding tools this milestone must support,
each against its clause in ITU-T H.264. `/app/TEST_INTERFACE.md` defines the
command line, the output byte layout, and the implementation constraints.

What matters is the **decoded samples**: for each bitstream, the output, split
into frames, must match frame by frame the decoded output of the reference
decoder for that bitstream: the decoded YUV shipped with the ITU-T H.264.1
conformance suite where there is one, and the output of the JM reference
decoder (ITU-T H.264.2) for bitstreams encoded for this milestone.

## 2. Read these local references

**Normative semantics:**
- `/app/SPEC/01-baseline.md` — the tool set required at this milestone, by
  ITU-T H.264 clause. The specification itself (ITU-T Rec. H.264) is the
  normative text; it is published by the ITU (local copy of the 04/2017
  edition: `/opt/docs/itu-t-h264/T-REC-H.264-201704.pdf`, with a text
  conversion beside it as `T-REC-H.264-201704.txt`; `/opt/docs` is reference
  material only, and everything the decoder needs must be in `/app`).

**Additional files:**
- `/app/TEST_INTERFACE.md` — invocation, output format, implementation
  constraints. Read this before writing any code; it defines exactly how the
  decoder is driven.
- `/app/WORKSPACE_HANDOFF.md` — what this workspace holds and what to leave
  behind for the next session.
- `/app/tests/visible/` — sample bitstreams with their expected per-frame
  digests, and one stream's full reference YUV so the output layout is
  unambiguous. Check progress with:

  ```
  node tests/visible/run_visible.mjs
  ```

  They are a small sample drawn from the same conformance suite, not the
  full range of bitstreams the decoder must handle.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and preserve the supplied documents,
  specifications and handoff files. Do not create a prose answer or an alternate
  entry point in place of the decoder.
- Do not search for, download, copy, wrap, or invoke
  an online implementation or task-specific answer; work only from this
  workspace, its documentation and `/opt/docs`, and do not search the system for
  other implementations or answers.
- Run the local checks named above and leave a reproducible handoff.
