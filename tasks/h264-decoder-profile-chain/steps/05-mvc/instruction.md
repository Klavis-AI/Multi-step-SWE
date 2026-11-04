# H.264/AVC decoder — multiview (MVC)

## 1. Implement this milestone

Extend the decoder in this workspace so that it decodes **multiview** H.264
bitstreams — the MVC amendment, ITU-T H.264 **Annex H** — in addition to
everything it already decodes. MVC adds the **Multiview High**
(`profile_idc` 118) and **Stereo High** (128) profiles, and the bitstreams
this milestone targets are Annex H streams whose views are carried by the
layered NAL unit types below. Their **first** sequence parameter set is a
High-family base layer, as Annex H requires; the multiview `profile_idc` is
carried by the **subset** sequence parameter set instead, and both 118 and 128
occur among those streams, so a subset-SPS parser must accept either.

This milestone requires three things:

- **The layered NAL unit types**: prefix NAL units (type 14), subset sequence
  parameter sets (type 15) carrying the MVC SPS extension, and coded slice
  extensions (type 20), together with the `nal_unit_header_mvc_extension`
  fields that say which view a slice belongs to.
- **Inter-view prediction**: a decoded view component of the same access unit
  used as a reference, entered into the reference picture lists in the order
  the subset SPS gives, and all views decoded and output.
- **Decoding a layered bitstream as its base layer**: a stream carrying
  enhancement layers a decoder does not implement must still decode to its base
  layer, because clause 7.4.1 requires a decoder to discard NAL units of types
  reserved or unspecified for it.

`/app/SPEC/05-mvc.md` lists what this milestone requires, each against its
clause.

**Where the pictures go.** A bitstream that declares more than one view — one
carrying a subset sequence parameter set (NAL unit type 15) — writes one file
per output view instead of the plain `-o` file. The name is the `-o` argument
with `_ViewId%04d` inserted before the extension, and the number is that view's
**actual `view_id`**, not a sequential index: a two-view stream whose views are
0 and 2 produces `out_ViewId0000.yuv` and `out_ViewId0002.yuv`. This holds even
if your decoder outputs only the base view of such a stream. Most bitstreams
at this milestone must decode correctly in every view they carry; a small
subset declares its views through type 15 but only its base view must match,
so `out_ViewId0000.yuv` is the only file that matters for those streams. In
either case, the per-view files are the decoder's output for such a stream,
and the plain `-o` file is no substitute for them — `/app/SPEC/05-mvc.md`
states the naming rule in full.

**Everything already specified in `/app/SPEC/01-baseline.md`,
`/app/SPEC/02-main.md`, `/app/SPEC/03-extended.md` and `/app/SPEC/04-frext.md`
must keep working.** Every bitstream this milestone targets is a layered
stream: each one carries a subset sequence parameter set and coded slice
extensions, and the inherited tools are exercised inside those streams rather
than by separate single-view bitstreams. For a group of these streams only the
**base-layer** output must match — see the base-layer requirement above — so
skipping types 14, 15 and 20 correctly matters as much here as decoding them.

The interface is unchanged and is defined in `/app/TEST_INTERFACE.md`:

```
bin/ldecod -i <input.264> -o <output.yuv> -s
```

Each bitstream must be decoded within **420 seconds**. A decode that takes longer is stopped where it got to, so this is a budget to design against, not only a limit to stay under.

`/app/bin/ldecod` is itself a **Node script**: a text file whose first line is
a `#!` naming `node`, for example `#!/usr/bin/env node`. A shell script is
not acceptable — including one whose only statement is `exec node ...`. This is
not a style preference: nothing but a Node script at that path is run, so a
decoder behind a wrapper is never reached, no matter how good the decoder is.
Put your logic in JavaScript that this script imports.

What matters is the **decoded samples**, frame by frame, across every view a
bitstream carries.

## 2. Read these local references

**Normative semantics:**
- `/app/SPEC/05-mvc.md` — the tools this milestone adds, and the multi-view
  output file naming.
- `/app/SPEC/04-frext.md`, `/app/SPEC/03-extended.md`, `/app/SPEC/02-main.md`,
  `/app/SPEC/01-baseline.md` — the tools that must keep working.
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
