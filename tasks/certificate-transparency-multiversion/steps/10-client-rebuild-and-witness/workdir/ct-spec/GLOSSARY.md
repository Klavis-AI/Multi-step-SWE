# GLOSSARY

The vocabulary used by the other pages, by RFC 6962, and by the product's own
identifiers. Where a term has a precise definition elsewhere in this corpus the
entry says where; where a term is commonly confused with another, the entry
says which.

Terms are alphabetical. RFC section numbers refer to `rfc/rfc6962.txt` unless
another RFC is named.

---

**add-chain / add-pre-chain** — the two write endpoints. `add-chain` submits an
ordinary certificate chain, `add-pre-chain` submits a precertificate chain.
Both return an SCT. RFC 6962 §4.1, §4.2; HTTP-API.md §3.

**admission control** — the decision of whether the log will accept a
submission at all: does the chain reach a trusted root, is the leaf expired,
does it carry a forbidden extension, is it too old. Distinct from validation of
the chain's cryptography. HTTP-API.md §5.

**ASN1Cert** — a single DER-encoded certificate as it appears on the wire, a
length-prefixed opaque byte string in TLS encoding and a base64 string in JSON.
Not a parsed certificate. TLS-CODEC.md §5.

**audit path** — the ordered list of sibling hashes in an inclusion proof,
bottom (leaf side) first. The JSON field name in `get-proof-by-hash` and
`get-entry-and-proof` is `audit_path`. MERKLE.md §2.

**backend** — the Merkle-tree service the frontend talks to over gRPC. It owns
the tree, the leaves, the sequencing and the signing of tree heads. Not part of
this product. ARCHITECTURE.md §2.

**backoff** — the client's delay before retrying a failed request. Doubling
with a 128-second ceiling, plus jitter from v1.0.5. CLIENT-API.md §1.1.

**base64** — every binary field in the JSON API is standard base64 with
padding, not URL-safe base64 and not hex. HTTP-API.md §2.

**consistency proof** — the evidence that a larger tree contains everything a
smaller tree contained, in the same order. Between sizes n1 and n2 with
n1 <= n2. RFC 6962 §2.1.2; MERKLE.md §3.

**cosigned STH** — an STH to which a witness has added its own signature, so
that a second party attests to having seen that head. Only meaningful while
`internal/witness` exists. CLIENT-API.md §6.3.

**CTFE** — Certificate Transparency Front End. The HTTP personality: it speaks
RFC 6962 to the world and gRPC to the backend, validates chains, issues SCTs
and reshapes proofs. The thing `ct_server` runs. ARCHITECTURE.md §1.

**DigitallySigned** — the TLS structure carrying a hash algorithm, a signature
algorithm and an opaque signature. RFC 5246 §4.7; TLS-CODEC.md §5.1. Its
JSON form is the base64 of its TLS encoding, not a JSON object.

**distributor** — the component of `submission/` that spreads one submission
across the logs a policy requires and collects the SCTs.
CLIENT-API.md §7.2.

**DNS API** — the experimental read interface that answers STH, consistency
and inclusion queries from TXT records. Present v1.0.13 to v1.1.0.
CLIENT-API.md §6.1.

**entrypoint** — the product's name for one of the endpoints, used as a metric
label and a request-log field. Note the two spellings and do not confuse them:
the **URL path suffixes** are the lower-case hyphenated strings of HTTP-API.md
§1 (`add-chain`, `get-sth`, …), while the **`EntrypointName` values** are the
CamelCase constants of CLIENT-API.md §8.1 (`AddChainName`, `GetSTHName`, …),
rendered by `String()` as exactly those spellings. The metric label and the
request-log field carry the CamelCase form. HTTP-API.md §1; CLIENT-API.md §8.1.

**EKU** — Extended Key Usage. An X.509 extension naming the purposes a
certificate may be used for. A log may be configured to accept only chains
whose leaf carries a permitted EKU. HTTP-API.md §5.

**extra data** — the second half of a log entry: for a certificate entry, the
issuance chain; for a precertificate entry, the precertificate plus the chain.
JSON field `extra_data`. Its layout is where the v1.2.0 storage milestone bites.
TLS-CODEC.md §5.4; CLIENT-API.md §8.4.

**frozen STH** — a fixed tree head a log serves forever, instead of asking the
backend; the log is not read-only, and its write endpoints still accept
submissions (HTTP-API.md §6.2). CONFIG.md §2; CLIENT-API.md §8.3.

**gossip** — the family of techniques for detecting a log presenting different
views to different clients, by having those clients exchange what they saw.
CLIENT-API.md §6.2.

**inclusion proof** — the evidence that a particular leaf is in a particular
tree at a particular index. RFC 6962 §2.1.1; MERKLE.md §2.

**issuance chain** — the chain of CA certificates between a submitted leaf and
a trusted root, stored as a log entry's extra data. From v1.2.0 it may be
stored outside the leaf and referenced by hash. CLIENT-API.md §8.4.

**leaf hash** — `SHA-256(0x00 ‖ TLS-encoding-of-MerkleTreeLeaf)`. The `hash`
parameter of `get-proof-by-hash` is the base64 of this. Not the hash of the
certificate. MERKLE.md §1.

**lax** — the struct tag that tells the forked `asn1` parser to accept a
specific malformation and record it rather than fail. X509-FORK.md §2.

**log id** — `SHA-256` of the log's DER-encoded `SubjectPublicKeyInfo`, 32
bytes. Identifies a log in an SCT. **Not** the same as a **tree id**, which is
the backend's own integer handle for a tree. Confusing the two is a classic
error, and the `logid/` package exists to make it harder.
CLIENT-API.md §5.5; CONFIG.md §2.

**log list** — the published JSON catalogue of known CT logs. Three
incompatible schemas over the product's life: v1, v2, v3. CLIENT-API.md §4.

**MerkleTreeLeaf** — the TLS structure that *is* the leaf: version, leaf type,
and a `TimestampedEntry`. Its encoding is the pre-image of the leaf hash.
TLS-CODEC.md §5.2.

**MMD** — Maximum Merge Delay. The interval within which a log promises to
incorporate a submission it has issued an SCT for. Carried in the log list and
in `LogInfo`; the product does not enforce it. CLIENT-API.md §2.1.

**mirror** — a CTFE serving a copy of another log's tree, read-only, with the
original log's key. ARCHITECTURE.md §2; HTTP-API.md §6.

**NonFatalErrors** — the error type accumulating problems that were recorded
but did not stop parsing. A parse can return **both** a usable certificate and
a non-nil error, and callers must check with the type's own predicate rather
than `err != nil`. X509-FORK.md §3.

**poison extension** — the critical extension, OID `1.3.6.1.4.1.11129.2.4.3`,
that makes a precertificate unusable as a certificate. Removed when building
the TBS structure the SCT signs. RFC 6962 §3.1; X509-FORK.md §4.

**precertificate** — a certificate-shaped object signed by the issuer (or by a
special precertificate signing certificate) carrying the poison extension,
submitted to a log so that the resulting SCT can be embedded in the real
certificate. RFC 6962 §3.1; X509-FORK.md §4.

**presentation language** — the notation TLS uses to define wire structures:
fixed and variable vectors with explicit length bounds, enums with explicit
widths, and discriminated unions. RFC 5246 §4; TLS-CODEC.md §2.

**quota** — the backend's accounting of how much a given user may consume.
Exposed by the frontend from v1.0.19 as a per-request "quota user", and
surfaced as HTTP 429 from v1.2.0. ARCHITECTURE.md §2; HTTP-API.md §6.1.

**Retry-After** — the HTTP header a 503 or 429 may carry, parsed first as an
integer number of seconds and then as an RFC 1123 date.
CLIENT-API.md §1.1.

**RPKI** — Resource Public Key Infrastructure. RFC 3779 address and AS-number
extensions, parsed by the fork from v1.0.21. X509-FORK.md §5.3.

**SCT** — Signed Certificate Timestamp. The log's signed promise to include a
submission within its MMD: version, log id, timestamp, extensions, signature.
RFC 6962 §3.2; TLS-CODEC.md §5.5.

**SIA** — Subject Information Access. The X.509 extension the fork learns to
parse at v1.0.21. X509-FORK.md §5.3.

**STH** — Signed Tree Head. A signed statement of the tree's size, timestamp
and root hash at a moment. RFC 6962 §3.5; TLS-CODEC.md §5.3.

**temporal log / shard** — a log that accepts only certificates whose
`NotAfter` falls inside a configured window, so that a log can be retired when
its window passes. Several shards together form one logical log.
CONFIG.md §2; CLIENT-API.md §1.4.

**tiled log** — a log served as static tiles rather than through the RFC 6962
HTTP API, addressed by a submission URL and a monitoring URL. Appears in the
log list at v1.3.3. CLIENT-API.md §4.3.

**TimestampedEntry** — the part of a `MerkleTreeLeaf` carrying the timestamp,
the entry type, the entry itself and the extensions. TLS-CODEC.md §5.2.

**TBS** — To Be Signed. The inner structure of a certificate that the issuer's
signature covers. Rebuilding it correctly, minus the poison extension, is what
makes an embedded SCT verify. X509-FORK.md §4.

**tree head signature** — the signature inside an STH, over a
`TreeHeadSignature` structure, not over the JSON. TLS-CODEC.md §5.3.

**tree id** — the backend's integer identifier for a tree. See **log id**.

**variable-length vector** — a sequence whose length is written before it, in
the smallest number of bytes able to express the declared maximum. The single
most common source of encoding bugs. TLS-CODEC.md §2.

**witness** — a service that remembers the newest STH it has seen for a log and
co-signs it, so that a split view can be detected. Present v1.1.3 to v1.2.0.
CLIENT-API.md §6.3.
