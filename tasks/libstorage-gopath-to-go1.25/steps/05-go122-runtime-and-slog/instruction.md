# Step 5 — three dependencies fewer, and a loop that means what it says

## 1. Implement this milestone

`/app` is a Go module built by the 1.25 toolchain, with real contexts, real
timeouts, errors that can be asked what they are, and one declaration where it
used to keep six. What it still carries is 2017's idea of what a program has to
bring with it.

It declares a language version from before the `for` loop was fixed, so two of
its goroutines are written around a rule that no longer exists. It logs through
a third-party library that has not had a release since 2017, through a second
logging facade layered on top of that one, and through the bridge that points
the facade at the library. It matches HTTP requests with a third-party router,
and carries per-request values through its handlers with a third-party
request-scoped store — a job the request's own context has done since Go 1.7.
And it draws its server names from a global generator hand-seeded off the
clock.

Every one of those is a job the standard library now does. Move the language
version forward, and let it.

When you are done, all of the following must hold.

1. **The module declares Go 1.22.** `go.mod`'s `go` directive is exactly
   `1.22`, and the tree builds and vets at that version.

   1.22 is where the `for` loop changed what it declares, where `net/http`'s
   pattern language grew methods and wildcards, and where `math/rand/v2`
   entered the standard library. The rest of this step is what those three
   changes are for. The directive is exact rather than a floor: it states the
   language semantics the whole tree is compiled and vetted under, and the
   subject of this step is what 1.22 makes available, so the number to declare
   is the one this requirement names rather than the highest one the toolchain
   would accept.

2. **No function literal works around a loop variable, and none of them
   reaches past the value it was handed.**

   Through Go 1.21 a `for` statement declared its variables once and every
   iteration assigned to that same storage, so a goroutine that outlived the
   iteration which started it saw whatever the loop had moved on to. Two
   goroutines here are written around that rule in the way the era prescribed:
   the function literal takes a parameter with the same name as the loop
   variable, and the `go` statement passes the loop variable straight into it,
   so that each iteration gets a copy of its own. From 1.22 the loop gives each
   iteration its own variable and that parameter copies nothing. Both literals
   lose the parameter and the argument and read the loop's variable directly.

   One literal in this tree has the shape the other way round, in the handler
   that detaches every volume of every service. It is handed the service it is
   to work on as a parameter, and then in two places ignores that parameter and
   reads the enclosing loop's variable for the same service's name — once for
   the key it files its results under, and once for the error it returns when
   there is no instance ID. It does not run inside the iteration that created
   it: it is enqueued as a task and called later, by the task service, with the
   service the task was enqueued on. Today, with one variable shared by every
   iteration, both of those reads see whichever service the loop has reached by
   the time the task actually runs, and raising the directive papers over that
   rather than fixing it. A closure that is given a value as a parameter uses
   the parameter.

3. **The logging library is the standard library's.**

   `github.com/Sirupsen/logrus` is imported by thirty files in nineteen
   packages, and when you are done no file in this repository imports it. Nor
   does any file import the logging facade layered above it,
   `github.com/codedellemc/gournal`, or the bridge that points that facade at
   that library, `github.com/codedellemc/gournal/logrus` — the bridge is
   imported under the alias `glogrus`, on the line after the facade, in the one
   file that configures both, which is what makes it easy to drop one and keep
   the other. `log/slog` is what does this job now, and when you are done at
   least five of this repository's packages import it directly.

   Copying the old library's own source into this repository under a
   first-party name is not replacing it, and neither is a first-party package
   that re-exports it: both leave the same code compiled into this tree, doing
   the same job, with this repository's name on it.

   Four pieces of this repository's own surface are load-bearing and do not
   move:

   * **`types.FieldLogger`** keeps its name and all twenty-seven of its
     methods, with the signatures they have. Twenty-six of them say nothing
     about which library writes the line. The twenty-seventh, `WithFields`,
     names the type of the map it takes; that type may become a first-party
     one, but it is still a single string-keyed map and the method still
     returns a `types.LogEntry`. `types.Context` still satisfies
     `types.FieldLogger`, because every caller inside this repository and every
     caller outside it logs through that interface.
   * **`types.Level`** keeps its seven constants — `PanicLevel`, `FatalLevel`,
     `ErrorLevel`, `WarnLevel`, `InfoLevel`, `DebugLevel`, `TraceLevel` — and
     keeps their order: `PanicLevel` is the smallest value and the most severe,
     `TraceLevel` is the largest value and the most verbose. The standard
     library's levels run the other way, larger meaning more severe, and
     declaring these in terms of those silently reverses every comparison ever
     written against them, here or anywhere else. Nothing in this tree reads
     the constants, so nothing will tell you.
   * **`LIBSTORAGE_LOGGING_LEVEL`** still selects the level, still by the names
     it accepts today, including both `warn` and `warning`. A name it does not
     accept is still not an error and still does not leave the logger at the
     standard library's own default: it comes up at warn, quietly, the way it
     does today. The `libstorage.logging.level` configuration key and the
     CLI's `-l` flag still do what they do.
   * **The logging that does not go through a context.** Twelve call sites use
     the library's package-level logger instead of a context's: six write a
     line through it — the server's two shutdown errors and its
     `all servers closed`, the configuration compatibility shim's `Debug`, the
     store's expiry sweep, and the schema validator — two take its `io.Writer`
     to dump an HTTP request or response into, two reach into it for the
     formatter and the hooks a fresh context inherits, and two set its level,
     one from configuration and one from the command line. That logger is the
     standard library's own package-level logger from now on — the one
     `log/slog` hands to any caller that asks for a logger without being given
     one — and the lines it writes are lines of this repository's log: they
     land on the same standard error, an operator greps them the same way, and
     they are written in the form below. Until something sets its level it
     keeps the one it has today — info, so an info line written through it
     comes out.

   **A log line looks exactly the way it looks today.** The format is logfmt,
   and all of it is observable: it is what an operator greps, and this
   repository's command-line tool writes it to a terminal on every run.

   * The line opens with `time=`, whose value is the timestamp in RFC 3339 —
     and it is quoted, because that format contains colons.
   * Then ` level=`, and then ` msg=`. The message field is omitted altogether
     when the message is empty.
   * Then the entry's fields, sorted by key.
   * A field keeps its own name even when the line has already used that name.
     The fields a context attaches include one called `time`, holding
     milliseconds rather than a date, so a context's line opens with the
     timestamp and carries that field again in its sorted place, under its own
     name. Nothing is renamed, prefixed or dropped to avoid the collision.
   * Exactly one space goes before each pair, so a line ends with its last
     value and not with a separator.
   * Level names are lower case and spelled out: `debug`, `info`, `warning`,
     `error`, `fatal`, `panic`. The middle one is `warning`, not `warn`,
     whatever the constant that selects it is called.
   * A value is written bare when every character in it is a letter, a digit,
     or one of `-` `.` `_` `/` `@` `^` `+`; otherwise it is written as a Go
     quoted string. An empty value is written bare, as `key=`.
   * A value that is not a string is rendered the way `fmt.Sprint` renders it
     and then judged for quoting on the same terms.
   * `WithError` files the error under the key `error`.
   * The `f` spellings take a format string and its arguments. The `ln`
     spellings keep the two renderings they have today: called on a context
     they run their arguments together, and called on an entry they put a
     space between each pair. Neither carries a newline into the message.

   The levels also still filter the way they filter now: a setting prints the
   messages logged at it and everything more severe, and nothing less severe,
   and the quietest setting is still not silent — an error logged at the error
   setting comes out. The two libraries order their levels in opposite
   directions, so a comparison carried across unchanged is a comparison that
   now means the opposite. There are eighteen such comparisons in the context's
   logging wrapper alone, guarding the `WithField`, `WithFields` and `WithError`
   paths, and they decide whether those lines are printed at all.

4. **The router is the standard library's.** `github.com/gorilla/mux` and
   `github.com/gorilla/context` both go: `net/http` matches the requests, and
   the request itself is what hands back the values matched out of the path.
   Twenty-six routes on twenty-one distinct method-and-path pairs go on
   answering exactly as they answer today.

   1.22's pattern language covers most of that — a pattern names a method and a
   path in one string, and `{name}` is a wildcard. Six things it does not
   cover, every one of them visible from outside this server, and every one of
   them something this server does today:

   * **The query string discriminates.** Eight routes are registered with a
     list of query keys they require, and four of those share the single pair
     `POST /volumes/{service}/{volumeID}`: copy, snapshot, attach and detach
     are told apart by nothing but `?attach`, `?detach` and the rest. No
     pattern can express that, so the code that registers the routes has to. A
     required key declared with an empty value matches whatever value the
     request gives it, and a request that satisfies none of a pair's routes is
     not a request this server answers.
   * **Registration order is the tie-break.** A request that satisfies two of
     those routes at once reaches the one registered first — the one declared
     earlier in the router's own route table.
   * **A method a path does not serve is a 404**: the same
     `404 page not found` an unknown path gets, with no `Allow` header offering
     the methods that would have worked. `PUT`, `PATCH`, `OPTIONS` and `HEAD`
     on a volume all answer that way, and so does `DELETE` on a service. The
     standard library answers 405 with an `Allow` header unless it is told
     otherwise.
   * **A trailing slash is a different path, and it is not served.**
     `/volumes/` is a 404 although `/volumes` is a 200, and the root route
     matches `/` alone rather than everything beneath it.
   * **`%2F` is a separator.** The path is decoded before it is matched, so
     `/volumes/vfs%2Fvfs-000` reaches the route for one volume of one service
     rather than the route for a whole service's volumes. `%23` likewise
     decodes: it is a `#` inside an identifier, and the service names it back
     in the error.
   * **A path that is not already in its shortest form is redirected rather
     than answered.** `/volumes//vfs` is a 301 to `/volumes/vfs`, and an
     identifier holding `..%2F..%2Fetc%2Fpasswd` is decoded, cleaned and
     answered with a 301 to `/etc/passwd` — it reaches no handler in this
     service at all, and where it is sent instead is part of the behaviour.

   Nothing folds case: `/VOLUMES/vfs` is a 404. And a query string that does
   not parse changes nothing — the request still reaches the route its path
   names.

5. **`math/rand` is gone, and `math/rand/v2` is in its place.** One file uses
   it, the server's name generator, which today builds a package-level
   generator over a source seeded off the clock. v2 needs neither: its
   top-level functions are seeded for you and safe for concurrent use, and the
   method that returns a bounded integer is spelled differently there.

   **`crypto/rand` is not `math/rand` and does not move.**
   `api/types/types_uuid.go` imports `crypto/rand` and calls `rand.Read` to
   fill a UUID. The two packages share a last path element and part of an API,
   and the failure mode here is silent: a UUID generator moved onto a
   deterministic source still compiles, still passes, and still produces
   UUIDs. Leave that import and that call exactly as they are.

6. **The observable behavior of the tree you inherit does not change.** Same
   HTTP API, same status codes, same JSON, same CLI output, same log output,
   same configuration keys and defaults, same exit codes. Requirements 3 and 4
   are that sentence spelled out for the two places this milestone walks
   straight through; they are not an allowance to change anything else.

   The repository's own tests must pass. Do not delete, skip, weaken or narrow
   them, do not silence a tool with a suppression comment instead of fixing
   what it found, and do not add a discard: a returned value goes into `_` in
   the places this tree already puts one and nowhere new, so a call you write
   yourself deals with everything it hands back -- not only with the error.

7. **The milestones you inherit stay done.** `golang.org/x/net/context` and its
   `ctxhttp` subpackage stay out of the build, the pair of files that once
   chose between that package and the standard library's stay gone, and the
   standard library's
   `context` stays live in the packages that deal in one; the server's
   configured read and write timeouts keep taking effect; stopping the server
   still drains what is in flight, still closes idle connections, and still gives up after a bounded grace
   period; a cancelled request still comes back promptly with the caller's own
   context error. `github.com/akutz/goof` stays out of the repository entirely;
   errors still lead to their causes through any number of layers, `errors.Is`
   and `errors.As` still find them, nothing decides what an error is by looking
   at the value in hand, and the HTTP status an error is reported with still
   belongs to the error rather than to the filter. The schema document still
   exists exactly once and Go still obtains it at build time; nothing in the
   tree is written twice because it names two types; the type parameters
   that replaced six near-identical declarations stay; `sort` and
   `io/ioutil` stay out of every package in this repository while `slices`,
   `maps` and `cmp` stay in, and a map is still copied by the standard
   library rather than by a loop.

8. **The module ground does not move**, other than the version requirement 1
   raises. `go.mod` still declares `github.com/codedellemc/libstorage`,
   `go build ./...` succeeds, `gofmt -s` reports nothing, `go vet ./...`
   reports nothing, nothing in the repository uses an identifier whose own
   documentation carries Go's `// Deprecated:` marker at the version `go.mod`
   declares, no dependency's source returns to the repository, and `go.mod`
   carries no `replace`, `exclude` or `ignore` directive. Build constraints keep selecting the
   files they selected: no constrained file is deleted, no file's constraint is
   dropped, widened or narrowed, no unconstrained file gains one, and no `// +build` line comes back.

   This milestone does move the module graph — in one direction only.
   Dependencies leave it and none join: every import this step adds is from
   the standard library. `go mod tidy` is what settles `go.mod`
   and `go.sum` afterwards; run it before you hand the tree over, and read what
   it removed.

## 2. Read these local references

Everything you need is in the container.

* **The toolchain's own documentation**, which is the whole specification for
  requirements 3, 4 and 5: `go doc log/slog`, `go doc log/slog.Handler`,
  `go doc log/slog.Record`, `go doc net/http.ServeMux` — whose opening section
  is the pattern language requirement 4 uses — `go doc net/http.Request`,
  `go doc math/rand/v2` and `go doc crypto/rand`. The language specification is
  at `$(go env GOROOT)/doc/go_spec.html`; its "For statements" section is what
  requirement 2 turns on. `go help go.mod` describes the directive requirement
  1 moves, and `go help buildconstraint` the constraints requirement 8 freezes.
* **The source of the three dependencies you are retiring**, unpacked in the
  module cache under `$(go env GOMODCACHE)`. The old logger's text formatter is
  the thing requirement 3 describes, and reading it is faster than inferring it
  from output.
* **The repository**, `/app` — in particular `api/context/context_logger.go`
  (the wrapper every caller logs through, and the eighteen guards),
  `api/types/types_logger.go` and `api/types/types_context.go` (the interface
  and the levels requirement 3 pins), `api/server/server.go` (the facade and
  its bridge, and one of the two goroutines),
  `api/server/services/services_task.go` (the other), `api/server/server_http.go`
  (where the routes are registered) and the six routers under
  `api/server/router/`, each of which declares its own routes in its own file
  (which routes carry query discriminators, and the order they are declared
  in),
  `api/server/router/volume/volume_routes.go` (the closure in requirement 2),
  `api/server/server_names.go` and `api/types/types_uuid.go` (the two `rand`s).

Useful commands as the repository defines them:

```
go build -o lss ./cli/lss/lss-linux     # the server/CLI binary
./lss -c <config>                       # start it; -l <level> sets the log level
go test ./...                           # the repository's own suite
go vet ./...                            # after the directive moves
```

Four facts about this tree that are not defects for you to chase:

* **Run the repository's tests as a non-root user.** libStorage resolves its
  default paths differently for uid 0, and 25 of `api/utils`' path cases assert
  the non-root answers. The account `builder` (uid 1001) exists in this image
  and owns `/app`; `su builder -c 'go test ./...'` is how the suite is meant to
  be run. Everything must also work when run as a non-root user.
* **A route's query discriminators are a flat slice of alternating key and
  value**, the way the router that is leaving took them, and `types.Route`
  already exposes that slice. Most of them ask for a key with an empty value,
  which is what a bare flag in a query string is.
* **The `mock` build tag has not compiled since 2017**, for three reasons none
  of which are yours. This milestone still covers mock-tagged source — it is
  source in this repository, and it imports the logger like everything else —
  but you cannot ask the compiler to check your work on it.
* **The facade configured in `api/server/server.go` is written to and never
  read.** It puts a level and an appender into the context under the facade's
  own keys, and nothing in this repository ever logs through it. Requirement 3
  retires it; you do not have to replace what it did.

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
