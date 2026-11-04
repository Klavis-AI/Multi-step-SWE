# TEST_INTERFACE — step 09 of 13 (certificate-transparency-go v1.1.2)

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
whether that applies at this milestone. **It applies here.**

Section 2's pinned manifest is v1.1.2's, and a number of imports in the
tree you were handed cannot be resolved against it. They fall into three
groups:

* `google.golang.org/grpc/naming`, which grpc 1.40 deletes outright. It is
  imported directly by `trillian/util/resolver.go`, and indirectly by
  `trillian/ctfe/ct_server` through `github.com/coreos/etcd`'s
  `clientv3/naming` (ARCHITECTURE.md §2.1, RELEASE-MAP.md §9).
* the modules behind `gossip/` outside `minimal/x509ext` —
  `github.com/mattn/go-sqlite3`, `github.com/google/monologue/…` and
  `github.com/google/trillian-examples/gossip/…` — which leave the require
  list entirely. Nothing you can write makes those files compile
  (RELEASE-MAP.md §9).
* packages that the pinned `github.com/google/trillian` provided at
  v1.1.0 and no longer provides here: `crypto`, `util/etcd` and the
  three self-registering `crypto/keys/{der,pem,pkcs11}/proto` packages, named
  by `trillian/ctfe/ct_server` and `trillian/migrillian`. These are not on this
  milestone's feature list; they are the cost of the pin moving, the compiler
  names every site, and moving the key-handler registration into the
  application is the upstream repair.

Until that code goes, `go build ./...` in `/app` fails at import resolution,
section 1's build obligation is not met, and `build-clean` — which section 7's
table lists as **GATE** — fails on the very first run, before you have changed
anything. Adding the withdrawn modules back is not an option: section 2 fixes
the dependency universe and checking is offline against a module cache that does
not hold them.

Two consequences, because neither is guessable from section 7 alone:

* A no-op here satisfies none of the new work: the gate itself fails. Section 7's
  note on no-ops covers both cases; v1.1.2 is the second one.
* Until the whole tree compiles there are **no candidate binaries at all**.
  The harness builds them with a single `go build -o <dir>/ ./...`, and that
  command writes nothing whatsoever when any package in the pattern has an
  unresolvable import — not "everything except the broken package". So every
  group of section 4 fails for want of the program it runs, `gate-endtoend`
  included, and so does every driver that links `trillian/ctfe`, which reaches
  `trillian/util` from both `handlers.go` and `instance.go`. A log full of
  groups failing for want of a binary, with nothing in them to do with the
  resolver or the gossip tree, is the signature of exactly this.

## 4. Command-line programs the verifier runs

v1.1.2 removes three programs and adds none, so the tree holds **15**
`package main` directories (CLI.md §1): `gossip_server`, `goshawk` and
`gosmin` are all gone, and only `gossip/minimal/x509ext` survives of the
gossip work.

| program | directory | what a group does with it |
| --- | --- | --- |
| `ct_server` | `trillian/ctfe/ct_server/` | 19 flags. Checked on the whole lifecycle: `--by_range` **must no longer exist**, `--align_getentries` (default **true**) and `--pkcs11_module_path` must, and a group passes the dead flag and requires exit **2**. The **whole set** is checked too: the non-glog flag names in `--help`, sorted, are compared against an exact list of nineteen (CLI.md §2.1's totals row), so a stray `flag` global anywhere in the `ctfe` import graph is a failure |
| `sctcheck`, `sctscan` | `ctutil/…` | checked on the `dns` flag being **gone** from both (CLI.md §4.4), and on `--log_list` surviving |
| `ctclient` | `client/ctclient/` | same: `--dns`, `--dns_base` and `--dns_server` all disappear here too (CLI.md §3.1, §4.4), and each is checked. Still the flat, one-command-word grammar — the rebuild is the next step |
| the other eleven | see CLI.md §1 | built and enumerated |

The flag-grammar and exit-status rules of earlier steps are unchanged and
still checked. Four things about `--help` that the groups rely on, stated here
because they are the harness's boundary rather than upstream's prose:

* Every program but `chainfix` parses its command line with Go's standard
  `flag` package, so **`-h`/`--help` prints the usage block and exits `0`** —
  it is not the unrecognised-flag path of CLI.md's preamble, which prints the
  same block and exits `2`. `chainfix` reads positional arguments first and
  exits `2` either way (CLI.md §4.6).
* The block goes to **stderr** on both of those paths — CLI.md's preamble says
  so, and it is the `flag` package's default output, which nothing in the
  product overrides — and **none of it goes to stdout**. The groups capture the
  two streams separately and check the usage text on stderr alone, so a program
  that wrote its usage to stdout instead would fail every case that reads it.
* The first line of that block is `Usage of <the path the binary was invoked
  by>:`, and each flag is one line of the `flag` package's own rendering:
  two spaces, a hyphen, the name, then the type word for a non-Bool
  (`  -log_list string`) and nothing for a Bool (`  -align_getentries`), with
  a non-default-valued flag's `(default …)` in the indented description block
  beneath it. **The wording of a flag's description is never compared** — it is
  yours to write.
* `align_getentries` is a `flag` package global of `trillian/ctfe`, and
  `sctcheck` and `sctscan` both reach that package through `loglist`, whose
  `logfilter.go` uses `ctfe.PEMCertPool`. So the flag appears in **their**
  usage output as well as `ct_server`'s, and a group checks that it does. This
  import edge is inherited, not new at this milestone; nothing asks you to
  create or remove it.

## 5. Packages and symbols the verifier links against

| import path | what the drivers use |
| --- | --- |
| `…/trillian/ctfe` | `ValidateLogConfigs([]*configpb.LogConfig) error` — new here — alongside `ValidateLogConfig`; `ValidatedLogConfig.KeyUsages` in its v1.1.2 meaning; `BuildLogBackendMap(*configpb.LogBackendSet) (LogBackendMap, error)`, the constructor that indexes a backend set by name and is what `LogBackendMap` values come from; `LogConfigFromFile` and `MultiLogConfigFromFile` in their v1.1.2 meaning (either encoding), `ValidateLogMultiConfig`, `ToMultiLogConfig`; the admission path with its leaf-only EKU filter; the `get-entries` handler's range coercion |
| `…/trillian/util` | reduced to `log_leaf.go` and `timesource.go`. `FixedBackendResolver` and the `fixedBackends` watcher behind it are **removed** with the whole of `resolver.go`, because `google.golang.org/grpc/naming` is not in the pinned dependency set here; a probe naming `util.FixedBackendResolver` is required not to build, and a tree that keeps the file fails to compile `trillian/util` and everything importing `trillian/ctfe` (ARCHITECTURE.md §2.1) |
| `…/ctutil` | `VerifySCTWithVerifier(sv *ct.SignatureVerifier, chain, sct, embedded)`, new here, including its refusal of a nil verifier. Going the other way, `NewLogInfoOverDNS`, `NewLogInfoOverDNSWrapper` and `LogInfoByKeyHashOverDNS` are **removed**, and a probe naming any of them is required not to build |
| `…/asn1` | `TagBMPString` and `Unmarshal` over a universal tag-30 element — the constant does not exist at v1.1.0, so a driver naming it cannot compile there |
| `…/x509` | the Go 1.13/1.14 merge: `PureEd25519` and the `Ed25519` key algorithm, `id-Ed25519` SPKI and PKCS#8 parsing with parameters **absent**, DSA digest truncation, and a nil-safe `Certificate.Equal`. Also `SignatureAlgorithmFromAI(pkix.AlgorithmIdentifier) SignatureAlgorithm`, the fork's **exported** mapping from a parsed `AlgorithmIdentifier` to a `SignatureAlgorithm` — it has no `crypto/x509` counterpart, because the standard library keeps its equivalent unexported, so §1's mirror rule does not supply it. `Certificate.SignatureAlgorithm` is set from the **TBS** copy of the identifier, so a lax-parsed certificate whose two identifiers disagree reports the inner one — an outer `ecdsa-with-SHA384` over a TBS `ecdsa-with-SHA256` reports `ECDSA-SHA256` (X509-FORK.md §5.4) |
| `…/dnsclient` | **must not exist.** A probe that imports it is required to fail to build *naming that import path*, alongside a positive control that must build; that failing build is the observation |
| `…/client` | `AddJSON` is **removed** from `LogClient`, and a probe naming it is required not to build; the rest of `LogClient` is unchanged |
| `…/loglist` | the v1 log list, which **must still resolve at v1.1.2** — it is the positive control the `…/ctutil` row's three removals are measured against. `loglist.Log` and `loglist.LogList` are the argument types of the two `LogInfo` constructors that **survive** the DNS cut: `ctutil.NewLogInfo(log *loglist.Log, hc *http.Client) (*ctutil.LogInfo, error)` and `ctutil.LogInfoByKeyHash(ll *loglist.LogList, hc *http.Client) (ctutil.LogInfoByHash, error)`. One probe declares both types, calls both constructors and is required to **build**; the probe that must *not* build declares the same two types beside the three withdrawn `…OverDNS` names, so it has to fail reporting `undefined: ctutil.NewLogInfoOverDNS` (and its two siblings) rather than an unresolved import. A tree that deletes `loglist/`, or that moves `Log` or `LogList` out of it, fails **all five** of that group's library cases rather than one. Nothing at this milestone changes the package: the `logfilter.go` edge onto `ctfe.PEMCertPool` that §4 describes is inherited, and no group asks you to add to it or take from it |
| `…/loglist2` | the v2 log-list URLs: `LogListURL`, `LogListSignatureURL` and `AllLogListURL` are `https://www.gstatic.com/ct/log_list/v2/log_list.json`, `…/v2/log_list.sig` and `…/v2/all_logs_list.json` — `v2`, not the `v2beta` of v1.1.0. They are compared as string constants; nothing fetches them |
| `…/gossip/minimal/x509ext` | the one part of `gossip/` that **survives** the collapse: `OIDExtensionCTSTH`, `OIDExtKeyUsageCTMinimalGossip`, the `LogSTHInfo` struct, and `LogSTHInfoFromCert`, `HasSTHInfo` and `STHFromCert` with the signatures CLIENT-API.md §6.2 gives. A probe names all six and is required to **build** — it is the positive control beside the three withdrawn gossip binaries, and a tree that deletes `gossip/` wholesale fails it |
| `…` (root) | the endpoint path constants a server group builds its URLs from: `GetRootsPath`, `AddChainPath`, `GetEntriesPath`, `GetProofByHashPath`, `GetEntryAndProofPath`, `GetSTHPath`, `GetSTHConsistencyPath` — the paths themselves are HTTP-API.md §1's, these are the spellings the drivers name. `AddJSONPath` and `AddJSONRequest` (member `Data`) are named as well: they **outlive** `LogClient.AddJSON`, which goes at this milestone, and a probe naming them is required to build (HTTP-API.md §1, CLIENT-API.md §4.1). One group also calls the response conversion this milestone adds, `(*AddChainResponse).ToSignedCertificateTimestamp() (*SignedCertificateTimestamp, error)` (CLIENT-API.md §3), beside the older `(*GetSTHResponse).ToSignedTreeHead()` |

`ctfe.ValidateLogConfigs([]*configpb.LogConfig) error` is checked as the
whole-*set* validator CONFIG.md §4.1 introduces, applied to a bare
`LogConfigSet`. It is handed no backend set, so the rules it applies are
exactly the backend-free half of CONFIG.md §4.4: every member must pass
`ValidateLogConfig`, every prefix must be non-empty, prefixes must be
distinct, and — the one place it parts company with `ValidateLogMultiConfig` —
**tree ids must be distinct across the whole set**, because there are no
backend names to key them on. It is checked on accept against reject and on
nothing else: no message it returns is compared, and no group requires
`ct_server` to route any configuration through it.

Nothing in this section is inherited by reference. Your workspace carries this
`TEST_INTERFACE.md` and no earlier one, so the two tables here are the whole of
the surface: every import path a driver at this step links is a row in one of
them, and a path in neither is not part of it. The table below is the half that
predates v1.1.2 — the inherited symbols this step's groups name
**directly**, enumerated so that nothing is reached for that is not written
down. Its last two rows are the harness's own side of the wire rather than
yours: a driver links them to stand the backend stub of §3.4 up, and nothing you
write names them.

| import path | inherited symbol named by a requirement group |
| --- | --- |
| `…/trillian/ctfe` | `NewCertValidationOpts` and `CertValidationOpts`, `ValidateChain`, `IsPrecertificate`, `NewPEMCertPool` and `*PEMCertPool` with `AppendCertsFromPEM`; `BuildLogBackendMap` with `LogBackendMap`, `ValidateLogMultiConfig` and `ToMultiLogConfig` (all four unchanged here, and re-checked under an `inherited-` name) |
| `…/trillian/ctfe/configpb` | `LogConfig`, `LogConfigSet`, `LogBackend`, `LogBackendSet`, `LogMultiConfig`, built in a driver as well as read from a file, each with the `Get<Field>()` accessor beside every field (CONFIG.md §1.2) |
| `…/client` and `…/jsonclient` | `client.New(uri string, hc *http.Client, opts jsonclient.Options) (*LogClient, error)`, the constructor the positive-control probes call to prove the package still builds once `AddJSON` is gone |
| `…/ctutil` | `VerifySCT`, against which the new `VerifySCTWithVerifier` is required to agree; and `LeafHash(chain []*x509.Certificate, sct *ct.SignedCertificateTimestamp, embedded bool) ([sha256.Size]byte, error)` with its base64 sibling `LeafHashB64`, which RFC 6962 leaf hashing is checked through (signatures in CLIENT-API.md §2, the hashing rule in MERKLE.md §1) |
| `…/x509` | `ParseCertificate` and `*Certificate` with its parsed fields, `IsFatal`, `ExtKeyUsage` with `ExtKeyUsageAny`, `ExtKeyUsageClientAuth` and `ExtKeyUsageServerAuth`, `KeyUsageDigitalSignature` and `KeyUsageCertSign`, `CreateCertificate`, `MarshalPKIXPublicKey`, `ParsePKIXPublicKey`, `ParsePKCS8PrivateKey`, `UnknownSignatureAlgorithm` |
| `…/asn1` | `Unmarshal`, `ObjectIdentifier`, `RawValue` |
| `…/x509/pkix` | `AlgorithmIdentifier`, the argument `SignatureAlgorithmFromAI` takes, and `Name`, used to build a certificate template. This is the fork's `pkix`, not `crypto/x509/pkix`: §1's mirror rule applies to it as to the rest of the fork |
| `…/tls` | `Marshal`, `Unmarshal`, `DigitallySigned` |
| `…` (root) | `SignedCertificateTimestamp`, `DigitallySigned`, `SignatureVerifier` with `NewSignatureVerifier` and `PublicKeyFromPEM`, `MerkleTreeLeaf` with `TimestampedEntry`, `TimestampedEntryLeafType`, `X509LogEntryType` and `V1`, `ASN1Cert`, and the request/response types `AddChainRequest`, `AddChainResponse`, `GetSTHResponse`, `GetRootsResponse`, `GetEntriesResponse`; `SignedTreeHead`, which is what `x509ext.STHFromCert` returns |
| `google/trillian`'s `TrillianLogServer` | implemented **by the benchmark**, not by you: a server group stands the whole interface up in its own process and points `ct_server` at it over a loopback gRPC socket (§3.4). Two things about it are checked, and they are facts about *your* front end: how many `QueueLeaf` calls it makes and what leaf bytes it hands over, and how many `GetLeavesByRange` calls it makes and with what start index and count after coercion. Nothing the stub itself does is checked |
| `google/trillian`'s `keyspb` | `PrivateKey` and `PublicKey`, the two wrappers a log configuration carries a key in (CONFIG.md §2.3) |
| `google/trillian`'s `types` | `LogRootV1`, which the backend stub marshals into the `SignedLogRoot` it hands back from `GetLatestSignedLogRoot`, `GetLeavesByRange` and `InitLog`. Harness-side: your front end sees only the marshalled bytes, and §6's pinned tree size and root hash are what it must read out of them |
| `google.golang.org/grpc` | `NewServer` and `*Server`, with `trillian.RegisterTrillianLogServer`, which is how that stub is stood up on the loopback socket `ct_server` is pointed at. Harness-side as well, and worth one line of disambiguation: the **top-level** `grpc` package is in the pinned manifest at `v1.40.0` and resolves normally. It is `google.golang.org/grpc/naming`, and only that, which §3.5 and the `…/trillian/util` row above say is gone |

Two names in that table are reached as methods rather than as package-level
identifiers, so they are spelled out here:
`(*x509.Certificate).CheckSignature(algo SignatureAlgorithm, signed, signature []byte) error`
and `(*x509.Certificate).CheckSignatureFrom(parent *Certificate) error`. They are
how the `…/x509` row's DSA digest truncation is observed — over a pinned
`dsa-with-sha256` certificate, its own self-signature, and a pinned detached
signature — and the algorithm handed to the first is `x509.DSAWithSHA256`, whose
rendering §6 fixes.

## 6. Formats crossing the boundary

Step 08's `TEST_INTERFACE.md` is replaced by this file, so the surface it
listed is repeated here rather than referred to. Your workspace carries this
document and no earlier one, and the list below is given **in full**. It is
not step 08's list copied forward: every inherited item was re-checked against
this step's own groups at v1.1.2, two of them have changed sides since
the step that introduced them, and a number of families earlier steps checked
are out of scope here at all. §6.2 names those and says why each was dropped.

**Carried forward from the steps behind you, restated:**

* **TLS wire encodings, byte for byte.** Everything RFC 6962 defines in TLS
  presentation language — `MerkleTreeLeaf` with its `TimestampedEntry`,
  `ASN1Cert`, `DigitallySigned` — is compared as the bytes `tls.Marshal`
  produces, never as a Go value. A group builds the leaf it expects, marshals
  it, and compares hex against the `leaf_input` it read back; another
  `tls.Unmarshal`s the bytes your front end handed the backend and looks
  inside. Field order, length prefixes and the leaf's extensions field are all
  inside that comparison.
* **JSON bodies, read field by field.** Responses are decoded into the
  request/response types §5 lists and inspected per field. The body is never
  compared as one string and key order, whitespace and field order are never
  asserted. Base64-carried fields are decoded first and compared as bytes: a
  `leaf_input` against a marshalled leaf, an `extra_data` against its pinned
  bytes, a `get-roots` certificate against the DER of the configured root, an
  SCT's signature against a verification. The query-parameter spellings are
  HTTP-API.md §4's and are part of the surface — `start`, `end`, `first`,
  `second`, `leaf_index`, `tree_size`, `hash`.
* **RFC 6962 leaf hashes.** `ctutil.LeafHashB64` and `ctutil.LeafHash` are
  compared against pinned known answers held in a fixture: the x509-entry
  form, the precert-entry form built from leaf plus issuer, and the raw 32
  bytes agreeing with the base64 for the same input. Getting a hash right
  means getting its pre-image right — the `0x00` leaf prefix over the leaf's
  own TLS encoding — but it is the hash that is compared. A precertificate
  without its issuer is an error, not a hash, and an embedded-SCT leaf hashes
  to the **precert** entry, not to the x509 entry.
* **PEM.** Roots reach the log as a `roots_pem_file` and come back out of
  `get-roots` as base64 DER, so the PEM-to-DER path is compared by digest. The
  log's private key reaches it through the key-handler registry (CONFIG.md
  §2.3) and this step exercises **both** wrappers: `keyspb.PrivateKey` with the
  DER inline, which the live-server groups use, and `keyspb.PEMKeyFile` with a
  `path` and **no** `password`, pointing at an unencrypted
  `-----BEGIN PRIVATE KEY-----` block, which the parse-only configuration
  fixtures use. A public key travels as `public_key { der: … }`.
* **The configuration file's encoding — and this is one of the two that
  inverted.** Earlier steps could say the file is text proto and nothing else.
  At v1.1.2 that is false: a file that does not parse as text proto is
  **retried as binary proto** (CONFIG.md §4.2), and that retry is one of this
  step's own checked capabilities. What survives from the earlier statement is
  the narrower half — a file that is neither encoding is refused, and the
  refusal is an `error` whose only channel is its message.
* **The configuration's shape.** `LogMultiConfig` wrapping a `LogBackendSet`
  and a `LogConfigSet`; `backend_spec` is an opaque string this benchmark fills
  with a loopback address and nothing parses it; each log names its backend
  with `log_backend_name`; tree ids are keyed **per backend**, so the same tree
  id on two different backends is accepted and the same tree id twice on one
  backend is rejected. The single-backend `LogConfigSet` form still loads.
* **URL-path assembly.** A log's prefix is joined to the `/ct/v1/…` suffixes,
  `/healthz` and `/metrics` are never prefixed, and a path no configured log
  claims answers **404** rather than being served by some other log.
* **`/healthz`.** The body is exactly `ok` with status 200 — compared as a
  whole string, with no trailing newline.
* **The tree head's three fields.** `tree_size`, `timestamp` and
  `sha256_root_hash` are taken from the backend's `SignedLogRoot`
  (`TimestampNanos` divided by 1e6 for the timestamp) and the
  `tree_head_signature` verifies under the log's configured key. A group pins
  all three values and verifies the signature with a `SignatureVerifier` built
  from the log's public key.
* **Admission decisions, as verdicts.** Chain validation is checked on what it
  decides, not on what it says: path order, an unrooted chain, the expired /
  unexpired inversion of the `not_after_start` / `not_after_limit` window, that
  the window is half-open at its upper bound, the only-CA case, and
  `IsPrecertificate`. Over HTTP the same decisions appear as a status — `400`
  for a refusal — and as whether the backend was called at all.
* **Mirror configurations.** A mirror log must carry a `public_key` and must
  **not** carry a private key. Checked as accept/reject, not on wording.
* **The queued leaf.** How many `QueueLeaf` calls your front end makes and what
  bytes it hands over. One admitted `add-chain` is exactly one `QueueLeaf`, and
  the submitted leaf's DER must be inside the TLS-decoded `MerkleTreeLeaf` the
  backend received. The same counting rule covers `GetLeavesByRange`: how many
  calls, with what start index and what count, after this milestone's coercion.
* **The forked x509's non-fatal boundary — the other one that inverted.**
  A defect that lands in `NonFatalErrors` leaves `x509.IsFatal(err)` false and
  returns a **usable** `*Certificate`; a fatal one returns nil. That much is
  unchanged. What changed is which defects sit on which side: from v1.1.0 the
  `ParseCertificate` family retries a failed strict `asn1.Unmarshal` with the
  `lax` parameter and records the strict error as non-fatal (X509-FORK.md
  §3.2), so at v1.1.2 the rows an earlier step listed as fatal are
  **non-fatal**. A group checks exactly these as non-fatal *and* reads
  recovered field values back off the returned certificate: a non-minimally
  encoded INTEGER serial; a non-minimally encoded RSA exponent; a negative RSA
  modulus; an RSA `AlgorithmIdentifier` whose parameters are **not** `NULL`; an
  `extKeyUsage` extension that is present but empty; a zero-length OBJECT
  IDENTIFIER sitting beside two real EKU OIDs, where the two real ones survive
  in order; a `PrintableString` holding bytes outside the set; an
  unsupported-curve key; an empty `SubjectInfoAccess`; and a signature
  `AlgorithmIdentifier` that disagrees with the one inside the TBS. The floor
  is still there and is measured with them: **truncated DER is fatal**, and the
  returned certificate is nil.
* **The declared flag set, and the `flag` package's rendering of it.** Which
  flags a program declares is compared as an exact set, and the usage block is
  read as text: one `  -name` or `  -name type` line per flag, its description
  indented under it, and `(default …)` present only where the flag's zero value
  is not the default. An undefined flag is exit status **2**. The usage
  block goes to **stderr** and only stderr — that is asserted, and CLI.md's
  preamble states it as behaviour.
* **Prometheus text exposition.** `/metrics` is read as text and a counter is
  located by its metric name and its label spellings. A label combination that
  was never incremented is simply absent from the exposition and reads as 0.

And, new at v1.1.2:

* **`Cache-Control` headers.** Exactly `public, max-age=86400` on
  `get-sth-consistency`, `get-proof-by-hash`, `get-entries` and
  `get-entry-and-proof`; **no** such header on `get-sth` or `get-roots`
  (HTTP-API.md §4.7). The header is compared as a string.
* **Coerced `get-entries` ranges.** The response's entry count and the
  boundary it stops at, for a request wider than the maximum and for one that
  is not aligned, with `--align_getentries` both on and off. The counter
  `aligned_get_entries{logid,aligned}` is read off `/metrics` as text.
* **Binary-encoded configuration.** The same `LogConfigSet` or
  `LogMultiConfig` message serialised as binary protobuf rather than text
  proto, in a file with any name; both must load, and a file that is neither
  must be **refused** by both entry points. The refusal is an `error`, so the
  only channel it has is its message, and §3.3 limits what may be read out of
  one: a group requires it to name the two identifiers the documents fix — the
  message type it was trying to build (`LogConfigSet` or `LogMultiConfig`,
  CONFIG.md §1) and the **path of the file it read** — and nothing else. The
  sentence they sit in is yours, in any wording and any order, and the
  protobuf library's own reasons are never looked at. Returning a non-nil
  error that names both is sufficient.
* **An EKU refusal.** Observed as a **status and an absence**, not as text:
  `add-chain` returns `400` and the backend is never called (HTTP-API.md
  §5.4). Nothing in the response body is compared — the body is a message, and
  §3.3 does not admit one here, because the status and the untouched backend
  already carry the whole checked fact. The same section's **leaf-only** rule is
  checked alongside it: the EKU list is tested against the leaf, so a chain
  whose *intermediate* carries no `extKeyUsage` extension at all is still
  admitted when the leaf's EKU is in the configured list.
* **A backend that cannot be dialled.** Where a group configures two backends
  and stands none of them up, `ct_server` still binds and serves (CLI.md §2.2
  step 5), and every request that needs the log answers **503** — the
  `Unavailable` row of HTTP-API.md §6.1 — after the handler has done its own
  range parsing, coercion and counting. That status is part of what those
  groups assert.
* **Ed25519 known answers.** Keys, SPKIs and signatures derived from a pinned
  seed; Ed25519 is deterministic, so every byte is fixed.
* **Algorithm names.** A short, closed list of `SignatureAlgorithm.String()`
  and `PublicKeyAlgorithm.String()` renderings is compared as text, taken
  either from the constant or off a parsed certificate. New here:
  `x509.PureEd25519` and `x509.Ed25519` both render **`Ed25519`** — the RFC
  8410 name, not the Go constant's spelling. Unchanged since v1.1.0:
  `x509.DSA` renders `DSA`, `x509.DSAWithSHA256` renders `DSA-SHA256`,
  `x509.ECDSAWithSHA256` renders `ECDSA-SHA256`, and the `PublicKeyAlgorithm`
  constants `x509.ECDSA` and `x509.RSA` render `ECDSA` and `RSA`. No other
  algorithm constant's rendering is compared at this milestone.

### 6.1 The strings this step compares whole

This is the audited list. A string not on it is not compared as a string at
this milestone, whatever some earlier step's list said.

**Compared as a complete value:**

* `ok` — the whole body of `/healthz`, with status 200 and nothing else in it.
* `public, max-age=86400` — the `Cache-Control` value on the four immutable
  endpoints. On `get-sth` and `get-roots` the checked fact is that the header
  key is **absent**, not that it is empty.
* `Usage of <absolute path of the binary>:` — the first line every
  flag-parsing program writes when given `--help`, with exit status 0.
* `  -align_getentries` — an exact line in `ct_server`'s usage block, with
  `(default true)` in the description indented beneath it. Likewise
  `  -pkcs11_module_path string` in `ct_server`, and `  -log_list string` in
  `sctscan` and `sctcheck`.
* The sorted, space-joined list of `ct_server`'s **19** non-glog flag names,
  exactly as §4 gives it. This is a set comparison: an extra flag fails it and
  a missing one fails it.
* The three `loglist2` endpoint constants, each equal to its whole URL:
  `https://www.gstatic.com/ct/log_list/v2/log_list.json`,
  `https://www.gstatic.com/ct/log_list/v2/log_list.sig`,
  `https://www.gstatic.com/ct/log_list/v2/all_logs_list.json`.
* The algorithm renderings the last bullet above lists: `Ed25519`, `DSA`,
  `DSA-SHA256`, `ECDSA-SHA256`, `ECDSA`, `RSA`.
* `30` as the decimal value of `asn1.TagBMPString`, and the decoded
  `BMPString` payloads `Beavis`, `ℕ - Double-struck N` and `B` (the last
  after a terminator is stripped).
* `sentinel` — the value a group plants in a destination string field before
  handing `asn1.Unmarshal` an **odd-length** `BMPString` body. The rejection
  must leave that field exactly as it found it and return a **nil** `rest`
  alongside the error. That is `encoding/asn1`'s own behaviour on a failed
  unmarshal, which X509-FORK.md §1 requires the fork to mirror; X509-FORK.md
  §2.4 states only that the odd length is an error. The error's own text is
  not compared.
* `truefalsefalsetrue` — the four `Certificate.Equal` nil-safety answers
  concatenated in order.
* `302a300506032b6570032100` — the hex prefix of an Ed25519 SPKI, with the
  32-byte public key appended.
* `000000` — the hex of each pinned `extra_data` the backend serves.
* `[]`, `[1 2]` and `[9]` — the `fmt` rendering of `ValidatedLogConfig.KeyUsages`
  for the nil, two-usage and single-usage cases.
* The hex of `sct.LogID.KeyID`, equal to the whole contents of the pinned
  `minted/keys/log-id.txt`.
* The pinned RFC 6962 leaf hashes in `rfc6962/kat.txt`, base64, whole.
* The comma-joined sorted SHA-256 hex digests of the DER `get-roots` returns.
* The configuration values a group writes into a config file and reads back
  through your parser and validator, rendered and joined one string per case:
  a `log_id` with its `prefix` (`1001/binary-alpha`, `text-alpha`);
  `roots_pem_file`, `ext_key_usages`, `log_backend_name`, `is_mirror` and the
  byte length of `public_key.der`; a backend `name` with its `backend_spec`
  (`be-a@127.0.0.1:65000`); the name `default` that `ToMultiLogConfig` lifts a
  bare `LogConfigSet` onto; and the `prefix@backend` binding each log in a
  multi-config carries. Each of these is a value the benchmark supplied, so
  what is checked is that it survives the round trip unchanged, not that you
  chose it — §6's "configuration's shape" bullet is the requirement and this
  is the form it is read in.

**Renderings of facts already on this list, in the group's own vocabulary:**

A group that has to compare several outcomes in one case renders them into a
token of its own choosing and compares the whole token. The token is the
group's; the facts inside it are the ones above. You do not produce any of
these spellings, and no other spelling of yours is compared.

* `nil` and `err` for whether a call returned an error, joined with `/` and `|`
  (`nil/err|nil/err` — `VerifySCT` and `VerifySCTWithVerifier` agreeing on the
  same good and bad SCT).
* `true` and `false` for a boolean answer, concatenated in order
  (`truefalsefalsetrue`, above).
* `status=<n> aligned=<n> misaligned=<n>`, which carries one HTTP status and
  the two `aligned_get_entries` readings §6 already describes into a single
  comparison.
* The `%q` list form where a `SEQUENCE OF` decodes to `[]string`
  (`["Beavis" "ℕ - Double-struck N"]`), whose members are the payloads above.
* A SHA-256 hex digest taken by the group over a value this list already pins
  — the Ed25519 `RawSubjectPublicKeyInfo`, and each DER `get-roots` returns.

**Compared as a substring, because the sentence around it is yours:**

* `aligned_get_entries{`, `logid="1001"`, `aligned="true"` and
  `aligned="false"` in the `/metrics` text. Label **order** is not asserted,
  and neither are the `# HELP` and `# TYPE` lines.
* In a configuration refusal, the message-type name (`LogConfigSet` or
  `LogMultiConfig`) and the path of the file that was read.
* In the compiler's output for a withdrawn symbol: the package path in
  `does not contain package github.com/google/certificate-transparency-go/dnsclient`
  at end of line, `undefined: ctutil.NewLogInfoOverDNS` (or
  `NewLogInfoOverDNSWrapper`, or `LogInfoByKeyHashOverDNS`), `AddJSON`, and
  `FixedBackendResolver`. The rest of the diagnostic is the toolchain's and is
  not read.
* The recovered field values a lax parse yields — a serial, an RSA exponent
  and modulus bit length, a curve name, a subject component, an EKU list, and
  the decimal **count** of a repeated field the parse recovered (`Extensions`,
  `ExtKeyUsage`, `OCSPServer`, `IssuingCertificateURL`) — are compared as
  rendered values joined together, so each is exact but the joining is the
  group's, not yours.
* The `|`-joined sequence of `Subject.CommonName` values along a chain
  `ValidateChain` returns, in the order it returns them, root last. Each name
  comes from the pinned fixture corpus and is exact; the separator and the
  ordering are the group's, not yours.

**Kinds that look like this and are not compared:**

* **Error and refusal bodies over HTTP.** No group at this milestone reads the
  text of a 400, a 404 or a 503. The status is the whole checked fact, together
  with whether the backend was reached.
* **Go `error` strings generally**, except for the two configuration
  identifiers above. Wording, punctuation, capitalisation and whether the
  message wraps another are all yours.
* **Log output.** Nothing written to stderr by a running server is compared,
  and glog's own flags are excluded from §4's flag set for the same reason.
* **JSON key order, whitespace and field order.** Bodies are decoded before
  they are inspected.
* **The inside of a `backend_spec`.** An opaque string; nothing parses it or
  depends on its shape. Where a group checks that a configuration round-tripped
  it is compared as one whole token (above), but no group looks inside one.
* **The protobuf library's own parse diagnostics**, and the `# HELP` text of
  any metric.

### 6.2 What earlier steps listed and this one does not

Every family below appeared in some step behind you and is deliberately not
restated above.

* **The whole error-body family from step 08.** `Internal Server Error\n`, the
  unmasked 500 form, the `bench{1001}: ` per-log diagnostic prefix,
  `Method Not Allowed\nmethod not allowed: <METHOD>\n`,
  `rejecting expired certificate`, and
  `rejecting certificate containing extension %v at index %d`. No group at this
  milestone reads an HTTP error body, so none of it is compared here. It still
  has to exist — `--mask_internal_errors` is one of the 19 flags §4 pins.
* **Flag-declaration lines for flags this release removes.** Step 04 compared
  `  -dns` as a declaration line in `sctscan` and `sctcheck`, together with
  `(default true)` under one and its absence under the other. At v1.1.2
  `-dns` must be **absent** from both usage blocks and rejected with exit 2, so
  that comparison is inverted here and is not carried. The same applies to
  `ct_server`'s `--by_range`.
* **`util.FixedBackendResolver` and the `fixedBackends` watcher.** Anything
  that resolved a backend through `…/trillian/util` is gone with
  `resolver.go` (§5). A group now requires a probe importing
  `FixedBackendResolver` to **fail** to build, which is a §4 build fact, not a
  format crossing the boundary.
* **Step 08's boundary-instant pair, both-knobs `ValidateLogConfig`
  rejection, `reject_extensions`, `frozen_sth`, the `LeafIdentityHash`
  dedup path, and `pkix.Name.String()`'s RFC 2253 rendering.** No group here
  configures or reads any of them. The half-open `not_after_limit` window
  survives, as a verdict, and is restated above.
* **Step 07's SIA emission round-trip, the RFC 3779 DER grammar, the RPKI
  leniency contrast, the three `logid`-labelled gauges, and the mirror
  diagnostic's wording.** The mirror *verdict* is restated above; its text is
  not. Of step 07's `x509ext` surface only the build survives here — §4
  pins the symbols and their signatures, and the STH-in-extension round-trip
  is out of scope.
* **Step 07's fatality rows as step 07 stated them.** Read the inverted form
  above, not step 07's prose: at v1.1.2 the lax retry has landed, so the
  non-minimal INTEGER, the non-`NULL` RSA `AlgorithmIdentifier` parameters and
  the malformed-OID-inside-a-present-extension cases are all **non-fatal**.
* **Step 06's quota user strings and the `get-entry-and-proof` field
  decoding.** `--quota_remote` and `--quota_intermediate` are still in §4's
  flag set and still have to work; no group here reads the charged user, and
  `get-entry-and-proof` is visited only for its `Cache-Control` header.
* **Step 05's CORS headers and its Merkle proof verification through
  `ctutil.LogInfo` and `ctclient consistency`.** CORS is still required by
  HTTP-API.md §7; nothing at this milestone asserts a proof's contents or a
  node hash, so Merkle node and root hashes are not on the compared list —
  the only root hash here is the pinned one in the STH body.
* **Step 04's log-list v1 JSON, `LogInfo.MMD`, `findlog`'s stdout,
  `VerifyInclusion` and its `-1` and cached-STH forms, and the
  `MerkleTreeLeafForEmbeddedSCT` / `MerkleTreeLeafFromChain` routing with its
  `ContainsSCT` pre-check.** What survives from that family is the leaf-hash
  known answers, restated above, including the fact that an embedded-SCT leaf
  hashes to the precert entry.
* **Steps 03 and 04's backend routing and per-log root-pool separation, and
  the distinct-signing-inputs plus STH-signature-cache gate.** No group here
  stands up two *live* backends or two logs with different root pools; the
  configuration-level per-backend tree-id keying survives and is restated.
* **Step 03's `empty backend name` and `duplicate backend name` message
  prefixes, and `Content-Type: application/json`.** Both verdicts are still
  checked; only their wording is dropped, under §3.3.
* **Step 02's `get-sth-consistency` parameter-error bodies, the `x509util`
  renderers, `logid.String()`, `ctclient`'s `sth` output line, the `certcheck`
  usage block and its empty-output assertions.** Those programs still have to
  build and still have to answer `--help` with the pinned first line; nothing
  else about their stdout is compared here.

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
| `inherited-admission-inversion` | gate |
| `inherited-multi-backend-config` | gate |
| `inherited-rfc6962-leaf-hash` | gate |
| `inherited-x509-lax-parsing` | gate |
| `asn1-bmpstring` | requirement |
| `cache-control-immutable` | requirement |
| `config-binary-protobuf-accepted` | requirement |
| `config-eku-list-semantics` | requirement |
| `config-validate-logconfigs` | requirement |
| `ctserver-flag-lifecycle` | requirement |
| `ctutil-verify-sct-with-verifier` | requirement |
| `dnsclient-withdrawn-from-sct-tools` | requirement |
| `eku-filter-admission` | requirement |
| `getentries-alignment-coercion` | requirement |
| `getentries-alignment-disabled` | requirement |
| `getentries-alignment-metric` | requirement |
| `release-surface-withdrawals` | requirement |
| `x509-ed25519` | requirement |
| `x509-go114-merges` | requirement |

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

* **PKCS#11 itself.** No PKCS#11 module is present in the image and no group
  loads one, signs with one, or inspects what `--pkcs11_module_path` does with
  its value. What *is* checked lives in §4 and is only the flag's existence and
  shape: `  -pkcs11_module_path string` must appear in `ct_server --help` and
  the name must be in the nineteen. Behind the flag, anything that compiles
  and does not disturb the other eighteen is acceptable.
* **Anything that needs the network.** The v2 log-list URLs are checked as
  string constants only — nothing fetches one, and no log-list document is
  parsed at this step.
* **Whether your `x509` is a fork or a wrapper.** The Go 1.13/1.14 merge is
  checked through the exported boundary — a signature that verifies, a digest
  that is truncated, a comparison that does not panic — never by reading your
  source (X509-FORK.md §1).
* **The one upstream Go change this release deliberately leaves out.** It is
  described in X509-FORK.md §5.5 because it explains the fork's posture, not
  because a group looks for it.
* **Anything dated after v1.1.2.** `ctclient` still has its flat
  grammar here, and `PEMCertPool` still lives in `trillian/ctfe`; both move
  at the next step.
* **Which Merkle library your proof code calls.** MERKLE.md §4.2 records that
  `merkle/logverifier` is the backend project's entry point in force at
  v1.1.2, and the pinned module set already contains that library and no
  other — so the choice is made by the manifest, which the benchmark owns
  (§2), not by you. No group names it, and RFC 6962 hashing is checked on its
  own terms instead.

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

A flag is born, defaulted true, and deleted — all inside one milestone — and
two more rules move out of chain verification into explicit tests.

`by_range` chose which backend RPC served `get-entries`. During this step it
is defaulted true and then removed outright, because the range-based RPC
becomes the only path. What is checked is the end state: the flag does not
exist and the range-based behaviour is unconditional. In its place
`--align_getentries` arrives, default **true**, trimming an over-wide range
down to a cacheable boundary and counting what it did on a metric — a
coercion, not a refusal, and one that happens before any backend call.

The same "make it explicit" move happens to EKUs. Until v1.1.0 the
configured usage list reached certificates only through path building; now a
leaf whose own extension shares nothing with the list is refused up front, and
an explicit `Any` clears the list rather than joining it.

The rest is withdrawal and absorption. CT-over-DNS goes: the package, the
`LogInfo` constructors and the flags in three programs. `AddJSON` goes. The
gossip tree collapses to the one extension type worth keeping. The
configuration parser learns to accept binary protobuf by falling back to it,
and the whole-set checks a single-backend configuration needs move into a
validator of their own, whose duplicate-tree-id check is **global**: one tree
id per set. The multi-backend validator is unchanged and still keys tree ids
per backend.
And the fork absorbs two Go releases: Ed25519 end to end, DSA digests
truncated to the subgroup order, a nil-safe certificate comparison, and
`BMPString`.
