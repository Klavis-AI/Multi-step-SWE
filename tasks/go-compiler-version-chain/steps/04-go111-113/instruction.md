# Go toolchain — Go 1.11 through Go 1.13

## 1. Implement this milestone

Carry the toolchain in `/app` forward to the Go 1.11–1.13 release train.

**Modules and minimal version selection.** `go.mod` may carry `require` and
`replace` directives as the Go Modules Reference defines them. A `require` line
names a module path and a version; a `replace` line redirects a module path
(optionally at one specific version) to another path or, when the target begins
with `./`, `../` or `/`, to a directory, resolved relative to the `go.mod` that
declares it. The build list is computed by **minimal version selection**: walk
the requirement graph from the main module, and for each module path select the
highest version any module in the graph requires. Imports then resolve to the
selected version's directory, so a module that requires `v1.0.0` of a dependency
must see `v1.4.0` when something else in the graph requires that. Each
dependency has its own `go.mod`, its own `go` directive and its own requirements.

**Wrapped errors** (Go 1.13). `fmt.Errorf` with a `%w` verb formats the operand
like `%v` and returns an error whose `Unwrap` method returns it.
`errors.Unwrap`, `errors.Is` and `errors.As` behave as their documentation
specifies: `Is` walks the chain comparing with `==` and honouring an
`Is(error) bool` method, `As` walks the chain and assigns the first error whose
concrete type is assignable to the target pointer's element type.

**The Go 1.13 number literals and shift counts.** Binary literals (`0b1011`),
the `0o` octal prefix, hexadecimal floating-point literals (`0x1p-2`), and
underscores as digit separators (`1_000_000`, `0x_FF_FF`) are legal, and shift
counts may be signed. All of these are illegal when the module's `go` directive
names a version before 1.13 and must be rejected then, on the line that holds
them. A negative shift count is a run-time panic.

**Vendoring.** When the main module has a `vendor/` directory, `go build` and
`go run` with `-mod=vendor` resolve every non-standard import from
`vendor/<import path>` instead of the module cache or a `replace` target, and
`vendor/modules.txt` records which modules and packages are vendored. A
vendored dependency needs no `go.mod` of its own.

Everything the workspace already does must keep working, including for modules
whose `go.mod` names an earlier version.

## 2. Read these local references

**Normative semantics:** `/app/SPEC/go-spec.md` (the specification as of Go
1.13), `/app/SPEC/go-memory-model.md`, `/app/SPEC/release-notes/go1.11.md` …
`go1.13.md`, and the API manifests in `/app/SPEC/api/`. The module mechanics
are the Go Modules Reference at https://go.dev/ref/mod (local copy:
`/opt/docs/go/ref-mod/mod.md`).

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
