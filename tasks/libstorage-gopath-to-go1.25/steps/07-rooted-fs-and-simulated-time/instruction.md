# Step 7 — an identifier is not a path, and a lifetime is not a goroutine

## 1. Implement this milestone

`/app` is a Go module at Go 1.23. Its enumeration is sequences, its logging is
structured, its errors lead to their causes, and its generics are its own.
Two things in it are still written the way 2015 wrote them, and both are the
same mistake in different clothing: a name supplied from outside is turned
into a path and then trusted, and a lifetime is implemented by a goroutine
that nobody can stop.

**The first.** `drivers/storage/vfs/storage` keeps one JSON document per
volume and one per snapshot, in two directories under the driver's data
directory. To find a document it formats the identifier it was given into the
directory's name — `fmt.Sprintf("%s/%s.json", d.volPath, volumeID)` — and
then opens, writes, lists or unlinks the result. Formatting does not reject
`..`, and `path.Join`, which the removal paths use, does not reject it
either; it cleans it. So `../snap/vfs-000-001` is a volume identifier that
names a snapshot document, and `../../outside` is a volume identifier that
names a file beside the data directory altogether. Every one of these
identifiers arrives from a caller.

The only thing standing in front of those operations is the vendored
existence check, and it is not a guard. It asks whether the *cleaned* path is
there, which for a path pointing one directory over is yes — and then the
open goes through. It is worse than that: it reports *present* for every
error that is not "does not exist", so a name the filesystem itself refuses
is announced as a file that exists, and the operation behind it fails with
whatever the kernel said.

Go 1.25 is where the standard library finished the answer to this. An
`os.Root` is a directory handle, and every operation you ask of it resolves
its argument *inside* that directory or fails — no `filepath.Abs`, no check
you have to remember to write, and no way for a name to reach past the
directory the handle is on, however many `..` elements it contains. At 1.25 it carries
the whole vocabulary this package needs — `Open`, `Create`, `Remove`,
`MkdirAll`, `RemoveAll`, and an `FS` you can hand to `io/fs`.

**The second.** `api/utils/utils_store.go` is the key/value store the whole
tree uses for configuration, for instance metadata and for a context's own
values. Built with a lifetime, it starts a goroutine on `time.Tick` to sweep
entries whose deadlines have passed. `time.Tick` returns a channel and keeps
no handle on the ticker behind it, so there is nothing to stop and nobody who
could stop it: every store ever built with a lifetime leaves a goroutine
waiting for the life of the process.

And it is the only thing that expires anything, which makes the store answer
four different questions four different ways. `IsSet` and `Get` consult an
entry's deadline. `Keys` reads the backing map. `Map` is built out of `Keys`
and `Get`. So for up to a sweep interval — and for a caller who reads once,
forever — an entry that has run out is absent from two views and present in
the other two, and a caller has no way to know which pair to believe.

Expiry does not need a goroutine. An entry that is asked about is an entry
whose deadline can be checked, and one that has passed can be dropped there
and then. That is one place instead of four, no sweeper, and four views that
agree because they are one answer.

**And a test that does not cost a lifetime to run.** A store's lifetime is
measured in seconds and the floor on the sweep was a second, so the honest
test for any of this used to mean sleeping. Go 1.25's `testing/synctest`
removes the reason: inside a bubble, time is the bubble's own, and it jumps
forward whenever everything in the bubble is waiting. A two-minute lifetime
passes instantly. The bubble also refuses to finish while a goroutine inside
it is still waiting on that clock — which is the same statement as "nothing
was left running", made by the framework rather than by an assertion you have
to invent.

Move the language version forward, confine the driver's two data
directories, retire the sweeper, and write the repository's own test for the
store in simulated time.

When you are done, all of the following must hold.

1. **The module declares Go 1.25.** `go.mod`'s `go` directive is exactly
   `1.25`, and the tree builds and vets at that version.

   1.25 is where `os.Root` gained `MkdirAll`, `RemoveAll`, `ReadFile` and
   `WriteFile` beside the `Open`, `Create`, `Stat`, `Remove` and `FS` it
   already had, and where `testing/synctest` became a released package with a
   `Test` function rather than an experiment behind a `GOEXPERIMENT`. The
   rest of this step is what those two changes are for. The directive is
   exact rather than a floor, for the reason it has been exact at every
   milestone here: it states the language semantics the whole tree is
   compiled and vetted under, and the subject of this step is what 1.25 makes
   available, so the number to declare is the one this requirement names
   rather than the highest one the toolchain would accept.

2. **The vfs driver reaches its documents through a confined directory
   handle, and through nothing else.** When you are done,
   `drivers/storage/vfs/storage` holds at least one `os.Root` — how many, and
   on which directories, is yours to decide — and:

   * no call to `os.Create`, `os.Remove` or `filepath.Glob` remains anywhere
     in this repository. All six of them are in this package today: two write
     a document, two unlink one, and two list what a directory holds. A root
     has a method for each, and `io/fs` will glob the `FS` a root hands out.
   * the four calls to the vendored existence check in this package are gone.
     There are eighteen in the tree; the other fourteen are not this step's —
     four in the vfs executor, which manages a device file rather than the
     data directory, and ten in configuration loading, TLS material and the
     CLI. Leave those alone.

   One property of that existence check is load-bearing for requirement 3
   and is easy to miss: it asks the filesystem a question one call before the
   operation asks the same question, and it collapses two different answers
   into one. A document that is absent and a name the filesystem refuses both
   come back from it as `false`, and the not-found error the driver returns
   next is the same in both cases — which is why requirement 3 has something
   to say about each of them separately.

3. **The identifiers that walk upwards stop working, and nothing else
   changes.** Take the driver as the service registry hands it out — a
   `types.StorageDriver`, with the data directory wherever the configuration
   puts it — and:

   * `VolumeInspect` given `../snap/<some snapshot id>` returns an error, not
     the snapshot.
   * `SnapshotInspect` given `../vol/<some volume id>` returns an error, not
     the volume.
   * `VolumeRemove` given `../snap/<some snapshot id>` returns an error, and
     the snapshot is still there afterwards.
   * `VolumeRemove` given `../../outside` returns an error, and a file of
     that name beside the data directory is still there afterwards.

   Which error each of these is, is yours. That a caller gets one, and that
   nothing on the other side of the directory boundary was read or unlinked,
   is not.

   Note that the removals walk upwards twice: once for the document and once
   for the per-volume directory the removal also deletes. Confining one and
   not the other leaves the second escape open.

   What must not change is every answer for an identifier that is *not* an
   escape:

   * The volume lifecycle behaves as it does now: create a volume, inspect
     it, list volumes, snapshot it, remove it.
   * An identifier that simply is not there is still reported as not found,
     and the message a caller reads is still exactly `resource not found` —
     for volume inspect, for snapshot inspect, and for volume remove.
   * An identifier the *filesystem* refuses is still reported with the
     operation and the absolute path it named. A volume identifier containing
     a NUL byte still produces
     `open <volumes directory>/<identifier>.json: invalid argument`, and one
     six hundred characters long still produces
     `open <volumes directory>/<identifier>.json: file name too long`.

     This last one is the requirement you are most likely to break without
     noticing. An `os.Root` names its own operations and reports the argument
     you gave it, not the path you would have built — so the string reaching
     a caller changes unless you restate it. These messages are rendered into
     HTTP response bodies, so they are part of this API's surface, not
     diagnostics.

4. **A store forgets an expired entry when it is asked about it, and keeps no
   goroutine.** When you are done:

   * one call to `time.Tick` remains in this repository, down from two. The
     one that stays polls a running executor for its output and is not this
     step's subject; the one that goes is the store's sweeper.
   * an entry whose lifetime has run out is absent from all four views of the
     store *at the same time*: `IsSet` is false, `Get` is nil, `Keys` does
     not list it, and `Map` does not contain it.
   * `Map` returns while another goroutine is calling `Set`, however many
     times the two are interleaved. It does not today: it takes the read lock
     and then calls two exported readers that take it again, and Go's
     `sync.RWMutex` will not grant a second read lock while a writer is
     waiting, so a `Set` arriving in between stops all three permanently.
     This is a defect in its own right and it is in scope here.
   * a store built with a lifetime leaves nothing of its own running once the
     caller that built it is done with it.

   Unchanged: an entry still inside its lifetime is in all four views, a
   store built without a lifetime keeps what it is given for as long as the
   process lives, and a sliding lifetime still slides — reading an entry in a
   store that was not built with a hard limit still extends it.

5. **The repository's own test for that store runs in simulated time.** At
   least one package imports `testing/synctest` and calls `synctest.Test`,
   and the function inside the bubble advances the bubble's clock — a
   lifetime that has to pass is a `time.Sleep` inside the bubble, where it
   costs nothing. A bubble that wraps an assertion and returns without
   advancing anything has tested nothing about a lifetime.

   Do not pay for this with wall-clock time elsewhere: the three `time.Sleep`
   calls in this tree today are all in non-test code, and no further call
   outside a bubble may join them. A sleep inside a bubble is simulated and free; a sleep outside one
   is a second of every future run of the suite and a flake on a loaded
   machine.

6. **Everything that worked still works.** The API answers every request as
   it does now — the same status codes, the same headers, the same JSON — and
   the repository's own tests pass in every package. The store, the logger,
   the router and the service registry are all reached by this step's two
   subjects; none of them changes behaviour.

   The repository's own tests are not deleted, skipped, weakened or narrowed:
   the number of test files, test functions, specs and assertions does not
   fall, no test file gains a build constraint, no tool is silenced with a
   suppression comment, and a returned value goes into `_` only in the places
   this tree already puts one. Two of these are easy to trip here: the number
   of assertions in the repository's own tests does not fall, and the number
   of values whose only destination is `_` does not rise. A lookup written to
   discard everything but its error is one of the second kind.

7. **The milestones you inherit stay done.** Nothing here needs you to undo
   any of them.

   `golang.org/x/net/context` and its `ctxhttp` subpackage stay out of the
   build, the pair of files that once chose between that package and the
   standard library's stay gone, and the standard library's `context` stays
   live in the packages that deal in one. The server's configured read and
   write timeouts keep taking effect; stopping the server still drains what is
   in flight, still closes idle connections, and still gives up after a bounded grace period; a cancelled
   request still comes back promptly with the caller's own context error.

   `github.com/akutz/goof` stays out of the repository entirely. Errors still
   lead to their causes through any number of layers and still say what they
   were doing on the way, `errors.Is` and `errors.As` still find them, an
   error type still answers `Unwrap`, nothing decides what an error is by
   comparing its text or its identity or by asserting it to a concrete type,
   no cause is dropped on the way out, and the HTTP status an error is
   reported with still belongs to the error rather than to the filter that
   writes it.

   The schema document still exists exactly once and Go still embeds it at
   build time; nothing in this tree is written twice because it names two
   types; the type parameters that replaced six near-identical declarations
   stay; `sort` and `io/ioutil` stay out of every package here while
   `slices`, `maps` and `cmp` stay in, and a map is still copied by the
   standard library rather than by a loop. `crypto/rand` stays exactly where
   it is.

   No function literal works around a loop variable, and none of them reaches
   past the value it was handed. `github.com/Sirupsen/logrus`,
   `github.com/codedellemc/gournal`, its `logrus` bridge,
   `github.com/gorilla/mux`, `github.com/gorilla/context` and `math/rand` all
   stay gone; `log/slog` stays in at least five packages and `math/rand/v2`
   in the one that needs it; and the values matched out of a request's path
   still come from the request itself.

   Enumeration stays a sequence. No function in this repository hands back a
   channel in order to enumerate something; the registry, context and service
   producers keep their `iter.Seq` types, can be walked more than once, start
   no goroutine, yield everything there is, and end the producer and release
   its lock when a walk ends early; at least four packages import `iter`; the
   middleware chain is still walked from its end with `slices.Backward`; a
   map's keys or values are still collected with the `slices` and `maps`
   adapters, with exactly one append loop of that shape left; and the four
   driver registries in `api/registry` still key on `unique.Handle[string]`.

8. **The module ground.** `go.mod` still declares
   `module github.com/codedellemc/libstorage`, `go build ./...` and
   `go vet ./...` are clean, every file is `gofmt -s` formatted, nothing in the
   repository uses an identifier whose own documentation carries Go's
   `// Deprecated:` marker at the version `go.mod` declares, no vendored source
   is added, `go.mod` gains no `replace`, `exclude` or `ignore` directive,
   build constraints keep selecting the files they selected, no `// +build`
   line comes back, and no file is tagged out of the build. Every import this step needs is from the standard
   library — `os`, `io/fs`, `testing/synctest` — so `go mod tidy` before you
   hand the tree over should change nothing.

## 2. Read these local references

Everything you need is in the container.

* **The toolchain's own documentation**, which is the whole specification for
  requirements 2 and 5: `go doc os.Root`, `go doc os.OpenRoot` and
  `go doc os.OpenInRoot` — the handle, what each of its methods promises
  about the argument it is given, and what it does with a symlink;
  `go doc io/fs` and `go doc io/fs.Glob` for the listing;
  `go doc testing/synctest`, `go doc testing/synctest.Test` and
  `go doc testing/synctest.Wait` — in particular what makes a goroutine
  "durably blocked", which is what decides both when the clock advances and
  when the bubble may end. `go doc time.Tick` says in its own first paragraph
  what requirement 4's first bullet is about. `go help go.mod` describes the
  directive requirement 1 moves, and `go help buildconstraint` the
  constraints requirement 8 freezes. The language specification is at
  `$(go env GOROOT)/doc/go_spec.html` and the memory model, which is what
  requirement 4's third bullet turns on, at `go_mem.html` beside it.
* **The repository**, `/app` — in particular
  `drivers/storage/vfs/storage/vfs_storage.go` (the driver, its `Init`, and
  the two removals), `vfs_storage_vol.go` and `vfs_storage_snap.go` (the path
  formatting, the readers, the writers and the listings — the two files are
  each other, twice), `drivers/storage/vfs/vfs.go` (where the data directory
  and its two subdirectories are named), `api/utils/utils_store.go` (the
  store, the sweeper and the four views), `api/types/types_store.go` (the
  interface those four views implement), `api/utils/utils_store_test.go` (how
  this store is tested today, and the package your new test belongs beside),
  and `api/utils/utils_errors.go` (the not-found error requirement 3 names,
  and the one place its message is written).

  `rg -n --glob '*.go' FileExists` finds all eighteen of the existence checks
  and shows which four are this step's in one pass;
  `rg -n --glob '*.go' 'os\.(Create|Remove|Open|MkdirAll|Stat)|filepath\.Glob' drivers/storage/vfs`
  finds all eighteen filesystem calls under the vfs driver, thirteen of them
  in the package this step is about and five in the executor beside it; and `rg -n --glob '*.go' 'time\.Tick'`
  finds both tickers. There is no `.git` directory in this workspace, so
  `git grep` is not available to you; `rg` and `grep -rn` are.

Useful commands as the repository defines them:

```
go build -o lss ./cli/lss/lss-linux     # the server/CLI binary
./lss -c <config>                       # start it; -l <level> sets logging
go test ./...                           # the repository's own suite
go test ./api/utils/                    # the store's own package
go vet ./...                            # after the directive moves
```

Five facts about this tree that are not defects for you to chase:

* **Run the repository's tests as a non-root user.** libStorage resolves its
  default paths differently for uid 0, and 25 of `api/utils`' path cases
  assert the non-root answers. The account `builder` (uid 1001) exists in
  this image and owns `/app`; `su builder -c 'go test ./...'` is how the
  suite is meant to be run. Everything must also work when run as a non-root user.
* **An `os.Root` is not a chroot and does not try to be.** It refuses a path
  that leaves the directory it was opened on, and a directory that is *inside*
  that root is not leaving it. If you open one root on the data directory and
  address `vol/<id>.json` and `snap/<id>.json` as names under it, then
  `vol/../snap/<id>.json` resolves happily, because `snap` is inside the
  root. Requirement 3's second and third bullets are about exactly that
  boundary, so think about which directory each root is opened on before you
  write the first one.
* **The `mock` build tag has not compiled since 2017**, for three reasons
  none of which are yours. This milestone still covers mock-tagged source —
  it is source in this repository — but you cannot ask the compiler to check
  your work on it.
* **The store has a defect that is not in this step and not yours.** Its
  constructor, given a map of initial values and a lifetime, touches a timer
  map it has not filled yet. No caller in this tree reaches it, no test does,
  and repairing it is not part of this milestone. Leave it as you find it.
* **`vet` will not find requirement 2 for you, in either direction.** Its
  version analyser resolves a method call on a root against the
  package-level function of the same name, so it says nothing about
  `r.MkdirAll` at a 1.24 directive and nothing about `testing/synctest` at
  any directive at all. Nothing in the tree will complain if you raise the
  directive and stop there, and nothing will complain if you use a 1.25
  method at a 1.23 directive either — the tree simply will not build.

Finally, a requirement that is not observable from inside this container:
**your tree must build and work in a clean container that holds the
repository and the Go toolchain and nothing else.** Anything
the driver needs at run time it must get from the repository and the standard
library.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and preserve the supplied source,
  tests and documents. Do not create a prose answer or an alternate entry point.
- Do not cheat by using online solutions or hints specific to this task.
- Run the local checks named above and leave a reproducible handoff for the next
  fresh session.
