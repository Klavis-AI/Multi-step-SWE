# TEST_INTERFACE — step 10 of 13 (certificate-transparency-go v1.1.3)

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
whether that applies at this milestone. **It applies here**, in the second of
the three shapes section 7 names — the module is still supplied and still
imported, and the pinned revision has deleted a **symbol** the inherited code
calls.

Section 2's pinned manifest moves `google.golang.org/grpc` v1.40.0 →
**v1.46.0** (with `github.com/google/trillian` v1.4.0 → v1.4.1 and
`google.golang.org/protobuf` v1.27.1 → v1.28.0 beside it). grpc 1.46 removes
`grpc.WithBalancerName`, which v1.40 still carried and had already marked
deprecated. The front end's `main` calls it twice — once on the
comma-separated multiple-backend arm of `--log_rpc_server` (CLI.md §2.1) and
once on the ordinary single-backend arm — so `go build ./...` in `/app` fails
on the very first run:

    trillian/ctfe/ct_server/main.go:172:36: undefined: grpc.WithBalancerName
    trillian/ctfe/ct_server/main.go:175:36: undefined: grpc.WithBalancerName

Comparing section 2's require list against the tree's imports will not show
you this, because nothing is missing from either; only building the tree does.
Section 1's build obligation is not met, and `build-clean` — which section 7's
table lists as **GATE** — fails before you have changed anything, on two of
its three cases: `inherited-command-binaries-present` and
`flag-parsing-binaries-report-their-own-usage`, both naming `ct_server`.

Three consequences, because none is guessable from section 7 alone:

* A no-op here satisfies none of the new work: the gate itself fails. Section 7's
  note on no-ops covers both cases; v1.1.3 is the second one.
* **Exactly one program is lost**, and be precise about that, because the
  convenient sentence — "the tree does not build, so there are no binaries" —
  is false here. The harness builds candidate binaries with a single
  `go build -o <dir>/ ./...`. An undefined symbol is a *type*-checking
  failure, confined to the packages that depend on it, and both call sites sit
  in `ct_server`'s own `package main`, which nothing else imports. The command
  therefore still writes **fourteen** of the fifteen programs v1.1.2
  left — `ct_hammer` among them — and `ct_server` is the one it does not.
  `gate-endtoend`, also a **GATE** group, then executes *none* of its cases,
  because its bring-up starts that binary; so does the in scope
  `witness-feeder-endtoend`. Two named `build-clean` failures plus an
  end-to-end group that reported nothing at all, with nothing in either to do
  with the command tree, the witness or the log lists, is the signature of
  exactly this. (An *unresolvable import* is the case that suppresses the
  whole command and leaves no binaries whatever. That is a real failure mode
  elsewhere in this series; it is not the one in front of you.)
* Pinning a different `google.golang.org/grpc` is not a repair. Section 2
  fixes the dependency universe, the benchmark restores its own `go.mod` and
  `go.sum` over yours before it builds anything, and checking is offline
  against a module cache that holds only the pinned revision — so the
  adaptation has to happen in the product's own code.

This is inherited plumbing, not this milestone's work, and it adds no requirement
of its own: `ct_server`'s flag set is unchanged at v1.1.3, no group
counts it or checks how it dials the backend (§3.4), and none of this is on
section 10's list of what the release brings. The fix is confined to the two
call sites the compiler names; upstream's own repair at v1.1.3 asks for
the same round-robin policy through a service-config-based dial option
instead. Do it first and the rest of the step is what sections 4 to 6
describe.

## 4. Command-line programs the verifier runs

v1.1.3 adds four `package main` directories and withdraws none: the whole
of `internal/witness/cmd/`. **19** programs in product scope, against **15** at
v1.1.2 (CLI.md §1, §4.10).

| program | directory | what a group does with it |
| --- | --- | --- |
| `ctclient` | `client/ctclient/` | the milestone. Run as a process against a benchmark-owned loopback log; checked on its whole argument grammar — the command word first and its flags after it, persistent flags binding on either side of it, every declared alias resolving, the two flags whose type and default change, and an unknown command, an unknown flag or a surplus positional each *refused* rather than ignored: exit **255**, nothing on standard output, a diagnosis on standard error (CLI.md §3.2) |
| `witness` | `internal/witness/cmd/witness/` | started on a loopback port the group picks at run time and driven over its HTTP API (CLIENT-API.md §6.3); checked on what it stores, what it refuses and what it co-signs, and separately on declaring the four flags CLI.md §4.10 gives it |
| `feeder`, witness `client`, the witness config tool | `internal/witness/cmd/` | built, runnable, checked on the flag names, types and defaults CLI.md §4.10 fixes for each, and on exiting **1** when a flag §4.10 calls required is left at its empty default. `feeder` is additionally run end to end against a loopback log and a `file://` log list |
| `ct_server` | `trillian/ctfe/ct_server/` | built, runnable and asked for its usage like the rest, and started as a real process by the two end-to-end groups, in front of the benchmark's own Trillian stub and against an ordinary single-log configuration. Its flag set is unchanged from v1.1.2 (CLI.md §2.1) and no group counts it. `is_readonly` is reached **in process**, through `trillian/ctfe`'s own configuration and handler-registration entry points, not through this binary |
| the other thirteen | see CLI.md §1 | built, runnable, and asked for their usage; `findlog` and the whole v1 log-list package are still here and leave at the next step |

The **19** is checked as a number and as a set, not only program by program:
a group builds every `package main` directory in the tree and compares the
names it gets back against CLI.md §1's `present at` column for v1.1.3 —
those nineteen, no more and no fewer. It builds them from the verifier's own
module and never writes into your tree, and it reads names only: the usage
text each program prints is checked elsewhere and is no part of this. A
`package main` of your own left in the tree is therefore a difference the
verifier sees, which is what the `wc -l` checkpoint in `WORKSPACE_HANDOFF.md`
is there to let you notice first.

The flag-grammar rules of the earlier steps are unchanged for every program
that still parses with the `flag` package: `--help` exits **0** and prints the
flag package's usage block, whose first line names the path the binary was
invoked by; an unrecognised flag exits **2**; `glog.Exit*` and `log.Fatal*`
exit **1**; `glog.Fatal*` exits **255**. `chainfix` remains the exception that
reads its positionals before the flag package runs (CLI.md §4.6).

`ctclient` is the other exception, and it is the point of the step: it no
longer parses with the `flag` package at all. `--help` is answered with a
command tree on stdout and exit **0**, and every parse error goes through
`Fatal`, so the exit status is **255** and not 1.

**No diagnosis is checked for its wording**, here or anywhere else in this step
(§3.3). Where a command must refuse something, what is checked is that it
refuses: the exit status, an empty standard output, a non-empty standard error
and — where the refusal must come first — that no request reached the log. Two
exceptions, and they are identifiers rather than sentences: an unknown flag and
an unknown command must be named back in the diagnosis, because otherwise
nothing distinguishes refusing *this* flag from refusing the command line as a
whole. CLI.md §3.2's per-command failure messages and the framework's own
`Error: …` lines are quoted in that document to tell you what to write; no
group compares them.

## 5. Packages and symbols the verifier links against

Drivers at this step link against exactly the packages below. The list is
complete on its own: it names what v1.1.3 introduces *and* what is
carried forward from the steps behind you, because your workdir holds no
other step's `TEST_INTERFACE.md` to defer to.

| import path | what the drivers use |
| --- | --- |
| `…` (root) | the RFC 6962 structures and path constants, unchanged from v1.1.2 and linked by **thirteen** of this step's groups — more of them than any other package here. The structures: `SignedTreeHead`, whose `tls.Marshal` **omits** `Version` — the one enum-typed field the codec is ever asked to encode with no `tls` tag, and an untagged enum field contributes no bytes, so the encoding is tree_size(8) timestamp(8) root(32) signature log_id(32) (TLS-CODEC.md §2, §5.6) — and is therefore the 88-byte preimage the witness co-signs (§6); `SignedCertificateTimestamp`; `DigitallySigned` and its `Base64String`; `TreeHeadSignature`; `MerkleTreeLeaf`; `TimestampedEntry`; `LogEntry`; `PreCert`; `PrecertChainEntry`; `CertificateChain`; `ASN1Cert`; `CTExtensions`; `SHA256Hash`; `LogID`. The enumerations `V1`, `X509LogEntryType`, `PrecertLogEntryType`, `TimestampedEntryLeafType`, `TreeHashSignatureType`. The helpers `SerializeSTHSignatureInput`, `SerializeSCTSignatureInput`, `MerkleTreeLeafFromChain`, `CreateX509MerkleTreeLeaf`, `LeafHashForLeaf`, `PublicKeyFromPEM`, `PublicKeyFromB64`, and `NewSignatureVerifier` with the `SignatureVerifier` it returns. The JSON carriers `AddChainRequest`, `AddChainResponse`, `GetSTHResponse`, `GetEntriesResponse`. And the path constants `AddChainPath`, `GetSTHPath`, `GetEntriesPath`, each carrying its leading `/` and its `ct/v1/` segment exactly as HTTP-API.md §1 spells the endpoint |
| `…/tls` | the RFC 5246 presentation-language codec of TLS-CODEC.md, linked by **twelve** of this step's groups: every wire run and every signature §6 pins is computed through it. `Marshal` and `Unmarshal` — the latter hands back the bytes it did not consume rather than consuming them, and refuses a run that is truncated or that declares a length the buffer cannot satisfy — together with `SignatureAndHashAlgorithm` and its `HashAlgorithm` and `SignatureAlgorithm` members, and the constants `SHA256`, `SHA512`, `RSA`, `ECDSA`. The root package's `DigitallySigned` **is** `tls.DigitallySigned`, so a driver builds the algorithm pair out of this package and marshals the result through it. Nothing here moves at v1.1.3, and that is the point of naming it: the witness countersignature is `tls.Marshal` of a whole `SignedTreeHead`, so a drift in this encoding would silently change what a witness signature means |
| `…/x509` | the fork, linked by seven groups: `Certificate`, `ParseCertificate`, `ParsePKCS8PrivateKey`, `CertPool`, `VerifyOptions`, `ExtKeyUsage` with `ExtKeyUsageAny`, and `IsFatal`, which is how a driver reads a parse verdict rather than testing `err != nil`. Read `IsFatal` at v1.1.3 and not as step 07 wrote it: the `lax` retry inside the `ParseCertificate` family landed at v1.1.0, behind this release, so a strict failure that `lax` can re-parse comes back **non-fatal** (X509-FORK.md §2.2, §3; §6.2) |
| `…/ctutil` | the SCT utilities, in their v1.1.2 shapes, linked by `inherited-ctutil-sct` and `gate-endtoend`: `VerifySCT`, `VerifySCTWithVerifier`, `LeafHash`, `LeafHashB64`, `ContainsSCT`. Each of the first four takes the chain as `[]*x509.Certificate` over the **fork's** `x509` and not the standard library's, the SCT as `*ct.SignedCertificateTimestamp`, and a trailing `embedded bool`; `VerifySCTWithVerifier` takes a `*ct.SignatureVerifier` as its first parameter, and `ContainsSCT` takes one `*x509.Certificate` and the SCT and returns `(bool, error)`. `LeafHash` returns the raw `[32]byte`, `LeafHashB64` the standard-base64 rendering of the same bytes |
| `…/x509util` | **`PEMCertPool`** — the same type as at v1.1.2 at a new import path, with the whole surface of CLIENT-API.md §5.3: `NewPEMCertPool`, `AddCert`, `Included`, `AppendCertsFromPEM`, `AppendCertsFromPEMFile`, `Subjects`, `CertPool`, `RawCertificates`. The package also carries **`CertificateToString`**, unchanged from v1.1.2 and at the path it already had: `ctclient-get-roots` and `ctclient-get-entries` call it on *your* tree to build the text they then require the subcommand to have printed (CLI.md §3.1), so what is checked is that the two renderings agree — no sentence this function writes is pinned anywhere in this step |
| `…/trillian/ctfe` | the entry points of v1.1.2, in those shapes and named here rather than implied: `LogConfigFromFile` and `ValidateLogConfig` (CONFIG.md §4.1), `SetUpInstance` with the `InstanceOptions` it takes and the `Instance` it returns (CLIENT-API.md §8.1, §8.2), `DefaultRequestLog` (§8.5), and the handler-name constant `GetSTHName` (§8.3). Plus `NewCertValidationOpts`, which takes `*x509util.PEMCertPool` as its first parameter (CLIENT-API.md §8.3). `ctfe.PEMCertPool` must be **gone**, proved the way section 3.3 describes |
| `…/loglist3` | born here: `LogList` with its version and list timestamp, `Operator`, `Log` with `PreviousOperators`, `PreviousOperator`, `LogStates` — the six-member state union — and all seven `LogStatus` constants, `UndefinedLogStatus` included, `TemporalInterval`, `NewFromJSON`, `NewFromSignedJSON`, the `FindLogBy…`/`FuzzyFindLog` family, and the filter file (CLIENT-API.md §4.3, §4.2): its four selection helpers `SelectByStatus(lstats []LogStatus)`, `TemporallyCompatible(cert)`, `RootCompatible(certRoot, roots)` and `Compatible(cert, certRoot, roots)`, each a method on the **pointer** `*LogList` that returns the filtered list as a `LogList` **value** — a driver hands what comes back straight to a function taking `loglist3.LogList`, not a pointer — and `LogRoots`, the `map[string]*x509util.PEMCertPool` the root filter reads |
| `…/loglist2`, `…/loglist` | both still present and still linked: a probe resolves `NewFromJSON` out of all three log-list packages in one program, which only builds at v1.1.3 — the one release at which all three exist (RELEASE-MAP.md §14). `loglist2`'s `LogRoots` retypes to the `x509util` pool |
| `…/jsonclient` | `New` and the `Options` it takes — its `Logger` and `UserAgent` fields — and on the client it returns, `PostAndParseWithRetry(ctx, path, req, rsp)` — four parameters, three return values, CLIENT-API.md §1.1 — against a scripted status sequence: **429** joins **503** on the retrying arm and honours `Retry-After` the same way (CLIENT-API.md §1.1). `RspError` and its `StatusCode` are how a driver reads the status a refused call came back with, rather than matching the message it carries (§3.3). All four are in their v1.1.2 shapes; only the retry arm moves here |
| `…/trillian/integration` | `HammerCTLog(ctx, cfg)` — a leading `context.Context`, and a loop that ends when it is cancelled (CLI.md §4.8). The second parameter's type name is part of the surface too: a driver assigns the function to a `func(context.Context, integration.HammerConfig) error`, so the type must stay exported, in this package, under that name |
| `…/trillian/ctfe/configpb` | `LogConfig.is_readonly`, field **19**, which does not exist at v1.1.2 in any form (CONFIG.md §1) |
| `github.com/google/trillian`, `google.golang.org/grpc`, `google.golang.org/protobuf` | **not your deliverable and out of scope as such** (§3.4) — named here because a driver at this step links them into the same binary as your code, and this section is the complete list of what the verifier links. This is the only row in the table whose paths are **not** under the `…` of §2, so every one of them is written out in full. Their versions are fixed by the benchmark-owned manifest and not by anything you write: `github.com/google/trillian v1.4.1`, `google.golang.org/grpc v1.46.0`, `google.golang.org/protobuf v1.28.0` (§2). `ctfe-readonly-mode`, `gate-endtoend` and `witness-feeder-endtoend` each stand up a backend from `trillian.RegisterTrillianLogServer` and the nine `TrillianLogServer` methods this Trillian carries — `QueueLeaf*`, `AddSequencedLeaves*`, `GetLeavesByRange*`, `GetLatestSignedLogRoot*`, `GetInclusionProof*`, `GetInclusionProofByHash*`, `GetConsistencyProof*`, `GetEntryAndProof*`, `InitLog*` — with `trillian.LogLeaf`, `trillian.QueuedLogLeaf`, `trillian.Proof`, `trillian.SignedLogRoot`, `trillian.TrillianLogClient` and `trillian.NewTrillianLogClient`; they serve it over `grpc.NewServer` on a loopback socket and dial it with `grpc.Dial` under `grpc.WithTransportCredentials(insecure.NewCredentials())` from `google.golang.org/grpc/credentials/insecure`. The root it answers with is a `types.LogRootV1` from `github.com/google/trillian/types`, marshalled and deliberately **unsigned**, because since v1.1.0 the front end calls `UnmarshalBinary` on `GetLogRoot()` and stops there. `ctfe-readonly-mode` links four more: `monitoring.InertMetricFactory` from `github.com/google/trillian/monitoring`, `keyspb.PrivateKey` and `keyspb.PublicKey` from `github.com/google/trillian/crypto/keyspb`, `keys.RegisterHandler` paired with `der.FromProto` from `github.com/google/trillian/crypto/keys` and `github.com/google/trillian/crypto/keys/der`, and `proto.Marshal` from `google.golang.org/protobuf/proto`, which is how it writes the binary-encoded `LogConfigSet` it then loads. You implement no part of any of this; what is checked is only what your code does against it |
| `…/ingestor/ranges`, `…/watcher` | **removed.** Nothing in the tree imported either at v1.1.2, and a driver naming either path must now fail to build, naming it — as an import and as a build target, both proved the way §3.3 describes |

That table is the whole surface. What earlier steps named is in it where a
driver here still links it — restated above at the paths and shapes it has at
v1.1.3, not at the ones the step that first named it wrote — and those
groups still run, under `inherited-` names where they have one.

Three of the entries are **negative**, and each is checked exactly as the
positive ones are, by a build that fails naming the thing that is not there
(§3.3): `ctfe.PEMCertPool` must be **undefined**, and `…/ingestor/ranges`
and `…/watcher` must **fail to resolve** — as an import and as a build
target. A package that must not resolve is a fact this section records, not
one it omits.

## 6. Formats crossing the boundary

Step 09's `TEST_INTERFACE.md` is replaced by this file. Your workdir carries
this one and no other step's, so the surface the steps behind you listed is
repeated here in full rather than referred to. What follows is **not** step
09's list copied forward: every carried bullet was re-read against this step's
own group drivers at v1.1.3 before it was restated, several earlier
entries have gone stale and one has inverted outright, and §6.2 names each
family that was dropped and says why.

Carried forward from the steps behind you, restated:

* **The TLS encodings RFC 6962 fixes** (TLS-CODEC.md). A `DigitallySigned` is
  a hash-algorithm byte, a signature-algorithm byte, a two-byte length and the
  signature — SHA-256/ECDSA over `deadbeef` is the eight bytes
  `04030004deadbeef` — and its base64 rendering is the base64 of exactly those
  bytes. A `TreeHeadSignature` is `0001` then eight bytes of timestamp, eight
  of tree size and thirty-two of root hash, and that run is byte for byte the
  input an STH signature is made over. `tls.Marshal` of a `SignedTreeHead`
  **omits** the `Version` field, which is what makes that structure 88 bytes,
  and it is the preimage the witness co-signature below is made over. A
  `MerkleTreeLeaf` is version, leaf type, entry type, a three-byte-length DER
  certificate and a two-byte extensions length, in that order. Unmarshalling
  hands back any trailing bytes rather than consuming them, and refuses a run
  that is truncated or that declares a length the buffer cannot satisfy.
* **The leaf hash and the log ID.** A leaf hash is SHA-256 over `0x00`
  followed by the TLS encoding of the `MerkleTreeLeaf`; a log ID is SHA-256
  over the DER `SubjectPublicKeyInfo`. Both are compared against values pinned
  in the benchmark for its own fixtures rather than recomputed from your code,
  in hex and in standard base64. A precertificate leaf carries the issuer key
  hash and the TBS structure, and every SCT helper that takes an entry agrees
  with the plain one on the same input. Asking for an SCT embedded in a
  certificate that carries none is an error and not an empty answer, and the
  membership helper tolerates a nil list.
* **The STH signature input and what verifies it.** Public keys are read both
  from PEM and from bare base64 DER; the bytes signed are the
  `TreeHeadSignature` run named above; altering the timestamp, the tree size
  or the root hash makes verification fail; a signature offered under the
  wrong hash or signature algorithm, or over the wrong structure version, is
  refused; and the outcome agrees with the standard library's own ECDSA
  verification of the same bytes under the same key.
* **The CT HTTP API as the RFC writes it** (HTTP-API.md). `get-sth` returns a
  tree head whose size, timestamp, base64 root hash and base64 signature are
  read as fields; `get-entries` returns `leaf_input` and `extra_data` per
  entry and those are compared byte for byte against the leaves that were
  queued; `add-chain` returns an SCT whose log ID equals the hash of the
  configured key exactly and whose signature verifies under it with the
  standard library; `get-roots` returns the configured roots. A submission
  reaches the backend as exactly **one** `QueueLeaf` carrying the leaf that
  was submitted, and `/healthz` answers `ok`.
* **The front end's route set and its read bodies.** Which paths a
  personality registers is checked as a set of exact path strings, and the
  bodies of `get-sth`, `get-entries` and `get-roots` are pinned documents read
  field by field. A request made with the wrong method is **405**; what is
  checked there is that the response names `POST`, the method it wanted, and
  not the sentence around it.
* **A log configuration in either proto encoding.** The same entry point loads
  the same configuration whether the file is text proto or the binary wire
  encoding — binary has been a fallback since v1.1.2 (CONFIG.md §4)
  — and this step's `ctfe-readonly-mode` group loads one configuration twice,
  once each way, before validating it.
* **The program set the tree already shipped.** The fifteen commands
  v1.1.2 carried are all built and all runnable. Each of them —
  `chainfix` and `ctclient` excepted — answers `--help` with exit status **0**
  and a first line naming *its own* path under the build directory; the
  standard library writes the sentence around that path and the sentence is
  out of scope. `chainfix` reads its positional arguments before the flag
  package runs and exits **2**.
* **The log-list packages of the earlier schemas.** `loglist` and `loglist2`
  are both still in the tree, still linkable and still expose `NewFromJSON`,
  and `loglist2`'s root map is still keyed by URL. The v3 package below joins
  them; this is the one release where all three exist at once.
* **The certificate pool — at the import path it lives at now.** Everything
  CLIENT-API.md §5.3 says about the pool holds here unchanged: it de-duplicates
  by SHA-256 over the DER, it reports a PEM file one of whose blocks is bad as
  a failure rather than a successful load, it appends from a file as well as
  from bytes, and the `*x509.CertPool` it hands out verifies a chain at a
  pinned instant. What
  changed is *where the name is*: it is `x509util.PEMCertPool` at v1.1.3,
  `ctfe.PEMCertPool` must not resolve at all, and both log-list packages' root
  maps are `map[string]*x509util.PEMCertPool` (§5). Read every earlier
  sentence that names an import path at the path §5 gives, not at the one that
  step wrote.
* **`jsonclient`'s retry arm.** A status the client treats as transient is
  retried rather than returned, and the caller sees the eventual success; a
  terminal status comes back as a `jsonclient.RspError` carrying that status.
  Which statuses sit on the retry arm changes at v1.1.3 — see the 429
  bullet below.
* **Merkle proofs as structures.** An inclusion or consistency proof crossing
  the boundary is an ordered list of 32-byte hashes over which the ordinary
  RFC 6962 verification runs. The benchmark computes the proofs it expects
  itself, in plain SHA-256 over its own pinned eight-leaf tree, rather than
  asking your code what they should be.

And, new at v1.1.3:

* **`ctclient`'s generated help.** The command tree is read back out of the
  binary through the only interface a user has. This is an output format and
  not a *message* in the sense of §3.3, and two different things are compared
  in it. Most of it is spelling CLI.md §3.2 fixes — a command name, an alias,
  a flag name, its type, its default, its one-line description. The rest is
  the framework's own rendering of those declarations, and that is checked too,
  because it is what a user actually reads: the root's `Usage:` line is the
  framework's `  ctclient [command]`, and each leaf's usage line is two
  spaces, `ctclient `, the usage string §3.2 spells for that command, and the
  ` [flags]` suffix the framework appends to any command that has flags at
  all. Those lines are compared exactly as printed, indentation included. A
  flag's type is the flag library's normalised name and not Go's: a bool
  prints no type at all, an `int64` prints `int`, a `uint64` prints `uint`, a
  `time.Duration` prints `duration`. A default is printed only when it is not
  that type's zero value, a string default is printed quoted, and a duration
  default is Go's own `String()` form — a maximum merge delay of one day
  reads `24h0m0s`, not `24h`. Compared line for line: the
  root command's short description as its first line, its `Usage:` line and
  the seven product names in its `Available Commands:` list; the five
  persistent flags of CLI.md §3.2 with their type, default and description,
  where the framework prints them — in the root's own `Flags:` block and in
  every leaf's `Global Flags:` block; and, for each of the seven leaves, its
  short description, its fully-spelled usage line, the `Aliases:` line naming
  it followed by every alias §3.2 declares for it, and its own `Flags:` block
  carrying the type, default and description of every flag §3.2 gives it and
  nothing else. Three things are filtered out before any comparison and none
  of them is checked: the framework's own `-h, --help` entry, its `help` and
  `completion` built-ins where they appear among the available commands, and
  the logging library's flags, which are step 09's subject and reach this help
  only by being merged into the root's persistent set. Column padding is not
  checked inside a `Flags:` or `Global Flags:` block or on an `Aliases:` line
  — runs of spaces and tabs are collapsed to one there before comparing — but
  every word is, and that collapse does not extend to the root header and the
  usage lines named above as exact.
* **The witness HTTP API.** The three paths of CLIENT-API.md §6.3, with the
  log ID path-escaped into them: the log list and the stored STH are **GET**,
  an update is a **PUT** carrying `UpdateRequest` as JSON. An accepted update
  is **200** with the co-signed STH as the body, whose `witness_signatures`
  verify under the witness's own key. The refusals follow §6.3's table
  exactly, and the distinction it draws between the two verbs is checked: a
  **GET** for a log with no tree head stored is **404** whether or not the
  witness follows that log, while a **PUT** naming a log it does not follow is
  **500**, not 404, because only a failed consistency precondition is given a
  status of its own. A body that cannot be read as an `UpdateRequest` —
  including an empty one — is **400**. An STH that is well formed but cannot
  be reconciled with the one already held is **409**, and that response
  carries the held STH as its body, byte for byte, so the caller can form a
  new request. Those **stored-STH bodies are the only witness response bodies
  checked for their content** — the CTFE's 400 below is the step's one other
  body-in-full, and §3.3 admits both only because they are listed here: §6.3 declares the *prose* of a refusal to be assembled
  from whatever error was raised and to be no interface at all, so a refusal
  body is checked only as being non-empty and not a tree head. A witness whose
  wording is its own, and one that does not route its storage failures through
  a gRPC status wrapper, both pass. The log list is the JSON encoding of what
  the witness knows, which is `null` when it knows nothing.
* **What the subcommands print.** Every rendering `ctclient` emits is carried
  over from the v1.1.2 file the rebuild replaces, word for word
  (CLI.md §3.2's closing paragraph), and the groups compare whole standard
  output against it. Three details are read **at v1.1.3** rather than as
  CLI.md §3.1's table writes them, because that table is anchored at v1.0 and
  its per-release notes do not reach them. Where they differ from §3.1, these
  three sentences are what this step's groups assert:
  * A pre-certificate entry is rendered from the **submitted certificate** in
    `extra_data`, through the ordinary certificate renderer — so `--text=false`
    prints a PEM `CERTIFICATE` block for it exactly as for an X.509 entry, and
    `--text` prints the parsed certificate. The `%x`-of-the-TBS rendering
    belongs to v1.0 alone. The header line before it is unchanged and still
    carries the issuer key hash taken from the leaf.
  * `upload` prints a `LogID: <hex>` line between the `Uploaded chain of …`
    line and the `LeafHash: <hex>` line, and the signature line beneath them
    carries the label twice — `Signature: Signature: Hash=… Sign=… Value=…` —
    because §3.1's one-line signature rendering is itself printed under a
    `Signature: ` label.
  * A query string is assembled by the standard library's encoder, which
    orders parameters by name, so a range request reads
    `?end=<last>&start=<first>`. Only `get-entries` is checked on that exact
    string; `bisect` is checked on which indices it asked for, not on how the
    parameters were spelled or in what order it asked.
* **A pinned v3 log list.** Every field of CLIENT-API.md §4.3 decoded exactly
  from one fixed document — the list version and timestamp, operator contact
  details, previous-operator history, which member of the state union is set,
  and the final tree head a read-only log carries — plus a detached signature
  over that document accepted through `NewFromSignedJSON` and a corrupted one
  refused.
* **404 on the write paths.** With `is_readonly` the two write handlers are
  never registered, so `add-chain` and `add-pre-chain` are **404** — not 405,
  not 403 — while every read endpoint still answers with its usual body
  (HTTP-API.md §6.2, CONFIG.md §2.5). A configuration naming `is_readonly`
  must also *load*, which at v1.1.2 it cannot (CONFIG.md §1.1). One
  response body in that group is read for its content: the **400** a
  *writable* log returns for an empty chain, which is what proves the 404
  above was caused by `is_readonly` and not by a rejected request. HTTP-API.md
  §2 fixes that body verbatim — the `Bad Request` wrapper line, the
  `bench{1001}: ` prefix and the handler's own sentence — so it is compared
  byte for byte. A document that spells a body out that way is the one
  exception §3.3 leaves open, and this is the only place in this step where a
  diagnostic is compared as a whole string rather than merely required to be
  non-empty.
* **A 429 as a retry rather than a result.** What crosses the boundary is how
  many POSTs the server sees and what the call finally returns — not how long
  the client slept in between.

### 6.1 The strings this step compares whole

Section 3.3 allows one kind of whole-string comparison: an output or a body
the corpus writes out in full, listed here. This step has more of them than
any step behind it, because a command-line grammar *is* its printed output.
This is that list, and it is complete.

1. **`ctclient`'s help, line by line.** The root's short description as its
   first line, its blank line, `Usage:` and the framework's own
   `  ctclient [command]`; the seven product names listed under its
   `Available Commands:` heading; each leaf's short description and its
   usage line `  ctclient <use> [flags]`, with the usage string CLI.md §3.2
   spells for it and the
   suffix the framework appends; each leaf's `Aliases:` line; and the flag
   lines of a `Flags:` or `Global Flags:` block, each with its name, its
   normalised type, its default where one is printed and its description.
   In a *leaf's* own `Flags:` block that is every line the block holds, so a
   flag CLI.md §3.2 does not give that command is a difference like any other;
   in the root's `Flags:` block and in any `Global Flags:` block it is the five
   persistent flags only, because those blocks also carry the logging library's
   merged-in flags, which belong to glog and are step 09's subject. The
   framework's own `-h, --help` line is set aside everywhere. The root header
   and the usage lines are compared exactly as printed, indentation included.
   Inside the flag and alias blocks only, runs of spaces and tabs collapse to
   one before comparing — every word is checked there, the column padding is
   not.
2. **The whole of standard output for all seven subcommands.** Not a
   prefix, not a substring: the whole stream, ending newline included,
   against these forms.
   `TZ=UTC` is pinned in the child's environment, so a rendered time is a
   fixed string and not a property of the machine.
   * `get-sth`:
     `<when> (timestamp <ms>): Got STH for V1 log (size=<n>) at <uri>, hash <hex>`
     then `Signature: Hash=SHA256 Sign=ECDSA Value=<hex>`.
   * `get-roots`: the roots in log order, each as a PEM `CERTIFICATE` block,
     or with `--text` each as the certificate renderer writes it; an empty
     root set prints nothing at all.
   * `get-entries`: per entry the header
     `Index=<i> Timestamp=<ms> (<when>) X.509 certificate:` or
     `Index=<i> Timestamp=<ms> (<when>) pre-certificate from issuer with keyhash <hex>:`,
     then the rendering of the certificate beneath it.
   * `upload`: `Uploaded chain of <n> certs to V1 log at <uri>, timestamp: <ms> (<when>)`,
     then `LogID: <hex>`, then `LeafHash: <hex>`, then the doubled
     `Signature: Signature: Hash=… Sign=… Value=…`; a precertificate upload
     prints `Uploading pre-certificate to log` as an extra first line.
   * `bisect`: `First entry with timestamp>=<ms> (<when>) found at index <i>`
     followed by that entry, or
     `No entry with timestamp>=<ms> (<when>) found up to tree size <n>`.
   * the two proof subcommands:
     `Inclusion proof for index <i> in tree of size <n>:` and one `  <hex>`
     line per hash, then
     `Verified that hash <hex> + proof = root hash <hex>`;
     `Consistency proof from size <a> to size <b>:` and its hash lines, then
     `Verified that hash <hex> @<a> + proof = hash <hex> @<b>`.
3. **`upload`'s request body.** The JSON the log receives is compared as a
   literal — `{"chain":["<b64>",…]}`, that key, that order, no whitespace.
4. **Three query strings.** `?end=<last>&start=<first>` on `get-entries`,
   `?hash=<escaped>&tree_size=<n>` on `get-proof-by-hash`, and
   `?first=<a>&second=<b>` on `get-sth-consistency` — the standard library's
   encoder orders parameters by name, which is why `end` precedes `start`.
   `bisect` is checked on the indices it asked for, not on this spelling.
5. **The front end's registered paths.** The handler set of a read-only log,
   of a writable log and of a mirror, each compared as its sorted list of
   exact path strings.
6. **One diagnostic body, in full.** The **400** a *writable* log returns for
   an empty chain, `Bad Request` wrapper line, `bench{1001}: ` prefix and the
   handler's own sentence, exactly as HTTP-API.md §2 spells it. It is the only
   error text compared for its wording anywhere in this step.
7. **The witness's URL space.** `/ctwitness/v0/logs`, and beneath it
   `/ctwitness/v0/logs/<path-escaped log ID>/sth` and `…/update` — the paths
   themselves, and the log list's body, which is `null` when the witness
   follows nothing and a JSON array of log IDs when it follows one.
8. **The stored STH a 409 hands back.** Byte for byte the raw bytes the
   witness already held, so the caller can form a new request from them.
9. **The witness tools' flag declarations.** For `witness`, four whole lines
   of its usage block — `  -config_file string`, `  -db_file string`,
   `  -listen string`, `  -private_key string`. For `feeder`, `client` and
   `config`, the flag names and the printed defaults, including
   `(default "https://www.gstatic.com/ct/log_list/v3/log_list.json")`,
   `(default 10s)` and `(default "config.yaml")`.
10. **The pinned wire runs and digests of the inherited groups.** The hex and
    base64 constants named in the first three carried-forward bullets — the
    `DigitallySigned` and `TreeHeadSignature` encodings, the 88-byte
    `SignedTreeHead`, the `MerkleTreeLeaf` header, and the pinned leaf-hash
    and log-ID renderings.
11. **`/healthz`'s body.** Exactly `ok`, and nothing else, with a 200.
12. **The toolchain's removal message.** A probe importing
    `…/ingestor/ranges` or `…/watcher` must fail to build with a line ending
    in `does not contain package <that path>`, and a build target naming
    either must fail the same way. That message is the whole of the removal
    proof.
13. **The program inventory.** The sorted names of the binaries a build of
    every `package main` directory produces, compared as one list against the
    nineteen of §4. Names only — nothing about what any of them prints is
    part of that comparison.

**That list is exhaustive: no group at this step compares any other message,
body, rendering or program output as a whole string.** Four kinds look like
the list above and are not:

* **Diagnostics, everywhere except item 6.** A witness refusal body is checked
  only as being non-empty and not a tree head; `ctclient`'s parse errors are
  checked as being non-empty, as being on **stderr** with stdout empty, and as
  naming back the flag or command word at fault — never as a sentence. The
  witness's own 405s carry an **empty** body, and that emptiness is the whole
  of what is compared there.
* **The standard library's usage blocks.** `build-clean` reads one line of
  one, and only for the binary path inside it. The wording is the standard
  library's, not yours, and is out of scope.
* **Ordering and padding that nobody declared.** Column widths inside a
  `Flags:` block; the order in which `bisect` probes the indices it probes;
  the order of the calls `feeder` makes to reach the state it reaches.
* **Elapsed time.** A retry is counted in requests the server saw and in what
  the call finally returned. How long the client slept, and with what jitter,
  crosses no boundary here.

### 6.2 What earlier steps listed and this one does not

Six families the steps behind you listed are deliberately absent above.

* **`ctclient` as a `flag`-package program.** Earlier steps described its
  usage block as the standard library writes one — a `Usage of …:` first line,
  a flat list of `-flag` declarations, `flag.ExitOnError`'s exit **2**, and a
  subcommand named as a positional word inside a usage row. None of that is
  true at v1.1.3. The binary is a command tree: `--help` prints the tree
  on stdout and exits **0**, and a parse error goes through `Fatal` and exits
  **255**. `build-clean` excludes `ctclient` from its usage check at both
  anchors for exactly this reason. The replacement is the first bullet of the
  new list, checked by `ctclient-command-tree` and `ctclient-arg-grammar` —
  dropped and replaced, not merely dropped.
* **"Compare from the first stable word onward."** An earlier step compared
  `ctclient`'s output only from `Got STH`, because the leading time rendering
  depended on the machine's zone. This step pins `TZ=UTC` in the child
  process and compares the **whole** of standard output, that prefix included.
  The older reading rule is not weakened here; it is gone, and item 2 above
  replaces it.
* **A precertificate entry rendered as `%x` of its TBS.** That rendering
  belongs to v1.0. At v1.1.3 a precertificate entry is rendered from the
  **submitted certificate** in `extra_data` through the ordinary certificate
  renderer, as the third new bullet says. Only the header line above it is
  unchanged.
* **`ctfe.PEMCertPool`, and every other pre-move import path.** Earlier steps
  named the pool under `…/trillian/ctfe`, and named `…/ingestor/ranges` and
  `…/watcher` as packages that exist. At v1.1.3 the first must not
  resolve and the other two must not build. Nothing about the pool's
  *behaviour* was dropped — it is restated above — only the path it is
  reached at.
* **Step 07's strict-parse table in its fatal form.** "A non-minimally-encoded
  `INTEGER` is fatal" was true when that step was written and is **inverted**
  by the time you reach this anchor: the `lax` retry inside the
  `ParseCertificate` family landed at **v1.1.0**, behind this release, and
  from there through v1.3.3 the strict failure is re-run with `lax` and the
  original error recorded as **non-fatal** (X509-FORK.md §2.2, §3). No group
  at this step checks that table at all. Carrying it forward unchecked would
  have put a false statement in front of you, which is worse than the missing
  list this section replaces.
* **Two narrowings that later releases widened.** A **405** compared as a
  whole body: this step compares only that the front end's 405 names `POST`,
  and the witness's 405s have no body to compare. And "a log configuration is
  a text proto and nothing else": binary has been accepted since
  v1.1.2 and `ctfe-readonly-mode` loads both, so the restriction is
  dropped while the format stays.

None of this is a licence to delete anything. Section 6 lists what is
**compared**; section 9 lists what is **out of scope**. A surface that appears
in neither still has to be there for your tree to build, and the build gate is
checked: the fifteen inherited commands, the packages an earlier step named and
this one does not, and everything §5 says is still linked against are all
built before any requirement group runs.

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
| `inherited-ctutil-sct` | gate |
| `inherited-sth-signatures` | gate |
| `inherited-tls-codec` | gate |
| `ctclient-arg-grammar` | requirement |
| `ctclient-bisect` | requirement |
| `ctclient-command-tree` | requirement |
| `ctclient-get-entries` | requirement |
| `ctclient-get-roots` | requirement |
| `ctclient-get-sth` | requirement |
| `ctclient-proof-subcommands` | requirement |
| `ctclient-upload` | requirement |
| `ctfe-readonly-mode` | requirement |
| `jsonclient-429-retry` | requirement |
| `loglist3-filters` | requirement |
| `loglist3-schema` | requirement |
| `moved-and-removed-symbols` | requirement |
| `witness-feeder-endtoend` | requirement |
| `witness-refusals` | requirement |
| `witness-tooling-cli` | requirement |
| `witness-update-and-cosign` | requirement |
| `x509util-pemcertpool` | requirement |

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

* **Feeding a *real* log.** In production `feeder` and the witness `client`
  are pointed at live CT logs and a fetched log list. Neither is here.
  `feeder` *is* run for real, but only against a benchmark-owned loopback log
  and a `file://` log list, and it is checked on what the witness ends up
  holding and on which logs in that list it contacted at all — not on the
  order of the calls it made to get there, nor on how many, nor on how long
  it waited between them. The
  witness `client` is driven nowhere at all: it is checked on being built, on
  being runnable, on the flag names and defaults CLI.md §4.10 fixes for it,
  and on refusing to start when a required flag is left at its empty
  default.
* **Where the witness keeps its state.** It is observed only through its HTTP
  API, so what it remembers within one run is checked and whether a given run
  put that on disk or in memory is not.
* **How long a 429 backs off.** That the call retries instead of returning at
  once, that `Retry-After` is read the same way as for a 503, and how many
  requests reach the server are all observable; the interval and its jitter
  are not.
* **`HammerCTLog` actually hammering.** The new leading parameter is observed
  by compiling against the signature; a load run against a live log is not
  performed anywhere.
* **Which library verifies Merkle proofs.** The pinned manifest contains
  exactly one and it is the one v1.1.3 uses (MERKLE.md §4.2). No group
  checks the import path you reach it through, and none checks the wording of
  the errors it returns.
* **Anything dated after v1.1.3.** The v1 log-list package and `findlog`
  still exist here and are still checked; their removal, the migration of
  their four dependants, `tools/` and klog in place of glog all belong to the
  next step.

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

A command-line grammar is an interface like any other, and v1.1.3 breaks
this one deliberately and completely.

Up to v1.1.2 `ctclient` was a single `flag`-package `main`: global
flags first, one positional command word last, seven spellings of that word,
and nothing at all to say about a surplus argument. At v1.1.3 that file is
deleted and the tool is a root command over `client/ctclient/cmd/`. The command
word comes first and its own flags come after it; five connection flags become
persistent and bind on either side of it; the canonical names become hyphenated
and the old spellings survive only as declared aliases; two flags change type
and default together; the logging library's flags are merged into the new flag
set so they keep working; and an unknown command, an unknown flag or a surplus
positional each produce a diagnostic and exit **255** instead of being ignored.
Nothing about what the tool *does* changes. Everything about how you ask
changes, and the help text is where a verifier reads the difference.

Around it sits the rest of the release: a type that moves package without
changing a line of itself, and takes every caller's field type with it; a third
log-list package born beside the two already there, so that this is the only
release where all three exist; a witness service that co-signs the tree heads
it can reconcile and refuses the ones it cannot; a status code that joins the
client's retrying arm before any log in the product starts returning it; a
config field that unregisters the two write handlers while everything else
keeps serving; and two packages deleted for having no callers.
