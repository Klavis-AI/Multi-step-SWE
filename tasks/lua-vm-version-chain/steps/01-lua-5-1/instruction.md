# Step 1 — A Lua 5.1 implementation in Java

## 1. Implement this milestone

Write a complete implementation of **Lua 5.1** in **Java**, in this workspace, and
expose it through the interface the Lua distribution itself uses.

**Build interface.** `make`, run with this directory as the working directory,
builds everything and produces an executable **`./lua`**. `make` must honour the
variable `MYCFLAGS`: whatever it contains is passed to the build as additional
compile-time macro definitions, exactly as `src/Makefile` does in the reference
distribution (`CFLAGS = … $(MYCFLAGS)`). The compile-time configuration macros
are the ones documented for the release being built; for Lua 5.1 they are the
`LUA_COMPAT_*` settings of `luaconf.h`, whose defaults this milestone uses.

**Clean build.** The build must also work from a copy of this workspace that
leaves out `./lua` and the top-level `build/`, `gen/` and `.git` directories, and
from which every compiled `.class` file has been deleted: in that copy, `make
clean` followed by `make` must succeed. So nothing the build needs may exist only
in those places: from your sources alone, `make clean && make` must produce
`./lua` and every class it runs.

**Run interface.** `./lua` is the standalone interpreter of the Lua 5.1 reference
manual, section 6, "Lua Stand-alone" (`/app/SPEC/lua-5.1/manual.html#6`):

```
lua [options] [script [args]]
```

with the options `-e stat`, `-l name`, `-i`, `-v`, `--` and `-`, the `arg` table,
the extra arguments visible as `...` in the main chunk, `LUA_INIT` handling, and
exit status 0 when the script completes and 1 when it does not. The interpreter's
own diagnostics must read as the reference interpreter's: the version line printed
by `-v` and at the top of an interactive session is exactly
`Lua 5.1.5  Copyright (C) 1994-2012 Lua.org, PUC-Rio` (one line, two spaces before `Copyright`);
the interactive prompt is `_PROMPT`, default `> ` (with the trailing space) and
`_PROMPT2`, default `>> `. An unrecognised or incomplete option is reported on
standard error, and **the first line is what must match the reference** — in this release
that first line is the `usage:` text itself, `usage: lua [options] [script [args]].`
(with the trailing period), for both an unrecognised option and one missing its
argument. There is also the `lua: <message>` line for an uncaught error, and the
message for a script that cannot be opened. The version banner and all of these
diagnostics go to standard error; the interactive prompt is written to standard
output. `/app/tests/conformance/` contains examples with their exact expected
output.

**Behaviour.** The language and the standard library are Lua 5.1 exactly as
`/app/SPEC/lua-5.1/manual.html` specifies them: the complete syntax of section 8,
the semantics of section 2, and the libraries of section 5 — basic, coroutine,
string (including patterns and `string.format`), table, math, io, os, package and
debug. Section 3 of that manual is the C API, which this implementation does not
have and is not required to have. In particular this milestone includes

* metatables and every metamethod the manual lists, including the Lua 5.1 rule
  that an order or equality metamethod is used only when both operands have the
  same one;
* closures with shared upvalues, varargs with the `arg` table that
  `LUA_COMPAT_VARARG` provides, multiple returns, and proper tail calls;
* coroutines, including the Lua 5.1 restriction on yielding across a metamethod
  or a C-call boundary;
* string patterns, `string.format` with C `printf` conversion semantics, and
  numbers formatted as `"%.14g"`;
* error handling: `error` levels, `pcall`, `xpcall` with the handler called
  before the stack unwinds, and `debug.traceback`;
* garbage collection that a program can observe: the `collectgarbage` options,
  weak tables in all three modes, `__gc` on userdata, and collection that happens
  on its own as a program allocates.

**Diagnostics are part of the behaviour.** Runtime errors, argument errors from
library functions and syntax errors must match the reference implementation's
text, including the `chunkname:line:` prefix and the short-source form the
manual's `debug` section describes. The Lua 5.1 test suite in
`/app/tests/lua5.1-tests/` asserts many of these messages directly (see
`errors.lua`), and the conformance programs in
`/app/tests/conformance/` show more of them with their exact expected output.

**Implementation constraints.** The interpreter must be *your* Java code:

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
written for this task.

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

**Normative semantics:** `/app/SPEC/lua-5.1/manual.html` — the Lua 5.1 reference
manual, with `/app/SPEC/lua-5.1/contents.html` as its index. This document
defines the task; everything required is specified there or is a diagnostic shown
in the material below.

**Additional files:** `/app/WORKSPACE_HANDOFF.md` describes the workspace and what
to leave behind. `/app/tests/lua5.1-tests/` is the official Lua 5.1 test suite as
published by lua.org; its individual files are there to read and to run through
`./lua` as ordinary programs. (The suite's own `all.lua` driver needs facilities
outside this milestone's scope — an internal C test library, binary chunks — so it
is not the way to exercise your build; use `tests/run-conformance.sh`.)
`/app/tests/conformance/` holds conformance programs together with the exact
output the reference implementation produces for them, and
`/app/tests/run-conformance.sh` runs them all and reports how many match.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and preserve the supplied documents,
  suites and handoff files. Leave the build working and the workspace
  self-contained, exactly as you would want to find it.
- Do not search for, download, copy, wrap, or invoke an
  online implementation or task-specific answer. Work only from this workspace,
  its documentation and the local references above; do not search the system for
  other implementations or answers.
- Run the local checks named above and leave a reproducible handoff for the next
  fresh session.
