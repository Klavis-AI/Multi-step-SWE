# TEST_INTERFACE — step 06 of 13 (certificate-transparency-go v1.0.19)

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

v1.0.19 adds `migrillian`, so the tree holds **14** `package main`
directories (CLI.md §1).

| program | directory | what a group does with it |
| --- | --- | --- |
| `ct_server` | `trillian/ctfe/ct_server/` | additionally started with `--quota_remote` and `--quota_intermediate`; checked on accepting both, on their defaults, and on start-up with them set |
| `migrillian` | `trillian/migrillian/` | checked on its **flag set** — all fourteen flags of CLI.md §4.7 with their types and defaults, and nothing else declared — and on its exit status. It takes no configuration file at v1.0.19 and nothing checks one |
| `scanlog` | `scanner/scanlog/` | checked on its flag set at **this** release: `quiet` is gone with `FetcherOptions.Quiet` and the `log_uri` default has moved to `https://` (CLI.md §4.1) |
| `ctclient` | `client/ctclient/` | checked on its `log_uri` default, which moves to `https://` at this release (CLI.md §3.1) |
| the other ten | see CLI.md §1 | they keep the command-line shapes they had at v1.0.17, with two exceptions v1.0.19 makes that nothing here checks: `preloader` also loses `quiet`, moves its log URI defaults to `https://` and switches to `glog.Exit*` (CLI.md §4.2), and `sctscan` also loses `quiet` (CLI.md §4.4). All ten must still build, and the nine of them that declare flags must still print usage; `chainfix` declares none and prints none, because it reads its positional arguments before `flag.Parse` (CLI.md §4.6, and the preamble's note on that path). Nothing else about them is checked here |

Four rules hold for every program and are checked wherever a group runs one
(CLI.md, the preamble on flag grammar and its exit-status table):

* an unrecognised flag makes the standard `flag` package print `Usage of
  <prog>:` and the flag list **to stderr** and exit **2**;
* `--help` (and `-h`) is the one case the `flag` package treats differently:
  the same usage block goes to stderr and the program exits **0**, because the
  package returns `flag.ErrHelp` and `flag.CommandLine` in its default
  `ExitOnError` mode turns that into 0. That holds at v1.0.19 as it did at
  v1.0.17, and it is how a group reads a program's flag set at all —
  `migrillian`'s fourteen flags are read out of exactly this output. The same
  text comes back with either status, and the status is what says whether the
  flag was understood. `chainfix` is the documented exception: it reads
  `os.Args` before `flag` runs, so it reaches neither path and exits **2**
  (CLI.md §4.6, and the preamble paragraph above it);
* a missing *required* flag is a bare diagnostic on stderr and exit **1**, with
  no usage dump;
* `glog.Exit*` and `log.Fatal*` exit **1**; `glog.Fatal*` exits **255**.

## 5. Packages and symbols the verifier links against

Step 05's `TEST_INTERFACE.md` is replaced by this file, so the surface it
listed is repeated here rather than referred to. Drivers at this step link
against exactly these packages of your tree, and against nothing else in it:

| import path | what the drivers use |
| --- | --- |
| `…` (root, imported as `ct`) | the RFC 6962 structures and path constants — `MerkleTreeLeaf`, `TimestampedEntry`, `TimestampedEntryLeafType`, `X509LogEntryType`, `PrecertLogEntryType`, `ASN1Cert`, `CertificateChain`, `PrecertChainEntry`, `V1`, `LogID`, `CTExtensions`, `DigitallySigned`, `SignedCertificateTimestamp`, `SignedTreeHead`, `TreeHeadSignature`, `TreeHashSignatureType`, `TreeLeafPrefix`, `AddChainRequest`, `AddChainResponse`, `GetSTHResponse`, `GetEntriesResponse`, `GetEntryAndProofResponse`, `LeafEntry`, `GetSTHPath`, `GetEntriesPath`, `GetEntryAndProofPath`, `LeafHashForLeaf`, `CreateX509MerkleTreeLeaf`, `MerkleTreeLeafFromChain`. The path constants carry the leading `/` and the `ct/v1/` segment, exactly as HTTP-API.md §1 spells the endpoints |
| `…/tls` | the codec of TLS-CODEC.md — `Marshal`, `Unmarshal`, `DigitallySigned`, `SignatureAndHashAlgorithm`, `SHA256`, `ECDSA`, `VerifySignature` |
| `…/x509` | the fork — `ParseCertificate`, `ParsePKIXPublicKey`, `Certificate` (its `Subject` and its `RawSubjectPublicKeyInfo`), and the `NonFatalErrors` predicate of X509-FORK.md §5.2 |
| `…/client`, `…/jsonclient` | `client.New`, `client.LogClient`, `LogClient.GetSTH`, `jsonclient.Options` |
| `…/ctutil` | `VerifySCT`, and only that one name — see §6.2 for the rest of the package |
| `…/scanner` | the fetcher of CLIENT-API.md §5.1 — `Fetcher`, `FetcherOptions`, `DefaultFetcherOptions`, `NewFetcher`, `Prepare`, `Run`, `Stop`, `EntryBatch` |
| `…/trillian/ctfe`, `…/trillian/ctfe/configpb` | `LogConfigFromFile`, `SetUpInstance`, `InstanceOptions`, `PathHandlers`, `GetTreeHead`, `RequestLog`, `configpb.LogConfig`, and the new surface in the table below |
| `…/trillian/util` | `BuildLogLeaf` and `ExtraDataForChain`, below. The import path is the fork's own — `github.com/google/certificate-transparency-go/trillian/util` — not Trillian's |
| `…/merkletree` | **must not resolve.** The package was withdrawn at v1.0.17 and a gate group proves it by compilation: a probe importing it must fail to build and the build error must name the import path, beside a control probe on a surviving package that must build and a probe on the backend library's `github.com/google/trillian/merkle` and `…/merkle/rfc6962` that must also build |

Beside your tree the drivers link four things from outside it, so they have to
keep resolving in the module graph you ship: `github.com/google/trillian`
(`TrillianLogClient`, `SignedLogRoot`, `LogLeaf`, `ChargeTo`, the request and
response messages, `RegisterTrillianLogServer`),
`github.com/google/trillian/monitoring` (`InertMetricFactory`, what a driver
puts in `InstanceOptions.MetricFactory`), `google.golang.org/grpc`, and the
blank import `github.com/google/trillian/crypto/keys/der/proto`, which
registers the `keyspb.PrivateKey` handler exactly as `ct_server`'s own `main`
does.

This milestone **adds** eight things to that surface, and each one is checked.
The authority for each is CLIENT-API.md; this table is the boundary the
verifier links against.

| symbol | package | shape at v1.0.19 |
| --- | --- | --- |
| `LogClient.GetEntryAndProof` | `…/client` | CLIENT-API.md §1.2 |
| `(*GetSTHResponse).ToSignedTreeHead()` | root | a method on the response type, usable with no `LogClient`; CLIENT-API.md §3 |
| `GetTreeHead` | `…/trillian/ctfe` | gains a **fifth** parameter `remoteQuota *string`; CLIENT-API.md §8.3 |
| `QuotaUserForCert`, `CertificateQuotaUserPrefix` | `…/trillian/ctfe` | CLIENT-API.md §8.6 |
| `InstanceOptions.RemoteQuotaUser`, `InstanceOptions.CertificateQuotaUser` | `…/trillian/ctfe` | two new fields beside `Deadline`, `MetricFactory` and `RequestLog`; CLIENT-API.md §8.2 |
| `RequestLog` | `…/trillian/ctfe` | an **eleven**-method interface here; drivers implement it whole, so the added `IssueSCT` is a compile-time break; CLIENT-API.md §8.5 |
| `BuildLogLeaf`, `ExtraDataForChain` | `…/trillian/util` | added to a package that already exists; CLIENT-API.md §8.7 |
| `FetcherOptions.Continuous` | `…/scanner` | a one-for-one swap: `Quiet` is **gone** and a driver that sets it will not compile; CLIENT-API.md §5.1 |

Nine groups build a whole personality, so the construction path they use is
part of the checked surface too, and they reach it two ways. **Two** drive the
library directly: the driver writes a text-proto `LogConfigSet`, reads it back
with `ctfe.LogConfigFromFile`, and calls `ctfe.SetUpInstance(ctx, client, cfg,
opts)` — a backend client, one `*configpb.LogConfig`, one
`ctfe.InstanceOptions` — taking the `*ctfe.PathHandlers` it returns and calling
`ServeHTTP` on the entry it wants, through the wrapper and never on a handler
field directly. The **other seven** start your `ct_server` as a subprocess, so
what they exercise is the binary's own flag-to-options wiring — the same call,
reached through `--log_config` and the flags rather than through a literal — and
that path is the broader of the two.

Three packages step 02 and step 03 linked are **not** linked by any driver
here: `…/x509util`, `…/logid` and `…/loglist`. Nothing in them may be
withdrawn — earlier steps' groups still stand behind you and section 9 is the
list of what is out of scope, not a list of what may be deleted — but no driver
at this step names a symbol in any of the three.

`client.CheckLogClient` is *not* on that list. It grew `BaseURI()` as its first
method at v1.0.17 — step 05, not this one (RELEASE-MAP.md §5) — it is
unchanged here, and no group at this step implements it.

## 6. Formats crossing the boundary

Step 05's `TEST_INTERFACE.md` is replaced by this file, so the list is given
here in full rather than referred to. It is **not** step 05's list copied
forward: every entry below was re-checked against *this* step's group drivers
at v1.0.19, one of them has **inverted** since the step you came from and
is marked, and §6.2 names the families earlier steps carried that nothing here
compares — so that you do not reconstruct them from memory and defend a
contract this step does not hold you to.

**Carried forward from the steps behind you, restated.**

* **TLS encodings.** `MerkleTreeLeaf`, `TimestampedEntry`, `DigitallySigned`,
  `CertificateChain`, `PrecertChainEntry`, the SCT signature input and the
  `TreeHeadSignature` input are compared **byte for byte** against vectors the
  *verifier* holds. Those vectors are not in `ct-spec/` and you cannot read
  them; they were produced from the field orders, tag parameters and widths of
  TLS-CODEC.md §2, §3 and §5, which determine the encoding completely.
  Re-derive an encoding from those rules and it will match; guess at one and it
  will not. The codec does not move at v1.0.19 and this step leans on it in
  four places: a group round-trips a `MerkleTreeLeaf` and requires the original
  bytes back and pins the vector-length framing at both widths, a group re-reads
  the two algorithm bytes of the `digitally-signed` framing as **sha256(4) /
  ecdsa(3)**, a group pins the length and the SHA-256 of a serialised leaf
  value, and the two `trillian/util` groups pin the length and the SHA-256 of
  the `CertificateChain` and `PrecertChainEntry` encodings that become a leaf's
  extra data. An empty chain is the three-byte 24-bit length `000000`, not an
  empty slice (CLIENT-API.md §8.7).
* **JSON bodies.** Field names, types and base64 padding exactly as
  HTTP-API.md §3 and §4 give them. Binary fields are standard base64 with
  padding; an empty list is `[]`, never `null`. A response is read field by
  field — nothing compares a whole JSON document as text. The responses a group
  decodes at *this* step are `get-sth` (`tree_size`, `timestamp`,
  `sha256_root_hash`, `tree_head_signature`), `get-roots` (`certificates`),
  `add-chain` and `add-pre-chain` (`sct_version`, `id`, `timestamp`,
  `extensions`, `signature`), `get-entries` (`entries`, each with `leaf_input`
  and `extra_data`) and `get-entry-and-proof` (below). Requests are read the
  same way: `leaf_index`, `tree_size`, `start` and `end` are decimal, and a
  submission's `chain` is an array of base64 DER.
* **Leaf hashes.** `SHA-256(0x00 ‖ TLS(MerkleTreeLeaf))`, compared as bytes
  (MERKLE.md §1), for an `x509_entry` and for a `precert_entry`, against pinned
  digests; a fourth case recomputes the pre-image from `ct.TreeLeafPrefix` and
  requires the same answer, so the domain-separation byte is checked and not
  merely assumed. The hash is also what `BuildLogLeaf` puts in `LeafValue`, and
  its `LeafIdentityHash` is **SHA-256 of the submitted certificate's DER and
  nothing else** (CLIENT-API.md §8.7) — pinned too.
* **Where a tree head's three fields come from.** HTTP-API.md §4.1 gives only
  their types, so the provenance is fixed here: the `timestamp` a `get-sth`
  response carries is **the backend root's `TimestampNanos`, converted to
  milliseconds** — not the personality's own clock — and `TreeSize` →
  `tree_size` and `RootHash` → `sha256_root_hash` come out of the same
  `SignedLogRoot`. Those three are also the three that enter the
  `TreeHeadSignature` input of TLS-CODEC.md §5.5. A **gate** group drives
  `get-sth` against a driver-local backend answering a pinned root and verifies
  the response's `tree_head_signature` as an ECDSA signature over that input
  rebuilt from those three values, under the public key the config named.
* **Assembled paths, and the catch-all.** `<log prefix> + <endpoint path>`,
  with the prefix normalised, exactly as HTTP-API.md §1.1 gives it. Checked here
  as four verdicts: the prefixed endpoint answers, the **unprefixed**
  `/ct/v1/get-sth` is **404**, an undefined endpoint *under* the prefix is
  **404**, and — this half is step 05's, and it survives — of the paths no
  handler claims only the exact path `/` answers **200** while every other one
  answers **404**. `/` is never prefixed.
* **PEM, and how a private key reaches a signer — read this even if you
  remember step 04's version, because the corpus has moved back again.** Root
  certificate files are given to `ct_server` by path, as `roots_pem_file`, and
  what a group compares is the DER set `get-roots` serves against the DER in
  the file that log's own config named. The log's **private key** is a
  `google.protobuf.Any` matched against the key-handler registry `ct_server`
  populates at start-up (CONFIG.md §2.3); the loader must never open the path
  itself. Step 04 said that every configuration its groups wrote named a
  `keyspb.PEMKeyFile` with a `path` and a `password`, over a
  passphrase-encrypted `EC PRIVATE KEY` file. That was true of step 04's corpus
  and it is **false here**: every configuration built at *this* step inlines the
  key as a **`keyspb.PrivateKey` carrying raw DER**, unencrypted, with no path
  and no password anywhere. That is a change of corpus, not of contract — assume
  neither key form and neither encryption state, keep both handlers registered,
  and route `private_key` through the registry, which is the only part of this
  that is yours to write. CONFIG.md §2.3's "Do not assume either one" is the
  rule, and it is the reason this bullet has now inverted twice.
* **The configuration file's encoding, and the one message shape used here.**
  The log config is a protocol-buffer message, and through v1.1.0 it is parsed
  as **text proto and nothing else** (CONFIG.md §4.2), so the JSON rendering of
  a valid message is a parse failure at v1.0.19 whatever it contains.
  Unknown field names are refused. Every configuration written at this step is
  text proto and is a **`LogConfigSet`** — repeated top-level `config`, no
  `backends` — because every group that starts a personality also passes
  `--log_rpc_server`, which is what selects that reading (CLI.md §2.2). It
  reaches your code either through `ct_server --log_config` or through
  `ctfe.LogConfigFromFile`. No group at this step checks a **rejection**
  verdict; what is checked is that a config written that way loads, that the
  instance it describes comes up, and that the roots it names are the roots
  served.
* **Removal by compilation.** A withdrawn package is observed the way the
  toolchain observes it, not with `ls`: `…/merkletree` must fail to import and
  the build error must **name the import path**, with a control probe on a
  surviving package that must build and a successor probe on the backend
  library's Merkle verifier that must build too. Deleting the directory is
  necessary; leaving a stub that still compiles fails the gate.

And, new at v1.0.19:

* **`get-entry-and-proof` responses.** `leaf_input`, `extra_data` and
  `audit_path`, exactly as HTTP-API.md §4 gives them; the client's request
  construction and response parsing are checked against pinned bytes.
* **Quota user strings.** The per-request identity the front end derives and
  passes to the backend is a string; a group checks how it is derived from a
  request and from a certificate, not what the backend does with it.

### 6.1 The strings this step compares whole

Section 3.3 sets its no-standard-library-wording rule aside only for what this
section lists, so here is the list, audited group by group. It has **four**
entries.

1. **The certificate quota user, whole.** Two comparisons, both exact. The
   exported constant `ctfe.CertificateQuotaUserPrefix` is compared against
   `@intermediate`, character for character. And `ctfe.QuotaUserForCert`'s
   result is compared as a whole string against the three-field rendering
   CLIENT-API.md §8.6 fixes —

       "<CertificateQuotaUserPrefix> <Subject.String()> <hex(SHA256(SubjectPublicKeyInfo)[0:5])>"

   — for six certificates: the two roots and the leaf of this benchmark's own
   corpus, the two `Fake*Authority` certificates of the upstream corpus, and one
   more whose expectation the group recomputes from that certificate's own
   `RawSubjectPublicKeyInfo` rather than pinning. Everything in that string is
   contract: the single spaces, the RFC 2253 subject order `pkix.Name.String()`
   produces, **five** bytes of the digest and not four or six, and lower-case
   hex. The same strings are compared again, as whole elements of
   `ChargeTo.User`, by the two groups that submit chains — so getting the
   rendering wrong fails more than one group.

2. **The query string a client composes.** Two request shapes, each compared
   whole against what the driver's own HTTP server received. `get-entries`
   is compared as the **raw** query the fetcher sent — `end=4&start=2`,
   `end=6&start=5`, `end=3&start=0`, `end=7&start=4`, in the order the batches
   were issued, so the parameter *set* and the batch boundaries are both checked.
   `get-entry-and-proof` is compared after canonicalisation —
   `leaf_index=7&tree_size=100000` — so its parameter names, their decimal
   spelling and the absence of any extra parameter are checked but their order on
   the wire is not.

3. **The first line of a `--help` block.** The `flag` package's default usage
   function prints `Usage of <os.Args[0]>:` and nothing else on that line. Every
   flag-parsing program a group runs at this step is required to still produce
   it — you get it by leaving `flag.Usage` alone and you lose it by installing a
   usage function of your own, so if you want a synopsis, print it **after** this
   line. For `migrillian` both ends of the line are compared: it must begin
   `Usage of ` and it must end `/migrillian:`, which is how "the binary named
   itself" is checked. A **gate** group is among those that require the prefix,
   so a replacement `flag.Usage` fails the step.

4. **A flag's declaration line, and the `(default …)` the `flag` package prints
   under it.** A declaration is two spaces, `-`, the name, and — for every type
   except `bool` — a space and the type word; a line that does not start with
   exactly two spaces and a dash is not seen as a declaration at all. What is
   compared at this step is the name, the type word beside it, and the printed
   default: `ct_server`'s `-quota_remote` and `-quota_intermediate` descriptions
   must each **end** `(default true)`; `migrillian`'s fourteen are compared as
   name, type and default together, including the two quoted string defaults
   `"https://ct.googleapis.com/aviator"` and `"localhost:8090"`, the duration
   `5s`, the integers `512`, `2`, `2`, `5`, `100` and `10`, and the five whose
   default the `flag` package prints not at all because it is the zero value;
   and `scanlog`'s and `ctclient`'s `-log_uri` defaults are compared as the
   quoted strings `"https://ct.googleapis.com/aviator"` and
   `"https://ct.googleapis.com/rocketeer"`. Flag *names* are also compared as
   whole sorted sets, less the logging library's seven: `ct_server`'s
   seventeen and `migrillian`'s fourteen, exactly, so an extra flag fails as
   surely as a missing one. **No description is ever compared** — the
   `(default …)` is read off the end of the line and the sentence in front of it
   is not looked at.

**That list is exhaustive.** No other output of any program, and no other
string your code produces, is compared against a fixed string at this
milestone. Several assertions look like this one and are not, and none of them
checks wording:

* **Bytes, digests and lengths.** The TLS vectors, the leaf hashes, the extra
  data, the serialised SCT, the pinned `LeafValue` and `LeafIdentityHash` — all
  hex or base64 renderings of *encodings*, compared against vectors derived from
  TLS-CODEC.md and MERKLE.md, not against prose.
* **Path and endpoint constants.** A driver compares the method and path of the
  request its client sent against `ct.GetEntryAndProofPath` from your own tree.
  That is a documented identifier (HTTP-API.md §1) checked against itself, not a
  string you are asked to word.
* **The backend's own names.** The set of RPC methods a request charged —
  `GetLatestSignedLogRoot`, `GetEntryAndProof`, `GetInclusionProofByHash`,
  `GetConsistencyProof`, `GetLeavesByIndex` — are `github.com/google/trillian`'s
  identifiers, observed on a driver-local stub. So is every `trillian.ChargeTo`
  field name.
* **Identifiers the group itself supplied.** The IP addresses a driver puts in
  `X-Forwarded-For` and reads back as the remote quota user, the log's prefix,
  a file path, a tree size, a leaf index. The group chose them; it is checking
  that they came back, not that you spelled something.
* **The recorder's own labels.** The `Start,LogPrefix,AddDERToChain,…,IssueSCT,
  Status` sequence a group requires of one accepted submission is written in the
  group's names for the `RequestLog` methods it implements. What is checked is
  the order and the multiplicity of the calls, not any text.
* **The Go toolchain's build error.** The withdrawal gate greps a build log for
  the import path `github.com/google/certificate-transparency-go/merkletree`.
  That is the toolchain's output naming a path, not yours.

No diagnostic, no error message and no usage description is compared anywhere
in this step. Where an error is required, it is checked as *an error was
returned and no usable value came back with it* — the group on
`ToSignedTreeHead` says so explicitly, checking only that the returned head is
nil and the error is not.

### 6.2 What earlier steps listed and this one does not

Named because you no longer have those files, and because reconstructing the
list from memory would have you defending surfaces nothing here measures.

* **The multi-backend configuration message.** `LogMultiConfig`,
  `LogBackendSet`, `backend_spec` and the `log_backend_name` binding were on
  steps 02, 03 and 04's lists, and step 04 ran both message shapes by toggling
  `--log_rpc_server`. **This step runs one shape only:** every group that starts
  a personality passes `--log_rpc_server`, so `--log_config` is always read as a
  `LogConfigSet`, one log, one backend. CONFIG.md §1 and §3 still give the other
  shape and a later step may measure it.
* **Every configuration *rejection*.** The JSON-config refusal, the
  unknown-field refusal, and the two diagnostics `empty backend name` and
  `duplicate backend name` that step 03 matched as documented prefixes. There is
  no configuration-validator group at this step at all: the only configuration
  verdicts here are acceptances.
* **Two logs at once, and per-log root separation.** Step 04 gave two logs
  different `roots_pem_file` pools and required each prefix to answer with its
  own set. This step runs a single log, `bench`. What survives of that entry is
  the narrower half restated above: `get-roots` serves exactly the DER the
  config named.
* **Signature distinctness and the STH signature cache.** Step 04's gate moved
  the pinned root by an entry, by a bit of the root hash and by a millisecond,
  required a different `tree_head_signature` each time, then asked again
  unchanged and required the byte-identical one back. **Nothing at this step
  moves the pinned root or asks twice.** The tree-head signature is still
  verified — see the provenance bullet above — but neither distinctness nor the
  cache is observed here.
* **`Content-Type: application/json` as a header value.** Step 03's in-process
  groups read it off the recorder. The kit at this step captures response
  headers and **no driver reads one**. HTTP-API.md §2 still fixes it and the
  responses this step decodes are JSON, so keep setting it.
* **The whole CORS layer.** Step 05's new material: `Access-Control-Allow-Origin:
  *` on any request carrying `Origin`, the `OPTIONS` pre-flight answered by the
  wrapper itself with `Access-Control-Allow-Methods`, and the three `Vary`
  values in order. It is still contract at v1.0.19 (HTTP-API.md §7) and it
  arrived at v1.0.17, but no group here sends an `Origin` header or a
  pre-flight, and none reads a response header, so none of it is observed. The
  other half of that same step-05 entry — the catch-all narrowing to `/` alone —
  **is** still checked, by a gate.
* **Merkle proof verification.** Inclusion through `ctutil.LogInfo` and the
  `VerifyInclusion*` family, consistency through `ctclient consistency`, and the
  node and root hashes of MERKLE.md §2–§4, all of which steps 04 and 05 listed.
  No group here builds a `LogInfo`, verifies a proof or runs `ctclient`.
  MERKLE.md §1's leaf hash *is* still checked, and the withdrawal gate still
  requires the backend library's verifier to build, so the capability must
  remain — it is simply not exercised.
* **The rest of `ctutil`.** `LeafHash`, `LeafHashB64`, `ContainsSCT`, the
  `embedded` pre-check and the entry-type choice it feeds, the
  `MerkleTreeLeafForEmbeddedSCT` route, the empty-chain and nil-SCT refusals,
  the `-1`/zero-hash/empty-string failure returns, and `LastSTH`/`SetSTH`
  caching — step 04's milestone, eleven checked cases of it. Exactly **one** name
  from that package is called at this step: `ctutil.VerifySCT`, once, in the
  gate, over a real submission with `embedded` false.
* **Log lists and `findlog`.** Schema v1's field names including `final_sth`
  and `disqualified_at`, `LogInfo.MMD` as a `time.Duration`, and `findlog`'s
  stdout with its `--verbose` block. `…/loglist` is not linked by any driver
  here and `findlog` is only required to build and to print a usage banner.
* **`logid`'s string forms** (step 02) and **`x509util`'s renderings** —
  `GeneralNamesToString` and `OtherNameToString` from step 02, and the poison
  and SCT-list sections of `CertificateToString` that step 03 compared byte for
  byte. Neither package is linked at this step.
* **`get-sth-consistency`'s parameter-error bodies** and every other error body
  earlier steps read for its documented prefix. Nothing at this step reads a
  body for its wording; see the end of §6.1.

**None of this is a licence to delete anything.** Section 6 is the list of what
is *compared*; section 9 is the list of what is *out of scope*; a surface that
appears in neither still has to compile, still has to resolve for every driver
that links its package, and may be measured again at the step after this one.

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
| `inherited-ctfe-config-acceptance` | gate |
| `inherited-merkletree-withdrawn` | gate |
| `inherited-rfc6962-leaf-hash` | gate |
| `inherited-tls-codec` | gate |
| `cli-new-command-surface` | requirement |
| `client-get-entry-and-proof` | requirement |
| `ctfe-add-chain-charges-intermediates` | requirement |
| `ctfe-add-chain-charges-remote-user` | requirement |
| `ctfe-get-tree-head-remote-quota` | requirement |
| `ctfe-quota-flags-disable-charging` | requirement |
| `ctfe-quota-user-for-cert` | requirement |
| `ctfe-read-endpoints-charge-remote-user` | requirement |
| `ctfe-requestlog-issue-sct` | requirement |
| `ctserver-quota-flag-grammar` | requirement |
| `migrillian-command-flag-grammar` | requirement |
| `scanner-fetcher-continuous-and-sth-caching` | requirement |
| `sth-response-to-signed-tree-head` | requirement |
| `trillian-util-build-log-leaf` | requirement |
| `trillian-util-extra-data-for-chain` | requirement |

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

* **Quota enforcement.** A backend a group stands up answers from pinned
  constants and never refuses anything for want of allowance (section 3.4), so
  nothing can run out. What is checked is the plumbing: the flags, the derived
  user strings, and the shape of the API the front end offers.
* **HTTP 429.** The status that quota exhaustion eventually produces does not
  arrive until v1.2.0; it is out of scope here.
* **migrillian actually migrating a log.** It needs both a source log and a
  backend. Only its **flag set** and its exit status are checked (CLI.md §4.7);
  its transfers are not.
* **A migrillian configuration file.** There is none at v1.0.19: migrillian
  takes no `config` flag until v1.0.21 and is driven entirely by the fourteen
  flags of CLI.md §4.7. Nothing here checks config parsing, config validation,
  or a refusal to start without a config.
* **Continuous fetching against a real log.** `FetcherOptions.Continuous` is
  checked for the field's presence, its default and its effect on the fetch loop
  against a driver's own loopback log (CLIENT-API.md §5.1); the network
  behaviour of a real growing log is not, and neither is what `Run` returns when
  the context it was given is cancelled — CLIENT-API.md §5.1 fixes how a
  continuous run is *stopped*, not what the call hands back, so a group accepts
  either a `nil` return or the context's own error.
* **The rename of `ctfe.LogContext` to the unexported `logInfo`, and of
  `AppHandler.Context` to `AppHandler.Info`.** CLIENT-API.md §8.3 records it
  as a v1.0.19 change, but the new type is unexported, so no driver can
  name it, construct it or call a handler through it: nothing outside the
  package can observe the rename except by reading your source, which no
  group does. What the handlers *do* is checked end to end instead. Renaming
  or not renaming the type changes nothing here.
* **Anything dated after v1.0.19.**

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

Two threads, one theme: the log stops pretending that resources are free.

The first thread is quota. Until now the front end passed work to the backend
and the backend either did it or failed. At v1.0.19 the front end names
*who* is asking — a remote quota user derived from the request, an intermediate
quota user derived from the certificate — and passes that along, so that the
error taxonomy gains a resource dimension: "I will not do this because you have
used your allowance" becomes distinct from "I cannot do this".

The second thread is `migrillian`, the tool that copies an existing log into a
backend, which has to care about ordering and about resuming where it stopped.

Several smaller changes ride along, and three of them are breaking in the
small. `LogClient` gains `GetEntryAndProof`, the last of RFC 6962's eight
endpoints (§4.8) and the sixth and last of the six read endpoints of
HTTP-API.md §4. `ctfe.RequestLog` gains an eleventh method, `IssueSCT`, so every
implementation of that exported interface must be updated. And
`scanner.FetcherOptions` **loses** `Quiet` in exchange for `Continuous`, so
code that set the old field stops compiling and `scanlog` loses `--quiet`.

Do not confuse this step with the last one: `CheckLogClient` acquired
`BaseURI()` at v1.0.17 and is untouched here.
