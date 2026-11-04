# Test interface — step 01, a draft-23 QUIC transport and HTTP/3 stack

This is the contract between your implementation and the checker. Every
name, path and flag the checker depends on is written down here; nothing
outside this document is assumed about your code. Where the document and
your intuition disagree, the document wins — it is what the tests were
written against.

Generated, together with this step's interface contract, from one source.
The two cannot disagree.

## What this step is

Build a QUIC transport library and an HTTP/3 layer on top of it, to IETF QUIC
draft-23, from nothing. At the end of this step `/app` holds a Rust crate named
`quiche` that speaks draft-23 to a peer it has never met, four example binaries
that use it, and a C header that a C program can link against.

Draft-23 is not RFC 9000. The RFCs are in `spec/` because fourteen later steps
converge on them and because the framing, the packet protection construction
and the HTTP/3 and QPACK layers are unchanged in substance; where draft-23
differs, the differences are listed below and they are load-bearing. This
environment has no general network access, so the draft itself cannot be
fetched; the differences listed below are the complete account of them.

`spec/` also carries RFC 9001 Appendix A's worked example, as
`rfc9001-appendix-a.txt` and `appendix-a-vectors.json`. That example is QUIC
**version 1**: its salt is `38762cf7f55934b34d179ae6a4c80cadccbb7f0a` and its
version number is `00000001`, and **neither is this step's**. It ships so you
can check your `HKDF-Expand-Label`, AEAD and header-protection plumbing against
a published vector; the salt and version to implement are the draft-23 ones
below. `spec/SHA256SUMS` covers the ten RFC texts and not those two derived
files.

## Interface elements the checker depends on

### `the crate`

A package named `quiche` with `crate-type = ["lib", "staticlib", "cdylib"]`.
The checker finds it by looking for a `Cargo.toml` that names that package,
first at `/app/quiche` and then at `/app`; both are accepted, here and at every
step, so either arrangement is a correct answer. The checker builds the workspace
with `cargo build --offline` and then links the library into a crate of its own
as a path dependency, so the package name, not the directory name, is what has
to be right. `/app/Cargo.lock` is provided and is the resolution the checker
builds offline against: you may add dependencies only if you also regenerate
it, and the checker's registry cache contains exactly what the provided lock
names, so in practice the dependency set is fixed.

### `draft-23 deviations from RFC 9000`

Five. All five are wire facts; four of them are observable from outside, and
one clause of the third -- the absence of a Retry integrity tag -- is a rule
the checker cannot catch you breaking at this step, because it reads everything
after the ODCID as the token and never looks inside it. The next step gives
Retry its tag, and a group there does check it:

* The version number on the wire is `0xff000017`, exported as
  `quiche::PROTOCOL_VERSION`.
* The Initial secret is derived with the draft salt
  `c3eef712c72ebb5a11a7d2432bb46365bef9f502`, not RFC 9001's. Everything else
  about packet protection — `HKDF-Expand-Label` with the `tls13 ` prefix, the
  `client in` / `server in` labels, AEAD_AES_128_GCM, the AES-ECB header
  protection mask over a 16-byte sample taken 4 bytes after the packet number
  offset — is exactly as RFC 9001 Section 5 describes it.
* A Retry packet carries the original destination connection ID inline: after
  the source connection ID comes a one-byte ODCID length, then the ODCID, then
  the token to the end of the datagram. There is no Retry integrity tag; that
  arrives in a later step. Write the tag-free layout anyway: nothing here
  observes it, but leaving 16 stray bytes between the ODCID and the token
  makes the packet unparseable at the step that does.
* Long header packet types live in bits 4–5 of the first byte:
  Initial `0`, 0-RTT `1`, Handshake `2`, Retry `3`.
* The ALPN identifiers are `hq-23` for the byte-stream mapping and `h3-23` for
  HTTP/3.

A sixth difference is on the wire but is **not yours to implement**: draft-23
carries the QUIC transport parameters in TLS extension codepoint `0xffa5`
rather than RFC 9001's `0x39`. That codepoint belongs to the TLS stack, and the
BoringSSL prebuilt into this image -- the one `QUICHE_BSSL_PATH` points at --
already emits it. Hand it your encoded parameters and the codepoint is right;
there is no knob for it on this side, and no group at this step observes it.
Do not spend budget looking for one.

### `quiche constants`

`PROTOCOL_VERSION: u32` is the draft-23 wire version `0xff000017`.

`MAX_CONN_ID_LEN: usize` is the longest connection ID this milestone accepts or
emits. It has to be a `usize` **constant**, usable as an array length: the
checker builds its fixed connection IDs as `[0x11u8; quiche::MAX_CONN_ID_LEN]`,
so a function, a `u32` or a private constant does not satisfy it. Its C mirror
is `QUICHE_MAX_CONN_ID_LEN`.

### `quiche::Config`

`Config::new(version: u32) -> Result<Config>`, plus the setters the checker
calls. Three of them can fail and return `Result<()>` — the checker calls each
with `.expect(…)`, so a bare `()` does not compile against the harness:
`load_cert_chain_from_pem_file(&str) -> Result<()>`,
`load_priv_key_from_pem_file(&str) -> Result<()>`,
`set_application_protos(&[u8]) -> Result<()>` — wire format,
length-prefixed, so `b"\x05hq-23"`. The rest return `()`:
`verify_peer(bool)`, `grease(bool)`, `log_keys()`,
`set_idle_timeout(u64)` in milliseconds,
`set_max_packet_size(u64)`, `set_initial_max_data(u64)`,
`set_initial_max_stream_data_bidi_local(u64)`,
`set_initial_max_stream_data_bidi_remote(u64)`,
`set_initial_max_stream_data_uni(u64)`, `set_initial_max_streams_bidi(u64)`,
`set_initial_max_streams_uni(u64)`, `set_ack_delay_exponent(u64)`,
`set_max_ack_delay(u64)`, `set_disable_active_migration(bool)`.

### `quiche::connect, accept, negotiate_version, retry`

```rust
pub fn connect(server_name: Option<&str>, scid: &[u8], config: &mut Config)
    -> Result<Box<Connection>>;
pub fn accept(scid: &[u8], odcid: Option<&[u8]>, config: &mut Config)
    -> Result<Box<Connection>>;
pub fn negotiate_version(scid: &[u8], dcid: &[u8], out: &mut [u8])
    -> Result<usize>;
pub fn retry(scid: &[u8], dcid: &[u8], new_scid: &[u8], token: &[u8],
             out: &mut [u8]) -> Result<usize>;
```

### `quiche::Connection`

`recv(&mut [u8]) -> Result<usize>` and `send(&mut [u8]) -> Result<usize>` are
the whole I/O surface: the library never touches a socket. Both return
`Error::Done` when there is nothing to do, and `Done` is not a failure.

Also `stream_send(u64, &[u8], bool) -> Result<usize>`,
`stream_recv(u64, &mut [u8]) -> Result<(usize, bool)>`, where the bool is true
once the peer has finished the stream and `Err(Error::Done)` means there is
nothing buffered to read right now — the checker's read loop drains until it
sees the bool or `Done`, one of the two, and would otherwise spin,
`stream_shutdown(u64, Shutdown, u64) -> Result<()>`,
`stream_capacity(u64) -> Result<usize>`, `stream_finished(u64) -> bool`,
`readable()` and `writable()` returning an iterator of stream ids,
`timeout() -> Option<Duration>`, `on_timeout()`,
`close(bool, u64, &[u8]) -> Result<()>`, `trace_id() -> &str`,
`application_proto() -> &[u8]`, `is_established() -> bool`,
`is_resumed() -> bool`, `is_closed() -> bool`, `stats() -> Stats`.

### `quiche::Header, quiche::Type`

`Header::from_slice(buf: &mut [u8], dcid_len: usize) -> Result<Header>` parses
a packet header without decrypting it — a server reads it before it knows
which connection the packet belongs to, and `dcid_len` tells it how long its
own connection IDs are, for short headers that do not carry the length.

Public fields: `ty: Type`, `version: u32`, `dcid: Vec<u8>`, `scid: Vec<u8>`,
`odcid: Option<Vec<u8>>`, `token: Option<Vec<u8>>`,
`versions: Option<Vec<u32>>`. `Type` has the variants
`Initial`, `Retry`, `Handshake`, `ZeroRTT`, `VersionNegotiation`, `Short`, and
derives `PartialEq` and `Debug`.

`versions` is the version list a Version Negotiation packet carries -- the
32-bit versions that follow the two connection IDs, **in the order they appear
on the wire**, as `Some(vec![...])`. It is `None` on every other packet type,
including a short header. A later milestone re-checks this field through
`Header::from_slice` by name, comparing it against a `Some(vec![...])` literal
of the parsed versions, so the name and the `Option`-wrapped `Vec<u32>` shape
are part of the contract rather than an internal choice, even though nothing at
this step reads the field.

`version` and `scid` are not optional, so they need a value even on packets
that carry no such field. `version` reads as `0` for both `Short` and
`VersionNegotiation`: for Version Negotiation that is the literal wire value,
while for a short header it is a convention, since the packet has no version
field and `from_slice` has no connection context from which to know the
negotiated one. `scid` is a different matter -- it is empty for `Short`, which
carries no Source Connection ID at all, but a Version Negotiation packet does
carry one and `from_slice` reports it.

### `quiche::Error and quiche::Stats`

`Error` is an enum with at least `Done`, `BufferTooShort`, `UnknownVersion`,
`InvalidPacket`, `InvalidState`, `InvalidStreamState`,
`InvalidTransportParam`, `CryptoFail`, `TlsFail`, `FlowControl`,
`StreamLimit`, `FinalSize`. It derives `PartialEq` and `Debug` and implements
`std::error::Error`.

`Stats` is a plain struct with public fields `recv: usize`, `sent: usize`,
`lost: usize`, `rtt: std::time::Duration`, `cwnd: usize`.

`cwnd` is the congestion window *available for new data* -- the controller's
window less the bytes currently in flight -- so on a connection that has not
sent anything yet it reads as the full initial window.

### `quiche::h3`

`h3::APPLICATION_PROTOCOL: &[u8] = b"\x05h3-23"`.

`h3::Config::new() -> Result<Config>` -- it returns a `Result`, exactly like
`quiche::Config::new`, not a bare `Config` -- with
`set_max_header_list_size(u64)`, `set_qpack_max_table_capacity(u64)` and
`set_qpack_blocked_streams(u64)`. `h3::Header::new(&str, &str)` with `name()`
and `value()` accessors.

`h3::Connection::with_transport(&mut quiche::Connection, &h3::Config)
-> Result<Connection>`, then `send_request(&mut quiche::Connection, &[Header],
bool) -> Result<u64>`, `send_response(&mut quiche::Connection, u64, &[Header],
bool) -> Result<()>`, `send_body(&mut quiche::Connection, u64, &[u8], bool)
-> Result<usize>`, `recv_body(&mut quiche::Connection, u64, &mut [u8])
-> Result<usize>`, and `poll(&mut quiche::Connection) -> Result<(u64, Event)>`.

`h3::Event` covers at least these three, and `Headers` is a **tuple** variant
carrying the field lines, not a struct variant:

```rust
#[derive(Clone, Debug, PartialEq)]
pub enum Event {
    Headers(Vec<Header>),
    Data,
    Finished,
}
```

The checker matches it as `h3::Event::Headers(list) => list`, so a
`Headers { .. }` of any shape does not compile against the harness.

`h3::Error` is an enum **of its own**, in `quiche::h3`, distinct from the
transport-level `quiche::Error`, and it has at least a `Done` variant meaning
"nothing to report right now". `h3::Connection::poll` returns
`Err(h3::Error::Done)` when there is no event, and the checker treats that as
"not yet" rather than as a failure -- it matches
`Err(quiche::h3::Error::Done) => None` and panics on any other error. It
derives `Debug` and `PartialEq`.

### `the example binaries`

Four programs the checker runs as processes -- four *different* programs, with
four literal names that are not negotiable at this step:

| name | what it is |
| --- | --- |
| `client` | the byte-stream (`hq-23`) client |
| `server` | the byte-stream (`hq-23`) server |
| `http3-client` | the HTTP/3 (`h3-23`) client |
| `http3-server` | the HTTP/3 (`h3-23`) server |

They may be `examples/` targets or their own binary targets; the checker looks
for each name under `target/{debug,release}` and
`target/{debug,release}/examples`, and it resolves **by name first** -- the
whole search is done for one name before the next name is tried anywhere -- so
it does not matter which of those directories a given program lands in, and
building one as a `[[bin]]` and another as an example is fine.

**Do not name any of the four `quiche-client` or `quiche-server` at this
step.** Those two names belong to the later milestone where a single program
does both the byte-stream and the HTTP/3 job, and the checker does not look for
them here. The section "The binaries" below states the general rule -- that
names are resolved exactly and this section is the one that supplies them --
so the two agree; if they ever appear not to, **this one governs**: it is the
one the tests at this step were written against. Naming your plain client
`quiche-client` does not make it findable here, and would in any case be the
wrong program for the HTTP/3 groups.

`server` and `http3-server` take `--listen <addr:port>`, `--cert <path>`,
`--key <path>`, `--root <dir>`, `--name <str>`, `--max-data <n>`,
`--max-stream-data <n>` and `--no-retry`. **Address validation is on unless
`--no-retry` is given.** `--root` defaults to `examples/root/`, which the
checker never relies on: it always passes an explicit `--root`, and the
directory it passes is the one that must be served.

`client` and `http3-client` take a URL as their positional argument, plus
`--max-data <n>`, `--max-stream-data <n>`, `--wire-version <hex>` and
`--no-verify`. `<hex>` is eight bare hexadecimal digits with **no `0x`
prefix**: every call site passes `--wire-version ff000017`, and a client that
accepts only `0xff000017` fails the groups that use it. They write the response
body to stdout and exit non-zero on failure. The checker always passes
`--no-verify`: its certificate is self-signed.

`qpack-encode` and `qpack-decode` also exist and are out of scope at this step.

### `the C API`

`include/quiche.h`, read from the root the checker resolved through the crate
search above -- so `/app/include/quiche.h` when `/app` carries a manifest
(this step's starting layout), and `<crate dir>/include/quiche.h` for a crate
at `/app/quiche` with no manifest at `/app`. The header always sits beside the
crate's own `Cargo.toml`, never in a directory of its own. Alongside it,
**both** a `staticlib` (`libquiche.a`) and a `cdylib` (`libquiche.so`). The
checker requires the `cdylib` to exist and links its C program against the
**`staticlib`**, passing no BoringSSL flags of its own; see the `cap-c-api`
row in the group table for what that means for your build script. The header
declares `QUICHE_PROTOCOL_VERSION` (`0xff000017`),
`QUICHE_MAX_CONN_ID_LEN` (`20`, the ceiling RFC 9000 Section 17.2 puts on a
connection ID), `QUICHE_MIN_CLIENT_INITIAL_LEN` (`1200`, RFC 9000
Section 14.1), an opaque
`quiche_config` and `quiche_conn`, `const char *quiche_version(void)`,
`quiche_config_new(uint32_t)`, the `quiche_config_set_*` mirrors of the Rust
setters, `quiche_config_free`,
`int quiche_header_info(const uint8_t *buf, size_t buf_len, size_t dcil,
uint32_t *version, uint8_t *type, uint8_t *scid, size_t *scid_len,
uint8_t *dcid, size_t *dcid_len, uint8_t *token, size_t *token_len)`,
`quiche_accept`, `quiche_connect`, `quiche_negotiate_version`, `quiche_retry`,
`quiche_conn_recv`, `quiche_conn_send`, `quiche_conn_stream_recv`,
`quiche_conn_stream_send`, `quiche_conn_is_established`,
`quiche_conn_is_closed` and `quiche_conn_free`, all `extern "C"`.

Five `quiche_config_*` functions are **not** `set_` mirrors: they keep the Rust
method name verbatim, with no `set_` inserted. The checker's C program calls the
first of them, so the `quiche_config_set_*` rule above has exactly these
exceptions and they are spelled:

```c
void quiche_config_verify_peer(quiche_config *config, bool v);
void quiche_config_grease(quiche_config *config, bool v);
void quiche_config_log_keys(quiche_config *config);
int  quiche_config_load_cert_chain_from_pem_file(quiche_config *config,
                                                 const char *path);
int  quiche_config_load_priv_key_from_pem_file(quiche_config *config,
                                               const char *path);
```

`quiche_connect(const char *server_name, const uint8_t *scid, size_t scid_len,
quiche_config *config)` is handed the **source** connection ID: the one the
client writes into the SCID field of the Initial it sends, and the one the
server will address it by. It is *not* the destination connection ID. The
client picks that one itself, at random, and it is the one the Initial secrets
are derived over. So the value you pass to `quiche_connect` comes back out of
`quiche_header_info`'s `scid` out-parameter, not its `dcid` one -- and the same
is true of `quiche::connect`'s `scid` argument on the Rust side.

### `required to exist, out of scope at this step`

Everything above is part of the surface this task builds on, and a later step
covers most of it. Only a subset is *observed here*, and the rest is listed
above because the C header, the example binaries and the fourteen later steps
depend on it -- not because a group at this step calls it. Getting one of these
wrong costs nothing now and something later:

* `quiche::negotiate_version` and `quiche::retry` as Rust calls. The Retry
  behaviour *is* checked, but by driving your `server` as a process and reading
  the bytes it emits, not through the Rust function.
* `Connection::close`, `stream_shutdown`, `stream_finished`, `writable`,
  `is_resumed` and `application_proto`.
* `Config::grease`, `Config::log_keys`, `set_ack_delay_exponent`,
  `set_max_ack_delay` and `set_disable_active_migration`.
* `h3::Config`'s three setters, `h3::Connection::send_body` and `recv_body`,
  and `h3::Event::Data` / `h3::Event::Finished` -- the HTTP/3 body path is
  exercised at this step through `http3-client` and `http3-server` as
  processes instead.
* The binaries' `--name` flag.
* `Stats`'s `recv`, `sent`, `lost` and `rtt`. Only `cwnd` is read here.
* Every `Error` variant except `Done` and `BufferTooShort`.

The list is exact as of this step: it is what no group at this step touches.

## The groups at this step

One row per group. **gate** groups are all-or-nothing: a failure means the step is not satisfied; **requirement** groups cover this step's own new capability. The rows are grouped by tier for reading; the order they run in, and each one's result, is what `reward_details.json` records.

| group | tier | surface | what it observes |
| --- | --- | --- | --- |
| `candidate-workspace-builds` | gate | T4 | The one check every step shares: `cargo build --offline` (default targets -- libraries and binaries only, not tests or benches) must complete for the workspace at `/app` within the harness's build timeout, before any other group in this table runs. It is the one group every step carries identically, and no step declares it as a capability of its own -- but it is not optional either: one failing gate group, including this one, means the step is not satisfied, however much of the new work is right. |
| `cap-initial-keys` | requirement | T2 | The first datagram the candidate's client sends is decrypted by the checker's own draft-23 implementation: Initial secrets from salt `c3eef712…` over the client's destination connection ID, header protection removed with the AES-ECB mask, packet protection removed with AEAD_AES_128_GCM, and the plaintext then contains a CRYPTO frame at offset 0 whose bytes begin a TLS 1.3 ClientHello. Nothing in the candidate's library is used to read it. |
| `cap-header-parse` | requirement | T5 | `Header::from_slice` over six fixed datagrams with exactly one correct answer each — a draft-23 Initial with a token, a Retry, a Handshake, a 0-RTT packet, a Version Negotiation packet and a short header with a 16-byte connection ID — reporting the right `ty`, `version`, `dcid`, `scid` and `token` for every one, plus two truncated datagrams that must return `Err(Error::BufferTooShort)` rather than panic. |
| `cap-stateless-retry` | requirement | T2 | The checker sends a well-formed draft-23 Initial, with no token, to the candidate's running server. The server answers with a Retry packet: long header, type bits `3`, version `0xff000017`, the ODCID field echoing the destination connection ID the checker used, and a non-empty token. The checker then replays an Initial carrying that token and the server proceeds instead of retrying again: it answers with an Initial of its own that acknowledges the one it accepted, which is what a server with no handshake keys can send and what RFC 9000 Section 13 requires of it. |
| `cap-stream-transfer` | requirement | T1 | The candidate's `client` fetches a 512 KiB pseudorandom file from the candidate's `server` over UDP loopback and writes it to stdout byte for byte. Large enough to cross the initial congestion window, the initial flow-control window and many packet boundaries, so a stack that reassembles out-of-order or retransmitted data incorrectly produces the wrong bytes rather than the right ones by luck. |
| `cap-h3-get` | requirement | T1 | The candidate's `http3-client` performs a GET against the candidate's `http3-server` and receives the exact body, over the `h3-23` ALPN. HTTP/3 end to end: control streams, SETTINGS, a QPACK-encoded HEADERS frame and DATA. |
| `cap-flow-control` | requirement | T3 | With `initial_max_stream_data_bidi_remote` set to 4096, the sender's `stream_capacity` falls to zero after 4096 bytes are in flight and `stream_send` returns `Ok(0)` -- it accepts no more, and reports that by writing zero bytes rather than by failing with `Done`; once the receiver has read those bytes and the resulting MAX_STREAM_DATA has been delivered, capacity reopens and the remainder of a 64 KiB transfer arrives intact and in order. The receiver extends the limit by what the application has actually consumed, which is what this milestone's stack does: it does not auto-tune the window upward. A 64 KiB transfer through a 4096-byte window therefore takes at least sixteen rounds of read-and-reopen, and the group checks that it did. |
| `cap-slow-start-window` | requirement | T3 | `Stats::cwnd` on a connection that has sent nothing is the ten-packet initial window, `10 × 1452 = 14520` bytes — and it is that on a second pair built with `set_max_packet_size(1200)` too, because the initial window is a constant of the congestion controller rather than ten times the configured packet size. It then grows past `2 × 14520` over eight round trips of acknowledged data on a lossless pair, and never shrinks along the way, which is slow start. |
| `cap-h3-static-table` | requirement | T3 | A request whose field lines are all QPACK static-table entries (`:method: GET`, `:scheme: https`, `:path: /`, `:authority`) and a response carrying `:status: 200` plus a literal header with a non-ASCII-safe value round-trip through the candidate's own HTTP/3 layer and arrive at the peer as the identical name/value list, in order, delivered by `poll` as `h3::Event::Headers(list)`. |
| `cap-idle-timeout` | requirement | T3 | With `set_idle_timeout(300)` on both sides and an established connection left alone, `timeout()` reports a deadline inside that window, `on_timeout()` at the deadline closes the connection, and `is_closed()` becomes true — while the same connection exercised with traffic stays open well past 300 ms. *(This is the one group at this step whose measurement is taken over real elapsed time, since its subject is a timeout; the harness sizes the margins generously, but a stall long enough to distort a whole window would decide it.)* |
| `cap-c-api` | requirement | T1 | A C program the checker writes is compiled against `include/quiche.h` under the root the crate search resolved (`/app/include/quiche.h` in this step's starting layout), linked against the candidate's **`libquiche.a`**, and run. The checker's link line is `-lpthread -ldl -lm -lstdc++` and nothing else — in particular it passes no `-L$QUICHE_BSSL_PATH -lssl -lcrypto` — so the archive has to be self-contained. rustc folds a native library into a `staticlib` output only when the build script declared it with `cargo:rustc-link-lib=static=`; declaring BoringSSL the default (dylib) way still yields a working `cdylib` and working example binaries, but an archive that fails to link with undefined `SSL_*` and `EVP_*` symbols. The `cdylib` must also exist beside it, and the checker checks that it does. It calls `quiche_version`, builds a config, calls `quiche_connect`, pulls the first datagram out with `quiche_conn_send`, and reads it back with `quiche_header_info` — reporting a draft-23 Initial whose **source** connection ID is the one it passed to `quiche_connect`, and whose destination connection ID is a non-empty one the client chose for itself. Two further assertions come from the constants rather than from this row: the first datagram is at least `QUICHE_MIN_CLIENT_INITIAL_LEN` (1200) bytes, and it carries no token. Both are the protocol's own rules and both are restated in the header section above. The header must therefore be valid C, and the symbols must actually be exported. |
| `gate-endtoend` | gate | T1 | The stack works at all: the candidate's `server` starts, the candidate's `client` completes a handshake against it over UDP loopback and retrieves a small file, and the client exits zero. If this fails, every other T1 observation at this step is noise. |

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

There is no skeleton and no starter crate: the workspace layout is yours to create, within the constraints above. What `/app` already contains is at least this document, `instruction.md`, `WORKSPACE_HANDOFF.md`, `spec/`, `Cargo.lock`, `timer.sh` and **`.cargo/config.toml`**. Keep all of them. `.cargo/config.toml` is the one that is easy to lose, and it is worth keeping for what it records rather than for what it does: it names `QUICHE_BSSL_PATH`, the prebuilt BoringSSL this milestone links. The same value is exported into the environment during the implementation phase and at measurement time, and cargo's `[env]` table yields to a variable that is already set, so a deleted or edited copy does not by itself change the build — an edit pointing at a path that does not exist is simply ignored. Its use is that the path is visible in the workspace instead of only in `env`.

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

