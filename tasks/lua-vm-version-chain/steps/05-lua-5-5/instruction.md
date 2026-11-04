# Step 5 — Move the implementation to Lua 5.5, with its compatibility matrix

## 1. Implement this milestone

This workspace holds a Lua implementation written in Java. Make it implement
**Lua 5.5** instead of the release it implements now.

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
5.5 reference manual, section 7 (`/app/SPEC/lua-5.5/manual.html#7`):

```
lua [options] [script [args]]
```

with the options `-e stat`, `-l name`, `-i`, `-v`, `-E`, `-W`, `--` and `-`, the `arg`
table, the extra arguments visible as `...` in the main chunk, `LUA_INIT`
handling, and exit status 0 when the script completes and 1 when it does not. The
interpreter's own diagnostics must read as the reference interpreter's. The
version line printed by `-v` and at the top of an interactive session is
exactly `Lua 5.5.1  Copyright (C) 1994-2026 Lua.org, PUC-Rio` (one line, two spaces
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

**Behaviour.** The language and the standard library are Lua 5.5 exactly as
`/app/SPEC/lua-5.5/manual.html` specifies them. Section 8 of that manual,
"Incompatibilities with the Previous Version", lists what changes relative to Lua
5.4; everything else the manual specifies continues to apply. The release's own
`src/Makefile` adds no compatibility option, so what section 8 removes is gone from
the default build, with one exception: `luaconf.h` itself turns
`LUA_COMPAT_GLOBAL` on by default (see the compatibility matrix below). In the
default build `global` therefore still works as an ordinary name (`local global = 1`
and `global = 5` are accepted) as well as starting the declarations below.

The parts of that delta that the conformance programs exercise most are

* `global` — a reserved word in the `-DLUA_COMPAT_GLOBAL=0` build below — and the
  declarations the grammar of section 9 now has for it in every build —
  `global Name`, `global function`, `global *` and the attribute
  forms — together with the diagnostics for using an undeclared global once a
  chunk declares any: a compile-time `variable 'y' not declared`, reported at the
  line where `y` is used;
* the control variable of a `for` loop being read only;
* the limit on a chain of `__call` metamethods;
* an error raised with no value, and what `pcall` then reports;
* `collectgarbage("param", …)` replacing the parameter forms of `"incremental"`
  and `"generational"`, with the parameter names section 6.2 lists. Without a new
  value it returns the current one, and this release's defaults are a pause of
  `250` and a step multiplier of `200`. Like the reference `lua`, the standalone
  interpreter switches the collector to generational mode when it starts, so a
  script's first `collectgarbage("incremental")` returns `"generational"`;
* the named vararg parameter of section 9's `varargparam`;
* `table.create`, and the default build having no `math.pow`, `math.atan2`,
  `math.cosh`, `math.sinh`, `math.tanh` or `math.log10` (`math.frexp` and
  `math.ldexp` are part of this release's library, section 6.8);
* **how a float prints by default.** Earlier releases format a float as `"%.14g"`;
  this release formats it as `"%.15g"` when that text reads back as the same number,
  and otherwise as `"%.17g"`. So `1e14` prints as `100000000000000.0` where the earlier
  releases print `1e+14`, and `4.31098127518968` prints exactly as written — fifteen
  significant digits, not seventeen — while `1/3` prints as `0.33333333333333331` and
  `100/7` as `14.285714285714286` (neither survives fifteen), and `0.1` still prints as
  `0.1`. `1e15 + 0.5` prints as `1000000000000000.5` rather than `1e+15`. This applies
  to `tostring`, `print`, string concatenation, `table.concat` and `io.write`.
  `string.format`'s explicit numeric conversions are unchanged — `%.14g`, `%g`, `%f`
  and `%e` format exactly as they did, so `string.format("%.14g", 1/3)` is still
  `0.33333333333333` — but `%s` converts through the same path as `tostring` and so
  follows the new rule. **This
  supersedes the `"%.14g"` rule stated for the earlier releases.** A float whose text
  would otherwise look like an integer keeps a trailing `.0` so that it still reads as a
  float — `4.0`, `2.0`, `10.0`, and `1e14` as `100000000000000.0` above. For `tostring`,
  `print`, concatenation and `table.concat` that is unchanged from the release that
  introduced the integer subtype; for `io.write` it is new here — the earlier releases
  write `1` for `1.0`, this one writes `1.0`.
* **the compile-time compatibility matrix.** `luaconf.h` of this release defines
  exactly two compatibility macros, and `make` must honour both of them through
  `MYCFLAGS`:
  * `LUA_COMPAT_GLOBAL` — on by default; building with `-DLUA_COMPAT_GLOBAL=0`
    makes `global` a reserved word again, as the manual's section 8.1 says;
  * `LUA_COMPAT_MATHLIB` — off by default; building with `-DLUA_COMPAT_MATHLIB`
    brings back exactly the six mathematical functions the default build lacks:
    `math.pow`, `math.atan2`, `math.cosh`, `math.sinh`, `math.tanh` and
    `math.log10`. As in Lua 5.3 and 5.4, `math.atan2` is `math.atan` under
    another name, so its second argument is optional.
  Each of three builds — with no `MYCFLAGS`, with
  `MYCFLAGS=-DLUA_COMPAT_MATHLIB`, and with `MYCFLAGS=-DLUA_COMPAT_GLOBAL=0` —
  must run conformance programs as the reference built the same way does, so
  `make clean && make MYCFLAGS=…` must produce a `./lua` that reflects the macros
  it was given;
* the diagnostics that changed, including the text for `break` outside a loop
  (now reported where it occurs, as `break outside loop near 'break'`) and for a
  jump into the scope of a variable.

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
constraints. That applies to both builds: the plain `make`, and the
`make clean && make MYCFLAGS=…` pass-through just above. The three
compatibility-matrix builds must succeed too. On top of that, the interpreter
must conform to the reference implementation: for Lua programs run through
`./lua`, the standard output, exit status and first line of standard error must
match the reference's. That holds both for programs taken from the release's own
test suite and for programs written for this task. Both this
version's own newly changed behaviour and everything else must work: the test
suite and the earlier-version behaviour this release still has to support.
Behaviour carried over from earlier versions is not enough on its own; this
version's own changes must be implemented.

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
and it must fail on a function this release cannot dump (one that is not a Lua
function), raising the error `bad argument #1 to 'string.dump' (Lua function expected)`,
as this release's reference does.

The special floats print exactly as the reference does: `1/0` is `inf`, `-1/0` is
`-inf`, `tostring(0/0)` is `-nan` and `tostring(-(0/0))` is `nan`.


## 2. Read these local references

**Normative semantics:** `/app/SPEC/lua-5.5/manual.html` — the Lua 5.5 reference
manual, with `/app/SPEC/lua-5.5/contents.html` as its index and section 8 as the
delta from the previous release. The manual of the release this workspace
implements today is still at `/app/SPEC/lua-5.4/manual.html`.

**Additional files:** `/app/WORKSPACE_HANDOFF.md` describes the workspace and what
to leave behind, and whatever notes the previous session left. The official Lua
5.5 test suite, as published at `https://www.lua.org/tests/`, is in this
workspace at `/app/tests/lua-5.5.1-tests/`; its individual files are there to
read and to run through `./lua` as ordinary programs. (The suite's own `all.lua`
driver needs facilities outside this milestone's scope — an internal C test
library, binary chunks — so it is not the way to exercise your build; use
`tests/run-conformance.sh`.)
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
