# TEST_INTERFACE — step 05 of 13 (certificate-transparency-go v1.0.17)

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

Unchanged in number from step 04: **13** `package main` directories
(CLI.md §1).

| program | directory | what a group does with it |
| --- | --- | --- |
| `scanlog` | `scanner/scanlog/` | exercised against the refactored fetcher: a group drives it over fixture data and checks its flag set and exit status |
| `certcheck` | `x509util/certcheck/` | **not** as at step 04: what `--validate` validates changes at v1.0.17 and a group checks it. When *neither* `--root` nor `--intermediate` is given, a file holding several certificates is read as a **chain**, leaf first — a trailing self-signed certificate is adopted as that file's trust anchor, every certificate between it and the leaf becomes an intermediate, and only the leaf is verified. Supplying either flag restores the explicit-pool behaviour and no anchor is adopted. Checked on **exit status alone** (CLI.md §4.3, "What `--validate` validates changes at v1.0.17") |
| `ct_server` | `trillian/ctfe/ct_server/` | **started by groups at this step** — step 04 only required it to build; here several groups run it as a subprocess against a config they write and a backend stub they stand up. Its command-line shape is that of step 04 plus the three tracing flags of CLI.md §2.1; its **serving** behaviour is checked, including the permissive **CORS wrapper** now in front of the whole mux and the narrowing of the catch-all to the exact path `/` (HTTP-API.md §7) — see section 6 |
| `ctclient`, `sctcheck`, `sctscan`, `findlog`, `crlcheck`, `preloader`, `dumpscts`, `chainfix`, `ct_hammer`, `gossip_server` | see CLI.md §1 | as at step 04 |

Four rules hold for every program and are checked wherever a group runs one
(CLI.md, the preamble on flag grammar):

* an unrecognised flag makes the standard `flag` package print `Usage of
  <prog>:` and the flag list **to stderr** and exit **2**;
* `--help` (and `-h`) is the one case the `flag` package treats differently: the
  same usage block goes to stderr and the program exits **0**, because the
  package returns `flag.ErrHelp` and `flag.CommandLine` in its default
  `ExitOnError` mode turns that into 0. That holds at v1.0.17 as it did at
  step 04, and it is how a group reads a program's flag set at all — the same
  text comes back with either status, and the status is what says whether the
  flag was understood. `chainfix` is the documented exception: it reads
  `os.Args` before `flag` runs, so it reaches neither path and exits **2**
  (CLI.md §4.6, and the preamble paragraph above it);
* a missing *required* flag is a bare diagnostic on stderr and exit **1**, with
  no usage dump;
* `glog.Exit*` and `log.Fatal*` exit **1**; `glog.Fatal*` exits **255**.

## 5. Packages and symbols the verifier links against

| import path | what the drivers use |
| --- | --- |
| `…/merkletree` | **nothing — this package must not exist at this milestone.** See section 10 |
| `github.com/google/certificate-transparency-go` | additionally `LeafHashForLeaf`, which arrives at v1.0.14 and is required from here on, and `CreateX509MerkleTreeLeaf` (CLIENT-API.md §3) |
| `…/tls` | the RFC 5246 presentation codec, **unchanged at v1.0.17** and the most widely linked sub-package at this step: six of the eight gate groups import it (`inherited-tls-codec`, `inherited-rfc6962-leaf-hash`, `inherited-inclusion-proof-verification`, `inherited-consistency-proof-verification`, `inherited-ctfe-config-acceptance` and `gate-endtoend`), so a tree in which this path does not resolve is not satisfied however complete the new work is, and six requirement groups link it besides. It is also the **control** of the `merkletree` removal probe below — that group compiles `tls.Marshal(uint16(0x0102))` first and abandons the whole group if it does not build, so `…/tls` breaking fails the removal requirement as well as the gate. What the drivers use: `Marshal(val interface{}) ([]byte, error)`; `Unmarshal(b []byte, val interface{}) ([]byte, error)`, whose **first** result is the unconsumed remainder and whose second is the error — a driver reads both and treats a non-empty remainder after a self-contained structure as a failure (TLS-CODEC.md §1); `VerifySignature(pubKey crypto.PublicKey, data []byte, sig DigitallySigned) error`; and the `DigitallySigned` type, reached two ways that must both keep working — by **direct Go conversion** from the root package's value, `tls.DigitallySigned(sth.TreeHeadSignature)`, and by `tls.Unmarshal` decoding an `add-chain` signature straight **into** a `ct.DigitallySigned`. The two types therefore stay convertible and the root one keeps the codec's tags. Drivers also declare **their own** structs against the tag grammar of TLS-CODEC.md §3 — `tls:"minlen:0,maxlen:65535"` and `tls:"minlen:0,maxlen:255"` on a `[]byte` — and compare the bytes, so §3's rule that the prefix width comes from the declared maximum and not from the value is executed here, not merely read; section 6 restates the encodings that pins. `MarshalWithParams`, `UnmarshalWithParams`, `Uint24`, `Enum`, `CreateSignature`, `SignatureAlgorithmFromPubKey`, `HashAlgorithm` and `SignatureAlgorithm` keep the shapes they had at step 04; no driver names them directly here, and the build gate still compiles them |
| `…/scanner` | `Fetcher`, `FetcherOptions`, `DefaultFetcherOptions`, `EntryBatch`, `NewFetcher`, `Prepare`, `Run`, `DefaultScannerOptions` and `ScannerOptions`, with these exact field names: **`FetcherOptions`** — `BatchSize int`, `ParallelFetch int`, `StartIndex int64`, `EndIndex int64`, `Quiet bool`; **`EntryBatch`** — `Start int64`, `Entries []ct.LeafEntry`; **`ScannerOptions`** — the **embedded** `FetcherOptions` (so `BatchSize`, `ParallelFetch`, `StartIndex`, `EndIndex` and `Quiet` are reached both by promotion and through `.FetcherOptions`) plus `Matcher`, `PrecertOnly bool`, `NumWorkers int`, `BufferSize int`. `DefaultFetcherOptions()` returns `BatchSize` **1000**, `ParallelFetch` **1**, `StartIndex` **0**, `EndIndex` **0**, `Quiet` **false**; `DefaultScannerOptions()` embeds those and sets `NumWorkers` **1**, `PrecertOnly` **false**. `Prepare` **mutates the caller's own options struct**, assigning `opts.EndIndex = int64(sth.TreeSize)` whenever `EndIndex` is 0 or larger than the tree; the range `[StartIndex, EndIndex)` is half-open. Defaults, signatures and the clamping contract in full: CLIENT-API.md §5.1 |
| `…/jsonclient` | additionally `JSONClient.BaseURI()` |
| `…/client` | `CheckLogClient` — **four** methods at this release, `BaseURI()` first (CLIENT-API.md §1.3). This **supersedes** step 04's "three-method interface, without `BaseURI`": a three-method implementation no longer satisfies it, and a driver here supplies all four. Also `LogClient.BaseURI()` |
| `…/dnsclient` | additionally `DNSClient.BaseURI()`, which returns `dns:` followed by the base **as the constructor stored it**, not as it was passed in. `New` absolutises the name, appending a trailing dot when there is none, so `New("ct.bench.invalid", …)` gives `BaseURI() == "dns:ct.bench.invalid."` — dot included. A driver compares that exact string (CLIENT-API.md §6.1: the signature block, and the paragraph under it that has `New` append the trailing dot) |
| `…/ctutil` | unchanged in signature — but now implemented on top of the external proof library. `LogInfo` is built by a driver from its exported fields (`Description`, `Client`, `MMD`, `Verifier`, `PublicKey`) and driven through `VerifyInclusion`, `VerifyInclusionLatest`, `VerifyInclusionAt`, `LastSTH` and `SetSTH` |
| `…/x509` | additionally the `VerifyOptions` switches `DisableCriticalExtensionChecks`, `DisableEKUChecks`, `DisableTimeChecks`, `DisablePathLenChecks`, `DisableNameConstraintChecks`, `DisableNameChecks`, and the leaf-EKU matching rule that changes alongside them (X509-FORK.md §5.6). Also `ParseCertificate` with the **SAN relaxation this release makes**, and the `Certificate.DNSNames` / `Certificate.EmailAddresses` it fills: from v1.0.17 a `dnsName` that is not a parsable domain and an `rfc822Name` that is not a parsable mailbox are **no longer fatal** — the parse succeeds and the raw string is stored verbatim, where step 04 returned a nil certificate and an error (**X509-FORK.md §3.1, "The SAN relaxation at v1.0.17"**). Of the two readings §3.1's vocabulary allows, the checked one is the stronger: the malformed name is **not** demoted to a `NonFatalErrors` entry, it is not reported at all, and `ParseCertificate` returns a **nil** error — a driver compares `err` against nil, so a tree that records a non-fatal error here fails the case. The syntax check has not been dropped, it has **moved**: see section 10 |
| `…/trillian/ctfe` | `GetCTLogID` as a control; **not** `LogEntry` and **not** `NewLogEntry`, both withdrawn at this release (CLIENT-API.md §8.3) |
| `github.com/google/trillian`, `google.golang.org/grpc` | **not your deliverable and out of scope as such** (section 3.4) — named here because a driver at this step links them into the same binary as your code, and section 5 is the complete list of what the verifier links. Four groups (`gate-endtoend`, `ctfe-cors-headers`, `ctfe-eku-checks-disabled`, `inherited-ctfe-config-acceptance`) build their backend stub from `trillian.RegisterTrillianLogServer`, the `TrillianLogServer` request and response messages (`QueueLeaf*`, `QueueLeaves*`, `AddSequencedLeaf*`/`AddSequencedLeaves*`, `GetLatestSignedLogRoot*`, `GetLeavesByIndex*`, `GetLeavesByRange*`, `GetLeavesByHash*`, `GetInclusionProof*`, `GetInclusionProofByHash*`, `GetConsistencyProof*`, `GetEntryAndProof*`, `GetSequencedLeafCount*`, `InitLog*`), `trillian.LogLeaf`, `trillian.QueuedLogLeaf`, `trillian.Proof` and `trillian.SignedLogRoot`, and serve it over `grpc.NewServer` on a loopback socket; a fifth, `inherited-multi-backend-serving`, links `google.golang.org/grpc` alone to open two listeners that answer nothing. Both modules come from the benchmark-owned manifest at the versions section 2 pins — `github.com/google/trillian v1.2.0`, `google.golang.org/grpc v1.20.1` — so what you leave in `go.mod` cannot change them, and you implement no part of either. What **is** checked is only what your front end does against that backend: the calls it makes, with what arguments, and what it turns the answers into |

Every import path step 04 named still resolves and is still linked against,
with one exception: `…/merkletree`, whose row above says it must not exist
at this milestone. On a path whose row above says *additionally*, that row
lists only what is new or changed at v1.0.17; every other symbol step 04
named on that path keeps the shape step 04 gave it, except the two the table
withdraws by name (`ctfe.LogEntry` and `ctfe.NewLogEntry`). On a path the table
above does not name at all, nothing changed at v1.0.17.

A removal is checked by compilation, never by `ls`. A group builds two probes
inside its own module: a **control** that imports a package this milestone
still has, which must build, and a **probe** that imports the removed path,
which must fail *naming that path*. Leaving the package in place fails the
probe; deleting unrelated code fails the control.

## 6. Formats crossing the boundary

Step 04's `TEST_INTERFACE.md` is replaced by this file, so the surface it
listed is repeated here rather than referred to. You do not have step 04's
file, and nothing below may be read as "look it up one step back". Nor is this
step 04's list copied forward: v1.0.17 withdraws a whole package and adds a
new entry point to the leaf hash, so some of what step 04 listed is no longer
true here and some has changed shape. Section 6.2 names every family that fell
away and says why.

**Carried forward from the steps behind you, restated.** Each of these was
checked against a group at *this* step before it was written down.

* **TLS wire encodings** (TLS-CODEC.md §2–§5). A variable-length vector carries
  a big-endian length prefix whose width comes from the declared maximum and
  not from the value, so four bytes under `maxlen:65535` encode as
  `0004deadbeef` and the same four under `maxlen:255` as `04deadbeef`. `uint64`
  is big-endian (`0102030405060708`). A buffer that ends inside a declared
  length is an **error**, never a short read. A `MerkleTreeLeaf` begins with
  version `0x00` and leaf type `0x00`, and survives a marshal/unmarshal round
  trip unchanged. This is gate material: it has to hold against the previous
  milestone's tree too.
* **The JSON bodies the log speaks** (HTTP-API.md §2–§4), in both directions
  over a real loopback socket. `get-sth` answers `tree_size`, `timestamp`,
  `sha256_root_hash`, `tree_head_signature`; `get-roots` answers
  `certificates`; `add-chain` takes `chain` and answers `sct_version`, `id`,
  `timestamp`, `extensions`, `signature`; `get-sth-consistency` answers
  `consistency`; `get-entries` answers `entries[]` with `leaf_input` and
  `extra_data`. Field names are exact and byte-valued fields are standard
  base64. The product is on both ends of that contract at this step: `ct_server`
  serves those bodies, and `ctclient` and `scanner` parse them back out of a
  log a driver serves.
* **Leaf hashes and Merkle hashes** (MERKLE.md §1). A leaf hash is SHA-256 over
  `0x00` followed by the serialised `MerkleTreeLeaf`; an internal node is
  SHA-256 over `0x01` followed by its two child hashes. The two pinned digests,
  one for an X.509 entry and one for a precert entry, are unchanged from the
  steps behind you and are still compared as hex. What has changed is the way
  in. From v1.0.14 the root package exports `ct.LeafHashForLeaf` and the prefix
  constant `ct.TreeLeafPrefix` (section 5), and a requirement group here requires
  that route to exist and to agree — digest for digest — with the hash computed
  independently from `tls.Marshal` and `crypto/sha256`, and with what
  `ctutil.LeafHash` returns for the same chain and SCT. A gate group still
  computes the same two digests the independent way only, because a gate driver
  has to compile against the previous milestone's tree, where
  `LeafHashForLeaf` does not yet exist. Both must produce the same bytes.
* **PEM, and the two private-key forms.** A root pool is a file of PEM
  `CERTIFICATE` blocks named by `roots_pem_file`, and `get-roots` republishes
  exactly that set as base64 DER. Both key forms CONFIG.md §2.3 allows are live
  at this step and both must work, because different groups here use different
  ones: a `keyspb.PEMKeyFile` carrying a `path` and a `password`, naming a
  **passphrase-encrypted** `EC PRIVATE KEY` file; and a `keyspb.PrivateKey`
  carrying the raw DER of an unencrypted key inline in the configuration text.
  Neither handler may be the only one that works.
* **The configuration file's encoding.** `--log_config` is a **text** proto at
  this release and nothing else (CONFIG.md §4.2); the binary-first retry is a
  later milestone. It reaches the code only by being handed to `ct_server` on
  its command line — no group at this step calls a configuration loader in
  process.
* **The two configuration message shapes, and which one `--log_rpc_server`
  selects** (CLI.md §2.2, CONFIG.md §3). With `--log_rpc_server` naming a
  backend address, `--log_config` is read as a **`LogConfigSet`**: a bare
  sequence of `config { … }` messages. With `--log_rpc_server` given empty, the
  same file is read as a **`LogMultiConfig`**: `backends { backend { … } }`
  together with `log_configs { config { … } }`, each `config` naming its
  `log_backend_name`. Groups here use both shapes, so both parses have to be
  right.
* **Per-log separation.** One `ct_server` carrying two log configurations
  answers each prefix under its own path, and the root set a prefix publishes
  is exactly the pool *its own* `config` named — compared as a sorted set of
  DER blobs, so serving the union of the two pools fails as surely as serving
  the wrong one.
* **Where a tree head's three fields come from.** `tree_size`, `timestamp` and
  `sha256_root_hash` are the backend's values echoed unchanged; the front end
  neither recomputes nor rounds them. `tree_head_signature` is a
  `digitally-signed` structure over the RFC 6962 `TreeHeadSignature` — version,
  signature type, timestamp, tree size, root hash, in that order — and it
  verifies under the public key the configuration named. This is observed over
  a socket against your own `ct_server` binary, not through any in-process
  set-up call.
* **No two distinct signing inputs share a signature.** An SCT is bound to the
  chain it was issued over: a second submission of a *different* chain gets an
  SCT that verifies over that chain, and the first chain's SCT does not verify
  over it.

And, new at v1.0.17:

Proof verification still consumes and produces exactly the bytes MERKLE.md
§1–§3 specify — the library underneath changed, the wire did not, and that is
what is checked here.

Concretely, at this milestone:

* **Inclusion** is checked through `ctutil.LogInfo`. A driver builds a
  `LogInfo` over a `client.CheckLogClient` of its own that answers `get-sth`
  and `get-proof-by-hash` from a tree the driver built to MERKLE.md §1, and
  drives `VerifyInclusionAt`, `VerifyInclusion` and `VerifyInclusionLatest`
  over every index of a tree whose size is not a power of two. A verified
  proof returns the leaf index; a wrong root, a tampered path and a truncated
  path each return **−1** and an error, never 0 and never a silent success.
  The hash the driver is asked for must be the RFC 6962 leaf hash of the leaf
  it was given.
* **Consistency** is checked through `ctclient consistency` against a log the
  driver serves, using `--prev_size`/`--size` and the `--prev_hash`/
  `--tree_hash` pair, which is the only path to consistency verification the
  product exposes at this release (CLI.md §3.1). A correct proof exits 0; a
  wrong old root, a wrong new root, a tampered element and `prev_size > size`
  each exit 1. The `n₁ == 0` case of MERKLE.md §3 is **not** checked — see
  section 9.
* Exact numeric inputs and expected roots are the driver's own, computed from
  the rules of MERKLE.md §1, not read from a fixture.

One HTTP-level format is new at v1.0.17 and a group checks it over a real
loopback socket against your own `ct_server`. From this release the **whole
mux is wrapped in a permissive CORS handler** — a CT log is public and
unauthenticated, so the policy allows everything — and the headers below are
what a browser acts on. HTTP-API.md §7 states the contract; these are the
parts that are asserted:

* a request carrying an `Origin` header is answered with
  `Access-Control-Allow-Origin: *`, and **the response body is unchanged**:
  CORS adds headers, it never rewrites an answer;
* a pre-flight `OPTIONS` carrying `Origin` and `Access-Control-Request-Method`
  is answered **by the CORS layer itself** — the RFC 6962 handler is not
  reached — with status **200**, `Access-Control-Allow-Origin: *` and
  `Access-Control-Allow-Methods` echoing the requested method;
* that pre-flight response carries `Vary` as **three separate header values**,
  in this order: `Origin`, `Access-Control-Request-Method`,
  `Access-Control-Request-Headers`;
* the catch-all is inside the wrapper too, so `/` carries the same headers.
  The catch-all also narrows at v1.0.17: only the exact path `/` answers
  **200** and any other path no handler claims answers **404**, where step 04
  answered 200 for every unmatched path (HTTP-API.md §7).

### 6.1 The strings this step compares whole

Everything on this list is compared **whole**, after the group's own
normalisation, and nothing else at this step is.

1. The base URIs the clients report (CLIENT-API.md §1 and §6):
   `http://ct.bench.invalid/log` from a `jsonclient` built on that URL, and
   again from one built on the same URL with trailing slashes, which `New`
   strips — **all** of them, not one; `http://ct.bench.invalid/pilot` from a
   `client.LogClient`, and again read through a `client.CheckLogClient`
   interface value, on which `BaseURI` is now the **first** method; and
   `dns:ct.bench.invalid.` from a `dnsclient` — the `dns:` scheme and the
   trailing dot are part of the string.
2. The CORS header values: `*` as `Access-Control-Allow-Origin` on a simple
   cross-origin `GET`, on `/`, and on a path that 404s; `POST` as
   `Access-Control-Allow-Methods` on the pre-flight, echoing the method that
   was asked for; and `Vary` as the three ordered values `Origin`,
   `Access-Control-Request-Method`, `Access-Control-Request-Headers`, compared
   as one joined string so that the order is checked and not just the set.
3. `scanlog`'s dump filenames. A dumped leaf is written as
   `cert-%014d-leaf.der`: the literal `cert-`, the entry index zero-padded to
   fourteen digits, then `-leaf.der`. The whole directory listing is compared
   as one space-joined sorted string, so a spurious extra file fails exactly as
   a missing or misspelt one does.
4. The type tokens the `flag` package prints on `ct_server`'s usage lines for
   the three tracing flags, taken as the remainder of the declaration line
   after the flag's own name: **empty** for `--tracing`, because a bool prints
   bare; `string` for `--tracing_project_id`; `int` for `--tracing_percent`.
5. The subject-alternative-name strings a parsed certificate carries: the
   dNSName `bench.invalid.` and the rfc822Name `no-at-sign` come back out of
   `ParseCertificate` **verbatim**, trailing dot and missing `@` and all; and a
   well-formed control certificate's dNSName is still `valid.bench.invalid`.

Four kinds of comparison look like the list above and are not:

* **`Usage of `.** The build gate asks only whether a command's usage text
  *contains* that fragment and *contains* the command's own name. The whole
  line is not compared at this step, and neither is any flag's declaration line
  outside item 4 above.
* **The toolchain's own build errors.** Two groups require a probe module to
  fail to compile and then look for the spelling of what was withdrawn in the
  compiler's output —
  `github.com/google/certificate-transparency-go/merkletree`, `ctfe.LogEntry`,
  `ctfe.NewLogEntry`. Only that the spelling appears is checked. The wording,
  the source position, and whether the failure lands at the import or at the
  use are all free.
* **Hex and base64 renderings of byte slices.** `0004deadbeef`, the two pinned
  leaf hashes, the DER of a root pool, a dumped leaf file: those compare
  **bytes**. The encoding is only how a failure gets printed.
* **Tuples a driver formatted for itself.** `size=… ts=… root=…`,
  `batch=1000 parallel=1 start=0 end=0 quiet=false`, `indices=[…] starts=[…]`,
  `[[2 4] [5 6]]`, `status=200 acao="*"` — the format string belongs to the
  driver. What is checked is the numbers and the header values inside it, not
  the shape they were printed in.

And nowhere at this step is the *text* of an error read. Where a group requires
a failure it checks a returned `-1`, a non-nil error value, an HTTP status, or
a process exit status — never a message. Section 9 says the same thing from the
other side.

### 6.2 What earlier steps listed and this one does not

* **Everything that lived in `…/merkletree`.** The steps behind you listed the
  in-tree Merkle library as part of the surface. At v1.0.17 that package is
  gone — section 5 says it must not exist, and section 10 explains how its
  absence is checked — because proof verification is outsourced. None of its
  types, constructors or error values is contract here; `RootMismatchError` in
  particular is no longer the product's to define. What *is* still checked is
  the wire: the proof bytes, and the verdicts the bullets above pin on them.
* **The embedded-SCT leaf-hash family.** Step 04 listed the rules for hashing a
  leaf whose SCT is embedded in the certificate — the `embedded=true` path, the
  SCT-list extension being stripped before hashing,
  `MerkleTreeLeafForEmbeddedSCT`, `ContainsSCT`, the refusals for an empty
  chain and for a nil SCT, and the `IsPrecertificate` dispatch. No group here
  reaches any of it: every `ctutil` call at this step passes `embedded=false`.
  Those functions must still exist for your tree to build; they are simply not
  compared.
* **The log-list surface.** Step 04 checked `LeafHashB64`, `LogInfoByKeyHash`,
  the rule that a failed lookup carries no partial answer, the log-list JSON
  schema v1, `LogInfo.MMD`, and the lines `findlog` prints on stdout. Nothing
  at this step reads any of it. `findlog` is still required to exist and to
  answer `--help`, but that is the build gate's business, not section 6's.
* **The whole-line usage strings.** Step 04 compared `Usage of <path>:` as a
  complete line, and the complete declaration lines of `-inclusion`, `-dns`,
  `-log_list string`, `-log_uri string` and `-deadline duration` together with
  their `(default true)` and `(default 30s)` suffixes. None of that is compared
  here; 6.1 lists what replaced it.
* **`x509util`'s text rendering and the configuration validator's
  diagnostics.** Step 04 listed `CertificateToString` output and two validation
  messages. No group at this step reads either — and see the previous section:
  no error text is compared anywhere here.
* **The STH signature cache.** Step 03 required that signing an unchanged tree
  head return the **byte-identical** signature, and that moving the head by an
  entry, a bit or a millisecond produce a different one. Only the SCT half of
  that milestone survives here, as the last carried-forward bullet above. No
  group at this step asserts the STH cache.
* **`Content-Type: application/json` as a header that is read.** Step 03
  compared it on a response. Drivers here set it on their own requests and
  serve it from their own fake logs, but no group asserts it on anything the
  product produced.

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
| `inherited-consistency-proof-verification` | gate |
| `inherited-ctfe-config-acceptance` | gate |
| `inherited-inclusion-proof-verification` | gate |
| `inherited-multi-backend-serving` | gate |
| `inherited-rfc6962-leaf-hash` | gate |
| `inherited-tls-codec` | gate |
| `certcheck-chain-from-file` | requirement |
| `client-base-uri` | requirement |
| `ctfe-cors-headers` | requirement |
| `ctfe-eku-checks-disabled` | requirement |
| `ctfe-logentry-type-withdrawn` | requirement |
| `ctserver-tracing-flags` | requirement |
| `leafhash-for-leaf-entrypoint` | requirement |
| `merkletree-package-withdrawn` | requirement |
| `scanlog-end-index` | requirement |
| `scanner-fetcher-api` | requirement |
| `x509-leaf-eku-strictness` | requirement |
| `x509-san-parse-relaxed` | requirement |
| `x509-verify-disable-switches` | requirement |

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

* **Which external Merkle library you call, or how.** What is checked is that
  the product's own `merkletree` package is gone and that proof verification
  still produces the right answers on the vectors of section 6. No group reads
  your import list or names a library symbol. The import path in force at this
  release is named in MERKLE.md §4.2 and is present in the pinned module set;
  nothing else is.
* **The identity of any error type or the wording of any message.** With
  `merkletree/` deleted, `RootMismatchError` no longer belongs to the product
  (MERKLE.md §2). Nothing here checks an error's type, its fields or its text —
  only that a bad proof is refused and a good one is accepted. The same goes
  for every diagnostic a command prints: `certcheck`, `scanlog` and `ctclient`
  are checked on exit status and on the files and bytes they produce.
* **`ctclient consistency` with `--prev_size` of 0 or less**, and therefore the
  `n₁ == 0` special case of MERKLE.md §3. The command rejects the argument
  before any proof is fetched (CLI.md §3.1), so the case has no reachable
  behaviour at the product's public surface and is out of scope at this or any
  later step.
* **`scanlog --dump_dir` output for precertificate entries.** Only the X.509
  leaf file of CLI.md §4.1 is checked.
* **The SQLite backend that RELEASE-MAP.md §5 lists as removed at
  v1.0.17.** The removal is the *backend project's*, not this product's:
  in the tree you inherit nothing about SQLite changes at v1.0.17, and
  `gossip_server` — which is built on it, and which section 4 still counts
  among the thirteen — must go on building. Nothing about SQLite is checked,
  before or after; deleting it to satisfy the release note fails the
  command-set group instead.
* **The gRPC → HTTP status map of HTTP-API.md §6.1.** Nothing about it moves
  at v1.0.17: the table is identical from v1.0 to v1.1.4, and the two cells
  that ever change (`Canceled`/`DeadlineExceeded` and `ResourceExhausted`)
  change at v1.2.0. Observing it needs a backend that returns a chosen gRPC
  status on demand, which is a property of the backend and not of this
  milestone, so **no group at this step asserts a status code produced by that
  map** — not as a new capability and not as an inherited one. Your front end
  must still map backend errors; it is simply not what is measured here.
* **Anything dated after v1.0.17.**

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

This is a **subtractive** milestone, and the first of several. The product had
its own Merkle proof verifier; at v1.0.17 it deletes it and takes proof
verification from the backend project's library instead. Every internal
consumer — `ctclient`, `ctutil`'s `LogInfo`, the integration tooling — is
rewritten to call the external verifier.

Deleting a capability is a milestone in exactly the way adding one is, and it
is checked the same way: by what is no longer reachable. A tree that still
contains a `merkletree` package has not performed this milestone, however
correct that package is. Move the consumers first, then delete, then check that
the build is clean.

The two additions that ride along with the deletion are small and easy to
miss: `LeafHashForLeaf` in the root package, and `scanner/fetcher.go`, which
factors the "walk the log in batches" loop out of the scanner so other tools
can reuse it.

v1.0.17 also carries a handful of smaller changes that have nothing to do
with proofs. **Each one is checked by its own group**, and each is easy to miss
precisely because the headline of the milestone is a deletion. In the order
you would most easily overlook them:

* the SAN syntax check leaves the parser — a `dnsName` that is not a parsable
  domain or an `rfc822Name` that is not a parsable mailbox is stored verbatim
  instead of failing the parse, and no error of any kind is returned. The check
  is not gone, it **moves** into `Verify`: the name-constraint walk parses each
  name where it compares it, and **fails the chain** when it cannot. Both
  halves are checked — the relaxed parse, and the verification that refuses what
  the parser now admits (**X509-FORK.md §3.1**);
* `ct_server`'s whole mux goes behind a permissive CORS handler, and its
  catch-all narrows to the exact path `/` (**HTTP-API.md §7**);
* `certcheck --validate` reads a multi-certificate file as a **chain** when
  neither `--root` nor `--intermediate` is given (**CLI.md §4.3**);
* the `x509.VerifyOptions` switch set is completed, and `trillian/ctfe` stops
  hand-editing certificates before `Verify` and sets those switches instead
  (**X509-FORK.md §5.6**, HTTP-API.md §5.1);
* leaf-EKU matching tightens in the same release: `ServerAuth` on the leaf no
  longer satisfies a request for `ClientAuth` or `OCSPSigning`, while an
  EKU-free leaf is still accepted for anything (**X509-FORK.md §5.6**);
* `ct_server` gains `--tracing`, `--tracing_project_id` and
  `--tracing_percent` (**CLI.md §2.1**); `scanlog` gains `--end_index`
  (**CLI.md §4.1**);
* `ctfe.LogEntry` and `ctfe.NewLogEntry` are withdrawn — naming either one no
  longer compiles (**CLIENT-API.md §8.3**);
* `BaseURI()` appears on `jsonclient.JSONClient`, `client.LogClient` and
  `dnsclient.DNSClient`, and becomes the **first** method of
  `client.CheckLogClient` (**CLIENT-API.md §1.1, §1.3, §6.1**).
