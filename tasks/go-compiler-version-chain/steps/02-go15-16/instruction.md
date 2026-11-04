# Go toolchain — Go 1.5 and Go 1.6

## 1. Implement this milestone

Carry the toolchain in `/app` forward to the Go 1.5/1.6 release train. Three
things are new.

**A real collector.** The runtime must reclaim memory precisely and keep
running while it does. Concretely: an object is live only while it is reachable
from a goroutine's active variables, a package-level variable, or a value held
in an interface, map, slice, channel or closure; anything else is garbage.
`runtime.SetFinalizer(obj, fn)` associates a finalizer with an object, the
collector clears that association and runs `fn(obj)` once the object becomes
unreachable, `runtime.SetFinalizer(obj, nil)` removes it.
`runtime.GC()` runs a collection to completion. `runtime.ReadMemStats` must
fill in `HeapAlloc`, `TotalAlloc`, `Mallocs`, `Frees`, `NumGC` and
`PauseTotalNs` with the meanings their documentation gives, and
`runtime/debug.SetGCPercent` and `debug.FreeOSMemory` must have the documented
effects. `NumGC` counts completed cycles and `PauseTotalNs` accumulates
stop-the-world time, so both must advance across a forced collection. The API
manifests in `/app/SPEC/api/` are the authority on what exists at this
milestone: nothing outside them is required, or available. A program that keeps allocating while another
goroutine runs must make progress in both: collection may not starve the
mutator, and a long allocation loop must neither stall nor grow without bound.

**`GOMAXPROCS` defaults to the number of CPUs** (Go 1.5), so
`runtime.GOMAXPROCS(0)` reports `runtime.NumCPU()` until a program changes it,
and `runtime.GOMAXPROCS(n)` returns the previous setting.

**The `internal` visibility rule** (Go 1.5) applies to every module: a package
whose import path contains an `internal` element may only be imported by
packages inside the tree rooted at that element's parent directory. An import
that breaks the rule is a compile-time error reported on the import line.

Also provide `bytes` and `sync/atomic` from `/app/SPEC/packages.txt`, with the
API the manifests in `/app/SPEC/api/` record through Go 1.6.

Everything the workspace already does must keep working, including for modules
whose `go.mod` names an earlier version: a module that says `go 1.4` must build
and behave exactly as it did before this milestone.

## 2. Read these local references

**Normative semantics:** `/app/SPEC/go-spec.md` (the specification as of Go
1.6), `/app/SPEC/go-memory-model.md`, `/app/SPEC/release-notes/go1.5.md` and
`go1.6.md`, and the API manifests in `/app/SPEC/api/`.

**Additional files:** `/app/SPEC/toolchain.md` (the toolchain interface, where
`$GOROOT` is, the build command, and exactly which behaviour must match the
reference toolchain), `/app/SPEC/milestone.md` (the `go` subcommands this
milestone uses), `/app/SPEC/packages.txt`, `/app/conformance/` (a corpus of
programs plus `selfcheck.py`), and `/app/WORKSPACE_HANDOFF.md`.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and preserve the supplied source,
  manifests, documents, and handoff files. Do not create a prose answer or
  alternate entry point unless Section 1 explicitly requires one.
- The toolchain is built by running `make` in `/app`, compiled with `cc`, from a
  tree with every build product deleted, so the default target must rebuild
  `$GOROOT/bin/go` from the sources in `/app` alone.
- Do not search for, download, copy, wrap, or invoke an
  online implementation or task-specific answer; work only from this workspace,
  its documentation and `/opt/docs`, and do not search the system for other
  implementations or answers.
- Run the local checks named by the references and leave a reproducible
  handoff for the next fresh session.
