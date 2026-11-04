# `ldecod` — the decoder interface

This file defines how the decoder is invoked and what it must write. It is the
same at every milestone; nothing is added to it later.

## 1. The program

Build/produce an executable at:

```
/app/bin/ldecod
```

This is the JM reference decoder's own name and location (`JM/bin/ldecod`). JM
is the reference software for ITU-T H.264, standardised as **ITU-T H.264.2**.

## 2. Command line

The decoder is always invoked exactly like the JM reference decoder:

```
bin/ldecod -i <input.264> -o <output.yuv> -s
```

| flag | meaning |
| --- | --- |
| `-i <file>` | input Annex B byte stream (ITU-T H.264 Annex B) |
| `-o <file>` | output file for the decoded pictures |
| `-s` | silent; suppress per-picture progress output |

These are JM's own flags, parsed by `ldecod`'s `ParseCommand`. No other flag is
required, and no flag selects decoding behaviour: **what to decode is carried by
the bitstream itself** — `profile_idc` in the sequence parameter set says which
tools are in use, exactly as ITU-T H.264 specifies.

Each bitstream must be decoded within the per-bitstream time limit this milestone's ticket states. A bitstream that runs longer is stopped, and the pictures it had not yet written are missing from its output.

## 3. Output format

`-o` receives the decoded pictures **in output order** (ITU-T H.264 clause 8.2.1
picture order count, and Annex C's bumping process), written as planar samples
with no header, no padding and no separators — the format of the reference YUV
files shipped with the ITU-T H.264.1 conformance bitstreams.

For each output picture, in this order:

1. the luma plane, `PicWidthInSamplesL × FrameHeightInSamplesL` samples;
2. the Cb plane, then the Cr plane, each
   `PicWidthInSamplesC × FrameHeightInSamplesC` samples.

Rules:

- **Cropping.** The frame cropping rectangle from the active SPS
  (`frame_crop_left_offset` … `frame_crop_bottom_offset`, scaled by `CropUnitX`
  and `CropUnitY` per clause 7.4.2.1.1) is applied: only the cropped rectangle
  is written.
- **Chroma.** Plane sizes follow `chroma_format_idc` — 4:2:0, 4:2:2 or 4:4:4.
  A monochrome stream (`chroma_format_idc` 0) is written as 4:2:0 with both
  chroma planes filled with `1 << (BitDepthC − 1)`, which is what `ldecod` does
  under its default `WriteUV = 1`.
- **Sample size.** One byte per sample when the bit depth is 8; otherwise two
  bytes per sample, little-endian, with the sample in the low bits.
- **Fields.** A complementary field pair is written as the single interleaved
  frame it forms, not as two separate pictures.

## 3a. How output is compared

Decoded pictures are compared **frame by frame** against the reference output.

- **The number of output frames must be right**: extra or missing frames are
  errors even when the frames you do produce match.
- **For some bitstreams only part of the output is compared.** Where a stream
  contains pictures whose correct samples this task's references do not
  determine, only the remaining pictures carry digests. The number of pictures
  you produce must still equal the number the stream should produce; the content
  of the pictures without digests is not compared.
- **A bitstream may have more than one output file.** Where it does, the frame
  count above is the total across that bitstream's outputs rather than per file.
  **Placement still matters**: every frame is compared by MD5 against the
  expectation for the file it belongs in, at that file's own frame position, so
  pictures written into the wrong output do not match. Producing the right TOTAL
  does not make up for them: a bitstream with two outputs whose pictures are all
  written into one of them matches only the frames that happen to land at the
  right position in the right file.
- Reference output is the decoded YUV shipped with the ITU-T H.264.1
  conformance bitstreams. For bitstreams encoded specifically for this task,
  the reference is the output of the JM reference decoder (ITU-T H.264.2).

## 4. Implementation constraints

- The decoder is **JavaScript executed by Node.js**. `/app/bin/ldecod` is an
  executable text file run directly by Node.
- **Text files in `/app` are limited to 64 MiB each.** A file larger than that
  which is not recognisably binary (90% or more printable bytes in its first
  8 KiB) is refused, because that is the shape a smuggled base64 or
  text payload takes rather than decoder source. Prose — `.md`, `.txt`, `.rst`,
  `.log` — is exempt: an oversized prose file is skipped, not refused, so a long
  log or handoff note is never a problem. Decoded video belongs in `.yuv`,
  which is binary and unaffected.
- **No compiled artefacts.** No file anywhere in the workspace may carry the
  signature of an executable or module image — `ELF` (`7f 45 4c 46`),
  Mach-O (`fe ed fa ce` / `cf fa ed fe`) or WebAssembly (`00 61 73 6d`).
  **The three differ in where a match is refused.** ELF and Mach-O are refused
  at the START of a file — there the file simply is an executable
  image, whatever it is named. The one exception is an ELF core dump (ELF type
  `ET_CORE`): a process that crashes — `node` aborting when its heap runs out,
  for example — can leave a `core` file in its working directory, and that may
  be `/app`. A core cannot be executed, so it is not refused; it can run to
  gigabytes, though, so delete it rather than leave it in `/app`.
  A match for those two deeper inside a file is noted but not refused: a decoder's
  own `.yuv` spans every byte value, so a four-byte coincidence is likelier than
  a smuggled artefact. Raw WebAssembly bytes are refused at any offset within the first
  8 KiB; the encoded forms below are refused anywhere in the file. **The difference is not the
  coincidence rate** -- a four-byte wasm header is about as likely to fall inside
  a `.yuv` as an ELF one. It is what a match would mean: an ELF or Mach-O image
  cannot be executed here at all, so an interior hit is worth noting and
  nothing more, while WebAssembly is the one artefact form this environment could
  actually run, and the capability layer refuses it a second time, when the call
  is made, for the same reason. The cost of a false positive is the same; the cost of a miss is
  not. A WebAssembly
  module is refused in three further forms anywhere in the text of a non-prose
  file: as base64 (`AGFzbQ`), as a decimal byte array (`0, 97, 115, 109`), and as
  a hex byte array (`0x00, 0x61, 0x73, 0x6d`).
- **Some names are refused as BARE WORDS anywhere in a non-prose file under
  `/app`, whatever they mean in your code.** The scan is lexical, not semantic:
  `fetch`, `WebSocket`, `EventSource`, `child_process`, `createRequire`,
  `importScripts`, `process.dlopen`, `process.binding`, `eval(` and
  `new Function` are each enough on their own. The single permitted form is
  `process.binding("constants")`; every other argument, and the bare name, are
  refused. **This bites honest code.** A bit-reader method of your own
  named `fetch()`, or a helper called `createRequire`, is refused even
  though nothing forbidden is being done — so choose other names. Files ending
  `.md`, `.txt`, `.rst` and `.log` are exempt, which is why the interface
  document can name these words in order to forbid them.
- **The bare word `WebAssembly` is itself refused.** The identifier appearing
  anywhere in a non-prose workspace file is refused on its own, before
  anything runs -- so do not write a feature test like `typeof WebAssembly`,
  even in a branch you never take. Files ending `.md`, `.txt`, `.rst` and
  `.log` are exempt, so prose may name the rule in order to state it.
- **The prose exemption covers the import FORMS too.** `require('net')`,
  `from 'dns'` and `import('tls')` written inside a `.md`, `.txt`, `.rst` or
  `.log` file are not scanned either, so the handoff note this ticket asks you
  to write may quote a forbidden form in order to describe it. In any other
  file, those forms are refused. The same holds for the base64 and
  byte-array WebAssembly payload tests, so you may quote or even copy this
  document freely. Only the binary-magic test — which reads the first
  bytes of a file — applies to prose as well as to code.
- **A prose file cannot be loaded as a module.** `require('./notes.md')` is
  refused, as are `.txt`, `.rst` and `.log`. Node would otherwise run any
  unrecognised extension through its JavaScript handler, so a `.md` whose bytes
  happen to be valid JavaScript would execute. Keep code in `.js`, `.mjs` or
  `.cjs`, and read data files with `fs` — which is unaffected.
- **`WebAssembly` must not be used.** This is enforced when the call is made,
  not only by scanning: `WebAssembly.compile`, `.instantiate`,
  `.compileStreaming`, `.instantiateStreaming`, `.validate` and the `Module`, `Instance`,
  `Memory`, `Table` and `Global` constructors **always throw**, with no
  exception for any caller, however the module bytes were spelled and however
  the call is reached -- directly, through a computed name, from a callback Node
  itself invokes, or through a prototype's `constructor`. The global object
  itself is still present, so
  feature-detecting it tells you nothing; only the calls are refused. `vm`,
  `repl` and `worker_threads` are refused partly for this reason — a fresh
  realm would otherwise carry its own untouched `WebAssembly`.
- **Whole-run budget.** Decoding this milestone's bitstreams stops after a
  fixed wall-clock budget for the run as a whole — at least 2400 seconds.
  Bitstreams not reached by then are not decoded. It is much larger than the
  per-bitstream limit above, but it applies to the whole run, so the two interact
  and **the per-bitstream limit is a cap on one pathological stream, not an
  allowance every bitstream may spend**.

  **Work out the figure that actually constrains you.** A milestone's run decodes
  many bitstreams inside a single whole-run budget, so the per-bitstream cap is a
  cap on one pathological stream and not an allowance every bitstream may spend. Where the
  whole-run budget binds tighter than the cap, **your ticket states the mean
  seconds per bitstream you must hold to**, beside the cap; where it does not,
  the cap is the only figure that binds and the ticket states that alone. Design
  against whichever figure your ticket names.
- **`bin/ldecod` is a Node script**: a text file whose first line is a `#!`
  naming `node`, for example `#!/usr/bin/env node`. That is the only accepted
  shape. A shell script is rejected — including one whose only statement is
  `exec node ...`. Put your own logic in JavaScript that this script imports,
  not in a wrapper around it.
  **The interpreter takes no arguments.** `#!/usr/bin/env node` is accepted and
  `env` may be given exactly `node`; a direct path must be one of
  `/usr/local/bin/node`, `/usr/bin/node`, `/bin/node` with nothing after it. So
  `#!/usr/bin/node --max-old-space-size=8192` and `#!/usr/bin/env -S node --flag`
  are both refused, and a refused entry point means nothing is decoded at all. If
  you need more heap than the default, there is no supported way to ask for it.
  The shebang may carry no flags. `NODE_OPTIONS` is consumed at process start and
  is overwritten when the decoder is started. And `v8` is not on the
  permitted-module list below, so `require('v8').setFlagsFromString` is refused
  by the capability layer **when the decode runs** -- that decode fails; it is
  not a workspace violation, so the other bitstreams are still decoded. The
  default heap is ample for these streams.

  **It must not change once the run starts.** The file is hashed before the
  first decode and re-hashed before every decode; if the bytes at that path
  differ, the decode being started is not run. Write it before the run begins and
  leave it alone — do not regenerate, rewrite or re-chmod it between decodes.
- **The decoder decodes the bitstream it is given.** It starts no subprocess and
  opens no socket of its own — and that is enforced at the capability, not only
  by refusing modules. `Socket.prototype.connect` is removed from the process
  before your code runs, so opening a connection throws however you obtained the
  constructor: `process.stdout.constructor` is a `Socket` (the decoder is
  started with pipe stdio), and `tty`'s prototype chain reaches the same object. Writing to
  `stdout` and `stderr` is unaffected — pipes arrive already connected.
  A few members of PERMITTED modules are removed for the same reason, because
  what matters is what a call reaches rather than which module it came from:
  `crypto.setEngine` (it loads a native OpenSSL engine — the same thing
  `process.dlopen` does), `crypto.setFips`, and `os.setPriority` and
  `os.getPriority` (both take a process id and so reach outside your own
  process). Everything else in those modules is untouched: `crypto.createHash`,
  `crypto.randomBytes`, `os.cpus`, `os.tmpdir` and the rest work normally, as
  does the whole of `fs` — your decoder writes its own output.

  A `#`-prefixed specifier resolved through `package.json` `"imports"` works on
  both module paths, and is judged by what it resolves to: `#lib/bitreader`
  pointing at your own file resolves, while a mapping that lands on a built-in
  outside the permitted set is refused exactly as the built-in's own name
  would be.

  The same holds for `process` itself, and again it is the capability that goes
  rather than the name. These methods are replaced before your code runs and
  throw if called: `execve`, `_debugProcess`, `kill`, `_kill`, `setuid`,
  `setgid`, `seteuid`, `setegid`, `initgroups`, `setgroups`, `dlopen` and
  `_linkedBinding` — each either reaches outside your own process or restores
  something this layer removed. `process.execve` is the sharpest of them: it
  would replace your process image with a fresh `node` carrying none of this
  layer, while keeping your pid, uid, working directory and open files. What is
  deliberately left alone is everything that only ends your own process and
  hands you nothing — `process.exit`, `process.abort` and `process.reallyExit`
  — and `process.exit()` is in fact implemented on top of `reallyExit`.
  Reaching `process` through a module (`node:process`,
  `getBuiltinModule("process")`, `require("node:process")`) returns the very
  same object, so the refusals are the same by whichever route you arrive.
  The CommonJS machinery is immutable while your decoder runs. `Module` and
  `Module.prototype` — reachable from any CommonJS file as `module.constructor`
  — have **every own data property** redefined non-writable and non-configurable
  before your code runs: the functions, the tables they read (`_cache`,
  `_extensions`, `_pathCache`), and `prototype` itself. Accessor properties are
  left alone. That includes
  `_load`, `_resolveFilename` and `_findPath`, so the resolver's functions
  cannot be replaced.

  Locking them is not by itself enough, because the tables those functions read
  are data and data can be written. So `Module._resolveFilename` is itself
  replaced, before your code runs, by a version that checks the filename it is
  about to return and refuses one that names a built-in outside the permitted
  set. `require` consults that function exactly once per load and uses what it
  returns, so the value that is checked is the value that is used. The ESM
  resolver is guarded the same way, on the URL it resolves to. The permitted set
  therefore applies to what a specifier RESOLVES to, not only to what you typed,
  on BOTH module paths: if a `require` or an `import` ends up naming a built-in
  module that is not on the list, it is refused at that point — whether it got
  there through a relative path, a `package.json` `"imports"` mapping, or
  anything else the resolver consults. This costs an ordinary decoder
  nothing — a relative path resolves to a file, and a file is never a built-in
  name — and it means the tables the resolver consults cannot decide the answer
  on your behalf. Nothing
  here is disabled: `require` resolves normally, `_cache` still fills on every
  load, and an ordinary decoder notices none of it. What it prevents is
  replacing the resolution path itself.
  While your decoder runs, these are also removed from its process and throw if
  used: the global `fetch`, `WebSocket` and
  `EventSource`, and **every built-in module except the ones a decoder needs.**
  These resolve and nothing else does:

  > `assert` `buffer` `console` `crypto` `events` `fs` `os` `path` `process`
  > `punycode` `querystring` `stream` `string_decoder` `timers` `tty` `url`
  > `util` `zlib`

  The list is matched on the HEAD of a bare specifier, builtin or not, so
  `require("h264/cavlc")` is refused. Reach your own code by relative path
  (`./cavlc.js`, `../lib/bitreader.js`); that always resolves. A `#`-prefixed
  `package.json` `"imports"` entry is the exception: it resolves, on both module
  paths, and is judged by what it points at — see the paragraph above.
  A subpath follows its parent, so `fs/promises` resolves and
  `test/reporters` does not. Everything outside that list is refused however you
  reach it — `import`, `require`, a computed specifier, an alias, or
  `process.getBuiltinModule` — including names beginning with an underscore.
  So `net`, `http`, `http2`, `https`, `dns`, `tls`, `dgram`, `cluster`,
  `undici`, `child_process`, `worker_threads`, `vm`, `repl`, `test`, `wasi` and
  `module` are all refused, as is anything else not listed above. This is an
  allowlist because a list of forbidden names is only ever as complete as its
  author's memory. `process.dlopen` is refused outright and `process.binding`
  is refused for every argument except the exact call
  `process.binding("constants")`, stated in full above -- for the
  same reason
  -- each is a way to reach the same capability without naming it. `module` in
  particular hands back `register()`, which can install a resolve hook that runs
  before this layer's own and rewrites a harmless specifier into a network one;
  `wasi` is a second door to the host. Specifiers are also judged by
  SCHEME: only `node:` and `file:` resolve, so a `data:`, `http:` or `https:`
  import is refused however it is spelled, and the match is case-insensitive.
  That closes `import("data:text/javascript," + src)`, which assembles its
  source at runtime where no workspace scan can see it. Your decode also runs with
  `--disallow-code-generation-from-strings`, so `eval` and `new Function` throw,
  and it runs as an unprivileged user (uid 65533), so it cannot modify the
  interpreter, or anything else the run relies on. It CAN
  still write to world-writable scratch such as `/tmp` and its own home. Nothing
  written there is read back apart from the decoder's own output, and each
  bitstream decodes in its own scratch directory under `/tmp`, which IS removed
  after that bitstream — but `/tmp` itself is not cleaned between bitstreams, so
  do not rely on it being empty. The decoder's working directory is that scratch
  directory for the bitstream being decoded; it is world-writable and owned by
  the decode user. The decoder is given no flags beyond
  `-i <bitstream> -o <output> -s`. Your decoder cannot use the network while it
  runs.
- **Nothing that loads or fabricates code outside your own source.** Also
  refused anywhere in the workspace: `eval(`, `new Function`, `importScripts`,
  `createRequire`, `process.dlopen`, and `process.binding` other than the exact
  call `process.binding("constants")`.
- **Every file in `/app` is examined**, whatever it is called and wherever it
  sits — including dotfiles and directories such as `.git`, and files whose
  extension suggests they are data. A payload hidden as `.yuv` and `eval`ed, or
  a helper tucked inside `.git/`, is found the same way a `.mjs` would be.
- **That scan covers files you did not write, and `git init` is the case that
  bites.** This image installs `git`. Initialising a repository under `/app`
  writes thirteen sample hooks into `.git/hooks/`, and one of them contains the
  word `fetch` — a form this ticket forbids in your own source. Git's own
  template hooks are therefore exempt *by content*: byte for byte, exactly as
  git ships them, **wherever in `/app` the repository sits**. Edit one, or add a
  file of your own beside them, and it is scanned like anything else you wrote.
  Files git writes for its own bookkeeping are likewise exempt from the
  identifier and module-form scans, because their contents are git's own or your
  commit messages: `git clone` writes a `fetch =` refspec into `config`, a
  message reading "rename fetch() to nextBits()" is an honest sentence, and a
  branch named `fetch-nals` puts that same word into `HEAD`. A **git directory**
  is recognised by what it contains — a `HEAD` naming a ref or an object, beside
  a `config`, a `commondir` or a `gitdir` — so this applies equally to `.git/`,
  to a bare repository, to `--separate-git-dir`, to a submodule's gitdir however
  deeply its path nests, and to a linked worktree's, which carries `commondir`
  and `gitdir` rather than a `config` of its own. A worktree's `.git` file, which holds
  `gitdir: <path>`, is exempt by that content.
  That pointer, and the `gitdir` and `commondir` files a linked worktree keeps
  beside its `HEAD`, must be a single line. Its content may hold any character a
  directory name may hold — `;` `(` `)` `"` `'` `` ` `` `{` `}` `=` included — so
  `git worktree add '../fetch (backup)'` and `.../wt;fetch` are exempt like any
  other pointer. What that exemption covers is the **bare-identifier** scan only:
  a worktree path carries a branch or directory name, so a word such as `fetch`
  in it is honest. The **import** scan still runs on these files, because a path
  is never a `require(...)`, an `import(...)` or a `from "..."`. A pointer whose
  content is a **module-reference form** is refused and reports `forbidden`, like
  anything else. A pointer whose content is some *other* call — `eval(x)`,
  `WebAssembly.compile(b)` — is **not** refused by this scan, because no
  character class separates a path from a statement without also refusing paths
  git writes. What bounds such a file is the runtime, not this scan: it loads
  under the same allowlist as your own code, so `child_process`, `net` and `vm`
  are refused and `process.binding` throws, exactly as they would anywhere in
  `/app`.
  Inside such a directory, exempt **by name**: `config`, `description`,
  `COMMIT_EDITMSG`, `MERGE_MSG`, `SQUASH_MSG`, `TAG_EDITMSG`, `BISECT_LOG`, and
  the rebase and sequencer message files. Exempt **by content shape**: the
  reflog under `logs/`, whose every line opens with two object names; and the
  ref files — `HEAD`, `packed-refs`, the `*_HEAD` family and everything under
  `refs/` — whose every line is a symbolic ref naming a ref path, an object name
  standing alone or followed by a ref path, a `FETCH_HEAD` record (two
  tab-separated fields, the first empty or `not-for-merge`), a peel line, or a
  line beginning `# `. A ref path here is whatever `git check-ref-format`
  accepts — anything but a space, `~`, `^`, `:`, `?`, `*`, `[`, a backslash or a
  control character — so a branch named `fetch#1` or `naïve-fix` is recognised
  and costs you nothing.
  Those shapes are narrow on purpose, and a file at one of those paths whose
  contents do not match them is scanned like anything else you wrote. Everything
  else in a git directory is scanned normally, and the executable-signature
  checks above apply to every file there without exception.
- **The practice runner is recorded by content — read it and run it, but leave
  it where it is.** `tests/visible/run_visible.mjs` imports `node:child_process`
  to start your decoder, which is allowed for it and not for you. It is exempt
  from the source scan *only at its own path and with its shipped content*, so a
  modified copy, or a verbatim copy at a different path, is scanned like any
  other file you wrote. Editing it or copying it elsewhere is therefore refused.
  If you want a variant run, pass it a name filter:
  `node tests/visible/run_visible.mjs <name>`.

- **No third-party packages.** Ship JavaScript you wrote. Nothing is installed
  when the decoder runs, and the capability layer removes the means of reaching a
  registry at that point, so a dependency added at decode time cannot be fetched
  even if it were permitted.

  **If you run `npm install` while exploring, delete `node_modules/` before you
  finish.** It is scanned like the rest of your workspace, and almost any package
  contains `fetch`, `child_process`, `eval(`, `new Function` or `WebAssembly`,
  any of which is refused. Every rule that produces that outcome is
  stated above; this is the concrete case where they meet, and it is the likeliest
  way for honest work to be rejected.

A workspace that violates any of these is rejected as a whole: none of its
bitstreams is decoded.
