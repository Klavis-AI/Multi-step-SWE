//! An in-memory client/server pair built from the candidate's own library:
//! the T3 surface.
//!
//! No sockets, no processes, no timing. A group that wants to know what the
//! congestion controller did, or which error variant came back, or whether a
//! stream delivered its bytes in order, drives this and reads the answer out
//! of the candidate's own types.
//!
//! **This file is per-step.** It is the one part of the harness that touches
//! the candidate's API directly, and that API moves: `recv` grows a second
//! argument, connection IDs stop being `&[u8]`, `send` starts returning pacing
//! information, `Connection` grows a type parameter. Each step's author starts
//! from the previous step's copy and changes what that step changed -- which
//! is a feature, because for several steps the change *is* the milestone.
//!
//! This copy is written against the 0.3.0 surface, which differs from step
//! 01's in three places:
//!
//! ```text
//! quiche::connect(Option<&str>, &[u8], &mut Config) -> Result<Pin<Box<Connection>>>
//! quiche::accept(&[u8], Option<&[u8]>, &mut Config) -> Result<Pin<Box<Connection>>>
//! Connection::recv(&mut [u8]) -> Result<usize>
//! Connection::send(&mut [u8]) -> Result<usize>
//! Config::set_max_idle_timeout(u64)      // was set_idle_timeout
//! ```
//!
//! Connection IDs are still bare `&[u8]`: the `ConnectionId` newtype does not
//! arrive until step 04.

use std::time::Duration;
use std::time::Instant;

use crate::testkit;

// ---------------------------------------------------------------------------
// The milestone shim.
//
// `pair.rs` is linked by `tests/reg.rs` and `tests/gate.rs` as well as by
// `tests/cap.rs`. The first two are gate groups: they assert behaviour
// inherited from step 01, so they have to compile against step 01's release as
// well as this one -- a gate that cannot compile against the previous
// milestone vetoes every candidate that has not done the work yet. `build.rs`
// picks each branch from the candidate's own sources rather than from the
// version string in its manifest.
// ---------------------------------------------------------------------------

/// How the candidate hands a connection back.
///
/// Boxed at step 01, pinned as well from this milestone on. Both deref to
/// `quiche::Connection`, so the rest of the file is written once.
#[cfg(quiche_pinned_conn)]
pub type Conn = std::pin::Pin<Box<quiche::Connection>>;

/// See [`Conn`].
#[cfg(not(quiche_pinned_conn))]
pub type Conn = Box<quiche::Connection>;

/// Set the idle timeout, in milliseconds, under whichever name it has.
///
/// Renamed from `set_idle_timeout` at this milestone; same units, same effect.
/// Public because `tests/reg.rs` configures its own `Config` and so meets the
/// same rename.
#[cfg(quiche_max_idle_timeout)]
pub fn set_idle(c: &mut quiche::Config, ms: u64) {
    c.set_max_idle_timeout(ms);
}

/// See [`set_idle`].
#[cfg(not(quiche_max_idle_timeout))]
pub fn set_idle(c: &mut quiche::Config, ms: u64) {
    c.set_idle_timeout(ms);
}

/// The largest datagram either side will be asked to produce. Comfortably
/// above every milestone's default maximum packet size, so the pair never
/// truncates a packet the candidate wanted to send.
pub const MAX_DATAGRAM: usize = 1452;

/// Wire-format ALPN for this milestone's HTTP-over-QUIC mapping.
pub const ALPN_HQ: &[u8] = b"\x05hq-27";

/// Wire-format ALPN for this milestone's HTTP/3.
pub const ALPN_H3: &[u8] = b"\x05h3-27";

/// The client's source connection ID in every [`Pair`]. Fixed, not random:
/// a failing group should replay identically.
pub const CLIENT_CID: [u8; quiche::MAX_CONN_ID_LEN] = [0x11; quiche::MAX_CONN_ID_LEN];

/// The server's source connection ID in every [`Pair`].
pub const SERVER_CID: [u8; quiche::MAX_CONN_ID_LEN] = [0x22; quiche::MAX_CONN_ID_LEN];

/// A client `Config` with the verifier's defaults: peer verification off
/// (the fixture certificate is self-signed), generous flow-control windows,
/// and a short idle timeout so a wedged group fails rather than hangs.
pub fn client_config(alpn: &[u8]) -> quiche::Config {
    let mut c = quiche::Config::new(quiche::PROTOCOL_VERSION).expect("Config::new");
    c.set_application_protos(alpn).expect("set_application_protos");
    c.verify_peer(false);
    tune(&mut c);
    c
}

/// A server `Config` using the verifier's own certificate and key. See
/// [`testkit::tls`] for why they are the verifier's and not the candidate's.
pub fn server_config(alpn: &[u8]) -> quiche::Config {
    let tls = testkit::tls();
    let mut c = quiche::Config::new(quiche::PROTOCOL_VERSION).expect("Config::new");
    c.set_application_protos(alpn).expect("set_application_protos");
    c.load_cert_chain_from_pem_file(tls.cert.to_str().expect("utf-8 cert path"))
        .expect("load_cert_chain_from_pem_file");
    c.load_priv_key_from_pem_file(tls.key.to_str().expect("utf-8 key path"))
        .expect("load_priv_key_from_pem_file");
    tune(&mut c);
    c
}

/// Limits shared by both sides. Deliberately large: a group that wants to
/// observe flow control sets its own smaller limit, and every other group
/// should never hit one by accident.
fn tune(c: &mut quiche::Config) {
    set_idle(c, 30_000);
    c.set_max_packet_size(MAX_DATAGRAM as u64);
    c.set_initial_max_data(10_000_000);
    c.set_initial_max_stream_data_bidi_local(1_000_000);
    c.set_initial_max_stream_data_bidi_remote(1_000_000);
    c.set_initial_max_stream_data_uni(1_000_000);
    c.set_initial_max_streams_bidi(100);
    c.set_initial_max_streams_uni(100);
}

/// Two connections and the datagrams between them.
pub struct Pair {
    /// The client side.
    pub client: Conn,
    /// The server side.
    pub server: Conn,
    /// Datagrams the client has produced, most recent last. A T2-flavoured
    /// group can inspect these without a socket.
    pub client_out: Vec<Vec<u8>>,
    /// Datagrams the server has produced, most recent last.
    pub server_out: Vec<Vec<u8>>,
}

impl Pair {
    /// A fresh pair with the verifier's default configs.
    pub fn new(alpn: &[u8]) -> Pair {
        Pair::with_configs(&mut client_config(alpn), &mut server_config(alpn))
    }

    /// A fresh pair with configs the group built itself. Connection IDs are
    /// fixed, not random: a failing group should replay identically.
    pub fn with_configs(client_cfg: &mut quiche::Config, server_cfg: &mut quiche::Config) -> Pair {
        let client = quiche::connect(Some("quic.test"), &CLIENT_CID, client_cfg)
            .expect("quiche::connect");
        let server = quiche::accept(&SERVER_CID, None, server_cfg).expect("quiche::accept");
        Pair { client, server, client_out: Vec::new(), server_out: Vec::new() }
    }

    /// Move every datagram each side currently wants to send to the other,
    /// once in each direction. Returns how many datagrams crossed.
    pub fn exchange(&mut self) -> usize {
        let a = flush(&mut self.client, &mut self.server, &mut self.client_out);
        let b = flush(&mut self.server, &mut self.client, &mut self.server_out);
        a + b
    }

    /// Exchange until neither side has anything more to say, or `rounds` have
    /// passed. Returns the number of rounds actually used.
    pub fn settle(&mut self, rounds: usize) -> usize {
        for i in 0..rounds {
            if self.exchange() == 0 {
                return i;
            }
        }
        rounds
    }

    /// Complete the handshake. Fails the group -- with both trace ids and the
    /// round count -- rather than looping forever if it does not converge.
    pub fn handshake(&mut self) {
        for _ in 0..40 {
            if self.client.is_established() && self.server.is_established() {
                return;
            }
            if self.exchange() == 0 {
                break;
            }
        }
        assert!(
            self.client.is_established() && self.server.is_established(),
            "handshake did not complete: client established={} closed={}, \
             server established={} closed={} (client sent {} datagrams, server sent {})",
            self.client.is_established(),
            self.client.is_closed(),
            self.server.is_established(),
            self.server.is_closed(),
            self.client_out.len(),
            self.server_out.len()
        );
    }

    /// Send `data` on `stream_id` from the client and read whatever the server
    /// makes of it, settling the connection in between. Returns the bytes the
    /// server read and whether it saw the stream finish.
    pub fn client_to_server(&mut self, stream_id: u64, data: &[u8], fin: bool) -> (Vec<u8>, bool) {
        let n = self.client.stream_send(stream_id, data, fin).expect("client stream_send");
        assert_eq!(n, data.len(), "client stream_send accepted {n} of {} bytes", data.len());
        self.settle(64);
        read_stream(&mut self.server, stream_id)
    }

    /// The mirror of [`Pair::client_to_server`].
    pub fn server_to_client(&mut self, stream_id: u64, data: &[u8], fin: bool) -> (Vec<u8>, bool) {
        let n = self.server.stream_send(stream_id, data, fin).expect("server stream_send");
        assert_eq!(n, data.len(), "server stream_send accepted {n} of {} bytes", data.len());
        self.settle(64);
        read_stream(&mut self.client, stream_id)
    }

    /// Drive both sides' timers forward by sleeping and calling `on_timeout`
    /// where one has expired. Used only by the groups whose subject is a
    /// timeout; everything else should settle instead.
    pub fn tick(&mut self, budget: Duration) {
        let end = Instant::now() + budget;
        while Instant::now() < end {
            let next = [self.client.timeout(), self.server.timeout()]
                .into_iter()
                .flatten()
                .min()
                .unwrap_or(Duration::from_millis(10));
            let nap = next.min(end.saturating_duration_since(Instant::now())).max(Duration::from_millis(1));
            std::thread::sleep(nap);
            if self.client.timeout() == Some(Duration::ZERO) || self.client.timeout().is_none() {
                self.client.on_timeout();
            }
            if self.server.timeout() == Some(Duration::ZERO) || self.server.timeout().is_none() {
                self.server.on_timeout();
            }
            self.exchange();
            if self.client.is_closed() && self.server.is_closed() {
                return;
            }
        }
    }
}

/// Drain everything `from` wants to send into `to`, recording the datagrams.
///
/// `Error::Done` is the loop's terminator on both sides and is not an error.
/// Anything else is: a group whose pair silently stopped exchanging packets
/// would report a timeout somewhere far away from the cause.
fn flush(
    from: &mut quiche::Connection, to: &mut quiche::Connection, log: &mut Vec<Vec<u8>>,
) -> usize {
    let mut buf = [0u8; MAX_DATAGRAM];
    let mut n = 0;
    loop {
        let len = match from.send(&mut buf) {
            Ok(v) => v,
            Err(quiche::Error::Done) => return n,
            Err(e) => panic!("send on {}: {e:?}", from.trace_id()),
        };
        log.push(buf[..len].to_vec());
        n += 1;
        let mut d = buf[..len].to_vec();
        match to.recv(&mut d) {
            Ok(_) => {},
            Err(quiche::Error::Done) => {},
            Err(e) => panic!("recv on {}: {e:?} ({len} byte datagram)", to.trace_id()),
        }
        if n > 4096 {
            panic!("flush did not terminate: {n} datagrams from {}", from.trace_id());
        }
    }
}

/// Read everything readable on one stream.
pub fn read_stream(conn: &mut quiche::Connection, stream_id: u64) -> (Vec<u8>, bool) {
    let mut out = Vec::new();
    let mut fin = false;
    let mut buf = [0u8; 65535];
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
            Err(e) => panic!("stream_recv {stream_id} on {}: {e:?}", conn.trace_id()),
        }
    }
    (out, fin)
}
