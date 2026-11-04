# Step 4 — one declaration where the tree keeps six

## 1. Implement this milestone

`/app` is a Go module built by the 1.25 toolchain, with real contexts, real
timeouts and errors that can be asked what they are. Its *library* is still
2016's: it declares a language version from before generics existed, so it
cannot say "the same code for any type" and has to write the code again for
each type instead.

Four things in this tree pay that bill. A Makefile rule pastes an 18 kB JSON
document into a Go source file so that a program can hold it, and nothing
reproduces the paste. Ten declarations exist in three groups whose members are
the same code with a different type written in it. Sorting and map-copying are
hand-rolled here because the standard library had nothing to offer a repository
of this age — three `sort.Interface` adapters for three element types, and
loops that copy one map into another key by key. And every file that reads or
writes a whole file reaches for a package the standard library has been telling
callers to stop using since Go 1.19.

Raise the language version, and let the language and its library do the work
this tree does by hand.

When you are done, all of the following must hold.

1. **The module declares Go 1.21.** `go.mod`'s `go` directive is exactly
   `1.21`, and the tree builds and vets at that version.

   1.21 is where `slices`, `maps` and `cmp` enter the standard library, and it
   is what the rest of this step needs. The directive is exact rather than a
   floor: it states the language semantics the whole tree is compiled and
   vetted under, and the subject of this step is the 1.21 library, so the
   number to declare is the one this requirement names rather than the highest
   one the toolchain would accept.

   Raising the version has one consequence beyond the packages it unlocks. A
   deprecation is judged against the version the module itself declares, so an
   identifier the standard library retired between 1.17 and 1.21 is invisible
   on this tree today and reported on it the moment the directive moves.
   Requirement 5 is the one that matters here — `io/ioutil` has carried Go's
   `// Deprecated:` marker since 1.19 — and requirements 1 and 5 are worth
   doing together rather than one at a time.

2. **The API schema exists once, and Go obtains it at build time.**

   `libstorage.json` at the root of the repository is the schema document for
   this API: 18 202 bytes, and the source of every `$ref` the server validates
   against. `api/utils/schema/schema_generated.go` is 540 lines whose entire
   content is those same 18 202 bytes inside a Go raw string literal, produced
   by the rule in `api/utils/schema/Makefile` and byte-identical to the
   document because somebody ran `make` once, in 2017. Nothing regenerates it;
   nothing checks that it still agrees; it agrees by luck.

   After this milestone the document appears in the tree exactly once, byte for
   byte, and the Go source that needs it pulls it in when the package is
   compiled rather than restating it. The generated file is gone and so is the
   rule that wrote it.

   This is a requirement about the property, not about a path: where the one
   copy lives and what Go type it arrives as are yours to choose. Two facts
   constrain the choice and are worth knowing before you start. A file embedded
   into a package must live in or under that package's own directory, so the
   document cannot stay at the repository root and be embedded from
   `api/utils/schema`. And the root package cannot host the embed either — it
   imports `api/server`, which reaches `api/utils/schema`, so an embed there
   and a use here is an import cycle. The document has exactly one reference in
   the repository outside documentation (the Makefile rule above), and the
   pasted constant has exactly one consumer, so moving it is a small change
   that touches nothing else.

3. **Nothing in this tree is written twice because it names two types.**

   Ten declarations, in three groups, have the same body once you erase the
   types written into it. A type parameter is what removes the reason each
   group exists, and each group wants a different shape of answer:

   * **Six getters in `api/utils/utils_store.go`** — `GetStore`,
     `GetStringSlice`, `GetIntSlice`, `GetBoolSlice`, `GetMap` and
     `GetInstanceID`. Each one is *look the key up, and give it back if it is a
     `T`, or give back the zero value if it is not*, with `T` the only thing
     they disagree about.
   * **Two readers under `drivers/storage/vfs/storage/`** — `readVolume` and
     `readSnapshot`, identical but for the struct they decode JSON into.
   * **Two helpers in `api/utils/utils_sort.go`**, which requirement 4 reaches
     as well.

   Go has no generic methods, so a 22-method interface cannot become generic
   and `types.Store` stays exactly as it is. The generic declarations are
   ordinary functions; an interface method that has one becomes a call to it.
   Do not spend time trying to parameterize the interface — the language will
   not let you, and the interface is not the duplication.

   **The rest of `utils_store.go` is a trap, and this is the part of the step
   most likely to be got wrong.** The file has a second family of getters that
   *look* like the first and are not: `GetString`, `GetBool`, `GetInt`,
   `GetInt64` and their four `*Ptr` variants do not assert a type, they convert
   one. A single `v, _ := s.Get(k).(T)` applied to the whole file compiles,
   reads beautifully, and silently deletes all of that. Some of what it would
   delete, all of it present today and all of it reachable from this API:

   * `GetString` renders whatever is stored through `%v`, so a stored `42`
     comes back as `"42"` and a stored `*string` comes back as a hex address.
   * `GetBool`, `GetInt` and `GetInt64` parse a stored string, and answer the
     zero value when the parse fails.
   * `GetIntPtr` and `GetInt64Ptr` return a nil pointer when the parse fails;
     `GetBoolPtr`, its neighbour in the same file, returns a non-nil pointer to
     `false`. They disagree, and both answers are relied upon.
   * `GetStringPtr` given a `string` returns a pointer to its own copy of it.

   This store is populated from configuration files and from JSON, so a value
   whose dynamic type is not the one being asked for is the common case here
   rather than the exotic one. Collapse what is genuinely one function written
   six times; leave what converts alone.

4. **Ordering and copying come from the standard library.**

   * The three `sort.Interface` adapter types in this repository, and the three
     `sort.Sort` calls that use them, go. `slices` sorts, and where an order
     has to be supplied rather than assumed, `cmp` is where the comparison
     comes from. Two of the adapters are in `api/utils/utils_sort.go` and one
     is in `api/types/types_localdevices.go`; the third is load-bearing, in a
     `MarshalText` whose sorted key order goes out on the wire. None of the
     three adapter types is named outside the file that declares it, so all
     three can be deleted rather than merely bypassed — but the two exported
     wrapper *functions* around them are this repository's API and one of them
     has a caller, so keep those and change what is inside them. When you are
     done, no package in this repository imports `sort`.
   * The unconditional map-copy loops — `for k, v := range src { dst[k] = v }`
     and nothing else in the body — become `maps.Copy`. Find them by the shape
     rather than by a count: **five of them** are in `api/context`,
     `api/server/httputils` and `api/server/handlers`, and the tree can hold
     more than those five — a file this platform does not build has the shape
     too, and so can a file that was written into this tree recently. Convert
     every one you can compile. One loop that has the shape is **not** one of
     them, and the compiler will tell you which: `maps.Copy` copies between maps
     with the *same* value type, and `NewStoreWithVars` in
     `api/utils/utils_store.go` copies a `map[string]string` into a
     `map[string]interface{}`. A converting copy is not a copy. Leave it as a
     loop.

   `maps` is thin at 1.21 — `Clone`, `Copy`, `DeleteFunc`, `Equal`,
   `EqualFunc`, and nothing else. Anything else that comes to mind is not in
   this version of the package and will not compile.

   Not required, but in scope if you want it: `slices.IndexFunc` and
   `slices.ContainsFunc` fit the three hand-written search loops in
   `api/server/auth/auth_token.go` and `drivers/os/linux/linux_mount.go`.

5. **`io/ioutil` is gone.** Ten files across six packages import it, one call
   site each, five distinct functions between them — `ReadAll`, `ReadFile`,
   `WriteFile`, `TempDir` and `TempFile`. Every one has a replacement in `io`
   or `os` with the same signature and the same behaviour; two of them are
   under a different name.

   No `.go` file in this repository may import `io/ioutil` when you are done.
   Giving the package a first-party name and importing that instead is not
   retiring it: the deprecated package is still compiled into this repository,
   the deprecated functions are still the ones being called, and the file that
   re-exports them carries every finding the ten files used to carry between
   them.

6. **The observable behavior of the tree you inherit does not change.** Same
   HTTP API, same status codes, same JSON, same CLI output, same log output,
   same configuration keys and defaults, same exit codes.

   Two places where that is narrower than it sounds, because this milestone
   walks straight through both. The store's conversions are behavior — see
   requirement 3, and read `GetString` before you replace it. And
   `LocalDevices.MarshalText` sorts its keys before writing
   `DRIVER=k::v,k::v`: that ordering is on the wire, and a sort that is no
   longer stable in the same way is a changed API even though no signature
   moved.

   The repository's own tests must pass. Do not delete, skip, weaken or narrow
   them, do not silence a tool with a suppression comment instead of fixing
   what it found, and do not discard a returned value into `_` to make
   something compile.

7. **The milestones you inherit stay done.** `golang.org/x/net/context` and its
   `ctxhttp` subpackage stay out of the build, the pair of files that once
   chose between that package and the standard library's stay gone, and the
   standard library's
   `context` stays live in the five first-party packages that deal in one; the
   server's configured read and write timeouts keep taking effect; stopping the
   server still drains what is in flight, still closes idle connections, and still gives up after a bounded
   grace period; a cancelled request still comes back promptly with the
   caller's own context error. `github.com/akutz/goof` stays out of the
   repository entirely; errors still lead to their causes through any number of
   layers, `errors.Is` and `errors.As` still find them, nothing decides what an
   error is by looking at the value in hand, and the HTTP status an error is
   reported with still belongs to the error rather than to the filter.

8. **The module ground does not move**, other than the version requirement 1
   raises. `go.mod` still declares `github.com/codedellemc/libstorage`,
   `go build ./...` succeeds, `gofmt -s` reports nothing, `go vet ./...`
   reports nothing, nothing in the repository uses an identifier whose own
   documentation carries Go's `// Deprecated:` marker at the version `go.mod`
   declares, no dependency's source returns to the repository, and `go.mod`
   carries no `replace`, `exclude` or `ignore` directive. Build constraints keep selecting the
   files they selected: no constrained file is deleted, no file's constraint is
   dropped, widened or narrowed, no unconstrained file gains one, and no `// +build` line comes back.

   Every import this milestone adds and every import it removes is from the
   standard library, so the module graph does not move. That is the property
   being asked for; `go mod tidy` leaving `go.sum` byte for byte as it found it
   is how you can confirm it for yourself before handing the tree over.

## 2. Read these local references

Everything you need is in the container.

* **The toolchain's own documentation**, which is the whole specification for
  requirements 4 and 5: `go doc slices`, `go doc maps`, `go doc cmp`,
  `go doc embed`, `go doc io`, `go doc os`, and `go doc io/ioutil`, whose every
  entry names the function that replaced it. The language specification,
  including the sections on type parameters and type inference, is at
  `$(go env GOROOT)/doc/go_spec.html`; `go help go.mod` describes the directive
  requirement 1 moves, and `go help buildconstraint` the constraints
  requirement 8 freezes.
* **The repository**, `/app` — in particular `api/utils/utils_store.go` (both
  getter families, and `types.Store` in `api/types/types_store.go` beside it),
  `api/utils/schema/` (the generated paste, the rule that made it, and the one
  line that consumes it), `api/utils/utils_sort.go` and
  `api/types/types_localdevices.go` (the adapters), and
  `drivers/storage/vfs/storage/` (the two readers).

Useful commands as the repository defines them:

```
go build -o lss ./cli/lss/lss-linux     # the server/CLI binary
./lss -c <config>                       # start it; -l <level> sets the log level
go test ./...                            # the repository's own suite
go vet ./...                             # after the directive moves
```

Four facts about this tree that are not defects for you to chase:

* **Run the repository's tests as a non-root user.** libStorage resolves its
  default paths differently for uid 0, and 25 of `api/utils`' path cases assert
  the non-root answers. The account `builder` (uid 1001) exists in this image
  and owns `/app`; `su builder -c 'go test ./...'` is how the suite is meant to
  be run. Everything must also work when run as a non-root user.
* **`api/utils/schema` still needs a `Makefile` after its rule is gone.** The
  repository's `test.mk` recurses into that directory for `build-tests`, `test`
  and `clean`, and a missing file there breaks `make test` for the whole tree.
  Every other framework package in this repository has exactly one line in its
  Makefile; look at `api/utils/filters/Makefile` for the shape.
* **The `mock` build tag has not compiled since 2017**, for three reasons none
  of which are yours. Requirement 5 still covers mock-tagged source — it is
  source in this repository — but you cannot ask the compiler to check your
  work on it.
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
