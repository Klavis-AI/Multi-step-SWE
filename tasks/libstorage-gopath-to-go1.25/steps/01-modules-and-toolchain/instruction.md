# Step 1 — from a GOPATH project to a Go module

## 1. Implement this milestone

`/app` holds libStorage as it was written for the GOPATH era: no `go.mod`, a
checked-in `vendor/` tree managed by glide, `// +build` constraint comments, and
one file that finds its own fixtures through `$GOPATH`. The Go toolchain in this
image is 1.25 and it is the only one available (`GOTOOLCHAIN=local`).

Turn the repository into a Go module that builds, tests and runs on its own,
with the module graph — not a checked-in copy of other people's code — supplying
its dependencies.

When you are done, all of the following must hold.

1. **The module.** `/app/go.mod` declares the module path
   `github.com/codedellemc/libstorage` — the import path every first-party file
   in the repository already spells — and a `go` directive of exactly `1.17`.
   Exactly: not a later version. 1.17 is the release that introduced the build
   constraint form required by point 5, and it is the version this milestone
   targets. The binary the repository builds must agree with `go.mod`, which is
   to say the build must actually use this module.

2. **The repository builds, and its dependencies come from the module
   graph.** `go build ./...` succeeds, and so does the binary the repository
   documents, `go build -o lss ./cli/lss/lss-linux`. No package that build
   compiles may have its source inside the repository: every dependency resolves
   through the module graph, at the versions minimal version selection picks.
   `go.mod` is where you say what the module is, not where you say what to
   overlook: it may not pin, redirect or bar a dependency by hand, and it may
   not drop a directory of the repository's own source out of the patterns
   above. A directory that the wildcard no longer reaches has not been made to
   build.

3. **The glide scaffolding is gone**: `glide.yaml`, `glide.lock` and
   `glide.mk`.

4. **No Go source in the repository deals in the `GOPATH` environment
   variable** — not `os.Getenv("GOPATH")`, not any other way of asking the
   environment, or the toolchain, for the same value, and not setting it
   either. Any path that was derived from it must be derived from something
   that still exists when the repository is not inside a GOPATH workspace, and
   must still point at the same files.

5. **Build constraints use the `//go:build` form.** Every file that carries a
   legacy `// +build` line must carry the modern constraint instead, with no
   `// +build` line left beside it. The constraint itself does not change: no
   constrained file is deleted, no file's constraint is dropped, and no file
   that has none gains one, so every platform and tag still selects exactly
   the files it selected before. That holds for the files whose constraint is
   their name rather than a comment too: the set of files that are built only
   on some platforms, and which platforms each of them is built on, does not
   change either. What has to survive is which files a configuration selects,
   not whether that configuration compiles: a tag whose packages did not build
   before this milestone is not required to build after it, and repairing a
   pre-existing compile error behind a tag is not part of this step.

6. **`gofmt -s` reports nothing** for the repository's own Go files.

7. **`go vet ./...` reports nothing.**

8. **No deprecated identifier is used** — nothing whose own documentation
   carries Go's `// Deprecated:` marker — with one exception: `io/ioutil`.
   That package is excluded from this requirement; every other identifier
   marked deprecated is covered by it.

9. **The observable behavior of the tree you inherit does not change.** Same
   HTTP API, same JSON, same CLI output, same log output, same configuration
   keys, same exit codes. This milestone is about how the project is built, not
   about what it does. The repository's own tests must pass — do not delete,
   skip, weaken or narrow them, do not silence a tool with a suppression
   comment instead of fixing what it found, and do not discard a returned
   value into `_` to make something compile.

## 2. Read these local references

Everything you need is in the container.

* **The repository**, `/app` — its source, its `Makefile`, its `.docs/`
  directory, and its own tests under `api/tests`, `api/utils`, `api/types`,
  `drivers/storage/vfs/tests` and elsewhere.
* **The toolchain's own documentation**, `go help mod`, `go help build`,
  `go help modules`, `go doc`, and `gofmt -h`. The module mirror this image uses
  (`GOPROXY=file:///opt/gomodmirror`) publishes exactly the versions the glide
  lockfile pinned, so `go mod tidy` has nothing to choose and needs nothing
  from anywhere else.

Useful commands as the repository defines them:

```
go build -o lss ./cli/lss/lss-linux     # the server/CLI binary
./lss -c <config>                       # start it; -l <level> sets the log level
go test ./...                            # the repository's own suite
```

Two facts about the inherited tree that are not defects for you to chase:

* **Run the repository's tests as a non-root user.** libStorage resolves its
  default paths differently for uid 0, and 25 of `api/utils`' path cases assert
  the non-root answers. The account `builder` (uid 1001) exists in this image
  and owns `/app`; `su builder -c 'go test ./...'` is how the suite is meant to
  be run. Everything must also work when run as a non-root user.
* Before point 7 is done, `go test` will refuse to run because it runs a subset
  of `go vet` first. `go test -vet=off ./...` gets you a test result in the
  meantime.

Finally, a requirement that is not observable from inside this container:
**your tree must build and work in a clean container that holds the
repository and the Go toolchain and nothing else.** There is no GOPATH
workspace there and no symlink from one into `/app` — this container still has
that link, because without it the tree you inherited could not be built at all.
Anything that works here only because of that link will not work there.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and preserve the supplied source,
  tests and documents. Do not create a prose answer or an alternate entry point.
- Do not cheat by using online solutions or hints specific to this task.
- Run the local checks named above and leave a reproducible handoff for the next
  fresh session.
