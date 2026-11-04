# Step 9 — Draft removal, key update, and prioritised stream iteration

## 1. Implement this milestone

Stop being a draft implementation. Since step 05 the stack has spoken both
RFC 9000 version 1 and draft-29, and every packet-handling path has had to
ask which of the two it was looking at. This milestone deletes drafts 27, 28
and 29 outright: `version_is_supported` recognises exactly one version, the
Initial salt is no longer a function of the version, and a client that
offers a draft gets a Version Negotiation packet listing only version 1.

With one version to protect, the connection can finally do the thing RFC
9001 requires of a long-lived one: rotate its own keys. A peer signals a key
update by flipping the key-phase bit in the short header and encrypting with
the next generation of application keys; the receiver derives that
generation, decrypts, flips its own bit, and keeps a copy of the old key
around for reordered packets. This step implements the receiving half of
that exchange, including the rule that a peer may not update twice without
the first update being acknowledged.

Three smaller capabilities arrive with it and all three are about giving the
application control it did not have. Streams gain prioritised iteration --
`stream_readable_next` and `stream_writable_next` return one stream at a
time in priority order and disarm it until it changes again, rather than
handing back the same unordered set on every call. The congestion
controller's starting window becomes configurable. And the application can
ask the connection to send an ACK-eliciting packet on demand, on a
particular path, instead of waiting for data to write.

The HTTP/3 datagram API added at step 05 is deleted. The transport-level
DATAGRAM frame API from step 04 stays exactly where it is; it is the HTTP/3
wrapper above it that goes.

Three things stop compiling, and all three are deletions.

**The HTTP/3 datagram API is gone.** `quiche::h3::Connection::send_dgram`,
`recv_dgram` and `dgram_max_writable_len` are removed. A step 05-08 solution
that calls any of them stops compiling and there is no renamed replacement
to reach for -- the capability is withdrawn at this milestone, not moved.

Be precise about the scope. The **transport** datagram API is untouched and
this step still requires all of it: `Config::enable_dgram`,
`Connection::dgram_send`, `dgram_recv`, `dgram_max_writable_len`,
`dgram_send_queue_len` and the rest keep working exactly as step 04 left
them. What disappears is the HTTP/3 layer's own flow-id-prefixed wrapper
over them. An implementation that deletes both does not satisfy this step's
inherited datagram group; an implementation that deletes neither does not
satisfy the removal group.

**`Connection::max_active_source_cids` is gone**, replaced by two accessors
that answer more useful questions: `source_ids()` iterates the connection
IDs this endpoint has issued and `retired_scids()` counts the ones the peer
has retired. `active_source_cids()` and `source_cids_left()` are unchanged.

**`Connection::stream_init_application_data` and
`Connection::stream_application_data` are gone.** The per-stream
application-data slot they managed does not survive the priority rework that
brings in `stream_readable_next`; nothing replaces it, and a solution that
used it has to keep its own map keyed by stream ID.

Nothing in the hot path changes shape. `connect`, `accept`, `send`, `recv`,
`send_on_path`, `SendInfo` and `RecvInfo` are all exactly as step 08 left
them, and every step 01-08 group that does not touch a deleted symbol must
still hold untouched.

What is newly in scope here:

* `cap-drafts-gone` — A client Initial is built independently, for
  each of draft-27 (`0xff00001b`), draft-28 (`0xff00001c`) and draft-29
  (`0xff00001d`), and sent to the candidate's `server` over UDP
  loopback. Each one must draw a Version Negotiation packet -- a long header
  with a zero Version field -- and the version list in it must be exactly
  one entry, `0x00000001`.

  The candidate's library is not involved in producing the stimulus, so this
  is a pure observation of emitted bytes. Three things fail here, each for
  its own reason: a server that still supports a draft answers with a server
  Initial or a Retry rather than a VN packet; a server that negotiates but
  still advertises what it can no longer speak offers a list longer than
  one; a server that negotiates with the wrong connection IDs fails the echo
  assertions (the VN packet's DCID is the client's SCID and its SCID is the
  client's DCID, per RFC 9000 Section 6.1).

  This is the same shape as step 03's `cap-drafts-removed`, which retired
  drafts 23, 24 and 25 and asserted a three-entry list. Step 03's group is
  *not* inherited here, because it is no longer true: at this milestone
  those three drafts draw a list of one, not a list of 27/28/29, and
  re-running step 03's assertion unchanged would fail a correct step-09
  stack. The capability it tested is subsumed by this group, which covers
  the same three versions along with the three new ones -- a client offering
  draft-23 at this milestone gets the same one-entry list as a client
  offering draft-29.
* `cap-key-update` — The key update is initiated by an independent peer,
  because there is no public API for initiating one and the
  candidate is only required to implement the responder. A `pair::Pair`
  handshakes with a keylog, which exposes
  `CLIENT_TRAFFIC_SECRET_0` and `SERVER_TRAFFIC_SECRET_0`; from the client
  secret the next generation is derived independently -- HKDF-Expand-Label
  with the label `quic ku`, then `quic key` and `quic iv` -- and used to
  build a separately protected 1-RTT packet with the key-phase bit set.

  Before anything is injected, those derived keys are proven right by
  opening one of the candidate's own packets with them. This is the practice
  step 06's `cap-ecn-ack-frame` established for this surface: an injection
  test whose keys are wrong fails identically to one whose candidate is
  wrong, so the keys are demonstrated first and the failure message
  distinguishes the two.

  The packet carries a STREAM frame. Five things are then asserted:

  * the candidate's server **decrypts it**, and `stream_recv` returns the
  payload byte-for-byte -- so it derived the same next generation, which
  means it used `quic ku` on the traffic secret and not something else; *
  the candidate reuses its **header-protection key** rather than re-deriving
  it, which is implied by the above succeeding at all: the injected packet's
  header protection used the generation-0 key, so a candidate that rotated
  it cannot even find the packet number; * the candidate's **own next packet
  has the key-phase bit set**, read back off the wire after
  removing header protection -- it flipped its own phase and is now sealing
  under the new generation; * a **second update before the first is
  acknowledged** is `Err(Error::KeyUpdate)`, which is what stops an
  implementation from treating every phase mismatch as a fresh derivation; *
  the **previous generation's opening key is retained** across reordering:
  on a second, fresh `pair::Pair`, a packet sealed with the old key phase
  and numbered below the one that triggered the update still decrypts, which
  is what lets packets that outrace the update on the wire arrive intact
  instead of being dropped as undecryptable.

  The candidate's replies are pulled off the connection and read directly
  rather than fed back into the pair. The injected packet number is a
  fiction of the independent peer, so an ACK covering it is a statement about
  that peer and not about the candidate.
* `cap-h3-datagram-removed` — A removal, checked as the compile failure it
  is rather than as the absence of an identifier in the candidate's source.
  The group writes two small fixture crates into a scratch directory, both
  depending on the candidate's crate by path, and builds each with `cargo
  build --offline`:

  * the **subject** calls `h3::Connection::send_dgram`,
  `h3::Connection::recv_dgram` and `h3::Connection::dgram_max_writable_len`.
  It must fail to build, and the compiler's output must name `send_dgram` --
  so a crate that fails for an unrelated reason, or a candidate whose crate
  does not build at all, does not read as a pass. * the **control** is the
  same crate with those three calls removed and the transport-level
  `dgram_send` / `dgram_recv` / `Connection::dgram_max_writable_len` calls
  left in place. It must build. This is what stops the group from passing on
  a candidate that deleted the transport datagram API too, or one whose `h3`
  module does not exist.

  Both fixtures build against the candidate's own crate, offline, into a
  target directory of their own. Each one therefore does compile the crate's
  dependency graph afresh, within the harness's per-case deadline; that is the
  cost of proving a deletion by compiling rather than by reading source.

  The pair is the whole group: the subject alone would be satisfied by any
  broken crate, and the control alone proves nothing about the deletion.
  Together they say the HTTP/3 wrapper is gone and the transport frame it
  wrapped is not.
* `cap-initial-cwnd-configurable` — Three `pair::Pair`s, identical except
  for one call. The first leaves the initial window at its default and the
  group reads `path_stats().next().unwrap().cwnd` immediately after the
  handshake. The second calls `set_initial_congestion_window_packets(20)`
  and the third calls it with `5`.

  The assertions are relative, not absolute, because the byte value depends
  on `max_send_udp_payload_size` and a candidate is free to have a different
  one: the 20-packet window is twice the default and the 5-packet window is
  half of it, exactly. The default is additionally asserted to be the ten
  packets RFC 9002 Section 7.2 recommends, twice over: as a ratio against
  the 20-packet reading, and as a byte count — ten times
  `Connection::max_send_udp_payload_size()`, the accessor the group calls on
  that same handshaken connection (it has existed since step 05). A stack
  whose window arithmetic and whose accessor disagree fails the second
  reading.

  A candidate that accepts the call and ignores it produces three equal
  readings and fails. A candidate that treats the argument as bytes produces
  a 20-byte window, which is smaller than the default rather than twice it,
  and fails in a way the failure message names.
* `cap-stream-next-prioritised` — One `pair::Pair`. The client opens five
  streams and gives them priorities with `stream_priority` before writing to
  any of them: two non-incremental streams at urgency 1, two non-incremental
  at urgency 3, and one incremental at urgency 1. All five carry data, and
  the group then drains the server's `stream_readable_next` and records the
  order.

  The expected order is fully determined by the documented rule and is
  checked as a sequence: both urgency-1 non-incremental streams first, in
  ascending stream ID; then the urgency-1 incremental stream; then the two
  urgency-3 non-incremental streams in ascending ID. Three of the four
  ordering clauses are therefore load-bearing here, and each has a failure
  that names it -- a candidate that ignores urgency, one that ignores the
  incremental class, and one that does not fall back to stream ID each
  produce a different wrong sequence. The fourth clause, rotation among
  equal-urgency incremental streams, is deliberately not exercised by this
  fixture (see below).

  Then the disarming property, which is the reason the method exists. After
  the queue has been drained, the group calls `stream_readable_next` again
  with no new packets delivered and requires `None`. An implementation that
  aliases the new method onto `readable()` -- draining it by re-reading the
  queue instead of removing the served stream from it -- keeps returning a
  stream and never reaches `None`, so it fails here. The group tests `None`
  and nothing else on this half: it does not require `readable()` to still
  list the five drained streams, because in this design a served stream has
  left the readable queue and `readable()` correctly reports the queue as
  empty alongside the drain.

  `stream_writable_next` is checked on the same pair for the one property
  that is not a restatement of the above: with the connection's send
  capacity exhausted it returns `None` even though individual streams still
  have their own capacity.

  All five streams are given explicit priorities rather than left at their
  defaults, deliberately. The default is incremental at urgency 127, and
  equal-urgency incremental streams rotate, so a group built on
  default-priority streams would be asserting a rotation offset rather than
  an ordering rule.

  The fourth clause -- round-robin rotation among equal-urgency incremental
  streams, RFC 9218 Section 4.2 -- is then checked on a second, fresh
  `pair::Pair`, kept separate from the fixture above because one of its
  streams is popped from the readable queue but never consumed via
  `stream_recv`, which would leave it in an ambiguous readable state for a
  second round on the same connection. Two new streams, both incremental at
  urgency 1, are written in one order, drained (consuming each via
  `stream_recv` this time), then written again in the reverse order. With no
  prior history the first drain serves them in arrival order; the second
  drain is checked to be the exact reverse, which is what distinguishes
  rotation from both "replay the same order every time" (ascending ID, or a
  fixed insertion slot) and "always serve the most recently written stream"
  (the second round would then coincidentally also read reversed, but the
  same code would fail a third round that reverses again).
* `cap-ack-eliciting-on-demand` — A `pair::Pair` is driven to quiescence
  -- handshake complete, every pending packet exchanged -- and the group
  first establishes the baseline that matters: `send()` on the client
  returns `Err(Error::Done)`, so there is nothing the connection wants to
  say on its own.

  `send_ack_eliciting()` is then called and `send()` must produce a
  datagram. That datagram is delivered to the server, and the server must
  answer it, which is the part that makes the group about an ACK-eliciting
  packet rather than about any packet at all: a datagram carrying only an
  ACK does not itself draw a reply, so a candidate that satisfies the
  request with a bare ACK fails here.

  `send_ack_eliciting_on_path` is then checked twice on the same connection:
  with the connection's own four-tuple it returns `Ok`, and with a
  four-tuple the connection has never seen it returns
  `Err(Error::InvalidState)` -- the same rejection `send_on_path` gives for
  the same mistake, which is the consistency a candidate is most likely to
  miss by accepting any address pair and discovering the problem later.
* `cap-source-ids-and-retired` — A `pair::Pair` handshakes with
  `set_active_connection_id_limit(3)` on both sides. Immediately afterwards
  `source_ids()` yields exactly one connection ID on each peer -- the
  handshake's -- and `retired_scids()` is zero on both.

  The client then issues a connection ID with `new_source_cid` and the group
  requires `source_ids()` to yield two, with the handshake's connection ID
  **first** and the newly issued one second. The order is asserted, not just
  the count, because the accessor's contract is that it starts from the
  handshake ID and an implementation that returns a set in hash order
  satisfies the count and not the contract.

  The peers then exchange packets so the server learns of the new ID, the
  server retires the client's original destination connection ID with
  `retire_destination_cid(0)`, and the exchange continues until the
  retirement reaches the client. `retired_scids()` on the client is then 1,
  and `source_ids()` yields one ID again -- the retired one is gone from it,
  which is what distinguishes the accessor from a log of everything ever
  issued.

  `active_source_cids()` and `source_cids_left()` are read alongside the two
  new accessors and asserted to agree with them, so a candidate that added
  the new pair as unrelated counters rather than views of the same state
  fails. That agreement check exercises the two accessors but does not by
  itself show `max_active_source_cids` is gone -- a candidate that kept the
  old method unused alongside the new pair would pass every assertion above.
  The group therefore closes with a compile-fail probe, the same pattern
  `cap-h3-datagram-removed` uses for its own removed methods: a throwaway
  crate that references `Connection::max_active_source_cids` as a function
  item must fail to build, and the compiler output must name
  `max_active_source_cids` specifically, so an unrelated build failure
  cannot satisfy the check by accident. No separate control probe is needed
  here, because the in-body calls to `active_source_cids()` and
  `source_cids_left()` on a live connection already double as the positive
  control.

`gate-endtoend` carries no capability of its own: it drives this step's
binaries end to end over loopback, and every gate group must pass. A stack
that implements every capability listed above and cannot complete one real
connection does not satisfy this step at all, however much of the new
capability it holds. The group table in `TEST_INTERFACE.md` is the contract
for it, as it is for the rest.

What must still hold: everything step 08 left working. 3 of this step's
groups re-run earlier milestones unchanged, listed by name in
`/app/WORKSPACE_HANDOFF.md`. They are gate groups as well, so any one of them
failing means the step is not satisfied. They are there because this step's
change plausibly breaks them: a rewrite that gets the new behaviour right
and drops an old one does not satisfy the step at all.

## 2. Read these local references

**Normative semantics:**

* `/app/spec/rfc9001.txt` — 5.1 (packet protection keys), 5.4 (header
  protection), 6 (key update), 6.1 (initiating a key update), 6.2
  (responding to a key update), 6.3 (timing of receive key generation).
  Section 6 is this step's headline capability, and the three sub-sections
  are where its rules actually live: that the key-phase bit selects the
  generation, that the header-protection key is **not** updated (5.4 and 6
  together), and that a peer may not update again until its first update is
  acknowledged. Section 5.1's `"quic ku"` label is the derivation the
  responder has to reproduce exactly.
* `/app/spec/rfc9000.txt` — 6 (version negotiation), 6.1 (sending version
  negotiation packets), 17.2.1 (version negotiation packet), 5.1.1 (issuing
  connection IDs). The drafts are gone, so version negotiation stops being a
  corner and becomes the path every non-version-1 client takes. 17.2.1 is
  the packet layout and 6.1 is the connection-ID echo rule, both of which
  the removal group asserts byte-wise. 5.1.1 is the background for
  `source_ids` and `retired_scids`.
* `/app/spec/rfc9002.txt` — 7.2 (initial and minimum congestion window).
  The recommended initial window, and the reason the new setter counts
  packets rather than bytes: the recommendation is itself expressed in
  packets.
* `/app/spec/rfc9218.txt` — 4 (priority parameters), 4.1 (urgency), 4.2
  (incremental). The two priority parameters `stream_readable_next` and
  `stream_writable_next` order by, and in particular what "incremental" is
  supposed to mean: an incremental stream is one a server may serve in
  chunks alongside its peers, which is why equal-urgency incremental streams
  round-robin rather than finishing in ID order.
* `/app/spec/rfc9297.txt` — 2 (HTTP datagrams), 2.1 (HTTP/3 datagrams).
  Read to understand what is being withdrawn. The HTTP/3 datagram API
  deleted here was an implementation of an earlier draft of this document;
  the transport-level DATAGRAM frame it sat on top of (RFC 9221) is
  untouched. The distinction is exactly the one this step's removal group
  and its inherited datagram group are drawn around.

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
