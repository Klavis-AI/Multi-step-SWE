# Step 3 — errors that can be asked what they are

## 1. Implement this milestone

`/app` is a Go module built by the 1.25 toolchain, with real contexts, real
timeouts and a draining shutdown. Its errors are still 2016's.

Every error this repository constructs comes from `github.com/akutz/goof`, a
small library that predates the standard library's error chains: it gives an
error a message and a bag of named fields, it marshals both to JSON, and it
stores the error that caused this one under a field called `inner`. What it
does not do is let anything walk to that cause. `errors.Is` and `errors.As`
return false on every error in this tree, so the code that has to know what went
wrong asks the only questions it can — it compares the error to a sentinel with
`==`, it compares the text of `Error()`, or it switches on the concrete type in
front of it. Each of those answers correctly today and stops answering correctly
the moment any caller adds a word of context on the way out. The filter that
decides the HTTP status of every error this API returns is one of them.

Give the errors a chain, and make the code ask its questions of the chain.

When you are done, all of the following must hold.

1. **`github.com/akutz/goof` is gone.** No package the repository compiles may
   reach it, by any route — not directly, not through another first-party
   package, and not through an alias or a local re-export. It is not enough to
   satisfy the compiler: no `.go` file in the repository may import it,
   including the three that this platform does not build (the darwin OS driver
   and the two files behind `//go:build mock`). Sixteen compiled packages reach
   it today across twenty-nine files and 139 references. All of that is about
   the source this repository owns: two of this module's own dependencies still
   reach the library themselves, so a requirement on it that survives in
   `go.mod` as an indirect one is correct, and is what `go mod tidy` will
   produce once the last first-party import is gone. What it supplies —
   an error with a message, an error with named fields, the HTTP error envelope
   the server puts on the wire, the JSON both of those marshal to, and the
   decoder the client reads that JSON back with — becomes this repository's own
   code, under `api/types`, where the error types that embed it already live.

2. **An error that was caused by another error leads to it.** Whatever a caller
   has added on the way out, and however many layers of it: `errors.Is` finds
   the cause by identity and `errors.As` finds it by type. This has two halves
   and both are required.

   * Two constructors in `api/utils` are handed the error that caused the one
     they are building — the bad-filter constructor and the batch-process
     constructor — and several call sites hand them one. The error they return
     has to lead to it. An error type that stores a cause reaches the standard
     library's walk only by exposing it: that is what an `Unwrap` method is
     for.
   * Where a call site renders an error into a new message and keeps only the
     text, the cause has to survive that too. `%w` is the verb with which a
     formatted message keeps the value it rendered rather than only its words.
     There are four such places, and each of them has a cause worth keeping.

   A cause that is reachable through one layer and not through two is not
   reachable.

3. **Nothing decides what an error is by looking at the value in hand.** Three
   constructs in this tree do, and all three are the same defect written three
   ways: comparing two error values with `==` or `!=`, recognising an error by
   the message its `Error()` returns, and switching or asserting on an error's
   concrete dynamic type. Nine sites in all. Each of them is a question about
   what an error *is*, asked in a way that only works while nothing is wrapped
   around it — and point 2 is about to wrap things around them. Ask the chain
   instead. Asking whether an error answers some question — an assertion to an
   interface — is not this construct and is not the thing being retired.

   Recognising an error by its message counts however it is written. The one
   site here compares two messages with `==`, but a substring test or a pattern
   match against a message is the same question asked in a way that is worse
   rather than better: it is answered yes by any error whose text happens to
   contain the sentinel's, including one that has nothing to do with it. Do not
   reach point 4's answer that way. A message is not a type, and text that a
   formatting verb rendered from an error is no more a fact about what the
   error is than the text its own method returns.

4. **The HTTP status an error is reported with belongs to the error, not to the
   filter.** Every error a route returns passes through one handler on its way
   out, and that handler currently decides the status by switching on the type
   of the value it was given: this error either is a not-found or it is a 500.
   After this milestone an error must be reported with the same status whether
   it is returned plain or returned with a layer of context wrapped around it.
   A not-found is a 404 either way; the same holds for every other status this
   API produces.

   The statuses themselves do not change, and neither does which error carries
   which. In particular, an error that stands for the outcome of an operation
   keeps its own answer even when the thing that stopped it would have answered
   differently on its own: a batch that partly completed reports a server error
   whatever interrupted it. The status belongs to the error that reports the
   outcome, not to the deepest one reachable beneath it.

5. **The observable behavior of the tree you inherit does not change.** Same
   HTTP API, same status codes, same JSON, same CLI output, same log output,
   same configuration keys and defaults, same exit codes.

   Errors are the API here, so read that literally. The message an error
   returns from `Error()` is what the server puts in the `message` field of an
   error response. The JSON an error marshals to is not an implementation
   detail either: the server puts it in the `error` field of that response, and
   an asynchronous task record carries a failed task's error in a field of its
   own, which `encoding/json` renders with whatever marshaller the error has.
   Both are part of the API, field for field, for every error this API
   produces. A replacement whose output is merely similar — a field renamed, a
   message that gained a prefix, a nested cause that renders as `{}` where it
   used to render as its text — is a different API.

   The repository's own tests must pass. Do not delete, skip, weaken or narrow
   them, do not silence a tool with a suppression comment instead of fixing
   what it found, and do not discard a returned value into `_` to make
   something compile.

6. **The milestones you inherit stay done.** `golang.org/x/net/context` and its
   `ctxhttp` subpackage stay out of the build and the standard library's
   `context` stays live in the five first-party packages that deal in one; the
   merged root-package `New` stays merged; the server's configured read and
   write timeouts keep taking effect at the values configured; stopping the
   server still drains what is in flight, still closes idle connections, and
   still gives up after a bounded grace period; a cancelled request still comes
   back promptly with the caller's own context error. This milestone rewrites
   the import block of twenty-nine files and the error every one of those
   points reports through, which is how they get lost.

7. **The module ground does not move.** `go.mod` still declares
   `github.com/codedellemc/libstorage` and a `go` directive of exactly `1.17` —
   everything this milestone needs has been in the standard library since Go
   1.13.
   `go build ./...` succeeds, `gofmt -s` reports nothing, `go vet ./...`
   reports nothing, no dependency's source returns to the repository, `go.mod`
   carries no `replace`, `exclude` or `ignore` directive, and nothing you write reaches for an
   identifier whose own documentation carries Go's `// Deprecated:` marker.
   Build constraints keep selecting the files they selected: no constrained
   file is deleted, no file's constraint is dropped, widened or narrowed,
   no unconstrained file gains one, and no `// +build` line comes back.

## 2. Read these local references

Everything you need is in the container.

* **The library you are retiring**, in the module cache. `go doc
  github.com/akutz/goof` lists its surface; the source is under
  `$(go env GOMODCACHE)/github.com/akutz/goof@*/`, and it is two files and some
  760 lines. Its `MarshalJSON` methods are the specification for point 5 — they,
  and nothing else, are what produced every error body the API returns today.
* **The repository**, `/app` — in particular `api/types/types_errors.go` (the
  eleven named error types, each of which embeds the library's interface),
  `api/utils/utils_errors.go` (the constructors), `api/server/handlers`
  (the filter that assigns statuses), `api/client/client_http.go` (the decoder
  on the other end of the wire), and `api/server/services/services_task.go`
  (the task record that marshals an error into its own JSON).
* **The toolchain's own documentation**: `go doc errors`, `go doc fmt.Errorf`,
  `go doc errors.Is`, `go doc errors.As`, `go doc errors.Unwrap`. Everything
  this milestone asks for arrived in Go 1.13 and is described there.

Useful commands as the repository defines them:

```
go build -o lss ./cli/lss/lss-linux     # the server/CLI binary
./lss -c <config>                       # start it; -l <level> sets the log level
go test ./...                            # the repository's own suite
```

Three facts about this tree that are not defects for you to chase:

* **Run the repository's tests as a non-root user.** libStorage resolves its
  default paths differently for uid 0, and 25 of `api/utils`' path cases assert
  the non-root answers. The account `builder` (uid 1001) exists in this image
  and owns `/app`; `su builder -c 'go test ./...'` is how the suite is meant to
  be run. Everything must also work when run as a non-root user.
* **The `mock` build tag has not compiled since 2017.** Three separate
  breakages, none of them yours and none of them this milestone's: a struct
  field that became unexported, a field whose type changed from a bool, and a
  test helper that no longer exists. Point 1 still covers the two mock-tagged
  files — they are source in this repository and they import the library — but
  you cannot ask the compiler to check your work on them, and fixing the tag is
  not part of this step. `gofmt` and the AST are what can see them.
* `api/utils/utils_waitfor.go` is unreachable: nothing in the repository calls
  `WaitFor`. Leaving it alone is fine.

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
