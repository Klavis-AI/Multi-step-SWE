# TEST_INTERFACE — step 11 of 13 (certificate-transparency-go v1.1.4)

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

## 4. Command-line programs the verifier runs

v1.1.4 is one program **narrower** than v1.1.3. `findlog` was
the v1 log-list tool and it leaves with the package it was built on; nothing
arrives to replace it, and nothing else in the inventory moves (CLI.md §1,
§4.5).

| program | directory | what a group does with it |
| --- | --- | --- |
| `ct_server` | `trillian/ctfe/ct_server/` | started as a subprocess against a config a group writes and driven end to end over a loopback socket; its **19**-flag set is unchanged from v1.1.3 (CLI.md §2.1) |
| witness `config` | `internal/witness/cmd/witness/config/` | pointed at a pinned log list without touching the network; checked on the **exact bytes** of the file it writes and on the flag set it accepts |
| `findlog` | `loglist/findlog/` — **gone** | building that import path must fail, and fail *naming it*, against a positive control that builds (section 3.3) |
| the others | see CLI.md §1 | built and enumerated; `ctclient`, `sctcheck`, `sctscan`, `ct_hammer`, `migrillian`, the submission pair, the other three `internal/witness/cmd/` programs (`witness`, `feeder` and the witness `client`; with the `config` row above, **four** of them at v1.1.4) and the rest must all still exist — **18** programs in all, v1.1.3's nineteen less `findlog` (CLI.md §1) |

The flag-grammar rules of step 06 are unchanged and still checked: `--help`
prints usage and exits **0** for every program that parses its command line
with the `flag` package or with the command framework; an unrecognised flag is
`Usage of <prog>:` on stderr and exit **2**; a missing required flag is a bare
diagnostic on stderr and exit **1**; `Exit*` exits **1** and `Fatal*` exits
**255** — and the submission server's unrecognised `--policy_type`, which is
fatal, is the one place in the product where a bad flag *value* takes the
second of those (CLI.md §4.9). `chainfix` stays outside all of it, as it has at
every release: no flag package, three positional arguments read straight out of
`os.Args` and no argument-count check, so a short command line runs off the end
of that slice and exits **2** (CLI.md §4.6). What moves at v1.1.4 is *which library
supplies them*: glog gives way to klog throughout the product, the exit
statuses survive the swap untouched, and the logger's own flag names do not —
a program that installs the new logger's flag set gets a different set, and a
program that installs none carries no logging flags at all (CLI.md, the
flag-grammar and exit-status preamble).

The log-list defaults move with the package: `ctclient`'s persistent
`--log_list` and `sctcheck`'s `--log_list` take the v3 all-logs constant
(CLI.md §3.2, §4.4), and the submission server's `--loglist_path` takes the v3
URL (CLI.md §4.9).

One flag of that server has behaviour a group depends on, so it is written out
here rather than left to its name. **`--dry_run`** (CLI.md §4.9, default
`false`) replaces the log client the proxy would build for each selected log
with a **stub**, and the stub is what makes the proxy runnable with no network
at all. It is not a short circuit around submission: the server still reads its
log list, still applies the policy, and still calls every log the policy
selected. Under it,

* no request leaves the process, at any endpoint;
* **`get-roots` succeeds for every selected log and answers with an empty root
  set** — so each of those logs, and no others, records a
  `last_get_roots_success` sample (§6.1 item 11), and the proxy's root
  information is *complete* rather than merely missing;
* and complete-and-empty is a refusal: no submitted chain can be rooted
  against it, so the distributor rejects **every** `add-chain` and
  `add-pre-chain` before any log is asked to add anything, and the proxy
  returns **502** with the §6.1 item 10 prefix. A dry run exercises log
  selection and the proxy's failure frames; a successful submission is not
  reachable through it.

That is the behaviour at every release the flag exists at, v1.1.0 onwards, and
it does not change here — only the parser behind `--loglist_path` does.

## 5. Packages and symbols the verifier links against

| import path | what the drivers use |
| --- | --- |
| `…/loglist` | **nothing — it must not resolve.** A driver importing it must fail to compile with the toolchain naming that exact path, and the path must not be a build target either (CLIENT-API.md §4.4) |
| `…/loglist/findlog` | **nothing — it must not resolve either.** The v1 list's search command is withdrawn with the package it searched, so `go build` on that exact path must fail *naming it*, against a positive control that must build **and** must produce a real executable: `…/ctutil/sctcheck`, a product command this release keeps. Listing the tree must agree — `go list` over `…/loglist/...` matches **no** package at all while `…/loglist3/...` still matches, so emptying the directory is not enough (CLI.md §4.5) |
| `…/loglist3` | the log-list package everything now speaks: `LogList`, `Operator`, `Log`, `LogStates` and `LogStatus`, `NewFromJSON`, `LogListURL` / `AllLogListURL`, the `FindLogBy…` family with `FuzzyFindLog`, and the selection helpers `SelectByStatus`, `Compatible`, `RootCompatible`, `TemporallyCompatible` and `LogRoots` (CLIENT-API.md §4.3) |
| `…/loglist2` | still present, still compiling, and with no consumer left anywhere in the product (RELEASE-MAP.md §11; the directory listing that shows it surviving is §14) |
| `…/ctutil` | **`NewLogInfo`** and **`LogInfoByKeyHash`** retyped onto v3, the `LogInfo` surface behind them, and the merge delay read from the v3 list's merge-delay field — which carries **an integer number of seconds**, and reaches `LogInfo.MMD` as the `time.Duration` of that many seconds (CLIENT-API.md §2.1) |
| `…/ctpolicy` | `CTPolicy` whose `LogsByGroup` takes a `*loglist3.LogList`, satisfied by both `ChromeCTPolicy` and `AppleCTPolicy`; `BaseName` and `BaseGroupFor`; `LogPolicyData` with `TotalLogs` and `GroupByLogs`; and all of `LogGroupInfo` — `Name`, `LogURLs`, `MinInclusions`, `IsBase`, `LogWeights`, `SetLogWeights`, `SetLogWeight`, `GetSubmissionSession`. The group names, the inclusion-count step function and the weighting rules are given in **CLIENT-API.md §7.1**, which is where a driver's expectations come from |
| `…/submission` | over the v3 types: `NewDistributor`, the `LogClientBuilder` type **and `BuildLogClient`, the concrete default builder of that type**, `AddChain`, `AddPreChain`, `RefreshRoots`, `SubmitToLog`, `AssignedSCT` with its `LogURL`, `ASN1MarshalSCTs`, and the log-list pair `NewLogListRefresher` / `LogListRefresher` and `NewLogListManager` / `LogListManager` with their methods (CLIENT-API.md §7.2). One of those methods needs its cancellation contract spelled out, because a driver relies on it to stay deterministic: **`LogListManager.Run(ctx, interval)`** arms the manager — it installs the package's metrics and starts the periodic refresh under `ctx` — and cancelling `ctx` stops the *schedule*, nothing else. A `Run` handed an already-cancelled context therefore schedules no refresh at all, and leaves the manager fully usable: `RefreshLogList`, `GetTwoLatestLogLists`, `ProduceClientLogList`, `Source` and `LastJSON` behave afterwards exactly as they would have before |

Steps 01 to 10 named more than the paths above, and you do not have their
`TEST_INTERFACE.md` — this file is the whole of your contract. So the rest of
that surface is written out here rather than referred to, and every path in it
has been checked against v1.1.4 before being listed. This table and the one
above are together the complete set of product packages a driver at this step
links against; nothing else has to be guessed at.

| import path | what a driver at this step names |
| --- | --- |
| `…` (root) | the RFC 6962 structures and the wire types around them: `MerkleTreeLeaf` with `Version`/`V1`, `LeafType`/`TimestampedEntryLeafType` and `TimestampedEntry`, `LogEntryType` with `X509LogEntryType` and `PrecertLogEntryType`, `ASN1Cert`, `CertificateChain`, `PrecertChainEntry`, `DigitallySigned`, `SignedCertificateTimestamp`, `SignedTreeHead` and `LeafEntry`; the builders and decoders `MerkleTreeLeafFromChain`, `MerkleTreeLeafFromRawChain(rawChain []ASN1Cert, etype LogEntryType, timestamp uint64) (*MerkleTreeLeaf, error)` — its DER-chain form, which CLIENT-API.md §3 gives with its three-entry limit and its parse failure — `CreateX509MerkleTreeLeaf`, `RawLogEntryFromLeaf(index int64, entry *LeafEntry) (*RawLogEntry, error)` and `LogEntryFromLeaf(index int64, leaf *LeafEntry) (*LogEntry, error)` — the two converter return types being the `RawLogEntry{Index int64; Leaf MerkleTreeLeaf; Cert ASN1Cert; Chain []ASN1Cert}` and `LogEntry{Index int64; Leaf MerkleTreeLeaf; X509Cert *x509.Certificate; Precert *Precertificate; JSONData []byte; Chain []ASN1Cert}` of CLIENT-API.md §3, with `Leaf` a value and each `Chain` a slice of `ASN1Cert` — `LeafHashForLeaf` and `IsPreIssuer(issuer *x509.Certificate) bool` — true
exactly when the certificate carries the CT extended key usage, CLIENT-API.md
§3; `NewSignatureVerifier` with `PublicKeyFromPEM`; the JSON bodies `AddChainRequest`, `AddChainResponse`, `GetSTHResponse`, `GetRootsResponse` and `GetEntriesResponse`; the endpoint constants `AddChainPath`, `GetSTHPath`, `GetRootsPath` and `GetEntriesPath`, which is how a group assembles a URL rather than spelling one; and `TimestampToTime` |
| `…/tls` | `Marshal` and `Unmarshal` over the structures in the row above — the codec every byte-for-byte comparison in section 6 passes through, in both directions — together with `SignatureAndHashAlgorithm` and its `SHA256` and `ECDSA` constants, the algorithm pair inside a `DigitallySigned` |
| `…/x509` | the fork, never `crypto/x509`: `ParseCertificate`, `ParseCertificates` and `ParseTBSCertificate`; `*Certificate` with the parsed fields the fixtures are compared on; `IsFatal`; `Certificate.IsPrecertificate`, which must answer on a **nil** receiver rather than panic; `RemoveCTPoison` and `RemoveSCTList`; `MarshalPKIXPublicKey`; and `ExtKeyUsage` with `ExtKeyUsageClientAuth` and `ExtKeyUsageServerAuth` |
| `…/client` | **`AddLogClient`** — the three-call interface (`AddChain`, `AddPreChain`, `GetAcceptedRoots`) that a `submission.LogClientBuilder` returns. At v1.1.4 that builder is declared over `*loglist3.Log`, so this interface and the v3 log list meet in one signature; a driver implements the interface itself and no log is ever dialled |
| `…/jsonclient` | `New(uri, hc, Options{…})` returning `*JSONClient`, `Options` carrying the one-method `Logger`, `PostAndParseWithRetry(ctx, path, req, rsp)` — its four parameters and three return values (CLIENT-API.md §1.1) — and `RspError`, the type the non-retryable arm produces |
| `…/ctutil` | beside the retyped constructors in the table above: `VerifySCT(pubKey, chain, sct, embedded)`, which the end-to-end gate checks a live `ct_server`'s SCT with; and the `LogInfo` members a group reads — `Description`, `PublicKey`, `Client`, `Verifier`, `MMD`, and `LastSTH`/`SetSTH`, which must round-trip |
| `…/loglist2` | the row above says it survives with no consumer; a driver nevertheless **links and runs** it, to show it still works rather than merely compiles: `NewFromJSON` over a v2 document, and `Operators` on the `*LogList` it returns. The probe is built, executed, and its whole stdout compared — item 12 of §6.1 |
| `…/trillian/ctfe` | `LogConfigFromFile`, `ValidateLogConfig` and `ValidateLogConfigs`; `ValidatedLogConfig` with `PubKey`, `PrivKey`, `KeyUsages`, `NotAfterStart`, `NotAfterLimit` and `FrozenSTH`; `SetUpInstance(ctx, InstanceOptions{Validated, Deadline, MetricFactory, RequestLog})` and the `*Instance` it returns, whose **`Handlers`** map is where section 6's registered endpoint set is read from — set-up dials nothing, so a nil backend client is enough to ask an instance which endpoints it registered; `DefaultRequestLog`; and the quota pair `CertificateQuotaUserPrefix` and `QuotaUserForCert` |
| `…/trillian/ctfe/configpb` | `LogConfig` — built field by field inside a driver as well as parsed from a file, and duplicated with `proto.Clone`, so it has to stay a generated protobuf message and not merely a struct of the same shape — which is also what puts a `Get<Field>()` accessor beside every field for a driver to call (CONFIG.md §1.2) |
| `github.com/google/trillian`, and `types`, `monitoring`, `crypto/keys`, `crypto/keys/der` and `crypto/keyspb` beneath it | **not yours to write** — §3.4: the log backend is out of scope. It is named because it must keep *resolving*. `trillian.TrillianLogServer`, which the **benchmark** implements and registers with `trillian.RegisterTrillianLogServer`, together with `LogLeaf`, `QueuedLogLeaf`, `SignedLogRoot`, `Proof` and the request/response pair of every call your front end makes; `github.com/google/trillian/types`' `LogRootV1`, which that root is marshalled from; `github.com/google/trillian/monitoring`'s `MetricFactory` and `InertMetricFactory`, the value `ctfe.InstanceOptions` is handed; and `github.com/google/trillian/crypto/keyspb`'s `PrivateKey` with `github.com/google/trillian/crypto/keys`' `RegisterHandler` and `github.com/google/trillian/crypto/keys/der`'s `FromProto`, which is how a driver populates the key-handler registry section 6 describes. Mind the collision: `…/trillian/ctfe` above is the **fork's own** `trillian/` directory, a package you do own, and has nothing to do with this module |
| `google.golang.org/grpc` | `NewServer` and `*Server`. The stub backend of §3.4 is served over a loopback TCP listener the driver opens for itself and your `ct_server` is pointed at it; the gRPC server is the benchmark's, never yours, and no driver speaks gRPC to your front end |
| `google.golang.org/protobuf` | `proto.Clone`, which a driver duplicates a `configpb.LogConfig` with so each validation case mutates an independent value; `types/known/timestamppb`'s `New`, which builds the `not_after_start` and `not_after_limit` values a driver validates; and `types/known/anypb`'s `New`, which wraps a `keyspb.PrivateKey` into the `google.protobuf.Any` a log configuration carries a key in (CONFIG.md §2.3) |

The last three rows are **third-party modules, not part of your deliverable**, and
nothing in them is checked as work of yours: you neither own the code nor choose
the versions — `go.mod` and `go.sum` are benchmark-owned and are replaced with
the pinned copies before any build (§2). They are written down only so that what
must keep **resolving** is visible. A driver is compiled in a throwaway module
beside your tree, so a dependency your manifest stops providing, or provides at
another version, takes that driver's whole group down with it.

What is re-*checked* here is narrower, and it is named: the TLS codec with the
RFC 6962 leaf and entry contract, the forked `x509`'s lax parsing and its CT
extensions, CTFE's log-configuration validation, CTFE's serving-mode and
certificate-quota admission, and `jsonclient`'s retry and backoff arm — one
`inherited-` group each — plus `ct_server` serving the RFC 6962 endpoints end
to end, step 09's get-entries alignment included, under the end-to-end gate.

The last two of those five are the ones a reader would not predict, so here is
why they are checked at a release that does not touch them. `is_readonly` and
the 429 retry arm both arrived at **v1.1.3**, the certificate quota users at
**v1.0.19** and `is_mirror` at **v1.0.21**; all of them are still required by
the tables above, and all of them are invisible from this step's own subject
matter, which is log lists. An inherited milestone that no group exercises is
a regression no run would report, and a submission that broke the read-only
handler removal or the 429 arm while moving `loglist` to `loglist3` would pass
everything else here. Because they are inherited they are **gate** groups:
they add no new requirement, and their failure means the step is not satisfied.
Inherited behaviour outside that list of five is still required of the product and is
not re-checked at this milestone.

## 6. Formats crossing the boundary

Step 10's `TEST_INTERFACE.md` is replaced by this file, so the list is given
here in full rather than referred to. It is **not** step 10's list copied
forward. Ten milestones have moved underneath it: some of what earlier steps
wrote has changed shape by v1.1.4, and some of it named a package or a
program this product no longer has — this is the release that removes one.
What is below is what is true *here*, and section 6.2 names what was dropped
and why, so that nothing is left for you to go looking for in a file you
cannot open.

Carried forward from the steps behind you, restated:

* **TLS encodings.** `MerkleTreeLeaf`, `TimestampedEntry`, `DigitallySigned`,
  `CertificateChain` and `PrecertChainEntry` are compared **byte for byte**
  against vectors the *verifier* builds for itself. Those vectors are not in
  `ct-spec/` and you cannot read them; they were produced from the field
  orders, tag parameters and widths of TLS-CODEC.md §1–§3 and §5, which
  determine the encoding completely. Re-derive an encoding from those rules
  and it will match; guess at one and it will not. Both entry types are built
  and decoded — an X.509 entry, and a precertificate entry carrying the issuer
  key hash and the TBS — `RawLogEntryFromLeaf` and `LogEntryFromLeaf` must
  round-trip them and recover the parsed certificate, and a leaf with trailing
  bytes or an undefined entry type is **refused** rather than guessed at.
* **Leaf hashes.** `SHA-256(0x00 ‖ TLS(MerkleTreeLeaf))`, compared as bytes
  against a pinned known answer (MERKLE.md §1): the X.509 leaf the inherited
  group assembles hashes to exactly
  `4de27390cab1f581fad3d8f6e611f42720505216a70d95f78b6bca5c042dd5cd`. A leaf
  hash is a property of the encoding, so a codec that is wrong by one byte is
  caught here as well as above.
* **JSON bodies.** Field names, types and base64 encodings exactly as
  HTTP-API.md §3 and §4 give them: `sct_version` a number and not `"v1"`,
  `id`, `signature`, `sha256_root_hash` and `tree_head_signature` standard
  base64 with padding, `certificates` and `chain` arrays of base64 DER **in
  order**, `entries` carrying `leaf_input` and `extra_data`. A response is read
  field by field; nothing compares a whole JSON document as text.
* **Assembled URL paths, and which of them a configuration registers.**
  `<handler prefix> + "/" + <log prefix> + <endpoint path>` (HTTP-API.md §1.1),
  with `/`, `/healthz` and `/metrics` never prefixed. The registered **set** is
  itself compared: a writable log registers all **eight** RFC 6962 endpoints,
  and `is_readonly` and `is_mirror` each remove exactly the two write handlers
  and leave **six** (HTTP-API.md §6.2, CONFIG.md §2.5). A read-only log still
  requires its roots and its key; a mirror requires neither.
* **`/healthz`.** Status **200** and a body that is exactly `ok` — two bytes,
  no newline, nothing else. It is the proof that `ct_server` came up at all,
  and it is the first thing to fail when start-up is broken.
* **PEM, and how a private key reaches a signer.** Root certificates are given
  to `ct_server` by path, as `roots_pem_file`, and the concatenation of those
  files is exactly what `get-roots` serves — the same DER, in order. The log's
  **private key** is a `google.protobuf.Any` matched against the key-handler
  registry `ct_server` populates at start-up (CONFIG.md §2.3); the loader must
  never open a path itself. Every configuration the groups at *this* step build
  — in process and for a live `ct_server` alike — inlines the key as a
  `keyspb.PrivateKey` carrying raw DER, so the DER handler is the one they
  exercise. What is checked is the routing, not the shape: CONFIG.md §2.3's "do
  not assume either one" still stands, and `keyspb.PEMKeyFile` with its `path`
  and its possibly non-empty `password` must stay registered.
* **The configuration file's encoding.** The log config is a protocol-buffer
  message, and every configuration a group at this step writes is **text
  proto**, in which unknown field names are refused — which is what makes
  CONFIG.md §1's field-arrives-at-release-N table observable at all. Steps 01
  to 04 said text proto and *nothing else*: that stopped being true at
  **v1.1.2**, where a file which does not parse as text proto began to be
  retried as **binary proto** (CONFIG.md §4.2), and it is not true here. A
  group checks the content verdict of a config it has actually loaded, never
  the file's encoding.
* **The multi-backend configuration message.** `LogMultiConfig` with its
  `LogBackendSet` and `LogConfigSet`, as CONFIG.md §3 gives it: `backends` is
  one `LogBackendSet` holding repeated `backend`, `log_configs` one
  `LogConfigSet` holding repeated `config`, and a backend's `backend_spec` is
  an opaque non-empty string at validation time.
* **The configuration's content rules, as accept-or-refuse verdicts.**
  `LogConfigFromFile`, `ValidateLogConfig` and `ValidateLogConfigs` are run
  over configurations written for the purpose: an unknown field name, a junk
  file, a `log_id` of zero, both reject knobs set at once, a duplicate prefix,
  a duplicate tree id and an empty prefix are each refused; the extended
  key-usage **names** map to exact `x509.ExtKeyUsage` values; the
  `not_after_start`/`not_after_limit` window and the merge-delay ordering are
  carried through unaltered; the mirror rules hold; a P-256 key survives
  validation as the same key. **No error string is checked anywhere in that
  family** — only the verdict, and the values a valid configuration yields.
* **Where a tree head's three fields come from.** The `timestamp` a `get-sth`
  response carries is the backend root's `TimestampNanos` **converted to
  milliseconds**, not the personality's own clock; `tree_size` is `TreeSize`
  and `sha256_root_hash` is `RootHash`. All three are read out of the
  `SignedLogRoot` the backend answered with, a group pins all three, and the
  signature over them is verified under the log's own key.
* **The leaf the front end queues.** For one `add-chain` the backend must see
  **exactly one** queued leaf, and its `LeafValue` is the TLS encoding of the
  Merkle leaf built from the submitted certificate — compared byte for byte
  against what the group built for itself.
* **Coerced `get-entries` ranges.** What crosses the boundary is the number of
  entries the response carries and the range the **backend** was asked for, not
  a diagnostic. With `--max_get_entries=10`, `?start=5&end=1004` is clipped and
  then truncated to five entries and the backend is asked for `start=5`,
  `count=5`. The entries' `leaf_input` bytes are compared exactly, and so is
  their `extra_data`.
* **Quota user strings.** `ctfe.CertificateQuotaUserPrefix` is exactly
  `@intermediate`, and the identity derived for a certificate is that prefix, a
  space, the certificate's `Subject.String()`, a space, and the first five bytes
  of the SHA-256 of its SPKI as ten lower-case hex digits (CLIENT-API.md §8.6).
  The derivation is **total** — a root, an intermediate and a leaf each get one,
  and all three differ.
* **Retry behaviour, as the caller sees it.** What crosses the boundary is how
  many requests the server saw and what the call finally returned, never how
  long the client slept. A **200** is one request; **408**, **503**, **429** and
  the default arm each retry and then succeed; `Retry-After` is honoured both as
  **integer seconds** and as an **RFC 1123 date**; and a request made with an
  already-cancelled context is never retried, so the server sees **nothing at
  all**. One error string in this family is compared whole — item 3 of §6.1.
* **The forked `x509` parser's non-fatal boundary, as it stands at
  v1.1.4 — not as step 07 wrote it.** The lax retry of X509-FORK.md §3.2
  arrived at **v1.1.0**, and by this release **every** malformed fixture step 07
  tabulated parses to a fully populated certificate together with an error for
  which `x509.IsFatal` is false. That inverts step 07 in more rows than the two
  usually named: a non-minimally-encoded INTEGER, an RSA `SubjectPublicKeyInfo`
  whose `AlgorithmIdentifier` parameters are **not `NULL`**, a **negative RSA
  modulus**, an empty extended-key-usage extension and one carrying a malformed
  OID, an empty subject-information extension, a `secp192r1` public key, and a
  subject string carrying a byte outside `PrintableString` are **all non-fatal
  here** — and each must still **report** an error rather than parse silently.
  Restating step 07's sentences unchanged would be false at this milestone. The
  floor is still a floor: **truncated DER**, **nil input**, and valid DER that
  is not a certificate at all are fatal and yield nothing, and
  `AppendCertsFromPEM` keeps what is only non-fatally wrong while dropping what
  is not a certificate. What is compared is the (`IsFatal`, error-present,
  certificate-returned) triple and the field values recovered from the
  certificate — the serial numbers, in fixture order — never the error's
  wording and never its type.
* **Object identifiers as text, and precertificate surgery.** An extension list
  is compared as the dotted-decimal renderings `asn1.ObjectIdentifier.String()`
  produces, in the certificate's own order — in particular RFC 6962 §3.1's
  poison extension `1.3.6.1.4.1.11129.2.4.3` and the SCT-list extension
  `1.3.6.1.4.1.11129.2.4.2`. `x509.RemoveCTPoison` and `x509.RemoveSCTList` must
  each drop exactly their own extension, leave every other one in place and in
  order, and **error** where there is none to drop; `IsPrecertificate` follows
  the poison and nothing else. An embedded SCT decodes to exact field values,
  and an SCT list re-encodes to the bytes it was parsed from.
* **Command-line flag grammar, as the binary prints it.** A command's `--help`
  output is read back through the only interface a user has, and three things in
  it are compared: the **set** of flag names the command registers, each flag's
  **type word** as the flag package renders it, and each **`(default "…")`**
  rendering. Those lines are compared with their leading two spaces intact. This
  is an output format and not a *message* in the sense of §3.3: the only
  descriptions checked are the ones CLI.md fixes, and nothing else in the usage
  text is read.
* **Process exit status.** A refusal is checked as a non-zero exit and an
  absence, not as a sentence. A successful `--help` exits **0**; a tool that
  parses no flags at all exits **2**; an unrecognised value for an enumerated
  flag goes through the logging library's fatal path and exits **255**.
* **Prometheus text exposition.** `/metrics` is read as whole lines: a counter
  at a pinned value, and the **label set** of a labelled gauge — which values of
  the label are present and which are not. The label values are URLs the group
  itself put into a log list, so what is proved is which logs were reached, not
  how anything is spelled.

And, new at v1.1.4:

* **A build failure that names a path.** The removal is observed as a compile
  error naming `…/loglist`, produced alongside a positive control that must
  build. Retyping the callers while leaving the package on disk fails, and so
  does deleting something else.
* **Pinned v3 log-list JSON.** A fixed document in the v3 schema, parsed by
  your code and never fetched. What is compared is which logs a finder or a
  selection returns, in which order, and what the entries carry — the key, the
  key hash, the URL, the state and the merge delay.
* **Policy group decisions.** For a certificate of a pinned lifetime, the group
  names, the minimum inclusion count of each, and the membership of each, over
  a pinned list (CLIENT-API.md §7.1).
* **A YAML file, byte for byte.** The witness config tool's output is compared
  as exact bytes and by digest, so the emitter's own conventions — block
  sequence indentation in particular — are part of the contract.
* **A refusal that names what it matched.** Resolving a log *by name* has no
  other observable: `ctclient`'s `--log_name` and `ct_hammer`'s
  `--src_log_name` either select one log or refuse, and the refusal is the only
  place the choice becomes visible. Under the last rule of section 3.3 a group
  therefore matches the identifiers inside such a message and nothing else —
  the **descriptions** of the logs that matched, in the order the list gives
  them; the **path or URL of the list actually read**; and, where a tool
  declines a list URL outright, that URL. The wording around them is never
  matched, and no message from the standard library is checked on its own
  account. Resolution by name is not the only refusal a log list produces,
  and the identifier is the same one: an entry whose `key` will not parse is
  refused by `ctutil.NewLogInfo`, and that refusal names the log by its v3
  **description** too (CLIENT-API.md §2.1), so a group matches the
  description and nothing else there either. The same rule is what lets the
  removal be observed as a build failure naming `…/loglist`.

  Two consequences, because groups here check both of them. First, a refusal
  names the descriptions it **matched** and no others: a `--log_name` or
  `--src_log_name` that matched nothing therefore names **no** description at
  all, and a group checks that none appears — a refusal that helpfully listed
  the list's other logs would be matching something it did not match. Second,
  the success arm. Exactly one match is the only case that is *not* refused,
  and the run is checked on **which identifiers are absent** rather than on how
  much it printed: no log description out of the pinned list may appear (item
  13 below), and neither may the path or URL of the list itself. Printing
  nothing at all after the resolution is a passing answer; what must not
  happen is a refusal, and what must not appear is any description the run did
  not actually match.

### 6.1 The strings this step compares whole

Section 3.3 allows exactly one kind of whole-string comparison: a body the
corpus prints in full, listed here. This is that list, for this step, and it is
complete.

1. **`/healthz`'s body.** Status **200** and a body of exactly `ok` — two
   bytes, no newline. It is compared whole as the proof the server came up.
2. **The toolchain's removal messages.** A build of a probe importing
   `github.com/google/certificate-transparency-go/loglist`, and separately one
   importing `github.com/google/certificate-transparency-go/loglist/findlog`,
   must fail with a line ending in exactly
   `does not contain package <that import path>`, while the control probes on
   `loglist3` and on `ctutil/sctcheck` must **build**. Listing the tree must
   agree: `…/loglist/...` matches no package while `…/loglist3/...` matches.
   This is the whole of the removal proof and it is a checked string — an
   emptied-but-present directory, or a package left behind with its callers
   retyped, does not produce it.
3. **`jsonclient`'s one checked error text.** A non-retryable status produces an
   error whose `Error()` is exactly `got HTTP status "400 Bad Request"`. This
   is the **only** place at this step where an `error`'s text is read. Every
   other failure is checked on *whether* it failed, on the status or exit code
   it carries, or on what it refused to produce.
4. **`ctclient`'s root description**, matched as a whole line:
   `A command line client for Certificate Transparency logs` (CLI.md §3.2).
5. **`sctscan`'s and `sctcheck`'s `--log_list` description**, matched whole:
   `Location of master CT log list (URL or filename)`.
6. **The two v3 defaults, as the usage text renders them.**
   `(default "https://www.gstatic.com/ct/log_list/v3/all_logs_list.json")` for
   `ctclient`, `ct_hammer`, `sctscan` and `sctcheck`;
   `(default "https://www.gstatic.com/ct/log_list/v3/log_list.json")` for the
   witness `config` tool and the submission `server`. The parentheses, the
   quotes and the URL are all inside the comparison.
7. **Flag sets, as sorted space-joined strings.** `ctclient`'s persistent set is
   exactly `help log_list log_name log_uri pub_key skip_https_verify`; the
   witness `config` tool's is exactly `config_file log_list_url`; `sctscan`'s is
   exactly its own flags together with klog's, twenty-one names and no others;
   `certcheck`'s is exactly its own thirteen plus klog's, `crlcheck`'s exactly
   its own three (`ca cert strict`) plus klog's, and the witness `feeder`'s
   exactly its own three (`log_list_url poll witness_url`) plus klog's; and
   `ct_server`'s is exactly the nineteen flags CLI.md §2.1 gives it plus klog's
   thirteen and nothing else. One name added or lost moves the string.
8. **Seven flag lines, whole, with their type words.** `  -log_file string`,
   `  -log_file_max_size uint`, `  -one_output`, `  -add_dir_header`,
   `  -skip_headers`, `  -skip_log_headers` and `  -loglist_path string` — each
   with its two leading spaces and its trailing newline. A bool that prints a
   type, or a `uint64` that prints anything but `uint`, moves them.
9. **The witness `config` tool's YAML output, as a whole file.** Its SHA-256 is
   exactly `d460e303ed2ea47e9e0d646c38e43f6aab39da81bcc2bcf4300fb36baa3f8d70`;
   its first line is exactly `Logs:`; its second line begins with four spaces
   and then `- PubKey`; and it carries exactly two `- PubKey:` values, the
   base64 keys of the two usable logs, in list order and with nothing else
   between them. The indentation is the emitter's own convention and is checked
   with the rest of the bytes.
10. **The submission proxy's response frames.** Three bodies are checked as a
    **prefix**, a non-empty remainder and a trailing newline, beside the status:
    `proxy: failed to parse add-chain body: ` and
    `proxy: failed to parse add-pre-chain body: ` at **400**, and
    `distributor unable to process cert-chain: ` at **502**. What follows each
    prefix is your diagnostic and is never read for its wording. One body is
    checked whole: a **GET** on the add-chain path is exactly status `404` with
    the body `404 page not found` and a newline.
11. **Two lines of that proxy's `/metrics`.** The whole line
    `log_list_updates 1`, and the set of `last_get_roots_success{logurl="…"}`
    label values, which must be exactly the four logs the policy selects out of
    the pinned list — every usable log, plus the pending and the qualified one.
12. **The surviving v2 reader's output.** A benchmark-written probe parses the
    v2 document `{"operators":[]}` with `loglist2.NewFromJSON` and prints
    `len(ll.Operators)` and the error it got. It is built, run, and its whole
    stdout compared: exactly `0 <nil>` — a count and a nil error, in Go's own
    rendering of both. The probe is the benchmark's code, not yours; what it
    checks is that `loglist2` is still there and still parses.
13. **Log descriptions out of the pinned v3 list**, compared as whole strings
    and in the order the list gives them: `Google Bench Usable Log`,
    `Google Bench Readonly Log`, `Google Bench Qualified Log`,
    `Shop Usable Log`; and, where they must not be matched, their **absence**
    — always `Shop Retired Log` and `Shop Pending Log` (the pinned list's only
    retired and pending entries), which no refusal in this step may name, and
    on the success arm of §6 above all six of them, since a run that resolved a
    name refuses nothing and so names no description at all. A description is
    the identifier §3.3 admits inside
    a refusal, so the sentence around it is never matched.
14. **Policy names and group names.** `Name()` renders `Chrome` and `Apple`;
    Chrome's group names comma-joined are exactly
    `All-logs,Google-operated,Non-Google-operated`; Apple returns exactly
    `ctpolicy.BaseName` and nothing beside it.
15. **Two log-list version strings.** `11.0` and `11.1`, read off the parsed
    list, which is how the refresher is shown to have noticed a change.
16. **Pinned known answers, re-rendered.** The X.509 leaf hash hex above; the
    comma-joined serial numbers recovered from the malformed fixtures; the
    SCT list's exact DER hex from the distributor; `get-roots`'s certificate set
    as the sorted comma-joined SHA-256 hex of the DER it served; the entries'
    `extra_data` as the hex `000000`; and
    `ct.TimestampToTime(…).UTC().Format("2006-01-02T15:04:05.000Z")` equal to
    `2009-02-13T23:31:30.000Z`. All are *values* re-rendered by the standard
    library, not messages.

**That list is exhaustive: no group at this step compares any other message,
rendering, body or program output as a whole string.**

Five kinds of comparison look like the list above and are not:

* **Bytes the harness itself supplied, handed back.** The stub backend's pinned
  tree size, timestamp and root hash; the JSON a stub serves; the public key a
  group wrote into a configuration. Those are the group's own values on a round
  trip, not your output.
* **Paths and names the group supplied, echoed back.** The path of the log list
  a tool was told to read, and the `--log_name` or `--src_log_name` string it
  was asked for, appearing in a refusal. Finding them proves the tool read the
  list it was given and got as far as looking; it says nothing about wording.
* **Byte slices compared as bytes.** TLS encodings, DER, leaf values, extra data
  and public keys are compared with byte equality, not as text, and nothing in
  their comparison is sensitive to how anything is spelled.
* **Whole-message reads on the failure path, which with one exception do not
  exist here.** Where a group expects a refusal it checks that there **was**
  one — a non-zero exit, a non-nil error, a status code, a file that was never
  written — and, where §3.3 allows it, the identifier inside it. Item 3 is the
  single exception in the whole step.
* **Usage text outside items 4 to 8.** A `--help` dump is scanned for the flag
  names, type words, defaults and two descriptions listed above. The framework's
  own headers, its column padding and the order of its blocks are not compared
  at this step.

### 6.2 What earlier steps listed and this one does not

Several families are absent from the list above on purpose, because restating
them would put a false — or merely out of scope — sentence in front of you:

* **Everything naming `loglist`, `findlog`, or the v1 log-list schema.** Steps
  01 to 04 listed the v1 schema's field names, `LogInfo.MMD` as read from a v1
  list, and `findlog`'s flag lines and its one-line-per-match stdout.
  **`…/loglist` and its `findlog` command do not exist at v1.1.4.** The
  only checked fact left about that package is item 2 of §6.1: importing it fails
  to build and the toolchain names the exact path. Nothing else about it is true
  here, and looking for the v1 reader in order to keep it working is a misread
  of the step.
* **Step 07's fatality rows.** Restated above, **inverted**, because the lax
  retry of X509-FORK.md §3.2 landed at v1.1.0. Every malformed fixture is
  non-fatal at this release, not only the two rows usually cited.
* **The witness HTTP API.** Step 10 listed the three paths of CLIENT-API.md
  §6.3, the `UpdateRequest` body, the 200/400/404/409/500 table and the
  stored-STH body compared byte for byte. No group at this step stands a witness
  server up; the only witness surface checked here is the `config` tool's YAML
  file and the `witness` binary's flag set.
* **`ctclient`'s generated help tree and its subcommands' printed output.** Step
  10 compared the whole command tree, every leaf's usage line, its `Aliases:`
  line, and the renderings of `get-entries`, `upload` and `bisect`. At this step
  exactly three things are read out of `ctclient`: its root description line
  (item 4), its persistent flag set (item 7) and its `--log_list` default (item
  6). The rest still has to build and still has to work; it is not re-compared.
* **Inclusion and consistency proof verification.** Step 05 checked
  `ctutil.LogInfo`'s `VerifyInclusion*` family and `ctclient consistency`
  against a live log. No group at this step verifies a proof. `LogInfo`'s STH
  cache is still exercised — `LastSTH` and `SetSTH` must round-trip — but
  nothing here checks a Merkle path.
* **`Cache-Control` headers, `Content-Type` as a header value, and CORS.** Steps
  03, 05 and 09 listed response headers. **No group at this step reads a
  response header at all.** The one place `Content-Type` appears is a header the
  harness's own stub sets on the way in.
* **The CTFE's `logid`-labelled Prometheus gauges** (step 07) and the
  `aligned_get_entries` metric (step 09). `/metrics` is read at this step only
  on the submission proxy, for the two lines of item 11. Alignment itself is
  still checked — as entry counts and the range the backend was asked for, not as
  a metric.
* **The `configs/` corpus.** Earlier steps shipped a directory of configuration
  files in the workdir and checked verdicts over it. **This step's workdir has no
  `configs/`**, and no group here is checked over such a corpus. The one group
  that loads a configuration file it did not write —
  `inherited-ctfe-log-config-validation`, which materialises two pinned fixtures
  out of the verifier's own asset tree — uses them as a starting point to mutate,
  and checks the validator's answers on the mutated copies rather than a verdict
  over the corpus. Every other configuration a group here checks is one that
  group wrote itself, in process or on disk for a live server.
* **PEM key files as the way a key reaches a signer.** Steps 02 and 04 wrote
  about `keyspb.PEMKeyFile`, its `path` and its password as the form a
  configuration uses. Every configuration built at this step inlines raw DER
  instead. The PEM handler must stay registered — that is in the carried-forward
  bullet above — but no group here loads a key through it.
* **Named diagnostics from the configuration and admission paths.** Step 03's
  `empty backend name` and `duplicate backend name` prefixes, and step 08's two
  admission refusal reasons, are not matched at this step. Those verdicts are
  checked as verdicts: refused or accepted, and what a valid input yields.
* **The gRPC → HTTP status map** and `--mask_internal_errors` (steps 08 to 10).
  The map is not re-exercised here, and the flag survives only as a name inside
  `ct_server`'s exact flag set (item 7).
* **The detached-signature log list.** Step 10 checked `NewFromSignedJSON` over a
  v3 list and its corrupted twin. No group at this step calls it. The v3 list is
  still parsed, and parsed exhaustively — that is the bullet above — but not
  through the signed-JSON entry point.

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
| `inherited-ctfe-admission` | gate |
| `inherited-ctfe-log-config-validation` | gate |
| `inherited-jsonclient-retry` | gate |
| `inherited-rfc6962-leaf-and-entry` | gate |
| `inherited-x509-fork-nonfatal` | gate |
| `ctclient-loglist-v3-default` | requirement |
| `cthammer-loglist-v3-default` | requirement |
| `ctpolicy-apple-loglist3` | requirement |
| `ctpolicy-chrome-loglist3` | requirement |
| `ctutil-loginfo-loglist3` | requirement |
| `glog-to-klog-flag-migration` | requirement |
| `loglist-v1-findlog-command-removed` | requirement |
| `loglist-v1-package-removed` | requirement |
| `loglist-v2-survives-with-no-consumers` | requirement |
| `sctscan-loglist-v3-default` | requirement |
| `submission-distributor-loglist3` | requirement |
| `submission-loglist-refresher-v3` | requirement |
| `submission-proxy-server-v3` | requirement |
| `witness-config-loglist3-yaml` | requirement |

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

* **migrillian's zero-size skip.** v1.1.4 has it skip the consistency
  check when the source root is size zero. Observing that needs a public log
  to read from and a backend to write to, and neither exists when your work is
  checked.
  The rule it leans on is stated in MERKLE.md §3 and is worth reading anyway.
* **`tools/`.** The dependency-tracking package that arrives here carries a
  build tag that excludes it from an ordinary build, so it produces no binary
  and changes nothing the verifier compiles.
* **Fetching a log list.** Every list here is a pinned fixture handed to your
  parser directly. The v3 URL constants are checked as the *defaults of flags*,
  never as something that is dialled.
* **The witness service's own protocol.** Co-signing an STH needs a live log
  and stored state. The one witness program observed here is the tool whose
  whole job is turning a log list into a configuration file.
* **Anything dated after v1.1.4.** The v2 log-list package still exists
  here and must still compile; its removal, issuance-chain storage and the
  chain cache belong to the next step.
* **What is left lying under a deleted directory.** The removal is checked as a
  build fact — the import path does not resolve, and it is not a build target.
  A stray non-Go file under `loglist/` is out of scope either way; the release
  deletes the directory and its bundled key, and deleting it is the right
  thing, but no group inspects the filesystem for it.

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

A removal, and the whole difficulty is in *where the callers land*.

`loglist/` — the v1 schema, flat, with a `Logs` array and an `Operators` array
joined by integer ids and a log's life expressed by two ad-hoc optional fields
— is deleted outright at v1.1.4, along with `findlog`, the one command
that existed only to search it. Its dependants do not step to the v2 package
sitting next to it. They jump **straight to v3**: the release names four of
them one by one — the `ctclient` command tree, `ctutil`'s `LogInfo` entry
points, `sctscan` and `ct_hammer` — and `sctcheck` follows `ctutil`, because
its own log-info factory is typed on it. In the same release `ctpolicy` and
`submission` make the same jump from v2, so that by the end of the step every
log-list consumer in the product speaks one schema.

The v2 package is the trap. It survives this release untouched and still
compiles, and it is the obvious destination for a v1 caller — which is exactly
why nothing must land there. What is left behind is a package with no consumer,
kept because it is still published, removed one release later.

Beyond that the release is a sweep rather than a design change: the logging
library is swapped for another with the same exit statuses and different flag
names, `tools.go` moves into a directory of its own, and migrillian learns that
an empty tree is consistent with anything.
