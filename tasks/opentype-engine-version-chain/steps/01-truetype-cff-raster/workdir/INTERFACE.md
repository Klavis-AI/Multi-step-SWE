# INTERFACE.md — how this engine is built and started

## 1. The program

The engine implements **`ftlint`**, the font tester shipped with
**freetype-demos** and built against **FreeType 2.14.3**. Its complete source is
at `/data/spec/ftlint.c`; it is the definition of the output format and of every
column, and it is the only description of the interface you need.

Your implementation of it must live at

    /app/bin/ftlint.js

and is started as

    node --permission --allow-fs-read=* --allow-fs-write=* --no-addons --jitless --freeze-flags-after-init --require <guard> /app/bin/ftlint.js <the tool's own arguments>

with `LC_ALL=C`, an arbitrary working directory, and font paths given as
ordinary absolute paths on the command line. It runs as an unprivileged user,
and its standard input and output are pipes it inherits: use them through descriptors 0 and 1 (`process.stdin`, `process.stdout`,
`fs.readSync(0, …)`, `fs.writeSync(1, …)`), not by reopening `/dev/stdin` or
`/dev/stdout`.
Its standard output must match, line by line,
the output of the real `ftlint` for the same arguments.

`ftlint`'s own usage text:

    Usage: ftlint [options] ppem fontname [fontname2..]

      -f L    Use hex number L as load flags (see `FT_LOAD_XXX')
      -r N    Set render mode to N
      -i I-J  Range of glyph indices to use (default: all)
      -q      Quiet mode without the rendering analysis

## 2. What the reference is

Expected output is produced by FreeType **2.14.3** (`VER-2-14-3`) with

    FREETYPE_PROPERTIES=truetype:interpreter-version=35

that is, FreeType's **classic TrueType interpreter**, which implements the
instruction set as published in the OpenType specification (`/data/spec/
tt_instructing_glyphs.txt`, `/data/spec/tt_graphics_state.txt`).

Only these load flags and this render mode need to be supported:

| argument | meaning |
|---|---|
| `-f 2`    | `FT_LOAD_NO_HINTING` — scale the outline, do not grid-fit it |
| `-f 8000` | `FT_LOAD_NO_AUTOHINT` — run the font's own TrueType bytecode when it has any, and never an autohinter |
| `-r 0`    | `FT_RENDER_MODE_NORMAL` — 8-bit antialiased coverage |

and `ppem` values 8, 11, 16, 24 and 40, with `-i` ranges bounded by the font's
glyph count.

## 3. Environment rules

The engine must be self-contained JavaScript under `/app`. It must run from a copy
of the workspace with `node_modules/` removed, and under
a guard that makes the following unavailable: `WebAssembly`,
`child_process`, `worker_threads`, `cluster`, every network module, `vm`, `v8`, `repl`, `inspector`, `perf_hooks`,
native addons (`.node`), `.wasm` files, and any module specifier that resolves
outside the workspace. Node's own `fs`, `path`, `buffer` and similar remain
available. Load the engine's own modules by relative path (`require('./…')`): the copy
is not at `/app`, so an absolute `/app/…` specifier resolves outside it.

The process runs with JIT compilation disabled (`node --jitless`), and the
time limit below applies to it as run that way.

Each invocation must finish within **60 seconds**.

## 4. What must match

`ftlint`'s output must match FreeType's for:

* `-f 2` over TrueType (`glyf`) fonts, including composite glyphs;
* `-f 2` over CFF (Type 2 charstring) fonts;
* `-f 8000` over fonts carrying `fpgm`/`prep`/glyph bytecode;
* the non-glyph lines: the `family style, N ppem, FLAGS, mode` line and the `OK.` / `N fails.`
  trailer (the file line merely echoes the argument, and the column header and its rule are
  constant text, so none of those three needs to match);
* malformed and fuzzed fonts: `ftlint` must never crash (a run that prints `Error:` on stderr
  is a crash), never exceed the time limit, and must still reach the same load /
  do-not-load decision FreeType reaches, reproducing FreeType's per-glyph output exactly where
  FreeType loads the font. For every font the non-glyph lines `ftlint` prints must match
  FreeType's exactly as well: where the face opens, the `family style, N ppem, FLAGS, mode`
  line and the trailer; where FreeType cannot open it, the `opening error = 0x%04x, <message>`
  line that `ftlint.c`'s `Error()` prints. The codes and messages in that line and in
  per-glyph `loading error` / `rendering error` lines are FreeType 2.14.3's own error table,
  `/opt/docs/freetype/fterrdef.h`, which `ftlint.c` includes through `fterrors.h` (also in
  `/opt/docs/freetype/`).

Each glyph line must be reproduced exactly, character for character, including the `shape`,
`X+Yturn`, `WxH`, `Xacut`, `Yacut` and MD5 columns.

The fonts in `/data/fonts` are a representative sample of these kinds of fonts, not the only
ones the engine must handle.

## 5. Checking your progress

    bash /data/tests-public/run-public-tests.sh            # all strata
    bash /data/tests-public/run-public-tests.sh cff        # one stratum

runs your engine over the 30 fonts in `/data/fonts` and prints, per stratum, how
many glyph lines match FreeType's recorded output. `/data/tests-public/expected.json`
holds those recorded outputs; `/data/tests-public/fonts.json` describes the fonts.
