# ARCHITECTURE — what this product is and where its boundaries are

`certificate-transparency-go` is a Go module whose import path is

    github.com/google/certificate-transparency-go

It is **not** a Certificate Transparency log. It is (a) a library stack that
implements the RFC 6962 data formats and their verification, and (b) a
*personality*: an HTTP front end, `ct_server`, that presents the RFC 6962
client API over a general-purpose Merkle-tree service which it does not
implement and does not own.

---

## 1. The two halves

### 1.1 The library stack

    github.com/google/certificate-transparency-go            root package, import name `ct`
      tls/                 the TLS presentation-language codec        (TLS-CODEC.md)
      asn1/, x509/         forked encoding/asn1 and crypto/x509       (X509-FORK.md)
      x509util/            helpers over the fork: PEM pools, printing, SIA, RPKI
      client/, jsonclient/ an RFC 6962 HTTP client                    (CLIENT-API.md §1)
      ctutil/              signature/inclusion verification helpers   (CLIENT-API.md §2)
      scanner/, fixchain/, preload/   batch tools over the client
      loglist/, loglist2/, loglist3/  parsers for the public log-list JSON
      logid/               the LogID type, from v1.0.5

The root package holds the RFC 6962 wire structures and the endpoint path
constants — the eight RFC paths and the non-RFC ones beside them, all listed
in HTTP-API.md §1. Everything that is hashed or signed is produced by `tls/`.

### 1.2 The personality

    trillian/
      ctfe/                the CT front end: handlers, chain validation, config
        configpb/          the log-configuration protocol buffer
        ct_server/         `package main` — the binary
      integration/         end-to-end drivers, `ct_hammer`
      migrillian/          from v1.0.19: copies an existing log into a backend
      util/                shared plumbing (backend resolution, time source);
                           from v1.0.19 also leaf construction — `BuildLogLeaf`
                           and `ExtraDataForChain` (CLIENT-API.md §8.7)

`ctfe` is a library; `ct_server` is fifty lines of flag parsing, a gRPC dial,
and a loop that registers each configured log's handlers on an
`http.ServeMux`. The interesting behaviour is all in `ctfe`, which is why the
verifier can exercise most of it without a server (CLIENT-API.md §8) and why
the few things that genuinely need a process — flag grammar, config
acceptance, endpoint routing, TLS transport — are exercised by running the
binary.

---

## 2. The backend boundary

`ct_server` speaks gRPC to a **Trillian log server** (`github.com/google/trillian`).
Trillian owns the Merkle tree, the leaf storage, the sequencing and the signing
of the tree head; the CTFE owns everything RFC 6962 says about *certificates*.

The whole of that boundary is the `trillian.TrillianLogClient` interface. At
v1.0 the CTFE uses exactly six methods:

| CTFE endpoint | Trillian RPC | what the CTFE adds |
| --- | --- | --- |
| `add-chain`, `add-pre-chain` | `QueueLeaves` | chain validation, `MerkleTreeLeaf` construction, SCT signing |
| `get-sth` | `GetLatestSignedLogRoot` | re-signs the root as an RFC 6962 `TreeHeadSignature` |
| `get-sth-consistency` | `GetConsistencyProof` | range parsing, base64 JSON shaping |
| `get-proof-by-hash` | `GetInclusionProofByHash` | leaf-hash parsing, proof shaping |
| `get-entries` | `GetLeavesByIndex`; from **v1.0.10** `GetLeavesByRange` is available too, selected per process by `--by_range` (CLI.md §2.1), which defaults to **false** through v1.0.21. From **v1.1.2** the flag is gone and `GetLeavesByRange` is the only path. | range clamping, leaf re-parsing, extra-data shaping |
| `get-entry-and-proof` | `GetEntryAndProof` | as above |

`get-roots` touches no backend at all: it serves the PEM pool loaded from the
log's configured `roots_pem_file` list.

Three things follow, and all three are graded:

1. **The log's identity is not the backend's.** Trillian signs its own tree
   head with its own key; the CTFE re-signs it under the *log's* key in the
   RFC 6962 `TreeHeadSignature` format. A `get-sth` response whose signature
   verifies under the backend's key is wrong.
2. **`log_id` is a Trillian tree id, `LogID` is a hash.** The `log_id` in the
   configuration is an `int64` naming a tree inside the backend. The RFC 6962
   `LogID` is the SHA-256 of the DER `SubjectPublicKeyInfo` of the log's public
   key. They are unrelated and must not be confused.
3. **The CTFE keeps no durable state, but it is not memoryless.** Every
   `get-sth` still asks the backend for the tree head; what is remembered is
   the signature over it. From **v1.0.10** each log holds a one-entry cache of
   the last signature it produced, keyed on the exact bytes that were signed,
   so a `get-sth` over an unchanged tree is answered with the *same* signature
   rather than a freshly generated one — this saves a signing operation and
   stops a non-deterministic signing method from handing successive callers
   distinguishable STHs. The cache is a private field of the log context at
   v1.0.10 and a named `SignatureCache` owned by the STH getter from
   **v1.0.21**, when `sth.go` arrives; the one-entry behaviour is the same
   through v1.3.3. From **v1.2.0** there is in addition an optional
   issuance-chain store. Restarting the front end discards both and loses
   nothing it cannot recompute.

> **In this benchmark no Trillian server is built and none is a deliverable.**
> Where a group needs a backend at all, the verifier supplies its own
> in-process `trillian.TrillianLogServer` over a real loopback gRPC socket,
> backed by a deterministic in-memory RFC 6962 tree; your code talks to it
> exactly as it would to a real backend, and you implement the client side, the
> leaf construction, the signing and the HTTP surface. Not every step stands
> one up — **this step's `TEST_INTERFACE.md` §3.4 says whether it does**, and
> that section, not this page, is the authority on what the backend boundary
> looks like here.

### 2.1 Backend addressing

`--log_rpc_server` is a *backend spec*: a comma-separated list of `host:port`,
or — when `--etcd_servers` is given — an etcd service name resolved through a
gRPC naming resolver. When the configuration resolves to exactly one backend
the connection is dialled **blocking**, so such a `ct_server` whose backend is
unreachable does not begin serving; that is observable behaviour and it is what
the "backend unreachable" checks look for. When it names two or more (from
v1.0.5, below) none of the dials blocks and the process serves at once — which
is how a verifier stands up a live HTTP surface with no backend behind it at
all, and why "unreachable backend" and "no listener" are only the same thing
for a single-backend configuration.

**How that resolution is implemented changes at v1.1.2, and the change is not
optional.** Through **v1.1.0** the mechanism is the deprecated
`google.golang.org/grpc/naming` package: `trillian/util/resolver.go` supplies
`FixedBackendResolver` (a `naming.Resolver` over a comma-separated list) and
`ct_server` announces its own endpoints through `github.com/coreos/etcd`'s
`clientv3/naming.GRPCResolver` using `naming.Update` values. From **v1.1.2**
`grpc/naming` is gone from the dependency set entirely, so none of that
compiles: `trillian/util/resolver.go` and `resolver_test.go` are **deleted**,
and `ct_server` resolves endpoints through etcd's own v3 API instead —
`go.etcd.io/etcd/client/v3` and `go.etcd.io/etcd/client/v3/naming/endpoints`,
which also relocates the etcd import path off `github.com/coreos/etcd`.
Nothing in `trillian/util` survives the deletion except `log_leaf.go` and
`timesource.go`. §8.7 of CLIENT-API.md describes that package as it stands
before this removal.

From **v1.0.5** a configuration may name *several* backends and assign each log
to one of them (CONFIG.md §3). The process then holds one gRPC connection per
backend and each log's handlers close over the connection for its own backend.

From **v1.3.3** `--trillian_tls_ca_cert_file` switches that dial from insecure
to TLS with the given CA roots.

### 2.2 Quota: who a backend call is charged to, from v1.0.19

Until **v1.0.19** the CTFE asked the backend to do work without saying on whose
behalf. From v1.0.19 it names the consumer, and that name crosses the boundary
in one place: every backend request message carries an optional

    ChargeTo *trillian.ChargeTo      // type ChargeTo struct { User []string }

and the CTFE fills it in. This is the whole of the quota surface on the
boundary — the front end *derives and passes identities*, and it is the backend
that decides whether anyone has run out. Nothing here refuses a request.

Two independent derivations exist, each switched on by its own `ct_server` flag
(CLI.md §2.1), each defaulting **on**:

* **The remote user**, from `--quota_remote`. A function of the HTTP request,
  installed as `InstanceOptions.RemoteQuotaUser` (CLIENT-API.md §8.2). The
  identity is the caller's **IP address as a bare string, with no port**,
  obtained by the rule the `github.com/tomasen/realip` package implements:
  if the request carries neither `X-Real-Ip` nor `X-Forwarded-For`, it is
  `RemoteAddr` with any `:port` stripped — so a loopback client is
  `127.0.0.1`, never `127.0.0.1:53124`; otherwise it is the first address in
  the comma-separated `X-Forwarded-For` list that is **not** in a private or
  link-local range (`127.0.0.0/8`, `10/8`, `172.16/12`, `192.168/16`,
  `169.254/16`, `::1/128`, `fc00::/7`, `fe80::/10`), and `X-Real-Ip` if no
  such address is found. A proxied request is therefore charged to the client
  in front of the proxy, not to the proxy.
* **The intermediate user**, from `--quota_intermediate`. A function of a
  certificate, installed as `InstanceOptions.CertificateQuotaUser`, so that one
  issuer flooding a log is rate-limited as an issuer. Its identity string is
  `ctfe.QuotaUserForCert` and its exact format is given in CLIENT-API.md §8.6;
  it is prefixed with the exported constant `ctfe.CertificateQuotaUserPrefix`,
  whose value is `@intermediate`, so that an issuer identity can never collide
  with an IP address.

**How the list is built.** `User` is a list because a single call can be
charged to several parties, and its order is part of the contract:

1. the remote user, if `--quota_remote` is on, is element 0 — always exactly
   one element, rebuilt per request, never accumulated across requests;
2. then, on `add-chain` and `add-pre-chain` only, if `--quota_intermediate` is
   on, one element per certificate **after the leaf** in the chain the front
   end validated — that is `chain[1:]` of what `ValidateChain` returned
   (CLIENT-API.md §8.3), in issuance order from the leaf upwards, so the trust
   anchor the path terminated at appears too, whether or not the submission
   carried it, and a chain that validates to a single certificate adds nothing.

If a derivation is switched off, its elements are simply absent and the rest of
the list keeps its order; if both are off, the request carries **no `ChargeTo`
at all** rather than an empty one.

**Reads are charged too.** The remote user is attached to every backend call
the CTFE makes — `get-sth`, `get-sth-consistency`, `get-proof-by-hash`,
`get-entries` and `get-entry-and-proof` alike, not only to submissions. Only
`get-roots` is uncharged, because it touches no backend at all. A request that
the CTFE itself refuses — a chain that does not verify, a parameter that does
not parse — never reaches the backend and so charges nobody.

**The library entry point takes a pointer, and the pointer is the switch.**
`ctfe.GetTreeHead`'s fifth parameter is `remoteQuota *string` (CLIENT-API.md
§8.3), and the test it applies is on the pointer, not on the string: `nil`
means "attach no `ChargeTo`", and **any** non-nil pointer means "charge this
user", including a pointer to the empty string, which produces a one-element
`User` list holding `""`. A caller with no user to name passes `nil`; passing
`&""` is a charge to an empty identity, not an absence of one.

None of this changes at any later release in this map: the field, the two
derivations, the ordering and the pointer rule are the same at v1.3.3 as at
v1.0.19.

---

## 3. The process and flag boundary

`ct_server` is configured by two disjoint mechanisms and the split is
load-bearing:

* **Flags** configure the *process*: where to listen, where the backend is, how
  long to wait, how much to allow, which optional subsystems to turn on. A flag
  applies to every log the process serves.
* **The log configuration file** configures the *logs*: one entry per log, each
  with its own key, its own roots, its own URL prefix and its own admission
  policy. Adding a log never needs a flag change.

`--log_config` names that file. `ctfe` reads and validates it **before** any
network activity, and a configuration that fails validation is fatal: the
process writes a diagnostic and exits non-zero without binding a port. That
property is what most configuration checks in this benchmark observe.

Flag inventory by release is in CLI.md §2. The shape to keep in mind:

* v1.0 — ten declared in `ct_server` itself, **eleven on the command
  line**: the root package contributes
  `--allow_verification_with_non_compliant_keys` to every program that links
  it. CLI.md §2.1 counts the command line, which is what `--help` shows.
* v1.0.17 adds tracing (3), v1.0.19 adds quota (2), v1.0.21 adds
  `--handler_prefix`.
* v1.1.0 removes the *defaults* of `--metrics_endpoint` and `--log_rpc_server`
  (they become empty) and adds `--mask_internal_errors`.
* v1.1.2 adds `--pkcs11_module_path`; v1.2.0 adds the three cache flags;
  v1.3.3 adds TLS, idle timeout, backend TLS CA and the three non-fresh
  submission flags.

Nothing is ever renamed. A flag that exists at release N and at release N+2
exists at N+1 with the same name and the same type.

---

## 4. What a "log config" is

A log configuration is a **protocol buffer message**, not a hand-rolled JSON
schema, and this matters because it decides how unknown fields behave.

    LogConfig        one log: identity, key, roots, admission policy, routing
    LogConfigSet     repeated LogConfig                       (the v1.0 shape)
    LogBackend       name + backend_spec                      (from v1.0.5)
    LogBackendSet    repeated LogBackend                      (from v1.0.5)
    LogMultiConfig   LogBackendSet + LogConfigSet             (from v1.0.5)

The file on disk is parsed as **text proto** (`prototext`), despite the v1.0
flag help string saying "JSON"; from v1.1.2 a file that does not parse as text
proto is retried as **binary proto**. Text-proto parsing rejects an unknown
field name outright, which is why removing a config field in a later release is
a breaking change a verifier can observe: a configuration written for release N
must *fail to load* at release N+1 if the field was removed.

Field-by-field semantics, the validation rules, and which field appears at
which release: **CONFIG.md**.

---

## 5. Request lifecycle: `add-chain`

Worth internalising once, because eight of the graded behaviours live on this
path.

1. `ct_server` routes `<prefix>/ct/v1/add-chain` to the handler registered for
   that log. A wrong HTTP method is `405` before anything else happens.
2. The body is JSON: `{"chain": ["<base64 DER>", …]}`. A body that is not JSON,
   or whose `chain` is empty, is `400`.
3. **Chain validation** (`cert_checker.go`): parse each element with the forked
   x509; apply the leaf-only admission tests — the expiry window, and from
   v1.1.0 `reject_extensions`; build a path from the submitted leaf to one of
   the log's configured roots; apply the remaining admission policy —
   `accept_only_ca` and `ext_key_usages`. Any failure is `400`.
   (`reject_extensions` examines **only the submitted leaf**; an OID carried
   solely by an issuer is not a refusal. HTTP-API.md §5.3 is authoritative on
   its scope.)
   Note that the chain is validated, *not* the certificate's trustworthiness in
   any browser sense: an expired certificate with a valid path is admissible
   unless the policy says otherwise, and a certificate carrying non-fatal parse
   errors is admissible (X509-FORK.md §3).
4. A `MerkleTreeLeaf` is built: `timestamp` from the instance's time source,
   `entry_type` from which endpoint was called, and for a precertificate the
   issuer key hash plus the poison-stripped TBS (X509-FORK.md §4.2, §4.3).
   Poison is what comes off **on this path**, because what was submitted is a
   poisoned precertificate. The verification path — `ctutil`, from v1.0.13 —
   runs the other way: a third party checking an SCT it found *inside an
   issued certificate* starts from a certificate that has no poison extension
   at all, and rebuilds the same leaf by removing the **SCT-list** extension
   instead (`x509.RemoveSCTList`, X509-FORK.md §4.2; CLIENT-API.md §2). Same
   leaf, opposite starting point, different extension — do not carry one
   sentence over to the other.
5. The leaf is TLS-encoded, hashed with the RFC 6962 leaf hash
   (`SHA-256(0x00 || leaf)`), and queued with `QueueLeaves`.
6. An SCT is produced by signing the `CertificateTimestamp` built from the same
   values, and returned as JSON (HTTP-API.md §3.1).

The **timestamp in the SCT and the timestamp in the leaf are the same value**,
and the `extensions` in both are empty. An implementation that stamps them
separately will produce an SCT whose signature does not correspond to the leaf
that was queued, and every inclusion check downstream will fail.

---

## 6. How the pieces are wired at start-up

    LogConfigFromFile / MultiLogConfigFromFile      parse the config (no validation)
        -> ValidateLogConfig / ValidateLogMultiConfig
    for each log:
        SetUpInstance(...)   -- release-dependent shape; see the table below
            -> load the private key through the registered key handlers
            -> load the roots PEM pool
            -> build CertValidationOpts from the config
            -> return this log's endpoint handlers
    register every returned path on the HTTP mux
    optionally start the metrics listener and the periodic internal get-sth

`PathHandlers` is `map[string]AppHandler` — a map, but a *named* map type, and
up to v1.0.21 the call returns a **pointer** to it together with an error; from
v1.1.0 the handlers hang off an `*Instance`. The sketch above carries an
ellipsis on purpose, because the call is re-spelled three times as arguments
fold into `InstanceOptions` — **write the row for the release you are on**:

| release | call | result |
| --- | --- | --- |
| v1.0 | `SetUpInstance(ctx, client, cfg, deadline, metricFactory)` | `(*PathHandlers, error)` |
| **v1.0.5** | `SetUpInstance(ctx, client, cfg, opts InstanceOptions)` — the deadline and the metric factory move into the options, and the error mapper joins them | `(*PathHandlers, error)` |
| **v1.0.21** | `SetUpInstance(ctx, opts InstanceOptions)` — the client folds in too; `SetUpMirrorInstance` sits beside it at this release only | `(*PathHandlers, error)` |
| **v1.1.0 .. v1.3.3** | `SetUpInstance(ctx, opts InstanceOptions)` — `is_mirror` is inspected in place of that separate entry point | `(*Instance, error)`, with the handlers on `Instance.Handlers` |

Write the return type as the corpus page gives it, not as a bare map. The exact
signatures, and what `InstanceOptions` holds at each release: CLIENT-API.md
§8.1 and §8.2.

Private keys are loaded through a **handler registry**: a `google.protobuf.Any`
in the config is matched against registered proto types. `ct_server` registers
the PEM-file, DER and PKCS#11 handlers at start-up. Two of those three are in
use across the verifier's steps — the inline `keyspb.PrivateKey` carrying raw
DER and the `keyspb.PEMKeyFile` naming a file — and a process that registers
only one of the two fails start-up on the other (CONFIG.md §2.3). **Which form
a given step's configurations use is that step's own business and is not
uniform across the thirteen:** that step's own `TEST_INTERFACE.md` names the
form its configurations carry, and that is the statement to trust; do not carry
an assumption from one step to the next.

---

## 7. Vocabulary map to the filesystem

| you are asked about | look in |
| --- | --- |
| a wire structure that is signed or hashed | root package + `tls/` |
| a certificate that parses in this product but not in Go's stdlib | `asn1/`, `x509/` |
| an HTTP status code the log returns | `trillian/ctfe/handlers.go` behaviour, HTTP-API.md |
| whether a submission is admissible | `trillian/ctfe/cert_checker.go` behaviour, HTTP-API.md §5 |
| a flag | `trillian/ctfe/ct_server/`, CLI.md §2 |
| a config field | `trillian/ctfe/configpb/`, CONFIG.md |
| proving an entry is in the tree | `merkletree/` (v1.0–v1.0.13), then the backend's library, MERKLE.md |
| checking someone else's SCT | `ctutil/`, CLIENT-API.md §2 |
