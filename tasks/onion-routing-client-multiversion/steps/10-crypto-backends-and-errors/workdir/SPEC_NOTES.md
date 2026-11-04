# Spec notes — step 10

Where this step's behaviour comes from, and where it does not.

Everything cited here is read from the pinned 2022 torspec corpus: torspec at
commit `1db24dddbf2203f2a465747302f133ef3ed6eed4`, dated 2022-11-28, which is
the revision contemporary with the arti release this checkpoint targets. Line
anchors are into that commit and nothing else; upstream has moved since, and a
line number from today's torspec will not land where these do. Every anchor is
written as

```
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/<file>#L<line>
```

The prose below is this package's own. The specs are quoted by reference, never
by transcription — if you want the exact words, follow the link.

A large part of step 10 has no spec behind it at all, and saying so is half the
point of this document. Section 11 lists what those parts are and what governs
them instead. Do not go looking for a torspec paragraph about `ErrorKind`; it
does not exist, and inventing one will lead you to the wrong design.

## 1. Why encoding is fallible now

Step 09's encoders could not fail. Step 10's return `Result` everywhere an
object is written. That is not a stylistic change, and it is not defensive
programming for its own sake: the wire format has hard, small limits, and a
value that does not fit one of them cannot be encoded at all.

The cell payload is fixed. A Tor cell carries a payload of exactly 509 bytes,
and the whole cell is 512 bytes on link protocols before version 4 and 514
bytes from version 4 onward, because the circuit ID field grows from two bytes
to four.

**tor-spec.txt §0.2 ("Security parameters"), L135:**
[tor-spec.txt#L135](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L135)

**tor-spec.txt §0.2, the cell length definition, L137:**
[tor-spec.txt#L137](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L137)

**tor-spec.txt §3 ("Cell Packet format"), the circuit-ID width rule, L517:**
[tor-spec.txt#L517](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L517)

509 is where `CELL_DATA_LEN` comes from, and it is the reason `Data::MAXLEN` is
`CELL_DATA_LEN - 11`: eleven bytes of the relay payload are header (section 5
below), leaving 498 for the caller's bytes. Handing `Data::new` 499 bytes is
not a bug in the caller's arithmetic that a panic should announce — it is a
perfectly reasonable request that the wire format cannot serve, so it comes
back as an error the caller can act on.

The same reasoning drives `NestedWriter::finish`. A length-prefixed field
describes its body with a one-, two-, or four-byte count. A body too long for
that count has no encoding. `finish()` therefore returns
`EncodeError::BadLengthValue` rather than truncating, and — this is the part
the tests are strictest about — it must leave the outer buffer untouched, so a
caller that recovers from the error does not transmit a half-written structure.

`Versions::new` is fallible for the same reason and nothing else: the VERSIONS
cell is variable-length, its body is described by a two-byte length, and two
bytes per version means the count saturates at `u16::MAX / 2` entries. That
limit is arithmetic on the format, not a policy choice.

## 2. Cell packet format: fixed and variable

The general cell layout is circuit ID, command, payload; variable-length cells
insert a two-byte body length before the body and then carry exactly that many
bytes rather than padding to the cell size.

**tor-spec.txt §3, the fixed-length cell layout, L496:**
[tor-spec.txt#L496](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L496)

**tor-spec.txt §3, padding of fixed-length cells, L564:**
[tor-spec.txt#L564](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L564)

That padding rule is why `ChannelCodec::write_cell` emits 514 bytes at link
version 4 for a `Padding` cell whose message body is empty: the payload region
is zero-filled up to the full cell length. It is also why `decode_cell` can
length-check before parsing — the codec knows how long the cell is before it
looks inside, which is exactly why a `tor_bytes::Error::Truncated` raised
*during* parsing maps to `ErrorKind::Internal` and not to a protocol violation.
The peer already sent enough bytes; if the parser ran out, the parser is wrong.

Which commands are fixed-length and which are variable is enumerated, and the
list matters here because the driver exercises one of each:

**tor-spec.txt §3, the fixed-length command list, L528:**
[tor-spec.txt#L528](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L528)

**tor-spec.txt §3, the variable-length command list, L541:**
[tor-spec.txt#L541](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L541)

PADDING is command 0 and fixed-length; PADDING_NEGOTIATE is command 12 and also
fixed-length; VERSIONS is command 7 and VPADDING is command 128, both
variable-length. Command 7 is called out separately because it is the one
command whose variable-length status is not derivable from the "command >= 128"
rule:

**tor-spec.txt §3, the rule for command 7, L512:**
[tor-spec.txt#L512](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L512)

Variable-length cells may generally be padded, with VERSIONS the exception:

**tor-spec.txt §3, the padding exception, L508:**
[tor-spec.txt#L508](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L508)

PADDING and VPADDING cells are dropped on receipt — they exist to consume
bandwidth, not to carry information — which is why the driver can treat them as
pure encode/decode round trips with no semantic expectations attached.

## 3. The VERSIONS handshake and its two-byte circuit ID

`Versions::encode_for_handshake` is a separate method from ordinary cell
encoding, and the reason is a genuine chicken-and-egg problem in the protocol:
the width of the circuit ID field depends on the link protocol version, and the
VERSIONS cell is how the link protocol version gets decided. The format
resolves it by fixing the *first* VERSIONS cell at the version-1 layout — a
two-byte circuit ID of zero — regardless of what is being offered.

**tor-spec.txt §4.1 ("Negotiating versions with VERSIONS cells"), the payload
format, L659:**
[tor-spec.txt#L659](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L659)

**tor-spec.txt §4.1, the list of link protocol differences, L678:**
[tor-spec.txt#L678](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L678)

Hence the exact byte string the `cellencode` group asserts for the offer
`[3, 4, 5]`:

```
00 00        two-byte CircID, always zero in the handshake cell
07           ChanCmd::VERSIONS
00 06        u16 body length: three versions, two bytes each
00 03 00 04 00 05
```

Each offered version is big-endian, as every multi-byte integer in this format
is. The negotiated version is the highest both sides name, which is what
`best_shared_link_protocol` computes and why it returns `Option` — disjoint
offers mean no link protocol, not a default one.

## 4. Nested length-prefixed structures

`NestedWriter` and `Reader::read_nested_*` are not conveniences invented for
arti. They exist because the format repeatedly nests a counted body inside a
larger counted body, and the two most-used cases are worth reading before
implementing the pair.

The CERTS cell carries a count of certificates, then for each one a type byte,
a two-byte length and that many bytes of certificate:

**tor-spec.txt §4.2 ("CERTS cells"), the cell layout, L693:**
[tor-spec.txt#L693](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L693)

EXTEND2 nests two levels: a count of link specifiers, each with a type, a
one-byte length and a body; then a handshake type, a two-byte length and a body:

**tor-spec.txt §5.1.2 ("EXTEND and EXTENDED cells"), the EXTEND2 payload, L1087:**
[tor-spec.txt#L1087](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L1087)

**tor-spec.txt §5.1.2, the link specifier types, L1101:**
[tor-spec.txt#L1101](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L1101)

Two behaviours in the reader come straight out of that shape:

- **A counted region is a hard boundary, not a hint.** Code parsing one link
  specifier must not be able to read into the next one by asking for more bytes
  than its own length promised. So `read_nested_u8len` hands the closure a
  reader bounded to exactly the counted region, and a closure that overruns
  sees `Truncated` against *that* boundary, not against the rest of the buffer.
- **Unparsed trailing bytes inside a counted region are an error.** A link
  specifier that declares 8 bytes and whose parser consumes 6 is malformed even
  though every byte was present, which is what `ExtraneousBytes` reports. The
  format's forward-compatibility story is the *type* byte, not slack inside a
  known type's body — unknown types are skipped whole, known types must account
  for everything they claimed.

Extensions inside an Ed25519 certificate are the same pattern again, and
cert-spec says plainly that a truncated extension is an error rather than
something to be tolerated:

**cert-spec.txt §2.1 ("Ed25519 Certificates"), the extension format, L73:**
[cert-spec.txt#L73](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/cert-spec.txt#L73)

**cert-spec.txt §2.1, truncated extensions, L90:**
[cert-spec.txt#L90](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/cert-spec.txt#L90)

## 5. Relay cell layout

The eleven-byte header that `Data::MAXLEN` subtracts is specified field by
field: relay command (1 byte), "recognized" (2), stream ID (2), digest (4),
length (2), then the body, then padding to the end of the payload.

**tor-spec.txt §6.1 ("Relay cells"), the unencrypted relay payload, L1659:**
[tor-spec.txt#L1659](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L1659)

**tor-spec.txt §6.1, the relay command list, L1669:**
[tor-spec.txt#L1669](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L1669)

1 + 2 + 2 + 4 + 2 = 11, and 509 - 11 = 498. That is the whole derivation of
`MAXLEN`, and it is why the constant is written as an expression in terms of
`CELL_DATA_LEN` rather than as a literal.

Three field semantics are graded through the encoder, so they are worth naming:

**"recognized" is zero on the wire at encode time.** It is a cheap first-pass
test for whether a cell is addressed to this hop; the sender writes zero and
the hop that can decrypt it reads zero back.

**tor-spec.txt §6.1, the purpose of the recognized field, L1702:**
[tor-spec.txt#L1702](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L1702)

**The digest is computed with the digest field zeroed.** Encoding writes four
zero bytes there and leaves filling it to the crypto layer, which is why the
`cellencode` group can assert an all-zero digest on a freshly encoded cell.

**tor-spec.txt §6.1, the digest check, L1712:**
[tor-spec.txt#L1712](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L1712)

**The stream ID is big-endian and zero means "not stream-specific".**

**tor-spec.txt §6.1, stream IDs, L1729:**
[tor-spec.txt#L1729](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L1729)

**tor-spec.txt §6.1, the Length field, L1738:**
[tor-spec.txt#L1738](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L1738)

Finally, the padding. This is why `RelayCell::encode` takes an rng at all — the
padding is not zero-fill. The format asks for a short run of zeroes after the
body so that a decryption attempt can be recognised as wrong, and random bytes
after that so the cell's tail carries no predictable plaintext:

**tor-spec.txt §6.1, the Padding field, L1744:**
[tor-spec.txt#L1744](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L1744)

The rng parameter is therefore load-bearing and cannot be dropped for an
implementation that zero-fills; the driver passes a seeded rng precisely so
that the *deterministic* part of the encoding — header fields and body — can be
asserted byte for byte while the padding stays free.

## 6. Ed25519 certificates

Every multi-byte integer in a certificate is big-endian:

**cert-spec.txt §1.2 ("Integer encoding"), L41:**
[cert-spec.txt#L41](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/cert-spec.txt#L41)

The certificate body is a version byte, a certificate type, a four-byte
expiration, a certified-key type, the 32-byte certified key, an extension
count, the extensions, and a 64-byte signature:

**cert-spec.txt §2.1 ("Ed25519 Certificates"), the field layout, L55:**
[cert-spec.txt#L55](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/cert-spec.txt#L55)

**cert-spec.txt §2.1, the VERSION and CERT_TYPE fields, L64:**
[cert-spec.txt#L64](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/cert-spec.txt#L64)

**cert-spec.txt §2.1, the CERTIFIED_KEY field, L66:**
[cert-spec.txt#L66](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/cert-spec.txt#L66)

### Expiry is measured in hours

This is the single most surprising thing in the format and the one the
`certcheck` group probes hardest. The expiration field is not a Unix timestamp
and not a count of seconds; it is a count of **hours** since the Unix epoch.
That is what makes four bytes enough to reach the year 491 936, and it is why
`Ed25519Cert::expiry()` multiplies by 3600 before constructing a `SystemTime`.
An implementation that treats the field as seconds produces a certificate that
expired in 1970 and will fail every timeliness case in the group.

**cert-spec.txt §2.1, the EXPIRATION_DATE field, L67:**
[cert-spec.txt#L67](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/cert-spec.txt#L67)

The fixtures encode 500 000 hours, which is 1 800 000 000 seconds — a little
past the middle of 2027. Both fixture certificates are timely at any plausible
"now" and expired at any instant at or after that; `is_expired_at` treats the
expiry instant itself as expired, so the boundary is closed on the far side.

### The signing key is not always in the certificate

`check_key` exists because an Ed25519 certificate can name its signer two ways,
and only one of them is self-contained. The signed-with-ed25519-key extension,
type 04, carries the signer's public key inside the certificate:

**cert-spec.txt §2.2.1, L100:**
[cert-spec.txt#L100](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/cert-spec.txt#L100)

**cert-spec.txt §2.2.1, the extension's length and contents, L106:**
[cert-spec.txt#L106](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/cert-spec.txt#L106)

The extension list in the appendix confirms the type number — the
"signed-with-ed25519-key" line, L175:
[cert-spec.txt#L175](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/cert-spec.txt#L175)

Without that extension, the verifier must be told the signing key out of band —
from a CERTS cell's other certificates, or from a relay's known identity. That
is the whole shape of `check_key(Option<&Ed25519Identity>)`, and it explains
each of its three outcomes:

| Certificate says | Caller says | Outcome |
| --- | --- | --- |
| extension present | `None` | extension wins; proceed |
| extension present | `Some(k)`, different | `CertError::KeyMismatch` |
| no extension | `None` | `CertError::MissingPubKey` |
| no extension | `Some(k)` | caller's key is used; proceed |

The extension also carries an "affects validation" flag, which is what makes
"unknown extension" a decidable question rather than a guess — an unrecognised
extension with the flag set must be rejected, one without it may be ignored:

**cert-spec.txt §2.1, the ExtFlags field, L83:**
[cert-spec.txt#L83](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/cert-spec.txt#L83)

### The certificate and key type numbers

The fixtures are type 4 certificates: an Ed25519 signing key certified by a
relay's identity key. tor-spec's CERTS-cell table gives the number in the
context this client actually uses it:

**tor-spec.txt §4.2, the certificate type list, L705:**
[tor-spec.txt#L705](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L705)

cert-spec's appendix agrees — the "identity" line, L146:
[cert-spec.txt#L146](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/cert-spec.txt#L146)

and reserves 0 through 3 for the X.509-based types that predate this format —
the "conflict" line, L143:
[cert-spec.txt#L143](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/cert-spec.txt#L143)

which is why `CertType::TLS_LINK_X509` is 1 and not 0, and why `KeyType` is a
separate numbering: certified-key type 1 is a raw Ed25519 key, while 2 and 3 are
digests of an RSA key and of an X.509 certificate respectively. The
certified-key type list — the "ed25519" line, L186:
[cert-spec.txt#L186](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/cert-spec.txt#L186)

That distinction is exactly what `CertifiedKey::as_ed25519()` encodes: it
returns `Some` only for key type 1, because for types 2 and 3 the 32 bytes are
a hash and interpreting them as a public key would be a category error.

### Why the check is a three-stage type pipeline

The signature covers the certificate minus its final 64 bytes, and the spec
states the order of operations before it states the arithmetic: know the
identity key first, then check the signature.

**cert-spec.txt §2.1, checking the signature, L93:**
[cert-spec.txt#L93](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/cert-spec.txt#L93)

That ordering matters for implementors: you cannot verify a signature until you
know which key signed, you cannot know which key signed until you have parsed
the extensions, and you should not act on a certificate's contents until both
are done. Arti turns that ordering into
types — `KeyUnknownCert` → `UncheckedCert` → `SigCheckedCert` → `Ed25519Cert` —
so that "I forgot to check the signature" is a compile error rather than a
review finding. The spec does not mandate this design; it mandates the
ordering, and the type pipeline is arti's way of making the ordering
unskippable. `dangerously_assume_timely` and `dangerously_assume_wellsigned`
are the deliberate, greppable escape hatches.

## 7. Digests, and why the backend is swappable

The default hash is SHA-1, with a 20-byte output, and SHA-256 and SHA3-256 are
used in specific places:

**tor-spec.txt §0.3 ("Ciphers"), the default hash, L177:**
[tor-spec.txt#L177](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L177)

**tor-spec.txt §0.3, HASH_LEN, L182:**
[tor-spec.txt#L182](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L182)

**tor-spec.txt §0.3, SHA-256 and SHA3-256, L184:**
[tor-spec.txt#L184](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L184)

SHA-1 is on the hot path, not a legacy corner: the older key derivation runs it
repeatedly, and the newer one runs HMAC-SHA256 the same way.

**tor-spec.txt §5.2.1 ("KDF-TOR"), the derived key material, L1343:**
[tor-spec.txt#L1343](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L1343)

**tor-spec.txt §5.2.2 ("KDF-RFC5869"), the ntor key split, L1372:**
[tor-spec.txt#L1372](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/tor-spec.txt#L1372)

That is the case for `accel-sha1-asm` and `accel-openssl`: a faster SHA-1 is a
faster client, on a function the protocol calls constantly. And it is also the
constraint on how the acceleration is allowed to work. The output of `Sha1` is
defined by FIPS 180-4, not by arti, so an accelerated backend is only correct if
it is byte-for-byte indistinguishable from the portable one. The
`llcryptodigest` group therefore asserts published test vectors rather than
self-consistency: `SHA-1("abc")` is `a9993e364706816aba3e25717850c26c9cd0d89d`
whichever backend computed it, and a backend that gets that wrong corrupts every
circuit it touches while passing any test written against its own output.

torspec does not discuss SHAKE; `Shake128` and `Shake256` are re-exported for
onion-service code that arrives in later work, and are graded here as correct
XOFs of the published FIPS 202 vectors -- the 32-byte output of
`Shake128("abc")` and of `Shake256("abc")` -- and as *streaming* XOFs (a longer
squeeze extends the shorter one rather than replacing it), which is the
RustCrypto `ExtendableOutput` contract.

## 8. Directory timeliness

`Timeliness` has three levels because a consensus has three distinct time
regimes, not two. A consensus document carries a valid-after time, a fresh-until
time, and a valid-until time, and they are required to be in that order:

**dir-spec.txt §1.4 ("Voting timeline"), L380:**
[dir-spec.txt#L380](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L380)

The three fields, in the consensus format — the "valid-after" line, L1924:
[dir-spec.txt#L1924](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L1924)
the "fresh-until" line, L1938:
[dir-spec.txt#L1938](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L1938)
and the "valid-until" line, L1946:
[dir-spec.txt#L1946](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L1946)

A consensus is "live" for a client when now is between valid-after and
valid-until — note that *fresh*-until is not the deadline; it is when the next
one is expected:

**dir-spec.txt §5.1 ("Downloading network-status documents"), L3696:**
[dir-spec.txt#L3696](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L3696)

The timeline then describes what clients do at each boundary:

**dir-spec.txt §1.4, the point at which all authorities have a consensus, L418:**
[dir-spec.txt#L418](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L418)

**dir-spec.txt §1.4, the client download window, L428:**
[dir-spec.txt#L428](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L428)

**dir-spec.txt §1.4, what happens at valid-until, L432:**
[dir-spec.txt#L432](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L432)

**dir-spec.txt §1.4, the 24-hour cutoff after valid-until, L435:**
[dir-spec.txt#L435](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L435)

Read together, these give the three levels a meaning that is not arbitrary:

| `Timeliness` | Meaning |
| --- | --- |
| `Strict` | the document is live by the spec's own definition — inside [VA, VU) with no slack at all |
| `Timely` | live, plus the tolerances a real client needs to survive a modestly wrong clock |
| `Unchecked` | give me whatever is cached; the caller is not making a security decision with it |

`Unchecked` is not laziness. A status display, a diagnostic dump, or a
"resuming from cache while we fetch" path genuinely wants the stale document,
and forcing those callers through a validity check would push them toward
`dangerously_*` escape hatches for a use that never needed one. What matters is
that the level is *named at the call site*, which is why `netdir` takes it as a
parameter and `timely_netdir` is a thin default rather than the only door.

### Where the tolerance defaults come from

`DirTolerance`'s two knobs are 24 hours before valid-after and 72 hours after
valid-until. The pre-tolerance is directly anchored: the spec's own timeline
already contemplates clients holding a consensus a day past its validity
window, and a client whose clock is a few hours fast must not conclude the
whole network is broken.

The 72-hour post-tolerance is arti's choice and has no spec line behind it. The
spec's own numbers govern how *authorities* schedule votes:

**dir-spec.txt §1.4, the minimum interval requirements, L437:**
[dir-spec.txt#L437](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L437)

but how long a *client* keeps limping along on an expired consensus before
declaring itself unable to bootstrap is a product decision about offline
laptops and suspended phones. Do not go looking for it in dir-spec; implement
the documented defaults (24 h / 72 h) and move on.

### The rename

Step 09 called this configuration section `download_tolerance`. Step 10 calls it
`directory_tolerance`, and the old name must stop working — not be silently
accepted, not be aliased. The rename is arti's, not torspec's, and the reason
it is graded so precisely is that a client that silently ignores a tolerance
setting the operator wrote is worse than one that rejects it: the operator
believes they widened the window and they did not.

`download_schedule` is a genuinely different thing and is untouched: it governs
retry pacing, which dir-spec does address:

**dir-spec.txt §5.5 ("Retrying failed downloads"), L3894:**
[dir-spec.txt#L3894](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L3894)

## 9. Address patterns and reachable addresses

The `AddrPortPattern` grammar is dir-spec's exit-pattern grammar, reused: an
address specification, a colon, and a port specification, where the address may
be a wildcard, an IPv4 address with an optional mask, or a bracketed IPv6
address with an optional prefix length, and the port may be a wildcard, a single
port, or an inclusive range.

**dir-spec.txt §2.1.3 ("Nonterminals in server descriptors"), the exit pattern,
L1482:**
[dir-spec.txt#L1482](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L1482)

**dir-spec.txt §2.1.3, the port specification, L1483:**
[dir-spec.txt#L1483](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L1483)

**dir-spec.txt §2.1.3, the address specification, L1490:**
[dir-spec.txt#L1490](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L1490)

That grammar is why the group's cases look the way they do: `*:*` is the
identity pattern, `*:443` constrains only the port, `10.0.0.0/8:*` constrains
only the address, and `*:80-443` is a range whose endpoints are both included.
And it is why a v4 pattern must not match a v6 address — the two address forms
are distinct productions in the grammar, and a `/8` mask is meaningless against
a 128-bit address. Rendering round-trips: a parsed pattern's `Display` must be
parseable again, which is what pins `new_all()` to the string `*:*` rather than
to an empty string or `*`.

The client side of this is a firewall accommodation, and guard-spec describes
it directly. A guard is excluded from the usable set when a reachable-addresses
policy forbids contacting it:

**guard-spec.txt §4.2 ("The Usable Sample"), the filter conditions, L327:**
[guard-spec.txt#L327](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/guard-spec.txt#L327)

The ordering constraint is the subtle part and it is stated explicitly:
filtering happens *after* sampling, not before.

**guard-spec.txt §4.2, why filters are applied after sampling, L356:**
[guard-spec.txt#L356](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/guard-spec.txt#L356)

This is an anonymity property, not an optimisation. If the restriction were
applied while choosing the sample, the sample itself would encode the user's
firewall configuration, and a relay that observed the client's guard choices
could infer that configuration. Sampling first and filtering second keeps the
sample a function of the network, and the filter a local view over it.

Two consequences land in the step's API:

- `GuardFilter` derives `Eq`/`PartialEq` and must compare *structurally*. A
  filter is persisted alongside the guard sample and compared against the one
  in effect; a filter holding a closure could not be compared, and the guard
  manager would be unable to tell whether the restriction had changed.
- `OwnedChanTarget::restrict_addr` returns the unrestricted target in its `Err`
  arm rather than a bare error. The caller that just discovered "none of this
  relay's addresses are permitted" is precisely the caller that needs to report
  which addresses those were.

`reachable_addrs` defaults to a single wildcard pattern — not an empty list,
which would mean "nothing is reachable" — so a configuration that merely
restates the wildcard is equal to the default.

## 10. Tor version strings

`TorVersion` parses the versioning scheme described in version-spec, which is
where `BadTorVersion` gets to be a distinct error kind rather than a generic
parse failure:

**version-spec.txt §2 ("The New Way"), the version grammar, L33:**
[version-spec.txt#L33](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/version-spec.txt#L33)

Four numeric components with an optional status tag, which is why `0.4.7.8`
parses and an arbitrary string does not.

Directory documents in general are keyword-line based, and that shape is what
`MissingToken` ("didn't find required entry") is reporting when a required
keyword is absent:

**dir-spec.txt §1.2 ("Document meta-format"), L265:**
[dir-spec.txt#L265](https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/dir-spec.txt#L265)

The `ParseErrorKind` taxonomy exists so a caller can distinguish "this document
is malformed" from "this document is well-formed but I do not trust it"
(`BadSignature`) from "this entry is the wrong shape" (`BadPolicy`,
`BadTorVersion`). The kinds themselves, and the `within` / `at_pos` context
builders, are arti's. What the specs supply is the *set of things that can go
wrong* at each parse site — and the requirement that adding context never
changes the classification is arti's own invariant, because the kind is what
callers branch on.

## 11. Where there is no spec, and what governs instead

The following parts of step 10 have no torspec anchor. That is a finding, not a
gap in the research, and pretending otherwise would send an implementor hunting
for text that does not exist.

**`tor_error::ErrorKind` and the whole `HasKind` mapping.** torspec describes a
wire protocol between peers. It says nothing about how a client library should
classify its own failures for its own callers. `ErrorKind` is arti's public
error taxonomy, and every mapping in this step — `CircuitClosed` to
`CircuitCollapse`, `HandshakeCertsExpired` to `ClockSkew`, `BytesErr` to
`TorProtocolViolation` — is a judgement about what a caller can usefully *do*,
not a protocol requirement. The one place protocol reasoning leaks in is the
`Truncated`-inside-a-length-checked-cell case in section 2, and even there the
spec supplies the premise, not the conclusion.

**Error *context* — the `object` and `parsed` strings.** That `tor_proto::Error`
carries `object: &'static str` so it can say which structure failed to parse,
and that `tor_cell::Error::BytesErr` carries `parsed`, is a diagnostics design.
The strings are asserted literally by the tests because they are the interface,
but they are arti's interface.

**`tor_persist` error shape.** The `"{source} while {action} on {resource}"`
rendering, the `ErrorSource` variants, the lock protocol, the
`<dir>/state/<key>.json` layout — all local storage design. Nothing in torspec
describes how a client persists its own state.

**`fs-mistrust` permission rules.** That `0o700` and `0o750` are acceptable and
`0o777` and `0o702` are not is a Unix security judgement about group- and
other-writability. torspec has nothing to say about filesystem modes.

**`safelog`.** Scrubbing sensitive values out of log output, and the
`enforce`/`disable`/`suppress` guard protocol, are entirely arti's. The
motivation is a real anonymity concern, but no spec text prescribes it.

**Cargo features.** `with-openssl`, `with-sha1-asm`, `accel-openssl`,
`accel-sha1-asm`, `full`, `encode`, `experimental` are build configuration.
Section 7 explains why accelerating SHA-1 is worth a feature at all; the
*naming and layering* — that `arti` forwards to `arti-client`, which forwards to
`tor-llcrypto` — is packaging, and it is graded because a feature nobody can
name is a feature nobody can turn on.

**`resolve_option`, `FilterCount`, `Timeliness` as a Rust enum, and the
`TimerangeBound` boundary semantics.** These are library ergonomics. Section 8
anchors *why* there are three timeliness regimes; that they are a `Copy` enum
with those three names is arti's. Likewise, that a half-open Rust range and an
inclusive one are treated identically by `TimerangeBound` — both endpoints
excluded — is an arti decision, and a slightly surprising one; it is graded
precisely because it is the kind of detail a reimplementation gets wrong by
following Rust's usual range conventions instead.

**Bootstrap blockage reporting.** `BlockageKind`, `DirBlockage`,
`ConnBlockage` and the conversions between them are a user-interface
vocabulary: they exist so a client can tell a human *why* it is not working.
The mapping choices are arti's — in particular that every directory blockage
collapses to `CantBootstrap`, because from the user's chair "the directory is
stuck" is one situation with one remedy, whatever the internal cause.
