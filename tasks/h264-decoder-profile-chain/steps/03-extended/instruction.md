# H.264/AVC decoder — Extended profile

## 1. Implement this milestone

Extend the decoder in this workspace so that it decodes **ITU-T H.264 Extended
profile** bitstreams (`profile_idc` 88) in addition to everything it already
decodes.

Extended is the error-resilience profile. It adds two tool families:

- **slice data partitioning** — a coded slice split across NAL unit types 2, 3
  and 4, where the B and C partitions may be absent;
- **SP and SI switching slices** — `slice_type` 3, 4, 8 and 9, whose
  reconstruction goes through the transform domain.

`/app/SPEC/03-extended.md` lists what this milestone requires, each against its
clause in ITU-T H.264, and states which SP/SI output must match the reference.

**Everything already specified in `/app/SPEC/01-baseline.md` and
`/app/SPEC/02-main.md` that this profile permits must keep working** — Extended
combines the slice-group and arbitrary-slice-order tools of the first with the B slices,
weighted prediction and interlaced coding of the second. Note that Extended
bitstreams never use CABAC (`entropy_coding_mode_flag` is 0). Bitstreams at this
milestone that exercise only inherited tools must decode correctly just as the
bitstreams that need this milestone's new tools must, so a regression there is
as serious as missing the new work.

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

What matters is the **decoded samples**, frame by frame.

## 2. Read these local references

**Normative semantics:**
- `/app/SPEC/03-extended.md` — the tools this milestone adds, and which SP/SI
  output must match the reference.
- `/app/SPEC/02-main.md` and `/app/SPEC/01-baseline.md` — the tools that must
  keep working.
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
