# Test interface — step 09, Draft removal, key update, and prioritised stream iteration

This is the contract between your implementation and the checker. Every
name, path and flag the checker depends on is written down here; nothing
outside this document is assumed about your code. Where the document and
your intuition disagree, the document wins — it is what the tests were
written against.

Generated, together with this step's interface contract, from one source.
The two cannot disagree.

## What this step is

Stop being a draft implementation. Since step 05 the stack has spoken both
RFC 9000 version 1 and draft-29, and every packet-handling path has had to ask
which of the two it was looking at. This milestone deletes drafts 27, 28 and 29
outright: `version_is_supported` recognises exactly one version, the Initial
salt is no longer a function of the version, and a client that offers a draft
gets a Version Negotiation packet listing only version 1.

With one version to protect, the connection can finally do the thing RFC 9001
requires of a long-lived one: rotate its own keys. A peer signals a key update
by flipping the key-phase bit in the short header and encrypting with the next
generation of application keys; the receiver derives that generation, decrypts,
flips its own bit, and keeps a copy of the old key around for reordered
packets. This step implements the receiving half of that exchange, including
the rule that a peer may not update twice without the first update being
acknowledged.

Three smaller capabilities arrive with it and all three are about giving the
application control it did not have. Streams gain prioritised iteration --
`stream_readable_next` and `stream_writable_next` return one stream at a time
in priority order and disarm it until it changes again, rather than handing back
the same unordered set on every call. The congestion controller's starting
window becomes configurable. And the application can ask the connection to send
an ACK-eliciting packet on demand, on a particular path, instead of waiting for
data to write.

The HTTP/3 datagram API added at step 05 is deleted. The transport-level
DATAGRAM frame API from step 04 stays exactly where it is; it is the HTTP/3
wrapper above it that goes.

## The flip

Something an earlier step established changes meaning here. This is the
part a candidate most often gets half-right: the new behaviour appears,
the old one is left working beside it, and both the new group and the
inherited group pass for the wrong reason — until the step that
requires the old behaviour to be *gone*.

Three things stop compiling, and all three are deletions.

**The HTTP/3 datagram API is gone.** `quiche::h3::Connection::send_dgram`,
`recv_dgram` and `dgram_max_writable_len` are removed. A step 05-08 solution
that calls any of them stops compiling and there is no renamed replacement to
reach for -- the capability is withdrawn at this milestone, not moved.

Be precise about the scope. The **transport** datagram API is untouched and
this step still requires all of it: `Config::enable_dgram`,
`Connection::dgram_send`, `dgram_recv`, `dgram_max_writable_len`,
`dgram_send_queue_len` and the rest keep working exactly as step 04 left them.
What disappears is the HTTP/3 layer's own flow-id-prefixed wrapper over them.
An implementation that deletes both fails this step's inherited group; an
implementation that deletes neither fails the removal group.

**`Connection::max_active_source_cids` is gone**, replaced by two accessors
that answer more useful questions: `source_ids()` iterates the connection IDs
this endpoint has issued and `retired_scids()` counts the ones the peer has
retired. `active_source_cids()` and `source_cids_left()` are unchanged.

**`Connection::stream_init_application_data` and
`Connection::stream_application_data` are gone.** The per-stream
application-data slot they managed does not survive the priority rework that
brings in `stream_readable_next`; nothing replaces it, and a solution that
used it has to keep its own map keyed by stream ID.

Nothing in the hot path changes shape. `connect`, `accept`, `send`, `recv`,
`send_on_path`, `SendInfo` and `RecvInfo` are all exactly as step 08 left
them, and every step 01-08 group that does not touch a deleted symbol must
still pass untouched.

## Interface elements the checker depends on

### `The supported version list`

`quiche::version_is_supported(version: u32) -> bool` returns true for exactly
one value, `PROTOCOL_VERSION` (`0x0000_0001`), and false for everything else --
including `0xff00_001b`, `0xff00_001c` and `0xff00_001d`, the three drafts step
05 through step 08 accepted.

`PROTOCOL_VERSION` keeps its value and its type. The three draft constants are
removed; nothing takes their place, and no public constant names a draft
version at this milestone.

`quiche::negotiate_version(scid, dcid, out) -> Result<usize>` writes a Version
Negotiation packet whose version list is exactly one entry long and contains
`PROTOCOL_VERSION`. As at every earlier milestone it echoes the client's Source
Connection ID as the packet's Destination Connection ID and the client's
Destination Connection ID as the packet's Source Connection ID (RFC 9000
Section 6.1), and the Version field is zero.

The single-version rule is not only about the negotiation path: the Initial
secret derivation no longer selects a salt. There is one Initial salt, RFC
9001's `38762cf7f55934b34d179ae6a4c80cadccbb7f0a`, and it is applied
unconditionally.

### `Key update, the receiving side`

`Connection` gains a key-phase bit and the machinery to follow a peer that
flips it. There is **no public method for initiating** a key update at this
milestone -- the capability this step requires is the responder.

**The AEAD is assumed, and it is AES-128-GCM.** The verifier opens your
packets with its own implementation, which derives 1-RTT keys from the traffic
secret in your keylog for `TLS_AES_128_GCM_SHA256` and for nothing else. RFC 9001
Section 5.3 permits `TLS_AES_256_GCM_SHA384` and `TLS_CHACHA20_POLY1305_SHA256`
as well; a stack that negotiates either is read as sending packets that do not
decrypt, and `cap-key-update` then fails with a message about the verifier's own
keys. The keylog line format carries no suite field, so nothing else tells the
checker which one you chose. Keep the pinned BoringSSL's preference order:
AES-128-GCM first.

The rules, all of them from RFC 9001 Section 6:

* The key phase is one bit in the short header's first byte, `0x04`. It lives
  under header protection, so it is not visible to an observer without the
  header-protection key.
* When a received short-header packet's key phase differs from the
  connection's own, the receiver derives the next generation of application
  keys and tries to decrypt with them. The next generation is
  HKDF-Expand-Label over the current traffic secret with the label
  `"quic ku"`, followed by the usual `"quic key"` and `"quic iv"` derivations.
* **The header-protection key is not updated.** It is derived once, from the
  handshake's traffic secret, and every generation of packet keys reuses it.
  An implementation that re-derives it along with the packet key cannot remove
  header protection from the peer's first updated packet and fails.
* The update is verified only when a packet actually decrypts under the new
  keys. On success the receiver replaces its sealing key, keeps the *previous*
  opening key so that reordered packets from before the update still decrypt,
  flips its own key phase, and from then on sends under the new generation.
* A peer that updates a second time before the first update has been
  acknowledged is a protocol violation: `recv` returns
  `Err(Error::KeyUpdate)`.

**Two places where this contract is stricter than the RFC it follows.** RFC
9001 Section 6.2 makes closing the connection with `KEY_UPDATE_ERROR` a MAY for
a second update before the first is acknowledged; here it is required, and the
observable is the `Error::KeyUpdate` above. RFC 9218 Section 4.1 leaves a
stream's default urgency implementation-defined; here it is **127**, not the
`3` that section's own guidance suggests -- the ordering groups read that
number, so take it from this document.

`stream_capacity` keeps the semantics it has had since step 01, and the
ordering groups read it: it reports the **minimum** of the connection's send
capacity and the stream's own, so a connection at its connection-level limit
reports 0 through a stream that still has room. `stream_send` obeys the same
minimum.

`Error` gains one variant, `KeyUpdate`, appended after `OutOfIdentifiers`. Its
wire error code is `0xe` (`CRYPTO_ERROR` territory per RFC 9001) and its C
value is **-19**. Every earlier variant keeps the C value step 01-08 gave it;
this step extends the table rather than renumbering it.

`Connection::timeout` and `timeout_instant` account for a key-update timer as
well as the idle and path timers: the previous generation's opening key is
retained for three PTOs after the update and dropped when that timer fires.

### `The HTTP/3 datagram API is removed; the transport one is not`

`quiche::h3::Connection` must **not** have `send_dgram`, `recv_dgram` or
`dgram_max_writable_len`. This is a deletion and it is checked as one: a fixture
crate that calls `h3_conn.send_dgram(...)` has to fail to compile.

Everything at the transport layer that step 04 introduced is still required and
is still checked at this step:

* `Config::enable_dgram(enabled: bool, recv_queue_len: usize, send_queue_len: usize)`
* `Connection::dgram_send(&mut self, buf: &[u8]) -> Result<()>`
* `Connection::dgram_recv(&mut self, buf: &mut [u8]) -> Result<usize>`
* `Connection::dgram_max_writable_len(&self) -> Option<usize>`
* `Connection::dgram_send_queue_len` / `dgram_recv_queue_len`

The two are easy to confuse because the removed HTTP/3 method and the retained
transport method share the name `dgram_max_writable_len`. The one on
`h3::Connection` goes; the one on `Connection` stays.

### `Config::set_initial_congestion_window_packets`

`Config::set_initial_congestion_window_packets(&mut self, packets: usize)`
sets the congestion controller's starting window, counted in packets rather
than bytes. The window in bytes is `packets * max_send_udp_payload_size`, using
whatever `set_max_send_udp_payload_size` was given for that config.

That packet size is also read back off the **connection**, not off the config,
by the group that checks the default. `Connection::max_send_udp_payload_size()
-> usize` has existed since step 05 -- the read-back of
`set_max_send_udp_payload_size`, clamped by the peer's advertised
`max_udp_payload_size` and capped at 16383 -- and the group calls it on the
handshaken connection of each pair and multiplies: the default window must be
exactly ten times *that* value, so the accessor and the window arithmetic have
to agree on the same connection. A tree that carries the packet size only on
`Config`, or that exposes the accessor under another name, fails to compile the
whole test target.

The default is unchanged from every earlier milestone: ten packets, RFC 9002
Section 7.2's recommendation.

The setting applies to a connection created from that config and is visible
immediately after the handshake as `path_stats().next().unwrap().cwnd`, before
any congestion event and before slow start has had anything to grow. Two
configs that differ only in this call therefore produce two connections whose
initial `cwnd` differs by exactly the ratio of the two packet counts.

### `Prioritised stream iteration`

`Connection::stream_readable_next(&mut self) -> Option<u64>` and
`Connection::stream_writable_next(&mut self) -> Option<u64>` return one stream
at a time in priority order. They are not the existing `readable()` /
`writable()` methods under new names, and the difference is not the ordering
alone. `readable()` and `writable()` both stay, and both report the current
membership of the same priority-ordered queues that these two methods drain.

Two properties define them.

**Order.** Streams are compared by the priority a stream was given with
`stream_priority(stream_id, urgency, incremental)`:

1. lower `urgency` first (the default is 127);
2. at equal urgency, a non-incremental stream before an incremental one;
3. among non-incremental streams at equal urgency, lower stream ID first;
4. among incremental streams at equal urgency, the order rotates -- a stream
   that has just been returned goes behind its peers, so repeated draining
   round-robins between them rather than starving all but one.

**Disarming.** A stream returned by `stream_readable_next` is not returned
again until it is re-armed by data arriving on it, when it is re-inserted into
the readable queue; a stream returned by `stream_writable_next` is not returned
again until its send capacity changes. Draining the readable queue and calling
again with no new packets delivered yields `None`. This is the whole point of
the pair: an application can drain them in a loop without tracking which
streams it has already served.

Note what "readable" means here, because it is easy to get backwards. In this
design a stream is *readable* when it is a member of the readable queue, and
serving it through `stream_readable_next` removes it from that queue.
`readable()` reports that same membership. So draining the queue to `None` also
leaves `readable()` empty -- it does not go on listing the drained streams, and
it cannot, because a stream that has been served is not in the queue any more.
The two are one account of the same state, not two independent views, and no
candidate can make them disagree.

`stream_writable_next` additionally returns `None` outright when the
*connection-level* send capacity is zero, because in that state no stream can
be written to regardless of its own capacity.

### `Connection::send_ack_eliciting and send_ack_eliciting_on_path`

`Connection::send_ack_eliciting(&mut self) -> Result<()>` schedules an
ACK-eliciting packet. It does not write one -- it arranges that the next
`send()` produces a packet that the peer must acknowledge, even on a connection
with nothing to say. A quiescent connection on which neither peer has written
returns `Err(Error::Done)` from `send()`; after `send_ack_eliciting()` the same
connection produces a datagram, and the peer that receives it answers.

`Connection::send_ack_eliciting_on_path(&mut self, local: SocketAddr, peer: SocketAddr) -> Result<()>`
is the same request aimed at one path of a multi-path connection. A four-tuple
the connection does not know is `Err(Error::InvalidState)`, the same rejection
`send_on_path` gives for the same mistake at step 08.

Both are idempotent in the sense that matters: calling them twice before
sending does not owe the peer two packets.

### `Connection::source_ids and Connection::retired_scids`

`Connection::source_ids(&self) -> impl Iterator<Item = &ConnectionId>` yields
every connection ID this endpoint has issued and not yet had retired, starting
with the one from the handshake. Immediately after a handshake there is exactly
one; after a successful `new_source_cid` there are two, and the handshake's
comes first.

`Connection::retired_scids(&self) -> usize` counts the source connection IDs
the peer has retired. It is zero after a handshake and rises when the peer
calls `retire_destination_cid` and the retirement reaches this endpoint.

Together they replace `max_active_source_cids`, which is removed.
`active_source_cids()`, `source_cids_left()`, `available_dcids()` and
`retired_scid_next()` are all unchanged from step 08 and all still required.

### `Required to exist, out of scope at this step`

Three additions at this milestone have to be present because a later step's
groups reach for them, and are deliberately out of scope here. They are listed
so that their absence is a contract violation rather than a surprise, and so
that no author reads their silence as permission to skip them.

* `Connection::is_server(&self) -> bool`. Trivially checkable, trivially
  faked, and nothing at this step turns on it.
* `Connection::timeout_instant(&self) -> Option<time::Instant>`, the absolute
  form of `timeout()`. Checking it would mean checking a clock reading.
* `Config::set_max_pacing_rate(&mut self, v: u64)`. Pacing is not observable
  through any surface this package uses, at this release or any other, so the
  setter is required to exist and its effect is never asserted.

### `the workspace binaries`

Unchanged in name from step 08. The section "The binaries" below states the
general rule -- that each name is resolved exactly, name-major, before the
next name is tried anywhere -- and this section supplies the names for this
step; if the two ever appear to disagree, this one governs.

* `quiche-server`, falling back to `server` for a candidate that never
  renamed it -- `--listen <addr>`, `--cert <pem>`, `--key <pem>`, `--root
  <dir>`, `--no-retry`.
* `quiche-client`, falling back to `client` -- a positional URL and
  `--no-verify`. `reg-08-probe-and-migrate`, the gate group inherited from
  step 08, still drives it with `--perform-migration`; no group new at this
  step passes it a flag step 08 did not already use.

Neither `http3-client` nor `http3-server` is resolved at this step: every
group here that starts a process wants the one general-purpose client and
server, not an HTTP/3-specific binary, so those two names from step 01 are
not in play.

## The groups at this step

One row per group. **gate** groups are all-or-nothing: a failure means the step is not satisfied; **requirement** groups cover this step's own new capability. The rows are grouped by tier for reading; the order they run in, and each one's result, is what `reward_details.json` records.

| group | tier | surface | what it observes |
| --- | --- | --- | --- |
| `candidate-workspace-builds` | gate | T4 | The one check every step shares: `cargo build --offline` (default targets -- libraries and binaries only, not tests or benches) must complete for the workspace at `/app` within the harness's build timeout, before any other group in this table runs. It is the one group every step carries identically, and no step declares it as a capability of its own -- but it is not optional either: one failing gate group, including this one, means the step is not satisfied, however much of the new work is right. |
| `cap-drafts-gone` | requirement | T2 | The verifier builds a client Initial itself, for each of draft-27 (`0xff00001b`), draft-28 (`0xff00001c`) and draft-29 (`0xff00001d`), and sends it to the candidate's `server` over UDP loopback. Each one must draw a Version Negotiation packet -- a long header with a zero Version field -- and the version list in it must be exactly one entry, `0x00000001`. The candidate's library is not involved in producing the stimulus, so this is a pure observation of emitted bytes. Three things fail here, each for its own reason: a server that still supports a draft answers with a server Initial or a Retry rather than a VN packet; a server that negotiates but still advertises what it can no longer speak offers a list longer than one; a server that negotiates with the wrong connection IDs fails the echo assertions (the VN packet's DCID is the client's SCID and its SCID is the client's DCID, per RFC 9000 Section 6.1). This is the same shape as step 03's `cap-drafts-removed`, which retired drafts 23, 24 and 25 and asserted a three-entry list. Step 03's group is *not* inherited here, because it is no longer true: at this milestone those three drafts draw a list of one, not a list of 27/28/29, and re-running step 03's assertion unchanged would fail a correct step-09 stack. The capability it tested is subsumed by this group, which covers the same three versions along with the three new ones -- a client offering draft-23 at this milestone gets the same one-entry list as a client offering draft-29. |
| `cap-key-update` | requirement | T2 | The verifier becomes the peer that initiates the key update, because there is no public API for initiating one and the candidate is only required to implement the responder. A `pair::Pair` handshakes with a keylog, which gives the verifier `CLIENT_TRAFFIC_SECRET_0` and `SERVER_TRAFFIC_SECRET_0`; from the client secret it derives the next generation itself -- HKDF-Expand-Label with the label `quic ku`, then `quic key` and `quic iv` -- and builds its own protected 1-RTT packet with the key-phase bit set. Before injecting anything the verifier proves its own keys are right by opening one of the candidate's own packets with them. This is the practice step 06's `cap-ecn-ack-frame` established for this surface: an injection test whose keys are wrong fails identically to one whose candidate is wrong, so the keys are demonstrated first and the failure message distinguishes the two. The packet carries a STREAM frame. Five things are then asserted: * the candidate's server **decrypts it**, and `stream_recv` returns the payload byte-for-byte -- so it derived the same next generation, which means it used `quic ku` on the traffic secret and not something else; * the candidate reuses its **header-protection key** rather than re-deriving it, which is implied by the above succeeding at all: the verifier applied header protection with the generation-0 key, so a candidate that rotated it cannot even find the packet number; * the candidate's **own next packet has the key-phase bit set**, read back off the wire by the verifier after removing header protection -- it flipped its own phase and is now sealing under the new generation; * a **second update before the first is acknowledged** is `Err(Error::KeyUpdate)`, which is what stops an implementation from treating every phase mismatch as a fresh derivation; * the **previous generation's opening key is retained** across reordering: on a second, fresh `pair::Pair`, a packet sealed with the old key phase and numbered below the one that triggered the update still decrypts, which is what lets packets that outrace the update on the wire arrive intact instead of being dropped as undecryptable. The candidate's replies are pulled off the connection and read by the verifier rather than fed back into the pair. The injected packet number is the verifier's fiction, so an ACK covering it is a statement about the verifier and not about the candidate. |
| `cap-h3-datagram-removed` | requirement | T4 | A removal, checked as the compile failure it is rather than as the absence of an identifier in the candidate's source. The group writes two small fixture crates into a scratch directory, both depending on the candidate's crate by path, and builds each with `cargo build --offline`: * the **subject** calls `h3::Connection::send_dgram`, `h3::Connection::recv_dgram` and `h3::Connection::dgram_max_writable_len`. It must fail to build, and the compiler's output must name `send_dgram` -- so a crate that fails for an unrelated reason, or a candidate whose crate does not build at all, does not read as a pass. * the **control** is the same crate with those three calls removed and the transport-level `dgram_send` / `dgram_recv` / `Connection::dgram_max_writable_len` calls left in place. It must build. This is what stops the group from passing on a candidate that deleted the transport datagram API too, or one whose `h3` module does not exist. Both fixtures build against the candidate's own crate, offline, into a target directory of their own -- so each one does compile the crate's dependency graph afresh, and each is bounded by the harness's per-case deadline (60 seconds here). That is the cost of proving a *deletion* by compiling rather than by reading source: the subject has to be compiled to show it does not, and the control has to be compiled to show the failure is about the deleted names and nothing else. The graph is small and the registry cache is local, so the deadline is generous rather than tight; a machine that needs longer than the deadline for one of these two builds would report it as a timeout, not as the group's own failure. The pair is the whole group: the subject alone would be satisfied by any broken crate, and the control alone proves nothing about the deletion. Together they say the HTTP/3 wrapper is gone and the transport frame it wrapped is not. |
| `cap-initial-cwnd-configurable` | requirement | T3 | Three `pair::Pair`s, identical except for one call. The first leaves the initial window at its default and the group reads `path_stats().next().unwrap().cwnd` immediately after the handshake. The second calls `set_initial_congestion_window_packets(20)` and the third calls it with `5`. The assertions are relative, not absolute, because the byte value depends on `max_send_udp_payload_size` and a candidate is free to have a different one: the 20-packet window is twice the default and the 5-packet window is half of it, exactly. The default is additionally asserted to be the ten packets RFC 9002 Section 7.2 recommends, and asserted twice over: as a ratio against the 20-packet reading (the default is exactly half of it) and as a byte count derived from `Connection::max_send_udp_payload_size()` -- called on the same handshaken connection the window was read from -- times ten. A candidate whose default window is right but whose packet-size handling is not fails the second comparison. A candidate that accepts the call and ignores it produces three equal readings and fails. A candidate that treats the argument as bytes produces a 20-byte window, which is smaller than the default rather than twice it, and fails in a way the failure message names. |
| `cap-stream-next-prioritised` | requirement | T3 | One `pair::Pair`. The client opens five streams and gives them priorities with `stream_priority` before writing to any of them: two non-incremental streams at urgency 1, two non-incremental at urgency 3, and one incremental at urgency 1. All five carry data, and the group then drains the server's `stream_readable_next` and records the order. The expected order is fully determined by the documented rule and is checked as a sequence: both urgency-1 non-incremental streams first, in ascending stream ID; then the urgency-1 incremental stream; then the two urgency-3 non-incremental streams in ascending ID. Three of the four ordering clauses are therefore load-bearing here, and each has a failure that names it -- a candidate that ignores urgency, one that ignores the incremental class, and one that does not fall back to stream ID each produce a different wrong sequence. The fourth clause, rotation among equal-urgency incremental streams, is deliberately not exercised by this fixture (see below). Then the disarming property, which is the reason the method exists. After the queue has been drained, the group calls `stream_readable_next` again with no new packets delivered and requires `None`. An implementation that aliases the new method onto `readable()` -- draining it by re-reading the queue instead of removing the served stream from it -- keeps returning a stream and never reaches `None`, so it fails here. The group tests `None` and nothing else on this half: it does not require `readable()` to still list the five drained streams, because in this design a served stream has left the readable queue and `readable()` correctly reports the queue as empty alongside the drain. `stream_writable_next` is checked on the same pair for the one property that is not a restatement of the above: with the connection's send capacity exhausted it returns `None` even though individual streams still have their own capacity. All five streams are given explicit priorities rather than left at their defaults, deliberately. The default is incremental at urgency 127, and equal-urgency incremental streams rotate, so a group built on default-priority streams would be asserting a rotation offset rather than an ordering rule. The fourth clause -- round-robin rotation among equal-urgency incremental streams, RFC 9218 Section 4.2 -- is then checked on a second, fresh `pair::Pair`, kept separate from the fixture above because one of its streams is popped from the readable queue but never consumed via `stream_recv`, which would leave it in an ambiguous readable state for a second round on the same connection. Two new streams, both incremental at urgency 1, are written in one order, drained (consuming each via `stream_recv` this time), then written again in the reverse order. With no prior history the first drain serves them in arrival order; the second drain is checked to be the exact reverse, which is what distinguishes rotation from both "replay the same order every time" (ascending ID, or a fixed insertion slot) and "always serve the most recently written stream" (the second round would then coincidentally also read reversed, but the same code would fail a third round that reverses again). |
| `cap-ack-eliciting-on-demand` | requirement | T3 | A `pair::Pair` is driven to quiescence -- handshake complete, every pending packet exchanged -- and the group first establishes the baseline that matters: `send()` on the client returns `Err(Error::Done)`, so there is nothing the connection wants to say on its own. `send_ack_eliciting()` is then called and `send()` must produce a datagram. That datagram is delivered to the server, and the server must answer it, which is the part that makes the group about an ACK-eliciting packet rather than about any packet at all: a datagram carrying only an ACK does not itself draw a reply, so a candidate that satisfies the request with a bare ACK fails here. `send_ack_eliciting_on_path` is then checked twice on the same connection: with the connection's own four-tuple it returns `Ok`, and with a four-tuple the connection has never seen it returns `Err(Error::InvalidState)` -- the same rejection `send_on_path` gives for the same mistake, which is the consistency a candidate is most likely to miss by accepting any address pair and discovering the problem later. |
| `cap-source-ids-and-retired` | requirement | T3 | A `pair::Pair` handshakes with `set_active_connection_id_limit(3)` on both sides. Immediately afterwards `source_ids()` yields exactly one connection ID on each peer -- the handshake's -- and `retired_scids()` is zero on both. The client then issues a connection ID with `new_source_cid` and the group requires `source_ids()` to yield two, with the handshake's connection ID **first** and the newly issued one second. The order is asserted, not just the count, because the accessor's contract is that it starts from the handshake ID and an implementation that returns a set in hash order satisfies the count and not the contract. The peers then exchange packets so the server learns of the new ID, the server retires the client's original destination connection ID with `retire_destination_cid(0)`, and the exchange continues until the retirement reaches the client. `retired_scids()` on the client is then 1, and `source_ids()` yields one ID again -- the retired one is gone from it, which is what distinguishes the accessor from a log of everything ever issued. `active_source_cids()` and `source_cids_left()` are read alongside the two new accessors and asserted to agree with them, so a candidate that added the new pair as unrelated counters rather than views of the same state fails. That agreement check exercises the two accessors but does not by itself show `max_active_source_cids` is gone -- a candidate that kept the old method unused alongside the new pair would pass every assertion above. The group therefore closes with a compile-fail probe, the same pattern `cap-h3-datagram-removed` uses for its own removed methods: a throwaway crate that references `Connection::max_active_source_cids` as a function item must fail to build, and the compiler output must name `max_active_source_cids` specifically, so an unrelated build failure cannot satisfy the check by accident. No separate control probe is needed here, because the in-body calls to `active_source_cids()` and `source_cids_left()` on a live connection already double as the positive control. |
| `reg-01-flow-control` | gate | T3 | Inherited from step 01, with the same claims: a 64 KiB transfer through a 4096-byte stream window completes byte-identically, and because a window that small has to be reopened before the next chunk can be written, it takes at least sixteen rounds of read-and-reopen to get there -- which the group counts. This is the inherited group drawn from a step whose surface this step rewrites. Step 09 replaces how readable and writable streams are reported: the sets `readable()` and `writable()` return are now views over a priority-ordered structure that `stream_readable_next` and `stream_writable_next` mutate. A stream's readiness and its flow-control state are now bookkept together, so this is exactly the inherited observation most likely to break, and to break quietly -- a transfer that stalls looks like a slow transfer until the round count is checked. |
| `reg-04-datagram-roundtrip` | gate | T3 | Inherited from step 04: both peers enable datagrams with `enable_dgram(true, 10, 10)`, a 256-byte payload is sent with `dgram_send`, received with `dgram_recv` and echoed back byte-for-byte. It is here for a reason specific to this step rather than as routine carry: the HTTP/3 datagram API is deleted at this milestone and this is the transport API underneath it, which is not. A candidate that reads "datagrams are removed" and deletes both layers passes `cap-h3-datagram-removed`'s subject fixture and fails here and on that group's control fixture. The two groups are drawn around the same boundary from opposite sides on purpose. |
| `reg-08-probe-and-migrate` | gate | T1 | Inherited from step 08: the candidate's own `client`, run with `--perform-migration` against the candidate's `server`, retrieves a 512 KiB pseudorandom file byte-identically and exits zero. `--source-port` is left at its default so the second bind takes a different ephemeral port. This is the required end-to-end inherited group. It is drawn from step 08 in particular because a key update is a change to packet protection and migration is the one inherited capability that moves packets between protection contexts mid-connection while the connection stays up. If the responder's key handling has an ordering bug involving paths -- keeping the previous opening key on the wrong path, say -- a single-path transfer will not find it and this will. No socket observation, and that is a deliberate narrowing from step 08's own group. Counting the client's bound UDP ports needs a sampler -- the harness polls `/proc/<pid>/fd` against `/proc/net/udp` roughly every 20 ms -- and whether any poll lands while both sockets are open is a question about the host's scheduler rather than about the candidate. `cap-probe-and-migrate` at step 08 still asks it, where a miss means that group fails; a gate case that can miss means the step is not satisfied for every candidate, so the gate asks only what is deterministic: the flag is accepted, the connection survives it, the bytes arrive. |
| `gate-endtoend` | gate | T1 | The candidate's `server` starts, the candidate's `client` completes a handshake against it over UDP loopback and retrieves a small file, and both exit zero. No migration, no key update, no flags: if this fails, every other observation at this step is noise. |

Surfaces: **T1** your client and server as processes over UDP
loopback; **T2** a QUIC endpoint the checker implements itself, reading
and writing real packets; **T3** your library linked into the checker's
own crate, public API only; **T4** a file or layout your code produces;
**T5** an input with exactly one correct answer.

## How this step is checked

The **gate** covers proof of life plus the milestones you already have: your
workspace builds, your binaries complete one end-to-end exchange, and the
earlier capabilities this step disturbs still work. Gate groups add no new
requirement of their own, but a failure in one means the step is not
satisfied, however much of the new work is right.

The **requirement** groups cover this step's own new capability, and every one
of them is in scope. Your step's interface document lists every group with its
tier.

Every case within a group must pass, and there is no separate requirement for
style or for extra work.

Three families of group, named in the report so you can tell them apart:

| prefix | tier | what it checks |
|---|---|---|
| `cap-*` | requirement | the capability this step introduces |
| `reg-NN-*` | gate | a capability step NN introduced, still working |
| `gate-endtoend` | gate | one end-to-end exchange through your own binaries |
| `candidate-workspace-builds` | gate | your workspace builds at all, before any other group runs |

`reg-NN-*` groups are not decoration: every step in this task rewrites
something an earlier step built, and the `reg-` groups are chosen from the
surfaces this step disturbs. (A step with no earlier milestone carries none of
them: this section describes the family, and the group table above is what
says whether a given step has any.) A rewrite that quietly drops an older
behaviour does not fail just one group; it means the step is not satisfied.

The corollary is worth stating plainly: a do-nothing continuation satisfies
none of the new work. A workspace carried forward untouched passes every gate
group — it has not broken anything — and passes no requirement group, because
it implements none of this step's capability.

### What the checker is allowed to look at

Behaviour, and only behaviour:

* your library, linked into a test harness that lives outside `/app` and calls
  your public API;
* your client and server binaries -- whichever names this step's own interface
  section gives them -- run as real processes talking to each other over UDP
  loopback;
* your binaries talking to a QUIC endpoint the checker implements itself, which
  derives its own keys and reads the bytes on the wire;
* files your code emits — qlog, netlog, a generated C header, the workspace
  layout;
* exact answers to inputs designed to have exactly one right answer.

It never **checks** anything by reading your source: no group greps your tree
for an identifier, counts your modules, or checks a log line. One qualification
belongs beside that: anything the checker's own crate *imports or names* has to
exist under that name, because it is a Rust program that compiles against your
crate. Those names are this step's interface section, and where a group builds
a probe of its own, the names that probe imports are written down there too.
Inside your own modules, name things whatever you like. Two plumbing
steps outside the checked path do read source text -- the image picks which
pinned BoringSSL to link by looking at `src/tls.rs`, and where this
milestone's release changed an API's shape the harness picks which dialect of
its own test shim to compile by looking for the new spelling in `src/`. Both
read live code only -- `//` comments are stripped before either check, and the
dialect probe, where this step has one, drops `/* ... */` blocks as well -- and
both key on spellings this step's own documents already tell you to change, so a
tree you have converted is never misread.
Name things whatever you like, put them wherever the manifest says, and write
in whatever style you prefer.

### Reading the report

`reward_details.json` names every group, its tier, whether it
passed, and for a failing group the first line of why. When the gate fails it
says which gate group failed and what it saw — a step reported as unsatisfied
without naming the group responsible would be indistinguishable from a
candidate that did nothing. When a group fails for a reason that is not about
your code — a build that did not finish inside its budget, a harness that
failed to link — it is reported as such rather than silently treated as a
failure.

## The build environment

Written once and generated into every step's copy of this document, so that the
fifteen copies cannot drift apart. Anything in this section that is *not* true
of your step is called out in the step's own section above, never by silently
contradicting it here.

### Toolchain

Rust **1.88** (`rustc 1.88`, `cargo 1.88`), Debian bookworm, x86-64. Also
installed: `clang`, `cmake`, `ninja`, `perl`, `go`, `pkg-config`, `git`,
`python3`, `jq`, `ripgrep`, `xxd`, `iproute2`, `libev`, `uuid-dev`.

### BoringSSL is prebuilt; your build script must link it, not build it

Your workspace does **not** contain BoringSSL and must not try to fetch or
compile it. A prebuilt static BoringSSL is installed in the image, and the
environment variable **`QUICHE_BSSL_PATH`** points at it:

```
$QUICHE_BSSL_PATH/
├── build/
│   ├── libcrypto.a
│   ├── libssl.a
│   ├── crypto/libcrypto.a     (hard link, for the older expected layout)
│   └── ssl/libssl.a           (hard link)
├── libcrypto.a                (copy, for the newest expected layout)
├── libssl.a
├── include/openssl/...
└── COMMIT                     the BoringSSL commit this was built from
```

All three layouts are populated, so a build script that looks in any of them
finds the archives. The archives are built with
`-DCMAKE_POSITION_INDEPENDENT_CODE=ON`, so they link into a `cdylib`.

`QUICHE_BSSL_PATH` is set in the environment in the checker image, and in the
implementation container your step's `.cargo/config.toml` is what sets it —
keep that file, or a build script that reads the variable has nothing to read
and nothing to link. Read it in your build script and emit the
`cargo:rustc-link-search` / `cargo:rustc-link-lib` lines yourself — nothing
does that for you.

The prebuilt BoringSSL that `QUICHE_BSSL_PATH` points at is the one your step
is built against. The measurement runs offline, so it is also the only one available:
you never need to know its commit hash, only to link against that path.

### The measurement runs offline

At measurement time the container has **no network**. `CARGO_NET_OFFLINE=true` is
set and the registry cache holds the crates this milestone's build can reach:
every crate in the `Cargo.lock` that ships in this workdir, the versions this
milestone's own manifests resolve to, the previous release's graph, and the
checker's own harness crate's graph. Nothing else is available. Consequences:

* **Keep `Cargo.lock`.** Do not delete it and do not run `cargo update`. It is
  the contract between your manifest and the offline cache, and a version it
  pins is reachable offline even where the requirement alone would not reach
  it.
* **A dependency you add that is outside that set will not resolve**, here or
  at measurement time, and the build failure is reported as a build failure. If you
  need something, check `Cargo.lock` first.
* **Your resolution is pinned to this milestone's set, here as well as at
  measurement time.** One image serves all fifteen milestones and its registry
  cache is the union of every milestone's crates, so the set above is not what
  the cache holds -- it is what you are allowed to resolve. The step's
  `setup.sh` points cargo's `crates-io` at a single directory source built
  from this milestone's own locks, before your first command runs. A
  dependency outside it therefore fails here too, with the checker's own
  message:

      no matching package named `NAME` found
      location searched: directory source `/opt/vendor/NN`

  A local build that resolves is a build that works here as well. Do not delete or edit
  `$CARGO_HOME/config.toml`; that file is the pin.
* Cargo resolves the *whole* workspace graph, dev-dependencies included, before
  it compiles anything — so even a dev-dependency you invent breaks the build.
* `CARGO_TARGET_DIR` is set to a shared directory whose dependency graph is
  already compiled. Your build pays for your code only.

### What the checker builds and runs

```
cd /app && cargo build --offline          # libraries and binaries
cd /app && cargo build --offline --examples   # best effort; never fatal
cd /app/<app package> && cargo build --offline   # best effort; never fatal
```

Your own tests are **not** compiled by the checker, and a broken test of yours
costs you nothing. Correspondingly, passing your own tests is not required:
the checker links your crate from its own throw-away harness crate outside
`/app`, and runs your binaries as processes.

### Where your crate has to be

The checker locates the transport crate by looking for a `Cargo.toml` whose
package `name` is `quiche`, first at `/app/quiche` and then at `/app`, and uses
the first of the two that exists. Every group that merely needs *the transport
crate* -- to build it, to read its manifest, to look inside its `src/` or its
`include/` -- goes through that search, so either arrangement satisfies all of
them.

The exceptions are the groups for which the layout is the thing being checked,
and there are two kinds. The step that *introduces* the workspace conversion
checks the move itself, against the paths that conversion is defined to
produce. From that step onward, a group that checks **workspace membership**
names the member directories it expects -- `quiche` among them -- because which
crates are members, and where they sit, is that milestone's subject matter
rather than an incidental path. A group of either kind says so in its own
interface section. Outside those groups the layout is never checked.

### The binaries

Several groups run your client and server as real processes over UDP loopback.

**This step's own interface section names the exact binaries it expects, and
that section governs.** Build them with a plain `cargo build` (or let the
best-effort `--examples` pass pick them up). Where they live does not matter:
`examples/`, `tools/apps/` or their own package are all fine. The checker
searches these directories, in this order, for each name before it moves to the
next name:

```
$CARGO_TARGET_DIR/debug/
$CARGO_TARGET_DIR/debug/examples/
$CARGO_TARGET_DIR/release/
$CARGO_TARGET_DIR/release/examples/
```

and then, in case a candidate ignored `CARGO_TARGET_DIR` or a workspace member
carries a target directory of its own, the same four in the same order under
`<workspace root>/target/`, `<workspace root>/quiche/target/` and
`<workspace root>/apps/target/`.

What *does* matter is the name, and it is not a free choice. Each binary the
checker needs is resolved from a short list of accepted names -- this step's own
interface section gives them, most-preferred first -- and the search is
name-major: the first name on that list is looked for across every directory
before the second is tried anywhere. A binary that is correct but named
something outside the list is invisible to the group that needs it, and the
group fails with no diagnostic.

Two consequences worth stating plainly, because both have bitten:

* A single release can ship more than one client, and they are not
  interchangeable — an HTTP/3 group needs the HTTP/3-capable binary
  specifically. Naming a general-purpose client something an HTTP/3 group
  resolves would hand that group a client that cannot speak HTTP/3.
* Upstream renamed these binaries across the fifteen releases. Do not carry a
  name forward from an earlier step because it worked there; read this step's
  interface section.

## Where this step starts

`/app` contains everything step 08 left behind, unchanged. The milestone before this one is modelled on quiche 0.16.0; this one is modelled on 0.18.0. Read `WORKSPACE_HANDOFF.md` for what is already there and what must keep working.

## Out of scope

Stated so you do not spend the budget on them.

* **Interoperability with other QUIC implementations.** No second stack is
  installed and there is no network at measurement time.
* **Performance.** Throughput, CPU, allocation counts and congestion-control
  *efficiency* are never measured. Congestion controllers are checked on
  observable state machine behaviour, not on how fast they go.
* **Anything under `fuzz/`**, and your own test suite: the checker does not
  compile either.
* **Documentation, comments, formatting, clippy.**
* **Platform support other than x86-64 Linux**, and every Windows, macOS, iOS
  or Android code path.
* **The `boringssl-boring-crate` feature** at the releases where it is
  optional. Its crate version is yanked from crates.io and cannot be resolved
  offline; the default feature set is what gets built.

