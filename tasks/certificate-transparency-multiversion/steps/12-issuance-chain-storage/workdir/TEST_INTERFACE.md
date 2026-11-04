# TEST_INTERFACE — step 12 of 13 (certificate-transparency-go v1.2.0)

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

### 3.5 The compile-time gate: the tree you were handed does not build

Section 7 warns that proof of life is not always free, and points here for
whether that applies at this milestone. **It applies here**, and in the third
of the three shapes section 7 names — the module is still supplied, the symbol
still resolves, and the *shape* of it has changed.

Section 2's pinned manifest moves a long way with this release:
`github.com/google/trillian` v1.5.1-0.20220819… → **v1.6.0**,
`google.golang.org/grpc` v1.48.0 → **v1.63.2**,
`google.golang.org/protobuf` v1.28.1 → **v1.34.1**,
`k8s.io/klog/v2` v2.80.1 → **v2.120.1**, and the `go` directive 1.17 →
**1.21.0**, besides three new direct requires — `github.com/hashicorp/golang-lru/v2`,
`github.com/go-sql-driver/mysql` and `github.com/DATA-DOG/go-sqlmock`, taking the
direct-require count from 24 to 27. The code you inherit was written
against the old set, and one of those moves breaks it: grpc's
`resolver.Address` no longer carries a `Type` field and `resolver.Backend` no
longer exists, so `trillian/ctfe/ct_server`'s multiple-backend branch — the
comma-separated `--log_rpc_server` path of CLI.md §2.1 — does not type-check.
The compiler names the one file and the one line; the repair is a small one
and is not on this milestone's feature list, it is the cost of the pin moving.

Three consequences, because none of them is guessable from section 7 alone:

* `go build ./...` in `/app` fails, section 1's build obligation is not met,
  and `build-clean` — which section 7's table lists as **GATE** — fails on the
  very first run, before you have changed anything. Until it passes the step
  is not satisfied, however much of the milestone below is finished.
* A no-op here satisfies none of the new work: the gate itself fails.
* The failure is one package, so `go build -o <dir>/ ./...` still produces
  every *other* program — and `ct_server` is the one it does not. Every group
  of section 4 that starts the front end therefore fails for want of a binary,
  `gate-endtoend` included, with nothing in the failure to do with the caches,
  the leaf layouts or the configuration fields this step is about. A log of
  that shape is the signature of exactly this and not of the work below.

Adding the withdrawn shapes back is not an option: section 2 fixes the
dependency universe, the benchmark restores its own `go.mod` and `go.sum` over
yours before it builds anything, and checking is offline against a module cache
that holds only what the manifest names.

---

## 4. Command-line programs the verifier runs

The program set is exactly what v1.1.4 left: **no program arrives at
v1.2.0 and none leaves** (CLI.md §1). What moves is the flag surface and
what a misconfigured process refuses to do.

| program | directory | what a group does with it |
| --- | --- | --- |
| `ct_server` | `trillian/ctfe/ct_server/` | started as a subprocess against a config a group writes, then driven over a real loopback socket; checked on `--cache_type` accepting `noop` and `lru` (and defaulting to `noop`), on `--cache_size` and `--cache_ttl` existing with their negative defaults, and on the start-up refusals below. The full 23-flag set of CLI.md §2.1 is the surface you must present — four flags arrive here, three of them the cache flags above — but each is checked on its own name and behaviour, never as a total |
| `ct_hammer` | `trillian/integration/ct_hammer/` | built with everything else; `seed` is **withdrawn** here (CLI.md §4.8), which your tree must do, though no group at this step reads its flag set — section 9 |
| the rest | see CLI.md §1 | built once, before any group runs, from the whole module; every program v1.1.4 had, including `migrillian`, the submission pair and all four `internal/witness/cmd/` programs — the witness, the feeder, the witness client and the witness config tool — must still exist. The **gate** group `build-clean` (section 7) enumerates that set by name — the eighteen basenames CLI.md §1 lists as present at v1.2.0 — and requires each of them to have been produced by that build, to be a non-empty executable file, and to terminate on its own when it is asked for its usage; *which* status it chooses is its own business and is out of scope there. A program that will not compile is simply absent from the build, and `build-clean` is itself one of the groups section 3 means when it says such a package fails every group that needs it — so, because that group is the gate, a single missing program fails the whole step and not only the groups that reach for a binary. What is not asserted is a *total* — section 9 |

The flag-grammar rules of step 06 are inherited unchanged: an unrecognised
flag is `Usage of <prog>:` on stderr and exit **2**; a missing required flag
is a bare diagnostic on stderr and exit **1** (CLI.md §1, *Flag grammar*).
Neither rule is **checked at this step** — no group here passes an unrecognised
flag or withholds a required one, and the usage request `build-clean` does make
is explicitly out of scope as regards the status it comes back with, as the table
above
says. They are stated because they are behaviour your tree must not lose, not
because a step-12 group reads them. The exit statuses
of the logging library are unchanged too, but the library itself is not:
`glog` was replaced by `klog` throughout the product at v1.1.4 and
the numbers survived the swap (CLI.md §1 preamble). At v1.2.0 that means
`klog.Exit*` and `log.Fatal*` exit **1**, and `klog.Fatal*` exits **255**.

**The cache flags refuse start-up with status 1.** `--cache_type` is checked on
the two values CLI.md §2.1 names, `noop` and `lru`, and on refusing a name that
is neither — the group passes it `--cache_type=redis`. One value is
deliberately **not** checked here, and it is the one CLI.md §2.1's shorter
phrasing elides: the empty string is `cache.Type`'s zero value `Unknown`, which
the factory of section 5.1 point 1 maps to the no-op rather than refusing
(CLIENT-API.md §8.4), so a bare `--cache_type=` starts. No group at this step
passes one, and nothing here asks you to refuse it. With `lru` the two sizing
flags are read, and their defaults are
the negative sentinels `-1` and `-1s`, which the factory of section 5.1 point 1
refuses — so `--cache_type=lru` on its own, or with only one of
`--cache_size` / `--cache_ttl` supplied, or with a negative `--cache_size`, is
a start-up failure as well. All of them are refused the way every other
start-up failure in this section is refused: the process reports the factory's
error and exits **1**, the `klog.Exit*` number above and not the `klog.Fatal*`
one. That status is the observable a group reads, so reporting these refusals
with `klog.Fatal*` — exit 255 — is a different answer to the same question and
fails the group even though the process did decline to start. What the
diagnostic *says* is out of scope (section 3.3); only that it declines, and with
which status.

Four refusals arrive with this milestone, and they are checked at **two
different layers**. Which layer a refusal is raised in is part of the contract,
because a tree that refuses in the wrong place still refuses — and still fails
the group that reaches for it.

*One* of the four is checked as **process** behaviour: the whole-configuration
rule that two logs may not share a private key (CONFIG.md §2.3; CLI.md §2.2
step 8). It is observed by starting `ct_server` on such a configuration and
requiring a non-zero exit that never binds a socket, so nothing is listening
afterwards.

The other *three* are checked **in process**, by a group that calls the library
directly on a configuration it built, with no `ct_server` involved:

* the CTFE arm's missing and unusable connection strings must be refused by
  **`ctfe.ValidateLogConfig`** (CONFIG.md §2.6, §4.3), and
* a public key that is not the signer's — or is of a type the cross-check does
  not cover — must be refused by **`ctfe.SetUpInstance`** (CONFIG.md §2.3).

Both are exactly where `ct_server` itself reaches them, at steps 2 and 7 of
CLI.md §2.2, so a solution that raises them there gets the process behaviour
for free. A solution that instead adds a bespoke check in `main` *beside* the
validator leaves the library arms silent and fails those groups, even though
the process it ships would still refuse to start.

The ordering CLI.md §2.2 gives is unchanged: the per-log rules run before the
whole-configuration one, so a configuration violating both fails on a per-log
rule first, and the exit status is 1 either way. None of the four sentences
above is matched as text — section 3.3 governs — they name *which* refusal is
meant.

## 5. Packages and symbols the verifier links against

| import path | what the drivers use |
| --- | --- |
| `…/trillian/ctfe/storage` | **`IssuanceChainStorage`** — born here — with `FindByKey(ctx, key)` and `Add(ctx, key, chain)`, and `NewIssuanceChainStorage(ctx, backend, dbConn)`, whose `TRILLIAN_GRPC` arm returns `(nil, nil)`: a nil store is the *signal* to keep chains in the leaf, not an error |
| `…/trillian/ctfe/cache` | `Type` with `Unknown`/`NOOP`/`LRU`, `Option{Size, TTL}`, the `IssuanceChainCache` interface, and `NewIssuanceChainCache` |
| `…/trillian/ctfe/cache/lru`, `…/trillian/ctfe/cache/noop` | the two implementations, constructed directly: `lru.NewIssuanceChainCache(lru.CacheOption{Size, TTL})`, and the no-op whose `Get` returns `(nil, nil)` so that a miss is never an error |
| `…/trillian/util` | leaf construction in **both** shapes side by side — `BuildLogLeaf` and `ExtraDataForChain` as before, `BuildLogLeafWithChainHash` and `ExtraDataForChainHash` new here |
| the root package | `CertificateChainHash` and `PrecertChainEntryHash`, the two structures section 6 pins, alongside the `CertificateChain` and `PrecertChainEntry` they sit beside; and, from the inherited surface the drivers reach by name, `MerkleTreeLeafFromRawChain(rawChain []ASN1Cert, etype LogEntryType, timestamp uint64) (*MerkleTreeLeaf, error)`, `(*AddChainResponse).ToSignedCertificateTimestamp()` (both CLIENT-API.md §3) and `SignedTreeHead`'s six fields, spelled out in CLIENT-API.md §3 |
| `…/tls` | `Marshal` and `Unmarshal` over all four of those, in both directions |
| `…/asn1` | the forked DER codec, already linked against since step 01, used here for the *stored* form of an issuance chain: `asn1.Marshal` over a `[]ct.ASN1Cert` of the certificates after the leaf, and `asn1.Unmarshal` back into the same slice type (CONFIG.md §2.6) |
| `…/trillian/ctfe/configpb` | `LogConfig.ctfe_storage_connection_string` (20) and `extra_data_issuance_chain_storage_backend` (21), the nested `LogConfig_IssuanceChainStorageBackend` enum with both of its values, the generated name/value maps, and the `Get<Field>()` accessor beside every field (CONFIG.md §1.2) |
| `…/trillian/ctfe` | `ValidateLogConfig` over the two new fields, `SetUpInstance` returning `*Instance`, and `InstanceOptions` — which at v1.2.0 also carries the cache selection, `CacheType cache.Type` and `CacheOption cache.Option` |
| `…/jsonclient` | the transport `client` has sat on since step 01, re-read here for the upload guard section 6 describes: `New(uri, hc, Options{})` and `PostAndParse(ctx, path, req, rsp)` with its three return values (CLIENT-API.md §1.1). A driver speaks to a loopback listener of its own, never to a candidate-built binary |
| `…/client` | `client.LogClient` over that transport, unchanged at v1.2.0 and re-checked here against a loopback log the driver owns: `New(uri, hc, jsonclient.Options{PublicKey: …})`, which returns an error for a `PublicKey` that does not parse, and the methods `GetSTH`, `AddChain`, `GetSTHConsistency`, `GetProofByHash`, `GetAcceptedRoots` and `GetEntries`. A client given the log's key must **refuse** an STH or an SCT that key did not sign, and a non-200 response must come back as `(nil, error)` rather than as a decoded value (CLIENT-API.md §1.2) |
| `…/x509` | the forked parser, linked against since step 01 and re-checked here: `ParseCertificate`, `ParseCertificates`, `ParseTBSCertificate`, `NewCertPool` with `AppendCertsFromPEM`, `RemoveCTPoison`, the `SignedCertificateTimestampList` carrier the TLS codec decodes into, and above all `IsFatal` — the boundary between “malformed, but parsed” (a non-nil certificate beside a non-fatal error) and “refused”, with `IsFatal(nil)` **false** (X509-FORK.md §3, §4.1–§4.2, §5.1). `RemoveSCTList` is reached through `…/ctutil` rather than called directly |
| `…/x509util` | `CertificatesFromPEM`, which drivers use to load a chain out of a PEM fixture in order, and `ParseSCTsFromSCTList`, which turns a decoded `x509.SignedCertificateTimestampList` into `[]*ct.SignedCertificateTimestamp` (CLIENT-API.md §5.2) |
| `…/ctutil` | `LeafHash(chain, sct, embedded) ([sha256.Size]byte, error)` and `LeafHashB64(chain, sct, embedded) (string, error)`, both over `[]*x509.Certificate` of the **forked** type, compared against pinned known answers for three pre-images: an X.509 entry, a precertificate with its issuer, and a certificate carrying the SCT inside it (CLIENT-API.md §2; section 6, “Leaf hashes”) |
| `…/loglist3` | the surviving log-list package, and the one field this release adds to it: `LogList.IsAllLogs`, json tag `is_all_logs,omitempty` (CLIENT-API.md §4.3). It is built **and run**, not merely named |
| `…/loglist2` | named here because it must be **absent**. A removal is proved the way section 3.3 describes: a probe that imports the path must fail to build naming it, against a positive control that must build. The control is the `…/loglist3` row above |
| `github.com/google/trillian`, with `types`, `monitoring`, `crypto/keys`, `crypto/keys/der` and `crypto/keyspb` beneath it | **third-party, and out of scope as work of yours** — section 3.4: the log backend is the benchmark's. It is named because it must keep *resolving* in your module graph. `trillian.UnimplementedTrillianLogServer`, embedded in the in-process stub the group writes and registered with `trillian.RegisterTrillianLogServer`, together with `LogLeaf`, `QueuedLogLeaf`, `SignedLogRoot`, `Proof` and the request/response pair of every call your front end makes — `QueueLeaf`, `AddSequencedLeaves`, `GetInclusionProof`, `GetInclusionProofByHash`, `GetConsistencyProof`, `GetLatestSignedLogRoot`, `GetEntryAndProof`, `GetLeavesByRange` and `InitLog`; `trillian/types`' `LogRootV1`, which that stub's root is marshalled from; `trillian/monitoring`'s `InertMetricFactory`, the value a driver hands `ctfe.InstanceOptions.MetricFactory`, whose declared type `monitoring.MetricFactory` it therefore has to keep satisfying; and `trillian/crypto/keyspb`'s `PrivateKey` and `PublicKey` with `trillian/crypto/keys`' `RegisterHandler` and `trillian/crypto/keys/der`'s `FromProto`, which is how a driver populates the key-handler registry before it calls `SetUpInstance`. `trillian.LogLeaf` is also the type the two `…/trillian/util` builders above return, so this module is linked by a group that stands up no backend at all. Mind the collision: `…/trillian/ctfe` and `…/trillian/util` above are the **fork's own** `trillian/` directory, packages you do own, and have nothing to do with this module |
| `google.golang.org/grpc`, with `codes` and `status` beneath it | **third-party, out of scope.** `grpc.NewServer` and `*grpc.Server`: the stub backend of section 3.4 is served on a loopback TCP listener the driver opens for itself and your `ct_server` is pointed at it — the gRPC server is the benchmark's, never yours, and no driver speaks gRPC to your front end. `codes.Code` with `ResourceExhausted`, `Canceled`, `DeadlineExceeded`, `NotFound`, `PermissionDenied`, `Unauthenticated`, `Unavailable` and `Aborted`, wrapped by `status.Error(code, …)`: that is how the stub returns a *coded* backend failure, and the HTTP status your front end turns each one into is what section 6.1's error-body item checks |
| `google.golang.org/protobuf`, with `proto`, `encoding/prototext`, `reflect/protoreflect` and `types/known/anypb` beneath it | **third-party, out of scope.** `proto.Marshal` over a `configpb.LogConfig`, which is how the two new fields' wire tags are read (section 6); `prototext.Unmarshal`, which is how their text-proto spellings and enum member names are read, and which must refuse an enum member the schema does not declare; `protoreflect.FieldNumber` with `StringKind` and `EnumKind`, read off `(&configpb.LogConfig{}).ProtoReflect().Descriptor().Fields()` so that field numbers 20 and 21, their names and their kinds are checked in the **generated descriptor** and not only in the Go struct — `configpb.LogConfig` therefore has to stay a generated protobuf message rather than a struct of the same shape; and `anypb.New`, which wraps a `keyspb.PrivateKey` into the `google.protobuf.Any` a log configuration carries a key in (CONFIG.md §2.3) |

The last three rows are **third-party modules, not part of your deliverable**,
and nothing in them is checked as work of yours: you neither own the code nor
choose the versions — `go.mod` and `go.sum` are benchmark-owned and are replaced
with the pinned copies before any build (section 2), and section 3.5 is about one
of those pins moving. They are written down only so that what must keep
**resolving** is visible. A driver is compiled in a throwaway module beside your
tree, so a dependency your manifest stops providing, or provides at another
version, takes that driver's whole group down with it.

### 5.1 The exact Go shapes a group writes

Section 3.3's rule stands: the verifier never reads your source to decide a
verdict. But it *compiles* against the names below, so a solution that
implements the right behaviour under a different spelling does not build. These
are the spellings the harness uses; they are part of the interface, and nothing
here says anything about how the behaviour is implemented behind them.

    // …/trillian/ctfe/cache
    func NewIssuanceChainCache(ctx context.Context, cacheType Type, option Option) (IssuanceChainCache, error)

    // …/trillian/ctfe/cache/noop  — an exported, zero-value-constructible struct
    type IssuanceChainCache struct{}
    func (c *IssuanceChainCache) Get(ctx context.Context, key []byte) ([]byte, error)
    func (c *IssuanceChainCache) Set(ctx context.Context, key, chain []byte) error

    // …/trillian/ctfe/cache/lru  — its own option type, one return value, no error
    type CacheOption struct { Size int; TTL time.Duration }
    func NewIssuanceChainCache(opt CacheOption) *IssuanceChainCache

    // …/trillian/util
    func BuildLogLeafWithChainHash(logPrefix string, merkleLeaf ct.MerkleTreeLeaf, leafIndex int64, cert ct.ASN1Cert, chainHash []byte, isPrecert bool) (trillian.LogLeaf, error)
    func ExtraDataForChainHash(cert ct.ASN1Cert, chainHash []byte, isPrecert bool) ([]byte, error)

    // …/trillian/ctfe/configpb  — protoc-gen-go's spelling of the nested enum
    // of CONFIG.md §1.2, written out because a group compiles against it
    type LogConfig_IssuanceChainStorageBackend int32
    const (
        LogConfig_ISSUANCE_CHAIN_STORAGE_BACKEND_TRILLIAN_GRPC LogConfig_IssuanceChainStorageBackend = 0
        LogConfig_ISSUANCE_CHAIN_STORAGE_BACKEND_CTFE          LogConfig_IssuanceChainStorageBackend = 1
    )
    var LogConfig_IssuanceChainStorageBackend_name  map[int32]string   // 0 -> "ISSUANCE_CHAIN_STORAGE_BACKEND_TRILLIAN_GRPC"
    var LogConfig_IssuanceChainStorageBackend_value map[string]int32   // and the inverse

    // …/trillian/ctfe
    type ValidatedLogConfig struct {
        …   // Config, PubKey, PrivKey, KeyUsages, NotAfterStart, NotAfterLimit
            // and FrozenSTH, unchanged; CONFIG.md §4.1 gives their types. No
            // driver at this step reads them — the two below are read by one
            // group, config-ctfe-storage-validation, and by four of its cases
        CTFEStorageConnectionString          string                                          // new here; note the all-caps CTFE
        ExtraDataIssuanceChainStorageBackend configpb.LogConfig_IssuanceChainStorageBackend  // new here
    }
    func (i *Instance) GetPublicKey() crypto.PublicKey
    type InstanceOptions struct {
        …   // the fields CLIENT-API.md §8.2 lists, unchanged. Three drivers set
            // four of them by name, so those spellings and types have to keep
            // accepting them: Validated *ValidatedLogConfig, Deadline
            // time.Duration, MetricFactory monitoring.MetricFactory — handed a
            // monitoring.InertMetricFactory{} — and RequestLog RequestLog,
            // handed a &ctfe.DefaultRequestLog{}. DefaultRequestLog is thereby
            // a linked symbol of this package too, not merely a mention; it is
            // unchanged here and CLIENT-API.md §8.5 and §8.8 specify it
        CacheType   cache.Type  // new here
        CacheOption cache.Option
    }

Eight points of behaviour go with those shapes, because a group observes each
of them and no other document states them:

1. **`cache.NewIssuanceChainCache` returns concrete types a caller can assert
   on.** `LRU` yields a `*lru.IssuanceChainCache`; `NOOP` **and** the zero value
   `Unknown` (`""`) both yield a `*noop.IssuanceChainCache`, so an unset cache
   type is the no-op rather than an error. Any other string is
   `(nil, error)`. For `LRU` only, a negative `Size` or a negative `TTL` is
   `(nil, error)`; the two are not validated for the other types, which ignore
   the option entirely.
2. **`lru.NewIssuanceChainCache` does not validate.** It is the direct
   construction path, takes `lru.CacheOption` rather than `cache.Option`, and
   returns the cache alone.
3. **`storage.NewIssuanceChainStorage`'s CTFE arm decides from the connection
   string alone, at construction, without contacting anything.** A string that
   does not begin with `mysql` — including the empty string — is
   `(nil, error)` returned by the factory itself. The verifier therefore never
   needs, and never starts, a database server: a store that accepted any string
   and deferred the decision to its first `Add` would be a different contract,
   and would fail here. This is a *separate* check from `ValidateLogConfig`'s
   refusals of the same shape (section 4), which act on the configuration
   before any factory is called.
4. **A backend value outside the enum is rejected by the factory and carried
   through by the validator.** `storage.NewIssuanceChainStorage` returns
   `(nil, error)` for any `LogConfig_IssuanceChainStorageBackend` that is
   neither `ISSUANCE_CHAIN_STORAGE_BACKEND_TRILLIAN_GRPC` nor
   `ISSUANCE_CHAIN_STORAGE_BACKEND_CTFE`. `ctfe.ValidateLogConfig` is the other
   way round: it acts on the two named values and leaves anything else to the
   factory, copying the field into `ValidatedLogConfig` unchanged rather than
   refusing the configuration. The two are not inconsistent — validation is
   about the *combination* of fields, construction is where an unusable backend
   becomes an error — but a solution has to implement both halves, so both are
   written down.
5. **The connection string is copied only on the `CTFE` arm.** The backend enum
   is copied into `ValidatedLogConfig` on every arm (point 4), but
   `ctfe_storage_connection_string` is not: the validator reaches
   `vCfg.CTFEStorageConnectionString = cfg.CtfeStorageConnectionString` only
   after the CTFE arm's three refusals have all passed. On the
   `TRILLIAN_GRPC` arm — and for any backend value outside the enum — nothing
   is validated and nothing is copied, so
   `ValidatedLogConfig.CTFEStorageConnectionString` is the **empty string**
   even when the configuration set one. A validator that carried the field
   through on both arms would be harmless at run time, because the gRPC arm
   never reads it, but it is not what this release does and a group observes
   the difference.
6. **`Instance.GetPublicKey` is total, never a panic.** It returns the public
   half of the instance's signer, and `nil` when the instance has no signer.
   That includes an `Instance` that was never set up: `(&ctfe.Instance{})`
   is a legal receiver and must answer `nil` rather than dereferencing its way
   into a panic. `Instance` is otherwise only ever produced by `SetUpInstance`,
   and a mirror instance — which has no signer — is the in-product case the
   nil answer exists for.
7. **The cache selection on `InstanceOptions` is honoured by set-up, not
   merely stored.** `SetUpInstance` passes `CacheType` and `CacheOption`
   through the factory of point 1 and returns the factory's error as its own:
   an unknown cache type, a negative `Size` for `lru` or a negative `TTL` for
   `lru` is `(nil, error)` out of `SetUpInstance` on an otherwise valid
   configuration, and the zero value of the pair sets up exactly as every
   configuration written in steps 01 to 11 does. For a non-`lru` type the
   option is not read, so a negative `Size` beside `noop` must still set up.
   No database is involved in observing this: with the default storage backend
   the store is nil (point 3), so the cache selection is the only thing left
   that can decide the outcome.
8. **The storage factory's refusal is propagated by set-up in exactly the same
   way.** `SetUpInstance` builds the issuance-chain store from the validated
   configuration and returns *that* factory's error as its own too. Both halves
   are observed together, on one configuration that names no connection string
   at all, so nothing is dialled: a backend value outside the enum validates
   (point 4) and then makes `SetUpInstance` return `(nil, error)`, while
   `TRILLIAN_GRPC` on the same configuration sets up and yields a non-nil
   `*Instance` even though the store it is given is nil (point 3). The pair is
   checked together on purpose: without the `TRILLIAN_GRPC` control, a
   personality that failed to set up for some unrelated reason would be read as the
   refusal. That configuration leaves `CacheType` and `CacheOption` at their
   zero values, which point 7 says set up cleanly, so the storage factory is
   the only thing left that can object.

`loglist3` — now the *only* log-list package in the tree — must still compile
and is built here as the positive control for `loglist2`'s removal. Four of
the rows above are re-checked by `inherited-` **gate** groups, because they are
the parts this milestone is most able to break: the TLS codec, the RFC 6962
Merkle leaf, the forked `x509` parser and `client.LogClient`. Outside the Go
standard library and `ctverify/vhelp` — the verifier's own helper package, which
ships inside the verifier image and is no part of your tree — the table above is
the whole of what a driver at this step imports, product and third-party alike.
A product package it does not name is carried only by your tree having to
compile as a whole — the `build-clean` gate builds
`github.com/google/certificate-transparency-go/...` — and not by a group of its
own at this step.

## 6. Formats crossing the boundary

Step 11's `TEST_INTERFACE.md` is replaced by this file, so the list is given
here in full rather than referred to. It is **not** step 11's list copied
forward. Eleven milestones have moved underneath it: some of what earlier
steps wrote has changed shape by v1.2.0, and some of it named a program or
a package this product no longer has. What is below is what is true *here*, and
section 6.2 names what was dropped and why — so that nothing is left for you to
go looking for in a file you cannot open.

Carried forward from the steps behind you, restated:

* **TLS encodings.** `MerkleTreeLeaf`, `TimestampedEntry`, `DigitallySigned`,
  `CertificateChain`, `PrecertChainEntry`, the SCT signature input and the
  `TreeHeadSignature` input are compared **byte for byte** against vectors the
  *verifier* holds. Those vectors are not in `ct-spec/` and you cannot read
  them; they were produced from the field orders, tag parameters and widths of
  TLS-CODEC.md §1–§3 and §5, which determine the encoding completely.
  Re-derive an encoding from those rules and it will match; guess at one and it
  will not. The codec's own rules are re-checked in the same place and none of
  them moves at this release: an integer's width comes from its tag and not
  from its Go type, a fixed-size array carries no length prefix, an `ASN1Cert`
  carries a uint24 length prefix and refuses to be empty (`minlen:1`), an empty
  vector is its length prefix and nothing else, a selector value with no union
  arm will not decode, a value outside its declared bounds will not encode, and
  `Unmarshal` hands trailing bytes back to the caller rather than refusing them
  while truncation is an error (TLS-CODEC.md §1).
* **Leaf hashes.** `SHA-256(0x00 ‖ TLS(MerkleTreeLeaf))`, compared as bytes
  (MERKLE.md §1), and `ctutil.LeafHashB64`'s standard-base64 rendering of the
  same hash, compared as a whole string against a pinned known answer. Both
  pre-images are still checked — a precertificate with its issuer, and a
  certificate carrying the SCT inside it, which hashes as the *precertificate*
  entry it was issued from once `x509.RemoveSCTList` has taken the SCT list
  back out of the TBS (CLIENT-API.md §2, §3; X509-FORK.md §4.2) — and a
  precertificate leaf still cannot be built from a chain of one.
* **JSON bodies.** Field names, types and base64 encodings exactly as
  HTTP-API.md §3 and §4 give them: `sct_version` a number and not `"v1"`, `id`,
  `signature`, `sha256_root_hash` and `tree_head_signature` standard base64
  with padding, `certificates` and `chain` arrays of base64 DER **in order**,
  `entries` carrying `leaf_input` and `extra_data`. A response is read field by
  field; nothing compares a whole JSON document as text.
* **Request construction.** The query parameters HTTP-API.md §4 names, spelled
  and valued as it gives them: `first` and `second` in decimal, `tree_size` in
  decimal, `hash` as the standard base64 of the RFC 6962 leaf hash, `start` and
  `end` in decimal. A group reads them off the request its own listener
  received.
* **Assembled URL paths.** `<handler prefix> + "/" + <log prefix> +
  <endpoint path>`, with the per-log `prefix` normalised inside `trillian/ctfe`
  rather than in `ct_server` — a leading slash prepended when absent, trailing
  slashes trimmed — so `bench`, `/bench` and `/bench/` all serve
  `/bench/ct/v1/…` (HTTP-API.md §1.1). `/`, `/healthz` and `/metrics` are
  **never** prefixed. Every group that stands a server up reaches the log
  through that assembly, and `/healthz` outside it.
* **`/healthz`.** Status **200** and a body that is exactly `ok` — two bytes,
  no newline, nothing else. Three groups use it as the proof that `ct_server`
  came up at all, so it is compared as a whole body and it is the first thing to
  fail when start-up is broken.
* **PEM, and how a private key reaches a signer.** Root certificates are given
  to `ct_server` by path, as `roots_pem_file`, and the concatenation of those
  files is exactly what `get-roots` serves. The log's **private key** is a
  `google.protobuf.Any` matched against the key-handler registry `ct_server`
  populates at start-up (CONFIG.md §2.3); the loader must never open a path
  itself. Every configuration the groups at *this* step build — in process and
  for a live `ct_server` alike — inlines the key as a `keyspb.PrivateKey`
  carrying raw DER, so the DER handler is the one they exercise. What is checked
  is the routing, not the shape: CONFIG.md §2.3's "do not assume either one"
  still stands, and `keyspb.PEMKeyFile` with its `path` and its possibly
  non-empty `password` must stay registered.
* **The configuration file's encoding.** The log config is a protocol-buffer
  message, and every configuration a group at this step writes is **text
  proto**, in which unknown field names are refused — which is what makes
  CONFIG.md §1's field-arrives-at-release-N table observable at all. Steps 02
  and 03 said text proto and *nothing else*: that stopped being true at
  **v1.1.2**, where a file which does not parse as text proto began to be
  retried as **binary proto** (CONFIG.md §4.2), and it is not true here. A
  group checks the content verdict of a config it has actually loaded, never
  the file's encoding.
* **The multi-backend configuration message.** `LogMultiConfig` with its
  `LogBackendSet` and `LogConfigSet`, as CONFIG.md §3 gives it: `backends` is
  one `LogBackendSet` holding repeated `backend`, `log_configs` one
  `LogConfigSet` holding repeated `config`. A backend's `backend_spec` is an
  opaque non-empty string at validation time, and a log reaches the backend its
  `log_backend_name` names and no other.
* **The gRPC → HTTP status map** of HTTP-API.md §6.1. Two rows move at this
  release — see below — and the rest must **not** move with them:
  `PermissionDenied` is still **403**, `NotFound` still **404**, `Aborted`
  still **409**, `Unauthenticated` still **401** and `Unavailable` still
  **503**. Each of those is asserted here, on a real socket, so that a tree
  which re-cut the table too widely is caught in the same group that checks the
  two rows which did move.
* **The leaf the front end queues.** `trillian.LogLeaf.LeafIdentityHash` is the
  **SHA-256 of `cert.Data`** — the submitted certificate's DER alone, not the
  chain, not the index, not the timestamp (CLIENT-API.md §8.7) — and
  `LeafValue` is the TLS encoding of the Merkle leaf. Both are compared byte
  for byte against what the group built for itself, and the identity hash is
  how the backend recognises a re-submission.
* **Where a tree head's three fields come from.** The `timestamp` a `get-sth`
  response carries is the backend root's `TimestampNanos` **converted to
  milliseconds**, not the personality's own clock; `tree_size` is `TreeSize`
  and `sha256_root_hash` is `RootHash`. All three are read out of the
  `SignedLogRoot` the backend answered with, a group pins all three, and the
  signature over them is verified under the log's own key. The injected
  `TimeSource` dates *SCTs* on the `add-chain` path; it does not date tree
  heads.
* **The forked `x509` parser's non-fatal boundary, as it stands at
  v1.2.0 — not as step 07 wrote it.** The lax retry of X509-FORK.md §3.2
  arrived at **v1.1.0**, so the two rows step 07 called fatal at v1.0.21 are
  **non-fatal** here: a **non-minimally-encoded INTEGER** and an RSA
  `SubjectPublicKeyInfo` whose `AlgorithmIdentifier` parameters are **not
  `NULL`** each return an error for which `x509.IsFatal` is false, together
  with a fully populated certificate. Restating step 07's sentence unchanged
  would be false at this milestone. The floor is still a floor: **truncated
  DER** and **trailing data after a certificate** are fatal and yield nothing,
  and `x509.NewCertPool().AppendCertsFromPEM` keeps what is only non-fatally
  wrong while rejecting what is not a certificate at all. What is compared is
  the (`IsFatal`, error-present, certificate-returned) triple and the field
  values recovered from the certificate — never the error's wording and never
  its type.
* **Object identifiers as text.** An extension list is compared as the
  dotted-decimal renderings `asn1.ObjectIdentifier.String()` produces, in the
  certificate's own order — in particular RFC 6962 §3.1's poison extension
  `1.3.6.1.4.1.11129.2.4.3`. `x509.RemoveCTPoison` must drop exactly that
  extension, leave every other one in place and in order, and **error** on a
  TBSCertificate that carries none.
* **One algorithm rendering.** `x509.PublicKeyAlgorithm.String()` renders
  `ECDSA`; it is read off a parsed certificate and compared. That is the whole
  of step 09's algorithm-name list that is re-checked here — no other
  `SignatureAlgorithm` or `PublicKeyAlgorithm` constant's rendering is
  compared at this milestone.
* **A removal is a build failure that names a path.** Section 3.3's rule,
  applied at this step to `loglist2`: a probe importing the path must fail to
  build *naming it*, alongside a positive control on `loglist3` that must
  build. Retyping the callers while leaving the package on disk fails, and so
  does deleting something else. The exact string matched is item 3 of §6.1.
* **`loglist3`'s one new field.** `LogList.IsAllLogs`, json tag
  `is_all_logs,omitempty` (CLIENT-API.md §4.3). It must encode as
  `"is_all_logs":true` when set, decode back to `true`, and be **absent** from
  the encoding when it is not set. The field is exercised through the encoder
  a caller reaches, not merely named.

And, new at v1.2.0:

* **Two leaf layouts, both live.** A queued leaf's extra data is either the
  TLS encoding of `CertificateChain` / `PrecertChainEntry`, exactly as it has
  been since step 01, or — for a log configured for CTFE issuance-chain
  storage — of the two structures this release adds:

      PrecertChainEntryHash {
        PreCertificate     ASN1Cert  `tls:"minlen:1,maxlen:16777215"`
        IssuanceChainHash  []byte    `tls:"minlen:0,maxlen:256"`
      }
      CertificateChainHash {
        IssuanceChainHash  []byte    `tls:"minlen:0,maxlen:256"`
      }

  In the certificate form the leaf carries the hash alone — the certificate is
  already in the Merkle leaf and is not repeated; in the precertificate form
  it carries a precertificate alongside the hash, because that is the only
  place the precertificate lives.
  A certificate entry's hash **may be empty**, and when it is, the entry means
  "there is no issuance chain to look up" rather than being malformed: the
  rehydration path produces a chain of length zero and the caller still sees a
  valid leaf. The precertificate field of the precertificate form is required
  and non-empty. Both forms are compared as exact byte strings, encode and
  decode.
* **The rest of the leaf is invariant.** Between the chain layout and the hash
  layout, the same submission must produce the same Merkle leaf, the same leaf
  hash and the same identity hash. Exactly one field differs. This is how "an
  existing log is unaffected" is checked.
* **The wire does not change.** `get-entries` and `get-entry-and-proof` emit a
  byte-identical `extra_data` whichever layout the stored leaf holds; a
  hash-bearing leaf is rehydrated from the store on the way out. The mode is
  invisible to a client and visible only in what the backend holds
  (CONFIG.md §2.6, HTTP-API.md §4.3).
* **What "byte-identical" means, as an equation a group checks.** Take a
  submission whose chain after the leaf is `issuers`. Let
  `der = asn1.Marshal(issuers)` and `key = sha256(der)` — the stored value and
  the stored key CONFIG.md §2.6 fixes. Then rehydrating must close the circle
  exactly: `tls.Unmarshal` of `ExtraDataForChainHash(cert, key, isPrecert)`
  yields `key` back; `FindByKey(key)` yields `der`; `asn1.Unmarshal(der)`
  yields `issuers`; and re-encoding those as `CertificateChain` (or
  `PrecertChainEntry`, keeping the precertificate the hash form carried) must
  equal **`ExtraDataForChain(cert, issuers, isPrecert)`** byte for byte. An
  empty `IssuanceChainHash` closes the same circle with a zero-length chain.
  This identity is what makes the two layouts interchangeable, and it is
  checked directly rather than inferred from the endpoints.
* **Two statuses move, and they still carry the ordinary error body.**
  `ResourceExhausted` from the backend becomes **429** where it was 403, and
  `Canceled` / `DeadlineExceeded` become **504** where they were 408
  (HTTP-API.md §6.1). The body is checked beside the status for both of these,
  and this bullet is the section 3.4 entry that says so: each response must
  carry the ordinary error body of HTTP-API.md §2.1 — the status text **of the
  status actually returned**, a newline, a non-empty error, and a trailing
  newline — and not a bare status line, an empty response, or the status text
  of the code the backend's error used to map to. What is fixed is that
  **frame**. The wording inside it is the error your front end was handed,
  which you neither choose nor are checked on inventing: no group matches it,
  and section 3.3's rule that a diagnostic's text is not an observable is
  unchanged. `--mask_internal_errors` still touches only 500, so neither of
  these two bodies is ever masked.
* **Cached behaviour, as observable values.** Fresh entries come back; an
  LRU whose capacity is a **positive** `Size`
  and which reaches that capacity evicts rather than growing without bound,
  and evicts by *recency* — the entry whose last successful `Get` or `Set` is
  oldest goes first, so reading an entry back saves it from the next eviction
  and a plain insertion-ordered queue is not this cache; a
  chain is associated with one key and not another;
  and the no-op implementation returns a nil chain and a nil error for every
  key it is asked for, so a caller must treat "not there" as distinct from
  "failed". That rule is general and not the no-op's alone: in **either**
  implementation a `Get` for a key the cache does not hold — never stored,
  or stored and since evicted — is `(nil, nil)` and never an error. `Set` is
  likewise a **replacement** and not an insert-once: setting a key the cache
  already holds leaves the *later* chain under that key, and the entry counts
  once against the capacity bound, not twice. A store that returns a chain for
  an unknown key, or that grows past the configured size, breaks the contract
  even though the log behind it still looks correct.
* **A redirect must not turn a submission into a read.** The client-side
  upload path is `jsonclient.PostAndParse`, and from v1.2.0 it looks at
  the method of the request that actually reached the server, not only at the
  status that came back. HTTP **301**, **302** and **303** all convert a POST
  into a GET; the GET then succeeds, and a caller that read only the status
  would report a submission accepted that the log never saw as a submission.
  So a POST whose final request method is not POST is an **error**, and the
  caller's response structure is left as it was — not filled in from the body
  of whatever request the server did serve. **307** and **308** preserve the
  method, so those must still be followed, still succeed and still parse:
  refusing every redirect is as wrong as accepting a converted one. This is a
  *client-side* guard and nothing about the server changes — the front end
  issues no redirects at all (HTTP-API.md §2), which is exactly why the guard
  has to live in the client, where somebody else's redirect can arrive.
* **Both LRU bounds are switched off by a non-positive value, and that is the
  default.** `Size` ≤ 0 means *no capacity bound*: the cache holds everything
  put into it and evicts nothing. `TTL` ≤ 0 means *no expiry*: an entry never
  ages out. `ct_server`'s `--cache_size` and `--cache_ttl` default to `-1` and
  `-1s` precisely because the default cache type is `noop`, for which neither
  value is read. Reaching the unbounded LRU through the `cache` factory means
  `Size: 0`, since the factory refuses a negative `Size` for the `lru` type
  (section 5.1); constructing `lru.NewIssuanceChainCache` directly, any value
  ≤ 0 is unbounded. The preceding bullet's eviction rule is therefore about a
  cache with a positive size, and the two are not in tension.

### 6.1 The strings this step compares whole

Section 3.3 allows exactly one kind of whole-string comparison: a body the
corpus prints in full, listed here. This is that list, for this step, and it is
complete.

1. **`/healthz`'s body.** Status **200** and a body of exactly `ok` — two
   bytes, no newline. Three groups compare that body whole, as the proof the
   server came up; the rest poll the endpoint for a 200 and read no further.
2. **The error-body frame on the two moved statuses.** A backend
   `ResourceExhausted` must reach the client as **429** and a backend
   `Canceled` or `DeadlineExceeded` as **504** (HTTP-API.md §6.1), and the body
   must keep the ordinary two-line shape of §2.1: it begins with the Go
   `http.StatusText` of the status it carries followed by a newline — so
   `Too Many Requests` and `Gateway Timeout`, those exact spellings — ends with
   a newline, and is longer than that prefix alone. The **prefix, the suffix
   and the non-emptiness** are compared; the detail line between them is your
   diagnostic and is never read for its wording.
3. **The toolchain's removal message.** A build of a probe importing
   `github.com/google/certificate-transparency-go/loglist2` must fail with a
   line ending in exactly
   `does not contain package github.com/google/certificate-transparency-go/loglist2`,
   and the matching probe on `loglist3` must build. This is the whole of the
   removal proof, and it is a checked string: an emptied-but-present directory,
   or a package left behind with its callers retyped, does not produce it.
4. **`loglist3`'s new json tag, through the encoder.** The bytes
   `"is_all_logs":true` must be **present** in the encoding of a list with
   `IsAllLogs` set, and the bytes `is_all_logs` must be **absent** from the
   encoding of one without it. Substring comparisons over an encoder's output —
   the only two at this step.
5. **`ctutil.LeafHashB64`'s return.** The standard-base64 rendering of the RFC
   6962 leaf hash, compared whole against a pinned known answer, for the
   precertificate case and for the SCT-embedded case.
6. **The `IssuanceChainStorage` method set, reflected and rendered.** The
   methods in name order, each as `reflect.Type.String()` renders it, joined
   by `"; "`, and equal to exactly this one line:

       Add(context.Context, []uint8, []uint8) error; FindByKey(context.Context, []uint8) ([]uint8, error)

   That checks the interface's **shape** — method names, arity, parameter and
   result types, and nothing else. Add a third method, reorder the parameters
   or widen a signature and the string moves.
7. **Three renderings off a parsed certificate.** The joined triple
   `4660/ECDSA/*.google.com` (serial in decimal, `PublicKeyAlgorithm.String()`,
   subject common name); the comma-joined serials `4097,2,1` from a PEM pool;
   and a comma-joined list of dotted-decimal extension OIDs containing
   `1.3.6.1.4.1.11129.2.4.3`. All are *parsed values* re-rendered by the
   standard library, not messages.
8. **configpb's schema spellings.** The field names
   `ctfe_storage_connection_string` and
   `extra_data_issuance_chain_storage_backend`, the enum's full name
   `configpb.LogConfig.IssuanceChainStorageBackend` and its two member names
   `ISSUANCE_CHAIN_STORAGE_BACKEND_TRILLIAN_GRPC` and
   `ISSUANCE_CHAIN_STORAGE_BACKEND_CTFE`, read off the generated descriptor.
   The field **numbers** 20 and 21 are proved separately, as wire bytes.
9. **The three `cache.Type` values.** `cache.Unknown` is the empty string,
   `cache.NOOP` is `noop` and `cache.LRU` is `lru` — lower case, compared
   exactly, neither trimmed nor case-folded, because `ct_server`'s
   `--cache_type` passes its flag value through to the factory unchanged.

**That list is exhaustive: no group at this step compares any other message,
rendering, body or program output as a whole string.** In particular, no
assertion anywhere at this step reads an `error`'s `Error()` text. Every
failure your code reports is checked on *whether* it failed, on the status or
sentinel it carries, or on what it refused to produce — never on how it is
worded, in English or any other language.

Four kinds of comparison look like the list above and are not:

* **Bytes the harness itself supplied, handed back.** The stub backend's
  pinned root hash, the `signature` bytes a group planted, the marker a probe
  writes to prove a request arrived by POST, the JSON the stub serves. Those
  are the group's own values on a round trip, not your output.
* **Values the group put into a configuration and expects to find again.** A
  backend's name and address, a MySQL DSN, a log's `prefix`. Comparing them
  proves the value was carried without alteration; it says nothing about
  wording.
* **Byte slices compared as bytes.** TLS encodings, DER, leaf values, identity
  hashes, chain hashes and extra data are compared with byte equality, not as
  text, and nothing in their comparison is sensitive to how anything is
  spelled.
* **Whole-message reads on the failure path, which do not exist here.** Where a
  group expects a request to be refused it checks that it was refused — a
  non-nil error, a validation verdict, a status code — and stops there.

### 6.2 What earlier steps listed and this one does not

Four families are absent from the list above on purpose, because restating
them would put a false sentence in front of you:

* **Everything naming `loglist` or `loglist2`, `findlog`, or the v1 and v2 log
  list schemas** — `final_sth`, `disqualified_at`, `findlog`'s flag lines and
  its output shape. Those packages and that program were removed at step 11 and
  `loglist2` is removed at this one; only `loglist3` survives. The only thing
  checked about `loglist2` here is that importing it fails (item 3).
* **`ctclient`'s and the other commands' printed output.** Steps 02 and 10
  listed rendered output from the command-line tools. No group at this step
  reads any program's stdout or stderr for its content: `ct_server`'s output is
  redirected to a file and never examined, and the only toolchain output read
  is the build failure of item 3. The commands are checked here only by having
  to build.
* **The `configs/` corpus.** Earlier steps shipped a directory of configuration
  files in the workdir and checked verdicts over it. This step's workdir has no
  `configs/`; every configuration a group at this step checks is one that group
  wrote itself, in process or on disk for a live server.
* **Step 07's fatality row for non-minimal INTEGER.** Restated above,
  inverted: it is fatal at v1.0.21 and **non-fatal** at v1.2.0, because
  the lax retry landed at v1.1.0. Do not read the non-`NULL` RSA
  `AlgorithmIdentifier` parameters row the same way — that one did *not*
  invert. It was already non-fatal at v1.0.21, and this step checks it as a
  second, unrelated violation that classifies the same way rather than as a
  change. The rows that really were fatal at v1.0.21 are non-minimal INTEGER
  (in the serial and in an RSA modulus), a negative RSA modulus, and a
  malformed or zero-length OID inside a present `extnValue`.

None of this is a licence to delete anything. Section 6 lists what is
**compared**; section 9 lists what is out of scope. A surface that appears in
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
| `inherited-logclient-signature-checked` | gate |
| `inherited-rfc6962-leaf-hash` | gate |
| `inherited-tls-codec` | gate |
| `inherited-x509-nonfatal-errors` | gate |
| `cache-factory-validation` | requirement |
| `cache-lru-issuance-chain` | requirement |
| `cache-noop-issuance-chain` | requirement |
| `config-ctfe-storage-validation` | requirement |
| `configpb-issuance-chain-storage-fields` | requirement |
| `ctfe-backend-error-http-status` | requirement |
| `ctfe-instance-cache-selection` | requirement |
| `ctfe-instance-key-consistency` | requirement |
| `ctserver-cache-flags` | requirement |
| `ctserver-shared-private-key` | requirement |
| `jsonclient-post-redirect-guard` | requirement |
| `leaf-both-layouts-coexist` | requirement |
| `leaf-extradata-chain-hash` | requirement |
| `leaf-rehydration-round-trip` | requirement |
| `loglist2-removed` | requirement |
| `storage-issuance-chain-interface` | requirement |
| `types-issuance-chain-hash-codec` | requirement |

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

* **The storage saving itself.** The stated point of the deduplication is at
  least a third less backend storage for a log that opts in. Nothing weighs a
  database, and nothing opens one. What is checked is the leaf layout, the hash
  that replaces the chain, and the round trip back to a byte-identical
  `extra_data` — the last of those as section 6's equation, over a
  driver-supplied `storage.IssuanceChainStorage`.
* **The front end's own rehydration, driven over HTTP.** Section 6's equation
  is checked; `get-entries` *performing* it is not. Reaching that path means a
  log configured for the CTFE backend, which means a connection string the
  storage factory accepts, which means a live MySQL server — and no group runs
  one (SCOPE-DB). So every `get-entries` case in this step reads a classic
  stored leaf on the Trillian-gRPC arm, and the hashed arm is checked on the
  encoders, the store interface and the equation that joins them. This is a
  deliberate scope limit, not an oversight: a solution still has to build both
  layouts and make them reconcile, but the HTTP handler that calls the
  reconciliation is only checked on the arm a verifier can reach.
* **An LRU entry actually expiring.** The TTL is half of `cache.Option` and is
  checked where it can be: the factory refuses a negative one, a non-positive
  one means *no expiry*, and `--cache_ttl` must exist with its `-1s` default.
  What no group does is wait one out. Observing an expiry means sleeping past
  a deadline, and an in-scope assertion that turns on the wall clock is forbidden
  here (SCOPE-TIME, section 8) — it would make the check depend on how
  loaded the machine was. Your cache must still honour the TTL it is handed,
  because the next milestone is built on this tree, but at this step it is the
  *configuration* of the bound that is checked and not the moment it fires.
* **`getentries_metrics`.** This milestone adds the flag and your tree must
  carry it; neither its default nor what it exports is read by a group here.
* **`ct_hammer` generating load.** It needs live logs. The withdrawal of
  `seed` is required of your tree (CLI.md §4.8) but is not inspected either:
  no group at this step reads `ct_hammer`'s flag set.
* **Flag sets and the program list counted as *totals*.**
  `ct_server` must present exactly the 23 flags of CLI.md §2.1 — nineteen
  inherited, four new — and the module must still build every program
  v1.1.4 had, but no group asserts either as a **number**. The three
  cache flags are each observed on their own name and behaviour, and the
  fourth is the bullet above. The program set *is* checked, and by name: the
  `build-clean` gate group of section 4 requires each of the eighteen
  basenames CLI.md §1 lists at v1.2.0 to be present, executable and able
  to answer a `--help` request, so a program that no longer builds fails the
  step rather than a requirement group. What that group
  does not do is count: it never lists the build directory, never compares its
  size with CLI.md §1's total, and never asserts that the set holds nothing
  else, so an extra `package main` directory of your own has no effect at this
  milestone — the requirement in section 4 that none arrives at v1.2.0
  still stands, it is simply not what is checked here.
* **The submission proxy, including its new option surface.** Unchanged since
  earlier steps in what it sends: the ability this release gives a caller to
  switch off root-compatibility checking is observable in the type and function
  surface of `submission`, not in anything sent to a log. The type surface is
  out of scope here either. `submission.NewDistributor` becoming variadic with a
  trailing `...DistributorOption`, and the arrival of the `DistributorOption`
  interface and of `DisableRootCompatibilityCheckingDistributorOption` alongside
  it (RELEASE-MAP.md section 12, CLIENT-API.md section 7.2), are real
  v1.2.0 changes that no group at this milestone compiles against or
  observes. Make the change or leave the old four-argument signature in place --
  neither matters here. Nothing imports the package from a test at this
  step.
* **Anything dated after v1.2.0.** All four `internal/witness/cmd/`
  programs are still here and still built; they leave at the very next step,
  along with the split of the issuance-chain service into direct and indirect
  halves, a second storage driver, TLS transport and tiled logs.

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

Everything this milestone adds is **additive**, and that is the whole of its
difficulty. There is no inversion here and no withdrawal worth the name: the
new leaf layout does not replace the old one, it stands beside it, and which
one a log uses is a per-log configuration choice that defaults to the layout
you already have.

So the failure this step actually catches is over-application. A tree in
which every log suddenly writes a hash into its extra data has broken every
log that came before it, and it has broken them silently, because the JSON a
client reads is identical either way. The contract is that a log configured
for the Trillian backend — which is the default, value 0, and what every
configuration written in steps 01 to 11 says — keeps the whole chain in the
leaf, byte for byte, *even when a cache is configured and a store exists*.
The two paths must agree on the Merkle leaf, the leaf hash and the identity
hash, and disagree on exactly one field.

Around that sit the smaller pieces: a storage interface and a cache interface
with two implementations, four new flags and two new configuration fields,
two key-consistency refusals at start-up (one per log, one over the whole
configuration), the second log-list generation deleted and one field added to
the one that survives, two cells of the gRPC-to-HTTP map moving at once, and
one guard on the client's upload path.
