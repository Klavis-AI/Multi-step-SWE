# Step 4 — OpenType 1.8 variable fonts

## 1. Implement this milestone

The engine in this workspace renders glyphs from TrueType and CFF outlines and
shapes text with OpenType Layout, including the complex scripts. It must now
support **variable fonts**: one font file that describes a continuous design
space, from which a particular instance is selected by axis coordinates.

Expose outlines through a third tool of the same family, **`hb-vector`** of
**HarfBuzz 14.4.0**, which draws the shaped, positioned glyphs as SVG:

    node /app/bin/hb-vector.js -O svg --precision=2 [--variations=...] --unicodes=... font-file

`/data/INTERFACE.md` fixes how all three tools are started and which options they
are run with; `/data/spec/hb-vector-help.txt` is that
build's own help output.

Normatively, from `/data/spec/otvaroverview.txt`,
`/data/spec/otvarcommonformats.txt` and the table chapters:

* **`fvar`** — the axes, their minimum/default/maximum values and flags, and the
  normalization of a user coordinate to the −1…+1 normalized space.
* **`avar`** — segment maps applied during normalization, in the order the
  specification gives them.
* **`gvar`** — glyph variation data: the shared tuple records, per-glyph tuple
  headers, intermediate-region tuples, private point numbers, packed point and
  delta encodings, the scalar computation for a tuple, the accumulation of deltas,
  and **inferred deltas** for points a tuple does not name — including the
  composite-glyph case.
* **`HVAR`** — advance-width variation with the delta-set index maps, and the
  fallback to `gvar`-derived phantom points when it is absent.
* **`MVAR`** — variation of font-wide metrics.
* **`CFF2`** — the CFF2 charstring format: its operator differences from Type 2,
  the `blend` operator and the variation-store it indexes, `vsindex`, and the
  absence of the width convention. `/data/spec/cff2.txt` defines it.
* **Item variation stores and delta-set index maps**, shared by the tables above,
  as `/data/spec/otvarcommonformats.txt` defines them.

Applying a set of axis coordinates must produce the instance's outlines and its
advances in both tools: `hb-vector --variations=…` draws the
instance, and `hb-shape --variations=…` shapes with the instance's metrics and
with whatever feature variations the font's `GSUB`/`GPOS` feature-variations
records select at those coordinates.

**Everything the engine already does must keep working unchanged.** `hb-shape`
must still reproduce the reference's output for the layout mechanisms — every
`GSUB`/`GPOS` lookup type, the contextual and chaining formats, `GDEF` — for
Latin, Cyrillic, Greek, Hebrew and Arabic, and for the Indic, Universal Shaping
Engine, Khmer, Myanmar, Thai, Hangul, Tibetan and Sinhala models. `ftlint` must
still print exactly what FreeType 2.14.3 prints with its classic TrueType
interpreter, for TrueType and CFF outlines, unhinted (`-f 2`) and with the font's
own bytecode (`-f 8000`).

Malformed input still matters for `ftlint`: for each malformed or fuzzed font, `ftlint` must not crash, not hang, and must reach the same load / do-not-load decision FreeType reaches; where FreeType loads the font, its per-glyph output and its `family style, N ppem, FLAGS, mode` line and trailer must match FreeType's exactly, and where FreeType cannot open it, `ftlint`'s `opening error` line must match FreeType's; the codes and messages are FreeType's own error table, `/opt/docs/freetype/fterrdef.h`. An `hb-vector` or `hb-shape` that crashes on a malformed font is a bug.

The engine must remain self-contained JavaScript in `/app`; `/data/INTERFACE.md`
§3 lists what is unavailable to it at run time.

## 2. Read these local references

**Normative semantics:** `/data/spec/otvaroverview.txt`,
`/data/spec/otvarcommonformats.txt`, `/data/spec/fvar.txt`, `/data/spec/avar.txt`,
`/data/spec/gvar.txt`, `/data/spec/hvar.txt`, `/data/spec/mvar.txt`,
`/data/spec/cff2.txt` — variable fonts; the layout chapters `/data/spec/gsub.txt`,
`/data/spec/gpos.txt`, `/data/spec/gdef.txt`, `/data/spec/chapter2.txt`; the
script-development chapters `/data/spec/sd-*.txt`; `/data/spec/ucd/` — Unicode
17.0.0; the remaining `/data/spec/*.txt` and `/data/spec/pdf/*.pdf` — the font
file, TrueType outlines and instructions, CFF and Type 2 charstrings;
`/data/spec/ftlint.c`, `/data/spec/hb-shape-help.txt` and
`/data/spec/hb-vector-help.txt` — the three tools' own definitions of their
options and output.

**Further references (local, not in `/data/spec/`):** under `/opt/docs/opentype/`,
the OpenType chapters `tt_instructions.md` (the TrueType instruction set),
`ttch01.md` (TrueType fundamentals), `kern.md`, `vhea.md`, `vmtx.md` and
`vorg.md`, and the OpenType Layout tag registry — `scripttags.md`,
`languagetags.md`, `featuretags.md` and the registered-feature pages
`features_ae.md`, `features_fj.md`, `features_ko.md`, `features_pt.md` and
`features_uz.md`; `/opt/docs/unicode/core-spec-17.0.0-chapter-3.html` — The
Unicode Standard 17.0.0, chapter 3 (normalization, conjoining jamo);
`/opt/docs/harfbuzz/utilities.html` — the HarfBuzz manual's chapter on its
command-line tools. `/opt/docs/SOURCES.txt` records where each came from.

**Additional files:** `/data/INTERFACE.md` — start commands, option subsets
and environment rules. `/data/vector-fonts/` with
`/data/tests-public/vector-tests.json`; `/data/shape-fonts/` with
`/data/tests-public/shape-tests.json`; `/data/fonts/` with
`/data/tests-public/ftlint-expected.json`.
`bash /data/tests-public/run-public-tests.sh vector|shape|ftlint [filter]` runs
your engine against any of them. `/app/WORKSPACE_HANDOFF.md` — leave a
reproducible handoff there.

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
