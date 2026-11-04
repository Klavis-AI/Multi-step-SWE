# Test interface — step 08, Connection migration, path events, and BBR

This is the contract between your implementation and the checker. Every
name, path and flag the checker depends on is written down here; nothing
outside this document is assumed about your code. Where the document and
your intuition disagree, the document wins — it is what the tests were
written against.

Generated, together with this step's interface contract, from one source.
The two cannot disagree.

## What this step is

Give the transport more than one path. Until this milestone a connection was a
single four-tuple: one local address, one remote address, fixed when the
connection was created. quiche 0.16.0 turns that into a set of paths that can
be probed, validated, migrated between and closed while the connection stays
up, which is what a phone crossing from wifi to cellular actually needs.

Three pieces have to land together for that to mean anything. The connection
gains an explicit path API -- `probe_path` to start validating a new
four-tuple, `send_on_path` to steer a datagram onto one, `migrate` to make a
validated path active, and `path_event_next` to drain a queue of `PathEvent`s
telling the application what the transport decided. Connection IDs gain
rotation -- `new_source_cid` to issue one, `retire_destination_cid` to stop
using one -- and this is not an optional extra: you cannot probe a second path
at all until both peers have spare connection IDs, because migrating while
reusing the same connection ID hands an on-path observer a free correlator.
And BBR arrives as a third congestion controller beside Reno and CUBIC.

One smaller change comes along for the ride and is easy to get wrong:
`h3::APPLICATION_PROTOCOL` stops being a packed wire-format blob and becomes
a list.

## The flip

Something an earlier step established changes meaning here. This is the
part a candidate most often gets half-right: the new behaviour appears,
the old one is left working beside it, and both the new group and the
inherited group pass for the wrong reason — until the step that
requires the old behaviour to be *gone*.

Four things stop compiling, and the first two are the ones that matter.

**`connect` and `accept` each take one more address.** A connection is no
longer described by a single peer address, so both constructors now name the
local end as well:

    // step 07
    quiche::connect(server_name, scid, peer, config)
    quiche::accept(scid, odcid, peer, config)

    // this step
    quiche::connect(server_name, scid, local, peer, config)
    quiche::accept(scid, odcid, local, peer, config)

The order is `local` then `peer` in both. This is not a convenience: the
connection has to know which local address its first path starts from, because
`probe_path` will later be asked for a different one.

**`RecvInfo` gains a `to` field**, so every construction of it breaks. Where
step 07 wrote `RecvInfo { from }`, this step writes `RecvInfo { from, to }` --
`from` the remote address the datagram arrived from, `to` the local address it
arrived on. See the descriptor element below, which is where this most often
goes wrong.

**`quiche::h3::APPLICATION_PROTOCOL` changes shape**, not value. At step 07 it
is a single pre-encoded buffer, `&[u8] = b"\x02h3\x05h3-29\x05h3-28\x05h3-27"`,
where each protocol is preceded by its own length byte -- the format TLS wants
on the wire. At this milestone it is `&[&[u8]] = &[b"h3", b"h3-29", b"h3-28",
b"h3-27"]`: a list of names, no length prefixes.

`Config::set_application_protos` changes with it, taking the list form. The old
behaviour is not deleted -- it is renamed `set_application_protos_wire_format`
and still accepts the packed buffer -- so a candidate may keep both, and this
step requires both to exist. What the step does not permit is the old signature
under the old name: code that passes a length-prefixed blob to
`set_application_protos` must stop compiling.

**`Stats` loses its per-path counters to `PathStats`.** A connection with
several paths has a congestion window, a round-trip time and a packet count
*per path*, not one of each, so the fields that describe a path move out of
`Connection::stats()` into a new `PathStats` struct reached through
`Connection::path_stats()`. `cwnd`, `rtt`, `pmtu` and `delivery_rate` are the
ones a step 01-07 solution is already reading; `stats().cwnd` stops compiling
and becomes `path_stats().next().unwrap().cwnd`, or a search of the iterator
for the path you meant. `Stats` keeps the connection-wide totals it always had
and gains `paths_count`.

The path API is additive: nothing about single-path connections changes, and
every step 01-07 group must still pass untouched.

## Interface elements the checker depends on

### `quiche::PathEvent`

A public enum in the crate root, `#[derive(Clone, Debug, PartialEq, Eq)]`,
drained one at a time by `Connection::path_event_next() -> Option<PathEvent>`.
The variants this step covers are

* `New(SocketAddr, SocketAddr)` -- a four-tuple this endpoint had not seen has
  carried a packet. **This is a server-side event only.** The client is the
  side that initiates paths, so a client never receives it; the reference
  client's handler for it is `unreachable!()`.
* `Validated(SocketAddr, SocketAddr)` -- path validation completed on
  `(local, peer)`; the path may now carry application data.
* `FailedValidation(SocketAddr, SocketAddr)` -- validation was attempted and
  did not complete.
* `Closed(SocketAddr, SocketAddr)` -- the path is gone and must not be used.
* `ReusedSourceConnectionId(u64, (SocketAddr, SocketAddr), (SocketAddr, SocketAddr))`
  -- the source connection ID with the given sequence number, which the peer
  had been using over the **first** pair, has now been seen over the **second**
  pair. This is the shape a NAT rebinding a client's port presents to a server:
  the connection ID is unchanged, the four-tuple is not. Both pairs are
  `(local, peer)` in the reporting endpoint's own frame, the first being where
  that connection ID was already in use and the second where it has just
  appeared. This step observes it on the server, and the event for a path the
  server is admitting is emitted **before** the `New` announcing that path.

Note that this is a five-of-six list, not the whole enum: the variant this step
does **not** check is `PeerMigrated(SocketAddr, SocketAddr)`, raised when a
validated path starts carrying non-probing packets. Nothing in this step's
harness asserts it, and a candidate is not checked on it.

In the four two-field variants the tuple is `(local, peer)` **from the
perspective of the endpoint reporting it**, so the same path is
`(client_local, server)` in the client's events and `(server, client_local)` in
the server's; `ReusedSourceConnectionId` carries two such pairs, each read the
same way. The two are not copies of one message; each endpoint reports its own
view.

The queue is drained, not polled: each event is returned once and
`path_event_next` yields `None` when the queue is empty.

### `Connection path methods`

`probe_path(&mut self, local: SocketAddr, peer: SocketAddr) -> Result<u64>`
starts validation of a four-tuple. It is the application saying "try this
path", not "use this path" -- data does not move until the transport reports
`PathEvent::Validated`. **The `u64` it returns is the sequence number of the
destination connection ID assigned to that path**, not a path index.

Two failure modes matter more than the success:

* Called before both peers have spare connection IDs, it returns
  `Err(Error::OutOfIdentifiers)` -- see the connection-ID element below. This
  is the single most common way a first attempt at migration fails.
* Called on a server for a path the server has not yet seen a packet on, it
  returns `Err(Error::InvalidState)`. A server may only probe a path the client
  has already used.

`send_on_path(&mut self, out: &mut [u8], from: Option<SocketAddr>, to:
Option<SocketAddr>) -> Result<(usize, SendInfo)>` is `send` with the path
chosen explicitly; `None` means "any". The relationship is literal rather than
approximate -- `send(out)` *is* `send_on_path(out, None, None)` -- so the
interesting case is the explicit one. Given a `from`/`to` pair the connection
does not know, it returns `Err(Error::InvalidState)` rather than quietly
falling back to the active path.

`migrate(&mut self, local: SocketAddr, peer: SocketAddr) -> Result<u64>` makes
a path the active one. A server calling it gets `Err(Error::InvalidState)`:
servers follow migrations, they do not initiate them.

`paths_iter(&self, from: SocketAddr) -> SocketAddrIter` enumerates the peer
addresses reachable from a given local address, which is how an event loop
knows which sockets to drain.

`is_path_validated(&self, from: SocketAddr, to: SocketAddr) -> Result<bool>`
answers whether a four-tuple is a path of this connection and has been
validated. The addresses are the path's local address and then its peer
address, in that order. A four-tuple the connection holds no path for is
`Err(Error::InvalidState)`, not `Ok(false)` -- the distinction the group at a
later step reads, where the method is called by name on a path a probe has just
validated and required to be `Ok(true)`. Nothing in this step's own harness
calls it; it is stated here because it is part of this release's path surface
and because the Rust method and the C `quiche_conn_is_path_validated` wrapper
of it, added at a later step, do not have the same shape: the Rust one returns
a `Result`.

### `Connection ID rotation, and why probing depends on it`

`new_source_cid(&mut self, scid: &ConnectionId, reset_token: u128,
retire_if_needed: bool) -> Result<u64>` issues a new source connection ID and
returns its sequence number. **The first one you issue is sequence 1** --
sequence 0 is the connection ID from the handshake. The `reset_token` is not
optional, because a connection ID with no stateless-reset token cannot be
reset.

`retire_destination_cid(&mut self, dcid_seq: u64) -> Result<()>` stops using
one of the peer's connection IDs, by sequence number.

Both are bounded by the peer's `active_connection_id_limit`, and
`Config::set_active_connection_id_limit(&mut self, v: u64)` is new here too --
step 07 carries the transport parameter but has no setter for it, because
nothing there could consume a spare identifier. **The default is 2 and values
below 2 are ignored**, which means every connection starts with room for
exactly one spare and a caller who wants more has to ask. The verifier's own
`Pair` calls this setter on every configuration it builds, behind a shim: when
the candidate's sources do not define the method the call site compiles to
nothing, and the pair falls back to the peer's default -- which is 2 anyway. A
crate without the setter therefore still compiles and still passes. What it
cannot do is ask for a *larger* limit, and `cap-path-failure` is the group that
needs one, which is why implementing it is worth the ten lines.

`new_source_cid` past the peer's limit returns `Err(Error::IdLimit)` when
`retire_if_needed` is false; with it true, the connection retires one of its
own first. `source_cids_left()`, `active_source_cids()`,
`max_active_source_cids()` and `available_dcids()` report the bookkeeping, and
`retired_scid_next()` drains the connection IDs the peer has told you to stop
using.

**The dependency that catches people:** a path is identified on the wire by its
connection ID, so probing a second path consumes a spare one. Until *both*
peers have called `new_source_cid` **and** exchanged packets so each has
learnt the other's, `probe_path` returns `Err(Error::OutOfIdentifiers)`.
Having a limit is not enough: the identifiers have to have actually crossed.

### `SendInfo and RecvInfo path descriptors`

Both structs carry both addresses at this milestone:

    pub struct SendInfo { pub from: SocketAddr, pub to: SocketAddr, pub at: Instant }
    pub struct RecvInfo { pub from: SocketAddr, pub to: SocketAddr }

**The two `to` fields do not mean the same thing, and this is the step's most
easily inverted detail.** On `SendInfo`, describing a datagram the stack is
handing you to put on the wire, `from` is the **local** address to send it
from and `to` is the **remote** address to send it to. On `RecvInfo`,
describing a datagram you are handing the stack, `from` is the **remote**
address it arrived from and `to` is the **local** address it arrived on.

So `to` is remote on the way out and local on the way in. A stack that fills
both structs from the same variable is symmetrical, self-consistent, and
wrong; the group that checks this asserts the actual endpoints, so merely
having the fields populated is not enough.

### `BBR as a third congestion controller`

`CongestionControlAlgorithm` gains a `BBR` discriminant, appended after the
two that already existed rather than inserted among them. The full layout at
this milestone is `Reno = 0`, `CUBIC = 1`, `BBR = 2` -- the first two carried
unchanged from step 03 (see that step's `set_cc_algorithm_name` element),
the third new here. `Config::set_cc_algorithm_name("bbr")` resolves to it.
The names `"reno"` and `"cubic"` keep working and `"cubic"` stays the
default, so nothing at steps 01-07 changes.

The discriminant values are load-bearing, not incidental: they cross the C
FFI boundary unchanged and are carried verbatim into qlog output, so a
candidate that adds `BBR` at a different value, or reorders the existing two
to make room for it, is wrong even though `set_cc_algorithm_name("bbr")`
still returns `Ok`.

The string is the contract. `FromStr` for the enum accepts exactly `"reno"`,
`"cubic"` and `"bbr"` and returns `Err(Error::CongestionControl)` for anything
else, rather than falling back to the default. That refusal is what makes
`"bbr"` a decidable observation: at step 07 the same call fails, here it
succeeds.

### `h3::APPLICATION_PROTOCOL and the two setters`

`quiche::h3::APPLICATION_PROTOCOL: &[&[u8]]` is a slice of protocol names
without length prefixes: `&[b"h3", b"h3-29", b"h3-28", b"h3-27"]`. Iterating
it yields four elements whose lengths are 2, 5, 5 and 5.

`Config::set_application_protos(&mut self, protos: &[&[u8]]) -> Result<()>`
takes that form. `Config::set_application_protos_wire_format(&mut self, protos:
&[u8]) -> Result<()>` takes the old packed form, and is how a caller that
already holds a length-prefixed buffer keeps working. Both must exist.

### `Stats, PathStats, and which counter lives where`

`Connection::stats() -> Stats` keeps everything that is a property of the
connection, and the group that reads it destructures it **exhaustively** --
every field named once, no `..` rest pattern -- so the struct has to have
exactly these twenty-two fields, spelled exactly like this:

```rust
pub struct Stats {
    pub recv: usize,
    pub sent: usize,
    pub lost: usize,
    pub retrans: usize,
    pub sent_bytes: u64,
    pub recv_bytes: u64,
    pub lost_bytes: u64,
    pub stream_retrans_bytes: u64,
    pub paths_count: usize,
    pub peer_max_idle_timeout: u64,
    pub peer_max_udp_payload_size: u64,
    pub peer_initial_max_data: u64,
    pub peer_initial_max_stream_data_bidi_local: u64,
    pub peer_initial_max_stream_data_bidi_remote: u64,
    pub peer_initial_max_stream_data_uni: u64,
    pub peer_initial_max_streams_bidi: u64,
    pub peer_initial_max_streams_uni: u64,
    pub peer_ack_delay_exponent: u64,
    pub peer_max_ack_delay: u64,
    pub peer_disable_active_migration: bool,
    pub peer_active_conn_id_limit: u64,
    pub peer_max_datagram_frame_size: Option<u64>,
}
```

The first nine are the connection-wide counters: the four packet counters, the
four byte counters and the new `paths_count`. The thirteen `peer_*` fields are
the peer's advertised transport parameters, under the `peer_` spellings and
shapes step 06 gave them -- `peer_disable_active_migration` a `bool`,
`peer_max_datagram_frame_size` an `Option<u64>` because DATAGRAM is negotiated
rather than defaulted, and the other eleven `u64`. The group asserts their
values against what the *peer's* `Config` was given, so a field left at zero,
or filled from the local configuration instead of the peer's, fails on the
value even when the spelling is right. They have been on `Stats` since step 06;
what changes here is that the four per-path fields (`cwnd`, `rtt`, `pmtu`,
`delivery_rate`) are gone from it.

`Connection::path_stats(&self) -> impl Iterator<Item = PathStats>` yields one
record per known path. `PathStats` carries `local_addr` and `peer_addr` so a
caller can tell which path it is looking at, `validation_state`, `active`, and
the counters that only make sense per path: `recv`, `sent`, `lost`, `retrans`,
`rtt`, **`cwnd`**, `sent_bytes`, `recv_bytes`, `lost_bytes`,
`stream_retrans_bytes`, `pmtu` and `delivery_rate`.

`validation_state`'s type is `PathState`, which is **not** re-exported at the
crate root at this milestone: an external test cannot name a variant of it, so
it compares two `PathState` values for equality instead. That comparison is
`assert_eq!`, which needs `PathState: Debug` as well as `PartialEq` -- a type
that derives only `PartialEq` does not compile against the harness, and the
failure is a compile error in the shared test binary rather than a failed
assertion.

The split is the reason `stats().cwnd` no longer compiles. A single-path
connection has exactly one `PathStats`, so the mechanical translation is to
take the first one; a connection that has probed a second path has two, in no
promised order, and a caller that cares must match on the addresses.

### `the workspace binaries`

Unchanged in name from step 06 and still driven headlessly:

* `target/debug/quiche-server` — `--listen <addr>`, `--cert <pem>`,
  `--key <pem>`, `--root <dir>`, `--no-retry`. Serves HTTP/0.9 and HTTP/3 on
  the same socket, chosen by ALPN.
* `target/debug/quiche-client` — `--no-verify`, `--http-version HTTP/3|HTTP/0.9`
  (default: negotiate), one positional URL; the body goes to stdout. New at
  this milestone: `--perform-migration`, which moves the client to a second
  local UDP socket partway through the fetch (see `cap-probe-and-migrate`
  below for exactly what is observed of that). `--source-port <port>` still
  exists from earlier steps; the migration group deliberately leaves it at
  its default of 0 so the second bind does not collide with the first.

Both honour `QLOGDIR`: if it is set and names an existing directory, each
connection writes `<scid>.sqlog` into it. The checker never writes into the
candidate's tree — certificates, the document root and the qlog directory are
all staged under its own scratch space and passed by flag or environment.

## The groups at this step

One row per group. **gate** groups are all-or-nothing: a failure means the step is not satisfied; **requirement** groups cover this step's own new capability. The rows are grouped by tier for reading; the order they run in, and each one's result, is what `reward_details.json` records.

| group | tier | surface | what it observes |
| --- | --- | --- | --- |
| `candidate-workspace-builds` | gate | T4 | The one check every step shares: `cargo build --offline` (default targets -- libraries and binaries only, not tests or benches) must complete for the workspace at `/app` within the harness's build timeout, before any other group in this table runs. It is the one group every step carries identically, and no step declares it as a capability of its own -- but it is not optional either: one failing gate group, including this one, means the step is not satisfied, however much of the new work is right. |
| `cap-probe-and-migrate` | requirement | T1 | The candidate's own `client`, run with `--perform-migration` against the candidate's `server`, completes a transfer while moving to a second local address. The checker starts the server, runs the client with the flag and a 512 KiB pseudorandom file to fetch, and requires the retrieved bytes to be byte-identical and the client to exit zero. The second half of the group is what stops a client that accepts the flag and ignores it. While the client runs, the checker polls that process's own open file descriptors -- `/proc/<pid>/fd` resolved against `/proc/net/udp` and `/proc/net/udp6` -- and records every local port it ever holds a UDP socket bound to. Migrating to a second local address means binding a second socket, so the group requires **two local ports bound at the same moment** -- the probe samples `/proc/net/udp` repeatedly while the client runs, so a second socket that is bound and then closed before the next sample is not observed. The ports the process held over the whole run are kept and quoted in the failure message, but the requirement the group asserts is simultaneity. This is an observation of the candidate's own process, needs no privileges and no packet capture, and is independent of how any implementation words its logs: the reference client's migration evidence is `info!` text, and checking that text would measure upstream's log wording rather than a capability. The checker leaves `--source-port` at its default of 0 so that both sockets take ephemeral ports; pinning it would make the second bind collide. A candidate whose client rejects `--perform-migration` exits non-zero and does not satisfy this group, which is also why this group is empty at step 07, where the flag does not exist. The limit of the observation is stated rather than hidden: two bound sockets is evidence of a second local address, not proof that a datagram ever left the second one, and no privilege-free surface at this release proves the latter -- `/proc/net/udp` has no per-socket byte counter and qlog gained no path events until after this milestone. An implementation that binds a decoy socket and never migrates would pass this group. The transport-level groups beside it are where migration itself is actually decided; this group's job is to show the capability reaching the candidate's own application. |
| `cap-path-events` | requirement | T3 | A `pair::Pair` handshakes with `set_active_connection_id_limit(2)`, both peers issue a connection ID with `new_source_cid` and exchange packets until each reports `available_dcids() == 1`, and only then does the client call `probe_path` for a second local address. The group asserts that ordering is load-bearing: called before that exchange, `probe_path` returns `Err(Error::OutOfIdentifiers)`, and the group checks that first. Then it asserts the asymmetry of the event queues, which is the part an implementation is most likely to get wrong by making them symmetric. The **client** sees exactly one event, `Validated(client_local_2, server)`, and then `None` -- it never sees `New`, because the client is the side that initiates paths. The **server** sees two, in order: `New(server, client_local_2)` and then `Validated(server, client_local_2)`, and then `None`. Both of the server's events arrive without the application probing back; a server that only reports `New` and waits to be told what to do fails here. The addresses are asserted, not just the variants, so filling the tuple in the other order fails. And `None` is asserted after each queue drains, so an implementation that re-reports its most recent event indefinitely fails even though every individual event it reports is correct. |
| `cap-sendinfo-from` | requirement | T3 | One `pair::Pair`, one datagram in each direction, four assertions -- that `SendInfo.from` is the sender's local address, `SendInfo.to` is the peer's address, `RecvInfo.from` is the sender's address, and `RecvInfo.to` is the receiver's local address. Both structs have both fields at step 07's tree too, so this group is not about presence. It is about `to` meaning the remote endpoint on the way out and the local endpoint on the way in. A stack that populates the two structs symmetrically -- the reading that feels natural, and the one a single shared helper produces -- gets two of the four assertions wrong. |
| `cap-bbr` | requirement | T3 | `Config::set_cc_algorithm_name("bbr")` returns `Ok`, a `pair::Pair` built on that config completes a handshake, and a 256 KiB transfer through it arrives byte-identical with a congestion window that did not shrink across it (`after >= before`; a BBR that opens the window passes, and so does one that holds it, since what the group rules out is a controller that closes it under sustained load). The group does not check BBR's state machine or its pacing decisions -- that would check an implementation rather than a capability, and reasonable BBRs differ. It checks that the name resolves, that the connection works under it, and that the controller opens the window under sustained load. For contrast it also asserts `set_cc_algorithm_name("cubic")` still returns `Ok` and that an unknown name returns `Err(Error::CongestionControl)` specifically, so a stack that accepts every string indiscriminately, or that silently falls back to the default, fails. Alongside BBR itself, the group checks two things that hold regardless of which algorithm is running. First, the discriminant layout the interface section above documents: `CongestionControlAlgorithm::Reno as u8`, `::CUBIC as u8` and `::BBR as u8` are asserted equal to `(0, 1, 2)` in one tuple, catching a BBR that compiles and works but was inserted at 1 or 2 in a way that renumbers CUBIC. Second, `Connection::stats()` is read back through an exhaustive twenty-two-field destructure of `quiche::Stats` -- naming every field once, with no `..` rest pattern, so a stack that has **not** removed the four fields `PathStats` took over at this milestone (`cwnd`, `rtt`, `pmtu`, `delivery_rate`) -- or that has added a twenty-third field, or renamed one -- fails to compile here rather than passing with the field silently still present. `sent_bytes`, `paths_count`, the four packet/byte counters and all thirteen `peer_*` transport-parameter fields -- the twenty-two spellings are written out in the `Stats, PathStats, and which counter lives where` section above -- are then asserted against concrete values traced to `pair::tune()` (or, for the four parameters `tune()` never touches, RFC 9000 Section 18.2's own defaults) -- driven non-zero by the transfer above rather than left at their post-handshake defaults, so their presence is a fact about behaviour rather than a fact about initialization. |
| `cap-migrate` | requirement | T3 | `Connection::migrate(local, peer)` itself, called by name -- distinct from `cap-probe-and-migrate`, which drives the reference client binary and never calls the method directly, and from `cap-path-events`, which probes a second path but never migrates onto it. A server calling `migrate()` is refused with `Error::InvalidState` unconditionally, before any connection ID or path state is even considered. A client that has probed and validated a second path (the same `exchange_cids` / `probe_path` / settle sequence `cap-path-events` uses) calls `migrate()` onto it and gets back `Ok(1)`, the destination connection ID sequence number of the path just made active -- the same return contract `probe_path` documents. A second call naming the same, already-active path also returns `Ok(1)`: migrating is idempotent, not a one-shot trigger that errors the second time. Real transfers cross both before and after the call, so the packet and byte counters below are driven by traffic rather than left at their post-handshake defaults. Before migrating, the original path reads `active: true` and the validated second path reads `active: false` -- probing validates a path, it does not make it active. After migrating, that is exactly reversed, and the original path is still present in `path_stats()` with its own counters intact: migrating away from a path does not remove it. The group then reads every field `PathStats` has on the now-active path through an exhaustive sixteen-field destructure -- naming each field once, `..` never used, so a seventeenth field or a renamed one fails to compile. Nine of the sixteen fields are not read by any other group in this step's harness: `recv`, `sent_bytes`, `recv_bytes`, `lost_bytes`, `stream_retrans_bytes`, `rtt`, `cwnd`, `pmtu` and `delivery_rate`. All but `delivery_rate` are asserted -- `rtt` and the four counters positive, `cwnd` and `pmtu` positive, nothing lost or retransmitted on a lossless in-memory path; `delivery_rate` is read to complete the shape and left otherwise unchecked, a rate estimate with no specified formula. `validation_state` is asserted equal to the original path's own `validation_state` rather than to a literal: `PathState`, the type of the field, is not re-exported at the crate root at this milestone, so an external test cannot name one of its variants at all, only compare same-typed values for equality. |
| `cap-path-failure` | requirement | T3 | The two `PathEvent` variants no other group in this step's harness ever provokes: every other path-event group only ever sees a probe succeed. `FailedValidation`: a client probes a second path and its PATH_CHALLENGE is emitted and then simply never delivered, three times in a row, with the client's own `Connection::timeout()` read fresh and slept out between attempts rather than a fixed sleep assumed. Three consecutive lost probes is what this milestone's reference behaviour turns into a failure rather than a fourth retransmission, and `path_event_next()` is asserted to yield exactly `FailedValidation(local, peer)` once they have. `Closed`: a server evicts an unused path to make room for a new one once `active_connection_id_limit` -- 2, the value `pair::tune()` sets on both configs in every group in this file -- is reached. With no socket to rebind, a passively-observed path is built the way a NAT rebinding a client's port looks to a server: two distinct real client packets (never one packet replayed, which the connection's own packet-number space would reject as a duplicate) are each delivered to the server carrying the same source connection ID but a different, never-before-seen source address. The first is admitted as a new, unused, unvalidated path beside the real one; the second pushes the path count over the limit, and the first spoofed path -- still unused, nobody having probed it or made it active -- is the one evicted. The event order across both deliveries is asserted exactly: `ReusedSourceConnectionId`, then (on the second delivery only) `Closed` for the evicted path, then `New` for the one just admitted -- and the connection is asserted still open throughout. An unused path that carries a non-probing frame is not stable enough to evict: this milestone follows a peer's migration the moment a packet on a different path is the highest-numbered non-probing packet the connection has seen, and an active path is never `unused`. Each spoofed packet is therefore captured with `Connection::send()` but held back, and only delivered under its spoofed address after a later-numbered packet has already crossed on the real path, so it is no longer the connection's newest non-probing packet. No spare destination connection ID is ever issued to the connection either (`exchange_cids()` is deliberately skipped), so the path cannot silently stop being `unused` by being handed one. |
| `cap-new-source-cid` | requirement | T3 | After a handshake with `set_active_connection_id_limit(2)`, the client calls `new_source_cid` with a fresh connection ID, a stateless-reset token and `retire_if_needed = false`. The call returns **`Ok(1)`** -- the first issued connection ID is sequence 1, because sequence 0 came from the handshake. The pair is then driven and the server's `available_dcids()` must have risen, which is the observable consequence of the NEW_CONNECTION_ID frame arriving. Issuing past the peer's limit with `retire_if_needed = false` must return `Err(Error::IdLimit)` rather than succeeding silently or panicking, and `source_cids_left()` must reach 0 at that boundary rather than going negative or saturating. |
| `cap-send-on-path` | requirement | T3 | With a validated second path in hand, `send_on_path(buf, Some(local), Some(peer))` names a path the connection knows and the returned `SendInfo` describes exactly that path -- `from` equal to the `local` asked for, `to` equal to the `peer`. The teeth are the negative case: `send_on_path` with a four-tuple the connection has never seen returns `Err(Error::InvalidState)`. It does not fall back to the active path, which is the failure mode that leaves a migration bug invisible until it is a packet on the wrong network. The group asserts the specific variant, not merely that the call failed. It does not assert that `send_on_path(buf, None, None)` matches `send(buf)`: in a correct implementation `send` is defined as that call, so the equivalence is true by construction and proves nothing about the candidate. Two path-inspection methods are exercised alongside the sends. `paths_iter` is asserted twice, positively and negatively: from the second local address it must report the server reachable as soon as that path has been probed, and from a local address the connection holds no path on it must report nothing. `path_stats()` is read for the per-path numbers, and is the accessor used here rather than `paths_iter` precisely because it reports every path the connection holds rather than only the reachable ones. |
| `cap-alpn-list-shape` | requirement | T3 | `quiche::h3::APPLICATION_PROTOCOL` iterates as four elements whose lengths are 2, 5, 5 and 5 and whose contents are `h3`, `h3-29`, `h3-28`, `h3-27` -- a list of names, with no length byte inside any element. At step 07 the same constant is one 21-byte buffer with the lengths embedded, so this assertion separates the two milestones without reading anyone's source. `Config::set_application_protos` accepts that list and a `H3Pair` built through it negotiates `h3`. Separately, `set_application_protos_wire_format` accepts the old packed buffer `b"\x02h3\x05h3-29\x05h3-28\x05h3-27"` and a `H3Pair` built through *it* negotiates `h3` as well -- which is what makes the change a rename rather than a deletion, and what a candidate who simply deleted the old code path fails. |
| `cap-retire-destination-cid` | requirement | T3 | Both peers issue a spare connection ID and exchange packets, so the client holds two destination connection IDs: the handshake's sequence 0 and the spare sequence 1. It then retires the handshake's. The call returns `Ok(())`, the pair is driven, and the peer observes the retirement through `retired_scid_next()` returning that exact connection ID and then `None`. Stream data still flows afterwards: retiring a connection ID is a rotation, not a teardown. Two refusals are asserted by variant, because they are different conditions and an implementation that conflates them is wrong about one of them. Retiring a sequence number the connection does not have is `Err(Error::InvalidState)` -- there is nothing to retire. Retiring the last remaining destination connection ID is `Err(Error::OutOfIdentifiers)` -- there is something to retire, and retiring it would leave the connection unable to address its peer at all. The order the three calls are made in is part of the observation. The unknown-sequence case is exercised first, while a spare still exists, because the emptiness condition is checked ahead of the lookup and would otherwise hide it; the last-one-standing case is exercised at the end, once the spare has been consumed. A group that ran them the other way round would be asserting a single error twice. |
| `reg-01-stream-transfer` | gate | T1 | Inherited from step 01: the candidate's `client` fetches a 512 KiB pseudorandom file from the candidate's `server` over loopback and the bytes match exactly. The path API is additive, so ordinary single-path transfers must be completely unaffected -- and this is the group that catches the usual casualty of a multi-path refactor, which is that every send now goes through path lookup and the one-path case was the last thing anyone retested. The client is given `--http-version HTTP/0.9` rather than left to negotiate. Step 01 checked this observation over the byte-stream protocol, and an inherited group has to re-run the observation it inherited; routing it through HTTP/3 would quietly make it a different group. |
| `reg-04-datagram-roundtrip` | gate | T3 | Inherited from step 04: DATAGRAM frames still round-trip, with both peers enabling them via `enable_dgram(true, 10, 10)` and a 256-byte payload echoed back byte-for-byte. A datagram is bound to a path and is not retransmitted, so of everything carried forward this is what the multi-path refactor is most likely to break and least likely to break loudly. |
| `reg-07-priority-update` | gate | T3 | Inherited from step 07: the HTTP/3 PRIORITY_UPDATE frame and its event, re-run with the same five stages and the same claims as step 07's own group -- one frame raises one event, a second update for an unread element raises none but replaces the stored value, taking it hands back the latest value once, the event then rearms, and a different prioritised element is stored beside the first rather than over it. This group also covers the ALPN change from a second angle: an HTTP/3 layer whose application protocols no longer negotiate never opens a control stream at all, so a careless edit to `set_application_protos` fails here, long before it reaches priorities, as well as in `cap-alpn-list-shape`. |
| `gate-endtoend` | gate | T1 | The candidate's `server` starts, the candidate's `client` completes a handshake against it over UDP loopback and retrieves a small file, and both exit zero. No migration, no flags: if this fails, every other observation at this step is noise. |

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

`/app` contains everything step 07 left behind, unchanged. The milestone before this one is modelled on quiche 0.14.0; this one is modelled on 0.16.0. Read `WORKSPACE_HANDOFF.md` for what is already there and what must keep working.

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

