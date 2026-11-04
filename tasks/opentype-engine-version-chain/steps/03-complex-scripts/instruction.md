# Step 3 — Complex-script shaping: Indic, the Universal Shaping Engine, and the rest

## 1. Implement this milestone

The engine in this workspace renders glyphs and shapes Latin, Cyrillic, Greek,
Hebrew and Arabic text through OpenType Layout. It must now shape the scripts
whose correct rendering needs more than lookup application: the ones that reorder
their own characters before the font's features ever run.

Everything continues to be driven through the same two tools, at the same paths:
`node /app/bin/ftlint.js` and `node /app/bin/hb-shape.js`. `/data/INTERFACE.md`
fixes how they are started and which options they are run with;
`/data/spec/hb-shape-help.txt` is HarfBuzz 14.4.0's own help output.

Normatively, from the script-development chapters in `/data/spec/sd-*.txt`, the
Unicode character database in `/data/spec/ucd/`, and the layout chapters
`/data/spec/gsub.txt`, `/data/spec/gpos.txt`, `/data/spec/gdef.txt`:

* **The Indic shaping model** (`sd-devanagari.txt`, `sd-bengali.txt`,
  `sd-gurmukhi.txt`, `sd-gujarati.txt`, `sd-oriya.txt`, `sd-tamil.txt`,
  `sd-telugu.txt`, `sd-kannada.txt`, `sd-malayalam.txt`): syllable
  identification, the character classes of `IndicSyllabicCategory.txt` and
  `IndicPositionalCategory.txt`, base-consonant determination, reordering of
  reph, pre-base matras and below-base forms, the `nukt`/`akhn`/`rphf`/`rkrf`/
  `pref`/`blwf`/`half`/`abvf`/`pstf`/`vatu`/`cjct` feature sequence and the
  positional `init`/`pres`/`abvs`/`blws`/`psts`/`haln` sequence, the old-spec
  (`deva`) versus new-spec (`dev2`) behaviour, and the joiner rules for ZWJ and
  ZWNJ.
* **The Universal Shaping Engine** (`sd-use.txt`): the USE character classes, the
  cluster grammar, the reordering it prescribes, and the feature sequence it
  applies — for every script that is neither Indic nor otherwise special-cased,
  including Balinese, Javanese, Tibetan and the rest.
* **Khmer and Myanmar** (`sd-khmer.txt`, `sd-myanmar.txt`): their own syllable
  structures, reordering and feature sequences, distinct from both Indic and USE.
* **Thai and Lao** (`sd-thai.txt`, `sd-lao.txt`): the `SARA AM` decomposition and
  reordering, and the PUA fallback behaviour the reference implements.
* **Hangul** (`sd-hangul.txt`): syllable composition and decomposition between
  Jamo and precomposed syllables (The Unicode Standard 17.0.0, section 3.12,
  local copy `/opt/docs/unicode/core-spec-17.0.0-chapter-3.html`), and the
  `ljmo`/`vjmo`/`tjmo` features.
* **Tibetan and Sinhala** (`sd-tibetan.txt`, `sd-sinhala.txt`), including the
  contraction and reordering behaviour their tests exercise.

Shaping output must match exactly as before: the glyph names or indices, cluster
values, offsets and advances `hb-shape` prints, character for character.

**Everything the engine already does must keep working unchanged.** `hb-shape`
must still produce the reference's output for Latin, Cyrillic, Greek, Hebrew and
Arabic text and for the `GSUB`/`GPOS`/`GDEF` mechanism tests — every lookup type,
the contextual and chaining formats, the mark-attachment and mark-filtering
behaviour, the cursive attachment rules, and the default shaping model. `ftlint`
must still print exactly what FreeType 2.14.3 prints with its classic TrueType
interpreter for TrueType and CFF outlines, unhinted (`-f 2`) and with the font's
own bytecode (`-f 8000`).

Malformed input still matters for `ftlint`: for each malformed or fuzzed font, `ftlint` must not crash, not hang, and must reach the same load / do-not-load decision FreeType reaches; where FreeType loads the font, its per-glyph output and its `family style, N ppem, FLAGS, mode` line and trailer must match FreeType's exactly, and where FreeType cannot open it, `ftlint`'s `opening error` line must match FreeType's; the codes and messages are FreeType's own error table, `/opt/docs/freetype/fterrdef.h`. An `hb-shape` that crashes on a malformed font is a bug.

The engine must remain self-contained JavaScript in `/app`; `/data/INTERFACE.md`
§3 lists what is unavailable to it at run time.

## 2. Read these local references

**Normative semantics:** `/data/spec/sd-*.txt` — the script-development chapters
for every script above, including the Universal Shaping Engine; `/data/spec/ucd/`
— the Unicode 17.0.0 character database, including
`IndicSyllabicCategory.txt`, `IndicPositionalCategory.txt`, `ArabicShaping.txt`,
`UnicodeData.txt` and the derived property files; `/data/spec/gsub.txt`,
`/data/spec/gpos.txt`, `/data/spec/gdef.txt`, `/data/spec/chapter2.txt` — the
layout tables; the remaining `/data/spec/*.txt` and `/data/spec/pdf/*.pdf` — the
font file, TrueType outlines and instructions, CFF and Type 2 charstrings;
`/data/spec/ftlint.c` and `/data/spec/hb-shape-help.txt` — the tools' own
definitions of their options and output.

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
and environment rules. `/data/shape-fonts/` with
`/data/tests-public/shape-tests.json` — HarfBuzz's own shaping corpus for this
milestone and for the earlier ones, with recorded output.
`/data/fonts/` with `/data/tests-public/ftlint-expected.json` — the rasterization
corpus. `bash /data/tests-public/run-public-tests.sh shape|ftlint [filter]` runs
either against your engine. `/app/WORKSPACE_HANDOFF.md` — leave a reproducible
handoff there.

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
