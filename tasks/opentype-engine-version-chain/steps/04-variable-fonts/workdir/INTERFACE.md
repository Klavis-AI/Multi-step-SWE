# INTERFACE.md — how this engine is built and started

## 1. The programs

The engine implements three real command-line tools, at pinned versions.
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
| `/app/bin/hb-vector.js` | **`hb-vector`** from **HarfBuzz 14.4.0**, which draws shaped glyph outlines as SVG. `/data/spec/hb-vector-help.txt` is that build's own `--help` output. |
| `/app/bin/hb-shape.js` | **`hb-shape`** from **HarfBuzz 14.4.0**. Its option set, defaults, exit codes and output syntax are its own; `/data/spec/hb-shape-help.txt` is that build's own `--help` output, and the `hb-shape(1)` man page and <https://harfbuzz.github.io/utilities.html> (local copy: `/opt/docs/harfbuzz/utilities.html`) document it. |

`hb-shape`'s default output syntax, from its own help text:

    [<glyph name or index>=<cluster>@<x offset>,<y offset>+<x advance>,<y advance>|...]

and `--batch` (defined by `/data/spec/batch.hh`, the tool's own source for it),
which reads one test per line from standard input, each line being
the tool's arguments separated by `;`, and prints one output line per input line.
`hb-shape` must support `--batch`.

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

A single tool invocation must finish within 60 seconds; the whole `--batch`
shaping run must finish within 900 seconds.

## 4. What must match

* **Variable outlines.** `hb-vector -O svg --precision=2` over variable fonts, at the default
  instance and at chosen axis coordinates, must produce the reference's SVG character for
  character, at axis coordinates other than those of the cases in `/data` as well.
* **Shaping with variations.** HarfBuzz's own shaping tests that pass `--variations`, and
  other cases of the same kind, replayed through `hb-shape --batch`, must match exactly.
* **Earlier shaping.** The shaping the engine already had to get right — the layout
  mechanisms, Latin/Cyrillic/Greek/Hebrew/Arabic, and the complex scripts — must still match
  exactly.
* **Rasterization.** The fonts `ftlint` already had to render must still render
  byte-identically to FreeType: every glyph line reproduced exactly.
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

The cases in `/data` are a representative sample of these suites and kinds of fonts.

## 5. Checking your progress

    bash /data/tests-public/run-public-tests.sh shape            # shaping tests
    bash /data/tests-public/run-public-tests.sh ftlint           # rasterization
    bash /data/tests-public/run-public-tests.sh vector           # outlines as SVG
