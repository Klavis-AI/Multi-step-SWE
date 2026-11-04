# Go toolchain — Go 1.19 through Go 1.21

## 1. Implement this milestone

Carry the toolchain in `/app` forward to the Go 1.19–1.21 release train.

**`min`, `max` and `clear`** (Go 1.21) are predeclared functions: `min` and
`max` take one or more ordered arguments of the same type and are constant when
their arguments are; `clear` empties a map or zeroes every element of a slice.
All three are illegal when the module's `go` directive names a version before
1.21 — before that they are ordinary identifiers — and must be rejected then.

**`panic(nil)`** (Go 1.21). A `panic` with a nil argument is delivered to
`recover` as a `*runtime.PanicNilError` whose `Error` is
`panic called with nil argument`, so `recover() == nil` is false. Under a `go`
directive before 1.21 the old behaviour stands and `recover` returns an
untyped nil. The same program must behave differently under the two directives.

**The `sync/atomic` types** (Go 1.19): `Int32`, `Int64`, `Uint32`, `Uint64`,
`Bool` and `Pointer[T]`, whose zero values are usable and whose `Load`,
`Store`, `Add`, `Swap` and `CompareAndSwap` methods behave as documented.

**`slices`, `maps` and `cmp`** (Go 1.21) with the functions the API manifests
record for those releases, and **`errors.Join`** (Go 1.20), whose result
reports `Is` for every joined error and whose `Error` is the joined messages
separated by newlines.

**The Go 1.20 `unsafe` additions**: `unsafe.String(ptr, len)`,
`unsafe.StringData(s)` and `unsafe.SliceData(s)`, plus the slice-to-array
conversion `[N]T(s)`.

**`context.WithCancelCause` and `context.Cause`** (Go 1.20), and
**`runtime/debug.SetMemoryLimit`** (Go 1.19), which returns the previous limit
and reads the current one back when called with a negative argument.

`go.mod` may also carry a `toolchain` directive (Go 1.21); it must be accepted.

Everything the workspace already does must keep working, including for modules
whose `go.mod` names an earlier version.

## 2. Read these local references

**Normative semantics:** `/app/SPEC/go-spec.md` (the specification as of Go
1.21), `/app/SPEC/go-memory-model.md`, `/app/SPEC/release-notes/go1.19.md` …
`go1.21.md`, and the API manifests in `/app/SPEC/api/`.

**Additional files:** `/app/SPEC/toolchain.md`, `/app/SPEC/milestone.md`,
`/app/SPEC/packages.txt`, `/app/conformance/`, `/app/WORKSPACE_HANDOFF.md`.

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
