# Go toolchain — Go 1.7 through Go 1.10

## 1. Implement this milestone

Carry the toolchain in `/app` forward to the Go 1.7–1.10 release train.

**`go test` with subtests.** `$GOROOT/bin/go test [-v] [-run PATTERN] ./...`
must compile each package together with its `_test.go` files, run every
`func TestXxx(*testing.T)` it declares, and report in the `go test` format
described in `/app/SPEC/milestone.md`. The `testing` package must provide `T`
with `Run`, `Log`, `Logf`, `Error`, `Errorf`, `Fatal`, `Fatalf`, `Fail`,
`FailNow`, `Skip`, `Skipf`, `SkipNow`, `Failed`, `Skipped`, `Name` and
`Helper`. `t.Run` (Go 1.7) runs a subtest whose name is the
parent's name, a `/`, and the subtest's name with spaces replaced by
underscores; a failing subtest fails its parent; `-run` selects with a
`/`-separated pattern, one element per subtest level. `Fatal`, `Fatalf`,
`SkipNow` and `FailNow` stop the calling test but not the run: deferred
functions in the stopped test still run. `t.Name()` is the full `/`-joined
subtest name and `t.Helper()` marks a function as a helper. Log lines carry the
`file.go:LINE: ` position of the call that produced them and, under `-v`, are
printed as they happen at one level of indentation whatever the subtest depth.

**`context`** (Go 1.7), with `Background`, `TODO`, `WithCancel`,
`WithDeadline`, `WithTimeout`, `WithValue`, the `Context` interface, and the
`Canceled` and `DeadlineExceeded` errors, behaving as its documentation
specifies: cancelling a context closes its `Done` channel and every descendant's,
`Err` reports `Canceled` or `DeadlineExceeded` after that and `nil` before,
cancelling twice is harmless, and `Value` walks up the chain.

**Type aliases** (Go 1.9): `type A = T` declares a second name for `T`, not a
new type — identical for assignment, method sets and `%T`. A type alias is
illegal when the module's `go` directive names a version before 1.9 and must be
rejected then, which is the general rule: the `go` directive in `go.mod`
selects the language version a package is compiled under, so the same file can
be legal under one directive and illegal under another.

**`sync.Map`** (Go 1.9) with `Load`, `Store`, `LoadOrStore`, `Delete` and
`Range`. The API manifests in `/app/SPEC/api/` are the authority on what
`sync.Map` has at this milestone: nothing outside them is required, or
available.

**Monotonic time** (Go 1.9): `time.Now` returns a `Time` that also carries a
monotonic clock reading. `Sub`, `Since`, `Until`, `Before`, `After` and `Equal`
use the monotonic reading when both operands have one; `Add` shifts both;
`Round` and `Truncate` strip it, leaving the wall-clock reading, and `Round(0)`
strips it without otherwise changing the value.

**`sort.Slice`, `sort.SliceStable` and `sort.SliceIsSorted`** (Go 1.8), which
sort any slice given a `less` function over indices.

Everything the workspace already does must keep working, including for modules
whose `go.mod` names an earlier version.

## 2. Read these local references

**Normative semantics:** `/app/SPEC/go-spec.md` (the specification as of Go
1.10), `/app/SPEC/go-memory-model.md`, `/app/SPEC/release-notes/go1.7.md` …
`go1.10.md`, and the API manifests in `/app/SPEC/api/`.

**Additional files:** `/app/SPEC/toolchain.md`, `/app/SPEC/milestone.md`,
`/app/SPEC/packages.txt`, `/app/conformance/` (a corpus of programs and
`selfcheck.py`), `/app/WORKSPACE_HANDOFF.md`.

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
