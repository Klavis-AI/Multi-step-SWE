//! new-capability groups: what this milestone can do that the previous one could not
//!
//! One `#[test]` per scored group, named exactly as the group id with dashes
//! turned into underscores: `cap-initial-keys` is `fn cap_initial_keys()`.
//! The driver runs each one on its own with `--exact` and asserts that
//! exactly one test executed, so a renamed, `#[ignore]`d or accidentally
//! deleted function scores zero for that group rather than passing silently.
//!
//! Observe behaviour. Never read the candidate's source, never grep it for an
//! identifier, and never assert on a private detail: every group sits on one
//! of the five observation surfaces -- T1 two-process loopback through the
//! candidate's own binaries, T2 the verifier speaking QUIC at the candidate,
//! T3 in-memory connection pairs through the public Rust API, T4 emitted
//! artefacts, T5 exact-answer negative corpora.

use std::time::Duration;

#[allow(unused_imports)]
use verifier::{pair, support, testkit};

#[allow(dead_code)]
const SHORT: Duration = Duration::from_secs(10);
#[allow(dead_code)]
const LONG: Duration = Duration::from_secs(60);

// ---------------------------------------------------------------------------
// helpers local to this target
// ---------------------------------------------------------------------------

/// A config pair with DATAGRAM enabled on both sides.
fn dgram_configs(recv: usize, send: usize) -> (quiche::Config, quiche::Config) {
    let mut c = pair::client_config(pair::ALPN_HQ);
    let mut s = pair::server_config(pair::ALPN_HQ);
    c.enable_dgram(true, recv, send);
    s.enable_dgram(true, recv, send);
    (c, s)
}

// ---------------------------------------------------------------------------
// groups
// ---------------------------------------------------------------------------

/// T3: an in-memory `Connection` API pair, not a wire observation. A datagram
/// crosses the connection in both directions and arrives byte for byte, and
/// it does so without ever appearing on a stream. This step's `client`/
/// `server` examples take no flags (they gain configuration only at step
/// 06), so there is no candidate binary this group could drive end to end;
/// see 04.toml's `cap-datagram-roundtrip` group for the full rationale.
#[test]
fn cap_datagram_roundtrip() {
    let (mut cc, mut sc) = dgram_configs(16, 16);
    let mut p = pair::Pair::with_configs(&mut cc, &mut sc);
    p.handshake();

    // Both directions must have negotiated the extension.
    let cmax = p.client.dgram_max_writable_len().expect("client: peer advertised no DATAGRAM support");
    let smax = p.server.dgram_max_writable_len().expect("server: peer advertised no DATAGRAM support");
    assert!(cmax > 0 && smax > 0, "writable len must be positive, got {cmax}/{smax}");

    let payload = testkit::body(256, 0x0405);

    p.client.dgram_send(&payload).expect("client dgram_send");
    p.settle(64);

    let mut buf = vec![0u8; 2048];
    let n = p.server.dgram_recv(&mut buf).expect("server dgram_recv");
    assert_eq!(&buf[..n], &payload[..], "server received a different datagram");

    // Nothing was delivered as stream data.
    assert_eq!(p.server.readable().count(), 0, "a DATAGRAM must not open a stream");

    // Echo it back.
    p.server.dgram_send(&buf[..n]).expect("server dgram_send");
    p.settle(64);

    let n = p.client.dgram_recv(&mut buf).expect("client dgram_recv");
    assert_eq!(&buf[..n], &payload[..], "client received a different echo");

    // The queue is empty again on both sides.
    assert_eq!(p.client.dgram_recv(&mut buf), Err(quiche::Error::Done));
    assert_eq!(p.server.dgram_recv(&mut buf), Err(quiche::Error::Done));
}

/// T2: the verifier decrypts the candidate's 1-RTT packets with its own
/// crypto, drops the one carrying a DATAGRAM frame, and requires that the
/// payload never appears on the wire again -- while a STREAM frame dropped
/// the same way does come back. Loss recovery is driven hard enough that a
/// retransmitting implementation would have retransmitted.
#[test]
fn cap_datagram_not_retransmitted() {
    let (mut cc, mut sc) = dgram_configs(16, 16);
    cc.log_keys();

    let (mut p, keylog) = pair::Pair::with_keylog(&mut cc, &mut sc);
    p.handshake();

    let secret = pair::keylog_secret(&keylog.contents(), "CLIENT_TRAFFIC_SECRET_0")
        .expect("no CLIENT_TRAFFIC_SECRET_0 in the candidate's keylog");
    let keys = support::PacketKeys::from_secret(support::Suite::Aes128Gcm, &secret)
        .expect("derive 1-RTT keys from the traffic secret");

    // The client's short headers carry the server's connection ID.
    let dcid_len = pair::SERVER_CID.len();

    let dgram_payload = testkit::body(128, 0xd6d6);
    let stream_payload = testkit::body(128, 0x5757);

    p.client.dgram_send(&dgram_payload).expect("dgram_send");
    p.client.stream_send(4, &stream_payload, false).expect("stream_send");

    // Pull packets from the client WITHOUT delivering them: this is the loss.
    let mut buf = vec![0u8; 2048];
    let mut saw_dgram = false;
    let mut saw_stream = false;
    let mut dropped = 0;
    loop {
        let len = match p.client.send(&mut buf) {
            Ok(v) => v,
            Err(quiche::Error::Done) => break,
            Err(e) => panic!("client send: {e:?}"),
        };
        dropped += 1;

        let plain = pair::decrypt_1rtt(&buf[..len], dcid_len, &keys, None)
            .expect("verifier could not decrypt the candidate's 1-RTT packet");

        for f in support::frames(&plain) {
            match f {
                Ok(support::Frame::Datagram { data, .. }) => {
                    assert_eq!(data, &dgram_payload[..], "DATAGRAM frame carried other bytes");
                    saw_dgram = true;
                },
                Ok(support::Frame::Stream { data, .. }) => {
                    if data == &stream_payload[..] {
                        saw_stream = true;
                    }
                },
                Ok(_) => {},
                Err(e) => panic!("verifier could not parse a frame: {e:?}"),
            }
        }
    }
    assert!(saw_dgram, "no DATAGRAM frame in the {dropped} packets the client sent");
    assert!(saw_stream, "no STREAM frame in the {dropped} packets the client sent");

    // Drive loss recovery: the peer acknowledges nothing, so the PTO must
    // fire and the client must retransmit what is retransmittable.
    let deadline = std::time::Instant::now() + Duration::from_secs(5);
    let mut retransmitted_stream = false;
    let mut retransmitted_dgram = false;

    while std::time::Instant::now() < deadline && !retransmitted_stream {
        if let Some(t) = p.client.timeout() {
            std::thread::sleep(t.min(Duration::from_millis(250)));
        } else {
            std::thread::sleep(Duration::from_millis(25));
        }
        p.client.on_timeout();

        loop {
            let len = match p.client.send(&mut buf) {
                Ok(v) => v,
                Err(quiche::Error::Done) => break,
                Err(e) => panic!("client send after PTO: {e:?}"),
            };

            let plain = match pair::decrypt_1rtt(&buf[..len], dcid_len, &keys, None) {
                Some(v) => v,
                None => continue,
            };

            for f in support::frames(&plain) {
                match f {
                    Ok(support::Frame::Datagram { data, .. }) => {
                        if data == &dgram_payload[..] {
                            retransmitted_dgram = true;
                        }
                    },
                    Ok(support::Frame::Stream { data, .. }) => {
                        if data == &stream_payload[..] {
                            retransmitted_stream = true;
                        }
                    },
                    Ok(_) => {},
                    Err(_) => break,
                }
            }
        }
    }

    assert!(
        retransmitted_stream,
        "the STREAM frame was never retransmitted, so loss recovery did not run \
         and this group proved nothing about DATAGRAM"
    );
    assert!(
        !retransmitted_dgram,
        "the lost DATAGRAM frame was retransmitted; DATAGRAM frames must never be"
    );
}

/// T3: the send queue length passed to `enable_dgram` is enforced, and the
/// signal for a full queue is `Error::Done` rather than a panic or a silent
/// drop.
#[test]
fn cap_dgram_queue_bounded() {
    let (mut cc, mut sc) = dgram_configs(3, 3);
    let mut p = pair::Pair::with_configs(&mut cc, &mut sc);
    p.handshake();

    // Fill the send queue exactly.
    for i in 0..3 {
        p.client.dgram_send(format!("dgram-{i}").as_bytes())
            .unwrap_or_else(|e| panic!("dgram_send #{i} into a 3-deep queue: {e:?}"));
    }

    assert_eq!(
        p.client.dgram_send(b"overflow"),
        Err(quiche::Error::Done),
        "a full send queue must report Done"
    );

    // Drain it onto the wire, then the queue accepts again.
    p.settle(64);
    p.client.dgram_send(b"after-drain").expect("dgram_send after the queue drained");

    // The receive queue is bounded the same way: send four, receive three.
    p.settle(64);
    let mut buf = vec![0u8; 2048];
    let mut received = 0;
    while p.server.dgram_recv(&mut buf).is_ok() {
        received += 1;
        if received > 8 {
            panic!("receive queue never emptied");
        }
    }
    assert!(received <= 3, "receive queue held {received} datagrams, cap was 3");
    assert!(received > 0, "receive queue held nothing");
}

/// T3: `Error` is a Rust enum whose `StreamStopped` variant carries the
/// peer's application error code inline. At 0.5.0 the enum had C-style
/// discriminants and no associated data, so the code had nowhere to live.
#[test]
fn cap_error_enum() {
    let mut cc = pair::client_config(pair::ALPN_HQ);
    let mut sc = pair::server_config(pair::ALPN_HQ);
    cc.log_keys();

    let (mut p, keylog) = pair::Pair::with_keylog(&mut cc, &mut sc);
    p.handshake();

    // Open stream 4 so both sides agree it exists.
    p.client.stream_send(4, b"hello", false).expect("stream_send");
    p.settle(64);

    // The candidate does not send STOP_SENDING itself at this milestone, so
    // the verifier builds the frame and protects the packet with its own
    // crypto -- the same keys the candidate derived, recovered from the
    // keylog, but the encoding is the verifier's.
    let secret = pair::keylog_secret(&keylog.contents(), "SERVER_TRAFFIC_SECRET_0")
        .expect("no SERVER_TRAFFIC_SECRET_0 in the keylog");
    let keys = support::PacketKeys::from_secret(support::Suite::Aes128Gcm, &secret)
        .expect("derive server 1-RTT keys");

    let stop_code = 0x123u64;
    // Packet number must be past anything the server already sent in the
    // 1-RTT space during the handshake, or the client dedupes our packet and
    // silently drops the frame.
    let pkt = build_stop_sending(&keys, &pair::CLIENT_CID, 4, stop_code, 1000);
    p.client.recv(&mut pkt.clone()).expect("client recv of the verifier's STOP_SENDING");

    match p.client.stream_send(4, b"more", false) {
        Err(quiche::Error::StreamStopped(code)) => {
            assert_eq!(code, stop_code, "StreamStopped carried the wrong error code");
        },
        Ok(n) => panic!("stream_send accepted {n} bytes after STOP_SENDING"),
        Err(e) => panic!("expected StreamStopped({stop_code:#x}), got {e:?}"),
    }

    // The same identity shows through stream_capacity.
    match p.client.stream_capacity(4) {
        Err(quiche::Error::StreamStopped(code)) => assert_eq!(code, stop_code),
        other => panic!("stream_capacity after STOP_SENDING: {other:?}"),
    }
}

/// Build a 1-RTT packet carrying one STOP_SENDING frame, protected with
/// `keys`. Entirely the verifier's own encoding.
fn build_stop_sending(
    keys: &support::PacketKeys, dcid: &[u8], stream_id: u64, error_code: u64, pn: u64,
) -> Vec<u8> {
    // Frame payload: STOP_SENDING (0x05), stream id, error code. Padded so the
    // packet is long enough to sample for header protection.
    let mut payload = Vec::new();
    support::encode_varint(0x05, &mut payload).expect("frame type");
    support::encode_varint(stream_id, &mut payload).expect("stream id");
    support::encode_varint(error_code, &mut payload).expect("error code");
    payload.resize(payload.len() + 64, 0); // PADDING

    // Short header: fixed bit set, key phase 0, 4-byte packet number.
    let pn_len = 4usize;
    let mut pkt = Vec::new();
    pkt.push(0x40 | (pn_len as u8 - 1));
    pkt.extend_from_slice(dcid);
    let pn_offset = pkt.len();
    pkt.extend_from_slice(&(pn as u32).to_be_bytes());

    let sealed = keys.seal(pn, &pkt, &payload).expect("seal");
    pkt.extend_from_slice(&sealed);

    support::apply_header_protection(keys, &mut pkt, pn_offset, pn_len)
        .expect("apply header protection");
    pkt
}

/// T3: `dgram_max_writable_len` reports a real budget -- it fits inside the
/// connection's maximum packet size, a datagram of exactly that length is
/// accepted, and one byte more is refused rather than silently truncated.
#[test]
fn cap_dgram_max_size() {
    let (mut cc, mut sc) = dgram_configs(16, 16);
    let mut p = pair::Pair::with_configs(&mut cc, &mut sc);
    p.handshake();

    let max = p.client.dgram_max_writable_len().expect("DATAGRAM not negotiated");

    // It has to leave room for the short header, the packet number, the AEAD
    // tag and the frame header inside one packet.
    assert!(
        max < pair::MAX_DATAGRAM,
        "writable len {max} does not account for packet overhead \
         (max packet size is {})",
        pair::MAX_DATAGRAM
    );

    p.client.dgram_send(&vec![0x42; max]).expect("a datagram of exactly the writable length");

    assert_eq!(
        p.client.dgram_send(&vec![0x43; max + 1]),
        Err(quiche::Error::BufferTooShort),
        "one byte over the writable length must be refused"
    );

    // The one that fit really does cross intact.
    let before = p.client_out.len();
    p.settle(64);
    let mut buf = vec![0u8; 4096];
    let n = p.server.dgram_recv(&mut buf).expect("server dgram_recv");
    assert_eq!(n, max, "the maximum-length datagram was truncated");
    assert!(buf[..n].iter().all(|&b| b == 0x42), "payload corrupted");

    // `max` has to be the *largest* payload that still fits in one packet, so
    // calibrate it against the packet that actually carried it rather than
    // against a guessed constant: the send path is capped by the peer's
    // max_udp_payload_size, not by our own receive size.
    let carrier = p.client_out[before..]
        .iter()
        .map(|d| d.len())
        .max()
        .expect("the client emitted no datagram for the DATAGRAM frame");
    assert!(carrier > max, "a {carrier}-byte packet cannot carry a {max}-byte payload plus overhead");
    assert!(
        carrier - max < 64,
        "writable len {max} is implausibly small: it travelled in a {carrier}-byte \
         packet, wasting {} bytes that a correct bound would have used",
        carrier - max
    );
    assert!(
        carrier <= pair::MAX_DATAGRAM,
        "carrier packet {carrier} exceeds the {} byte max packet size",
        pair::MAX_DATAGRAM
    );
}

/// T3: enabling DATAGRAM on one side only is not enough. The extension is
/// negotiated per direction, so a client whose peer never advertised
/// `max_datagram_frame_size` must refuse to send rather than emit a frame the
/// peer is required to treat as a connection error.
#[test]
fn cap_dgram_not_negotiated() {
    let mut cc = pair::client_config(pair::ALPN_HQ);
    let mut sc = pair::server_config(pair::ALPN_HQ);
    cc.enable_dgram(true, 16, 16);
    // sc deliberately left without enable_dgram.

    let mut p = pair::Pair::with_configs(&mut cc, &mut sc);
    p.handshake();

    assert_eq!(
        p.client.dgram_max_writable_len(),
        None,
        "the peer never advertised DATAGRAM support, so there is no writable length"
    );

    assert_eq!(
        p.client.dgram_send(b"unilateral"),
        Err(quiche::Error::InvalidState),
        "sending a DATAGRAM to a peer that does not support them must fail"
    );

    // And the connection is still healthy.
    assert!(!p.client.is_closed() && !p.server.is_closed());
    let (got, fin) = p.client_to_server(4, b"streams still work", true);
    assert_eq!(&got, b"streams still work");
    assert!(fin);
}

/// T3: connection IDs are a type, not a slice. `Header::from_slice` hands
/// back `ConnectionId`s that compare equal to the bytes that were on the
/// wire, and both the owned and the borrowed constructor round-trip. Also
/// exercises the rest of `ConnectionId`'s hand-implemented trait surface:
/// `Default`, `From<Vec<u8>>`, `Clone`, `Hash`, and the lower-hex `Debug`
/// format -- none of these are derived, so each is a real implementation
/// choice worth pinning down independently of `PartialEq`/`AsRef`.
#[test]
fn cap_connid_wrapper() {
    // A real Initial, produced by the candidate, is the only packet whose
    // connection IDs the verifier knows independently: the pair chose them.
    let mut cc = pair::client_config(pair::ALPN_HQ);
    let mut sc = pair::server_config(pair::ALPN_HQ);
    let mut p = pair::Pair::with_configs(&mut cc, &mut sc);

    let mut buf = vec![0u8; pair::MAX_DATAGRAM];
    let len = p.client.send(&mut buf).expect("client's first flight");

    let hdr = quiche::Header::from_slice(&mut buf[..len], quiche::MAX_CONN_ID_LEN)
        .expect("Header::from_slice on the candidate's own Initial");

    assert_eq!(hdr.ty, quiche::Type::Initial);
    assert_eq!(
        hdr.scid.as_ref(),
        &pair::CLIENT_CID[..],
        "the source connection ID is not the one connect() was given"
    );
    assert_eq!(
        hdr.dcid.len(),
        hdr.dcid.as_ref().len(),
        "ConnectionId::len disagrees with the bytes it holds"
    );

    // Owned and borrowed constructors produce the same connection ID, and
    // neither disturbs the bytes.
    let raw: [u8; 5] = [0xde, 0xad, 0xbe, 0xef, 0x01];
    let borrowed = quiche::ConnectionId::from_ref(&raw);
    let owned = quiche::ConnectionId::from_vec(raw.to_vec());

    assert_eq!(borrowed.as_ref(), &raw[..]);
    assert_eq!(owned.as_ref(), &raw[..]);
    assert_eq!(borrowed, owned, "from_ref and from_vec must compare equal");
    assert_eq!(borrowed.len(), raw.len());

    // `Default` is the empty connection ID, the same value `from_vec(vec![])`
    // would produce.
    let default_cid = quiche::ConnectionId::default();
    assert!(default_cid.is_empty(), "ConnectionId::default() must be empty");
    assert_eq!(
        default_cid,
        quiche::ConnectionId::from_vec(Vec::new()),
        "default() must equal from_vec(vec![])"
    );

    // `From<Vec<u8>>` is the same construction path as `from_vec()`, not a
    // separate one that happens to produce an equal value.
    let via_from: quiche::ConnectionId = raw.to_vec().into();
    assert_eq!(via_from, owned, "From<Vec<u8>>::into() must match from_vec()");
    assert_eq!(via_from.as_ref(), &raw[..]);

    // `Clone` produces an equal value regardless of which constructor built
    // the original, and cloning does not disturb the bytes.
    assert_eq!(borrowed.clone(), borrowed, "cloning a borrowed ConnectionId");
    assert_eq!(owned.clone(), owned, "cloning an owned ConnectionId");
    assert_eq!(
        borrowed.clone(),
        owned.clone(),
        "clones of equal-content borrowed/owned ConnectionIds must still be equal"
    );

    // `Hash` agrees with `Eq`: two connection IDs with the same bytes, built
    // through different constructors, hash identically.
    use std::collections::hash_map::DefaultHasher;
    use std::hash::{Hash, Hasher};
    let hash_of = |cid: &quiche::ConnectionId| {
        let mut h = DefaultHasher::new();
        cid.hash(&mut h);
        h.finish()
    };
    assert_eq!(
        hash_of(&borrowed),
        hash_of(&owned),
        "equal ConnectionIds (borrowed vs owned) must hash equal"
    );

    // `Debug` prints lower-hex with no separators and no derived struct
    // wrapper -- not the `[222, 173, 190, 239, 1]` a derived impl would give.
    assert_eq!(format!("{:?}", borrowed), "deadbeef01");
    assert_eq!(format!("{:?}", owned), "deadbeef01");

    // A connection ID the server picked, carried on the server's reply,
    // survives the same round trip. The Initial we pulled out by hand never
    // reached the server, so hand it over first: otherwise the pair sits on a
    // loss-detection timer with nothing to exchange and never converges.
    let mut first = buf[..len].to_vec();
    p.server.recv(&mut first).expect("server recv of the client's first flight");
    p.handshake();

    let reply = p.server_out.first().expect("the server never replied").clone();
    let mut reply = reply;
    let shdr = quiche::Header::from_slice(&mut reply, quiche::MAX_CONN_ID_LEN)
        .expect("Header::from_slice on the server's reply");
    assert_eq!(
        shdr.scid.as_ref(),
        &pair::SERVER_CID[..],
        "the server's reply does not carry the connection ID accept() was given"
    );
    assert_eq!(
        shdr.scid.len(),
        pair::SERVER_CID.len(),
        "ConnectionId::len disagrees with the bytes it holds on the wire"
    );
}
