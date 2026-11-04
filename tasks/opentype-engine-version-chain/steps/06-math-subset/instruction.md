# Step 6 — A subsetter, and the MATH table

## 1. Implement this milestone

The engine in this workspace renders glyphs, shapes text, applies variations and
draws colour glyphs. It must now **write fonts**, not only read them: produce a
reduced font that contains exactly what a chosen subset of the original needs and
is valid everywhere the original was.

Expose that through a fourth tool of the same family, **`hb-subset`** of
**HarfBuzz 14.4.0**:

    node /app/bin/hb-subset.js --output-file=OUT [options] font-file

`/data/INTERFACE.md` fixes how all four tools are started and which options they
are run with; `/data/spec/hb-subset-help.txt` is that
build's own help output, which defines its option set.

`hb-subset` must support these options: `--unicodes` (including `*`), `--gids`,
`--layout-features`, `--drop-tables`, `--no-hinting`, `--retain-gids`,
`--glyph-names` and `--variations`. 
What a correct subset means, normatively from the chapters in `/data/spec/`:

* **Glyph closure.** The requested code points map through `cmap` to glyphs;
  those glyphs pull in their composite components, and the glyphs any retained
  `GSUB` lookup can produce from them. `--layout-features` decides which features
  are retained and therefore which lookups participate.
* **Every table retargeted, not merely copied.** `glyf`/`loca` or `CFF`/`CFF2`
  rebuilt for the new glyph order; `hmtx`/`vmtx` and `hhea`/`vhea` truncated
  consistently (`vmtx` and `vhea`: `/opt/docs/opentype/vmtx.md`, `vhea.md`);
  `cmap` rebuilt in the formats the retained characters need;
  `post` and glyph names according to `--glyph-names`; `maxp`, `head` and `OS/2`
  updated, including the Unicode range bits; `GSUB`/`GPOS`/`GDEF` with their
  coverage tables, class definitions, lookups and feature lists renumbered and
  pruned; `COLR`/`CPAL` and the bitmap and `SVG ` tables reduced to the retained
  glyphs; `fvar`/`avar`/`gvar`/`HVAR`/`VVAR`/`MVAR` and the item variation stores
  reduced, or instantiated away when `--variations` pins an axis.
* **`MATH`** (`/data/spec/math.txt`): the constants table, the per-glyph italic
  correction, top-accent attachment and extended-shape coverage, the kerning-info
  table with its correction heights, and the variants table with its glyph
  assemblies — all reduced to the retained glyphs, with coverage and index tables
  renumbered.
* **`--retain-gids`** keeps the original glyph indices, leaving holes;
  `--no-hinting` drops the instruction streams and the tables that exist only to
  serve them; `--drop-tables` removes exactly what it names.

The fonts your subsetter writes must match the reference subsetter's output
table by table, pass **OTS 9.3.0**'s validity checks, and still shape the same. Each case records the exact
`hb-shape` arguments its expectation was made with, in the case's `shape_opts`
field, so you can reproduce any expectation yourself.

**Everything the engine already does must keep working unchanged.** `hb-vector`
must still draw variable instances and colour glyphs as the reference draws them;
`hb-shape` must still reproduce the reference's shaping for the layout
mechanisms, for Latin, Cyrillic, Greek, Hebrew and Arabic, and for the Indic,
Universal Shaping Engine, Khmer, Myanmar, Thai, Hangul, Tibetan and Sinhala
models; `ftlint` must still print exactly what FreeType 2.14.3 prints with its
classic TrueType interpreter for TrueType and CFF outlines, unhinted (`-f 2`) and
with the font's own bytecode (`-f 8000`).

Malformed input still matters for `ftlint`: for each malformed or fuzzed font, `ftlint` must not crash, not hang, and must reach the same load / do-not-load decision FreeType reaches; where FreeType loads the font, its per-glyph output and its `family style, N ppem, FLAGS, mode` line and trailer must match FreeType's exactly, and where FreeType cannot open it, `ftlint`'s `opening error` line must match FreeType's; the codes and messages are FreeType's own error table, `/opt/docs/freetype/fterrdef.h`. An `hb-shape` or `hb-vector` that crashes on a malformed font is a bug.

The engine must remain self-contained JavaScript in `/app`; `/data/INTERFACE.md`
§3 lists what is unavailable to it at run time.

## 2. Read these local references

**Normative semantics:** `/data/spec/math.txt` — the MATH table;
`/data/spec/otff.txt` — the font file, its table directory and checksums;
`/data/spec/cmap.txt`, `/data/spec/glyf.txt`, `/data/spec/loca.txt`,
`/data/spec/hmtx.txt`, `/data/spec/maxp.txt`, `/data/spec/head.txt`,
`/data/spec/hhea.txt`, `/data/spec/os2.txt`, `/data/spec/post.txt`;
`/data/spec/gsub.txt`, `/data/spec/gpos.txt`, `/data/spec/gdef.txt`,
`/data/spec/chapter2.txt`; the variable-font and colour chapters already in
`/data/spec/`; `/data/spec/pdf/*.pdf` — CFF and Type 2 charstrings;
`/data/spec/ftlint.c`, `/data/spec/hb-shape-help.txt`,
`/data/spec/hb-vector-help.txt`, `/data/spec/hb-subset-help.txt` — the four
tools' own definitions of their options and output.

**Further references (local, not in `/data/spec/`):** under `/opt/docs/opentype/`,
the OpenType chapters `tt_instructions.md` (the TrueType instruction set),
`ttch01.md` (TrueType fundamentals), `kern.md`, `vhea.md`, `vmtx.md`, `vorg.md`,
`ebdt.md`, `eblc.md`, `base.md`, `hdmx.md`, `ltsh.md` and `vdmx.md`, and the
OpenType Layout tag registry — `scripttags.md`, `languagetags.md`,
`featuretags.md` and the registered-feature pages `features_ae.md`,
`features_fj.md`, `features_ko.md`, `features_pt.md` and `features_uz.md`;
`/opt/docs/unicode/core-spec-17.0.0-chapter-3.html` — The Unicode Standard 17.0.0,
chapter 3 (normalization, conjoining jamo); `/opt/docs/harfbuzz/utilities.html` —
the HarfBuzz manual's chapter on its command-line tools;
`/opt/docs/w3c/compositing-1-20240321.html` — Compositing and Blending Level 1,
which `colr.txt` cites for the `PaintComposite` modes. `/opt/docs/SOURCES.txt`
records where each came from.

**Additional files:** `/data/INTERFACE.md`; `/data/subset-fonts/` with
`/data/tests-public/subset-tests.json` — fonts to subset and the table set the
reference produces for each case; `/data/vector-fonts/`, `/data/shape-fonts/` and
`/data/fonts/` with their `tests-public` expectations;
`bash /data/tests-public/run-public-tests.sh subset|vector|shape|ftlint [filter]`.
`/app/WORKSPACE_HANDOFF.md` — leave a reproducible handoff there.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`; the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and preserve the supplied documents and
  handoff files. `/data` is read-only payload; anything you want to keep must live
  in `/app`.
- Leave `/app` source-only: no build products, no `node_modules`, no binary
  files, plain ASCII, scratch under `/tmp`.
  Write the fonts your subsetter produces to the path `--output-file` names, not
  into `/app`.
- Do not cheat by using online solutions or hints specific to this task. Do not
  copy, vendor, wrap or invoke an existing font, shaping or subsetting engine, in
  any language, in whole or in part; the engine must be your own JavaScript
  written against the specifications above.
- Run the local checks named above and keep `/app/WORKSPACE_HANDOFF.md` an
  accurate description of the repository.
