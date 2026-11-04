# TEST_INTERFACE — step 13 of 13 (certificate-transparency-go v1.3.3)

This is the public contract the hidden verifier holds you to. Everything the
verifier needs in order to reach your code — a module path, an import path, a
symbol name, a program name, a flag, an output format, an exit status, a build
command — is stated here, or in a page of `/app/ct-spec/` that
`/app/ct-spec/README.md` points at. The verifier may combine these interfaces
in ways this file does not enumerate; it will not use an interface this file
does not name.

*Semantics* come from `/app/ct-spec/` and the RFCs shipped inside it. This file
describes the plumbing: how the verifier reaches your implementation, what it
builds, and how a result is decided.

---

## 1. Workspace layout and build

    /app/
      go.mod, go.sum        benchmark-owned build manifest (section 2)
      ct-spec/              the documentation corpus, PROFILE.md, README.md, SHA256SUMS
      TEST_INTERFACE.md     this file
      WORKSPACE_HANDOFF.md  what carries from step to step
      timer.sh              remaining budget for this step
      <everything else>     your implementation: packages and `package main`
                            directories, laid out as section 5 requires

* The implementation language is **Go**. The verifier compiles your tree with
  the Go toolchain baked into its own image — the same major version as the one
  in your container — and never with a toolchain it downloads.
* `/app` is one Go module, rooted at `/app`. Do not nest a second `go.mod`
  inside it, and do not add a `go.work`: the verifier builds with `GOWORK=off`,
  so a workspace file is ignored where it is not actively harmful.
* **`go build ./...` run in `/app` must succeed.** The verifier runs the
  equivalent of

      cd /app && go build -o <dir outside /app>/ ./...

  with a 1500-second ceiling, before any group runs. That prerequisite build is
  **best effort and is not itself a group**: nothing checks its exit status
  directly. What it decides is which binaries exist — every `package main`
  directory it builds becomes a binary named after its directory, and those
  binaries are what the command-line groups of section 4 execute. A package
  that does not compile therefore fails every group that needs it, and a
  tree in which nothing builds meets no requirement, because every driver group
  links your packages.
* The verifier's build output, its build cache and its scratch directories are
  **all outside `/app`**. It never writes into your tree except to restore the
  two manifest files of section 2, and it never reuses a build cache you left
  behind. Anything you compiled during the step is irrelevant when your work is
  checked: sources are the deliverable.
* `go test ./...` is **not** what is checked. You may keep your own tests in the
  tree — they are harmless as long as they compile — but the verifier does not
  run them and no group's result depends on them.

## 2. Module identity and import paths

* The module path is exactly

      github.com/google/certificate-transparency-go

  and it is fixed. Every driver in scope imports your code through it, so a
  different module path means every driver fails to compile. Sub-packages are
  addressed by directory: `…/tls`, `…/x509`, `…/asn1`, `…/client`,
  `…/trillian/ctfe`, and so on, exactly as section 5 names them.
* **`go.mod` and `go.sum` are benchmark-owned.** They are part of the overlay,
  not part of your deliverable. Before any build, the verifier **replaces**
  `/app/go.mod` and `/app/go.sum` with its own pinned copies — byte-identical
  to the ones sitting in your workspace right now. Whatever you leave in those
  two files is discarded when your work is checked.
* You may edit them locally if a broken manifest is blocking you, but **you
  must not rely on a dependency the pinned manifest does not already contain**.
  The pinned require list is the whole dependency universe available to you.
  Read it: it is at `/app/go.mod`, and `/app/go.sum` fixes the exact versions.
  If a package you want is not in there, it does not exist for this task.
* The measurement runs **offline**. The verifier builds with

      GOFLAGS=-mod=mod  GOPROXY=off  GOSUMDB=off  GOTOOLCHAIN=local  GOWORK=off

  against a module cache that was populated when the verifier image was built.
  `GOPROXY=off` means an unpinned module cannot be resolved at all, and
  `GOTOOLCHAIN=local` means a `toolchain` line asking for another compiler is a
  build failure rather than a download.
* Consequences worth stating plainly: no `go get` when your work is checked;
  no network then; a `replace` directive you add is discarded with the rest of
  the file; and a vendor directory is never consulted.

## 3. How the verifier reaches your implementation

### 3.1 Groups

Your work is checked through **independent groups**. Each group is exactly one
compilation unit and exactly one operating-system process. Two kinds exist:

* **driver groups** — a benchmark-owned `package main` program, compiled on its
  own inside a throwaway module named `ctverify` whose only connection to your
  work is

      replace github.com/google/certificate-transparency-go => /app

  then run as its own process. It links against your packages directly, so it
  sees exactly the exported surface of section 5.
* **process groups** — a shell script that runs the binaries built from your
  tree (section 4), feeds them configuration files, drives a server over a real
  loopback socket, and inspects exit statuses, stdout, and files written.

A driver that fails to compile fails **its own group and no other**. No two
groups share a binary or a process. Groups are not all the same kind, though:
each is either a **gate** group, which is mandatory and means the step is not
satisfied if it fails, or a **requirement** group. Section 7 lists which is
which for this step.

### 3.2 How a group announces its result

Each group prints lines to stdout, each prefixed with a per-run nonce that is
generated fresh for every run:

    <nonce> CASE <case-id>: PASS
    <nonce> CASE <case-id>: FAIL <free text>
    <nonce> SUMMARY executed=<n> failed=<m>

Only nonce-prefixed lines are parsed. A group passes if and only if there is
exactly one `SUMMARY` line, the number of `CASE` lines equals `executed`,
`executed` equals the count the verifier expected in advance, `failed` is zero,
and the process exits 0 without having been timed out or killed.

You cannot produce these lines: the nonce is unguessable, it is stripped from
the environment of every candidate binary a group runs, and a group never lets
a binary's stdout reach its own. Printing something that looks like a pass is
not a route to a pass.

### 3.3 What a group may and may not look at

* A group observes **behaviour**: a return value, an encoding, an exit status,
  an HTTP response, a file on disk, a failure to compile.
* A group never reads your source to decide a verdict and never checks an
  unexported identifier.
* A group does not check the *wording* of a message. Where a message is the
  only channel through which a checked fact can be observed at all, a group may
  check what that message **identifies** — and then only identifiers the
  documents themselves fix: an import path, a log's description, the path or
  URL of a list actually read, a field of an HTTP response body. It never
  matches the sentence those identifiers sit in, and it never checks a message
  produced by the Go standard library or by a dependency on its own account —
  where such a message carries an identifier that is checked, only that identifier is
  matched.
* **The one exception: a body the corpus prints in full.** Some outputs are
  not messages but *formats* — a response body, a fixed two-line refusal, a
  parse error whose exact text a specification page prints complete, with no
  elision and nothing left to the implementer. Where the corpus fixes the
  whole string that way, section 6 may list it, and what section 6 lists is
  compared **byte for byte**: the whole string, including any clause the Go
  standard library contributes to it, and including a trailing newline where
  the corpus shows one. That is the only way the previous paragraph is ever
  set aside, it reaches no further than section 6's own list — at some
  milestones that list names no such body at all, and then nothing is set
  aside — and anything the corpus gives only in part, or only by describing
  it, stays under the previous paragraph and is checked on its identifiers
  alone.
* Where a milestone consists of *removing* something, the group proves it by
  compiling a probe that imports the removed path and requiring the build to
  fail *naming that path*, always alongside a positive control that must build.
  Deleting unrelated code will not fool it, and neither will leaving the old
  package in place.
* Per-group limits: 900 seconds to compile a driver, 600 seconds to run the
  process.

### 3.4 The log backend

`google/trillian` is **out of scope**. It is not part of this product, it is
not a deliverable, and no group asks you to implement any part of it.

Some of this step's groups drive your front end against a backend. Where one
does, the group's own process stands up an **in-process
`trillian.TrillianLogServer` on a loopback gRPC socket**, benchmark-owned and
answering from **pinned constants** — a fixed tree size, a fixed root hash,
fixed timestamps — while recording what your front end asked it for. What is
checked is what the front end does against that backend: the gRPC calls it
makes, in what order, with what arguments, and what it turns the answers into.
The backend's own behaviour is never checked.

A stub may also be told to answer one call with a chosen **gRPC error status**
instead of a result. Where a group does that, the error itself is the group's,
not yours; what is checked is the HTTP status your front end maps it to, exactly
as the table of HTTP-API.md §6.1 gives it for the release under test. The body
that goes with it is checked only where **section 6 of this step** says so.
Where section 6 lists no such body, nothing about the body is in scope here;
where it does list one, section 6 is what fixes how much of it is contract —
typically the §2.1 `<status text>\n<error>\n` frame — and how much is the
backend's own wording, which you neither choose nor are required to invent.

Injecting an error status is a capability the harness always has; it is not one
every step exercises. Whether any group at *this* step uses it is settled by
**section 6** and by the group table in **section 7**: where neither names an
injected error status, the capability is **provided but out of scope here**. It is
described anyway so that the backend's range of behaviour is not a surprise,
but at such a step nothing you do about mapping an injected status has any
effect.

The stub is not a service you configure or start. It is created by the group,
on a loopback port the group picks at run time, and its address is handed to your
`ct_server` by the group — through `--log_rpc_server`, through the
configuration file, or through both, whichever the release under test
supports (CLI.md §2.1, CONFIG.md §3). Never assume a
fixed port, a fixed address, or that a backend is present at all: groups that
do not need one do not create one, and your server must still start, serve what
it can, and fail what it cannot in the documented way.

---

### 3.5 The compile-time gate, and `go vet` beside it

Section 1's build obligation is checked by one group, `build-clean`, which
section 7's table lists as **GATE**. It has three cases, and two different
kinds of obligation live in it.

**Cases 1 and 3 are ordinary regression.** Case 1 builds a one-line probe
against the product's root package, so the module graph has to resolve. Case 3
runs `go vet` -- the standard analyser set, no extra flags -- over the five
packages this release changes, and fails if it reports anything at all:

    go vet ./loglist3 ./jsonclient ./x509 ./trillian/ctfe ./trillian/ctfe/storage

Only each package's **non-test** files are analysed: the group enumerates them
with `go list` and hands vet that file list, because the bare pattern form of
the command would also analyse `*_test.go`, and section 1's build obligation
is `go build ./...`, which never compiles a test file. Nothing you leave in an
inherited `_test.go` can fail this case. Nothing else in the tree is
vetted either, and vet output is not read for content: the case checks the
exit status. The analysers most likely to
have something to say about new code here are `printf`, `structtag`,
`copylocks` and `lostcancel` -- a mis-spelled struct tag on the log-list types
or a `context.WithCancel` whose cancel is dropped in the driver-selecting arm
of `./trillian/ctfe/storage` would each fail this group. The `postgresql`
package under it is not one of the five, so vet never sees it.

**Case 2 is not regression, and it is the one place at v1.3.3 where the
gate is not already satisfied by the tree you were handed.** It compiles every
package under `github.com/google/certificate-transparency-go/...`. Section 2's
pinned manifest is v1.3.3's, and it no longer lists
`github.com/gorilla/mux` or `github.com/mattn/go-sqlite3`; the only packages
that imported those two were under `internal/witness`. So while
`internal/witness` is still in the tree those imports cannot resolve, the
whole-tree build fails, and the gate fails with it. Deleting `internal/witness`
(section 4) is a **gate condition** at this milestone as well as an in-scope one.
Read section 7's table accordingly: `witness-package-removed` is one requirement
among many, but it matters here beyond its own group — skipping the deletion
means the whole step is not satisfied.
Adding the two modules back is not an option: section 2 fixes the dependency
universe, and checking is offline against a module cache that does not hold
them.

Two consequences of that, because neither is guessable from section 7 alone:

* A no-op here satisfies none of the new work: the gate itself fails. Section 7's
  note on no-ops covers both cases; v1.3.3 is the second one.
* Until the whole tree compiles there are **no candidate binaries at all**.
  The harness builds them with a single `go build -o <dir>/ ./...`, and that
  command writes nothing whatsoever when any package in the pattern has an
  unresolvable import -- not "everything except the broken package". So every
  group that *runs* a program instead of linking one reports zero executed
  cases, `gate-endtoend` included. A log full of groups that executed nothing
  and have no connection to the witness code is the signature of exactly this.

## 4. Command-line programs the verifier runs

v1.3.3 is where the program set stops changing: **14** `package main`
directories (CLI.md §1), **four** fewer than at v1.2.0. Nothing
arrives; the whole of `internal/witness/cmd/` leaves, and it holds four of them
-- `cmd/client`, `cmd/feeder`, `cmd/witness` and `cmd/witness/config`.

| program | directory | what a group does with it |
| --- | --- | --- |
| `ct_server` | `trillian/ctfe/ct_server/` | started as a subprocess against a config a group writes; checked on its **31**-flag set (CLI.md §2.1) — a group reads `ct_server -h` and requires every one of the 31 names in that table's v1.3.3 column to appear, and requires `by_range` and `allow_verification_with_non_compliant_keys`, which the same table marks gone by v1.3.3, not to. The logging library's own flags are neither counted nor asserted on, exactly as CLI.md §2.1 says. Separately, what the new flags change over a real socket is checked |
| `migrillian` | `trillian/migrillian/` | checked on `etcd_servers`, `lock_file_path` and `election_delay` being **gone**, and on what it does when started without `--force_master`: exit **1**, and a refusal that names **`etcd`** (CLI.md §4.7). That one name is the whole of what is matched — case-insensitively, anywhere in the output — because it is the only part of the sentence the documents fix (section 3.3). Word the rest however you like. The same group also runs it **with** `--force_master`, against a backend that answers and a config file holding `migration_configs: {}` — CLI.md §4.7 gives the message and its fields — and that run must load the config, find no migration to perform and exit **0** |
| `preloader` | `preload/preloader/` | checked on `--target_bearer_token`, `--end_index` and `--continuous`, and on `--continuous` together with a non-zero `--end_index` being fatal **before the temporal log config is read** (CLI.md §4.2). Nothing it prints is checked. The ordering is observed instead of read: the path a group passes to `--target_temporal_log_cfg` is a FIFO, so the verifier can see whether that path was ever opened for reading. A refused run must exit **1** without opening it; every run the guard lets through must open it, and is then handed an empty file, which is a fatal config error of its own — also exit **1**. So put the guard above the config load and read the config with an ordinary open |
| the other eleven | see CLI.md §1 | built and enumerated: a group builds every `package main` directory in the tree and checks the names it gets back — exactly the fourteen of CLI.md §1, no more and no fewer, with no program named `witness`, `feeder`, `config` or the witness `client` among them, and every one of them answering a `-h` request. `ct_hammer` must answer to `--bearer_token` and `--start_index` |

The flag-grammar rules of step 06 are unchanged and still checked: an
unrecognised flag is `Usage of <prog>:` on stderr and exit **2**; a missing
required flag is a bare diagnostic on stderr and exit **1**; `klog.Exit*` and
`log.Fatal*` exit **1**, `klog.Fatal*` exits **255** — klog's names, not
glog's, because glog left the product at v1.1.4 (CLI.md's preamble table
gives both spellings and the same two statuses).

Two `ct_server` start-up decisions are new and are observed.
`--tls_certificate` and `--tls_key` **together** put the listener on TLS,
minimum version 1.2. The pair is inert unless both are set: one given without
the other leaves the listener on plain HTTP and is not a start-up error.
`--http_idle_timeout` is applied only when positive; a negative value
means "leave the server's own idle timeout alone" (CLI.md §2.3,
HTTP-API.md §6.3).

## 5. Packages and symbols the verifier links against

| import path | what the drivers use |
| --- | --- |
| `…/loglist3` | the tiled half of the schema, over pinned log-list JSON: `TiledLog`, `TiledLogs` on `Operator`, the constant `AllLogListSignatureURL`, the six finders `FindTiledLogByName`, `…ByURL`, `…ByKeyHash`, `…ByKeyHashPrefix`, `…ByKey` and `FuzzyFindTiledLog` (CLIENT-API.md §4.3, and §5.1 below for the exact spellings), and the three filters one group drives over both halves at once — `SelectByStatus([]LogStatus)`, `TemporallyCompatible(cert)` and `Compatible(cert, certRoot, roots)`, pointer-receiver methods each returning a `LogList` **value** (§5.1 gives the full signatures), so a driver passes the result to a function taking the list by value (CLIENT-API.md §4.3) |
| `…/x509` | `ParseCertificate` and `ParseCertificates` refusing a certificate whose two `AlgorithmIdentifier` structures differ byte for byte, and `ParseTBSCertificate` accepting the same input unchanged (X509-FORK.md §5.4) |
| `…/trillian/ctfe/storage`, `…/storage/postgresql` | `NewIssuanceChainStorage` choosing a driver from the connection string, and the PostgreSQL implementation behind the **unchanged** `IssuanceChainStorage` interface (CLIENT-API.md §8.4, CONFIG.md §2.6) |
| `…/trillian/ctfe` | `ValidateLogConfig` and `ValidatedLogConfig` over `ctfe_storage_connection_string` — the field the new arm validates — and the entrypoint name constant `LogV3JSONName`. A connection string the pgx parser rejects must come back as an error rather than a validated config, and that error must **name** two things: the config field `ctfe_storage_connection_string` and the word `postgres` (any case). CONFIG.md §2.6 gives the sentence those two names sit in; only the names are matched (section 3.3). `logv3_url` is **not** validated and gains no field on `ValidatedLogConfig`: it rides on the embedded `Config` and is read by the handler (CONFIG.md §2.7) |
| `…/jsonclient` | `Options.Authorization`, new here, **and what it puts on the wire**: when it is non-empty every request the client issues — `GetAndParse`, `PostAndParse` and `PostAndParseWithRetry` alike — carries one `Authorization` request header whose value is the option's string **verbatim**. Nothing is prepended, appended or re-encoded: no `Bearer `, no base64, no trimming. (The `Bearer ` prefix belongs to the *preloader*, which composes it from `--target_bearer_token` before handing the result to this field — CLI.md §4.2.) When it is empty no `Authorization` header is sent at all, rather than an empty one. It is independent of `UserAgent`, which keeps its own behaviour: a non-empty `UserAgent` is sent verbatim as the `User-Agent` request header, and setting both sends both |
| `…` (the root package, imported as `ct`) | the RFC 6962 structures the drivers assemble by hand, in **seven** groups — `build-clean`'s control probe included, where it is `ct.V1` that proves the module graph resolves. `MerkleTreeLeaf` with `Version`/`V1`, `LeafType`/`TimestampedEntryLeafType` and `TimestampedEntry`; `LogEntryType` with `X509LogEntryType` and `PrecertLogEntryType`; `ASN1Cert`, `CertificateChain`, `PreCert` and `LogEntry`; `SignedCertificateTimestamp` and `SignedTreeHead`; the two signature inputs `SerializeSCTSignatureInput` and `SerializeSTHSignatureInput`; `NewSignatureVerifier`, which is how three groups check a running server's signatures under the log's configured key — `VerifySTHSignature` in all three and `VerifySCTSignature` in two of them; the response bodies `AddChainResponse`, `GetSTHResponse` and `GetEntriesResponse`, and the two response-to-structure conversions two groups call — `(*GetSTHResponse).ToSignedTreeHead()` and `(*AddChainResponse).ToSignedCertificateTimestamp()`, the latter from v1.1.2, both CLIENT-API.md §3; and **`LogV3JSONPath`**, new here (§5.1). The RFC endpoint paths are *not* read out of this package at this step — every group spells `/ct/v1/…` itself — but `LogV3JSONPath` is, and the route you register has to agree with the constant you export (§6.1 item 10) |
| `…/tls` | the codec of TLS-CODEC.md, linked by four groups and the package every byte-for-byte comparison in section 6 passes through. `Marshal` over the structures in the row above — three of those groups build a `MerkleTreeLeaf` this way twice over: once as the `LeafValue` the stub is told to hold, which must come back as a `get-entries` answer's `leaf_input` unaltered, and once (in `gate-endtoend`) as the `LeafValue` your front end is required to have queued for a chain it accepted — and `Unmarshal` back, in both the runs-short arm and the trailing-bytes arm section 6 fixes. Beside them `DigitallySigned`, `SignatureAndHashAlgorithm` with its `SHA256` and `ECDSA` constants, and the tag types **`Enum`** and **`Uint24`**, which is where the width rules are written: a driver declares `tls.Enum` fields with `tls:"maxval:255"` and `tls:"maxval:65535"` and a bare `tls.Uint24`, and the widths that come out are checked. This is the product's own package and not `crypto/tls`: the group that puts the listener on TLS imports the standard library's under the same name, and the two are unrelated |
| `…/x509util` | the most widely linked package at this step — **seven** groups import it, more than any other. `CertificatesFromPEM`, which six of them use to load a PEM chain fixture in order; and `NewPEMCertPool` with the `*PEMCertPool` it returns and that pool's `AppendCertsFromPEM`, `AddCert` and `RawCertificates`, which is how the tiled-log filter group reads a pinned certificate out of a fixture and builds the `loglist3.LogRoots` map it hands to `RootCompatible`. An eighth group, `witness-package-removed`, does not import it but names it as a `go build` **target**: that is its second positive control, and if `…/x509util` is not a buildable package by path the group stops before it probes anything |
| `…/trillian/ctfe/configpb` | `LogConfig` built field by field inside a driver — `LogId`, `Prefix`, `PrivateKey`, `ExtraDataIssuanceChainStorageBackend` and `CtfeStorageConnectionString` — and the nested enum `LogConfig_IssuanceChainStorageBackend` with both generated constants, `LogConfig_ISSUANCE_CHAIN_STORAGE_BACKEND_TRILLIAN_GRPC` and `…_CTFE`, plus an out-of-range conversion of that enum type to stand for an unknown backend. Two groups name those Go spellings directly, so they are a compile-time obligation rather than compared text (§6.2): what a driver hands `ValidateLogConfig` is this generated message, not a struct of the same shape, and the `PrivateKey` on it is a protobuf `Any` |
| `…/trillian/ctfe/cache`, `…/trillian/ctfe/cache/lru`, `…/trillian/ctfe/cache/noop` | **three separate import paths, and all three have to resolve.** The sub-packages are not reachable through the parent and one driver imports every one of them by name, so a cache that lives entirely in `cache` fails the group at compile time however it behaves. From `cache`: the `Type` values `Unknown`, `NOOP` and `LRU`, `Option{Size, TTL}`, the `IssuanceChainCache` interface with `Set(ctx, key, chain) error` and `Get(ctx, key) ([]byte, error)`, and `NewIssuanceChainCache(ctx, Type, Option)`. From `lru`: `NewIssuanceChainCache(lru.CacheOption{Size, TTL})` — its **own** option type, not `cache.Option` — constructed directly and round-tripped. From `noop`: the exported type **`noop.IssuanceChainCache`**, which the driver type-asserts the factory's result to, so behaving like a no-op is not enough; the `NOOP` arm and the defaulting arm must both return that concrete type. `Type` is **case-sensitive** and is checked that way: `cache.Type("LRU")` must be refused while `cache.LRU` is accepted, which is what still pins the three values to lower case (§6.2). `cache.Unknown` is the empty type and defaults to no-op rather than erroring; a negative `Size` is refused; an LRU at capacity evicts the least recently used entry and the resulting miss is `(nil, nil)` |
| `…/internal/witness/…` | imported **to prove it is gone**: a probe naming the path must fail to build, and fail *as an absence* — the module no longer containing the package, rather than the package failing to compile — beside a positive control that must build (section 3.3) |
| `github.com/google/trillian`, `…/trillian/types`, `…/trillian/crypto/keyspb`, `google.golang.org/grpc`, `google.golang.org/protobuf/types/known/anypb` | **not this product, out of scope** (section 3.4), and not yours to change: their versions are fixed by section 2's pinned manifest, which you may not edit. They are named here because drivers name them, so they have to keep resolving in your module graph — seeing them is the point, owning them is not. From `trillian`: `UnimplementedTrillianLogServer` and `RegisterTrillianLogServer`, with `LogLeaf`, `QueuedLogLeaf`, `SignedLogRoot`, `Proof` and the request/response pair of every call the front end makes — this is section 3.4's in-process stub, written by the group and not by you, and what is checked is what your front end asks it for. From `trillian/types`: `LogRootV1`, which that stub's root is marshalled from. From `grpc`: `NewServer` and `*grpc.Server`, the loopback socket the stub is served on, in the seven groups that need a backend and in `migrillian-etcd-flags-removed`, which stands one up merely to have an address to point the program at. From `keyspb` and `anypb`: `keyspb.PrivateKey` wrapped by `anypb.New`, which is how the two storage groups put a DER signing key on a `configpb.LogConfig` |

That table is the whole of it. Every product package a driver at this step
links is in it, with the symbols that matter at v1.3.3, and no driver here
links anything else in the tree. Nothing in section 5 refers you to an earlier
step's `TEST_INTERFACE.md`, because you do not have one: this file is the
entire contract.

Four of this step's groups are inherited regression rather than new obligation, and they
are how the milestones behind you stay checked. All four are **gate** groups, so
their failure means the step is not satisfied. They link no package the table
above does not already name:

* `inherited-tls-codec` — the root package and `…/tls`: RFC 6962's encodings byte for
  byte, in both directions.
* `inherited-ctfe-endpoint-contract` — the root package, `…/tls` and `…/x509util`,
  against a running `ct_server`.
* `inherited-loglist3-log-lookups` — `…/loglist3`, but its **RFC 6962 half**, which the
  row above does not cover: `NewFromJSON`, `Log`, `LogStates` and `LogStatus` with
  `UndefinedLogStatus`, `PendingLogStatus`, `UsableLogStatus`, `RetiredLogStatus` and
  `ReadOnlyLogStatus`, and the constants `LogListURL`, `LogListSignatureURL` and
  `AllLogListURL`. Its input is a document with no tiled logs in it at all — that is the
  point: adding a second population to `Operator` must not change how the first is read.
* `inherited-issuance-chain-storage` — `…/trillian/ctfe`, `…/trillian/ctfe/storage`, the
  three `…/trillian/ctfe/cache…` paths and `…/trillian/ctfe/configpb`.

**Paths that must not resolve.** `…/internal/witness/…` is the live one: the subtree is
deleted at v1.3.3 and the only checked remnant of it is four build failures, one per
path — `…/internal/witness/cmd/witness`, `…/internal/witness/client/http`,
`…/internal/witness/verifier` and `…/internal/witness/api` — each required to end a line
with `does not contain package <that path>` (§6.1 item 14), beside two positive controls
that must succeed first: a `…/loglist3` probe that builds *and* runs, and `…/x509util` as
a plain build target. The earlier withdrawals — `…/merkletree`, `…/loglist` with
`…/loglist/findlog`, and `…/loglist2`, whose last releases RELEASE-MAP.md §14 gives — are
still absent at v1.3.3 and must stay absent, but no group here probes them;
`internal/witness` is what the removal probe names now (§6.2).

### 5.1 The exact Go shapes a group writes

These are the spellings the harness uses, so they are part of the interface: a
driver naming them is compiled against your tree, and a different spelling is a
compile failure that affects the whole group rather than one case.

    // loglist3 -- the six tiled finders, beside the six RFC 6962 ones, which
    // keep their names, their signatures and their behaviour.
    func (ll *LogList) FindTiledLogByName(name string) []*TiledLog
    func (ll *LogList) FindTiledLogByURL(url string) *TiledLog
    func (ll *LogList) FindTiledLogByKeyHash(keyhash [sha256.Size]byte) *TiledLog
    func (ll *LogList) FindTiledLogByKeyHashPrefix(prefix string) []*TiledLog
    func (ll *LogList) FindTiledLogByKey(key []byte) *TiledLog
    func (ll *LogList) FuzzyFindTiledLog(input string) []*TiledLog

    const AllLogListSignatureURL = "https://www.gstatic.com/ct/log_list/v3/all_logs_list.sig"

    // loglist3 -- the filters over both halves at once. Each has a pointer
    // receiver and returns the filtered list BY VALUE: a driver passes what
    // comes back to a function taking loglist3.LogList, so a *LogList return
    // is a compile failure that affects the group (CLIENT-API.md §4.3).
    func (ll *LogList) SelectByStatus(lstats []LogStatus) LogList
    func (ll *LogList) TemporallyCompatible(cert *x509.Certificate) LogList
    func (ll *LogList) RootCompatible(certRoot *x509.Certificate, roots LogRoots) LogList
    func (ll *LogList) Compatible(cert *x509.Certificate, certRoot *x509.Certificate, roots LogRoots) LogList

    // loglist3 -- the operator gains one field. A group marshals JSON with
    // this tag and hands it to NewFromJSON, so the tag is what it reads.
    type Operator struct {
        Name      string      `json:"name"`
        Email     []string    `json:"email"`
        Logs      []*Log      `json:"logs"`
        TiledLogs []*TiledLog `json:"tiled_logs"`
    }

    // loglist3 -- the two URLs a tiled log is addressed by, and the two tags a
    // group writes (CLIENT-API.md §4.3 prints the whole struct).
    SubmissionURL string `json:"submission_url"`
    MonitoringURL string `json:"monitoring_url"`

    // trillian/ctfe/storage/postgresql -- the third driver, behind the
    // unchanged storage.IssuanceChainStorage interface. One return value, no
    // error: a bad connection string is not diagnosed here.
    func NewIssuanceChainStorage(ctx context.Context, dbConn string) *IssuanceChainStorage

    // jsonclient -- one new field on the existing options struct. Non-empty
    // means "send this exact string as the Authorization request header, on
    // every request"; empty means "send no such header". See section 5 above
    // and CLIENT-API.md 1.1 -- the value is never decorated with a scheme.
    type Options struct { /* ... */ Authorization string }

    // the new endpoint is named in two packages
    const LogV3JSONPath = "/log.v3.json"               // package ct, the path
    const LogV3JSONName = EntrypointName("LogV3JSON")  // package ctfe, the name

## 6. Formats crossing the boundary

Step 12's `TEST_INTERFACE.md` is replaced by this file, so the comparison
surface is given here in full rather than referred to. It is **not** step 12's
list copied forward. Twelve milestones have moved underneath it, and the jump
from v1.2.0 to v1.3.3 is the largest in the package: a whole subtree is
deleted, a second transport appears, and the log list learns a second kind of
log. Every bullet below was re-checked against this step's own group drivers
before it was restated; §6.2 names the families earlier steps listed that this
step does **not** compare, and says why for each.

**Carried forward from the steps behind you, restated.**

* **The RFC 6962 TLS encodings, byte for byte.** Not round-tripped — compared
  against bytes assembled by hand: `MerkleTreeLeaf` in both arms (`x509_entry`
  and `precert_entry`), the `TimestampedEntry` inside it, `PreCert` (a 32-byte
  issuer key hash, then the TBS as a `uint24`-prefixed vector), `ASN1Cert`,
  `CertificateChain`, `DigitallySigned`, and the two signature inputs — the SCT
  input and the `TreeHeadSignature` input (TLS-CODEC.md). The rules that
  produce those bytes are measured with them: an integer's width comes from its
  tag and from nothing else; a fixed-size array carries **no** length prefix of
  its own; a vector is its length prefix followed by its members, so an empty
  one is the prefix alone — a `TimestampedEntry` with no extensions ends
  `00 00`; a selector value matching no arm is refused, not skipped; a value
  outside a declared `minlen`/`maxlen` is refused; and a decode that leaves
  trailing bytes hands them back rather than erroring, while one that runs
  short errors.
* **The JSON response bodies, member by member.** `add-chain` and
  `add-pre-chain` answer an `AddChainResponse`; `get-sth` a `GetSTHResponse`;
  `get-roots` a `certificates` array of standard-base64 DER; `get-entries` an
  `entries` array of `leaf_input`/`extra_data` pairs. Member spellings are the
  contract and the whitespace between them is not, **except** where §6.1 says a
  body is compared whole. `sct_version` is a JSON number, not `"v1"`; `id` and
  `signature` are standard base64. Where a response is JSON the header is
  exactly `Content-Type: application/json`, and that value is compared whole on
  `get-entries` and on the new `/log.v3.json`.
* **Query parameters, spelled as the server must accept them.**
  `get-entries` takes `start` and `end`; `get-sth-consistency` takes `first`
  and `second`; `get-proof-by-hash` takes `hash` and `tree_size`. A request
  that omits them, or whose range is inverted or negative, is refused with a
  **status**, not with a sentence of particular wording.
* **The assembled URL path.** A log's endpoints live at
  `<handler prefix> + "/" + <log prefix> + <endpoint path>`, and the per-log
  prefix is normalised by trimming slashes, so a prefix written with its own
  leading slash does not produce a doubled separator and one with a trailing
  slash does not produce an empty segment (HTTP-API.md §1.1). Every config a
  group writes here uses the bare prefix `bench`, so what is actually compared
  is the assembly: `/bench/ct/v1/get-sth` is served, an unknown log prefix is
  **404**, and `/healthz` and `/` are never prefixed at all.
* **`/healthz`.** Status **200** and a body of exactly `ok` — two characters,
  no newline. Three groups compare that body whole, one of them twice: once
  over TLS and once over plaintext.
* **The trust anchors the config names.** `get-roots` answers with exactly the
  certificates in that log's `roots_pem_file`, as standard-base64 DER, compared
  as a set: nothing extra, nothing missing, nothing re-encoded.
* **The config file is text proto, and an unknown field is fatal.** Every
  config a group writes is `prototext`, so the generated `configpb` field
  *names* are load-bearing before a request is ever made. A tree whose
  `LogConfig` is missing `logv3_url`, `max_merge_delay_sec`, `not_after_start`
  or `not_after_limit` — or which spells one of them differently — cannot even
  start the server for the groups that set them.
* **The multi-log config's shape.** `LogMultiConfig` carries a `LogBackendSet`
  of named backends and a `LogConfigSet` of logs; `backend_spec` is an opaque
  non-empty string the server dials, and `log_backend_name` is what routes a
  log to one of those backends. The signing key travels as a protobuf `Any`;
  the form exercised here is an inline `keyspb.PrivateKey` carrying DER
  (CONFIG.md §2.3).
* **The leaf the front end queues.** For an accepted submission the Trillian
  leaf the backend receives is compared field by field: `LeafValue` is the
  TLS-marshalled `MerkleTreeLeaf`, `ExtraData` the TLS-marshalled
  `CertificateChain` of the issuers, and `LeafIdentityHash` the SHA-256 of the
  end-entity **certificate DER**.
* **The tree head's three fields.** `tree_size` is the backend's tree size,
  `sha256_root_hash` its root hash, and `timestamp` its timestamp converted
  from nanoseconds to **milliseconds**; the STH signature must verify under the
  log's configured public key.
* **`Cache-Control` on the immutable reads.** The day-long `public,
  max-age=86400` that arrived at v1.1.2 is still the value, and `get-sth` and
  `get-roots` still send no such header at all (HTTP-API.md §4.7). What changes
  at this release is only that `get-entries` has a second lifetime, below.
* **Admission decisions are statuses, not sentences.** A malformed JSON body,
  an empty chain, a chain member that is not base64, a chain reaching no
  configured root, and a plain certificate submitted to `add-pre-chain` are all
  **400**. Section 3.3 governs the text; only the status is compared.
* **Method discipline.** An entrypoint answers the verb HTTP-API.md gives it
  and **405** to any other: `GET` on `add-chain` or `add-pre-chain`, `POST` on
  `get-sth` or `get-roots`. The body that comes with that 405 is compared whole
  — see the new bullets below and §6.1.
* **The v3 log list document.** `loglist3.NewFromJSON` parses the published
  schema, and the surface over it is checked whole: the five ordinary finders
  (`FindLogByName`, `FindLogByURL`, `FindLogByKey`, `FindLogByKeyHash`,
  `FindLogByKeyHashPrefix`) and `FuzzyFindLog`; the `LogStates` union, where
  exactly one member is set and `LogStatus()` reports which, an absent state is
  `UndefinedLogStatus`, and `Active()` returns the read-only state through its
  second slot and every other state through its first; `SelectByStatus`, which
  drops an operator that keeps no log; and `GoogleOperated()`, true when any
  operator `email` entry contains `google-ct-logs@googlegroups` and keyed on
  nothing else (CLIENT-API.md §4.2–4.3). `LogStatus` is a stringer-generated
  enum, so its printable names are the constants' own identifiers, and both
  those names and the three published v3 URL constants are compared as whole
  strings — see §6.1.
* **Issuance-chain storage and its cache.** The storage interface is exactly
  `FindByKey(context.Context, []byte) ([]byte, error)` and
  `Add(context.Context, []byte, []byte) error`, proved by compiling an
  implementation of the driver's own against it rather than by rendering it as
  text. The backend selector is checked: the Trillian-gRPC backend builds no
  storage at all and ignores any connection string; the CTFE backend refuses an
  empty connection string and an unsupported driver; an unknown backend is
  refused. Config validation drops the connection string for Trillian-gRPC and
  requires it for CTFE. The cache is checked as behaviour: a round trip returns
  the bytes that went in; an LRU at capacity evicts, and a miss is `(nil, nil)`
  rather than an error; the empty cache type defaults to no-op; a no-op stores
  nothing; an unknown type and a negative size are both refused; and the type
  names are case-sensitive, so `LRU` is not `lru`.
* **A removal is a build failure that names a path.** A package this release
  deletes is never proved gone with `ls` or by reading a file. It is observed
  as a build, by import path, inside a module whose `replace` points at your
  tree, that fails with the toolchain naming the exact path — beside a positive
  control that must succeed, so a probe cannot pass because nothing builds. At
  this release the deleted subtree is `internal/witness`; the exact wording
  required is in §6.1.

And, new at v1.3.3:

* **413 on an oversized write.** The request body of `add-chain` and
  `add-pre-chain` is bounded by `--max_cert_chain_size` (default `512000`);
  over-size is **413**, and the read endpoints stay unbounded
  (HTTP-API.md §6.3). The refusal is checked byte for byte, and it is the one
  error body besides §3.1's that carries the log's diagnostic identity — for a
  log with prefix `bench` and log id `1001` at a `4096` limit, exactly

      Request Entity Too Large
      bench{1001}: certificate chain exceeds 4096-byte limit: http: request body too large

  with `<limit>` the configured value, not the size the client sent. The
  trailing `http: request body too large` is **`net/http`'s own wording**, not
  yours — it is the string `http.MaxBytesReader`'s error carries. It is inside
  the compared bytes all the same, so wrap the body with that reader and pass
  its error through into the refusal rather than composing a sentence of your
  own. This is one of the bodies section 3.3's exception for printed formats
  is about: the corpus prints it in full (HTTP-API.md §6.3), so it counts as
  an output format and the whole line is matched, stdlib clause included.
* **429 with a fixed body.** A submission whose leaf `NotBefore` is older than
  `--non_fresh_submission_age`, arriving over a configured limit, is refused
  with exactly

      Too Many Requests
      rate-limited submission considered to be non-fresh

  Fresh submissions are never refused this way, and masking does not touch it —
  only 500 bodies are masked.
* **`/log.v3.json`.** Per-log, prefixed like the RFC endpoints, GET only, with
  the five members named in HTTP-API.md §7.1 in that order. The body is checked
  byte for byte, so its encoding is part of the contract: it is the **compact**
  `encoding/json` encoding of that object — no indentation, no spaces after the
  separators and **no trailing newline** — sent with
  `Content-Type: application/json`. `json.Marshal` followed by a write produces
  it; `json.NewEncoder(w).Encode(v)` appends a newline and does not, and
  `json.MarshalIndent` does not. `temporal_interval` is omitted only when
  **neither** bound is configured; when either is, the member is present and
  carries both keys, with the bound that is not configured sent as the empty
  string. With `logv3_url` unset the endpoint is **404** with
  `logV3JSON: logv3_url is not set in config`.
* **Two cache lifetimes on one endpoint.** A `get-entries` response carrying
  fewer leaves than were asked for is `public, max-age=60`; a full one keeps
  `max-age=86400`. The other three immutable endpoints are unchanged, and
  `get-sth` and `get-roots` still set no header (HTTP-API.md §4.7).
* **A parse refusal over pinned DER.** `x509: mismatching signature algorithm
  identifiers`, from bytes in which only the outer, unsigned identifier was
  altered — the same bytes a bare TBS parse still accepts. That is the whole
  of the error text, printed in full at X509-FORK.md §5.4, and it is compared
  exactly: not a substring match, no prefix or suffix of your own, and no
  wrapping context added around it.
* **A log list with two kinds of log in it.** Tiled entries are addressed by a
  submission URL and a monitoring URL, and `FindTiledLogByURL` matches either;
  the ordinary finders and the tiled finders search disjoint halves of the same
  document (CLIENT-API.md §4.3).
* **The wrapper's 405.** Unchanged since v1.0 and still checked here, now on
  the new endpoint too: a method the entrypoint does not take is **405** with
  the §2.1 two-line body, so a `POST /log.v3.json` is exactly

      Method Not Allowed
      method not allowed: POST

  — the status text, then HTTP-API.md §2's row verbatim with the method
  spelled as the request spelled it. Compared byte for byte, trailing newline
  included.
* **The bare root path.** `/` is **200** with an empty body; at v1.3.3
  every other unmatched path is **404** (HTTP-API.md §7).
* **One body that is deliberately *not* compared.** A plaintext GET to the TLS
  listener is **400**, and that response is `net/http`'s alone — the request
  never reaches a handler of yours. Only the status is checked; the body is
  read solely to confirm it is not a successful answer. The corpus does not
  print that body in full, so section 3.3's ordinary rule applies to it and
  that section's exception for printed formats does not.

### 6.1 The strings this step compares whole

Section 3.3's rule is behaviour, not wording, and its one exception is a body
the corpus prints in full. This is the complete list of what that exception
covers at v1.3.3. Everything on it is compared with `==` against a
literal, newlines included; nothing on it is a substring match.

1. **`ok`** — the whole body of `/healthz`, over HTTPS and over plaintext.
2. **`application/json`** — the whole `Content-Type` of `/log.v3.json` and of
   `get-entries`.
3. **`public, max-age=86400`** and **`public, max-age=60`** — the whole
   `Cache-Control` value of a full and of a short `get-entries` response, and
   the day-long one again on `get-proof-by-hash`.
4. **The `/log.v3.json` payload.** Compact `encoding/json`, no indentation, no
   space after a separator, **no trailing newline**, members in the order
   HTTP-API.md §7.1 declares them. Four forms are pinned: the full object with
   a temporal interval; the object of a log with no interval configured, which
   is exactly `key`, `log_id`, `mmd`, `url` and nothing else; and the two
   one-bound intervals, which keep both keys —
   `{"start_inclusive":"2020-01-01T00:00:00Z","end_exclusive":""}` and
   `{"start_inclusive":"","end_exclusive":"2060-01-01T00:00:00Z"}`.
5. **`Not Found` + newline + `logV3JSON: logv3_url is not set in config` +
   newline** — the whole 404 body of `/log.v3.json` on a log with no
   `logv3_url`.
6. **`Method Not Allowed` + newline + `method not allowed: POST` + newline** —
   the whole 405 body, with the method spelled as the request spelled it.
7. **`Request Entity Too Large` + newline +
   `bench{1001}: certificate chain exceeds 4096-byte limit: http: request body too large`
   + newline** — the whole 413 body, with the prefix, the log id and the
   configured limit substituted, and the trailing `net/http` clause included.
   The same body is compared at the default limit `512000`.
8. **`Too Many Requests` + newline +
   `rate-limited submission considered to be non-fresh` + newline** — the whole
   429 body of the non-fresh submission limiter.
9. **`x509: mismatching signature algorithm identifiers`** — the whole error
   text from `ParseCertificate` and `ParseCertificates`, compared with `==` and
   not as a substring.
10. **`/log.v3.json` and `LogV3JSON`** — the values of `ct.LogV3JSONPath` and
    `ctfe.LogV3JSONName`, compared as constants and used to fetch the endpoint,
    so the route you register and the constant you export have to agree.
11. **The three published v3 log-list URLs** — `loglist3.LogListURL`,
    `LogListSignatureURL` and `AllLogListURL`, joined and compared as one
    string, all three under `…/ct/log_list/v3/`.
12. **`https://www.gstatic.com/ct/log_list/v3/all_logs_list.sig`** — the value
    of `loglist3.AllLogListSignatureURL`, new at this release.
13. **`UsableLogStatus`, `RetiredLogStatus`, `ReadOnlyLogStatus`** — the
    printable names of `loglist3.LogStatus`, joined with `/` and compared as
    one string. They are the constants' own identifiers, not lower-case words.
14. **`does not contain package <import path>`** — the toolchain's refusal,
    matched at the end of a line for each of
    `…/internal/witness/cmd/witness`, `…/internal/witness/client/http`,
    `…/internal/witness/verifier` and `…/internal/witness/api`. This is the
    Go toolchain's own wording, not yours; what you control is that the
    directory is gone.
15. **`--backend flag must be specified`** — `migrillian`'s refusal when no
    backend is given (CLI.md §8). It is required to be present in the control
    runs and required to still be the stopping point after the etcd flags are
    withdrawn.
16. **`Bearer harbor-ct-bench-token`, `Basic aGFyYm9yOmJlbmNo`** and
    **`ct-go-harbor-bench/1.0`** — values a group hands to
    `jsonclient.Options` and reads off its own listener. The strings are the
    group's; what is checked is that they cross the client verbatim, once, on
    both GET and POST and across a retry, and that `Authorization` and
    `User-Agent` do not displace each other.

**Kinds that look like this and are not.**

* **Renderings a group owns.** `shape()` in the tiled-log filter group, the
  `|`-joined field dump in the tiled-log finder group, `200/true,200/true,200/true`,
  `sct,sct,sct`, `200 application/json`, `0x0303`. The separators and the
  layout belong to the driver; only the values inside them are yours.
* **Identifier matches inside an error, not whole bodies.** The PostgreSQL
  config refusal must name `ctfe_storage_connection_string` and contain
  `postgres` lower-cased; `migrillian`'s usage must not contain `etcd` in any
  case. Section 3.3's ordinary rule applies: the sentence around the
  identifier is yours.
* **Name sets, not strings.** The fourteen program names under `cmd/`, the
  thirty-one `ct_server` flag names, and the two flags that must be gone, are
  extracted from `--help` output and compared as sets of *names*. The usage
  text they are extracted from is yours.
* **Config field names.** They are load-bearing because `prototext` rejects an
  unknown field, not because anything prints them.
* **Fixture bytes.** The pinned TLS server certificate, the trust anchors, the
  mismatching-signature-algorithm DER and the marshalled TLS structures are
  compared as **bytes**. They are not text and no encoding of them is implied.
* **The 400 from a plaintext GET to the TLS listener.** Explicitly not
  compared; see the last bullet above.
* **Every other message.** Section 3.3 still governs. Say it how you like, as
  long as the identifiers the documents fix are in it.

### 6.2 What earlier steps listed and this one does not

Each of these was listed by a step behind you and is **not** part of this
step's comparison surface. The reason matters, because it is different in each
case.

* **Everything under `internal/witness`.** Step 11 compared the witness
  `config` tool's YAML file as a whole file and listed the witness HTTP API's
  paths and its stored-STH body. v1.3.3 **deletes that subtree**, so none
  of those formats exists to compare. The only checked fact left about the
  witness is item 14 above: importing any of its four packages must fail to
  build with the toolchain naming that exact path.
* **The gRPC-to-HTTP status map.** Steps 04 through 12 listed how a backend
  error becomes an HTTP status — `NotFound` to 404, `InvalidArgument` to 400,
  `ResourceExhausted` to 429, `Canceled`/`DeadlineExceeded` to 504, everything
  else 500 with a masked body — and step 12 listed the two of those that had
  just moved. The kit at this step can still drive a stub into a chosen gRPC
  status, but **no group here does**, so none of that map is measured at this
  milestone. It is still what the release does. The 429 that *is* checked here
  is a different one: the non-fresh submission limiter, whose body is fixed.
* **The RFC 6962 leaf hash.** `SHA-256(0x00 ‖ TLS(MerkleTreeLeaf))` and
  `ctutil.LeafHashB64`'s pinned base64 answer are not computed by any group
  here. Do not confuse the leaf hash with the `LeafIdentityHash` that *is*
  compared, which is the SHA-256 of the certificate DER.
* **The x509 fatality rows.** Step 07 listed which malformations are fatal and
  which survive as non-fatal notes; that table inverted at v1.1.0 and step 12
  restated it in its inverted form. No group here re-checks it. The one x509
  fact this release adds is the mismatching-signature-algorithm refusal, and
  what is compared there is that nothing is returned, an error is present, its
  text is exactly item 9, and a bare `ParseTBSCertificate` over the same bytes
  still succeeds and round-trips them unchanged.
* **Certificate renderings and OIDs as text.** The `serial/algorithm/subject`
  rendering, `x509.PublicKeyAlgorithm`'s printable `ECDSA`,
  `asn1.ObjectIdentifier.String()`'s dotted form, and the before/after DER of
  `RemoveCTPoison` and `RemoveSCTList` are untouched by every group here.
* **Step 12's two issuance-chain leaf layouts.** The hashed `extra_data`
  layout, `ExtraDataForChainHash`, and rehydration on the way out were step
  12's headline formats. Every log this step configures uses the Trillian-gRPC
  storage backend, which stores no chain out of line, so those encodings are
  never reached and never compared. The interface, the backend selector, the
  cache and the new PostgreSQL driver's configuration are checked instead.
* **The `IssuanceChainStorage` method set as a rendered string.** Step 12
  compared the interface's methods as `reflect.Type.String()` rendered them.
  No group here uses `reflect` at all; the same obligation is enforced by
  compiling an implementation against the interface and by assigning the
  PostgreSQL store to a variable of that type. That is stricter, not looser —
  but it is not a string.
* **The configpb field numbers and descriptor spellings.** Step 12 proved
  field numbers 20 and 21 as wire bytes and read the field names back off the
  descriptor. No group here reads a descriptor or a wire number. The generated
  Go identifiers — `CtfeStorageConnectionString`,
  `ExtraDataIssuanceChainStorageBackend` and the two
  `ISSUANCE_CHAIN_STORAGE_BACKEND_*` enum constants — are named directly by
  drivers, so they are compile-time obligations rather than compared text.
* **The three `cache.Type` literals.** Step 12 compared `cache.Unknown`,
  `cache.NOOP` and `cache.LRU` as whole strings. Here what is checked is the
  factory's behaviour: the empty type defaults to no-op, an unknown type is
  refused, and `cache.Type("LRU")` is refused — which is what still pins the
  values to lower case.
* **`--cache_size` and `--cache_ttl`'s sentinel defaults, and
  `keyspb.PEMKeyFile`.** Earlier steps compared the `-1` defaults and the
  second private-key handler. Here the two flags are checked only by **name**
  in the `ct_server` flag set, and every config inlines a DER
  `keyspb.PrivateKey`. CONFIG.md §2.3's "do not assume either one" still
  stands; neither is compared.
* **`loglist3.IsAllLogs`, and `loglist2` as the removal probe.** `IsAllLogs` is
  called by no group here. `loglist2` is still gone — it is simply no longer
  what the removal probe names; `internal/witness` is.
* **The client's redirect guard.** Step 12 listed it in detail. No group here
  issues, follows or forbids a redirect.

None of this is a licence to delete anything. Section 6 lists what is
**compared**; section 9 lists what is **out of scope**. A surface that appears in
neither still has to be there for your tree to build, and the build gate is
checked.

---

## 7. Scope

The gate is mandatory, and the requirement groups define the new work:

* **GATE groups are mandatory.** They add no new requirement: they are proof
  of life — the workspace builds and the artifact starts — plus *inherited
  regression*: an end-to-end path through the milestones this step carries
  forward. If a single gate group fails, this step is not satisfied, however
  complete your new work is. Preserving what already works is therefore not a
  requirement in its own right; it is the precondition for the new work being
  in scope at all.

  Proof of life is not always free. Do not assume the tree you were handed
  already passes the gate: where this release's pinned dependencies (section 2)
  stop supplying something inherited code still uses, the workspace does not
  compile until that code is adapted or removed, and the build gate is not
  satisfied until you do it — so a gate group can fail on the very first run,
  before you have changed anything. This takes three shapes, and checking only
  the first will mislead you. A pinned `go.mod` may stop providing a **module**
  an inherited file imports; or it may still provide the module while the pinned
  revision has removed a **symbol** — a function, method or type — that
  inherited code calls; or it may keep the symbol and **change its shape**, so
  that a call which still resolves no longer type-checks. In the last two cases
  the manifest looks perfectly clean and the build fails anyway. Which of them
  applies here, if any, is settled by building the tree you were handed, not by
  reading section 2: where section 3 carries
  compile-time material it tells you directly, but section 3 saying nothing is
  not a promise that the gate is free.
* **Requirement groups are this milestone's new capability and its peripheral
  breadth.** All of them are in scope, and the milestone's capability is
  expected across every family, not just one corner of it.
* A no-op satisfies none of the new work. Whether it *also* fails the gate
  depends on the step, for the reason in the first bullet — it does wherever
  the release's pinned manifest makes the inherited tree uncompilable.
* A group that fails to compile affects exactly that group — unless it is a
  gate group, in which case the step is not satisfied. This is why the
  verifier compiles one driver per group instead of one test binary per step:
  a single unresolved symbol in a requirement group cannot take the whole
  step down with it.
* The table below names every group in this step and its kind.
* Nothing you do in this step can abort the run or stop you from attempting a
  later step.

### Groups in this step

| Group | Kind |
| --- | --- |
| `build-clean` | gate |
| `gate-endtoend` | gate |
| `inherited-ctfe-endpoint-contract` | gate |
| `inherited-issuance-chain-storage` | gate |
| `inherited-loglist3-log-lookups` | gate |
| `inherited-tls-codec` | gate |
| `binaries-fourteen-mains` | requirement |
| `ctfe-get-entries-partial-cache` | requirement |
| `ctfe-logv3-json-endpoint` | requirement |
| `ctfe-max-cert-chain-size` | requirement |
| `ctfe-non-fresh-submission-rate-limit` | requirement |
| `ctfe-postgresql-issuance-chain-config` | requirement |
| `ctserver-tls-transport` | requirement |
| `jsonclient-authorization-header` | requirement |
| `loglist3-tiled-log-filters` | requirement |
| `loglist3-tiled-log-finders` | requirement |
| `migrillian-etcd-flags-removed` | requirement |
| `preloader-continuous-mode` | requirement |
| `witness-package-removed` | requirement |
| `x509-mismatching-sig-alg` | requirement |

A failure in any gate row means the step is not satisfied.

## 8. Determinism rules the verifier follows

These are promises about the environment, so that you never have to defend
against a moving target:

* **No wall clock.** Nothing is checked against what time it is when you run. A
  timestamp a group compares is either injected as a fixed value or read back
  out of your own response and used only as the pre-image of a further check,
  never compared against a clock the verifier read. Where a group must let a
  live process read the real clock — a personality deciding an admission, say —
  the fixtures it drives are dated far enough either side of the present that
  the verdict is the same whenever "the present" happens to be.
* **No network and no DNS** when your work is checked. Every certificate, chain,
  configuration file, key and vector is a pinned fixture, and the fixture
  corpus is verified against a checksum manifest before any group runs. A
  corrupt corpus fails the run.
* **Loopback only.** Every listening socket a group creates binds `127.0.0.1`
  on a port the group chooses at run time: either by binding port `0` and
  serving on the listener the kernel handed back, or — where the port has to
  be named on a command line before the process that will bind it exists — by
  probing for a free one below the kernel's ephemeral range. Never hard-code a
  port number in anything the verifier might run; never assume a fixed one is
  free.
* **Deadlines bound waiting; they are never assertions.** Where a group starts
  a process of yours it waits — for a readiness signal, or for the process to
  exit where exiting is the documented behaviour — and gives up after a fixed
  deadline; where it polls or retries it does so a fixed number of times; and
  where it runs a one-shot command it wraps it in a generous `timeout`. None of
  these is checked as a duration: no case fails because your code was slow.
  Every one of them is sized so that the only way to reach it is to never do
  the thing at all — never to become ready, or never to exit — and reaching it
  is reported as that failure, not as a slow one.
* **Elapsed time is only ever a lower bound or an ordering, never an upper
  one.** The rule above says no case is checked on how long you took; the one
  exception is a *documented* backoff or retry interval, where the delay is
  itself the behaviour under test. There a group may require that a retry
  arrived **no earlier** than the documented interval, and that two successive
  intervals grew in the documented order. Both are read from the stub server's
  own arrival timestamps, never from a clock your code reports, and a slow
  machine only makes them easier to satisfy. No group anywhere in this task
  requires anything to happen *within* a time budget.
* **Fresh scratch.** Every group gets a private empty writable directory as its
  working directory, and it is removed afterwards. Nothing leaks between
  groups, and no group depends on another having run first.
* **Pinned keys.** Signing keys used in assertions are fixtures. A signature is
  checked against an exactly specified pre-image, not against "some valid
  signature".
* **Unprivileged execution.** Your code always runs as an unprivileged user
  that cannot write the verifier's output directory.

---
## 9. Out of scope at this milestone

* **Tiled logs as a protocol.** The log list learns to *describe* one — two
  URLs, a key, a state — and that description is checked against pinned JSON.
  Nothing in this product fetches a tile or serves one, and no group asks you
  to.
* **The backend TLS dial.** `--trillian_tls_ca_cert_file` is checked on
  existing, on being read at start-up and on what an unusable file does to a
  starting process. Nothing TLS-serving stands behind it, so the handshake is
  not observed.
* **What `--http_idle_timeout` does to a connection.** The flag is checked on
  one thing only: its **name** appearing in `ct_server -h`, among the 31 of
  CLI.md §2.1's v1.3.3 column. No group ever starts a server with it set,
  and nothing measures when an idle connection is closed — that would be
  checking a duration against a wall clock, which section 8 rules out. So
  neither the value you parse it into nor the moment you apply it is reachable
  by any group at this milestone. Declare it as CLI.md §2.1 types it, implement
  it as CLI.md §2.3 describes — applied only when positive — and nothing here
  will time you.
* **The limiter's throughput.** Non-freshness is checked on the decision, not on
  the timing: the fixture chains a group submits have a `NotBefore` of
  2020-01-01 and the thresholds it configures are `1s` on one side and about a
  century on the other, so which side of `--non_fresh_submission_age` a
  submission falls on is fixed by the fixture and cannot change with the day
  the run happens on. The refusal is then checked on its status and its body,
  and the rate is always `1/1h`, so no token can refill inside a run. Nothing
  counts requests per second, and no group measures how long anything took.
* **Migration and preloading traffic.** Both milestones here are about the
  command line: the flags migrillian no longer has and what it does without
  `--force_master`, and the preloader's new flags and the combination it
  refuses. Neither program copies a log under checking — that would need two
  live ones.
* **The two shapes of the leaf-building service.** v1.3.3 splits it into
  a direct form that holds no storage and an indirect one that puts a hash in
  the leaf and the chain in storage (CLIENT-API.md §8.4). Both types are
  unexported, and the split changes nothing a group can reach from outside the
  process: what the front end stores and what it puts in a leaf is the step-12
  behaviour, and that is checked here under `inherited-` names. No group names
  either type, and section 3.3's rule that an unexported identifier is never
  checked is why it could not.
* **`…/submission`.** No group imports the package, and nothing the proxy
  *does* is checked: nothing about `NewDistributor`, its options, or the
  distributor's root handling, and nothing it answers over HTTP — no group
  ever serves from it. Whatever v1.3.3 changes inside the package is
  outside the checked surface for the same reason. It is reached in exactly
  two places, both about the tree rather than about the proxy: its `server`
  binary is one of section 4's fourteen and has to build and answer `-h` like
  the rest, and `build-clean` case 2 compiles the package with everything
  else.
* **`IsAllLogs`.** The whole-list marker on `LogList` is part of the v3 schema
  a group parses, but it arrived before this milestone and no group sets it or
  asserts anything about it.
* **What `internal/witness` used to do.** Co-signing, its SQLite state and the
  feeder's polling were out of scope, and the removal is checked as an absence
  (section 3.3), not as a behaviour — and, per section 3.5,
  as a gate condition: the tree has to build without it.
* **An error status injected into the backend.** Section 3.4's stub can be
  told to answer one call with a chosen gRPC status instead of a result, and
  the mapping from that status to an HTTP one is a checked behaviour — at
  **step 12**, where a requirement group injects them and reads the statuses back.
  No group at this milestone injects one. The capability is compiled into
  every group here that stands a backend up and is exercised by none of them,
  so it is **provided but out of scope**: nothing in this step's requirements,
  gate or otherwise, moves with how you map a backend failure. The step-12
  behaviour is still expected of you and you should not remove it — it is
  simply not measured again here.
* **Nothing is deferred.** This is the last anchor in the series. There is no
  later milestone that an unimplemented behaviour belongs to, and no
  `v1.3.3`-dated capability is being held back for one.

Out of scope at any step, and stated here so that you do not spend the budget on
it:

* **`google/trillian` itself.** The backend is never a deliverable and is never
  checked (section 3.4). Where a group needs one it brings its own.
* **The `vendor/`-to-modules build migration** that upstream performs at
  v1.1.0. The benchmark owns `go.mod` and `go.sum` at every step (section 2),
  so there is no migration for you to perform and nothing about it is observed.
* **Anything requiring a database server.** No MySQL, MariaDB, PostgreSQL or
  SQLite server runs at any step. Where a milestone adds storage, what is
  checked is the interface, its cache behaviour, the configuration surface and
  the leaf-shape contract — never a live connection.
* **Performance, memory use, log output and code layout inside a package.**
  Nothing is timed except the coarse per-group ceilings of section 3.3, and no
  group reads your source to form a verdict.

## 10. This step

The difficulty here is not depth, it is arithmetic. Ten or so changes land at
once, they touch six different parts of the tree, and almost none of them
depends on another. No group in the milestone depends on finishing one family
perfectly: independent groups over independent changes mean the honest strategy
is to go wide first and deepen afterwards.

Two of the changes are not additive, and they are the ones to be careful with.
The parser gets **stricter**: a certificate carrying one signature-algorithm
identifier inside the signed TBS and a different one outside it parsed at
v1.2.0 and must not parse at v1.3.3 — while a bare TBS parse,
which has only one identifier to compare, must go on accepting exactly what it
accepted before. Get that exemption wrong and the inherited groups fail as well
as this one. And a whole subtree leaves: `internal/witness` and its four programs are
deleted outright, which is checked by compiling against the path and requiring
the build to fail naming it. **Do that one first.** Section 3.5 of
TEST_INTERFACE explains why: until it is gone the workspace does not compile
at all against v1.3.3's pinned manifest, so the gate fails and no
candidate binary is produced for any other group to run. The whole step
sits behind it.

The rest is addition, and most of it is observable from outside the process: a
listener that speaks TLS when two flags are set, a 413 where a body is too
large, a 429 with a fixed two-line body for a stale submission, a shorter cache
lifetime on a short answer, one more JSON endpoint that is off until the config
turns it on, a third storage driver behind an interface that does not change,
and a second kind of log in the log list that the existing finders never
return.
