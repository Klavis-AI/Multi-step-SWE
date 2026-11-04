# TLS-CODEC — the `tls/` package contract

The product carries its own reflective codec for the TLS presentation language
of RFC 5246 section 4. Everything Certificate Transparency signs or hashes is
expressed in that language, so this codec is the bottom of the stack: get it
wrong and every signature, every leaf hash and every proof above it is wrong.

Normative references shipped with this corpus:

* `rfc/rfc5246.txt` **section 4** — the presentation language itself
  (4.1 basic types, 4.3 vectors, 4.5 enumerateds, 4.6.1 variants,
  4.7 `digitally-signed`).
* `rfc/rfc6962.txt` sections 3.2–3.5 — the CT structures written in it.

This page is the contract for the package at import path
`github.com/google/certificate-transparency-go/tls`. It is present and
unchanged in shape at **every** anchor from v1.0 to v1.3.3.

---

## 1. What the package exposes

Four exported entry points and two helper types.

    func Marshal(val interface{}) ([]byte, error)
    func MarshalWithParams(val interface{}, params string) ([]byte, error)
    func Unmarshal(b []byte, val interface{}) ([]byte, error)
    func UnmarshalWithParams(b []byte, val interface{}, params string) ([]byte, error)

    type Uint24 uint32        // an unsigned 3-byte integer
    type Enum uint64          // an unsigned integer whose width comes from a tag

`Unmarshal` returns the **unconsumed remainder** of `b` as its first result.
It does not fail on trailing data; the caller decides whether a non-empty
remainder is an error, and in this product every caller that decodes a
self-contained structure decides that it **is** one:

* `ctfe` and `ctutil` both treat leftover bytes after a `MerkleTreeLeaf` as an
  error — see HTTP-API.md §4.
* From **v1.0.21**, `gossip/minimal/x509ext.LogSTHInfoFromCert` does the same
  for the STH-in-a-certificate extension: it calls `Unmarshal` on the
  extension's raw `Value` and, if the remainder is non-empty, returns a nil
  `*LogSTHInfo` and `trailing data (%d bytes) after STH`. The extension body
  must encode **exactly one** `LogSTHInfo` and nothing more (CLIENT-API.md
  §6.2), so well-formed bytes with even one byte appended are rejected — which
  is the only way a caller can tell this decoder is not simply ignoring what it
  did not read.

`MarshalWithParams` and `UnmarshalWithParams` accept a tag string of exactly
the same grammar as a struct field tag (§3), applied to the top-level value.
This is how a bare `[]byte` is encoded as a length-prefixed opaque vector
without wrapping it in a struct.

The package also carries the `digitally-signed` machinery of RFC 5246 §4.7:

    type DigitallySigned struct {    // Algorithm, then a 2-byte-prefixed opaque
        Algorithm SignatureAndHashAlgorithm
        Signature []byte             // tag: minlen:0,maxlen:65535
    }
    type SignatureAndHashAlgorithm struct {   // Hash then Signature, one byte each
        Hash      HashAlgorithm      // tag: maxval:255
        Signature SignatureAlgorithm // tag: maxval:255
    }

    func CreateSignature(privKey crypto.PrivateKey, hashAlgo HashAlgorithm, data []byte) (DigitallySigned, error)
    func VerifySignature(pubKey crypto.PublicKey, data []byte, sig DigitallySigned) error
    func SignatureAlgorithmFromPubKey(k crypto.PublicKey) SignatureAlgorithm

`HashAlgorithm` and `SignatureAlgorithm` are `Enum`s with the RFC 5246 §7.4.1.4.1
code points: `None`=0, `MD5`=1, `SHA1`=2, `SHA224`=3, `SHA256`=4, `SHA384`=5,
`SHA512`=6; `Anonymous`=0, `RSA`=1, `DSA`=2, `ECDSA`=3. Their `String()` methods
render the names above, and an unrecognised value as `UNKNOWN(<n>)`.

`SignatureAlgorithmFromPubKey` is a **total** function — it has a single
return and no error — mapping `*ecdsa.PublicKey` to `ECDSA`, `*rsa.PublicKey`
to `RSA` and `*dsa.PublicKey` to `DSA`. Anything else, including a value that
is not a key at all, falls through to **`Anonymous`**; there is no way for it
to report a failure and it must not be made to.

`CreateSignature` switches on the **dynamic type** of `privKey`, and the two
types it handles are the **value** forms `rsa.PrivateKey` and
`ecdsa.PrivateKey` — not their pointers. A caller holding the
`*ecdsa.PrivateKey` that `x509.ParseECPrivateKey` returns passes `*priv`. Any
other dynamic type, pointer forms included, is returned as an error
("unsupported private key type") and never panics. This shape is unchanged
through v1.3.3.

`VerifySignature` dispatches on `sig.Algorithm.Signature`, not on the Go type
of the key. When the two disagree it must refuse rather than attempt the
verification: `cannot verify RSA signature with *ecdsa.PublicKey key` and its
DSA/ECDSA analogues. An algorithm identifier outside `{RSA, DSA, ECDSA}` is
`unsupported Algorithm.Signature in signature: <hash>`. RSA signatures are
PKCS#1 v1.5; DSA and ECDSA signatures are the DER `SEQUENCE { r, s }` of
RFC 3279, and a signature with `r <= 0` or `s <= 0` is rejected before the
curve arithmetic. Trailing bytes after that SEQUENCE are logged, not fatal.
This behaviour is the same at every anchor v1.0..v1.3.3.

`rfc/rfc6979.txt` is shipped for reference on deterministic (EC)DSA: nothing in
this product requires deterministic nonces, but a candidate producing ECDSA
signatures needs to understand why two signatures over the same input differ
and therefore why a verifier can only ever assert *verification*, never
signature bytes. See HTTP-API.md §8.

---

## 2. The type mapping

Each row is "TLS type on the wire" → "Go type you declare" → "tag you must
write". Encoding is big-endian throughout. There is no padding and no
alignment.

| TLS (RFC 5246 §4) | Go type | required tag | on the wire |
| --- | --- | --- | --- |
| `opaque`, `uint8` | `byte` / `uint8` | none | 1 byte |
| `uint16` | `uint16` | none | 2 bytes, big-endian |
| `uint24` | `tls.Uint24` | none | 3 bytes, big-endian |
| `uint32` | `uint32` | none | 4 bytes |
| `uint64` | `uint64` | none | 8 bytes |
| `enum { … } E` | `tls.Enum` (or a named type whose kind is `Enum`) | `size:S` **or** `maxval:N` | S bytes, or the minimum number of bytes that can hold N |
| `T v<N..M>` | `[]T` | `minlen:N,maxlen:M` | a length prefix wide enough for M, then the elements back to back |
| `opaque v[N]` | `[N]byte` | none | exactly N bytes, no prefix |
| `struct { … }` | `struct { … }` | none | fields in declaration order |
| `select(S) { case e: T }` | `*T` | `selector:Field,val:e` | the chosen branch only, nothing for the others |

Two consequences that are easy to get wrong and that the verifier exercises
directly:

* **The length prefix width is derived, not declared.** It is the number of
  bytes needed to hold `maxlen`, not the number needed to hold the actual
  length. `maxlen:65535` is always a 2-byte prefix even for an empty vector;
  `maxlen:16777215` is always 3 bytes; `maxlen:255` is 1 byte. A vector with
  `maxlen:256` takes a 2-byte prefix.
* **`maxval:N` sizes an enum the same way.** `maxval:255` is one byte,
  `maxval:65535` is two. `size:S` states the width directly and is the only
  form that can give an enum a width wider than its largest legal value needs.

A field with **no `tls` tag at all** is a third case, and is not an error by
itself. Where its Go type fixes a width — `uint16`, `uint32`, `uint64`,
`tls.Uint24`, a struct, a fixed `[N]byte` — the field encodes exactly as a
tagged one does. Where the type does not fix a width the field's width is
**zero**, and the two types that leaves are the **enum**, whose width comes
only from a tag, and the **slice**, whose length prefix does: the zero value
in such a field (the enum's `0`, an empty slice) contributes **no bytes at
all**, and any other value cannot be encoded (`value <n> too large for
size`, §4). The field this matters for is `SignedTreeHead.Version` (§5.6):
unlike every other `Version` field the codec encodes, which carries
`maxval:255` (§5.1), it has no tag, and the witness pre-image depends on
that — a whole `SignedTreeHead` encodes as tree_size(8) timestamp(8)
root(32) signature log_id(32) with nothing at all in front of the tree size.
Tag that field `maxval:255` and the structure becomes one byte longer, with a
`00` before the tree size.

Fixed-length vectors of anything other than `opaque`/`uint8` are **not
supported**; a `[N]T` for other T is a structural error. A variable-length
vector whose element type is itself a variable-length vector is not expressible
either: wrap the inner type in a single-field struct and give the inner field
its own `minlen`/`maxlen` tag.

---

## 3. Tag grammar

A field tag is the value of the struct tag key `tls`, e.g.

    Signature []byte `tls:"minlen:0,maxlen:65535"`

The value is a comma-separated list of clauses. Recognised clauses:

| clause | meaning |
| --- | --- |
| `maxval:N` | this is an enum whose largest legal value is N; width = bytes needed for N |
| `size:S` | this is an enum encoded in exactly S bytes |
| `maxlen:M` | this is a variable-length vector with upper bound M; prefix width = bytes needed for M |
| `minlen:N` | lower bound of a variable-length vector (default 0) |
| `selector:Field` | this field is one branch of a variant chosen by sibling field `Field` |
| `val:V` | this branch is chosen when the selector's value is V |

Rules the implementation must enforce, because they are the difference between
a codec that round-trips and one that silently mis-encodes:

* A clause that does not parse as its stated type is **ignored**, not an error.
  `maxlen:banana` behaves as if it were absent.
* `maxlen` sets the width even if `size`/`maxval` already did; the last
  size-setting clause seen wins in tag order.
* A non-selector field that **carries a `tls` tag** must end up with a width
  between 1 and 8 bytes. Zero width ("field of unknown size") and more than 8
  bytes are both structural errors, raised when the tag is parsed rather than
  when data is seen. A field that carries no tag at all is a different case
  (§2): it is sized by its Go type where the type fixes a width, and is
  zero-width otherwise, without being an error.
* `minlen` greater than `maxlen` is a structural error ("length range
  inverted").
* `val:` without `selector:` is a structural error ("specified selector value
  but not field").
* A struct field with no `tls` tag at all is legal: `uint16`, `uint32`,
  `uint64`, `tls.Uint24`, a struct and a fixed array fix its width, while an
  enum and a slice do not, which leaves those two zero-width (§2).

### 3.1 Variants (unions)

A variant is expressed as a run of pointer fields that share a selector:

    EntryType    LogEntryType   `tls:"maxval:65535"`   // the selector
    X509Entry    *ASN1Cert      `tls:"selector:EntryType,val:0"`
    PrecertEntry *PreCert       `tls:"selector:EntryType,val:1"`

* The selector field must appear **earlier in the same struct**. A selector
  named by a branch that has not been seen yet is the structural error
  "selector not seen: `<name>`".
* Every branch field must be a pointer type; a non-pointer branch is "choice
  field not a pointer type".
* Two branches with the same `val:` for the same selector is "duplicate
  selector value for `<selector>`".
* On **unmarshal**, a selector value that matches no branch is the syntax error
  "`<selector>`: unhandled value for selector". Exactly one branch is allocated
  and filled; the others stay nil.
* On **marshal**, exactly the branch matching the current selector value must
  be non-nil: a nil chosen branch is "chosen field is nil", and a non-nil
  unchosen branch is "unchosen field is non-nil". The unchosen branches
  contribute **no bytes at all** — not even a zero-length marker.

---

## 4. Error behaviour

Two internal error kinds, distinguished only by their message prefix; neither
type is exported, so nothing may be graded on the Go type, only on the text:

* `tls: structure error: <field>: <msg>` — the data is well formed but the Go
  type or its tags cannot represent it. Tag faults, unsupported Go types,
  variant misuse, out-of-range values.
* `tls: syntax error: <field>: <msg>` — the encoded data is malformed:
  truncation, an unhandled selector value, a vector length outside its bounds.

`<field>: ` is present only when the field has a name; the top level from
`Marshal`/`Unmarshal` has none, so a top-level fault reads
`tls: structure error: <msg>`.

The message texts the rest of the product and its tests rely on:

| situation | kind | message |
| --- | --- | --- |
| tag gives no width | structure | `field of unknown size in <tag>` |
| width > 8 | structure | `specified size too large in <tag>` |
| `minlen` > `maxlen` | structure | `specified length range inverted in <tag>` |
| `val:` without `selector:` | structure | `specified selector value but not field in <tag>` |
| value wider than the field — including any non-zero value in an untagged enum field or a non-empty untagged slice (§2) | structure | `value <n> too large for size` |
| vector shorter than `minlen` | structure | `value <n> too small for minimum <m>` |
| vector longer than `maxlen` | structure | `value <n> too large for maximum <m>` |
| input ends inside a scalar | syntax | `truncated uint8` / `uint16` / `uint24` / `uint32` / `uint64` |
| input ends inside a length prefix | syntax | `truncated variable-length integer` |
| input ends inside a vector body | syntax | `truncated slice` |
| input ends inside a fixed array | syntax | `truncated array` |
| selector value with no branch | syntax | `<selector>: unhandled value for selector` |
| a Go type the codec cannot handle | structure | `unsupported type: <type> of kind <kind>` |
| `Uint24` value ≥ 2²⁴ on marshal | structure | `uint24 overflow <n>` |
| enum with no sizing tag encoded as the **top-level** value (passed to `Marshal`, or to `MarshalWithParams` whose params string carries no `size`/`maxval`) | structure | `enum field tag missing` |
| untagged enum or slice **field**, on unmarshal (§2) | structure | `no field size information available` |
| slice field with no `maxlen` | structure | `slice field tag missing` |

A verifier group may assert on the `tls: structure error: ` / `tls: syntax
error: ` prefix and on the substrings above. It never asserts on the Go error
type.

---

## 5. The CT structures written in this language

These live in the **root** package `github.com/google/certificate-transparency-go`
(import name `ct`), not in `tls/`, but their encoding is entirely determined by
the rules above. RFC 6962 §3.2–3.5 gives the presentation-language source;
what follows is the Go shape the verifier links against. Field order is the
encoding order.

### 5.1 Scalar and enum types

| Go type | underlying | tag used at point of use | values |
| --- | --- | --- | --- |
| `Version` | `tls.Enum` | `maxval:255` | `V1` = 0 |
| `LogEntryType` | `tls.Enum` | `maxval:65535` | `X509LogEntryType` = 0, `PrecertLogEntryType` = 1, `XJSONLogEntryType` = 0x8000 (experimental) |
| `MerkleLeafType` | `tls.Enum` | `maxval:255` | `TimestampedEntryLeafType` = 0 |
| `SignatureType` | `tls.Enum` | `maxval:255` | `CertificateTimestampSignatureType` = 0, `TreeHashSignatureType` = 1 |
| `SHA256Hash` | `[32]byte` | none (fixed) | — |
| `CTExtensions` | `[]byte` | `minlen:0,maxlen:65535` | — |
| `ASN1Cert` | struct with one field `Data []byte` | `minlen:1,maxlen:16777215` | a 3-byte-prefixed DER certificate |
| `PreCert` | struct: `IssuerKeyHash [32]byte`, `TBSCertificate []byte` (`minlen:1,maxlen:16777215`) | — | RFC 6962 §3.2 |
| `LogID` | struct with one field `KeyID [32]byte` | none (fixed) | — |
| `DigitallySigned` | a local alias of `tls.DigitallySigned` carrying JSON methods | — | — |

The tag column is the tag written on the field wherever the type appears in
the structures of §5.2–§5.5 and in `TreeHeadSignature` (§5.3).
`SignedTreeHead` (§5.6) is the one structure in this section where that column
does **not** apply, and it is the only untagged enum field the codec is ever
asked to encode: its `Version` carries no tag at all, deliberately — that is
what §2's untagged-field rule is for, and it is why a whole `SignedTreeHead`
is 88 bytes rather than 89. (`AddChainResponse.SCTVersion` (§3.2) is
untagged too, but that struct is JSON-only and never reaches this codec.)

`XJSONLogEntryType` (0x8000) and its `JSONDataEntry` branch are an
**experimental, non-RFC** extension present from v1.0 onwards. They are part of
the variant table for `TimestampedEntry` and `CertificateTimestamp`, so a codec
that omits the branch will not round-trip the upstream type declaration, but no
endpoint is required to accept an `add-json` submission (see HTTP-API.md §7).

#### `String()` on the four enums

Each of the four carries a `String() string` method, so it renders through
`%v` and `fmt.Stringer` rather than as a bare number. Three of them return the
Go constant's own name; **`SignatureType` does not**, and that is the one to
get right:

| type | value | `String()` |
| --- | --- | --- |
| `Version` | `V1` | `V1` |
| `LogEntryType` | `X509LogEntryType` | `X509LogEntryType` |
| `LogEntryType` | `PrecertLogEntryType` | `PrecertLogEntryType` |
| `LogEntryType` | `XJSONLogEntryType` | `XJSONLogEntryType` (v1.0 .. v1.1.0; the arm goes with the xjson removal at v1.1.2) |
| `MerkleLeafType` | `TimestampedEntryLeafType` | `TimestampedEntryLeafType` |
| `SignatureType` | `CertificateTimestampSignatureType` | **`CertificateTimestamp`** |
| `SignatureType` | `TreeHashSignatureType` | **`TreeHash`** |

An unrecognised value falls through to a parenthesised numeric form —
`UnknownVersion(%d)`, `UnknownEntryType(%d)`, `UnknownLeafType(%d)`,
`UnknownSignatureType(%d)` — with the value in decimal. The two
`SignatureType` renderings are RFC 6962 §3.2's `certificate_timestamp` and
`tree_hash` written in Go case, not the constant names, and they are the same
at every release from v1.0 to v1.3.3.

### 5.2 `MerkleTreeLeaf` (RFC 6962 §3.4)

    struct {
      Version         version;          // 1 byte, 0 for v1
      MerkleLeafType  leaf_type;        // 1 byte, 0 = timestamped_entry
      select (leaf_type) {
        case timestamped_entry: TimestampedEntry;
      }
    } MerkleTreeLeaf;

    struct {
      uint64          timestamp;        // 8 bytes, milliseconds since the epoch
      LogEntryType    entry_type;       // 2 bytes
      select (entry_type) {
        case x509_entry:    ASN1Cert;   // 3-byte-prefixed DER
        case precert_entry: PreCert;    // 32-byte issuer key hash + 3-byte-prefixed DER TBS
      }
      CtExtensions    extensions;       // 2-byte-prefixed opaque
    } TimestampedEntry;

Go field names, which the verifier uses: `MerkleTreeLeaf{Version, LeafType,
TimestampedEntry}`; `TimestampedEntry{Timestamp, EntryType, X509Entry,
PrecertEntry, JSONEntry, Extensions}`.

### 5.3 The two signature inputs

RFC 6962 §3.2 (SCT) and §3.5 (STH). Both begin `version, signature_type` and
both are serialised with this same codec; the resulting bytes are what
`tls.CreateSignature` signs.

    struct {                            // CertificateTimestamp — the SCT input
      Version        sct_version;       // 1 byte
      SignatureType  signature_type;    // 1 byte, 0 = certificate_timestamp
      uint64         timestamp;         // 8 bytes
      LogEntryType   entry_type;        // 2 bytes
      select (entry_type) { … }         // exactly as in TimestampedEntry
      CtExtensions   extensions;
    }

    struct {                            // TreeHeadSignature — the STH input
      Version        version;           // 1 byte
      SignatureType  signature_type;    // 1 byte, 1 = tree_hash
      uint64         timestamp;         // 8 bytes
      uint64         tree_size;         // 8 bytes
      opaque         sha256_root_hash[32];
    }

Observe that a `CertificateTimestamp` and the `TimestampedEntry` of the
corresponding `MerkleTreeLeaf` share their tail exactly: the SCT signature
input is the leaf's timestamped entry with a two-byte `(version,
signature_type)` prefix instead of a two-byte `(version, leaf_type)` prefix.

### 5.4 Chain and entry wrappers

| Go type | fields | tags |
| --- | --- | --- |
| `PrecertChainEntry` | `PreCertificate ASN1Cert`, `CertificateChain []ASN1Cert` | `minlen:1,maxlen:16777215` and `minlen:0,maxlen:16777215` |
| `CertificateChain` | `Entries []ASN1Cert` | `minlen:0,maxlen:16777215` |
| `JSONDataEntry` | `Data []byte` | `minlen:0,maxlen:1677215` |
| `PrecertChainEntryHash` (**from v1.2.0**) | `PreCertificate ASN1Cert`, `IssuanceChainHash []byte` | `minlen:1,maxlen:16777215` and `minlen:0,maxlen:256` |
| `CertificateChainHash` (**from v1.2.0**) | `IssuanceChainHash []byte` | `minlen:0,maxlen:256` |

The `JSONDataEntry` bound is **1677215**, one digit short of 16777215. That is
what upstream has at every anchor; reproduce it. It makes the prefix 3 bytes
either way, so it is observable only through the boundary error message.

The last two rows are the hash-bearing alternatives **v1.2.0** adds beside the
first two, for a log configured to keep issuance chains outside the leaf
(CONFIG.md §2.6). They are ordinary members of this codec and obey every rule
of sections 2 and 3; the only thing to notice is the `maxlen:256` on the hash,
which by section 2's derivation rule makes its length prefix **two** bytes —
256 does not fit in one — where the chain vectors' is three.
`CertificateChainHash` carries no certificate at all — the certificate is
already in the Merkle leaf — while `PrecertChainEntryHash` keeps the
precertificate, because that is the only place it lives. An empty
`IssuanceChainHash` is well-formed and encodes as the two bytes `00 00`, and a
32-byte SHA-256 hash encodes as `00 20` followed by the hash.

### 5.5 `SignedCertificateTimestamp`

    struct {
      Version         sct_version;      // 1 byte
      LogID           id;               // 32 bytes, no prefix
      uint64          timestamp;        // 8 bytes
      CtExtensions    extensions;       // 2-byte-prefixed opaque
      digitally-signed signature;       // hash(1) + sig(1) + 2-byte-prefixed opaque
    } SignedCertificateTimestamp;

Go field names: `SCTVersion`, `LogID`, `Timestamp`, `Extensions`, `Signature`.
This is the structure that is TLS-encoded into the SCT-list X.509 extension and
that `sctcheck`/`sctscan` recover from a served certificate; the JSON
`add-chain` response carries the same five values field by field instead
(HTTP-API.md §3.1).

### 5.6 Types that are *not* a wire blob

`LogEntry` and `SignedTreeHead` are Go-side aggregates: neither of them is
handed to a log or returned by one as a single opaque TLS-encoded value, and
neither appears as a unit in any wire format of HTTP-API.md §3.
`SignedTreeHead` and the `AddChainResponse` family are JSON structures
(HTTP-API.md §3); `LogEntry` is the parsed result of a `get-entries` row —
`Index`, `Leaf`, exactly one of `X509Cert`/`Precert`/`JSONData`, and `Chain` —
and deliberately does **not** match any structure in RFC 6962.

That is a statement about wire formats, not about the codec. `SignedTreeHead`
is an ordinary struct built out of types §5.1 already describes, in this
order: `Version`, `TreeSize uint64`, `Timestamp uint64`, `SHA256RootHash` (a
fixed 32-byte array), `TreeHeadSignature` (a `DigitallySigned`), `LogID` (a
fixed 32-byte array) — CLIENT-API.md §3. `tls.Marshal` therefore encodes it
field by field under the rules above, and only its `Version` field is
unusual: it carries no tag, so by §2 it contributes no bytes. With the pinned
8-byte `DigitallySigned` (one byte of hash, one of signature, a two-byte
length prefix and four bytes of body) the result is exactly **88 bytes** —
tree_size(8) timestamp(8) root(32) signature(8) log_id(32) — and that
encoding is load-bearing: the witness co-signature introduced at **v1.1.3**
is computed over it and over nothing else (CLIENT-API.md §6.3). A
`SignedTreeHead` whose `Version` was tagged like every other `Version` field
would still round-trip through JSON and still sign the wrong bytes.
`LogEntry`, by contrast, holds a `*MerkleTreeLeaf` and a parsed chain, and
nothing encodes it as a whole.

`SHA256Hash` and `DigitallySigned` carry `MarshalJSON`/`UnmarshalJSON` that
render them as base64 strings, plus `FromBase64String` and `Base64String`
helpers. A `DigitallySigned` in JSON is the base64 of its **TLS encoding**, not
of the raw signature bytes. The four helpers, whose shapes are *not* symmetric
and are the same at every release v1.0 .. v1.3.3:

    func (s *SHA256Hash) FromBase64String(b64 string) error
    func (s SHA256Hash) Base64String() string              // one value, no error
    func (d *DigitallySigned) FromBase64String(b64 string) error
    func (d DigitallySigned) Base64String() (string, error)   // two values

`SHA256Hash.Base64String` cannot fail — the value is a fixed 32 bytes.
`DigitallySigned.Base64String` re-runs the TLS marshal and so can. Both
`FromBase64String` methods take a pointer receiver and reject input that does
not decode; `SHA256Hash.FromBase64String` additionally rejects a decoded length
other than 32.

---

## 6. Reading route

1. `rfc/rfc5246.txt` §4.1–4.3 for integers and vectors, §4.6.1 for variants,
   §4.7 for `digitally-signed`.
2. §2 and §3 of this page for the Go mapping and tag grammar.
3. `rfc/rfc6962.txt` §3.2–3.5 alongside §5 of this page for the CT structures.
4. HTTP-API.md §3 for where each encoded structure appears on the wire.
