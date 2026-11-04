# Step 2 — OpenType Layout: GSUB, GPOS and GDEF

## 1. Implement this milestone

The engine in this workspace renders glyphs. It must now also **shape text**:
turn a sequence of Unicode code points into the sequence of positioned glyphs the
font's OpenType Layout tables prescribe. Expose that through **`hb-shape`** of
**HarfBuzz 14.4.0**, at

    node /app/bin/hb-shape.js [options] font-file

`/data/INTERFACE.md` fixes how both tools are started, which options they are
run with, and the `--batch` protocol `hb-shape` is driven with. `/data/spec/hb-shape-help.txt` is that build's own help output:
its option set and its default output syntax `[name=cluster@dx,dy+ax,ay|…]`.

Normatively, from `/data/spec/chapter2.txt`, `/data/spec/gsub.txt`,
`/data/spec/gpos.txt` and `/data/spec/gdef.txt`:

* **The common table formats.** Script and language-system records with
  `DefaultLangSys` and explicit `LangSysRecord`s, feature lists and feature
  indices, lookup lists with their flags, `Coverage` formats 1 and 2, class
  definition formats 1 and 2, device and variation index tables, and the ordering
  rules that decide which lookups run and in what order.
* **`GSUB`, every lookup type.** Single (formats 1–2), Multiple, Alternate,
  Ligature, Contextual (formats 1–3), Chaining contextual (formats 1–3),
  Extension, and Reverse chaining single substitution, including the sequence
  rules, the recursion into nested lookups, and the mark-skipping and
  mark-filtering-set behaviour lookup flags impose.
* **`GPOS`, every lookup type.** Single, Pair (formats 1–2, with both value
  records and both value formats), Cursive attachment, Mark-to-Base,
  Mark-to-Ligature, Mark-to-Mark, Contextual and Chaining contextual positioning,
  and Extension positioning — with anchor formats 1–3, the attachment rules, and
  the cursive `RIGHT_TO_LEFT` handling.
* **`GDEF`.** Glyph class definitions, attachment lists, ligature caret lists,
  mark attachment classes and mark glyph sets, and the effect all of these have on
  lookup matching and on the glyph properties reported.
* **The default shaping model.** Script and language detection from the text,
  bidirectional reordering of the buffer for right-to-left runs, Unicode
  normalization as the shaper performs it, the feature set applied by default and
  the ones `--features` turns on and off, cluster values as HarfBuzz assigns and
  merges them, and the fallback positioning applied when a font has no `GPOS`.
* **Scripts.** Latin, Cyrillic, Greek, Hebrew and Arabic, including the Arabic
  joining algorithm over the Unicode joining types in
  `/data/spec/ucd/ArabicShaping.txt`, the `init`/`medi`/`fina`/`isol` feature
  application it drives, and the mark ordering and normalization those scripts
  require. `/data/spec/ucd/` carries the Unicode character database files the
  reference uses.

**Everything the engine already does must keep working unchanged.** `ftlint` must
still print, for every font and size, exactly what FreeType 2.14.3 prints with its
classic TrueType interpreter — the file line, the `family style, N ppem, FLAGS,
mode` line, the column header, the per-glyph `shape`, `X+Yturn`, `WxH`, `Xacut`,
`Yacut` and MD5 columns, and the `OK.` / `N fails.` trailer — for TrueType and CFF
outlines, unhinted (`-f 2`) and with the font's own bytecode (`-f 8000`).

Malformed input still matters for `ftlint`: for each malformed or fuzzed font, `ftlint` must not crash, not hang, and must reach the same load / do-not-load decision FreeType reaches; where FreeType loads the font, its per-glyph output and its `family style, N ppem, FLAGS, mode` line and trailer must match FreeType's exactly, and where FreeType cannot open it, `ftlint`'s `opening error` line must match FreeType's; the codes and messages are FreeType's own error table, `/opt/docs/freetype/fterrdef.h`. An `hb-shape` that crashes on a malformed font is a bug.

The engine must remain self-contained JavaScript in `/app`; `/data/INTERFACE.md`
§3 lists what is unavailable to it at run time.

## 2. Read these local references

**Normative semantics:** `/data/spec/chapter2.txt`, `/data/spec/gsub.txt`,
`/data/spec/gpos.txt`, `/data/spec/gdef.txt` — OpenType Layout; `/data/spec/ucd/`
— the Unicode character database files the reference uses; the remaining
`/data/spec/*.txt` chapters and `/data/spec/pdf/*.pdf` — the font file, TrueType
outlines and instructions, and the CFF and Type 2 charstring formats;
`/data/spec/ftlint.c` and `/data/spec/hb-shape-help.txt` — the two tools'
own definitions of their options and output.

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
and environment rules. `/data/shape-fonts/` and
`/data/tests-public/shape-tests.json` — HarfBuzz's own shaping corpus for this
milestone with its recorded output; `/data/fonts/` and
`/data/tests-public/ftlint-expected.json` — the rasterization corpus.
`bash /data/tests-public/run-public-tests.sh shape|ftlint` runs either against
your engine and reports how much matches. `/app/WORKSPACE_HANDOFF.md` — leave a
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
