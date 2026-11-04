# Step 8 — a server that has been stopped is a server that has stopped

## 1. Implement this milestone

`/app` is a Go module at Go 1.25. It is the last step of this line, and by
now almost everything in it is written the way the current language is
written: its modules are modules, its context reaches every call, its errors
carry their causes, its containers are generic, its logging is structured,
its enumeration is sequences, and its filesystem access is rooted.

What is left is the thing none of that touched: **what the program
leaves running.**

Start a server in this repository and stop it, and by every observable
measure it has stopped — the listener is closed, the socket is gone, `Close`
has returned. Start and stop it a hundred times in one process and the
process is holding a hundred goroutines, each parked forever on a channel
nobody will ever send to, each holding a storage service, the driver that
service initialised, and the configuration behind both. Nothing reports
this. Nothing fails. The program simply never gets smaller.

**Where it comes from.** Every storage service, in its initialisation,
creates an unbuffered channel of pending tasks and starts a goroutine to
take work off it. That goroutine has no way of being told that there will
be no more work: it waits, and waiting is all it can do. There is no code
path anywhere in the repository that shuts a service down — the server's
own shutdown walks its endpoints and its log writers and then returns, and
the package that owns the services has no shutdown at all, only an
initialisation.

The same channel has a second edge. Enqueuing a task starts a goroutine
whose whole body is a send on that unbuffered channel. While a consumer is
running, that send completes. Once the consumer is gone it never does, and
the sender joins it — so whatever ends the one has to account for the
other.

**What has to become true.**

1. **A storage service has a lifetime, and something ends it.** After a
   server has been stopped, the goroutines that server's services started
   are no longer running — not shutting down, not blocked on a send that
   will be serviced later, not running. This must hold for one server and it
   must hold for many, and it must hold however they are stopped: one after
   another, or all at once from as many goroutines. Concurrent shutdown must
   be safe as well as complete: tearing a shared structure down from two
   goroutines at once is a panic, and a panic is not a shutdown. Work handed
   to a service that has already stopped must be a no-op, not a crash and
   not a goroutine that waits forever to deliver it.

2. **Express that lifetime with `context`.** The package that runs a
   server's tasks must reach the standard library's `context` — not a
   bespoke quit channel, not a boolean guarded by a mutex. This repository
   already speaks `context` everywhere else; a cancellation that cannot be
   chained to a deadline and cannot be handed to anything that already takes
   one is not the same facility, and this is the last place in the server
   that shuts something down without it.

   Note that this package already imports a *different* package named
   `context` — the repository's own, at `api/context`. The requirement is
   the standard library's.

3. **A running server is still a running server.** Everything above is
   satisfied by a server that starts nothing, and by one that tears itself
   down so eagerly that it stops answering. Neither is the milestone. While
   a server is running it must still accept connections on the address it
   was configured with, and it must still run the tasks that are given to
   it — and run them the way it runs them today: handing a task over comes
   back straight away rather than when the work has finished, and the work
   itself runs one unit at a time. A service that runs each task on whatever
   goroutine handed it over, or that starts a fresh goroutine for every task
   and lets each one end by itself, has nothing left to leak and has also
   stopped being a service that runs work: the first has no concurrency, the
   second has no limit on it, and neither one has a lifetime.

4. **The exported API does not change.** This is a milestone about
   lifetimes, not about signatures. The exported surface of
   `api/server`, `api/types`, `api/utils`, `api/context`,
   `api/utils/config`, `api/registry` and the `vfs` storage driver must
   remain callable exactly as it is today — same names, same parameters,
   same results. You may add to it; you may not re-sign or rename what is
   there.

   This one is worth taking literally. There is a second goroutine leak in
   this tree that you will find while you are working — a helper that waits
   for a function with a timeout, returns when the timeout wins, and leaves
   the function running with no way to stop it. It cannot be fixed without
   changing its signature, because a function that takes no context cannot
   be told to give up. **Leave it alone.** It is a real defect and it is not
   this milestone.

5. **Keep what the tree already has.** Nothing it has retired comes back,
   and nothing it has introduced is withdrawn to make this milestone easier.
   In particular:

   * **Behaviour.** The observable behavior of the tree you inherit does not
     change: same HTTP API, same status codes, same JSON, same CLI output,
     same log output, same configuration keys and defaults, same exit codes.
     The repository's own tests pass in every package.
   * **The module ground.** `go.mod` declares
     `module github.com/codedellemc/libstorage` with a `go` directive of
     exactly `1.25` and no `replace`, `exclude` or `ignore` directive.
     `go build ./...`, `go vet ./...` and `gofmt -s` stay silent; nothing in
     the repository uses an identifier whose own documentation carries Go's
     `// Deprecated:` marker; no dependency's source is in the repository.
     Build constraints are written in the `//go:build` form with no
     `// +build` line beside them, and keep selecting the files they select
     today: no constrained file is deleted, no constraint is dropped, widened
     or narrowed, and no unconstrained file gains one. The repository's own
     tests are not deleted, skipped, weakened or narrowed — the number of test
     files, test functions, specs and assertions does not fall — no tool is
     silenced with a suppression comment, and a returned value goes into `_`
     only in the places this tree already puts one.
   * **Contexts.** `golang.org/x/net/context` and its `ctxhttp` subpackage
     stay out of the build, the pair of files that once chose between that
     package and the standard library's stay gone, and the standard library's
     `context` stays live in the packages that deal in one. The server's
     configured read and write timeouts keep taking effect; stopping the
     server still drains what is in flight, still closes idle connections, and still gives up after a bounded
     grace period; a cancelled request still comes back promptly with the
     caller's own context error.
   * **Errors.** `github.com/akutz/goof` stays out of the repository
     entirely. Errors still lead to their causes through any number of layers,
     `errors.Is` and `errors.As` still find them, an error type still answers
     `Unwrap`, nothing decides what an error is by comparing its text or its
     identity or by asserting it to a concrete type, no cause is dropped on
     the way out, and the HTTP status an error is reported with still belongs
     to the error rather than to the filter that writes it.
   * **Generics and the standard library.** The schema document still exists
     exactly once and Go still embeds it at build time; nothing in this tree
     is written twice because it names two types; the type parameters stay;
     `sort` and `io/ioutil` stay out of every package here while `slices`,
     `maps` and `cmp` stay in, and a map is still copied by the standard
     library rather than by a loop. `crypto/rand` stays exactly where it is.
   * **The 1.22 runtime and `slog`.** No function literal works around a loop
     variable, and none of them reaches past the value it was handed.
     `github.com/Sirupsen/logrus`, `github.com/codedellemc/gournal`, its
     `logrus` bridge, `github.com/gorilla/mux`, `github.com/gorilla/context`
     and `math/rand` all stay gone; `log/slog` stays in at least five packages
     and `math/rand/v2` in the one that needs it; and the values matched out
     of a request's path still come from the request itself.
   * **Sequences.** No function in this repository hands back a channel in
     order to enumerate something; the registry, context and service
     producers keep their `iter.Seq` types, can be walked more than once,
     start no goroutine, yield everything there is, and end the producer and
     release its lock when a walk ends early; at least four packages import
     `iter`; the middleware chain is still walked from its end with
     `slices.Backward`; a map's keys or values are still collected with the
     `slices` and `maps` adapters, with exactly one append loop of that shape
     left; and the four driver registries in `api/registry` still key on
     `unique.Handle[string]`.
   * **Rooted files and simulated time.** `drivers/storage/vfs/storage` still
     reaches its documents only through an `os.Root`: an identifier that walks
     upwards out of its data directory still fails without reading or
     unlinking anything on the other side, an identifier that is simply not
     there or that the filesystem refuses still reports what it reports
     today, and no `os.Create`, `os.Remove` or `filepath.Glob` call, and none
     of that package's former existence checks, comes back. The key/value
     store in `api/utils` still forgets an expired entry when it is asked
     about it, its four views still agree, it keeps no sweeper goroutine, and
     one `time.Tick` remains in the tree; its own test still runs in
     simulated time under `testing/synctest`. The three `time.Sleep` calls
     that stand outside a bubble are all in non-test code, and no further
     call outside a bubble joins them, in a test or anywhere else.

6. **No data race, under a seeded, repeated schedule.** The lifetime has to
   be given without a data race in the sense of the Go memory model, and the
   race detector the toolchain ships is what decides it: anything it reports
   as `WARNING: DATA RACE` is a failure, wherever it is reported. Two runs,
   both with the test order shuffled by the fixed seed `20261005`, must
   report none:

   * the repository's own tests, `go test -race -shuffle=20261005 ./...` —
     here only the race reports matter; whether every test passes is
     requirement 5's business; and
   * a server put through requirement 1 three times over in one process
     (`go test -race -count=3 -shuffle=20261005`): one server started, given
     work and stopped, several servers stopped one after another, and several
     stopped all at once from as many goroutines. Requirement 1 has to hold on
     every one of the three repetitions with the race detector watching, not
     only on the first.

## 2. Read these local references

Everything you need is in the container.

* **The toolchain's own documentation.** `go doc context`,
  `go doc context.WithCancel` and `go doc context.CancelFunc` — in
  particular what is guaranteed about a cancelled context's `Done` channel,
  which is what makes requirement 1 checkable rather than hopeful.
  `go doc sync.WaitGroup`, for the difference between assuming a goroutine
  has finished and knowing it has. The memory model, which is what "no
  longer running" has to mean precisely, is at
  `$(go env GOROOT)/doc/go_mem.html`.
* **The repository**, `/app`. The subject of this step is
  `api/server/services` — what a service starts, what starts a service, and
  what happens to either when a server goes away — together with the
  server's own shutdown path in `api/server`. Not every goroutine in that
  package is a problem; several are bounded and return on their own, and
  telling those apart from the ones that do not is part of the work.

  There is no `.git` directory in this workspace, so `git grep` is not
  available to you; `rg` and `grep -rn` are.

* **`vet` will not find any of this for you.** There is no analyser for a
  goroutine that outlives its caller, and none for a wait that nothing will
  ever end. The tree builds and vets silently today and it will build and
  vet silently if you change nothing.
* **Run the repository's tests as a non-root user.** libStorage resolves its
  default paths differently for uid 0, and 25 of `api/utils`' path cases
  assert the non-root answers. The account `builder` (uid 1001) exists in
  this image and owns `/app`; `su builder -c 'go test ./...'` is how the
  suite is meant to be run. Everything must also work when run as a non-root user.

Finally, a requirement that is not observable from inside this container:
**your tree must build and work in a clean container that holds the
repository and the Go toolchain and nothing else.** Anything
the server needs at run time it must get from the repository and the
standard library.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and preserve the supplied source,
  tests and documents. Do not create a prose answer or an alternate entry point.
- Do not cheat by using online solutions or hints specific to this task.
- Run the local checks named above and leave a reproducible handoff for the next
  fresh session.
