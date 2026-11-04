# CLI — the command-line programs and their flag grammar

Every `package main` in the product is a small program: flag parsing, a call
into the library, and an exit status. This page is the inventory. It is
prescriptive: where it gives a flag name, a type or a default, that is the
value the verifier may look for.

Two conventions hold everywhere and are worth stating once.

**Flag grammar.** All programs use Go's standard `flag` package (only
`ctclient` from v1.1.3 differs — §3). Therefore `-name value`, `--name value`,
`-name=value` and `--name=value` are all accepted and equivalent; a boolean
flag is set by bare `-name` or `-name=true`, and **`-name true` does not work**
for a boolean — the `true` is taken as a positional argument. An unrecognised
flag makes the `flag` package print `Usage of <prog>:` and the flag list to
**stderr** and exit **2**. None of that is your code; it is what using the
standard package gives you, and it is what the verifier expects to see.

**How a flag appears in that list.** The same standard package decides the
shape, and the verifier reads flag types and defaults back out of it, so the
two conventions that surprise people are worth stating. Each flag is printed
as two lines — `  -name type` and then the usage text indented on the next —
where *type* is the **kind, not the width**: an `Int64` flag and an `Int` flag
both print `int`, a `Duration` prints `duration`, a `String` prints `string`,
and a **`Bool` prints no type word at all**, so a boolean's first line is just
`  -name`. The default is appended to the usage line as `(default <value>)`,
quoted for a string — but **only when it is not the type's zero value**. A
`String` defaulting to `""`, an `Int` defaulting to `0` and a `Bool`
defaulting to `false` therefore show no `(default …)` clause anywhere. Where
a table below gives a flag's type as `Int64` or its default as `""`, that is
the declaration, not the rendering. One thing can still change that token,
and it is your own usage text: Go's `flag` package treats a
**backquoted word** in a flag's usage string as the name of that flag's
argument and prints it *in place of* the type, so a `String` flag whose
usage reads ``the `project` to trace to`` renders
`  -tracing_project_id project`, and a `Bool` whose usage contains a
backquoted word stops printing bare. The verifier reads these tokens, so
**do not backquote a word** in the usage text of any flag whose type a
table below gives.

**Exit status.** The product uses logging libraries whose exit behaviour is
fixed:

| call | exit status |
| --- | --- |
| `glog.Exit` / `glog.Exitf` / `klog.Exit*` / `log.Fatal*` | **1** |
| `glog.Fatal` / `glog.Fatalf` / `klog.Fatal*` | **255** |
| `flag` package rejecting an unknown flag | **2** |
| `flag` package handling `-h` or `--help` | **0** |
| normal return from `main` | **0** |

`-h` and `--help` are not unrecognised flags. The standard package handles
them itself, prints exactly the same `Usage of <prog>:` block to stderr, and
returns `flag.ErrHelp`, which `flag.CommandLine` in its default `ExitOnError`
mode turns into exit **0**. The same output therefore comes with either status,
and which one it is says whether the flag was understood. A program that reads
its positional arguments before calling `flag.Parse` reaches neither path — see
`chainfix` at §4.6.

`glog` is replaced by `klog` at **v1.1.4** throughout the product. The exit
statuses are unchanged by that swap. What does change is the set of flags the
logger puts on a binary's command line, and which binaries get it at all.

**The two logging flag sets.** Every flag below is registered by the logging
library on Go's own `flag.CommandLine`, so it appears in a binary's `--help`
alongside that binary's own flags and is not declared by the product anywhere.

| | flag names |
| --- | --- |
| `github.com/golang/glog`, up to v1.1.3 — **seven** | `alsologtostderr`, `log_backtrace_at`, `log_dir`, `logtostderr`, `stderrthreshold`, `v`, `vmodule` |
| `k8s.io/klog/v2`, from v1.1.4 — **thirteen** | the same seven, plus `add_dir_header`, `log_file`, `log_file_max_size`, `one_output`, `skip_headers`, `skip_log_headers` |

klog's set is a strict superset, so the six on the second line are the
ones whose presence distinguishes a v1.1.4 binary from a v1.1.3 one. Their
usage text is klog's own and this document does not reproduce it.

**Which binaries carry it.** The difference between the two libraries is *when*
they register. glog registers in its own package `init()`, so at v1.1.3 any
binary that imports glog — directly or anywhere in its import graph — ends up
with glog's seven flags whether it asked for them or not. klog registers
nothing until `klog.InitFlags(nil)` is called, so from v1.1.4 a binary carries
the logging flags **only if its own `main()` calls that function**, and the
upstream migration added the call to some `main()`s and not others:

| at v1.1.4 | programs |
| --- | --- |
| calls `klog.InitFlags(nil)`, so carries all thirteen | `ctclient`\*, `sctcheck`, `sctscan`, `ct_hammer`, `migrillian`, `ct_server`, the submission `server`, `certcheck`, `crlcheck`, and the witness `witness`, `feeder` and `client` |
| does **not** call it, so carries **no** logging flag at all | `dumpscts`, `preloader`, `scanlog`, the submission `hammer`, `chainfix`, and the witness `config` |

\* `ctclient` is the exception that proves the rule: it calls
`klog.InitFlags(nil)` in `main()`, but its command framework has already copied
`flag.CommandLine` during package `init()`, so the thirteen never reach its
help — see §3.2. Every other program on the first row parses with the `flag`
package directly and shows all thirteen.

The table is the **v1.1.4** state, and one program moves after it: at
**v1.3.3** `preloader`'s `main()` gains the `klog.InitFlags(nil)` call, so from
that release it carries all thirteen and leaves the second row. (Nothing grades
preloader's logging flag set — §1's "present at" column is what is compared for
that program.)

This partition is a v1.1.4 fact and has no counterpart before it: up to v1.1.3
a program shows glog's seven if glog is anywhere in its import graph, which it
is for every program that logs at all, so there is nothing to opt into and
nothing to omit.

A "required flag is missing" diagnostic is **never** accompanied by a usage
block. It is a bare message on stderr and exit 1. Do not add a usage dump.

---

## 1. Program inventory

`package main` directories at each anchor. This table is a structural
requirement: the verifier can enumerate which binaries a build produces.

| program | directory | present at |
| --- | --- | --- |
| `ct_server` | `trillian/ctfe/ct_server/` | all 13 |
| `ctclient` | `client/ctclient/` | all 13 |
| `preloader` | `preload/preloader/` | all 13 |
| `dumpscts` | `preload/dumpscts/` | all 13 |
| `scanlog` | `scanner/scanlog/` | all 13 |
| `chainfix` | `fixchain/chainfix/` | all 13 |
| `ct_hammer` | `trillian/integration/ct_hammer/` | all 13 |
| `certcheck` | `x509util/certcheck/` | v1.0.5 → v1.3.3 (**added at step 2**) |
| `crlcheck` | `x509util/crlcheck/` | v1.0.5 → v1.3.3 (**added at step 2**) |
| `sctcheck` | `ctutil/sctcheck/` | v1.0.13 → v1.3.3 (**added at step 4**) |
| `sctscan` | `ctutil/sctscan/` | v1.0.13 → v1.3.3 (**added at step 4**) |
| `findlog` | `loglist/findlog/` | v1.0.13 → v1.1.3 (**removed at step 11**) |
| `gossip_server` | `gossip/gossip_server/` | v1.0 → v1.1.0 (**removed at step 9**) |
| `goshawk` | `gossip/minimal/goshawk/` | v1.0.21 and v1.1.0 **only** |
| `gosmin` | `gossip/minimal/gosmin/` | v1.0.21 and v1.1.0 **only** |
| `migrillian` | `trillian/migrillian/` | v1.0.19 → v1.3.3 (**added at step 6**) |
| submission `server` | `submission/server/` | v1.1.0 → v1.3.3 (**added at step 8**) |
| submission `hammer` | `submission/hammer/` | v1.1.0 → v1.3.3 (**added at step 8**) |
| `witness` | `internal/witness/cmd/witness/` | v1.1.3, v1.1.4, v1.2.0 **only** |
| `feeder` | `internal/witness/cmd/feeder/` | v1.1.3, v1.1.4, v1.2.0 **only** |
| `client` (witness) | `internal/witness/cmd/client/` | v1.1.3, v1.1.4, v1.2.0 **only** |
| `config` (witness) | `internal/witness/cmd/witness/config/` | v1.1.3, v1.1.4, v1.2.0 **only** |

Counts, at every one of the thirteen step anchors: **8** at v1.0, 10 at v1.0.5
and v1.0.10, **13** at v1.0.13 and v1.0.17, 14 at v1.0.19, 16 at v1.0.21, 18 at
v1.1.0, 15 at v1.1.2, **19** at v1.1.3, **18** at v1.1.4 (v1.1.3's nineteen less
`findlog`) and at v1.2.0, 14 at v1.3.3.
(Two files under `x509/` are excluded throughout: they carry a build-ignore
constraint — spelled `// +build ignore` alone at v1.0 through v1.1.3, with
`//go:build ignore` added alongside it from v1.1.4 — so they are never linked
into a binary and are not programs.)

---

## 2. `ct_server` — the one the verifier runs most

Import path `github.com/google/certificate-transparency-go/trillian/ctfe/ct_server`.
No positional arguments are accepted or examined.

### 2.1 The full flag set by release

An `x` means the flag exists at that release with the name, type and default
given. Nothing in this table is ever renamed, and no type ever changes.

| flag | type | default | 1.0 | .5 | .10 | .13 | .17 | .19 | .21 | 1.1.0 | 1.1.2 | 1.1.3 | 1.1.4 | 1.2.0 | 1.3.3 |
| --- | --- | --- | -- | -- | -- | -- | -- | -- | -- | -- | -- | -- | -- | -- | -- |
| `http_endpoint` | String | `localhost:6962` | x | x | x | x | x | x | x | x | x | x | x | x | x |
| `metrics_endpoint` | String | `localhost:6963` → **`""` from v1.1.0** | x | x | x | x | x | x | x | x | x | x | x | x | x |
| `log_rpc_server` | String | `localhost:8090` → **`""` from v1.1.0** | x | x | x | x | x | x | x | x | x | x | x | x | x |
| `rpc_deadline` | Duration | `10s` | x | x | x | x | x | x | x | x | x | x | x | x | x |
| `get_sth_interval` | Duration | `180s` | x | x | x | x | x | x | x | x | x | x | x | x | x |
| `log_config` | String | `""` | x | x | x | x | x | x | x | x | x | x | x | x | x |
| `max_get_entries` | Int64 | `0` | x | x | x | x | x | x | x | x | x | x | x | x | x |
| `etcd_servers` | String | `""` | x | x | x | x | x | x | x | x | x | x | x | x | x |
| `etcd_http_service` | String | `trillian-ctfe-http` | x | x | x | x | x | x | x | x | x | x | x | x | x |
| `etcd_metrics_service` | String | `trillian-ctfe-metrics-http` | x | x | x | x | x | x | x | x | x | x | x | x | x |
| `tracing` | Bool | `false` | | | | | x | x | x | x | x | x | x | x | x |
| `tracing_project_id` | String | `""` | | | | | x | x | x | x | x | x | x | x | x |
| `tracing_percent` | Int | `0` | | | | | x | x | x | x | x | x | x | x | x |
| `quota_remote` | Bool | **`true`** | | | | | | x | x | x | x | x | x | x | x |
| `quota_intermediate` | Bool | **`true`** | | | | | | x | x | x | x | x | x | x | x |
| `handler_prefix` | String | `""` | | | | | | | x | x | x | x | x | x | x |
| `mask_internal_errors` | Bool | `false` | | | | | | | | x | x | x | x | x | x |
| `pkcs11_module_path` | String | `""` | | | | | | | | | x | x | x | x | x |
| `cache_type` | String | `noop` | | | | | | | | | | | | x | x |
| `cache_size` | Int | `-1` | | | | | | | | | | | | x | x |
| `cache_ttl` | Duration | `-1s` | | | | | | | | | | | | x | x |
| `http_idle_timeout` | Duration | `-1s` | | | | | | | | | | | | | x |
| `tls_certificate` | String | `""` | | | | | | | | | | | | | x |
| `tls_key` | String | `""` | | | | | | | | | | | | | x |
| `trillian_tls_ca_cert_file` | String | `""` | | | | | | | | | | | | | x |
| `non_fresh_submission_age` | Duration | `24h` | | | | | | | | | | | | | x |
| `non_fresh_submission_burst` | Int | `1` | | | | | | | | | | | | | x |
| `non_fresh_submission_limit` | String | `""` | | | | | | | | | | | | | x |
| `max_cert_chain_size` | Int64 | `512000` | | | | | | | | | | | | | x |
| `by_range` | Bool | `false` | | | x | x | x | x | x | x | | | | | |
| `align_getentries` | Bool | **`true`** | | | | | | | | | x | x | x | x | x |
| `getentries_metrics` | Bool | `false` | | | | | | | | | | | | x | x |
| `allow_verification_with_non_compliant_keys` | Bool | `false` | x | x | x | x | x | x | | | | | | | |

The last four are not declared in `ct_server` at all: `by_range`,
`align_getentries` and `getentries_metrics` belong to the `ctfe` package and
`allow_verification_with_non_compliant_keys` to the **root** package. They are
`flag` package globals, so they appear on the command line and in the `--help`
output of every program that links those packages — which for the root-package
one means *every* program in the tree — exactly like the rest.

Totals: **11** (v1.0), 11, **12** (v1.0.10), 12, **15** (v1.0.17),
**17** (v1.0.19), 17 (v1.0.21 — one flag arrives and another is removed),
**18** (v1.1.0), **19** (v1.1.2), 19, 19, **23** (v1.2.0), **31** (v1.3.3).

These totals count every flag marked for that release in the table above —
the ones `ct_server` declares in its own `main.go` *and* the `flag`-package
globals from `ctfe` and the root package listed just above, because all of them
appear on `ct_server`'s command line and in its `--help` output. At **v1.1.0**
the 18 therefore include `by_range`. What the totals exclude is the logging
library's flags, which every binary carries automatically. Every binary
also carries the logging library’s flags automatically: up to v1.1.3 that
means glog’s seven (`alsologtostderr`, `log_backtrace_at`, `log_dir`,
`logtostderr`, `stderrthreshold`, `v`, `vmodule`), registered by glog’s
package `init()` in any binary that imports it. Those flags appear in each
binary’s `--help` output but are not `ct_server` flags and are not counted
here. See the preamble above for the full accounting.

Notes on individual flags, in the order they arrive:

* `max_get_entries` — a value of 0 means "use the default of 1000". It caps
  the number of entries a `get-entries` request may span (HTTP-API.md §4.3).
* `get_sth_interval` — the period of the **periodic internal `get-sth`** of
  §2.2 step 9 and ARCHITECTURE.md §6: a per-log background task that
  re-obtains the log's own tree head so the process's metrics follow a tree
  that changes without it. **A value of `0` disables that task entirely**, at
  every release from v1.0 to v1.3.3, and is a supported setting and not an
  error: the process starts normally, binds its listener and serves, simply
  without the periodic refresh. The decision is made on the duration itself,
  before any timer exists — a non-positive interval starts no task, and no
  ticker is constructed from it. This ordering is part of the contract and not
  an implementation detail: Go's `time.NewTicker` panics on a non-positive
  interval, so a process that builds the ticker first and tests the value
  afterwards dies at start-up on a setting the flag itself advertises as
  legal. Upstream's own one-line description says as much — *"Interval between
  internal get-sth operations (0 to disable)"* — though, like every
  description, its wording is yours. A verifier that runs
  `ct_server` may pass `--get_sth_interval=0` for exactly this reason, and
  does.
* `log_config` — the help text at **v1.0 only** says *"File holding log config
  in JSON"*. It is wrong even at v1.0: the file is parsed as text proto. From
  v1.0.5 the help text reads *"File holding log config in text proto format"*.
  The wording is not graded — like every other description it is yours —
  but the *behaviour* it misdescribes at v1.0 is: the file is text proto at
  every release in this task.
* `metrics_endpoint` / `log_rpc_server` defaults becoming empty at **v1.1.0**
  is a real behaviour change: from v1.1.0 a process started with no
  `--log_rpc_server` and a plain `LogConfigSet` has nowhere to send RPCs and
  must say so, where before it would silently dial `localhost:8090`.
* `quota_remote` and `quota_intermediate` default to **true**. Quota is on
  unless you turn it off.
* `handler_prefix` prefixes every handler path that does not carry its own
  prefix, so `--handler_prefix=/logs` moves `add-chain` to
  `/logs/<prefix>/ct/v1/add-chain`.
* `mask_internal_errors`, when set, strips the error text from 500 responses
  (HTTP-API.md §2.1).
* `cache_type` accepts `noop` and `lru`; an unrecognised value is a start-up
  failure. The empty string is `cache.Type`'s zero value `Unknown`, which the
  factory maps to the no-op (CLIENT-API.md §8.4), so `--cache_type=` starts
  rather than refusing. `cache_size` and `cache_ttl` are meaningless for
  `noop`.
* `non_fresh_submission_limit` is a rate in the form `"<count>/<duration>"`,
  e.g. `30/1s`; empty disables the limiter.
* `max_cert_chain_size` bounds the request body of `add-chain` and
  `add-pre-chain` only; over-size bodies get **413** (HTTP-API.md §6.3).
* `by_range` (v1.0.10) selects the range-based backend RPC for `get-entries`;
  it is removed at **v1.1.2**, when that RPC becomes the only path.
* `align_getentries` (v1.1.2, default **true**) truncates a maximally-sized
  `get-entries` range down to the next multiple of the maximum, so that
  responses are cacheable (HTTP-API.md §4.3).
* `allow_verification_with_non_compliant_keys` (v1.0 .. v1.0.19) suppresses the
  key-compliance refusal when a signature verifier is constructed over a key
  RFC 6962 does not permit — an RSA key under 2048 bits, or an ECDSA key off
  the P-256 curve. With the flag unset, constructing the verifier fails with
  `public key is RSA with < 2048 bits (size:%d)` or
  `public is ECDSA, but not on the P256 curve`; with it set, the same text is
  logged as a warning and the verifier is returned. At **v1.0.21** the flag is
  **removed from every command line** and replaced by an exported package
  variable of the same meaning, `AllowVerificationWithNonCompliantKeys`,
  defaulting to false (CLIENT-API.md §3). This is a removal, not a change of
  default: after v1.0.21 no command line can ask for the old behaviour, and
  passing the flag is an unrecognised-flag exit **2**.

### 2.2 Start-up order and what is fatal

The order matters because the verifier observes *which* failure a
misconfigured process reports, and because a process that fails must not have
bound its port.

1. Parse flags. (Unknown flag → exit 2, from the `flag` package.)
2. Read and validate `--log_config`. From **v1.0.5** these are two reportable
   outcomes: unreadable → `Failed to read config: …`, invalid →
   `Invalid config: …`. Both exit 1, before any socket is touched. At **v1.0**
   there is one combined outcome instead — either failure exits 1 reporting
   `Failed to read log config: …` — and the split wording above does not apply.
3. Resolve the backend set. A `LogConfigSet` with no `--log_rpc_server`, or a
   `LogMultiConfig` *plus* `--log_rpc_server`, is a configuration error.
4. From **v1.3.3**, if `--trillian_tls_ca_cert_file` is set, read and parse it
   as a CA certificate. If it cannot be read or holds no certificate,
   `Failed to create TLS credentials from Trillian CA certificate: …` and
   exit 1 — before the etcd connection, before the backend dial and before any
   socket is bound.
5. If `--etcd_servers` is set, connect to etcd; failure exits 1.
6. Dial each backend. The dial is **blocking** only when the configuration
   resolves to exactly one backend — which is every `LogConfigSet`, since
   `--log_rpc_server` becomes a single backend whatever it names, and every
   single-backend `LogMultiConfig`. There, unreachable → `Could not dial RPC
   server: …` and exit 1, which is why the verifier can rely on "no backend,
   no listener". That spelling is from **v1.0.5**; at **v1.0** the same
   failure reports `Could not connect to rpc server: …`. From **v1.0.5**, a
   configuration naming **two or more**
   backends dials them all non-blocking and the process begins serving
   immediately whether or not any of them ever answers; a request that then
   needs an unreachable backend fails on its own — the backend's `Unavailable`
   surfaces as **503** — instead of preventing start-up.
7. For each log, `SetUpInstance`. Failure → `Failed to set up log instance for
   …` and exit 1. This is where a bad key, a missing roots file or an
   unparseable admission policy surfaces.
8. From **v1.2.0**, reject a configuration in which two logs share a private
   key: `Same private key used by more than one log`, exit 1. The comparison is
   made on the **public key each instance derives from its signer**, against
   the public keys of the logs already set up in this pass, so it fires however
   the duplicate was spelled -- the same `PrivateKey` file named twice, the same
   PEM inlined twice, or two encodings of one key. It covers ECDSA, Ed25519 and
   RSA keys; a signer whose public key is of any other type is not compared.
   Because the check runs inside the per-log loop of step 7, the logs already
   registered when the duplicate is found have been set up, and the exit
   happens before the listener opens. Before v1.2.0 no release performs the
   check and such a configuration starts normally. CONFIG.md section 2.3 states
   the same rule from the configuration side.
9. Register handlers, optionally start the metrics listener and the periodic
   internal `get-sth`, then serve.

A successful start-up prints nothing to stdout and serves until signalled.

### 2.3 What changes about *running* it

* **v1.3.3** — the HTTP listener is TLS when **both** `--tls_certificate` and
  `--tls_key` are set. The pair is inert unless both are set: given only one of
  them the process starts normally and the listener stays plain HTTP, so a
  missing half is not a start-up error and produces no diagnostic.
  `--trillian_tls_ca_cert_file` switches the *backend* dial to TLS with those
  CA roots. `--http_idle_timeout` is applied to the server; a negative value
  means "leave the Go default alone".

#### Signals and how the process ends

`ct_server` installs exactly one handler, for **SIGINT and SIGTERM together**,
before it begins serving; nothing else is trapped and no release re-reads its
configuration on a signal — there is no reload, only termination. The signal
is logged first in every release. What happens next splits at **v1.0.21**:

| releases | on SIGINT or SIGTERM |
| --- | --- |
| v1.0 .. v1.0.19 | the listener is abandoned where it stands and the process calls `os.Exit(1)` — **exit status 1**, in-flight requests dropped |
| v1.0.21 .. v1.3.3 | the HTTP server is shut down gracefully with a 60-second grace period for in-flight requests; `ListenAndServe` returns `http.ErrServerClosed`, `main` waits for the shutdown to finish and returns — **exit status 0** |


---

## 3. `ctclient`

The RFC 6962 client as a command-line tool. It is rebuilt on a different
command framework at **v1.1.3** (step 10) and the two grammars are
incompatible — that break is itself the graded thing.

### 3.1 v1.0 .. v1.1.2 — standard `flag`, one positional command word

    ctclient [options] <cmd>

Exactly one positional word. A missing or unknown command prints the message
plus a usage block beginning `Usage: ctclient [options] <cmd>` and the list of
commands **to stderr**, and exits **1**.

Commands, and the release each arrives at:

| command | aliases | from |
| --- | --- | --- |
| `sth` | — | v1.0 |
| `upload` | — | v1.0 |
| `getroots` | `get_roots`, `get-roots` | v1.0 |
| `getentries` | `get_entries`, `get-entries` | v1.0 |
| `inclusion` | `inclusion-proof` | v1.0.5 |
| `consistency` | — | v1.0.13 |
| `bisect` | — | v1.1.0 |

Global flags, with the release each arrives at (all removed flags noted):

| flag | type | default | arrives / leaves |
| --- | --- | --- | --- |
| `log_uri` | String | `http://ct.googleapis.com/aviator` → `…/rocketeer` at v1.0.10 → `https://…/rocketeer` at v1.0.19 | v1.0 |
| `pub_key` | String | `""` | v1.0 |
| `cert_chain` | String | `""` | v1.0 |
| `text` | Bool | `true` | v1.0 |
| `first`, `last` | Int64 | `-1` | v1.0 |
| `size`, `leaf_hash` | Int64 / String | `-1` / `""` | v1.0.5 |
| `log_mmd` | Duration | `24h` | v1.0.10 |
| `log_name` | String | `""` | v1.0.13 |
| `log_list` | String | `loglist.LogListURL` → `AllLogListURL` at v1.1.0 | v1.0.13 |
| `tree_hash`, `prev_hash` | String | `""` | v1.0.13 |
| `prev_size` | Int64 | `-1` | v1.0.13 |
| `dns_base` | String | `""` | v1.0.13, **removed v1.1.2** |
| `dns` | Bool | `false` | v1.0.13, **removed v1.1.2** |
| `dns_server` | String | `""` | v1.1.0, **removed v1.1.2** |
| `skip_https_verify` | Bool | `false` | v1.1.0 |
| `timestamp` | Int64 | `0` | v1.1.0 |
| `chain` | Bool | `false` | v1.1.0 |

Error handling moves from `log.Fatal*` to `glog.Exit*` at v1.0.13; the exit
status (1) does not change.

**`inclusion` and `consistency` verify, they do not only fetch.** Both fetch
the proof from the log and print it, and both then check it with whichever
proof verifier the release owns (MERKLE.md §4) — so the answers they give are
the answers of MERKLE.md §2 and §3, at every release, however the verifier is
obtained:

* `inclusion` needs `--leaf_hash` (or `--cert_chain`) and takes the tree size
  from `--size`, or from the log's current STH when `--size` is not positive.
  When it has an STH it verifies the returned audit path against that STH's
  root hash.
* `consistency` requires **both** `--size` and `--prev_size` to be positive,
  and accepts `--tree_hash`/`--prev_hash` only as a pair — one without the
  other is an error. With both hashes supplied it verifies the returned proof
  between them; with neither it prints the proof and verifies nothing.

`upload`'s `--cert_chain` is mandatory **and must yield at least one
certificate**. The reader collects the blocks whose PEM type is exactly
`CERTIFICATE` and passes over every other block, so a file of keys, CSRs or
comment text alone is the zero-certificate case. All three refusals — no flag
at all, a file that cannot be read, and a file that reads to no certificate
block — are fatal **before the log is contacted**: the program exits **1**,
writes nothing to standard output, and sends no request. The same holds at
every release, including the rebuilt command-line client of v1.1.3 (§3.2),
whose messages are the ones below carried over unchanged.

Any failure — a proof that does not reconstruct the expected root, a range
with `prev_size` greater than `size`, a missing or ill-formed bound — exits
**1** without printing a verified-ok line, and the diagnosis goes to
**standard error**. A failure noticed before the proof is printed leaves
standard output **empty**; one noticed while checking a proof the log did
return has already printed that proof, because both commands print what they
fetched and only then verify it. A hash argument is **ill-formed** unless it
decodes to exactly **32 bytes** — a leaf hash, a tree hash and a previous tree
hash are all SHA-256 values — so a string that is too short, too long or not
decodable at all never reaches `get-proof-by-hash` (or `get-sth-consistency`).
Through **v1.1.2** the only accepted spelling is hex; from **v1.1.3** hex or
standard base64 (CLI.md §3.2's table), which is the same rule that release's
`--leaf_hash` help text states. Because `--prev_size` must be positive, the
`n₁ == 0` case of MERKLE.md §3 is not reachable through this command at any
release.

#### What the four v1.0 commands print

Standard output, in this order. `<when>` is a timestamp rendered by Go's
`time.Time.String()` after `time.Unix(ms/1000, (ms%1000)*1e6)` — the
milliseconds-since-epoch field split into seconds and nanoseconds, in the
local zone. `<sig>` is the one-line signature rendering

    Signature: Hash=%v Sign=%v Value=%x

over `DigitallySigned.Algorithm.Hash`, `.Algorithm.Signature` and
`.Signature`: the two `Enum` names of TLS-CODEC.md §1 and the raw signature in
lower-case hex. That form is unchanged from v1.0 to v1.1.2.

| command | what it writes to stdout at v1.0 |
| --- | --- |
| `sth` | `<when>: Got STH for <version> log (size=<n>) at <log-uri>, hash <root-hash-hex>` + newline, then `<sig>` + newline |
| `upload` | for a pre-certificate chain only, `Uploading pre-certificate to log` + newline first; then `<when>: Uploaded chain of <n> certs to <version> log at <log-uri>` + newline; **from v1.0.5** `LeafHash: <hex>` + newline, the 32-byte leaf hash of what was submitted in lower-case hex (MERKLE.md §1); then `<sig>` + newline |
| `getroots` | every accepted root, in the order the log returned them, through the certificate renderer below — nothing else |
| `getentries` | per entry, `Index=<i> Timestamp=<when> ` with **no** newline, then either `X.509 certificate:` + newline and the leaf certificate through the renderer, or `pre-certificate from issuer with keyhash <hex>:` + newline and the pre-certificate through the renderer below — its TBS at v1.0, the submitted certificate from **v1.0.1** |

`<version>` is `Version.String()` / `SCTVersion.String()`, so `V1`
(TLS-CODEC.md §5.1). `<log-uri>` at v1.0 is the `--log_uri` flag exactly as
given; **from v1.0.17** both lines print the client's stored base URI instead
(CLIENT-API.md §1.1 — every trailing `/` removed).

The certificate renderer is selected by `--text`, which defaults to **true**:

* `--text` (the default) prints `x509util.CertificateToString` of the parsed
  certificate followed by a newline. **At v1.0 only**, `getentries` takes the
  pre-certificate's TBS out of the leaf and parses it with
  `x509.ParseTBSCertificate` before rendering it.
* `--text=false` prints a PEM `CERTIFICATE` block for a whole certificate.
  **At v1.0 only**, a pre-certificate's TBS instead prints as `%x` of the raw
  bytes and a newline, **not** PEM.
* **From v1.0.1** `getentries` hands the renderer the *submitted* certificate
  out of `extra_data` rather than the TBS out of the leaf, so a
  pre-certificate entry renders exactly like an X.509 one: a PEM `CERTIFICATE`
  block under `--text=false`, the parsed certificate under the default. The
  header line above it is unchanged and still carries the issuer key hash
  taken from the leaf.
* A certificate that will not parse is logged and skipped, not fatal.

**The lines that move.** Each is worth pinning only at the release under
test:

| line | releases |
| --- | --- |
| `%v: Got STH for %v log (size=%d) at %v, hash %x` | v1.0 .. v1.0.10 |
| `%v (timestamp %d): Got STH for %v log (size=%d) at %v, hash %x` | v1.0.13 .. v1.1.2 |
| `%v: Uploaded chain of %d certs to %v log at %v` | v1.0 .. v1.0.5 |
| `LeafHash: %x` — `upload` only, on the line after whichever `Uploaded chain …` line the release prints, or after `LogID:` from v1.0.13 | **v1.0.5** onward |
| `Uploaded chain of %d certs to %v log at %v, timestamp: %v` | v1.0.10 only |
| `Uploaded chain of %d certs to %v log at %v, timestamp: %d (%v)` | v1.0.13 .. v1.1.2 |
| `Index=%d Timestamp=%v ` | v1.0 .. v1.0.19 |
| `Index=%d Timestamp=%d (%v) ` | v1.0.21 .. v1.1.2 |
| `LogID: %x` — `upload` only, between the `Uploaded chain of …` line and `LeafHash:` | **v1.0.13** onward |
| `Signature: ` printed by `upload`'s own call site in front of `<sig>`, so the label appears twice on that line | **v1.0.5** onward (`sth` prints `<sig>` alone at every release) |
| the `getentries` pre-certificate entry rendered from the *submitted* certificate rather than from the TBS | **v1.0.1** onward |

The usage block, **on stderr**, is the caller's message on its own line and
then, at v1.0:

    Usage: ctclient [options] <cmd>
    where cmd is one of:
       sth         retrieve signed tree head
       upload      upload cert chain and show SCT (needs -cert_chain)
       getroots    show accepted roots
       getentries  get log entries (needs -first and -last)

with one further line as each later command in the table above arrives. Three
spaces of indent, then the command name padded to a fixed column — 12
characters wide through v1.0.10, widening to 14 at v1.0.13 when
`consistency` joins the list.

**This block is not what `-h`/`--help` prints.** It is the program's own
message, printed when the command word is missing or is not one of the names
above. `-h`/`--help` never reaches it: the `flag` package handles the flag
first, prints its own `Usage of <prog>:` banner and flag list to **stderr**,
and exits **0** — the §1 grammar, unchanged by anything in this section, and
what a group runs every one of these binaries with.

### 3.2 v1.1.3 .. v1.3.3 — subcommands with per-command flags

The program becomes a root command with named subcommands, each carrying its
own flag set, and `main.go` shrinks to a three-line shim over a `cmd/`
package. The consequences the verifier can see:

* **No subcommand accepts positional arguments.** Passing one is an error.
* Flags split into *persistent* (accepted before or after the subcommand,
  applying to all) and *per-command*.
* `text` and `chain` move from global to per-command.
* `size` and `prev_size` change type from Int64 to **Uint64** and their
  default from `-1` to **`0`**.
* Canonical command names become hyphenated, with the old names kept as
  aliases.
* `cmd`'s package `init()` does `pflag.CommandLine.AddGoFlagSet(flag.CommandLine)`,
  merging whatever is registered on Go's own `flag.CommandLine` at that moment
  into the command framework's flag set, where the framework then folds it into
  the root command's *persistent* flags. **What that merge picks up changes at
  v1.1.4.** glog registers its flags in glog's own package `init()`, which has
  already run, so at v1.1.3 `-v`, `-logtostderr` and the rest of glog's seven do
  appear under `Global Flags:` and still work. klog registers nothing until
  `klog.InitFlags(nil)` is called, and `main()` calls it *after* every package
  `init()` has run — so from v1.1.4 the merge copies an **empty** set, no
  logging flag reaches `ctclient`'s help, and `-v` is rejected as an unknown
  flag. The persistent set at v1.1.4 is exactly the five below plus the
  framework's own `help`.
* An unknown subcommand or unknown flag prints usage to **stderr**; the
  resulting error goes through `Fatal`, so the exit status is **255**, not 1.

Persistent flags: `skip_https_verify` Bool `false`; `log_name` String `""`;
`log_list` String `loglist.AllLogListURL` (`loglist3.AllLogListURL` from
v1.1.4); `log_uri` String `https://ct.googleapis.com/rocketeer`; `pub_key`
String `""`.

| command | aliases | its own flags |
| --- | --- | --- |
| `get-sth` | `sth` | — |
| `get-roots` | `getroots`, `roots` | `text` Bool `true` |
| `get-entries` | `getentries`, `entries` | `first` Int64 `-1`, `last` Int64 `-1`, `chain` Bool `false`, `text` Bool `true` |
| `get-inclusion-proof` | `getinclusionproof`, `inclusion-proof`, `inclusion` | `leaf_hash` String `""`, `cert_chain` String `""`, `timestamp` Int64 `0`, `size` Uint64 `0`; **v1.3.3 adds** `extensions` String `""` |
| `get-consistency-proof` | `getconsistencyproof`, `consistency-proof`, `consistency` | `size` Uint64 `0`, `tree_hash` String `""`, `prev_size` Uint64 `0`, `prev_hash` String `""` |
| `upload` | `add-chain` | `cert_chain` String `""`, `log_mmd` Duration `24h` |
| `bisect` | `find-timestamp` | `timestamp` Int64 `0`, `chain` Bool `false`, `text` Bool `true` |

Between v1.1.3 and v1.3.3 the only flag change is `--extensions` on
`get-inclusion-proof` at v1.3.3.

#### The generated help, which is the only way to read the tree back out

The command framework generates `--help` from the declarations, so the
declarations are the interface. They do not change between v1.1.3 and v1.3.3.

The root command is `ctclient` with the short description

    A command line client for Certificate Transparency logs

and no `Run`, so a bare invocation prints the root help on **stdout** and exits
**0** rather than failing. That help opens with the short description, a blank
line, `Usage:` and `  ctclient [command]`, and lists the seven subcommands
under `Available Commands:` (the framework's own `help` and `completion`
entries appear there too and are not part of this inventory).

Every subcommand's usage line splices in one shared connection-flag fragment:

    {--log_uri uri | --log_name name [--log_list {file|uri}]} [--pub_key file]

Writing that fragment as `CONN`, the seven leaves are

| command | short description | usage line |
| --- | --- | --- |
| `get-sth` | Fetch the latest STH of the log | `get-sth CONN` |
| `get-roots` | Fetch the root certificates accepted by the log | `get-roots CONN` |
| `get-entries` | Fetch a range of entries in the log | `get-entries CONN --first=idx [--last=idx]` |
| `get-inclusion-proof` | Fetch and verify the inclusion proof for an entry | `get-inclusion-proof CONN {--leaf_hash=hash \| --cert_chain=file} [--timestamp=ts] [--size=N]`; **v1.3.3 appends** ` [--extensions=exts]` |
| `get-consistency-proof` | Fetch and verify a consistency proof between two tree states | `get-consistency-proof CONN --size=N --tree_hash=hash --prev_size=N --prev_hash=hash` |
| `upload` | Submit a certificate (pre-)chain to the log | `upload CONN --cert_chain=file [--log_mmd=dur]` |
| `bisect` | Find a log entry by timestamp | `bisect CONN --timestamp=ts [--chain] [--text=false]` |

`bisect` is the one command with no v1.0-era prose elsewhere in this document:
it binary-searches the log for the first entry whose timestamp is at or after
`--timestamp`, issuing a `get-entries` request for one index per probe, and
prints the entry it lands on in the same shape `get-entries` uses.

`get-entries`'s `--last` needs one sentence of its own: **when it is left at its
default, `--last` is `--first`**, at every release. `get-entries CONN
--first=7` therefore asks for the single entry at index 7 — the request goes
out with `end == start == 7`, not with the `-1` the flag's own default carries
and not with "everything to the end of the log". `--first=7 --last=9` is the
three-entry form.

Each flag carries a description, and those descriptions are what the help
prints beside it. They are unchanged from the v1.1.2 `flag`-package
declarations of §3.1 for every flag that existed there:

| flag | description |
| --- | --- |
| `skip_https_verify` | Skip verification of HTTPS transport connection |
| `log_name` | Name of log to retrieve information from --log_list for |
| `log_list` | Location of master log list (URL or filename) |
| `log_uri` | CT log base URI |
| `pub_key` | Name of file containing log's public key |
| `first` | First entry to get |
| `last` | Last entry to get |
| `chain` | Display entire certificate chain |
| `text` | Display certificates as text |
| `leaf_hash` | Leaf hash to retrieve (as hex string or base64) |
| `cert_chain` | Name of file containing certificate chain as concatenated PEM files |
| `timestamp` | Timestamp to use for inclusion checking |
| `size` | Tree size to query at |
| `tree_hash` | Tree hash to check against (as hex string or base64) |
| `prev_size` | Previous tree size to get consistency against |
| `prev_hash` | Previous tree hash to check against (as hex string or base64) |
| `log_mmd` | Log's maximum merge delay |
| `extensions` (v1.3.3) | CT extensions in SCT (as hex string) |

The five persistent flags appear under `Global Flags:` in every leaf command's
help, the per-command flags under `Flags:` in their own. The `log_list` default
— and the value the help prints in its `(default "…")` — is the log-list
package's **all-logs** constant, not its signed-list constant. The two are
distinct at every release, and the all-logs one is what any flag *named*
`log_list` defaults to:

| release | constant | its value |
| --- | --- | --- |
| up to and including v1.1.3 | `loglist.AllLogListURL` | `https://www.gstatic.com/ct/log_list/all_logs_list.json` |
| from v1.1.4 | `loglist3.AllLogListURL` | `https://www.gstatic.com/ct/log_list/v3/all_logs_list.json` |
| from v1.1.4, not a `log_list` default — but the value the submission server's `--loglist_path` (§4.9) and the witness tools' `--log_list_url` (§4.10) default to | `loglist3.LogListURL` | `https://www.gstatic.com/ct/log_list/v3/log_list.json` |

`loglist3.LogListURL` is the signed list and has `loglist3.LogListSignatureURL`
(`…/v3/log_list.sig`) beside it; it is what the witness tools take on
`--log_list_url` (§4.10). Anything whose flag is called `log_list` — `ctclient`,
`sctcheck`, `sctscan`, `ct_hammer` — defaults to `AllLogListURL`.

Resolution by name is observable only through its own refusal. `--log_name` is
matched against the list's log **descriptions** — case-insensitively, and as a
substring, so `dubious` matches a log described `Dubious Log` — and the command
proceeds only when exactly one description matches. Both no match and more than
one match are fatal, and the two refusals say different things: the first names
the log list the tool actually read, the second names every description that
matched, comma-separated, in the order the list gives them. `ctclient` resolves
this way from v1.0.13 on, and `ct_hammer`'s `--src_log_name` (§4.8) resolves and
refuses the same way from v1.1.0 on.

The *diagnostics* are not this document's: the framework writes `Error: unknown
flag: …`, `Error: unknown command "…" for "ctclient"` and `Error: accepts at
most 0 arg(s), received N` in its own words, pinned by the version in the
module manifest, and the per-command failure messages (`No -first option
supplied`, `No certificate chain file specified with -cert_chain`, `No
-timestamp option supplied`, `No valid --size supplied`, `Need both --prev_hash
and --tree_hash or neither`) and the success renderings (`Got STH for …`,
`Uploaded chain of …`, `Inclusion proof for index …`, `Verified that hash …`,
`Consistency proof from size …`, `First entry with timestamp>=…`) are carried
over from the v1.1.2 file the rebuild replaces, unchanged, word for word.

---

## 4. The other programs

Grammar and the flags that change across releases. Flags not mentioned here do
not change once introduced.

### 4.1 `scanlog` — scans a log, matching entries

No positionals. v1.0: `log_uri` (String, `http://ct.googleapis.com/aviator`),
`match_subject_regex` (`.*`), `match_issuer_regex` (`""`), `precerts_only`,
`serial_number`, `batch_size` (Int `1000`), `num_workers` (Int `2`),
`parallel_fetch` (Int `2`), `start_index` (Int64 `0`), `quiet` (Bool `false`),
`print_chains` (Bool `false`).

Changes: **v1.0.10** adds `parse_errors`, `non_fatal_errors`, `validate_errors`
(all Bool `false`) and `dump_dir` (String `""`) — the error-scanning mode.
**v1.0.17** adds `end_index` (Int64 `0`). **v1.0.19** removes `quiet` and moves
the default URI to `https://`. **v1.0.21** adds `sct_timestamp_ms` (Uint64
`0`). No flag changes after v1.0.21. Uses the standard `log` package
throughout, never glog/klog.

`--start_index` and `--end_index` are the scanner's half-open range
`[start_index, end_index)`, handed straight to the fetcher of CLIENT-API.md
§5.1: from v1.0.17 `--end_index=0` means "to the current tree size", and a
non-zero value is clamped down to the tree size but never up. The log is then
walked in consecutive `get-entries` requests of at most `--batch_size` entries
each, so the requests a bounded scan makes are determined by the two bounds
and the batch size alone — the last request is short when `--end_index` cuts
it. A scan that completes exits **0**, whether or not anything matched.

`--dump_dir` writes each matched entry's DER into that directory, one file per
certificate, named from the entry's own log index and never from its contents:

| file | name | holds |
| --- | --- | --- |
| an X.509 entry's leaf | `cert-%014d-leaf.der` | the leaf certificate exactly as the log served it |
| a precert entry's precertificate | `pecert-%014d-precert.der` (the prefix really is misspelled, at every release that has the flag) | `chain[0]` |
| each issuer above it | `<prefix>-%014d-%02d.der` | that chain element, numbered by its chain position |

`%014d` is the zero-padded decimal log index, so a directory listing sorts in
log order. Write failures are logged and do not change the exit status.

### 4.2 `preloader` / `dumpscts` — copy a log into another log

`preloader` takes no positionals. v1.0: `source_log_uri`, `target_log_uri`,
`target_temporal_log_cfg`, `batch_size` (1000), `num_workers` (2),
`parallel_fetch` (2), `parallel_submit` (2), `start_index` (0), `quiet`,
`sct_file`, `precerts_only`.

Changes: **v1.0.19** removes `quiet`, moves URIs to `https://`, and switches to
`glog.Exit*`. **v1.0.21** adds the seven HTTP transport-tuning flags
(`tls_timeout` 30s, `response_header_timeout` 30s, `max_idle_conns_per_host`
10, `max_idle_conns` 100, `idle_conn_timeout` 90s, `disable_keepalive` false,
`expect_continue_timeout` 1s). **v1.3.3** adds `target_bearer_token` (String
`""`), `end_index` (Int64 `0`) and `continuous` (Bool `false`) — and makes
`--continuous` with a non-zero `--end_index` a fatal error, exit 1. That check
reads nothing but the two parsed flags, and it is made **before** the target
temporal log configuration is read, so the combination is refused even when
`--target_temporal_log_cfg` names a file that does not exist. The wording of
the refusal is not fixed by this document.

`dumpscts` has exactly one flag at every release, `sct_file` (String `""`), and
exits 1 with `Must specify --sct_file` if it is empty.

### 4.3 `certcheck` / `crlcheck` — from v1.0.5

Both take one or more positionals; zero positionals is legal and exits 0.
`certcheck`'s positionals are files, and **from v1.0.21** a positional
beginning `https://` is fetched from that site instead. `crlcheck`'s
positionals are "file or URL" **from v1.0.5** — the release the binary
arrives at — and stay that way through v1.3.3: each CRL positional goes
through `x509util.ReadPossiblePEMURL` (CLIENT-API.md §5.2), so one beginning
`http://` or `https://` is fetched and anything else is read as a file.
v1.0.10 changes the flag set below and does **not** change that; what it does
add is that under `-cert` the positional is read as a plain **file**
(`ReadPossiblePEMFile`), never as a URL, and that too is unchanged through
v1.3.3.

`certcheck` v1.0.5: `root`, `intermediate` (String `""`), `verbose`,
`validate`, `timecheck`, `check_revocation` (all Bool `false`).

`timecheck` (Bool `false`; renamed `check_time` at v1.1.0 with the same default
and polarity) is what puts **validity dates** into `certcheck`'s verdict, and
it is the only thing that does. With the flag absent, verification runs with
the time checks **off**: an expired or not-yet-valid certificate still verifies
and the run exits **0**. With it given, a certificate outside its validity
window fails verification and the target is counted. Nothing else about the
run's verification changes with the flag.
**v1.0.10** adds `ignore_unknown_critical_exts` (Bool `false`). **v1.0.21**
adds `system_roots` (Bool `false`).

**v1.1.0 is the rename milestone** and it is graded, so read it carefully:

| before v1.1.0 | at v1.1.0 | note |
| --- | --- | --- |
| `timecheck` Bool `false` | `check_time` Bool `false` | pure rename |
| `ignore_unknown_critical_exts` Bool `false` | `check_unknown_critical_exts` Bool **`true`** | **polarity and default both flip** |
| `check_revocation` | `check_revocation` | unchanged |

v1.1.0 also adds `strict` (Bool `true`), `check_name` (Bool `true`),
`check_eku` (Bool `true`), `check_path_len` (Bool `true`) and
`check_name_constraint` (Bool `true`). No **flag** changes after v1.1.0.

`strict` decides what a **non-fatal** parse error costs. A certificate that
parses only with a `NonFatalErrors` (X509-FORK.md §3) is still a usable
certificate and is still checked; with `strict` at its default that target is
nonetheless added to the failure count described below, so the run exits **1**,
and with `-strict=false` it is not added and the run exits **0**. A *fatal*
parse error fails the target either way. `strict` switches no `check_*` test on
or off and has no effect on `--validate`.

**What `--validate` validates changes at v1.0.17**, and it is graded:

| release | a file holding several certificates |
| --- | --- |
| v1.0.5 .. v1.0.13 | every certificate in the file is verified **independently** against the pools built from `--root`/`--intermediate` alone. A self-contained chain file therefore always fails: the leaf's issuer is not in any pool, and neither is the root. |
| **v1.0.17** onward | if **neither** `--root` nor `--intermediate` was given, the file is read as a **chain**, leaf first. If the last certificate is self-signed (its subject equals its issuer) it is adopted as a trust anchor for this file; every certificate between the leaf and that anchor becomes an intermediate; and **only the leaf** is verified. |

**What "neither" means splits at v1.0.21**, and the split is graded. At
**v1.0.17 and v1.0.19** a single condition — `--root` empty *and*
`--intermediate` empty — gates both halves together: supplying either flag
restores the explicit-pool behaviour for that file, neither the anchor nor the
intermediates are adopted, and a file whose anchor is only inside itself then
fails. From **v1.0.21 through v1.3.3** the one condition becomes two, tested
independently:

| what is adopted from the file | condition, v1.0.21 .. v1.3.3 |
| --- | --- |
| a self-signed last certificate, as a trust anchor | `--system_roots` false **and** `--root` empty |
| every certificate after the first, as an intermediate | `--intermediate` empty |

So from v1.0.21 `--intermediate` no longer suppresses anchor adoption and
`--root` no longer suppresses intermediate adoption. `certcheck
--intermediate=some.pem --validate chain.pem` still adopts the self-signed
anchor inside `chain.pem` and still validates, where at v1.0.17 the same
command would have refused it. One further detail moves with the split: at
v1.0.17 and v1.0.19 the certificate adopted as the anchor is *excluded* from
the intermediate pool, and from v1.0.21 it is put in both.

At every release from v1.0.17 a chain file whose last certificate is *not*
self-signed supplies no anchor, so an unrooted chain is still refused. Also
from v1.0.17, **only the leaf is verified whatever the flags say**: the single
`chain[0].Verify` sits outside the adoption branch, so the move away from the
per-certificate loop of v1.0.5 .. v1.0.13 is not conditional on the flags the
way the adoption is. `--verbose` says when an anchor was adopted at **v1.0.17
and v1.0.19 only** — the note goes away with the helper refactor at v1.0.21
(see "Which stream each writes on" below), while the adoption itself stays.

Exit status is the only thing a caller should key on: certcheck accumulates one
failure count over all targets and all reasons — unreadable file, parse error,
revocation, verification — and exits **1** if it is non-zero, **0** otherwise.
The diagnostics themselves go to stderr and their wording is not stable across
releases.

`crlcheck` v1.0.5: `ca` (String `""`), `verbose` (Bool `false`). **v1.0.10**
removes `verbose` and adds `strict` and `cert` (both Bool `false`). Nothing
changes after v1.0.10.

`crlcheck` reads each positional through `x509util.ReadPossiblePEMURL` with the
block label **`X509 CRL`** (CLIENT-API.md §5.2) — that exact spelling, at every
release; a PEM block labelled anything else is skipped by the reader and the
target then yields no CRL. A **DER** file needs no label: with no
`BEGIN X509 CRL` in the bytes the reader returns them whole.

**Which stream each writes on.** The description `--verbose` asks for is
written to **stdout**; every diagnostic goes to stderr. For `certcheck` that
holds at every release v1.0.5 .. v1.3.3 — the certificate description and the
revocation data `--check_revocation` fetched are on stdout at all of them, and
the "treating the final cert in the chain as a root" note is too, at the two
releases that have it (**v1.0.17 and v1.0.19 only**; v1.0.21 moves chain
validation into a helper and drops the note). A run
**without** `--verbose` therefore writes nothing at all there, whatever its exit
status. `crlcheck` behaves the same way **at v1.0.5**, the one release where it
has a `verbose` flag. **From v1.0.10** `crlcheck` has no `verbose` flag and
writes nothing to stdout at all: the CRL description becomes an *info* record
of the logging package the release uses — `glog` through v1.1.3, `klog` from
v1.1.4 — so where it goes is whatever the logging flags say, and it is stdout
under none of them. No group at any step reads that record; what is graded of
`crlcheck` after v1.0.5 is its flag set and its exit status.

**What the file-valued flags accept.** `crlcheck`'s `-ca` goes through
`x509util.ReadPossiblePEMFile` at every release from v1.0.5, so it reads a PEM
or a DER certificate file indifferently, and so do `certcheck`'s positionals.
`certcheck`'s `--root` and `--intermediate` do **not**, and the difference is
graded: at **v1.0.5** they are read raw and parsed as DER, so a PEM file there
is a fatal parse failure; **from v1.0.10** they go through the same possible-PEM
reader and accept either encoding.

Both set a failure flag per target and `os.Exit(1)` at the end if any target
failed; otherwise 0.

### 4.4 `sctcheck` / `sctscan` — from v1.0.13

`sctcheck [flags] <target>…`, where a target is an `https://` URL (chain taken
from the TLS handshake) or a PEM file. Zero targets is legal, exit 0. Exits 1
if any target had an invalid SCT.

An **invalid** SCT is not only one whose signature fails to verify: it is any
SCT the program examined and could not fully check. At **v1.0.13 .. v1.0.21**
nothing suppresses the inclusion step, so an inclusion proof is attempted for
every SCT whose log the program resolved in the list, and an SCT whose
inclusion cannot be shown — for any reason, a log that cannot be reached
included — is counted invalid and makes the run exit **1**, even when the SCT
and its signature are otherwise sound. An SCT younger than its log's MMD is
*reported* less severely, a warning rather than an error, but is counted
invalid just the same: at every release the MMD comparison changes the log line
and never the verdict. **v1.1.0**'s `check_inclusion` (Bool `true`) makes that
step **optional** — it makes an existing check skippable, it does not introduce
it — and from that release a signature that fails to verify also counts its SCT
invalid on its own, which through **v1.0.21** it does not: there a signature
failure is reported and the verdict rests on whether the SCT could be
deserialised, its log found in the list by key hash, its log info built, and
its inclusion shown.

A *target* is never fatal. One that cannot be read, that holds no PEM block,
or that holds no certificate at all is reported and **skipped**, and the run
carries on to the next target; with no invalid SCT anywhere it still exits
**0**. That asymmetry with the log list below is deliberate and holds at every
release the program exists.

The log list named by `--log_list` is read and parsed **before** the first
target is examined, so a list that cannot be fetched or does not parse is
fatal: the program exits **non-zero** without looking at any target, even when
every target would have been clean on its own. The exact status is whatever the
runtime's fatal log-and-exit produces and is not a contract; only the
invalid-SCT exit is pinned at 1.

v1.0.13: `log_list` (String, the log-list URL), `deadline` (Duration `30s`),
`dns` (Bool `false`). **v1.1.0** adds `check_inclusion` (Bool **`true`**).
**v1.1.2** removes `dns`. **v1.1.4** moves the default to the v3 log list.

`sctscan` takes no positionals. v1.0.13: `log_uri`, `log_list`, `dns` (Bool
**`true`**), `inclusion` (Bool `false`), `deadline` (30s), `batch_size` (1000),
`num_workers` (2), `parallel_fetch` (2), `start_index` (0), `quiet`.
**v1.0.19** removes `quiet`. **v1.1.2** removes `dns`.

The DNS flags across `ctclient`, `sctcheck` and `sctscan` all disappear
together at **v1.1.2** (step 9): the DNS-over-CT client feature is withdrawn
wholesale.

Both take `--log_list` with the usage text **`Location of master CT log list
(URL or filename)`** — the "CT" is in these two and not in `ctclient`'s or
`ct_hammer`'s, whose text is `Location of master log list (URL or filename)`.
At v1.0.13 .. v1.0.21 the default is `loglist.LogListURL`; from **v1.1.0** both
take the log-list package's all-logs constant (§3.2's table), so the
`(default "…")` the help prints is
`https://www.gstatic.com/ct/log_list/all_logs_list.json` at v1.1.0 .. v1.1.3 and
`https://www.gstatic.com/ct/log_list/v3/all_logs_list.json` from **v1.1.4**,
where the list is also parsed with `loglist3`.

### 4.5 `findlog` — v1.0.13 to v1.1.3 only

`findlog [flags] <log-selector>…`; **at least one positional is required**
and zero exits 1 with `No logs specified` and no usage block. Flags:
`log_list`, `log_list_sig`, `log_list_pubkey` (String), `verbose` (Bool). At
v1.1.0 the two default URLs change (`log_list` to the all-logs list,
`log_list_sig` to empty). The program and the whole `loglist/` package are
**deleted at v1.1.4** (step 11).

A positional is a **selector**, not just a name. At every release the program
exists (**v1.0.13 .. v1.1.3**) each one is resolved with
`loglist.FuzzyFindLog` — CLIENT-API.md §4 fixes the trimming it applies and
the order it tries the forms in — so a case-insensitive **substring** of a
log's description, a log **URL** with or without its trailing `/`, and the
**base64** or **hex** encoding of either a log's 32-byte key hash or its DER
public key all resolve to that log. One selector can therefore match **more
than one** log, because the description match returns every log it matches, and
each match prints its own line; a selector that matches nothing prints nothing
and is not an error. From **v1.0.21** a hex key-hash **prefix** resolves too,
through the `FindLogByKeyHashPrefix` that release adds.

The detached signature is fetched and verified **only when `--log_list_pubkey`
names a key**. With no public key `--log_list_sig` is never read at all — not
even at v1.0.13 .. v1.0.21, where it defaults to a URL — so an offline run
given nothing but a local `--log_list` file succeeds.

### 4.6 `chainfix`

No flags at any release — the `flag` package is not imported. Strictly
positional:

    chainfix <log-url> <chains-file> <error-dir>

read directly as `os.Args[1..3]`. There is **no argument-count check**: fewer
than three arguments panics with an index-out-of-range and exit 2. That is the
upstream behaviour; do not add a check.

### 4.7 `migrillian` — from v1.0.19

Copies an existing public log into a backend. No positionals.

At **v1.0.19** it is a single-log tool configured entirely by flags — it has no
`config` flag at this release and cannot be given a configuration file — and
those flags are exactly these fourteen, with these types and defaults:

| flag | type | default |
| --- | --- | --- |
| `ct_log_uri` | String | `https://ct.googleapis.com/aviator` |
| `pub_key` | String | `""` |
| `trillian_uri` | String | `localhost:8090` |
| `log_id` | Int64 | `0` |
| `max_idle_conns_per_host` | Int | `10` |
| `max_idle_conns` | Int | `100` |
| `grpc_dial_timeout` | Duration | `5s` |
| `ct_batch_size` | Int | `512` |
| `ct_fetchers` | Int | `2` |
| `submitters` | Int | `2` |
| `submitter_batches` | Int | `5` |
| `start_index` | Int64 | `0` |
| `end_index` | Int64 | `0` |
| `mirror` | Bool | `false` |

`ct_log_uri` and `trillian_uri` are the only two of the fourteen with a
non-empty string default, and `ct_log_uri` is the only one naming a **public
log**: the tool is written to be runnable with nothing but `--trillian_uri` and
`--log_id` supplied. `start_index`/`end_index` are the
scanner's half-open range (CLIENT-API.md §5.1) — `end_index` `0` means "to the
end of the log" — and `mirror` runs the migration continuously rather than
stopping at the end, which is `FetcherOptions.Continuous`.

At **v1.0.21** it is reworked to be **config-file driven**, and this is the
one command line among the inherited programs that changes at that release.
The eight **per-log** flags are removed — `ct_log_uri`, `pub_key`,
`trillian_uri`, `log_id`, `ct_batch_size`, `start_index`, `end_index` and
`mirror` — and replaced by `config` (String `""`), plus `force_master`
(Bool `false`), `etcd_servers` (String `""`), `lock_file_path`
(`/migrillian/master`) and `metrics_endpoint` (`localhost:8099`). The six
**process-wide tuning** flags of the fourteen are *not* touched here and are
still declared: `max_idle_conns_per_host`, `max_idle_conns`,
`grpc_dial_timeout`, `ct_fetchers`, `submitters` and `submitter_batches`. So
v1.0.21's `migrillian` declares eleven flags in total. **v1.1.0**
removes the four tuning flags into the config proto and adds `election_delay`.
**v1.1.2** adds `backend` (String `""`), required: missing it is
`--backend flag must be specified`, exit 1. **v1.3.3** removes etcd entirely —
`etcd_servers`, `lock_file_path` and `election_delay` all go; running
**without** `--force_master` exits 1 with a message saying etcd is no longer
supported, while a run **with** `--force_master` proceeds like any other —
it takes mastership for granted and migrates what the configuration names,
exiting **0** once the range is complete (an empty migration set is complete
immediately). The order of the two checks is part of the contract and does not
change at this release: `--backend` is tested first, immediately after the
flags are parsed and before anything to do with mastership is looked at, so a
run with neither flag set still stops at `--backend flag must be specified`.

**The file `--config` names** (present from **v1.0.21**, the release the flag
arrives at) is a text proto `MigrillianConfig` from
`github.com/google/certificate-transparency-go/trillian/migrillian/configpb`,
with a binary-proto fallback. Its fields:

    message MigrillianConfig {
      LogBackendSet      backends          = 1;   // deprecated, never read
      MigrationConfigSet migration_configs = 2;
    }

    message MigrationConfigSet {
      repeated MigrationConfig config = 1;
    }

`migration_configs` is the one the tool reads, at every release from v1.0.21
through v1.3.3: it walks `migration_configs.config` in file order and starts
one migration controller per entry. An **empty** set is well formed and is not
an error — `migration_configs: {}`, or a `config` list with no entries in it,
names no migrations — so the run carries on past the configuration and starts
with nothing to do rather than stopping. That is what makes a `--force_master`
run of the v1.3.3 tool exit **0** with no CT log to copy: it is not waiting
for work, it has none. The names above are therefore load-bearing in both
directions: a file naming a field `MigrillianConfig` does not have is not a
warning but a refusal — after the `--backend` check, exit **1** with
`Failed to load MigrillianConfig` and the parse error naming the field — and
an entry that is present but incomplete (`MigrationConfig` needs a source URI,
a public key, a positive log ID and a positive batch size) exits 1 at
`Failed to validate MigrillianConfig`.

### 4.8 `ct_hammer`

The end-to-end load generator; no positionals. `--log_config` is required and
missing it exits 1 with `Test aborted as no log config provided (via
--log_config)` and no usage block. The per-endpoint weight flags (`add_chain`
20, `add_pre_chain` 20, `get_sth` 2, `get_sth_consistency` 2,
`get_proof_by_hash` 2, `get_entries` 2, `get_roots` 1, `get_entry_and_proof`
0, `invalid_chance` 10) exist at every release with those defaults.

Additions: **v1.0.5** `leaf_not_after`, `min_get_entries`, `max_get_entries`,
`rate_limit`, `ignore_errors`. **v1.0.10** `oversized_get_entries`,
`max_retry`. **v1.1.0** `banner`, the four source-log flags, `log_list`,
`skip_https_verify`, `buffered_chains`, `batch_size`, `parallel_fetch`,
`req_deadline`, `duplicate_chance`. **v1.1.2** `strict_sth_consistency_size`
(Bool `true`). **v1.2.0** removes `seed`. **v1.3.3** adds `bearer_token` — and **only**
`bearer_token`: `start_index` is older and survives unchanged, so it is an
inherited control here, not an addition.

At **v1.1.3** the library entry point it calls gains a leading context
parameter: `HammerCTLog(cfg)` becomes `HammerCTLog(ctx, cfg)`. No flag changes
with it. Exits 1 if any operation failed.

### 4.9 `submission` server and hammer — from v1.1.0

The submission proxy. Server flags, unchanged across all six releases except
the log-list default: `http_endpoint` (`localhost:5951`), `loglist_path` (the
v2 log-list URL, **v3 from v1.1.4**), `loglist_refresh_interval` (24h),
`roots_refresh_interval` (24h), `policy_type` (`chrome`; the only other
accepted value is `apple`), `dry_run` (false), `add_prechain_timeout` (10s),
`load_pending_qualified_logs` (true). An unrecognised `--policy_type` goes
through `Fatalf`, so it exits **255**, not 1 — this is the one place in the
product where a bad flag *value* gives 255.

Hammer flags, identical at every release: `proxy_endpoint`
(`http://localhost:5951/`), `duration` (0), `count` (10), `qps` (5).

Both lists are the flags these two binaries *declare*. A binary's `--help`
also shows every package-scope `flag` global its import graph happens to reach
— the logging library's flags (glog's seven up to v1.1.3, klog's thirteen from
v1.1.4, and see the preamble for which binaries get them), and the §2.1 flags
that belong to `ctfe` or to the root package rather than to `ct_server`. Those
are not flags of the submission binaries and are not part of either list above.

### 4.10 Gossip and witness

`gossip_server` (v1.0 → v1.1.0): `database` (`/tmp/gossip.sq3`), `listen`
(`:8080`), `log_public_keys` (comma-separated PEM files). Unchanged for its
whole life, then deleted at v1.1.2.

`goshawk` and `gosmin` exist only at v1.0.21 and v1.1.0. They are not
symmetric: `gosmin` is configured entirely by its config file and takes
`config` (String `""`) and nothing else at v1.0.21, while `goshawk` is a
scanner and declares **five** flags at that release, the config file plus four
that tune the scan.

| program | flag | type | default |
| --- | --- | --- | --- |
| `gosmin` | `config` | String | `""` |
| `goshawk` | `config` | String | `""` |
| `goshawk` | `batch_size` | Int | `1000` |
| `goshawk` | `num_workers` | Int | `2` |
| `goshawk` | `parallel_fetch` | Int | `2` |
| `goshawk` | `start_index` | Int64 | `0` |

`batch_size` is the maximum number of entries asked for per `get-entries`
call, `parallel_fetch` the number of concurrent fetches, `num_workers` the
number of concurrent matchers, and `start_index` the log index the scan starts
at. Neither program will start without its configuration: `config` has no
usable default, and both read it immediately after `flag.Parse` and stop with
`glog.Exitf` — **exit status 1**, no usage block — when it is empty or will
not load. That status is what distinguishes a refused configuration from a
successful run (0) and from a rejected flag (2, see the preamble).

At v1.1.0 `goshawk` loses `num_workers` and `start_index` — keeping
`config`, `batch_size` and `parallel_fetch` — and gains `incident_mysql_uri`,
`state_mysql_uri`, `state` and `flush_state`; `gosmin` gains
`metrics_endpoint`.

The **four** `internal/witness/cmd/` programs exist only at v1.1.3, v1.1.4 and
v1.2.0, with identical flags at all three releases. Three sit directly under
`cmd/`; the fourth, `config`, sits one level further down in
`internal/witness/cmd/witness/config/` and is a tool that writes the witness's
YAML configuration from a log list.

| program | flags and defaults |
| --- | --- |
| `witness` | `listen` (`:8000`), `db_file` (`:memory:`), `config_file` (`config/config.yaml`), `private_key` (`""`) |
| `feeder` | `log_list_url` (`https://www.gstatic.com/ct/log_list/v3/log_list.json`), `witness_url` (`""`), `poll` (10s) |
| `client` | the same three, plus `witness_pk` (`""`) |
| `config` | `log_list_url` (`https://www.gstatic.com/ct/log_list/v3/log_list.json`), `config_file` (`config.yaml`) |

Each exits 1 with a bare `--X must not be empty` when a required flag is left
at its empty default: `--private_key` and `--config_file` for `witness`,
`--witness_url` for `feeder`, `--witness_url` and `--witness_pk` for `client`.
`config` requires nothing.

`witness`'s `--private_key` carries **the PEM text of the signing key itself**,
not the name of a file holding it: the flag value is PEM-decoded in place and
the block parsed as PKCS#8. This is the opposite convention from
`gossip_server`'s `--log_public_keys` above, which names files. The key is an
ECDSA P-256 key and is what the witness co-signs with (CLIENT-API.md §6.3).

`feeder`, `client` and `config` resolve `--log_list_url` through a scheme table
that accepts `http`, `https` and **`file`**, so `file:///path/to/list.json` is
read straight off the filesystem with no network. What they parse it as moves:
at v1.1.3 `feeder` and `config` read it as the **v2** schema (`loglist2`) while
`client` already reads **v3**; from v1.1.4 all three read v3. Either way only
`operators[].logs[]` entries whose `state` union has `usable` set are followed,
and the fields used are `log_id`, `key` and `url` (CLIENT-API.md §4.2, §4.3).

---

## 5. Configuration files, by program

| program | flag | format |
| --- | --- | --- |
| `ct_server` | `--log_config` | text proto `LogConfigSet` or `LogMultiConfig`; binary proto also accepted from v1.1.2 (CONFIG.md) |
| `ct_hammer` | `--log_config` | text proto `LogConfigSet` (the help text says "JSON"; it is wrong) |
| `preloader` | `--target_temporal_log_cfg` | text proto `TemporalLogConfig`, binary fallback |
| `migrillian` | `--config` (**from v1.0.21 only** — at v1.0.19 migrillian has no config flag and is driven entirely by the flags of §4.7) | text proto `MigrillianConfig` (§4.7 gives its fields), binary fallback |
| `goshawk`, `gosmin` | `--config` | text proto |
| `witness` | `--config_file` | **YAML** — schema below |
| witness `config` | `--config_file` | **YAML** — the same schema, written rather than read |
| `feeder`, witness `client`, witness `config` | `--log_list_url` | CT log-list JSON over an `http`, `https` or `file` URL (§4.10) |
| `submission/server` | `--loglist_path` | CT log-list JSON (v2 schema, v3 from v1.1.4) |
| `ctclient`, `sctcheck`, `sctscan`, `findlog`, `ct_hammer` | `--log_list` | CT log-list JSON, file path or URL |

`certcheck`, `crlcheck`, `dumpscts`, `scanlog` and `submission/hammer` read no
configuration file. `feeder` and witness `client` have no configuration file of
their own; their only input document is the log list listed above.

### 5.1 The witness YAML (v1.1.3 .. v1.2.0)

`witness --config_file` is a YAML document with exactly one top-level key. The
keys are capitalised, because they are the Go field names with explicit
`yaml:"…"` tags of that spelling, and a lower-case `logs:`/`pub_key:` does not
bind:

    Logs:
        - PubKey: <standard-base64 of the log's PKIX SubjectPublicKeyInfo DER>
        - PubKey: <…one entry per log the witness follows…>

`PubKey` is the same base64 DER that a CT log list carries in its `key` field —
not PEM, and not a filename. A witness started with an empty list follows no
logs; the list is fixed for the life of the process.

**The indentation is the marshaller's, and the marshaller changes at v1.1.4.**
The `config` tool does not format this file itself; it hands the struct to the
YAML library and writes what comes back, so the block-sequence indentation is
whatever that library's default is:

| release | library | `Logs:` sequence indent | line 2 |
| --- | --- | --- | --- |
| v1.1.3 | `gopkg.in/yaml.v2` | **two** spaces | `  - PubKey: …` |
| v1.1.4, v1.2.0 | `gopkg.in/yaml.v3` | **four** spaces | `    - PubKey: …` |

The rendering above shows the v1.1.4 form. Nothing else about the file moves:
one key, one sequence, one `PubKey` per entry in log-list order, and a single
trailing newline. The file is compared **byte for byte**, so the indent is part
of the contract and re-indenting it by hand is a failure even though the YAML
parses to the same value.

Everything else about a log is derived from that one key. In particular the
**log ID** the witness uses in its URLs is

    logID = standard-base64( SHA-256( the PKIX DER decoded from PubKey ) )

which is the same value CT itself uses as a log's ID, and is why the witness's
router has to path-unescape the ID out of a URL (CLIENT-API.md §6.3).

---

---

## 6. Reading route

1. §2.1 for the flag table of the release you are on; §2.2 for what must be
   fatal.
2. CONFIG.md before implementing anything `--log_config` touches.
3. §3 only when you reach step 10, and then read both halves — the break is
   the milestone.
4. §1 to check which binaries must exist, and which must have stopped
   existing.
