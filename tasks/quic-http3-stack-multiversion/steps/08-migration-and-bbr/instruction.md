# Step 8 — Connection migration, path events, and BBR

## 1. Implement this milestone

Give the transport more than one path. Until this milestone a connection was
a single four-tuple: one local address, one remote address, fixed when the
connection was created. quiche 0.16.0 turns that into a set of paths that
can be probed, validated, migrated between and closed while the connection
stays up, which is what a phone crossing from wifi to cellular actually
needs.

Three pieces have to land together for that to mean anything. The connection
gains an explicit path API -- `probe_path` to start validating a new
four-tuple, `send_on_path` to steer a datagram onto one, `migrate` to make a
validated path active, and `path_event_next` to drain a queue of
`PathEvent`s telling the application what the transport decided. Connection
IDs gain rotation -- `new_source_cid` to issue one, `retire_destination_cid`
to stop using one -- and this is not an optional extra: you cannot probe a
second path at all until both peers have spare connection IDs, because
migrating while reusing the same connection ID hands an on-path observer a
free correlator. And BBR arrives as a third congestion controller beside
Reno and CUBIC.

One smaller change comes along for the ride and is easy to get wrong:
`h3::APPLICATION_PROTOCOL` stops being a packed wire-format blob and becomes
a list.

Four things stop compiling, and the first two are the ones that matter.

**`connect` and `accept` each take one more address.** A connection is no
longer described by a single peer address, so both constructors now name the
local end as well:

// step 07 quiche::connect(server_name, scid, peer, config)
quiche::accept(scid, odcid, peer, config)

// this step quiche::connect(server_name, scid, local, peer, config)
quiche::accept(scid, odcid, local, peer, config)

The order is `local` then `peer` in both. This is not a convenience: the
connection has to know which local address its first path starts from,
because `probe_path` will later be asked for a different one.

**`RecvInfo` gains a `to` field**, so every construction of it breaks. Where
step 07 wrote `RecvInfo { from }`, this step writes `RecvInfo { from, to }`
-- `from` the remote address the datagram arrived from, `to` the local
address it arrived on. See the descriptor element below, which is where this
most often goes wrong.

**`quiche::h3::APPLICATION_PROTOCOL` changes shape**, not value. At step 07
it is a single pre-encoded buffer, `&[u8] =
b"\x02h3\x05h3-29\x05h3-28\x05h3-27"`, where each protocol is preceded by
its own length byte -- the format TLS wants on the wire. At this milestone
it is `&[&[u8]] = &[b"h3", b"h3-29", b"h3-28", b"h3-27"]`: a list of names,
no length prefixes.

`Config::set_application_protos` changes with it, taking the list form. The
old behaviour is not deleted -- it is renamed
`set_application_protos_wire_format` and still accepts the packed buffer --
so a candidate may keep both, and this step requires both to exist. What the
step does not permit is the old signature under the old name: code that
passes a length-prefixed blob to `set_application_protos` must stop
compiling.

**`Stats` loses its per-path counters to `PathStats`.** A connection with
several paths has a congestion window, a round-trip time and a packet count
*per path*, not one of each, so the fields that describe a path move out of
`Connection::stats()` into a new `PathStats` struct reached through
`Connection::path_stats()`. `cwnd`, `rtt`, `pmtu` and `delivery_rate` are
the ones a step 01-07 solution is already reading; `stats().cwnd` stops
compiling and becomes `path_stats().next().unwrap().cwnd`, or a search of
the iterator for the path you meant. `Stats` keeps the connection-wide
totals it always had and gains `paths_count`.

The path API is additive: nothing about single-path connections changes, and
every step 01-07 group must still pass untouched.

What is newly in scope here:

* `cap-probe-and-migrate` — The candidate's own `client`, run with
  `--perform-migration` against the candidate's `server`, completes a
  transfer while moving to a second local address. The checker starts the
  server, runs the client with the flag and a 512 KiB pseudorandom file to
  fetch, and requires the retrieved bytes to be byte-identical and the
  client to exit zero.

  The second half of the group is what stops a client that accepts the flag
  and ignores it. While the client runs, the checker polls that process's own
  open file descriptors -- `/proc/<pid>/fd` resolved against `/proc/net/udp`
  and `/proc/net/udp6` -- and records every local port it ever holds a UDP
  socket bound to. Migrating to a second local address means binding a
  second socket, so the group requires **two local ports bound at the same
  moment**: the probe samples `/proc/net/udp` repeatedly while the client
  runs, and a second socket bound and then closed before the next sample is
  not observed. The ports held over the whole run are recorded and quoted in
  the failure message, but the assertion is simultaneity. This is an observation of the candidate's
  own process, needs no privileges and no packet capture, and is independent
  of how any implementation words its logs: the reference client's migration
  evidence is `info!` text, and checking that text would test upstream's
  log wording rather than a capability.

  The checker leaves `--source-port` at its default of 0 so that both sockets
  take ephemeral ports; pinning it would make the second bind collide.

  A candidate whose client rejects `--perform-migration` exits non-zero and
  does not meet this capability, which is also why this group is empty at
  step 07, where the flag does not exist.

  The limit of the observation is stated rather than hidden: two bound
  sockets is evidence of a second local address, not proof that a datagram
  ever left the second one, and no privilege-free surface at this release
  proves the latter -- `/proc/net/udp` has no per-socket byte counter and
  qlog gained no path events until after this milestone. An implementation
  that binds a decoy socket and never migrates would pass this group. The
  transport-level groups beside it are where migration itself is actually
  decided; this group's job is to show the capability reaching the
  candidate's own application.
* `cap-path-events` — A `pair::Pair` handshakes with
  `set_active_connection_id_limit(2)`, both peers issue a connection ID with
  `new_source_cid` and exchange packets until each reports
  `available_dcids() == 1`, and only then does the client call `probe_path`
  for a second local address. The group asserts that ordering is
  load-bearing: called before that exchange, `probe_path` returns
  `Err(Error::OutOfIdentifiers)`, and the group checks that first.

  Then it asserts the asymmetry of the event queues, which is the part an
  implementation is most likely to get wrong by making them symmetric. The
  **client** sees exactly one event, `Validated(client_local_2, server)`,
  and then `None` -- it never sees `New`, because the client is the side
  that initiates paths. The **server** sees two, in order: `New(server,
  client_local_2)` and then `Validated(server, client_local_2)`, and then
  `None`. Both of the server's events arrive without the application probing
  back; a server that only reports `New` and waits to be told what to do
  fails here.

  The addresses are asserted, not just the variants, so filling the tuple in
  the other order fails. And `None` is asserted after each queue drains, so
  an implementation that re-reports its most recent event indefinitely fails
  even though every individual event it reports is correct.
* `cap-sendinfo-from` — One `pair::Pair`, one datagram in each direction,
  four assertions -- that `SendInfo.from` is the sender's local address,
  `SendInfo.to` is the peer's address, `RecvInfo.from` is the sender's
  address, and `RecvInfo.to` is the receiver's local address.

  Both structs have both fields at step 07's tree too, so this group is not
  about presence. It is about `to` meaning the remote endpoint on the way
  out and the local endpoint on the way in. A stack that populates the two
  structs symmetrically -- the reading that feels natural, and the one a
  single shared helper produces -- gets two of the four assertions wrong.
* `cap-bbr` — `Config::set_cc_algorithm_name("bbr")` returns `Ok`, a
  `pair::Pair` built on that config completes a handshake, and a 256 KiB
  transfer through it arrives byte-identical with a congestion window that
  did not shrink across it (`after >= before`). A controller that opens the
  window passes, and so does one that holds it: what the group rules out is
  a controller that closes it under load it is not losing anything to.

  The group does not check BBR's state machine or its pacing decisions --
  that would test an implementation rather than a capability, and
  reasonable BBRs differ. It checks that the name resolves, that the
  connection works under it, and that the controller opens the window under
  sustained load. For contrast it also asserts
  `set_cc_algorithm_name("cubic")` still returns `Ok` and that an unknown
  name returns `Err(Error::CongestionControl)` specifically, so a stack that
  accepts every string indiscriminately, or that silently falls back to the
  default, fails.

  Alongside BBR itself, the group checks two things that hold regardless of
  which algorithm is running. First, the discriminant layout the interface
  section above documents: `CongestionControlAlgorithm::Reno as u8`,
  `::CUBIC as u8` and `::BBR as u8` are asserted equal to `(0, 1, 2)` in one
  tuple, catching a BBR that compiles and works but was inserted at 1 or 2
  in a way that renumbers CUBIC. Second, `Connection::stats()` is read back
  through an exhaustive twenty-two-field destructure of `quiche::Stats` --
  naming every field once, with no `..` rest pattern, so a stack that has
  **not** removed the four fields `PathStats` took over at this milestone
  (`cwnd`, `rtt`, `pmtu`, `delivery_rate`) -- or that has added a
  twenty-third field, or renamed one -- fails to compile here rather than
  passing with the field silently still present. The twenty-two spellings
  are written out in `TEST_INTERFACE.md`'s `Stats, PathStats, and which
  counter lives where` section, the four that must be gone included.
  `sent_bytes`,
  `paths_count`, the four packet/byte counters and all thirteen `peer_*`
  transport-parameter fields are then asserted against concrete values
  traced to `pair::tune()` (or, for the four parameters `tune()` never
  touches, RFC 9000 Section 18.2's own defaults) -- driven non-zero by the
  transfer above rather than left at their post-handshake defaults, so their
  presence is a fact about behaviour rather than a fact about
  initialization.
* `cap-migrate` — `Connection::migrate(local, peer)` itself, called by
  name -- distinct from `cap-probe-and-migrate`, which drives the reference
  client binary and never calls the method directly, and from
  `cap-path-events`, which probes a second path but never migrates onto it.
  A server calling `migrate()` is refused with `Error::InvalidState`
  unconditionally, before any connection ID or path state is even
  considered. A client that has probed and validated a second path (the same
  `exchange_cids` / `probe_path` / settle sequence `cap-path-events` uses)
  calls `migrate()` onto it and gets back `Ok(1)`, the destination
  connection ID sequence number of the path just made active -- the same
  return contract `probe_path` documents. A second call naming the same,
  already-active path also returns `Ok(1)`: migrating is idempotent, not a
  one-shot trigger that errors the second time.

  Real transfers cross both before and after the call, so the packet and
  byte counters below are driven by traffic rather than left at their
  post-handshake defaults. Before migrating, the original path reads
  `active: true` and the validated second path reads `active: false` --
  probing validates a path, it does not make it active. After migrating,
  that is exactly reversed, and the original path is still present in
  `path_stats()` with its own counters intact: migrating away from a path
  does not remove it.

  The group then reads every field `PathStats` has on the now-active path
  through an exhaustive sixteen-field destructure -- naming each field once,
  `..` never used, so a seventeenth field or a renamed one fails to compile.
  Nine of the sixteen fields are not read by any other group in this step's
  harness: `recv`, `sent_bytes`, `recv_bytes`, `lost_bytes`,
  `stream_retrans_bytes`, `rtt`, `cwnd`, `pmtu` and `delivery_rate`. All but
  `delivery_rate` are asserted -- `rtt` and the four counters positive,
  `cwnd` and `pmtu` positive, nothing lost or retransmitted on a lossless
  in-memory path; `delivery_rate` is read to complete the shape and left
  otherwise unchecked, a rate estimate with no specified formula.
  `validation_state` is asserted equal to the original path's own
  `validation_state` rather than to a literal: `PathState`, the type of the
  field, is not re-exported at the crate root at this milestone, so an
  external test cannot name one of its variants at all, only compare
  same-typed values for equality.
* `cap-path-failure` — The two `PathEvent` variants no other group in this
  step's harness ever provokes: every other path-event group only ever sees
  a probe succeed.

  `FailedValidation`: a client probes a second path and its PATH_CHALLENGE
  is emitted and then simply never delivered, three times in a row, with the
  client's own `Connection::timeout()` read fresh and slept out between
  attempts rather than a fixed sleep assumed. Three consecutive lost probes
  is what this milestone's reference behaviour turns into a failure rather
  than a fourth retransmission, and `path_event_next()` is asserted to yield
  exactly `FailedValidation(local, peer)` once they have.

  `Closed`: a server evicts an unused path to make room for a new one once
  `active_connection_id_limit` -- 2, the value `pair::tune()` sets on both
  configs in every group in this file -- is reached. With no socket to
  rebind, a passively-observed path is built the way a NAT rebinding a
  client's port looks to a server: two distinct real client packets (never
  one packet replayed, which the connection's own packet-number space would
  reject as a duplicate) are each delivered to the server carrying the same
  source connection ID but a different, never-before-seen source address.
  The first is admitted as a new, unused, unvalidated path beside the real
  one; the second pushes the path count over the limit, and the first
  spoofed path -- still unused, nobody having probed it or made it active --
  is the one evicted. The event order across both deliveries is asserted
  exactly: `ReusedSourceConnectionId`, then (on the second delivery only)
  `Closed` for the evicted path, then `New` for the one just admitted -- and
  the connection is asserted still open throughout.

  An unused path that carries a non-probing frame is not stable enough to
  evict: this milestone follows a peer's migration the moment a packet on a
  different path is the highest-numbered non-probing packet the connection
  has seen, and an active path is never `unused`. Each spoofed packet is
  therefore captured with `Connection::send()` but held back, and only
  delivered under its spoofed address after a later-numbered packet has
  already crossed on the real path, so it is no longer the connection's
  newest non-probing packet. No spare destination connection ID is ever
  issued to the connection either (`exchange_cids()` is deliberately
  skipped), so the path cannot silently stop being `unused` by being handed
  one.
* `cap-new-source-cid` — After a handshake with
  `set_active_connection_id_limit(2)`, the client calls `new_source_cid`
  with a fresh connection ID, a stateless-reset token and `retire_if_needed
  = false`. The call returns **`Ok(1)`** -- the first issued connection ID
  is sequence 1, because sequence 0 came from the handshake.

  The pair is then driven and the server's `available_dcids()` must have
  risen, which is the observable consequence of the NEW_CONNECTION_ID frame
  arriving. Issuing past the peer's limit with `retire_if_needed = false`
  must return `Err(Error::IdLimit)` rather than succeeding silently or
  panicking, and `source_cids_left()` must reach 0 at that boundary rather
  than going negative or saturating.
* `cap-send-on-path` — With a validated second path in hand,
  `send_on_path(buf, Some(local), Some(peer))` names a path the connection
  knows and the returned `SendInfo` describes exactly that path -- `from`
  equal to the `local` asked for, `to` equal to the `peer`.

  The teeth are the negative case: `send_on_path` with a four-tuple the
  connection has never seen returns `Err(Error::InvalidState)`. It does not
  fall back to the active path, which is the failure mode that leaves a
  migration bug invisible until it is a packet on the wrong network. The
  group asserts the specific variant, not merely that the call failed.

  It does not assert that `send_on_path(buf, None, None)` matches
  `send(buf)`: in a correct implementation `send` is defined as that call,
  so the equivalence is true by construction and proves nothing about the
  candidate.

  Two path-inspection methods are exercised alongside the sends.
  `paths_iter` is asserted twice, positively and negatively: from the second
  local address it must report the server reachable as soon as that path has
  been probed, and from a local address the connection holds no path on it
  must report nothing. `path_stats()` is read for the per-path numbers, and
  is the accessor used here rather than `paths_iter` precisely because it
  reports every path the connection holds rather than only the reachable
  ones.
* `cap-alpn-list-shape` — `quiche::h3::APPLICATION_PROTOCOL` iterates as
  four elements whose lengths are 2, 5, 5 and 5 and whose contents are `h3`,
  `h3-29`, `h3-28`, `h3-27` -- a list of names, with no length byte inside
  any element. At step 07 the same constant is one 21-byte buffer with the
  lengths embedded, so this assertion separates the two milestones without
  reading anyone's source.

  `Config::set_application_protos` accepts that list and a `H3Pair` built
  through it negotiates `h3`. Separately,
  `set_application_protos_wire_format` accepts the old packed buffer
  `b"\x02h3\x05h3-29\x05h3-28\x05h3-27"` and a `H3Pair` built through *it*
  negotiates `h3` as well -- which is what makes the change a rename rather
  than a deletion, and what a candidate who simply deleted the old code path
  fails.
* `cap-retire-destination-cid` — Both peers issue a spare connection ID
  and exchange packets, so the client holds two destination connection IDs:
  the handshake's sequence 0 and the spare sequence 1. It then retires the
  handshake's. The call returns `Ok(())`, the pair is driven, and the peer
  observes the retirement through `retired_scid_next()` returning that exact
  connection ID and then `None`. Stream data still flows afterwards:
  retiring a connection ID is a rotation, not a teardown.

  Two refusals are asserted by variant, because they are different
  conditions and an implementation that conflates them is wrong about one of
  them. Retiring a sequence number the connection does not have is
  `Err(Error::InvalidState)` -- there is nothing to retire. Retiring the
  last remaining destination connection ID is `Err(Error::OutOfIdentifiers)`
  -- there is something to retire, and retiring it would leave the
  connection unable to address its peer at all.

  The order the three calls are made in is part of the observation. The
  unknown-sequence case is exercised first, while a spare still exists,
  because the emptiness condition is checked ahead of the lookup and would
  otherwise hide it; the last-one-standing case is exercised at the end,
  once the spare has been consumed. A group that ran them the other way
  round would be asserting a single error twice.

`gate-endtoend` carries no capability of its own: it drives this step's
binaries end to end over loopback, and every gate group must pass. A stack
that implements every capability listed above and cannot complete one real
connection does not satisfy this step at all, however much of the new
capability it holds. The group table in `TEST_INTERFACE.md` is the contract
for it, as it is for the rest.

What must still hold: everything step 07 left working. 3 of this step's
groups re-run earlier milestones unchanged, listed by name in
`/app/WORKSPACE_HANDOFF.md`. They are gate groups as well, so any one of them
failing means the step is not satisfied. They are there because this step's
change plausibly breaks them: a rewrite that gets the new behaviour right
and drops an old one does not satisfy the step at all.

## 2. Read these local references

**Normative semantics:**

* `/app/spec/rfc9000.txt` — 9 (connection migration), 9.3 (responding to
  connection migration), 8.2 (path validation), 5.1 (connection ID). Section
  9 is the normative account of what a migration is allowed to look like,
  and 8.2 is the PATH_CHALLENGE/PATH_RESPONSE exchange that `probe_path`
  starts and `PathEvent::Validated` reports the end of. 5.1 is why
  connection-ID rotation belongs in the same step as migration rather than a
  later one: migrating without it defeats the point, and in quiche it is
  also what makes probing mechanically possible.

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
