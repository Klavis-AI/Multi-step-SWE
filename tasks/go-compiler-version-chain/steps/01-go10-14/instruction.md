# Go toolchain — Go 1.0 through Go 1.4

## 1. Implement this milestone

Build a Go toolchain in C that compiles and runs Go programs written against
the **Go 1.4 language specification**, driven through the `go` command.

`$GOROOT/bin/go` must support `go run .` and `go build -o NAME .` for a module
directory, and the language it accepts must be the one `/app/SPEC/go-spec.md`
defines: packages and imports, the full type system (named types, structs with
embedding and promoted fields, interfaces and method sets, pointers, arrays,
slices, maps, channels, function types), constants with arbitrary-precision
arithmetic and `iota`, all statements including `switch`, type switches,
`select`, labelled `break`/`continue`, `goto` and `fallthrough`, closures,
variadic calls, multiple return values, `defer`/`panic`/`recover`, and the
memory model in `/app/SPEC/go-memory-model.md`.

A module may contain several packages: `go.mod` declares the module path, and
an import of that path or a path below it resolves to the corresponding
directory of the module. Package-level variables are initialised in dependency
order, imported packages are initialised before their importers, and `init`
functions run in the order the specification gives.

The runtime behind it must provide goroutines, unbuffered and buffered channels
with `select`, a scheduler that runs ready goroutines and reports
`fatal error: all goroutines are asleep - deadlock!` when none can proceed,
maps, slices with `append`/`copy` aliasing semantics, UTF-8 strings, interface
values carrying a dynamic type, and the run-time panics the specification
requires (out-of-range index, out-of-range slice bounds, nil map write, nil
pointer dereference, integer divide by zero, failed type assertion, send on or
close of a closed channel). Memory is reclaimed: a program that allocates far
more than it keeps live must run to completion, and `runtime.ReadMemStats` must
report a live heap (`HeapAlloc`) that is a small fraction of everything the
program ever allocated (`TotalAlloc`), more objects freed than still live
(`Frees` against `Mallocs`), and at least one completed collection (`NumGC`),
all with the meanings that package's documentation gives them.

Programs also use the standard library. Provide the packages listed in
`/app/SPEC/packages.txt` — the `go` command looks for their sources under
`$GOROOT/src/<import path>/` — with the exported API the Go project's own
manifests in `/app/SPEC/api/` record for Go 1.0 through Go 1.4 and the
behaviour their documentation specifies. Only the part of that API that
programs like those in `/app/conformance/` reference has to work.

A program that the specification declares illegal must be rejected: the build
must fail, and a diagnostic in the conventional `file:line:col: message` form
must be reported on standard error for each line that holds an illegal
construct, and for no other line.

## 2. Read these local references

**Normative semantics:** `/app/SPEC/go-spec.md` (The Go Programming Language
Specification as of Go 1.4), `/app/SPEC/go-memory-model.md`,
`/app/SPEC/release-notes/go1.1.md` … `go1.4.md`, and the API manifests in
`/app/SPEC/api/` (`go1.txt` is the Go 1.0 API; `go1.N.txt` lists what release
1.N added), filtered to the packages this milestone requires.

**Additional files:** `/app/SPEC/toolchain.md` defines the toolchain interface —
where `$GOROOT` is, the build command, how `go` is driven, and exactly which
behaviour must match the reference toolchain. `/app/SPEC/milestone.md` lists the
`go` subcommands this milestone uses. `/app/SPEC/packages.txt` lists the
standard-library packages to provide. `/app/conformance/` holds a corpus of
programs — from the Go project's own `test/` directory and beyond — with the
behaviour the reference toolchain produces for them, and `selfcheck.py` to run
it. `/app/WORKSPACE_HANDOFF.md` is the handoff note for whoever picks the
workspace up next.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and preserve the supplied source,
  manifests, documents, and handoff files. Do not create a prose answer or
  alternate entry point unless Section 1 explicitly requires one.
- Write the toolchain in C. It is built by running `make` in `/app`, compiled
  with `cc`, from a tree with every build product deleted, so the default
  target must rebuild `$GOROOT/bin/go` from the sources in `/app` alone.
- Do not search for, download, copy, wrap, or invoke an
  online implementation or task-specific answer; work only from this workspace,
  its documentation and `/opt/docs`, and do not search the system for other
  implementations or answers.
- Run the local checks named by the references and leave a reproducible
  handoff for the next fresh session.
