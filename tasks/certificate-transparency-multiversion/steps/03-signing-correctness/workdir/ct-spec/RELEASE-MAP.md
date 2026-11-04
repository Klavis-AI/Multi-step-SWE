# RELEASE-MAP — what changed at each anchor

Thirteen releases of certificate-transparency-go, from the first tagged Go
implementation to the current one. This page is the index by *release*; the
other pages are indexed by *subject*. When you need to know "what is different
about the version I am building", start here and follow the pointers.

A rule that holds throughout: **a release's behaviour is the behaviour of that
release**, not of the newest one. Where a page in this corpus says a field,
flag, method or status code arrives at vX.Y.Z, it is absent before that.
Writing tomorrow's behaviour today is as wrong as writing yesterday's.

---

## 0. The anchors

| # | release | date | one-line characterisation |
| ---: | --- | --- | --- |
| 1 | v1.0 | 2017-09-11 | the whole RFC 6962 stack, first cut |
| 2 | v1.0.5 | 2017-11-24 | one frontend, many backends |
| 3 | v1.0.10 | 2018-02-23 | signature caching made correct |
| 4 | v1.0.13 | 2018-03-21 | verification becomes a library |
| 5 | v1.0.17 | 2018-05-08 | proof verification leaves the product |
| 6 | v1.0.19 | 2018-05-24 | quota becomes explicit |
| 7 | v1.0.21 | 2018-08-17 | the non-compliant-key escape hatch is removed |
| 8 | v1.1.0 | 2019-11-14 | admission control inverted under unchanged names |
| 9 | v1.1.2 | 2021-09-21 | a flag's whole life in one step; surface withdrawals |
| 10 | v1.1.3 | 2022-05-13 | the client's command grammar rebuilt |
| 11 | v1.1.4 | 2022-10-18 | the v1 log list removed outright |
| 12 | v1.2.0 | 2024-05-28 | issuance chains get their own storage |
| 13 | v1.3.3 | 2026-02-26 | a second transport, a third storage, tiled logs |

Three of these are *subtractive* milestones — v1.0.17, v1.1.4 and, in part,
v1.1.2 and v1.3.3. Deleting a capability is as much a milestone as adding one,
and is graded the same way: by what is no longer reachable.

---

## 1. v1.0 — the RFC 6962 stack

Everything at once, because there is no previous release.

* `tls/` — the reflective TLS presentation-language codec (TLS-CODEC.md).
* `asn1/` and `x509/` — the forked parsers, `NonFatalErrors`, precertificate
  handling (X509-FORK.md).
* the root package — `MerkleTreeLeaf`, `TimestampedEntry`, `LogEntry`, the SCT
  and STH signature inputs, `SignatureVerifier`, the JSON request/response
  types and the nine endpoint paths (TLS-CODEC.md §5, HTTP-API.md §1).
* `merkletree/` — the product's own inclusion and consistency verifiers
  (MERKLE.md §4.1).
* `trillian/ctfe` — handlers, chain validation, SCT issuance,
  `SetUpInstance(ctx, client, cfg, deadline, mf)` (ARCHITECTURE.md,
  CLIENT-API.md §8.1).
* `ct_server` with ten flags of its own — eleven on the command line, see
  CLI.md §2.1 — and a `--log_config` holding a `LogConfigSet` of ten-field
  `LogConfig` messages, with **no** `LogMultiConfig` (CLI.md §2, CONFIG.md §1).
  The `--log_config` help text says "in JSON" at v1.0 and is simply wrong: the
  file is parsed as **text proto** at this release and through v1.1.0, and a
  JSON-encoded config is refused (CONFIG.md §4.2). `log_backend_name`, the
  eleventh field, arrives at v1.0.5.
* `client`, `jsonclient`, `scanner`, `preload`, `fixchain`, `x509util`
  (CLIENT-API.md §1, §5).

Read for this release: ARCHITECTURE.md end to end, then TLS-CODEC.md, then
HTTP-API.md §1–§4.

## 2. v1.0.5 — multiple backends

* `LogMultiConfig`, `LogBackend`, `LogBackendSet`; the single-backend file shape
  is *converted* into the multi shape and one validator sees both
  (CONFIG.md §3, §4).
* `logid/` package (CLIENT-API.md §5.5).
* `merkletree` grows `VerifyInclusionProofByHash` and
  `RootFromInclusionProofAndHash` (MERKLE.md §4.1) — they are **not** new at
  v1.0.13, and `ctutil`'s inclusion path at v1.0.13 does not call them.
* `SetUpInstance` takes an `InstanceOptions` (CLIENT-API.md §8.1–§8.2).
* `jsonclient`'s three request helpers start returning the raw body; the
  backoff is rewritten; `client.RspError` appears (CLIENT-API.md §1.1).
* structured `x509.Error` / `Errors` (X509-FORK.md §5.2).

Both config shapes must load, forever after. That is the milestone: not a new
format, a second legal format.

## 3. v1.0.10 — signing correctness

The advisory behind it is v1.0.7: a signature cache in the CTFE could return a
signature computed over one input for a different input. The milestone is that
**no two distinct signing inputs may share a cached signature**, and that the
cache is invisible in the output — a caller cannot tell from any response
whether a signature was cached or freshly computed.

Nothing on the wire changes: no request, response or signature format moves.
The property is about what may *not* happen. ARCHITECTURE.md §5 places the
signing step in the add-chain lifecycle, and CONFIG.md §4.4 has the
validation rows this release adds.

The release is not only that property, though, and the rest of it is graded:

* `trillian/ctfe` gains the per-log **STH signature cache** that carries the
  rule — the mechanism, not just the prohibition. It is keyed on the whole
  serialised tree head, so an unchanged tree head is re-served with the
  byte-identical signature and any change to it is signed afresh.
* `trillian/ctfe.RequestLog`, `DefaultRequestLog` and
  `InstanceOptions.RequestLog` — the request-logging hook interface
  (CLIENT-API.md §8.5).
* `x509`: `SerializedSCT`, `SignedCertificateTimestampList`, and the
  `Certificate.RawSCT` and `Certificate.SCTList` fields that hold a parsed
  certificate's embedded SCT list (X509-FORK.md §4.5).
* `x509util.CertificateToString` renders the poison and SCT-list extensions
  instead of dumping them as unknown OIDs (CLIENT-API.md §5.2).
* `ValidateLogMultiConfig` widens from four checks to eleven (CONFIG.md §4.4).
* `certcheck` gains `ignore_unknown_critical_exts`; `crlcheck` drops
  `verbose` and gains `strict` and `cert` (CLI.md §4.3).
* `ct_server` gains the `by_range` flag (CLI.md §2.1).

## 4. v1.0.13 — verification as a library

* `ctutil/` is born: `LeafHash`, `LeafHashB64`, `VerifySCT`, `ContainsSCT`,
  `LogInfo` and the `VerifyInclusion*` family (CLIENT-API.md §2).
* `loglist/` — the v1 log-list schema (CLIENT-API.md §4.1).
* `dnsclient/` — CT over DNS (CLIENT-API.md §6.1).
* `client.CheckLogClient` (CLIENT-API.md §1.3).
* `ct.MerkleTreeLeafForEmbeddedSCT` in the root package (CLIENT-API.md §3) —
  the precertificate case of the existing `MerkleTreeLeafFromChain`.

Note the ordering trap: `LeafHashForLeaf` in the root package is **not** here;
it arrives at v1.0.14 and so is first visible to you at v1.0.17.

## 5. v1.0.17 — outsourced proofs

Subtractive.

* `merkletree/` is **deleted**. Proof verification comes from the backend
  project's Merkle library instead (MERKLE.md §4.2). Every internal consumer is
  rewritten.
* the SQLite backend is removed.
* `LeafHashForLeaf` is available in the root package (CLIENT-API.md §3).
* `scanner/fetcher.go` factors the batch-walking loop out of the scanner
  (CLIENT-API.md §5.1).
* `jsonclient.JSONClient.BaseURI()` (CLIENT-API.md §1.1).
* `CheckLogClient` gains `BaseURI()` as its first method — a breaking interface
  change, at **this** release and not at v1.0.19 (CLIENT-API.md §1.3). From
  here on every implementation of the interface has four methods.
* `x509` stops treating a malformed `dnsName` or `rfc822Name` as a fatal parse
  error (X509-FORK.md §3.1); the `VerifyOptions` disable-switch set is
  completed and leaf EKU matching tightens with it (X509-FORK.md §5.6).
* `ct_server` goes behind a permissive CORS wrapper and narrows its catch-all
  to the exact path `/` (HTTP-API.md §7); the CTFE sets the §5.6 switches
  instead of hand-editing certificates (HTTP-API.md §5.1).
* `certcheck --validate` reads a multi-certificate file as a chain when
  neither pool flag is given (CLI.md §4.3); `ct_server` gains three tracing
  flags (CLI.md §2.1) and `scanlog` gains `--end_index` (CLI.md §4.1).
* `ctfe.LogEntry` and `ctfe.NewLogEntry` are withdrawn (CLIENT-API.md §8.3).

The graded consequence of a deletion is that the path stops resolving. A tree
that still contains `merkletree/` has not done the milestone.

## 6. v1.0.19 — quota and migration

* an explicit certificate-quota API in the CTFE, with `--quota_remote` and
  `--quota_intermediate` (CLI.md §2.1), a quota user derived from the request
  and one derived from each submitted intermediate, and both carried to the
  backend in `trillian.ChargeTo` (ARCHITECTURE.md §2.2). The exported
  `ctfe.QuotaUserForCert` and `ctfe.CertificateQuotaUserPrefix` are the
  certificate half (CLIENT-API.md §8.6); `InstanceOptions` gains
  `RemoteQuotaUser` and `CertificateQuotaUser` (CLIENT-API.md §8.2).
* `GetTreeHead` gains a `remoteQuota` parameter (CLIENT-API.md §8.3).
* `LogClient.GetEntryAndProof` (CLIENT-API.md §1.2).
* `(*ct.GetSTHResponse).ToSignedTreeHead()` — the wire-to-memory conversion of
  a `get-sth` response becomes a method on the response type, usable without a
  `LogClient` (CLIENT-API.md §3).
* `ctfe.RequestLog` gains an eleventh method, `IssueSCT`, called once when an
  SCT is about to be returned (CLIENT-API.md §8.5). Adding a method to an
  exported interface breaks every existing implementation.
* **`trillian/util` gains leaf construction**: `BuildLogLeaf` and
  `ExtraDataForChain` join the resolver and time-source plumbing already in
  that package, so the CTFE and migrillian build identical leaves
  (CLIENT-API.md §8.7). Neither name exists at v1.0.17.
* `scanner.FetcherOptions` swaps `Quiet` for `Continuous`, so the fetcher can
  follow a growing log; `scanlog` loses `--quiet` with it (CLIENT-API.md §5.1,
  CLI.md §4.1).
* `scanlog` and `ctclient` move their default log URIs to `https://`
  (CLI.md §4.1, §3.1).
* migrillian begins: ordering and resumption when copying a log into the
  backend. At this release it is flag-driven only — the config file arrives at
  v1.0.21 (CLI.md §4.7).

The error taxonomy gains a resource dimension: "I will not do this because you
have used your allowance" is now distinct from "I cannot do this".

## 7. v1.0.21 — key compliance and gossip

* `allow_verification_with_non_compliant_keys` is **removed**, not defaulted
  off (CLI.md §2). There is no longer a way to ask for the old behaviour.
* `gossip/minimal` with `goshawk`, `gosmin` and `x509ext` (CLIENT-API.md §6.2).
* a global URL path prefix plus a per-log path (CONFIG.md §2, HTTP-API.md §1).
* SIA and RFC 3779 RPKI support in `x509`/`x509util` (X509-FORK.md §5.3),
  and the insecure curve secp192r1 with it (X509-FORK.md §5.7).
* `x509.IsFatal`, the exported predicate for the fork's non-fatal parse
  contract (X509-FORK.md §3.1).
* `SetUpInstance` loses its explicit `client` and `cfg` parameters
  (CLIENT-API.md §8.1); `GetTreeHead` is replaced by `STHGetter`, with
  `PingTreeHead` taking over the periodic backend poll and
  `SetUpMirrorInstance` standing up a mirror — both of these at this release
  only (CLIENT-API.md §8.1, §8.3).
* the merge delays are exported on `/metrics` as well as configured
  (CONFIG.md §2.2).
* the `package main` count reaches 16.

## 8. v1.1.0 — admission inversion

The largest single release in the set.

* **`reject_expired` and `reject_unexpired` invert their meaning under
  unchanged names** (HTTP-API.md §5, CONFIG.md §2). The same config file now
  produces the opposite admission decision. Nothing in the file signals this.
* `reject_extensions`, `frozen_sth`, `/healthz`, `--mask_internal_errors`
  (CONFIG.md §2, HTTP-API.md §2, §7).
* `--metrics_endpoint` and `--log_rpc_server` lose their defaults (CLI.md §2).
* `ctpolicy/`, `submission/`, `schedule/`, `watcher/`, `loglist2/` are born
  (CLIENT-API.md §4.2, §7).
* the three named `asn1` laxnesses and `NonFatalErrors` returned from the
  `ParseCertificate` family (X509-FORK.md §2, §3).
* `SetUpInstance` returns an `*Instance`; `InstanceOptions` takes
  `Validated *ValidatedLogConfig`, `STHStorage`, `MaskInternalErrors`;
  `NewCertValidationOpts` appears (CLIENT-API.md §8).
* `jsonclient.RspError` with `client.RspError` as an alias; `Options.UserAgent`
  (CLIENT-API.md §1.1).
* `certcheck` flag renames (CLI.md §4).
* the `package main` count reaches 18.

Out of scope at this step and not graded: the `vendor/`-to-modules build
migration. The benchmark owns the module manifest at every step
(TEST_INTERFACE.md §2), so there is no migration for you to perform.

## 9. v1.1.2 — flag lifecycle and surface withdrawals

* `by_range` is defaulted true and then **deleted**, both inside this one step
  (CLI.md §2). Its whole life is one milestone.
* EKU filtering; get-entries coercion and `--align_getentries`
  (HTTP-API.md §4, CLI.md §2).
* the log config may be **binary-encoded protobuf**: text proto is tried first
  and binary is the fallback (CONFIG.md §4).
* a second config validator, `ValidateLogConfigs`, splits out the whole-set
  checks for a bare `LogConfigSet`; having no backend names to key on, its
  duplicate-tree-id check is **global**. The older multi-backend validator is
  unchanged and still keys them per backend (CONFIG.md §4.4).
* `ctutil.VerifySCTWithVerifier` (CLIENT-API.md §2).
* the Go 1.13/1.14 merge into the forked `x509` and `asn1`: Ed25519, DSA
  digest truncation, a nil-safe `Certificate.Equal`, `BMPString` — and one
  upstream change deliberately left out (X509-FORK.md §2.4, §5.5).
* `gossip/` collapses to `minimal/x509ext` only; `dnsclient/` is **deleted**
  along with the three DNS `LogInfo` constructors and the `--dns*` flags
  (CLIENT-API.md §6.1, §6.2).
* **the deprecated `google.golang.org/grpc/naming` resolver is withdrawn.**
  `trillian/util/resolver.go` and `resolver_test.go` — `FixedBackendResolver`
  and its `naming.Watcher` — are **deleted**, and `ct_server` moves its endpoint
  announcement to etcd's own v3 API, relocating the import path from
  `github.com/coreos/etcd/clientv3/naming` to `go.etcd.io/etcd/client/v3` and
  `go.etcd.io/etcd/client/v3/naming/endpoints` (ARCHITECTURE.md §2.1). This one
  is load-bearing: `grpc/naming` is absent from the pinned dependency set at
  this milestone, so a tree that keeps `resolver.go` fails to build
  `trillian/util` and with it everything that imports `trillian/ctfe`.
* the backend's queue RPC is renamed: `QueueLeaves` becomes **`QueueLeaf`**,
  and the `trillian` module pinned at this milestone declares only the new
  name — a front end still calling the old one does not build
  (ARCHITECTURE.md §2.1).
* `client.AddJSON` is **removed** (CLIENT-API.md §1.2).
* the log-list v2 URLs move from `v2beta` to `v2` (CLIENT-API.md §4.2).
* `merkle/logverifier` replaces `merkle` as the proof entry point
  (MERKLE.md §4.2).

## 10. v1.1.3 — client rebuild and witness

* `ctclient` is rebuilt as a Cobra-style command tree under
  `client/ctclient/cmd/`: subcommands with their own flag sets replace one flat
  `--get_sth`-style grammar (CLI.md §3).
* `ctfe.PEMCertPool` moves to `x509util.PEMCertPool` (CLIENT-API.md §5.3);
  `CertValidationOpts` and `loglist2`'s `LogRoots` retype to match.
* `loglist3/` and `internal/witness` are born (CLIENT-API.md §4.3, §6.3).
* `ingestor/` and `watcher/` are **deleted**.
* `HammerCTLog` takes a context.
* `PostAndParseWithRetry` starts backing off on 429 (CLIENT-API.md §1.1).
* read-only CTFE mode (HTTP-API.md §6).
* the proof library becomes `transparency-dev/merkle/proof` (MERKLE.md §4.2).

## 11. v1.1.4 — log-list v1 removal

* `loglist/` is **removed outright**. Its four dependants migrate from v1
  straight to v3, skipping v2 entirely (CLIENT-API.md §4.4). `findlog`, which
  was v1-only, is deleted.
* `tools/`.
* migrillian skips the consistency check when the root size is zero
  (MERKLE.md §3).
* glog is replaced by klog, which changes the logging flag set (CLI.md, the
  preamble on flag grammar).

## 12. v1.2.0 — issuance chain storage

* `IssuanceChainStorage`, the `cache/` package (LRU and no-op) and
  `services.go` (CLIENT-API.md §8.4).
* config gains `ctfe_storage_connection_string`,
  `extra_data_issuance_chain_storage_backend`, `cache_type`, `cache_size`,
  `cache_ttl` (CONFIG.md §1, §2).
* public/private key consistency is enforced and two logs may not share a
  private key (CONFIG.md §4).
* `loglist2/` is **removed** (CLIENT-API.md §4.4).
* `ResourceExhausted` from the backend maps to **429**, and a deadline maps to
  **504** (HTTP-API.md §6.1).
* the **client's** upload path gains a redirect guard: from **v1.2.0**
  `jsonclient.PostAndParse` fails a POST that a redirect turned into another
  method — 301, 302 and 303 all convert a POST into a GET — instead of reading
  the redirected reply as an accepted submission, while 307 and 308, which
  preserve the method, are still followed (CLIENT-API.md §1.1). Nothing on the
  server changes: the front end issues no redirects at any release from v1.0
  to v1.3.3, and never accepts a method as a no-op (HTTP-API.md §2).
* `submission.NewDistributor` becomes **variadic** — it gains a trailing
  `...DistributorOption`, and the `DistributorOption` interface and
  `DisableRootCompatibilityCheckingDistributorOption` arrive with it
  (CLIENT-API.md §7.2). This is a **v1.2.0** change, not a v1.3.3 one;
  v1.3.3 carries the same signature unchanged.

Both leaf layouts live at once: a log configured for the backend keeps the
whole chain in the leaf's extra data, a log configured for storage keeps a hash
and puts the chain elsewhere. Existing logs are unaffected.

## 13. v1.3.3 — transport and tiled logs

* `--tls_certificate`, `--tls_key`, `--trillian_tls_ca_cert_file`,
  `--http_idle_timeout` (CLI.md §2): a second transport scheme, both for the
  frontend's own listener and for its connection to the backend.
* PostgreSQL as a third issuance-chain storage backend (CONFIG.md §2).
* `TiledLogs` and the `FindTiledLog*` family in `loglist3`
  (CLIENT-API.md §4.3).
* etcd is removed from migrillian.
* mismatching signature-algorithm identifiers between a certificate's
  `signatureAlgorithm` and its `tbsCertificate.signature` are disallowed
  (X509-FORK.md §5.4).
* non-fresh submission rate limiting, three flags (CLI.md §2, HTTP-API.md §6).
* a short cache max-age on short `get-entries` responses (HTTP-API.md §4).
* a maximum request body size, answered with 413 (HTTP-API.md §6).
* `/log.v3.json` (HTTP-API.md §7).
* the preloader gains a continuous mode (CLI.md §4).
* `internal/witness` is **deleted** — the last withdrawal (CLIENT-API.md §6.3).
  The v1.3.3 manifest drops `github.com/gorilla/mux` and
  `github.com/mattn/go-sqlite3` along with it. Both are required at v1.1.3,
  v1.1.4 and v1.2.0 — exactly the releases at which `internal/witness`
  exists — and at v1.2.0 nothing outside that subtree imports either of them.
  Neither is in the v1.3.3 manifest, so at v1.3.3 a tree that still carries
  `internal/witness` does not build at all.
* `jsonclient.Options.Authorization` (CLIENT-API.md §1.1).
* the `package main` count falls to 14.

---

## 14. Structural facts you can check

These are the coarsest signals of "am I at the right release", and they are
verifiable from a directory listing alone.

| condition | true at |
| --- | --- |
| `merkletree/` exists | v1.0 .. v1.0.13 |
| `dnsclient/` exists | v1.0.13 .. v1.1.0 |
| `gossip/` beyond `minimal/x509ext` exists | v1.0 .. v1.1.0 |
| `ingestor/` exists | v1.0.5 .. v1.1.2 |
| `watcher/` exists | v1.1.0 .. v1.1.2 |
| `loglist/` exists | v1.0.13 .. v1.1.3 |
| `loglist2/` exists | v1.1.0 .. v1.1.4 |
| `loglist3/` exists | v1.1.3 .. v1.3.3 |
| `internal/witness` exists | v1.1.3 .. v1.2.0 |
| `ctutil/` exists | v1.0.13 .. v1.3.3 |
| `logid/` exists | v1.0.5 .. v1.3.3 |
| `ctpolicy/`, `submission/`, `schedule/` exist | v1.1.0 .. v1.3.3 |

`package main` directories in product scope, excluding the two build-ignored
files under `x509/`: **8** at v1.0, **13** at v1.0.13 and unchanged at
**v1.0.17**, **16** at v1.0.21, **18** at v1.1.0, **14** at v1.3.3
(CLI.md §1). v1.0.17 adds no program and removes none — its additions are
library-side.

---

## 15. Reading route

1. Find your release in §0, read its section.
2. Follow each pointer into the subject page; those pages state the behaviour,
   this page only states when it arrives.
3. Re-read §14 before you believe you are finished: the absences are as
   graded as the presences.
