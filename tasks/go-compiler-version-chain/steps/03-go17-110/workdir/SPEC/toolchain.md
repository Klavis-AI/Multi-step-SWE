# Toolchain interface

The entry point is the Go toolchain's own interface: the `go` command
(`cmd/go`, documented at https://pkg.go.dev/cmd/go; local copy:
`/opt/docs/go/cmd-go/cmd-go.txt`), the `$GOROOT` installation layout, the
`go.mod` file format from the Go Modules Reference (https://go.dev/ref/mod;
local copy: `/opt/docs/go/ref-mod/mod.md`), and the language defined by the
specification next to this file. This document names only where things live,
how the toolchain is built, and exactly which behaviour must match the
reference toolchain.

## 1. Where the toolchain lives

    $GOROOT            /app/goroot, exported by the caller
    the `go` command   /app/goroot/bin/go
    library sources    /app/goroot/src/<import path>/

This is Go's own installation layout: `go` sits in `$GOROOT/bin` and the
standard library lives under `$GOROOT/src`, one directory per import path.
`make` runs in `/app` and must create `/app/goroot/bin/go`. The caller exports
`GOROOT=/app/goroot`; read it from the environment rather than assuming it.

## 2. How it is built

    make            run in /app, default target

The default target must create `$GOROOT/bin/go`. The build runs with a `PATH`
that contains only a C compiler (`cc`, `gcc`), `make`, `ar`, `ld`, `as`,
`ranlib`, and ordinary POSIX shell utilities. Everything under `$GOROOT/bin`
and `$GOROOT/pkg`, every `*.o`/`*.a` file, and every ELF file in the workspace
is deleted before that build, so the toolchain must be reproducible from the
sources in `/app` alone.

## 3. How it is driven

Each case is a fresh directory containing one or more `.go` files, possibly in
subdirectories of the module, and the `go` command runs with that directory as
the working directory. Almost every case also writes a
`go.mod`; a few deliberately do not, to check what the toolchain does outside a
module. The caller sets
`GOROOT=/app/goroot`, `GOPATH` to a scratch directory, `GO111MODULE=on` and
`GOFLAGS=` (empty), and inherits the rest of the container's environment.

The subcommands this milestone uses are listed in
`milestone.md`. The `go.mod` always declares a module path and a `go`
directive; imports of packages inside the module resolve relative to that
module path, as the Modules Reference describes.

## 4. What must match the reference toolchain

* `go run .` — the process exit status, and the program's output, formed by
  concatenating everything captured on standard output and then everything
  captured on standard error (not interleaved chronologically). The built-in
  `print`/`println` and run-time panics go to standard error, so they appear
  after `fmt`'s output even when the program produced them first. Before
  comparison both sides are normalised the same way: everything from the first `goroutine N [...]` line onwards is
  dropped, as are `[signal ...]` lines, lines beginning with `# `, and the
  `exit status N` line that the `go` command prints for a failing program. The
  first `panic: ...` or `fatal error: ...` line is *not* dropped and must match.
* `go build -o a.out .` — whether the build succeeds, and, when the package
  being built is `package main`, that a **non-empty file was written to the
  `-o` path**. Nothing is asserted about that file's format: emit a native
  binary, bytecode for your own loader, or anything else your `go run` can
  execute. A *library* package is exempt, because `go build` writes no
  output for one, which is what the reference does too.
* `go test [-v] [-run PATTERN] ./...` — the process exit status, and the `go`
  command's output, assembled and normalised the same way, plus one extra
  rewrite: every `<digits>.<digits>s` duration becomes `0.00s` on both sides,
  so the timings your toolchain prints are never compared. **Where the tested
  program's own writes end up is not the `go run` rule above, and it
  matters.** `go test` does not pass the test binary's standard error through to
  the `go` command's standard error. It captures both of the test binary's
  streams, merges them **in the order the program wrote them** — not all of
  standard output and then all of standard error — and emits that text on the
  **`go` command's own standard output, ahead of the package's test report**.
  A `println` from a package-level `init`, which `go run` would show *after*
  `fmt`'s output because it goes to standard error, therefore appears here
  *before* the first `=== RUN` line and in its true position relative to that
  package's `fmt` output. Without `-v` the same captured text is buffered, and
  then:
  - **discarded when the package passes** — a passing package without `-v`
    prints its `ok  \t<import path>\t0.00s` line and nothing else, however
    much the test binary wrote to either stream;
  - **printed ahead of the `--- FAIL` block when the package fails.**

  Every case produces this output before its first test starts, so where
  output written *during* a test lands relative to the `=== RUN`/`--- PASS`
  lines is not compared.
* a program that must be rejected — the build must fail, and the set of source
  line numbers that carry a diagnostic must be exactly the set of lines holding
  the illegal constructs. Diagnostics are matched by position only: each one is
  a line on standard error of the form `file.go:LINE: message` or
  `file.go:LINE:COL: message` (the column is optional), and only `LINE` is
  compared. The message text is never compared.

The recorded behaviour every case is compared against was produced by the
**reference gc toolchain** running the same program in the same module
directory with the same `go` directive. That is also what pins
the formats the specification leaves open: the built-in `print`/`println`
output, the text of run-time panics, and the wording of the fatal error a
deadlocked program reports. `/app/conformance/` contains programs that exercise
all three together with their recorded output, and is the authoritative record
of those formats for this workspace.

### What each milestone covers

Two things must hold at once:

- **this milestone's own behaviour** -- the constructs and APIs section 1 of
  the ticket describes, at this milestone's `go` directive;
- **compatibility** -- the language suite at this milestone's `go` directive,
  and everything earlier `go` directives already had to do.

## 5. What is out of scope

The specification next to this file is the whole Go language; no milestone
needs all of it. Nothing below is required at any milestone, and no case in
`/app/conformance/` needs it:

- **complex numbers** — `complex64`, `complex128`, `complex`, `real`, `imag`
  and imaginary literals;
- **cgo**, assembly files, build tags other than the `go` directive's effect,
  linker flags, `-race`, `-msan`, `-cover`, `-fuzz` corpus growth, and cross
  compilation;
- **`reflect`**, `encoding/*`, `net/*`, `os/exec`, `text/*`, `regexp` and every
  package not listed in `/app/SPEC/packages.txt`;
- **goroutine stack traces**: a panic's first line is compared, everything from
  the first `goroutine N [` line on is discarded;
- the **exact text** of compiler diagnostics: only the line a diagnostic is
  reported on is compared.

Everything that must match the reference toolchain is described in section 4.

## 6. Checking your own progress

`/app/conformance/` holds a corpus of programs taken from the Go project's own
`test/` directory plus additional programs, together with the behaviour the
reference toolchain produces for them, in JSON. Run it with

    python3 /app/conformance/selfcheck.py --jobs 8

The corpus is a representative sample, not an exhaustive one: passing all of
it does not by itself mean the milestone is complete.

## 7. Resource limits

These limits apply to the build and to every case; `selfcheck.py` applies the
same per-case ceilings:

- each case runs as its own process with **4 GiB of address space**
  (`RLIMIT_AS`), **300 seconds of CPU** (`RLIMIT_CPU`) and no core dump;
- each case has a **30-second wall clock**, raised to **120 seconds** for the
  cases that drive the collector;
- seven cases run at a time, on a seven-CPU container. Nothing detects the
  CPU count: do not assume more;
- `make` must finish within **20 minutes**, and every case must have run
  within **40 minutes** of the build starting.

A toolchain whose process is killed by one of these limits fails that case, so
keep peak memory per compiled program well inside 4 GiB.

Every case also runs as an **unprivileged user**, not as root: neither
your `go` command nor the programs it produces run with any privilege. What
this means in practice:

- the case's own directory is writable, and `$HOME`, `GOPATH` and `GOCACHE` are
  all set to paths inside it, so a build cache or temporary file has somewhere
  to go;
- **`/app` belongs to that user and stays writable**, during `make` and while
  cases run. `HOME` and `TMPDIR` during the build are directories inside
  `/app` that you own;
- **but `/app` is shared by every case, and seven cases run at once.** Anything
  your toolchain writes into its own tree at *run* time is therefore shared
  mutable state across concurrent processes: make it concurrency-safe, or keep
  it out of the tree. `GOCACHE`, `HOME` and `GOPATH` while cases run are set
  for you to per-case directories, which is the race-free path;
- nothing outside those paths should be assumed readable.

**Nothing you supply ever runs as root.** `make` is dropped to the same
unprivileged user as the cases -- it runs a Makefile you wrote, so it gets no
more privilege than the programs it produces. The only step that runs as root
is the deletion of build products before your build begins.

The build runs `make` in `/app` with `PATH` holding only a C toolchain
and POSIX utilities (`cc`, `gcc`, `make`, `ar`, `ranlib`, `as`, `ld`, and the
usual shell tools -- there is no C++ compiler and no Go toolchain), with the
proxy variables pointed at a dead address, after deleting `goroot/bin`,
`goroot/pkg`, every `*.o`, `*.a`, `*.so` and `*.obj`, every regular file whose
contents begin with the ELF magic, and `bin/` and `pkg/` at the root of `/app`
as well as under `goroot/`. A file is removed on its *contents*, not its
permission bits: a shell script or a bytecode image that is not ELF survives. `$GOROOT/bin/go` must therefore be produced by that `make` from the
sources in `/app`, and it is the only program the cases ever invoke.

## 8. The liveness check

Before any case runs, one small program is built and run under **each** `go`
directive the step must answer to -- this milestone's own, and the oldest the
chain still supports.

That pair is the *liveness* check and it is not the whole set of directives
your toolchain has to keep answering. The module's `go` directive selects the
language version a package is compiled under, and the cases carry directives
from `go 1.4` up to the newest this milestone adds -- a case at an older
directive is a case your toolchain still has to answer, so an intermediate
directive is covered by the ordinary cases and not by this check. Adding a
milestone means raising the cap, not moving it.

**That program must build and run correctly under each of those directives.**
The toolchain must also be the C program `make` builds from this tree: no
program under `$GOROOT/bin` or `$GOROOT/pkg/tool` (following any symlink) may
itself be built by an upstream Go toolchain (gc), and no such program may be
left anywhere in `/app`, by the build or by the toolchain once it runs.

The program is small and uses nothing exotic -- a slice literal, a `for ... :=
range` loop over it, integer arithmetic, and `println` -- but its expected
output is **computed by running it**, not written anywhere in its source:

```go
package main

func main() {
	v := []int{ ...eight int literals... }
	s := ...an int literal...
	for i, x := range v {
		s = s*131 + x + i
	}
	println("live", s)
}
```

The constants differ from run to run. A toolchain that evaluates `int`
arithmetic with Go's wraparound semantics, ranges over a slice, and prints
with `println` passes it; there is nothing to special-case and nothing to look
up. If your `go run .` works on a program like this one, it passes this
check.

