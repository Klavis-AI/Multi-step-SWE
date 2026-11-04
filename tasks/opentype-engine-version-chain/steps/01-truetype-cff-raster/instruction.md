# Step 1 — A TrueType and CFF font engine that renders what FreeType renders

## 1. Implement this milestone

Build a font engine in this workspace, from nothing, in JavaScript, and expose it
through **`ftlint`**, the font tester of the FreeType project. Driving it is

    node /app/bin/ftlint.js [options] ppem fontname [fontname2..]

`/data/INTERFACE.md` fixes how it is started and which options it is run with;
`/data/spec/ftlint.c` is `ftlint`'s own source and is the
definition of its options, its columns and its exact output format.

The reference is **FreeType 2.14.3** with its **classic TrueType interpreter**
(`interpreter-version=35`). For every invocation your program must print what that
FreeType prints — the file line, the `family style, N ppem, FLAGS, mode` line, the
column header, one line per glyph carrying the `shape`, `X+Yturn`, `WxH`, `Xacut`,
`Yacut` and MD5 columns that `ftlint.c` computes, and the `OK.` / `N fails.`
trailer. The MD5 is taken over the rendered 8-bit bitmap, so agreement means your
rasterizer produced FreeType's coverage bytes, not merely a similar shape.

That requires, normatively from the OpenType specification chapters in
`/data/spec/` and the Adobe specifications in `/data/spec/pdf/`:

* **The font file (`/data/spec/otff.txt`).** The table directory, TrueType and CFF
  flavours, font collections, and the tables the renderer needs: `head`, `maxp`,
  `hhea`, `hmtx`, `loca`, `glyf`, `cmap`, `name`, `post`, `OS/2`, `cvt `, `fpgm`,
  `prep`, `gasp`. `ftlint` prints the family and subfamily names, so the `name`
  table must be read the way FreeType chooses those two strings.
* **TrueType outlines (`/data/spec/glyf.txt`, `/data/spec/loca.txt`).** Simple
  glyphs with every flag, repeat and short-vector encoding; composite glyphs with
  offset and point-matching arguments, `SCALE`, `XY_SCALE` and `2x2` transforms,
  `USE_MY_METRICS`, `ROUND_XY_TO_GRID` and nesting; the two `loca` formats.
* **CFF Type 2 charstrings (`/data/spec/pdf/5176.CFF.pdf`,
  `/data/spec/pdf/5177.Type2.pdf`).** INDEX and DICT structures, charsets,
  encodings, local and global subroutines with the bias rule, `FDSelect`/`FDArray`
  and CID-keyed fonts, the full operator set including the flex family, the stem
  and hint-mask operators with the implicit `vstem` rule, the width convention
  against `defaultWidthX`/`nominalWidthX`, `endchar`'s deprecated four-argument
  form, and the `FontMatrix`.
* **Scaling.** `ppem` on the command line is turned into the scale FreeType derives
  from `FT_Set_Char_Size(ppem << 6, ppem << 6, 72, 72)`, and outlines reach the
  rasterizer in 26.6 fixed point.
* **Grid-fitting (`/data/spec/tt_instructing_glyphs.txt`,
  `/data/spec/tt_graphics_state.txt`; the instruction set itself:
  `/opt/docs/opentype/tt_instructions.md`).** Under load flag `8000`
  (`FT_LOAD_NO_AUTOHINT`) a font's own bytecode runs: the control-value table
  scaled to the size, `fpgm` once, `prep` at each size, then each glyph's program
  over a point zone that carries the two phantom points, with the complete
  instruction set — the graphics state and its projection, freedom and dual
  vectors, round states including super-round, control-value cut-in, minimum
  distance, single-width, the point-moving instructions, the delta instructions,
  interpolation and `IUP`, function definition and calls, the arithmetic and
  comparison instructions, and the measurement instructions. Under load flag `2`
  (`FT_LOAD_NO_HINTING`) no grid-fitting happens at all. An autohinter is never
  used, for either flag.
* **Rasterization.** Antialiased rendering (`-r 0`, `FT_RENDER_MODE_NORMAL`) to an
  8-bit coverage bitmap whose dimensions, bearings and every byte match FreeType's
  — which is exact-area coverage over the scan-converted outline, with the curve
  flattening FreeType performs.
* **Malformed input.** Fonts in the wild are broken and fuzzed. Your program must
  never crash and never hang on one; it must reach the same decision FreeType
  reaches about whether the face can be loaded at all; and where FreeType does
  load such a font, your output for it must match FreeType's exactly, exactly as
  for any other font — a damaged table makes individual glyphs fail to load or
  render, and those failures are themselves part of what `ftlint` prints.
  Where FreeType cannot open the face at all, `ftlint` prints an `opening error`
  line instead, and that line must match too. The codes and messages in all of
  these lines are FreeType's own error table, `/opt/docs/freetype/fterrdef.h`.

The engine must be self-contained JavaScript in `/app`: `/data/INTERFACE.md` §3
lists what is unavailable to it at run time (WebAssembly, native addons, child
processes, network, and any module outside the workspace), and every invocation
must finish within 60 seconds.

## 2. Read these local references

**Normative semantics:** `/data/spec/*.txt` — the OpenType specification chapters
listed above; `/data/spec/pdf/5176.CFF.pdf` and `/data/spec/pdf/5177.Type2.pdf` —
Adobe's Compact Font Format and Type 2 Charstring specifications.
`/data/spec/ftlint.c` — the tester's own source, which defines its options and the
exact text of every column it prints.

**Further OpenType chapters (local, not in `/data/spec/`):**
`/opt/docs/opentype/tt_instructions.md` — the TrueType instruction set;
`/opt/docs/opentype/ttch01.md` — TrueType fundamentals (FUnits and scaling, the
interpreter, the scan converter); `/opt/docs/freetype/fterrdef.h` — FreeType
2.14.3's error codes and messages, which `ftlint.c` prints through `fterrors.h`
(`/opt/docs/freetype/fterrors.h`). `/opt/docs/SOURCES.txt` records where each came
from.

**Additional files:** `/data/INTERFACE.md` — the start command, the option subset
in use, and the environment rules.
`/data/fonts/` — 30 real fonts covering plain, composite, hinted TrueType and CFF.
`/data/tests-public/run-public-tests.sh` — runs your engine over those fonts
against FreeType's recorded output and reports, per stratum, how many glyph lines
match; use it to check progress. `/app/WORKSPACE_HANDOFF.md` — leave a reproducible
handoff there.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`; the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and preserve the supplied documents and
  handoff files. `/data` is read-only payload and is not carried forward; anything
  you want to keep must live in `/app`.
- Leave `/app` source-only: no build products, no `node_modules`, no binary
  files. Keep the files you write plain ASCII, and write scratch data under
  `/tmp`.
- Do not cheat by using online solutions or hints specific to this task. Do not
  copy, vendor, wrap or invoke an existing font engine, in any language, in whole
  or in part; the engine must be your own JavaScript written against the
  specifications above.
- Run the local checks named above and keep `/app/WORKSPACE_HANDOFF.md` an accurate
  description of the repository.
