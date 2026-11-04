# Spec notes — step 01, "cells, circuits and directories"

Prose notes on where each graded behaviour comes from. Every citation is to the
torspec pin `1db24dddbf2203f2a465747302f133ef3ed6eed4` (2022-11-28), in the form

```
https://gitlab.torproject.org/tpo/core/torspec/-/blob/1db24dddbf2203f2a465747302f133ef3ed6eed4/<path>#L<line>
```

No specification text is reproduced here — only my own description of the
behaviour, with the line it comes from. The `#L` numbers are the line of the
section heading, so a reader lands at the top of the relevant passage.

## 1. Cells on the wire

### 1.1 Fixed and variable cell layouts

tor-spec §3 (3. Cell Packet format, `tor-spec.txt#L488`) defines the two
framings. A fixed-length cell is a circuit ID, one command byte, and a payload
that is always `PAYLOAD_LEN` bytes whatever the command's body actually needs —
short bodies are padded. A variable-length cell inserts a big-endian 16-bit
length between the command and the body, and the body is exactly that long.
§3 also fixes `CIRCID_LEN`: two bytes for link versions 1–3, four bytes from
version 4 up, with the special provision that the first VERSIONS cell carries
two-byte circuit IDs regardless (`tor-spec.txt#L517`).

**Where each half of that rule is graded.** The two halves live in different
functions, and confusing them is the one way to lose `cellcodec` while being
right about the protocol:

- The **two-byte VERSIONS provision** is graded in `chanmsg`, on
  `Versions::encode_for_handshake`, against `versions_handshake.bin`. That is
  the correct home for it: VERSIONS is what negotiates the link version, so it
  cannot be framed at a width the link version decides. The client's first
  write in `linkhs` is the same encoding.
- The **negotiated-width rule** is graded in `cellcodec`, on
  `ChannelCodec`, and every case there constructs the codec with
  `ChannelCodec::new(4)`. **No group frames a VERSIONS cell through
  `ChannelCodec`**, so do not special-case VERSIONS inside `write_cell`; it is
  never asked to carry one. Nor does any group observe a codec at link
  versions 1–3, so the two-byte branch of `CIRCID_LEN` is ungraded here and
  `write_cell` may simply always use four bytes.

**This split is stable for the whole ladder, not just this step.** `/app` is
cumulative and this document stays binding at every later step, so the claim
above is stated to be true at every milestone rather than only at this one. It
rests on a design decision the channel codec carries in its own header
comment — *VERSIONS cells are not supported via the encoder/decoder, since
VERSIONS always uses a two-byte circuit-ID* — and on the codec never
consulting the link version it was constructed with. Both hold unchanged from
this step's milestone through the last one: the codec writes a four-byte
circuit ID unconditionally throughout, and the link-version field it stores is
never read. So nothing here is scoped to this step's tag, and a later step does
not inherit a rule that quietly stopped being true.

What *would* change it, stated so a later step can override this without
ambiguity: if some later step's harness ever frames a VERSIONS cell through
`ChannelCodec`, or constructs a codec at a link version below 4, that step's
own `TEST_INTERFACE.md` says so and **its** statement governs for that step.
Absent such a statement, the rule above continues to apply.

The consequences the `cellcodec` group asserts on are direct: a fixed cell is
4 + 1 + 509 = 514 bytes at link version 4; a variable cell is
4 + 1 + 2 + len; the payload of a fixed cell is always 509 bytes even when the
message inside is shorter.

The padding rule then runs the other way as well: §3 says the payload of a
variable-length cell may itself be padded with padding bytes, *except* for
VERSIONS, which must not carry anything past the version list
(`tor-spec.txt#L508`). No group feeds a VERSIONS body with a trailing byte to
a decoder, so how you treat one is yours to choose; the rule is recorded here
because it is why the version list is self-delimiting from the cell length
alone. The only VERSIONS body any group decodes is the well-formed one the
responder sends in `linkhs`.

The claim in §3 that a command byte of 7 (VERSIONS) or ≥ 128 marks a
variable-length cell (`tor-spec.txt#L512`) is what `ChanCmd::is_var_cell`
encodes. Note that this rule is stated for a *version 3 or higher* connection;
on a version 2 connection VERSIONS is the only variable-length command. The
group asserts the version-4 reading.

### 1.2 Command numbers

The fixed-length command table begins at `tor-spec.txt#L525`, and the
variable-length table at `tor-spec.txt#L534` — PADDING, CREATE, CREATED, RELAY,
DESTROY, CREATE_FAST, CREATED_FAST, NETINFO, RELAY_EARLY, CREATE2, CREATED2,
PADDING_NEGOTIATE in the first; VERSIONS, VPADDING, CERTS, AUTH_CHALLENGE,
AUTHENTICATE, AUTHORIZE in the second. `chancmd` asserts each numeric value.
The descriptions in the comments column are also the source of the
`accepts_circid_val` rule: only commands that are about a circuit bear a
nonzero circuit ID, and the channel-wide commands — PADDING, VPADDING,
PADDING_NEGOTIATE, VERSIONS, NETINFO, CERTS, AUTH_CHALLENGE, AUTHENTICATE —
do not. A command this version does not recognize is a third case: it
constrains nothing and accepts either circuit ID, which is the
forward-compatibility reading of the same tables and is what `chancmd`
asserts for `ChanCmd::from(201)`.

### 1.3 The relay cell header

tor-spec §6.1 (6.1. Relay cells, `tor-spec.txt#L1647`) gives the 11-byte relay
header: command, two bytes of "recognized", a two-byte stream ID, a four-byte
digest, and a two-byte length. §6.1 also fixes `PAYLOAD_LEN` as the fixed-cell
payload minus those 11 bytes, which is where `Data::MAXLEN == 498` comes from.

Two properties matter for grading:

- The recognized and digest fields are zeroed on the sending side and are
  meaningful only after the onion layer is peeled, so an encoder must write
  zeros (`tor-spec.txt#L1647`, and the routing discussion at
  `tor-spec.txt#L1556`).
- The length field counts the *body*, not the header, and the receiving side
  must reject a length that runs past the end of the cell rather than reading
  whatever follows.
- The padding after the body is **not** random from its first byte. §6.1 says
  an implementation should fill the padding field with four zero-valued bytes
  and only then with as many random bytes as will fit
  (`tor-spec.txt#L1746`); if fewer than four bytes are left, all of them are
  zero. The rationale is proposal 289: the zeros let a relay recognise a cell
  it has decrypted correctly, and the random tail stops the padding being
  predictable. An encoder that starts its random filler at the first padding
  byte is therefore wrong, even though the same section forbids a *reader*
  from relying on padding contents.

`relayhdr` asserts the exact byte layout — header, body and the four zero
padding bytes that follow it — against `cell_relay_data.bin`, which was laid
out by hand from this section, and asserts the rejection case.

### 1.4 Per-command relay bodies

§6.1's command table (`tor-spec.txt#L1672` onward) is the index for
`relaymsg`: RELAY_DATA is the payload verbatim; RELAY_BEGIN's body is a
NUL-terminated "host:port" plus a four-byte flags word (`tor-spec.txt#L1756`);
RELAY_END's body is a reason byte plus, for the exit-policy reason, an address
and a TTL; RELAY_CONNECTED carries either nothing, an IPv4 address and TTL, or
a sentinel with an address type and a 16-byte IPv6 address; RELAY_RESOLVED
carries a sequence of typed answers each followed by a 32-bit TTL, or a single
error byte (`tor-spec.txt#L1960`).

The flag word is the interesting part for `relaymsg`: §6.1 defines three bits
with inverted polarity for the IPv4 bit, and the encoding rule that an all-zero
flag word is omitted from the body — which is why an IPv4-only BEGIN and an
"any address family" BEGIN differ in length, and why the group asserts both a
509-byte fixture comparison and a byte count for the zero-flags case.

The hostname rules — lowercase before sending, bracket an IPv6 literal, refuse
a non-ASCII target — come from the same section and from the name-lookup
discussion at §6.4 (`tor-spec.txt#L1960`).

### 1.5 Circuit extension

tor-spec §5.1.2 (EXTEND and EXTENDED cells, `tor-spec.txt#L1082`) defines the
EXTEND2 body: a count of link specifiers, then each specifier as a type byte, a
length byte and that many value bytes, then the handshake type and a length-
prefixed handshake. The link-specifier types and their meanings are in the same
section: an IPv4 or IPv6 OR address with a port, an RSA identity, an Ed25519
identity. Where an unrecognized type is handled — kept as opaque bytes — is the
forward-compatibility allowance that the group asserts through
`LinkSpec::Unrecognized`.

§5.1.1 (`tor-spec.txt#L1050`) covers the CREATE2/CREATED2 pair, whose body is a
two-byte handshake type and a length-prefixed handshake; the ntor type is 2,
per §5.1.4 (`tor-spec.txt#L1221`). `extend2` asserts the CREATE2 layout against
`cell_create2.bin` and the EXTEND2 layout against `relay_extend2.bin`.

The ordering rule — link specifiers are sorted before being written — is
asserted as IPv4, then RSA, then Ed25519, then IPv6. This is an implementation
agreement rather than a wire requirement, so the group checks both that the
constructor sorts and that it sorts into exactly that order.

The sort key is **not** the link-specifier type byte. Those are IPv4 = 0,
IPv6 = 1, RSA = 2, Ed25519 = 3, so sorting on them would give IPv4, IPv6, RSA,
Ed25519 — a different order from the one asserted, and the wrong one. Sort on a
separate rank: 0 for an IPv4 ORPort, 1 for an RSA identity, 2 for an Ed25519
identity, 3 for an IPv6 ORPort, and for an unrecognized specifier its own type
byte. The group reads the order back as the sequence of encoded type bytes and
expects `[0, 2, 3, 1]`.

### 1.6 SENDME

tor-spec §7.3.1 (SENDME Cell Format, `tor-spec.txt#L2103`) defines the versioned
body: the first byte is a version; version 0 has an empty body; version 1 is a
two-byte length followed by a 20-byte tag. Anything else must not be
interpreted as a tag. §7.3 (`tor-spec.txt#L2053`) and §7.4
(`tor-spec.txt#L2143`) establish that SENDME appears both at circuit level,
with a zero stream ID, and at stream level. `sendmefc` asserts the tagged body
against `relay_sendme_tag.bin`, the empty body, both no-tag version cases, and
the truncated-tag rejection.

## 2. The link handshake

tor-spec §4 (`tor-spec.txt#L603`) describes the v3 handshake: the initiator
sends VERSIONS; the responder replies with VERSIONS, then CERTS,
AUTH_CHALLENGE and NETINFO. §4.1 (`tor-spec.txt#L638`) is VERSIONS and the
negotiation rule; §4.2 (`tor-spec.txt#L688`) is CERTS, one or more certificates
keyed by type; §4.3 (`tor-spec.txt#L808`) is AUTH_CHALLENGE, a 32-byte value
plus the authentication methods the responder will accept; §4.5
(`tor-spec.txt#L939`) is NETINFO, which carries a timestamp, the observed
address of the other party, and a list of the sender's own addresses.

Three details shape the `linkhs` group:

- The first cell may be VERSIONS, VPADDING or PADDING, and padding plus
  unrecognized commands may appear in the middle of the handshake
  (`tor-spec.txt#L627`). A client that treats an unexpected command as fatal
  fails the test that interleaves VPADDING.
- The handshake is only complete once CERTS, AUTH_CHALLENGE and NETINFO have
  all arrived, and the client must reject a peer that sends no VERSIONS cell at
  all — the negotiation in §4.1 has no default.
- Exactly one CERTS cell is expected, and a NETINFO cell is required. The
  sentence setting out that order is in §2 ("Connections"), which sketches the
  whole handshake, rather than in §4.1 (`tor-spec.txt#L334`).
- A *second* AUTH_CHALLENGE is as much an error as a second CERTS. §4 gives
  the responder's cells as a fixed sequence with one of each, so a client that
  tolerates a repeat is accepting a handshake the spec does not describe.
  `linkhs` feeds exactly that script and requires the handshake to fail.

The NETINFO body is graded byte for byte, so §4.5's record layout
(`tor-spec.txt#L944`) is worth restating: a 4-byte big-endian timestamp; then
the *other* party's address as a one-byte type, a one-byte length and that
many value bytes; then a one-byte count of the sender's own addresses; then
that many records in the same three-field form. Type `0x04` is IPv4 with
length 4 and type `0x06` is IPv6 with length 16, and a length that disagrees
with the type means the address is ignored. §4.5 also says a client SHOULD
send 0 as its timestamp, to avoid being fingerprinted by its clock. So a
client's NETINFO is `00 00 00 00`, one address record for the peer, and a zero
count — 11 bytes when the peer's address is IPv4 — while a relay announcing
one IPv4 and one IPv6 address of its own writes 35. Those two lengths are what
`chanmsg` asserts.

The group drives this over an in-memory stream rather than a socket, so nothing
about TLS is graded; the observable is the VERSIONS cell the client writes and
the `Result` of the handshake against a byte script.

## 3. Certificates

cert-spec.txt §2.1 (Ed25519 Certificates, `cert-spec.txt#L48`) defines the
document: a version byte, a certificate type, an expiration expressed as hours
since the Unix epoch, a key type, a certified key, a list of extensions, and a
signature. §1.2 (`cert-spec.txt#L41`) states the integer encoding — the
expiration is a four-byte big-endian count of hours, which is why the group
multiplies by 3600 to get a duration and why the boundary case flips exactly at
that instant.

§2.2 (`cert-spec.txt#L98`) and §2.2.1 (`cert-spec.txt#L100`) define the
extensions: the signed-with-ed25519-key extension carries the signing key, and
an extension whose type is unknown must be ignored unless its critical bit is
set, in which case the certificate is unusable. `edcert` asserts both branches
against `edcert_unknownext.bin` and `edcert_criticalext.bin`.

Each extension record is a two-byte big-endian length, a one-byte type, a
one-byte flags field and then that many bytes of body — the length counts the
body alone. The criticality flag is **bit 0 of the flags byte** (`flags & 1`),
not the high bit: `edcert_criticalext.bin` sets flag `0x01`, and a reader that
tests `0x80` accepts a document it must refuse.

The certificate-type and key-type code tables are in the same section, and are
what `CertType`/`KeyType` mirror. §2.3 (`cert-spec.txt#L113`) is the RSA-to-
Ed25519 cross-certificate, which is out of scope for this step.

Authority certificates are dir-spec §3.1 (Creating key certificates,
`dir-spec.txt#L1510`). Their distinguishing feature is that one document
carries two RSA identities — an identity key and a signing key — which is why
`AuthCertKeyIds` has two fields and why `authcert` asserts both fingerprints
and that a certificate whose fingerprint does not match its key is rejected.
The validity interval is the `published`/`expires` pair on the document, and it
is checked through the same `Timebound` machinery as the Ed25519 certificates,
so `authcert` asserts `NotYetValid` before and `Expired` after.

## 4. Directory documents

### 4.1 Consensus

dir-spec §3.4.1 (Vote and consensus status document formats,
`dir-spec.txt#L1865`) is the normative description of the document the
`nsparse` group parses. The pieces the group asserts on:

- The header carries `valid-after`, `fresh-until` and `valid-until`, which is
  the `Lifetime` triple.
- Each relay appears on an `r` line with a nickname, identity, publication
  time, address, OR port and directory port; on an `s` line with its flags; on
  a `v` line with its version; on a `pr` line with its subprotocol versions; on
  a `w` line with its bandwidth; and on an `m` line with the digest of its
  microdescriptor.
- The consensus body is sorted by RSA identity, which is why
  `consensus-unsorted.txt` is an error rather than a reordering.
- Server-side parameters live on a `params` line in the header and bandwidth
  weights on `bandwidth-weights` in the footer; the two are keyed lookups and
  are distinct, which the group asserts by reading a key from each.
- A consensus must be signed, which is why `consensus-nosig.txt` fails.

§3.9.2 (Microdescriptor consensus, `dir-spec.txt#L3357`) explains why the
microdescriptor flavour replaces each relay's descriptor with a digest — that
digest is what makes a consensus alone insufficient to build a directory, and
it is the mechanism the `netdirsel` group exercises.

### 4.2 Microdescriptors

dir-spec §3.3 (Computing microdescriptors, `dir-spec.txt#L1647`) is the
specification of the document, and §5.2 (Downloading server descriptors or
microdescriptors, `dir-spec.txt#L3760`) of how a client fetches them. The
fields the `mdparse` group asserts are the `onion-key` TAP key — the RSA
public key, which must be exactly 1024 bits: a document whose key is another
size is refused, which is what `md-bad-keylen.txt` is for — the `id ed25519`
identity, the `p` and `p6` port policies, and the `family` line. (The ntor key
is a *different* line, `ntor-onion-key`, and is not asserted here.)
The digest is the SHA-256 of the document as transmitted (without any
annotation line), which is why the group recomputes it from the fixture text.

The IPv6 default deserves a note: a microdescriptor with no `p6` line means
"reject everything on IPv6", not "no policy" — the same reading applies to a
microdescriptor's absent IPv4 policy only in the sense that a client must not
assume reachability. The group asserts `reject 1-65535`.

### 4.3 Router descriptors

dir-spec §2.1.1 (Server descriptor format, `dir-spec.txt#L482`) is the
descriptor grammar. The `router` line takes exactly five arguments
(`dir-spec.txt#L482`); the `identity-ed25519` certificate block belongs *after*
the `router` line, so a descriptor that carries an identity certificate before
its `router` line is rejected, as is one with no Ed25519 identity document at
all (rejected at the version 3 protocol level), and the two refusals are told
apart from each other and from a `router` line with too few arguments. That is
the whole of the
`rdparse` group: it grades structure and rejection, because the tag exposes no
field accessors on `RouterDesc`.

### 4.4 Authority certificates and diffs

dir-spec §4.5 (Consensus diffs, `dir-spec.txt#L3621`) and §4.5.1 (Consensus
diff format, `dir-spec.txt#L3632`) define the diff document: a header naming
the format version, a `hash` line with the SHA3-256 of the input and output
documents, and then a body of `d`/`c`/`a` commands over line numbers. §4.5.2
(`dir-spec.txt#L3652`) is the server-side rule for when a client may ask for a
diff rather than a full document, which is what the `x-or-diff-from-consensus`
header in the `dirreq` group belongs to.

`consdiff` asserts the command semantics: `d` deletes an inclusive range, `c`
replaces a range, `a` inserts *after* a given line with `0a` prepending, and a
`c` or `a` command's replacement body ends at a line holding a single `.` —
the format's terminator, which is not itself an inserted line, so a body
without one is an error. The commands address the post-image **from the end
backwards** — the highest line number comes first — must stay in range, and the
result's digest must match the `hash` line. `looks_like_diff` is the cheap
first-line test a client uses before handing a body to the applier.

### 4.5 HTTP requests

dir-spec §5.1 (Downloading network-status documents, `dir-spec.txt#L3685`),
§5.2 (`dir-spec.txt#L3760`) and §6.1 (HTTP headers,
`dir-spec.txt#L3952`) are the source for the `dirreq` group. The paths are
fixed by the directory protocol — a consensus at `/tor/status-vote/...`, a
microdescriptor bundle at `/tor/micro/d/`, a key bundle at `/tor/keys/fp-sk/`,
a descriptor request at `/tor/server/d/` or `/tor/server/all`.

Two encoding details are easy to get wrong and both are graded.

The first is the `.z` suffix. Every directory URL in dir-spec is written with
an optional trailing `.z`, and §6.1 explains what it means: a server that gets
no `Accept-Encoding` decides between "deflate" and "identity" purely by
whether the requested URL ends in `.z` (`dir-spec.txt#L3964`). §3.11 spells
the consensus URLs out in the same form, both the plain one and the one that
lists authority fingerprints (`dir-spec.txt#L3529`). A client that wants the
compressed document asks for it in the path, so every URI the `dirreq` group
builds ends in `.z`, after any digest list.

The second is the base64 alphabet. The microdescriptor digests in
`/tor/micro/d/` are **standard** base64 with the trailing `=` padding dropped
— not the URL-safe variant. §4.3 (Downloading microdescriptors from directory
authorities, `dir-spec.txt#L3587`) says so in the clearest possible way: the
separator between digests is `-` *because* `+` is one of the characters base64
can emit. If the alphabet were the URL-safe one, `+` could not occur and the
sentence would make no sense. Fingerprints and descriptor digests elsewhere
are plain lowercase hex, per dir-spec §1.2 (Document meta-format,
`dir-spec.txt#L260`).

§6.1 additionally prescribes `if-modified-since` in the HTTP-date form, the
`x-or-diff-from-consensus` header, and `Accept-Encoding` naming the
compression algorithms the client can decode — of which "deflate" and
"identity" are the two every client is assumed to have.

The group asserts on the method and URI of each built request, on those
headers, and on the `partial_docs_ok` and `max_response_len` answers that tell
the fetching code how much to read and whether a short answer is still worth
keeping. It does not grade what happens to a response: decompression,
retries and cache handling belong to later steps.

## 5. Selecting relays

### 5.1 Consensus parameters

dir-spec §3.4.1's `params` line and the parameter definitions scattered through
dir-spec and path-spec are the source of `netparams`. path-spec §2.2 (Path
selection and constraints, `path-spec.txt#L303`) and §2.4.6 (Consensus
parameters governing behavior, `path-spec.txt#L607`) name the parameters that
affect circuit building; dir-spec §3.8.3 (Computing Bandwidth Weights,
`dir-spec.txt#L3136`), the `bwweightscale` parameter.

The behaviour worth noting is that a parameter has a *clamp range*, not a
validation: a consensus that sets `circwindow` above the maximum gets the
maximum — and one that sets a parameter below its minimum gets the minimum —
and the client does not reject the consensus. The clamp runs in both directions
whether or not a group reaches each one: this step's `netparams` group asserts
the too-large direction (`circwindow` and the path-threshold percentage against
its ceiling) and the in-range direction, and no case here exercises the
too-small direction. Separately the group asserts that
a parameter the client has never heard of is silently ignored — it stays
visible on the parsed consensus, but does not appear in `NetDir::params()`.

### 5.2 Path selection

path-spec §2.2 (`path-spec.txt#L303`) and §2.2.1 (Choosing an exit,
`path-spec.txt#L383`) define the constraints on a path: a guard, a middle, an
exit; three hops; the exit must be one the directory says will accept the
target port; and no two hops may be in the same family or be the same relay
(`path-spec.txt#L303`). §1.2 (A relay's bandwidth, `path-spec.txt#L135`) and
§5.1 (How consensus bandwidth weights factor into entry guard selection,
`path-spec.txt#L763`) are the bandwidth-weight background: the `w` line
supplies the relay's own bandwidth and the consensus footer supplies the
weights, and the product of the two drives selection per role.

The graded consequences are: a relay whose weight for a role is zero is
ineligible for that role even if it holds the flag — a relay with an exit
policy covering the wanted port but `Wed=0` is not a usable exit — a
directory-cache path is one hop, and an exit path is three. The `pathsel` group
grades relay choice through `NetDir::pick_relay` with a seeded RNG, because
`TorPath` exposes nothing but its length at this tag.

## 6. The SOCKS proxy

The base protocol is SOCKS5 (RFC 1928) and SOCKS4/4a; the extensions a Tor
client must support are in socks-extensions.txt §2 (Name lookup,
`socks-extensions.txt#L63`) and §3 (Other command extensions,
`socks-extensions.txt#L81`). Name lookup is the RESOLVE and RESOLVE_PTR
commands, which is why the `socks` group asserts that those two are accepted
while BIND and UDP_ASSOCIATE are refused — a Tor client implements the command
set up to and including these extensions and nothing beyond it.

The negotiation order in the group is the RFC 1928 one: the client offers a
method list, the server picks one, and the method it picks is the reason the
group asserts that username/password (method 2, RFC 1929) wins over no-auth
(method 0) when both are offered. The SOCKS4a variant is distinguished by the
`0.0.0.x` sentinel address followed by the hostname; the userid precedes it in
both SOCKS4 and SOCKS4a. The reply formats are the RFC ones — a SOCKS5 reply
is the version, the status, a reserved byte, an address type and the bound
address and port; a SOCKS4 reply is a zero byte, `0x5a` for granted and `0x5b`
for rejected, then the bound address and port.

The truncation behaviour is not an RFC rule but a consequence of running the
handshake over a byte stream: a message that stops mid-field must be reported
as incomplete and retried once more bytes arrive, not treated as malformed.

## 7. Subprotocol versions

version-spec.txt §2 (The New Way, `version-spec.txt#L29`) and §3 (Version
status, `version-spec.txt#L64`) define the version-line syntax that
`tor-protover` parses — protocol names with comma-separated versions and
ranges — and the canonical form a consensus must use. tor-spec §9
(Subprotocol versioning, `tor-spec.txt#L2186`) and its subsections name the
protocols themselves: Link (`tor-spec.txt#L2239`), LinkAuth
(`tor-spec.txt#L2253`), Relay (`tor-spec.txt#L2267`), HSDir
(`tor-spec.txt#L2359`), DirCache (`tor-spec.txt#L2370`), Desc
(`tor-spec.txt#L2380`), Microdesc (`tor-spec.txt#L2393`), Cons
(`tor-spec.txt#L2405`), Padding (`tor-spec.txt#L2418`), FlowCtrl
(`tor-spec.txt#L2430`).

The canonical form is what the `protover` group asserts on hardest: entries
sorted by protocol name, each protocol's versions ascending, **any run of two
or more consecutive versions** collapsed to a `lo-hi` range — a run of exactly
two included, so `Relay=1,2` renders as `Relay=1-2` — and the whole rendering
idempotent. Two subtleties the group pins down: an unrecognized protocol name
is preserved verbatim and is case-sensitive, so the version line a relay
publishes survives a client that predates it; and a protocol whose version list
is empty contributes nothing to the rendering.

## 8. Directory-manager configuration

dir-spec §5 (Client operation, `dir-spec.txt#L3680`), §5.1
(`dir-spec.txt#L3685`), §5.4 (Using directory information,
`dir-spec.txt#L3842`) and §5.5 (Retrying failed downloads,
`dir-spec.txt#L3890`) describe a client's relationship with the directory: it
has a cache, a set of authorities it trusts, a set of fallback mirrors, and a
schedule. §5.4.2 (Managing naming, `dir-spec.txt#L3877`) is the authority-vs-
mirror distinction.

`dirmgrcfg` grades exactly the configuration surface: the cache path is
mandatory; naming an authority replaces the built-in trust set; a
configuration with neither an authority nor a fallback cannot be built; the
identity on an authority is its v3 identity, which is what it is matched
against when a certificate arrives; and a per-parameter override reaches the
consensus as parsed. The proxy settings and address filters that the same
objects carry are not graded — they are not observable through the public
surface at this tag.

The single-shot directory manager with a disk cache is, at this tag, not
gradeable from outside the crate: `CacheUsage`, `RetryConfig` and `RetryDelay`
live in private modules, and `DirMgr` has no public constructor that runs with
no network and an empty cache directory. That is a limitation of the milestone's
public boundary, not of the capability.

## 9. Out of scope

- TLS and the cryptographic primitives. Only their composition is graded:
  certificate chains, handshake sequencing, digest construction, and the
  parsing of the values they produce. No group calls a cipher or a hash
  primitive to compare it against a vector.
- Onion services, at every step.
- The relay and directory-authority side. A client parses and validates what
  they publish; it does not publish.
- Pluggable transports, and wall-clock behaviour of any kind.
- **Four capabilities named in `instruction.md` §1 that carry no weight at this
  step**: the ntor handshake, layered onion encryption, the circuit manager as
  a manager, and the directory manager as a manager. The cell bodies that
  *carry* an ntor handshake (`CREATE2`, `EXTEND2`) are graded in `extend2`,
  which treats the handshake payload as opaque bytes; relay-cell framing and
  padding are graded in `relayhdr` with the recognized and digest fields
  written as zero; `tor-circmgr` is graded only through its path builders in
  `pathsel`; and `tor-dirmgr` only through its configuration builders in
  `dirmgrcfg`. `TEST_INTERFACE.md` §3 carries the same table with the
  corresponding group names.
