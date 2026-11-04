# Step 3 — Move the implementation to Lua 5.3

## 1. Implement this milestone

This workspace holds a Lua implementation written in Java. Make it implement
**Lua 5.3** instead of the release it implements now.

**Build interface (unchanged).** `make`, run with this directory as the working
directory, builds everything and produces an executable **`./lua`**. `make` must
honour the variable `MYCFLAGS`: whatever it contains is passed to the build as
additional compile-time macro definitions, exactly as `src/Makefile` does in the
reference distribution (`CFLAGS = … $(MYCFLAGS)`). The compile-time configuration
macros are the ones documented for the release being built, in `luaconf.h`.

**Clean build.** The build must also work from a copy of this workspace that
leaves out `./lua` and the top-level `build/`, `gen/` and `.git` directories, and
from which every compiled `.class` file has been deleted: in that copy, `make
clean` followed by `make` must succeed. So nothing the build needs may exist only
in those places: from your sources alone, `make clean && make` must produce
`./lua` and every class it runs.

**Run interface (unchanged).** `./lua` is the standalone interpreter of the Lua
5.3 reference manual, section 7 (`/app/SPEC/lua-5.3/manual.html#7`):

```
lua [options] [script [args]]
```

with the options `-e stat`, `-l name`, `-i`, `-v`, `-E`, `--` and `-`, the `arg`
table, the extra arguments visible as `...` in the main chunk, `LUA_INIT`
handling, and exit status 0 when the script completes and 1 when it does not. The
interpreter's own diagnostics must read as the reference interpreter's. The
version line printed by `-v` and at the top of an interactive session is
exactly `Lua 5.3.6  Copyright (C) 1994-2020 Lua.org, PUC-Rio` (one line, two spaces
before `Copyright`); the interactive prompt is `_PROMPT`, default `> ` (with the
trailing space), and `_PROMPT2`, default `>> `. An unrecognised or incomplete
option is reported on standard error, and **the first line is what must match
the reference**: `lua: unrecognized option '-x'` for an unrecognised one and
`lua: '-e' needs argument` for an option missing its argument, each followed by
the `usage:` block, which need not match. There is also the `lua: <message>`
line for an uncaught error, and the message for a script that cannot be opened. The version
banner (from `-v` and at the top of an interactive session) and the interactive
prompt are written to standard output; the `usage:` text, the `lua:` error line
and the cannot-open message go to standard error. `/app/tests/conformance/`
contains examples with their exact expected output.

**Behaviour.** The language and the standard library are Lua 5.3 exactly as
`/app/SPEC/lua-5.3/manual.html` specifies them. Section 8 of that manual,
"Incompatibilities with the Previous Version", lists what changes relative to Lua
5.2; everything else the manual specifies continues to apply. The release ships
with the compatibility options its `src/Makefile` enables by default
(`LUA_COMPAT_5_2`), so the functions that option keeps are still present. Under it
the default build still has the mathematical functions `math.pow`, `math.atan2`,
`math.cosh`, `math.sinh`, `math.tanh`, `math.frexp`, `math.ldexp` and
`math.log10`, the `bit32` library, and the `__ipairs` metamethod, which `ipairs`
still calls when its argument has one. The names that Lua 5.2 kept only for Lua 5.1
compatibility — `module`, `package.seeall`, `package.loaders`, `loadstring`, the
global `unpack` and `table.maxn` — are gone.

The parts of that delta that the conformance programs exercise most are

* the integer subtype: integer and float values with `math.type`, wrap-around
  64-bit integer arithmetic, `//` and `%` with the manual's floor semantics and
  their errors on a zero integer divisor, `/` and `^` always producing floats,
  exact comparison and equality between the two subtypes, integer-valued results
  from `math.floor`/`math.ceil`/`#`/`os.time`/`string.len` and friends, the
  conversion rules between strings and numbers, and `tostring`, `print` and string
  concatenation printing a float that looks like an integer with a trailing `.0`,
  which `io.write` does not do — it writes `1` for `1.0`;
* the bitwise operators `&`, `|`, `~` (binary and unary), `<<` and `>>` with the
  manual's shift semantics, their metamethods, and the "number has no integer
  representation" error for operands that are not exactly integers;
* numerals: an integer numeral is an integer, a hexadecimal numeral wraps around,
  a decimal numeral that does not fit becomes a float, and `\u{XXX}` escapes;
* the `utf8` library and `string.pack`/`string.unpack`/`string.packsize`;
* `table.move`, `ipairs` respecting metamethods, `coroutine.isyieldable`, and the
  `math` additions `maxinteger`, `mininteger`, `tointeger`, `type` and `ult`;
* the library surface listed above: what the release's own `src/Makefile` keeps
  through `LUA_COMPAT_5_2`, and what is gone;
* the diagnostics that changed: the text of runtime, argument and syntax errors —
  including where a variable kind such as `local 'x'` or `global 'x'` now appears
  in the message — and the shape of `debug.traceback`.

**Diagnostics are part of the behaviour.** Runtime errors, argument errors from
library functions and syntax errors must match the reference implementation's
text, including the `chunkname:line:` prefix and the short-source form. The
conformance programs in `/app/tests/conformance/` show many of them with their
exact expected output.

**Implementation constraints (unchanged).** The interpreter must be *your* Java
code:

* every implementation source file in this workspace is a `.java` file, compiled
  by the JDK that is installed here; no C, C++ or assembly sources, no prebuilt
  object files, shared libraries, executables, `.jar` files or any other
  archive or compiled binary, and no third-party library;
* `make` must build from those sources alone: it may run the JDK and nothing
  else, and it must not fetch anything — no `curl`, `wget`, URL, package manager,
  version-control checkout, or other compiler or interpreter appears in the
  makefile or in any script the build runs;
* the compiled classes must not declare native methods and must not reference
  `java.lang.Runtime`, `java.lang.Process`, `java.lang.ProcessBuilder`,
  `java.lang.foreign`, `sun.misc.Unsafe`, `jdk.internal.*`, `java.lang.reflect`,
  `java.lang.invoke.MethodHandles`, `java.lang.ClassLoader`, `Class.forName`,
  `System.load`/`System.loadLibrary`, `java.net`, `java.nio.channels`,
  `java.lang.instrument` or `javax.script`;
* `./lua` may be a launcher script, in which case it is at most 4096 bytes,
  contains the substring `java`, and does nothing but start the JVM on the
  classes built here
  -- it may not run another language's interpreter or a Lua binary from
  elsewhere on the system, and its text does not name one either, not even as
  part of a class, package or path name (so a main class called `lua51.Main`
  counts as naming a Lua binary) -- and with those classes removed, running `./lua` has
  to fail;
* `make` honours `MYCFLAGS` even when it names a macro this release does not use:
  `make clean && make MYCFLAGS=-DSOMETHING` still builds a working `./lua`.

The build must succeed, must produce `./lua`, and must satisfy every one of these
constraints. On top of that, the interpreter must conform to the reference
implementation: for Lua programs run through `./lua`, the standard output, exit
status and first line of standard error must match the reference's. That holds
both for programs taken from the release's own test suite and for programs
written for this task. Both this version's own newly changed behaviour and
everything else must work: the test suite and the earlier-version behaviour this
release still has to support. Behaviour carried over from earlier versions is not
enough on its own; this version's own changes must be implemented.

**Programs that must fail.** Some conformance programs are ones a correct
interpreter has to *reject*. Every one of these programs must exit with a non-zero
status, and its error message must match the reference like any other
conformance program's output. An interpreter that cannot produce a failure —
one whose `assert` returns instead of raising, say — does not meet this
requirement.

**Resource ceilings.** Every conformance program runs under
a limit of **15 seconds of wall-clock time** and **1 GiB of resident memory**,
with the whole process group killed when either is exceeded; a program that hits
a limit has failed. None of the programs needs anything near either figure on
the reference implementation, and the local runner in
`tests/run-conformance.sh` applies the same two limits. The example programs it
runs are a small representative sample, not an exhaustive one. `make` itself has
900 seconds, and the conformance run as a whole has a time budget of its own: a
program there is no time left to start has failed too.

Every conformance program runs in a private working directory of its own, with
the workspace first on `PATH`, so the interpreter is invoked as `lua` and its
diagnostics name it that way; `tests/run-conformance.sh` does the same.

`os.execute` and `io.popen` cannot start a process in this environment; their
*Lua-visible behaviour* is not prescribed. However, the implementation constraint
above bans `java.lang.Runtime`, `java.lang.Process`, and `java.lang.ProcessBuilder`,
so an implementation of `os.execute` must not route through those APIs. The
simplest correct approach is to have
`os.execute` return `nil, "exit", 1` unconditionally, or to raise a
`"cannot execute"` error. Loading a precompiled (binary) chunk is not required, and
the *output* of `string.dump` is not prescribed. `string.dump` itself is required: it must exist,
and it must fail on a function this release cannot dump, raising the error
`unable to dump given function`, as this release's reference does.

The special floats print exactly as the reference does: `1/0` is `inf`, `-1/0` is
`-inf`, `tostring(0/0)` is `-nan` and `tostring(-(0/0))` is `nan`.


## 2. Read these local references

**Normative semantics:** `/app/SPEC/lua-5.3/manual.html` — the Lua 5.3 reference
manual, with `/app/SPEC/lua-5.3/contents.html` as its index and section 8 as the
delta from the previous release. The manual of the release this workspace
implements today is still at `/app/SPEC/lua-5.2/manual.html`.

**Additional files:** `/app/WORKSPACE_HANDOFF.md` describes the workspace and what
to leave behind, and whatever notes the previous session left. The official Lua
5.3 test suite, as published at `https://www.lua.org/tests/`, is in this
workspace at `/app/tests/lua-5.3.4-tests/` — lua.org publishes no suite for the
exact patch release, and instructs using the one for the closest release of the
same version; its individual files are there to read and to run through `./lua`
as ordinary programs. (The suite's own `all.lua` driver needs facilities outside
this milestone's scope — an internal C test library, binary chunks — so it is not
the way to exercise your build; use `tests/run-conformance.sh`.)
`/app/tests/conformance/` holds conformance programs together with the exact
output the reference implementation produces for them, and
`/app/tests/run-conformance.sh` runs them all and reports how many match.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and preserve the supplied documents and
  handoff files. Leave the build working and the workspace self-contained,
  exactly as you would want to find it.
- Do not search for, download, copy, wrap, or invoke an
  online implementation or task-specific answer. Work only from this workspace,
  its documentation and the local references above; do not search the system for
  other implementations or answers.
- Run the local checks named above and leave a reproducible handoff for the next
  fresh session.
