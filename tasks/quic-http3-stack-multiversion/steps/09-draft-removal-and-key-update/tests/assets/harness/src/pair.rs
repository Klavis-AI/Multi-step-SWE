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
//! information, a connection stops being pinned. Each step's author starts
//! from the previous step's copy and changes what that step changed -- which
//! is a feature, because for several steps the change *is* the milestone.
//!
//! This copy is written against the 0.18.0 surface. Nothing in the hot path
//! changed shape between 0.16.0 and 0.18.0, so the signatures below are the
//! ones step 08 established and the pair itself is step 08's, unchanged:
//!
//! ```text
//! quiche::connect(Option<&str>, &ConnectionId, SocketAddr, SocketAddr,
//!                &mut Config)
//!     -> Result<Connection>
//! quiche::accept(&ConnectionId, Option<&ConnectionId>, SocketAddr, SocketAddr,
//!                &mut Config) -> Result<Connection>
//! Connection::recv(&mut [u8], RecvInfo { from, to }) -> Result<usize>
//! Connection::send(&mut [u8]) -> Result<(usize, SendInfo)>
//! Connection::send_quantum() -> usize
//! Connection::peer_cert() -> Option<&[u8]>
//! ```
//!
//! A connection is now an ordinary owned value. The pair therefore stores
//! `quiche::Connection` directly rather than `Pin<Box<quiche::Connection>>`,
//! and a group that wants to prove the connection is movable can move one out
//! of [`Pair`] and keep using it.
//!
//! A connection knows the address of its peer, so the pair has to invent two:
//! [`client_addr`] and [`server_addr`]. They are ordinary loopback addresses
//! that nothing ever binds -- the datagrams are moved by [`Pair::exchange`],
//! not by the kernel -- but they are what the candidate is told, and therefore
//! what `SendInfo.to` has to echo back.
//!
//! Every `SendInfo` either side produces is kept, in order, beside the
//! datagram it described: [`Pair::client_info`] and [`Pair::server_info`].
//!
//! This copy also carries [`Keylog`] support, because a group that has to read
//! a 1-RTT packet can only do so if the verifier's own crypto is given the
//! traffic secret. The secret comes out of the candidate through the standard
//! NSS keylog hook, and the decryption is done by `quiche_verifier_support` --
//! never by the candidate.
//!
//! What *this* copy adds is on top of that keylog support: the verifier can now
//! *write* a 1-RTT packet as well as read one. [`seal_1rtt`] builds a
//! short-header packet with the verifier's own AEAD and header protection, and
//! [`seal_1rtt_phase`] does the same with the key-phase bit under the caller's
//! control. [`open_1rtt`] is [`decrypt_1rtt`] with the header bits kept, so a
//! group can ask what key phase a packet the candidate wrote was sent under.
//! This milestone's key update is the reason all three exist: there is no
//! public API on `Connection` for *initiating* one, so the only way to observe
//! the responder is to be the peer that initiates it, and being that peer
//! means protecting a packet with a key the candidate never handed over.
//!
//! Finally, this copy adds [`H3Pair`]: the same two transport connections with
//! an HTTP/3 connection bolted on each end through the public
//! `quiche::h3::Connection::with_transport` constructor. This milestone's
//! HTTP/3 additions -- PRIORITY_UPDATE and the events it raises -- are only
//! reachable from there, and every step after this one will want it too.

use std::net::Ipv4Addr;
use std::net::SocketAddr;
use std::net::SocketAddrV4;
use std::sync::Arc;
use std::sync::Mutex;
use std::time::Duration;
use std::time::Instant;

use quiche_verifier_support as support;

use crate::testkit;

/// The largest datagram either side will be asked to produce. Comfortably
/// above every milestone's default maximum packet size, so the pair never
/// truncates a packet the candidate wanted to send.
pub const MAX_DATAGRAM: usize = 1452;

/// Wire-format ALPN for this milestone's byte-stream mapping over QUIC
/// version 1. `hq-interop` is the token the apps offer first at this release;
/// the `hq-29` family is still accepted beside it.
pub const ALPN_HQ: &[&[u8]] = &[b"hq-interop"];

/// Wire-format ALPN for this milestone's HTTP/3, matching the first entry of
/// `quiche::h3::APPLICATION_PROTOCOL`.
pub const ALPN_H3: &[&[u8]] = &[b"h3"];

/// The client's source connection ID, and therefore the Destination
/// Connection ID on every short-header packet the *server* sends.
pub const CLIENT_CID: [u8; quiche::MAX_CONN_ID_LEN] = [0x11; quiche::MAX_CONN_ID_LEN];

/// The server's source connection ID, and therefore the Destination
/// Connection ID on every short-header packet the *client* sends. A T2-style
/// group needs its length to find the packet number offset.
pub const SERVER_CID: [u8; quiche::MAX_CONN_ID_LEN] = [0x22; quiche::MAX_CONN_ID_LEN];

/// The transport stream the client's HTTP/3 control stream lands on.
///
/// `h3::Connection::with_transport` opens the control stream before anything
/// else, so it is the client's first client-initiated unidirectional stream.
pub const H3_CLIENT_CONTROL_STREAM: u64 = 2;

/// The transport stream the server's HTTP/3 control stream lands on, by the
/// same argument.
pub const H3_SERVER_CONTROL_STREAM: u64 = 3;

/// The address the pair tells the candidate its client lives at.
///
/// Nothing binds it. It exists because `quiche::accept` takes the peer address
/// as an argument at this milestone, and because `SendInfo.to` on the server
/// side has to come back equal to it.
pub fn client_addr() -> SocketAddr {
    SocketAddr::V4(SocketAddrV4::new(Ipv4Addr::new(127, 0, 0, 1), 44301))
}

/// The address the pair tells the candidate its server lives at. See
/// [`client_addr`].
pub fn server_addr() -> SocketAddr {
    SocketAddr::V4(SocketAddrV4::new(Ipv4Addr::new(127, 0, 0, 1), 44302))
}

/// The second local address the client migrates to.
///
/// A different port on the same loopback host, which is exactly the shape of
/// the migration the reference client performs with `--perform-migration`:
/// a second UDP socket, same interface, new source port. Nothing binds it --
/// the pair has no sockets -- so the port only has to differ from
/// [`client_addr`].
pub fn client_addr_2() -> SocketAddr {
    SocketAddr::V4(SocketAddrV4::new(Ipv4Addr::new(127, 0, 0, 1), 44303))
}

/// An address neither end has ever seen, for the groups that check a path API
/// refuses a four-tuple it does not know.
pub fn unknown_addr() -> SocketAddr {
    SocketAddr::V4(SocketAddrV4::new(Ipv4Addr::new(127, 0, 0, 1), 44999))
}

/// A client `Config` with the verifier's defaults: peer verification off
/// (the fixture certificate is self-signed), generous flow-control windows,
/// and a short idle timeout so a wedged group fails rather than hangs.
pub fn client_config(alpn: &[&[u8]]) -> quiche::Config {
    let mut c = quiche::Config::new(quiche::PROTOCOL_VERSION).expect("Config::new");
    c.set_application_protos(alpn).expect("set_application_protos");
    c.verify_peer(false);
    tune(&mut c);
    c
}

/// A server `Config` using the verifier's own certificate and key. See
/// [`testkit::tls`] for why they are the verifier's and not the candidate's.
pub fn server_config(alpn: &[&[u8]]) -> quiche::Config {
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

/// [`client_config`], with the ALPN list given in the packed wire format --
/// each name preceded by its own length byte, as TLS carries it.
///
/// This milestone renamed the old `set_application_protos` to
/// `set_application_protos_wire_format` and gave the old name the list form,
/// so both spellings exist and a group that cares checks both.
pub fn client_config_wire_format(alpn: &[u8]) -> quiche::Config {
    let mut c = quiche::Config::new(quiche::PROTOCOL_VERSION).expect("Config::new");
    c.set_application_protos_wire_format(alpn)
        .expect("set_application_protos_wire_format");
    c.verify_peer(false);
    tune(&mut c);
    c
}

/// [`server_config`], with the ALPN list given in the packed wire format.
pub fn server_config_wire_format(alpn: &[u8]) -> quiche::Config {
    let tls = testkit::tls();
    let mut c = quiche::Config::new(quiche::PROTOCOL_VERSION).expect("Config::new");
    c.set_application_protos_wire_format(alpn)
        .expect("set_application_protos_wire_format");
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
    c.set_max_recv_udp_payload_size(MAX_DATAGRAM);
    c.set_max_send_udp_payload_size(MAX_DATAGRAM);
    c.set_initial_max_data(10_000_000);
    c.set_initial_max_stream_data_bidi_local(1_000_000);
    c.set_initial_max_stream_data_bidi_remote(1_000_000);
    c.set_initial_max_stream_data_uni(1_000_000);
    c.set_initial_max_streams_bidi(100);
    c.set_initial_max_streams_uni(100);
    // Already the default at this milestone, set explicitly because the whole
    // path API depends on it: with a limit below 2 there is never a spare
    // connection ID, and `probe_path` can only ever return OutOfIdentifiers.
    c.set_active_connection_id_limit(2);
}

/// Two connections, the datagrams between them, and the descriptors that came
/// back with those datagrams.
pub struct Pair {
    /// The client side. An owned `quiche::Connection`: this milestone stopped
    /// handing connections back pinned and boxed.
    pub client: quiche::Connection,
    /// The server side.
    pub server: quiche::Connection,
    /// Datagrams the client has produced, most recent last. A T2-flavoured
    /// group can inspect these without a socket.
    pub client_out: Vec<Vec<u8>>,
    /// Datagrams the server has produced, most recent last.
    pub server_out: Vec<Vec<u8>>,
    /// The `SendInfo` the client returned beside each entry of
    /// [`Pair::client_out`], same order, same length.
    pub client_info: Vec<quiche::SendInfo>,
    /// The `SendInfo` the server returned beside each entry of
    /// [`Pair::server_out`], same order, same length.
    pub server_info: Vec<quiche::SendInfo>,
}

impl Pair {
    /// A fresh pair with the verifier's default configs.
    pub fn new(alpn: &[&[u8]]) -> Pair {
        Pair::with_configs(&mut client_config(alpn), &mut server_config(alpn))
    }

    /// A fresh pair with configs the group built itself. Connection IDs and
    /// addresses are fixed, not random: a failing group should replay
    /// identically.
    pub fn with_configs(client_cfg: &mut quiche::Config, server_cfg: &mut quiche::Config) -> Pair {
        let ccid = quiche::ConnectionId::from_ref(&CLIENT_CID);
        let scid = quiche::ConnectionId::from_ref(&SERVER_CID);
        let client =
            quiche::connect(Some("quic.test"), &ccid, client_addr(), server_addr(), client_cfg)
                .expect("quiche::connect");
        let server = quiche::accept(&scid, None, server_addr(), client_addr(), server_cfg)
            .expect("quiche::accept");
        Pair {
            client,
            server,
            client_out: Vec::new(),
            server_out: Vec::new(),
            client_info: Vec::new(),
            server_info: Vec::new(),
        }
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
        let a = flush(
            &mut self.client,
            &mut self.server,
            &mut self.client_out,
            &mut self.client_info,
        );
        let b = flush(
            &mut self.server,
            &mut self.client,
            &mut self.server_out,
            &mut self.server_info,
        );
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

    /// Give each end a spare connection ID and drive the exchange until both
    /// have learnt the other's. Returns the two sequence numbers issued.
    ///
    /// **Call this before any `probe_path`.** A path is identified on the wire
    /// by its connection ID, so a second path needs a spare one; until both
    /// ends have issued one *and* the NEW_CONNECTION_ID frames have actually
    /// crossed, `probe_path` returns `Error::OutOfIdentifiers`. Doing the
    /// dance by hand in every group would be four lines of noise repeated
    /// eight times, and getting the "and then settle" half wrong is the most
    /// common way a first migration test fails for a reason that has nothing
    /// to do with migration.
    pub fn exchange_cids(&mut self) -> (u64, u64) {
        let (c_cid, c_token) = spare_cid(0x31);
        let (s_cid, s_token) = spare_cid(0x41);
        let c_seq = self
            .client
            .new_source_cid(&c_cid, c_token, true)
            .expect("client new_source_cid");
        let s_seq = self
            .server
            .new_source_cid(&s_cid, s_token, true)
            .expect("server new_source_cid");
        self.settle(16);
        assert!(
            self.client.available_dcids() > 0 && self.server.available_dcids() > 0,
            "connection IDs did not cross: client has {} spare dcids, server has {}",
            self.client.available_dcids(),
            self.server.available_dcids()
        );
        (c_seq, s_seq)
    }

    /// Probe `local` from the client, settle, and assert the path validated on
    /// both ends. Returns the events each end reported, oldest first.
    ///
    /// The two ends do not see the same thing and that asymmetry is scored, so
    /// this returns both lists rather than a single verdict.
    pub fn probe_and_settle(&mut self, local: SocketAddr) -> (Vec<quiche::PathEvent>, Vec<quiche::PathEvent>) {
        self.client.probe_path(local, server_addr()).expect("probe_path");
        self.settle(32);
        (drain_events(&mut self.client), drain_events(&mut self.server))
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
            let nap = next
                .min(end.saturating_duration_since(Instant::now()))
                .max(Duration::from_millis(1));
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

/// Drain everything `from` wants to send into `to`, recording the datagrams
/// and the descriptors that came with them.
///
/// The datagram is delivered to whichever path the *sender* said it was for:
/// `RecvInfo` is built from the `SendInfo` that came back beside the packet,
/// `from` from `info.from` and `to` from `info.to`. At a single-path milestone
/// that is a long way of saying "the other end"; here it is what makes the
/// pair multi-path correct, because a datagram `send_on_path` put on the
/// second path arrives at the second path rather than at whichever address the
/// caller happened to pass in.
///
/// Note the deliberate asymmetry, which is the step's own trap in miniature:
/// the sender's `from`/`to` are local/remote, and they are copied into the
/// receiver's `from`/`to`, which are remote/local. Same two addresses, and the
/// field names swap meaning across the boundary.
///
/// `Error::Done` is the loop's terminator on both sides and is not an error.
/// Anything else is: a group whose pair silently stopped exchanging packets
/// would report a timeout somewhere far away from the cause.
fn flush(
    from: &mut quiche::Connection, to: &mut quiche::Connection, log: &mut Vec<Vec<u8>>,
    infos: &mut Vec<quiche::SendInfo>,
) -> usize {
    let mut buf = [0u8; MAX_DATAGRAM];
    let mut n = 0;
    loop {
        let (len, info) = match from.send(&mut buf) {
            Ok(v) => v,
            Err(quiche::Error::Done) => return n,
            Err(e) => panic!("send on {}: {e:?}", from.trace_id()),
        };
        log.push(buf[..len].to_vec());
        infos.push(info);
        n += 1;
        let mut d = buf[..len].to_vec();
        let ri = quiche::RecvInfo { from: info.from, to: info.to };
        match to.recv(&mut d, ri) {
            Ok(_) => {},
            Err(quiche::Error::Done) => {},
            Err(e) => panic!("recv on {}: {e:?} ({len} byte datagram)", to.trace_id()),
        }
        if n > 4096 {
            panic!("flush did not terminate: {n} datagrams from {}", from.trace_id());
        }
    }
}

/// Like [`flush`], but tolerant of a connection that has been told to close.
///
/// Once one end calls `conn.close()` because it rejected something its peer
/// sent, the other end may legitimately answer a datagram with
/// `Error::Done`, and the closing end stops producing packets. A group whose
/// subject *is* the close needs the datagrams to keep moving until both ends
/// have seen the CONNECTION_CLOSE rather than a panic from the strict path.
fn flush_lenient(from: &mut quiche::Connection, to: &mut quiche::Connection) -> usize {
    let mut buf = [0u8; MAX_DATAGRAM];
    let mut n = 0;
    loop {
        let (len, info) = match from.send(&mut buf) {
            Ok(v) => v,
            Err(_) => return n,
        };
        n += 1;
        let mut d = buf[..len].to_vec();
        let _ = to.recv(&mut d, quiche::RecvInfo { from: info.from, to: info.to });
        if n > 4096 {
            return n;
        }
    }
}

/// A connection ID and its stateless-reset token, both derived from one byte
/// so a failing group replays identically. Length is `MAX_CONN_ID_LEN`,
/// matching the two the pair starts with.
pub fn spare_cid(seed: u8) -> (quiche::ConnectionId<'static>, u128) {
    let bytes = [seed; quiche::MAX_CONN_ID_LEN];
    let token = u128::from_be_bytes([seed; 16]);
    (quiche::ConnectionId::from_vec(bytes.to_vec()), token)
}

/// Drain one end's path-event queue.
///
/// Draining to `None` is part of what the groups assert, so this returns the
/// events in order and leaves the queue empty; a group that wants to prove the
/// queue is a queue rather than a repeating register calls it twice and
/// expects the second call to come back empty.
pub fn drain_events(conn: &mut quiche::Connection) -> Vec<quiche::PathEvent> {
    let mut out = Vec::new();
    while let Some(e) = conn.path_event_next() {
        out.push(e);
        if out.len() > 64 {
            panic!("path event queue did not drain: {out:?}");
        }
    }
    out
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
// HTTP/3 on top of the pair
// ---------------------------------------------------------------------------

/// An established transport pair with an HTTP/3 connection on each end.
///
/// Built only from public API: `quiche::h3::Config::new` and
/// `quiche::h3::Connection::with_transport`. The `pub mod testing` helpers
/// inside the candidate's own crate are off limits to the verifier, so this
/// is the harness's equivalent.
pub struct H3Pair {
    /// The transport pair underneath.
    pub t: Pair,
    /// The client's HTTP/3 connection.
    pub client: quiche::h3::Connection,
    /// The server's HTTP/3 connection.
    pub server: quiche::h3::Connection,
}

/// An HTTP/3 `Config` with the verifier's defaults.
pub fn h3_config() -> quiche::h3::Config {
    quiche::h3::Config::new().expect("h3::Config::new")
}

impl H3Pair {
    /// Handshake over the `h3` ALPN, then bring up HTTP/3 on both ends and
    /// let the control streams and SETTINGS cross.
    pub fn new() -> H3Pair {
        H3Pair::over(Pair::new(ALPN_H3))
    }

    /// [`H3Pair::new`] over a transport pair the group built itself.
    ///
    /// `t` must not have handshaked yet; this does it, so that a group which
    /// only wanted to change how the ALPN was configured does not have to
    /// restate the rest. Fails the group if the handshake does not negotiate
    /// `h3`, because every later assertion would then be about a connection
    /// that is not carrying HTTP/3.
    pub fn over(mut t: Pair) -> H3Pair {
        t.handshake();
        assert_eq!(
            t.client.application_proto(),
            b"h3",
            "expected the handshake to negotiate the h3 application protocol; observed {:?}",
            String::from_utf8_lossy(t.client.application_proto())
        );
        let cfg = h3_config();
        let client = quiche::h3::Connection::with_transport(&mut t.client, &cfg)
            .expect("h3::Connection::with_transport on the client");
        let server = quiche::h3::Connection::with_transport(&mut t.server, &cfg)
            .expect("h3::Connection::with_transport on the server");
        let mut h = H3Pair { t, client, server };
        h.t.settle(16);
        h
    }

    /// Exchange datagrams until both ends are quiet.
    pub fn settle(&mut self) {
        self.t.settle(32);
    }

    /// Exchange datagrams without insisting either end is still healthy, for
    /// groups whose subject is a connection close.
    pub fn settle_closing(&mut self) {
        for _ in 0..16 {
            let a = flush_lenient(&mut self.t.client, &mut self.t.server);
            let b = flush_lenient(&mut self.t.server, &mut self.t.client);
            if a + b == 0 {
                return;
            }
        }
    }

    /// One `poll` on the server's HTTP/3 connection.
    pub fn poll_server(&mut self) -> quiche::h3::Result<(u64, quiche::h3::Event)> {
        self.server.poll(&mut self.t.server)
    }

    /// One `poll` on the client's HTTP/3 connection.
    pub fn poll_client(&mut self) -> quiche::h3::Result<(u64, quiche::h3::Event)> {
        self.client.poll(&mut self.t.client)
    }
}

impl Default for H3Pair {
    fn default() -> H3Pair {
        H3Pair::new()
    }
}

/// Encode one HTTP/3 frame as `varint(type) varint(len) payload`.
///
/// The verifier builds HTTP/3 frames with its own varint writer rather than
/// with the candidate's, so a group that writes a frame onto a control stream
/// is testing the candidate's reader and not its writer.
pub fn h3_frame(frame_type: u64, payload: &[u8]) -> Vec<u8> {
    let mut out = Vec::new();
    support::encode_varint(frame_type, &mut out).expect("encode frame type");
    support::encode_varint(payload.len() as u64, &mut out).expect("encode frame length");
    out.extend_from_slice(payload);
    out
}

/// The payload of a PRIORITY_UPDATE frame: the prioritized element ID as a
/// varint, then the priority field value verbatim.
pub fn priority_update_payload(prioritized_element_id: u64, field_value: &[u8]) -> Vec<u8> {
    let mut out = Vec::new();
    support::encode_varint(prioritized_element_id, &mut out).expect("encode element id");
    out.extend_from_slice(field_value);
    out
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
/// A variable-length integer, appended to `out`.
///
/// RFC 9000 Section 16. Every frame a group hand-builds is a sequence of
/// these, so it is worth a name.
pub fn varint(v: u64, out: &mut Vec<u8>) {
    support::encode_varint(v, out).expect("varint out of range");
}

/// A protected 1-RTT packet carrying `payload`, built by the verifier.
///
/// The key phase bit is clear. Use [`seal_1rtt_phase`] when it matters.
pub fn seal_1rtt(
    keys: &support::PacketKeys, dcid: &[u8], packet_number: u64, payload: &[u8],
) -> Vec<u8> {
    seal_1rtt_phase(keys, dcid, packet_number, payload, false)
}

/// [`seal_1rtt`], with the key phase bit under the caller's control.
///
/// The bit lives in the first byte, which header protection masks, so it is
/// set before [`support::apply_header_protection`] runs and is invisible on
/// the wire to anyone without the header-protection key -- which is the whole
/// reason a key update cannot be spotted by an observer.
pub fn seal_1rtt_phase(
    keys: &support::PacketKeys, dcid: &[u8], packet_number: u64, payload: &[u8], key_phase: bool,
) -> Vec<u8> {
    const PN_LEN: usize = 4;
    assert!(packet_number < u32::MAX as u64, "packet number {packet_number} needs more than 4 bytes");

    let mut pkt = Vec::with_capacity(1 + dcid.len() + PN_LEN + payload.len() + support::TAG_LEN);
    // 0x40 is the fixed bit; bits 0-1 carry `PN_LEN - 1`; 0x04 is the key
    // phase; spin is 0.
    let first = 0x40 | (PN_LEN as u8 - 1) | if key_phase { 0x04 } else { 0 };
    pkt.push(first);
    pkt.extend_from_slice(dcid);
    let pn_offset = pkt.len();
    pkt.extend_from_slice(&(packet_number as u32).to_be_bytes());

    let ciphertext = keys
        .seal(packet_number, &pkt, payload)
        .expect("sealing a 1-RTT payload with the verifier's own AEAD");
    pkt.extend_from_slice(&ciphertext);

    support::apply_header_protection(keys, &mut pkt, pn_offset, PN_LEN)
        .expect("applying header protection with the verifier's own mask");
    pkt
}

/// One 1-RTT packet the candidate wrote, opened by the verifier.
///
/// [`decrypt_1rtt`] answers "what frames were in it". This answers that and
/// "what key phase was it sent under", which is only readable once header
/// protection has been removed.
#[derive(Debug)]
pub struct Opened {
    /// The decrypted frame payload.
    pub payload: Vec<u8>,
    /// The key phase bit, taken from the unmasked first byte.
    pub key_phase: bool,
    /// The full packet number, reconstructed from the truncated one.
    pub packet_number: u64,
}

/// [`decrypt_1rtt`], keeping the header bits it discards.
pub fn open_1rtt(
    packet: &[u8], dcid_len: usize, keys: &support::PacketKeys, largest_acked: Option<u64>,
) -> Option<Opened> {
    if support::is_long_header(*packet.first()?) {
        return None;
    }

    let pn_offset = support::ShortHeader::parse(packet, dcid_len).ok()?.pn_offset;

    let mut pkt = packet.to_vec();
    let pn = support::remove_header_protection(keys, &mut pkt, pn_offset).ok()?;
    let packet_number = support::decode_packet_number(largest_acked, pn.truncated, pn.len);

    let payload_offset = pn_offset + pn.len;
    let aad = &pkt[..payload_offset];
    let ciphertext = &pkt[payload_offset..];
    let payload = keys.open(packet_number, aad, ciphertext).ok()?;

    // Re-read the first byte now that the mask is off.
    let key_phase = support::ShortHeader::parse(&pkt, dcid_len).ok()?.key_phase();

    Some(Opened { payload, key_phase, packet_number })
}

/// Decrypt a short-header 1-RTT packet and return its payload bytes, or `None`
/// if the packet is malformed or decryption fails.
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

/// Every frame in a decrypted 1-RTT payload, as the support crate decoded it.
///
/// Iteration stops at the first frame the support crate cannot decode; that is
/// deliberate there, and it is why this returns what it found rather than
/// insisting the whole payload parsed.
pub fn decoded_frames(frame_payload: &[u8]) -> Vec<support::Frame<'_>> {
    let mut out = Vec::new();
    for f in support::frames(frame_payload) {
        match f {
            Ok(v) => out.push(v),
            Err(_) => break,
        }
    }
    out
}
