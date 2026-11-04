# Specification notes — step 03 (`03-reactor-and-events`)

First-party notes on the normative facts this step is graded against. Every
statement below is my own summary of a numbered section of the Tor
specifications; no specification text is reproduced. Read the cited section if
you need the full wording.

All citations are against torspec at the pinned commit
`1db24dddbf2203f2a465747302f133ef3ed6eed4`. The URL pattern is

```
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/<file>#L<line>
```

---

## 1. Channel cell framing

**tor-spec §3, "Cell Packet format"** —
[tor-spec.txt#L488](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L488)

A fixed-length cell carries, in this order: the circuit id, a one-byte command,
and a payload padded out to a constant width. The payload width constant is
509 bytes, defined among the constants in tor-spec §0.2 ("Security
parameters") rather than in §3 itself —
[tor-spec.txt#L135](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L135).
The total cell width therefore depends only on the circuit-id width: 512 bytes
under link protocols below 4, 514 bytes from link protocol 4 onward, again
from tor-spec §0.2 —
[tor-spec.txt#L137](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L137).

A variable-length cell replaces the fixed payload with a two-byte big-endian
length followed by exactly that many payload bytes; the circuit id and command
fields are unchanged —
[tor-spec.txt#L503](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L503).

**Circuit-id width.** Two bytes for link protocols 1, 2 and 3; four bytes from
link protocol 4 onward. Crucially, the first VERSIONS cell — and anything sent
before it — always uses the two-byte form, because at that point no protocol
version has been agreed —
[tor-spec.txt#L517](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L517).

**Which commands are variable-length.** Command 7 is variable-length on every
connection that has one; from link protocol 3 onward, so is every command
numbered 128 or above —
[tor-spec.txt#L512](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L512).

**Command numbers used by this step's cases.** From the fixed-length table —
[tor-spec.txt#L528](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L528):

| Number | Name | Kind |
| --- | --- | --- |
| 0 | PADDING | fixed-length |
| 8 | NETINFO | fixed-length |

From the variable-length table —
[tor-spec.txt#L541](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L541):

| Number | Name | Kind |
| --- | --- | --- |
| 7 | VERSIONS | variable-length |
| 128 | VPADDING | variable-length |
| 129 | CERTS | variable-length |

Padding cells exist precisely so that a peer may send them without conveying
anything; a receiver must ignore their contents, and a VERSIONS payload in
particular must not be padded —
[tor-spec.txt#L508](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L508).
An implementation that drops the connection when it meets a VPADDING or PADDING
cell mid-handshake is wrong.

## 2. Version negotiation

**tor-spec §4.1, "Negotiating versions with VERSIONS cells"** —
[tor-spec.txt#L638](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L638)

The payload of a VERSIONS cell is a sequence of two-byte big-endian integers and
nothing else. The initiator sends its cell as soon as the TLS handshake is done;
the responder answers with its own. Each side then selects the numerically
highest version present in *both* lists. If the two lists share no version, the
connection cannot be used and must be closed. A malformed cell — an odd payload
length, for instance — is likewise fatal —
[tor-spec.txt#L659](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L659).

The version numbers themselves are enumerated at
[tor-spec.txt#L678](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L678):
2 introduced variable-length cells, 3 introduced the in-protocol handshake, 4
widened the circuit id to four bytes, and 5 added link padding negotiation.
Consequently a client that offers only version 4 is offering a link protocol in
which the *reply* uses four-byte circuit ids even though its own opening cell
used two.

## 3. CERTS cells

**tor-spec §4.2, "CERTS cells"** —
[tor-spec.txt#L688](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L688)

A CERTS cell is variable-length. Its payload opens with a one-byte count, then
repeats that many times: a one-byte certificate type, a two-byte length, and
that many certificate bytes. A count of zero is therefore a well-formed, empty
CERTS payload of exactly one byte. Trailing bytes past the last certificate are
to be ignored —
[tor-spec.txt#L693](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L693).
No more than one certificate of any given type may appear in one cell —
[tor-spec.txt#L714](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L714).
A second CERTS cell on one connection is a protocol violation: the cell is what
declares the peer's keys, and there is only one such declaration per handshake.

## 4. NETINFO cells

**tor-spec §4.5, "NETINFO cells"** —
[tor-spec.txt#L939](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L939)

Once version 2 or higher has been agreed, each side sends the other a NETINFO
cell. Its payload is, in order: a four-byte timestamp; the peer's address as a
one-byte type, a one-byte length and that many value bytes; a one-byte count of
the sender's own addresses; then that many addresses in the same
type/length/value shape —
[tor-spec.txt#L944](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L944).

Address type 4 means IPv4 and its length field must be 4; type 6 means IPv6 and
its length must be 16. A length that disagrees with its type means the address
is to be disregarded —
[tor-spec.txt#L955](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L955).

The timestamp counts seconds since the Unix epoch, big-endian; a client is
advised to send zero rather than its real clock so as not to make itself
distinguishable —
[tor-spec.txt#L964](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L964).
This is why the scripted relay in this step's driver sends a zero timestamp: it
is both conforming and clock-free.

The handshake is therefore complete, from the client's point of view, once
VERSIONS has been exchanged, one CERTS cell has arrived, and a NETINFO cell has
arrived. A stream that ends before the NETINFO cell has not completed a
handshake.

## 5. Optimistic data

**tor-spec §6.2, "Opening streams and transferring data"** —
[tor-spec.txt#L1756](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L1756)

Against an exit that does not support optimistic data, a client must wait for
the exit's acknowledgement of a stream before it may send any payload on that
stream. Against an exit that does support it, the client is permitted to send
data cells immediately after the begin cell, without waiting to learn whether
the stream opened at all —
[tor-spec.txt#L1819](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L1819).
The exit queues such early data and either replays it when the stream opens or
discards it if the stream fails —
[tor-spec.txt#L1827](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L1827).

Optimism is thus a per-stream choice made at the moment the stream is begun,
not a property of the circuit or the channel — which is why it belongs in a
per-stream parameter object rather than in any connection-wide configuration.

**socks-extensions §5, "Optimistic data"** —
[socks-extensions.txt#L100](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/socks-extensions.txt#L100)
restates the same trade-off from the SOCKS side: the client saves one
round trip, at the cost of having to cope with a failure reported after it has
already sent its request. A client that turns optimism on must therefore not
treat "data already written" as "stream established".

**tor-spec §6.2.1, "Opening a directory stream"** —
[tor-spec.txt#L1838](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L1838) —
covers the directory case: a relay serving directory documents answers a
directory-begin cell as though it had been asked to connect to its own
directory port. Directory requests are the classic client-speaks-first exchange
(an HTTP GET), so they are exactly the case that optimism pays for.

This checkpoint acts on that: its directory-stream constructor opens
optimistically and offers no way to ask for anything else. That decision is
internal to the constructor. It is **not** the default carried by the
per-stream parameter object, which stays non-optimistic, and the two must not
be conflated — see `TEST_INTERFACE.md` §5, which records why the directory
behaviour is not graded here.

## 6. Directory request URLs

**dir-spec Appendix B, "General-use HTTP URLs"** —
[dir-spec.txt#L4032](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L4032)

A single server descriptor is addressed by the hex encoding of its descriptor
digest under the path prefix `/tor/server/d/` —
[dir-spec.txt#L4114](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L4114).
Several descriptors are requested at once by joining their digests with `+`
under the same prefix —
[dir-spec.txt#L4119](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L4119).
The concatenation of every descriptor the server knows is at
`/tor/server/all` —
[dir-spec.txt#L4133](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L4133).
(Descriptors may also be addressed by relay identity fingerprint under
`/tor/server/fp/`, but clients are told not to fetch them that way, because it
lets a colluding cache hand one client a descriptor nobody else sees —
[dir-spec.txt#L4109](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L4109).)

**The `.z` suffix.** dir-spec §6.1, "HTTP headers" —
[dir-spec.txt#L3952](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L3952) —
names the compression algorithms a directory server may use, and explains that a
client which sends no `Accept-Encoding` header gets a deflated document if the
requested URL ends in `.z` and an uncompressed one otherwise —
[dir-spec.txt#L3964](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L3964).
Appending `.z` is therefore how a request asks for a compressed reply, and
arti's descriptor requests do so on every URL. Requests that *do* send
`Accept-Encoding` list `deflate` and `identity` at minimum; anonymous requests
are told not to advertise anything beyond `deflate`, since the header set is
itself a fingerprint —
[dir-spec.txt#L3970](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L3970).

Ordering: nothing in dir-spec requires the digests in a multi-digest URL to be
sorted, but a request whose URL depends on insertion order is needlessly
distinguishable and needlessly uncacheable, so arti emits them in ascending
order. The graded behaviour is the sorted form.

## 7. Path constraints behind the default configuration

**path-spec §2.2, "Path selection and constraints"** —
[path-spec.txt#L303](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/path-spec.txt#L303)

Among the constraints every generated path must satisfy: no relay may appear
twice in one path —
[path-spec.txt#L311](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/path-spec.txt#L311) —
and, unless the operator has disabled the rule, no two relays in one path may
come from the same /16 IPv4 network —
[path-spec.txt#L315](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/path-spec.txt#L315).

The /16 rule is the origin of the default IPv4 subnet-family prefix length of
16 in arti's path configuration. The specification of this vintage states no
corresponding IPv6 rule; arti's default prefix length of 32 for IPv6 is an
implementation choice which this step grades as a configuration default, not as
a specification requirement.

The "avoid this relay" restriction attached to a guard request is the same
no-relay-twice constraint seen from the guard's side: if a relay is already
committed to elsewhere in the path under construction, the guard chosen for
that path must not be it.

## 8. Streams, isolation and guards

**path-spec §3, "Attaching streams to circuits"** —
[path-spec.txt#L733](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/path-spec.txt#L733)

A request is attached to a circuit that can carry it, and the appropriate begin
or resolve cell is sent on it. Whether a given circuit "can carry" a given
request is where isolation lives: a client that must not let two activities
share a circuit expresses that by refusing to consider them compatible. arti
models this with opaque tokens — two requests may share a circuit only when
their tokens agree — so the meaningful, testable properties are that freshly
minted tokens never collide, that a token equals itself, and that the
no-isolation token is distinct from every minted one.

**guard-spec §2, "State instances"** —
[guard-spec.txt#L82](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/guard-spec.txt#L82) —
describes the guard algorithm's variables as an object with persistent and
non-persistent parts, of which more than one instance may exist at a time.
Persistence is not incidental to guards: a guard set that is forgotten across
restarts defeats the point of having guards at all. That is what makes the
state manager, its exclusive lock, and its typed handles part of the graded
surface here rather than an implementation detail.

**guard-spec §4.6, "Selecting guards for circuits"** —
[guard-spec.txt#L466](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/guard-spec.txt#L466) —
is the section behind the guard-request vocabulary: a request says what the
circuit is for, and the outcome reported afterwards says whether the guard
worked, failed, could not be judged, or was never really tried.

## 9. Link-handshake certificates

The CERTS cell of section 3 is a container; what it carries is four
certificates, and what the client does with them is the whole of link
authentication. This step's driver builds that chain itself, so the formats
are stated here in full.

**tor-spec §4.2, certificate types** — [tor-spec.txt#L702](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L702) —
lists the types a CERTS cell may carry. Four of them authenticate a relay that
has an Ed25519 identity: type 2, the self-signed RSA1024 identity certificate;
type 4, the Ed25519 signing key signed by the Ed25519 identity key — [tor-spec.txt#L705](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L705);
type 5, the TLS link certificate signed by the signing key; and type 7, the
Ed25519 identity signed by the RSA identity. Types 1 to 3 are DER-encoded
X.509; the rest use the cert-spec formats below, and type 7 differs again from
types 4 to 6. No more than one certificate of any one type may appear.

The client's obligations are enumerated as a list of MUSTs — [tor-spec.txt#L718](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L718). In order:
exactly one certificate of each of types 2, 4, 5 and 7 — [tor-spec.txt#L720](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L720); nothing
expired; every signature correct; the key certified by the signing-to-link
certificate equals the SHA-256 digest of the certificate that authenticated the
TLS connection — [tor-spec.txt#L730](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L730); the identity key named in the identity-to-signing
certificate is the key that signed it; and the cross-certificate certifies the
Ed25519 identity and is signed with the RSA identity from the identity
certificate — [tor-spec.txt#L737](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L737).

**cert-spec §2.1, "Ed25519 Certificates"** — [cert-spec.txt#L48](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/cert-spec.txt#L48)

Types 4 and 5 use one compact format: a one-byte VERSION holding `01`, a
one-byte CERT_TYPE, a four-byte EXPIRATION_DATE, a one-byte CERT_KEY_TYPE, a
32-byte CERTIFIED_KEY, a one-byte N_EXTENSIONS, that many extensions, then a
64-byte SIGNATURE — [cert-spec.txt#L55](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/cert-spec.txt#L55). The expiration date counts **hours** since the Unix
epoch, not seconds — [cert-spec.txt#L69](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/cert-spec.txt#L69). The signature covers every byte of the certificate
that precedes it.

Each extension is a two-byte ExtLength, a one-byte ExtType, a one-byte
ExtFlags, and ExtLength bytes of data — [cert-spec.txt#L76](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/cert-spec.txt#L76).

CERT_KEY_TYPE says what the 32 bytes of CERTIFIED_KEY are: `01` an Ed25519
public key, `02` the SHA-256 of an RSA key, `03` the SHA-256 of an X.509
certificate, which is the value type 5 uses — [cert-spec.txt#L188](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/cert-spec.txt#L188). (Tor before 0.4.5.1-alpha
wrote `01` for every type, so an implementation should accept `01` in a type-5
certificate and infer the real key type from CERT_TYPE.)

**cert-spec §2.2.1, "Signed-with-ed25519-key extension [type 04]"** — [cert-spec.txt#L100](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/cert-spec.txt#L100)

This is how a certificate carries the key that signed it: ExtLength is 32 and
ExtData is an Ed25519 public key — [cert-spec.txt#L108](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/cert-spec.txt#L108). When present it MUST match the key
the signature was actually made with. A type-4 certificate needs it, because
the identity key is not otherwise anywhere in the handshake.

**cert-spec §2.3, "RSA->Ed25519 cross-certificate"** — [cert-spec.txt#L113](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/cert-spec.txt#L113)

Type 7 has a different shape: a 32-byte ED25519_KEY, a four-byte
EXPIRATION_DATE, a one-byte SIGLEN, and SIGLEN bytes of SIGNATURE — [cert-spec.txt#L118](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/cert-spec.txt#L118).
The signature is computed over the SHA-256 of the non-signature part of the
certificate — the first 36 bytes — prefixed with the literal string
`Tor TLS RSA/Ed25519 cross-certificate` — [cert-spec.txt#L128](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/cert-spec.txt#L128). The direction matters: the
RSA key vouches for the Ed25519 key and not the other way round, because the
1024-bit RSA key is the weaker of the two.
