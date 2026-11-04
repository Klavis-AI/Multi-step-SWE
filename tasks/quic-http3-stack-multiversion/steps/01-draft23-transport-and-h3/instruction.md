# Step 1 — a draft-23 QUIC transport and HTTP/3 stack

## 1. Implement this milestone

Build a QUIC transport library and an HTTP/3 layer on top of it, to IETF
QUIC draft-23, from nothing. At the end of this step `/app` holds a Rust
crate named `quiche` that speaks draft-23 to a peer it has never met, four
example binaries that use it, and a C header that a C program can link
against.

Draft-23 is not RFC 9000. The RFCs are in `spec/` because fourteen later
steps converge on them and because the framing, the packet protection
construction and the HTTP/3 and QPACK layers are unchanged in substance;
where draft-23 differs, the differences are listed below and they are
load-bearing. This environment has no network access, and everything the build needs is already installed. The upstream project's source, its releases, mirrors and package registries are unreachable, so a copy cannot be fetched by any route.

`spec/` also carries RFC 9001 Appendix A's worked example, as
`rfc9001-appendix-a.txt` and `appendix-a-vectors.json`. That example is QUIC
**version 1**: its salt is `38762cf7f55934b34d179ae6a4c80cadccbb7f0a` and
its version number is `00000001`, and **neither is this step's**. It ships
so you can check your `HKDF-Expand-Label`, AEAD and header-protection
plumbing against a published vector; the salt and version to implement are
the draft-23 ones below. `spec/SHA256SUMS` covers the ten RFC texts and not
those two derived files.

What is newly in scope here:

* `cap-initial-keys` — The first datagram the candidate's client sends is
  decrypted by the checker's own draft-23 implementation: Initial secrets
  from salt `c3eef712…` over the client's destination connection ID, header
  protection removed with the AES-ECB mask, packet protection removed with
  AEAD_AES_128_GCM, and the plaintext then contains a CRYPTO frame at offset
  0 whose bytes begin a TLS 1.3 ClientHello. Nothing in the candidate's
  library is used to read it.
* `cap-header-parse` — `Header::from_slice` over six fixed datagrams with
  exactly one correct answer each — a draft-23 Initial with a token, a
  Retry, a Handshake, a 0-RTT packet, a Version Negotiation packet and a
  short header with a 16-byte connection ID — reporting the right `ty`,
  `version`, `dcid`, `scid` and `token` for every one, plus two truncated
  datagrams that must return `Err(Error::BufferTooShort)` rather than panic.
* `cap-stateless-retry` — The checker sends a well-formed draft-23 Initial,
  with no token, to the candidate's running server. The server answers with
  a Retry packet: long header, type bits `3`, version `0xff000017`, the
  ODCID field echoing the destination connection ID the checker used, and a
  non-empty token. The checker then replays an Initial carrying that token
  and the server proceeds instead of retrying again, answering with an
  Initial of its own that acknowledges the one it accepted -- the only
  packet a server with no handshake keys can send, and the one RFC 9000
  Section 13 requires of it.
* `cap-stream-transfer` — The candidate's `client` fetches a 512 KiB
  pseudorandom file from the candidate's `server` over UDP loopback and
  writes it to stdout byte for byte. Large enough to cross the initial
  congestion window, the initial flow-control window and many packet
  boundaries, so a stack that reassembles out-of-order or retransmitted data
  incorrectly produces the wrong bytes rather than the right ones by luck.
* `cap-h3-get` — The candidate's `http3-client` performs a GET against the
  candidate's `http3-server` and receives the exact body, over the `h3-23`
  ALPN. HTTP/3 end to end: control streams, SETTINGS, a QPACK-encoded
  HEADERS frame and DATA.
* `cap-flow-control` — With `initial_max_stream_data_bidi_remote` set to
  4096, the sender's `stream_capacity` falls to zero after 4096 bytes are in
  flight and `stream_send` returns `Ok(0)` -- it accepts no more, and
  reports that by writing zero bytes rather than by failing with `Done`;
  once the receiver has read those bytes and the resulting MAX_STREAM_DATA
  has been delivered, capacity reopens and the remainder of a 64 KiB
  transfer arrives intact and in order.

  The receiver extends the limit by what the application has actually
  consumed, which is what this milestone's stack does: it does not auto-tune
  the window upward. A 64 KiB transfer through a 4096-byte window therefore
  takes at least sixteen rounds of read-and-reopen, and the group checks
  that it did.
* `cap-slow-start-window` — `Stats::cwnd` on a connection that has sent
  nothing is the ten-packet initial window, `10 × 1452 = 14520` bytes — and
  it is that on a second pair built with `set_max_packet_size(1200)` too,
  because the initial window is a constant of the congestion controller
  rather than ten times the configured packet size. It then grows past `2 ×
  14520` over eight round trips of acknowledged data on a lossless pair, and
  never shrinks along the way, which is slow start.
* `cap-h3-static-table` — A request whose field lines are all QPACK
  static-table entries (`:method: GET`, `:scheme: https`, `:path: /`,
  `:authority`) and a response carrying `:status: 200` plus a literal header
  with a non-ASCII-safe value round-trip through the candidate's own HTTP/3
  layer and arrive at the peer as the identical name/value list, in order,
  delivered by `poll` as `h3::Event::Headers(list)`.
* `cap-idle-timeout` — With `set_idle_timeout(300)` on both sides and an
  established connection left alone, `timeout()` reports a deadline inside
  that window, `on_timeout()` at the deadline closes the connection, and
  `is_closed()` becomes true — while the same connection exercised with
  traffic stays open well past 300 ms.
* `cap-c-api` — A C program the checker writes is compiled against
  `include/quiche.h` under the root it found the crate in — `/app/include/quiche.h`
  in the layout this step starts from — linked against the candidate's **`libquiche.a`**,
  and run. The checker's link line is `-lpthread -ldl -lm -lstdc++` and
  nothing else — in particular it passes no `-L$QUICHE_BSSL_PATH -lssl
  -lcrypto` — so the archive has to be self-contained. rustc folds a native
  library into a `staticlib` output only when the build script declared it
  with `cargo:rustc-link-lib=static=`; declaring BoringSSL the default
  (dylib) way still yields a working `cdylib` and working example binaries,
  but an archive that fails to link with undefined `SSL_*` and `EVP_*`
  symbols. The `cdylib` must also exist beside it, and the checker checks
  that it does. It calls `quiche_version`, builds a config, calls
  `quiche_connect`, pulls the first datagram out with `quiche_conn_send`,
  and reads it back with `quiche_header_info` — reporting a draft-23 Initial
  whose **source** connection ID is the one it passed to `quiche_connect`,
  and whose destination connection ID is a non-empty one the client chose
  for itself. The same program also requires the first datagram to be at
  least `QUICHE_MIN_CLIENT_INITIAL_LEN` (1200) bytes and to carry no token;
  both are the protocol's own rules for a client's first Initial. The header
  must therefore be valid C, and the symbols must actually be exported.

`gate-endtoend` carries no capability of its own: it drives this step's
binaries end to end over loopback, and every gate group must pass. A stack
that implements every capability listed above and cannot complete one real
connection does not satisfy this step at all, however much of the new
capability it holds. The group table in `TEST_INTERFACE.md` is the contract
for it, as it is for the rest.

There is no previous step. `/app` holds at least this document,
`TEST_INTERFACE.md`, `WORKSPACE_HANDOFF.md`, `spec/`, `Cargo.lock`,
`timer.sh` and `.cargo/config.toml`; keep every one of them, and
`.cargo/config.toml` in particular, because it is where the path to the
prebuilt BoringSSL is written down in the workspace. Nothing else in this
container sets that variable, so a build script that reads it has nothing to
link against if the file goes. One more file, `setup.sh`, is the
environment's rather than yours: the harness runs it before you start, at
every step, and it is what pins the milestone's dependency set (see the
offline-execution note in `TEST_INTERFACE.md`); it is re-uploaded each step, so there
is nothing to preserve in it. Apart from those, the workspace layout is yours
to create, within the constraints `TEST_INTERFACE.md` states. Every later
milestone builds on what you lay down here.

## 2. Read these local references

**Normative semantics:**

* `/app/spec/rfc9000.txt` — 12 (packets and frames), 13 (delivery), 16–17
  (varints, packet formats), 18–19 (transport parameters, frame types). The
  framing, the variable-length integer encoding, the stream state machine
  and the transport parameters are the same in draft-23 as in the published
  RFC. The packet header formats are the same except for the Retry layout
  and the version number.
* `/app/spec/rfc9001.txt` — 5 (packet protection), 5.1–5.3 (key
  derivation, AEAD), 5.4 (header protection), 8.2 (transport parameters
  extension). The construction is draft-23's construction; only the Initial
  salt differs. Appendix A's worked example ships beside the RFC as
  `rfc9001-appendix-a.txt` and `appendix-a-vectors.json`, and it is the QUIC
  **version 1** example — salt `38762cf7…`, version `00000001` — so use it
  to check your HKDF and AEAD plumbing, never as this step's salt; neither
  file is listed in `SHA256SUMS`, which covers the ten RFC texts. Section
  8.2's extension codepoint is `0xffa5` here rather than `0x39`, and the
  pinned BoringSSL supplies it.
* `/app/spec/rfc9002.txt` — 6 (loss detection), 7 (congestion control).
  Acknowledgement handling, the PTO timer and a NewReno-shaped congestion
  controller. Slow start's behaviour is observed at this step.
* `/app/spec/rfc9114.txt` — 4 (expressing HTTP), 6 (streams), 7 (frames),
  8 (error handling). HTTP/3 over the transport you just built: the control
  stream, SETTINGS, the HEADERS and DATA frames, and the request/response
  exchange the checker drives end to end.
* `/app/spec/rfc9204.txt` — 3 (static table), 4.5 (field line
  representations), Appendix A (the static table itself). QPACK. At this
  step only the static table and literal representations are exercised; the
  dynamic table may be present but nothing requires it.

**Additional files:**

* `/app/TEST_INTERFACE.md` — the contract between your code and the
  checker: every name, path and flag it depends on, the group table it
  defines, and what is deliberately out of scope. Where it and your intuition
  disagree, it wins.
* `/app/WORKSPACE_HANDOFF.md` — what the fourteen later steps will assume
  of the workspace you are about to lay out, and which names in it are not
  yours to change.
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
