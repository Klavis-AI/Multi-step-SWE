# TEST_INTERFACE — step 01 of 13 (certificate-transparency-go v1.0)

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

At v1.0 the tree holds **8** `package main` directories (CLI.md §1), and
`go build ./...` must produce a binary for each. What each one is checked on:

| program | directory | what a group does with it |
| --- | --- | --- |
| `ct_server` | `trillian/ctfe/ct_server/` | starts it with a fixture config and a flag set; checks that it comes up and serves, the flag grammar, `get-roots`, where `/metrics` is served, and clean shutdown. Configuration *acceptance and rejection* are checked too, but through `LogConfigFromFile` and `SetUpInstance` in a driver (sections 5 and 6), not by running this binary on a bad file; and **no group checks one of the `glog` start-up diagnostics CLI.md §2.2 names** at this step, because section 9 removes log output from the check at every severity. The `flag` package's usage dump is not one of those and *is* checked — see the three rules below |
| `ctclient` | `client/ctclient/` | runs the v1.0 flat flag grammar (CLI.md §3.1) and checks argument handling and exit status |
| `scanlog`, `preloader`, `dumpscts`, `chainfix`, `ct_hammer`, `gossip_server` | see CLI.md §1 | checks only that `go build ./...` produces an executable of that name, and that the eight names are the whole set. No group runs any of the six at this step |

Three rules hold for every program and are checked wherever a group *runs* one
— which at this step means `ct_server` and `ctclient` only (CLI.md, the
preamble on flag grammar):

* an unrecognised flag makes the standard `flag` package print `Usage of
  <prog>:` and the flag list **to stderr** and exit **2**;
* a missing *required* flag is a bare diagnostic on stderr and exit **1**, with
  no usage dump;
* `glog.Exit*` and `log.Fatal*` exit **1**; `glog.Fatal*` exits **255**. At
  v1.0 no program in scope reaches `glog.Fatal*` on any input a group can
  supply, so only the exit-**1** half of that rule is observed at this step.

`ct_server` at this milestone declares ten flags of its own and carries an
eleventh, `--allow_verification_with_non_compliant_keys`, from the root
package: eleven rows are marked at the v1.0 column of CLI.md §2.1.
Those eleven are the product's own; on top of them every binary that links
`glog` also carries `glog`'s seven global flags, `--log_dir` among them (CLI.md,
the preamble on flag grammar), and a group may supply any of the eighteen.

Where a group runs `ct_server` as a server, two of those flags always come
from the verifier, and they carry different things:

* `--log_config` names the configuration file, whose contents are section 6's
  business.
* `--log_rpc_server` names the Merkle-tree backend. At v1.0 this is the
  **only** way a backend address reaches `ct_server`: the config message has no
  backend field of any kind until v1.0.5 (CONFIG.md §1's field table, CONFIG.md
  §3), so where section 3.4 says the stub's address reaches your `ct_server`
  through the configuration file, that describes the later releases'
  arrangement — at v1.0 read it as `--log_rpc_server`. Whether anything
  answers at the address is still the group's choice (section 3.4).

Start-up, the flag grammar, the endpoints that need no tree, the exit status on
a signal (CLI.md §2.3) and clean shutdown are what is checked.

## 5. Packages and symbols the verifier links against

Driver groups import your tree through the module path of section 2. At this
milestone they link against:

| import path | symbols this step's contract covers |
| --- | --- |
| `github.com/google/certificate-transparency-go` | **structures** `MerkleTreeLeaf`, `TimestampedEntry`, `LogEntry`, `SignedCertificateTimestamp`, `SignedTreeHead`, `TreeHeadSignature`, `DigitallySigned`, `ASN1Cert`, `CertificateChain`, `PreCert`, `JSONDataEntry`, `CTExtensions`, `SHA256Hash`, `LogID` (TLS-CODEC.md §5). **Enums and constants** `Version`/`V1`, `LogEntryType`/`X509LogEntryType`/`PrecertLogEntryType`/`XJSONLogEntryType`, `MerkleLeafType`/`TimestampedEntryLeafType`, `SignatureType`/`CertificateTimestampSignatureType`/`TreeHashSignatureType`, **each with a `String() string` method** whose renderings TLS-CODEC.md §5.1 gives. **Base64 helpers** `SHA256Hash.Base64String`/`FromBase64String` and `DigitallySigned.Base64String`/`FromBase64String`, whose arities differ (TLS-CODEC.md §5.6). **Functions** `NewSignatureVerifier`, `SignatureVerifier` with `VerifySCTSignature` and `VerifySTHSignature`, `PublicKeyFromPEM`, `MerkleTreeLeafFromChain`, `SerializeSCTSignatureInput`, `SerializeSTHSignatureInput` (CLIENT-API.md §3; the two `Verify*Signature` methods' prototypes are below this table). **The ten JSON wire types** `AddChainRequest`, `AddChainResponse`, `AddJSONRequest`, `GetSTHResponse`, `GetSTHConsistencyResponse`, `GetProofByHashResponse`, `LeafEntry`, `GetEntriesResponse`, `GetRootsResponse`, `GetEntryAndProofResponse`, with the Go field names and `json:` tags of CLIENT-API.md §3.2. **The nine endpoint path constants** of HTTP-API.md §1 — the eight RFC paths and `AddJSONPath` |
| `…/tls` | `Marshal`, `Unmarshal`, `MarshalWithParams`, `UnmarshalWithParams`, `Enum`, `Uint24`, `DigitallySigned`, `SignatureAndHashAlgorithm`, `HashAlgorithm` with `None`/`MD5`/`SHA1`/`SHA224`/`SHA256`/`SHA384`/`SHA512`, `SignatureAlgorithm` with `Anonymous`/`RSA`/`DSA`/`ECDSA`, `CreateSignature`, `VerifySignature`, and the struct-tag grammar of TLS-CODEC.md §1 and §3 |
| `…/x509` | `ParseCertificate`, `ParseCertificates`, `ParseTBSCertificate`, `Certificate`, `NonFatalErrors`, `RemoveCTPoison`, `BuildPrecertTBS`, `OIDExtensionCTPoison`, `NewCertPool`, `VerifyOptions`, `ExtKeyUsage` with `ExtKeyUsageAny` and `ExtKeyUsageCertificateTransparency`, `ParseECPrivateKey`, `ParsePKIXPublicKey`; and, on `Certificate` itself, the fields and methods a driver at this step reads — `Raw`, `RawTBSCertificate`, `RawIssuer`, `RawSubject`, `RawSubjectPublicKeyInfo`, `Extensions` (read for `Id`, `Critical`, `Value`), `Subject` and `Issuer` (read for `CommonName` and `Organization`), `SerialNumber`, `NotBefore`, `NotAfter`, `DNSNames`, `IPAddresses`, `IsCA`, `PermittedDNSDomains`, **`AuthorityKeyId`**, `SignatureAlgorithm`, `Signature`, `ExtKeyUsage`, and the three methods `CheckSignature`, **`CheckSignatureFrom`** and `Verify`. Those members keep their `crypto/x509` names, shapes and semantics — the fork re-spells none of them (X509-FORK.md §1) — except that the `Extension`, `Name` and `AlgorithmIdentifier` values they carry come from the fork's own copy of `pkix` at `…/x509/pkix` (X509-FORK.md's header), which no driver imports by name; two of them are checked on a fork-specific detail and are the reason they are named here rather than left implicit — `AuthorityKeyId` and the AKI entry of `Extensions` are what `BuildPrecertTBS` reads a pre-issuer's authority key identifier out of (X509-FORK.md §4.2), and `PermittedDNSDomains` is what survives the non-fatal `nameConstraints` parse of X509-FORK.md §3 (X509-FORK.md) |
| `…/asn1` | `Unmarshal`, `Marshal`, `ObjectIdentifier`, `StructuralError`, `SyntaxError` |
| `…/merkletree` | `NewTreeHasher` and the `TreeHasher` it returns, with `HashEmpty`, `HashLeaf`, `HashChildren`; `NewMerkleVerifier` and the `MerkleVerifier` it returns, with `VerifyInclusionProof`, `RootFromInclusionProof`, `VerifyConsistencyProof`; `RootMismatchError`; and the two untyped integer constants `LeafPrefix` = 0 and `NodePrefix` = 1 (MERKLE.md §4.1) |
| `…/client`, `…/jsonclient` | `client.New` and the `LogClient` it returns, with `AddChain`, `AddPreChain`, `GetSTH`, `GetSTHConsistency`, `GetProofByHash`, `GetEntries`, `GetRawEntries`, `GetAcceptedRoots` (CLIENT-API.md §1.2); `jsonclient.New`; `jsonclient.Options` constructed as a struct literal with the fields `PublicKey` and `Logger`; and the one-method `jsonclient.Logger` interface (CLIENT-API.md §1.1) |
| `…/x509util` | `CertificateToString` — the only symbol of this package a driver links against at v1.0, and the one `ctclient get-roots` renders with. CLIENT-API.md §5.2's arrival table dates the rest of the package: five other functions exist at v1.0 (`NameToString`, `CertificateFromPEM` with a **`string`** argument, `OidFromNamedCurve`, `OidForStandardExtension`, `OidInExtensions`) and are out of scope; the PEM chain reader, the file/URL readers, the CRL renderers and the SCT extraction helpers are **not in this release** and no group looks for them |
| `…/trillian/ctfe` | `LogConfigFromFile`, `SetUpInstance`, `PathHandlers`, `AppHandler` with its exported `Name` and `Method` fields, `EntrypointName` and the eight `…Name` constants, `NewPEMCertPool` and `PEMCertPool` with `AddCert`, `AppendCertsFromPEM`, `AppendCertsFromPEMFile`, `RawCertificates`, `Subjects`, `CertPool`; `GetCTLogID`, `IsPrecertificate`, `ValidateChain`, `MaxGetEntriesAllowed` (CLIENT-API.md §8.1 and §8.3; `LogConfigFromFile`'s signature is CONFIG.md §3). The pool's own loading, de-duplication and subject rules are CLIENT-API.md §5.3 |
| `…/trillian/ctfe/configpb` | `LogConfigSet` and `LogConfig`, as the Go structs `protoc-gen-go` generates from CONFIG.md §1, with the field names **and the `Get…` accessors** CONFIG.md §1.2 spells out |

A symbol named here must be exported with exactly this name, from exactly this
import path, with the signature the corpus page gives. A driver that cannot
resolve one fails its own group and no other.

Nothing in this section is inherited by reference — this is the first step, and
your workspace carries this `TEST_INTERFACE.md` and no other. The table above
and the table below are together the whole of the surface **at the level of
import paths**: every import path a driver at this step links is a row in one
of them, and a path in neither is not part of it. Within a path, the symbol
lists name everything whose spelling, signature or behaviour this product
fixes, including every member on which a group checks something the fork does
differently from the standard library; they are not an inventory of every
identifier a driver mentions, because where the fork inherits a standard-library
declaration unchanged (X509-FORK.md §1) a driver may read any part of it and
nothing about that part is this contract's to state. The table above is the half you write. The table below is the half
you do not — the **third-party modules** a driver names directly. Nothing in
them is checked as work of yours: you neither write that code nor choose its
versions, because `go.mod` and `go.sum` are benchmark-owned and are replaced
with the pinned copies before any build (§2). They are written down only so
that what must keep **resolving** is visible: a driver is compiled in a
throwaway module beside your tree, so a dependency your manifest stops
providing, or provides at another version, takes that driver's whole group down
with it. Every path in this second table is written in full, never with the
`…/` shorthand, because that shorthand means *your* module and none of these
are it.

| import path (third-party, not yours) | what the drivers use |
| --- | --- |
| `github.com/google/trillian` | **not yours to write** — §3.4: the log backend is out of scope. It is named because it must keep *resolving*. `TrillianLogServer`, which the **benchmark** implements as the in-process stub of §3.4, and `RegisterTrillianLogServer`, which puts that stub on a `*grpc.Server`. At this step's pin the interface has **ten** methods and no `Unimplemented` embed, so the stub declares every one of them — `QueueLeaf`, `QueueLeaves`, `GetLatestSignedLogRoot`, `GetLeavesByIndex`, `GetLeavesByHash`, `GetInclusionProof`, `GetInclusionProofByHash`, `GetConsistencyProof`, `GetEntryAndProof`, `GetSequencedLeafCount` — together with each one's request/response message pair, and with `LogLeaf`, `QueuedLogLeaf` and `Proof`. At this pin `SignedLogRoot` still carries the plain fields `TimestampNanos`, `RootHash`, `TreeSize`, `LogId` and `TreeRevision` rather than a marshalled `LogRootV1`, and that is the shape your front end reads an STH out of. `TrillianLogClient` is named by no driver here, but it is the declared type of `SetUpInstance`'s second argument (CLIENT-API.md §8.1), which the in-process groups pass as a bare `nil` |
| `github.com/google/trillian/monitoring` | `InertMetricFactory{}`, the do-nothing `MetricFactory` with which a driver builds an instance without publishing anything. It is the **fifth argument** of v1.0's `SetUpInstance(ctx, client, cfg, deadline, mf)` (CLIENT-API.md §8.1), so the declared parameter type `monitoring.MetricFactory` is part of *your* function's signature even though the package is not yours |
| `github.com/google/trillian/crypto/keyspb` | `PEMKeyFile` and its `Path` field — the concrete message a driver unwraps a config's `private_key` `Any` into, and whose type URL it requires to be exactly `type.googleapis.com/keyspb.PEMKeyFile` (CONFIG.md §2.3). `keyspb.PublicKey`, carrying the DER in a field named `Der`, is reached the same way through `LogConfig`'s `public_key` (CONFIG.md §1.2) |
| `github.com/google/trillian/crypto/keys/der/proto` and `github.com/google/trillian/crypto/keys/pem/proto` | imported for their **side effect only**, as `_`, exactly as `ct_server`'s own `main` does: they register the DER and PEM-file entries of the key-handler registry CONFIG.md §2.3 describes, without which the key loader behind `SetUpInstance` cannot build a signer. A driver names these two paths and no symbol inside them, which is still a link: drop either from the module graph and that group fails to build |
| `github.com/golang/protobuf/proto` | `proto.Clone`, with which a driver copies a parsed `*configpb.LogConfig` before changing exactly one field of it, so that each `SetUpInstance` refusal is provoked by that one change and nothing else |
| `github.com/golang/protobuf/ptypes` | `ptypes.UnmarshalAny`, the helper that takes the `keyspb.PEMKeyFile` back out of a config's `private_key` `Any` |
| `google.golang.org/grpc` | `NewServer` and the `*Server` it returns, with `Serve` and `Stop`: a group binds a loopback socket for itself and stands the §3.4 stub on it. The gate group stands one up with **no service registered at all**, because v1.0's `ct_server` dials its single backend **blocking** and does not begin serving until that dial returns (CLI.md §2.2) — so something must be listening even for a `get-roots` that never reaches the backend. The gRPC server is the benchmark's, never yours, and no driver speaks gRPC to your front end |
| `google.golang.org/grpc/codes` and `google.golang.org/grpc/status` | `codes.Code` with `OK`, `NotFound`, `ResourceExhausted`, `DeadlineExceeded`, `Unknown` and `Unimplemented`, and `status.Error`: the stub answers a chosen call with a chosen gRPC status instead of a result (§3.4), so that the backend-error map of HTTP-API.md §6.1 can be walked, and refuses with `Unimplemented` the four methods no v1.0 CTFE code path reaches. The status is the group's; the HTTP status your front end maps it to is yours |
| `golang.org/x/net/context` | `Context`, the spelling this pin's generated `TrillianLogServer` uses for its first parameter. At the Go version the pinned `go.mod` declares it is a **type alias** for the standard library's `context.Context`, so the two are interchangeable in a signature; the path is listed because a driver imports it and it therefore has to resolve |

These nine rows are **third-party modules and not part of your deliverable**.
§2's pinned manifest fixes them at `github.com/google/trillian`
**v0.0.0-20170911114049-c0a332878f82**, `github.com/golang/protobuf`
**v0.0.0-20170902000452-17ce1425424a**, `google.golang.org/grpc` **v1.6.0** and
`golang.org/x/net` **v0.0.0-20170828231752-66aacef3dd8a**. Those exact vintages
are why the shapes above are the shapes above: a later Trillian is a different
`SignedLogRoot` and a different method set.

One collision is worth spelling out, because the two names differ by a prefix
the eye skips. The `…/trillian/ctfe` and `…/trillian/ctfe/configpb` rows of the
first table expand, by §2's directory rule, to
`github.com/google/certificate-transparency-go/trillian/ctfe` and
`github.com/google/certificate-transparency-go/trillian/ctfe/configpb` — the
**fork's own** `trillian/` directory, code you own and are checked on.
`github.com/google/trillian` is a different module altogether, upstream, which
you do not own and which is out of scope. Read one for the other and half of this
section inverts.

These shapes are easy to get wrong and are all checked at compile time:

* `SetUpInstance` returns **`(*PathHandlers, error)`** at v1.0 — a
  pointer to a named map type, not a bare map. `PathHandlers` is
  `map[string]AppHandler`, so `len(*handlers)` is the entrypoint count.
  ARCHITECTURE.md §6 sketches the call; CLIENT-API.md §8.1 is the signature.
* `PublicKeyFromPEM` returns **four** values, and
  `NewSignatureVerifier` **two** (CLIENT-API.md §3).
* `SHA256Hash.Base64String()` returns one value; `DigitallySigned.Base64String()`
  returns two (TLS-CODEC.md §5.6).
* the two `SignatureVerifier` methods a group calls take the CT structure
  itself, not a buffer and a signature:

      func (s SignatureVerifier) VerifySCTSignature(sct SignedCertificateTimestamp, entry LogEntry) error
      func (s SignatureVerifier) VerifySTHSignature(sth SignedTreeHead) error

  Each re-derives its own signature input — section 6's SCT input from `entry`
  and `sct`'s version, type and timestamp; the `TreeHeadSignature` input from
  `sth` — and checks `sct.Signature` / `sth.TreeHeadSignature` against it, so a
  caller passes the structures and nothing else. `LogClient` has wrappers with
  these same two names (CLIENT-API.md §1.2) and a **different** argument list;
  those belong to `…/client`, and a group that asks the root package for them
  must find them on `SignatureVerifier`. `VerifySignature(data []byte, sig
  tls.DigitallySigned) error` is the third method on the same type and is what
  the other two are built on.
* `tls.CreateSignature(privKey, hashAlgo, data)` (TLS-CODEC.md §1) dispatches on
  the private key's **dynamic type**, and the types it accepts are the
  **value** forms `rsa.PrivateKey` and `ecdsa.PrivateKey` — a `*ecdsa.PrivateKey`
  is not one of them, so a caller holding the pointer `ParseECPrivateKey`
  returns passes `*priv`. Any other dynamic type is returned as an error, never
  a panic.
* the fork does not re-spell what it inherits. `NewCertPool`, `VerifyOptions`
  and its `Roots` / `Intermediates` / `KeyUsages` fields, `ExtKeyUsageAny`,
  `ParseECPrivateKey` and `ParsePKIXPublicKey` keep their `crypto/x509` names
  and signatures (X509-FORK.md §1: "everything else in the fork is upstream
  Go"), so a group builds a pool and verify options exactly as it would against
  the standard library — only the `Certificate` they carry is the fork's.

`ParseBodyAsJSONChain` is **not** exported at v1.0 and nothing here
requires it; it becomes part of the package surface at v1.1.0
(CLIENT-API.md §8.3).

## 6. Formats crossing the boundary

* **TLS encodings** — `MerkleTreeLeaf`, `TimestampedEntry`, `DigitallySigned`,
  the SCT signature input and the `TreeHeadSignature` input are compared
  **byte for byte** against vectors the *verifier* holds. Those vectors are not
  in `ct-spec/` and you cannot read them; they were produced from the field
  orders, tag parameters and widths of TLS-CODEC.md §2, §3 and §5, which
  determine the encoding completely. Re-derive an encoding from those rules and
  it will match; guess at one and it will not.
* **JSON bodies** — field names, types and base64 padding exactly as
  HTTP-API.md §3 and §4 give them. Binary fields are standard base64 with
  padding; an empty list is `[]`, never `null`.
* **Leaf hashes** — `SHA-256(0x00 ‖ TLS(MerkleTreeLeaf))`, compared as bytes
  (MERKLE.md §1).
* **PEM** — root certificate files given to `ct_server` by path, and the log's
  **private key**, named by a `keyspb.PEMKeyFile` message inside `private_key`
  with its `path` and `password` fields (CONFIG.md §2.3). Every configuration
  a group at this step builds a log instance from names its key that way, and
  the file it names is **passphrase-encrypted**, with the passphrase supplied in
  `password`. (Configurations that are only *parsed* may name an unencrypted
  key and leave `password` unset; the field is optional.) Decrypting it is
  not yours to write: `private_key` is a `google.protobuf.Any` matched against
  the key-handler registry `ct_server` populates at start-up (CONFIG.md §2.3),
  and the pinned PEM handler does the decryption once the message reaches it.
  A loader that opens the path itself and ignores `password` will not get a
  signer out of it.
* **Configuration** — the log config is a protocol-buffer message, and at
  v1.0 it is parsed as **text proto and nothing else** (CONFIG.md §4.2),
  despite `--log_config`'s help text saying JSON. That is checked from both
  sides: a valid text-proto `LogConfigSet` must load, and the JSON rendering of
  the very same message must be **refused**. The single-log fixtures ship in
  both encodings for exactly that reason (the `multi-*` fixtures are text-proto
  only; they belong to the next milestone), and the `.expect` files beside them
  are labels no group reads — at this release the verdict is fixed by the
  encoding. Unknown
  field names are refused too, which is what makes CONFIG.md §1's
  field-arrives-at-release-N table observable.
* **Backend error statuses** — `ctfe-endtoend-over-socket` answers one backend
  call with a chosen gRPC status and checks the HTTP status your front end maps
  it to, exactly as HTTP-API.md §6.1's table gives it for this release: a
  backend `NotFound` must become 404, `ResourceExhausted` 403,
  `DeadlineExceeded` 408, and a code outside the table 500. Only the status is
  checked there, never the body that carries it.

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
| `client-over-socket` | requirement |
| `ct-leaf-hash-known-answers` | requirement |
| `ct-sct-signature-input` | requirement |
| `ct-signature-verifier` | requirement |
| `ct-sth-signature-input` | requirement |
| `ctclient-cli` | requirement |
| `ctfe-config-loading` | requirement |
| `ctfe-endtoend-over-socket` | requirement |
| `ctfe-handler-envelope` | requirement |
| `inherited-ctfe-pool-and-logid` | requirement |
| `inherited-json-wire-types` | requirement |
| `inherited-rfc6962-constants` | requirement |
| `inherited-x509-fork-parse` | requirement |
| `merkletree-proofs` | requirement |
| `tls-digitally-signed` | requirement |
| `tls-integer-enum-codec` | requirement |
| `tls-vectors-and-variants` | requirement |
| `x509-nonfatal-errors` | requirement |
| `x509-precert-tbs` | requirement |

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

* **Anything from a later release.** No group at this step looks for `ctutil`,
  `logid`, `loglist`, `ct.LeafHashForLeaf`, a multi-backend config, or any
  other capability the corpus dates after v1.0. Where v1.0's
  documented behaviour differs from a later release's, v1.0's is what is
  checked. Nothing here *rejects* a symbol that arrives early — no group
  checks an absence — but nothing requires one either, and a later release's
  *behaviour* written early does not satisfy the requirement the earlier
  behaviour carried.
  Two cases are easy to get wrong because the corpus lists them beside things
  that do exist now:
  * the fork's `x509.IsFatal`. The *behaviour* it later names — a
    `NonFatalErrors` value is returned alongside a usable certificate, and only
    a different error type is fatal — is checked here in full, but the exported
    predicate arrives at v1.0.21 (X509-FORK.md §3.1), so section 5 does not
    name it and no group calls it.
  * `x509util`'s SCT extraction helpers, PEM chain reader and file/URL
    readers. CLIENT-API.md §5.2 lists the package at its widest and then dates
    each function; at v1.0 only six exist and `CertificateToString` is
    the only one any group reaches. What is checked there is the **wiring**:
    that `ctclient get-roots` renders the certificates it fetched through
    `x509util.CertificateToString` rather than through `%v` or PEM. The
    expected text is produced by calling *your* `CertificateToString` on the
    same certificates, so the renderer's own output is compared with itself and
    no sentence, label or indent it emits is pinned by any group at this
    milestone.
* **The wording of flag help strings.** The `--log_config` help text at
  v1.0 says "JSON" while the loader parses text proto; neither string is
  checked. What is checked is which files load and which are refused.
* **Metrics content.** *Where* `/metrics` is served is observable and checked,
  on both halves of HTTP-API.md §7's rule: a non-empty `--metrics_endpoint`
  that differs from `--http_endpoint` gets a listener of its own, and
  `/metrics` is then served on that listener and is **not** registered on the
  HTTP endpoint's mux; an empty `--metrics_endpoint` puts it on the HTTP
  endpoint instead. At v1.0 the flag defaults to `localhost:6963`
  (CLI.md §2.1), so a group that wants the second arrangement passes
  `--metrics_endpoint=` explicitly and never relies on the default. What is
  *counted* on that page, and what any metric or its labels are called, is not
  checked.
* **Log output.** Nothing checks what a program writes to its log files or to
  `glog`, at any severity. Diagnostics that reach a *client* — an HTTP body, a
  usage block on stderr — are a different thing and are checked where a document
  fixes them.

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

You are building the whole stack from nothing, so the order matters more here
than anywhere else. The codec is underneath everything: `tls` encodes the leaf,
the leaf is what is hashed, the hash is what proofs are about, and the same
encoding is the input to every signature. Get `tls` exactly right before you
write anything that depends on it, and check it against the field orders, tag
parameters and widths of TLS-CODEC.md §2, §3 and §5 rather than against your
own intuition. The verifier's byte vectors are not in the corpus; those rules
are what they were generated from, so deriving the encoding from them is the
whole of the answer.

After that the dependency order is: the forked `asn1` and `x509`, then the root
package's structures and enums, then `merkletree`, then the client and the
front end. The front end is the only part that needs a server, and it is checked
without a backend (section 3.4) — a `ct_server` that starts, loads a valid
config, refuses an invalid one, serves `get-roots` and shuts down cleanly satisfies
those groups whether or not anything behind it works.

Breadth beats depth here more than at any later step: eight programs must
build, and a package that does not compile silently fails every group that
imports it.

One note on section 7's table, because the names mislead if you read them on
their own. Four of the groups listed there are called `inherited-*`, and this
step inherits nothing. At v1.0 those four **establish** the baseline —
the JSON wire types, the RFC 6962 constants and enums, the fork's parse
behaviour, and the CTFE root pool and log ID — that steps 2 onwards will be
regressed against; the prefix records what they will mean later, not that they
re-check something you were handed. All four are **requirement** groups, not gate
groups, exactly as section 7's kind column shows. That column is the authority in
every case: the verifier reads a group's kind from its own manifest and never
from the group's name.

For the same reason, read section 7's description of the gate groups — "proof of
life … plus *inherited regression*" — with its second half struck out here.
Nothing precedes step 01, so there is no earlier milestone to regress
against and no gate group does it. At v1.0 the gate is proof of life
only: the tree compiles, `go build ./...` yields all eight commands, the flag
conventions hold on `ct_server`, and `ct_server` starts and serves the log its
configuration names. Everything else in section 7's table is **in scope**.
