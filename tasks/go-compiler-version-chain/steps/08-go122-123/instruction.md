# Go toolchain — Go 1.22 and Go 1.23

## 1. Implement this milestone

Carry the toolchain in `/app` forward to the Go 1.22/1.23 release train. Two of
these changes are gated on the module's `go` directive, so the same program
must behave differently depending on it.

**Per-iteration loop variables** (Go 1.22). Under a `go` directive of 1.22 or
later, the variables declared by a `for` statement's init clause, and by a
`for … range` clause using `:=`, are **new variables in each iteration**: a
function literal or a pointer created in one iteration observes that
iteration's value, and the three-clause form carries the value forward into the
post statement. Under a directive before 1.22 there is one variable for the
whole loop and closures share it. Both behaviours must be implemented and
selected by the directive.

**`for range` over an integer** (Go 1.22): `for i := range n` iterates `i` from
`0` to `n-1`, and is illegal before 1.22.

**Range over a function** (Go 1.23): `for … range f` where `f` is
`func(yield func() bool)`, `func(yield func(V) bool)` or
`func(yield func(K, V) bool)` calls `f` with a yield function that runs the
loop body and returns false once the loop stops, so `break` and `return` inside
the body stop the iteration. The `iter` package's `Seq` and `Seq2` name those
function types. Range over a function is illegal before 1.23.

**`unique`** (Go 1.23): `unique.Make(v)` returns a `Handle[T]`; two handles are
equal exactly when the values they were made from are equal, and `Value`
returns the value back.

**The `slices`, `maps` and `cmp` additions of these two releases**, with the
signatures the API manifests in `/app/SPEC/api/` record — that file is the
authority, and nothing outside it is required. They include the iterator
functions `slices.All`, `slices.Values`, `slices.Backward`, `slices.Collect`,
`slices.Sorted`, `slices.SortedFunc`, `slices.Chunk`, `maps.Keys`,
`maps.Values` and `maps.All`; `slices.Concat` and `cmp.Or` from Go 1.22; and
`slices.Repeat` from Go 1.23. An iterator stops when the loop body stops.

Everything the workspace already does must keep working, including for modules
whose `go.mod` names an earlier version.

## 2. Read these local references

**Normative semantics:** `/app/SPEC/go-spec.md` (the specification as of Go
1.23), `/app/SPEC/go-memory-model.md`, `/app/SPEC/release-notes/go1.22.md` and
`go1.23.md`, and the API manifests in `/app/SPEC/api/`.

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
