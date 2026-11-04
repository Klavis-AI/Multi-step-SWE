# INTERFACE.md — how this engine is built and started

## 1. The programs

The engine implements four real command-line tools, at pinned versions.
Your implementations live at the paths below and are started with

    node --permission --allow-fs-read=* --allow-fs-write=* --no-addons --jitless --freeze-flags-after-init --require <guard> /app/bin/<tool>.js <the tool's own arguments>

with `LC_ALL=C` and an arbitrary working directory. Each tool runs as an
unprivileged user, and its standard input and output are pipes it inherits:
use them through descriptors 0 and 1 (`process.stdin`, `process.stdout`,
`fs.readSync(0, …)`, `fs.writeSync(1, …)`), not by reopening `/dev/stdin` or
`/dev/stdout`.

| your path | the tool it implements |
|---|---|
| `/app/bin/ftlint.js`   | **`ftlint`**, the font tester of **freetype-demos**, built against **FreeType 2.14.3**. Its complete source is `/data/spec/ftlint.c`, which defines its options and the exact text of every column it prints. |
| `/app/bin/hb-subset.js` | **`hb-subset`** from **HarfBuzz 14.4.0**, which writes a reduced font. `/data/spec/hb-subset-help.txt` is that build's own `--help` output. |
| `/app/bin/hb-vector.js` | **`hb-vector`** from **HarfBuzz 14.4.0**, which draws shaped glyph outlines as SVG. `/data/spec/hb-vector-help.txt` is that build's own `--help` output. |
| `/app/bin/hb-shape.js` | **`hb-shape`** from **HarfBuzz 14.4.0**. Its option set, defaults, exit codes and output syntax are its own; `/data/spec/hb-shape-help.txt` is that build's own `--help` output, and the `hb-shape(1)` man page and <https://harfbuzz.github.io/utilities.html> (local copy: `/opt/docs/harfbuzz/utilities.html`) document it. |

`hb-shape`'s default output syntax, from its own help text:

    [<glyph name or index>=<cluster>@<x offset>,<y offset>+<x advance>,<y advance>|...]

and `--batch` (defined by `/data/spec/batch.hh`, the tool's own source for it),
which reads one test per line from standard input, each line being
the tool's arguments separated by `;`, and prints one output line per input line.
`hb-shape` must support `--batch`.

With `--show-extents` ("Output glyph extents" in `/data/spec/hb-shape-help.txt`),
each glyph's entry ends, after its position fields, with the glyph's extents in
the form HarfBuzz's text serializer gives them
(`HB_BUFFER_SERIALIZE_FLAG_GLYPH_EXTENTS`):

    [<glyph name or index>=<cluster>@<x offset>,<y offset>+<x advance>,<y advance><<x_bearing>,<y_bearing>,<width>,<height>>|...]

The four numbers are HarfBuzz's `hb_glyph_extents_t`, in font units: `x_bearing`,
the distance from the x-origin to the left extremum of the glyph; `y_bearing`, the
distance from the top extremum of the glyph to the y-origin; `width`, from the left
extremum to the right one; `height`, from the top extremum to the bottom one, which
is negative because font coordinates grow up. A glyph whose extents HarfBuzz cannot
obtain gets no `<...>` part.

## 2. What the references are

* Rasterization: FreeType **2.14.3** with `FREETYPE_PROPERTIES=truetype:interpreter-version=35`
  (the classic TrueType interpreter).
* Shaping: HarfBuzz **14.4.0**, OpenType backend (`--shaper=ot`, `--font-funcs=ot`).

`ftlint` needs to support only `-f 2` (`FT_LOAD_NO_HINTING`) or `-f 8000`
(`FT_LOAD_NO_AUTOHINT`), `-r 0` (`FT_RENDER_MODE_NORMAL`), an `-i` range, and ppem
in {8, 11, 16, 24, 40}.

## 3. Environment rules

The engine must be self-contained JavaScript under `/app`. It must run from a copy of the
workspace with `node_modules/` removed, and under
a guard that makes unavailable: `WebAssembly`, `child_process`, `worker_threads`,
`cluster`, every network module, `vm`, `v8`, `repl`, `inspector`, `perf_hooks`, native addons (`.node`), `.wasm` files, and
any module specifier resolving outside the workspace. Node's `fs`, `path`,
`buffer` and similar remain available. Load the engine's own modules by relative path (`require('./…')`): the copy
is not at `/app`, so an absolute `/app/…` specifier resolves outside it.

The process runs with JIT compilation disabled (`node --jitless`), and the
time limits below apply to it as run that way.

A single tool invocation must finish within 60 seconds, except where this
document states otherwise below; the whole `--batch` shaping run must finish
within 900 seconds.

## 4. What must match

* **Subset structure.** The fonts your `hb-subset` writes must match the reference's output
  table by table, as fontTools' TTX serialisation shows them (normalised: `checkSumAdjustment`,
  `created` and `modified` are ignored), with the same glyph order and `cmap`.
* **Subset validity.** Run through **OTS 9.3.0** (`ots-sanitize`), every produced font must
  get the same verdict the reference's subset gets.
* **Subset shaping.** Shaped by the reference `hb-shape`, the produced font must give the same
  output as the reference's own subset. The text is NOT derived from which glyphs you
  retained: each case carries the exact `hb-shape` arguments its expectation was recorded with
  in its `shape_opts` field, and the output must match for those arguments, used verbatim.
* **Earlier work.** The shaping, outline and colour drawing cases the engine already had to
  pass must still match exactly, and `ftlint` must still render byte-identically to FreeType.
* **Malformed fonts.** For the malformed and fuzzed fonts `ftlint` already had to survive,
  `ftlint` must never crash (a run that prints `Error:` on stderr is a crash), never
  exceed the time limit, reach the same load / do-not-load decision FreeType reaches, and —
  where FreeType loads the font — reproduce its per-glyph output exactly. For every font the
  non-glyph lines `ftlint` prints must match FreeType's exactly as well: where the face opens,
  the `family style, N ppem, FLAGS, mode` line and the trailer; where FreeType cannot open it,
  the `opening error = 0x%04x, <message>` line that `ftlint.c`'s `Error()` prints. The codes
  and messages in that line and in per-glyph `loading error` / `rendering error` lines are
  FreeType 2.14.3's own error table, `/opt/docs/freetype/fterrdef.h`, which `ftlint.c`
  includes through `fterrors.h` (also in `/opt/docs/freetype/`).

A single `hb-subset` invocation must finish within 120 seconds.

## 5. Checking your progress

    bash /data/tests-public/run-public-tests.sh subset           # subsetting
    bash /data/tests-public/run-public-tests.sh vector           # outlines and colour
    bash /data/tests-public/run-public-tests.sh shape            # shaping
    bash /data/tests-public/run-public-tests.sh ftlint           # rasterization
