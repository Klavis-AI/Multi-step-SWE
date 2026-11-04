# CONFIG — the log configuration protocol buffer

`--log_config` names a file containing a serialised `configpb` message. This
page gives the message definitions, the field-number-to-release map, and the
validation rules. Almost everything here is start-up behaviour, and start-up
behaviour is easy for a verifier to observe: it either exits 0 and serves, or
it exits 1 with a particular diagnostic.

---

## 1. The messages

Package `configpb`, proto3, import path
`github.com/google/certificate-transparency-go/trillian/ctfe/configpb`. It
imports `keyspb` (the backend project's key protos),
`google.protobuf.Any` and `google.protobuf.Timestamp`.

    message LogConfigSet {
      repeated LogConfig config = 1;
    }

    message LogBackend {                    // from v1.0.5
      string name         = 1;
      string backend_spec = 2;
    }

    message LogBackendSet {                 // from v1.0.5
      repeated LogBackend backend = 1;
    }

    message LogMultiConfig {                // from v1.0.5
      LogBackendSet backends   = 1;
      LogConfigSet  log_configs = 2;
    }

    message SignedTreeHead {                // from v1.1.0
      int64 tree_size           = 1;
      int64 timestamp           = 2;
      bytes sha256_root_hash    = 3;
      bytes tree_head_signature = 4;
    }

`LogConfig` is the interesting one. Field numbers are **stable** — a field
added later takes the next free number, never a recycled one, so the numbers
themselves record the history:

| # | field | type | from |
| --- | --- | --- | --- |
| 1 | `log_id` | int64 | v1.0 |
| 2 | `prefix` | string | v1.0 |
| 3 | `roots_pem_file` | repeated string | v1.0 |
| 4 | `private_key` | `google.protobuf.Any` | v1.0 |
| 5 | `public_key` | `keyspb.PublicKey` | v1.0 |
| 6 | `reject_expired` | bool | v1.0 |
| 7 | `ext_key_usages` | repeated string | v1.0 |
| 8 | `not_after_start` | `google.protobuf.Timestamp` | v1.0 |
| 9 | `not_after_limit` | `google.protobuf.Timestamp` | v1.0 |
| 10 | `accept_only_ca` | bool | v1.0 |
| 11 | `log_backend_name` | string | **v1.0.5** |
| 12 | `is_mirror` | bool | **v1.0.21** |
| 13 | `override_handler_prefix` | string | **v1.0.21** |
| 14 | `max_merge_delay_sec` | int32 | **v1.0.21** |
| 15 | `expected_merge_delay_sec` | int32 | **v1.0.21** |
| 16 | `frozen_sth` | `SignedTreeHead` | **v1.1.0** |
| 17 | `reject_unexpired` | bool | **v1.1.0** |
| 18 | `reject_extensions` | repeated string | **v1.1.0** |
| 19 | `is_readonly` | bool | **v1.1.3** |
| 20 | `ctfe_storage_connection_string` | string | **v1.2.0** |
| 21 | `extra_data_issuance_chain_storage_backend` | enum | **v1.2.0** |
| 22 | `logv3_url` | string | **v1.3.3** |

The enum at field 21, nested inside `LogConfig`:

    enum IssuanceChainStorageBackend {
      ISSUANCE_CHAIN_STORAGE_BACKEND_TRILLIAN_GRPC = 0;
      ISSUANCE_CHAIN_STORAGE_BACKEND_CTFE          = 1;
    }

### 1.1 Why the field set is itself a test

The file is parsed as **text proto**, and text-proto parsing rejects unknown
field names. So a configuration that names `is_readonly` must **fail to load**
at v1.1.2 and load cleanly at v1.1.3. Every "field arrives at release N" row
above is therefore directly observable: write the field, start the server,
look at the exit status. This is the main reason the configuration is a proto
and not hand-rolled JSON, and it is why you must not silently ignore fields
you do not recognise.

### 1.2 The generated Go names

Callers -- including the verifier -- reach these messages as ordinary Go
structs in package `configpb`, named by `protoc-gen-go`'s usual mapping: the
message name unchanged, and each field's snake_case name in CamelCase. So the
v1.0 `LogConfig` is

    type LogConfig struct {
        LogId         int64
        Prefix        string
        RootsPemFile  []string
        PrivateKey    *<any.Any>            // google.protobuf.Any
        PublicKey     *keyspb.PublicKey
        RejectExpired bool
        ExtKeyUsages  []string
        NotAfterStart *<timestamp.Timestamp>
        NotAfterLimit *<timestamp.Timestamp>
        AcceptOnlyCa  bool
    }

The two well-known-type fields are written here in angle brackets because the
Go spelling of the wrapper package depends on which protobuf runtime generated
the file (`google_protobuf.Any` under the v1.0-era generator, `anypb.Any`
under the modern one). Only the *message* type is fixed by this page; the
import alias is yours.

Note `LogId` and `AcceptOnlyCa`, which the mapping spells that way and not
`LogID` / `AcceptOnlyCA`. `LogConfigSet` has the single field `Config
[]*LogConfig`; from v1.0.5 `LogMultiConfig` has `Backends *LogBackendSet` and
`LogConfigs *LogConfigSet`, and `LogBackend` has `Name` and `BackendSpec`.
`keyspb.PublicKey` carries the DER in a field named `Der`. Later fields follow
the same rule (`IsMirror`, `OverrideHandlerPrefix`, `MaxMergeDelaySec`,
`FrozenSth`, `RejectUnexpired`, `RejectExtensions`, `IsReadonly`,
`CtfeStorageConnectionString`, `Logv3Url`).

**A generated message is a struct *and* its accessors.** Beside each field
`protoc-gen-go` emits the conventional accessor `Get<Field>()`, which returns
that field's value and the field's zero value on a nil receiver, and callers
reach a message that way as well as by field. The verifier does: drivers call
`GetBackends`, `GetBackend`, `GetLogConfigs`, `GetConfig`, `GetLogId`,
`GetPrefix`, `GetRootsPemFile`, `GetLogBackendName`, `GetName`,
`GetBackendSpec`, `GetPublicKey`, `GetIsMirror`, `GetOverrideHandlerPrefix`,
`GetMaxMergeDelaySec`, `GetExpectedMergeDelaySec`,
`GetCtfeStorageConnectionString` and — on the `LogConfig` of field 21 —
`GetExtraDataIssuanceChainStorageBackend`, which returns the generated enum
type and not its number. Each one returns the field of the same name, so a
message that is a struct of the right shape but carries no accessors is not
this package: the driver that calls one does not compile against it. Generating
the file rather than hand-writing it is the way to get both halves at once, and
the hand-written shape above is then exactly what the generator emits for the
fields.

---

## 2. Field semantics

### 2.1 Identity and routing

* `log_id` — the **backend's** tree identifier, an int64. Not the RFC 6962
  `LogID`, which is the SHA-256 of the log's DER `SubjectPublicKeyInfo` and is
  derived, never configured (ARCHITECTURE.md §2).
* `prefix` — the URL segment between the handler prefix and `/ct/v1/…`.
  Required, and unique across the whole configuration.
* `override_handler_prefix` (v1.0.21) — replaces `--handler_prefix` for this
  log. Normalised by trimming slashes from both ends and prepending one `/`,
  so `logs`, `/logs` and `/logs/` are the same thing.
* `log_backend_name` (v1.0.5) — names an entry in the `LogBackendSet`. Only
  meaningful in a `LogMultiConfig`.

### 2.2 Merge delay

`max_merge_delay_sec` and `expected_merge_delay_sec` (both v1.0.21) are
advertised parameters, not enforced ones. `max_merge_delay_sec` is what
`/log.v3.json` publishes as `mmd` at v1.3.3. Both must be non-negative and
`expected ≤ max`.

Advertised also means exported. From **v1.0.21** a running front end publishes
each configured log's two delays on `/metrics` as gauges, in seconds, labelled
by log ID:

| series | label | value |
| --- | --- | --- |
| `max_merge_delay` | `logid` | that log's `max_merge_delay_sec` |
| `expected_merge_delay` | `logid` | that log's `expected_merge_delay_sec` |
| `known_logs` | `logid` | `1` for every configured log |

`known_logs` predates this release; the two delay gauges arrive with the
config fields and all three are still exported at v1.3.3. They are set once
per log as the instance is built, so they are present as soon as the log is
being served, whether or not anything has been submitted to it.

### 2.3 Keys

`private_key` is a `google.protobuf.Any`. The concrete message inside it is
matched against a **registry of key handlers** that `ct_server` populates at
start-up: a PEM-file handler, a DER handler, and (from v1.1.2, gated on
`--pkcs11_module_path`) a PKCS#11 handler. The benchmark's configurations use
two of the three: most inline the key as a `keyspb.PrivateKey` carrying raw
DER, and some name a file with a `keyspb.PEMKeyFile`. Do not assume either
one.

`keyspb.PEMKeyFile` has two fields, `path` and `password`, and the file it
names may be **passphrase-encrypted** — several of the benchmark's
configurations use exactly that, with the passphrase in `password`. Nothing
about the decryption is yours to write: the registered PEM handler does it when
the message reaches it, which is the practical reason `private_key` must be
routed through the registry rather than opened by path. A `password` left empty
means the file is not encrypted; it does not mean the field is unused.

`public_key` is optional in general but **required** when `is_mirror` or
`frozen_sth` is set, because in those cases there may be no private key to
derive it from. From **v1.2.0** the two are cross-checked: if both are present
and the public key is an ECDSA, Ed25519 or RSA key, it must equal the signer's
public key, or start-up fails with `public key is not consistent with private
key`; a key type outside those three gives `failed to verify consistency of
public key with private key`.

Also from v1.2.0, `ct_server` refuses to start if **two logs share a private
key**: `Same private key used by more than one log`.

### 2.4 Admission policy

`roots_pem_file` is a list of PEM files; their concatenation is the log's root
pool, and it is also exactly what `get-roots` serves. At least one is
required — `need to specify RootsPemFile` — **except for a mirror**. From
v1.0.21 the requirement is conditioned on `is_mirror` being false (§2.5): a
mirror registers no write handlers, never admits a chain and therefore needs
no root pool, so a mirror config with no `roots_pem_file` at all is valid and
starts. The check is not part of `ValidateLogConfig` — it lives where the
files are actually read, as the instance is built — so a mirror config with
no roots passes the per-log validator either way (§4.1).

`ext_key_usages` is a list of EKU names. The names are matched against a fixed
table, identical at every release, which maps each spelling onto the
corresponding `x509.ExtKeyUsage` constant:

| name in the config | `x509.ExtKeyUsage` constant |
| --- | --- |
| `Any` | `ExtKeyUsageAny` |
| `ServerAuth` | `ExtKeyUsageServerAuth` |
| `ClientAuth` | `ExtKeyUsageClientAuth` |
| `CodeSigning` | `ExtKeyUsageCodeSigning` |
| `EmailProtection` | `ExtKeyUsageEmailProtection` |
| `IPSECEndSystem` | `ExtKeyUsageIPSECEndSystem` |
| `IPSECTunnel` | `ExtKeyUsageIPSECTunnel` |
| `IPSECUser` | `ExtKeyUsageIPSECUser` |
| `TimeStamping` | `ExtKeyUsageTimeStamping` |
| `OCSPSigning` | `ExtKeyUsageOCSPSigning` |
| `MicrosoftServerGatedCrypto` | `ExtKeyUsageMicrosoftServerGatedCrypto` |
| `NetscapeServerGatedCrypto` | `ExtKeyUsageNetscapeServerGatedCrypto` |

The match is exact and case-sensitive, and it is the whole table — there are
no aliases. Recognised names are appended to `ValidatedLogConfig.KeyUsages`
**in the order they appear in the config**, and an unrecognised name is a
start-up error, `unknown extended key usage: %s`, reported on the first one
that fails rather than accumulated. An empty list means "any". What
"any" *looks like* in the validated config changes at v1.1.2, and both forms
are observable on `ValidatedLogConfig.KeyUsages`:

| releases | empty list | explicit `Any` in the list |
| --- | --- | --- |
| v1.0.21 .. v1.1.0 | rewritten to the one-element slice `[ExtKeyUsageAny]` | just another entry, kept in the slice alongside the others |
| v1.1.2 .. v1.3.3 | left **empty** | short-circuits: the slice is cleared to nil, the rest of the list is not even read, and EKU checking is disabled outright |

From v1.1.2 an empty `KeyUsages` therefore means "do not filter", and it is
reached by either spelling. What a non-empty list *does* changes at the same
release — see HTTP-API.md §5.4.

`not_after_start` / `not_after_limit` define a **half-open** window
`[start, limit)` on the leaf's `NotAfter` — this is how temporal sharding is
configured. Both are optional and independent: either may be omitted, and an
omitted endpoint leaves the corresponding `*time.Time` on
`ValidatedLogConfig` **nil** rather than becoming the zero time. Each endpoint
that *is* present must be a valid `google.protobuf.Timestamp` — which means
`nanos` in `[0, 1e9)` and `seconds` inside the proto's stated range — or
start-up fails with `invalid start timestamp` / `invalid limit timestamp`.
The two endpoints are then compared only if both are present, and the limit
must not precede the start (`limit before start`); limit *equal* to start is
accepted by the validator, even though the window it describes is empty.

`accept_only_ca`, `reject_expired`, `reject_unexpired` and `reject_extensions`
are described with their runtime effect in HTTP-API.md §5. Two
configuration-level rules:

* From **v1.1.0**, `reject_expired && reject_unexpired` is a start-up error,
  `rejecting all certificates` — such a log could never accept anything.
* `reject_extensions` entries are dotted-decimal OIDs, parsed at start-up;
  a bad one is `failed to parse RejectExtensions: %v`.

### 2.5 Serving mode

| field | from | effect |
| --- | --- | --- |
| `is_mirror` | v1.0.21 | the log mirrors another; write handlers are not registered (404) and a private key is *forbidden* — `unnecessary private key for mirror` |
| `frozen_sth` | v1.1.0 | `get-sth` serves this STH forever; writes still work |
| `is_readonly` | v1.1.3 | write handlers not registered (404); the log keeps its key |

`frozen_sth` is validated hard at start-up: it must be signed by the log's own
key and verify under `public_key`. Failures are
`empty public key for frozen STH`, `failed to create signature verifier: %v`,
`invalid frozen STH: %v` and `signature verification failed: %v`. A frozen STH
you cannot verify is not served with a warning — the process refuses to start.

### 2.6 Issuance-chain storage (v1.2.0)

`extra_data_issuance_chain_storage_backend` selects where the certificate
chain that came with a submission is kept.

* `…TRILLIAN_GRPC` (default, value 0) — the chain is stored in the backend
  leaf's extra data, as it always was.
* `…CTFE` (value 1) — the backend leaf stores only a **hash** of the chain
  (TLS-CODEC.md §5.4: `CertificateChainHash` / `PrecertChainEntryHash`), and
  the chain itself goes into a database named by
  `ctfe_storage_connection_string`.

The critical property: **the wire format does not change.** `get-entries` and
`get-entry-and-proof` look the hash up, rehydrate the full chain, and emit a
byte-identical `extra_data`. The mode is invisible to clients and visible only
in what the backend holds.

Validation: choosing `CTFE` without a connection string is `missing
ctfe_storage_connection_string when issuance chain storage backend is CTFE`;
an unrecognised scheme is `unsupported driver in
ctfe_storage_connection_string`; a MySQL DSN that will not parse is `failed to
parse ctfe_storage_connection_string for mysql driver`. v1.3.3 adds a
PostgreSQL driver with the matching `…for postgresql pgx driver` message.

"Will not parse" is **not** a URL grammar. At **v1.2.0**, and unchanged
through **v1.3.3**, what decides it for a `mysql` connection string is the
pinned `github.com/go-sql-driver/mysql` driver's own DSN grammar — the parser
that package exports as `ParseDSN` — so a string it accepts must be accepted
here even where `net/url` would refuse it. The difference shows up in ordinary
strings: `mysql://ctfe:letmein@tcp(127.0.0.1:3306)/ctfe`, with or without a
`?parseTime=true&loc=UTC` query on the end, is a well-formed DSN and passes
this validation, while a URL parser rejects its `tcp(…)` address; and
`mysql://this is not a dsn` is malformed under both and is refused.

All of that belongs to the `CTFE` arm alone: on the `TRILLIAN_GRPC` arm
nothing is validated and nothing is copied, so the connection string is
absent from the validated configuration even if the configuration set one.
The backend selector itself is carried through on every arm, including a
value outside the enum.

A CTFE-backed store is fronted by a cache selected by `--cache_type`
(`noop` or `lru`), `--cache_size` and `--cache_ttl`. Invalid values are
`invalid cache_type flag`, `invalid cache_size flag`, `invalid cache_ttl
flag`.

The key a chain is stored under is the **SHA-256 of the DER (ASN.1) encoding
of the issuance chain**, that is, of the submitted certificates *after* the
leaf: the end-entity certificate is not part of what is hashed, because it is
already in the Merkle leaf. It is **not** the TLS encoding of a
`CertificateChain`. The same bytes that are hashed are the bytes stored, so
look-up is `FindByKey(sha256(der))` and rehydration decodes what comes back
with the same ASN.1 decoder. A submission with no issuer certificates still
hashes something — the DER encoding of an empty sequence — so the hash written
into the leaf is never empty on this path; separately, the leaf *encoding*
permits an empty `IssuanceChainHash`, and the rehydration path reads that as
"there is no chain to look up" and produces a zero-length chain rather than
treating the entry as malformed.

### 2.7 `logv3_url` (v1.3.3)

Published verbatim as the `url` field of `/log.v3.json` (HTTP-API.md §7.1).
Unset disables that endpoint with a 404.

---

## 3. Single-backend and multi-backend configurations

At **v1.0** the file is a `LogConfigSet` and there is exactly one backend, the
one named by `--log_rpc_server`.

From **v1.0.5** the file may instead be a `LogMultiConfig`, pairing a
`LogBackendSet` with the log configs; each log then selects its backend by
name through `log_backend_name`. The two forms are mutually exclusive with the
flag: supplying `--log_rpc_server` *and* a `LogMultiConfig` is a configuration
error, and so is a plain `LogConfigSet` with no `--log_rpc_server` (from
v1.1.0, where the flag's default became empty).

There is a conversion helper,
`ToMultiLogConfig(cfg []*configpb.LogConfig, beSpec string)`, which wraps a
flat **slice of log configs** — exactly what `LogConfigFromFile` returns, not a
`*configpb.LogConfigSet` — plus a backend spec into a `LogMultiConfig` with a
single synthesised backend **named `"default"`** — that is how the single-backend path
is implemented from v1.0.5 onwards. The synthesised backend carries the supplied
`beSpec` string, and every log in the converted config has its `log_backend_name`
set to `"default"`.

---

## 4. Parsing and validation

### 4.1 Entry points, and when each appeared

| function | from | note |
| --- | --- | --- |
| `LogConfigFromFile(filename) ([]*configpb.LogConfig, error)` | v1.0 | signature never changes |
| `MultiLogConfigFromFile(filename) (*configpb.LogMultiConfig, error)` | v1.0.5 | |
| `ToMultiLogConfig(cfg []*configpb.LogConfig, beSpec string) *configpb.LogMultiConfig` | v1.0.5 | takes the **slice** `LogConfigFromFile` returns, not a `*configpb.LogConfigSet`; signature never changes |
| `ValidateLogMultiConfig(cfg *configpb.LogMultiConfig) (map[string]*configpb.LogBackend, error)` | v1.0.5 | return type is spelled `LogBackendMap` from v1.0.21, declared there as a type **alias** (`type LogBackendMap = map[string]*configpb.LogBackend`), so it is the same type and not merely the same shape; the argument never changes |
| `ValidateLogConfig(cfg) (*ValidatedLogConfig, error)` | **v1.0.21** | the per-log validator; before this, per-log checks were inline in `SetUpInstance` |
| `ValidateLogConfigs(cfg []*configpb.LogConfig) error` | **v1.1.2** | the whole-set validator, split out |

`ValidatedLogConfig` is the pre-digested form handed to `SetUpInstance` — the
original config plus everything the validator had to parse in order to check
it:

    type ValidatedLogConfig struct {
        Config        *configpb.LogConfig
        PubKey        crypto.PublicKey    // the parsed public_key (§2.3)
        PrivKey       proto.Message       // ptypes.DynamicAny before v1.1.2
        KeyUsages     []x509.ExtKeyUsage  // §2.4
        NotAfterStart *time.Time
        NotAfterLimit *time.Time
        FrozenSTH     *ct.SignedTreeHead  // from v1.1.0
        // from v1.2.0, CTFEStorageConnectionString and
        // ExtraDataIssuanceChainStorageBackend (§2.6)
    }

The six fields that exist at v1.0.21 keep those names and those types all the
way to v1.3.3. `PubKey` is filled in for a mirror as well as for a signing
log — it is the whole reason a mirror is required to carry a `public_key`
(§2.5) when it has no private key to derive one from.

`Config` is the **same pointer** that was passed in, not a copy — the
validator wraps the caller's config rather than cloning it. `PrivKey` holds
the `private_key` `Any` **already unwrapped**: at v1.0.21 the field is a
`ptypes.DynamicAny` whose `Message` is the concrete key message the `Any`
carried, a `*keyspb.PrivateKey` for a DER key (§2.3). Both exist for the same
reason the function returns a struct at all — the results are handed on to
instance set-up so that nothing is parsed or validated twice, and a caller
that re-derives them from `Config` is repeating work the validator has
already done.

**The loaders parse; the validators validate.** `LogConfigFromFile` and
`MultiLogConfigFromFile` read the file, decode the message and check only that
the set is non-empty — the second one's check being "non-empty log
configurations *and* non-empty backends". Everything else a configuration can
get wrong falls to `ValidateLogConfig`, `ValidateLogConfigs` and
`ValidateLogMultiConfig` (§4.3, §4.4), each at the releases that have that
rule, and reaches `SetUpInstance` only through them. A configuration a
validator rejects at a given release is still a **parse success** there: the
loaders' contract is decoding, not adjudication.

### 4.2 Text proto, then binary proto

Through **v1.1.0** the file is parsed as text proto only; failure is `failed
to parse log config: %v` (or `failed to parse multi-backend log config: %v`).

From **v1.1.2** a file that does not parse as text proto is retried as
**binary proto**, and only if both fail is it an error — which is why the
message now reports both attempts:

    failed to parse LogConfigSet from %q as text protobuf (%v) or binary protobuf (%v)
    failed to parse LogMultiConfig from %q as text protobuf (%v) or binary protobuf (%v)

So from v1.1.2 the same `ct_server` accepts either encoding of the same
message, chosen by trial.

### 4.3 Per-log validation rules

Checked by `ValidateLogConfig` from v1.0.21; the same conditions existed
earlier, scattered through `SetUpInstance` — with one exception. The three
`not_after` rows below are the exception: from **v1.0.10** they are performed
inline by `ValidateLogMultiConfig` (§4.4), not by `SetUpInstance`, and v1.0.21
moves them here. The wording differs accordingly: v1.0.10 prefixes them
`log_config: ` and appends ` for: %v`.

| condition | message | from |
| --- | --- | --- |
| `log_id == 0` | `empty log ID` | v1.0.21 |
| public key present but unparseable | `invalid public key` (v1.1.2 onward it wraps the parse error) | v1.0.21 |
| mirror without a public key | `empty public key for mirror` | v1.0.21 |
| frozen STH without a public key | `empty public key for frozen STH` | **v1.1.0** |
| no private key on a non-mirror | `empty private key` | v1.0.21 |
| private key that will not load | `invalid private key` | v1.0.21 |
| private key on a mirror | `unnecessary private key for mirror` | v1.0.21 |
| both reject flags | `rejecting all certificates` | **v1.1.0** |
| unknown EKU name | `unknown extended key usage: %s` | v1.0 (inline), v1.0.21 (validator) |
| bad `not_after_start` | `invalid start timestamp` | v1.0.21 |
| bad `not_after_limit` | `invalid limit timestamp` | v1.0.21 |
| limit before start | `limit before start` | v1.0.21 |
| `max_merge_delay_sec < 0` | `negative maximum merge delay` | v1.0.21 |
| `expected_merge_delay_sec < 0` | `negative expected merge delay` | v1.0.21 |
| expected > max | `expected merge delay exceeds MMD` | v1.0.21 |
| frozen STH that will not verify | `failed to create signature verifier: %v`, `invalid frozen STH: %v`, `signature verification failed: %v` | **v1.1.0** |
| CTFE storage without a connection string | `missing ctfe_storage_connection_string when issuance chain storage backend is CTFE` | **v1.2.0** |
| unsupported storage driver | `unsupported driver in ctfe_storage_connection_string` | **v1.2.0** |
| a `mysql` connection string whose DSN will not parse | `failed to parse ctfe_storage_connection_string for mysql driver` | **v1.2.0** |

Before v1.0.21 the equivalent inline failures were `need to specify
RootsPemFile` and `need to specify PrivateKey`.

### 4.4 Whole-file validation rules

| condition | message | from |
| --- | --- | --- |
| no logs at all | `empty log config found` | v1.0 — in `LogConfigFromFile`, not in either validator |
| a multi-config missing either half | `config is missing backends and/or log configs` | v1.0.5 — in `MultiLogConfigFromFile`, not in either validator |
| backend with no name | `empty backend name` | rule v1.0.5; this wording **v1.0.10** (v1.0.5 says `empty name for backend`) |
| backend with no spec | `empty backend_spec for backend` | v1.0.5 |
| two backends with the same name | `duplicate backend name` | v1.0.5 |
| two backends with the same spec | `duplicate backend spec` | **v1.0.10** |
| a log with no prefix | `log config: empty prefix` | **v1.0.10** |
| two logs with the same prefix | `log config: duplicate prefix` | **v1.0.10** |
| a log naming a backend that is not defined | `log config: references undefined backend` | v1.0.5 |
| two logs on the same tree *of the same backend* | `log config: dup tree id` | **v1.0.10** |
| `not_after_start` that will not parse | `log_config: invalid start timestamp` | **v1.0.10** (moves to §4.3's `ValidateLogConfig` at v1.0.21, losing the prefix) |
| `not_after_limit` that will not parse | `log_config: invalid limit timestamp` | **v1.0.10** (likewise) |
| `not_after_limit` before `not_after_start` | `log_config: limit before start` | **v1.0.10** (likewise) |
| any per-log failure | `log config: %v: %v` | v1.0.21 — the wrapper around `ValidateLogConfig` |

Rows marked **v1.0.10** are that release's widening of `ValidateLogMultiConfig`:
v1.0.5's validator checks backend names, backend specs and that every log
names a defined backend, and nothing else about the logs.

The duplicate-tree-id rule arrives at **v1.0.10** — v1.0.5 does not check tree
ids at all — and it is **per backend from the start**, keyed on
`"<backend name>-<tree id>"`, so the *same* tree id on two *different*
backends is legal at every release that has the check. It has to be: tree ids
are allocated independently by each backend. The key is unchanged from
v1.0.10 through v1.3.3.

What **v1.1.2** adds is a *second*, separate validator for the single-backend
shape, `ValidateLogConfigs([]*configpb.LogConfig) error` (§4.1). It is handed
a bare `LogConfigSet`, so it has no backend names to key on, and its
duplicate-tree-id check is necessarily **global**: one tree id per set. The
two validators coexist; which one applies is decided by which shape of config
file was loaded, not by the release.

---

## 5. A minimal configuration

For orientation only; the fields are as described above.

    config {
      log_id: 1
      prefix: "athos"
      roots_pem_file: "/path/to/roots.pem"
      private_key: {
        [type.googleapis.com/keyspb.PEMKeyFile] {
          path: "/path/to/key.pem"
          password: ""
        }
      }
      max_merge_delay_sec: 86400
      expected_merge_delay_sec: 120
    }

(The last two fields only from v1.0.21.) A `LogMultiConfig` wraps the same
`config` blocks in `log_configs { … }` beside a `backends { backend { name:
"default" backend_spec: "localhost:8090" } }`.

---

## 6. Reading route

1. §1 for the message shapes and the field-number table.
2. §2 for whichever field you are about to implement.
3. §4.3 and §4.4 before writing any validation — the messages are the
   contract.
4. HTTP-API.md §5 for the runtime side of the admission fields.
5. CLI.md §2.2 for where validation sits in the start-up sequence.
