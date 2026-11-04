# Go toolchain — Go 1.17 and Go 1.18

## 1. Implement this milestone

Carry the toolchain in `/app` forward to the Go 1.17/1.18 release train.

**Generics** (Go 1.18). Type parameters on functions and on types, with
constraints, instantiation and type inference, exactly as the specification in
`/app/SPEC/go-spec.md` defines them:

* `func F[P C1, Q C2](…)` and `type T[P C] …` declare type parameters; methods
  on a generic type repeat the receiver's type parameters (`func (s *Stack[T])
  Push(v T)`).
* A constraint is an interface that may contain method requirements, union
  terms with `~` for underlying types, `comparable`, and embedded constraint
  interfaces. `any` is an alias for `interface{}`.
* An instantiation `F[int]` or `T[string, int]` substitutes the type arguments
  throughout; identical type arguments name the identical instantiated type.
* Type arguments are inferred from ordinary arguments where possible, so
  `F(x)` works when `x` determines the type parameters.
* Inside a generic body, operations must be the ones every type in the
  constraint's type set supports, and `var zero T` gives that type's zero value.
* A type parameter list, and an instantiation of a generic function or type
  such as `F[int]`, is illegal when the module's `go` directive names a
  version before 1.18 and must be rejected then.

**The Go 1.17 `unsafe` additions.** `unsafe.Add(ptr, len)` returns a pointer
advanced by a byte offset; `unsafe.Slice(ptr, len)` returns a slice of `len`
elements starting at `ptr`, and panics if `len` is negative or if `ptr` is nil
with a non-zero length. Both are illegal before `go 1.17`. `unsafe.Sizeof`,
`unsafe.Alignof` and `unsafe.Offsetof` are constants with the values the
implementation's memory layout gives them. Converting a slice to an array
pointer, `(*[N]T)(s)`, is legal from Go 1.17 and panics when `len(s) < N`.

**Fuzz targets** (Go 1.18). `go test` discovers exported `FuzzXxx(*testing.F)`
functions alongside `TestXxx`. Without `-fuzz` a target is run against its seed
corpus only: `f.Add(args...)` records one seed, and `f.Fuzz(fn)` runs `fn` once
per recorded seed, in the order the seeds were added, as a subtest of the
target named `seed#0`, `seed#1` and so on, with the seed's values spread across
`fn`'s parameters after its leading `*testing.T`. Tests run before fuzz
targets. `-fuzz` itself, and the on-disk corpus under `testdata/`, are out of
scope.

Everything the workspace already does must keep working, including for modules
whose `go.mod` names an earlier version.

## 2. Read these local references

**Normative semantics:** `/app/SPEC/go-spec.md` (the specification as of Go
1.18 — the type-parameter, type-set and type-inference sections are new),
`/app/SPEC/go-memory-model.md`, `/app/SPEC/release-notes/go1.17.md` and
`go1.18.md`, and the API manifests in `/app/SPEC/api/`.

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
