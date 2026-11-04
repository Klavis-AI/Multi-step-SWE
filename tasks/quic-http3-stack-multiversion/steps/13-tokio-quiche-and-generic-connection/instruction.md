# Step 13 — An async workspace, a connection generic over its buffers, and a second BBRv2

## 1. Implement this milestone

The largest structural change since the workspace conversion, and it arrives
on four fronts at once: the repository grows an asynchronous front end, the
connection type becomes generic over where its outbound bytes live, the
congestion controller list grows a fifth entry, and two smaller knobs land
beside them.

**An asynchronous front end, as four new workspace members.** `tokio-quiche`
is a Tokio-based server and client built on top of the transport crate;
`buffer-pool`, `datagram-socket` and `task-killswitch` are the three small
crates it rests on. The workspace goes from five members to nine. The root
manifest also grows `[workspace.package]` and `[workspace.dependencies]`
tables, so the members inherit shared metadata and shared dependency
versions instead of each repeating them, and it declares `resolver = "2"`.

`tokio-quiche` is a consumer of the transport crate, not a dependency of it.
Nothing in `quiche` may depend on it, and the existing binaries keep working
with no knowledge that it is there.

**`Connection` becomes generic over a buffer factory.** Two new public
traits, `BufFactory` and `BufSplit`, describe how the crate allocates and
slices the buffers that hold outbound stream data. `Connection` gains a type
parameter with a default, so every existing signature keeps compiling, and
two new constructors let a caller pick a different one:
`connect_with_buffer_factory` and `accept_with_buf_factory`. The HTTP/3
layer follows -- every `h3` method that takes a connection now takes `&mut
Connection<F>`.

The point of the parameter is the two sends built on top of it.
`Connection::stream_send_zc` takes ownership of a buffer the factory
produced and returns whatever it could not fit, so the bytes are never
copied into the crate's own storage; `h3::Connection::send_body_zc` does the
same for an HTTP/3 body, and will ask the buffer to grow a frame header on
its front if the buffer says it can.

**A fifth congestion controller.** The `gcongestion` port of BBR and BBRv2
arrives as `CongestionControlAlgorithm::Bbr2Gcongestion = 4`, named
`bbr2_gcongestion` in string form, and `Connection::gcongestion_enabled`
reports which of the two recovery implementations a connection is running.
The four discriminants that were already there do not move. The enum itself
moves from `recovery.rs` into `recovery/mod.rs`, which changes nothing a
caller can see: it is still `quiche::CongestionControlAlgorithm`.

**Two smaller additions.** `Config::set_send_capacity_factor` multiplies the
send capacity the connection is willing to hand out in one call.
`h3::Priority` becomes `Clone` and `Copy`, and
`h3::Connection::send_additional_headers_with_priority` sends an additional
header section and reprioritises the stream in the same call.

`Connection` stops being a plain struct and becomes `Connection<F>` with a
default type parameter. The default is what keeps every existing call site
compiling -- `quiche::Connection` still means the same thing it meant last
step, and `connect` and `accept` still return it. What changes is that code
which *names* the type in a signature of its own, or writes an `impl` block
for it, now has to decide whether it is talking about the default
instantiation or about any instantiation. The HTTP/3 layer made that
decision one way: every method is generic.

The default factory type is deliberately not exported. A caller can use the
default by writing `Connection`, and can supply its own by naming its own
factory; there is no third option in which it names the crate's.

`CongestionControlAlgorithm` gains a fifth variant. Anything that matches on
it exhaustively stops compiling until the new arm is added, and anything
that assumed the set had four members -- a lookup table, a round-trip test
over every name -- is now wrong rather than merely incomplete.

`bbr2_gcongestion` is a name in the string table whether or not the
implementation behind it is compiled in. Selecting it is always accepted;
whether a connection then *runs* on the new recovery implementation is what
`gcongestion_enabled` answers, and it is a property of the build, not of the
name.

What is newly in scope here:

* `cap-tokio-quiche` — The new workspace member, used the way a consumer
  would use it, against the candidate's own client.

  A small crate is written into scratch -- a `Cargo.toml` with its
  own empty `[workspace]` table so it does not get pulled into the
  candidate's, a path dependency on `<candidate root>/tokio-quiche`, `tokio`
  with `rt` and `macros`, and `futures` -- with a `main.rs` of about sixty
  lines that binds a `std::net::UdpSocket`, calls `listen` with
  `ConnectionParams::new_server`, `SimpleConnectionIdGenerator` and
  `DefaultMetrics`, and answers every `H3Event::IncomingHeaders` with
  `:status 200` and a fixed body. That crate is the observation instrument;
  building it is the first assertion, because a `tokio-quiche` whose public
  surface is not the one the interface document fixes does not compile
  against it.

  It is built with `cargo build --offline -j 4` and with `CARGO_TARGET_DIR`
  pointed at the same target directory the driver already used for the
  candidate's workspace, so every one of the crate's dependencies is already
  compiled and the probe build is a link rather than a rebuild -- measured
  at about 20 seconds cold and nothing thereafter, against the group's
  per-case ceiling.

  Then the two halves meet. The probe is spawned as a server on a free
  loopback port with the fixture certificate and key; the candidate's
  **own** `quiche-client` is run at it with `--no-verify --http-version
  HTTP/3` and a `https://127.0.0.1:<port>/probe.bin` URL. Four assertions,
  in order:

  * the client did not hit its deadline; * the client exited 0; * the
  probe's stderr contains `path=/probe.bin`, which is the server's report of
  the `:path` pseudo-header it actually received -- so the request reached
  the application layer and was decoded, not merely accepted at the
  transport; * the bytes the client wrote out are exactly the 4096-byte
  fixed pattern the probe sent, compared by length first and then
  byte-for-byte.

  The `path=` line is the probe's own output, printed by the probe crate's own
  source, so it is not a claim about the candidate's logging. Nothing is
  asserted about `tokio-quiche`'s tracing, its metrics values, or its client
  side; the client path, the datagram surface, and the connection hooks are
  all out of scope for this step's observation.

  The case takes the harness's T1 serial guard, because it binds a port and
  runs two processes.
* `cap-generic-connection` — That `Connection<F>` is generic in more than
  its signature: the buffers really come from the factory the caller named.

  The harness supplies `CountingFactory`, whose `buf_from_slice` increments
  a thread-local counter and whose buffer holds its bytes behind an `Arc`,
  and `PrefixFactory`, a second factory of a different Rust type. Both are
  built out of the crate's own `BufFactory`/`BufSplit`, so the group fails
  to compile at all if those traits are not exported from the crate root --
  which is the first thing it is measuring.

  Three legs over a 48 KiB stream transfer, all through
  `connect_with_buffer_factory`/`accept_with_buf_factory` and the ordinary
  `send`/`recv`/`stream_send`/`stream_recv` loop:

  * **both ends on `CountingFactory`** -- the server reassembles the payload
  byte-for-byte and sees the fin, *and* the counter is strictly higher after
  the transfer than before. A candidate that accepted the type parameter and
  then routed through its own default storage passes the first half and
  fails the second. * **both ends on plain `connect`/`accept`** -- the
  transfer still works and the counter does **not** move. This is the
  control: without it, a counter that was being bumped by something ambient
  would make leg 1 vacuous. * **client on `CountingFactory`, server on
  `PrefixFactory`** -- the bytes still agree. The factory is a local storage
  decision; nothing about it is negotiated or visible on the wire, and the
  two ends of a connection are free to disagree.

  `DefaultBufFactory` is deliberately never named. The harness cannot name
  it -- it is not exported -- and the group is built so that it does not
  need to: the default is reached by writing `Connection`, and a custom one
  by naming the harness's own type.

  Not asserted: how many buffers the factory is asked for, what sizes they
  are, or when. Those are scheduling details of the send path and would make
  the group a statement about an implementation's chunking rather than about
  the type parameter.
* `cap-gcongestion-bbr2` — The fifth congestion controller, pinned at four
  levels: the discriminant, the name, the accessor, and whether it actually
  moves bytes.

  **Discriminants.** `Reno as u32 == 0`, `CUBIC == 1`, `BBR == 2`, `BBR2 ==
  3`, `Bbr2Gcongestion == 4`. The enum is `#[repr(C)]` and crosses the FFI
  boundary, so the four older values must be unchanged and the new one
  appended rather than inserted. `BBR2 != Bbr2Gcongestion` as values,
  because they are two different algorithms that happen to share a name in
  prose.

  **Names.** `set_cc_algorithm_name` accepts each of `reno`, `cubic`, `bbr`,
  `bbr2` and `bbr2_gcongestion` with `Ok(())`, and answers
  `Err(Error::CongestionControl)` for `bbr3_gcongestion` -- a near-miss
  chosen deliberately over an obviously absurd string, because the failure
  this catches is a lookup that falls through to a default when it does not
  recognise a name.

  **The accessor.** Three pairs that differ in exactly one line, each fully
  handshaked, each asked `gcongestion_enabled()` at both ends. The answers
  must be `(CUBIC, false)`, `(BBR2, false)`, `(Bbr2Gcongestion, true)` -- in
  that order, as a whole -- and the two ends of each pair must agree with
  each other, since both were configured the same way. `bbr2` mapping to
  `false` is not incidental: the crate is built with its default features,
  `gcongestion` is not among them, so the legacy implementation is what that
  name selects and the group would be wrong to expect otherwise.

  **That it works.** Each of the three pairs then carries a 256 KiB
  transfer, which must arrive byte-for-byte with its fin and leave neither
  end closed. A variant that parses and then cannot move data is not the
  feature.

  Not asserted: any throughput, timing or window figure. What separates
  BBRv2 from the gcongestion BBRv2 at runtime is pacing and window dynamics,
  and neither is something a bounded in-process case can measure without
  becoming a benchmark.
* `cap-stream-send-zc` — The zero-copy stream send, and specifically the
  part of it that an implementation could fake.

  Over a handshaked `Connection<CountingFactory>` pair:

  * **The explicit length is honoured exactly.** A 48 KiB buffer is sent
  with `len = Some(1000)` on a connection whose capacity is far larger, so
  nothing but the caller's own limit can be binding: the call returns
  `1000`, the remainder is `Some`, its length is `payload.len() - 1000`, and
  its bytes are the tail of the buffer that was handed over. * **The
  remainder is the caller's buffer, not a copy of it.** A second send of the
  whole buffer with `len = None` must be partial -- the payload is
  deliberately larger than a fresh connection's capacity -- and the harness
  keeps a clone of the buffer before giving it away. `CountingBuf::clone`
  shares the allocation, so `Arc::ptr_eq` between that witness and the
  returned remainder is the zero-copy claim itself. An implementation that
  copied the unsent bytes into storage of its own and rebuilt a buffer from
  a slice returns the right bytes in the wrong allocation and fails here. *
  **The arithmetic closes.** The transfer is then finished by feeding the
  remainder back through the same call in a bounded loop, and on every round
  the bytes written plus the remainder's length equal what was handed over.
  `Error::Done` is a retry, not a failure. * **The bytes arrive.** The
  stream is closed with an empty `stream_send(.., true)` and the server
  reassembles the full 48 KiB byte-for-byte with its fin.

  * **`fin` lands at the offset the call reached.** The buffer goes out once
  more, on a fresh stream, with `len = Some(1000)` and `fin = true`. The
  call returns a tail, and the peer must then see exactly 1000 bytes on that
  stream **with the fin**: an explicit length is the caller's own limit, not
  a capacity truncation, so it does not demote `fin`. Offering the returned
  tail to a further call is then `Err(FinalSize)` -- those bytes are past
  the final size the stream just committed to. The partiality is forced with
  an explicit length rather than by leaning on the send capacity, so this
  says the same thing on a loaded host as on an idle one. * **The errors are
  the ones `stream_send` already returns.** Two of them are driven directly.
  A send on stream 3 -- server-initiated, unidirectional, and this is the
  client -- is `Err(InvalidStreamState(3))`, naming the stream the call was
  made on. A send on a stream the peer has shut down with a STOP_SENDING is
  `Err(StreamStopped(code))` carrying **the peer's code**. The particular
  code is this case's own choice, held in a `const` and deliberately not
  written down here: what is asserted is that the number the peer shut the
  stream down with is the number that comes back, and a contract that named
  it would let an implementation return *that* number instead of carrying
  the peer's.

  Not asserted: how large each accepted chunk is, or how many rounds the
  loop takes. Both follow from the congestion window at each moment and
  neither is part of the contract; the loop only carries a sanity bound so
  that a non-progressing implementation fails as a stall rather than as a
  hang. Nor is `Error::Done` driven deliberately -- it arrives on its own
  during the transfer loop, where it is treated as a retry.
* `cap-h3-send-body-zc` — Both branches of the HTTP/3 zero-copy body send,
  and that they are indistinguishable to the peer.

  `send_body_zc` asks the caller's buffer to grow the DATA frame header on
  its front via `BufSplit::try_add_prefix`. The harness supplies one factory
  whose buffers accept a prefix (`PrefixFactory`) and one whose buffers
  refuse it (`CountingFactory`, which takes the trait's default
  implementation and returns `false`), and runs the same 3 KiB body through
  a complete HTTP/3 request/response over a `Connection<F>` pair on each.

  * **The count excludes the header.** Both legs must report exactly the
  body length. The prefix-accepting branch writes more bytes to the stream
  than the application supplied, and reporting that larger number would be
  reporting bytes the caller never asked to send; the case fails a candidate
  that forgot to subtract the prefix. * **Both legs deliver the body.** The
  client's `h3::Event::Data` yields the 3 KiB byte-for-byte in each leg. *
  **The two branches agree.** The bytes from the accepting leg and the bytes
  from the refusing leg compare equal to each other, so a candidate that
  ignored `try_add_prefix` and always wrote the header separately still has
  to produce the same wire bytes -- and one that mis-framed only the
  in-place branch fails this comparison even if each leg were somehow
  self-consistent.

  The exchange runs on `ALPN_H3` and confirms `application_proto() == b"h3"`
  before sending anything, so an HTTP/3 assertion cannot be made against a
  connection that negotiated something else.

  * **The header section has to have gone first.** On a third pair, the
  server polls the request and then calls `send_body_zc` without having
  called `send_response`: that must be `Err(h3::Error::FrameUnexpected)`,
  the documented variant and not a generic failure. The same call then
  succeeds once the response headers have gone, on the same stream with a
  buffer of the same kind -- so the leg is about the ordering rule and not
  about the stream or the buffer being wrong in some other way.

  Not asserted: the DATA frame's bytes as seen on the wire, or how many
  `stream_send` calls each branch made. The distinction between the branches
  is an allocation strategy; the only thing that may differ observably is
  nothing.
* `cap-send-capacity-factor` — That the send capacity factor scales what a
  single `stream_send` will accept, exactly and not merely monotonically.

  Four freshly handshaked pairs, identical but for one line, each handed the
  same 512 KiB payload in one `stream_send` -- a payload deliberately larger
  than any of the capacities involved, so every measurement is
  capacity-bound rather than payload-bound:

  | configuration | accepted | | --- | --- | | factor unset | `n` | |
  `set_send_capacity_factor(1.0)` | `n` | | `set_send_capacity_factor(2.0)`
  | `2 * n` | | `set_send_capacity_factor(4.0)` | `4 * n` |

  with `0 < n < 512 KiB`. The unset row and the 1.0 row must be equal, which
  is how the documented default of 1 is checked without reading it back. The
  2.0 and 4.0 rows are exact multiples, which is what distinguishes an
  implementation that multiplies from one that merely enlarges a buffer
  somewhere.

  A fifth pair at factor 2.0 then carries a 128 KiB transfer to completion,
  byte-for-byte with its fin, so that a factor which produced a bigger
  number and a broken connection does not pass.

  Not asserted: any effect on throughput, on the congestion window, or on
  what leaves the socket. The factor governs how much the application may
  hand over before being told to wait, and a case that claimed more would be
  claiming the congestion controller had been overridden.
* `cap-h3-priority-helpers` — The two HTTP/3 priority additions, held to
  their documented contracts over a `Connection<CountingFactory>` -- because
  the whole `h3` API became generic in the buffer factory in the same
  release, and a group that used the default connection would not notice if
  it had not.

  **`Priority` as a value type.** A `Priority` is assigned away and the
  original is still usable afterwards, which only compiles if it is `Copy`;
  it is cloned; `Priority::default()` equals `Priority::new(3, false)`, the
  default urgency the specification names; and `Priority::new(5, true) !=
  Priority::new(5, false)`, so equality covers the incremental flag and not
  only the urgency.

  **`send_additional_headers_with_priority`.** Over a complete HTTP/3
  exchange:

  * called by the server **before** any initial header section on that
  stream -- `Err(h3::Error::FrameUnexpected)`; * called after the response
  headers, with `is_trailer_section = true` and a `Priority` built at
  **urgency 200** -- `Ok(())`. Urgency is a three-bit field, so this is the
  clamp: the value is pinned to 7 before the internal offset of 124 is
  added, and nothing overflows. A candidate that rejected the value, or that
  added 200 to 124 and wrapped, fails here; * called a **second** time with
  `is_trailer_section = true` -- `Err(h3::Error::FrameUnexpected)`, because
  a stream carries at most one trailer section; * called by the **client**
  with `is_trailer_section = false` -- `Err(h3::Error::FrameUnexpected)`,
  because an additional non-trailing section is a response construct.

  **What the peer sees.** The client is then drained, and must observe
  exactly two header sections on the request stream -- the `:status 200`
  response section first, then the trailer section with its name/value pairs
  verbatim -- followed by `h3::Event::Finished`. Two, not three: the refused
  calls must have put nothing on the wire.

  Not asserted: any scheduling consequence of the priority. The
  specification is explicit that priority parameters are a suggestion a
  server need not act on, and there is nothing for a single-request case to
  observe even if it did.
* `cap-async-workspace` — The shape of the workspace itself, read the way
  cargo reads it.

  First, cheaply: a `Cargo.toml` must exist at each of the four new members
  -- `buffer-pool`, `datagram-socket`, `task-killswitch`, `tokio-quiche` --
  and at each of the five carried ones -- `apps`, `h3i`, `octets`, `qlog`,
  `quiche`. This runs before the metadata call so that a missing directory
  is reported as a missing directory rather than as a cargo error.

  Then authoritatively:

  ```
  cargo metadata --offline --no-deps --format-version 1 --manifest-path <root>/Cargo.toml
  ```

  with `CARGO_TARGET_DIR` in scratch. Each of the nine names must appear in
  the `workspace_members` array, and the array must have **exactly nine**
  entries -- an extra member fails as surely as a missing one, because the
  membership list is the thing being observed and a candidate that added a
  tenth crate of its own has not reproduced this release's workspace.

  Member entries are matched by package name against the id syntax cargo
  emits, accepting both the `path+file://...#name@version` and `...#name`
  spellings, so the group does not break on a cargo whose id format changed.

  The metadata call proves membership and only membership. It does **not**
  stand in for a check of the root manifest's own tables: `--no-deps` asks
  cargo for the workspace's package list, and a candidate that wrote literal
  values into every member manifest instead of inheriting them produces
  exactly the same nine-member list whether the root declares
  `[workspace.package]` at all. Nor can it say anything about dependency
  edges, which `--no-deps` omits by construction.

  So the group reads two things off the manifests directly, with whole-line
  comments stripped first:

  * the root manifest carries `[workspace.package]` and
  `[workspace.dependencies]` and sets `resolver` to `"2"` -- either TOML
  string spelling, any spacing; * `quiche/Cargo.toml` names none of
  `buffer-pool`, `datagram-socket`, `task-killswitch` or `tokio-quiche`, in
  any dependency table.

  Not asserted: dependency versions, feature lists, edition, or any member's
  internal structure, and nothing at all about what the four new manifests
  contain beyond existing. The group's claim is the shape of the workspace
  -- which crates are in it, what the root hands down, and which way the one
  dependency that matters points. The rest is checked where it can be seen
  behaving.

`gate-endtoend` carries no capability of its own: it drives this step's
binaries end to end over loopback, and every gate group must pass. A stack
that implements every capability listed above and cannot complete one real
connection does not satisfy this step at all, however much of the new
capability it holds. The group table in `TEST_INTERFACE.md` is the contract
for it, as it is for the rest.

What must still hold: everything step 12 left working. 3 of this step's
groups re-run earlier milestones unchanged, listed by name in
`/app/WORKSPACE_HANDOFF.md`. They are gate groups as well, so any one of
them failing means the step is not satisfied. They are there because this
step's change plausibly breaks them: a rewrite that gets the new behaviour
right and drops an old one does not satisfy the step at all.

## 2. Read these local references

**Normative semantics:**

* `/app/spec/rfc9000.txt` — 2.2 (sending and receiving data), 2.4
  (operations on streams), 4 (flow control), 4.1 (data flow control), 4.3
  (flow control performance). 2.2 is why a send call may accept part of what
  it is offered and no more. Stream data is an ordered byte stream that the
  sender hands over in whatever sized pieces it likes; nothing at the
  protocol level preserves the boundaries between calls, and nothing obliges
  the endpoint to take a whole piece at once. Read it before reasoning about
  `stream_send_zc`'s tail: the split point is an implementation's capacity
  decision, not a protocol event, and the receiver cannot tell where it
  happened.

  2.4 is the sending-side API this crate is a realisation of -- write data,
  close, reset -- and is worth re-reading here because a zero-copy send is
  the same operation with a different ownership story, not a new one. In
  particular `fin` is a property of the byte stream rather than of any one
  write, which is why it lands at whatever offset the write reached and why
  the bytes left over afterwards are past the final size rather than merely
  unsent.

  4 and 4.1 are the two limits that decide how much a send can accept: the
  per-stream limit and the connection-wide limit, both advertised by the
  receiver, both only ever increased. They are the reason a send that
  returns a tail is a normal outcome rather than an error, and the reason
  `Error::Done` means "nothing right now" rather than "never".

  4.3 is the counterpart to `set_send_capacity_factor` seen from the
  protocol. It is about the receiver's limits being large enough to keep the
  sender busy; the factor is about the *sender's* willingness to take bytes
  from its application before the congestion controller has room for them.
  Reading 4.3 makes clear that the factor cannot make the connection faster
  than the receiver allows -- it changes where the data waits, not how much
  may fly.
* `/app/spec/rfc9002.txt` — 7 (congestion control), 7.2 (initial and
  minimum congestion window), 7.3 (congestion control states), 7.7 (pacing),
  7.8 (underutilizing the congestion window). Section 7 is the framing this
  step's fifth algorithm sits inside. It defines the congestion window as a
  limit on bytes in flight and states plainly that QUIC's recommended
  controller is one option among many: endpoints MAY use another, provided
  it follows the ECN and persistent-congestion requirements. That is the
  licence under which a crate ships five, and it is why adding a fifth is
  not a protocol change at all -- nothing on the wire says which one is
  running, which is exactly why the API has to expose `gcongestion_enabled`
  for a caller to find out.

  7.2 and 7.3 are the vocabulary the algorithm names are shorthand for:
  initial window, minimum window, slow start, recovery, congestion
  avoidance. Read them for what all five implementations have in common, so
  that "BBRv2 versus the gcongestion BBRv2" is understood as two
  implementations of one job rather than two behaviours.

  7.7 is pacing, which is the part of the job the two BBR implementations
  differ most in, and 7.8 is the section that `set_send_capacity_factor`
  exists to act on. 7.8 describes a sender that is limited by its own
  application or by its own flow control rather than by congestion, and
  observes that such a sender's congestion window says nothing useful about
  the path. A capacity factor above 1.0 is one answer to that: let the
  application hand over more than the window currently justifies, so the
  window is not underutilised the moment it opens. Read 7.8 for why this is
  a buffering decision and not a congestion-control one.
* `/app/spec/rfc9114.txt` — 4.1 (HTTP message framing), 7.2.1 (DATA
  frames), 7.2.2 (HEADERS frames). 4.1 is the permitted shape of an HTTP/3
  message as a sequence of frames: an optional run of informational header
  sections, the final header section, then DATA, then an optional trailer
  section. It is the whole of the ordering contract that `send_body_zc` and
  `send_additional_headers_with_priority` are checked against -- a body
  before any header section, or a second trailer section, are violations of
  this sequence and not of any rule those two methods invent.

  4.1 is also where the client/server asymmetry comes from, and it is worth
  reading rather than taking on trust: informational responses are a
  response construct, so a request may not carry one, which is why a client
  calling the additional-headers method with `is_trailer_section == false`
  is refused before anything else is looked at.

  7.2.1 is the DATA frame, and it is the reason a zero-copy body send has to
  think about prefixes at all: the payload is preceded by a type and a
  length, so the bytes the application owns are not the bytes that go on the
  wire unless something can put a header in front of them.
  `BufSplit::try_add_prefix` is that something, and a buffer that refuses is
  not an error -- it just means the header is written as its own piece, and
  7.2.1 is what makes those two arrangements indistinguishable to the
  receiver.

  7.2.2 is the HEADERS frame. Nothing on the wire distinguishes an
  informational, final or trailing section, so a receiver tells them apart
  by position -- which is why the case reads them in order and compares them
  in order.
* `/app/spec/rfc9218.txt` — 4 (priority parameters), 4.1 (urgency), 4.2
  (incremental), 6 (reprioritization), 10 (server scheduling). 4.1 is the
  arithmetic behind the clamp. Urgency is an integer **between 0 and 7
  inclusive**, in descending order of priority, and the default is 3 --
  which is also why `Priority::default()` equals `Priority::new(3, false)`
  and not something else. A three-bit field has no representation for 200,
  so the choice is between rejecting the value and clamping it; this crate
  clamps, and knowing the field's range is what tells you the clamped result
  is 7 rather than 200 mod 8.

  4.2 is the other field: incremental is a boolean, it is independent of
  urgency, and it changes how a server may interleave responses at the
  *same* urgency. That independence is why two `Priority` values with equal
  urgency and different incremental flags are not equal.

  6 is reprioritization -- the statement that a priority may be signalled
  again later in the life of a request, and that the signals are cumulative
  and last-one-wins. It is the reason there is a *send headers and set
  priority* method at all rather than only a priority-at-request-time
  argument.

  10 is the disclaimer that makes all of this testable. Priority parameters
  are a suggestion; a server is not required to act on them and a client
  cannot observe scheduling from the outside. Read it before expecting a
  test to assert ordering: what is checkable is that the call is accepted,
  that the header sections arrive intact and in order, and that an
  out-of-range urgency does not break either.

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
