# Step 5 — RFC 9000 version 1 beside the drafts, and the pacing descriptors

## 1. Implement this milestone

Add the published QUIC version 1 — `0x00000001`, RFC 9000 and RFC 9001 — to
a stack that so far has only ever spoken drafts, and make it the default
without removing the drafts. One server process must now answer a version-1
Initial and a draft-29 Initial with the right key schedule for each, which
means the Initial salt, the Retry integrity key and the transport-parameters
TLS codepoint all become functions of the negotiated version rather than
constants.

At the same time both halves of the I/O hot path change shape. `recv` grows
a second argument carrying the address a datagram came from, and `send`
starts returning a descriptor beside the length: where the packet should go,
and the time it should go at. The second field is the whole point — the send
path now has a pacer, and the caller is expected to honour the deadline
rather than write the datagram immediately.

Client-side session resumption comes back with it: a connection can export
its TLS session state and a later connection can import it, which is the
ground `is_resumed()` and 0-RTT stand on. HTTP/3 picks up the plain `h3`
ALPN token now that there is a final version to attach it to, and `Error`
gains a second variant carrying data.

`Connection::recv` and `Connection::send` both change signature in the same
release, and nothing about the old shapes keeps compiling:

```rust
// before (0.7.0)                       // after (0.9.0)
fn recv(&mut self, buf: &mut [u8])      fn recv(&mut self, buf: &mut [u8],
    -> Result<usize>                        info: RecvInfo) -> Result<usize>
fn send(&mut self, out: &mut [u8])      fn send(&mut self, out: &mut [u8])
    -> Result<usize>                        -> Result<(usize, SendInfo)>
```

`quiche::connect` and `quiche::accept` grow a `SocketAddr` in the same place
— `connect(server_name, scid, to, config)` and `accept(scid, odcid, from,
config)` — because a connection now knows its peer's address instead of
being told it implicitly by whoever owned the socket. A stack that keeps the
old signatures beside the new ones has not done this step; there is one
shape, and it is the new one.

The version flip is the subtler half. `PROTOCOL_VERSION` becomes
`0x00000001`, but drafts 27, 28 and 29 stay negotiable, so a server that
"upgraded to v1" by replacing the draft constants fails the half of this
step that requires the drafts still to work, and a server that added v1 to
the version list without making the Initial salt version-dependent fails the
other half. Both salts have to be live in the same process at the same time:
`38762cf7f55934b34d179ae6a4c80cadccbb7f0a` for version 1,
`afbfec289993d24c9e9786f19c6111e04390a899` for draft-29, and
`c3eef712c72ebb5a11a7d2432bb46365bef9f502` for drafts 27 and 28.

`Error::InvalidStreamState` stops being a bare variant and starts carrying
the stream id that provoked it, exactly as `StreamStopped` did one step
earlier. Code that matched `Error::InvalidStreamState` by name no longer
compiles.

Drafts 27 and 28 stay negotiable at this milestone alongside 29 and v1, and
nothing about their key schedules changes here, so this step's own groups
re-verify only v1 and draft-29 side by side (`cap-two-salts`) rather than
all four versions at once. Drafts 27/28 are not independently re-checked
again after this step until step 09, which removes them and asserts they are
rejected outright; a candidate that silently breaks their salts between here
and then goes unnoticed, and that gap is accepted
rather than closed with a new group.

What is newly in scope here:

* `cap-rfc9000-v1` — `quiche::PROTOCOL_VERSION` is `0x00000001`, and the
  running server completes a whole version-1 address-validation exchange
  with a QUIC endpoint the checker implements itself. The checker sends a
  token-less version-1 Initial; the answer must be a Retry with `0x00000001`
  in its header, no inline ODCID field, a non-empty token, and a 16-byte
  integrity tag that recomputes correctly under RFC 9001 Section 5.8's
  *version-1* key `be0c690b9f66575a1d766b54e368c84e` and nonce
  `461599d35d632bf2239825bb` over the destination connection id the checker
  chose. The checker then sends a second version-1 Initial carrying that
  token and the Retry's source connection id, and opens the server's answer
  with Initial keys it derives itself from that connection id under version
  1's salt `38762cf7f55934b34d179ae6a4c80cadccbb7f0a`; the answer has to be
  an Initial acknowledging packet number 0. A stack that has version 1 in
  its list but keys it from a draft salt, or tags its Retry with a draft's
  key, fails at the step that needs the key rather than at the version
  field. Nothing in the candidate's library is used to build or read any of
  the four packets.
* `cap-two-salts` — One server process, two versions, two key schedules,
  all three observed from outside. First the checker sends an Initial at a
  version this milestone does not support (draft-23, `0xff000017`) and reads
  the Version Negotiation packet that comes back: the list in it must
  contain `0x00000001` *and* `0xff00001d`, so version 1 was added to the
  negotiable set rather than substituted for it. Then it completes a full
  Retry exchange at draft-29 and another at version 1 against that same
  process, and opens each server answer with Initial keys derived from that
  version's salt — `afbfec289993d24c9e9786f19c6111e04390a899` for draft-29
  and `38762cf7f55934b34d179ae6a4c80cadccbb7f0a` for version 1. Both must
  open, and each answer must carry the version it was asked in. Finally each
  Retry's integrity tag is recomputed under the *other* version's Retry key
  and must not verify, which is what makes this a statement about two live
  key schedules rather than about one that happens to be reached twice.
* `cap-client-version-v1` — The candidate's `client` is pointed at a UDP
  socket the checker owns, and the first datagram it sends is read with the
  checker's own implementation: a long header carrying version `0x00000001`,
  Initial keys derived from the packet's own destination connection id under
  version 1's salt, header protection removed with the AES-ECB mask, packet
  protection removed with AEAD_AES_128_GCM, and a CRYPTO frame at offset 0
  whose bytes begin a TLS 1.3 ClientHello. This is `PROTOCOL_VERSION`
  observed from outside the process: a stack that still defaults to draft-29
  produces a datagram that does not open under this salt.
* `cap-sendinfo-at` — `Connection::send` returns `(usize, SendInfo)`, and
  `at` is an instant the caller can schedule against. Across a complete
  handshake driven through the in-memory pair, every `send` that produced
  bytes is bracketed by an `Instant` taken before the call and one taken
  after: `at` is never earlier than the first, and for a connection whose
  congestion window is nowhere near full it is never later than a second
  past the second. The `Instant` is also usable — `duration_since` against
  the pre-call reading succeeds rather than panicking. What this group can
  and cannot separate is worth stating plainly: it catches a far-future or
  unusable placeholder — `Instant::now() + 1_000_000`, or one that makes
  that `duration_since` underflow — and it does not catch `Instant::now()`.
  On a lossless in-memory pair whose congestion window is never driven to
  its limit there is nothing to pace, so a correct pacer's answer and
  `Instant::now()` are the same instant to within the clock's resolution,
  and no observation available to a black-box checker could tell them apart.
  What is in scope is the descriptor's shape and the usability of the
  instant, not the existence of a pacer.
* `cap-recvinfo-from` — The address plumbing the two descriptors carry. A
  client created with `connect(.., to, ..)` and a server created with
  `accept(.., from, ..)` are driven through a handshake in which every
  datagram is delivered with an explicit `RecvInfo { from }` naming the
  sender, and the `SendInfo.to` each side reports names its peer: the
  client's is the server address it was constructed with and the server's is
  the client address it was constructed with, on every packet of the
  exchange. A stack that ignores the addresses it is handed and reports a
  wildcard, a zero port or its own address fails.
* `cap-0rtt-client` — A client exports its TLS session state and a second
  client imports it. After a first handshake against a server whose `Config`
  has a fixed ticket key, `session()` returns `Some(bytes)`; a fresh client
  connection built from a fresh `Config`, given those bytes through
  `set_session()` before its first packet, completes a second handshake
  against a server built from a second `Config` carrying the same ticket
  key, and both ends then report `is_resumed() == true`. A control
  connection that is given no session reports `is_resumed() == false`
  against the same server, so the group cannot be passed by returning `true`
  unconditionally.
* `cap-h3-alpn-h3` — `h3::APPLICATION_PROTOCOL` is exactly
  `b"\x02h3\x05h3-29\x05h3-28\x05h3-27"` — the plain `h3` token first, the
  three draft tokens after it, wire format with one length byte each. A pair
  configured with the constant on both sides negotiates `h3` and not
  `h3-29`: `application_proto()` returns `b"h3"` on both ends. A pair whose
  server offers only `h3-29` still negotiates `h3-29`, so the list is a
  preference order and not a replacement.
* `cap-invalid-stream-state` — `Error::InvalidStreamState` carries the
  stream id. On an established connection, a `stream_recv` on a
  locally-initiated unidirectional stream — a send-only stream, which the
  local endpoint may never read — returns
  `Err(Error::InvalidStreamState(id))`, and `if let
  Error::InvalidStreamState(got) = err` binds `got` to the stream id that
  was passed in. Two different stream ids produce two different payloads, so
  the group cannot be passed by a variant that carries a constant.

`gate-endtoend` carries no capability of its own: it drives this step's
binaries end to end over loopback, and every gate group must pass. A stack
that implements every capability listed above and cannot complete one real
connection does not satisfy this step at all, however much of the new
capability it holds. The group table in `TEST_INTERFACE.md` is the contract
for it, as it is for the rest.

What must still hold: everything step 04 left working. 3 of this step's
groups re-run earlier milestones unchanged, listed by name in
`/app/WORKSPACE_HANDOFF.md`. They are gate groups as well, so any one of them
failing means the step is not satisfied. They are there because this step's
change plausibly breaks them: a rewrite that gets the new behaviour right
and drops an old one does not satisfy the step at all.

## 2. Read these local references

**Normative semantics:**

* `/app/spec/rfc9000.txt` — 15 (version numbers), 17.2 (long header
  packets), 17.2.5 (Retry), 19 (frame types). Version 1 is `0x00000001` and
  this is the document that says so. The long header layout, the Retry
  packet and the frame encodings are the ones the drafts already used; what
  changes is which version number is authoritative and that an endpoint now
  has to hold more than one of them at once.
* `/app/spec/rfc9001.txt` — 5.2 (initial secrets), 5.4 (header
  protection), 5.8 (Retry packet integrity), Appendix A (sample packet
  protection). Section 5.2 carries version 1's Initial salt,
  `38762cf7f55934b34d179ae6a4c80cadccbb7f0a`, and Section 5.8 carries
  version 1's Retry integrity key and nonce. Appendix A's worked example is
  a complete version-1 client Initial with its keys and its ciphertext, and
  it is the reference the checker's own implementation is checked against; a
  stack that disagrees with the appendix disagrees with the endpoint on the
  other side of every T2 group at this step.
* `/app/spec/rfc9002.txt` — 7.7 (pacing). `SendInfo.at` is the pacer's
  output. Section 7.7 describes the interval a sender should leave between
  packets of a congestion window and why a sender that ignores it bursts;
  this release moves that decision out of the send call and into a value the
  caller is expected to honour.
* `/app/spec/rfc9114.txt` — 3.1 (discovering an HTTP/3 endpoint), 7
  (frames). The `h3` ALPN token this milestone adds is the one RFC 9114
  registers for HTTP/3 over QUIC version 1. The draft tokens beside it are
  the same protocol with an earlier transport underneath.

**Additional files:**

* `/app/TEST_INTERFACE.md` — the contract between your code and the
  checker: every name, path and flag it depends on, the group table it
  defines, and what is deliberately out of scope. Where it and your intuition
  disagree, it wins.
* `/app/WORKSPACE_HANDOFF.md` — what the previous step left behind and
  which of its milestones remain in scope here.
* `/app/spec/README.md` — which sections of which document are
  load-bearing for this step, and `SHA256SUMS` proving the corpus is
  byte-identical to the IETF originals.
* `/app/spec/appendix-a-vectors.json` — RFC 9001 Appendix A's worked
  example as named byte strings. The checker carries a byte-identical copy of
  the same file in its own harness; no group at this step reads either one, so
  it is a reference for your own plumbing rather than a check that runs.
* `/app/.cargo/config.toml` — `QUICHE_BSSL_PATH` points at the BoringSSL
  prebuilt into the image. Link it; do not fetch or compile one. The
  build must succeed with no network.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Continue cumulatively in `/app`; preserve inherited source, manifests,
  documents, and handoff files.
- This environment has no network access, and everything the build needs is already installed. The upstream project's source, its releases, mirrors and package registries are unreachable, so a copy cannot be fetched by any route. Do not search for, download, copy, wrap, or invoke an
  online implementation or task-specific answer; do not inspect `/tests`,
  later-step directories, or solution material.
- Do not edit the supplied documents (`TEST_INTERFACE.md`, `spec/`) or weaken, filter, rename,
  or disable tests. Run the references' local checks and leave a reproducible
  handoff.
- This milestone is modelled on a real crate. Depending on, vendoring, or
  copying source from `quiche`, `tokio-quiche`, `quinn`, `s2n-quic`, `neqo` or
  any other QUIC or HTTP/3 implementation is a task-specific answer and is out
  of bounds; the tree is built offline and a manifest that names one will not
  resolve. Cryptography, byte-buffer and async-runtime crates are ordinary
  dependencies and are fine.
