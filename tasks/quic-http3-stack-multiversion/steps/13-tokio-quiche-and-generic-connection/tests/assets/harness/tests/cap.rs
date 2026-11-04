//! new-capability groups: what this milestone can do that the previous one could not
//!
//! One `#[test]` per scored group, named exactly as the group id with dashes
//! turned into underscores: `cap-generic-connection` is
//! `fn cap_generic_connection()`. The driver runs each one on its own with
//! `--exact` and asserts that exactly one test executed, so a renamed,
//! `#[ignore]`d or accidentally deleted function scores zero for that group
//! rather than passing silently.
//!
//! Observe behaviour. Never read the candidate's source, never grep it for an
//! identifier, and never assert on a private detail: every group sits on one
//! of the five observation surfaces -- T1 two-process loopback through the
//! candidate's own binaries, T2 the verifier speaking QUIC at the candidate,
//! T3 in-memory connection pairs through the public Rust API, T4 emitted
//! artefacts, T5 exact-answer negative corpora.

use std::cell::Cell;
use std::path::Path;
use std::path::PathBuf;
use std::sync::Arc;
use std::time::Duration;

#[allow(unused_imports)]
use quiche::h3::NameValue;
#[allow(unused_imports)]
use verifier::{pair, support, testkit};

#[allow(dead_code)]
const SHORT: Duration = Duration::from_secs(10);
#[allow(dead_code)]
const LONG: Duration = Duration::from_secs(60);

// ---------------------------------------------------------------------------
// Buffer factories the harness owns
// ---------------------------------------------------------------------------
//
// `Connection` grew a type parameter at this milestone, and the only honest
// way to observe that from outside the crate is to supply a factory the
// harness wrote and watch the connection use it. Two of them are defined
// here because the zero-copy send path has two documented branches, chosen by
// whether the buffer accepts a prefix:
//
//   * `CountingFactory` refuses a prefix (it takes `BufSplit::try_add_prefix`'s
//     default), so an HTTP/3 body must go out as a separate frame header write
//     followed by the raw body.
//   * `PrefixFactory` accepts one, so the frame header is prepended into the
//     very buffer the application handed over and a single write carries both.
//
// Both must put identical bytes on the wire. That equality is the observation.

thread_local! {
    /// How many buffers the factories below have produced on this thread.
    ///
    /// Thread-local, not global: `cargo test` runs the harness's tests on a
    /// pool of threads, and a process-wide counter would be a race between
    /// whichever groups happened to be scheduled together. quiche never moves
    /// a connection's work off the thread driving it, so a per-thread count is
    /// exact and reproducible.
    static FACTORY_CALLS: Cell<usize> = const { Cell::new(0) };
}

/// Read the per-thread buffer-production count.
fn factory_calls() -> usize {
    FACTORY_CALLS.with(|c| c.get())
}

/// Record one buffer production.
fn bump_factory_calls() {
    FACTORY_CALLS.with(|c| c.set(c.get() + 1));
}

/// A factory whose buffers are a reference-counted `Vec` plus a window, so
/// splitting one is an index adjustment rather than a copy.
#[derive(Clone, Debug, Default)]
pub struct CountingFactory;

/// [`CountingFactory`]'s buffer: a shared allocation and the half-open range
/// of it this handle owns.
#[derive(Clone, Debug, Default)]
pub struct CountingBuf {
    data: Arc<Vec<u8>>,
    start: usize,
    end: usize,
}

impl AsRef<[u8]> for CountingBuf {
    fn as_ref(&self) -> &[u8] {
        &self.data[self.start..self.end]
    }
}

impl quiche::BufFactory for CountingFactory {
    type Buf = CountingBuf;

    fn buf_from_slice(buf: &[u8]) -> CountingBuf {
        bump_factory_calls();
        CountingBuf {
            data: Arc::new(buf.to_vec()),
            start: 0,
            end: buf.len(),
        }
    }
}

impl quiche::BufSplit for CountingBuf {
    fn split_at(&mut self, at: usize) -> Self {
        assert!(at <= self.end - self.start, "split past the end of the buffer");
        let mid = self.start + at;
        let tail = CountingBuf {
            data: Arc::clone(&self.data),
            start: mid,
            end: self.end,
        };
        self.end = mid;
        tail
    }

    // `try_add_prefix` is deliberately left at the trait's default, which
    // refuses. A shared allocation has nowhere to put the prefix.
}

/// A factory whose buffers own their bytes outright and can therefore take a
/// prefix in place.
#[derive(Clone, Debug, Default)]
pub struct PrefixFactory;

/// [`PrefixFactory`]'s buffer.
#[derive(Clone, Debug, Default)]
pub struct PrefixBuf(Vec<u8>);

impl AsRef<[u8]> for PrefixBuf {
    fn as_ref(&self) -> &[u8] {
        &self.0
    }
}

impl quiche::BufFactory for PrefixFactory {
    type Buf = PrefixBuf;

    fn buf_from_slice(buf: &[u8]) -> PrefixBuf {
        bump_factory_calls();
        PrefixBuf(buf.to_vec())
    }
}

impl quiche::BufSplit for PrefixBuf {
    fn split_at(&mut self, at: usize) -> Self {
        PrefixBuf(self.0.split_off(at))
    }

    fn try_add_prefix(&mut self, prefix: &[u8]) -> bool {
        self.0.splice(0..0, prefix.iter().copied());
        true
    }
}

// ---------------------------------------------------------------------------
// A transport pair that is generic over the buffer factory
// ---------------------------------------------------------------------------
//
// `pair::Pair` is spelled in terms of `quiche::Connection`, which is
// `Connection<DefaultBufFactory>` now that the default type parameter exists.
// Groups that supply their own factory need their own two-connection driver;
// it is the same loop, with the concrete type replaced by a parameter. The
// two ends are parameterised separately so a group can prove that the choice
// of factory is local to one endpoint and invisible on the wire.

/// Build an unhandshaken client/server pair, each end with its own factory.
fn gpair<A, B>(alpn: &[&[u8]]) -> (quiche::Connection<A>, quiche::Connection<B>)
where
    A: quiche::BufFactory,
    B: quiche::BufFactory,
{
    let mut cc = pair::client_config(alpn);
    let mut sc = pair::server_config(alpn);
    gpair_with_configs(&mut cc, &mut sc)
}

/// [`gpair`] from configs the caller has already shaped.
fn gpair_with_configs<A, B>(
    client_cfg: &mut quiche::Config, server_cfg: &mut quiche::Config,
) -> (quiche::Connection<A>, quiche::Connection<B>)
where
    A: quiche::BufFactory,
    B: quiche::BufFactory,
{
    let ccid = quiche::ConnectionId::from_ref(&pair::CLIENT_CID);
    let scid = quiche::ConnectionId::from_ref(&pair::SERVER_CID);
    let client = quiche::connect_with_buffer_factory::<A>(
        Some("quic.test"),
        &ccid,
        pair::client_addr(),
        pair::server_addr(),
        client_cfg,
    )
    .expect("quiche::connect_with_buffer_factory");
    let server = quiche::accept_with_buf_factory::<B>(
        &scid,
        None,
        pair::server_addr(),
        pair::client_addr(),
        server_cfg,
    )
    .expect("quiche::accept_with_buf_factory");
    (client, server)
}

/// Move every datagram `from` currently wants to send into `to`, exactly as
/// `pair::flush` does, and return how many crossed.
fn gflush<A, B>(
    from: &mut quiche::Connection<A>, to: &mut quiche::Connection<B>,
) -> usize
where
    A: quiche::BufFactory,
    B: quiche::BufFactory,
{
    let mut buf = [0u8; pair::MAX_DATAGRAM];
    let mut n = 0;
    loop {
        let (len, info) = match from.send(&mut buf) {
            Ok(v) => v,
            Err(quiche::Error::Done) => return n,
            Err(e) => panic!("send on {}: {e:?}", from.trace_id()),
        };
        n += 1;
        let mut d = buf[..len].to_vec();
        let ri = quiche::RecvInfo {
            from: info.from,
            to: info.to,
        };
        match to.recv(&mut d, ri) {
            Ok(_) => {},
            Err(quiche::Error::Done) => {},
            Err(e) =>
                panic!("recv on {}: {e:?} ({len} byte datagram)", to.trace_id()),
        }
        assert!(n <= 4096, "gflush did not terminate: {n} datagrams");
    }
}

/// Exchange in both directions until neither end has anything left to say.
fn gsettle<A, B>(
    client: &mut quiche::Connection<A>, server: &mut quiche::Connection<B>,
    rounds: usize,
) -> usize
where
    A: quiche::BufFactory,
    B: quiche::BufFactory,
{
    for i in 0..rounds {
        let a = gflush(client, server);
        let b = gflush(server, client);
        if a + b == 0 {
            return i;
        }
    }
    rounds
}

/// Complete the handshake on a generic pair, or fail the group saying so.
fn ghandshake<A, B>(
    client: &mut quiche::Connection<A>, server: &mut quiche::Connection<B>,
) where
    A: quiche::BufFactory,
    B: quiche::BufFactory,
{
    for _ in 0..40 {
        if client.is_established() && server.is_established() {
            return;
        }
        if gflush(client, server) + gflush(server, client) == 0 {
            break;
        }
    }
    panic!(
        "handshake did not complete: client established={} closed={}, \
         server established={} closed={}",
        client.is_established(),
        client.is_closed(),
        server.is_established(),
        server.is_closed()
    );
}

/// Drain one stream to its end, exactly as `pair::read_stream` does.
fn gread_stream<F>(
    conn: &mut quiche::Connection<F>, stream_id: u64,
) -> (Vec<u8>, bool)
where
    F: quiche::BufFactory,
{
    let mut out = Vec::new();
    let mut fin = false;
    let mut buf = vec![0u8; 65535];
    loop {
        match conn.stream_recv(stream_id, &mut buf) {
            Ok((n, f)) => {
                out.extend_from_slice(&buf[..n]);
                fin |= f;
                if f {
                    break;
                }
            },
            Err(quiche::Error::Done) => break,
            Err(e) => panic!("stream_recv {stream_id}: {e:?}"),
        }
    }
    (out, fin)
}

/// Push `data` from `client` to `server` on `stream_id` in as many pieces as
/// send capacity allows, the generic twin of `pair::Pair::bulk_client_to_server`.
///
/// A single `stream_send` is capped by the connection's send capacity, which
/// starts at the initial congestion window; anything larger than that has to
/// be written across several rounds with a settle in between to collect the
/// acknowledgements that reopen the window.
fn gbulk<A, B>(
    client: &mut quiche::Connection<A>, server: &mut quiche::Connection<B>,
    stream_id: u64, data: &[u8],
) -> (Vec<u8>, bool)
where
    A: quiche::BufFactory,
    B: quiche::BufFactory,
{
    let mut sent = 0usize;
    let mut got: Vec<u8> = Vec::new();
    let mut fin = false;
    let mut rounds = 0usize;
    while sent < data.len() || !fin {
        rounds += 1;
        assert!(
            rounds <= 4096,
            "bulk transfer on stream {stream_id} stalled after {rounds} rounds \
             with {sent} of {} bytes accepted and {} delivered",
            data.len(),
            got.len()
        );
        if sent < data.len() {
            match client.stream_send(stream_id, &data[sent..], false) {
                Ok(n) => sent += n,
                Err(quiche::Error::Done) => {},
                Err(e) => panic!(
                    "stream_send on {stream_id} after {sent} of {} bytes: {e:?}",
                    data.len()
                ),
            }
            if sent == data.len() {
                client
                    .stream_send(stream_id, &[], true)
                    .expect("mark the stream finished");
            }
        }
        gsettle(client, server, 64);
        let (chunk, f) = gread_stream(server, stream_id);
        got.extend_from_slice(&chunk);
        fin |= f;
    }
    (got, fin)
}

// ---------------------------------------------------------------------------
// groups
// ---------------------------------------------------------------------------

/// `Connection` is generic over a caller-supplied buffer factory.
///
/// Three legs, because "it compiles" is not an observation:
///
///  1. A pair whose *both* ends are built through
///     `connect_with_buffer_factory` / `accept_with_buf_factory` with the
///     harness's own factory completes a handshake and carries a payload
///     byte-for-byte, and the factory's production counter moves while it
///     happens -- the connection really routes its stream storage through the
///     supplied type rather than ignoring the parameter.
///  2. A control pair built with plain `connect`/`accept` moves the same
///     payload and does *not* move the counter, which is what makes leg 1's
///     counter reading mean something.
///  3. A mixed pair -- default factory on the client, the harness's on the
///     server -- still interoperates, so the parameter is an endpoint-local
///     storage decision and not a wire-visible one.
#[test]
fn cap_generic_connection() {
    let payload = testkit::body(48 * 1024, 0x13_0001);

    // Leg 1: both ends on the harness's factory.
    let before = factory_calls();
    let (mut c, mut s) = gpair::<CountingFactory, CountingFactory>(pair::ALPN_HQ);
    ghandshake(&mut c, &mut s);
    let (got, fin) = gbulk(&mut c, &mut s, 4, &payload);
    assert_eq!(
        got.len(),
        payload.len(),
        "custom-factory server reassembled {} bytes, sent {}",
        got.len(),
        payload.len()
    );
    assert!(got == payload, "custom-factory transfer was not byte-for-byte");
    assert!(fin, "custom-factory server never saw the stream's fin");
    let after = factory_calls();
    assert!(
        after > before,
        "the connection produced no buffers through the supplied factory \
         ({before} before the transfer, {after} after): the type parameter was \
         accepted but the storage it names was never used"
    );

    // Leg 2: the control. Plain `connect`/`accept` must not touch it.
    let before_default = factory_calls();
    let mut p = pair::Pair::new(pair::ALPN_HQ);
    p.handshake();
    let (got, fin) = p.bulk_client_to_server(4, &payload);
    assert!(got == payload, "default-factory transfer was not byte-for-byte");
    assert!(fin, "default-factory server never saw the stream's fin");
    assert_eq!(
        factory_calls(),
        before_default,
        "a pair built with plain connect/accept produced buffers through the \
         harness's factory; the counter is measuring something ambient rather \
         than the factory the connection was given"
    );

    // Leg 3: one end custom, one end default, same wire.
    let (mut c, mut s) = gpair::<CountingFactory, PrefixFactory>(pair::ALPN_HQ);
    ghandshake(&mut c, &mut s);
    let (got, fin) = gbulk(&mut c, &mut s, 8, &payload);
    assert!(
        got == payload,
        "a client on one factory and a server on another did not agree on the \
         {} bytes that crossed between them",
        payload.len()
    );
    assert!(fin, "mixed-factory server never saw the stream's fin");
}

/// The gcongestion BBRv2 backend is selectable, is distinct from the legacy
/// BBRv2, and carries a real transfer.
///
/// `CongestionControlAlgorithm` gained a fifth variant at this milestone and
/// moved out of the congestion module, and `Connection::gcongestion_enabled`
/// is the accessor that says which of the two recovery backends a path ended
/// up with. The group pins the discriminant, the name mapping including its
/// rejection of an unknown name, the accessor's answer for three different
/// configurations, and that the new backend actually delivers bytes.
#[test]
fn cap_gcongestion_bbr2() {
    // The discriminant is part of the FFI surface: `#[repr(C)]` with the four
    // older values unchanged and the new one appended.
    assert_eq!(quiche::CongestionControlAlgorithm::Reno as u32, 0);
    assert_eq!(quiche::CongestionControlAlgorithm::CUBIC as u32, 1);
    assert_eq!(quiche::CongestionControlAlgorithm::BBR as u32, 2);
    assert_eq!(quiche::CongestionControlAlgorithm::BBR2 as u32, 3);
    assert_eq!(
        quiche::CongestionControlAlgorithm::Bbr2Gcongestion as u32,
        4,
        "the gcongestion variant must be appended as 4, not inserted"
    );
    assert_ne!(
        quiche::CongestionControlAlgorithm::BBR2,
        quiche::CongestionControlAlgorithm::Bbr2Gcongestion,
        "the two BBRv2 backends are different algorithms and must not compare equal"
    );

    // Name mapping, including the rejection.
    let mut cfg = pair::client_config(pair::ALPN_HQ);
    for name in ["reno", "cubic", "bbr", "bbr2", "bbr2_gcongestion"] {
        assert_eq!(
            cfg.set_cc_algorithm_name(name),
            Ok(()),
            "set_cc_algorithm_name({name:?}) was rejected"
        );
    }
    assert_eq!(
        cfg.set_cc_algorithm_name("bbr3_gcongestion"),
        Err(quiche::Error::CongestionControl),
        "an unknown congestion control name must be rejected, not defaulted"
    );

    // The accessor, on three pairs that differ only in the algorithm.
    let mut answers = Vec::new();
    for algo in [
        quiche::CongestionControlAlgorithm::CUBIC,
        quiche::CongestionControlAlgorithm::BBR2,
        quiche::CongestionControlAlgorithm::Bbr2Gcongestion,
    ] {
        let mut cc = pair::client_config(pair::ALPN_HQ);
        let mut sc = pair::server_config(pair::ALPN_HQ);
        cc.set_cc_algorithm(algo);
        sc.set_cc_algorithm(algo);
        let mut p = pair::Pair::with_configs(&mut cc, &mut sc);
        p.handshake();
        let client = p
            .client
            .gcongestion_enabled()
            .expect("an established connection has an active path");
        let server = p
            .server
            .gcongestion_enabled()
            .expect("an established connection has an active path");
        assert_eq!(
            client, server,
            "{algo:?}: the two ends of one pair disagree on which recovery \
             backend they are running"
        );
        answers.push((algo, client));

        // The algorithm must still move bytes, not merely be selectable.
        let payload = testkit::body(256 * 1024, 0x13_0002);
        let (got, fin) = p.bulk_client_to_server(4, &payload);
        assert_eq!(
            got.len(),
            payload.len(),
            "{algo:?}: server reassembled {} bytes of {}",
            got.len(),
            payload.len()
        );
        assert!(got == payload, "{algo:?}: transfer was not byte-for-byte");
        assert!(fin, "{algo:?}: server never saw the stream's fin");
        assert!(!p.client.is_closed(), "{algo:?}: client closed during the transfer");
        assert!(!p.server.is_closed(), "{algo:?}: server closed during the transfer");
    }

    let want = [
        (quiche::CongestionControlAlgorithm::CUBIC, false),
        (quiche::CongestionControlAlgorithm::BBR2, false),
        (quiche::CongestionControlAlgorithm::Bbr2Gcongestion, true),
    ];
    assert_eq!(
        answers[..],
        want[..],
        "gcongestion_enabled must be true for exactly the gcongestion variant; \
         the crate is built with default features, so `bbr2` is the legacy \
         implementation"
    );
}

/// `stream_send` gained a zero-copy sibling that takes ownership of the
/// application's buffer and hands back whatever did not fit.
///
/// The observation that separates a real implementation from a wrapper that
/// copies: the returned remainder must be *the caller's own buffer, split*.
/// `CountingBuf` keeps its bytes behind an `Arc`, so if quiche split the
/// buffer the harness handed it, the remainder points at the same allocation;
/// if quiche copied the bytes into storage of its own and rebuilt a buffer
/// from a slice, it does not. The group then finishes the transfer through
/// the same call and checks the server reassembled the original bytes.
#[test]
fn cap_stream_send_zc() {
    use quiche::BufFactory;

    let payload = testkit::body(48 * 1024, 0x13_0003);
    let (mut c, mut s) = gpair::<CountingFactory, CountingFactory>(pair::ALPN_HQ);
    ghandshake(&mut c, &mut s);

    // An explicit `len` shorter than the buffer must be honoured exactly: the
    // send capacity of a freshly handshaked connection is far larger than 1000
    // bytes, so nothing but the caller's own limit can be binding here.
    let short = CountingFactory::buf_from_slice(&payload);
    let (n, rem) = c
        .stream_send_zc(4, short, Some(1000), false)
        .expect("stream_send_zc with an explicit len");
    assert_eq!(n, 1000, "an explicit len of 1000 wrote {n} bytes");
    let rem = rem.expect("a partial write must return the unwritten remainder");
    assert_eq!(
        rem.as_ref().len(),
        payload.len() - 1000,
        "remainder length does not account for the whole buffer"
    );
    assert!(
        rem.as_ref() == &payload[1000..],
        "the remainder is not the tail of the buffer that was handed over"
    );

    // The zero-copy claim. Keep a clone of the buffer before giving it away:
    // `CountingBuf::clone` shares the allocation, so `Arc::ptr_eq` against the
    // remainder says whether quiche split the buffer or copied out of it.
    let whole = CountingFactory::buf_from_slice(&payload);
    let witness = whole.clone();
    let (n2, rem2) = c
        .stream_send_zc(8, whole, None, false)
        .expect("stream_send_zc with no explicit len");
    assert!(n2 > 0, "stream_send_zc with no len wrote nothing");
    assert!(
        n2 < payload.len(),
        "the payload is deliberately larger than a fresh connection's send \
         capacity, so this write must be partial; it accepted all {n2} bytes"
    );
    let rem2 = rem2.expect("a partial write must return the unwritten remainder");
    assert_eq!(
        rem2.as_ref().len(),
        payload.len() - n2,
        "remainder length does not account for the whole buffer"
    );
    assert!(
        Arc::ptr_eq(&witness.data, &rem2.data),
        "the remainder sits in a different allocation from the buffer that was \
         handed over: the send copied the bytes instead of taking the buffer"
    );

    // Finish stream 8 through the same call and check what arrived.
    let mut left = Some(rem2);
    let mut rounds = 0usize;
    while let Some(buf) = left.take() {
        rounds += 1;
        assert!(rounds <= 4096, "zero-copy transfer stalled after {rounds} rounds");
        match c.stream_send_zc(8, buf.clone(), None, false) {
            Ok((wrote, rest)) => {
                assert_eq!(
                    wrote + rest.as_ref().map_or(0, |r| r.as_ref().len()),
                    buf.as_ref().len(),
                    "written plus remainder must equal what was handed over"
                );
                left = rest.filter(|r| !r.as_ref().is_empty());
            },
            Err(quiche::Error::Done) => left = Some(buf),
        Err(e) => panic!("stream_send_zc: {e:?}"),
        }
        gsettle(&mut c, &mut s, 64);
    }
    c.stream_send(8, &[], true).expect("mark stream 8 finished");
    gsettle(&mut c, &mut s, 64);
    let (got, fin) = gread_stream(&mut s, 8);
    assert_eq!(
        got.len(),
        payload.len(),
        "server reassembled {} bytes of the {} that were sent zero-copy",
        got.len(),
        payload.len()
    );
    assert!(got == payload, "the zero-copy transfer was not byte-for-byte");
    assert!(fin, "server never saw stream 8 finish");

    // `fin` is applied at the offset the call actually reached, and an
    // explicit `len` is the caller's own limit rather than a capacity
    // truncation: the call comes back with a tail *and* closes the stream at
    // the bytes it took. The tail it handed back can then never be sent.
    // Forcing the partiality with `len` rather than by leaning on the send
    // capacity makes the leg say the same thing on a loaded host as on an
    // idle one.
    let held = CountingFactory::buf_from_slice(&payload);
    let (n3, rem3) = c
        .stream_send_zc(12, held, Some(1000), true)
        .expect("stream_send_zc with an explicit len and fin set");
    assert_eq!(n3, 1000, "an explicit len of 1000 wrote {n3} bytes");
    let rem3 = rem3.expect("a 1000-byte limit on a larger buffer must leave a tail");
    gsettle(&mut c, &mut s, 64);
    let (part, part_fin) = gread_stream(&mut s, 12);
    assert_eq!(
        part.len(),
        1000,
        "the peer saw {} bytes on stream 12, not the 1000 that were sent",
        part.len()
    );
    assert!(
        part_fin,
        "the peer never saw stream 12 finish; `fin` is applied at the offset \
         the call reached, and an explicit `len` does not demote it"
    );
    match c.stream_send_zc(12, rem3, None, false) {
        Err(quiche::Error::FinalSize) => {},
        other => panic!(
            "the tail of a send that carried `fin` sits past the final size \
             and must be Err(FinalSize); observed {:?}",
            other.map(|(n, _)| n)
        ),
    }

    // The errors are the ones `stream_send` already returns. Stream 3 is
    // server-initiated and unidirectional and this is the client, so the
    // local endpoint may not write to it at all.
    let wrong = CountingFactory::buf_from_slice(b"not this way");
    match c.stream_send_zc(3, wrong, None, false) {
        Err(quiche::Error::InvalidStreamState(id)) => assert_eq!(
            id, 3,
            "InvalidStreamState named stream {id} rather than the stream the \
             call was made on"
        ),
        other => panic!(
            "stream_send_zc on a peer-initiated unidirectional stream must be \
             Err(InvalidStreamState); observed {:?}",
            other.map(|(n, _)| n)
        ),
    }

    // And after a STOP_SENDING, `StreamStopped` carrying the peer's code. The
    // code is this case's own choice and is deliberately not named in the
    // contract: what is asserted is that the number the peer shut the stream
    // down with is the number that comes back.
    const STOPPED: u64 = 0x13_1616;
    c.stream_send(16, b"before", false)
        .expect("open stream 16 before the peer stops it");
    gsettle(&mut c, &mut s, 64);
    s.stream_shutdown(16, quiche::Shutdown::Read, STOPPED)
        .expect("server STOP_SENDING on stream 16");
    gsettle(&mut c, &mut s, 64);
    let after = CountingFactory::buf_from_slice(b"after");
    match c.stream_send_zc(16, after, None, false) {
        Err(quiche::Error::StreamStopped(code)) => assert_eq!(
            code, STOPPED,
            "StreamStopped reported code {code}, not the {STOPPED} the peer \
             sent in its STOP_SENDING"
        ),
        other => panic!(
            "stream_send_zc after a STOP_SENDING must be Err(StreamStopped); \
             observed {:?}",
            other.map(|(n, _)| n)
        ),
    }
}

/// HTTP/3 bodies can be handed over as raw buffers, and the frame header is
/// prepended into the caller's buffer when the buffer allows it.
///
/// `send_body_zc` has two documented branches, chosen by whether
/// `BufSplit::try_add_prefix` accepts the DATA frame header:
///
///   * accepted -- the header rides in the application's own buffer and one
///     write carries both, and the returned count must exclude the header
///     the application never asked to send;
///   * refused (the trait's default) -- the header goes out as a separate
///     `stream_send` ahead of the body.
///
/// The group drives both with a factory of each shape and requires the peer
/// to see the same body either way. A candidate that forgot to subtract the
/// prefix length, or that ignored `try_add_prefix` and always wrote the header
/// separately, fails one leg or the other.
#[test]
fn cap_h3_send_body_zc() {
    let body = testkit::body(3 * 1024, 0x13_0004);

    let (accepted_n, accepted) = h3_body_zc::<PrefixFactory>(&body);
    let (refused_n, refused) = h3_body_zc::<CountingFactory>(&body);

    assert_eq!(
        accepted_n,
        body.len(),
        "with a buffer that accepts the frame header, send_body_zc reported \
         {accepted_n} bytes for a {} byte body; the count must be of body \
         bytes, not of body plus the header the caller never supplied",
        body.len()
    );
    assert_eq!(
        refused_n,
        body.len(),
        "with a buffer that refuses the frame header, send_body_zc reported \
         {refused_n} bytes for a {} byte body",
        body.len()
    );
    assert!(
        accepted == body,
        "the prefix-accepting leg delivered {} bytes that do not match the body",
        accepted.len()
    );
    assert!(
        refused == body,
        "the prefix-refusing leg delivered {} bytes that do not match the body",
        refused.len()
    );
    assert!(
        accepted == refused,
        "the two branches of send_body_zc put different bytes on the wire"
    );

    h3_body_zc_before_headers();
}

/// `send_body_zc` inherits `send_body`'s ordering rule: the header section has
/// to have gone first, or the call is `Err(h3::Error::FrameUnexpected)`.
///
/// Driven on its own pair rather than inside `h3_body_zc`, because the point
/// is the state the connection is in *before* `send_response` is called and
/// that helper's whole shape is "headers first, then body".
fn h3_body_zc_before_headers() {
    use quiche::BufFactory;

    let (mut c, mut s) = gpair::<CountingFactory, CountingFactory>(pair::ALPN_H3);
    ghandshake(&mut c, &mut s);
    let cfg = pair::h3_config();
    let mut hc = quiche::h3::Connection::with_transport(&mut c, &cfg)
        .expect("h3::Connection::with_transport on the client");
    let mut hs = quiche::h3::Connection::with_transport(&mut s, &cfg)
        .expect("h3::Connection::with_transport on the server");
    gsettle(&mut c, &mut s, 16);

    let req = vec![
        quiche::h3::Header::new(b":method", b"GET"),
        quiche::h3::Header::new(b":scheme", b"https"),
        quiche::h3::Header::new(b":authority", b"quic.test"),
        quiche::h3::Header::new(b":path", b"/order"),
    ];
    let stream_id = hc
        .send_request(&mut c, &req, true)
        .expect("h3 send_request over a Connection<F>");
    gsettle(&mut c, &mut s, 16);

    let mut saw_request = false;
    while let Ok((id, ev)) = hs.poll(&mut s) {
        if let quiche::h3::Event::Headers { .. } = ev {
            assert_eq!(id, stream_id, "server saw headers on an unexpected stream");
            saw_request = true;
        }
    }
    assert!(saw_request, "server never polled the request headers");

    // No `send_response` has been made on this stream. The body call must be
    // refused, and refused with the documented variant rather than with a
    // generic failure.
    let mut early = CountingFactory::buf_from_slice(b"body before headers");
    match hs.send_body_zc(&mut s, stream_id, &mut early, false) {
        Err(quiche::h3::Error::FrameUnexpected) => {},
        other => panic!(
            "send_body_zc before the header section must be \
             Err(h3::Error::FrameUnexpected); observed {other:?}"
        ),
    }

    // And the same call succeeds once the header section has gone, so the leg
    // above is about the ordering and not about the buffer or the stream.
    hs.send_response(
        &mut s,
        stream_id,
        &[quiche::h3::Header::new(b":status", b"200")],
        false,
    )
    .expect("h3 send_response over a Connection<F>");
    let mut later = CountingFactory::buf_from_slice(b"body after headers");
    let n = hs
        .send_body_zc(&mut s, stream_id, &mut later, true)
        .expect("send_body_zc after the header section");
    assert!(n > 0, "send_body_zc reported {n} bytes after the header section");
}

/// One HTTP/3 request/response over a `Connection<F>`, with the response body
/// sent through `send_body_zc`. Returns what the server reported writing and
/// what the client actually read.
fn h3_body_zc<F>(body: &[u8]) -> (usize, Vec<u8>)
where
    F: quiche::BufFactory,
    F::Buf: quiche::BufSplit,
{
    let (mut c, mut s) = gpair::<F, F>(pair::ALPN_H3);
    ghandshake(&mut c, &mut s);
    assert_eq!(
        c.application_proto(),
        b"h3",
        "expected the handshake to negotiate h3"
    );
    let cfg = pair::h3_config();
    let mut hc = quiche::h3::Connection::with_transport(&mut c, &cfg)
        .expect("h3::Connection::with_transport on the client");
    let mut hs = quiche::h3::Connection::with_transport(&mut s, &cfg)
        .expect("h3::Connection::with_transport on the server");
    gsettle(&mut c, &mut s, 16);

    let req = vec![
        quiche::h3::Header::new(b":method", b"GET"),
        quiche::h3::Header::new(b":scheme", b"https"),
        quiche::h3::Header::new(b":authority", b"quic.test"),
        quiche::h3::Header::new(b":path", b"/zc"),
    ];
    let stream_id = hc
        .send_request(&mut c, &req, true)
        .expect("h3 send_request over a Connection<F>");
    gsettle(&mut c, &mut s, 16);

    let mut saw_request = false;
    while let Ok((id, ev)) = hs.poll(&mut s) {
        if let quiche::h3::Event::Headers { .. } = ev {
            assert_eq!(id, stream_id, "server saw headers on an unexpected stream");
            saw_request = true;
        }
    }
    assert!(saw_request, "server never polled the request headers");

    hs.send_response(
        &mut s,
        stream_id,
        &[quiche::h3::Header::new(b":status", b"200")],
        false,
    )
    .expect("h3 send_response over a Connection<F>");

    let mut buf: F::Buf = F::buf_from_slice(body);
    let mut reported = 0usize;
    let mut rounds = 0usize;
    while reported < body.len() {
        rounds += 1;
        assert!(rounds <= 4096, "send_body_zc stalled after {rounds} rounds");
        let last = reported + buf.as_ref().len() >= body.len();
        match hs.send_body_zc(&mut s, stream_id, &mut buf, last) {
            Ok(n) => reported += n,
            Err(quiche::h3::Error::Done) => {},
            Err(e) => panic!("send_body_zc: {e:?}"),
        }
        gsettle(&mut c, &mut s, 64);
    }
    gsettle(&mut c, &mut s, 64);

    let mut got = Vec::new();
    let mut scratch = vec![0u8; 65535];
    let mut rounds = 0usize;
    loop {
        rounds += 1;
        assert!(rounds <= 4096, "client never finished reading the body");
        let mut progressed = false;
        while let Ok((id, ev)) = hc.poll(&mut c) {
            progressed = true;
            match ev {
                quiche::h3::Event::Data => {
                    while let Ok(n) = hc.recv_body(&mut c, id, &mut scratch) {
                        got.extend_from_slice(&scratch[..n]);
                    }
                },
                quiche::h3::Event::Finished => return (reported, got),
                _ => {},
            }
        }
        if !progressed && gsettle(&mut c, &mut s, 64) == 0 {
            return (reported, got);
        }
    }
}

/// `Config::set_send_capacity_factor` scales how much the connection will
/// accept into its send buffer in one go.
///
/// Measured as a ratio rather than an absolute: three pairs identical except
/// for the factor, each asked for the same oversized write on a freshly
/// handshaked connection with nothing in flight. The byte count a plain
/// `stream_send` returns there is the send capacity, and the capacity is the
/// underlying window multiplied by the factor, so doubling and quadrupling the
/// factor must double and quadruple the count. Nothing here reads a clock.
#[test]
fn cap_send_capacity_factor() {
    let payload = testkit::body(512 * 1024, 0x13_0005);

    let accepted = |factor: Option<f64>| -> usize {
        let mut cc = pair::client_config(pair::ALPN_HQ);
        let mut sc = pair::server_config(pair::ALPN_HQ);
        if let Some(f) = factor {
            cc.set_send_capacity_factor(f);
            sc.set_send_capacity_factor(f);
        }
        let mut p = pair::Pair::with_configs(&mut cc, &mut sc);
        p.handshake();
        p.client
            .stream_send(4, &payload, false)
            .expect("stream_send on a freshly handshaked connection")
    };

    let unset = accepted(None);
    let one = accepted(Some(1.0));
    let two = accepted(Some(2.0));
    let four = accepted(Some(4.0));

    assert!(one > 0, "a freshly handshaked connection accepted no bytes at all");
    assert!(
        one < payload.len(),
        "the payload is deliberately larger than any of these capacities; \
         factor 1.0 swallowed all {} bytes, so the measurement is not \
         capacity-bound",
        payload.len()
    );
    assert_eq!(
        unset, one,
        "the documented default factor is 1, so leaving it unset and setting \
         it to 1.0 must give the same capacity"
    );
    assert_eq!(
        two,
        2 * one,
        "factor 2.0 accepted {two} bytes where factor 1.0 accepted {one}; the \
         send capacity must scale with the factor"
    );
    assert_eq!(
        four,
        4 * one,
        "factor 4.0 accepted {four} bytes where factor 1.0 accepted {one}"
    );

    // A scaled connection must still be a working connection.
    let mut cc = pair::client_config(pair::ALPN_HQ);
    let mut sc = pair::server_config(pair::ALPN_HQ);
    cc.set_send_capacity_factor(2.0);
    sc.set_send_capacity_factor(2.0);
    let mut p = pair::Pair::with_configs(&mut cc, &mut sc);
    p.handshake();
    let want = testkit::body(128 * 1024, 0x13_0006);
    let (got, fin) = p.bulk_client_to_server(4, &want);
    assert!(got == want, "a connection with a scaled send capacity corrupted its transfer");
    assert!(fin, "server never saw the stream finish");
}

/// `h3::Priority` is copyable, and additional header sections can be sent with
/// a priority attached.
///
/// Two things arrived together at this milestone and this group holds both to
/// their documented contracts. `Priority` gained `Clone` and `Copy`, so a
/// caller can keep using a value it passed by reference. And
/// `send_additional_headers_with_priority` sends a trailing header section
/// *and* reprioritises the stream in one call, clamping the urgency into the
/// 0..=7 the specification allows rather than letting an out-of-range value
/// through -- a value that would overflow the internal urgency offset if it
/// were not clamped.
///
/// The exchange runs over a `Connection<CountingFactory>`, because the whole
/// HTTP/3 API became generic over the buffer factory in the same release.
#[test]
fn cap_h3_priority_helpers() {
    // `Copy`: `base` is still usable after being assigned away, and both
    // copies compare equal to it. This block does not compile if `Priority` is
    // only movable.
    let base = quiche::h3::Priority::new(5, true);
    let copied = base;
    let cloned = base.clone();
    assert_eq!(base, copied, "a copied Priority must equal its source");
    assert_eq!(base, cloned, "a cloned Priority must equal its source");
    assert_eq!(
        quiche::h3::Priority::default(),
        quiche::h3::Priority::new(3, false),
        "the documented defaults are urgency 3, non-incremental"
    );
    assert_ne!(
        quiche::h3::Priority::new(5, true),
        quiche::h3::Priority::new(5, false),
        "the incremental flag is part of a Priority's identity"
    );

    let (mut c, mut s) = gpair::<CountingFactory, CountingFactory>(pair::ALPN_H3);
    ghandshake(&mut c, &mut s);
    let cfg = pair::h3_config();
    let mut hc = quiche::h3::Connection::with_transport(&mut c, &cfg)
        .expect("h3::Connection::with_transport on the client");
    let mut hs = quiche::h3::Connection::with_transport(&mut s, &cfg)
        .expect("h3::Connection::with_transport on the server");
    gsettle(&mut c, &mut s, 16);

    let req = vec![
        quiche::h3::Header::new(b":method", b"GET"),
        quiche::h3::Header::new(b":scheme", b"https"),
        quiche::h3::Header::new(b":authority", b"quic.test"),
        quiche::h3::Header::new(b":path", b"/prio"),
    ];
    let stream_id = hc.send_request(&mut c, &req, true).expect("send_request");
    gsettle(&mut c, &mut s, 16);
    while hs.poll(&mut s).is_ok() {}

    let trailers = vec![
        quiche::h3::Header::new(b"x-checksum", b"0f1e2d3c"),
        quiche::h3::Header::new(b"x-trailer", b"yes"),
    ];

    // Before the initial HEADERS have been sent there is no stream to add a
    // section to, and the call must say so rather than inventing one.
    assert_eq!(
        hs.send_additional_headers_with_priority(
            &mut s, stream_id, &trailers, &base, true, true
        ),
        Err(quiche::h3::Error::FrameUnexpected),
        "additional headers before the initial HEADERS must be refused"
    );

    hs.send_response(
        &mut s,
        stream_id,
        &[quiche::h3::Header::new(b":status", b"200")],
        false,
    )
    .expect("send_response");

    // An urgency far outside the specification's 0..=7 must be clamped, not
    // rejected and not passed through.
    let wild = quiche::h3::Priority::new(200, false);
    assert_eq!(
        hs.send_additional_headers_with_priority(
            &mut s, stream_id, &trailers, &wild, true, true
        ),
        Ok(()),
        "an out-of-range urgency must be clamped into 0..=7, not refused"
    );

    // Only one trailing section is allowed per stream.
    assert_eq!(
        hs.send_additional_headers_with_priority(
            &mut s, stream_id, &trailers, &base, true, true
        ),
        Err(quiche::h3::Error::FrameUnexpected),
        "a second trailing header section on the same stream must be refused"
    );

    // A client may only send trailers; a non-trailer additional section from
    // the client side is a protocol error regardless of priority.
    assert_eq!(
        hc.send_additional_headers_with_priority(
            &mut c, stream_id, &trailers, &base, false, false
        ),
        Err(quiche::h3::Error::FrameUnexpected),
        "a client may only send trailing header sections"
    );

    gsettle(&mut c, &mut s, 32);

    // The trailers must actually arrive, in order, after the response.
    let mut sections: Vec<Vec<(Vec<u8>, Vec<u8>)>> = Vec::new();
    let mut finished = false;
    for _ in 0..32 {
        let mut progressed = false;
        while let Ok((id, ev)) = hc.poll(&mut c) {
            progressed = true;
            assert_eq!(id, stream_id, "client saw an event on an unexpected stream");
            match ev {
                quiche::h3::Event::Headers { list, .. } => {
                    sections.push(
                        list.iter()
                            .map(|h| (h.name().to_vec(), h.value().to_vec()))
                            .collect(),
                    );
                },
                quiche::h3::Event::Finished => finished = true,
                _ => {},
            }
        }
        if !progressed && gsettle(&mut c, &mut s, 32) == 0 {
            break;
        }
    }
    assert_eq!(
        sections.len(),
        2,
        "expected two header sections at the client -- the response and the \
         trailers -- and observed {}",
        sections.len()
    );
    assert_eq!(
        sections[0],
        vec![(b":status".to_vec(), b"200".to_vec())],
        "the first section must be the response headers"
    );
    let want: Vec<(Vec<u8>, Vec<u8>)> = trailers
        .iter()
        .map(|h| (h.name().to_vec(), h.value().to_vec()))
        .collect();
    assert_eq!(
        sections[1], want,
        "the trailing section did not arrive with the fields it was sent with"
    );
    assert!(finished, "the client never saw the request stream finish");
}

/// The release restructured the repository into an async workspace: four new
/// members alongside the five that were already there.
///
/// Read out of `cargo metadata`, which is the same question a downstream
/// consumer's build asks, and checked against an exact expectation rather than
/// a substring sweep. The member list is a structural fact about the produced
/// tree, which is what T4 scores; nothing here reads a line of the candidate's
/// source.
#[test]
fn cap_async_workspace() {
    let dir = testkit::scratch("cap-async-workspace");
    let root = testkit::candidate_root();

    const NEW: [&str; 4] =
        ["buffer-pool", "datagram-socket", "task-killswitch", "tokio-quiche"];
    const CARRIED: [&str; 5] = ["apps", "h3i", "octets", "qlog", "quiche"];

    for name in NEW.iter().chain(CARRIED.iter()) {
        let manifest = root.join(name).join("Cargo.toml");
        assert!(
            manifest.is_file(),
            "expected a workspace member manifest at {}; the workspace at {} \
             does not have one there",
            manifest.display(),
            root.display()
        );
    }

    let mut c = testkit::cmd("cargo");
    c.arg("metadata")
        .arg("--offline")
        .arg("--no-deps")
        .arg("--format-version")
        .arg("1")
        .arg("--manifest-path")
        .arg(root.join("Cargo.toml"))
        .env("CARGO_TARGET_DIR", dir.join("metadata-target"));
    let meta = testkit::run(&mut c, LONG, &dir, "metadata");
    assert!(
        meta.ok(),
        "expected `cargo metadata` on the candidate workspace at {} to \
         succeed; observed status {:?}.\n--- stderr ---\n{}",
        root.display(),
        meta.status,
        meta.stderr
    );
    let members = workspace_members_text(&meta.stdout).unwrap_or_else(|| {
        panic!(
            "`cargo metadata` produced no workspace_members array ({} bytes \
             of stdout)",
            meta.stdout.len()
        )
    });
    for name in NEW.iter() {
        assert!(
            member_named(members, name),
            "expected the candidate's workspace to have a `{name}` member; \
             cargo reported workspace_members = {members}"
        );
    }
    for name in CARRIED.iter() {
        assert!(
            member_named(members, name),
            "the `{name}` member carried forward from the previous milestone \
             is gone; cargo reported workspace_members = {members}"
        );
    }
    let count = members.matches("path+file").count();
    assert_eq!(
        count,
        NEW.len() + CARRIED.len(),
        "expected exactly {} workspace members and cargo reported {count}: \
         {members}",
        NEW.len() + CARRIED.len()
    );

    // `cargo metadata --no-deps` reports membership and nothing else: not the
    // root manifest's own tables, and not a single dependency edge. The two
    // requirements below are therefore read straight off the manifests, with
    // comment lines stripped first so that a comment mentioning a crate name
    // cannot decide the outcome either way.
    let root_manifest = uncommented(&manifest_text(&root.join("Cargo.toml")));
    for table in ["[workspace.package]", "[workspace.dependencies]"] {
        assert!(
            root_manifest.contains(table),
            "the root manifest at {} does not declare {table}; the members \
             inherit their metadata and their dependency versions from it, and \
             the contract spells the tables out in this form",
            root.join("Cargo.toml").display()
        );
    }
    assert!(
        resolver_is_two(&root_manifest),
        "the root manifest at {} does not set `resolver = \"2\"`",
        root.join("Cargo.toml").display()
    );

    // The asynchronous layer is built on top of the transport crate. The
    // transport crate must not be built on top of it.
    let quiche_manifest = uncommented(&manifest_text(&root.join("quiche").join("Cargo.toml")));
    for name in NEW.iter() {
        assert!(
            !quiche_manifest.contains(name),
            "the transport crate's manifest names `{name}`; `quiche` must not \
             depend on the four crates this milestone adds, in any dependency \
             table -- the asynchronous layer sits above the transport, not \
             beneath it"
        );
    }
}

/// A manifest's text, or a group failure naming the path that is missing.
fn manifest_text(path: &Path) -> String {
    std::fs::read_to_string(path).unwrap_or_else(|e| {
        panic!("could not read the manifest at {}: {e}", path.display())
    })
}

/// `text` with whole-line `#` comments removed. Trailing comments are left
/// alone: a `#` inside a string is legal TOML and stripping from the first
/// `#` on a line would corrupt it.
fn uncommented(text: &str) -> String {
    text.lines()
        .filter(|l| !l.trim_start().starts_with('#'))
        .collect::<Vec<_>>()
        .join("\n")
}

/// Whether a manifest sets the v2 feature resolver, in either of TOML's two
/// string spellings and with any spacing around the `=`.
fn resolver_is_two(text: &str) -> bool {
    text.lines().any(|l| {
        let l = l.trim();
        let Some(rest) = l.strip_prefix("resolver") else {
            return false;
        };
        let rest = rest.trim_start();
        let Some(rest) = rest.strip_prefix('=') else {
            return false;
        };
        let rest = rest.trim();
        rest.starts_with("\"2\"") || rest.starts_with("'2'")
    })
}

/// The `"workspace_members": [ ... ]` array of a `cargo metadata` document, as
/// raw text. Structural extraction only -- the harness has no JSON parser and
/// does not need one to ask which names cargo listed.
fn workspace_members_text(metadata: &str) -> Option<&str> {
    let start = metadata.find("\"workspace_members\":")?;
    let open = metadata[start..].find('[')? + start;
    let close = metadata[open..].find(']')? + open;
    Some(&metadata[open..=close])
}

/// Whether `members` -- the text of a `workspace_members` array -- names a
/// package called `name`. Cargo spells each entry as a package id containing
/// the path to the member's directory, so the test is for the directory
/// component rather than a bare substring, which would match `quiche` inside
/// `tokio-quiche`.
fn member_named(members: &str, name: &str) -> bool {
    members
        .split(',')
        .any(|entry| entry.contains(&format!("/{name}#")) || entry.contains(&format!("/{name}@")))
}

/// The workspace's new async front end actually serves HTTP/3.
///
/// This is the milestone's headline crate, and the only honest way to observe
/// it is to run it: a probe binary outside the candidate's workspace depends on
/// `tokio-quiche` by path, stands up a server with `listen` +
/// `ServerH3Driver` + `ServerH3Connection`, and the candidate's own
/// `quiche-client` fetches from it over a real UDP socket. Two processes, one
/// wire, bytes compared at the end -- nothing here can be satisfied by a type
/// that compiles and does nothing.
///
/// The probe is a separate crate rather than a dependency of this harness on
/// purpose. If the harness itself linked `tokio-quiche`, a candidate who
/// delivered every other group but not this crate would fail to build the
/// harness at all and score zero everywhere; keeping the dependency in a probe
/// confines that failure to this group.
///
/// `CARGO_TARGET_DIR` points at the directory the driver already built the
/// candidate's workspace into. The probe's own dependency graph is that
/// workspace's graph, so nearly every unit it needs is already compiled there
/// and the build is seconds rather than minutes. It is also why this group
/// takes the T1 lock: it is the only group that writes to that directory.
#[test]
fn cap_tokio_quiche() {
    let _guard = testkit::serial_t1();
    let dir = testkit::scratch("cap-tokio-quiche");
    let tls = testkit::tls();

    let want = testkit::body(4096, 0x7c0d_0013);
    let body_file = dir.join("probe-body.bin");
    std::fs::write(&body_file, &want).expect("write probe body fixture");

    let probe = write_tq_probe(&dir);
    let build = build_tq_probe(&dir, &probe);
    assert!(
        build.ok(),
        "expected a crate outside the workspace to be able to depend on the \
         candidate's `tokio-quiche` by path and build against it; the build \
         failed with status {:?}.\n--- cargo stderr ---\n{}",
        build.status,
        build.stderr
    );
    let probe_bin = testkit::candidate_target_dir().join("debug").join("tq-probe");
    assert!(
        probe_bin.is_file(),
        "the probe build reported success but produced no binary at {}",
        probe_bin.display()
    );

    let port = testkit::free_port();
    let mut sc = testkit::cmd(&probe_bin);
    sc.arg(format!("127.0.0.1:{port}"))
        .arg(&tls.cert)
        .arg(&tls.key)
        .arg(&body_file);
    let server = testkit::spawn_server(&mut sc, port, &dir, "tq-probe", LONG);

    let url = format!("https://127.0.0.1:{port}/probe.bin");
    let client_bin = testkit::client_bin();
    let mut cc = testkit::cmd(&client_bin);
    cc.arg("--no-verify").arg("--http-version").arg("HTTP/3").arg(&url);
    let run = testkit::run(&mut cc, LONG, &dir, "tq-client");

    let log = server.log();
    assert!(
        !run.timed_out,
        "expected {} to fetch {url} from the tokio-quiche probe and exit \
         within {LONG:?}; observed: still running at the deadline, killed.\n\
         --- client stderr ---\n{}\n--- probe log ---\n{log}",
        client_bin.display(),
        run.stderr
    );
    assert_eq!(
        run.status.and_then(|s| s.code()),
        Some(0),
        "expected {} to exit 0 after fetching {url} from the tokio-quiche \
         probe; observed status {:?}.\n--- client stderr ---\n{}\n\
         --- probe log ---\n{log}",
        client_bin.display(),
        run.status,
        run.stderr
    );

    // The probe reports the `:path` it was asked for, so a server that answers
    // every stream with the same canned bytes regardless of the request cannot
    // pass this.
    assert!(
        log.contains("path=/probe.bin"),
        "the probe never reported receiving a request for /probe.bin, so the \
         HTTP/3 request never reached the tokio-quiche driver.\n\
         --- probe log ---\n{log}"
    );

    let got = &run.stdout_bytes;
    assert_eq!(
        got.len(),
        want.len(),
        "expected the {} byte response body on the client's stdout; observed \
         {} bytes.\nfirst 32 expected: {}\nfirst 32 observed: {}\n\
         --- client stderr ---\n{}\n--- probe log ---\n{log}",
        want.len(),
        got.len(),
        testkit::hex(&want[..32.min(want.len())]),
        testkit::hex(&got[..32.min(got.len())]),
        run.stderr
    );
    let diff = want.iter().zip(got.iter()).position(|(a, b)| a != b);
    assert!(
        diff.is_none(),
        "the tokio-quiche probe's response body came back altered: first \
         difference at offset {0:?}, expected {1}, observed {2}.\n\
         --- probe log ---\n{log}",
        diff,
        testkit::hex(&want[diff.unwrap_or(0)..(diff.unwrap_or(0) + 16).min(want.len())]),
        testkit::hex(&got[diff.unwrap_or(0)..(diff.unwrap_or(0) + 16).min(got.len())]),
    );

    drop(server);
}

/// The probe server's source. Kept as one literal rather than an asset file so
/// that the recipe it exercises -- `listen`, `ServerH3Driver`,
/// `ServerH3Connection`, `OutboundFrame` -- is readable next to the
/// assertions that depend on it.
const TQ_PROBE_MAIN: &str = r#"use futures::SinkExt;
use futures::StreamExt;
use tokio_quiche::buf_factory::BufFactory;
use tokio_quiche::http3::driver::H3Event;
use tokio_quiche::http3::driver::IncomingH3Headers;
use tokio_quiche::http3::driver::OutboundFrame;
use tokio_quiche::http3::driver::ServerH3Event;
use tokio_quiche::http3::settings::Http3Settings;
use tokio_quiche::listen;
use tokio_quiche::metrics::DefaultMetrics;
use tokio_quiche::quic::SimpleConnectionIdGenerator;
use tokio_quiche::quiche::h3;
use tokio_quiche::quiche::h3::NameValue;
use tokio_quiche::settings::CertificateKind;
use tokio_quiche::settings::Hooks;
use tokio_quiche::settings::QuicSettings;
use tokio_quiche::settings::TlsCertificatePaths;
use tokio_quiche::ConnectionParams;
use tokio_quiche::ServerH3Connection;
use tokio_quiche::ServerH3Driver;

/// argv: <listen-addr> <cert> <key> <body-file>
#[tokio::main(flavor = "current_thread")]
async fn main() {
    let a: Vec<String> = std::env::args().collect();
    let (addr, cert, key) = (a[1].clone(), a[2].clone(), a[3].clone());
    let body = std::fs::read(&a[4]).expect("read body fixture");

    let socket = std::net::UdpSocket::bind(&addr).expect("bind");
    eprintln!("probe listening on {}", socket.local_addr().unwrap());

    let tls = TlsCertificatePaths {
        cert: &cert,
        private_key: &key,
        kind: CertificateKind::X509,
    };
    let params =
        ConnectionParams::new_server(QuicSettings::default(), tls, Hooks::default());
    let mut stream = listen(
        vec![socket],
        params,
        SimpleConnectionIdGenerator,
        DefaultMetrics,
    )
    .expect("listen")
    .remove(0);

    while let Some(Ok(initial)) = stream.next().await {
        let (h3_driver, h3_controller) = ServerH3Driver::new(Http3Settings::default());
        let conn = initial.start(h3_driver);
        let mut h3 = ServerH3Connection::new(conn, h3_controller);
        let body = body.clone();
        tokio::spawn(async move {
            let rx = h3.h3_controller.event_receiver_mut();
            while let Some(ServerH3Event::Core(ev)) = rx.recv().await {
                if let H3Event::IncomingHeaders(IncomingH3Headers {
                    headers, mut send, ..
                }) = ev
                {
                    let path = headers
                        .iter()
                        .find(|h| h.name() == b":path")
                        .map(|h| String::from_utf8_lossy(h.value()).into_owned())
                        .unwrap_or_default();
                    eprintln!("path={path}");
                    send.send(OutboundFrame::Headers(
                        vec![h3::Header::new(b":status", b"200")],
                        None,
                    ))
                    .await
                    .expect("send response headers");
                    send.send(OutboundFrame::body(
                        BufFactory::buf_from_slice(&body),
                        true,
                    ))
                    .await
                    .expect("send response body");
                }
            }
        });
    }
}
"#;

/// Write the probe crate into `dir` and return its root. It is its own
/// workspace (`[workspace]` with no members) so that it does not try to join
/// the candidate's, and it reaches `tokio-quiche` by absolute path.
fn write_tq_probe(dir: &Path) -> PathBuf {
    let probe = dir.join("tq-probe");
    std::fs::create_dir_all(probe.join("src")).expect("create probe crate");
    let tq = testkit::candidate_root().join("tokio-quiche");
    assert!(
        tq.join("Cargo.toml").is_file(),
        "expected the candidate's workspace to contain a `tokio-quiche` crate \
         with a manifest at {}",
        tq.join("Cargo.toml").display()
    );
    std::fs::write(
        probe.join("Cargo.toml"),
        format!(
            "[package]\nname = \"tq-probe\"\nversion = \"0.0.0\"\n\
             edition = \"2021\"\npublish = false\n\n[workspace]\n\n\
             [dependencies]\ntokio-quiche = {{ path = \"{}\" }}\n\
             tokio = {{ version = \"1\", features = [\"rt\", \"macros\"] }}\n\
             futures = \"0.3\"\n",
            tq.display()
        ),
    )
    .expect("write probe Cargo.toml");
    // Seed the probe's lockfile from the candidate workspace's own, when it
    // ships one. Without a lock, `cargo build --offline` resolves every
    // requirement to the newest version present in the image's registry
    // cache. That cache also holds the previous release's graph, which the
    // image fetches unlocked, so a requirement like `libc = "0.2.76"` can
    // resolve above the version this release's lock names -- and one newer
    // `libc`, `serde` or `syn` recompiles everything above it, BoringSSL
    // included, instead of linking against the prewarmed target directory.
    // The candidate's lock names the versions its own workspace build just
    // used, which are exactly the units already in that directory; cargo
    // keeps them and adds only the probe itself.
    let candidate_lock = testkit::candidate_root().join("Cargo.lock");
    if candidate_lock.is_file() {
        std::fs::copy(&candidate_lock, probe.join("Cargo.lock"))
            .expect("seed the probe Cargo.lock from the candidate workspace");
    }
    std::fs::write(probe.join("src/main.rs"), TQ_PROBE_MAIN).expect("write probe main.rs");
    probe
}

fn build_tq_probe(dir: &Path, probe: &Path) -> testkit::Run {
    let mut c = testkit::cmd("cargo");
    c.arg("build")
        .arg("--offline")
        .arg("-j")
        .arg("4")
        .arg("--manifest-path")
        .arg(probe.join("Cargo.toml"))
        .env("CARGO_TARGET_DIR", testkit::candidate_target_dir());
    testkit::run(&mut c, LONG, dir, "tq-probe-build")
}
