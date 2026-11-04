# TEST_INTERFACE — step 08 of 13 (certificate-transparency-go v1.1.0)

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

Section 7 warns that proof of life is not always free and points here for
whether that applies at this milestone. **It applies here**, in the third of
the three shapes section 7 names — the module is still supplied, the symbol
still resolves, and the *shape* of it has changed.

Section 2's pinned manifest moves `github.com/google/trillian` from **v1.2.1**,
the revision v1.0.21 pinned, to
**v1.2.2-0.20190612132142-05461f4df60a**. At that revision
`trillian.SignedLogRoot` stops carrying the tree head as scalar fields of its
own — `timestamp_nanos`, `tree_size` and `root_hash` are gone — and the signed
head travels instead as one opaque `log_root`, unmarshalled into a
`github.com/google/trillian/types.LogRootV1` (§5). The inherited CTFE still
reads the fields directly, so `go build ./...` in `/app` fails on the very
first run:

    trillian/ctfe/handlers.go:463:35: slr.TimestampNanos undefined
        (type *trillian.SignedLogRoot has no field or method TimestampNanos)
    trillian/ctfe/handlers.go:464:34: slr.TreeSize undefined
        (type *trillian.SignedLogRoot has no field or method TreeSize)
    trillian/ctfe/handlers.go:540:52: rsp.SignedLogRoot.TreeSize undefined

The compiler stops at its ten-error limit inside that one package, so what it
prints is not the whole of the repair: `trillian/ctfe/sth.go` reads the same
three fields and is never reached before the limit. Comparing section 2's
require list against the tree's imports will not show you any of this, because
nothing is missing from either; only building the tree does.

So section 1's build obligation is not met, and the gate group `build-clean` —
which section 7's table lists as **GATE** — fails its `product-tree-builds`
case before you have changed anything. Three consequences, because none of
them is guessable from section 7 alone:

* A tree that implements nothing new satisfies nothing here: the **gate** itself
  fails, however much of the milestone below is finished.
* **Three programs are not built, and the other thirteen are.** Be precise
  about what that loses, because the convenient sentence — "the tree does not
  build, so there are no binaries" — is false here. The harness builds
  candidate binaries with a single `go build -o <dir>/ ./...`, and the failure
  at v1.1.0 is a *type* error; a type error is confined to the packages
  that depend on the broken one, so the command still writes a binary for
  every package that compiles. Measured on the v1.0.21 tree this step
  starts from, thirteen of its sixteen programs survive. What you lose is
  `ct_server`, which imports `trillian/ctfe` directly; `ct_hammer`, which
  imports it directly and again through `trillian/integration`; and
  `migrillian`, which reaches it through `trillian/migrillian/core`. The five
  groups of section 4 that start `ct_server` therefore fail for want of it,
  `gate-endtoend` included; three of `binaries-eighteen-mains`'s cases fail
  for this reason alone, the two for `server` and `hammer` failing for the
  separate reason that they are this milestone's own new work; and every
  driver that links `…/trillian/ctfe` fails to compile. A log of that shape is
  the signature of exactly this, and of nothing to do with the admission
  window, the lax ASN.1 parsing or the `certcheck` rename below. (An
  *unresolvable import* is the case that suppresses the whole command and
  leaves no binaries at all. That is a real failure mode elsewhere in this
  series, but it is not the one in front of you.)
* Pinning a different `github.com/google/trillian` is not a repair. Section 2
  fixes the dependency universe, the benchmark restores its own `go.mod` and
  `go.sum` over yours before it builds anything, and checking is offline
  against a module cache that holds only the pinned revision — so the
  adaptation has to happen in the product's own code.

This is inherited plumbing, not this milestone's work: it adds no requirement of
its own and it is on none of the feature lists below. Do it first and the rest
of the step is what sections 4 to 6 describe.

---

## 4. Command-line programs the verifier runs

v1.1.0 has **18** `package main` directories, up from 16 at
v1.0.21 (CLI.md §1) — the widest the tree has been *so far*, but not
the widest it ever gets: v1.1.3 reaches 19. The number checked here is 18
because that is v1.1.0's count, not because it is a maximum. The
submission proxy's `server` and `hammer` arrive here; `gossip_server`,
`goshawk` and `gosmin` are all still present, and all three leave at the next
step.

| program | directory | what a group does with it |
| --- | --- | --- |
| `ct_server` | `trillian/ctfe/ct_server/` | started as a subprocess against a config a group writes; checked on its 18-flag set, on `--mask_internal_errors` existing, on `--metrics_endpoint` and `--log_rpc_server` having **empty** defaults, on the escape hatch step 7 removed still being gone, on CORS, on `/healthz`, on the `/` catch-all answering an empty **200** while every other unclaimed path answers **404**, on `/metrics` answering on the HTTP mux, and on `--handler_prefix` moving the per-log entrypoints — the old path must 404 — while `/`, `/healthz` and `/metrics` all stay at the root (HTTP-API.md §1.1, §7) |
| `certcheck` | `x509util/certcheck/` | run with the renamed flags; checked on which names the binary accepts and rejects, on the new defaults, on the exit status of CLI.md §4.3, and on what the two flags below actually **do** — a binary that accepts a new name and ignores it is not the rename this milestone asks for (CLI.md §4.3) |
| submission `server` | `submission/server/` | checked on being built and on its flag set (CLI.md §4.9) |
| the other fifteen | see CLI.md §1 | built and enumerated; `ct_hammer`, `migrillian`, the gossip three and the rest must all still exist |

The flag-grammar rules of step 06 still hold, and nothing here licenses the
tree to stop obeying them — and **two** of them are re-checked at this step,
both read only off the exit status:

* **an unrecognised flag exits 2** — checked on `certcheck`, by running it
  with each of the two deleted flag names, and on `ct_server`, by passing it
  the escape hatch step 7 removed;
* **`-h` exits 0** — read on **all eighteen** binaries, with `chainfix` the
  one documented exception at **2**, because it indexes `os.Args` before
  `flag.Parse` (CLI.md preamble:44-50 and §4.6, which also tells you not to
  add an argument-count check to make it 0).

Nothing at this step reads stderr for `Usage of <prog>:`, and neither the
`glog.Exit*` → **1** row nor the `glog.Fatal*` → **255** row of CLI.md's exit
table is re-checked here. `certcheck`'s own verdict status — **1** if any
certificate failed, **0** otherwise (CLI.md §4.3) — is a different rule, and
it *is* checked: see immediately below.

`certcheck`'s exit status is the only channel a group reads, and two of the
flags CLI.md §4.3 introduces are checked through it on their **effect**, as is
one thing the flags do by being **absent** (below):

* **`-strict` (Bool, default `true`)** decides what a *non-fatal* parse error
  costs. `certcheck` reads each target through the `ParseCertificate` family,
  so a certificate that parses only with a `NonFatalErrors` (X509-FORK.md §3)
  still yields a usable certificate rather than a failure. With `-strict` left
  at its default that non-fatal error is nonetheless counted into the failure
  total CLI.md §4.3 describes, so a run over such a certificate exits **1**;
  with `-strict=false` it is not counted, and the same run exits **0**. A
  *fatal* parse error fails the target either way, `-strict` or not. `-strict`
  governs nothing else: it does not switch any `check_*` test on or off and it
  has no effect on `--validate`.
* **`-check_time` (Bool, default `false`)** is the renamed `timecheck`, and it
  is the validity-window test inside `--validate`: with `-check_time=true` a
  chain whose leaf is expired, or not yet valid, is a failure and the run exits
  **1**; with `-check_time=false` the same chain verifies and the run exits
  **0**. It changes nothing when `--validate` is not given.

And one behaviour that the pool flags gate by being **left unset**: with
neither `--root` nor `--intermediate` given, `--validate` reads the file it was
handed as a chain, adopts a self-signed last certificate as the trust anchor
and takes the rest as intermediates. CLI.md §4.3 is explicit that from
**v1.0.21** — so already inherited here, and unchanged at v1.1.0 — those
are two independent conditions rather than one: `--intermediate` alone does
not switch anchor adoption off, and `--root` alone does not switch intermediate
adoption off. A group drives all three arrangements and checks the exit status;
the behaviour is inherited, the command lines that reach it here are not,
because they carry `-check_time`, and the group they sit in cannot pass without
the rename.

The **18** is read off `ct_server --help`, so it is 18 *names on the command
line*, and it is CLI.md §2.1's v1.1.0 column read literally: every row marked
for v1.1.0, glog's seven excluded. That includes **`by_range`**, which the
`ctfe` package declares as a `flag` global rather than `ct_server`'s own
`main.go` — it is on the command line, so it counts. Only glog's seven are
filtered out, and nothing else is.

Two further `ct_server` obligations are checked here:

* **The escape hatch stays gone.** `--allow_verification_with_non_compliant_keys`,
  removed at step 07, is checked three ways: it must not appear in `--help`,
  passing it must exit **2**, and the 18-name flag set above must not contain
  it. Only the exit *status* is compared — the message is the Go `flag`
  package's, not the product's, and is out of scope.
* **CORS.** The whole mux is wrapped permissively (HTTP-API.md §7): an ordinary
  `GET` carries `Access-Control-Allow-Origin: *`, and an `OPTIONS` preflight
  is answered by the wrapper with a `Vary` naming `Origin`,
  `Access-Control-Request-Method` and `Access-Control-Request-Headers`.

`/healthz` is new and is checked over a real socket: **200**, body exactly `ok`
with no trailing newline, served at the root of the HTTP endpoint whatever
`--handler_prefix` is set to, and answered without consulting the backend.
`/`, `/healthz` and `/metrics` are the three paths a handler prefix never
moves (HTTP-API.md §1.1).

`/metrics` is checked on **where** it answers and on nothing else.
`--metrics_endpoint`'s default is empty from v1.1.0 (CLI.md §2.1), and an
empty setting is not a third case: it means *the HTTP endpoint*, so the process
opens one listener and `/metrics` answers **200** on the same mux as `/` and
`/healthz`. That is HTTP-API.md §7's "otherwise on the same mux" branch, which
an empty value selects; only a non-empty `--metrics_endpoint` naming a
different address gets a listener of its own. What the body counts is out of
scope (§9).

## 5. Packages and symbols the verifier links against

| import path | what the drivers use |
| --- | --- |
| `…/trillian/ctfe` | **`NewCertValidationOpts`** — new here, and the reason admission can be checked without a clock: the drivers build the options with a pinned time. Also `ValidateLogConfig` over the new fields, `SetUpInstance` returning **`*Instance`**, and `InstanceOptions`, which the drivers fill with `Validated *ValidatedLogConfig` (replacing `Config`), `MaskInternalErrors bool` and the inherited `Client`, `Deadline`, `MetricFactory` and `RequestLog`. The third v1.1.0 field, `STHStorage`, is **not** exercised at this step (§9) |
| `…/trillian/ctfe/configpb` | `LogConfig.reject_unexpired` (17), `frozen_sth` (16), `reject_extensions` (18), and the `SignedTreeHead` message — none of which exists at v1.0.21 |
| `…/asn1` | `UnmarshalWithParams(b, v, "lax")` and the `asn1:"lax"` struct tag, over pinned DER/BER byte strings |
| `…/x509` | `ParseCertificate`, `ParseCertificates` and `ParseTBSCertificate` in their v1.1.0 shape, with `IsFatal` and `NonFatalErrors`. All three accumulate non-fatal errors and return a usable value alongside the strict error they recorded — but the three are **not** interchangeable, and X509-FORK.md §3.2 is checked on the difference: `ParseCertificates`'s own `lax` retry cannot fire, so a defect in the *outer* certificate structure that `ParseCertificate` recovers is **fatal** for the plural function, while everything recovered deeper in — inside the shared per-certificate work — is recovered by both. `ParseCertificates` also unions its `NonFatalErrors` over every certificate in a concatenation |
| `…/ctpolicy` | `CTPolicy`, `ChromeCTPolicy`, `AppleCTPolicy` — named in a compile probe, not called (CLIENT-API.md §7.1) |
| `…/submission` | `Submitter`, `AssignedSCT` — named in a compile probe (CLIENT-API.md §7.2) |
| `…/schedule` | `Every` — named in a compile probe (CLIENT-API.md §7.3) |
| `…/watcher` | `NewDiff`, `DiffEvent` — named in a compile probe (CLIENT-API.md §7.4). The package is born here and is checked on existing and compiling under those two names; its behaviour is out of scope |
| `…/loglist2` | `LogList`, `LogRoots` — named in a compile probe (CLIENT-API.md §4.2) |
| `…/jsonclient` | `RspError` as a type of its own, with `client.RspError` as an alias, and `Options.UserAgent` (CLIENT-API.md §1.1) |
| `…/client` | `New`, `LogClient.GetSTH` and `LogClient.AddChain`, used only as the two request paths through which `RspError` and `UserAgent` are observed |

`ValidateChain(rawChain [][]byte, opts CertValidationOpts)` returns the
**verified path**, not the submission: leaf first, then each issuer in order,
**including the trust anchor it terminated at**, even when the submission did
not carry that anchor. CLIENT-API.md §8.3 states the rule in full and it has
not changed at any release: the returned path reproduces the submission
element for element, and is either exactly as long as the submission or
exactly one certificate longer — that one extra element being the anchor the
submission stopped short of. Where a shorter path also reproduces the
submission it is the one returned, because path building reaches the
configured roots before it recurses through the submission's own
intermediates; a path that skips, reorders or substitutes a submitted
certificate is discarded even though it verifies.

Every chain the groups here submit is an instance of the first case: a
two-element `[leaf, intermediate]` against a pool holding the root and not
the intermediate, which therefore comes back as **three** certificates. That
is the number the groups count, and a refusal is `len(chain) == 0` together
with a non-nil error. Read §8.3 for the arrangements that give a different
number; none of them occurs in this step's fixtures.

The path is built with time checks **disabled** (HTTP-API.md §5.2), so an
expired issuer on the path is not by itself a refusal: from v1.1.0 the
only expiry test is the explicit, leaf-only one that runs before path building.

`ctfe.ParseBodyAsJSONChain(r *http.Request)` is the exported front half of the
write handlers, new at v1.1.0 and taking the request **alone** — the
per-log context parameter its unexported predecessor took is gone
(CLIENT-API.md §8.3). It reads the body, decodes a JSON object with a `chain`
array of base64 DER into a `ct.AddChainRequest`, and refuses an empty or
absent `chain`. It is an envelope decoder and nothing more: element order is
preserved exactly as submitted, unknown JSON members are ignored, and whether
an element is a parseable certificate is not its question — that is §5's. It
is checked directly as a function as well as through the handlers.

Three claims about `jsonclient` are checked directly. CLIENT-API.md §1.1
*declares* the shapes they are about — `Options.UserAgent` (v1.1.0), the
`RspError{Err, StatusCode, Body}` struct, its `Error() string` method and the
`client.RspError` alias — but the three behaviours below are stated **here**
and nowhere else, so this is the contract for them:

* `client.RspError` is a **type alias** for `jsonclient.RspError`, not a
  separate defined type. One `reflect.Type`, whose package path is
  `…/jsonclient`, reached by either name; a value returned through `client`
  type-asserts to either spelling.
* `RspError.Error()` returns the wrapped `Err`'s text **and nothing else** —
  it does not append the status or the body. The status and body are reached
  through the `StatusCode` and `Body` fields, and a non-200 read populates
  both.
* `Options.UserAgent`, when non-empty, is sent as the `User-Agent` request
  header on the GET path and on the POST path alike. When it is empty the
  product sets no header of its own; what the standard library then sends is
  out of scope.

Steps 01 to 07 named more than the table above, and you do not have their
`TEST_INTERFACE.md` — each workspace carries only its own, so that surface is
written out here rather than referred to, and every path in it has been
checked against v1.1.0 before being listed. The table above and the two
below are together the complete set of packages a driver at this step links
against; nothing else has to be guessed at. Nothing a driver here imports has
been *withdrawn* at v1.1.0 — this milestone adds five packages and
removes none — so there is no must-not-resolve row in either table; the only
path checked on failing to resolve is the synthetic negative control of §6.

The tree is cumulative: every package the earlier milestones built is still in
it, still has to compile, and nothing this step adds is allowed to break it.
What a driver here actually *links* is narrower than that, and it is exactly
the tables on this page. Narrower again is what is re-*checked*: the subset
below, by this step's five `inherited-` groups, which §7 lists as **GATE** —
they add no new requirement and are mandatory. The rest of the earlier surface stays
checked where it was introduced; a name absent from these tables is not a name
this step licenses you to remove, only one no group here re-reads. These are
the inherited names a driver at this step reaches for, so that nothing is
reached for that is not written down:

| import path | inherited symbols a driver at this step names |
| --- | --- |
| `…/trillian/ctfe` | `ValidateChain`, `NewPEMCertPool` and `*PEMCertPool` with `AppendCertsFromPEM`, `CertValidationOpts`; `QuotaUserForCert(cert *x509.Certificate) string` and the `CertificateQuotaUserPrefix` constant it prefixes with (ARCHITECTURE.md §2.2, CLIENT-API.md §8.6); and `DefaultRequestLog`, the value the three `ctfe-` groups fill `InstanceOptions.RequestLog` with |
| `…/x509` | `ExtKeyUsage` and the `ExtKeyUsageCodeSigning` constant, passed as `NewCertValidationOpts`'s last argument; `ExtKeyUsageClientAuth` and `ExtKeyUsageServerAuth`, read back off a parsed certificate; `*Certificate` with the parsed fields the fixtures are compared on; and the precertificate pair `BuildPrecertTBS` and `RemoveCTPoison` |
| `…/asn1` | beside the `lax` surface in the table above, the strict entry point the lax cases are compared against — `Unmarshal` — and `ObjectIdentifier` |
| `…/tls` | `Marshal` and `Unmarshal` over the RFC 6962 structures in the root row below, in both directions — the codec every byte-for-byte comparison in §6 passes through; `DigitallySigned` with `SignatureAndHashAlgorithm` and its `SHA256` and `ECDSA` constants; and `CreateSignature`, which a driver signs a pinned leaf with before asking `ctutil` to verify it |
| `…/ctutil` | `VerifySCT`, `LeafHash`, `LeafHashB64` and `ContainsSCT`. `ctutil/ctutil.go` is byte-identical at v1.0.21 and v1.1.0, so this is inherited in the strictest sense: a do-nothing continuation already satisfies it, and only a regression can fail it |
| `…/logid` | `FromBytes`, `FromB64` and `FromPubKeyB64`; the `LogID` value they return, which is a 32-byte array — `FromBytes` rejects 31 and 33 — and is the SHA-256 of the SubjectPublicKeyInfo; and its `String()` rendering, whose exact text §6.1 pins |
| `…/trillian/ctfe/configpb` | `LogConfig.is_mirror` (field 12), which makes both write paths **404** while every read path keeps working (HTTP-API.md §6.2); and the messages around `LogConfig` that a configuration file is parsed into — `LogConfigSet`, `LogMultiConfig`, `LogBackend` and `LogBackendSet` — which have to stay generated protobuf messages rather than structs of the same shape, because a driver reads them with the text unmarshaller, round-trips them through the binary one, and calls the `Get<Field>()` accessor beside each field (CONFIG.md §1.2) |
| `github.com/google/certificate-transparency-go` (root) | the endpoint path constants — `AddChainPath`, `AddPreChainPath`, `GetSTHPath`, `GetRootsPath`, `GetProofByHashPath`, `GetSTHConsistencyPath`, `GetEntriesPath` — and the response types `GetSTHResponse`, `GetProofByHashResponse`, `GetSTHConsistencyResponse`, `GetEntriesResponse`, `AddChainRequest`, `ASN1Cert`; and beside those the RFC 6962 structures the codec carries: `MerkleTreeLeaf` with `V1`, `TimestampedEntry` and `TimestampedEntryLeafType`, `X509LogEntryType` and `PrecertLogEntryType`, `LogEntry`, `LogID`, `DigitallySigned`, `SignedCertificateTimestamp`, `SignedTreeHead`, `AddChainResponse`, `GetRootsResponse` and `CTExtensions`; the builders and hashes `MerkleTreeLeafFromRawChain(rawChain []ASN1Cert, etype LogEntryType, timestamp uint64) (*MerkleTreeLeaf, error)` — the DER-chain form of `MerkleTreeLeafFromChain`, which CLIENT-API.md §3 gives with its three-entry limit and its parse failure — `CreateX509MerkleTreeLeaf`, `LeafHashForLeaf`, `SerializeSCTSignatureInput` and `SerializeSTHSignatureInput`; and `NewSignatureVerifier` with `PublicKeyFromPEM` |

Three further modules are linked by drivers at this step, and they are named
here for one reason only: they must keep **resolving**. They are
**third-party code, not part of your deliverable**, nothing in them is checked
as work of yours, and you neither own them nor choose their versions —
`go.mod` and `go.sum` are benchmark-owned and are replaced with the pinned
copies before any build (§2), and `google/trillian` in particular is never
checked at all (§3.4). A driver is compiled in a throwaway module beside your
tree, so a dependency your manifest stops providing, or provides at another
version, takes that driver's whole group down with it. Read §3.5 before you
treat this as inert: the trillian pin is exactly where this step's build gate
fails on arrival.

| import path | what a driver at this step names |
| --- | --- |
| `github.com/google/trillian`, with `types`, `monitoring`, `crypto/keyspb`, `crypto/keys/der/proto` and `crypto/keys/pem/proto` beneath it | **not yours to write** — §3.4: the log backend is out of scope. `trillian.TrillianLogServer`, which the **benchmark** implements over `trillian.UnimplementedTrillianLogServer` and registers with `trillian.RegisterTrillianLogServer`, together with `TrillianLogClient` and `NewTrillianLogClient`, `LogLeaf`, `QueuedLogLeaf`, `Proof`, the `ChargeTo` your front end attaches, and the request/response pair of every call in the interface — `QueueLeaves`, `GetLatestSignedLogRoot`, `GetInclusionProofByHash`, `GetConsistencyProof` and `GetLeavesByIndex` among them. The groups implement that interface themselves, in process, and check *which* calls the front end makes and *who* each is charged to — never the backend's own behaviour (SCOPE-BACKEND). Also `SignedLogRoot`, which at the revision v1.1.0 pins carries the tree head only as the opaque `log_root`, and `github.com/google/trillian/types`' `LogRootV1`, which that field marshals from — §3.5 is where that matters, because it is what the tree you were handed does not yet know; `github.com/google/trillian/monitoring`'s `InertMetricFactory`, the value `ctfe.InstanceOptions.MetricFactory` is handed; `github.com/google/trillian/crypto/keyspb`'s `PrivateKey`, `PublicKey` and `PEMKeyFile`; and the two `crypto/keys/…/proto` packages, imported blank exactly as `ct_server`'s own `main` imports them, so that the key-handler registry CONFIG.md §2.3 describes is populated. Mind the collision: `…/trillian/ctfe` in the tables above is the **fork's own** `trillian/` directory, a package you do own, and has nothing to do with this module |
| `github.com/golang/protobuf` | `proto.Marshal`, `proto.Unmarshal` and `proto.UnmarshalText` — the text form is how a driver reads a configuration back off disk before round-tripping it through the binary one — and `ptypes.MarshalAny` with `ptypes.UnmarshalAny`, which wrap and unwrap the `google.protobuf.Any` a log configuration carries its key in (CONFIG.md §2.3). v1.1.0 pins the pre-APIv2 `github.com/golang/protobuf`, not `google.golang.org/protobuf` |
| `google.golang.org/grpc` | `NewServer` and `*Server`: the stub backend of §3.4 is served over a loopback TCP listener the driver opens for itself and your `ct_server` is pointed at it. Also `Dial` with `WithInsecure` and `CallOption`, which the three backend-free `ctfe-` groups use to hand an in-process instance a log client aimed at an address nothing listens on. The gRPC server is the benchmark's, never yours, and no driver speaks gRPC to your front end |

## 6. Formats crossing the boundary

Step 07's `TEST_INTERFACE.md` is replaced by this file, so the list is given
here in full rather than referred to. It is **not** step 07's list copied
forward: every entry below was re-checked against *this* step's group drivers
at v1.1.0, four of them have **inverted or moved** since the step you
came from
and are marked where they sit, and §6.2 names the families earlier steps
carried that nothing here compares — so that you do not reconstruct them from
memory and defend a contract this step does not hold you to.

**Carried forward from the steps behind you, restated.**

* **TLS encodings.** `MerkleTreeLeaf`, `TimestampedEntry`, `DigitallySigned`,
  the SCT signature input and the `TreeHeadSignature` input are compared
  **byte for byte** against vectors the *verifier* holds. Those vectors are
  not in `ct-spec/` and you cannot read them; they were produced from the
  field orders, tag parameters and widths of TLS-CODEC.md §2, §3 and §5,
  which determine the encoding completely. Re-derive an encoding from those
  rules and it will match; guess at one and it will not. The codec does not
  move at v1.1.0 and a gate group leans on it in six places: it pins the
  length and the SHA-256 of a serialised `x509_entry` leaf and of a
  `precert_entry` leaf; it pins that leaf's framing field by field — version,
  leaf type, timestamp, entry type, the uint24 certificate length and the
  uint16 extensions length; it pins the whole `digitally-signed` encoding as
  hex, algorithm bytes included; it pins the whole `TreeHeadSignature` input
  as hex; it requires the original bytes back from a round trip; and it
  re-checks the codec's own rules — a uint24 vector length prefix, a value
  outside its declared bounds refusing to encode, an unknown selector value
  refusing to decode, and a truncated buffer refusing to decode. The same
  encoding crosses the wire: two groups rebuild the `MerkleTreeLeaf` they
  expect the front end to queue and compare it, as bytes, to what the
  in-process backend actually received.
* **JSON bodies.** Field names, types and base64 padding exactly as
  HTTP-API.md §3 and §4 give them; binary fields are standard base64 with
  padding. The responses a group decodes at *this* step are `get-sth`
  (`tree_size`, `timestamp`, `sha256_root_hash`, `tree_head_signature`),
  `get-roots` (`certificates`), `add-chain` (`sct_version`, `id`,
  `timestamp`, `extensions`, `signature`), `get-proof-by-hash` (`leaf_index`,
  `audit_path`), `get-sth-consistency` (`consistency`) and `get-entries`
  (`entries`, each with `leaf_input` and `extra_data`). Requests are read the
  same way: `hash`, `tree_size`, `first`, `second`, `start` and `end` arrive
  as query parameters, and a submission's `chain` is an array of base64 DER.

  **Inverted here.** Steps 02, 04 and 06 all said that a response is read
  field by field and that *nothing compares a whole JSON document as text*.
  That is false at v1.1.0: the frozen-STH group compares the **entire**
  `get-sth` body against one pinned string. On that one endpoint under that
  one configuration the field **names**, their **order** and the exact
  absence of whitespace are contract. It is item 4 of §6.1.
* **Leaf hashes.** `SHA-256(0x00 ‖ TLS(MerkleTreeLeaf))`, compared as bytes
  (MERKLE.md §1), for an `x509_entry` and for a `precert_entry`, against
  pinned digests; a further case recomputes the pre-image by prepending the
  `0x00` byte itself and requires the same answer, so the domain-separation
  byte is checked and not merely assumed. `ctutil.LeafHash` and
  `ctutil.LeafHashB64` are pinned against that answer in hex and in standard
  base64, and cross-checked against `ct.LeafHashForLeaf`. The hash is also
  what `BuildLogLeaf` puts in `LeafValue`, and the same leaf's
  `LeafIdentityHash` is **SHA-256 of the submitted certificate's DER and
  nothing else** (CLIENT-API.md §8.7).
* **Merkle node and root hashes, as bytes.** An audit path and a consistency
  proof are compared as byte slices, never as text, and so is a tree head's
  root hash. Be exact about what that checks here: those hashes are the ones
  the driver's own backend answered with, so what is proved is that the front
  end carried them through `get-proof-by-hash`, `get-sth-consistency` and
  `get-sth` **unaltered**. No group at this step computes a Merkle node hash
  or recomputes a root from a proof — see §6.2.
* **Where a tree head's three fields come from.** HTTP-API.md §4.1 gives only
  their types, so the provenance is fixed here: `TreeSize` → `tree_size` and
  `RootHash` → `sha256_root_hash` come out of the backend's `SignedLogRoot`
  and are compared against the root the driver's backend answered with, and
  the `timestamp` is that same root's **`TimestampNanos` converted to
  milliseconds**, not the personality's own clock. All three enter the
  `TreeHeadSignature` input of TLS-CODEC.md §5.5, and a gate group verifies
  the served `tree_head_signature` as an ECDSA signature over that input
  rebuilt from the three values **as served**, under the public key the
  config named — so a timestamp taken from anywhere else fails the signature
  even though no case reads that field on its own.
* **The STH signature cache.** Signing an unchanged tree head twice must
  yield **byte-identical** signature bytes. Nothing in a response says
  whether a cache was used; only the bytes do.
* **Assembled paths, the handler prefix, and the catch-all.**
  `<handler prefix> + "/" + <log prefix> + <endpoint path>`, exactly as
  HTTP-API.md §1.1 gives it. Checked here as five verdicts: under
  `--handler_prefix=/logs` the endpoint at `/logs/<log prefix>/ct/v1/…`
  answers while the same endpoint at the bare `/<log prefix>/ct/v1/…` is
  **404**; a log prefix no configuration names is **404**; a path no handler
  claims is **404**; the exact path `/` answers **200** with a body of
  **zero bytes**; and `/healthz` is served at the root of the HTTP endpoint
  whatever `--handler_prefix` says.
* **PEM, and how a private key reaches a signer — read this even if you
  remember step 06's version, because the corpus has moved again.** Root
  certificate files are given to `ct_server` by path, as `roots_pem_file`,
  and what a group compares is the DER set `get-roots` serves against the DER
  in the file that log's own config named. The log's **private key** is a
  `google.protobuf.Any` matched against the key-handler registry `ct_server`
  populates at start-up (CONFIG.md §2.3); the loader must never open the path
  itself. At this step **both** key messages are present and they split by
  what the group does: every configuration that starts or builds a live
  personality inlines the key as a **`keyspb.PrivateKey` carrying raw DER**,
  and the two groups that only *parse* a configuration name a
  **`keyspb.PEMKeyFile` with a `path`**. No `password` field appears anywhere
  at this step, and no key file is encrypted. That is a change of corpus, not
  of contract — assume neither key form and neither encryption state, keep
  both handlers registered, and route `private_key` through the registry,
  which is the only part of this that is yours to write. CONFIG.md §2.3's "Do
  not assume either one" is the rule, and it is the reason this bullet has
  now moved three times.
* **The configuration file's encoding, and both message shapes.** The log
  config is a protocol-buffer message and at v1.1.0 it is written and
  parsed as **text proto** (CONFIG.md §4.2). A group writes one to disk,
  reads it back and parses it, then compares field by field: scalars, the
  **order** of a repeated `roots_pem_file`, the escape sequences inside
  `public_key`'s `der`, the `private_key` `Any`'s `type_url` and the path
  inside it, and that a field left unset is its zero value rather than an
  error. Both shapes are exercised — a **`LogConfigSet`** (repeated
  top-level `config`, no `backends`) parsed from text, and a
  **`LogMultiConfig`** (`backends { backend { name, backend_spec } }`
  alongside `log_configs { config { … } }`) round-tripped through marshal and
  unmarshal, with the backend's **name** and its **spec** compared as the
  opaque strings step 02 called them. Every personality started from disk at
  this step is started from a `LogMultiConfig`; the in-process groups build a
  `LogConfig` directly and hand it to `SetUpInstance`.
* **`logid` string forms.** The standard base64 of a 32-byte log id; the
  round trip through `FromB64` and through `FromBytes`; the refusal of a
  31-byte and a 33-byte input; that the id is the SHA-256 of the SPKI,
  recomputed locally; and the `String()` rendering `logid:[<base64>]` —
  prefix and brackets included, compared whole.
* **Precertificates, the poison extension, and the rebuilt TBS.** The poison
  extension is `1.3.6.1.4.1.11129.2.4.3`, **critical**, with an ASN.1 `NULL`
  body — the two bytes `05 00`; it is the **last** extension of a
  precertificate, and `BuildPrecertTBS` removes it and nothing else, so the
  rebuilt TBS is pinned by SHA-256 and its extension list compared as a
  comma-joined list of dotted-decimal OIDs. The `IssuerKeyHash` of a
  `precert_entry` is the SHA-256 of the issuer's **SPKI**, pinned in hex.
  Fields read off a parsed certificate — `KeyUsage`, `ExtKeyUsage`, `IsCA`,
  `DNSNames`, `AuthorityKeyId`, `SubjectKeyId`, `SerialNumber`, `MaxPathLen`
  and `MaxPathLenZero` — are compared as joined renderings.
* **SCT verification through `ctutil`.** `VerifySCT` over a chain and a
  public key, for the non-embedded case and for the embedded one, together
  with the four ways it must fail: a tampered signature, a moved timestamp,
  the wrong certificate and the wrong key. `ContainsSCT` is the non-erroring
  way to ask first, and an **empty chain** is an error rather than a panic.
* **Quota user strings.** The per-request identity the front end derives and
  passes to the backend is a string, and it is compared whole:
  `ctfe.CertificateQuotaUserPrefix` is `@intermediate`, and `QuotaUserForCert`
  renders that prefix, one space, then the certificate subject as
  `pkix.Name.String()` gives it. Which RPCs carry a `ChargeTo`, in what
  order, and which carry none, is checked on the in-process backend's own
  record of the calls it received (CLIENT-API.md §8.6).
* **CORS.** `Access-Control-Allow-Origin: *` on an ordinary GET; an `OPTIONS`
  pre-flight answered by the wrapper with **200**, the same
  `Access-Control-Allow-Origin: *`, `Access-Control-Allow-Methods: POST` and
  a body of **zero bytes**; and a `Vary` header carrying exactly three
  values, in this order — `Origin`, `Access-Control-Request-Method`,
  `Access-Control-Request-Headers`.
* **Flag names, as the exact declared set.** Identifiers, never help prose: a
  binary's own flags are scraped out of its `-h` output, glog's seven
  excluded, sorted, comma-joined and compared whole — for `ct_server` and for
  the two binaries that arrive at v1.1.0. Whether a flag carries a
  `(default …)` suffix is part of the same surface and is checked by substring
  on that flag's help block alone. A flag that no longer exists is checked
  twice: by **name absence** from the help text, and by Go's `flag` package
  exiting **2** when it is passed. The `flag` package's usage banner and
  every message it prints to stderr are SCOPE-DIAGNOSTIC and are never
  compared.
* **Arrival and absence by compilation.** Whether a package is there is
  observed the way the toolchain observes it, never with `ls`: a probe module
  that imports it must build. The negative control is an import path no
  anchor has, and its build must fail **naming that path**. Nothing in either
  gate reads a candidate source file.
* **Mirror configuration, narrowed to what is checked.** A mirror log carries
  `is_mirror: true` and a `public_key` and **no** private key; so configured
  it really starts, both write paths answer **404**, and every read path
  answers exactly what the non-mirror personality answers — the `get-roots`
  and `get-sth-consistency` bodies are compared **byte for byte** between the
  two. Step 07's two configuration *refusals* — a mirror that carries a
  private key, a mirror that omits its public key — are **out of scope here**;
  see §6.2.
* **The `x509` fatality table — INVERTED at v1.1.0, and this is the step
  where it flips.** A group runs a fixed table of fixtures through
  `ParseCertificate` and requires an exact (certificate-returned,
  error-present, `IsFatal`) triple for each, then compares the recovered
  certificate's fields. Step 07 said that a **non-minimally-encoded
  `INTEGER`** and a **negative RSA modulus** are **fatal** at v1.0.21 — nil
  certificate, fatal error — and that was true there. **It is false here.**
  The lax retry of X509-FORK.md §3.2 lands at v1.1.0: when the strict
  parse fails, `ParseCertificate` runs it again with the `lax` tag set, and
  if that succeeds it returns **both** the certificate **and** a non-fatal
  error. Five fixtures are pinned that way — a non-minimal `INTEGER` in the
  certificate, a non-minimal `INTEGER` inside an RSA `SubjectPublicKeyInfo`,
  a zero-length `ExtendedKeyUsage` OID, a negative RSA modulus, and a subject
  `PrintableString` carrying bytes outside the set — and for each of the five
  a certificate **is** returned, an error **is** returned, and `IsFatal` is
  **false**. Step 07's other two rows survive unchanged: an RSA
  `AlgorithmIdentifier` parameter that is anything other than `NULL`, absent
  included, is still recovered, and a zero-length `extnValue` on
  `ExtendedKeyUsage` is still non-fatal.

  What is *also* checked is that the recovery is real rather than a husk: the
  zero-length EKU OID must come back as `[ServerAuth ClientAuth]`, and the
  non-minimal RSA key must come back with `E == 65537` over a 2048-bit
  modulus. And three things stay **fatal** and must not be demoted along with
  the rest — **truncated DER**, **trailing data after a well-formed
  certificate**, and **trailing data after a certificate the lax retry
  recovered**. The plural `ParseCertificates` inherits all of it, including
  that a defect in the outer `SEQUENCE` stays fatal, and `ParseTBSCertificate`
  takes the same retry.
* **The `PrintableString` relaxation — also INVERTED at v1.1.0.** Step
  07 said that a `PrintableString` carrying characters outside the
  PrintableString set is **not an error at all** at v1.0.21, because `asn1`
  recovered it as ISO 8859-1 / T.61 unconditionally and there was nothing to
  opt into. At v1.1.0 that recovery moves **behind the `lax` struct
  tag**: the strict parser now **rejects** those same bytes and leaves the
  destination at its zero value, and only `UnmarshalWithParams(…, "lax")`
  recovers them. The same flip applies at the `asn1` layer to a **zero-length
  `OBJECT IDENTIFIER`** and a **non-minimally-encoded `INTEGER`** — strict
  refuses, `lax` accepts. Both halves are compared: the strict parser's
  refusal, and the **value** the lax parser yields.
* **The `PrintableString` character set itself, which did not move.** The
  accepted set of X509-FORK.md §2.2 is checked as a set, at strict: every
  member parses and renders back identically, and an exhaustive sweep of the
  complement is refused. `&` and `*` are **inside** the accepted set at
  v1.1.0 — they are part of this fork's charset, not a laxness, and a
  string of nothing but those two parses strictly.

And, new at v1.1.0:

* **Admission decisions as errors.** Two *refusal reasons* are matched on
  verbatim, and only those two are contract: `rejecting expired certificate`,
  and `rejecting certificate containing extension %v at index %d` — where
  `%v` is the dotted-decimal OID and `%d` is the index into the certificate's
  own `Extensions` list, counted from zero (HTTP-API.md §5.3). Each is
  compared inside the whole documented body, which at v1.1.0 is
  `Bad Request\nfailed to verify add-chain contents: chain failed to verify:
  <reason>\n` (HTTP-API.md §2.1 for the `<status text>\n<error>\n` frame,
  §5's preamble for the double wrapper).

  Four further bodies are compared, and this list is exhaustive — no group in
  this step reads an error body that is not one of them:

  1. **A masked 500** is exactly `Internal Server Error\n` — an equality,
     because the whole point of the flag is that nothing follows it.
  2. **An unmasked 500** is asserted on three things: it *begins* with the
     §2.1 status line `Internal Server Error\n`, it *ends* with a newline,
     and the detail between the two **contains the backend's own error
     text**. Read that last clause carefully, because step 07's wording said
     the opposite and was wrong: the detail *is* compared, by substring. The
     text looked for is the one the group's own in-process backend was rigged
     to return, not a string you write; what it checks is that masking is a
     two-sided property, so with `--mask_internal_errors` off whatever the
     backend said must still reach the client. A handler that swallows or
     rewrites the backend's error fails here just as surely as one that leaks
     it when the flag is on. Any wrapper of your own around the failed call,
     on either side of that text, is not pinned.

     A **503** is checked one notch weaker: it must begin with
     `Service Unavailable\n`, end with a newline, and be longer than the
     status line alone — and nothing about the detail is compared, because it
     is grpc-go's rendering of an `Unavailable` status and you did not write
     it. Both 503 bodies read at this step are **reads** — `get-sth` against
     an unreachable backend — and a frozen log's failed **write** is checked
     on its status alone, its body never read at all.

     That a backend call was actually made — rather than the handler
     short-circuiting — is checked separately in every one of these cases, by
     counting the calls the in-process backend received, never by reading the
     message.
  3. **A 400 from a malformed `add-chain` envelope** is asserted on three
     things and no more: the status, the `Bad Request\n` line of §2.1, and the
     **per-log diagnostic identity** `<prefix>{<log_id>}: ` that v1.1.0
     puts in front of this one message and no other — rendered `bench{1001}: `
     for the corpus log, and **absent** at v1.0.21, which is why it is
     checked at all (HTTP-API.md §3.1). The sentence that follows the identity
     is not compared; only that something follows it.
  4. **A 405 from the wrong method** is compared in full, because §2's wrapper
     table fixes the whole body as a format rather than as prose:
     `Method Not Allowed\nmethod not allowed: <METHOD>\n`, with `<METHOD>` the
     method actually sent (HTTP-API.md §2). It is here because masking must
     *not* touch it.

  Nothing else in this step is checked on error wording.

  The mirror rule and the both-knobs case are checked on the **decision**, not
  on any text: refusing a currently-valid leaf under `reject_unexpired` is
  checked as a **400**, and setting both knobs is checked as
  `ValidateLogConfig` returning an error at all (HTTP-API.md §5.2). Any
  wording you like for those two.

  **The boundary instant is checked in both directions, so fix it now.** "In
  the past" (HTTP-API.md §5.2) is a *strict* comparison against the current
  time the options were built with: a leaf whose `NotAfter` is exactly that
  instant has **not** expired, so `reject_expired` admits it and
  `reject_unexpired` refuses it; one second later the two answers swap. Both
  groups pin that pair of instants.

  With **both** knobs set at the decision level there is no precedence to
  get right and none is checked: the two rules partition the certificates
  between them — expired ones fall to `reject_expired`, everything else to
  `reject_unexpired` — so whichever order you test them in, every chain is
  refused. That total refusal is what the groups assert, and it is the reason
  `ValidateLogConfig` rejects the combination at start-up.
* **Pinned DER and BER.** Every `asn1` and `x509` case is a fixed byte string
  chosen to land in one named branch — a non-minimal `INTEGER`, a zero-length
  `OBJECT IDENTIFIER`, a `PrintableString` whose bytes are really ISO 8859-1,
  T.61, or neither. What is compared is the error the strict parser gives and
  the value the lax parser yields.

  The `PrintableString` relaxation is checked on the **string that comes out**,
  not only on the absence of an error, so §2.2's "try ISO 8859-1, then T.61"
  is load-bearing as an *order*: bytes that pass the ISO 8859-1 test are
  transcoded as ISO 8859-1 and never reach the T.61 decoder, even where the
  two decodings differ, and bytes that fail both stay an error under `lax`
  with the destination left at its zero value. Only *parsing* is checked — the
  marshalling half of §2.2 is out of scope (§9).
* **Distinguished names as text.** A parsed certificate's `Subject` and
  `Issuer` are compared through `pkix.Name.String()`, whose rendering is
  RFC 2253 order — most specific attribute first, comma-separated, no spaces
  around the separators, so a five-RDN subject comes out
  `CN=…,O=…,L=…,ST=…,C=US`. That is CLIENT-API.md §8.6's rendering, described
  there for `QuotaUserForCert` but produced by the same `pkix.Name.String()`
  everywhere. Not yours to change — but note *which* `pkix` it is: the fork's
  own copy beside the forked `x509` (X509-FORK.md's header), whose `String()`
  is the pre-Go-1.14 one, so attributes it does not map are dropped from the
  rendering where a modern standard-library `pkix.Name` would keep them. The
  pinned expectations are the fork's; a solution that routed these names
  through `crypto/x509/pkix` instead would disagree on exactly those
  certificates. It is checked only because it is the compact way to compare a
  whole parsed name against a pinned fixture.
* **`/healthz`.** Status, body bytes and path, as above.
* **A frozen STH.** A pinned `(tree_size, timestamp, root_hash)` triple with a
  signature computed under the corpus log key, given in the config and served
  verbatim by `get-sth` (CONFIG.md §2.5).
* **The leaf the front end queues.** The end-to-end group's in-process
  backend is a real dedup index and keys it exactly the way a Trillian log
  does: on `trillian.LogLeaf.LeafIdentityHash`, which
  `util.BuildLogLeaf` sets to the **SHA-256 of `cert.Data`** — the submitted
  certificate's DER alone, not the chain, not the index, not the timestamp
  (CLIENT-API.md §8.7). Re-submitting the same certificate must therefore
  arrive with the same identity hash, so that the backend can hand the first
  leaf value back and the second `add-chain` returns the first SCT. A front
  end that leaves the field empty, or derives it from anything else, fails
  that case and nothing else.

### 6.1 The strings this step compares whole

Section 3.3 allows exactly one kind of whole-string comparison: a body, a
header value or a rendering the corpus fixes in full. This is that list, for
this step, and it is complete — every literal below was read off a group
driver, and no driver at this step compares a literal that is not here.

**HTTP response bodies, compared whole, trailing newline included.**

1. The two admission refusals, each inside the whole §2.1 body:

       Bad Request\nfailed to verify add-chain contents: chain failed to verify: rejecting expired certificate\n
       Bad Request\nfailed to verify add-chain contents: chain failed to verify: rejecting certificate containing extension <oid> at index <n>\n

   The second appears four times, with `2.5.29.17` at index 4, `2.5.29.19` at
   index 2 (twice, one of them the lowest-index rule), and
   `1.3.6.1.4.1.11129.2.4.3` at index 5.
2. `Internal Server Error\n` — the masked 500, twice: on a read and on a
   submission. An equality, because the whole point of the flag is that
   nothing follows it.
3. `Method Not Allowed\nmethod not allowed: POST\n` — compared in full,
   because §2's wrapper table fixes the whole body as a format.
4. The frozen `get-sth` document, whole, as one line of JSON with no
   whitespace and its four members in this order: `tree_size`, `timestamp`,
   `sha256_root_hash`, `tree_head_signature`. The values are the ones
   `frozen_sth` carried in the configuration, and the signature is compared
   as text *and* verified as a signature.
5. `ok` — `/healthz`'s body. Two bytes, no newline. Compared whole three
   times: on the plain personality, on the `--handler_prefix` personality,
   and again as the body of the CORS GET.
6. **Zero bytes.** The catch-all `/`'s 200 and the CORS pre-flight's 200 are
   each required to carry a body of length 0.

**HTTP response header values, compared whole.**

7. `text/plain; charset=utf-8` — the `Content-Type` of the masked 500.
8. `*` — `Access-Control-Allow-Origin`, twice.
9. `POST` — `Access-Control-Allow-Methods` on the pre-flight.
10. `Origin|Access-Control-Request-Method|Access-Control-Request-Headers` —
    the `Vary` values joined with `|`, so their **number and order** are
    checked, not merely their presence.

**Bodies compared by prefix, suffix or substring — never whole.** Listed
here because they look like the list above and are not, and because the
boundary between them is exactly where your freedom of wording begins.

11. The unmasked 500: prefix `Internal Server Error\n`, suffix `\n`, and the
    backend's own error text somewhere between them. See the second numbered
    item under **Admission decisions as errors** above; the substring is the
    harness's own planted text, not a contract literal.
12. The 503s: prefix `Service Unavailable\n`, suffix `\n`, and longer than
    the prefix alone. Nothing else.
13. The masked 400: prefix `Bad Request\nbench{1001}: `, suffix `\n`, and at
    least two bytes between them. The `bench{1001}: ` half is contract; the
    sentence after it is not.
14. `empty integer` — a substring of the `asn1` error for an `INTEGER` with a
    zero-length body, required at strict **and** under `lax`, because that
    one is not among the three the tag relaxes.
15. Three substrings of a binary's `-h` output: `(default ` must be **absent**
    from the help blocks of `--metrics_endpoint` and `--log_rpc_server`,
    `(default "localhost:6962")` must be **present** in the block of
    `--http_endpoint`, and `allow_verification_with_non_compliant_keys` must
    not appear anywhere in the text.
16. The negative-control build failure must name the import path the probe
    asked for. The path is the one the group supplied.

**Flag sets, sorted, comma-joined, compared whole.**

17. `ct_server`'s own eighteen, glog's excluded:
    `by_range,etcd_http_service,etcd_metrics_service,etcd_servers,get_sth_interval,handler_prefix,http_endpoint,log_config,log_rpc_server,mask_internal_errors,max_get_entries,metrics_endpoint,quota_intermediate,quota_remote,rpc_deadline,tracing,tracing_percent,tracing_project_id`.
18. The submission server's:
    `add_prechain_timeout,dry_run,http_endpoint,load_pending_qualified_logs,loglist_path,loglist_refresh_interval,policy_type,roots_refresh_interval`.
19. The submission hammer's: `count,duration,proxy_endpoint,qps`.

**Renderings off a parsed certificate or a parsed config.**

20. Three chains rendered as their subjects' common names joined with `|`,
    root last — for the currently-valid chain, the expired chain, and the
    short-life chain whose intermediate has outlived its leaf. These check
    *which path was built*, not any message.
21. Five whole-certificate summaries from the lax-recovery table, each
    `serial|subject|issuer|comma-joined extension OIDs|notBefore|notAfter|
    public-key algorithm`, with the two names rendered by `pkix.Name.String()`
    and the two times as `2006-01-02T15:04:05Z`.
22. `5/1+2/false` — a leaf's `KeyUsage`, its EKUs joined with `+`, and
    `IsCA`.
23. `valid.bench.invalid/<AKI hex>/4097` — DNS names, authority key id,
    serial.
24. `true/0/true/<SKI hex>` — an intermediate's `IsCA`, `MaxPathLen`,
    `MaxPathLenZero` and subject key id.
25. `1.3.6.1.4.1.11129.2.4.3/critical=true/0500` — the poison extension's
    OID, criticality and body.
26. `2.5.29.15,2.5.29.37,2.5.29.19,2.5.29.35,2.5.29.17` — a clean leaf's
    extension OIDs in order, and the same list with the poison OID appended
    for the precertificate.
27. `logid:[<base64>]` — `logid.LogID.String()`.
28. `type.googleapis.com/keyspb.PEMKeyFile` and the `log-key.pem` inside it;
    the log `prefix` `bench`; the two `roots_pem_file` names in the order
    written; the backend `name` `default` and `backend_spec`
    `localhost:8090`. These are values the group put into a configuration and
    expects to find again.
29. `@intermediate` and
    `@intermediate CN=Harbor CT Bench Intermediate,O=Harbor CT Bench 2f5b59344c`.
30. `127.0.0.1` and `203.0.113.9` as `ChargeTo` users, and
    `pinned-extra-data` as a `get-entries` `extra_data`.
31. `github.com/google/certificate-transparency-go/jsonclient` — the package
    path `reflect` reports for `RspError`, which is how the alias is proved
    to be one type and not two. Beside it, `RspError.Error()` must return the
    **wrapped error's text and nothing else** — no status code, no URL, no
    decoration — and that is compared whole against the text the group itself
    wrapped.

**ASN.1 string values, compared whole after parsing.**

32. Strict, unchanged on the way out: `*.bench.invalid`, `Harbor & Co`,
    `*&*&`, `'()`, `+,-./`, `:=?`, `*&`, `Harbor CT Bench`, and the whole
    76-byte accepted set as one string.
33. Recovered under `lax`, pinned **by value**: `100%`, `*é` (an ISO 8859-1
    byte transcoded to UTF-8), `&` followed by a literal newline (a control
    byte kept verbatim), the byte sequences `41 C3 A9 42`, `41 C2 A0 42`,
    `41 0A 42` and `41 7F 42`, and the same ISO 8859-1 recovery one
    `SEQUENCE` deep. A `NUL` byte stays rejected under `lax`, with the
    destination left empty.
34. `|2.5.4.3` — two `OBJECT IDENTIFIER`s from one `SEQUENCE OF` joined with
    `|`, the first zero-length and therefore rendering as the empty string.

**Pinned digests, framings and encodings, as hex or base64.**

35. `<leaf SHA-256>/513` and `<precert leaf SHA-256>/<issuer key hash>` for
    the two serialised `MerkleTreeLeaf`s; the leaf hash in hex and in
    standard base64; the clean rebuilt TBS's SHA-256; the issuer key hash on
    its own.
36. `0/0/1500000000000/0/<len>/0` — the `MerkleTreeLeaf` framing read field
    by field.
37. `04030004deadbeef` and `deadbeef` — a `DigitallySigned` marshalled and
    unmarshalled. `000003010203` — a uint24-prefixed vector.
38. The whole `TreeHeadSignature` input as hex:
    `0001` ‖ timestamp ‖ tree size ‖ root hash.
39. `3059301306` — the first five bytes of the configured SPKI DER, which
    proves the text proto's escape sequences survived the round trip.
40. The base64 log id, on its own and inside the `logid:[…]` rendering.

**That list is exhaustive: no group at this step compares any other message,
rendering, body or program output as a whole string.** A Go `error`'s
`Error()` text is read in exactly two places — the `empty integer` substring
of item 14, and the `RspError` wrapping rule of item 31, where the text
compared is one the group itself planted. Everywhere else —
`ValidateLogConfig`'s refusals, `ValidateChain`'s refusals, every other
`asn1` and `x509` refusal, `ParseBodyAsJSONChain`'s five refusals, every
`ctutil` refusal and every `logid` refusal — the checking is on *whether* the
call failed and on what it refused to produce, never on how it is worded.

Five kinds of comparison look like the list above and are not:

* **Bytes the harness itself supplied, handed back.** The stub backend's
  pinned root hash and audit path, its `pinned-extra-data`, the
  `backend exploded` text it was rigged to return, the JSON a recorder
  serves, the `harbor-ct-bench/1 (step-08)` user agent a group passes in and
  reads off its own recorder. Those are the group's own values on a round
  trip, not your output.
* **Two of your own answers compared with each other.** The mirror's
  `get-roots` and `get-sth-consistency` bodies against the log's; the second
  `get-sth` against the first; the second STH signature against the first;
  the duplicate submission's SCT against the original's. Nothing about the
  spelling is fixed — only that the two agree.
* **Byte slices compared as bytes.** TLS encodings, DER, leaf values,
  identity hashes, root hashes and audit paths are compared with byte
  equality, not as text.
* **Program output that is never read.** Every command-line binary at this
  step is checked on its **exit status**, and on flag *names* scraped from
  `-h`. `certcheck`'s stdout is redirected to a file the script never opens;
  `ct_server`'s output is likewise redirected and never examined.
* **Whole-message reads on the failure path, which mostly do not exist
  here.** Where a group expects a refusal it checks that it was refused — a
  non-nil error, a status code, a nil result, an exit status — and stops.

### 6.2 What earlier steps listed and this one does not

These families are absent from the list above on purpose. Restating them
would put a false or out-of-scope sentence in front of you.

* **Prometheus exposition on `/metrics`.** Step 07 checked the
  `logid`-labelled `max_merge_delay`, `expected_merge_delay` and `known_logs`
  gauges, read back from a running front end. No group at this step reads a
  gauge. `/metrics` is fetched once and checked on **status 200 alone**;
  §9 puts the content of that body out of scope. The merge-delay numbers you
  will see in every configuration here are *inputs*, not expectations.
* **SIA and RFC 3779.** Step 07's largest new surface — `1.3.6.1.5.5.7.1.11`,
  `sbgp-ipAddrBlock`, `sbgp-autonomousSysNum`, the strict/lenient asymmetry
  between them, and the byte-for-byte SIA emission round trip. Nothing at
  this step parses or emits either extension. The x509 groups here cover
  poison, key usage, extended key usage, basic constraints, SAN, AKI and SKI
  and nothing else.
* **`LogSTHInfo` in an X.509 extension.** `1.3.6.1.4.1.11129.2.4.5`,
  `LogSTHInfoFromCert`, `STHFromCert` and `HasSTHInfo`, and the rule that a
  remainder after the encoded structure is an error. No group at this step
  touches any of it.
* **The two mirror refusals.** Step 07 named `unnecessary private key for
  mirror` and the missing-`public_key` refusal. This step starts a mirror and
  checks what it serves (above), but feeds no invalid mirror configuration to
  the validator. The only missing-`public_key` refusal checked here belongs to
  `frozen_sth`, not to mirroring.
* **Prefix normalisation.** Step 07 said that `bench`, `/bench` and `/bench/`
  are the same log. Only **one** spelling of the log prefix is ever used at
  this step and no two spellings are compared. The normalisation rule is still
  in HTTP-API.md §1.1 and your code still has to honour it; it simply is not
  what any case here decides on. Which paths *escape* the prefix is **not**
  withdrawn: all three of `/`, `/healthz` and `/metrics` are shown to be
  prefix-immune under `--handler_prefix` by `ctserver-flags-and-healthz`.
* **Every configuration *rejection* earlier steps listed.** The JSON-config
  refusal, the unknown-field-name refusal, `empty backend name` and
  `duplicate backend name`, and the `--log_rpc_server` choice between
  `LogConfigSet` and `LogMultiConfig`. The only `ValidateLogConfig`
  rejections checked at this step are the two v1.1.0 adds — both
  admission knobs set at once, and a malformed `frozen_sth` — and each is
  checked as "returned an error at all", with no wording attached.
* **Per-log separation and two roots pools.** Step 04 gave two logs one
  server and required each `get-roots` to serve only its own pool. Every
  configuration that *serves* at this step names exactly one log with exactly
  one `roots_pem_file`; the two-file and two-log cases here are parsed in
  memory and never started. The only multi-log fact checked is that an
  unconfigured prefix is a 404.
* **Signature distinctness.** Steps 03 to 06 required that no two distinct
  signing inputs share a signature. Nothing at this step compares two
  signatures for **in**equality; the SCT signature bytes are deliberately not
  compared at all, only verified. The *cache* half — an unchanged tree head
  signing to identical bytes — survives and is restated above.
* **Merkle proof verification and the rest of `ctutil`.** Inclusion through
  `ctutil.LogInfo`, consistency through `ctclient`, `VerifyInclusion*`
  returning `-1` on failure, the cached-STH semantics, `LogInfo.MMD` as a
  `time.Duration`, `MerkleTreeLeafForEmbeddedSCT` and
  `ctutil.MerkleTreeLeafFromChain`. None of that is exercised here;
  `ctutil` at this step is `LeafHash`, `LeafHashB64`, `ContainsSCT` and
  `VerifySCT`.
* **`get-entry-and-proof`.** Step 06's own new material. Every stub backend
  at this step deliberately refuses the underlying RPC and no driver ever
  requests the endpoint.
* **Log lists, `findlog`, and rendered tool output.** Schema v1's field names
  including `final_sth` and `disqualified_at`; `findlog`'s one-line-per-log
  stdout; `ctclient`, `sctcheck`, `sctscan` and `certcheck` output; the
  `Usage of <path>:` banner step 04 compared whole; and `x509util`'s
  `CertificateToString`, `GeneralNamesToString` and `OtherNameToString`.
  `loglist2` exists at v1.1.0 and is checked **only** by building. Every
  binary here is checked on exit status and on flag names.
* **`Content-Type: application/json` as a header that is read.** The only
  response header values read at this step are the `Content-Type` of the
  masked 500 and the three CORS headers. Your JSON endpoints must still send
  the right content type; nothing here checks that they do.
* **`[]` versus `null` for an empty list.** Steps 01, 02 and 06 fixed it.
  Every JSON read at this step goes through typed decoding, which erases the
  distinction, so no case at this step can tell them apart. HTTP-API.md §3
  still says `[]`.

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
| `inherited-config-and-logid` | gate |
| `inherited-ctutil-verification` | gate |
| `inherited-precert-and-x509` | gate |
| `inherited-reads-and-quota` | gate |
| `inherited-tls-serialization` | gate |
| `admission-inversion-sct` | requirement |
| `admission-reject-expired` | requirement |
| `admission-reject-unexpired` | requirement |
| `admission-window-ca-and-eku` | requirement |
| `asn1-lax-oid-and-integer` | requirement |
| `asn1-lax-printable-string` | requirement |
| `asn1-printable-string-charset` | requirement |
| `binaries-eighteen-mains` | requirement |
| `certcheck-flag-rename` | requirement |
| `config-validation-new-fields` | requirement |
| `ctfe-frozen-sth` | requirement |
| `ctfe-mask-internal-errors` | requirement |
| `ctfe-reject-extensions` | requirement |
| `ctserver-flags-and-healthz` | requirement |
| `ctserver-mirror-serving` | requirement |
| `jsonclient-rsperror-useragent-and-chain-body` | requirement |
| `new-packages-compile` | requirement |
| `x509-nonfatal-parse-certificate` | requirement |

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

* **`goshawk`'s MySQL-backed state.** v1.1.0 gives it
  `incident_mysql_uri`, `state_mysql_uri`, `state` and `flush_state`. No
  database runs, so the flags and start-up behaviour are observable and the
  storage is not.
* **The submission proxy doing any submitting.** It needs live logs and a
  fetched log list. Its presence, its flag set and its defaults are checked;
  its traffic is not.
* **Fetching a log list over the network.** Nothing retrieves a log list,
  and nothing parses one either: `loglist2` is checked only on existing and
  compiling, through a probe that names `LogList` and `LogRoots`. Its
  parsing behaviour is out of scope at this step.
* **Metrics content.** That `/metrics` is served where the flags say is
  observable; what is counted there is not. No group reads a metric name, a
  label or a value.
* **`InstanceOptions.STHStorage`.** The third field v1.1.0 adds to
  `InstanceOptions` (CLIENT-API.md §8.2) must exist for the field list to be
  right, but nothing here supplies one or observes a mirror serving an STH
  from it, so `get-sth` against a mirror is out of scope here. The
  `MirrorSTHStorage` interface, `MirrorSTHGetter` and `DefaultMirrorSTHStorage`
  were checked at step 07 and are not re-checked here. What **is** checked of
  `is_mirror` here is the part the previous release's *binary* could not do:
  CLIENT-API.md §8.1 — the library has served mirrors since the previous
  release, but that release's `ct_server` opens per-log registration with a
  refusal and never listens, so a running mirror is new at v1.1.0. A group
  starts one and checks both write paths 404ing and the read paths answering
  exactly as the non-mirror log beside it does.
* **Marshalling a `PrintableString`.** X509-FORK.md §2.2's closing sentence —
  that marshalling applies the same accepted set *without* the `&` allowance
  — is documented and is out of scope at this step. Every `asn1` and `x509`
  case here is a parse.
* **The `Field` member on `asn1` errors.** X509-FORK.md §2.3 gives
  `StructuralError` and `SyntaxError` a `Field` and says the rendered text
  names it. No group here reads `Field` or matches a field path; the `asn1`
  cases compare the documented message substrings only.
* **`get-entry-and-proof`, `Cache-Control` and `AddJSONPath`.** All three are
  in the corpus and all three are still part of the product, but no group at
  this step exercises them: they were checked where they were introduced and
  this milestone does not change them.
* **How dependencies are vendored.** v1.1.0 is the release that drops
  the checked-in `vendor/` directory in favour of plain module resolution
  (RELEASE-MAP.md §8). Nothing checks it: no group looks for a `vendor/`
  directory, looks for its absence, or inspects `go.mod`'s `require` list.
  What *is* checked is that the tree builds and that every package resolves
  through the module graph, which is true either way.
* **Anything dated after v1.1.0.** `by_range` still exists here and is
  still checked; `align_getentries`, `pkcs11_module_path` and EKU
  short-circuiting belong to the next step.

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

One config file, two releases, opposite answers. That is the whole of this
milestone's difficulty, and nothing in the file signals the change.

Up to v1.0.21, `reject_expired` was never really an admission rule:
it was fed into path building as "do not disable time checks", so it rejected
an expired *intermediate* exactly as readily as an expired leaf, and there was
no way to ask for the opposite. At v1.1.0 there are two knobs and an
explicit **leaf-only** test that runs *before* path building, after which path
building always runs with time checks disabled and an explicit current time.
Same field name, same file, different decision. `reject_unexpired` is its
mirror and did not exist before in any form; setting both is a start-up
refusal, because such a log could admit nothing.

Around that inversion sit the rest of the largest release in the set:
`reject_extensions`, a frozen STH that `get-sth` serves forever while writes
keep working, `/healthz`, `--mask_internal_errors`, two `ct_server` defaults
withdrawn, five new packages, the three named `asn1` laxnesses behind a `lax`
tag with the `ParseCertificate` family retrying through it, and a `certcheck`
flag rename in which one flag's polarity **and** default both flip.
