# TEST_INTERFACE — step 03 of 13 (certificate-transparency-go v1.0.10)

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

### 3.5 The compile-time gate: a method the pinned manifest has taken away

Section 7 warns that proof of life is not always free and points here for
whether that applies at this milestone. **It applies here**, and in the second
of the two shapes section 7 names — which is the one the obvious check misses.
Section 2's manifest still provides every module the inherited tree imports,
so comparing the manifest against the tree's import list comes back clean; what
moved is an API *inside* a module that is still provided, and the build fails
anyway.

The pinned `github.com/google/trillian` advances at this step, and at the
revision section 2 names, `testonly/integration.LogEnv` no longer has a
`CreateLog` method. Exactly one inherited file calls it —
`trillian/integration/logenv.go`, in `NewCTLogEnv`, where the provisioning loop
reads `logID, err := logEnv.CreateLog()` and assigns `cfg.LogId = logID`. That
one line is the whole of the breakage: `go build ./...` in `/app` fails there,
section 1's build obligation is not met, `ct_hammer` — the one program whose
`package main` sits under that package — is not produced, and `build-clean`,
which section 7's table lists as **GATE**, fails on the very first run, before
you have changed anything. Pinning an older `trillian` back is not an option:
section 2 fixes the dependency universe and checking is offline against a module
cache that does not hold it.

The upstream repair at v1.0.10 provisions the tree through Trillian's admin
client instead of through `LogEnv`:

    tree, err := client.CreateAndInitTree(ctx,
        &trillian.CreateTreeRequest{Tree: stestonly.LogTree},
        logEnv.Admin, nil, logEnv.Log)
    ...
    cfg.LogId = tree.TreeId

with `github.com/google/trillian/client` and
`stestonly "github.com/google/trillian/storage/testonly"` added to the file's
imports. Nothing checks *how* you repair it: no group links
`trillian/integration`, and the only thing any group asks of `ct_hammer` is
that it exists and prints its own usage. What is checked is that the tree
compiles and that section 4's ten binaries come out of `go build ./...`.

Two consequences, because neither is guessable from section 7 alone:

* A no-op here satisfies none of the new work: the gate itself fails. Section 7's
  note on no-ops covers both cases; v1.0.10 is the second one.
* This is migration work, not milestone work. It adds no requirement of its
  own — the gate groups are mandatory — and it is the precondition for the rest
  of this step, so do it first. `WORKSPACE_HANDOFF.md`'s checkpoint
  step 2 (`go build ./... && echo BUILD-OK`) surfaces it on your first build.

## 4. Command-line programs the verifier runs

Unchanged from step 02: **10** `package main` directories (CLI.md §1), and the
same ten binaries must come out of `go build ./...`.

| program | directory | what a group does with it |
| --- | --- | --- |
| `ct_server` | `trillian/ctfe/ct_server/` | started with a generated `LogMultiConfig` against loopback backend stubs, as before. Four groups drive that process end to end over HTTP: three of them against a config naming **one** log and one stub, and one — the group that re-checks step 02's routing milestone — against a config naming **two** logs and two separate stubs. The per-log separation of the signature cache (§10) is checked *in process* instead, with two `LogContext`s in one program, because it is a property of the library and not of the command. It is also run with `--help`: at v1.0.10 the `ctfe` package's new `by_range` (Bool, default `false`) reaches its command line and its usage output — CLI.md §2.1 |
| `certcheck` | `x509util/certcheck/` | run with `--help`; at v1.0.10 its flag set gains `ignore_unknown_critical_exts` (Bool, default `false`) — CLI.md §4.3 |
| `crlcheck` | `x509util/crlcheck/` | run with `--help`; at v1.0.10 `verbose` is **withdrawn** and `strict` and `cert` (both Bool, default `false`) are added. Its positionals were already "file or URL" at v1.0.5 and are unchanged here — CLI.md §4.3 |
| the other seven | see CLI.md §1 | as at step 02 |

A group that reads `--help` compares it at the level CLI.md §4.3 documents:
the presence or absence of a flag **name**, and its type and default as the
`flag` package prints them. The one-line human descriptions beside each flag
are out of scope and you may word them as you like — `ct_server`'s `log_config`
included, whose text says JSON at v1.0 and is wrong there, and is still not
read (CLI.md §2.1).

The groups that start `ct_server` pass exactly these flags, all of them
documented in CLI.md §2.1: `--log_config=<generated LogMultiConfig>`,
`--log_rpc_server=` (empty, which is what selects the multi-config reader —
CLI.md §2.2 step 3), `--http_endpoint=127.0.0.1:<a free loopback port the
group picked before starting the process>`,
`--metrics_endpoint=` (empty, so metrics share the HTTP port and no group
binds the default), `--get_sth_interval=0`, `--rpc_deadline=30s` and
`--logtostderr`. Read the `get_sth_interval` note in CLI.md §2.1 before you
start the periodic internal `get-sth`: **0 disables it**, and a process that
constructs a ticker from the value before testing it panics and never binds,
which fails those four groups at start-up rather than on anything they
assert. Readiness is `GET /<prefix>/ct/v1/get-roots` answering 200, which
touches no backend (ARCHITECTURE.md §2). The group polls for that answer and
abandons the group after **90 seconds** without one; the deadline is a bound
on waiting and is never asserted on (§8), so nothing you do is checked on how
quickly the process comes up — only on its coming up at all.

Four rules hold for every program and are checked wherever a group runs one
(CLI.md, the preamble on flag grammar and exit status — read it before you
write a `main`):

* an unrecognised flag makes the standard `flag` package print `Usage of
  <prog>:` and the flag list **to stderr** and exit **2**;
* `-h` and `--help` are *not* unrecognised: the same package prints the same
  `Usage of <prog>:` block and exits **0**. Groups use that difference —
  `--help` on a program that parses its flags is expected to exit 0 and to
  open with `Usage of <path the group invoked>:`. The one exception is
  `chainfix`, which reads its positional arguments before calling
  `flag.Parse` and so reaches neither path: with fewer than three arguments it
  panics on the index and exits **2**, `--help` included. CLI.md §4.6 states
  this and says not to add an argument-count check; a group holds you to it;
* a missing *required* flag is a bare diagnostic on stderr and exit **1**, with
  no usage dump;
* `glog.Exit*` and `log.Fatal*` exit **1**; `glog.Fatal*` exits **255**.

`ct_server` gains the duplicate-prefix check as an explicit validation error at
v1.0.10 (CONFIG.md §4.4): at v1.0.5 the same file failed later,
when two logs tried to register the same HTTP path. The verdict is the same;
the diagnostic and the moment are not.

## 5. Packages and symbols the verifier links against

Step 02's `TEST_INTERFACE.md` is replaced by this file, so the surface it
listed is repeated here rather than referred to. Drivers at this step link
against exactly these packages, and against nothing else in the tree:

| import path | what the drivers use |
| --- | --- |
| `…` (root) | the RFC 6962 structures and path constants — `AddChainPath`, `GetSTHPath`, `GetSTHConsistencyPath`, `GetEntriesPath`, `GetProofByHashPath`, `GetEntryAndProofPath`, `AddChainRequest`, `SignedCertificateTimestamp`, `DigitallySigned`, `MerkleTreeLeaf`. The path constants carry the leading `/` and the `ct/v1/` segment, exactly as HTTP-API.md §1 spells the endpoints |
| `…/tls` | the codec of TLS-CODEC.md, `Marshal`/`Unmarshal` |
| `…/x509`, `…/x509util` | the fork and its printers, including the structured `x509.Error`/`x509.Errors` surface of X509-FORK.md §5.2 |
| `…/merkletree` | the tree hasher and the proof verifiers, each declared for this release by MERKLE.md §4.1: `NewTreeHasher` and `TreeHasher`'s `HashEmpty`/`HashLeaf`/`HashChildren`, `NewMerkleVerifier` and `MerkleVerifier`'s `VerifyInclusionProof`, `VerifyInclusionProofByHash`, `RootFromInclusionProof` and `VerifyConsistencyProof`. The package is untouched by the v1.0.5 → v1.0.10 transition, so these are the same signatures step 02 linked against |
| `…/client`, `…/jsonclient` | `client.New`, `LogClient.GetSTH`, `jsonclient.Options`, `client.RspError`, and the three request helpers in their v1.0.5 shape returning `(*http.Response, []byte, error)` |
| `…/trillian/ctfe`, `…/trillian/ctfe/configpb` | `LogConfigFromFile`, `MultiLogConfigFromFile`, `ToMultiLogConfig`, `ValidateLogMultiConfig`, `SetUpInstance` in its v1.0.5 shape, the generated messages' `Get<Field>()` accessors (CONFIG.md §1.2), and the new surface in the table below |
| `…/trillian/util` | the time source, below |

`logid` is no longer linked by a driver at this step, but step 02's groups
still stand behind you and nothing in it may be withdrawn.

Drivers also link against the signature construction of TLS-CODEC.md §5.3 and
§5.5 and against `trillian/ctfe`'s validation and issuance entry points as
before.

This milestone also **adds** an exported request-logging interface and, with
it, the in-process construction path that five groups use to drive handlers
without a network. These are checked, so they are named here in full. The
authority for each is CLIENT-API.md §8; this table is the boundary the
verifier links against.

| symbol | package | shape |
| --- | --- | --- |
| `NewLogContext` | `trillian/ctfe` | `NewLogContext(logID int64, prefix string, validationOpts CertValidationOpts, rpcClient trillian.TrillianLogClient, signer *crypto.Signer, instanceOpts InstanceOptions, timeSource util.TimeSource) *LogContext` — seven parameters, in that order; `crypto` is `github.com/google/trillian/crypto` |
| `(*LogContext).Handlers` | `trillian/ctfe` | `Handlers(prefix string) PathHandlers` |
| `PathHandlers` | `trillian/ctfe` | `map[string]AppHandler`, keyed by the full path including the leading `/` and the prefix |
| `AppHandler` | `trillian/ctfe` | struct with exported fields `Context *LogContext`, `Handler func(context.Context, *LogContext, http.ResponseWriter, *http.Request) (int, error)`, `Name EntrypointName`, `Method string`; it is an `http.Handler` — `ServeHTTP(w, r)` |
| `InstanceOptions` | `trillian/ctfe` | gains a `RequestLog RequestLog` field at v1.0.10, beside `Deadline`, `MetricFactory` and `ErrorMapper` |
| `RequestLog` | `trillian/ctfe` | **new at v1.0.10**; ten methods, listed in CLIENT-API.md §8.5 |
| `DefaultRequestLog` | `trillian/ctfe` | **new at v1.0.10**; the shipped implementation, usable as `new(ctfe.DefaultRequestLog)` |
| `TimeSource`, `NewFixedTimeSource` | `trillian/util` | `TimeSource` has `Now() time.Time`; `NewFixedTimeSource(t time.Time) *FixedTimeSource`. The import path is `github.com/google/certificate-transparency-go/trillian/util` — the fork's own, not Trillian's |
| `CertValidationOpts` | `trillian/ctfe` | the third `NewLogContext` parameter. Every field of it is **unexported** at v1.0.10 and it has no exported constructor, so the only value a driver can build is the zero value `ctfe.CertValidationOpts{}` — a context that admits no chain. That is deliberate and sufficient: the in-process groups drive the read entrypoints and the pre-validation part of `add-chain` |
| `monitoring.InertMetricFactory` | `github.com/google/trillian/monitoring` | what a driver puts in `InstanceOptions.MetricFactory`; usable as `monitoring.InertMetricFactory{}` and observed by nothing |
| `trillian.TrillianLogClient`, `trillian.SignedLogRoot` | `github.com/google/trillian` | the backend interface `NewLogContext` takes and the root it answers with. A driver supplies its own client and fills `TimestampNanos`, `TreeSize`, `RootHash` and `LogId` on the root it returns; §6 says which of those the front end must put where |
| `x509.Certificate.RawSCT`, `.SCTList` | `x509` | **new at v1.0.10**; X509-FORK.md §4.5 |
| `x509.SerializedSCT`, `x509.SignedCertificateTimestampList` | `x509` | **new at v1.0.10**; the two carrier structs of X509-FORK.md §4.5, each with its one exported field |
| `x509.CreateCertificate` | `x509` | signature unchanged; at v1.0.10 it also **synthesises** the SCT-list extension from whichever of a template's `RawSCT` and `SCTList` is set (X509-FORK.md §4.5), which a group checks by round trip |
| `x509util.RevocationReasonToString` | `x509util` | **new at v1.0.10** (CLIENT-API.md §5.2 arrival table). A driver takes it as a **function value**, so only its arity and its `string` result are pinned, and calls it with an integer reason code. The wording it returns is diagnostic and is **not** checked; what is checked is that two different codes do not describe themselves identically and that an unassigned code still returns something |

A driver builds a context with `NewLogContext`, takes `Handlers(prefix)`, and
calls `ServeHTTP` on the entry it wants — through the wrapper, never the
`Handler` field on its own — so the argument order, the `TimeSource` injection
point and the `AppHandler` field names are all part of the checked surface. Two
consequences follow, and both are checked:

* `ServeHTTP` drives `InstanceOptions.RequestLog` around **every** request, so
  a driver always supplies one. The groups that do not observe the hooks
  supply `new(ctfe.DefaultRequestLog)`; the one that does supplies its own
  ten-method value. A `RequestLog` that has to be defaulted by the wrapper
  before it can be used is not what CLIENT-API.md §8.2 describes.
* Because the driver goes through the wrapper, what it reads back off the
  recorder is the **whole response** an HTTP client would have seen — status,
  headers and body, `Content-Type: application/json` included (HTTP-API.md
  §2). Where in your code that header is set is yours; that it is on the
  response the wrapper finished with is checked.

The value hooks — `StartAndEnd`, `LeafHash`, `TreeSize`, `LeafIndex`,
`AddDERToChain` — report what an entrypoint parsed out of the request, and
CLIENT-API.md §8.8's "only when the value concerned parsed successfully" has a
second half a group relies on: each one fires **as soon as the value is in
hand and before the backend round trip that uses it**, so the hooks are
reported whatever the backend then answers. A request whose parameters do not
parse reports no value hook at all — just `Start`, `LogPrefix` and the
`Status` the rejection produced.

## 6. Formats crossing the boundary

Steps 01 and 02 each listed these, and step 02's `TEST_INTERFACE.md` is
replaced by this file, so the list is repeated here in full rather than
referred to:

* **TLS encodings** — `MerkleTreeLeaf`, `TimestampedEntry`, `DigitallySigned`,
  the SCT signature input and the `TreeHeadSignature` input are compared
  **byte for byte** against vectors the *verifier* holds. Those vectors are not
  in `ct-spec/`; they were produced from the field orders, tag parameters and
  widths of TLS-CODEC.md §2, §3 and §5, which determine the encoding
  completely.
* **JSON bodies** — field names, types and base64 padding exactly as
  HTTP-API.md §3 and §4 give them. Binary fields are standard base64 with
  padding; an empty list is `[]`, never `null`.
* **Leaf hashes** — `SHA-256(0x00 ‖ TLS(MerkleTreeLeaf))`, compared as bytes
  (MERKLE.md §1).
* **PEM and key messages** — root certificate files are given to `ct_server`
  by path. The log's private key is a `google.protobuf.Any` routed through the
  key-handler registry (CONFIG.md §2.3), and the configurations the groups at
  *this* step generate inline the key as a **`keyspb.PrivateKey` carrying raw
  DER**. `keyspb.PEMKeyFile` with its `path` and `password` is still used by
  parsed-only fixtures and by earlier steps' groups, so both handlers must be
  registered; CONFIG.md §2.3's "Do not assume either one" is the rule.
* **Configuration** — the log config is a protocol-buffer message parsed as
  **text proto and nothing else** through v1.1.0 (CONFIG.md §4.2), so the JSON
  rendering of a valid message is a parse failure at v1.0.10 whatever its
  contents. Unknown field names are refused. A group checks the content
  verdict of a config it has actually loaded, not the file's encoding.
* **The multi-backend configuration message.** `LogMultiConfig` with its
  `LogBackendSet` and `LogConfigSet`, as CONFIG.md §1 and §3 give them:
  `backends` is one `LogBackendSet` holding repeated `backend`, and
  `log_configs` is one `LogConfigSet` holding repeated `config`. A backend's
  `backend_spec` is an opaque non-empty string at validation time, and a group
  may hand you an address that answers nothing.
* **Backend routing.** A log reaches the backend its `log_backend_name` names
  and no other, and the binding is live for the life of the process: a group
  runs two logs over two backends at once and moves one backend's root
  underneath it.
* **The two RFC 6962 extension sections of a certificate dump.** From
  v1.0.10 `x509util.CertificateToString` renders the poison and the
  SCT-list extension itself instead of leaving either in the unhandled-OID
  bucket, and CLIENT-API.md §5.2 prints that rendering **complete** — both
  title lines, the ` critical` suffix rule, the five SCT fields in order, the
  per-entry numbering, the fallback for an entry that will not TLS-decode and
  the hex-dump rule — with nothing elided and with the indentation called out
  as significant. It is a *format* and not a message, so a group compares
  those two sections **byte for byte**, spaces included: 12 spaces before
  each title line, 14 before `SCT [i]:`, 18 before the five fields and before
  the hex dump that replaces them for an undecodable entry, 20 before the hex
  dump of a signature, `%02x:` per byte with the trailing colon written on the
  last byte too, and **16 bytes per line**. Only those two sections are
  compared — the rest of the certificate dump around them is not, and neither
  is the wording `x509util.RevocationReasonToString` returns (§5).

**That list is exhaustive.** It is the whole of what §3.3's exception reaches
at this step: no group compares any other body, message or output format byte
for byte. Three kinds of string are compared outside it, all under §3.3's
ordinary identifier rule, and they are named here so the boundary is not
guesswork:

* **Flag names**, as the exact declared set §4 describes — identifiers, never
  the one-line human descriptions printed beside them.
* **Two configuration diagnostics, as prefixes only** — `empty backend name`
  and `duplicate backend name`, both given verbatim by CONFIG.md §4.4. What
  follows each prefix is the offending message rendered by Go's default
  formatter, which no document fixes, so nothing past the prefix is compared.
  No other validator diagnostic at this step is read for its wording: every
  other configuration case checks the accept/reject verdict alone.
* **`Content-Type: application/json`** as a header value, which HTTP-API.md §2
  fixes and §5 repeats.

One addition in kind rather than in format: a group may compute the **exact
signature input** for two different submissions — two different timestamps,
two different certificates, the same log, or the same certificate under two
different logs — and require that what your code signs over differs in the
documented way for every pair that differs at all.

And one field provenance, because more than one group depends on it and
HTTP-API.md §4.1 gives only the type: the `timestamp` a `get-sth` response
carries is **the backend root's `TimestampNanos`, converted to milliseconds**
— not the personality's own clock. The three fields of the tree head a group
compares, and the three that enter the `TreeHeadSignature` input, are all read
out of the `SignedLogRoot` the backend answered with: `TreeSize` →
`tree_size`, `RootHash` → `sha256_root_hash`, `TimestampNanos` / 1e6 →
`timestamp`. The injected `TimeSource` dates *SCTs* on the `add-chain` path
(RFC 6962 §3.2); it does not date tree heads.

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
| `inherited-backend-routing` | gate |
| `inherited-merkle-verifiers` | gate |
| `inherited-sct-signature-inputs` | gate |
| `inherited-tls-codec` | gate |
| `cli-flag-surface` | requirement |
| `config-duplicate-backend-spec` | requirement |
| `config-duplicate-prefix` | requirement |
| `config-duplicate-tree-id` | requirement |
| `config-empty-log-prefix` | requirement |
| `config-notafter-range` | requirement |
| `ctfe-requestlog-hooks` | requirement |
| `sth-signature-cache-distinct-inputs` | requirement |
| `sth-signature-cache-invisible-in-response` | requirement |
| `sth-signature-cache-not-shared-between-logs` | requirement |
| `sth-signature-cache-over-http` | requirement |
| `sth-signature-cache-stable` | requirement |
| `x509-embedded-sct-list` | requirement |
| `x509util-ct-extension-display` | requirement |

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

* **The cache's data structure, eviction policy, size or hit rate.** Only two
  consequences are checked, both in §10: an unchanged tree head re-served
  byte-identically, and any change to a signing input producing a signature
  that verifies over that input and no other. How you hold the cached value is
  yours. No group inspects a cache directly, reads a counter, or times
  anything.
* **Any cache on the SCT path.** v1.0.10 caches tree-head signatures only.
  Groups require SCT signatures over *distinct* inputs to be distinct — which
  recomputation satisfies — and never require two SCTs to be identical.
  The rule in §10 is stated for both paths, but only the tree-head path is
  *required*. v1.0.5 already signs every distinct SCT input distinctly,
  so requiring that half would demand work already done; it is carried by a
  **gate** group instead (§7), which re-checks for a regression. Breaking it
  means the step is not satisfied, so read
  §10's rule as a constraint on the SCT path and as the checked capability on
  the tree-head path.
* **`AddCertToChain` and the order it reports a chain in.** `AddDERToChain`
  fires once per submitted element, in submission order, *before* the chain is
  validated, and that is checked. `AddCertToChain` fires only after a chain has
  validated against a configured root pool, which an in-process driver cannot
  set up — `CertValidationOpts` has no exported field and no exported
  constructor at v1.0.10 (§5). No group observes `AddCertToChain` or its
  leaf-first, root-last order. Implement both hooks as CLIENT-API.md §8.8
  describes them; only the first is measured here.
* **What the three new command-line flags do at run time.** `certcheck`'s
  `ignore_unknown_critical_exts`, `crlcheck`'s `strict` and `cert`, and
  `ct_server`'s `by_range` are checked on their **declaration** — name, type and
  default as CLI.md §4.3 and §2.1 give them — and not on their effect.
  `by_range` selects a backend RPC the stub backends at this step do not serve
  (SCOPE-BACKEND), and the other two need CRL and certificate material the
  pinned corpus does not carry. Wire them to the behaviour CLI.md describes
  anyway: a later step may measure it, and the declaration is worthless
  without it.
* **Anything dated after v1.0.10.**

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

This milestone is a bug fix that carries a rule: **no two distinct signing
inputs may ever share a signature**, and the mechanism that makes signing
cheaper must be invisible in the output.

The advisory behind it is v1.0.7. A front end that caches a computed signature
under a key that does not capture the whole signing input will, sooner or
later, return a signature computed over one thing as the signature over
another. The consequence is not a slow log; it is a log that has signed
something it never saw.

So the property to establish is: for any two signing inputs that differ in any
byte, the signatures differ correspondingly. Read TLS-CODEC.md §5.3 and §5.5
for exactly which bytes are in each signature input — the timestamp, the entry
type, the entry, the extensions and, for a tree head, the size and root hash —
and make sure nothing that enters those bytes is missing from whatever
identifies a cached result.

**The tree-head cache itself is required at v1.0.10, and it is checked.**
The fix that carries the rule is a per-log cache of the last STH signature:
the front end keeps the serialised tree head it last signed together with the
signature it produced, and when the tree head it is about to sign is
byte-identical to that one it returns the stored signature instead of signing
again. Because the corpus log key is ECDSA P-256 and Go's ECDSA is randomised,
this is directly observable and that is how it is in scope: **two `get-sth`
calls over an unchanged tree must return the byte-identical
`tree_head_signature`**, where recomputation would return two different ones.
Per *log*, not per process — two logs in one `ct_server` do not share the
cache, and a tree head that changes in any field invalidates it.

What "invisible" means here is narrow and worth stating exactly: the cache
adds **no field, header or status** to any response, and it never returns a
signature that does not verify over the tree head actually served. It is not
invisible in the *value* of `tree_head_signature`, and it is not meant to be.

Everything else at this step is small. Keep step 01 and step 02 working.
