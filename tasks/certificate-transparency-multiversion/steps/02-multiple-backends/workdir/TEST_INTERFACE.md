# TEST_INTERFACE — step 02 of 13 (certificate-transparency-go v1.0.5)

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

v1.0.5 adds `certcheck` and `crlcheck`, so the tree holds **10**
`package main` directories (CLI.md §1) and `go build ./...` must produce ten
binaries.

| program | directory | what a group does with it |
| --- | --- | --- |
| `ct_server` | `trillian/ctfe/ct_server/` | as at step 01, and additionally: it is fed a **multi-backend** config file and a **single-backend** config file and must accept both, and it is fed configs that name an undefined backend or use a duplicate backend name and must refuse them. Duplicate **prefix** checking is a different rule and arrives at v1.0.10, so at this release it must be **absent**: a group feeds the front end a config whose two logs share a prefix and requires the validator to **accept** it (CONFIG.md §4.4 -- v1.0.5's validator "checks backend names, backend specs and that every log names a defined backend, and nothing else about the logs"). Rejecting it fails that group |
| `certcheck` | `x509util/certcheck/` | run over pinned fixture certificates; checked on exit status, on the flag set of CLI.md §4.3 (names, types, defaults, and the absence of the flags later releases add), and on the **stdout** the `-verbose` description carries -- that the certificate's own values reached it, never the labels, order or layout. A run without `-verbose` must write nothing to stdout. stderr wording is never read |
| `crlcheck` | `x509util/crlcheck/` | checked the same way, on a CRL the driver **mints in-process** from the pinned bench root with the Go standard library -- no CRL fixture is shipped, and nothing about the CRL depends on your code. Both encodings are fed to it, and `-ca` is given a PEM certificate (CLI.md §4.3). The flag-set case is the negative one: `strict` and `cert` belong to v1.0.10 and must not be declared yet. Three of the CRL's own values must reach **stdout** under `-verbose`, and they are the whole of what is required of the description: the **issuer's common name**; the CRL's **authority key identifier**, in hex, with or without separators (either form passes); and the **serial number of the revoked certificate**, in decimal or in hex. A description that prints the dates, the signature algorithm and the revocation reason but omits the key id fails, and nothing else about the layout, the labels, the field order or the date format is read |
| `ctclient`, `scanlog`, `preloader`, `dumpscts`, `chainfix`, `ct_hammer`, `gossip_server` | see CLI.md §1 | unchanged from step 01 |

Four rules hold for every program and are checked wherever a group runs one
(CLI.md, the preamble on flag grammar):

* an unrecognised flag makes the standard `flag` package print `Usage of
  <prog>:` and the flag list **to stderr** and exit **2**;
* `-h` and `--help` are **not** unrecognised flags: the same package prints the
  same block, also **to stderr**, and exits **0**. Because the block goes to
  stderr, stdout stays **empty** on such a run, and a group checks that as well
  as the status. The one program this does not describe is `chainfix`, which
  imports no flags and indexes `os.Args` before anything parses: three missing
  positionals panic and exit **2** (CLI.md §4.6), and a group requires exactly
  that rather than a usage block;
* a missing *required* flag is a bare diagnostic on stderr and exit **1**, with
  no usage dump;
* `glog.Exit*` and `log.Fatal*` exit **1**; `glog.Fatal*` exits **255**.

### 4.1 Exactly how the verifier starts `ct_server`, and what it leaves at its default

Every group that drives the front end builds the same command line. A build
that ignores one of these flags, or that treats one of these values as an
error, fails that group:

    ct_server --log_config=<path to a config this group wrote> \
              --http_endpoint=127.0.0.1:<port> \
              --metrics_endpoint=127.0.0.1:<port> \
              --get_sth_interval=0 \
              --rpc_deadline=20s \
              --logtostderr

Both ports are loopback ports the group picks at run time, so no port number
is fixed anywhere. `--logtostderr` is glog's, not `ct_server`'s. To that line each
group appends exactly one more flag:

| the shape under test | appended | meaning |
| --- | --- | --- |
| **multi-backend** (a `LogMultiConfig`) | `--log_rpc_server=` — the empty string | the backends come from the file; the flag must not be consulted |
| **single-backend** (a bare `LogConfigSet`, step 01's shape) | `--log_rpc_server=<addr>` | the one backend, as at step 01 |

No other flag is ever passed and no positional argument is. Everything else
keeps the default CLI.md §2.1 gives for v1.0.5, and the verifier depends on
those defaults being the ones in force:

| flag | default at v1.0.5 | why the default matters here |
| --- | --- | --- |
| `http_endpoint` | `localhost:6962` | always overridden above; nothing may be hard-coded to the default |
| `metrics_endpoint` | `localhost:6963` | always overridden above, to a second free port |
| `log_rpc_server` | `localhost:8090` | **non-empty by default**, which is why the multi-backend runs pass it explicitly empty rather than omitting it |
| `rpc_deadline` | `10s` | overridden to `20s` above; both values must be accepted |
| `get_sth_interval` | `180s` | overridden to **`0`** above. CLI.md §2.1 defines `0` as *disabling the periodic internal get-sth entirely* — a supported setting, not an error. The process must start, bind, serve, and build no ticker from it. This is the one default the verifier deliberately contradicts, and it does so on every run |
| `max_get_entries` | `0` | left at its default, which §2.1 defines as *use 1000*, not *allow none* |
| `etcd_servers` | `""` | left empty; no group runs an etcd-backed election, and the empty state is the checked one |
| `allow_verification_with_non_compliant_keys` | `false` | left at its default; every corpus key is compliant |

The usage **block** is read for `certcheck` only, and there only for the flag
names, types and defaults CLI.md §4.3 gives (its descriptions are stated
nowhere and are out of scope). The usage **banner** — the one line `Usage of
<the path the binary was invoked by>:` — is read for `certcheck` and, by the
gate group, for each of the eight commands inherited from v1.0
except `chainfix`. Section 6 states exactly how both are compared.

## 5. Packages and symbols the verifier links against

### 5.1 What this step inherits from step 01

This step adds to step 01's tree; it replaces none of it. The drivers link the
same import paths step 01's did, and this list is complete **for the packages
that are yours to write**, so nothing here depends on still having step 01's
copy of this file:

`github.com/google/certificate-transparency-go` — the root `ct` package — and,
below it, `asn1`, `client`, `jsonclient`, `merkletree`, `tls`, `x509`,
`x509/pkix`, `x509util`, `trillian/ctfe` and `trillian/ctfe/configpb`; plus
`logid`, which is new at v1.0.5 and is the only package any driver here
links that step 01's did not. It is not the only *directory* v1.0.5 adds:
RELEASE-MAP.md §14 records that `ingestor/` also exists from v1.0.5. No
corpus page describes what is in it, no driver links it, and §9 below says it
is out of scope. Alongside your packages the drivers also link **third-party**
modules out of the benchmark-owned manifest §2 pins — `github.com/google/trillian`,
`google.golang.org/grpc` and `github.com/golang/protobuf`, with their
sub-packages. None of them is yours to write and none is checked as such; the
last row of §5.2 names them in full, so that the completeness claim above can
be read literally rather than charitably.

Every symbol step 01 named in those packages is still linked and still checked:
several groups here are `inherited-*` groups that re-run step 01's boundaries
against this tree, and they are **gate** groups: they add no new requirement,
and a single one of them failing means this whole step is not satisfied. The
corpus pages that enumerate that inherited surface, so it can be recovered
without step 01's documents, are TLS-CODEC.md §1–§5 (the codec, the struct tags
and the RFC 6962 structures), X509-FORK.md §1–§4 and §5.1 (the `asn1`/`x509`
fork, the `lax` relaxations, `NonFatalErrors`, precertificates, the poison
extension and `ParseTBSCertificate`), MERKLE.md §1–§4 (the three hashing rules
and the two proof algorithms), CLIENT-API.md §1 (the log client and
`jsonclient`), §3 (the root-package helpers) and §5 (`x509util`), HTTP-API.md
§1–§4 (the paths, the generic wrapper and the eight RFC 6962 endpoints), and
CONFIG.md §1–§2 (the `LogConfig` message and its field semantics). CLIENT-API.md
§2 (`ctutil`) and §4 (the log lists) are **not** inherited — both arrive after
v1.0.5 and §9 below says so. Nothing on the inherited pages changes at
v1.0.5 except where §5.2 below says it does **and where the inherited
page's own release column says it does** — read both, because *inherited* here
means "you already have this page", not "nothing on it moves". The one that
catches people is CLIENT-API.md §5 (`x509util`): its "When each of these
arrives" table dates **seven** function names to v1.0.5 and records one
outright removal, all of them on a page §5.1 has just called inherited. §5.2
below carries every one of them, so the table there is the delta list you can
work from.

**The inherited checkpoint, and it is checkable before you change anything:**
at v1.0 the tree holds **8** `package main` directories, so
`go build ./...` produces eight binaries (CLI.md §1, the counts line), and the
configuration file has exactly one legal shape — a bare `LogConfigSet`
(CONFIG.md §3). If your tree does not build those eight and load that shape,
the `inherited-*` groups here will not pass however much of this step you
finish.

### 5.2 What v1.0.5 adds or changes

| import path | what the drivers use |
| --- | --- |
| `…/logid` | `LogID`, `FromBytes`, `FromB64`, `FromB64OrDie`, `FromPubKeyB64`, `FromPubKeyB64OrDie`, `Bytes`, `String` |
| `…/trillian/ctfe` | `MultiLogConfigFromFile`, `ToMultiLogConfig`, `ValidateLogMultiConfig`, and `SetUpInstance` in its v1.0.5 shape — `SetUpInstance(ctx, client, cfg, opts InstanceOptions)` with `InstanceOptions{Deadline, MetricFactory, ErrorMapper}` |
| `…/jsonclient` | the three request helpers in their v1.0.5 shape — `GetAndParse(ctx, path, params, rsp)`, `PostAndParse(ctx, path, req, rsp)` and `PostAndParseWithRetry(ctx, path, req, rsp)`, the parameter lists of CLIENT-API.md §1.1 — returning `(*http.Response, []byte, error)`, and the rewritten backoff. CLIENT-API.md §1.1 fixes that backoff's ceiling and its jitter but not where it starts: the first interval is still **1 second** and the multiplier is still **2**, so the un-jittered sequence is 1 s, 2 s, 4 s … up to the 128 s (2^7) ceiling — v1.0's schedule in a new shape, which is what "the ceiling does not change; the shape does" means. A group times the first two 503 waits as lower bounds, so a backoff that starts below one second fails it however correct the rest of the object is |
| `…/client` | `RspError` |
| `…/merkletree` | `(MerkleVerifier).VerifyInclusionProofByHash(leafIndex, treeSize int64, proof [][]byte, root []byte, leafHash []byte) error` and `(MerkleVerifier).RootFromInclusionProofAndHash(leafIndex, treeSize int64, proof [][]byte, leafHash []byte) ([]byte, error)`, both new at v1.0.5 and both taking the **leaf hash** where the v1.0 pair took the leaf data. MERKLE.md §4.1 is where they are specified, under its "from v1.0.5" marker, and that page — not this table — is the normative one for what they compute; the row is here because §5.2 is the release delta and a priced group links these two |
| `…/x509` | the structured error surface of X509-FORK.md §5.2, in the shape §5.3 below gives. Also **`x509/names.go`**, which arrives at v1.0.5 (X509-FORK.md §5.2) and brings two exported types a driver builds as **composite literals** — so their field spellings and their element types are checked at compile time, and both declarations are given here in full rather than left to be inferred from CLIENT-API.md §5.2's rendering: `type OtherName struct { TypeID asn1.ObjectIdentifier; Value asn1.RawValue }` — the OID field is spelled **`TypeID`**, not `OID`, `Type` or `TypeId`, and `Value` is an `asn1.RawValue` whose `.Bytes` is what §5.2 renders as hex — and `type GeneralNames struct { DNSNames []string; EmailAddresses []string; DirectoryNames []pkix.Name; URIs []string; IPNets []net.IPNet; RegisteredIDs []asn1.ObjectIdentifier; OtherNames []OtherName }`, whose seven fields are declared in the order §5.2's renderer walks them. Note the element types the names do not give away: **`URIs` is `[]string`**, not `[]*url.URL`; `IPNets` is `[]net.IPNet`, not `[]*net.IPNet`; `DirectoryNames` is `[]pkix.Name`, from **the fork's** `github.com/google/certificate-transparency-go/x509/pkix` — the `pkix` that sits beside the `x509` package these two types are declared in — not the standard library's `crypto/x509/pkix` |
| `…/x509util` | **the whole v1.0.5 delta, not only the two renderers** — CLIENT-API.md §5.2's release table dates all of it, and §5.1 above does not override that table. (1) Two **new files** arrive. `x509util/files.go` brings `ReadPossiblePEMFile(filename, blockname string) ([][]byte, error)` and `ReadPossiblePEMURL(target, blockname string) ([][]byte, error)`, which CLIENT-API.md §5.2 specifies in full under "The two 'possible PEM' readers"; they are also how `certcheck` and `crlcheck` read their inputs. `x509util/revoked.go` brings `CRLToString(crl *x509.CertificateList) string` — the **fork's** `x509.CertificateList`, the type `x509/revoked.go` adds at this release (X509-FORK.md §5.2), and **not** `pkix.CertificateList`; CLIENT-API.md §5.2's signature block writes the same fork type, so the two pages agree. (2) The two `Oid…` helpers are **respelled** and the v1.0 spellings are gone: `OIDForStandardExtension(oid asn1.ObjectIdentifier) bool` and `OIDInExtensions(oid asn1.ObjectIdentifier, extensions []pkix.Extension) (int, bool)`; `OidFromNamedCurve` is **removed** outright with nothing replacing it. (3) `OIDForStandardExtension`'s answer set is RFC 5280's standard extensions **plus the two CT extensions X509-FORK.md §4.1 names** — `1.3.6.1.4.1.11129.2.4.3` (the poison extension) and `1.3.6.1.4.1.11129.2.4.2` (the SCT list extension) — and both of those return `true`. That is the part no other page states and the part a group checks directly: RFC 5280's set alone answers `false` for the poison OID and fails. (4) The two renderers `GeneralNamesToString(*x509.GeneralNames) string` and `OtherNameToString(x509.OtherName) string`, byte-exact per §6.1 item 2 |
| root `ct` | `LogEntryFromLeaf(index int64, leaf *LeafEntry) (*LogEntry, error)`, over the `LogEntry` of CLIENT-API.md §3 — `Index int64`; `Leaf MerkleTreeLeaf` (**a value**, not a pointer); `X509Cert *x509.Certificate`; `Precert *Precertificate`; `JSONData []byte`; `Chain []ASN1Cert`; `(*MerkleTreeLeaf).X509Certificate() (*x509.Certificate, error)`; `(*MerkleTreeLeaf).Precertificate() (*x509.Certificate, error)`; `Precertificate{Submitted ASN1Cert, IssuerKeyHash [32]byte, TBSCertificate *x509.Certificate}` |
| `github.com/google/trillian` (and `…/monitoring`, `…/crypto/keyspb`, `…/crypto/keys/pem/proto`, `…/crypto/keys/der/proto`), `google.golang.org/grpc` (and `…/codes`, `…/status`), `github.com/golang/protobuf/ptypes` (and `…/ptypes/any`) | **not your deliverable, and out of scope as such** (§3.4) — listed because a driver links them into the same binary as your code, which is what makes §5.1's completeness claim literal. At **this step's** pin the `TrillianLogServer` interface has **ten** methods and no `Unimplemented` embed, so a stub declares every one of them — `QueueLeaf`, `QueueLeaves`, `GetLatestSignedLogRoot`, `GetLeavesByIndex`, `GetLeavesByHash`, `GetInclusionProof`, `GetInclusionProofByHash`, `GetConsistencyProof`, `GetEntryAndProof`, `GetSequencedLeafCount` — and `SignedLogRoot` still carries the plain fields `TimestampNanos`, `RootHash`, `TreeSize`, `LogId` and `TreeRevision` rather than a marshalled `LogRootV1`, which is the shape your front end reads an STH out of. The pin **moved** between step 01 and this step (from `v0.0.0-20170911114049-c0a332878f82`) and neither of those two shapes moved with it; §7's warning that a pinned module may change shape is about later steps, not this one. Five groups build a stub backend from `trillian.RegisterTrillianLogServer` and serve it over `grpc.NewServer` on a loopback socket (`gate-endtoend`, `backend-routing-add-chain`, `backend-shared-by-two-logs`, `ctclient-inclusion-subcommand`, `getsth-consistency-param-errors`); `config-multi-validation` starts a real `ct_server` with no backend reachable at all and **imports neither module directly** — both are still linked into its binary, transitively, because `trillian/ctfe` and `configpb` pull them in; the claim here is about direct imports, never about the link. `ctfe-setup-instance` links no socket either, and builds `keyspb.PEMKeyFile` and `keyspb.PrivateKey` messages with `ptypes.MarshalAny`, registers the two trillian key handlers, hands `InstanceOptions` a `monitoring.MetricFactory`, and reads `status`/`codes` values back through the error mapper. All of these come from the benchmark-owned manifest §2 pins — `github.com/google/trillian v1.0.4-0.20171121150315-bc4e6834483a` and `google.golang.org/grpc v1.7.3` — so what you leave in `go.mod` cannot change them, and you implement no part of either |

The two `jsonclient` signature changes and the `SetUpInstance` change are
**breaking**: a driver compiled against the step-01 shapes will not build here,
and that is deliberate.

Both key forms do occur at this step, so do not assume either one. The
configuration fixtures under `configs/` name a file with a `keyspb.PEMKeyFile`,
as CONFIG.md §2.3 and the minimal configuration at §5 show. `configs/` holds
seven text protos — `multi-valid.textproto`, `multi-two-backends.textproto`,
`multi-unknown-backend.textproto`, `multi-dup-prefix.textproto`,
`v10-single-valid.textproto`, `v10-single-unknown-field.textproto` and
`v10-single-missing-key.textproto` — of which the first six carry that key block
and the last deliberately carries none. JSON twins exist for the three
`v10-single-*` files only; there is no JSON form of any `multi-*` fixture
(section 6). Of those seven the groups at **this** step read five: the four
`multi-*` files and `v10-single-valid.textproto`.
`v10-single-unknown-field.*` and `v10-single-missing-key.*` are the config
loader's negative inputs from step 01, re-checked again at v1.1.4 but not here.
The corpus is one directory shared by every step, so their presence is not a
hint that something at v1.0.5 reads them: nothing at this step checks what
your code does with either.
The `log_config` values that the driver synthesises when it starts a real
`ct_server` inline the key as a `keyspb.PrivateKey` carrying raw DER instead.
The reason is a property of the pinned dependency rather than of your code, and
it is worth stating because CONFIG.md §2.3 reads the other way at first
glance. §2.3's "a `password` left empty means the file is not encrypted" is the
*field's* semantics, and it is right; but the PEM handler registered by the
trillian revision this benchmark pins declines to open a PEM block when
`password` is empty, encrypted or not, and the corpus log key —
`minted/keys/log-key.pem`, which is what every `PEMKeyFile` in `configs/` names
— is an unencrypted `PRIVATE KEY` block with no `password` beside it. A
`PEMKeyFile` therefore yields no signer at this step however correct your
loader is, which is why the live runs use the DER form.

One group feeds a `PEMKeyFile` anyway, and checks only that the refusal it gets
back **names the path it was given** — observable evidence that `private_key`
was routed through the key-handler registry rather than opened by the loader or
handed to the DER handler — and that supplying a non-empty `password` for the
same file fails *differently*, so the message reached the handler's decryption
step. No wording is checked in either case: the sentences around the path are
trillian's. The key material is the same fixture either way, and CONFIG.md §2.3
describes both message types as valid.

### 5.3 The structured error surface, in full

X509-FORK.md §5.2 names `ErrorID` and
`func NewError(id ErrorID, args ...interface{}) Error`; RELEASE-MAP.md §2 names
`x509.Error` and `x509.Errors` alongside them. This is the whole of the shape a
driver needs in order to write the call, and the whole of what is checked:

    type ErrorID int

    const (
        ErrInvalidID ErrorID = iota   // the zero value, no defect
        ErrInvalidCertList            // fatal
        ErrTrailingCertList           // fatal
        ErrUnexpectedlyCriticalCertListExtension   // NOT fatal
        // ... one identifier per defect, in a fixed order that is only
        //     ever appended to, so an id already issued never moves
        ErrMaxID                      // one past the last defined id
    )

    type Error struct {
        ID      ErrorID
        Summary string
        Fatal   bool
        // An implementation may carry further descriptive fields
        // (category, field name, spec reference). None of them is checked.
    }

    func NewError(id ErrorID, args ...interface{}) Error
    func (e Error) Error() string        // a value receiver, not a pointer one

    type Errors struct {
        Errs []Error
    }

    func (e *Errors) Empty() bool
    func (e *Errors) AddID(id ErrorID, args ...interface{})
    func (e *Errors) Error() string
    func (e *Errors) Fatal() bool
    func (e *Errors) FirstFatal() error
    func (e Errors) Filter(filtered []ErrorID) Errors

What each one must do, and nothing beyond it is checked:

* `NewError(id, args…)` returns that id's template with `args` formatted into
  its `Summary`, and with the fatal flag that belongs to the id. The flag
  belongs to the defect, not to the call site. An `id` at or beyond `ErrMaxID`
  is not a panic and not a zero value: the result carries that `ID` and is
  **fatal**.
* `Error()` on a single error carries that error's `Summary` to the caller,
  and no more than that is checked of it: whatever else it decorates the
  message with — a severity letter, an id number — is not pinned. On a set it
  carries every member's `Summary`, and again no more than that is checked of a
  **multi-member** rendering: how the members are separated from one another,
  and what heading (if any) introduces them, is not stated and is not
  required. A set holding exactly **one** member is the one set rendering that
  **is** pinned, and it carries **that member alone**:
  `x509.Errors{Errs: []x509.Error{e}}` and `e` render byte for byte alike —
  no heading, prefix or separator in front of the member, and nothing after
  it.
* `Empty()` is true exactly while `Errs` is empty. `AddID(id, args…)` appends
  `NewError(id, args…)` — one entry per call, in call order.
* `Fatal()` is true exactly when some member has `Fatal` set, and
  `FirstFatal()` returns the first such member, **as an `x509.Error` value**
  behind the `error` interface (so a caller can type-assert `x509.Error` and
  read its `ID` back), or `nil`.
* `Filter(ids)` returns a new `Errors` holding the members whose `ID` is not in
  `ids`, in their original order.

Two identities are named above because a group exercises the fatal and the
non-fatal case and needs a fixed example of each: `ErrInvalidCertList` is
fatal, `ErrUnexpectedlyCriticalCertListExtension` is not. No further identity
is checked, and no defect's wording is.

## 6. Formats crossing the boundary

Step 01 listed most of these, and step 01's `TEST_INTERFACE.md` is replaced by
this file, so the list is given here in full rather than referred to. One entry
— the key one — is **not** what step 01 said, and is marked.

* **TLS encodings.** `MerkleTreeLeaf`, `TimestampedEntry`, `DigitallySigned`,
  the SCT signature input and the `TreeHeadSignature` input are compared byte
  for byte against vectors the *verifier* holds. Those vectors are not in
  `ct-spec/` and you cannot read them; they were produced from the field
  orders, tag parameters and widths of TLS-CODEC.md §2, §3 and §5, which
  determine the encoding completely. Re-derive an encoding from those rules and
  it will match; guess at one and it will not.
* **JSON bodies.** Field names, types and base64 padding exactly as
  HTTP-API.md §3 and §4 give them. Binary fields are standard base64 with
  padding; an empty list is `[]`, never `null`. A response is read field by
  field — nothing compares a whole JSON document as text.
* **Leaf hashes.** `SHA-256(0x00 ‖ TLS(MerkleTreeLeaf))`, compared as bytes
  (MERKLE.md §1), and the Merkle node and root hashes of MERKLE.md §1–§4.
* **PEM, and how a private key reaches a signer — read this even if you read
  step 01's version.** Root certificate files are given to `ct_server` by path,
  as `roots_pem_file`. The log's **private key** is a `google.protobuf.Any`
  matched against the key-handler registry `ct_server` populates at start-up
  (CONFIG.md §2.3); the loader must never open the path itself. Step 01 added
  that every configuration its groups build an instance from names a
  **passphrase-encrypted** file with the passphrase in `password`. That was
  true of step 01's corpus and it is **false here**: every `keyspb.PEMKeyFile`
  in `configs/` names `minted/keys/log-key.pem`, an unencrypted `PRIVATE KEY`
  block, and carries **no `password` field at all**, while the configurations
  the groups synthesise for a live `ct_server` inline that same key as a
  `keyspb.PrivateKey` carrying raw DER (§5.2). Assume neither key form and
  neither encryption state; route `private_key` through the registry, which is
  the only part of this that is yours to write.
* **The configuration file's encoding.** The log config is a protocol-buffer
  message, and at v1.0.5 it is parsed as **text proto and nothing else**
  (CONFIG.md §4.2), despite `--log_config`'s help text saying JSON. Unknown
  field names are refused, which is what makes CONFIG.md §1's
  field-arrives-at-release-N table observable.
* **The multi-backend configuration message.** `LogMultiConfig` with its
  `LogBackendSet` and `LogConfigSet`, as CONFIG.md §3 gives it — generated
  messages, so each field carries the `Get<Field>()` accessor beside it
  (CONFIG.md §1.2), and a driver calls those as well as reading the fields. The corpus
  ships the multi-backend configs as text proto only. Through v1.1.0 the loader
  parses text proto and nothing else (CONFIG.md §4.2), so a JSON config file is
  a parse failure at this step whatever its contents; the `.json` twins under
  `configs/` are the negative input for the releases that accept JSON and are
  not a multi-backend fixture here. A group checks the content verdict of a
  config it has actually loaded, not the file's encoding.
* **Backend specs.** A backend's address is an opaque non-empty string at
  validation time; a group may hand you an address that answers nothing.
* **Backend error statuses and the error mapper.** `ctfe-setup-instance`
  answers the backend's call with `codes.NotFound` and checks the status your
  front end returns, through an `InstanceOptions.ErrorMapper` the driver
  supplies: a mapper that **claims** the error decides the status (the driver's
  mapper answers **418**, so 418 is what must come back), and one that
  **declines** leaves HTTP-API.md §6.1's table in charge (`NotFound` → **404**).
  Both halves of §6.1's "consulted before the table" are therefore checked here.
  Only the status is read, never the body.
* **`logid` string forms.** The base64 of a 32-byte log id, and the rejection
  of anything that is not exactly 32 bytes.

### 6.1 The strings this step compares whole

Section 3.3 checks a message on the identifiers the documents fix and never on
the sentence around them. The exception is a string the corpus prints
*complete*, and this is the list of them for this step. Everything named here
is compared as a whole string — including any clause the Go standard library
contributes and any trailing newline the corpus shows — and **the list is
exhaustive: no group at this step compares any other message, rendering or
program output as a whole string.**

1. **`get-sth-consistency`'s parameter-error body.** All six detail texts of
   HTTP-API.md §4.2, each inside §2.1's frame, giving the whole body
   `Bad Request\nfailed to parse consistency range: <detail>\n` — the status
   line, the newline and the final newline are `http.Error`'s and are compared
   with the rest. Both range checks carry their two offending values, so the
   bodies for `?first=-1&second=2` and `?first=5&second=2` end
   `first and second params cannot be <0: -1 2` and
   `invalid first, second params: 5 2`. §4.2 prints all six and both value
   suffixes, so nothing here is left to you.
2. **`x509util`'s two renderers.** `GeneralNamesToString` over a pinned
   `GeneralNames` and `OtherNameToString` over a pinned `OtherName`, each as
   the whole returned string. CLIENT-API.md §5.2 fixes the field order, the
   per-entry prefixes, the `", "` join and the lower-case hex completely, so
   the rendering is a format and not prose.
3. **`logid.String()`.** Exactly `logid:[<standard base64 of the 32 bytes>]`
   (CLIENT-API.md §5.5).
4. **Two lines `ctclient` prints.** The `sth` line
   `Got STH for V1 log (size=<n>) at <log-uri>, hash <hex>`, compared from
   `Got STH` onward — the `<when>:` prefix in front of it is Go's
   `time.Time.String()` in the local zone and is *not* compared — and, in the
   usage block on stderr, the `inclusion` command's own row, `   inclusion   `:
   three spaces of indent and the name padded to the twelve-character column.
   CLI.md §3.1 prints both the line and the block's geometry in full, and the
   row is how the command's arrival at v1.0.5 is observed at the process
   boundary at all.
5. **The `flag` package's usage banner and flag lines.** `certcheck -help`
   writes nothing to stdout, exits 0, and its stderr *begins* with the line
   `Usage of <the path the binary was invoked by>:`; the flags of CLI.md §4.3
   are then matched as whole lines in the `  -<name>` / `  -<name> <type>` form
   that page's preamble fixes, and the block must contain no `(default `
   anywhere. The same banner line, byte for byte, is required of each of the
   eight commands inherited from v1.0 when run with `--help`
   (`chainfix` excepted — §4). None of this text is yours; it is listed here
   because the flag *set* and the *binary's own identity* are not otherwise
   observable from outside the process, and because CLI.md's preamble states
   the shape exactly.
6. **Outputs required to be empty** are compared to the empty string, which is
   the degenerate case of the same rule: `certcheck` reading a certificate
   without `-verbose` writes nothing to stdout and nothing to stderr, and
   `certcheck` with no positional argument writes nothing to stdout.

Four kinds of comparison that look like this one but are not, so that the list
above stays readable as exhaustive:

* **Documented fragments matched inside an undocumented sentence.**
  `Invalid config:` (CLI.md §2.2), `need to specify RootsPemFile` and
  `need to specify PrivateKey` (CONFIG.md §4.3), `unknown extended key usage:
  <name>` (CONFIG.md §2.4) and jsonclient's `got HTTP Status "<code> <text>"`
  (CLIENT-API.md §1.1) are each looked for *within* a diagnostic whose
  surrounding words are never read. That is section 3.3's ordinary rule, not
  this exception.
* **Bytes the harness itself served.** `jsonclient`'s new raw-body return and
  `client.RspError`'s body are compared against the exact bytes the group's own
  stub log wrote, so what is checked is that your code passed them through
  unaltered, not that it chose any wording.
* **An implementation compared with itself.** `crlcheck` over the same CRL in
  DER and in PEM must produce the *same* stdout, and a one-member `x509.Errors`
  must render as its single member does. Both fix a relationship and no text.
* **Paths and names the group supplied.** A file name echoed back in a
  diagnostic, a log's prefix, a backend's name: identifiers under section 3.3,
  matched as substrings.

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
| `inherited-leaf-hash-known-answers` | gate |
| `inherited-merkle-proofs` | gate |
| `inherited-tls-codec` | gate |
| `backend-routing-add-chain` | requirement |
| `backend-shared-by-two-logs` | requirement |
| `certcheck-binary` | requirement |
| `client-rsperror` | requirement |
| `config-multi-from-file` | requirement |
| `config-multi-validation` | requirement |
| `config-single-to-multi` | requirement |
| `crlcheck-binary` | requirement |
| `ctclient-inclusion-subcommand` | requirement |
| `ctfe-setup-instance` | requirement |
| `getsth-consistency-param-errors` | requirement |
| `jsonclient-body-and-backoff` | requirement |
| `leaf-precertificate-accessor` | requirement |
| `logid-package` | requirement |
| `merkletree-inclusion-by-hash` | requirement |
| `serialization-logentry-from-leaf` | requirement |
| `x509-structured-errors` | requirement |
| `x509util-files-and-oids` | requirement |

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

* **Anything dated after v1.0.5**, including `ctutil`, the log lists and
  the per-log validator that arrives at v1.0.21.
* **Which backend a given log is routed to when the config does not say.**
  There is no default and no fallback to check: a log names its backend or the
  whole file is refused (section 4).
* **Everything the structured error surface renders, and every defect identity
  beyond the two section 5.3 names.** `Error()` on an error or a set is checked
  only for carrying the summaries through (section 5.3); the severity letter,
  the id number, the separators, the headings, `VerboseError`'s longer form,
  the per-category `String()` methods and the per-defect wording are stated on
  no page and are checked nowhere, at this step or any later one. Implement them
  as you see fit.
* **`ingestor/`.** v1.0.5 adds this directory upstream alongside `logid`
  (RELEASE-MAP.md §14) and no corpus page describes what is in it. Nothing at
  this step links it, builds it or checks it, and you are not required to
  create it.

Routing itself **is** in scope, and end to end. Two groups stand up **two** stub
backends on loopback at once, start your `ct_server` against a config that
names both, submit to each log's own prefix over HTTP and require that each
submission arrived at exactly the socket its log named, once -- so the mapping
is checked live, not only as the validator's return value. Three further groups
drive a real `ct_server` against a single stub backend, and one more starts the
binary with **no** backend reachable at all, on a config that names an
undefined backend or repeats a backend name, and requires it to exit 1 saying
`Invalid config:` -- validation is step 2 of the eight-step start-up CLI.md
§2.2 fixes, so the refusal must happen before the first dial and before the
first bind. All of them build the command line section 4.1 gives, and nothing
else.

The rewritten backoff is the one place at this step where a group looks at
elapsed time, and section 8's exception is what governs it: a 503 retry must
arrive **no earlier** than the interval CLIENT-API.md §1.1 documents --
honouring `Retry-After` in both of its documented spellings -- and two
successive 503 waits must grow. Those are two lower bounds and an ordering,
read from the stub log's own arrival timestamps. Every other retry case is
checked by *counting* the requests the stub saw, never by timing them: the 408
arm is required to make a second request, not to make it within any budget.
One case does hand a call a context that is *already* bounded -- a 300 ms
deadline against an endpoint answering 503 forever -- and it is worth being
exact about how that sits with section 8. It compares no duration: what it
checks is that `context.DeadlineExceeded` reaches the caller unwrapped enough
for `errors.Is` to find it, and that the stub saw exactly one request. The only
timing it leans on is the same documented lower bound as the rest of this
paragraph -- a first interval of **one second** cannot fit a second attempt
inside 300 ms -- so a conforming backoff passes it with a 3x margin and a
slow machine cannot fail it. Section 8's rule is about what is *checked*; it
does not stop a case from supplying a context that expires on its own.
Three parts of CLIENT-API.md §1.1's backoff are deliberately **not** checked, in
either direction, and this is the whole of what is left out: the up-to-250 ms
jitter; the 128-second ceiling *as such* — that two successive waits grow is
checked, but no group drives the wait far enough to reach the ceiling and then
observes that it stopped growing; and `decreaseMultiplier`, the shrink after a
success. Build all three as §1.1 describes them and expect nothing here to
look.

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

The milestone is a second legal shape for the configuration file, not a
replacement. After this step one validator must see both shapes, because the
single-backend file is **converted** into the multi-backend form on load and
everything downstream sees only the converted result. A build with two parallel
validators will pass the easy cases and fail the cross-referencing ones.

The rest of the step is smaller but real: `logid` exists so that a log id and a
backend tree id can never be confused again; `jsonclient` starts handing its
callers the raw response body, which changes three signatures; the backoff is
rewritten with jitter; and `SetUpInstance` begins its long migration into an
options struct.

Do not break step 01 while doing it. Several groups at this step are
`inherited-*` groups that re-run step 01's boundaries, and they are gate
groups: preserving step 01 is not a requirement in its own right, but breaking
it means this step is not satisfied however complete the new work is.
