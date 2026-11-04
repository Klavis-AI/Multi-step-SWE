# TEST_INTERFACE — step 04 of 13 (certificate-transparency-go v1.0.13)

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

**No group at this step stands up a working backend.** There is no Trillian
stub here: no group runs a log server, no group registers a log service on any
socket it opens, and no in scope boundary depends on a backend answering. Where a
group needs a canned log root it supplies one **in process**, by handing the
personality a driver-local value that satisfies the backend *client* interface —
a value in the driver's own address space, reachable over no socket and speaking
no RPC. The library surfaces of section 5, the programs of section 4,
configuration acceptance and rejection and the flag grammar are all reachable
without a backend, and that is where this milestone's new work is checked.

`ct_server` *is* started, though, and its running behaviour *is* checked. A group
that needs it opens one loopback socket per backend its configuration names,
registers no service on any of them, and names those addresses to your front end
by whichever route the release under test supports — on the command line as
`--log_rpc_server`, or inside the log configuration as a backend's
`backend_spec`, or both (CLI.md §2.1, CONFIG.md §3). Never assume it is the
command-line one: where a group needs the multi-backend config shape to be read,
it must leave `--log_rpc_server` empty, because that is the only way the release
parses the configuration as a `LogMultiConfig` at all. The listeners exist only
so that the dials succeed; every call your front end makes over one fails. What
is checked is therefore everything your front end does **before** it needs an
answer from the log: that it starts at all against the configuration it is
given, that it routes each log's prefix to the right handler, and that it serves
the accepted roots — none of which requires the backend to reply.

Do not read that as permission to fail a backend call badly, and do not read it
as a promise that the front end is unchecked. The groups that start the binary
are **gate** groups — section 7's table names every group in this step and its
kind — so a front end that will not come up, or that comes up on the wrong
paths, or that serves the wrong roots, means the whole step is not satisfied.

---

### 3.5 The compile-time gate: the tree you were handed does not build

Section 7 warns that proof of life is not always free and points here for
whether that applies at this milestone. **It applies here**, in the third of
the three shapes section 7 names — the one where the manifest looks clean.

Section 2's pinned manifest is v1.0.13's, and it still supplies
`github.com/google/trillian`; the inherited tree still imports it, and nothing
has been removed from the require list. What moved is a **signature**. At
v1.0.10 `trillian/crypto`'s `Signer.Sign` returned a
`*sigpb.DigitallySigned` — a struct with a `Signature` field — and at the
revision this milestone pins it returns a plain `[]byte`. The two inherited
call sites in `trillian/ctfe/serialize.go` still resolve `Sign`, still compile
their argument list, and then read `.Signature` off the result, which a
`[]byte` does not have:

    trillian/ctfe/serialize.go:48:24: signature.Signature undefined
        (type []byte has no field or method Signature)
    trillian/ctfe/serialize.go:77:24: signature.Signature undefined

So `go build ./...` in `/app` fails on the very first run, section 1's build
obligation is not met, and `build-clean` — which section 7's table lists as
**GATE** — fails before you have changed anything. Comparing section 2's
require list against the tree's imports will not show you this, because
nothing is missing from either; only building the tree does.

Three consequences, because none is guessable from section 7 alone:

* A no-op here satisfies none of the new work: the gate itself fails. Section 7's
  note on no-ops covers both cases; v1.0.13 is the second one.
* The two programs that reach the broken package are **not built**. The
  harness builds candidate binaries with a single `go build -o <dir>/ ./...`.
  Be precise about what that loses, because the convenient sentence — "the
  tree does not build, so there are no binaries" — is not true here. The
  failure at v1.0.13 is a *type* error, and a type error is confined to
  the packages that depend on it: the command still writes a binary for every
  package that compiles, so eight of the ten survive. What you lose is
  `ct_server`, which imports `trillian/ctfe` directly, and `ct_hammer`, which
  reaches it through `trillian/integration`. Any group of section 4 that runs
  one of those two then fails for want of its program, as does every driver
  that links `trillian/ctfe`. (An *unresolvable import* is the case that
  suppresses the whole command and leaves no binaries at all. That is a real
  failure mode elsewhere in this series, but it is not the one in front of
  you.)
* Pinning a different `github.com/google/trillian` is not a repair. Section 2
  fixes the dependency universe and checking is offline against a module cache
  that holds only the pinned revision, so the adaptation has to happen in the
  product's own code.

This is inherited plumbing, not this milestone's work: it adds no requirement of
its own, it is not on the feature list of section 5, and the fix is confined to
the two call sites the compiler names. Do it first and the rest of the step is what
sections 4 to 6 describe.

## 4. Command-line programs the verifier runs

v1.0.13 adds `sctcheck`, `sctscan` and `findlog`, so the tree holds **13**
`package main` directories (CLI.md §1).

| program | directory | what a group does with it |
| --- | --- | --- |
| `sctcheck` | `ctutil/sctcheck/` | run over fixture PEM certificates — some carrying embedded SCTs, some not, plus unreadable and non-certificate files — and over log-list files that cannot be read or are not log lists; checked on its **exit status**, on the flag set of CLI.md §4.4 and on the flag-grammar rules below. The log list is loaded **before** the first target is examined, so a list that cannot be read or parsed is fatal: the run exits **non-zero** even when every target would have been clean on its own. What the program prints while checking targets is **not** checked, on either stream — the verdict is the exit status. No group hands it an `https://` URL or a bare SCT file |
| `sctscan` | `ctutil/sctscan/` | checked on its flag set **only**, read out of the `--help` usage block the requirement group `new-command-binaries` asks it for — so it must at least answer `--help` like every other flag-parsing program here. No group ever points it at a log, a log list or a DNS server, so nothing about its scanning behaviour — including how it reacts to a missing flag — is in scope |
| `findlog` | `loglist/findlog/` | run against a fixture log list; checked on its flag set, its exit status and the stdout contract of section 6. The detached signature is fetched and checked **only when `--log_list_pubkey` names a key**: with no public key there is nothing to verify against, so `--log_list_sig` — which carries a non-empty URL default at v1.0.13 (CLI.md §4.5) — is not read at all, and an offline run given only `--log_list` succeeds |
| `ct_server` | `trillian/ctfe/ct_server/` | built like every other binary **and started**, by **gate** groups only. Each such group writes a pinned log configuration, opens a loopback gRPC listener per backend the configuration names with **no service registered** on any of them, and checks only what the front end does before it needs an answer from the log: that it comes up at all, that it answers on each configured log's own prefix, and that `get-roots` serves exactly the DER set that log's `roots_pem_file` names. Section 3.4 states the whole of it. No chain is submitted, no proof is fetched, and nothing else about its running behaviour is checked here |
| `ctclient`, `certcheck`, `crlcheck`, `scanlog`, `preloader`, `dumpscts`, `chainfix`, `ct_hammer`, `gossip_server` | see CLI.md §1 | as at step 03 |

Four rules hold for every program and are checked wherever a group runs one
(CLI.md, the preamble on flag grammar):

* an unrecognised flag — **or a value the flag's declared type cannot parse**,
  such as `--deadline=bogus` for a `Duration` — makes the standard `flag`
  package print `Usage of <path the group invoked>:` and the flag list **to
  stderr**, write nothing to stdout, and exit **2**;
* `--help` (and `-h`) is the one case the `flag` package treats differently:
  the same usage block goes to stderr and the program exits **0**. Every
  flag-parsing program in the table above answers `--help` that way, which is
  how a group reads a program's flag set at all, and the block must **open
  with `Usage of <path the group invoked>:`** — the *absolute path the group
  used to start the binary*, because the `flag` package prints `os.Args[0]`
  and not a name the program chooses. Keep the default `flag.Usage`: replacing
  it with your own banner changes that first line and fails the groups
  listed in section 6, one of which is a gate group. `chainfix` is the
  documented exception — it reads `os.Args` before `flag` runs, so it exits 2
  (CLI.md §4.6);
* a missing *required* flag is a bare diagnostic on stderr and exit **1**, with
  no usage dump;
* `glog.Exit*` and `log.Fatal*` exit **1**; `glog.Fatal*` exits **255**. Which
  of the two a program reaches for is **yours to choose, and no group checks
  the choice**. A group pins an exact non-zero status only where CLI.md fixes
  one for that program — `sctcheck` exits 1 when a target had an invalid SCT
  (§4.4), `chainfix` exits 2 (§4.6) — and everywhere else, including every
  fatal path CLI.md does not give a number to, it asks only that the run exit
  **non-zero**.

One consequence of using the standard `flag` package is checked directly.
Where CLI.md §4.4–§4.6 gives a flag a **non-zero** default, the usage block
renders that default as a trailing `(default <value>)` on the flag's
description line; where the default is the type's zero value, the `flag`
package prints no `(default …)` at all. A group may therefore check that
`sctscan`'s `-dns` line carries `(default true)` and that its `-inclusion`
line carries no default, without knowing what either description says. **No
group greps a description string**: the `(default …)` suffix is read, the
sentence in front of it is not. CLI.md §4.4 does fix the exact usage text of
`--log_list` for `sctcheck` and `sctscan`, and that text really does differ
from `ctclient`'s, so write it as given — but nothing here checks it, and
every description CLI.md leaves unspecified is yours to write.

## 5. Packages and symbols the verifier links against

Step 03's `TEST_INTERFACE.md` is replaced by this file, so the surface it
listed is restated here rather than referred to. This table is the whole of
what a driver at this step links: every import path below resolves at
v1.0.13, and no driver here names an import path this table omits. Three
of the paths are **new** at v1.0.13 — `…/ctutil`, `…/loglist` and
`…/dnsclient`. The rest are inherited, and an inherited row lists what a
driver **at this step** uses on that path, which is not always everything an
earlier step used.

| import path | what the drivers use |
| --- | --- |
| `…/ctutil` | `LeafHash`, `LeafHashB64`, `VerifySCT`, `ContainsSCT`, `LogInfo`, `LogInfoByHash`, `NewLogInfo`, `LogInfoByKeyHash`, `NewLogInfoOverDNS`, `NewLogInfoOverDNSWrapper`, `LogInfoByKeyHashOverDNS`, `LastSTH`, `SetSTH`, `VerifySCTSignature`, `VerifyInclusion`, `VerifyInclusionLatest`, `VerifyInclusionAt` (CLIENT-API.md §2, §2.1, §2.2). A driver reads all five of `LogInfo`'s exported fields off the value a constructor handed back — `Description`, `Client`, `MMD`, `Verifier` and `PublicKey` (§2.1) — so an implementation that keeps any of them unexported, or leaves one unset, is caught |
| `…/loglist` | `LogList`, `Log`, `Operator`, `STH`, `NewFromJSON`, `NewFromSignedJSON`, `FindLogByName`, `FindLogByURL`, `FindLogByKeyHash`, `FindLogByKey`, `FuzzyFindLog` (CLIENT-API.md §4, §4.1). Two of these are checked on semantics a signature line does not carry: `NewFromSignedJSON`'s detached-signature scheme and `FuzzyFindLog`'s leading/trailing trim, both written out in §4. The package also declares the two log-list **URL constants** §4 gives for this release — `LogListURL` and `LogListSignatureURL`; they are the documented defaults of `sctcheck`, `sctscan` and `findlog` flags (CLI.md §4.4, §4.5), and **no driver links a constant directly** |
| `…/dnsclient` | `New` and the exported `DNSClient.Verifier` field only (CLIENT-API.md §6.1) — see section 9 for the rest |
| `…/jsonclient` | `Options` only, and only as an argument: a driver constructs a zero or key-bearing `jsonclient.Options` and hands it to a constructor — `dnsclient.New`, whose **second** parameter it is (CLIENT-API.md §6.1), or `client.New`, whose **third** it is (§1.1, §1.2). The field names it may set are `Logger`, `PublicKey` and `PublicKeyDER`; `UserAgent` is from v1.1.0 and is not used here |
| `…/client` | `CheckLogClient` — the three-method interface of CLIENT-API.md §1.3, **without** `BaseURI` at this release: it is what `ctutil.NewLogInfo` takes, and a driver satisfies it with a value of its own, so a fourth required method would break compilation. Also `New(uri string, hc *http.Client, opts jsonclient.Options) (*LogClient, error)` and the three `*LogClient` methods the gate group `inherited-http-log-client` drives against a loopback `httptest` server — `GetSTH`, `GetProofByHash` and `GetAcceptedRoots` (§1.2) |
| root package | `MerkleTreeLeafFromChain`, `MerkleTreeLeafForEmbeddedSCT`, `TimestampToTime`, `PublicKeyFromB64`, `PublicKeyFromPEM` (**four** returns: key, key hash, the **unconsumed input remainder**, error), `SHA256Hash`, `TreeLeafPrefix`, `TreeNodePrefix` (CLIENT-API.md §3). Also the RFC 6962 structures and enumerations the drivers pass across the boundary — `SignedCertificateTimestamp`, `SignedTreeHead`, `MerkleTreeLeaf`, `GetProofByHashResponse`, `V1`, `X509LogEntryType`, `PrecertLogEntryType`, `TimestampedEntryLeafType` (CLIENT-API.md §3.2; TLS-CODEC.md §5.1, §5.2, §5.5) — and the four endpoint **path constants** that two gate groups compare a served request against: `AddChainPath`, `GetSTHPath`, `GetProofByHashPath` and `GetRootsPath`, each carrying the leading `/` and the `ct/v1/` segment exactly as HTTP-API.md §1 spells the endpoint |
| `…/tls` | `Marshal` and `Unmarshal` — the RFC 5246 presentation codec of TLS-CODEC.md §1, and the most widely linked sub-package at this step: every group's fixture kit decodes a pinned SCT with `Unmarshal` and reads the **unconsumed remainder** it returns first. **New at v1.0.13:** `SignatureAlgorithmFromPubKey` — one return, no error. A driver also names the algorithm enumerations it compares against — `HashAlgorithm`'s `SHA256`, and `SignatureAlgorithm`'s `Anonymous`, `RSA`, `DSA` and `ECDSA` (TLS-CODEC.md §5.1) |
| `…/x509` | the fork's `Certificate` — including the `RawTBSCertificate` field a driver scans for the poison and SCT-list OIDs — `ParseCertificate`, `ParsePKIXPublicKey`, `MarshalPKIXPublicKey`, and the pre-existing `RemoveCTPoison`. **New or first exercised at v1.0.13:** `RemoveSCTList`, `(*Certificate).IsPrecertificate`, and the `SignedCertificateTimestampList.SCTList` / `SerializedSCT.Val` fields (X509-FORK.md §4.2, §4.4, §4.5) |
| `…/x509util` | `CertificateFromPEM`, `CertificatesFromPEM`, `ParseSCTsFromCertificate`, `ParseSCTsFromSCTList`, `ExtractSCT`, `ReadFileOrURL`, `GetIssuer` (CLIENT-API.md §5.2) |
| `…/merkletree` | **still present at v1.0.13, and still linked** — this is the last release that has it. The gate group `inherited-merkletree-proofs` uses `NewTreeHasher` (returning `*TreeHasher`), the `TreeHasher` type and its `HashEmpty`, `HashLeaf` and `HashChildren`; and `NewMerkleVerifier` (returning a `MerkleVerifier` **by value**, not a pointer) with its `VerifyInclusionProof` and `RootFromInclusionProof`. Both constructors take the same `HasherFunc`, `func([]byte) []byte`, and `VerifyInclusionProof` takes the leaf **data**, not its hash (MERKLE.md §4.1). The by-hash pair `VerifyInclusionProofByHash` / `RootFromInclusionProofAndHash` still compiles but is checked at step 02, not here (section 9). At v1.0.13 the package no longer declares the domain-separation constants itself: they have moved to the root package as `ct.TreeLeafPrefix` / `ct.TreeNodePrefix`, and `TreeHasher` reads them from there |
| `…/trillian/ctfe` | the personality §3.5's type error lives in — until that is repaired, the driver below does not compile and the group is a **GATE**. `inherited-sth-signature-cache` builds the personality **in process** from: `LogConfigFromFile`; `SetUpInstance` in the shape it holds from v1.0.5 to v1.0.19, `(ctx, trillian.TrillianLogClient, *configpb.LogConfig, InstanceOptions) (*PathHandlers, error)` (CLIENT-API.md §8.1) — the loaded config goes straight from one call into the other, so no driver names `configpb` itself; `InstanceOptions` with its `Deadline`, `MetricFactory` and `RequestLog` fields (§8.2); `DefaultRequestLog`, supplied as `new(ctfe.DefaultRequestLog)` because `AppHandler.ServeHTTP` drives a `RequestLog` around every request (§8.5); `PathHandlers`, a `map[string]AppHandler` keyed by the **full** path — leading `/`, then the log's prefix, then the endpoint; and `AppHandler`, which is an `http.Handler` and is driven here with an `httptest` recorder rather than a socket. The group goes through `SetUpInstance` and **not** `NewLogContext` on purpose: `NewLogContext`'s signer parameter changes shape at exactly this release, while `SetUpInstance` has the same signature at v1.0.10 and at v1.0.13, so no signer type crosses the driver boundary |
| `github.com/google/trillian`, `github.com/google/trillian/monitoring`, `github.com/google/trillian/crypto/keys/pem/proto`, `google.golang.org/grpc` | **not your deliverable and out of scope as such** (section 3.4) — named here because drivers at this step link them into the same binary as your code, and this section is the complete list of what the verifier links. `inherited-sth-signature-cache` declares a driver-local `trillian.TrillianLogClient` (embedding the interface and defining only `GetLatestSignedLogRoot`, whose request and response are `trillian.GetLatestSignedLogRootRequest` and `trillian.GetLatestSignedLogRootResponse` and whose variadic tail is `...grpc.CallOption`), answers with a `trillian.SignedLogRoot` it fills in itself, puts `monitoring.InertMetricFactory{}` in `InstanceOptions.MetricFactory`, and **blank-imports** `github.com/google/trillian/crypto/keys/pem/proto` for the `init()` that registers the `PEMKeyFile` handler `SetUpInstance` needs to turn the config's private key into a signer — the same import `ct_server` carries for the same reason. `gate-endtoend` and `inherited-multi-backend-serving` link `google.golang.org/grpc` alone, for `grpc.NewServer` on loopback sockets that register no service (section 3.4). Both modules come from the benchmark-owned manifest at the versions section 2 pins — `github.com/google/trillian v1.1.0`, `google.golang.org/grpc v1.20.1` — so what you leave in `go.mod` cannot change them, and you implement no part of either |

Every one of those is given with its full signature at the reference above.
Where a group turns on a signature rather than a name — `NewLogInfoOverDNS`
taking **one** argument, `PublicKeyFromPEM` returning **four** values,
`IsPrecertificate` returning a bare `bool` — the signature in ct-spec/ is what
the driver compiles against.

`LeafHashForLeaf` in the root package is **not** part of this milestone: it
arrives at v1.0.14 and is first visible to you at step 05. A driver here does
not look for it. `merkletree` is not new here either — its by-hash entry
points have existed since v1.0.5 and are checked at step 02 — but its row above
is not decoration: a gate group links the package at this step, so it must
still compile and still answer.

**No import path is withdrawn at v1.0.13.** Every path in the table must
resolve. No group here builds a probe that requires a package to be *absent*,
so deleting one of these packages fails the build gate of §3.5 rather than
counting towards a removal requirement.

## 6. Formats crossing the boundary

Steps 01, 02 and 03 each carried a list under this heading, and step 03's
`TEST_INTERFACE.md` is **replaced by this file** — you no longer have it. So
everything of theirs that is still contract at v1.0.13 is restated below in
full and merged with what this step adds. Nothing in this section refers you to
a document you cannot open. Two entries are **not** what step 03 said and are
marked as such, and three things step 03 listed are gone; those are named at
the end so that you do not reconstruct them from memory.

**Inherited, and still contract at v1.0.13.**

* **TLS encodings.** `MerkleTreeLeaf`, `TimestampedEntry`, `DigitallySigned`,
  the SCT signature input and the `TreeHeadSignature` input are compared **byte
  for byte** against vectors the *verifier* holds. Those vectors are not in
  `ct-spec/` and you cannot read them; they were produced from the field
  orders, tag parameters and widths of TLS-CODEC.md §2, §3 and §5, which
  determine the encoding completely. Re-derive an encoding from those rules and
  it will match; guess at one and it will not. The codec does not move at
  v1.0.13, and this step leans on it harder than its predecessors did: a
  group re-encodes a pinned SCT with `tls.Marshal` and requires the original
  bytes back, another pins the length and the SHA-256 of a serialised leaf, and
  the two algorithm bytes of the `digitally-signed` framing on a tree-head
  signature are read as **sha256(4) / ecdsa(3)**.
* **JSON bodies.** Field names, types and base64 padding exactly as
  HTTP-API.md §3 and §4 give them. Binary fields are standard base64 with
  padding; an empty list is `[]`, never `null`. A response is read field by
  field — nothing compares a whole JSON document as text. The three responses a
  group decodes at *this* step are `get-sth` (`tree_size`, `timestamp`,
  `sha256_root_hash`, `tree_head_signature`), `get-proof-by-hash`
  (`leaf_index`, `audit_path`) and `get-roots` (`certificates`).
* **Leaf hashes and Merkle hashes.** `SHA-256(0x00 ‖ TLS(MerkleTreeLeaf))`,
  compared as bytes (MERKLE.md §1), and the node and root hashes of
  MERKLE.md §1–§4, compared as bytes against values worked out from the RFC
  rather than copied out of the tree you were handed.
* **PEM, and how a private key reaches a signer — read this even if you
  remember step 03's version, because the corpus has moved back.** Root
  certificate files are given to `ct_server` by path, as `roots_pem_file`, and
  what a group compares is the DER set `get-roots` serves against the DER in
  the file that log's own config named. The log's **private key** is a
  `google.protobuf.Any` matched against the key-handler registry `ct_server`
  populates at start-up (CONFIG.md §2.3); the loader must never open the path
  itself. Step 03 said that the configurations its groups built inlined the key
  as a `keyspb.PrivateKey` carrying raw DER. That was true of step 03's corpus
  and it is **false here**: every configuration the groups at *this* step write
  names a **`keyspb.PEMKeyFile`** carrying a `path` **and a `password`**, and
  the file it names holds a **passphrase-encrypted** `EC PRIVATE KEY` block.
  Assume neither key form and neither encryption state, keep both handlers
  registered, and route `private_key` through the registry — which is the only
  part of this that is yours to write. CONFIG.md §2.3's "Do not assume either
  one" is the rule.
* **The configuration file's encoding.** The log config is a protocol-buffer
  message, and through v1.1.0 it is parsed as **text proto and nothing else**
  (CONFIG.md §4.2), so the JSON rendering of a valid message is a parse failure
  at v1.0.13 whatever its contents. Unknown field names are refused. Every
  configuration a group writes at this step is text proto, and it reaches your
  code either through `ct_server --log_config` or through
  `ctfe.LogConfigFromFile`. No group at this step checks a **rejection**
  verdict: the validator cases that did were step 03's and this step has none.
  What is checked here is that a config written that way loads and that the
  instance it describes comes up.
* **The two configuration message shapes, and the flag that selects each.**
  Both are exercised at this step, and a group chooses between them with
  `--log_rpc_server` exactly as CLI.md §2.2 describes. With `--log_rpc_server`
  naming a backend, `--log_config` is read as a **`LogConfigSet`** — repeated
  top-level `config`, no `backends` — the shape step 01 introduced, which
  v1.0.5 and later convert to a `LogMultiConfig` on load rather than supersede.
  With `--log_rpc_server=` **empty**, the same file is read as a
  **`LogMultiConfig`**, as CONFIG.md §1 and §3 give it: `backends` is one
  `LogBackendSet` holding repeated `backend`, `log_configs` is one
  `LogConfigSet` holding repeated `config`, and each config names its backend
  with `log_backend_name`. A backend's `backend_spec` is an opaque non-empty
  string at validation time.
* **Per-log separation — this is what replaces step 03's backend-routing
  entry, and it is narrower.** Step 03 said a group runs two logs over two
  backends at once and moves one backend's root underneath it. The first half
  still happens; the second does **not**, and nothing at this step observes
  which backend a request reaches — that cannot be seen from outside the
  process without a Trillian stub and no group here brings one. What a group
  does instead is give the two logs **different `roots_pem_file` pools** and
  require each prefix to answer `get-roots` with exactly the DER set *its own*
  config names, which catches a personality that registered one config twice or
  reused one pool for both prefixes. The `log_backend_name` binding is still
  contract (CONFIG.md §3) and a later step may observe it; do not drop it
  because this step cannot see it. Both loopback backends are gRPC listeners
  with **no service registered**, so "an address that answers nothing" is
  literally what you are handed: your server must still start and still serve
  everything that does not need a backend.
* **Where a tree head's three fields come from.** HTTP-API.md §4.1 gives only
  their types, so the provenance is fixed here: the `timestamp` a `get-sth`
  response carries is **the backend root's `TimestampNanos`, converted to
  milliseconds** — not the personality's own clock — and `TreeSize` →
  `tree_size` and `RootHash` → `sha256_root_hash` come out of the same
  `SignedLogRoot`. Those three are also the three that enter the
  `TreeHeadSignature` input of TLS-CODEC.md §5.5. A **gate** group drives
  `get-sth` through the handler `ctfe.SetUpInstance` returns, against a
  driver-local backend answering a pinned root, and verifies each response's
  `tree_head_signature` as an ECDSA signature over that input rebuilt from
  those three values. An injected time source dates *SCTs* on the `add-chain`
  path (RFC 6962 §3.2); it does not date tree heads.
* **No two distinct signing inputs may share a signature.** Step 03's
  milestone, carried here by that same **gate** group: it moves the pinned root
  by one entry, then by one bit of the root hash, then by one millisecond, and
  requires a different `tree_head_signature` each time and one that verifies
  over the head actually served — then asks again with nothing changed at all
  and requires the **byte-identical** signature back, which is the per-log STH
  signature cache. Both halves are gate here: breaking either means the step is not
  satisfied.

**New at v1.0.13.**

* **Leaf hashes for embedded and non-embedded SCTs.** `ctutil.LeafHash` is
  given a chain, an SCT and an `embedded` boolean, and the boolean is one of
  the two inputs that decide the pre-image — the bullet after this one is the
  other. An embedded SCT covers the precertificate form, rebuilt with the
  **SCT-list** extension removed (`x509.RemoveSCTList` over the issued
  certificate's TBS — *not* the poison extension, which an issued certificate
  does not carry; X509-FORK.md §4.2, CLIENT-API.md §2) and the issuer key hash
  filled in. All three shapes — a plain issued certificate, a precertificate
  with its issuer, and a certificate carrying the SCT inside it — are compared
  byte for byte against pinned vectors, and a group checks the rebuilt TBS
  itself.
* **Which entry type a leaf gets, and which builder makes it.** `embedded` is
  not the only input. `LeafHash`, `LeafHashB64` and `VerifySCT` hash the chain
  as a `PrecertLogEntryType` entry whenever `embedded` is true **or** `chain[0]`
  is itself a precertificate — `(*x509.Certificate).IsPrecertificate`,
  X509-FORK.md §4.4 — and as an `X509LogEntryType` entry only for a plain
  issued certificate with `embedded` false. Handing in a poisoned
  precertificate with `embedded` false is therefore not an error and does not
  give an X.509 leaf: it gives the precertificate leaf, and a group pins that
  hash and that signature check. The two routes to the pre-image are distinct
  and both are checked — the `embedded` case goes through
  `ct.MerkleTreeLeafForEmbeddedSCT`, every other case through
  `ct.MerkleTreeLeafFromChain` with the entry type just chosen (CLIENT-API.md
  §3).
* **`MerkleTreeLeafFromChain` is not a second route to the embedded leaf.**
  Asked for a `PrecertLogEntryType` entry it performs the *submission*-side
  rebuild of X509-FORK.md §4.2–§4.3, which begins by removing the **poison**
  extension. An issued certificate carrying an SCT list has no poison
  extension, so that call **fails** rather than returning a leaf, and
  `ct.MerkleTreeLeafForEmbeddedSCT` is the only route for that certificate. A
  group checks exactly this refusal.
* **`embedded` is verified, not trusted.** `LeafHash`, `LeafHashB64` and
  `VerifySCT` must **error** when `embedded` is true and `chain[0]` does not
  in fact carry that SCT — the `ContainsSCT` pre-check of CLIENT-API.md §2.
  Two cases check exactly this.
* **An absent chain or an absent SCT is an error, not a panic.** Before
  `embedded` is consulted at all, `LeafHash`, `LeafHashB64` and `VerifySCT`
  **reject an empty `chain` and a nil `sct`** with an error, and they reject by
  returning — the zero hash, the empty string, a non-nil error — not by
  indexing `chain[0]` or dereferencing the SCT (CLIENT-API.md §2). **Four
  cases** check exactly these two refusals, two through the hashes and two
  through `VerifySCT`. `ct.MerkleTreeLeafForEmbeddedSCT` is held to the same
  standard for the same reason: it needs the issuer as well as the
  certificate, so a chain of **fewer than two** is an error returned before
  anything reaches `chain[1]` (CLIENT-API.md §3), and a fifth case checks that
  one. A group is one process: a driver that panics inside your code loses
  every case in its group, not just the case that panicked.
* **Log-list JSON, schema v1.** The exact field names of CLIENT-API.md §4.1,
  including `final_sth` and `disqualified_at`, parsed from a pinned fixture and
  from a signed fixture.
* **`LogInfo.MMD` is a `time.Duration`.** A log list gives
  `maximum_merge_delay` as an `int` counting **seconds** (CLIENT-API.md §4.1);
  the `LogInfo` that `NewLogInfo` and its DNS variants build carries that many
  seconds as a duration, so a list entry saying `86400` gives an `MMD` equal to
  `86400 * time.Second`. A group compares it against pinned values.
* **Failure returns carry no partial answer.** Every `VerifyInclusion*`
  returns **-1** with its error, never 0, because 0 is a valid leaf index. The
  same discipline is checked across the rest of the surface: `LeafHash` returns
  the **zero** 32-byte array and `LeafHashB64` the **empty string** when either
  errors, and `LogInfoByKeyHash` / `LogInfoByKeyHashOverDNS` return a **nil**
  map rather than the entries they had built before the failure. A caller that
  ignores the error must not be able to mistake the value for an answer.
* **The cached STH.** Which of the `VerifyInclusion*` family fetches an STH,
  and which of them stores what it fetched where `LastSTH` can see it, is
  checked exactly as CLIENT-API.md §2.1 states it: `VerifyInclusion` fetches
  every call and records; `VerifyInclusionLatest` fetches only when the cache
  is empty, and records; `VerifyInclusionAt` never fetches. All three, and
  `VerifySCTSignature`, re-stamp the leaf they are handed before hashing it.
* **`findlog`'s stdout.** One line per log matched, in the order the arguments
  were given; each line carries that log's description and its URL, and no
  line is printed for an argument matching nothing. Under `--verbose` each of
  those lines is followed by an indented block for the same log carrying: its
  DER key in hex **and** base64, the SHA-256 of that key in hex **and**
  base64, its merge delay in seconds, one entry per operator, its
  `dns_api_endpoint` when it has one, and — when it has a `final_sth` or a
  `disqualified_at` — that STH's four fields and the raw `disqualified_at`
  value. The two **byte-valued** STH fields, `sha256_root_hash` and
  `tree_head_signature`, are printed in **lowercase hex**, not in the base64
  the log list carries them as; the log list's own encoding is a transport
  detail of the JSON and is not what this block shows. `tree_size`,
  `timestamp` and `disqualified_at` are printed as the decimal integers the
  list gives. A group checks that those values are present and correct for a
  pinned list; the exact wording, spacing and date rendering around them are
  **not** checked.
* **The strings compared byte for byte at this milestone, in full.** Section
  3.3 sets its no-standard-library-wording rule aside only for what *this*
  section lists, so here is the list. It has two entries, and both are lines
  the standard `flag` package prints complete, with nothing left to the
  implementer to word:

  1. **The first line of a `--help` block**, compared as a whole line against

         Usage of <path the group invoked>:

     — the word `Usage`, a space, `of`, a space, the **absolute path the group
     used to start the binary**, and a colon. Nothing precedes it and nothing
     else shares the line. It is contract because the `flag` package's default
     usage function produces it from `os.Args[0]`: you get it by leaving
     `flag.Usage` alone, and you lose it by installing a usage function of your
     own — including one that opens with the synopsis CLI.md §4.5 describes for
     `findlog`. If you want a synopsis, print it **after** this line. It is
     compared for every flag-parsing program a group runs: the inherited ones
     of section 4 other than `chainfix`, which aborts before the `flag` package
     is reached, and all three of this milestone's new commands. **One of the
     groups that compares it is a gate group**, so a replacement `flag.Usage`
     fails the step.
  2. **A flag's declaration line** inside that same block, compared as a whole
     line, together with the `(default …)` suffix on the line beneath it. The
     `flag` package renders a declaration as two spaces, `-`, the flag's name,
     and — for every type except `bool` — a space and the type word, putting
     the description, and any default, on the *following* line. The declaration
     lines compared at this step are `  -inclusion`, `  -dns`,
     `  -log_list string`, `  -log_uri string` and `  -deadline duration`. Two
     default suffixes are compared with them, each as a **suffix** of the line
     that follows its own declaration: `(default true)` under `sctscan`'s
     `-dns`, and `(default 30s)` under `sctcheck`'s `-deadline`. Two absences
     are compared the same way: the line under `sctscan`'s `-inclusion` and the
     line under `sctcheck`'s `-dns` must contain no `(default ` at all, because
     both of those flags are `bool` with the zero default and the `flag`
     package then prints none (CLI.md §4.4 gives every one of these). What is
     contract is the flag name, its type and its default; the
     two-space-and-type rendering and the `(default …)` spelling are the `flag`
     package's own and are the same for every implementation that uses it.
     **No description is ever compared** — the suffix is read off the end of
     the following line, and the sentence in front of it is not looked at.

  **That list is exhaustive.** No other output of any program is compared
  against a fixed string at this milestone, on either stream. Every remaining
  assertion on a program's stdout or stderr in this step is one of three other
  kinds, and none of them checks wording:

  * a **substring** match on an identifier the documents themselves fix — a
    log's description, its URL, its DER key, that key's hash, the merge delay,
    an operator name, a `dns_api_endpoint`, the STH values in the `findlog`
    bullet above;
  * an assertion that a stream is **empty** — `findlog`'s stdout when an
    argument matched nothing and on each of its error exits, and `sctcheck`'s
    stdout on a usage error and under `--help`;
  * the **shape** of stdout: how many non-empty lines it holds and in what
    order, which is how "one line per match, in argument order, and no line for
    an argument that matched nothing" is checked at all. The lines are counted
    as well as read; what is *in* them is the substring rule above.

  No diagnostic, no error message and no usage description is compared
  anywhere in this step.

**Three things step 03 listed are not here.** They are named because you no
longer have step 03's file to check them against, and reconstructing them from
memory would have you defending a contract this step does not hold you to:

* **The two RFC 6962 extension sections of a certificate dump.** Step 03
  compared `x509util.CertificateToString`'s poison and SCT-list renderings byte
  for byte, indentation and hex widths included. **No group at this step
  compares them**, and they are therefore absent from the byte-for-byte list
  above. CLIENT-API.md §5.2 still describes that rendering and you should not
  remove it — a later step may measure it again — but nothing here does.
* **The two configuration diagnostics `empty backend name` and `duplicate
  backend name`**, which step 03 matched as documented prefixes. This step has
  no configuration-validator group at all, so no validator diagnostic is read
  for its wording anywhere here.
* **`Content-Type: application/json` as a header value.** Step 03's in-process
  groups read it off the recorder. No group at this step reads a response
  header. HTTP-API.md §2 still fixes it, and the `get-roots` and `get-sth`
  responses this step *does* decode are JSON, so keep setting it; it is simply
  not what is being observed here.


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
| `inherited-http-log-client` | gate |
| `inherited-merkletree-proofs` | gate |
| `inherited-multi-backend-serving` | gate |
| `inherited-rfc6962-leaf-hash` | gate |
| `inherited-sth-signature-cache` | gate |
| `ct-key-and-time-helpers` | requirement |
| `ct-merkle-leaf-for-embedded-sct` | requirement |
| `ctutil-contains-sct` | requirement |
| `ctutil-endtoend-verification` | requirement |
| `ctutil-inclusion-proof` | requirement |
| `ctutil-leaf-hash` | requirement |
| `ctutil-loginfo-and-dns-clients` | requirement |
| `ctutil-verify-sct` | requirement |
| `findlog-cli` | requirement |
| `loglist-parse-and-find` | requirement |
| `loglist-signed-json` | requirement |
| `new-command-binaries` | requirement |
| `sctcheck-cli` | requirement |
| `x509-precert-helpers` | requirement |
| `x509util-issuer-and-file-loading` | requirement |
| `x509util-pem-parsing` | requirement |
| `x509util-sct-extraction` | requirement |

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

* **Every `dnsclient` read method.** `GetSTH`, `GetSTHConsistency` and
  `GetProofByHash`, and with them all four query shapes of the table in
  CLIENT-API.md §6.1, are **out of scope at this step**. The resolver is an
  unexported field that `New` fixes to `net.LookupTXT`, and the constructor
  that would let a test point a client at its own server —
  `NewForNameServer` — does not arrive until v1.1.0. Exercising them would
  need live DNS, and section 8 forbids that. Write them to the specification;
  nothing here runs them. What *is* checked of `dnsclient` is `New`: the key
  handling, the exported `Verifier` field, and that a `*DNSClient` satisfies
  `client.CheckLogClient` well enough for `ctutil.NewLogInfoOverDNS` to
  return one.
* **Symbols CLIENT-API.md lists for a later release.** `(*DNSClient).BaseURI`
  is from v1.0.17, `loglist.FindLogByKeyHashPrefix` from v1.0.21, and
  `dnsclient.NewForNameServer`, `x509util.MarshalSCTsIntoSCTList` and the whole
  of `loglist`'s filtering surface — `logfilter.go`, with `ActiveLogs`,
  `Compatible` and the rest (CLIENT-API.md §4.1) — from v1.1.0. None of them
  exists at v1.0.13 and none is looked for. At v1.0.13 `loglist` is
  the schema, the two constructors and the five finders, and nothing else.
* **Surface that belongs to an earlier step.** `merkletree`'s
  `VerifyInclusionProofByHash` and `RootFromInclusionProofAndHash`, and
  `x509util`'s `ReadPossiblePEMFile` and `ReadPossiblePEMURL`, are all v1.0.5
  symbols checked at step 02; the front end of step 01, the multi-backend
  configuration of step 02 and step 03's STH signature cache are **in scope** at
  their own steps and are out of scope again here. Out of scope is not the same as
  not run: this step inherits all of it and must not break it, so the build
  group still checks every binary and **five** groups re-run inherited
  behaviour — `inherited-http-log-client`, `inherited-merkletree-proofs`,
  `inherited-rfc6962-leaf-hash`, `inherited-multi-backend-serving` (step 02's
  multi-backend configuration) and `inherited-sth-signature-cache` (step 03's
  STH signature cache). The last of those matters more than the others at this
  step in particular: the repair §3.5 describes lands in
  `trillian/ctfe/serialize.go`, the same file that holds `signV1TreeHead` and
  its two cache calls, so a repair that rewrites that function can drop the
  cache without touching anything this step requires. The group drives the
  personality's `get-sth` entrypoint in process, varies one component of the
  RFC 6962 signature input at a time and requires that no two distinct inputs
  share a signature and that an unchanged input is served from the cache. All
  five are **gate** groups (§7): they re-run that behaviour without adding a
  requirement, and a regression there means the step is not satisfied.
* **Fetching a log list over the network.** The list is a fixture.
* **`LeafHashForLeaf`**, which belongs to v1.0.14 and is checked from step 05.
* **Anything dated after v1.0.13**, including `loglist2`, `loglist3` and
  `VerifySCTWithVerifier`.

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

Verification stops being something the front end does to itself and becomes a
library anybody can import. That reframing is the milestone: after this step a
third party holding a certificate, an SCT and a log's public key can decide, on
their own, whether that SCT is genuine and whether the log kept its promise.

Two details matter most. The first is `embedded`: the same SCT
verifies against two different pre-images depending on whether it travelled
inside the certificate or beside it. Setting it false when the SCT was
embedded yields a leaf hash that is well-formed and wrong; setting it true
when the certificate does not carry that SCT must be refused outright. The
second is the `-1` return: an inclusion check that fails must not return index
0, which is a perfectly good answer.

`loglist` and `dnsclient` arrive with it — the first because verification needs
to know which logs exist, the second because an auditor may not want to reveal
which certificate they are asking about.
