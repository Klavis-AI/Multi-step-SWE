# TEST_INTERFACE — step 07 of 13 (certificate-transparency-go v1.0.21)

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

v1.0.21 brings the tree to **16** `package main` directories (CLI.md §1):
`goshawk` and `gosmin` arrive with `gossip/minimal`. Both exist at this
release and the next one only.

| program | directory | what a group does with it |
| --- | --- | --- |
| `ct_server` | `trillian/ctfe/ct_server/` | 17 flags, `--handler_prefix` among them; checked on the flag set, on path assembly under a prefix, and on start-up |
| `certcheck` | `x509util/certcheck/` | checked on its flag set, and on nothing else. It gains `system_roots` (Bool, default `false`) at v1.0.21, and a positional beginning `https://` is now **fetched from that site** rather than opened as a file — that fetch is out of scope (§9), the new flag is not (CLI.md §4.3) |
| `goshawk`, `gosmin` | `gossip/minimal/…` | built, enumerated, run for their flag grammar, **including each flag's type and default as printed in its own usage**, and run once more with no `--config` at all (CLI.md §4.10 gives the flags, the types, the defaults and the refusal). They are **not** symmetric: `gosmin` declares `config` alone, `goshawk` declares `config` plus the four scanner flags `batch_size`, `num_workers`, `parallel_fetch` and `start_index` |
| `migrillian` | `trillian/migrillian/` | **reworked at v1.0.21 and checked on the rework** (CLI.md §4.7). It stops being a single-log tool configured by flags and becomes config-file driven: `config`, `force_master`, `etcd_servers`, `lock_file_path` and `metrics_endpoint` (default `localhost:8099`) must be declared — five flags, as CLI.md §4.7 enumerates them, which is what takes v1.0.21's `migrillian` to eleven in total — and the eight per-log flags it had at v1.0.19 — `ct_log_uri`, `pub_key`, `trillian_uri`, `log_id`, `ct_batch_size`, `start_index`, `end_index`, `mirror` — must be gone. This is the one program in the table below whose command line is *not* "as at step 06" |
| the other eleven | see CLI.md §1 | as at step 06 |

Fifteen of the sixteen must **print their own usage**, and are run with
`--help` to check it. The command's **stdout and stderr are captured into one
buffer**, and that combined text must contain the literal `Usage of ` **and**
the command's own name — an unanchored substring test for that pair, nothing
more. You get both by leaving `flag.Usage` alone, so print any synopsis of
your own *after* that line rather than in place of it. `chainfix` is the one
exclusion: it reads `os.Args` positionally and never calls `flag.Parse`
(CLI.md §4.6), so it has no usage text to print, and it is required to be
present and executable only. The thirteen flag-driven commands the tree
already had are checked by a **gate** group; `goshawk` and `gosmin` by a
in scope one.

**On this `--help` path the exit status is out of scope.** A command that
writes its usage and then exits non-zero still passes, and a command that
writes nothing fails even if it exits **0**: only what was written is read as
evidence. The status is consulted at all only when the command wrote nothing,
and then only to word the failure. That tolerance is specific to `--help` —
on the rejected-flag path described next the status **is** checked and must be
exactly **2**. §6.1 item 4 restates both match rules, including where the
match is anchored and where it is not.

Every program in the tree loses one flag, and **thirteen of the sixteen are
checked for the loss**. Until v1.0.19 the root package registered a
`flag` global, so `allow_verification_with_non_compliant_keys` appeared on the
command line of every binary that linked it. At v1.0.21 it is gone: a group
passes it to a binary and requires the standard unrecognised-flag behaviour —
`Usage of <prog>:` on stderr, exit **2**, and the rejected flag's own name
somewhere in what the binary wrote. The sentence that name sits in is the
standard library's wording and is out of scope (§3.3). The thirteen checked are
the ones that exist at both v1.0.19 and v1.0.21 *and* have a flag
grammar:
`goshawk` and `gosmin` are excluded because they do not exist at
v1.0.19, and `chainfix` because it reads `os.Args` positionally and
never calls `flag.Parse` (CLI.md §4.6), so it has no flag grammar at any
release.

The flag-grammar rules of the previous steps are unchanged. The
unrecognised-flag rule is the one re-checked here, on every binary, by the
removal above; the `glog.Exit*`/`glog.Fatal*`/missing-required-flag exit codes
still hold (CLI.md's flag-grammar preamble) and are not re-asserted on the
binaries you inherited — they were checked where they were introduced. They
are asserted once on new ground: `goshawk` and `gosmin` are required to stop
with `glog.Exitf`'s exit **1** when they are started without a usable
`--config` (CLI.md §4.10) — a statement about two programs that do not exist
at v1.0.19 rather than a re-check. The **status** is what is checked
there; what either program prints on its way out is not.

## 5. Packages and symbols the verifier links against

| import path | what the drivers use |
| --- | --- |
| root package | `NewSignatureVerifier` and its key-compliance refusals, and the exported variable **`AllowVerificationWithNonCompliantKeys`** that replaces the flag — drivers set it, unset it, and require the documented behaviour either way (CLIENT-API.md §3) |
| `…/trillian/ctfe` | `SetUpInstance(ctx, opts)` in its two-argument form and the v1.0.21-only `SetUpMirrorInstance(ctx, opts, stor)`; `InstanceOptions` carrying `Config`, `Client`, `Deadline`, `MetricFactory`, `RequestLog`, `RemoteQuotaUser`, `CertificateQuotaUser`; the `STHGetter` interface with `LogSTHGetter` and `MirrorSTHGetter`; `MirrorSTHStorage` with its one method `GetMirrorSTH(ctx, maxTreeSize int64)`, and the `DefaultMirrorSTHStorage` / `DefaultMirrorSTHFactory` pair behind it; the v1.0.21-only free function `PingTreeHead`; `ValidateLogConfig` and the fields of `ValidatedLogConfig`, `PubKey` among them; `ValidateLogMultiConfig` and the named type its second result now has, `LogBackendMap`; and `DefaultRequestLog`, which a driver builds with `new(...)` — it is an empty struct with no required fields. **`GetTreeHead` is gone** and a driver that imports it must fail to build. CLIENT-API.md §8.1 and §8.3 give every signature, CONFIG.md §4.1 the two structs and `LogBackendMap` |
| `…/trillian/ctfe` — **receiver kinds** | Four of the types above are used in ways that do not compile unless the receiver kind matches, so this step states it: `GetSTH` hangs off ***pointer*** receivers — `func (*LogSTHGetter) GetSTH(context.Context) (*ct.SignedTreeHead, error)` and `func (*MirrorSTHGetter) GetSTH(…)`, so `*LogSTHGetter` and `*MirrorSTHGetter`, not the bare structs, are what satisfy `STHGetter`. The two defaults hang off ***value*** receivers — `func (DefaultMirrorSTHStorage) GetMirrorSTH(ctx context.Context, maxTreeSize int64) (*ct.SignedTreeHead, error)` and `func (DefaultMirrorSTHFactory) NewStorage(logID [sha256.Size]byte) (MirrorSTHStorage, error)` — so a driver may write `var s ctfe.MirrorSTHStorage = ctfe.DefaultMirrorSTHStorage{}` and may call the method directly on the composite literal, and both are checked |
| `…/trillian/ctfe` — **what set-up refuses** | At v1.0.21 `InstanceOptions` has no `Validated` field (CLIENT-API.md §8.2), so `SetUpInstance` and `SetUpMirrorInstance` are handed the raw `*configpb.LogConfig` and **validate it themselves** before they build anything: each returns `(nil, error)` for any config `ValidateLogConfig` would refuse — a zero `log_id` and a missing `private_key` on a non-mirror among them (CONFIG.md §4.3) — and, for a non-mirror, also when `roots_pem_file` is absent or names a file that cannot be read (CONFIG.md §2.4). Refusal is checked as a nil handler set and a non-nil error; no message text is checked. |
| `…/trillian/ctfe/configpb` | `override_handler_prefix`, `is_mirror`, `max_merge_delay_sec`, `expected_merge_delay_sec` — all new here — and, on the same `LogConfig`, the pre-existing `ext_key_usages`, `not_after_start` and `not_after_limit` whose validation *moves into* `ValidateLogConfig` at this release (CONFIG.md §2.4, §4.3). Also the whole-file messages `LogMultiConfig`, `LogConfigSet`, `LogBackendSet` and `LogBackend`, and the generated `Get<Field>()` accessor beside every field of all of them (CONFIG.md §1.2) |
| `…/x509`, `…/x509util` | the SIA and RFC 3779 surface: `SubjectTimestamps`, `SubjectCARepositories`, `RPKIAddressRanges`, `RPKIASNumbers`, `RPKIRoutingDomainIDs`, the extension constants `OIDExtensionSubjectInfoAccess` / `OIDExtensionIPPrefixList` / `OIDExtensionASList`, the address-family constants `IPv4AddressFamilyIndicator` and `IPv6AddressFamilyIndicator` (which is what a driver compares an `AddressFamily` against, rather than a bare number), and the `IPAddressPrefix` / `IPAddressRange` / `IPAddressFamilyBlocks` / `ASIDRange` / `ASIdentifiers` types **by their field names** (X509-FORK.md §5.3); the `ExtKeyUsage` constants that `ext_key_usages` names map onto, `ExtKeyUsageAny` / `ExtKeyUsageServerAuth` / `ExtKeyUsageClientAuth` / `ExtKeyUsageOCSPSigning` among them (CONFIG.md §2.4); the new curve secp192r1 with `OIDNamedCurveP192` and `OIDFromNamedCurve` (X509-FORK.md §5.7); and `IsFatal`, the exported fatal/non-fatal predicate (X509-FORK.md §3.1), which the drivers use wherever a parse may return a certificate and an error together |
| `…/gossip/minimal/x509ext` | `LogSTHInfo` and its TLS tags, `LogSTHInfoFromCert`, `HasSTHInfo`, `STHFromCert`, and the two OIDs. `LogSTHInfoFromCert` **rejects trailing bytes** after the encoded structure, which is narrower than the codec's own rule — TLS-CODEC.md §1 |
| `github.com/google/trillian/…` (the pinned dependency, §2) | the two collaborators `InstanceOptions` needs and the product does not supply: `monitoring.InertMetricFactory{}` from `.../trillian/monitoring`, the do-nothing `MetricFactory` used so a driver can build an instance without publishing anything, and `keyspb.PrivateKey` / `keyspb.PublicKey` from `.../trillian/crypto/keyspb`, the concrete messages that go inside a config's `private_key` / `public_key` `Any` (CONFIG.md §2.3). From `github.com/golang/protobuf/ptypes`, the two helpers that put a message into an `Any` and take it out again: `MarshalAny`, and the `DynamicAny` that `ValidatedLogConfig.PrivKey` is at this release, whose `Message` field holds the unwrapped `*keyspb.PrivateKey` (CONFIG.md §4.1). Their shapes are fixed by the pinned `go.mod`, not by this product |

Nothing in this section is inherited by reference. Your workspace carries this
`TEST_INTERFACE.md` and no earlier one, so the table above and the table below
are together the whole of the surface: every import path a driver at this step
links is a row in one of them, and a path in neither is not part of it. The
table below is the half that predates v1.0.21 — the packages and symbols
this step's groups name **directly** without this milestone having changed
them — written out rather than referred to, and each one re-checked against
v1.0.21 before being listed. Nothing in the fork is withdrawn at
v1.0.21, so every package either table names must **resolve**; the one
thing at this milestone that must not is a symbol rather than a package,
`ctfe.GetTreeHead`, which the second row of the first table states.

| import path | what a driver at this step names |
| --- | --- |
| `…` (root), beyond the **root package** row of the first table | the RFC 6962 wire surface: `MerkleTreeLeaf` with `Version`/`V1`, `TimestampedEntry` and `TimestampedEntryLeafType`, `X509LogEntryType` and `PrecertLogEntryType`, `ASN1Cert`, `CertificateChain`, `DigitallySigned`, `CTExtensions`, `LogID`, `SHA256Hash`, `SignedCertificateTimestamp` and `SignedTreeHead`; the builders and the hash `CreateX509MerkleTreeLeaf`, `MerkleTreeLeafFromChain` and `LeafHashForLeaf`, beside the `TreeLeafPrefix` byte a driver prepends by hand to check the last of them; `TreeHeadSignature` with `TreeHashSignatureType`, which is what an STH signature is verified over; `PublicKeyFromPEM` beside that row's `NewSignatureVerifier` and the `*SignatureVerifier` it returns; the JSON bodies `AddChainRequest`, `AddChainResponse`, `GetSTHResponse` and `GetRootsResponse`; and the endpoint constants `AddChainPath`, `AddPreChainPath`, `GetSTHPath`, `GetSTHConsistencyPath`, `GetProofByHashPath`, `GetEntriesPath`, `GetEntryAndProofPath` and `GetRootsPath` — every URL a group requests is assembled out of those rather than spelled, so their values are HTTP-API.md §1's and their spellings are fixed here |
| `…/tls` | `Marshal` and `Unmarshal` over the structures in the row above, in both directions, with `Unmarshal`'s trailing-bytes return read rather than discarded; `DigitallySigned` and `SignatureAndHashAlgorithm` with the `SHA256` and `ECDSA` constants; `Enum`, the type a TLS-wire enumeration widens to; and `VerifySignature(pubKey, data, DigitallySigned)`, which an STH signature is checked with |
| `…/asn1` | `ObjectIdentifier` — **the fork's**, not `encoding/asn1`'s. `x509ext.OIDExtensionCTSTH` and `x509ext.OIDExtKeyUsageCTMinimalGossip` are values of this type and `x509.Certificate.UnknownExtKeyUsage` is a slice of it, so a driver that puts an unknown extended key usage on a template names this package and no other. The two are not interchangeable |
| `…/x509/pkix` | `Extension` with `Id`, `Critical` and `Value`, and `Name`, from which certificate templates are built. Again the fork's `pkix`, never `crypto/x509/pkix`: §2's directory rule applies to it as to the rest of the fork |
| `…/x509`, beyond the first table's `…/x509`, `…/x509util` row | `ParseCertificate` and `CreateCertificate`; `*Certificate` with `ExtraExtensions`, `Extensions`, `UnknownExtKeyUsage` and `RawSubjectPublicKeyInfo` (read to compare a P-192 key's SPKI bytes against the standard library's); `MarshalPKIXPublicKey` and `ParsePKIXPublicKey`; `NonFatalErrors`, the concrete type a driver type-asserts an error to where it wants the non-fatal arm by name rather than through `IsFatal`; `Certificate.Verify` with `VerifyOptions`, `CertPool` and `NewCertPool`, and `ExtKeyUsage` with `ExtKeyUsageAny` as the `KeyUsages` that options value carries; `KeyUsageCertSign`; `OIDSubjectInfoAccessTimestamp` and `OIDSubjectInfoAccessCARepo`, the two access-method OIDs read out of an SIA extension; and `OIDNamedCurveP224`, `OIDNamedCurveP256`, `OIDNamedCurveP384` and `OIDNamedCurveP521` beside the new `OIDNamedCurveP192`, which a driver requires to be five distinct values |
| `…/x509util` | `CertificateToString(*x509.Certificate) string`, the human-readable rendering a driver reads a secp192r1 subject public key back out of |
| `…/trillian/ctfe`, beyond the first table's three rows | `PathHandlers`, the named `map[string]AppHandler` that `SetUpInstance` and `SetUpMirrorInstance` both return a **pointer** to, and which a driver ranges over to learn which endpoints an instance registered and mounts to serve one; `LogConfigFromFile`, which reads a `LogConfigSet` file into `[]*configpb.LogConfig`; and `NewPEMCertPool` with `*PEMCertPool`, which is the positive control in the build probe that measures `GetTreeHead`'s withdrawal — it has to build at every release for the failure of the `GetTreeHead` probe to mean anything |
| `…/client` and `…/jsonclient` | `client.New(uri string, hc *http.Client, opts jsonclient.Options) (*LogClient, error)` with `jsonclient.Options`' `PublicKeyDER` field, and `(*LogClient).AddChain(ctx, []ct.ASN1Cert) (*ct.SignedCertificateTimestamp, error)`. The end-to-end gate submits through the product's own client rather than by hand, so both must build **and** must work against a live `ct_server` |
| `…/ctutil` | `VerifySCT(pubKey crypto.PublicKey, chain []*x509.Certificate, sct *ct.SignedCertificateTimestamp, embedded bool) error`, which the SCT a live `ct_server` hands back is checked with |
| `github.com/google/trillian` | **not yours to write** — §3.4: the log backend is out of scope. It is named because it must keep *resolving*. `TrillianLogServer`, which the **benchmark** implements and registers with `RegisterTrillianLogServer`; `NewTrillianLogClient` and the `TrillianLogClient` interface, which is the declared type of `ctfe.InstanceOptions.Client`; `LogLeaf`, `QueuedLogLeaf`, `SignedLogRoot` and `Proof`; and the request/response pair of **every** method of that interface — the stub implements the whole of it, `QueueLeaf` and `QueueLeaves`, `GetLatestSignedLogRoot`, `GetLeavesByRange`, `GetLeavesByIndex` and `GetLeavesByHash`, `GetInclusionProof` and `GetInclusionProofByHash`, `GetConsistencyProof`, `GetEntryAndProof`, `AddSequencedLeaf` and `AddSequencedLeaves`, `GetSequencedLeafCount` and `InitLog` — so that a group can point your `ct_server` at something that answers |
| `github.com/google/trillian/crypto/keys/der/proto` | imported for its **side effect only**, as `_`, exactly as `ct_server`'s own `main` does: it registers the `keyspb.PrivateKey` proto handler, without which the key loader behind `SetUpInstance` cannot build a signer from a DER key. A driver names this path and no symbol inside it, which is still a link: drop it from the module graph and the group fails to build |
| `github.com/golang/protobuf/proto` | `proto.Message` and `proto.Marshal`, with which a driver serialises a `configpb.LogConfig` one field at a time. That is how the four new fields of the first table's `…/trillian/ctfe/configpb` row are shown to be real generated protobuf fields carrying the wire tags CONFIG.md §1 fixes, rather than plain struct members that happen to have the right names |
| `github.com/golang/protobuf/ptypes/any` and `github.com/golang/protobuf/ptypes/timestamp` | `any.Any`, the declared type of `LogConfig`'s `private_key` and `public_key`, and `timestamp.Timestamp`, the declared type of `not_after_start` and `not_after_limit` (CONFIG.md §1). Both are built directly in a driver, `Timestamp` included with a `Nanos` outside `[0, 1e9)`, so that an instant no clock can represent can be offered to `ValidateLogConfig` and refused |
| `google.golang.org/grpc` | `NewServer` and `*Server`, which stand the §3.4 stub up on a loopback socket the group binds for itself, and `DialContext` with `WithInsecure` and `WithBlock`, which is where the `trillian.TrillianLogClient` handed to `InstanceOptions.Client` comes from. The gRPC server is the benchmark's, never yours, and no driver speaks gRPC to your front end |

The last five rows, together with the `github.com/google/trillian/…` row in the
first table, are **third-party modules and not part of your deliverable**.
Nothing in them is checked as work of yours: you neither write that code nor
choose its versions — `go.mod` and `go.sum` are benchmark-owned and are
replaced with the pinned copies before any build (§2), which fix
`github.com/google/trillian` at **v1.2.1**, `github.com/golang/protobuf` at
**v1.3.1** and `google.golang.org/grpc` at **v1.20.1**. They are written down
only so that what must keep **resolving** is visible: a driver is compiled in a
throwaway module beside your tree, so a dependency your manifest stops
providing, or provides at another version, takes that driver's whole group down
with it.

One collision is worth spelling out, because the two names differ by a prefix
the eye skips. The `…/trillian/ctfe` rows expand, by §2's directory rule, to
`github.com/google/certificate-transparency-go/trillian/ctfe` — the **fork's
own** `trillian/` directory, code you own and are checked on, and likewise
`github.com/google/certificate-transparency-go/trillian/ctfe/configpb`.
`github.com/google/trillian` is a different module altogether, upstream, which
you do not own and which is out of scope. Read one for the other and half of this
section inverts.

## 6. Formats crossing the boundary

Step 06's `TEST_INTERFACE.md` is replaced by this file, so the list is given
here in full rather than referred to. It is **not** step 06's list copied
forward: every entry below was re-checked against *this* step's group drivers
at v1.0.21 before it was written down. One entry has **inverted** since the
step you came from and is marked where it sits; one family step 06 dropped is
**back** at this release and is restated; and §6.2 names the families earlier
steps carried that nothing here compares — so that you do not reconstruct a
contract from memory and defend a surface this step does not hold you to.

**Carried forward from the steps behind you, restated.**

* **TLS encodings.** `MerkleTreeLeaf`, `TimestampedEntry`, `DigitallySigned`,
  `CertificateChain`, the SCT signature input and the `TreeHeadSignature` input
  are compared **byte for byte** against vectors the *verifier* holds. Those
  vectors are not in `ct-spec/` and you cannot read them; they were produced
  from the field orders, tag parameters and widths of TLS-CODEC.md §2, §3 and
  §5, which determine the encoding completely. Re-derive an encoding from those
  rules and it will match; guess at one and it will not. The codec does not move
  at v1.0.21 and this step leans on it in several places. A variable-length
  vector carries a big-endian length prefix whose width comes from the declared
  maximum and not from the value, so four bytes under a 24-bit maximum encode as
  `000004deadbeef` and the same four under a 16-bit maximum as `0004deadbeef`. A
  `CertificateChain` is a **vector of vectors**, not a concatenation: two
  entries of one and two bytes encode as `000009000001aa000002bbcc`. A
  `digitally-signed` structure is hash byte, signature byte, then the signature
  as its own 16-bit vector — `04030004deadbeef` for sha256(4)/ecdsa(3) over four
  bytes. A buffer that ends inside a declared length is an **error**, never a
  short read. And a `MerkleTreeLeaf` survives a marshal/unmarshal round trip
  with the original bytes back and **nothing left over**. This step's new
  `LogSTHInfo` extension rides on the same codec.
* **JSON bodies.** Field names, types and base64 padding exactly as
  HTTP-API.md §3 and §4 give them. Binary fields are standard base64 with
  padding; an empty list is `[]`, never `null`. The responses a group decodes at
  *this* step are `get-sth` (`tree_size`, `timestamp`, `sha256_root_hash`,
  `tree_head_signature`), `get-roots` (`certificates`) and `add-chain`
  (`sct_version`, `id`, `timestamp`, `extensions`, `signature`).
  **This bullet has inverted since step 06.** Step 06 told you that a response
  is read field by field and that nothing compares a whole JSON document as
  text. That is no longer true here: the mirror group compares the **entire**
  `get-sth` body as one string, so field order and separator bytes are checked
  for that one response — see §6.1 item 2. Every other response at this step is
  still read field by field.
* **Leaf hashes.** `SHA-256(0x00 ‖ TLS(MerkleTreeLeaf))`, compared as bytes
  (MERKLE.md §1), for an `x509_entry` and for a `precert_entry` against pinned
  digests, with a third case that recomputes the pre-image from
  `ct.TreeLeafPrefix` and requires the same answer — so the domain-separation
  byte is checked and not merely assumed. The same hash is what the front end
  must put in the leaf it queues: two groups take the `trillian.LogLeaf` their
  own stub backend received from a real `add-chain` and require its
  `LeafValue` to be the RFC 6962 leaf for that submission, and verify the
  returned SCT with `ctutil.VerifySCT` over the chain with `embedded` **false**
  — so the non-embedded pre-image route is still exercised, as a means.
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
  with the prefix normalised, exactly as HTTP-API.md §1.1 gives it. Still checked
  here as the same verdicts step 06 listed: the prefixed endpoint answers, the
  **unprefixed** `/ct/v1/get-sth` is **404**, an undefined endpoint *under* the
  prefix is **404**, and of the paths no handler claims only the exact path `/`
  answers **200** while every other one answers **404**. `/` is never prefixed.
  v1.0.21 puts a second, global prefix in front of all of that; the new
  bullet below states it.
* **PEM, and how a private key reaches a signer.** Root certificate files are
  given to `ct_server` by path, as `roots_pem_file`, and what a group compares
  is the DER set `get-roots` serves against the DER in the file that log's own
  config named — as a **sorted set** of base64 blobs, so serving a superset
  fails as surely as serving the wrong set. The log's **private key** is a
  `google.protobuf.Any` matched against the key-handler registry `ct_server`
  populates at start-up (CONFIG.md §2.3); the loader must never open the path
  itself. As at step 06, and unlike step 04, every configuration built at *this*
  step inlines the key as a **`keyspb.PrivateKey` carrying raw DER**,
  unencrypted, with no path and no password anywhere, and names the public half
  as a `keyspb.PublicKey` beside it. That is a change of corpus, not of
  contract — CONFIG.md §2.3's "Do not assume either one" is the rule, both
  handlers stay registered, and routing `private_key` through the registry is
  the only part of this that is yours to write.
* **The configuration file's encoding.** The log config is a protocol-buffer
  message, and through v1.1.0 it is parsed as **text proto and nothing else**
  (CONFIG.md §4.2), so the JSON rendering of a valid message is a parse failure
  at v1.0.21 whatever it contains. Every configuration written at this step
  is text proto, reaching your code either through `ct_server --log_config` or
  through an in-process call. **Unknown field names are refused**, and that half
  is checked here: a group parses a `LogConfig` text block carrying all four of
  this release's new fields and requires it to load, requires a block naming a
  field no release has to be **refused**, and requires an empty config set to be
  refused too. The JSON-refusal half is stated by CONFIG.md and is *not*
  exercised at this step — no group feeds a JSON config — so §6.2 lists it.
* **The multi-backend configuration message — back at this release.** Step 06
  ran one message shape only and said so. v1.0.21 checks both again, though
  not through the flag: `LogMultiConfig` with its `LogBackendSet` of
  `LogBackend { name, backend_spec }` and a `LogConfigSet` whose each `config`
  names its `log_backend_name` is built in process and handed to
  `ctfe.ValidateLogMultiConfig`, whose second result is the named type
  `ctfe.LogBackendMap` — a map **keyed by backend name**, one entry per declared
  backend, each carrying the `backend_spec` it was given back unaltered. A
  backend spec is an opaque non-empty string and is never dialled by that
  validator. The `--log_rpc_server` half of step 04's entry is still one shape
  only here: every group that starts a real personality passes
  `--log_rpc_server`, so `--log_config` on the command line is always read as a
  `LogConfigSet` (CLI.md §2.2).
* **The forked verifier's chain admission.** Pinned PEM fixtures decode to the
  certificates they name; a leaf verifies to the bench root **only** through the
  intermediate, and neither without the intermediate nor without the root; the
  validity window is enforced at a pinned instant, so an expired and a
  not-yet-valid leaf both fail while the in-window one passes; a leaf under an
  untrusted anchor is refused by the bench pool and accepted by the pool that
  does contain its issuer; and `CheckSignatureFrom` follows the chain rather
  than the trust pool. The precertificate poison extension is **critical**,
  carries DER `NULL` — the two bytes `0500` — and comes back in
  `UnhandledCriticalExtensions` as `1.3.6.1.4.1.11129.2.4.3`. Over the wire, the
  same rules are what makes `add-chain` refuse an unrooted submission **without
  reaching the backend**: the refusal is checked as a non-2xx together with a
  backend call count of zero.
* **Object identifiers as text.** Extension and algorithm OIDs are compared as
  the dotted decimal strings `asn1.ObjectIdentifier.String()` produces —
  never as byte slices and never by a name. §6.1 lists every one of them.
* **One algorithm rendering.** `x509.PublicKeyAlgorithm.String()` renders `RSA`
  and `ECDSA`, and both spellings are compared inside larger assertions about a
  parsed key.
* **Removal observed by compilation.** A withdrawn name is observed the way the
  toolchain observes it, not with `ls` and not by reading a source file: a probe
  using `ctfe.GetTreeHead` must **fail to build** and the build output must
  **name that symbol**, guarded on both sides — a positive control probe using
  names `ctfe` has at every anchor must build, a negative control using a name
  no anchor has must fail naming *it*, and a probe using `ctfe.PingTreeHead`
  with its v1.0.21 signature must build. Deleting is necessary; leaving a
  stub that still compiles fails the group. v1.0.21 also removes a **flag**,
  and that removal is observed the same way — from outside, by running the
  binary — which is §6.1 items 4 and 5.

And, new at v1.0.21:

* **Assembled URL paths.** `<handler prefix> + "/" + <log prefix> +
  <endpoint path>`, with the per-log override normalised by trimming slashes
  and re-adding one leading slash, so `logs`, `/logs` and `/logs/` are the
  same thing. The **per-log `prefix` is normalised the same way**, inside
  `trillian/ctfe` rather than in `ct_server`, so `bench`, `/bench` and
  `/bench/` all serve `/bench/ct/v1/…` and a driver that calls
  `SetUpInstance` directly sees it (HTTP-API.md §1.1). `/` and `/metrics` are
  **never** prefixed.
* **Prometheus exposition on `/metrics`.** The `logid`-labelled gauges
  `max_merge_delay`, `expected_merge_delay` and `known_logs`, read back from a
  running front end and compared against the configured values
  (CONFIG.md §2.2).
* **SIA and RFC 3779 extensions.** Pinned DER carrying timestamps, CA
  repositories, address blocks and AS-number blocks; what is compared is the
  parsed structure, and an empty SIA extension is the non-fatal
  `x509: empty SubjectInfoAccess extension`. The two families are **not**
  treated alike when the DER is broken, and both sides of that asymmetry are
  checked (X509-FORK.md §3.1, §5.3):
  * **SIA is strict.** Only the *empty* extension is non-fatal. Content that
    does not decode — including a byte left over after the SIA `SEQUENCE` —
    is a **fatal** parse error and no certificate comes back. Trailing data is
    fatal inside an extension body for the same reason it is fatal after the
    certificate.
  * **RPKI is lenient.** A malformed `IPAddrBlocks` or `ASIdentifiers` body,
    trailing data included, goes to `NonFatalErrors`: the certificate parses,
    `IsFatal` is false, and the corresponding field is left **`nil`** — not
    zero-length, and not the partially decoded structure (X509-FORK.md §3.1).
* **SIA emission, and what a parse quietly drops.** Supporting the extension
  is emission as well as parsing, so a driver round-trips a template through
  `CreateCertificate` and back through `ParseCertificate` and compares the
  extension **byte for byte** against DER it builds itself with the Go
  standard library. What is fixed:
  * a template carrying `SubjectTimestamps`, `SubjectCARepositories` or both
    yields **exactly one** extension identified by `1.3.6.1.5.5.7.1.11`, and
    it is **not critical**;
  * its `extnValue` is RFC 5280's `SubjectInfoAccessSyntax` — a
    `SEQUENCE OF AccessDescription`, each `AccessDescription` a
    `SEQUENCE { accessMethod OBJECT IDENTIFIER, accessLocation GeneralName }`.
    Every `SubjectTimestamps` entry comes first, under `1.3.6.1.5.5.7.48.3`,
    then every `SubjectCARepositories` entry, under `1.3.6.1.5.5.7.48.5`, and
    within each list the template's order is preserved. Each location is a
    `uniformResourceIdentifier`: context-specific tag **6**, primitive,
    carrying the string's bytes and nothing else;
  * a template carrying **neither** field yields **no** SIA extension at all;
  * coming back in, the parser harvests only what it has a field for and is
    silent about the rest. An `AccessDescription` whose location is any
    `GeneralName` other than a `uniformResourceIdentifier` is **skipped**, and
    one whose `accessMethod` is neither of the two OIDs above is **ignored** —
    neither is an error, neither is a non-fatal diagnostic, and the entries
    on either side of it are still collected. The strictness above is about
    DER that will not decode, not about content this release has no field for.
* **RFC 3779 DER, as the fixtures encode it.** `ct-spec/rfc/` does not ship
  RFC 3779, and the two extension bodies a group feeds in are hand-built, so
  the grammar they use is stated here:
  * `sbgp-ipAddrBlock` (`1.3.6.1.5.5.7.1.7`) carries
    `IPAddrBlocks ::= SEQUENCE OF IPAddressFamily`, where `IPAddressFamily`
    is `SEQUENCE { addressFamily OCTET STRING (SIZE(2..3)), ipAddressChoice }`
    — the octet string being the two-byte AFI big-endian, optionally followed
    by one SAFI byte. `ipAddressChoice` is either `NULL`, meaning inherit from
    the issuer, or `SEQUENCE OF IPAddressOrRange`; each element of that is
    either a `BIT STRING` (a prefix — the leading octets of the address, the
    `BIT STRING`'s bit length being the prefix length) or a
    `SEQUENCE { min BIT STRING, max BIT STRING }` (a range).
  * `sbgp-autonomousSysNum` (`1.3.6.1.5.5.7.1.8`) carries
    `ASIdentifiers ::= SEQUENCE { asnum [0] EXPLICIT ASIdentifierChoice
    OPTIONAL, rdi [1] EXPLICIT ASIdentifierChoice OPTIONAL }`, each
    `ASIdentifierChoice` either `NULL`, meaning inherit, or
    `SEQUENCE OF ASIdOrRange`; each element of that is either an `INTEGER`
    (one AS number) or a `SEQUENCE { min INTEGER, max INTEGER }` (a range).
  * Both decode into the `x509/rpki.go` types X509-FORK.md §5.3 lists, and
    the inherit alternative sets `InheritFromIssuer` with the slices beside
    it left empty.
* **Pinned DER whose parse is non-fatal.** A group runs a fixed table of
  fixtures through `ParseCertificate` and requires an exact
  (`IsFatal`, error-present, certificate-returned) triple for each. Three rows
  are easy to get the wrong way round and are called out here (X509-FORK.md
  §2.2, §3.1):
  * an **`ExtendedKeyUsage` extension with a zero-length `extnValue`** is
    non-fatal: the certificate is returned, with both `ExtKeyUsage` and
    `UnknownExtKeyUsage` empty. Contrast an `extnValue` that *is* present but
    contains a malformed OID, which stays fatal — the demotion is of the empty
    body, not of bad content;
  * an **RSA `SubjectPublicKeyInfo` whose `AlgorithmIdentifier` parameters are
    anything other than `NULL`**, absent included, is non-fatal: the key is
    still decoded as an RSA key;
  * a **`PrintableString` carrying characters outside the PrintableString
    set** in the subject is not an error at all at v1.0.21 — no error, and
    `IsFatal` false — because `asn1` recovers such a string as ISO 8859-1 /
    T.61 unconditionally. The `lax` tag that makes the same recovery
    *conditional* does not exist until v1.1.0, so there is nothing to opt into
    here.
  The same table carries rows that stay **fatal** at v1.0.21 and must not
  be demoted along with the three above: an INTEGER that is not minimally
  encoded, and a **negative RSA modulus** in the subject public key. The lax
  retry that recovers those arrives at v1.1.0 (X509-FORK.md §3.2); at
  v1.0.21 each returns a nil certificate and a fatal error.
* **An STH inside an X.509 extension.** `LogSTHInfo` is TLS-encoded
  (TLS-CODEC.md §2) and carried in an extension identified by
  `1.3.6.1.4.1.11129.2.4.5`; round-tripping it and recovering a
  `ct.SignedTreeHead` from a certificate is checked against pinned bytes.
  `LogSTHInfoFromCert` is one of the callers that **does** treat a non-empty
  remainder as an error: the extension value must be exactly one encoded
  `LogSTHInfo` and nothing else, so a truncated body and a body with one extra
  byte are both refused. The codec itself is looser — it hands the remainder
  back and lets the caller decide (TLS-CODEC.md §1) — and this caller decides
  against it.
  A certificate carrying **no** such extension is likewise an **error** from
  both `LogSTHInfoFromCert` and `STHFromCert` — not an empty result and not a
  nil-nil pair; `HasSTHInfo` is the non-erroring way to ask. `STHFromCert`
  lifts the recovered structure into a `ct.SignedTreeHead`, and what is checked
  of the result is exactly the five values the extension carries — version,
  tree size, timestamp, root hash and the tree-head signature.
* **Mirror configuration.** A mirror log requires a `public_key` and
  **forbids** a private key: `unnecessary private key for mirror`.

### 6.1 The strings this step compares whole

Section 3.3 sets its no-standard-library-wording rule aside only for what this
section lists, so here is the list, audited group by group. It has **ten**
entries.

1. **A log's handler path set, joined whole.** `SetUpInstance` and
   `SetUpMirrorInstance` return a map of registered paths; a group sorts its
   keys ascending, joins them with **single spaces**, and compares the result
   against one fixed string. For a log that is the eight paths

       /bench/ct/v1/add-chain /bench/ct/v1/add-pre-chain
       /bench/ct/v1/get-entries /bench/ct/v1/get-entry-and-proof
       /bench/ct/v1/get-proof-by-hash /bench/ct/v1/get-roots
       /bench/ct/v1/get-sth /bench/ct/v1/get-sth-consistency

   (one line, spaces only, wrapped here for the page), and for a mirror it is
   the same list **less the two `add-` paths**. Because the whole set is one
   string, an extra registration fails as surely as a missing one, and the
   endpoint spellings of HTTP-API.md §1 are checked here even though no request
   is sent. The same comparison is repeated for the prefix spellings `bench`,
   `/bench` and `/bench/`, which must all produce that identical string.

2. **A mirror's `get-sth` body, whole.** This is the one whole-JSON comparison
   in the package at this step, and it is why the inherited JSON bullet above is
   marked as inverted. The body must be exactly

       {"tree_size":<n>,"timestamp":<n>,"sha256_root_hash":"<b64>","tree_head_signature":"<b64>"}

   — those four names, in that order, no spaces, standard base64 with padding
   for the two byte fields — for the STH the driver's own storage handed back.
   Alongside it the response's `Content-Type` is compared as a whole header
   value against `application/json`.

3. **The Prometheus gauge names and their one label.** A group reads
   `/metrics` and looks for lines beginning exactly
   `<name>{logid="<id>"}` for `known_logs`, `max_merge_delay` and
   `expected_merge_delay`, taking the value off the end. The metric names, the
   label name `logid`, the braces and the quoting are all checked; a gauge
   exported under another name, or labelled by anything other than `logid`,
   reads as `ABSENT`, which is the value the control case requires for a log id
   no configuration named.

4. **`Usage of ` and the program's own name.** Every flag-driven command in the
   tree is run with `--help` and its combined output must contain the literal
   `Usage of ` **and** the command's own name — that pair is how "the binary
   named itself" is checked. You get both by leaving `flag.Usage` alone and you
   lose them by installing a usage function of your own, so if you want a
   synopsis, print it **after** that line. A separate group requires the same
   banner on the *rejected-flag* path with the match **anchored**: `^Usage of `,
   at the start of a line. Two things about how this is read are contract and
   are easy to trip over: on the `--help` path the **exit status is not read**,
   so a command that prints its usage and then exits non-zero passes and a
   command that prints nothing at all fails (it reads as `no-usage`, which is
   what makes the negative controls mean anything); on the rejected-flag path
   the status **is** checked and must be **2**.

5. **A flag's declaration line, and the `(default …)` printed under it.** A
   declaration is two spaces, `-`, the name, and — for every type except
   `bool` — a space and the type word; a line not starting with exactly two
   spaces and a dash is not seen as a declaration at all, and the default is
   read from a `(default …)` that **ends** a following line. What is compared
   whole at this step: the six gossip flags' types and defaults, as
   `config=string/- batch_size=int/1000 num_workers=int/2 parallel_fetch=int/2
   start_index=int/-` for `goshawk` and `config=string/-` for `gosmin`; and
   presence reports naming flags one at a time for `ct_server`, `migrillian`
   and `certcheck`. On the removal path the rejected flag's **own name with its
   leading dash** — `-allow_verification_with_non_compliant_keys` — must appear
   somewhere in what the binary wrote, and the controls require
   `^  -quota_remote` to appear when an existing flag is accepted and
   `-this_flag_has_never_existed` when an unknown one is not. **No flag
   description is ever compared** — the type word and the `(default …)` are
   read off the line and the sentence between them is not looked at.

6. **Two lines of `x509util.CertificateToString`.** The rendering is filtered to
   the lines X509-FORK.md §5.7 fixes — the ones beginning `Public Key:` and
   `ASN1 OID` — each trimmed of leading space, and the pair must be exactly

       Public Key: (192 bit)
       ASN1 OID: secp192r1

   for the secp192r1 fixture. Nothing else in that block is compared; its
   header lines are not fixed by any document and are not read.

7. **Object identifiers, as dotted decimal text.** Compared through
   `asn1.ObjectIdentifier.String()`. The complete set at this step:
   the five named curves `1.2.840.10045.3.1.1` (P-192), `1.3.132.0.33` (P-224),
   `1.2.840.10045.3.1.7` (P-256), `1.3.132.0.34` (P-384) and `1.3.132.0.35`
   (P-521); the SIA extension and its two access methods,
   `1.3.6.1.5.5.7.1.11`, `1.3.6.1.5.5.7.48.3` and `1.3.6.1.5.5.7.48.5`; the two
   RFC 3779 extensions `1.3.6.1.5.5.7.1.7` and `1.3.6.1.5.5.7.1.8`; the two
   gossip OIDs `1.3.6.1.4.1.11129.2.4.5` (the STH extension) and
   `1.3.6.1.4.1.11129.2.4.6` (the minimal-gossip EKU, which must also come back
   in `UnknownExtKeyUsage` spelled that way); and the precertificate poison
   `1.3.6.1.4.1.11129.2.4.3`, which must appear in
   `UnhandledCriticalExtensions`. Renumbering any of the four existing curves
   fails as surely as omitting P-192.

8. **Two algorithm names and two curve names.** `x509.PublicKeyAlgorithm`
   renders `RSA` and `ECDSA`, and both appear inside larger assertions about a
   parsed key. The curve names `"P-192"` and `"P-256"` are read off
   `elliptic.Curve.Params().Name` and compared with their quotes; those come
   from the Go standard library, not from you, but a curve table that hands back
   the wrong `elliptic.Curve` is caught by them.

9. **The toolchain's build error naming a symbol.** The withdrawal group
   requires a failed build whose output contains `ctfe.GetTreeHead`, and its
   negative control requires a failed build naming
   `ctfe.NoSuchFunctionAtAnyAnchor`. That is the compiler's output quoting a
   name from your tree, and it is the whole of the withdrawal proof: an
   emptied-out function that still compiles does not produce it.

10. **The `ext_key_usages` name vocabulary** (CONFIG.md §2.4). `ServerAuth`,
    `ClientAuth` and `OCSPSigning` are written into a config and must be
    recognised; a name no release has must make the whole config **refused**.
    These are strings your code *reads*, not strings it prints — what the
    assertion compares is the `x509.ExtKeyUsage` values that come out, in the
    order written, so the vocabulary is checked by acceptance and refusal rather
    than by any rendering. An **empty** list is not "no filter" at this release:
    it must be rewritten to the one-element slice `[ExtKeyUsageAny]`.

**That list is exhaustive.** No other output of any program, and no other
string your code produces, is compared against a fixed string at this
milestone. In particular **no x509 diagnostic is ever compared**: the bullets
above name `x509: empty SubjectInfoAccess extension` and
`unnecessary private key for mirror` so that you know which condition is meant,
but no driver matches either, and both are checked as *a non-fatal error was
attached* and *the config was refused with a nil result*. Six kinds of
comparison look like the list above and are not:

* **Values the harness itself supplied, handed back.** The stub backend's
  pinned tree size, timestamp, root hash and signature bytes; the log prefixes
  `bench` and `oldlog`; the handler prefix and the per-log override; the backend
  names and the specs `127.0.0.1:1` and `127.0.0.1:2`; the log ids; the merge
  delays; the `not_after` window. Those are the group's own values on a round
  trip, and comparing them proves they were carried without alteration.
* **Renderings the *driver* composes from your parsed values.** `fatal=… err=…
  cert=…` verdict rows, `0a/8,c0a8/16` and `ac10/16-ac1f/16` for recovered
  address prefixes and ranges, `64496,64511` and `64500-64510` for AS numbers,
  `IPv4=1 IPv6=2` for the two address-family constants, `serving=200 exit=0
  listener-closed=true` for a signalled shutdown, `[1 2]` for a parsed EKU list.
  The format is the driver's; only the values inside it are yours.
* **Byte slices compared as bytes or as hex of bytes.** The TLS vectors, the
  leaf hashes, the SIA `extnValue` compared against DER the driver builds with
  the Go standard library's `encoding/asn1`, the `LogSTHInfo` encoding, the
  re-marshalled SPKI, the DER `NULL` `0500` of the poison extension. Nothing in
  those is sensitive to how anything is spelled.
* **The fixture corpus's own names.** `Harbor CT Bench Root`,
  `Harbor CT Bench Intermediate`, `valid.bench.invalid`, `*.google.com` and the
  pinned serial numbers are what the verifier's certificates contain; they are
  the input, not your output.
* **The backend's and the dependency's identifiers.** `trillian.LogLeaf` field
  names, `keyspb.PrivateKey` rendered as a Go type name, the gRPC methods a
  driver's stub records. Those belong to the pinned dependency.
* **Exit statuses and HTTP statuses, which are numbers.** Exit **2** for a
  rejected flag, **1** for `goshawk` or `gosmin` started without a usable
  `--config`, **0** after `SIGTERM` or `SIGINT`; 200, 400 and 404 over the wire.
  They are checked, and nothing about the text printed beside them is.

### 6.2 What earlier steps listed and this one does not

Named because you no longer have those files, and because reconstructing the
list from memory would have you defending surfaces nothing here measures.

* **`get-entry-and-proof` responses**, which were step 06's own new material:
  `leaf_input`, `extra_data` and `audit_path` pinned byte for byte, and the
  canonicalised query `leaf_index=7&tree_size=100000` beside them. Nothing at
  this step decodes one. The endpoint must still be **registered** — it is in
  the path set of §6.1 item 1 — and must answer **400** to a request carrying no
  parameters, and that is the whole of what is checked of it here.
* **Quota user strings**, step 06's other new entry: `CertificateQuotaUserPrefix`
  as `@intermediate`, `QuotaUserForCert`'s three-field rendering with its five
  digest bytes and lower-case hex, and those same strings as elements of
  `ChargeTo.User`. Not derived, not compared, not observed at this step. What
  survives is structural only: `InstanceOptions` must **carry** the
  `RemoteQuotaUser` and `CertificateQuotaUser` fields and a driver must be able
  to set them (§5).
* **The raw query a fetcher composes.** Step 06 compared `end=4&start=2` and its
  three siblings in issue order. No group here runs a client-side fetcher.
* **Flag *sets* as whole sorted collections, and most of the `(default …)`
  corpus.** Step 06 compared `ct_server`'s seventeen flag names and
  `migrillian`'s fourteen as exact sorted sets, and pinned `migrillian`'s
  fourteen defaults, `ct_server`'s two `(default true)` descriptions and
  `scanlog`'s and `ctclient`'s `-log_uri` URLs. At this step flags are asked
  about **one name at a time** and the only types and defaults compared are the
  six gossip ones of §6.1 item 5. `migrillian` is still checked here, but on
  which names are present and which are gone, not on a total.
* **The JSON-configuration refusal.** CONFIG.md §4.2 still says a config file is
  text proto and nothing else through v1.1.0, and that is still true at
  v1.0.21 — but no group here feeds a JSON config, so it is stated and not
  measured. The unknown-field half of that same rule **is** checked, above.
* **Merkle proof verification, and the rest of `ctutil`.** Inclusion through
  `ctutil.LogInfo` and the `VerifyInclusion*` family, consistency through
  `ctclient consistency`, the node and root hashes of MERKLE.md §2–§4, and
  `LeafHash`, `LeafHashB64`, `ContainsSCT`, `MerkleTreeLeafForEmbeddedSCT`,
  the `-1`/zero-hash failure returns and `LastSTH`/`SetSTH`. Exactly **one**
  name from that package is called at this step — `ctutil.VerifySCT`, over a
  real submission with `embedded` false — and no group builds a `LogInfo`,
  verifies a proof or runs `ctclient`. MERKLE.md §1's leaf hash *is* still
  checked, so the capability must remain; it is simply not exercised.
* **Log lists and `findlog`.** Schema v1's field names including `final_sth`
  and `disqualified_at`, `LogInfo.MMD` as a `time.Duration`, and `findlog`'s
  stdout. `…/loglist` is not linked by any driver here, and `findlog` is
  required only to build, to print a usage banner and to reject the removed
  flag.
* **`logid`'s string forms, and most of `x509util`.** `GeneralNamesToString`,
  `OtherNameToString`, and the poison and SCT-list sections of
  `CertificateToString` that step 03 compared byte for byte. `x509util` is
  linked at this step by exactly one driver, for the two key-description lines
  of §6.1 item 6, and nothing else in the package renders a name.
* **The whole CORS layer.** `Access-Control-Allow-Origin: *`, the `OPTIONS`
  pre-flight answered by the wrapper itself, and the three `Vary` values in
  order. Still contract at v1.0.21 (HTTP-API.md §7) and it arrived before
  this step, but no group here sends an `Origin` header or a pre-flight. The
  other half of that same step-05 entry — the catch-all narrowed to `/`
  alone — **is** still checked.
* **Signature distinctness and the STH signature cache.** Step 04's gate moved
  the pinned root by an entry, by a bit and by a millisecond, required a
  different `tree_head_signature` each time, then asked again unchanged and
  required the byte-identical one back. Nothing at this step moves the pinned
  root or asks twice; the tree-head signature is still verified, once.
* **Per-log *root pool* separation.** Step 04 gave two logs different
  `roots_pem_file` pools and required each prefix to answer with its own set.
  This step runs several logs at once — two for the metrics group, four across
  two prefix spaces for the override group — but they share one roots file, so
  what is checked of multi-log serving here is **URL-space** separation, not pool
  separation. The narrower half survives: `get-roots` serves exactly the DER the
  config named.
* **`BuildLogLeaf`'s identity hash, and the extra-data encodings.** Step 06
  pinned `LeafIdentityHash` as SHA-256 of the submitted DER and pinned the
  `CertificateChain` and `PrecertChainEntry` encodings that become a leaf's
  extra data, including the three-byte empty chain `000000`. This step pins the
  leaf's `LeafValue` and the `CertificateChain` framing, and re-asserts neither
  the identity hash nor `PrecertChainEntry`.
* **The `…/merkletree` withdrawal.** Step 06 greped a build log for that import
  path. This step's build probe observes a withdrawn **symbol** instead —
  `ctfe.GetTreeHead` — and nothing here mentions `merkletree`.
* **Every error body and every diagnostic read for its wording.**
  `get-sth-consistency`'s parameter-error bodies, `empty backend name`,
  `duplicate backend name`, and the structured error surface earlier steps
  matched as documented prefixes. Nothing at this step reads a body or an error
  for its text; see the end of §6.1.

**None of this is a licence to delete anything.** Section 6 is the list of what
is *compared*; section 9 is the list of what is *out of scope*; a surface that
appears in neither still has to be there for your tree to build, still has to
resolve for every driver that links its package, and may be measured again at
the step after this one.

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
| `inherited-ctfe-serving-contract` | gate |
| `inherited-rfc6962-leaf-hash` | gate |
| `inherited-tls-codec` | gate |
| `inherited-x509-chain-verification` | gate |
| `cli-new-command-surface` | requirement |
| `config-new-proto-fields` | requirement |
| `ctfe-gettreehead-withdrawn` | requirement |
| `ctfe-instance-setup-and-mirror` | requirement |
| `ctfe-mirror-sth-getter` | requirement |
| `ctfe-validate-log-config-mirror` | requirement |
| `ctserver-graceful-shutdown` | requirement |
| `ctserver-handler-prefix-paths` | requirement |
| `ctserver-merge-delay-metrics` | requirement |
| `ctserver-override-handler-prefix` | requirement |
| `gossip-minimal-x509ext` | requirement |
| `keyflag-removed-from-binaries` | requirement |
| `signature-verifier-key-compliance` | requirement |
| `x509-curves-secp192r1` | requirement |
| `x509-isfatal-nonfatal-parsing` | requirement |
| `x509-rfc3779-blocks` | requirement |
| `x509-subject-info-access` | requirement |

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

* **Gossiping.** `goshawk` and `gosmin` need live logs and, from the next
  release, a database. Their presence, their flag grammar and the `x509ext`
  data format are checked; no gossip traffic is.
* **RPKI semantics.** The address and AS-number blocks are checked as parsing:
  the structures come out of pinned DER with the right fields. No path
  validation or resource-certificate policy is implemented or checked.
* **Replicating from a real upstream log.** Nothing fetches entries or tree
  heads from another log, over the network or otherwise, and no storage that
  really tracks a source log is written or checked. What **is** checked is the
  local half of the mirror path: the interface shape, the mirror config
  validation, and the behaviour of `MirrorSTHGetter` against a storage the
  driver supplies — that the storage's STH is served unaltered, that the
  `maxTreeSize` it is asked for is the backend's current tree size, that one
  `get-sth` is one call, and that a storage error is not a 200
  (CLIENT-API.md §8.3).
* **`certcheck`'s `https://` fetch.** CLI.md §4.3 records that a positional
  argument beginning `https://` is fetched from that site rather than opened
  as a file. Reaching a site is out of scope for every group in this package,
  so that behaviour is stated but out of scope. What *is* checked of `certcheck`
  at this step is its flag set, and specifically that it now declares
  `system_roots`; its verification behaviour was checked where it was
  introduced and is not re-checked here.
* **The *number* of `package main` directories.** CLI.md §1 records that
  v1.0.21 has sixteen of them, and `WORKSPACE_HANDOFF.md`'s checkpoint
  prints the count so that you can confirm it for yourself. What the verifier
  checks is that each of the sixteen **named** programs is built and
  executable — the fourteen inherited ones in `build-clean`, `goshawk` and
  `gosmin` in `cli-new-command-surface` — and never that the directory it
  builds them into holds exactly sixteen entries: the verifier compiles
  programs of its own alongside yours there, so counting entries would
  measure the harness rather than your tree. An extra `package main` of your
  own therefore has no effect.
* **Anything dated after v1.0.21.** In particular the admission inversion
  of the next step: at this release `reject_expired` still means "do not
  disable time checks" and still applies to the whole path, and that is what
  is checked here.

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

A removal that cannot be undone from the command line, and a log that finally
knows where it lives.

Until v1.0.19 the key-compliance rules of RFC 6962 — RSA at 2048 bits
or more, ECDSA on P-256 — could be waived by a flag that the root package
registered, which meant every binary in the tree accepted it. At v1.0.21
that flag does not exist. The waiver survives as an exported package variable
that an importing program can set, so the capability is not deleted; what is
deleted is the ability to ask for it from outside the process.

The second thread is addressing. A global `--handler_prefix`, a per-log
override normalised to a single leading slash, and two advertised merge delays
mean a log now has a stated location and stated timing promises. The third is
mirrors: `GetTreeHead` gives way to an `STHGetter` interface with a real
implementation and a mirror implementation behind it.

Around those, the parsers learn Subject Information Access and RFC 3779
address and AS-number blocks, and `gossip/minimal` arrives with the small,
durable piece of it: an STH encoded into an X.509 extension.
