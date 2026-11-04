# Go toolchain — Go 1.24 and Go 1.25

## 1. Implement this milestone

Carry the toolchain in `/app` forward to the Go 1.24/1.25 release train — the
final milestone in this workspace.

**Generic type aliases** (Go 1.24): an alias declaration may take type
parameters, `type StringPair[V any] = Pair[string, V]`. Instantiating the alias
names the aliased type itself, so the alias and its target are identical types
for assignment, method sets and `%T`.

**`runtime.AddCleanup`** (Go 1.24): `AddCleanup(ptr, fn, arg)` attaches a
cleanup to the object `ptr` points at and returns a `Cleanup` whose `Stop`
method detaches it. Unlike `SetFinalizer` the cleanup function does not take
the object, so it cannot resurrect it; `AddCleanup` panics when `ptr` is nil.

**`testing/synctest`** (GA in Go 1.25): `synctest.Test(t, f)` runs `f` in a
bubble with a synthetic clock. Inside the bubble, time only advances when every
goroutine in it is durably blocked, so a goroutine that sleeps for a second
returns immediately in real time while `time.Since` still reports that the
second elapsed, and goroutines with different sleeps still finish in time
order. `synctest.Wait` blocks until every goroutine in the bubble other than
the caller is durably blocked.

**`go vet`** (`/app/SPEC/milestone.md` describes its output). It must run the
analyzers that report: a `fmt.Printf`-family call whose argument count does not
match the verbs in a constant format string; a `sync.WaitGroup.Add` call made
from inside the goroutine that the `WaitGroup` is waiting on (the `waitgroup`
analyzer added in Go 1.25); and a `TestXxx` function in a `_test.go` file whose
signature is not `func(*testing.T)`. Only the positions it reports are
specified; its message text is free.

**The iterator forms in `strings` and `bytes`** (Go 1.24): `strings.Lines`,
`strings.SplitSeq`, `strings.SplitAfterSeq`, `strings.FieldsSeq`,
`strings.FieldsFuncSeq` and `bytes.Lines`, with the signatures the API
manifests record. Each returns an `iter.Seq` over what the corresponding
non-iterator function returns, produced lazily, and stops when the loop body
stops. `Lines` yields newline-terminated lines: each keeps its trailing
newline, and the last one has a newline only if the input ends with one.

**`sync.WaitGroup.Go`** (Go 1.25): `wg.Go(f)` increments the counter, runs `f`
in a new goroutine, and decrements when `f` returns, including when `f` panics.

Parameterised type aliases are gated on the `go` directive naming **1.23 or
later**; under an earlier directive the declaration must be rejected on its own
line. `/app/SPEC/release-notes/go1.24.md` says Go 1.24 "now fully supports"
them, and that is about the *release* rather than the *directive*: 1.23 put the
gate at `go1.23` and 1.24 turned the feature on by default. The gate this
milestone must implement is the one the specification and the compiler use,
`go1.23`.

Everything the workspace already does must keep working, including for modules
whose `go.mod` names an earlier version — the whole chain from `go 1.4`
onwards.

## 2. Read these local references

**Normative semantics:** `/app/SPEC/go-spec.md` (the specification as of Go
1.25), `/app/SPEC/go-memory-model.md`, `/app/SPEC/release-notes/go1.24.md` and
`go1.25.md`, and the API manifests in `/app/SPEC/api/`.

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
