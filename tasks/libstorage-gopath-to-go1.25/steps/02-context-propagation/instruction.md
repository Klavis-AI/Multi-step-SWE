# Step 2 — context that is actually honored

## 1. Implement this milestone

`/app` is now a Go module built by the 1.25 toolchain, and it already passes a
`types.Context` through several hundred call sites. What it does not do is
*honor* those contexts. The context type itself is still
`golang.org/x/net/context` — the pre-1.7 shim that the standard library
absorbed in 2016 — the client reaches the network through that package's
`ctxhttp` helper, the two HTTP timeouts the server documents as configuration
are read by nobody, and stopping the server severs whatever it was in the
middle of serving.

Make the contexts real: the standard library's, with the deadlines and the
shutdown the repository already claims to support.

When you are done, all of the following must hold.

1. **The `golang.org/x/net/context` shim is gone.** No package the repository
   compiles may reach `golang.org/x/net/context`, or its `ctxhttp` subpackage,
   by any route — not directly, not through another first-party package, and
   not through an alias or a local re-export that keeps the old path in the
   build. The standard library's `context` takes its place in the five
   first-party packages that deal in a context type today: the root package,
   `api/types`, `api/context`, `api/server` and `client`. Every exported
   signature that deals in contexts keeps the shape it has: what accepts a
   standard library context still accepts one, what returns a `types.Context`
   still returns one, and `types.Context` still satisfies the standard
   `context.Context`.

2. **The pre-Go-1.7 twin is gone.** The root package carries two copies of the
   same `New` function, `libstorage_go17.go` and `libstorage_pre_go17.go`,
   selected by a `go1.7` build constraint and differing by one import line. No
   toolchain that can build this module will ever select the `!go1.7` copy.
   Both files go, replaced by a single unconstrained one; the function it
   declares keeps its name, its signature and its behavior.

3. **A cancelled or expired context still stops an in-flight request
   promptly.** The client honors cancellation today only because `ctxhttp` is
   doing it; point 1 takes `ctxhttp` away. After this milestone a request whose
   context is cancelled, or whose deadline passes, must still return a context
   error within a short bounded time of that happening — not when the server
   eventually answers — and the server must see the request go away.

4. **The server's configured HTTP timeouts take effect.** Two keys already
   exist, are already declared in `api/types`, and are already registered with
   a default of 300: `libstorage.http.readTimeout` and
   `libstorage.http.writeTimeout`. Their values are **whole seconds**.
   Today nothing reads them, so setting either changes nothing. After this
   milestone:

   * a server configured with a small read timeout must cut off a client that
     opens a request and then stops sending it, rather than holding the
     connection indefinitely;
   * a server configured with a small write timeout must cut off a response
     the handler is too slow to produce;
   * a server left on the defaults must not do either of those things to an
     ordinary request, and must still serve one normally.

   The values must come from the configuration, at the value configured. A
   fixed duration compiled into the server is not this requirement. Whatever
   whole-second values these two keys are given, the server has to hold to
   them, and not only to the values you happened to try.

5. **Stopping the server drains what it is serving, and bounds how long it
   waits.** Closing a `types.Server` today drops the listeners and lets the
   process exit cut whatever was still in flight. After this milestone,
   closing one must:

   * let requests already in flight run to completion, and not return until
     they have;
   * close the connections that are merely open and idle rather than leaving
     them to the client;
   * bound that waiting, so that one request which never finishes cannot keep
     the close — and therefore the process — alive forever. Choose the grace
     period yourself: ten seconds at the least, so that a request which is
     merely slow still gets to finish, and about a minute at the outside.

6. **A normal stop is not reported as a server failure.** The server's accept
   loop ends with an error every time, including when the stop is the thing
   that ended it, and the code that decides whether that error is a failure
   currently does so by looking for a substring of the message. That substring
   is the 2017 spelling of one particular cause. Decide it instead from the
   sentinel values the standard library documents for exactly this, so that
   every ordinary way of ending the accept loop is recognised as ordinary. Get
   this wrong and the repository's own suite will hang rather than fail, which
   is a slower thing to find than a failure would be. `go test` prints a
   goroutine dump when it gives up, and the dump names the lines that are
   waiting on each other.

7. **The observable behavior of the tree you inherit does not change.** Same
   HTTP API, same JSON, same CLI output, same log output, same configuration
   keys and defaults, same exit codes. Points 4 and 5 add behavior that was
   configurable-but-absent; they do not change what an ordinary request or an
   ordinary run does. The repository's own tests must pass — do not delete,
   skip, weaken or narrow them, do not silence a tool with a suppression
   comment instead of fixing what it found, and do not discard a returned value
   into `_` to make something compile.

8. **The module ground does not move.** `go.mod` still declares
   `github.com/codedellemc/libstorage` and a `go` directive of exactly `1.17`.
   `go build ./...` succeeds, `gofmt -s` reports nothing, `go vet ./...`
   reports nothing, no dependency's source returns to the repository, `go.mod`
   carries no `replace`, `exclude` or `ignore` directive, and nothing you write reaches for an
   identifier whose own documentation carries Go's `// Deprecated:` marker.
   Build constraints keep selecting the files they selected, with the single
   exception point 2 asks for: no other constrained file is deleted, no file's
   constraint is dropped or changed, no unconstrained file gains one, and no
   `// +build` line comes back.

## 2. Read these local references

Everything you need is in the container.

* **The repository**, `/app` — in particular `api/context`, `api/types`,
  `api/server`, `api/client`, `client`, the two root-package twins, and the
  configuration registered in `imports/config`.
* **The toolchain's own documentation**: `go doc context`, `go doc net/http`,
  `go doc net/http.Server`, `go doc errors`, `go doc net`. The standard library
  gained everything this milestone needs between Go 1.7 and Go 1.16, and the
  shim package's own documentation says so.
* The configuration keys and their defaults are declared in
  `api/types/types_config.go` and registered in `imports/config`;
  `.docs/user-guide/config.md` is where the server's configuration is
  documented generally.

Useful commands as the repository defines them:

```
go build -o lss ./cli/lss/lss-linux     # the server/CLI binary
./lss -c <config>                       # start it; -l <level> sets the log level
go test ./...                            # the repository's own suite
```

Two facts about this tree that are not defects for you to chase:

* **Run the repository's tests as a non-root user.** libStorage resolves its
  default paths differently for uid 0, and 25 of `api/utils`' path cases assert
  the non-root answers. The account `builder` (uid 1001) exists in this image
  and owns `/app`; `su builder -c 'go test ./...'` is how the suite is meant to
  be run. Everything must also work when run as a non-root user.
* `api/utils/utils_waitfor.go` is unreachable: nothing in the repository calls
  `WaitFor`. It looks like it belongs to this milestone and it is not part of
  it; leaving it alone is fine.

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
