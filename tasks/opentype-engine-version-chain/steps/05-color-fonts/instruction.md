# Step 5 — Colour fonts: COLR v0 and v1, and CBDT/sbix

## 1. Implement this milestone

The engine in this workspace renders monochrome glyphs, shapes text with
OpenType Layout including the complex scripts, and applies variations. It must
now render **colour glyphs**: fonts that carry their own colour artwork, whether
as layered outlines, as a paint graph, or as embedded bitmaps. A font that
carries its artwork as SVG documents is drawn the way the reference draws it:
from its plain outlines.

Colour output goes through the same drawing tool as before:

    node /app/bin/hb-vector.js -O svg --precision=2 [--font-palette=N] [--variations=...] --unicodes=... font-file

and through `hb-shape --show-extents`, whose glyph extents for a colour glyph
come from its colour data (the output syntax is in `/data/INTERFACE.md` §1).

`/data/INTERFACE.md` fixes how all three tools are started and which options
they are run with.

Normatively, from the table chapters in `/data/spec/`:

* **`COLR` version 0 with `CPAL`** (`colr.txt`, `cpal.txt`): the base-glyph and
  layer records, the palette table with its colour records and its entry labels,
  the palette-type and label arrays, and the composition of layers in order.
* **`COLR` version 1**: the paint graph — `PaintColrLayers`, `PaintSolid`,
  `PaintLinearGradient`, `PaintRadialGradient`, `PaintSweepGradient`,
  `PaintGlyph`, `PaintColrGlyph`, the transform paints, `PaintComposite` with its
  composite modes (Compositing and Blending Level 1, local copy
  `/opt/docs/w3c/compositing-1-20240321.html`), and the variable forms of each;
  colour lines with their extend modes and colour stops; the clip list and the
  layer list; and the variation of paint values through the item variation
  store.
* **`CBDT`/`CBLC`** (`cbdt.txt`, `cblc.txt`): the strike and index subtable
  formats, the metrics records, and the embedded PNG data.
* **`sbix`** (`sbix.txt`): strikes, glyph data records, their graphic types and
  the origin offsets.
* **The `SVG ` table** (`svg.txt`): the reference's `hb-vector` does not render
  a glyph's SVG document; for a font that has one it draws the glyph's plain
  outline, and that is what yours must draw. Rendering the SVG documents is not
  required.
* **Palette selection**: which palette applies by default and how an explicitly
  chosen palette index changes the result, as the reference's `--font-palette`
  option selects it.

**Everything the engine already does must keep working unchanged.**
`hb-vector` must still draw variable-font instances as the reference draws them;
`hb-shape` must still reproduce the reference's output for the layout mechanisms,
for Latin, Cyrillic, Greek, Hebrew and Arabic, and for the Indic, Universal
Shaping Engine, Khmer, Myanmar, Thai, Hangul, Tibetan and Sinhala models;
`ftlint` must still print exactly what FreeType 2.14.3 prints with its classic
TrueType interpreter, for TrueType and CFF outlines, unhinted (`-f 2`) and with
the font's own bytecode (`-f 8000`).

Malformed input still matters for `ftlint`: for each malformed or fuzzed font, `ftlint` must not crash, not hang, and must reach the same load / do-not-load decision FreeType reaches; where FreeType loads the font, its per-glyph output and its `family style, N ppem, FLAGS, mode` line and trailer must match FreeType's exactly, and where FreeType cannot open it, `ftlint`'s `opening error` line must match FreeType's; the codes and messages are FreeType's own error table, `/opt/docs/freetype/fterrdef.h`. An `hb-shape` or `hb-vector` that crashes on a malformed font is a bug.

The engine must remain self-contained JavaScript in `/app`; `/data/INTERFACE.md`
§3 lists what is unavailable to it at run time.

## 2. Read these local references

**Normative semantics:** `/data/spec/colr.txt`, `/data/spec/cpal.txt`,
`/data/spec/cbdt.txt`, `/data/spec/cblc.txt`, `/data/spec/sbix.txt`,
`/data/spec/svg.txt` — the colour tables; `/data/spec/otvarcommonformats.txt` —
the item variation store the variable paints index; the variable-font, layout,
script-development and font-file chapters already in `/data/spec/`;
`/data/spec/ucd/` — Unicode 17.0.0; `/data/spec/ftlint.c`,
`/data/spec/hb-shape-help.txt`, `/data/spec/hb-vector-help.txt` — the tools' own
definitions of their options and output.

**Further references (local, not in `/data/spec/`):** under `/opt/docs/opentype/`,
the OpenType chapters `tt_instructions.md` (the TrueType instruction set),
`ttch01.md` (TrueType fundamentals), `kern.md`, `vhea.md`, `vmtx.md`, `vorg.md`,
`ebdt.md` and `eblc.md` (the bitmap formats `cbdt.txt` and `cblc.txt` extend), and
the OpenType Layout tag registry — `scripttags.md`, `languagetags.md`,
`featuretags.md` and the registered-feature pages `features_ae.md`,
`features_fj.md`, `features_ko.md`, `features_pt.md` and `features_uz.md`;
`/opt/docs/unicode/core-spec-17.0.0-chapter-3.html` — The Unicode Standard 17.0.0,
chapter 3 (normalization, conjoining jamo); `/opt/docs/harfbuzz/utilities.html` —
the HarfBuzz manual's chapter on its command-line tools;
`/opt/docs/w3c/compositing-1-20240321.html` — Compositing and Blending Level 1,
which `colr.txt` cites for the `PaintComposite` modes. `/opt/docs/SOURCES.txt`
records where each came from.

**Additional files:** `/data/INTERFACE.md`; `/data/vector-fonts/` with
`/data/tests-public/vector-tests.json`; `/data/shape-fonts/` with
`/data/tests-public/shape-tests.json`; `/data/fonts/` with
`/data/tests-public/ftlint-expected.json`;
`bash /data/tests-public/run-public-tests.sh vector|shape|ftlint [filter]`.
`/app/WORKSPACE_HANDOFF.md` — leave a reproducible handoff there.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`; the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and preserve the supplied documents and
  handoff files. `/data` is read-only payload; anything you want to keep must live
  in `/app`.
- Leave `/app` source-only: no build products, no `node_modules`, no binary
  files, plain ASCII, scratch under `/tmp`.
- Do not cheat by using online solutions or hints specific to this task. Do not
  copy, vendor, wrap or invoke an existing font or shaping engine, in any
  language, in whole or in part; the engine must be your own JavaScript written
  against the specifications above.
- Run the local checks named above and keep `/app/WORKSPACE_HANDOFF.md` an
  accurate description of the repository.
