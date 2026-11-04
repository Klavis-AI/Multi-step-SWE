# Step 6 — twelve channels that were never channels

## 1. Implement this milestone

`/app` is a Go module at Go 1.22. It logs through the standard library and
routes through the standard library, its loop variables are per-iteration, its
errors lead to their causes, and one declaration does the work that used to
take six. What it still carries is 2015's only way to hand a caller a
collection it does not own.

Twelve functions in this repository are written the same way. Make an
unbuffered channel. Start a goroutine that takes a read lock over the thing
being enumerated, and send every element into the channel. Hand the channel
back. The caller writes `for x := range f()` and it reads well, so it has
never looked wrong.

It is wrong, and not as a matter of style. A caller that stops before the end
abandons that goroutine, blocked forever on a send nobody will ever receive.
The read lock it took is released by a deferred call in a function that never
returns, so it holds that lock for the life of the process — and the next
registration of a driver, a router or a context key waits behind it for just
as long. Four route handlers in this tree return out of the middle of one of
those loops on the error path. The rest are one `break` away.

A function the caller calls has none of that failure mode. It runs on the
caller's own goroutine; a caller that stops asking is a call that returns
false, which the producer sees, and it returns, and its deferred unlock runs.
Go 1.23 is where a function of that shape became something `range` accepts.

Move the language version forward, convert the twelve, and take the two
smaller things that arrive with the same release: the loops that exist only to
copy a map's keys or values into a slice, and four registries whose keys are
the same handful of driver names for the whole life of the process.

When you are done, all of the following must hold.

1. **The module declares Go 1.23.** `go.mod`'s `go` directive is exactly
   `1.23`, and the tree builds and vets at that version.

   1.23 is where a function became rangeable, where `iter` gave the two
   function types that makes legal their names, where `slices` and `maps`
   grew the adapters between a sequence and a collection, and where `unique`
   entered the standard library. The rest of this step is what those changes
   are for. The directive is exact rather than a floor, for the reason it has
   been exact at every milestone here: it states the language semantics the
   whole tree is compiled and vetted under, and the subject of this step is
   what 1.23 makes available, so the number to declare is the one this
   requirement names rather than the highest one the toolchain would accept.

2. **Enumeration is a sequence.** Eleven exported declarations have exactly
   these types when you are done:

   ```go
   // api/context
   func CustomHeaderKeys() iter.Seq[interface{}]
   func CustomLoggerKeys() iter.Seq[interface{}]

   // api/registry
   func ConfigRegs(ctx types.Context) iter.Seq[gofig.ConfigRegistration]
   func StorageExecutors() iter.Seq[types.StorageExecutor]
   func StorageDrivers() iter.Seq[types.StorageDriver]
   func OSDrivers() iter.Seq[types.OSDriver]
   func IntegrationDrivers() iter.Seq[types.IntegrationDriver]
   func Routers() iter.Seq[types.Router]

   // api/server/services
   func StorageServices(ctx types.Context) iter.Seq[types.StorageService]
   func Tasks(ctx types.Context) iter.Seq[*types.Task]

   // api/types, on the TaskTrackingService interface
   Tasks() iter.Seq[*Task]
   ```

   These are the types, not merely types of this shape. A named type of your
   own declared over one of these function types is a different type, and
   these declarations are what callers inside and outside this repository are
   written against.

   The twelfth producer is the task service's own, behind
   `services.Tasks`, and it goes with them. When you are done, no function in
   this repository hands back a channel in order to enumerate something.

   **The channels that stay are the ones a caller waits on rather than reads.**
   A task's completion is reported through one. `types.TaskTrackingService`
   declares `TaskWait`, `TaskWaitAll`, `TaskWaitC` and `TaskWaitAllC` as
   returning `<-chan int` and still does; the package functions and the
   implementation behind them keep `TaskWaitC` and `TaskWaitAllC` as channels,
   while `TaskWait` and `TaskWaitAll` block and return nothing, as they do
   today. So is a running server's failure: `server.Close` and the two
   constructors that hand back a `<-chan error` keep them. None of those is
   enumeration, and none of them is in scope here.

   Six things have to be true of the eleven, and of the twelfth:

   * **A sequence can be walked more than once**, and yields the same
     elements each time. A channel made once, before the caller has asked for
     anything, cannot be.
   * **Walking one starts no goroutine.** The walk runs on the goroutine that
     drives it; the count of goroutines in the process during a walk is the
     count before it.
   * **Walked to the end, each yields everything there is**: every registered
     executor, storage driver, OS driver, integration driver and router;
     every custom key whose flags include the one that producer is for, and
     no others; every configured storage service; every tracked task; every
     registered configuration registration.
   * **A walk that ends early ends the producer.** Nothing is left running
     once the walk returns, and the lock the producer took is released, so
     the next registration of an executor, a driver, a router or a custom key
     returns at once instead of blocking behind an enumeration that has no
     reader left.
   * **`handlers.NewInstanceIDHandler` takes
     `iter.Seq[types.StorageService]`** and returns `types.Middleware`. It is
     the one signature in another package that a converted producer forces,
     because it is the one caller that is handed the services instead of
     ranging over them itself — and it still reads all of them, not the first.
   * **At least four of this repository's packages import `iter`.**

3. **The reverse walk is a standard adapter.** The server's middleware chain
   is assembled by walking a slice of handlers from its end, and it does that
   today through a helper that makes a channel and starts a goroutine to
   count backwards into it — the same construct as the twelve, for a slice
   rather than a registry. `slices.Backward` is what walks a slice from its
   end, so the helper goes, and at least one call to `slices.Backward`
   replaces it.

   The chain still wraps in the order it wraps today: a handler earlier in a
   route's list of middlewares still ends up further out, and the global
   handlers still wrap the route's. Every request reads this order
   on the way in and on the way out.

4. **A collection is collected, not appended to in a loop.** A handful of
   loops in this repository declare a slice, range over a map, append each
   key or each value to the slice, and stop. That is what the adapters in
   `slices` and `maps` are: one names a map's keys or values as a sequence,
   the other turns a sequence into a slice. When you are done, exactly one
   loop of that shape is left.

   The rule for which: a loop goes where the slice's element type is the
   sequence's element type. Where it is not — where the destination is an
   interface the map's element type merely satisfies — the loop stays, because
   a collector collects what it is given and converting on the way is not
   something the standard library does. That is the one that stays; do not
   force it.

   Some of them sort the slice immediately afterwards. Sorting is something
   a collector can do on the way, and the standard library has the spelling
   for it.

   Some of them hand their slice back to a caller, and **an empty collection
   stays an empty collection.** A store with nothing in it answers `Keys()`
   with a slice that is not nil and has length zero, and marshals as `[]`
   and not as `null`. The obvious collector allocates nothing for an empty
   sequence and hands back nil, which is a different JSON document and a
   different answer to `== nil`.

   At least six calls to the sequence adapters in `slices` and `maps` stand
   in the tree when you are done, counted together as one family.

5. **A driver's name is interned where it is used as a key.**
   `api/registry` holds four maps of constructors keyed by a driver's name,
   and eight sites read or write one of them. The names are a handful of
   short strings fixed at init time and compared far more often than they are
   stored, which is what `unique` is for: all four of those maps key on a
   `unique.Handle[string]`, made from the same folded name this package uses
   today.

   A name is still not case-sensitive. A driver registered as `VFS` is still
   found when it is asked for as `vfs`, `Vfs` or `VFS`, at all four
   registries, and an unknown name still comes back as the same error with
   the same field in it.

6. **The observable behavior of the tree you inherit does not change.** Same
   HTTP API, same status codes, same JSON, same CLI output, same log output,
   same configuration keys and defaults, same exit codes. Requirements 2
   through 5 are that sentence spelled out for the places this milestone walks
   straight through; they are not an allowance to change anything else.

   Two of the twelve are read by parts of this tree whose behavior is directly
   observable.
   `registry.Routers` is how the server learns its route table, so every
   route still answers as it answers today — including the query-string
   discrimination, the bare 404 for a method a path does not serve, the
   trailing slash that is not the same route, and the redirect for a path
   that is not in its shortest form. `CustomLoggerKeys` is walked once per
   log line that carries context fields, so a log line is still the same
   bytes: same field order, same quoting, same level names.

   The repository's own tests must pass. Do not delete, skip, weaken or narrow
   them, do not silence a tool with a suppression comment instead of fixing
   what it found, and do not add a discard: a returned value goes into `_` in
   the places this tree already puts one and nowhere new, so a call you write
   yourself deals with everything it hands back -- not only with the error.

7. **The milestones you inherit stay done.**

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

8. **The module ground does not move**, other than the version requirement 1
   raises. `go.mod` still declares `github.com/codedellemc/libstorage`,
   `go build ./...` succeeds, `gofmt -s` reports nothing, `go vet ./...`
   reports nothing, nothing in the repository uses an identifier whose own
   documentation carries Go's `// Deprecated:` marker at the version `go.mod`
   declares, no dependency's source returns to the repository, and `go.mod`
   carries no `replace`, `exclude` or `ignore` directive. Build constraints keep selecting the
   files they selected: no constrained file is deleted, no file's constraint is
   dropped, widened or narrowed, no unconstrained file gains one, and no `// +build` line comes back.

   The module graph moves in one direction at most:
   every import this step adds is from the standard library. `go mod tidy` is
   what settles `go.mod` and `go.sum` afterwards; run it before you hand the
   tree over.

## 2. Read these local references

Everything you need is in the container.

* **The toolchain's own documentation**, which is the whole specification for
  requirements 2, 4 and 5: `go doc iter`, `go doc iter.Seq`, `go doc iter.Seq2`
  — the two function types, and the contract a producer written by hand owes
  its caller — `go doc slices`, `go doc maps` and `go doc unique`. The
  language specification is at `$(go env GOROOT)/doc/go_spec.html`; its "For
  statements" section is what requirement 2 turns on, and it is where the
  three shapes `range` accepts are written down. `go help go.mod` describes
  the directive requirement 1 moves, and `go help buildconstraint` the
  constraints requirement 8 freezes.
* **The repository**, `/app` — in particular `api/registry/registry.go` (six
  of the twelve producers, and the four registries requirement 5 is about),
  `api/context/context_keys.go` (two more, and the lock every context in that
  package reads), `api/server/services/services.go` and
  `api/server/services/services_task.go` (the last four, one of which already
  copies what it is going to hand out before it makes its channel),
  `api/types/types_services.go` (the interface that declares one of them),
  `api/server/handlers/handlers_instanceid.go` (the caller in requirement 2's
  fifth bullet), `api/server/server_middleware.go` (requirement 3), and
  `api/utils/utils_store.go`, `api/types/types_log.go`,
  `api/types/types_localdevices.go`, `api/server/server_http.go`,
  `api/client/client_http.go` and
  `drivers/storage/libstorage/libstorage_driver_funcs.go` (the loops in
  requirement 4).

  `rg -n --glob '*.go' 'make\(chan'` finds the twelve and the helper in
  requirement 3 in one pass, and `rg -n --glob '*.go' 'range registry\.'` and
  its two siblings for `services.` and `context.Custom` find their callers.
  There is no `.git` directory in this workspace, so `git grep` is not
  available to you; `rg` and `grep -rn` are.

Useful commands as the repository defines them:

```
go build -o lss ./cli/lss/lss-linux     # the server/CLI binary
./lss -c <config>                       # start it; -l <level> sets logging
go test ./...                           # the repository's own suite
go vet ./...                            # after the directive moves
```

Four facts about this tree that are not defects for you to chase:

* **Run the repository's tests as a non-root user.** libStorage resolves its
  default paths differently for uid 0, and 25 of `api/utils`' path cases assert
  the non-root answers. The account `builder` (uid 1001) exists in this image
  and owns `/app`; `su builder -c 'go test ./...'` is how the suite is meant to
  be run. Everything must also work when run as a non-root user.
* **Every caller of the twelve but one is already a `range`.** A `range` over
  a channel and a `range` over a sequence are written the same way, so the
  loops themselves do not change when the producers do — which also means the
  compiler will not point you at them, and a producer left half-converted is
  a producer that still builds.
* **The `mock` build tag has not compiled since 2017**, for three reasons none
  of which are yours. This milestone still covers mock-tagged source — it is
  source in this repository — but you cannot ask the compiler to check your
  work on it.
* **The registries' locks are per-registry.** Each of the six producers in
  `api/registry` reads under a lock of its own; the two in `api/context`
  share one lock with each other and with the call that registers a new
  custom key, and the two in `api/server/services` likewise share one with
  the call that adds a service. So the lock one walk holds is the lock that
  registering into that same registry needs -- and the walk that abandons it
  is the one that keeps the registration waiting. Requirement 2's fourth
  bullet is about each of these registries, not about one of them.

Finally, a requirement that is not observable from inside this container:
**your tree must build and work in a clean container that holds the
repository and the Go toolchain and nothing else.**

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and preserve the supplied source,
  tests and documents. Do not create a prose answer or an alternate entry point.
- Do not cheat by using online solutions or hints specific to this task.
- Run the local checks named above and leave a reproducible handoff for the next
  fresh session.
