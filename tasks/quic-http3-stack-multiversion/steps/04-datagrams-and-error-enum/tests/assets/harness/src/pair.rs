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
//! This copy is written against the 0.7.0 surface. Three things moved since
//! 0.1.0 and all three are visible here:
//!
//! ```text
//! quiche::connect(Option<&str>, &ConnectionId, &mut Config)
//!     -> Result<Pin<Box<Connection>>>
//! quiche::accept(&ConnectionId, Option<&ConnectionId>, &mut Config)
//!     -> Result<Pin<Box<Connection>>>
//! Connection::recv(&mut [u8]) -> Result<usize>
//! Connection::send(&mut [u8]) -> Result<usize>
//! Config::enable_dgram(bool, usize, usize)
//! ```
//!
//! Connection IDs are `quiche::ConnectionId` rather than `&[u8]`, the
//! constructors return a pinned box, and DATAGRAM support is switched on
//! through `Config`, not through `Connection`.
//!
//! This copy also carries [`keylog`] support, because the DATAGRAM groups have
//! to read a 1-RTT packet and the verifier's own crypto can only do that if it
//! is given the traffic secret. The secret comes out of the candidate through
//! the standard NSS keylog hook, and the decryption is done by
//! `quiche_verifier_support` -- never by the candidate.

use std::pin::Pin;
use std::sync::Arc;
use std::sync::Mutex;
use std::time::Duration;
use std::time::Instant;

use quiche_verifier_support as support;

use crate::testkit;

// ---------------------------------------------------------------------------
// The milestone shim.
//
// This is the step that wraps connection IDs in a `ConnectionId` type and
// renames the datagram-size setter. `pair.rs` is linked by `tests/reg.rs` and
// `tests/gate.rs`, which are gate groups asserting behaviour inherited from
// earlier steps, so it has to compile against the previous milestone too; a
// gate that cannot vetoes every candidate that has not done the work yet.
// `build.rs` picks each branch from the candidate's own sources.
// ---------------------------------------------------------------------------

/// `quiche::connect`, taking the source connection ID as plain bytes.
///
/// Before this milestone the argument *was* `&[u8]`; from here on it is a
/// `ConnectionId` borrowed from the same bytes.
#[cfg(quiche_connection_id)]
fn connect_with(
    name: Option<&str>, scid: &[u8], cfg: &mut quiche::Config,
) -> quiche::Result<Pin<Box<quiche::Connection>>> {
    let scid = quiche::ConnectionId::from_ref(scid);
    quiche::connect(name, &scid, cfg)
}

/// See [`connect_with`].
#[cfg(not(quiche_connection_id))]
fn connect_with(
    name: Option<&str>, scid: &[u8], cfg: &mut quiche::Config,
) -> quiche::Result<Pin<Box<quiche::Connection>>> {
    quiche::connect(name, scid, cfg)
}

/// `quiche::accept`, taking the connection IDs as plain bytes.
#[cfg(quiche_connection_id)]
fn accept_with(
    scid: &[u8], odcid: Option<&[u8]>, cfg: &mut quiche::Config,
) -> quiche::Result<Pin<Box<quiche::Connection>>> {
    let scid = quiche::ConnectionId::from_ref(scid);
    let odcid = odcid.map(quiche::ConnectionId::from_ref);
    quiche::accept(&scid, odcid.as_ref(), cfg)
}

/// See [`accept_with`].
#[cfg(not(quiche_connection_id))]
fn accept_with(
    scid: &[u8], odcid: Option<&[u8]>, cfg: &mut quiche::Config,
) -> quiche::Result<Pin<Box<quiche::Connection>>> {
    quiche::accept(scid, odcid, cfg)
}

/// Cap the datagram size, under whichever name the setter has.
///
/// `set_max_packet_size` became `set_max_udp_payload_size` and then split into
/// a send and a receive half; this milestone is where the receive half arrives.
#[cfg(quiche_udp_payload_size)]
fn set_udp_payload(c: &mut quiche::Config, n: usize) {
    c.set_max_recv_udp_payload_size(n);
}

/// See [`set_udp_payload`].
#[cfg(not(quiche_udp_payload_size))]
fn set_udp_payload(c: &mut quiche::Config, n: usize) {
    c.set_max_udp_payload_size(n as u64);
}

/// The largest datagram either side will be asked to produce. Comfortably
/// above every milestone's default maximum packet size, so the pair never
/// truncates a packet the candidate wanted to send.
pub const MAX_DATAGRAM: usize = 1452;

/// Wire-format ALPN for this milestone's HTTP-over-QUIC mapping. The
/// milestone is draft-29, so the token is `hq-29`.
pub const ALPN_HQ: &[u8] = b"\x05hq-29";

/// Wire-format ALPN for this milestone's HTTP/3, matching
/// `quiche::h3::APPLICATION_PROTOCOL`'s first entry.
pub const ALPN_H3: &[u8] = b"\x05h3-29";

/// The client's source connection ID, and therefore the Destination
/// Connection ID on every short-header packet the *server* sends.
pub const CLIENT_CID: [u8; quiche::MAX_CONN_ID_LEN] = [0x11; quiche::MAX_CONN_ID_LEN];

/// The server's source connection ID, and therefore the Destination
/// Connection ID on every short-header packet the *client* sends. A T2-style
/// group needs its length to find the packet number offset.
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
    c.set_max_idle_timeout(30_000);
    set_udp_payload(c, MAX_DATAGRAM);
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
    pub client: Pin<Box<quiche::Connection>>,
    /// The server side.
    pub server: Pin<Box<quiche::Connection>>,
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
        let client =
            connect_with(Some("quic.test"), &CLIENT_CID, client_cfg).expect("quiche::connect");
        let server = accept_with(&SERVER_CID, None, server_cfg).expect("quiche::accept");
        Pair { client, server, client_out: Vec::new(), server_out: Vec::new() }
    }

    /// A pair whose client writes its TLS secrets into the returned buffer.
    ///
    /// `client_cfg` must already have had `log_keys()` called on it; this is
    /// not done here because a `Config` is the caller's to shape. The buffer
    /// fills during [`Pair::handshake`] and is read with [`keylog_secret`].
    pub fn with_keylog(
        client_cfg: &mut quiche::Config, server_cfg: &mut quiche::Config,
    ) -> (Pair, Keylog) {
        let mut p = Pair::with_configs(client_cfg, server_cfg);
        let log = Keylog::default();
        p.client.set_keylog(Box::new(log.clone()));
        (p, log)
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


// ---------------------------------------------------------------------------
// Keylog capture and 1-RTT decryption
// ---------------------------------------------------------------------------

/// A shared, thread-safe sink for the candidate's NSS-format keylog lines.
///
/// `Connection::set_keylog` wants a `Box<dyn Write + Send + Sync>` and hands
/// back nothing, so the buffer has to be shared rather than owned. Cloning a
/// `Keylog` clones the handle, not the contents.
#[derive(Clone, Debug, Default)]
pub struct Keylog(Arc<Mutex<Vec<u8>>>);

impl Keylog {
    /// The bytes written so far.
    pub fn contents(&self) -> Vec<u8> {
        self.0.lock().expect("keylog mutex").clone()
    }
}

impl std::io::Write for Keylog {
    fn write(&mut self, buf: &[u8]) -> std::io::Result<usize> {
        self.0.lock().expect("keylog mutex").extend_from_slice(buf);
        Ok(buf.len())
    }

    fn flush(&mut self) -> std::io::Result<()> {
        Ok(())
    }
}

/// Pull one secret out of an NSS keylog by its label.
///
/// The format is `LABEL <client_random_hex> <secret_hex>`, one record per
/// line. Only the first record for a label is returned, which is what a
/// single connection with no key update produces.
pub fn keylog_secret(log: &[u8], label: &str) -> Option<Vec<u8>> {
    let text = String::from_utf8_lossy(log);
    for line in text.lines() {
        let mut f = line.split_whitespace();
        if f.next() != Some(label) {
            continue;
        }
        let _client_random = f.next()?;
        let secret = f.next()?;
        return unhex(secret);
    }
    None
}

/// Decode a lowercase or uppercase hex string.
fn unhex(s: &str) -> Option<Vec<u8>> {
    if s.len() % 2 != 0 {
        return None;
    }
    (0..s.len())
        .step_by(2)
        .map(|i| u8::from_str_radix(&s[i..i + 2], 16).ok())
        .collect()
}

/// Decrypt one short-header (1-RTT) packet with the verifier's own crypto.
///
/// `dcid_len` is the length of the Destination Connection ID the packet
/// carries -- the receiver's own connection ID, which the pair fixes at
/// [`CLIENT_CID`] and [`SERVER_CID`]. `largest_acked` seeds packet number
/// decoding; `None` is right for the first packets of a connection.
///
/// Returns the decrypted frame payload. Every step of this -- header
/// protection, packet number recovery, AEAD -- is
/// `quiche_verifier_support`'s, so a frame found in the result is a fact
/// about the candidate's wire image and not about its parser.
pub fn decrypt_1rtt(
    packet: &[u8], dcid_len: usize, keys: &support::PacketKeys, largest_acked: Option<u64>,
) -> Option<Vec<u8>> {
    if support::is_long_header(*packet.first()?) {
        return None;
    }

    let hdr = support::ShortHeader::parse(packet, dcid_len).ok()?;
    let pn_offset = hdr.pn_offset;

    let mut pkt = packet.to_vec();
    let pn = support::remove_header_protection(keys, &mut pkt, pn_offset).ok()?;
    let packet_number = support::decode_packet_number(largest_acked, pn.truncated, pn.len);

    let payload_offset = pn_offset + pn.len;
    let aad = &pkt[..payload_offset];
    let ciphertext = &pkt[payload_offset..];

    keys.open(packet_number, aad, ciphertext).ok()
}

/// Every DATAGRAM frame payload in a decrypted 1-RTT payload.
///
/// Frame iteration stops at the first frame the support crate cannot decode;
/// that is deliberate there, and it is why this returns what it found rather
/// than insisting the whole payload parsed.
pub fn datagram_payloads(frame_payload: &[u8]) -> Vec<Vec<u8>> {
    let mut out = Vec::new();
    for f in support::frames(frame_payload) {
        match f {
            Ok(support::Frame::Datagram { data, .. }) => out.push(data.to_vec()),
            Ok(_) => {},
            Err(_) => break,
        }
    }
    out
}
