# Step 4 — DATAGRAM extension and Error enum transformation

## 1. Implement this milestone

Add the DATAGRAM extension (draft-ietf-quic-datagram) to the transport
layer, allowing unreliable application data to bypass the stream layer
entirely. Transform the Error enum from C-style discriminants to idiomatic
Rust enum variants carrying associated data. Strengthen the ConnectionId
type from raw slices to a lifetime-aware wrapper. These changes land in
quiche 0.7.0 between draft-29 and the final RFCs.

DATAGRAM frames (types 0x30 and 0x31) carry application payloads up to one
packet's worth, with no delivery guarantees and no retransmission. The
receiver maintains a bounded queue; the sender learns the peer's maximum
datagram size from transport parameters and can query available capacity
before sending.

The Error enum changes identity: what was `pub enum Error { Done = -1, ...
}` with explicit C-compatible discriminants becomes `pub enum Error { Done,
BufferTooShort, ..., StreamStopped(u64), ... }`. At this step exactly one
variant gains associated data -- `StreamStopped(u64)`, carrying the peer's
STOP_SENDING error code. Every other variant, including
`InvalidStreamState`, is still bare here; the transformation is one variant
at a time across releases, not all at once, and `InvalidStreamState` does
not gain its own `u64` payload until 0.9.0 (step 05). This is a breaking
change for any code matching on the integer representation or using FFI
error codes, though the Rust-side match arms remain compatible by name.

ConnectionId transitions from `&[u8]` / `Vec<u8>` to `ConnectionId<'a>`, a
wrapper that can hold either a borrowed slice or an owned vector. Existing
call sites that passed raw slices now need `ConnectionId::from_ref(&slice)`
or `ConnectionId::from_vec(vec)`.

What is newly in scope here:

* `cap-datagram-roundtrip` — An in-memory candidate connection pair both
  enable datagrams with `enable_dgram(true, 16, 16)`. The client sends a
  256-byte pseudorandom payload via `dgram_send()`, the server receives it
  with `dgram_recv()`, echoes it back, and the client reads the echo and
  verifies byte-for-byte equality. The datagram traverses the connection
  without stream framing, and both sides confirm `dgram_max_writable_len()`
  returns `Some(n)` with a non-zero `n` before sending. This step's own
  `client`/`server` examples have no DATAGRAM flags at all (they take no
  configuration until step 06), so the application-level echo behaviour this
  group needs is not drivable end to end through the candidate's binaries;
  the in-memory `Connection` API pair is the only surface that observes it.
* `cap-datagram-not-retransmitted` — A packet from the candidate
  containing a DATAGRAM frame (type 0x30 or 0x31) is decrypted and
  deliberately dropped, and the candidate must never retransmit that
  frame -- even after loss recovery has genuinely run. Nothing is ever
  acknowledged; instead the candidate's own PTO (probe-timeout) loop is
  driven directly: its `timeout()` is called to learn when the next timer
  fires, then a sleep, then `on_timeout()`, repeated for up to 5 seconds
  of wall-clock time. A same-flight STREAM frame dropped the same way must
  reappear once a PTO fires -- proof that the timer genuinely ran and would
  have retransmitted something -- while the DATAGRAM frame's payload must
  never reappear in any packet the PTO loop produces. The candidate may send
  a new DATAGRAM frame with different data, but the original frame's payload
  never reappears. This distinguishes datagrams from STREAM frames, which
  are always retransmitted on loss via that same PTO mechanism. This is
  read-only: the candidate's own packets are decrypted with 1-RTT keys
  recovered from the candidate's keylog and no packet is ever constructed or
  injected, so the observation runs on the in-memory candidate
  connection pair (T3) with a passive crypto-derived read (T2) layered on
  top, not on an independently-injected wire packet.
* `cap-dgram-queue-bounded` — With `enable_dgram(true, 3, 3)`, the sender
  calls `dgram_send()` four times in rapid succession. The first three
  succeed (or the first three fit in the send queue), and the fourth returns
  `Error::Done` because the queue is full. Once the receiver has drained the
  queue with `dgram_recv()`, the sender can enqueue another. This confirms
  the queue length is enforced and that `Done` is the correct signal, not a
  panic or silent drop.
* `cap-error-enum` — A STOP_SENDING frame for stream 4 with error code
  0x123 is built independently, protected with 1-RTT keys derived from the
  candidate's own keylog, and injected as a crafted packet into the
  candidate's connection via `recv()` -- the candidate never sends
  STOP_SENDING itself at this milestone, so the frame comes from an
  independent peer rather than from a passive read. The next `stream_send()`
  call on that stream then returns `Err(Error::StreamStopped(0x123))`, and a
  match on the error extracts the code via `if let
  Error::StreamStopped(code) = err { assert_eq!(code, 0x123); }`.
  `stream_capacity()` on the same stream shows the same identity: it also
  returns `Err(Error::StreamStopped(0x123))` rather than a byte count, since
  a peer that no longer wants the data cannot have capacity for it either.
  This confirms the enum carries associated data rather than requiring a
  separate accessor, and that the STOP_SENDING signal is visible through
  both APIs that can observe it.
* `cap-dgram-max-size` — The client enables datagrams and completes the
  handshake. `enable_dgram` sets `max_datagram_frame_size` to 65536 on both
  sides -- quiche's own fixed value at this release, recommended by
  draft-ietf-quic-datagram-01 (see this step's DATAGRAM methods interface
  entry) -- so the transport parameter itself is never the binding limit;
  the real ceiling is how much of one packet is left after header, packet
  number and AEAD tag overhead. The group does not assert a literal byte
  count: it reads the candidate's own `dgram_max_writable_len()` ->
  `Some(n)` and then proves that `n` is the true boundary rather than a
  guess. It checks `n` is smaller than the connection's configured maximum
  UDP payload (so the claimed budget accounts for overhead at all), sends a
  datagram of exactly `n` bytes and requires it to succeed, sends `n + 1`
  bytes and requires `Err(Error::BufferTooShort)`, and then inspects the
  actual wire packet the `n`-byte datagram travelled in: that packet must be
  larger than `n` (room existed for the frame header) and less than 64 bytes
  larger than `n` (`carrier - max < 64`; the reported budget was not
  needlessly conservative). This self-calibrates against whatever `n` the
  candidate returns rather than asserting a fixed byte count directly, so it
  does not depend on the exact header, packet-number and AEAD-tag overhead a
  correct implementation computes -- only on that overhead being comfortably
  under the 64-byte slack, which it is at this release. What it does not
  catch is a *conservative* but self-consistent answer: a value that leaves
  more than 64 bytes unused still passes, because the slack is measured
  against the packet the candidate itself produced. An overstatement -- a
  budget larger than one packet can carry -- fails on the `n + 1` leg or on
  the wire-length check.
* `cap-dgram-not-negotiated` — The client enables datagrams with
  `enable_dgram(true, 16, 16)` but the server does not, or advertises
  `max_datagram_frame_size = 0`. After the handshake, the client's
  `dgram_max_writable_len()` returns `None`, and `dgram_send()` returns
  `Error::InvalidState` rather than sending a datagram the peer cannot
  parse. This confirms unilateral enablement does not break the connection.
* `cap-connid-wrapper` — `Header::from_slice()` over a draft-29 Initial
  packet returns a `Header` whose `dcid` and `scid` are `ConnectionId<'a>`.
  Calling `.as_ref()` on each yields a `&[u8]` that matches the raw bytes
  from the wire. Constructing a `ConnectionId::from_vec()` and a
  `ConnectionId::from_ref()` and comparing them with `.as_ref()` confirms
  both borrowed and owned paths work, and that the wrapper does not corrupt
  the ID. The group also exercises the rest of the type's hand-implemented
  (not derived) trait surface: `ConnectionId::default()` is the empty ID and
  equals `from_vec(vec![])`; `From<Vec<u8>>::into()` produces the same value
  `from_vec()` does; `.clone()` on both a borrowed and an owned value
  produces an equal value; two equal-content connection IDs built through
  different constructors hash identically; and `format!("{:?}", cid)` prints
  lower-hex with no separators (e.g. `deadbeef01`), not the bracketed list a
  derived `Debug` would produce.

`gate-endtoend` carries no capability of its own: it drives this step's
binaries end to end over loopback, and every gate group must pass. A stack
that implements every capability listed above and cannot complete one real
connection does not satisfy this step at all, however much of the new
capability it holds. The group table in `TEST_INTERFACE.md` is the contract
for it, as it is for the rest.

What must still hold: everything step 03 left working. 2 of this step's
groups re-run earlier milestones unchanged, listed by name in
`/app/WORKSPACE_HANDOFF.md`. They are gate groups as well, so any one of them
failing means the step is not satisfied. They are there because this step's
change plausibly breaks them: a rewrite that gets the new behaviour right
and drops an old one does not satisfy the step at all.

## 2. Read these local references

**Normative semantics:**

* `/app/spec/rfc9221.txt` — 3 (transport parameter), 4 (datagram frame
  types), 5 (behavior and usage, including 5.2 acknowledgement handling and
  5.3 flow control). The DATAGRAM extension as implemented at quiche 0.7.0.
  quiche 0.7.0 (2021-02-01) predates this RFC's publication (2022-03) and
  actually tracks draft-ietf-quic-datagram-01, but that draft is not in this
  task's seeded spec corpus and its wire format is unchanged in the RFC: the
  frame types are 0x30 and 0x31 (RFC 9221 §4), the transport parameter is
  `max_datagram_frame_size` (0x20, RFC 9221 §3), and the semantics are
  unreliable delivery with bounded queuing (§5). Treat this corpus copy as
  the normative text for those facts.
* `/app/spec/rfc9000.txt` — 13.3 (retransmission). DATAGRAM frames are
  never retransmitted and do not contribute to loss recovery. They are
  ack-eliciting but not retransmittable, a new category in the loss state
  machine.

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
