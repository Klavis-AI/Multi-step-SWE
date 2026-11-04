# HTTP-API — the client API the log serves

RFC 6962 §4 defines the eight endpoints, their parameters and their JSON
bodies. `rfc/rfc6962.txt` is normative for *what the fields mean*. This page is
normative for *what this product does*: the exact Go JSON field names, the
status code chosen for each failure, the headers, and the release at which each
behaviour arrived.

Everything here is observable over a socket, so almost all of it is graded.

---

## 1. Paths, methods and prefixes

The eight RFC 6962 paths, as constants in the root package, identical at every
anchor v1.0 .. v1.3.3:

| constant | path | method |
| --- | --- | --- |
| `AddChainPath` | `/ct/v1/add-chain` | POST |
| `AddPreChainPath` | `/ct/v1/add-pre-chain` | POST |
| `GetSTHPath` | `/ct/v1/get-sth` | GET |
| `GetSTHConsistencyPath` | `/ct/v1/get-sth-consistency` | GET |
| `GetProofByHashPath` | `/ct/v1/get-proof-by-hash` | GET |
| `GetEntriesPath` | `/ct/v1/get-entries` | GET |
| `GetRootsPath` | `/ct/v1/get-roots` | GET |
| `GetEntryAndProofPath` | `/ct/v1/get-entry-and-proof` | GET |

Two more path constants live beside them: `AddJSONPath = "/ct/v1/add-json"`
(marked *"Experimental addition"*, present at every anchor) and, from
**v1.3.3**, `LogV3JSONPath = "/log.v3.json"` (§7.1). There is also a parallel
set of bare-name constants of type `APIEndpoint` — `AddChainStr`,
`GetSTHStr` and so on — holding the same names *without* the `/ct/v1/` prefix,
used for metric labels and client dispatch.

### 1.1 How a path is assembled

    <handler prefix> + "/" + <log prefix> + <endpoint path>

`<log prefix>` is the per-log `prefix` config field. `<handler prefix>` is
`--handler_prefix` (from v1.0.21), overridden per log by the config field
`override_handler_prefix` (also v1.0.21), which is normalised by trimming
slashes and re-adding one leading slash. So with `--handler_prefix=/logs` and a
log whose prefix is `yyz`, `add-chain` lives at
`/logs/yyz/ct/v1/add-chain`.

The per-log `prefix` is itself **normalised**, at every release from v1.0 to
v1.3.3, and by the `trillian/ctfe` call that builds a log's handler map rather
than by `ct_server`: a prefix that does not begin with `/` gets one prepended,
and every trailing `/` is trimmed. `yyz`, `/yyz` and `/yyz/` are therefore
three spellings of the same log and all three serve
`/yyz/ct/v1/add-chain` under an empty handler prefix. The `"/"` in the line
above is the one that normalisation guarantees, not a second separator laid on
top of it: a prefix written with its own leading slash does not produce a
doubled one, and a trailing slash does not produce an empty path segment.

`/`, `/healthz` and `/metrics` are **never** prefixed.

---

## 2. The generic request wrapper

Every endpoint handler is wrapped by a common dispatcher, and that wrapper's
behaviour is stable from v1.0 to v1.3.3:

| condition | status | body |
| --- | --- | --- |
| request method ≠ the endpoint's method | **405** | `method not allowed: <METHOD>` |
| GET endpoint whose form fails to parse | **400** | `cannot parse form: …` |
| handler returns an error but a 200 status | **500** | `http handler misbehaved, status: %d` — respelled `… misbehaved, st: %d` from **v1.1.0** |
| success | **200** | JSON, `Content-Type: application/json` |

A PUT, DELETE or HEAD to any endpoint is **405**. There is no redirect
anywhere in this product, and no method is quietly accepted as a no-op.

A success body is **compact** JSON — no indentation, and no space after `:`
or `,` — the form Go's `json.Marshal` writes. The examples throughout this
page are pretty-printed for readability; the bytes on the wire are not.

Each request gets a context deadline of `--rpc_deadline` (default 10s) for its
backend call.

### 2.1 The error body format

Error bodies are produced by Go's `http.Error`, which appends a newline. The
body is

    <status text>\n<error>\n

for example `Bad Request\nfailed to parse add-chain body: …` — though from
**v1.1.0** that particular message, and only that one, carries the log's own
`<prefix>{<log_id>}: ` diagnostic identity in front of the error text, so the
body is `Bad Request\nbench{1001}: failed to parse add-chain body: …` for a log
with prefix `bench` and log id `1001`; see §3.1. From the same release the body
goes through a per-log method that honours `--mask_internal_errors`:

* flag off (default): unchanged, every status appends the error text;
* flag on: a **500** body is exactly `Internal Server Error` and the newline
  `http.Error` appends — `Internal Server Error\n`, byte for byte, with no
  error text behind it.
  400, 403, 404, 405, 429 and the rest still append their error.

Only 500 is ever masked. That asymmetry is the point of the flag: internal
errors may quote backend internals, client errors may not.

---

## 3. The write endpoints

### 3.1 `add-chain` and `add-pre-chain`

Request body:

    {"chain": ["<base64 DER>", "<base64 DER>", …]}

leaf first, then issuers. Response body, on 200:

    {
      "sct_version": 0,
      "id":          "<base64 32-byte LogID>",
      "timestamp":   1512345678901,
      "extensions":  "",
      "signature":   "<base64 DigitallySigned>"
    }

Field names are exactly `sct_version`, `id`, `timestamp`, `extensions`,
`signature` and never change. `sct_version` is the numeric enum value, so `0`
for V1, not the string `"v1"`. `extensions` is a **string** (base64 of the
extension bytes) and is `""` in every SCT this log issues. `signature` is the
base64 of the TLS-encoded `DigitallySigned` (TLS-CODEC.md §1), not of the
raw signature bytes.

Failure statuses:

| condition | status | message |
| --- | --- | --- |
| body is not the expected JSON, or `chain` is empty | 400 | `failed to parse add-chain body: …`, prefixed — see below |
| chain validation fails for any reason | 400 | `failed to verify add-chain contents: …` |
| the leaf cannot be assembled | 400 | `failed to build MerkleTreeLeaf: …` |
| the backend rejects the queue | mapped, §2.2 / §6.1 | |
| the backend's response is absent or has a length other than 1 | 500 | |
| SCT generation or the response write fails | 500 | |

Two details of that first row will trip a byte-exact comparison.

**The prefix.** From **v1.1.0** that one message — and only that one — is
prefixed with the log's own diagnostic identity: the per-log `prefix` config
field and the numeric `log_id`, rendered `<prefix>{<log_id>}`, followed by
`: `. So an empty `chain` submitted to a log with prefix `bench` and log id
`1001` produces exactly

    Bad Request
    bench{1001}: failed to parse add-chain body: cert chain was empty

Up to **v1.0.21** the same body carries no prefix. This identity is a
*diagnostic* one and is unrelated to §1.1's URL assembly, where `<log prefix>`
is a path component. Exactly one other client-visible error body in this
document carries the prefix, and only from **v1.3.3**: the **413** of §6.3,
which is raised on this same body-reading path before parsing begins. Nothing
else is prefixed, at any release — in particular the `failed to verify
add-chain contents:` row is not.

**The empty-chain text.** A `chain` array that is present but empty is
`cert chain was empty`, at every release from v1.0 to v1.3.3. The check lives
in body parsing, so it is reported on this row and never reaches §5.

**There is no distinct status for a rejected certificate.** Every admission
failure in §5 is a 400 carrying `failed to verify add-chain contents:`. Do not
invent 403 for policy rejections.

### 3.2 The duplicate-submission rule

Submitting the same chain twice returns **200 both times** with the *same*
SCT — the backend deduplicates by leaf hash and returns the stored leaf, and
the CTFE reconstructs the original SCT from it rather than issuing a new one
with a fresh timestamp. An implementation that mints a second SCT with a later
timestamp is wrong and is trivially detectable.

"The same SCT" means the same `sct_version`, the same `id`, the same
`timestamp` and the same `extensions` — the four fields that are a function of
the stored leaf. It does **not** mean byte-identical `signature`: SCT signing
has no cache (only the STH signature is cached, RELEASE-MAP.md §3) and the
corpus log key is ECDSA, whose signatures are randomised, so the second
response may and usually will carry different signature bytes over the same
`TimestampedEntry`. Both signatures verify against the same entry, which is the
property that matters; the timestamp is the field that gives a second minting
away.

The leaf identity the backend deduplicates on is the front end's, not the
backend's invention: `LeafIdentityHash` is `SHA-256` over the submitted leaf
certificate's DER (CLIENT-API.md §8.7), so it is stable across submissions even
though `LeafValue` is rebuilt with a fresh timestamp each time.

---

## 4. The read endpoints

### 4.1 `get-sth`

No parameters. Response:

    {
      "tree_size":           <uint64>,
      "timestamp":           <uint64 ms>,
      "sha256_root_hash":    "<base64 32 bytes>",
      "tree_head_signature": "<base64 DigitallySigned>"
    }

The signature is over the TLS encoding of a `TreeHeadSignature`
(TLS-CODEC.md §5.3) under the **log's** key, not the backend's
(ARCHITECTURE.md §2). Note the wire response has no `log_id`; the
`SignedTreeHead` *Go type* does carry `sth_version` and `log_id` fields, but
those belong to the client-side representation, not to this response body.

From **v1.0.21** the STH is not fetched from the backend on every request:
an `STHGetter` abstraction is introduced with a live implementation, a mirror
implementation, and — from **v1.1.0** — a frozen implementation that serves
one constant STH from the `frozen_sth` config field forever.

### 4.2 `get-sth-consistency`

Parameters `first` and `second`, both required, both decimal, both ≥ 0, with
`first ≤ second`. Response `{"consistency": ["<base64>", …]}`.

**`first=0` is answered without the backend.** A consistency proof from tree
size 0 is empty by definition, so once the range has parsed, a `first` of 0
short-circuits: the front end replies **200** with `{"consistency": []}` and
issues no proof RPC at all. Both parameters are still required and still
validated first, so a missing or malformed `second` is the 400 below whatever
`first` says. This holds from v1.0 to v1.3.3 and is the only proof endpoint
with such a case; MERKLE.md §5 says the same from the Merkle side.

Parameter errors are **400** with `failed to parse consistency range: …`
wrapping one of: `parameter 'first' is required`, `parameter 'second' is
required`, `parameter 'first' is malformed`, `parameter 'second' is malformed`, `first and second
params cannot be <0`, `invalid first, second params: %d %d`. The last one
carries the two values it rejected, space-separated, so `first=5&second=2`
produces the whole body

    Bad Request
    failed to parse consistency range: invalid first, second params: 5 2

Those texts are the front end's own, but they are not all as old as the
endpoint. At **v1.0** only the two range checks exist — `first and second params
cannot be <0` and `invalid first, second params` — and an absent or unparseable
parameter falls through to `strconv`, so the client is handed the Go parser's
message, which names neither the parameter nor which of the two conditions it
hit. The four parameter-specific texts arrive at **v1.0.5** and are unchanged
from there to v1.3.3. Both range checks append the two offending values to their
text, so the first is `first and second params cannot be <0: -1 2` just as the
second is `invalid first, second params: 5 2`.

**Present-but-empty is *required*, not *malformed*.** From **v1.0.5** to
v1.3.3 the handler tests each parameter for emptiness before it tries to parse
it, and `r.FormValue` returns the empty string both for a parameter that is
absent and for one written with no value. So `?first=&second=2` is
`parameter 'first' is required`, exactly as `?second=2` is, and
`?first=1&second=` is `parameter 'second' is required`; neither is ever
reported as malformed. Only a parameter that is present **and** non-empty
**and** unparseable gives a `… is malformed` text. At **v1.0** the question
does not arise: there are no parameter-specific texts there at all and both
cases fall through to `strconv`.

From **v1.0.13**, asking for a proof against a tree the log has not reached is
**400** `need tree size: %d for proof but only got: %d` rather than whatever
the backend error happened to map to. A proof the backend returns that does not
decode is **500** `backend returned invalid proof`.

### 4.3 `get-entries`

Parameters `start` and `end`, inclusive, both required, both ≥ 0, `start ≤
end`. Response:

    {"entries": [{"leaf_input": "<base64>", "extra_data": "<base64>"}, …]}

`leaf_input` is the TLS-encoded `MerkleTreeLeaf`. `extra_data` is the TLS
encoding of a `CertificateChain` (for an X.509 entry) or a
`PrecertChainEntry` (for a precert entry) — see TLS-CODEC.md §5.4.

Rules, in order:

1. A missing or malformed parameter, a negative value, or `start > end` is
   **400** `bad range on get-entries request: …`, wrapping the reason. The two
   reasons the front end writes itself are
   `start (%d) and end (%d) parameters must be >= 0` and
   `start (%d) and end (%d) is not a valid range`, both with the submitted
   values substituted, so `start=3&end=1` produces the whole body

       Bad Request
       bad range on get-entries request: start (3) and end (1) is not a valid range

   A parameter that is absent or not a number is wrapped the same way, but the
   wrapped text is then whatever the standard library's integer conversion
   produced and is not part of this contract. Both front-end texts are
   unchanged from v1.0 to v1.3.3.
2. A range wider than the maximum is **coerced, not rejected**: `end` becomes
   `start + max − 1`. The maximum is 1000, overridable by
   `--max_get_entries`. This has been the behaviour since v1.0.
3. From **v1.1.2**, with `--align_getentries` (default **true**), a
   *maximally sized* range is then truncated down to the next multiple of the
   maximum — `end -= (end+1) % max` — so that large sequential fetches land on
   cacheable boundaries. "Maximally sized" is decided on the span the client
   asked for, `end − start + 1`, **before** rule 2's coercion has been
   applied, and the test is `≥ max`: a request for fewer entries than the
   maximum is answered exactly as it was asked, is not truncated, and is not
   counted. The handler reports what it did through a counter exported at
   `/metrics`, `aligned_get_entries`, with labels `logid` — the log's own
   `log_id`, in decimal — and `aligned`, incremented once per request that
   reaches the truncation step; `aligned` is `true` when the range already
   ended on a boundary and nothing had to be trimmed, `false` when it was
   trimmed. Both
   the coercion and the counter happen in the handler's own range parsing,
   **before** any backend call, so they are observable with no backend at all.
   With `--align_getentries=false` the truncation does not happen and the
   coercion of rule 2 is all that applies.
4. If the tree is too small to contain `start` at all, the answer is **400**
   `need tree size: %d to get leaves but only got: %d` (from **v1.0.13**;
   before that it came out of the generic backend mapping as a 500).
5. An `end` past the end of the tree with `start` inside it is **not** an
   error. The response is simply shorter than asked for.

Which backend RPC is used changes: index-based at v1.0, selectable by a
`--by_range` flag from **v1.0.10**, and range-based unconditionally from
**v1.1.2** when that flag is deleted. Sanity failures on the returned leaves
are 500 (`backend returned too many leaves`, `backend returned unexpected leaf
index`).

### 4.4 `get-proof-by-hash`

Parameters `hash` (base64 of the RFC 6962 leaf hash — MERKLE.md §1 — required,
non-empty, valid base64) and `tree_size` (required, decimal, ≥ 1). Response
`{"leaf_index": <int64>, "audit_path": ["<base64>", …]}`.

Parameter failures are 400, and this endpoint is the one that names itself
twice — the handler prefixes its own reason with `get-proof-by-hash: `, and two
of the three reasons repeat the endpoint name inside it. The three, unchanged
from v1.0 to v1.3.3:

| condition | body after the status line |
| --- | --- |
| `hash` absent or empty | `get-proof-by-hash: missing / empty hash param for get-proof-by-hash` |
| `hash` present but not valid base64 | `get-proof-by-hash: invalid base64 hash: …` |
| `tree_size` absent, non-numeric or < 1 | `get-proof-by-hash: missing or invalid tree_size: <the parameter as supplied>` |

The middle one wraps the standard library's own base64 complaint and only the
prefix is the front end's. The last one echoes the raw form value, so a
`tree_size=0` gives `…missing or invalid tree_size: 0` and an absent one gives
a trailing empty string.

Two failures are **404**, both from v1.0.13:

* `log returned tree size: %d but we expected: %d`
* `get-proof-by-hash: backend did not return a proof`

The second was a **500** at v1.0 .. v1.0.10 and became a 404 at v1.0.13; "that
leaf is not in that tree" is a client-visible not-found, not a server fault.

### 4.5 `get-entry-and-proof`

Parameters `leaf_index` and `tree_size`. Response
`{"leaf_input": …, "extra_data": …, "audit_path": [...]}`.

Parameter errors are 400 `failed to parse get-entry-and-proof params: …`,
wrapping `tree_size must be > 0`, `leaf_index must be >= 0` or
`leaf_index %d out of range for tree of size %d` — or, when a parameter is
absent or not a number, the standard library's integer conversion error, which
is not part of this contract. From v1.0.13 an
under-sized tree is 400 `need tree size: %d for proof but only got: %d`.

### 4.6 `get-roots`

No parameters, no backend call. Response
`{"certificates": ["<base64 DER>", …]}` — the PEM pool loaded from the log's
`roots_pem_file` list, re-encoded as base64 DER. The only failure is **500**
`get-roots failed with: %v`.

### 4.7 Cache-Control

| release | behaviour |
| --- | --- |
| v1.0 .. v1.1.0 | no `Cache-Control` header on any endpoint |
| **v1.1.2** | `Cache-Control: public, max-age=86400` on the four "immutable answer" endpoints: `get-sth-consistency`, `get-proof-by-hash`, `get-entries`, `get-entry-and-proof` |
| **v1.3.3** | `get-entries` alone becomes conditional: a short response (fewer leaves than asked for, i.e. the tail of the tree) gets `public, max-age=60`; a full response keeps `max-age=86400`. The other three are unchanged. |

`get-sth` and `get-roots` never set `Cache-Control`.

---

## 5. Admission control

All of this lives on the `add-chain` / `add-pre-chain` path and all of it
surfaces as **400**.

Every refusal raised by chain validation — items 1 to 6 of §5.1, and the
expiry, extension and EKU tests of §5.2 to §5.4 — reaches the client wrapped
**twice**. The validation layer wraps its own reason in `chain failed to
verify: `, and the handler wraps that in the §3.1 message. A leaf refused by
`reject_expired` therefore produces exactly

    Bad Request
    failed to verify add-chain contents: chain failed to verify: rejecting expired certificate

with no log prefix. Both wrappers are present at every release from v1.0 to
v1.3.3. Item 7 is the one exception: the poison test runs after chain
validation has already succeeded, so its reason is wrapped in `precert test
failed: ` instead, under the same §3.1 outer message.

**The inner wrapper's shape changes at v1.0.21.** The example above is the
v1.0.21-and-later form, `chain failed to verify: <reason>`. Up to **v1.0.19**
the same wrapper also echoes the request it rejected, and joins the two with
the word `because`:

    chain failed to verify: <the AddChainRequest, rendered by %v> because: <reason>

The echoed request is the decoded `ct.AddChainRequest` — a slice of slices of
bytes printed as decimal numbers — so the body is long, its length depends on
the chain submitted, and nothing but its two fixed ends is worth comparing. The
`<reason>` is the last thing in the body either way, and the trailing ` because: `
is what tells the two eras apart. A group grading this at v1.0 .. v1.0.19
therefore matches on the leading `failed to verify add-chain contents: chain
failed to verify: ` and on the reason at the end, never on the whole body.

One reason in particular is **not** the front end's to write: when no path to a
configured root exists because the submitted chain's issuer is not trusted, the
reason is whatever the forked `x509` verifier returned — at every release the
standard `x509: certificate signed by unknown authority`. §5.1 item 3's
`no path to root found when trying to validate chains` is the *other* arm of
the same test, reached when verification succeeds but returns no chain at all.

### 5.1 What is checked, at every release

1. Every element parses with the forked x509. A **fatal** parse error is
   returned as it stands, with no wrapping of its own beyond the two layers
   above — the forked parser's own error text is what the client sees. A
   **non-fatal** one is tolerated and the certificate is admitted
   (X509-FORK.md §3).
2. The chain is non-empty. This one is caught earlier, in body parsing, so it
   surfaces as the §3.1 message `cert chain was empty` and not as a §5
   refusal.
3. A path exists from the submitted leaf to one of the configured roots
   (`no path to root found when trying to validate chains`), and the submitted
   chain is in RFC order (`no RFC compliant path to root found when trying to
   validate chain`).
4. Temporal shard window, if configured: `certificate NotAfter (%v) < %v` and
   `certificate NotAfter (%v) >= %v` for `not_after_start` / `not_after_limit`.
   The window is half-open: `[start, limit)`.
5. `accept_only_ca`: `only certificates with CA bit set are accepted`.
6. `ext_key_usages`, translated to allowed leaf EKUs. Empty means "any".
   How the list is applied changes twice, at v1.0.17 and at v1.1.2 — §5.4.
7. For `add-pre-chain`, the poison extension must be present, critical and
   well-formed: `CT poison ext is not critical or invalid: %v`. The reverse
   mismatch is refused the same way, at every release: a chain submitted to
   `add-chain` whose leaf carries the poison extension is a 400 on the §3.1
   `failed to verify add-chain contents:` row, and a plain certificate
   submitted to `add-pre-chain` is refused alongside it.

Path building always disables the checks a browser would apply and a log must
not. A log records what was issued; it does not adjudicate it. *Which* checks
are off is release-dependent, and it is the switch set of X509-FORK.md §5 that
says so:

* **up to v1.0.13** there are no switches beyond `DisableTimeChecks`, so the
  CTFE gets the same effect by hand — it blanks the leaf's
  `UnhandledCriticalExtensions`, strips the `PolicyConstraints` OID out of each
  intermediate's, lifts the pre-issuer's CT EKU out of `chain[1]` and bumps
  each `MaxPathLen` above it, then puts all of that back afterwards;
* **from v1.0.17** the hand-editing is deleted and the switches do the work:
  `DisableCriticalExtensionChecks`, `DisableEKUChecks`, `DisablePathLenChecks`
  and `DisableNameConstraintChecks` are all set, `DisableNameChecks` stays
  **false**, and `DisableTimeChecks` remains `!reject_expired`. Critical
  extensions, EKUs, path lengths and name constraints are therefore all
  unchecked; issuer/subject chaining and signatures still are.

### 5.2 Expiry: the change at v1.1.0

This is the single most confusable behaviour in the product.

**Up to v1.0.21** there is one knob, `reject_expired`, and it is not really an
admission rule at all: it is wired straight into path building as "do not
disable time checks". The consequence is that it applies to the **whole
path** — an expired intermediate rejects the submission just as an expired
leaf does — and there is no way to ask for the opposite.

**From v1.1.0** there are two knobs and an explicit, leaf-only test that runs
*before* path building:

* `reject_expired` — reject if the leaf's `NotAfter` is in the past.
  `rejecting expired certificate`.
* `reject_unexpired` — reject if it is **not** in the past, i.e. reject
  currently-valid and not-yet-valid certificates. `rejecting unexpired
  certificate`.

Path building then always runs with time checks disabled and an explicit
current time. So from v1.1.0 an expired *intermediate* no longer rejects the
chain; only the leaf's own expiry is consulted. Setting both flags is a
start-up error, `rejecting all certificates`, because it admits nothing.

The current time this test compares against is the one carried in the
validation options — `NewCertValidationOpts`'s `currentTime` parameter
(CLIENT-API.md §8.3). When that value is the zero `time.Time`, and only then,
the wall clock is read at request time instead. Nothing in the configuration
file pins it: `SetUpInstance` builds the options without a `currentTime`, so a
running `ct_server` always takes the wall clock here. The injected value exists
so that admission can be decided — and graded — without a clock.

### 5.3 `reject_extensions` (from v1.1.0)

A list of dotted-decimal OIDs. A certificate carrying any of them is rejected:
`rejecting certificate containing extension %v at index %d`. `%v` is the
offending OID in dotted-decimal form; `%d` is its index into *that
certificate's own* `Extensions` list, counted from zero — not an index into
the submitted chain and not an index into `reject_extensions`. Unparseable
OIDs in the config are a start-up failure, `failed to parse
RejectExtensions: %v`.

**Scope: the submitted leaf, and nothing else.** Like §5.2's expiry test and
§5.4's EKU test, this rule examines `chain[0]` — the first certificate in the
`add-chain` or `add-pre-chain` body — and no other element of the submission.
An OID carried *only* by an intermediate or by a root is **not** a refusal,
even though those issuers are part of the path the log goes on to build. So
"a certificate carrying any of them" above means the submitted leaf; the
"*that certificate's own*" in the `%d` disambiguation is there because the
same wording has to serve `add-chain` and `add-pre-chain`, not because more
than one certificate is examined.

The test also runs **before** path building, together with §5.2's and §5.4's,
so a refusal never reaches the backend and never depends on whether a path to
a configured root exists. ARCHITECTURE.md §5 step 3 gives the coarse shape of
the lifecycle and names path building before the admission policy; the precise
ordering inside chain validation is the one stated here and in §5.2 and §5.4 —
parse, then the leaf-only tests, then path building.

**Which OID is named when more than one matches.** The leaf's own `Extensions`
list is walked in order and the **first** entry whose OID appears in
`reject_extensions` is the one refused and reported. The order of the OIDs in
`reject_extensions` is irrelevant. Configuring
`["2.5.29.17", "2.5.29.19"]` against a leaf whose `basicConstraints`
(`2.5.29.19`) sits at index 2 and whose `subjectAltName` (`2.5.29.17`) sits at
index 4 therefore produces `rejecting certificate containing extension
2.5.29.19 at index 2`, never `… 2.5.29.17 at index 4`: `%d` is always the
lowest index at which the leaf carries any listed OID.

Everything in this section holds unchanged from v1.1.0 through v1.3.3.

### 5.4 EKU filtering: the change at v1.1.2

Another rule that moves out of path building and becomes an explicit
leaf-only test in front of it.

**Up to v1.0.13** the configured EKU list is handed to chain verification as
the set of acceptable key usages and has effect only through path building: a
leaf whose own EKUs do not satisfy the list fails inside `Certificate.Verify`.

**From v1.0.17 up to v1.1.0** the list is still handed to chain verification
in the same field — but path building now sets `DisableEKUChecks` (§5.1), and
that switch turns off both the leaf test and the CA-nesting test inside
`Verify`. The consequence is blunt and easy to miss: **for these releases
`ext_key_usages` has no effect on admission at all.** Under a
`codeSigning`-only configuration a `serverAuth` leaf, a `clientAuth` leaf and a
leaf with no EKU extension are all admitted. Everything else about admission —
the path to a configured root, `accept_only_ca`, the shard window, the poison
rule — is unchanged, so an unrooted chain is still refused.

**From v1.1.2** the list gets its own test back, in front of path building
rather than inside it. If the validated list is non-empty, the leaf is tested
first: its own EKU extension must share at least one entry with the list, or
the submission is refused with

    rejecting certificate without EKU in %v

A leaf carrying **no** EKU extension at all shares nothing, so it is refused
too. The test is leaf-only — an intermediate's EKUs are not consulted — and it
runs before path building, so a refusal never reaches the backend. An empty
validated list (CONFIG.md §2.4, including the `Any` short-circuit) disables
the test entirely, and the list is still passed to path building as before.

---

---

## 6. Backend errors, and how they become statuses

### 6.1 The gRPC → HTTP map

The backend returns gRPC status codes; the CTFE maps them. An error that is
not a gRPC status at all is **500**.

| gRPC code | v1.0 .. v1.1.4 | **v1.2.0 and v1.3.3** |
| --- | --- | --- |
| `OK` | 200 | 200 |
| `Canceled`, `DeadlineExceeded` | **408** | **504** |
| `InvalidArgument`, `OutOfRange`, `AlreadyExists` | 400 | 400 |
| `NotFound` | 404 | 404 |
| `PermissionDenied` | 403 | 403 |
| `ResourceExhausted` | **403** | **429** |
| `Unauthenticated` | 401 | 401 |
| `FailedPrecondition` | 412 | 412 |
| `Aborted` | 409 | 409 |
| `Unimplemented` | 501 | 501 |
| `Unavailable` | 503 | 503 |
| anything else | 500 | 500 |

Both changed cells move at **v1.2.0** and both are graded: quota exhaustion
stops looking like an authorisation failure and starts looking like
rate-limiting (429), and a backend timeout stops looking like the *client*
timing out (408) and starts looking like a gateway failure (504).

From v1.0.5 the map is overridable per instance by a pluggable error mapper,
consulted before the table.

### 6.2 Read-only and mirror logs

| config field | from | effect |
| --- | --- | --- |
| `is_mirror` | **v1.0.21** | the two write handlers are **not registered**, so `add-chain` and `add-pre-chain` return **404** (not 405, not 403) |
| `frozen_sth` | **v1.1.0** | `get-sth` serves one constant STH forever; **writes are still accepted** |
| `is_readonly` | **v1.1.3** | same handler removal as `is_mirror`: **404** on the write paths |

The distinction between `frozen_sth` and `is_readonly` matters: freezing the
STH does not close the log to submissions, and a verifier can tell the two
apart by the status on `add-chain`.

### 6.3 v1.3.3: rate limiting and body size

**Non-fresh submissions.** A submission is *non-fresh* when the leaf
certificate's `NotBefore` is older than `--non_fresh_submission_age` (default
24h) relative to the instance's current time. When `--non_fresh_submission_limit`
is set (form `"<count>/<duration>"`, e.g. `30/1s`) a token-bucket limiter with
burst `--non_fresh_submission_burst` is applied **only** to those submissions.
Over the limit gives **429** with the body

    Too Many Requests
    rate-limited submission considered to be non-fresh

Fresh submissions are never limited, and masking does not apply (only 500 is
masked).

**Body size.** `--max_cert_chain_size` (default 512000) bounds the request body
of the two write endpoints only; over-size gives **413**. Read endpoints are
unbounded. The bound is imposed by wrapping those two handlers, so the refusal
happens before the body is parsed, and the body it returns is the §2.1 shape
carrying the §3.1 diagnostic identity. For a log with prefix `bench` and log id
`1001` at the default limit it is exactly

    Request Entity Too Large
    bench{1001}: certificate chain exceeds 512000-byte limit: http: request body too large

— that is, `<prefix>{<log_id>}: `, then `certificate chain exceeds <limit>-byte
limit: `, then the transport's own `http: request body too large`. `<limit>` is
the configured value in bytes, not the number of bytes the client sent.

**Transport.** `--tls_certificate` + `--tls_key` switch the listener to TLS
(minimum TLS 1.2). Both flags are required for that: the pair is inert unless
both are set, so a server given only one of them starts normally and goes on
serving plain HTTP rather than failing at start-up. A v1.3.3 instance on TLS
serves TLS *only* on that port: a cleartext HTTP request sent to it is
answered **400** and never reaches a handler, so no entrypoint and no
`/healthz` answers over cleartext there. That 400 is produced by `net/http`
itself, from the ordinary `http.Server` with a `TLSConfig`, and its body is
the standard library's — nothing in this product composes it, and nothing
specifies its text.
`--http_idle_timeout` overrides the idle timeout when
positive. The server's other timeouts are fixed:
`ReadHeaderTimeout` 10s, `ReadTimeout` 2m, `WriteTimeout` 2m, `IdleTimeout`
2m, `MaxHeaderBytes` 128 KiB.

---

## 7. Endpoints outside RFC 6962

| path | from | behaviour |
| --- | --- | --- |
| `/` | **v1.0.13** | 200 with an empty body, for naive health checkers. At **v1.0.17** the catch-all narrows: only the exact path `/` answers 200, and any other path no handler claims answers **404**. (At v1.0.13 the catch-all answered 200 for *every* unmatched path.) |
| `/healthz` | **v1.1.0** | 200 with the body exactly `ok` — no newline. It does **not** consult the backend; it is unconditional. |
| `/metrics` | v1.0 | Prometheus. An empty `--metrics_endpoint` means the HTTP endpoint — metrics are served on the same mux as `/` and `/healthz` — and that rule holds at **every** release; empty merely becomes the *default* value at **v1.1.0** (CLI.md §2.1). A non-empty value that differs from `--http_endpoint` gets a listener of its own. |
| `/log.v3.json` | **v1.3.3** | §7.1 |

From **v1.0.17** the whole mux is wrapped in a permissive CORS handler, so
every path above and every RFC 6962 path of §1 is served through it. The log is
public and unauthenticated, so the policy allows everything:

* a request carrying an `Origin` header is answered with
  `Access-Control-Allow-Origin: *`, and the response body is unchanged — CORS
  adds headers, it never rewrites an answer;
* a pre-flight `OPTIONS` carrying `Origin` and
  `Access-Control-Request-Method` is answered by the CORS layer itself (the
  RFC 6962 handler is not reached) with status **200**,
  `Access-Control-Allow-Origin: *` and `Access-Control-Allow-Methods` echoing
  the requested method;
* a pre-flight response carries `Vary` listing **`Origin`**,
  `Access-Control-Request-Method` and `Access-Control-Request-Headers` — three
  separate `Vary` header values, in that order, because all three inputs
  select the response;
* the catch-all `/` is inside the wrapper too, so it carries the same headers.

### 7.1 `/log.v3.json` (v1.3.3)

Per-log and prefixed like the RFC endpoints, GET only. Body:

    {
      "key":               "<base64 DER SubjectPublicKeyInfo>",
      "log_id":            "<base64 SHA-256 of that DER>",
      "mmd":               <int32 seconds>,
      "temporal_interval": {"start_inclusive": "<RFC3339>",
                            "end_exclusive":   "<RFC3339>"},
      "url":               "<the logv3_url config field>"
    }

`temporal_interval` is a **pointer with `omitempty`**, so it is omitted only
when *neither* `not_after_start` nor `not_after_limit` is configured. Configure
either one and the object appears — the condition in the handler is `||`, not
`&&`. Its own two members carry no `omitempty`, so **both keys are always
present** once the object is: a log with only `not_after_start` set emits
`"end_exclusive": ""`, and one with only `not_after_limit` emits
`"start_inclusive": ""`. Timestamps that are set are `.UTC().Format(time.RFC3339)`.
`mmd` is the `max_merge_delay_sec` config field.

The body is `json.Marshal` of a fixed struct written straight to the
`ResponseWriter`, which pins three things a test can assert on:

* **compact** — no indentation and no spaces after `:` or `,`;
* **no trailing newline** — this is `w.Write(jsonData)`, not
  `json.NewEncoder(w).Encode`, which would append one;
* **fixed member order** — struct field order, which is exactly the order shown
  above: `key`, `log_id`, `mmd`, `temporal_interval`, `url`.

The response carries `Content-Type: application/json`. `key` and `log_id` have
no `omitempty` either, so an instance with no signer still emits both as empty
strings rather than dropping them.

If `logv3_url` is unset the endpoint answers **404** with
`logV3JSON: logv3_url is not set in config` — so it is off by default.

---

## 8. Reading route

1. `rfc/rfc6962.txt` §4 for the meaning of every parameter and field.
2. §1 and §2 of this page — paths, method dispatch, error-body shape.
3. §3 then §4 for the endpoint you are implementing.
4. §5 before touching admission; §5.2 twice if you are at step 8.
5. §6.1 whenever a backend error has to become a status code.
6. CONFIG.md for every config field named above.
7. §7 for the endpoints outside RFC 6962, and for the permissive CORS wrapper
   that sits in front of the whole mux from **v1.0.17** (step 5) — the header
   contract there applies to every §1 path as well as to the ones §7 lists.
