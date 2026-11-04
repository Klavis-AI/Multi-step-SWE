# CLIENT-API — the Go library surface

This page is the exported API of the library half of the product: the HTTP
client, the JSON transport under it, the verification helpers, the log-list
parsers, the scanner, and the `ctfe` entry points a `ct_server` calls. The
verifier links against these names, so a signature given here is load-bearing:
a driver that calls `New(uri, hc, opts)` will not compile against a `New` that
takes three different things.

Names are given in Go's declaration syntax. Bodies are not, and are not the
point — the behaviour is specified in prose beside each one.

---

## 1. `client/` and `jsonclient/`

### 1.1 `jsonclient` — the transport

    package jsonclient

    // The logging sink. One method, and only this one, at every release
    // from v1.0 to v1.3.3.
    type Logger interface {
        Printf(string, ...interface{})
    }

    type Options struct {
        Logger        Logger
        PublicKey     string
        PublicKeyDER  []byte
        UserAgent     string   // from v1.1.0
        Authorization string   // from v1.3.3
    }

    type JSONClient struct{ … }
    func New(uri string, hc *http.Client, opts Options) (*JSONClient, error)
    func (c *JSONClient) BaseURI() string                      // from v1.0.17

`New` stores the URI with **every** trailing `/` removed, and `BaseURI()`
returns exactly that stored string: `http://host/log`, `http://host/log/` and
`http://host/log///` all give `http://host/log`. Nothing else is normalised —
the scheme, host case and path are returned as supplied.

A **nil** `hc` is legal at every release: the constructor substitutes an
`*http.Client` of its own, so a caller with no transport to configure passes
`nil` rather than building a client it does not need. The same constructor
(`client.New`, §1.2) forwards its own `hc` here, so the tolerance is inherited.

`PublicKey` is PEM text; `PublicKeyDER` is the same key as DER. Supplying both
is an error; supplying neither means signatures are not verified. A `PublicKey`
string that carries anything after its one PEM block is an error too — the
unconsumed remainder `ct.PublicKeyFromPEM` returns must be empty (§3).

`Logger` is the one-method interface above — `Printf(string, ...interface{})`
and nothing else, so any type with that single method satisfies it and
`*log.Logger` does too. A nil `Options.Logger` means the standard library's
`log` package is used.

The three request methods change signature once, at **v1.0.5**:

| release | signature shape |
| --- | --- |
| v1.0 | `GetAndParse`, `PostAndParse`, `PostAndParseWithRetry` all return `(*http.Response, error)` |
| **v1.0.5** onward | all three return `(*http.Response, []byte, error)` — the raw body is handed back alongside |

Only the **return** list moves there. The parameters do not change at any
release from v1.0 to v1.3.3, and they are part of the load-bearing surface —
a driver calls these three by their full argument lists:

    func (c *JSONClient) GetAndParse(ctx context.Context, path string, params map[string]string, rsp interface{}) (*http.Response, []byte, error)
    func (c *JSONClient) PostAndParse(ctx context.Context, path string, req, rsp interface{}) (*http.Response, []byte, error)
    func (c *JSONClient) PostAndParseWithRetry(ctx context.Context, path string, req, rsp interface{}) (*http.Response, []byte, error)

`path` is appended to the base URI the constructor stored (above); `params`
is the GET's query string, encoded as the next paragraph describes; `req` is
JSON-encoded into the POST body, which is sent with a `Content-Type:
application/json` header; `rsp`, when non-nil, is the value the decoded
response body is written into, and nothing else is written to it. So the
retrying call is `PostAndParseWithRetry(ctx, path, req, rsp)` — a
`context.Context` first and then exactly three parameters — against **three**
return values, and its two siblings are `PostAndParse(ctx, path, req, rsp)`
and `GetAndParse(ctx, path, params, rsp)`. The `LogClient` of §1.2 embeds
this type, so the promoted methods have these same shapes.

#### The POST redirect guard, from v1.2.0

`PostAndParse` looks at the method of the request that actually reached the
server, not only at the status that came back. HTTP **301**, **302** and
**303** all convert a POST into a GET; that GET then succeeds, and a caller
reading only the status would record a submission the log never saw as one.
From **v1.2.0**, and unchanged at **v1.3.3**, a reply whose final request
method is not `POST` is therefore an **error**: nothing is decoded into the
caller's response value, which is left exactly as it was passed in, and no
`*http.Response` and no body come back beside the error. **307** and **308**
preserve the method, so they are followed, parsed and returned exactly as
before, and so is a plain 200 from an endpoint that does not redirect —
refusing every redirect would be as wrong as accepting a converted one. At
every release **from v1.0 to v1.1.4** there is no such check and the
redirected reply is parsed like any other. The guard is entirely client-side;
this product's own server issues no redirects at any release (HTTP-API.md §2).

`RspError` — the error type carrying an HTTP failure — is declared in
**`client`** from v1.0.5, and **moves to `jsonclient` at v1.1.0**, with
`client.RspError` left behind as a type alias so existing callers keep
compiling:

    // in jsonclient, from v1.1.0
    type RspError struct {
        Err        error
        StatusCode int
        Body       []byte
    }
    func (e RspError) Error() string

    // in client, from v1.1.0
    type RspError = jsonclient.RspError

**Which functions return it.** From **v1.0.5 through v1.0.21** the `jsonclient`
helpers never build an `RspError`; `client.LogClient`'s own methods do, and only
when the helper handed one back a response. On that path — `GetSTH`,
`GetSTHConsistency`, `GetProofByHash`, `GetAcceptedRoots`, `GetRawEntries` (and
so `GetEntries`), `AddJSON` and the add-chain path — a non-2xx comes back as
`RspError{Err, StatusCode, Body}` carrying that response's status code and the
**raw, unparsed** body, and so does a response that arrived intact but failed to
parse or failed signature verification. Where no response is available the bare
error is returned, unwrapped. From **v1.1.0** the type moves into `jsonclient`
(`client.RspError` becoming the alias shown above) and the helpers build it for
a non-2xx themselves, so that case no longer depends on `LogClient` seeing a
response. `LogClient` still builds one itself for the other case — a response
that arrived intact and then failed to parse or failed signature verification —
at every release from v1.0.5 to v1.3.3.

The one place that distinction is observable is the retrying add path. **At
v1.0.5 only**, `PostAndParseWithRetry`'s default arm returns a **nil**
`*http.Response` alongside the body, so `AddChain`, `AddPreChain` and `AddJSON`
report a non-2xx as the plain `got HTTP Status %q` error and never as an
`RspError`. **From v1.0.10 through v1.0.21** that arm returns the response as
well, and those three report an `RspError` like every other method. **From
v1.1.0** the response is nil again — but the arm's own error is already an
`RspError`, so the three still report one, by the shorter route.

#### Retry and backoff

`PostAndParseWithRetry` is the one that retries. The state machine:

| response | action |
| --- | --- |
| 200 | return |
| **408** Request Timeout | retry **immediately**, no backoff |
| **503** Service Unavailable | back off, honouring `Retry-After` |
| **429** Too Many Requests | falls to the default arm up to v1.1.2; **from v1.1.3, identical to 503** |
| anything else | return an error carrying `got HTTP status %q` — the text is `got HTTP Status %q`, capital *S*, through **v1.0.21**, and lower-case **from v1.1.0**, where the arm stops handing the string up and builds the error itself; `PostAndParse`'s own non-2xx path keeps the capital at every release, so the two are not the same string. The *type* is release-dependent: a plain `fmt.Errorf` string through **v1.0.21**, and `RspError{StatusCode, Body, Err}` only from **v1.1.0**, where this type moves into `jsonclient`. The `*http.Response` returned beside it is **nil at v1.0.5**, the response itself **from v1.0.10 through v1.0.21**, and **nil again from v1.1.0** (where it no longer matters, the error already being an `RspError`). The `[]byte` body beside it is the reply body on every arm that read one, and nil only where none was read — the context-expiry arm at every release, and this default arm from **v1.1.0**, where it returns `nil` for both body and response. **v1.0.5 is the exception**: there this arm returns the reply body beside a **nil** `*http.Response` — the shape §1.1 above describes, and the one a caller at that release can rely on, `AddChain`, `AddPreChain` and `AddJSON` getting the body and no response — which is what decides whether `LogClient`'s add-chain path can wrap it, at the releases where it has to (§1.1 above) |

`Retry-After` is parsed first as an integer number of seconds and, failing
that, as an RFC 1123 date. `context.Canceled` and `context.DeadlineExceeded`
are never retried.

The backoff itself is rewritten at **v1.0.5**: v1.0 has plain doubling from 1 s
to a 128 s ceiling; from v1.0.5 it is a small backoff object with a multiplier
capped so the maximum interval is still 128 s (2^7), plus up to 250 ms of
jitter, and a `decreaseMultiplier` step so a recovering server is not punished
forever. The ceiling does not change; the shape does.

The 429 change at v1.1.3 matters because the *log* starts returning 429 at
v1.2.0 for quota exhaustion (HTTP-API.md §6.1). The client learns to back off
before the server learns to ask.

#### How a GET query string is built

`GetAndParse` loads its `params` into a `url.Values` and appends
`vals.Encode()` to the path. `url.Values.Encode` sorts by parameter name, so
the query string of every read endpoint is in **alphabetical order by
parameter name**, not in the order the parameters are listed in RFC 6962 or in
this document. In practice that means `end=...&start=...`,
`leaf_index=...&tree_size=...`, `hash=...&tree_size=...` and
`first=...&second=...`. This is unchanged from v1.0 through v1.3.3.

### 1.2 `client.LogClient`

    package client

    func New(uri string, hc *http.Client, opts jsonclient.Options) (*LogClient, error)
    type LogClient struct{ … }   // embeds jsonclient.JSONClient

| method | signature | from |
| --- | --- | --- |
| `AddChain` | `(ctx context.Context, chain []ct.ASN1Cert) (*ct.SignedCertificateTimestamp, error)` | v1.0 |
| `AddPreChain` | same shape | v1.0 |
| `AddJSON` | `(ctx context.Context, data interface{}) (*ct.SignedCertificateTimestamp, error)` | v1.0, **removed at v1.1.2** |
| `GetSTH` | `(ctx context.Context) (*ct.SignedTreeHead, error)` | v1.0 |
| `GetSTHConsistency` | `(ctx context.Context, first, second uint64) ([][]byte, error)` | v1.0 |
| `GetProofByHash` | `(ctx context.Context, hash []byte, treeSize uint64) (*ct.GetProofByHashResponse, error)` | v1.0 |
| `GetAcceptedRoots` | `(ctx context.Context) ([]ct.ASN1Cert, error)` | v1.0 |
| `GetRawEntries` | `(ctx context.Context, start, end int64) (*ct.GetEntriesResponse, error)` | v1.0 |
| `GetEntries` | `(ctx context.Context, start, end int64) ([]ct.LogEntry, error)` | v1.0 |
| `VerifySTHSignature` | `(sth ct.SignedTreeHead) error` | v1.0 |
| `VerifySCTSignature` | `(sct ct.SignedCertificateTimestamp, ctype ct.LogEntryType, certData []ct.ASN1Cert) error` | v1.0 |
| `GetEntryAndProof` | `(ctx context.Context, index, treeSize uint64) (*ct.GetEntryAndProofResponse, error)` | **v1.0.19** |

The method for accepted roots is `GetAcceptedRoots`, not `GetRoots`; there has
never been a `LogClient.GetRoots`.

`GetEntries` differs from `GetRawEntries` in that it parses each returned
`leaf_input`/`extra_data` pair into a `ct.LogEntry`; `GetRawEntries` hands back
the response as received.

**`GetSTH` verifies the head it returns.** When the client carries a public key
— `Options.PublicKey` or `Options.PublicKeyDER`, §1.1 — it checks the response's
tree-head signature against it, over the signature input §3 rebuilds from the
head that was just parsed, and a head that key did not sign comes back as an
error with no head, exactly as a failed SCT check does on the add path. With no
key configured there is no verifier and the check does not run, which is the
other half of §1.1's "supplying neither means signatures are not verified". So
`ctclient get-sth` — which is this method — succeeds on an unverifiable head
when given no `--pub_key` and fails on it when given the log's key, which is
the difference a driver reads. No other read method verifies a signature:
`GetSTHConsistency`, `GetProofByHash` and `GetEntries`/`GetRawEntries` return
what the log answered, and `GetAcceptedRoots` returns it too, base64-decoding
each certificate into an `ASN1Cert` and failing only over one that will not
decode.

#### Two checks the client makes for itself

These are client-side, at every release from v1.0 to v1.3.3, and a caller
observes them without the log being reachable at all:

* **The entry range is validated before the request is built.**
  `GetRawEntries` — and therefore `GetEntries`, which calls it — refuses a
  negative `end` (`end should be >= 0`) and a range with `end < start`
  (`start should be <= end`), returning the error and sending nothing. A log
  that would have answered **400** (HTTP-API.md §4.3 rule 1) is never asked.
* **`AddChain` and `AddPreChain` verify the SCT they are handed.** After the
  response parses, the client TLS-decodes `signature` into a
  `tls.DigitallySigned`, rejects trailing bytes after it, assembles the
  `ct.SignedCertificateTimestamp`, and then calls `VerifySCTSignature` on it
  with the entry type and **the chain that was just submitted**. A log that
  returns a well-formed SCT over some *other* certificate therefore makes
  `AddChain` fail, not succeed. When `Options` carried no public key there is
  no verifier and this check does not run.

### 1.3 `CheckLogClient`

The read-only interface the verification helpers accept, so that a caller can
supply something other than a real HTTP client.

    // v1.0.13
    type CheckLogClient interface {
        GetSTH(context.Context) (*ct.SignedTreeHead, error)
        GetSTHConsistency(ctx context.Context, first, second uint64) ([][]byte, error)
        GetProofByHash(ctx context.Context, hash []byte, treeSize uint64) (*ct.GetProofByHashResponse, error)
    }

At **v1.0.17** `BaseURI() string` is prepended as the first method, so that a
checker can report which log an answer came from. That is a breaking interface
change: from v1.0.17 on, every implementation must grow the method, and a
three-method implementation no longer satisfies the interface.

    // v1.0.17 onwards
    type CheckLogClient interface {
        BaseURI() string
        GetSTH(context.Context) (*ct.SignedTreeHead, error)
        GetSTHConsistency(ctx context.Context, first, second uint64) ([][]byte, error)
        GetProofByHash(ctx context.Context, hash []byte, treeSize uint64) (*ct.GetProofByHashResponse, error)
    }

`LogClient` satisfies it by promotion from the embedded `jsonclient.JSONClient`
(§1.1); `dnsclient.DNSClient` has its own (§6.1).

### 1.4 Temporal (multi-)logs

    func TemporalLogConfigFromFile(filename string) (*configpb.TemporalLogConfig, error)
    type AddLogClient interface{ … }
    type TemporalLogClient struct{ … }
    func NewTemporalLogClient(cfg configpb.TemporalLogConfig, hc *http.Client) (*TemporalLogClient, error)
    func (tlc *TemporalLogClient) IndexByDate(when time.Time) (int, error)

A temporal log is a series of shards, each accepting certificates whose
`NotAfter` falls in its window; `IndexByDate` picks the shard. At **v1.1.0**
`NewTemporalLogClient` takes a **pointer** `*configpb.TemporalLogConfig`
instead of a value — a small, compile-breaking change that belongs to that
step.

---

## 2. `ctutil/` — verifying somebody else's SCT

Added at **v1.0.13**. The signatures do not change after that.

    func LeafHash(chain []*x509.Certificate, sct *ct.SignedCertificateTimestamp, embedded bool) ([sha256.Size]byte, error)
    func LeafHashB64(chain []*x509.Certificate, sct *ct.SignedCertificateTimestamp, embedded bool) (string, error)
    func VerifySCT(pubKey crypto.PublicKey, chain []*x509.Certificate, sct *ct.SignedCertificateTimestamp, embedded bool) error
    func VerifySCTWithVerifier(sv *ct.SignatureVerifier, chain []*x509.Certificate, sct *ct.SignedCertificateTimestamp, embedded bool) error   // from v1.1.2
    func ContainsSCT(cert *x509.Certificate, sct *ct.SignedCertificateTimestamp) (bool, error)

`chain` is leaf-first. `embedded` says whether the SCT was found inside the
certificate's SCT-list extension rather than delivered alongside it, and it
changes the leaf that gets hashed: an embedded SCT covers the *precertificate*
form, so the leaf is rebuilt with the **SCT-list** extension removed
(`x509.RemoveSCTList` over the issued certificate's `RawTBSCertificate`,
X509-FORK.md §4.2) and the issuer key hash filled in (X509-FORK.md §4.3).

It is **not** the poison extension that comes off here. What `ctutil` is
handed at `chain[0]` is the *issued* certificate, which carries an SCT-list
extension and no poison extension at all, so `RemoveCTPoison` and
`BuildPrecertTBS` — the submission-side rebuild of ARCHITECTURE.md §5, which
starts from a poisoned precertificate — **fail** on it: there is no poison
extension for them to remove. The two rebuilds run in opposite directions over
different extensions; X509-FORK.md §4.2 states the distinction. (The wording
of the error is not part of any contract.)

Setting `embedded` false for an SCT that was in fact embedded produces a leaf
hash that is well-formed and wrong. Setting it **true** is checked rather than
trusted: before building anything, the `embedded` path runs the `ContainsSCT`
test described below on `chain[0]`, and `LeafHash`, `LeafHashB64` and
`VerifySCT` all **return an error** when it comes back false — when the
certificate carries no SCT list, or carries one that does not hold *this* SCT.
They do not hash what they were asked to hash and leave the caller to discover
the mismatch later.

When `embedded` is false the entry type is **not** fixed at `X509LogEntryType`:
the leaf is built for whatever `chain[0]` actually is. `ctutil` asks the
certificate — `chain[0].IsPrecertificate()` — and uses `PrecertLogEntryType`
when that is true **or** when `embedded` is true, and `X509LogEntryType` only
when both are false. A precertificate handed in with `embedded` false therefore
gets a precertificate leaf, built by `MerkleTreeLeafFromChain`; the `embedded`
case is the only one that reaches `MerkleTreeLeafForEmbeddedSCT`. The two
builders are not interchangeable — `MerkleTreeLeafFromChain` rebuilds the TBS
through `x509.BuildPrecertTBS`, which removes the **poison** extension, so it
fails on an issued certificate carrying an embedded SCT list (X509-FORK.md
§4.2).

Two argument checks run **before** any of that, at every release from v1.0.13.
`LeafHash`, `LeafHashB64` and `VerifySCT` — and `VerifySCTWithVerifier` from
v1.1.2, which shares the same leaf construction — **reject an empty `chain`
and a nil `sct` with an error**, whatever `embedded` says. Neither is a panic:
nothing indexes `chain[0]` and nothing reads `sct.Timestamp` until both checks
have passed, so a caller who hands in `nil` gets an error back and a live
process. That is deliberately *unlike* `ContainsSCT` below, which treats a nil
argument as a question with an answer rather than as a mistake; here an absent
chain or an absent SCT is a mistake, and the `embedded` pre-check just
described runs only once both are present.

`LeafHash` returns the zero hash together with the error on failure — do not
return a partially-filled array. `VerifySCTWithVerifier` rejects a nil
verifier; it shares the same pre-check, because it shares the same leaf
construction.

`ContainsSCT` asks whether the certificate's SCT-list extension already holds
*this* SCT. It compares the TLS encoding of the whole SCT (TLS-CODEC.md §5.5)
against each entry of the list byte for byte, so an SCT differing only by one
millisecond of `Timestamp` is a different SCT and is not found. A `nil`
certificate or a `nil` SCT is **not** an error: either one returns
`(false, nil)`.

### 2.1 `LogInfo`

    type LogInfo struct {
        Description string
        Client      client.CheckLogClient
        MMD         time.Duration
        Verifier    *ct.SignatureVerifier
        PublicKey   []byte
        // plus an unexported mutex and cached STH
    }
    type LogInfoByHash map[[sha256.Size]byte]*LogInfo

    func NewLogInfo(log *loglist.Log, hc *http.Client) (*LogInfo, error)
    func LogInfoByKeyHash(ll *loglist.LogList, hc *http.Client) (LogInfoByHash, error)

    func (li *LogInfo) LastSTH() *ct.SignedTreeHead
    func (li *LogInfo) SetSTH(sth *ct.SignedTreeHead)
    func (li *LogInfo) VerifySCTSignature(sct ct.SignedCertificateTimestamp, leaf ct.MerkleTreeLeaf) error
    func (li *LogInfo) VerifyInclusionLatest(ctx context.Context, leaf ct.MerkleTreeLeaf, timestamp uint64) (int64, error)
    func (li *LogInfo) VerifyInclusion(ctx context.Context, leaf ct.MerkleTreeLeaf, timestamp uint64) (int64, error)
    func (li *LogInfo) VerifyInclusionAt(ctx context.Context, leaf ct.MerkleTreeLeaf, timestamp, treeSize uint64, rootHash []byte) (int64, error)

`Description` and `PublicKey` are carried over from the log-list entry the
constructor was handed: `PublicKey` holds that entry's `Key` field **as it
stands** — the DER `SubjectPublicKeyInfo` the list carries, not a re-encoding
of it, and not its hash — and it is the same bytes `Verifier` was built from.
That holds at every release the type exists, including from v1.1.4 where the
entry is a `loglist3.Log` (§4.3).

All three `VerifyInclusion*` return **`-1`** and an error on failure, never
`0`, because `0` is a valid leaf index.

Each of them **re-stamps the leaf it is handed**: `leaf.TimestampedEntry.
Timestamp` is overwritten with the `timestamp` argument before the leaf is
marshalled and hashed. A leaf built for the wrong timestamp therefore still
verifies, and the caller's leaf is modified in place.
`VerifySCTSignature` does the same, taking the timestamp from the SCT.

They differ only in which tree they prove against, and that is what the cached
STH behind `LastSTH`/`SetSTH` is for:

* `VerifyInclusion` fetches a fresh STH on **every** call and stores it with
  `SetSTH`, so after it returns `LastSTH` is the STH it just used.
* `VerifyInclusionLatest` uses the cached STH and fetches **only when the
  cache is empty**, storing what it fetched. A second call after a successful
  first one performs no fetch.
* `VerifyInclusionAt` is told the tree size and root hash and never fetches an
  STH at all.

The parameter type of `NewLogInfo` and `LogInfoByKeyHash` follows whichever
log-list package is current: `loglist` up to v1.1.3, **`loglist3` from
v1.1.4** (§4).

`NewLogInfo` refuses an entry it cannot turn into a usable `LogInfo`, and
**every error it returns identifies the log by that entry's `Description`** —
the case a caller meets in practice is a `Key` that will not parse as a DER
`SubjectPublicKeyInfo`, and the refusal names the log all the same. What is
fixed here is the *identifier*, not the sentence around it: the wording is the
implementation's and is not part of this contract, but a diagnostic that
identified the log by its URL, or by nothing, would not be this behaviour.
`LogInfoByKeyHash` does not skip an entry its factory refused — it abandons
the map and returns that error, so one unusable key refuses the whole list.
Both hold at **every release the type exists, v1.0.13 through v1.3.3**, over
whichever log-list generation the paragraph above names for the release in
hand.

`VerifyInclusionAt` is where the Merkle backend swap of MERKLE.md §4.2 is
visible: the function's signature never changes, but the library it calls does,
four times.

### 2.2 DNS variants (v1.0.13 .. v1.1.0 only)

    func NewLogInfoOverDNS(log *loglist.Log) (*LogInfo, error)
    func NewLogInfoOverDNSWrapper(log *loglist.Log, _ *http.Client) (*LogInfo, error)
    func LogInfoByKeyHashOverDNS(ll *loglist.LogList, hc *http.Client) (LogInfoByHash, error)

`NewLogInfoOverDNS` takes **one** argument — there is no HTTP client to hand
it. `NewLogInfoOverDNSWrapper` exists only to give that constructor the same
shape as `NewLogInfo`, so that `LogInfoByKeyHashOverDNS` and
`LogInfoByKeyHash` can share one loop; its `*http.Client` is accepted and
ignored, and it is otherwise `NewLogInfoOverDNS`.

Both refuse a log whose `dns_api_endpoint` (§4.1) is empty: that field is the
DNS client's base name and without it there is nothing to query. So
`LogInfoByKeyHashOverDNS` fails on any list containing one such log, while
`LogInfoByKeyHash` over the same list succeeds.

These exist only while `dnsclient/` does (§6) and disappear with it at v1.1.2.

---

## 3. Root-package helpers

    // serialization.go, from v1.0.14 — so present at v1.0.17 and later, absent at v1.0.13
    func LeafHashForLeaf(leaf *MerkleTreeLeaf) ([sha256.Size]byte, error)

    // types.go
    type SignatureVerifier struct{ … }
    func NewSignatureVerifier(pk crypto.PublicKey) (*SignatureVerifier, error)
    func (s SignatureVerifier) VerifySignature(data []byte, sig tls.DigitallySigned) error
    func (s SignatureVerifier) VerifySCTSignature(sct SignedCertificateTimestamp, entry LogEntry) error
    func (s SignatureVerifier) VerifySTHSignature(sth SignedTreeHead) error

    // serialization.go — the two signature inputs, at every release from v1.0
    func SerializeSCTSignatureInput(sct SignedCertificateTimestamp, entry LogEntry) ([]byte, error)
    func SerializeSTHSignatureInput(sth SignedTreeHead) ([]byte, error)

    // serialization.go
    func MerkleTreeLeafFromChain(chain []*x509.Certificate, etype LogEntryType, timestamp uint64) (*MerkleTreeLeaf, error)
    func MerkleTreeLeafFromRawChain(rawChain []ASN1Cert, etype LogEntryType, timestamp uint64) (*MerkleTreeLeaf, error)
    func MerkleTreeLeafForEmbeddedSCT(chain []*x509.Certificate, timestamp uint64) (*MerkleTreeLeaf, error)   // from v1.0.13
    func CreateX509MerkleTreeLeaf(cert ASN1Cert, timestamp uint64) *MerkleTreeLeaf
    func IsPreIssuer(issuer *x509.Certificate) bool
    func TimestampToTime(ts uint64) time.Time

    // signatures.go
    func PublicKeyFromPEM(b []byte) (crypto.PublicKey, SHA256Hash, []byte, error)
    func PublicKeyFromB64(b64PubKey string) (crypto.PublicKey, error)

    // types.go, the hash type
    type SHA256Hash [sha256.Size]byte

    // types.go, the tree head — the same six fields at every release
    type SignedTreeHead struct {
        Version           Version
        TreeSize          uint64
        Timestamp         uint64
        SHA256RootHash    SHA256Hash
        TreeHeadSignature DigitallySigned   // the signature; TLS-encoded when it is on the wire
        LogID             SHA256Hash
    }

    // types.go, the domain-separation bytes, from v1.0.13
    const TreeLeafPrefix = byte(0x00)
    const TreeNodePrefix = byte(0x01)

    // types.go, from v1.0.19
    func (r *GetSTHResponse) ToSignedTreeHead() (*SignedTreeHead, error)

    // types.go, from v1.1.2
    func (r *AddChainResponse) ToSignedCertificateTimestamp() (*SignedCertificateTimestamp, error)

The last two `SignatureVerifier` methods are the ones a caller normally reaches
for: each builds its own signature input — `SerializeSCTSignatureInput` for the
first, `SerializeSTHSignatureInput` for the second — and then calls
`VerifySignature` on it, so the caller passes the CT structures and nothing
else. `LogClient` exposes wrappers under the same two names (§1.2) taking a
different argument list; they are the client's, not the root package's.

`GetSTHResponse` is the JSON shape of a `get-sth` response as it comes off the
wire (HTTP-API.md §4.1) — `TreeSize uint64`, `Timestamp uint64`,
`SHA256RootHash []byte` and `TreeHeadSignature []byte`, the last two decoded
from base64 by `encoding/json` itself. From **v1.0.19** the root package
exposes the conversion from that wire form to the in-memory
`ct.SignedTreeHead` as a method on the response, so that any caller holding a
raw response — not only `client.LogClient`, which already did this inline —
can produce a verifiable tree head. It:

* copies `TreeSize` and `Timestamp` across unchanged;
* requires `SHA256RootHash` to be **exactly 32 bytes** and copies it into the
  `[32]byte` field; any other length is an error naming the expected and actual
  lengths, and no tree head is returned;
* TLS-decodes `TreeHeadSignature` into a `tls.DigitallySigned`
  (TLS-CODEC.md §3) and requires the **whole** field to be consumed: trailing
  bytes after the signature structure are an error, as is a field that does not
  decode at all.

The method never verifies the signature — it only reshapes — so a tree head it
returns still has to be checked against the log's key (§2).

`AddChainResponse` gets the same treatment from **v1.1.2**: the root package
adds `ToSignedCertificateTimestamp`, the conversion from the wire response to
the in-memory `ct.SignedCertificateTimestamp`. It copies `SCTVersion` and
`Timestamp` across unchanged; requires `ID` to be exactly 32 bytes and copies
it into `LogID.KeyID`; base64-decodes `Extensions` into the extension bytes;
and TLS-decodes `Signature` into a `DigitallySigned`, requiring the **whole**
field to be consumed — trailing bytes after the signature structure are an
error, as is a field that does not decode at all. It returns the SCT alone,
like `ToSignedTreeHead` above it, and the caller still has to verify the
signature.

`CreateX509MerkleTreeLeaf` is the no-error, no-chain form: it wraps one
already-DER-encoded `ASN1Cert` and a timestamp into an `X509LogEntryType`
`MerkleTreeLeaf` with `V1` versioning and an empty extensions field. It cannot
fail, so it returns the leaf alone. `MerkleTreeLeafFromChain` builds the leaf
for a chain and a given entry type, and `MerkleTreeLeafFromRawChain` is the
same builder over DER-encoded input: it parses the chain's `ASN1Cert` entries
— at most the first three, which is all an RFC 6962 leaf can use, since a
precertificate leaf reaches at most `chain[2]` through a pre-issuer — and
delegates to `MerkleTreeLeafFromChain`, so the leaf it returns is the one the
parsed form would return. An entry that will not parse is an error naming its
position, `failed to parse chain[%d] cert: %v`; the test is `err != nil`
through **v1.0.19** and the fork's `IsFatal` from **v1.0.21**, so from that
release an entry carrying only non-fatal errors is parsed and used rather than
refused.
`MerkleTreeLeafForEmbeddedSCT` is the precertificate case of it — `chain` is
leaf-first with the issuer next — and is what `ctutil`'s `embedded` path
builds. It needs both certificates: at every release from v1.0.13 a chain of
fewer than two is an **error**, not a panic, returned before anything reaches
`chain[1]`. `PublicKeyFromPEM` returns **four** values: the key, the SHA-256 of
its DER `SubjectPublicKeyInfo` (which is exactly the log id an SCT carries),
the **unconsumed remainder of the input** — whatever follows the one PEM block
it decoded, which is empty when the input holds nothing but that block, and is
*not* the DER just described — and an error; `PublicKeyFromB64` returns only
the key and an error.
`IsPreIssuer` is the exported form of the test `BuildPrecertTBS` applies to
its pre-issuer argument: it is `true` exactly when the certificate's parsed
`ExtKeyUsage` slice contains `x509.ExtKeyUsageCertificateTransparency`
(X509-FORK.md §4.1), and false otherwise — an ordinary CA carries no such
usage, so it is not a pre-issuer.

`TimestampToTime` reads its argument as **milliseconds** since the Unix epoch
(TLS-CODEC.md §5.1) and splits it into whole seconds and nanoseconds — it does
not check the value, so a field holding *seconds* is rendered as a date in
January 1970. `TreeLeafPrefix` and `TreeNodePrefix` are the named forms of the
0x00/0x01 bytes of MERKLE.md §1, and exist in the root package only **from
v1.0.13**; up to v1.0.10 the only exported names for those two bytes are
`merkletree.LeafPrefix` and `merkletree.NodePrefix` (MERKLE.md §4.1).

`NewSignatureVerifier` enforces RFC 6962's key rules and is the only place
they are enforced: an RSA key shorter than 2048 bits gives
`public key is RSA with < 2048 bits (size:%d)`, an ECDSA key that is not on
P-256 gives `public is ECDSA, but not on the P256 curve`, and any other key
type gives `Unsupported public key type %v` — that last one is fatal at every
release and cannot be overridden.

The escape hatch over the first two changes shape at **v1.0.21**:

| releases | how the hatch is reached | effect |
| --- | --- | --- |
| v1.0 .. v1.0.19 | the command-line flag `allow_verification_with_non_compliant_keys`, a `flag` global declared in the root package (CLI.md §2.1) | when set, the refusal text is logged as a warning and the verifier is returned |
| v1.0.21 .. v1.3.3 | the exported package variable `AllowVerificationWithNonCompliantKeys bool`, default `false` | same effect, but only an importing program can set it; no command line can |

So at v1.0.21 the flag is **gone from every binary in the tree**, not defaulted
off, and a program invoked with it exits 2 as for any unknown flag. The
variable that replaces it is documented as a deliberate, library-only
concession.

### 3.1 The two signature inputs

`SerializeSCTSignatureInput` and `SerializeSTHSignatureInput` are the root
package's exported names for the two byte strings RFC 6962 signs, and they are
present at **every** release from v1.0 to v1.3.3. They build the bytes; they do
not sign or verify them, and `SignatureVerifier` (above) is what consumes the
result.

    func SerializeSCTSignatureInput(sct SignedCertificateTimestamp, entry LogEntry) ([]byte, error)

takes the SCT's own `SCTVersion`, `Timestamp` and `Extensions` together with the
entry being certified, and emits RFC 6962 §3.2's signature input: version,
`SignatureType` `certificate_timestamp`, timestamp, entry type, the entry body
(the `ASN1Cert` for an X.509 entry, the `PreCert` for a precertificate, the
`JSONDataEntry` for an xjson entry) and the extensions — the same field order
as the `TimestampedEntry` of TLS-CODEC.md §5.4, with the version and signature
type in front. A `SCTVersion` other than `V1` is an error, as is a `LogEntry`
that carries none of the three entry bodies.

    func SerializeSTHSignatureInput(sth SignedTreeHead) ([]byte, error)

emits RFC 6962 §3.5's tree-head signature input: version, `SignatureType`
`tree_hash`, timestamp, tree size, root hash. A `Version` other than `V1` is an
error. The root hash is the fixed 32-byte `SHA256Hash`, unprefixed.

Both return TLS encodings (TLS-CODEC.md §2–§3), so they are byte-for-byte
determined by the values passed in.

### 3.2 The JSON wire structs

The JSON request and response bodies of HTTP-API.md §3–§4 are Go structs in the
**root** package, and the verifier links against them by name. Field names are
fixed by the `json:` tags HTTP-API.md gives; the Go names are these, at every
release from v1.0 unless noted:

    type AddChainRequest  struct{ Chain [][]byte `json:"chain"` }
    type AddChainResponse struct {
        SCTVersion Version `json:"sct_version"`
        ID         []byte  `json:"id"`
        Timestamp  uint64  `json:"timestamp"`
        Extensions string  `json:"extensions"`
        Signature  []byte  `json:"signature"`
    }
    type AddJSONRequest   struct{ Data interface{} `json:"data"` }
    type GetSTHResponse   struct {
        TreeSize          uint64 `json:"tree_size"`
        Timestamp         uint64 `json:"timestamp"`
        SHA256RootHash    []byte `json:"sha256_root_hash"`
        TreeHeadSignature []byte `json:"tree_head_signature"`
    }
    type GetSTHConsistencyResponse struct{ Consistency [][]byte `json:"consistency"` }
    type GetProofByHashResponse    struct {
        LeafIndex int64    `json:"leaf_index"`
        AuditPath [][]byte `json:"audit_path"`
    }
    type LeafEntry struct {
        LeafInput []byte `json:"leaf_input"`
        ExtraData []byte `json:"extra_data"`
    }
    type GetEntriesResponse       struct{ Entries []LeafEntry `json:"entries"` }
    type GetRootsResponse         struct{ Certificates []string `json:"certificates"` }
    type GetEntryAndProofResponse struct {
        LeafInput []byte   `json:"leaf_input"`
        ExtraData []byte   `json:"extra_data"`
        AuditPath [][]byte `json:"audit_path"`
    }

`[]byte` members are base64 in JSON because `encoding/json` renders them that
way; `AddChainResponse.Extensions` is a **`string`**, already base64, and is
`""` in every SCT this log issues. `AddJSONRequest` outlives the client method
that used it: `LogClient.AddJSON` goes at v1.1.2 (§1.2) but the struct is
declared at every release from v1.0 to v1.3.3.

From **v1.0.5** the root package adds several helpers for consuming log entries:

    // Converts a wire LeafEntry (leaf_input / extra_data bytes from get-entries)
    // into a parsed LogEntry; from v1.0.5.
    func LogEntryFromLeaf(index int64, leaf *LeafEntry) (*LogEntry, error)

    // Accessor methods on MerkleTreeLeaf; from v1.0.5.
    func (m *MerkleTreeLeaf) X509Certificate() (*x509.Certificate, error)
    func (m *MerkleTreeLeaf) Precertificate() (*x509.Certificate, error)

    // Reshaped precertificate container; from v1.0.5.
    type Precertificate struct {
        Submitted      ASN1Cert
        IssuerKeyHash  [32]byte
        TBSCertificate *x509.Certificate
    }

    // The two entry shapes these converters produce. LogEntry is present at
    // every release from v1.0; RawLogEntry, and the two functions under it,
    // arrive at v1.0.21. `Leaf` is a **value**, not a pointer, and exactly
    // one of the three certificate fields is non-nil.
    type LogEntry struct {
        Index    int64
        Leaf     MerkleTreeLeaf
        X509Cert *x509.Certificate   // the parsed certificate
        Precert  *Precertificate
        JSONData []byte
        Chain    []ASN1Cert          // the entry's issuers, in order
    }

    // RawLogEntry is that structure before the ASN.1 work: the certificate
    // bytes as the log stored them. From v1.0.21, as are its two functions.
    type RawLogEntry struct {
        Index int64
        Leaf  MerkleTreeLeaf
        Cert  ASN1Cert               // the submitted (pre-)certificate
        Chain []ASN1Cert
    }
    func RawLogEntryFromLeaf(index int64, entry *LeafEntry) (*RawLogEntry, error)
    func (rle *RawLogEntry) ToLogEntry() (*LogEntry, error)

`LogEntryFromLeaf` TLS-decodes `LeafEntry.LeafInput` into a `MerkleTreeLeaf`
and rejects trailing bytes after it, then TLS-decodes `LeafEntry.ExtraData`
into the structure that matches the leaf's entry type — `CertificateChain` for
`X509LogEntryType`, `PrecertChainEntry` for `PrecertLogEntryType`
(TLS-CODEC.md §5.4) — again rejecting trailing bytes; any other entry type is
an error. `RawLogEntryFromLeaf` is the same decode with none of the ASN.1
work: it fills `Index`, the parsed `Leaf` and the submitted certificate and
chain, and leaves the parsing of the certificate to `ToLogEntry`, which is
what `LogEntryFromLeaf` calls. Both return a **nil** result and an error when
the leaf's input is malformed or carries an entry type the format does not
define, and `ToLogEntry` may return a usable entry together with a non-nil
error when the certificate parsed only non-fatally, exactly as
`ParseCertificate` does (X509-FORK.md §3.1).

`X509Certificate` parses and returns the certificate from the leaf's
`X509Entry`, and returns an error when the leaf's entry type is not
`X509LogEntryType`. `Precertificate` parses the leaf's `PrecertEntry`
`TBSCertificate` — the log's stored form, already rebuilt, not the submitted
precertificate — and returns an error when the entry type is not
`PrecertLogEntryType`. Both are read-only accessors: neither verifies
anything.

Everything else in the root package is data: the wire structures and enums of
TLS-CODEC.md §5 and the path constants of HTTP-API.md §1.

---

## 4. Log-list packages

Three generations of the public CT log-list JSON, each a separate package, each
overlapping the next for a while.

| package | schema | born | died |
| --- | --- | --- | --- |
| `loglist/` | v1 | **v1.0.13** | **v1.1.4** |
| `loglist2/` | v2 | **v1.1.0** | **v1.2.0** |
| `loglist3/` | v3 | **v1.1.3** | — |

*born* is the first release the directory is present in; *died* is the first
release it is **gone** from, so `loglist/`'s last release is v1.1.3 and
`loglist2/`'s is v1.1.4. RELEASE-MAP.md §14's "exists" table states the same
two lifetimes as inclusive ranges and agrees with this one.

The shared surface, present in all three with the package's own types:

    const LogListURL, LogListSignatureURL, AllLogListURL   // AllLogListURL from v1.1.0
    func NewFromJSON(llData []byte) (*LogList, error)
    func NewFromSignedJSON(llData, rawSig []byte, pubKey crypto.PublicKey) (*LogList, error)
    func (ll *LogList) FindLogByName(name string) []*Log
    func (ll *LogList) FindLogByURL(url string) *Log
    func (ll *LogList) FindLogByKeyHash(keyhash [sha256.Size]byte) *Log
    func (ll *LogList) FindLogByKeyHashPrefix(prefix string) []*Log   // from v1.0.21
    func (ll *LogList) FindLogByKey(key []byte) *Log
    func (ll *LogList) FuzzyFindLog(input string) []*Log

`NewFromJSON` is a plain `encoding/json` unmarshal into the package's own
schema (§4.1, §4.2, §4.3) with no validation beyond what the decoder does.
`NewFromSignedJSON` checks a **detached** signature over those same bytes
first and, if it verifies, returns exactly what `NewFromJSON` would have
returned; if it does not, it returns an error and no list. The scheme is
fixed — nothing in the data selects it:

* the signed message is `llData` **verbatim**, the JSON bytes as served, not a
  canonicalised, re-indented or otherwise re-encoded form;
* the hash is **SHA-256**, always;
* the signature algorithm comes from the **type of `pubKey`**: `RSA` for an
  `*rsa.PublicKey`, `ECDSA` for an `*ecdsa.PublicKey`. Any other key type is
  an error returned before verification is attempted, not a fallback to
  `Anonymous`;
* `rawSig` is the **raw** signature value — a DER `ECDSA-Sig-Value`, or a
  PKCS#1 v1.5 block — exactly the bytes the `.sig` URL serves. It is **not** a
  TLS-encoded `DigitallySigned` (TLS-CODEC.md §1) and carries no algorithm
  bytes of its own; the hash/algorithm pair above is supplied by
  `NewFromSignedJSON` itself, not parsed out of `rawSig`. A `DigitallySigned`
  is the encoding this project uses for the signatures it *defines*
  (TLS-CODEC.md §5.3, §5.5), so the log list is the exception, and that is why
  it is spelled out here.

A wrong key, a key of an unsupported type, a flipped bit, a truncated
signature and a trailing byte after the signature are all the same outcome: an
error, and no `LogList`. A list that verifies but is not valid log-list JSON
fails in `NewFromJSON` afterwards. All three packages do this identically.

Each package's URL constants point at its own schema's path segment under
`https://www.gstatic.com/ct/log_list/`: nothing extra for `loglist`
(`log_list.json`, `log_list.sig`, `all_logs_list.json`), `v2/` for `loglist2`,
`v3/` for `loglist3` — whose four are written out in full in §4.3. The one
wrinkle is `loglist2`'s own history, in §4.2: its segment is `v2beta/` at
v1.1.0 and becomes `v2/` at v1.1.2.

How the finders match is part of the contract:

* `FindLogByName` is a **substring** match on the description, compared
  **case-insensitively**, and returns every log that matches — hence a slice,
  possibly empty. A finder that matches nothing is not an error anywhere in
  the family: the three that return a single log — `FindLogByURL`,
  `FindLogByKeyHash`, `FindLogByKey` — return a **nil** `*Log`, and the
  slice-returning `FindLogByName` and `FindLogByKeyHashPrefix` return an empty
  slice; `FuzzyFindLog`'s nil is the same fact, spelled at the end of its
  bullet below. `FindTiledLogBy…` and `FuzzyFindTiledLog`, over the tiled
  half, answer `nil` the same way (§4.3).
* `FindLogByURL` compares with any trailing `/` stripped from **both** sides,
  so `ct.example.org/aber` and `ct.example.org/aber/` name the same log.
* `FindLogByKeyHash` takes the SHA-256 of the log's DER key;
  `FindLogByKey` takes that DER key itself.
* `FuzzyFindLog` **trims leading and trailing spaces and tabs from the input
  first** — those two characters only, and only at the ends — and everything
  below sees the trimmed string, the name lookup included, so
  `" Wales Secondary "` finds the log that `"Wales Secondary"` finds. It then
  tries that string in this order and returns on the first hit: name, then
  URL, then — decoding it as **base64** — a 32-byte key hash and then a DER
  key, then, after **every internal whitespace character is stripped** as
  well, the same two decoded as **hex**. The end-trim applies to every
  attempt; the internal-whitespace strip applies only to the hex pair. Then,
  lastly, a string of **hex digits whose count is odd** — the one case
  `hex.DecodeString` refuses outright — is read as a **key-hash prefix**:
  every log whose key hash's hex begins with that string is returned, the
  same rule `FindLogByKeyHashPrefix` applies. It returns nil when nothing
  matches.

### 4.1 Schema v1 (`loglist`)

Flat: a `Logs` array and an `Operators` array joined by integer ids.

    type LogList struct {
        Logs      []Log      `json:"logs"`
        Operators []Operator `json:"operators"`
    }
    type Operator struct {
        ID   int    `json:"id"`
        Name string `json:"name"`
    }
    type Log struct {
        Description       string `json:"description"`
        Key               []byte `json:"key"`
        MaximumMergeDelay int    `json:"maximum_merge_delay"`
        OperatedBy        []int  `json:"operated_by"`
        URL               string `json:"url"`
        FinalSTH          *STH   `json:"final_sth,omitempty"`
        DisqualifiedAt    int    `json:"disqualified_at,omitempty"`
        DNSAPIEndpoint    string `json:"dns_api_endpoint,omitempty"`
    }
    type STH struct {
        TreeSize          int    `json:"tree_size"`
        Timestamp         int    `json:"timestamp"`
        SHA256RootHash    []byte `json:"sha256_root_hash"`
        TreeHeadSignature []byte `json:"tree_head_signature"`
    }

`disqualified_at` is in **seconds**; the STH's `timestamp` is in
**milliseconds**, as everywhere else in CT. The two `[]byte` fields are base64
in the JSON, as Go's `encoding/json` does for byte slices.

A log's life-cycle is expressed by two ad-hoc optional fields, `final_sth` and
`disqualified_at`. That is the schema's limitation and the reason for v2.

From **v1.1.0** `loglist` is not only a schema: the package gains
`logfilter.go`, whose `LogList.ActiveLogs` and `LogList.Compatible` filter a
list, the second against `LogRoots` — a `map[string]*ctfe.PEMCertPool` keyed
by log URL. The v2 equivalents are section 4.2's. Through **v1.0.21** there is
no `logfilter.go` and no filtering surface at all: the package is the schema
above, the two constructors and the five finders, and nothing else.

From v1.1.0 that arrival has a build-level consequence. Because `LogRoots` is
typed in terms of `ctfe.PEMCertPool`, importing `loglist` at all pulls
`trillian/ctfe` into the binary, and with it every flag `ctfe` registers on
`flag.CommandLine`. That is why `sctcheck` and `sctscan`, which import
`loglist` for `--log_list`, list flags in their own `--help` that neither
program declares — from **v1.1.2**, `--align_getentries` among them. Up to
v1.0.21 those usage blocks carry only the program's own flags plus the ones
`glog` and the root package register in every binary (CLI.md §2.1).

### 4.2 Schema v2 (`loglist2`)

Nested: operators own their logs, and status becomes a first-class structure.

    type LogList struct { Operators []*Operator `json:"operators"` }
    type Operator struct {
        Name  string   `json:"name"`
        Email []string `json:"email"`
        Logs  []*Log   `json:"logs"`
    }
    type Log struct {
        Description      string            `json:"description,omitempty"`
        LogID            []byte            `json:"log_id"`
        Key              []byte            `json:"key"`
        URL              string            `json:"url"`
        DNS              string            `json:"dns,omitempty"`
        MMD              int32             `json:"mmd"`
        State            *LogStates        `json:"state,omitempty"`
        TemporalInterval *TemporalInterval `json:"temporal_interval,omitempty"`
        Type             string            `json:"log_type,omitempty"`
    }

    type LogStatus int
    const (
        UndefinedLogStatus LogStatus = iota
        PendingLogStatus
        QualifiedLogStatus
        UsableLogStatus
        ReadOnlyLogStatus
        RetiredLogStatus
        RejectedLogStatus
    )

`LogStates` holds one optional state object per status (`pending`,
`qualified`, `usable`, `readonly`, `retired`, `rejected`); exactly one should
be set, and `LogStatus()` reports which. Every state but the read-only one is a
`LogState` carrying just a `Timestamp`; the read-only one is a distinct
`ReadOnlyLogState` that adds

    FinalTreeHead TreeHead  `json:"final_tree_head"`
    type TreeHead struct {
        SHA256RootHash []byte `json:"sha256_root_hash"`
        TreeSize       int64  `json:"tree_size"`
    }

and `TemporalInterval`'s two `time.Time` fields are spelled

    StartInclusive time.Time `json:"start_inclusive"`
    EndExclusive   time.Time `json:"end_exclusive"`

Four methods hang off those types:

    func (ls *LogStates) LogStatus() LogStatus
    func (ls *LogStates) String() string
    func (ls *LogStates) Active() (*LogState, *ReadOnlyLogState)
    func (op *Operator) GoogleOperated() bool

`String()` is `LogStatus().String()`, and `LogStatus` is a `stringer`-generated
enum, so the printable name of a state is the *constant's own identifier* —
`UsableLogStatus`, `ReadOnlyLogStatus`, `UndefinedLogStatus` — not a lower-case
word. `Active()` returns whichever state is set, through the second slot when
that state is the read-only one and through the first otherwise, and
`(nil, nil)` on a nil receiver or an empty union. `GoogleOperated()` is true
when any of the operator's `email` entries contains
`google-ct-logs@googlegroups`; it looks at nothing else.

The URL constants point at `…/log_list/v2beta/…` at v1.1.0 and move to
`…/log_list/v2/…` at **v1.1.2**.

`logfilter.go` adds the selection helpers, and their shapes matter as much as
any other signature here — a filtered list is passed straight into a function
that takes the list **by value**:

    type LogRoots map[string]*x509util.PEMCertPool
    func (ll *LogList) SelectByStatus(lstats []LogStatus) LogList
    func (ll *LogList) TemporallyCompatible(cert *x509.Certificate) LogList
    func (ll *LogList) RootCompatible(certRoot *x509.Certificate, roots LogRoots) LogList
    func (ll *LogList) Compatible(cert *x509.Certificate, certRoot *x509.Certificate, roots LogRoots) LogList

Each of the four has a **pointer receiver** and returns the filtered list as
a `LogList` **value**, never a `*LogList`: what comes back is read with
`.Operators` exactly as the `NewFromJSON` result is. `x509.Certificate` is
the **fork's**, and the pool in `LogRoots` is `ctfe.PEMCertPool` when the
file arrives at v1.1.0 and `x509util.PEMCertPool` from v1.1.3 (§5.3).
`loglist` (v1) carries a subset of these and no more — `ActiveLogs() LogList`
and `Compatible(cert, certRoot, roots)` — while `loglist3` repeats all four
verbatim (§4.3).

### 4.3 Schema v3 (`loglist3`)

v2 plus list-level metadata and operator history:

    type LogList struct {
        Version          string      `json:"version,omitempty"`
        LogListTimestamp time.Time   `json:"log_list_timestamp,omitempty"`
        Operators        []*Operator `json:"operators"`
        IsAllLogs        bool        `json:"is_all_logs,omitempty"`   // v1.2.0
    }
    type PreviousOperator struct {
        Name    string    `json:"name"`
        EndTime time.Time `json:"end_time"`
    }

with `PreviousOperators []*PreviousOperator` added to `Log`.

The package arrives whole at **v1.1.3** — `Version`, `LogListTimestamp`,
`Operators`, `PreviousOperator` and `PreviousOperators` are all there from that
release. Exactly one field is added later and it is **`IsAllLogs`, at v1.2.0**;
it is *not* a v1.3.3 addition, so it is already present in a v1.2.0 tree. What
v1.3.3 adds to this package is the tiled-log surface below.

"v2 plus" is literal: `LogStatus` and its seven constants, `LogStates`,
`LogState`, `ReadOnlyLogState` with its `FinalTreeHead`, `TreeHead`,
`TemporalInterval` with `StartInclusive`/`EndExclusive`, and the four methods
`LogStatus()`, `String()`, `Active()` and `GoogleOperated()` are all carried
over from §4.2 under the same names and with the same behaviour, in this
package's own types. So are the finder family and the `logfilter.go` helpers.

At **v1.3.3** the package gains tiled logs — a different kind of log entirely,
addressed by two URLs instead of one:

    type TiledLog struct {
        Description       string              `json:"description,omitempty"`
        LogID             []byte              `json:"log_id"`
        Key               []byte              `json:"key"`
        SubmissionURL     string              `json:"submission_url"`
        MonitoringURL     string              `json:"monitoring_url"`
        DNS               string              `json:"dns,omitempty"`
        MMD               int32               `json:"mmd"`
        PreviousOperators []*PreviousOperator `json:"previous_operators,omitempty"`
        State             *LogStates          `json:"state,omitempty"`
        TemporalInterval  *TemporalInterval   `json:"temporal_interval,omitempty"`
        Type              string              `json:"log_type,omitempty"`
    }

`Operator` gains a fourth field for them, and it is the one field on that
struct with no `omitempty`, so it serialises even when empty:

    type Operator struct {
        Name      string      `json:"name"`
        Email     []string    `json:"email"`
        Logs      []*Log      `json:"logs"`
        TiledLogs []*TiledLog `json:"tiled_logs"`   // v1.3.3
    }

A full parallel set of finders comes with it (`FindTiledLogByName`, `…ByURL`,
`…ByKeyHash`, `…ByKeyHashPrefix`, `…ByKey`, `FuzzyFindTiledLog`) and a new
constant `AllLogListSignatureURL`. `FindTiledLogByURL` matches **either** URL.

That completes the package's set of four URL constants. The first three are
present from **v1.1.3**, `AllLogListSignatureURL` only from **v1.3.3**, and
their values are:

    LogListURL             = "https://www.gstatic.com/ct/log_list/v3/log_list.json"
    LogListSignatureURL    = "https://www.gstatic.com/ct/log_list/v3/log_list.sig"
    AllLogListURL          = "https://www.gstatic.com/ct/log_list/v3/all_logs_list.json"
    AllLogListSignatureURL = "https://www.gstatic.com/ct/log_list/v3/all_logs_list.sig"

They pair up as list and detached signature over that list: the first two cover
the signed Chrome list, the last two the all-logs list. `AllLogListURL` is what
every flag *named* `log_list` defaults to; `LogListURL`'s value is what the
submission server's `--loglist_path` and the witness tools' `--log_list_url`
default to. See CLI.md §3.2, §4.9 and §4.10 for which programs take which, and
what the `loglist`-era values were.

The three `logfilter.go` selectors gain a tiled half at the same release, and
it is the same predicate applied twice. `SelectByStatus`,
`TemporallyCompatible` and `RootCompatible` each build a filtered `TiledLogs`
slice beside the filtered `Logs` slice, keep the operator when **either** slice
ends up non-empty, and drop it only when both are empty; the rest of the
operator record is copied through unchanged. What each filters on:

* `SelectByStatus(lstats)` keeps an entry whose `State.LogStatus()` is any of
  the statuses given, in either half.
* `TemporallyCompatible(cert)` is an empty list for a nil `cert`. Otherwise an
  entry with no `temporal_interval` is always kept, and one that has an
  interval is kept when the certificate's `NotAfter` is before `EndExclusive`
  and at or after `StartInclusive`. A tiled log carries the same
  `TemporalInterval` field and is judged by the same rule.
* `RootCompatible(certRoot, roots)` is an empty list when `certRoot` is non-nil
  and is not a CA. Otherwise it looks each entry up in the `LogRoots` map — an
  ordinary log under its `URL`, **a tiled log under its `SubmissionURL`** — and
  an entry that is *absent* from the map is treated as compatible and kept, on
  the grounds that nothing is known about its roots; an entry that is present
  is kept only when its pool contains `certRoot`. A tiled log's `MonitoringURL`
  is never a key, so a pool registered under it leaves that log looking unknown
  and therefore kept. This is the one place in the package where a tiled log's
  two URLs are not interchangeable — contrast `FindTiledLogByURL`, which
  matches either.
* `Compatible(cert, certRoot, roots)` is `TemporallyCompatible(cert)` followed
  by `RootCompatible(certRoot, roots)` over its result — plainly composed, with
  no shortcut for a nil `certRoot`: the second step still runs, and a nil
  `certRoot` therefore keeps only the entries whose roots are *unknown* — an
  entry with a known pool is dropped, since no pool can be shown to include a
  certificate that is not there.

### 4.4 The removals

At **v1.1.4** `loglist/` is deleted and every consumer of either older schema
moves to `loglist3`: the v1 consumers `ctutil`, `ctclient`, `sctcheck`,
`sctscan` and `ct_hammer`, and the v2 consumers `ctpolicy`, `submission` and
the witness `feeder` and `config` tools (CLI.md §4.10). The witness `client`
tool was already on `loglist3` at v1.1.3 and so does not move. `findlog` is
deleted outright because it was the v1-only tool. At **v1.2.0** `loglist2/` is deleted too, leaving exactly one log-list
package.

---

## 5. `scanner/`, `x509util/`, `fixchain/`, `preload/`, `logid/`

### 5.1 `scanner`

    type Matcher interface{ … }
    type MatchAll struct{}
    type MatchNone struct{}
    type MatchSerialNumber struct{ … }
    type MatchSubjectRegex struct{ … }
    type MatchIssuerRegex struct{ … }

    type ScannerOptions struct{ … }              // shape changes at v1.0.17, below
    func DefaultScannerOptions() *ScannerOptions
    type Scanner struct{ … }
    func NewScanner(client *client.LogClient, opts ScannerOptions) *Scanner
    func (s *Scanner) Scan(ctx context.Context, foundCert, foundPrecert func(*ct.RawLogEntry)) error

The scan callbacks take `*ct.LogEntry` early on and `*ct.RawLogEntry` by
v1.3.3, and the matcher set grows `MatchSCTTimestamp`, `LeafMatcher`,
`CertParseFailMatcher` and `CertVerifyFailMatcher` — the last two are what let
`scanlog --parse_errors` find certificates the parser chokes on.

#### The fetcher, from v1.0.17

At **v1.0.17** the package gains `fetcher.go`, factoring the "walk the log in
batches" loop out of the scanner so other tools can reuse it. There is no
`scanner/fetcher.go` at v1.0.13 and none of these names resolves there.

    type FetcherOptions struct {
        BatchSize     int     // entries requested in one get-entries call
        ParallelFetch int     // concurrent fetch workers
        StartIndex    int64   // first index to fetch, inclusive
        EndIndex      int64   // one past the last index to fetch; 0 means "to the STH"
        Quiet         bool    // suppress the progress messages
    }
    func DefaultFetcherOptions() *FetcherOptions

    type EntryBatch struct {
        Start   int64          // log index of the first entry in this batch
        Entries []ct.LeafEntry // the entries of the range, in index order
    }

    type Fetcher struct{ … }
    func NewFetcher(client *client.LogClient, opts *FetcherOptions) *Fetcher   // client becomes the LogClient interface at v1.1.2
    func (f *Fetcher) Prepare(ctx context.Context) (*ct.SignedTreeHead, error)
    func (f *Fetcher) Run(ctx context.Context, fn func(EntryBatch)) error
    func (f *Fetcher) Stop()

`DefaultFetcherOptions()` returns `BatchSize` **1000**, `ParallelFetch` **1**,
`StartIndex` **0**, `EndIndex` **0**, `Quiet` **false**.

At **v1.0.19** the struct's fifth field is **replaced**: `Quiet` is removed and
`Continuous bool` takes its place. This is a one-for-one swap, and it is the
reason `scanlog` loses its `--quiet` flag at the same release (CLI.md §4.1);
from v1.0.19 there is no `Quiet` anywhere in `scanner`, so naming it does not
compile, and `DefaultFetcherOptions()` returns the same four numbers with
`Continuous` **false**.

    // v1.0.19 onwards
    type FetcherOptions struct {
        BatchSize     int
        ParallelFetch int
        StartIndex    int64
        EndIndex      int64
        Continuous    bool   // keep fetching after EndIndex is reached
    }

`Continuous` turns a bounded walk into one that follows the log. With it
**false** — the default, and the only behaviour before v1.0.19 — `Run` walks
`[StartIndex, EndIndex)` once and returns. With it **true**, reaching the end
of the range is not the end of the run: the fetcher re-reads the log's STH,
assigns the new `sth.TreeSize` to `EndIndex`, and carries on from where it
stopped, so a caller sees consecutive, non-overlapping batches as the tree
grows. It waits, with backoff, for an STH strictly larger than the size it
last saw, and it never re-fetches an index it has already delivered. Such a
run has no natural end and is stopped by cancelling the context passed to
`Run` (or by `Stop`).

The range is **half-open**: `[StartIndex, EndIndex)`. `EndIndex == 0` is the
"no bound given" spelling, not an empty range.

`Prepare` fetches the log's STH, caches it in the fetcher, and **clamps the
caller's options in place**: it assigns `opts.EndIndex = int64(sth.TreeSize)`
whenever `EndIndex` is `0` or larger than the tree. The options struct the
caller passed to `NewFetcher` is mutated — the clamp is observable through the
caller's own `*FetcherOptions`, which is how a caller learns what range will
actually be walked. `Run` calls `Prepare` itself if it has not already been
called.

From **v1.0.19** that cache is also a short-circuit: a second `Prepare` on the
same fetcher returns the STH it is already holding and issues **no further
`get-sth`**, so two `Prepare` calls cost the log one request and both hand back
the same tree size. At v1.0.17 the STH is stored but not reused, and every
`Prepare` queries the log again.

`Run` splits `[StartIndex, EndIndex)` into consecutive ranges of at most
`BatchSize`, requesting each with one `get-entries` call whose `start`/`end`
are **inclusive** (HTTP-API.md §3), and invokes the callback once per batch
with that batch's first log index in `Start`. A final batch is short when
`EndIndex` cuts it, not only when the tree runs out. With `ParallelFetch` 1 the
batches are delivered in index order.

`ScannerOptions` is rebuilt at **v1.0.17** to **embed** `FetcherOptions`
rather than repeat its fields, so `BatchSize`, `ParallelFetch`, `StartIndex`,
`EndIndex` and the fifth field are reached by promotion and `.FetcherOptions`
names the embedded value. The fifth field is `Quiet` at v1.0.17 and
`Continuous` from v1.0.19, so the scanner's own struct gains and loses those
names with the fetcher's and never declares either itself:

    // v1.0.17 onwards
    type ScannerOptions struct {
        FetcherOptions
        Matcher     interface{}
        PrecertOnly bool
        NumWorkers  int
        BufferSize  int
    }

`DefaultScannerOptions()` returns `*ScannerOptions` at every release; from
v1.0.17 it embeds `*DefaultFetcherOptions()`, sets `Matcher` to `&MatchAll{}`,
`PrecertOnly` false and `NumWorkers` **1**, and leaves `BufferSize` zero.
Before v1.0.17 `ScannerOptions` declared `BatchSize`, `NumWorkers`,
`ParallelFetch`, `StartIndex` and `Quiet` as its own fields and had no
`EndIndex` at all — a v1.0.13 scanner can say where to start and never where
to stop.

### 5.2 `x509util`

    func NameToString(name pkix.Name) string
    func CertificateToString(cert *x509.Certificate) string
    func CertificateFromPEM(pemBytes []byte) (*x509.Certificate, error)
    func CertificatesFromPEM(pemBytes []byte) ([]*x509.Certificate, error)
    func OIDForStandardExtension(oid asn1.ObjectIdentifier) bool
    func OIDInExtensions(oid asn1.ObjectIdentifier, extensions []pkix.Extension) (int, bool)
    func ParseSCTsFromSCTList(sctList *x509.SignedCertificateTimestampList) ([]*ct.SignedCertificateTimestamp, error)   // from v1.0.13
    func ParseSCTsFromCertificate(cert []byte) ([]*ct.SignedCertificateTimestamp, error)                               // from v1.0.13
    func ExtractSCT(sctData *x509.SerializedSCT) (*ct.SignedCertificateTimestamp, error)                               // from v1.0.13
    func MarshalSCTsIntoSCTList(scts []*ct.SignedCertificateTimestamp) (*x509.SignedCertificateTimestampList, error)   // from v1.1.0
    func ReadPossiblePEMFile(filename, blockname string) ([][]byte, error)
    func ReadPossiblePEMURL(target, blockname string) ([][]byte, error)
    func ReadFileOrURL(target string, client *http.Client) ([]byte, error)
    func GetIssuer(cert *x509.Certificate, client *http.Client) (*x509.Certificate, error)
    func CRLToString(crl *x509.CertificateList) string
    func RevocationReasonToString(reason x509.RevocationReasonCode) string
    func GeneralNamesToString(names *x509.GeneralNames) string   // from v1.0.5
    func OtherNameToString(name x509.OtherName) string           // from v1.0.5

Two changes worth knowing: `CertificateFromPEM` takes a **`string`** at v1.0
and a **`[]byte`** later, and the `Oid…` prefix is capitalised to `OID…`.

The two PEM readers are deliberately **not** equally strict, and the
difference is contractual:

* `CertificateFromPEM` decodes exactly one block and **rejects trailing
  bytes** after it; it also rejects a block that is not a certificate and one
  whose DER will not parse.
* `CertificatesFromPEM` walks a chain in order and **stops silently** as soon
  as the remaining input holds no further PEM block — trailing non-PEM text is
  not an error, and input with no block at all yields an empty slice and no
  error. A block that *is* present but is not a parsable certificate is still
  an error.

**The three SCT extraction helpers, from v1.0.13.** They are not in the
package at v1.0, v1.0.5 or v1.0.10 — the `x509` carrier types they read arrive
three releases earlier than the helpers do (X509-FORK.md §4.5), so before
v1.0.13 a caller decodes a `SerializedSCT.Val` through the TLS codec itself
(TLS-CODEC.md §5.5). From v1.0.13:
`ParseSCTsFromCertificate` takes the certificate as **either PEM or DER** and
returns an empty slice for a certificate carrying no SCT-list extension; input
that is not a certificate at all is an error. `ParseSCTsFromSCTList` walks
`SCTList.SCTList` in order and is **all-or-nothing**: one entry that will not
decode fails the whole call, and no partial slice is returned. `ExtractSCT`
does one entry and rejects a nil argument and a truncated one.

`ReadFileOrURL` fetches `target` over HTTP(S) when it parses as a URL with one
of those two schemes and otherwise reads it as a local file — so a missing
file is an error and a non-`http(s)` scheme is treated as a filename. The
bytes come back verbatim; no PEM decoding happens. `GetIssuer` looks at the
certificate's Authority Information Access issuer URLs: with **none** it
returns `(nil, nil)` — not an error — and otherwise it fetches the **first**
URL only, failing on a non-200 response, on a body that does not parse as a
certificate, and on an unreachable host.

**`CertificateToString` and the two RFC 6962 extensions, from v1.0.10.**
Before v1.0.10 the poison and SCT-list extensions fall through to the
unhandled-extension dump, printed as a bare OID and a hex blob. From v1.0.10
`CertificateToString` renders each of them itself, and they no longer appear
in the unhandled dump. The rendering is fixed and is unchanged through
v1.3.3; indentation is in spaces and is significant:

    <12sp>RFC6962 Pre-Certificate Poison:< critical if the extension is critical>
    <16sp>.....

    <12sp>RFC6962 Certificate Transparency SCT:< critical if critical>
    <14sp>SCT [<i>]:
    <18sp>Version: <decimal SCTVersion>
    <18sp>LogID: <standard base64 of the 32-byte key id>
    <18sp>Timestamp: <decimal milliseconds>
    <18sp>Signature: {<hash algorithm> <signature algorithm>}
    <18sp>Signature:
    <20sp><hex dump of the signature>

Two lines begin `Signature:` and they are different things. The first carries
the SCT's **whole** `tls.SignatureAndHashAlgorithm` — both names, the hash
first, separated by one space and wrapped in braces, which is Go's default
rendering of that two-field struct under `%s`. An SCT signed with SHA-256 and
ECDSA therefore prints `Signature: {SHA256 ECDSA}`, not `Signature: SHA256`.
The second is a bare label introducing the hex dump of the signature bytes on
the following lines.

The ` critical` suffix is written only for a critical extension, and a newline
always follows the title line either way. One `SCT [i]:` block appears per
entry of `cert.SCTList.SCTList`, numbered from 0 in list order; an entry whose
`Val` will not TLS-decode is dumped as hex at 18 spaces instead of showing the
five fields. Hex dumps are `%02x:` per byte — the trailing colon is always
written, including on the last byte — **16 bytes per line**, each line
prefixed by its indent, with a newline between lines and one closing newline
after the last.

**The two "possible PEM" readers, from v1.0.5.** Both return a slice of DER
blobs and take the PEM block name to look for. The decision is made on the
**content**, not the extension: if the bytes contain the literal text
`BEGIN <blockname>` the input is walked as PEM and every block whose type equals
`blockname` is collected in file order, with blocks of any other type skipped;
otherwise the bytes are returned **whole, as one element**, which is how a DER
file is handled. A file that cannot be read is an error
(`%s: failed to read data: %v`). `ReadPossiblePEMURL` differs only in where the
bytes come from: a target beginning `http://` or `https://` is fetched, and
**anything else is treated as a filename** and handed to `ReadPossiblePEMFile`.

**`OIDInExtensions`, from v1.0.5.** It returns `(count, anyCritical)`: how many
extensions in the slice carry that OID, and whether **any** of those carried it
with the critical bit set. A count of 0 comes back with `false`. It does not
stop at the first match, so a duplicated OID is reported as a count above one
rather than as an error.

**`GeneralNamesToString` and `OtherNameToString`, from v1.0.5.** Both are fixed
renderings. `OtherNameToString` is the OID's dotted form, `=`, then the
**lower-case hex** of `Value.Bytes` with no separators:
`1.3.6.1.4.1.11129.2.4.3=dead`.

`GeneralNamesToString` walks the seven `GeneralNames` fields in **this order** —
`DNSNames`, `EmailAddresses`, `DirectoryNames`, `URIs`, `IPNets`,
`RegisteredIDs`, `OtherNames` — emitting each entry in its own slice order, and
joins the whole list with **`", "`** (comma, space). Empty fields contribute
nothing, so the separator never appears at either end and an empty
`GeneralNames` renders as the empty string. The per-entry prefixes are:

    DNS:<name>
    email:<address>
    DirName:<NameToString of the pkix.Name>
    URI:<uri>
    IP Address:<ip>            (when the IPNet has a nil Mask)
    IP Address:<ip>/<mask>     (otherwise; the mask is Go's IPMask.String,
                                i.e. lower-case hex, so a /16 is ffff0000)
    Registered ID:<dotted OID>
    othername:<OtherNameToString of the entry>

#### When each of these arrives

The listing above is the package at its widest. Only six of the functions are
there at v1.0, and the two `Oid…` spellings in that first set are renamed one
release later, so a step must check this column before naming any of them:

| function | first release | note |
| --- | --- | --- |
| `NameToString`, `CertificateToString` | **v1.0** | |
| `CertificateFromPEM` | **v1.0** | `string` argument v1.0 .. v1.0.10, `[]byte` from v1.0.13 |
| `OidFromNamedCurve`, `OidForStandardExtension`, `OidInExtensions` | **v1.0** only | see below |
| `OIDForStandardExtension`, `OIDInExtensions` | **v1.0.5** | the `Oid…` spellings are gone from v1.0.5 |
| `ReadPossiblePEMFile`, `ReadPossiblePEMURL`, `CRLToString` | **v1.0.5** | |
| `GeneralNamesToString`, `OtherNameToString` | **v1.0.5** | |
| `RevocationReasonToString` | **v1.0.10** | |
| `CertificatesFromPEM`, `ReadFileOrURL`, `GetIssuer` | **v1.0.13** | |
| `ParseSCTsFromSCTList`, `ParseSCTsFromCertificate`, `ExtractSCT` | **v1.0.13** | the SCT extraction helpers |
| `MarshalSCTsIntoSCTList` | **v1.1.0** | |

`OidFromNamedCurve` is not renamed but **removed**: it is in the package at
v1.0 and gone from v1.0.5 onwards, with no `OIDFromNamedCurve` replacing it.

So at **v1.0** the whole of `x509util` is exactly six exported functions —
`NameToString`, `CertificateToString`, `CertificateFromPEM`,
`OidFromNamedCurve`, `OidForStandardExtension` and `OidInExtensions`: no PEM
chain reader, no URL reader, no SCT extraction, no CRL rendering.

### 5.3 `PEMCertPool` and where it lives

    type PEMCertPool struct{ … }
    func NewPEMCertPool() *PEMCertPool
    func (p *PEMCertPool) AddCert(cert *x509.Certificate)
    func (p *PEMCertPool) Included(cert *x509.Certificate) bool
    func (p *PEMCertPool) AppendCertsFromPEM(pemCerts []byte) (ok bool)
    func (p *PEMCertPool) AppendCertsFromPEMFile(pemFile string) error
    func (p *PEMCertPool) Subjects() (res [][]byte)
    func (p *PEMCertPool) CertPool() *x509.CertPool
    func (p *PEMCertPool) RawCertificates() []*x509.Certificate

`Included` is the one method that is not there from the start: it arrives at
**v1.1.0** and is present at every later release; the other seven are the
type's surface from v1.0. The type lives in **`trillian/ctfe`** up to v1.1.2
and **moves to `x509util` at v1.1.3**, unchanged in content. After the move
`ctfe.PEMCertPool` does not exist, and `CertValidationOpts`'s root pool field
changes type to match.

**What the methods do, at every release.** The type wraps an `x509.CertPool`
and additionally keeps the certificates themselves, because `get-roots` is
answered out of the pool and needs them (HTTP-API.md §4.6).

* A certificate is identified by the **SHA-256 of its DER**, `cert.Raw`.
  `AddCert` ignores a certificate whose fingerprint the pool already holds, so
  a pool never holds the same certificate twice: calling it twice with one
  certificate leaves the pool one longer than it was, not two.
* `RawCertificates` returns the certificates the pool holds, in the order they
  were added, each still the certificate that was parsed; a fresh pool holds
  none. This is the list `get-roots` re-encodes as base64 DER.
* `AppendCertsFromPEM` walks the input with a PEM decoder and **skips every
  block that is not a `CERTIFICATE`** — a block of another type, or one
  carrying headers of its own, is passed over rather than refused. It reports
  `true` only when it found at least one certificate **and** every certificate
  block it found parsed; the first block that will not parse ends the walk
  there, with `false`, and whatever earlier blocks added stays in the pool. The
  test is `err != nil` through **v1.0.19** and the fork's `IsFatal` from
  **v1.0.21**, so from that release a certificate the fork still parses with
  recorded non-fatal errors is accepted rather than refused. This is stricter
  than `x509.CertPool`'s method of the same name,
  which ignores a bad block as long as one good one loaded.
* `AppendCertsFromPEMFile` reads the same bytes from a path and turns the
  byte-slice form's verdict into an error: a read error comes back as an error,
  and so does a file holding no certificate or one whose certificate blocks do
  not all parse. This is the call a log's configured `roots_pem_file` list is
  loaded through, once per file, while the instance is built (CONFIG.md §2.4).
* `Subjects` returns the **DER-encoded raw subject** of every certificate in
  the pool — `RawSubject`, not a rendered `Subject` — one per member, in pool
  order. (`get-roots` does not use it: that handler re-encodes the `Raw` bytes
  of `RawCertificates` as base64 DER, HTTP-API.md §4.6.)
* `Included` answers from the same fingerprint index: whether this certificate
  is in the pool, not whether it chains to one.
* `CertPool` hands out the wrapped `*x509.CertPool`, which is what chain
  building uses, so a certificate added through `AddCert` is a trust anchor
  for it.

### 5.4 `fixchain`

Reconstructs a chain that a log will accept from one that it will not, by
fetching missing intermediates. Surface is essentially frozen across all 13
releases:

    func Fix(cert *x509.Certificate, chain []*x509.Certificate, roots *x509.CertPool, client *http.Client) ([][]*x509.Certificate, []*FixError)
    type FixError struct{ … }
    func (e FixError) TypeString() string
    func (e FixError) MarshalJSON() ([]byte, error)
    func UnmarshalJSON(b []byte) (*FixError, error)
    type Fixer struct{ … }
    func NewFixer(workerCount int, chains chan<- []*x509.Certificate, errors chan<- *FixError, client *http.Client, logStats bool) *Fixer
    type Logger struct{ … }
    func NewLogger(ctx context.Context, workerCount int, errors chan<- *FixError, client client.AddLogClient, limiter Limiter, logStats bool) *Logger
    type Limiter interface{ Wait() }
    type FixAndLog struct{ … }
    func NewFixAndLog(ctx context.Context, fixerWorkerCount, loggerWorkerCount int, errors chan<- *FixError, client *http.Client, logClient client.AddLogClient, limiter Limiter, logStats bool) *FixAndLog

### 5.5 `preload` and `logid`

`preload` exports exactly one type:

    type AddedCert struct {
        CertDER                    ct.ASN1Cert
        SignedCertificateTimestamp ct.SignedCertificateTimestamp
        AddedOk                    bool
        ErrorMessage               string
    }

`logid` (from **v1.0.5**) is a tiny package whose whole job is to stop people
confusing a log id with a tree id:

    type LogID [sha256.Size]byte
    func FromBytes(bytes []byte) (LogID, error)
    func FromB64(logIDB64 string) (LogID, error)
    func FromB64OrDie(logIDB64 string) LogID
    func FromPubKeyB64(pubKeyB64 string) (LogID, error)
    func FromPubKeyB64OrDie(pubKeyB64 string) LogID
    func (l LogID) Bytes() []byte
    func (l LogID) String() string

`FromBytes` rejects any input that is not exactly 32 bytes. `String()` returns the
string `logid:[<b64>]` where `<b64>` is the standard base64 encoding of the 32-byte id.
The two `…OrDie` forms are the same two conversions with the error turned into a
fatal log: on well-formed input each returns exactly what the checked form
returns, and on anything else it terminates the process rather than reporting.
This is unchanged for the package's whole life (v1.0.5 onward).

---

## 6. Packages with a finite life

### 6.1 `dnsclient/` (v1.0.13 .. v1.1.0)

An experimental CT-over-DNS client: the same read-side methods as
`LogClient`, satisfying `CheckLogClient`, but answering from TXT records
instead of HTTPS, so an auditor can query a log without an attributable HTTP
connection.

    type DNSClient struct {
        Verifier *ct.SignatureVerifier   // nil for no verification
        // plus the unexported base name and resolver
    }

    func New(base string, opts jsonclient.Options) (*DNSClient, error)
    func NewForNameServer(base string, opts jsonclient.Options, ns string) (*DNSClient, error)   // from v1.1.0
    func (c *DNSClient) BaseURI() string     // returns "dns:<base>"; from v1.0.17
    func (c *DNSClient) GetSTH(ctx context.Context) (*ct.SignedTreeHead, error)
    func (c *DNSClient) GetSTHConsistency(ctx context.Context, first, second uint64) ([][]byte, error)
    func (c *DNSClient) GetProofByHash(ctx context.Context, hash []byte, treeSize uint64) (*ct.GetProofByHashResponse, error)

`New` appends a trailing dot to `base` if it has none, and parses
`opts.PublicKeyDER`/`PublicKey` if either is set: with a usable key the
exported `Verifier` field is non-nil and every answer's signature is checked;
with **no** key at all `Verifier` is left **nil** and signatures are not
checked, which is legal. A key that will not parse, or one RFC 6962 forbids
(DSA, short RSA, off-curve ECDSA — §3), is an error from the constructor.

The resolver is unexported and is fixed to `net.LookupTXT` by `New`. Until
`NewForNameServer` arrives at v1.1.0 there is no way for an importer to point
a `DNSClient` at a server of its own, so the three read methods above cannot
be exercised without live DNS.

Query shapes (TXT in every case, with a trailing dot appended to `base`). Both
proof queries are paged: the name carries a leading `<index>.` counter that
starts at 0 and advances by one per hash returned, and paging stops when a
lookup fails or comes back empty:

| query | name | answer |
| --- | --- | --- |
| STH | `sth.<base>` | one record matching `<size>.<timestamp>.<b64 root hash>.<b64 DigitallySigned>` |
| consistency proof | `<index>.<first>.<second>.sth-consistency.<base>`, index 0..255 | concatenated 32-byte hashes |
| leaf index by hash | `<base32 of hash, '=' stripped>.hash.<base>` | a decimal index |
| inclusion proof | `<index>.<leafIndex>.<treeSize>.tree.<base>`, index 0..255 | concatenated 32-byte hashes |

The whole package, the three DNS `LogInfo` constructors, and the `--dns*`
flags on `ctclient`, `sctcheck` and `sctscan` are **deleted together at
v1.1.2**.

### 6.2 `gossip/`

`gossip/gossip_server` exists from v1.0. At **v1.0.21** a much larger
`gossip/minimal/` tree appears (`gosmin`, `goshawk`, a config proto; MySQL
state follows at v1.1.0) and at **v1.1.2** all of it is deleted **except** one
file,
`gossip/minimal/x509ext/x509ext.go`, which survives to v1.3.3:

    var OIDExtensionCTSTH             = asn1.ObjectIdentifier{1, 3, 6, 1, 4, 1, 11129, 2, 4, 5}
    var OIDExtKeyUsageCTMinimalGossip = asn1.ObjectIdentifier{1, 3, 6, 1, 4, 1, 11129, 2, 4, 6}

    type LogSTHInfo struct {
        LogURL            []byte   `tls:"maxlen:255"`
        Version           tls.Enum `tls:"maxval:255"`
        TreeSize          uint64
        Timestamp         uint64
        SHA256RootHash    ct.SHA256Hash
        TreeHeadSignature ct.DigitallySigned
    }
    func LogSTHInfoFromCert(cert *x509.Certificate) (*LogSTHInfo, error)
    func HasSTHInfo(cert *x509.Certificate) bool
    func STHFromCert(cert *x509.Certificate) (*ct.SignedTreeHead, error)

It encodes an STH inside an X.509 extension so that a certificate can carry
one log's view of itself to a verifier who can compare it with their own —
gossip, reduced to a data format.

### 6.3 `internal/witness/` (v1.1.3 .. v1.2.0)

A small HTTP service that remembers the newest STH it has seen for each of a
configured set of logs, in a SQLite database, and co-signs it. A client pushes
a newer STH plus a consistency proof; the witness verifies consistency against
what it holds, stores the new head and returns a co-signed STH. That is a
cheap way to detect a split view.

    const HTTPGetSTH  = "/ctwitness/v0/logs/%s/sth"
    const HTTPUpdate  = "/ctwitness/v0/logs/%s/update"
    const HTTPGetLogs = "/ctwitness/v0/logs"

    type UpdateRequest struct { STH []byte; Proof [][]byte }
    type CosignedSTH struct { /* STH fields */ WitnessSigs []ct.DigitallySigned `json:"witness_signatures"` }

    var ErrSTHTooOld = errors.New("STH too old")
    func NewWitnessVerifier(pk crypto.PublicKey) (*WitnessVerifier, error)
    func (wv WitnessVerifier) VerifySignature(sth api.CosignedSTH) error

**Starting one.** The server takes its four flags from CLI.md §4.10 and needs
two of them to be right before it will serve anything:

* `--config_file` is the YAML of CLI.md §5.1 — one `Logs:` sequence of
  `PubKey:` entries, each the standard-base64 PKIX SubjectPublicKeyInfo DER of
  a log it will follow. The keys are capitalised exactly so.
* `--private_key` is the **PEM text of the witness's own PKCS#8 ECDSA key**,
  passed as the flag value itself rather than as the name of a file (CLI.md
  §4.10). A witness given a path instead finds no PEM block and exits before
  it listens.

A log is addressed throughout by the ID derived from its configured key,

    logID = base64( SHA-256( PKIX DER ) )

which is standard base64 and therefore may contain `/`, so the ID is
path-escaped into a URL and path-unescaped out of it again.

**What it answers.** The three paths above, `GET`, `PUT`, `GET` in the order
listed, and nothing else — another method on a routed path is **405** with an
empty body, and an unrouted path is the router's own plain **404**.

| condition | status | body |
| --- | --- | --- |
| update accepted (first STH for a log, or a consistent larger one) | 200 | the co-signed STH |
| update identical to the one already held | 200 | **the stored STH raw, exactly as submitted — not co-signed** |
| `GET` of an STH for a log with nothing stored, followed or not | 404 | a diagnosis |
| `PUT` naming a log the witness does not follow | **500**, not 404 | a diagnosis |
| request body that is not an `UpdateRequest` | 400 | a diagnosis |
| STH that does not parse, is signed by the wrong key, or names a different log than the URL | 500 | a diagnosis |
| STH well formed but irreconcilable — smaller, same size with a different root, or growth whose consistency proof does not verify | 409 | **the stored STH raw**, so the caller can re-aim |

An STH submitted with an **empty `log_id`** is taken to name the log in the
URL: the witness fills the field in from the path before it stores or co-signs
anything, so the co-signed body carries that log's ID even though the
submission did not. A submission carrying a `log_id` that names some *other*
log is the 500 row above.

The 500 on an unfollowed log is not a considered choice: only a failed
precondition is mapped to a status of its own, so everything else that is not
a `GET` miss falls through to an internal error. The *bodies* are prose
assembled from whatever error was raised — including the wrapped gRPC status
text and, for a malformed body, `encoding/json`'s own message — and are not an
interface anything should depend on.

**What it signs.** The co-signature in `WitnessSigs` is over

    tls.Marshal(ct.SignedTreeHead{ … as returned … })

— the whole structure in the presentation-language codec, not the
`TreeHeadSignature` pre-image the log itself signs — hashed with SHA-256 and
signed with the witness's key. There is exactly one entry in `WitnessSigs`.
`WitnessVerifier.VerifySignature` checks that, and accepts an STH only if one
of the signatures present verifies.

The whole tree is **removed at v1.3.3**.

---

## 7. `ctpolicy/`, `submission/`, `schedule/`, `watcher/` — from v1.1.0

### 7.1 `ctpolicy`

Given a certificate and an approved log list, decide which logs a submitter
must hit and how many SCTs each group must yield.

    type CTPolicy interface {
        LogsByGroup(cert *x509.Certificate, approved *loglistN.LogList) (LogPolicyData, error)
        Name() string
    }
    type ChromeCTPolicy struct{}
    type AppleCTPolicy struct{}
    type LogGroupInfo struct {
        Name          string
        LogURLs       map[string]bool    // the group's members, keyed by log URL
        MinInclusions int                // how many SCTs this group must yield
        IsBase        bool               // true only for the all-logs group
        LogWeights    map[string]float32 // submission weights; the default is 1
    }
    func (group *LogGroupInfo) SetLogWeights(weights map[string]float32) error
    func (group *LogGroupInfo) SetLogWeight(logURL string, w float32) error
    func (group *LogGroupInfo) GetSubmissionSession() []string
    type LogPolicyData map[string]*LogGroupInfo
    func (p LogPolicyData) TotalLogs() int
    const BaseName = "All-logs"
    func BaseGroupFor(approved *loglistN.LogList, incCount int) (*LogGroupInfo, error)
    type GroupSet map[string]bool
    func GroupByLogs(lg LogPolicyData) map[string]GroupSet

`loglistN` is `loglist2` at v1.1.0 .. v1.1.3 and `loglist3` from v1.1.4.

**The group names are data, not prose.** `LogsByGroup` returns a map keyed by
`LogGroupInfo.Name`, so the names are part of the interface and a caller looks
groups up by them. They are the same string at every release from v1.1.0:

| policy | `Name()` | groups it returns | `MinInclusions` | `IsBase` |
| --- | --- | --- | --- | --- |
| `ChromeCTPolicy` | `Chrome` | `All-logs`, `Google-operated`, `Non-Google-operated` | the step function below for `All-logs`; **1** for each of the other two | true only for `All-logs` |
| `AppleCTPolicy` | `Apple` | `All-logs` only | the step function below | true |

`BaseName` is the constant holding `All-logs`, and `BaseGroupFor(ll, n)` builds
that group: every log in `ll`, `IsBase` true, `MinInclusions` `n`. It returns a
**nil** `*LogGroupInfo` and an error when `n` exceeds the number of logs in the
list — an under-sized list is refused, not silently clamped. `Google-operated`
holds the logs of the operators for which `Operator.GoogleOperated()` is true
and `Non-Google-operated` holds exactly the rest, so the two partition the list
and `TotalLogs()` counts the distinct logs across all groups.

`populate` fills both `LogURLs` and `LogWeights` in one pass, so a freshly
returned group has a weight of **1.0** for every member and `len(LogWeights) ==
len(LogURLs)`.

**The inclusion-count step function.** `incCount` is derived from the
certificate's lifetime in whole months — `(endYear-startYear)*12 +
(endMonth-startMonth)`, decremented when `NotAfter`'s day-of-month is before
`NotBefore`'s, so "fifteen months less a day" is 14:

| lifetime in months | `MinInclusions` for the base group |
| --- | --- |
| `< 15` | 2 |
| `<= 27` | 3 |
| `<= 39` | 4 |
| otherwise | 5 |

Both policies use the same table; it is unchanged v1.1.0 .. v1.3.3.

**The weighting surface.** `SetLogWeights` **resets** every member's weight to
0 and then applies the supplied map, ignoring URLs that are not members, so the
map keeps its original length and non-listed members end at 0. `SetLogWeight`
sets one member's weight without resetting the others. Both refuse, with an
error and no change, a negative weight, and a weight that would leave fewer
positive-weighted members than `MinInclusions`; `SetLogWeight` also refuses a
URL that is not a member of the group. `GetSubmissionSession` returns the
member URLs with a positive weight, in weighted-random order, and an empty
(non-nil) slice for an empty group.

### 7.2 `submission`

An SCT-getting proxy: keep a refreshed log list, build a client per log, race
`add-chain` across the policy's groups until enough SCTs come back, serve the
result over HTTP.

    type Distributor struct{ … }
    func NewDistributor(ll *loglistN.LogList, plc ctpolicy.CTPolicy, lcBuilder LogClientBuilder, mf monitoring.MetricFactory) (*Distributor, error)
    func (d *Distributor) RefreshRoots(ctx context.Context) map[string]error
    // loadPendingLogs narrows the set of logs a submission goes to. With it
    // **false**, only logs whose state is *usable* are submitted to; with it
    // **true**, the list is not narrowed to usable logs, so a pending log is
    // eligible too. The flag names the difference; it does not reorder or
    // filter anything else.
    func (d *Distributor) AddChain(ctx context.Context, rawChain [][]byte, loadPendingLogs bool) ([]*AssignedSCT, error)
    func (d *Distributor) AddPreChain(ctx context.Context, rawChain [][]byte, loadPendingLogs bool) ([]*AssignedSCT, error)
    func (d *Distributor) SubmitToLog(ctx context.Context, logURL string, chain []ct.ASN1Cert, asPreChain bool) (*ct.SignedCertificateTimestamp, error)
    type LogClientBuilder func(*loglistN.Log) (client.AddLogClient, error)
    func BuildLogClient(log *loglistN.Log) (client.AddLogClient, error)

    type LogListManager struct{ … }
    func NewLogListManager(llr LogListRefresher, mf monitoring.MetricFactory) *LogListManager
    func (llm *LogListManager) Run(ctx context.Context, llRefresh time.Duration)
    func (llm *LogListManager) RefreshLogList(ctx context.Context) (*LogListData, error)
    func (llm *LogListManager) GetTwoLatestLogLists() (*LogListData, *LogListData)
    func (llm *LogListManager) ProduceClientLogList() LogListData
    func (llm *LogListManager) Source() string
    func (llm *LogListManager) LastJSON() []byte
    type LogListRefresher interface {
        Refresh() (*LogListData, error)
        LastJSON() []byte
        Source() string
    }
    type LogListData struct {
        JSON         []byte
        List         *loglistN.LogList
        DownloadTime time.Time
    }
    func NewLogListRefresher(llPath string) LogListRefresher
    func NewCustomLogListRefresher(client *http.Client, llPath string) LogListRefresher
    type Proxy struct{ … }
    type ProxyServer struct{ … }
    type SCTBatch struct{ … }
    type Submitter interface{ … }
    type AssignedSCT struct {
        LogURL string
        SCT    *ct.SignedCertificateTimestamp
    }
    func GetSCTs(ctx context.Context, submitter Submitter, chain []ct.ASN1Cert, asPreChain bool, groups ctpolicy.LogPolicyData) ([]*AssignedSCT, error)

**Which logs get a client.** `NewDistributor` walks the supplied list and
builds a client — through the `LogClientBuilder` it was handed — for every log
whose state is `usable`, `pending` or `qualified`. Logs in the `readonly` and
`retired` states get no client and are never submitted to. `BuildLogClient` is
the package's own default builder and the one the proxy passes: it is a
concrete function of the `LogClientBuilder` type, so a caller can both call it
and assign it to a variable of that type.

**`Distributor` behaviour that a caller can observe.** `RefreshRoots` returns a
map keyed by **log URL**, carrying one entry per log whose root fetch failed
and nothing for the logs that succeeded. `AddChain` / `AddPreChain` return one
`*AssignedSCT` per log that answered, each carrying the answering log's URL in
`AssignedSCT.LogURL`; they refuse an empty chain and refuse a pre-chain whose
leaf is not a pre-certificate, in both cases with an error and no SCTs.
`SubmitToLog` submits to one named log and returns that log's SCT; an unknown
log URL is an error rather than a silent no-op.

**The proxy's HTTP surface.** The `server` binary of §4.9 mounts the proxy on
four routes and they are part of the interface, not an internal detail:

| method and path | meaning |
| --- | --- |
| `POST /ct/v1/proxy/add-chain/` | submit a chain; the body is CTFE's `add-chain` JSON, `{"chain":[<base64 DER>…]}` |
| `POST /ct/v1/proxy/add-pre-chain/` | the same for a pre-certificate chain |
| `GET /metrics` | the Prometheus text exposition of the metrics below |
| `GET /` | a human-readable status page |

The two submission routes are **POST-only**; the trailing slash is part of the
path, and a `GET` on one of them reaches no handler, so Go's own multiplexer
answers `404` with the body `404 page not found\n`.

Both routes answer errors with `http.Error`, so the body is the message plus a
newline and the status is one of three:

| status | body | when |
| --- | --- | --- |
| **400** | `proxy: failed to parse add-chain body: <reason>` — and `add-pre-chain` on the pre-chain route | the request body is not the expected JSON, or carries an empty chain |
| **502** | `distributor unable to process cert-chain: <reason>` | the body parsed but the distributor refused the chain — unparseable DER, or no log will root it |
| **500** | the error's own text | the SCT list could not be marshalled |

`<reason>` is the underlying library's wording and is not fixed by this
document; the status, the prefix before the colon and the trailing newline are.

**Metrics.** `GET /metrics` carries, among the process defaults,
`log_list_updates` — a counter of log-list refreshes, so it reads `1` once the
first list has been loaded — and `last_get_roots_success`, a gauge of the Unix
timestamp of the last successful `get-roots`, labelled `logurl` with one series
per log the distributor built a client for (§7.2's status rule, so usable,
pending and qualified logs only).

**`LogListRefresher` / `LogListManager` semantics.** `Source()` returns the
path or URL the refresher was constructed with. `LastJSON()` returns an
**empty, non-nil** `[]byte` before the first successful refresh and the last
bytes read afterwards. `Refresh()` returns `(nil, nil)` — not an error — when
the source is byte-identical to the previous read, a populated `*LogListData`
when it has changed, and an error when the source cannot be read or does not
parse. `NewLogListManager(nil, …)` produces a manager that refuses to refresh.

`RefreshLogList` also **archives**: each successful refresh moves the list it
was holding into the previous slot and installs the new one, so
`GetTwoLatestLogLists()` returns `(latest, previous)` in that order — the most
recently refreshed list first, the one before it second — and both are `nil`
before any successful refresh, with the second still `nil` after the first.
`ProduceClientLogList()` returns that latest list, by value; the client-side
filtration its name promises is not implemented at these releases, so what it
returns is the list `RefreshLogList` installed.
    func ASN1MarshalSCTs(scts []*AssignedSCT) ([]byte, error)

`NewDistributor` becomes variadic with an options interface at **v1.2.0**, not
v1.3.3; v1.3.3 carries the same signature unchanged:

    func NewDistributor(ll *loglist3.LogList, plc ctpolicy.CTPolicy, lcBuilder LogClientBuilder, mf monitoring.MetricFactory, distributorOptions ...DistributorOption) (*Distributor, error)
    type DistributorOption interface{ Apply(d *Distributor) error }
    type DisableRootCompatibilityCheckingDistributorOption struct{}

### 7.3 `schedule`

One function, unchanged from v1.1.0 to v1.3.3:

    func Every(ctx context.Context, period time.Duration, f func(context.Context))

Runs `f` at once and then every `period` until the context is done.

### 7.4 `watcher` — **v1.1.0 .. v1.1.2 only**

The smallest of the four and the shortest-lived: it watches a log-list file or
URL and reports what changed between polls. It is **deleted at v1.1.3**
(RELEASE-MAP.md §14), so it exists at exactly two anchors and a tree built for
a later release must not contain it.

    type DiffEvent struct {
        Diffs []diffmatchpatch.Diff
        Err   error
    }
    type Diff struct{ … }                 // all fields unexported
    func NewDiff(ctx context.Context, path string, isPathURL bool,
        checkInterval time.Duration, events chan<- DiffEvent) *Diff
    func (d *Diff) GetSyncedData() []byte

`path` is a filesystem path when `isPathURL` is false and a URL when it is
true; either way the read goes through `x509util.ReadFileOrURL` (§5.2) with a
10s HTTP timeout. `NewDiff` polls once before it returns and then re-polls
every `checkInterval` through `schedule.Every` (§7.3). Each poll sends at most
one `DiffEvent` on `events`: a failed read sends one carrying only `Err`, and
a read whose contents differ from the last synced copy sends one carrying only
`Diffs`. The diff elements are `diffmatchpatch.Diff` values from
`github.com/sergi/go-diff/diffmatchpatch`, which is therefore a module
dependency for as long as this package exists. `Diff`'s fields are all
unexported, so `GetSyncedData` is the only reader of its state.

---

## 8. `trillian/ctfe` — the entry points `ct_server` calls

### 8.1 `SetUpInstance`, the API that breaks four times

| release | signature |
| --- | --- |
| v1.0 | `SetUpInstance(ctx context.Context, client trillian.TrillianLogClient, cfg *configpb.LogConfig, deadline time.Duration, mf monitoring.MetricFactory) (*PathHandlers, error)` |
| **v1.0.5** | `SetUpInstance(ctx context.Context, client trillian.TrillianLogClient, cfg *configpb.LogConfig, opts InstanceOptions) (*PathHandlers, error)` |
| **v1.0.21** | `SetUpInstance(ctx context.Context, opts InstanceOptions) (*PathHandlers, error)` |
| **v1.1.0** | `SetUpInstance(ctx context.Context, opts InstanceOptions) (*Instance, error)` — stable to v1.3.3 |

The direction of travel is one argument at a time disappearing into
`InstanceOptions`, and then the return type growing from a bare handler map to
an object that also exposes the STH getter.

    type Instance struct {          // v1.1.0
        Handlers  PathHandlers
        STHGetter STHGetter
        // unexported log info
    }

    // v1.2.0 and later
    func (i *Instance) GetPublicKey() crypto.PublicKey

**`GetPublicKey` arrives at v1.2.0** and is the only exported accessor for the
unexported log info: it returns the public half of the signer the instance was
built with — the same key the log's `get-sth` signatures verify against and the
same key `--log_config`'s private key implies. It is **total**: on an
`Instance` with no signer behind it, including a zero `Instance{}` a caller
constructed itself, it returns a nil `crypto.PublicKey` rather than panicking.
A log that came up under a *mirror* config has no signer, so it too reports
nil. Before v1.2.0 there is no such method and the key is not reachable from
outside the package.

At **v1.0.21 only** a second entry point sits beside it, for a log configured
with `is_mirror`:

    // v1.0.21 only
    func SetUpMirrorInstance(ctx context.Context, opts InstanceOptions,
        stor MirrorSTHStorage) (*PathHandlers, error)

It builds the log from the same `InstanceOptions`, then attaches a
`MirrorSTHGetter` reading from `stor` in place of the signing getter a normal
log gets, and returns the same kind of handler map — without the write
endpoints, because the config is a mirror (§8.3 below, HTTP-API.md §6.2).
Passing a mirror config to plain `SetUpInstance` at this release yields the
same handler set but the default, always-failing storage. From **v1.1.0** the
separate entry point is gone and `SetUpInstance` inspects `is_mirror` itself.

**The library and the binary do not arrive together.** `is_mirror` is a
`LogConfig` field from **v1.0.21** and everything above is reachable from a
program that links `trillian/ctfe` directly, but at **v1.0.21 the `ct_server`
binary refuses to serve one**: its per-log registration opens with an
`if cfg.IsMirror` that returns `mirrors are not supported`, so the process
fails setup for that log and never listens. It never calls
`SetUpMirrorInstance`; nothing in the shipped command does. From **v1.1.0
through v1.3.3** the guard is gone and `ct_server` brings a mirror up like any
other log, with the handler set of HTTP-API.md §6.2. So v1.0.21 is the release
that can *build* a mirror and v1.1.0 is the first that can *run* one, and a
configuration file that only differs by `is_mirror: true` is the difference
between a server that starts and one that does not.

### 8.2 `InstanceOptions`, field by field

| field | from |
| --- | --- |
| `Deadline time.Duration`, `MetricFactory monitoring.MetricFactory`, `ErrorMapper func(error) (int, bool)` | **v1.0.5** |
| `RequestLog RequestLog` (§8.5) | **v1.0.10** |
| `RemoteQuotaUser func(*http.Request) string`, `CertificateQuotaUser func(*x509.Certificate) string` (§8.6, ARCHITECTURE.md §2.2) | **v1.0.19** |
| `Config *configpb.LogConfig`, `Client trillian.TrillianLogClient` | **v1.0.21** |
| `Validated *ValidatedLogConfig` (replacing `Config`), `STHStorage MirrorSTHStorage`, `MaskInternalErrors bool` | **v1.1.0** |
| `CacheType cache.Type`, `CacheOption cache.Option` (§8.4) | **v1.2.0** |
| `FreshSubmissionMaxAge time.Duration`, `NonFreshSubmissionLimiter *rate.Limiter` | **v1.3.3** |

### 8.3 The rest of the package surface

    type EntrypointName string
    type PathHandlers map[string]AppHandler

    // from v1.1.0 only -- see below
    func ParseBodyAsJSONChain(r *http.Request) (ct.AddChainRequest, error)

    // AppHandler is the value type of PathHandlers and implements http.Handler.
    type AppHandler struct {
        // first field and handler signature: see the table below
        Handler func(context.Context, <ctx type>, http.ResponseWriter, *http.Request) (int, error)
        Name    EntrypointName   // which entrypoint this is
        Method  string           // exactly http.MethodGet or http.MethodPost
    }

    func IsPrecertificate(cert *x509.Certificate) (bool, error)
    func ValidateChain(rawChain [][]byte, validationOpts CertValidationOpts) ([]*x509.Certificate, error)
    func GetCTLogID(pk crypto.PublicKey) ([sha256.Size]byte, error)

    var MaxGetEntriesAllowed int64 = 1000

`ValidateChain` returns the **verified path**, not the submission: the leaf
first, then each issuer in order, **including the trust anchor it terminated
at**, even when the submitted chain stopped short of it.

Verification usually finds more than one path, and **which one is returned is
part of the contract**: it is always a path that reproduces the submission
element for element — the same certificates, in the same order, and either
exactly as long as the submission or exactly one certificate longer. That one
extra certificate is the only thing a verified path is ever allowed to add,
and it is the trust anchor the submission stopped short of. Where a *shorter*
path also reproduces the submission it is the one returned, because path
building reaches the configured roots before it recurses through the
submission's own intermediates: if the submission's last certificate is itself
one of the configured roots, the path terminates there and exactly the
submitted certificates come back. So `[leaf, intermediate]` returns **three**
certificates against a pool that holds the root and not the intermediate, but
**two** — just what was submitted — against a pool that holds the intermediate
as a trust anchor in its own right, and `[leaf, intermediate, root]` returns
**three** against either pool. A path that skips, reorders or substitutes a
submitted certificate is discarded even though it verifies, and if no verified
path reproduces the submission the call refuses rather than returning some
other valid path (HTTP-API.md §5.1 item 3).

A refusal is a nil slice and a non-nil error. All of this holds at every
release from v1.0 to v1.3.3.

**`ParseBodyAsJSONChain` is exported only from v1.1.0.** From v1.0 through
v1.1.0's predecessor it is the package-private `parseBodyAsJSONChain`, and it
takes the per-log context as its first parameter — `(c LogContext, r
*http.Request)` at v1.0..v1.0.5, `(c *LogContext, …)` at v1.0.10..v1.0.17,
`(li *logInfo, …)` at v1.0.19..v1.0.21 — matching the `AppHandler` context
column below. Nothing outside the package can call it before v1.1.0, and
nothing is entitled to; exporting it early is harmless but is not what the
release did.

Also exported at every release from v1.0, the eight `EntrypointName`
constants and `Entrypoints`, a `[]EntrypointName` holding them in this order.
The *values* are CamelCase entrypoint names, not the URL spellings, and they
are what a metric label and an `AppHandler.Name` carry:

| constant | value |
| --- | --- |
| `AddChainName` | `AddChain` |
| `AddPreChainName` | `AddPreChain` |
| `GetSTHName` | `GetSTH` |
| `GetSTHConsistencyName` | `GetSTHConsistency` |
| `GetProofByHashName` | `GetProofByHash` |
| `GetEntriesName` | `GetEntries` |
| `GetRootsName` | `GetRoots` |
| `GetEntryAndProofName` | `GetEntryAndProof` |

Note `GetSTH` and `GetSTHConsistency`, which capitalise `STH`, against
`AddPreChain` and `GetEntryAndProof`, which do not hyphenate. These are
distinct from the root package's `APIEndpoint` constants of HTTP-API.md §1.

`PathHandlers` is an ordinary map, so its keys are the assembled paths of
HTTP-API.md §1.1 and ranging over it gives every registered entrypoint. The
last two fields of `AppHandler` are exported, spelled exactly `Name` and
`Method`, and carry those types at **every** release from v1.0 to v1.3.3;
`Method` holds the `net/http` method constant the entrypoint accepts, and it is
what §2's 405 is decided from. The first field and the matching `Handler`
parameter carry the per-log context and are the part that moves:

| releases | first field | `<ctx type>` |
| --- | --- | --- |
| v1.0 .. v1.0.5 | `Context LogContext` | `LogContext` |
| v1.0.10 .. v1.0.17 | `Context *LogContext` | `*LogContext` |
| v1.0.19 .. v1.3.3 | `Info *logInfo` (the type is unexported) | `*logInfo` |

Every handler the package registers has an `EntrypointName` constant beside it,
spelled `<Entrypoint>Name` and defined as `EntrypointName("<Entrypoint>")` — so
`AddChainName = EntrypointName("AddChain")`, and the same for `AddPreChain`,
`GetSTH`, `GetSTHConsistency`, `GetProofByHash`, `GetEntries`, `GetRoots` and
`GetEntryAndProof`. `var Entrypoints []EntrypointName` collects them, and the
name is what the request log and the metrics label a request with. That set of
eight is stable from v1.0; **v1.3.3** adds a ninth,
`LogV3JSONName = EntrypointName("LogV3JSON")`, for the endpoint of
HTTP-API.md §7.1.

The package also carried its own submission-side leaf container from v1.0, and
**withdraws it at v1.0.17**:

    // v1.0 .. v1.0.13 only — removed at v1.0.17
    type LogEntry struct {
        Leaf  ct.MerkleTreeLeaf
        Chain []ct.ASN1Cert
    }
    func NewLogEntry(leaf ct.MerkleTreeLeaf, certChain []*x509.Certificate) *LogEntry

It duplicated the root package's own `ct.LogEntry` and was only ever used to
carry a just-built leaf plus its chain to the backend. From **v1.0.17**
neither identifier exists in `trillian/ctfe`, and a program naming either one
does not compile; callers use `ct.LogEntry` (TLS-CODEC.md §5) instead.

`CertValidationOpts` has only unexported fields. At v1.0 they are
`trustedRoots`, `rejectExpired`, `notAfterStart`, `notAfterLimit`,
`acceptOnlyCA`, `extKeyUsages`; **v1.1.0** adds `currentTime`,
`rejectUnexpired`, `rejectExtIds`; **v1.1.3** retypes `trustedRoots` to
`*x509util.PEMCertPool`. Because the fields are unexported, a constructor is
required, and it appears at **v1.1.0**:

    func NewCertValidationOpts(trustedRoots *ctfe.PEMCertPool, currentTime time.Time,
        rejectExpired, rejectUnexpired bool,
        notAfterStart, notAfterLimit *time.Time,
        acceptOnlyCA bool, extKeyUsages []x509.ExtKeyUsage) CertValidationOpts

The first parameter tracks the move described in §5.3: it is
`*ctfe.PEMCertPool` at **v1.1.0** and **v1.1.2**, and becomes
`*x509util.PEMCertPool` at **v1.1.3**. Nothing else about the signature
changes. The remaining seven parameters are in the order shown at every
release that has the constructor.

`GetTreeHead(ctx, client, logID, prefix)` exists at v1.0, gains a fifth
`remoteQuota *string` parameter at **v1.0.19**, and is **removed at v1.0.21**
in favour of the `STHGetter` interface:

    // v1.0.19 .. v1.0.19 only
    func GetTreeHead(ctx context.Context, client trillian.TrillianLogClient,
        logID int64, prefix string, remoteQuota *string) (*ct.SignedTreeHead, error)

The fifth parameter is the remote quota user of ARCHITECTURE.md §2.2, and the
**pointer**, not the string, is what decides: `nil` attaches no `ChargeTo` to
the `GetLatestSignedLogRoot` request, and any non-nil pointer attaches a
`ChargeTo` whose `User` is exactly one element long, holding whatever the
pointer points at — a pointer to `""` included. The `ChargeTo` is built afresh
inside each call, so consecutive calls never accumulate users. The returned
tree head is unaffected by the quota user either way; it is the same STH the
four-argument form returns.

The withdrawal is not a straight deletion: at **v1.0.21, and at that release
only**, a narrower free function takes over the one job the removed one still
had for `ct_server` — the periodic internal get-sth tick that keeps the STH
gauges current:

    // v1.0.21 only
    func PingTreeHead(ctx context.Context, client trillian.TrillianLogClient,
        logID int64, prefix string) error

Its four parameters are the first four of the old `GetTreeHead`, in the same
order. It fetches the backend's latest signed log root, updates the last-STH
timestamp and tree-size gauges from it, and returns **only** an error — never
an STH. `ct_server` calls it on a timer, once per configured log, and merely
warns when it fails. It exists at v1.0.21 alone: at v1.1.0 the tick moves
behind `Instance`'s own STH getter and the free function is gone.


    type STHGetter interface { GetSTH(ctx context.Context) (*ct.SignedTreeHead, error) }
    type LogSTHGetter struct{ … }        // unexported fields; *LogSTHGetter is an STHGetter
    type MirrorSTHGetter struct{ … }     // unexported fields; *MirrorSTHGetter is an STHGetter
    type FrozenSTHGetter struct{ … }     // v1.1.0
    type MirrorSTHStorage interface {
        // GetMirrorSTH returns an STH of TreeSize <= maxTreeSize, doing its
        // best to maximise the returned TreeSize and/or Timestamp.
        GetMirrorSTH(ctx context.Context, maxTreeSize int64) (*ct.SignedTreeHead, error)
    }
    type DefaultMirrorSTHFactory struct{}
    func (f DefaultMirrorSTHFactory) NewStorage(logID [sha256.Size]byte) (MirrorSTHStorage, error)
    type DefaultMirrorSTHStorage struct{}

The two getters differ only in where the tree head comes from, and both are
reached through the `STHGetter` interface, never by name, once the instance is
built. `LogSTHGetter` asks the backend and signs. `MirrorSTHGetter` asks its
`MirrorSTHStorage` and serves whatever comes back **unaltered** — it does not
re-sign and does not re-time — and the `maxTreeSize` it passes is the tree
size of the backend's current signed log root, so a mirror never serves an STH
ahead of what the backend it fronts has actually got. One `get-sth` request is
one call to the storage. A storage that returns an error is a failed
`get-sth`: the response is not a 200.

`DefaultMirrorSTHStorage` is the placeholder a deployment is meant to replace.
Its `GetMirrorSTH` always returns an error and never an STH, so a mirror left
on the default serves no tree heads at all; `DefaultMirrorSTHFactory` is the
empty struct whose `NewStorage` hands one back for any log ID and never fails.
These four names arrive at v1.0.21 and are still there at v1.3.3;
`SetUpMirrorInstance` (§8.1) is the v1.0.21-only way to attach a storage.

### 8.4 Storage and cache (v1.2.0)

    package storage
    type IssuanceChainStorage interface {
        FindByKey(ctx context.Context, key []byte) ([]byte, error)
        Add(ctx context.Context, key []byte, chain []byte) error
    }
    func NewIssuanceChainStorage(ctx context.Context, backend configpb.LogConfig_IssuanceChainStorageBackend, dbConn string) (IssuanceChainStorage, error)

    package cache
    type Type string
    const ( Unknown Type = ""; NOOP Type = "noop"; LRU Type = "lru" )
    type Option struct { Size int; TTL time.Duration }
    type IssuanceChainCache interface {
        Get(ctx context.Context, key []byte) ([]byte, error)
        Set(ctx context.Context, key, chain []byte) error
    }
    func NewIssuanceChainCache(ctx context.Context, cacheType Type, option Option) (IssuanceChainCache, error)

    package cache/noop
    type IssuanceChainCache struct{}        // exported, zero-value constructible

    package cache/lru
    type CacheOption struct { Size int; TTL time.Duration }   // not cache.Option
    func NewIssuanceChainCache(opt CacheOption) *IssuanceChainCache   // one value, no error

    package storage/mysql                       // v1.2.0
    package storage/postgresql                  // v1.3.3
    type IssuanceChainStorage struct{ … }       // exported, in each package
    func NewIssuanceChainStorage(ctx context.Context, dbConn string) *IssuanceChainStorage

The two database drivers are siblings with the same shape, and `postgresql` is
added at **v1.3.3** in the image of the `mysql` package that has been there
since v1.2.0: an exported concrete `IssuanceChainStorage` whose *pointer*
implements the `storage.IssuanceChainStorage` interface above, and a
constructor taking the connection string that returns **one value and no
error** — opening the database handle is treated as fatal to the process if it
fails, so there is no error to hand back. `storage.NewIssuanceChainStorage` is
the only one of the three a deployment calls; it chooses between them on the
connection string's prefix.

`NewIssuanceChainStorage` returns `(nil, nil)` for the `TRILLIAN_GRPC`
backend — a nil storage is the signal to keep chains in the backend leaf, not
an error. Its `CTFE` arm decides from the connection string **at construction**
and contacts nothing: a string not beginning with `mysql`, the empty string
included, is `(nil, error)`. (**v1.3.3** accepts a second prefix, `postgres`,
on the same terms; at v1.2.0 `mysql` is the only one.) A backend value that is
neither of the two enum
members is `(nil, error)` as well — note that `ctfe.ValidateLogConfig` does the
opposite with the same value and copies it through untouched, since validation
is about the *combination* of configuration fields and construction is where an
unusable backend becomes an error.

`cache.NewIssuanceChainCache` maps `NOOP` **and** the zero value `Unknown`
(`""`) to `&noop.IssuanceChainCache{}`, and `LRU` to
`lru.NewIssuanceChainCache`; any other string is `(nil, error)`. For `LRU`
only, a negative `Size` or a negative `TTL` is also `(nil, error)`. The noop
cache's `Get` returns `(nil, nil)` and ignores the option entirely. In the LRU,
a `Size` of zero or less means *no capacity bound* and a `TTL` of zero or less
means *no expiry*, which is why the direct constructor accepts values the
factory refuses.

**The LRU in the name is load-bearing.** With a **positive** `Size` the cache
holds at most that many entries and, on reaching the bound, discards by
**recency, not arrival**: the entry whose last successful `Get` or `Set` is
oldest is the one that goes. A `Get` that finds its key therefore *renews* that
entry and postpones its eviction, so an insertion-ordered queue — first in,
first out regardless of reads — is not this cache and is observably different
from it. `Set` is a **replacement**, not an insert-once: setting a key already
held leaves the later chain under that key, renews it, and keeps the count at
one entry for that key rather than two. And in **both** implementations a `Get`
for a key the cache does not hold — never stored, stored and since evicted, or
stored and since expired — is `(nil, nil)`, never an error, so "absent" is
something a caller distinguishes from "failed" by the nil chain and not by an
error.

The service layer on top is one type at **v1.2.0** and splits at **v1.3.3**
into a direct implementation (no storage, `FixLogLeaf` is a no-op) and an
indirect one (hash in the leaf, chain in storage), both satisfying a small
`BuildLogLeaf` / `FixLogLeaf` interface. That `BuildLogLeaf` is a **method on
that v1.3.3 service interface** and is a different thing from the package-level
`trillian/util.BuildLogLeaf` function of §8.7, which exists from v1.0.19 and is
what the v1.3.3 service implementation calls.

### 8.5 `RequestLog` — structured logging of a request, from v1.0.10

`ctfe` calls out to a caller-supplied logger at fixed points while handling a
request. The interface is exported, the CTFE calls **every** method on it, and
`InstanceOptions.RequestLog` (§8.2, from v1.0.10 — the field and the interface
arrive together) is how an implementation is installed; the package's own
default implementation writes to `glog`.

    // v1.0.10 .. v1.0.17
    type RequestLog interface {
        Start(context.Context) context.Context
        LogPrefix(context.Context, string)
        AddDERToChain(context.Context, []byte)
        AddCertToChain(context.Context, *x509.Certificate)
        FirstAndSecond(context.Context, int64, int64)
        StartAndEnd(context.Context, int64, int64)
        LeafIndex(context.Context, int64)
        TreeSize(context.Context, int64)
        LeafHash(context.Context, []byte)
        Status(context.Context, int)
    }

At **v1.0.19** an eleventh method is inserted between `LeafHash` and `Status`:

    // v1.0.19 onwards
        IssueSCT(context.Context, []byte)

Adding a method to an exported interface is a breaking change: a ten-method
implementation that satisfied `RequestLog` at v1.0.17 does not satisfy it from
v1.0.19 and does not compile as an `InstanceOptions.RequestLog`.

What each call means, and when it happens:

* `Start` is called **once, first**, at the beginning of handling a request,
  and returns the context used for everything after it.
* `LogPrefix` is called once, with the log's **label** — not its bare
  configured prefix. The label is `fmt.Sprintf("%s{%d}", prefix, logID)`:
  a log with prefix `athos` and log id `9` gives exactly `athos{9}`.
  Unchanged from v1.0.10 to v1.3.3.
* `AddDERToChain` is called once per submitted certificate, **before** anything
  is parsed or verified, in submission order with the root last.
* `AddCertToChain` is called once per certificate **after** the chain has
  parsed and verified, in the same order.
* `FirstAndSecond`, `StartAndEnd`, `LeafIndex`, `TreeSize` and `LeafHash` are
  the read-endpoint parameter hooks, each called at most once and only when the
  parameters it names parsed.
* `IssueSCT` is called once, **only when an SCT is actually going to be
  returned to the client**, with the SCT in its **TLS-serialised** form — the
  same bytes the response body carries base64-encoded. A submission that fails
  before an SCT exists does not produce this call.
* `Status` is called **once, last**, with the HTTP status code the handler
  settled on.

So a successful `add-chain` over a two-certificate chain produces exactly
`Start`, `LogPrefix`, `AddDERToChain`, `AddDERToChain`, `AddCertToChain`,
`AddCertToChain`, `Status(200)`, in that order — and from **v1.0.19**, once
the eleventh method exists, `IssueSCT` between `AddCertToChain` and `Status`.
Before v1.0.19 there is no `IssueSCT` call because there is no such method.

### 8.6 The quota users, from v1.0.19

    const CertificateQuotaUserPrefix = "@intermediate"
    func QuotaUserForCert(c *x509.Certificate) string

`QuotaUserForCert` is the value installed in
`InstanceOptions.CertificateQuotaUser` by `ct_server --quota_intermediate`
(ARCHITECTURE.md §2.2). It is a pure, total function of a certificate — it
validates nothing and rejects nothing, so it may be handed a root, an
intermediate or a leaf — and it returns three space-separated fields:

    "<CertificateQuotaUserPrefix> <Subject.String()> <hex(SHA256(SubjectPublicKeyInfo)[0:5])>"

that is: the constant above; then the certificate's subject rendered by
`pkix.Name.String()` (RFC 2253 order, comma-separated, most specific first, so
`CN=FakeIntermediateAuthority,OU=Eng,O=Google,L=London,ST=London,C=GB`) — with
every `/` in that rendering **escaped as `%2F` from v1.2.0** onward, where
through v1.1.4 the rendering is spliced in unescaped; then
the **first five bytes** of the SHA-256 of the certificate's raw DER
`SubjectPublicKeyInfo`, lower-case hex, ten characters. Two certificates with
the same subject and different keys therefore get different identities, and a
re-issued certificate with the same subject and the same key gets the same one.

There is no matching `QuotaUserForRequest`: the remote half is
`realip.FromRequest` from `github.com/tomasen/realip`, assigned directly to
`InstanceOptions.RemoteQuotaUser`, and its rule is in ARCHITECTURE.md §2.2.

### 8.7 `trillian/util` — leaf construction, from v1.0.19

The `trillian/util` package itself is older than this — it already holds the
backend resolver and the time source. What **v1.0.19** adds to it is the "turn
a verified chain into the `trillian.LogLeaf` the backend stores" step, lifted
out of the CTFE handlers so that `migrillian` (CLI.md §4.7) and the CTFE build
byte-identical leaves:

    package util   // github.com/google/certificate-transparency-go/trillian/util

    func BuildLogLeaf(logPrefix string, merkleLeaf ct.MerkleTreeLeaf, leafIndex int64,
        cert ct.ASN1Cert, chain []ct.ASN1Cert, isPrecert bool) (trillian.LogLeaf, error)

    func ExtraDataForChain(cert ct.ASN1Cert, chain []ct.ASN1Cert,
        isPrecert bool) ([]byte, error)

Neither name exists before v1.0.19; both keep this *argument* list through
v1.3.3. The return changes once: at **v1.3.3** every exported builder in this
file returns `*trillian.LogLeaf` rather than a `trillian.LogLeaf` value.
Through **v1.2.0** it is the value form above.

`ExtraDataForChain` produces the *extra data* RFC 6962 §4.6 stores beside a
leaf, and its shape depends only on `isPrecert`:

* a **certificate** entry gets the TLS encoding of `ct.CertificateChain`
  (TLS-CODEC.md §5) holding the chain **after the leaf** — the leaf itself is
  not repeated, because it is already in the leaf value. An empty chain is
  therefore a three-byte encoding, the 24-bit length `000000`, not an empty
  byte slice;
* a **precertificate** entry gets the TLS encoding of `ct.PrecertChainEntry`,
  whose `PreCertificate` field is `cert` and whose `CertificateChain` field is
  the chain after it — here the submitted precertificate *is* repeated, which
  is why a precert's extra data is larger than a certificate's for the same
  chain.

`BuildLogLeaf` assembles the four fields the backend needs and does nothing
else:

| field | value |
| --- | --- |
| `LeafValue` | the TLS encoding of `merkleLeaf` — `tls.Marshal` of the whole `MerkleTreeLeaf` |
| `ExtraData` | `ExtraDataForChain(cert, chain, isPrecert)` |
| `LeafIndex` | the `leafIndex` argument, passed through unchanged |
| `LeafIdentityHash` | **SHA-256 of `cert.Data`** — the submitted certificate's DER, and nothing else |

`LeafIdentityHash` is how the backend recognises a duplicate submission, so it
is deliberately a hash of the certificate alone: the same certificate submitted
with a different chain, at a different index or at a different timestamp has
the same identity hash and a different leaf value. `logPrefix` is used only in
the log line an error produces; it does not enter the leaf. An error from
either `tls.Marshal` is returned with a zero `trillian.LogLeaf`.

**v1.2.0 adds a second pair, for a log that stores the issuance chain out of
line** (CONFIG.md §2.6). Neither name exists before v1.2.0:

    // v1.2.0 and later
    func BuildLogLeafWithChainHash(logPrefix string, merkleLeaf ct.MerkleTreeLeaf,
        leafIndex int64, cert ct.ASN1Cert, chainHash []byte,
        isPrecert bool) (trillian.LogLeaf, error)

    func ExtraDataForChainHash(cert ct.ASN1Cert, chainHash []byte,
        isPrecert bool) ([]byte, error)

They are the `chainHash` twins of the two above, and they differ from them in
exactly one thing: the extra data holds a **hash of the chain** instead of the
chain. `ExtraDataForChainHash` TLS-encodes `ct.CertificateChainHash` for a
certificate — a single `IssuanceChainHash` field, because the certificate is
already in the Merkle leaf — and `ct.PrecertChainEntryHash` for a
precertificate, which repeats the precertificate beside the hash for the same
reason `PrecertChainEntry` does (TLS-CODEC.md §5.4 for both layouts and their
length bounds; the hash field caps at 256 bytes and an **empty** hash is legal
and means "no issuance chain", while the precertificate field is required).

`BuildLogLeafWithChainHash` fills the same four fields from the same table,
with `ExtraData` coming from `ExtraDataForChainHash(cert, chainHash, isPrecert)`
and `LeafIdentityHash` still SHA-256 of `cert.Data` alone. So for one
submission the chain-bearing leaf and the hash-bearing leaf agree on
`LeafValue`, `LeafIndex` and `LeafIdentityHash` and differ only in
`ExtraData` — which is what lets a log switch storage mode without disturbing
entries it already holds. A **nil** `chainHash` makes the builder fall back to
the chain form; an empty non-nil slice does not.

---

### 8.8 `RequestLog`, restated — call sequence and the prefix label

This section expands §8.5 and does not replace it; §8.5 remains the
cross-reference the rest of the corpus uses. v1.0.10 adds an exported request-logging interface, the `InstanceOptions`
field that carries it, and a shipped implementation. It is a **hook
interface**: the package calls it, you do not call the package.

    type RequestLog interface {
        Start(context.Context) context.Context
        LogPrefix(context.Context, string)
        AddDERToChain(context.Context, []byte)
        AddCertToChain(context.Context, *x509.Certificate)
        FirstAndSecond(context.Context, int64, int64)
        StartAndEnd(context.Context, int64, int64)
        LeafIndex(context.Context, int64)
        TreeSize(context.Context, int64)
        LeafHash(context.Context, []byte)
        Status(context.Context, int)
    }

    type DefaultRequestLog struct{ … }   // implements it, logging through glog

Ten methods at v1.0.10, and that exact set through v1.0.17. **v1.0.19 adds an
eleventh**, `IssueSCT(context.Context, []byte)`, called with the serialised
SCT an `add-chain` or `add-pre-chain` issued; the set is then stable through
v1.3.3. Because this is an interface the package *requires*, adding a method
breaks every out-of-tree implementation — so an implementation written for
v1.0.10 stops satisfying `RequestLog` at v1.0.19.

Who calls what, and in what order:

* `AppHandler.ServeHTTP` calls `Start` **first**, with the request's context,
  and uses the context `Start` returns for every later call on that request —
  so an implementation may carry per-request state on the context.
* Immediately after `Start` it calls `LogPrefix` with the log's label. That
  label is `fmt.Sprintf("%s{%d}", prefix, logID)` — for a log configured with
  prefix `athos` and log id `9`, exactly `athos{9}`. Unchanged v1.0.10 ..
  v1.3.3.
* `Status` is called **exactly once** per request, with the status actually
  served. This includes the requests that never reach a handler: a request
  whose method does not match the entrypoint's is refused by `ServeHTTP`
  itself, and it still reports `Start`, `LogPrefix`, `Status(405)` and
  nothing else. A malformed GET query reports `Status(400)` the same way.
* The remaining **seven** of the ten are called by the entrypoints, only when
  the value concerned parsed successfully (from v1.0.19 `IssueSCT` makes eight;
  §8.5 gives it): `AddDERToChain` once per submitted
  certificate before validation and `AddCertToChain` once per certificate
  after it, both leaf-first with the root last; `FirstAndSecond` by
  `get-sth-consistency`; `StartAndEnd` by `get-entries`; `LeafIndex` **and
  `TreeSize`** by `get-entry-and-proof`; `LeafHash` **and `TreeSize`** by
  `get-proof-by-hash`.
* `TreeSize` therefore belongs to **both** of the last two: each of those two
  entrypoints reports *every* parameter it parsed, not just the one that
  names it. Each reports them only once the whole parameter set has parsed,
  and in the order the handler parses it — `LeafIndex` then `TreeSize` for
  `get-entry-and-proof`, `LeafHash` then `TreeSize` for `get-proof-by-hash`,
  which is the order HTTP-API.md §4.4 and §4.5 give the query parameters in.
  A request in which any parameter fails to parse reports no value hook at
  all, only `Start`, `LogPrefix` and `Status(400)`. That pairing and that
  order hold from v1.0.10 through v1.3.3.

The interface is satisfied by `new(ctfe.DefaultRequestLog)` if you want the
shipped behaviour, and nothing in the package inspects the concrete type.

## 9. Reading route

1. §1.2 for the client you are being asked to build or call.
2. §2 whenever a *signature* or an *inclusion proof* has to be checked.
3. §4 for the log-list generation in force at your release — and §4.4 for the
   removals, which are milestones in their own right.
4. §8.1 and §8.2 before touching `SetUpInstance`; that function's shape is the
   clearest marker of which release you are at.
5. TLS-CODEC.md for every type named `ct.*` that crosses a wire.
