# Go toolchain — Go 1.14 through Go 1.16

## 1. Implement this milestone

Carry the toolchain in `/app` forward to the Go 1.14–1.16 release train.

**Asynchronous preemption** (Go 1.14). A goroutine must be preemptible even
when its loop contains no function call, no channel operation and no
allocation: a program that starts such goroutines must still run its other
goroutines to completion, complete a `runtime.GC()`, and finish. No goroutine
may monopolise the scheduler.

**`//go:embed`** (Go 1.16). A `//go:embed pattern…` comment immediately above a
package-level `var` declaration initialises that variable from files in the
package directory, as `go help embed` and the `embed` package documentation
describe. A `string` or `[]byte` variable takes the contents of a single
matching file; an `embed.FS` variable takes a read-only file system holding
every matching file under its path relative to the package directory. A
pattern that names a directory matches everything under it except names
beginning with `.` or `_` unless the pattern is prefixed with `all:`. A
pattern that matches nothing is a compile-time error, reported on the line of
the directive. `embed.FS` provides `ReadFile`, `ReadDir` and `Open`, and
satisfies `io/fs.FS`.

**`io/fs`** (Go 1.16), with `FS`, `File`, `FileInfo` (`Name`, `Size`,
`IsDir`), `DirEntry` (`Name`, `IsDir`, `Info`), `ReadDirFS`, `ReadFileFS`,
`ValidPath`, `ReadFile`, `ReadDir`, `WalkDir`, `SkipDir`, `Sub`, `Glob` and
`ErrNotExist`, behaving as documented — in particular `WalkDir` visits the root
itself first and then walks each directory's entries in the order `ReadDir`
returns them, which for an `embed.FS` is sorted by filename, descending into a
directory immediately after visiting it. `ValidPath` reports whether a name is
an unrooted, slash-separated path: `.` alone is valid, and a name is invalid if
it is empty, begins or ends with a slash, contains an empty, `.` or `..`
element, or contains two consecutive slashes.

**Module mode on by default** (Go 1.16). Building or running in a directory
that has no `go.mod` in it or any parent is an error. `GO111MODULE` still
selects the mode: `GO111MODULE=off` restores GOPATH mode, in which a directory
with no `go.mod` builds as long as it imports nothing outside the standard
library.

Everything the workspace already does must keep working, including for modules
whose `go.mod` names an earlier version.

## 2. Read these local references

**Normative semantics:** `/app/SPEC/go-spec.md` (the specification as of Go
1.16), `/app/SPEC/go-memory-model.md`, `/app/SPEC/release-notes/go1.14.md` …
`go1.16.md`, and the API manifests in `/app/SPEC/api/`.

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
