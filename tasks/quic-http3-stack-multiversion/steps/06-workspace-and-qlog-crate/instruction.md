# Step 6 — the cargo workspace, the qlog crate and the flag-taking apps

## 1. Implement this milestone

Reshape the repository. Up to now the library has been a single crate with
its root at `src/lib.rs` and a handful of positional-argument programs under
`examples/`. At this milestone the repository becomes a cargo workspace with
three members: `quiche/` holds the transport and HTTP/3 library, `qlog/` is
a separately versioned crate that owns the qlog schema and its streaming
serialiser, and `apps/` is a real binary crate that builds `quiche-client`
and `quiche-server` — programs that take flags instead of having their
listen address, certificate paths and document root compiled in.

The qlog split is not cosmetic. The trace format changes with it: a qlog is
no longer one JSON document written at the end of a connection but a JSON
Text Sequence, one record per line, each prefixed with the record separator
`0x1e`, written as the connection runs. `Connection::set_qlog` keeps its
signature and gains `set_qlog_with_level` beside it, so a caller can ask for
`Core`, `Base` or `Extra` importance.

Three transport capabilities arrive with the reshuffle. The receiver's flow
control window autotunes: it starts at the advertised initial limit and
doubles each time the application drains it inside two round trips, bounded
by two new `Config` setters. `RESET_STREAM` from the peer becomes visible to
the application as `Error::StreamReset(u64)` on the transport and
`h3::Event::Reset(u64)` on HTTP/3, where before a reset stream simply went
quiet. And the ACK frame's ECN-count form — frame type `0x03` — is parsed,
where before it was an unknown frame type and a connection error.

HTTP/3 renames one setting to match the published RFC:
`Config::set_max_header_list_size` becomes `set_max_field_section_size`, and
`h3::Connection::peer_settings_raw()` exposes the peer's SETTINGS exactly as
they arrived, unknown identifiers and all.

The crate root moves. `quiche/Cargo.toml` is now the library's manifest and
`quiche/src/lib.rs` its root; the workspace manifest at the repository root
declares the members and builds no code of its own:

```toml
# ./Cargo.toml
[workspace]
members = [ "quiche", "qlog", "apps" ]
exclude = [ "fuzz", "tools/http3_test" ]
```

`fuzz/` and `tools/http3_test/` are real crates that stay out of the
workspace — they are inherited from earlier steps and neither is a member —
and the root manifest has to say so, or cargo warns about an unlisted manifest
on every build. The `exclude` list is read structurally, like `members`.

Anything that cargo-builds the repository from the top must therefore find
three packages — `quiche`, `qlog` and `quiche_apps` — and a checker that
links the library links `quiche/`, not `.`. A tree that keeps `src/lib.rs`
at the root and adds a `qlog` directory beside it has not done this step.

`apps/` replaces `examples/`. The two programs it produces are
`target/debug/quiche-client` and `target/debug/quiche-server`, they parse
their command line with docopt, and every setting the old examples had
compiled in is now a flag:

```text
quiche-server --listen <addr> --cert <file> --key <file> --root <dir>
              --index <name> --max-data BYTES --max-stream-data BYTES
              --max-window BYTES --max-stream-window BYTES --idle-timeout MS
              --no-retry --dump-packets PATH ...
quiche-client [options] URL...
              --no-verify -n REQUESTS -H HEADER --wire-version VERSION
              --session-file PATH --max-window BYTES --max-stream-window BYTES ...
```

`--listen` defaults to `127.0.0.1:4433`, which is the address the previous
three milestones had no way to change. A server that still ignores its
arguments and binds a compiled-in port fails this step even if the transport
underneath is perfect.

The qlog serialisation flips from JSON to JSON-SEQ. A file that opens with
`{` and a `"traces"` array is the old format; the new one opens with the
byte `0x1e` followed by a single-line JSON object carrying
`"qlog_format":"JSON-SEQ"`, and every event after it is another
`0x1e`-prefixed line.

`Error` and `h3::Event` both gain a variant, so an exhaustive `match` over
either stops compiling:

```rust
quiche::Error::StreamReset(u64)   // new
quiche::h3::Event::Reset(u64)     // new
```

and the HTTP/3 setting is renamed rather than aliased:

```rust
// before (0.9.0)                     // after (0.11.0)
h3::Config::set_max_header_list_size  h3::Config::set_max_field_section_size
```

What is newly in scope here:

* `cap-workspace-layout` — The shape of the produced tree, read
  structurally and not searched for identifiers. The repository root must
  carry a workspace manifest whose `members` list names three directories
  and whose `exclude` list names `fuzz` and `tools/http3_test` — two real,
  nested crates that are not workspace members and would otherwise make
  cargo warn about an unlisted manifest; `quiche/Cargo.toml` must declare a
  package called `quiche` whose `version` field reads exactly `0.11.0` — the
  version is parsed out and compared, not skimmed past — with its library
  root at `quiche/src/lib.rs`; `qlog/Cargo.toml` must declare a separate
  package called `qlog`; `apps` must declare the package `quiche_apps`; and
  there must be no `src/lib.rs` at the repository root, because the crate
  moved rather than being copied. The workspace's shared `target/debug/`
  must then hold two executables named `quiche-client` and `quiche-server`.

  That last check is also where the previous three milestones' no-argument
  `examples/http3-server` stops mattering, and it is worth being exact about
  how. The example *source* is still in the tree — it moved with the crate,
  to `quiche/examples/http3-server.rs`, and deleting it is not required.
  What changed is that no group resolves it by name any more: the T1 groups
  at this step ask for `quiche-server`, so a tree that still builds the
  example but does not produce `quiche-server` fails here and at every T1
  group, while a tree that produces both is fine. The checker reads the
  manifests with a TOML parser and checks the files on disk; it does not
  grep any source file.
* `cap-apps-cli-flags` — Two of the candidate's own processes on loopback
  UDP, configured entirely from the command line. The checker picks a free
  ephemeral port, writes a document root under its own scratch directory
  containing a 24 KiB generated body and a differently named index file, and
  starts `quiche-server --listen 127.0.0.1:PORT --cert ... --key ... --root
  ... --index ...`. It then requires that the server bound exactly that
  port; that `quiche-client --no-verify https://127.0.0.1:PORT/payload.bin`
  exits 0 having written the 24 KiB body to stdout byte for byte, with no
  framing and no headers; that the same client with `-n 3 --dump-responses
  DIR` exits 0 having written nothing at all to stdout and leaving exactly
  three files in `DIR`, named `payload.bin`, `payload.bin.2` and
  `payload.bin.3`, each of them the whole 24 KiB body; that
  `https://127.0.0.1:PORT/` is answered with the contents of the file
  `--index` named rather than with `index.html`; and that a path outside the
  root is answered `Not Found!` without killing the server. Every one of
  those facts is a flag the previous milestone's binaries had no way to
  accept. The three responses are checked through `--dump-responses` rather
  than on stdout because the three requests are in flight together and their
  bodies interleave there.

  Four more flags are checked the same way, each against its own fresh
  server/client pair: `--dump-packets DIR` must leave one raw file per
  incoming datagram in `DIR`, named `<n>.pkt` for a decimal counter `n` that
  rises in arrival order (the checker orders the directory by parsing that
  stem as a number), on both the server started above and a client started
  below, before any header of that datagram is even parsed --
  checked by requiring at least one dumped file to still carry the
  long-header form bit every Initial packet sets. `--no-retry` is checked by
  parsing the raw long-header bytes `--dump-packets` leaves behind rather
  than by reading any process's own output: without the flag, a token-less
  Initial must be met with a genuine, real-version Retry packet (type bits
  `0b11` at RFC 9000's version-independent header offset); with it, no such
  packet may appear anywhere in what the client received. `--wire-version`
  is checked the same way in the other direction: the client's own default
  (`babababa`) is a reserved "grease" version that always trips version
  negotiation first, so an explicitly chosen, distinct grease value must be
  the very first version the server ever receives. `--session-file PATH` is
  checked by requiring the named file to exist and be non-empty after a
  connection completes, since a client that never called into that
  TLS-session-caching path would leave nothing there.

  `-H`/`--header` is deliberately out of scope at this group and at this
  step: `build_h3_response` (the reference server's only request-header
  consumer) reads exactly `:scheme`, `:authority`/`host`, `:path`, `:method`
  and `priority` and silently drops every other header, so a header this
  flag adds has no server-observable effect through any channel a group here
  can reach short of decrypting and QPACK-decoding the request --
  `--dump-packets` captures ciphertext, and no process's own
  stdout/stderr/qlog output echoes request headers back. Building that
  decode capability for one low-priority CLI flag would be materially more
  work than everything else in this group put together.
* `cap-qlog-jsonseq` — The trace file a real connection leaves behind,
  parsed as JSON Text Sequences. The checker runs a request against the
  candidate's own server with `QLOGDIR` pointing at an empty directory — and
  reads it while the server is still running, so the records must be flushed
  as the connection goes rather than held in memory until the process exits.
  It then requires: exactly one `server-*.sqlog` and one `client-*.sqlog`; every
  record in each file prefixed with the byte `0x1e` and parsing as a single
  JSON object; a first record whose `qlog_version` is `"0.3"`, whose
  `qlog_format` is `"JSON-SEQ"` and whose `trace.vantage_point.type` is
  `"server"` on the server side and `"client"` on the client side; every
  later record carrying exactly the keys `time`, `name` and `data`; and the
  set of `name` values being exactly `transport:parameters_set`,
  `transport:packet_sent`, `transport:packet_received`,
  `transport:data_moved` and `recovery:metrics_updated` — no more, no fewer.
  It then requires that the `parameters_set` record with `"owner":"local"`
  carries the four values the server was given on its command line
  (`--max-data`, `--max-stream-data`, `--max-streams-bidi`,
  `--idle-timeout`) under their RFC 9000 names, which is what makes this a
  statement about a trace of the connection that happened and not about a
  template. The same record is also required to carry the correct value for
  every other field the local transport parameters expose:
  `initial_max_stream_data_uni` and `initial_max_streams_uni` (the apps
  binary feeds these from `--max-stream-data`/its own default,
  respectively), `max_udp_payload_size` (the 1350-byte constant every
  quiche-apps binary hardcodes), `ack_delay_exponent`, `max_ack_delay` and
  `active_connection_id_limit` (quiche's own built-in transport-parameter
  defaults), and `disable_active_migration` (hardcoded `true` by the apps
  binary). The `parameters_set` record with `"owner":"remote"` -- the
  server's report of what quiche-client sent it -- is held to the same
  standard: since the client in this connection is launched with none of the
  matching CLI overrides, every one of these fields is checked against
  quiche-client's own docopt default or the same apps/quiche constant, not
  merely checked for presence or counted. No substring search is used
  anywhere.

  A second connection, this time with the client started under a tiny,
  pinned `--max-stream-window` (and a matching small `--max-stream-data`)
  against a body many times that size, requires that the client's own trace
  show at least ten MAX_STREAM_DATA extensions on the response stream and
  that no single extension grow the offered window by more than four times
  the configured ceiling. `--max-stream-window` and `--max-stream-data` are
  CLI-only plumbing on top of the `Config` receiver-window and stream-window
  setters -- those setters are already exercised in-memory by
  `cap-stream-window-autotune` -- so this is the check that these two flags
  actually reach them, rather than being silently ignored in favour of the
  built-in defaults. `--max-window`, the connection-level sibling, is parsed
  by the binaries but exercised by no group at this step, so nothing here
  observes whether it is wired up.
* `cap-ecn-ack-frame` — The checker builds and encrypts a 1-RTT packet of
  its own and requires the candidate's server to read it. Packet protection
  is entirely independent of the candidate: the client's application traffic
  secret is taken out of the candidate through the standard NSS keylog hook,
  the AEAD, the nonce and the header-protection mask are implemented
  separately, and the keys are confirmed right by first decrypting one of the
  candidate's own 1-RTT packets with them. The payload is two frames: an ACK
  frame of type `0x03` acknowledging packet 0 with ECN counts `(ECT0, ECT1,
  CE) = (7, 11, 3)`, followed by a STREAM frame carrying 64 generated bytes
  on stream 8. Feeding that packet to the server must return `Ok`, and
  `stream_recv` on stream 8 must then yield those 64 bytes exactly — which
  is only possible if all three ECN varints were consumed and the parser
  arrived at the frame after them. The same packet at the previous milestone
  is `Error::InvalidFrame` and a `FRAME_ENCODING_ERROR` connection close, so
  the group is also checked in the other direction. Every packet the server
  then wants to send is drained, decrypted with those independent keys and
  decoded frame by frame: none of them may be a
  `CONNECTION_CLOSE`, one of them must carry an ACK range covering the
  injected packet number, and the server must be neither closed nor draining
  afterwards — `is_closed()` and `is_draining()` (the two accessors the group
  calls by name) must both return `false`. The drain is done by hand rather
  than by exchanging with the pair, because the injected packet carries a
  packet number the pair's own client never sent and the resulting ACK is
  legitimately a protocol violation at that client.
* `cap-stream-window-autotune` — Three in-memory pairs that differ in
  exactly one setting. All three receivers advertise
  `initial_max_stream_data_bidi_remote = 1024` and are given a
  connection-level limit far too large to ever be the thing that bites; they
  are given `set_max_stream_window(1024)`, `(2048)` and `(8192)`
  respectively. Each runs eight identical rounds of "offer the sender the
  whole buffer, exchange, drain the receiver completely", and the checker
  records how many bytes the sender was allowed to place in each round.

  The pinned receiver must hand out exactly 1024 bytes in each of the eight
  rounds — the limit advances by one window each time, which is the previous
  milestone's behaviour and the floor this group holds the candidate to. For
  the other two the first round must still be 1024, the allowance must never
  go down, it must never exceed the maximum that receiver was configured
  with, it must reach that maximum before the eighth round, and across the
  whole run it must take only the values on the doubling ladder between the
  two — {1024, 2048} for the first and {1024, 2048, 4096, 8192} for the
  second, and no value in between. Raising the maximum must move strictly
  more data in the same eight rounds, and the 8192 receiver must move at
  least four times what the pinned one moved.

  A receiver that ignores `set_max_stream_window` fails the pinned run and
  the never-exceeds bound; a receiver that never grows the window fails the
  ladder and the reaches-its-maximum bound; a receiver that grows the window
  by something other than doubling fails the ladder. The window is
  deliberately small enough that the initial congestion window can never be
  the limiter, so every number in the run is a statement about flow control.

  A fourth, separate run repeats the 8192 case but pins the connection-level
  ceiling to exactly that same 8192 instead of to the oversized value the
  first three runs use — the tightest ceiling a single-stream connection
  could need. It must reach the same {1024, 2048, 4096, 8192} ladder and the
  same eighth-round maximum as the third run: the connection window, kept at
  or above the largest stream window in play, must not throttle a stream
  window that grows up to and stays within it.
* `cap-stream-reset-error` — What a reader learns when the writer resets
  the stream. The checker has the client send 100 bytes on stream 0 without
  finishing it, lets the server read them, then has the client reset the
  stream with application error code `0xbeef` and exchanges again. The reset
  stream must first be offered to the application as readable — that is the
  path the reset travels — and `stream_recv` on it must then return
  `Err(Error::StreamReset(0xbeef))`, the exact variant carrying the exact
  code. The group also requires that the reset is reported once rather than
  repeatedly: having handed the reset over, the stream must drop out of
  `readable()` and must not come back into it however much more is
  exchanged. Asking the stream again must keep returning the same
  `Error::StreamReset(0xbeef)` rather than data or `Error::Done`. Finally a
  second, untouched stream on the same connection must still deliver its
  data and its `fin`, and neither endpoint may close. At the previous
  milestone there is no such variant and a reset stream simply goes quiet.
* `cap-h3-peer-settings-raw` — The peer's SETTINGS frame, read back
  exactly as it arrived. The checker drives the candidate's transport
  `Connection` directly on the client side — no HTTP/3 connection there —
  and writes an HTTP/3 control stream by hand onto client uni-directional
  stream 2: the stream type varint `0x00`, then a SETTINGS frame carrying
  four pairs in this order, one of them an identifier no implementation
  recognises:

  ```text
  (0x01, 4096)      QPACK_MAX_TABLE_CAPACITY
  (0x1f * 3 + 0x21, 0xdead)   a reserved "grease" identifier
  (0x06, 1024)      MAX_FIELD_SECTION_SIZE
  (0x07, 12)        QPACK_BLOCKED_STREAMS
  ```

  The server side is a real `h3::Connection`. After the exchange,
  `peer_settings_raw()` must return `Some` of exactly those four pairs in
  exactly that order — the unknown identifier included and not filtered out,
  the order preserved rather than sorted or canonicalised. The group also
  requires that polling the server raises no error, because an unrecognised
  SETTINGS identifier must be ignored rather than rejected, and that
  `peer_settings_raw()` was `None` before the frame arrived.

  A "grease" identifier is not the only kind of unrecognised one: the
  HTTP/2- reserved identifiers (`0x0`, `0x2`, `0x3`, `0x4`, `0x5`) are still
  a connection error under RFC 9114 Section 7.2.4.1. On a second,
  independent pair — SETTINGS may only be sent once per control stream — the
  checker writes a control stream carrying only `(0x02, 1)` and requires the
  server's poll to raise exactly `h3::Error::SettingsError`.

  Separately, this milestone renames `set_max_header_list_size` to
  `set_max_field_section_size` on `h3::Config`, and the rename is not merely
  cosmetic: on a third pair, built with the server's own `h3::Config`
  carrying a small `set_max_field_section_size` limit, a request whose
  header block exceeds that limit must still raise
  `h3::Error::ExcessiveLoad` when the server polls it, exactly as
  `set_max_header_list_size` did at the previous milestone. Of the three
  identifiers this implementation recognises by number
  (`0x1`/QPACK_MAX_TABLE_CAPACITY, `0x6`/MAX_FIELD_SECTION_SIZE and
  `0x7`/QPACK_BLOCKED_STREAMS), only `0x6` has an effect reachable through
  the public API at this milestone: this quiche version has no QPACK dynamic
  table implementation, so `0x1` and `0x7` are accepted, stored and returned
  by `peer_settings_raw()` exactly like any other identifier but drive no
  further observable behaviour. That is why this group's functional-effect
  case is built around `0x6` specifically rather than a generic "recognised
  identifiers take effect".

`gate-endtoend` carries no capability of its own: it drives this step's
binaries end to end over loopback, and every gate group must pass. A stack
that implements every capability listed above and cannot complete one real
connection does not satisfy this step at all, however much of the new
capability it holds. The group table in `TEST_INTERFACE.md` is the contract
for it, as it is for the rest.

What must still hold: everything step 05 left working. 3 of this step's
groups re-run earlier milestones unchanged, listed by name in
`/app/WORKSPACE_HANDOFF.md`. They are gate groups as well, so any one of them
failing means the step is not satisfied. They are there because this step's
change plausibly breaks them: a rewrite that gets the new behaviour right
and drops an old one does not satisfy the step at all.

## 2. Read these local references

**Normative semantics:**

* `/app/spec/rfc9000.txt` — 4 (flow control), 4.1 (data flow control), 4.2
  (increasing flow control limits), 13.4 (explicit congestion notification),
  19.3 (ACK frames), 19.3.2 (ECN counts), 19.4 (RESET_STREAM frames).
  Section 19.3.2 is the layout of the three ECN-count varints and the rule
  that frame type `0x03` is the only ACK form that carries them; consuming
  exactly three varints in exactly that order is what this step adds to the
  frame parser. Sections 4.1 and 4.2 are the flow-control limits the
  autotuning receive window moves, and in particular the advice in 4.2 that
  a receiver should extend a limit before the sender is blocked — the window
  doubling is an implementation of that advice, not a departure from it.
  Section 19.4 is the `RESET_STREAM` frame whose error code now reaches the
  application.
* `/app/spec/rfc9002.txt` — A.7 (on receiving an acknowledgment), 7
  (congestion control). The ECN counts on an ACK frame are input to loss
  recovery, and this is the document that says what a sender does with an
  acknowledgement it has just parsed. It also fixes the round-trip-time
  estimate the window autotuner compares its own update interval against.
* `/app/spec/rfc9114.txt` — 7.2.4 (SETTINGS), 7.2.4.1 (defined SETTINGS
  parameters), 7.2.4.2 (initialisation), 8.1 (HTTP/3 error codes), 4.1 (HTTP
  message framing). `SETTINGS_MAX_FIELD_SECTION_SIZE` is the name Section
  7.2.4.1 gives identifier `0x6`, which is why the Rust setter is renamed
  here. Section 7.2.4 is also the source of the two rules
  `peer_settings_raw` has to be consistent with: unrecognised identifiers
  must be ignored rather than rejected, and the identifiers reserved for
  HTTP/2 compatibility must be treated as a connection error. Section 8.1
  carries `H3_EXCESSIVE_LOAD` = `0x107`.
* `/app/spec/rfc9218.txt` — 2 (priority parameters). Not this step's
  business, and included only to say so: HTTP/3 priority stays where step 03
  left it. `send_response_with_priority` and `stream_priority` are
  unchanged, and nothing in this milestone reads a `priority` request
  header.

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
