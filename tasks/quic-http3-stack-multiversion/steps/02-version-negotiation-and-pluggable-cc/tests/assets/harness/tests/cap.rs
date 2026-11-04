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
//!
//! At 0.3.0 the flag-taking `quiche-client`/`quiche-server` do not exist --
//! `tools/apps` does not become a workspace member until step 06 -- so every
//! group here that needs a process drives the example binaries, whose listen
//! port, certificate paths and document root are all compiled in. See
//! REQUIREMENTS.md, "T1 binary CLI eras".

use std::io::ErrorKind;
use std::net::{Ipv4Addr, SocketAddr, UdpSocket};
use std::time::{Duration, Instant};

#[allow(unused_imports)]
use verifier::{pair, support, testkit};

#[allow(dead_code)]
const SHORT: Duration = Duration::from_secs(10);
#[allow(dead_code)]
const LONG: Duration = Duration::from_secs(60);

/// The four versions this release answers to, best first.
const DRAFT27: u32 = 0xff00_001b;
const DRAFT25: u32 = 0xff00_0019;
const DRAFT24: u32 = 0xff00_0018;
const DRAFT23: u32 = 0xff00_0017;

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

fn head(b: &[u8], n: usize) -> String {
    testkit::hex(&b[..n.min(b.len())])
}

/// A well-formed start to a ClientHello that promises more bytes than the
/// packet carries, so a server buffers it and waits rather than tearing the
/// connection down. The groups below are about version selection and address
/// validation, not about TLS.
fn partial_client_hello() -> Vec<u8> {
    let mut crypto = vec![0x01, 0x00, 0x04, 0x00, 0x03, 0x03];
    crypto.extend(std::iter::repeat(0x42).take(32));
    crypto
}

/// Start the example HQ server in a staged working directory and return it
/// with the address it listens on.
fn hq_server(dir: &std::path::Path, files: &[(&str, Vec<u8>)]) -> (testkit::Server, SocketAddr) {
    let cwd = testkit::example_server_cwd(dir, files);
    let mut sc = testkit::cmd(testkit::server_bin());
    sc.current_dir(&cwd);
    testkit::configure_server(&mut sc, &cwd, testkit::EXAMPLE_SERVER_PORT);
    let s = testkit::spawn_server(&mut sc, testkit::EXAMPLE_SERVER_PORT, dir, "server", SHORT);
    let addr = SocketAddr::from((Ipv4Addr::LOCALHOST, testkit::EXAMPLE_SERVER_PORT));
    (s, addr)
}

/// Drive a real QUIC handshake from this process, over UDP, against a server
/// the candidate is running. Returns the ALPN the two of them settled on.
///
/// This is the only way to ask a *process* what it will negotiate: the answer
/// is on the wire, not in an API.
fn udp_handshake(to: SocketAddr, alpn: &[u8], version: u32) -> Result<Vec<u8>, String> {
    let sock = UdpSocket::bind((Ipv4Addr::LOCALHOST, 0)).map_err(|e| format!("bind: {e}"))?;
    sock.set_read_timeout(Some(Duration::from_millis(50))).map_err(|e| format!("timeout: {e}"))?;
    sock.connect(to).map_err(|e| format!("connect: {e}"))?;

    let mut config = quiche::Config::new(version).map_err(|e| format!("Config::new: {e:?}"))?;
    config.set_application_protos(alpn).map_err(|e| format!("set_application_protos: {e:?}"))?;
    config.verify_peer(false);
    config.set_max_idle_timeout(5_000);
    config.set_max_packet_size(pair::MAX_DATAGRAM as u64);
    config.set_initial_max_data(1_000_000);
    config.set_initial_max_stream_data_bidi_local(1_000_000);
    config.set_initial_max_stream_data_bidi_remote(1_000_000);
    config.set_initial_max_stream_data_uni(1_000_000);
    config.set_initial_max_streams_bidi(100);
    config.set_initial_max_streams_uni(100);

    let scid = [0x33u8; quiche::MAX_CONN_ID_LEN];
    let mut conn =
        quiche::connect(Some("quic.test"), &scid, &mut config).map_err(|e| format!("{e:?}"))?;

    let mut out = [0u8; pair::MAX_DATAGRAM];
    let deadline = Instant::now() + SHORT;
    loop {
        loop {
            match conn.send(&mut out) {
                Ok(n) => {
                    sock.send(&out[..n]).map_err(|e| format!("send: {e}"))?;
                },
                Err(quiche::Error::Done) => break,
                Err(e) => return Err(format!("conn.send: {e:?}")),
            }
        }

        if conn.is_established() {
            return Ok(conn.application_proto().to_vec());
        }
        if conn.is_closed() {
            return Err("the connection closed before the handshake completed".to_string());
        }
        if Instant::now() >= deadline {
            return Err(format!("no handshake within {SHORT:?}"));
        }

        let mut buf = [0u8; 65535];
        match sock.recv(&mut buf) {
            Ok(n) => {
                let _ = conn.recv(&mut buf[..n]);
            },
            Err(e) if e.kind() == ErrorKind::WouldBlock || e.kind() == ErrorKind::TimedOut => {
                conn.on_timeout();
            },
            Err(e) if e.kind() == ErrorKind::ConnectionRefused => {},
            Err(e) => return Err(format!("sock.recv: {e}")),
        }
    }
}

// ---------------------------------------------------------------------------
// groups
// ---------------------------------------------------------------------------

/// `cap-version-negotiation`: an Initial carrying a version the candidate does
/// not know draws a Version Negotiation packet listing the ones it does.
///
/// Step 01 had exactly one version and nothing to negotiate. Here there are
/// four, so a client that asks for something else has to be told what is on
/// offer rather than ignored or answered with a server Initial.
#[test]
fn cap_version_negotiation() {
    let _lock = testkit::serial_t1();
    let dir = testkit::scratch("cap-version-negotiation");
    let (server, to) = hq_server(&dir, &[("index.txt", b"vn\n".to_vec())]);

    let ep = testkit::Endpoint::new();
    let odcid: Vec<u8> = (0xa0u8..0xb4).collect();
    let scid: Vec<u8> = (0x50u8..0x58).collect();
    let crypto = partial_client_hello();

    // A version from the reserved-for-testing pattern, which no release of
    // this library will ever claim to support. There is no Initial salt for
    // it -- that is the point -- so the packet is built as a well-formed
    // draft-27 Initial and then relabelled. A server is supposed to read the
    // version field and answer before it tries to decrypt anything, so the
    // payload being unopenable under the advertised version is exactly the
    // situation a real client in this position creates.
    let unknown: u32 = 0x6b33_43cf;
    let mut pkt = support::ClientInitial::new(DRAFT27, &odcid, &scid, &crypto)
        .build()
        .unwrap_or_else(|e| panic!("the verifier could not build its own client Initial: {e}"));
    pkt[1..5].copy_from_slice(&unknown.to_be_bytes());
    assert_eq!(
        support::peek_version(&pkt).expect("version field of the verifier's own packet"),
        unknown,
        "the verifier mislabelled its own probe packet"
    );
    ep.send_to(&pkt, to);

    let reply = ep.recv(SHORT).unwrap_or_else(|| {
        panic!(
            "expected the server at {to} to answer an Initial carrying the unknown version \
             {unknown:#010x} within {SHORT:?}; observed no datagram at all.\n\
             --- server log ---\n{}",
            server.log()
        )
    });

    assert!(
        support::is_long_header(reply[0]),
        "expected a long-header Version Negotiation packet; observed first byte {:02x}, \
         datagram {}.\n--- server log ---\n{}",
        reply[0],
        head(&reply, 64),
        server.log()
    );

    let version = support::peek_version(&reply)
        .unwrap_or_else(|e| panic!("cannot read the version field of the reply: {e}"));
    assert_eq!(
        version,
        0,
        "a Version Negotiation packet is identified by a zero version field; observed \
         {version:#010x}, so the server answered with something else -- most likely a server \
         Initial, which is exactly what it must not do.\ndatagram {}\n--- server log ---\n{}",
        head(&reply, 64),
        server.log()
    );

    let vn = support::VersionNegotiationPacket::parse(&reply).unwrap_or_else(|e| {
        panic!(
            "the reply has a zero version field but does not parse as Version Negotiation: {e}\n\
             datagram {}\n--- server log ---\n{}",
            head(&reply, 64),
            server.log()
        )
    });

    // The server mirrors the connection IDs: what the client used as its
    // source comes back as the destination.
    assert_eq!(
        vn.dcid,
        &scid[..],
        "expected the client's source connection ID {} back in the destination field; observed \
         {}.\n--- server log ---\n{}",
        testkit::hex(&scid),
        testkit::hex(vn.dcid),
        server.log()
    );
    assert_eq!(
        vn.scid,
        &odcid[..],
        "expected the client's destination connection ID {} back in the source field; observed \
         {}.\n--- server log ---\n{}",
        testkit::hex(&odcid),
        testkit::hex(vn.scid),
        server.log()
    );

    // PLAN CORRECTION. The group plan expected all four supported versions
    // here. The release advertises three: it offers draft-27, draft-24 and
    // draft-23 and leaves draft-25 out of the list, even though
    // `version_is_supported` accepts it. The Oracle is ground truth, so that
    // is what this group pins -- including the absence of draft-25, which is
    // the part a candidate would most plausibly get wrong by "fixing".
    assert_eq!(
        vn.versions,
        vec![DRAFT27, DRAFT24, DRAFT23],
        "expected the advertised version list [{DRAFT27:#010x}, {DRAFT24:#010x}, \
         {DRAFT23:#010x}] in that order; observed {:#010x?}. Note that draft-25 \
         ({DRAFT25:#010x}) is accepted by this release but is deliberately not advertised \
         here.\n--- server log ---\n{}",
        vn.versions,
        server.log()
    );

    drop(server);
}

/// `cap-draft27-default`: the crate's own default version is draft-27 on the
/// wire, and choosing it does not cost the older ones.
///
/// Step 01 spoke draft-23 and nothing else. The milestone is not "draft-27
/// exists" but "draft-27 is preferred while draft-23 still works", so this
/// group checks both halves.
#[test]
fn cap_draft27_default() {
    // Half one: the default version, as it appears on the wire.
    let mut cc = pair::client_config(pair::ALPN_HQ);
    let mut sc = pair::server_config(pair::ALPN_HQ);
    let mut p = pair::Pair::with_configs(&mut cc, &mut sc);

    let mut buf = vec![0u8; pair::MAX_DATAGRAM];
    let len = p.client.send(&mut buf).expect("the client's first flight");
    let wire = support::peek_version(&buf[..len]).expect("version field of the client Initial");
    assert_eq!(
        wire, DRAFT27,
        "a Config built with the crate's own default version put {wire:#010x} on the wire; \
         expected draft-27 {DRAFT27:#010x}. datagram {}",
        head(&buf[..len], 48)
    );

    // And it really is a draft-27 Initial: it decrypts under that version's
    // salt, which a packet merely labelled draft-27 would not.
    let dec = support::decrypt_initial(
        &buf[..len],
        DRAFT27,
        support::Perspective::Client,
    )
    .unwrap_or_else(|e| {
        panic!(
            "the client's Initial claims version {wire:#010x} but does not decrypt under the \
             draft-27 Initial salt: {e}"
        )
    });
    assert!(
        !dec.crypto_data().expect("CRYPTO data").is_empty(),
        "the draft-27 Initial decrypted but carried no CRYPTO data"
    );

    // Half two: all four versions still complete a handshake against a server
    // built the ordinary way. A release that had *replaced* draft-23 rather
    // than added to it would negotiate here instead of completing.
    for (name, version) in
        [("draft-27", DRAFT27), ("draft-25", DRAFT25), ("draft-24", DRAFT24), ("draft-23", DRAFT23)]
    {
        let mut cc = quiche::Config::new(version)
            .unwrap_or_else(|e| panic!("Config::new({name}) was refused: {e:?}"));
        cc.set_application_protos(pair::ALPN_HQ).expect("set_application_protos");
        cc.verify_peer(false);
        cc.set_max_idle_timeout(30_000);
        cc.set_max_packet_size(pair::MAX_DATAGRAM as u64);
        cc.set_initial_max_data(1_000_000);
        cc.set_initial_max_stream_data_bidi_local(1_000_000);
        cc.set_initial_max_stream_data_bidi_remote(1_000_000);
        cc.set_initial_max_stream_data_uni(1_000_000);
        cc.set_initial_max_streams_bidi(100);
        cc.set_initial_max_streams_uni(100);

        let mut sc = pair::server_config(pair::ALPN_HQ);
        let mut p = pair::Pair::with_configs(&mut cc, &mut sc);
        p.settle(64);

        assert!(
            p.client.is_established() && p.server.is_established(),
            "{name} ({version:#010x}) did not complete a handshake against a default server: \
             client established={} closed={}, server established={} closed={}. All four versions \
             are supposed to be live at this milestone.",
            p.client.is_established(),
            p.client.is_closed(),
            p.server.is_established(),
            p.server.is_closed()
        );
    }
}

/// `cap-retry-integrity-tag`: the Retry packet changed shape.
///
/// At step 01 (draft-23) a Retry carried the original destination connection
/// ID inline and anyone could forge one. `quiche::retry()` takes no version
/// argument at this milestone: a token-less Initial in any of the four
/// supported versions now draws a Retry ending in a 16-byte AEAD tag over a
/// pseudo-packet built from that ODCID, with the ODCID field gone. This group
/// checks the new shape, the tag, and that the token still works.
#[test]
fn cap_retry_integrity_tag() {
    let _lock = testkit::serial_t1();
    let dir = testkit::scratch("cap-retry-integrity-tag");
    let (server, to) = hq_server(&dir, &[("index.txt", b"retry\n".to_vec())]);

    let ep = testkit::Endpoint::new();
    let odcid: Vec<u8> = (0xa0u8..0xb4).collect();
    let scid: Vec<u8> = (0x50u8..0x58).collect();
    let crypto = partial_client_hello();

    let first = support::ClientInitial::new(DRAFT27, &odcid, &scid, &crypto)
        .build()
        .unwrap_or_else(|e| panic!("the verifier could not build its own client Initial: {e}"));
    ep.send_to(&first, to);

    let reply = ep.recv(SHORT).unwrap_or_else(|| {
        panic!(
            "expected the server at {to} to answer a token-less draft-27 Initial within \
             {SHORT:?}; address validation is on by default, so the answer should have been a \
             Retry. Observed no datagram at all.\n--- server log ---\n{}",
            server.log()
        )
    });

    let retry = support::RetryPacket::parse(&reply, DRAFT27).unwrap_or_else(|e| {
        panic!(
            "the answer to a token-less Initial does not parse as a draft-27 Retry: {e}\n\
             datagram {}\n--- server log ---\n{}",
            head(&reply, 96),
            server.log()
        )
    });

    // The shape: no inline ODCID, and a tag at the end.
    assert!(
        retry.odcid.is_none(),
        "a draft-27 Retry carries no original-destination-connection-ID field; the parse found \
         one ({:?}), so the packet still has draft-23's shape.\ndatagram {}\n\
         --- server log ---\n{}",
        retry.odcid.map(testkit::hex),
        head(&reply, 96),
        server.log()
    );
    assert!(
        retry.integrity_tag.is_some(),
        "a draft-27 Retry ends in a 16-byte integrity tag; none was found.\ndatagram {}\n\
         --- server log ---\n{}",
        head(&reply, 96),
        server.log()
    );
    assert!(
        !retry.token.is_empty(),
        "the Retry carried an empty token, so there is nothing for the client to replay.\n\
         --- server log ---\n{}",
        server.log()
    );

    // The tag verifies against the pseudo-packet built from the connection ID
    // *we* chose, which is the whole point of it: a forged Retry cannot
    // produce this tag without having seen the client's first Initial.
    support::verify_retry_integrity(&reply, DRAFT27, &odcid).unwrap_or_else(|e| {
        panic!(
            "the Retry's integrity tag does not verify against the original destination \
             connection ID {}: {e}\ndatagram {}\n--- server log ---\n{}",
            testkit::hex(&odcid),
            head(&reply, 96),
            server.log()
        )
    });

    // A tag computed over the wrong ODCID must not verify, or the check above
    // proves nothing.
    let mut wrong = odcid.clone();
    wrong[0] ^= 0xff;
    assert!(
        support::verify_retry_integrity(&reply, DRAFT27, &wrong).is_err(),
        "the Retry's integrity tag verified against a connection ID it was not computed over \
         ({}), so the tag is not binding.\n--- server log ---\n{}",
        testkit::hex(&wrong),
        server.log()
    );

    // Replaying with the issued token proceeds to a handshake rather than
    // drawing a second Retry. After a Retry the client's destination
    // connection ID -- and so its Initial keys -- become the Retry's source.
    let token = retry.token.to_vec();
    let new_dcid = retry.scid.to_vec();
    let mut second = support::ClientInitial::new(DRAFT27, &new_dcid, &scid, &crypto);
    second.token = &token;
    let second = second
        .build()
        .unwrap_or_else(|e| panic!("the verifier could not build its second Initial: {e}"));
    ep.send_to(&second, to);

    let reply2 = ep.recv(SHORT).unwrap_or_else(|| {
        panic!(
            "expected the server to answer an Initial carrying the token it just issued, within \
             {SHORT:?}; observed no datagram at all.\n--- server log ---\n{}",
            server.log()
        )
    });
    assert!(
        support::is_long_header(reply2[0]),
        "expected a long-header server Initial in answer to a token-carrying Initial; observed \
         first byte {:02x}.\ndatagram {}\n--- server log ---\n{}",
        reply2[0],
        head(&reply2, 64),
        server.log()
    );
    let hdr2 = support::LongHeader::parse(&reply2).unwrap_or_else(|e| {
        panic!(
            "the answer to the token-carrying Initial does not parse as a long header: {e}\n\
             datagram {}\n--- server log ---\n{}",
            head(&reply2, 96),
            server.log()
        )
    });
    assert_ne!(
        hdr2.ty,
        support::LongPacketType::Retry,
        "the server issued a second Retry for an Initial that carried the token from the \
         first, so the token is not being accepted.\ndatagram {}\n--- server log ---\n{}",
        head(&reply2, 96),
        server.log()
    );
    assert_eq!(
        hdr2.ty,
        support::LongPacketType::Initial,
        "expected a server Initial once the token was presented; observed {:?}.\ndatagram {}\n\
         --- server log ---\n{}",
        hdr2.ty,
        head(&reply2, 96),
        server.log()
    );

    drop(server);
}

/// `cap-pluggable-cc`: congestion control became a choice, and the choice is
/// wired to something real.
///
/// The failure worth catching is a selector that is stored and ignored: an
/// implementation where `set_cc_algorithm_name` returns `Ok` for anything and
/// every connection runs the same hard-coded controller. So this group checks
/// the rejections as carefully as the acceptances, and then checks that a
/// connection built each way actually moves data and opens its window.
#[test]
fn cap_pluggable_cc() {
    // Accepted by name.
    let mut c = quiche::Config::new(DRAFT27).expect("Config::new");
    c.set_cc_algorithm_name("reno").expect("set_cc_algorithm_name(\"reno\") must be accepted");

    // Refused by name. A silent fallback to the default is the bug.
    for bad in ["cubic", "bbr", "", "Reno", "reno "] {
        let mut c = quiche::Config::new(DRAFT27).expect("Config::new");
        assert!(
            c.set_cc_algorithm_name(bad).is_err(),
            "set_cc_algorithm_name({bad:?}) returned Ok. Only \"reno\" is registered at this \
             milestone; anything else must be refused rather than silently falling back."
        );
    }

    // Accepted as an enum value, without going through a string at all.
    let mut c = quiche::Config::new(DRAFT27).expect("Config::new");
    c.set_cc_algorithm(quiche::CongestionControlAlgorithm::Reno);
    drop(c);

    // All three routes carry data and open their window.
    const LEN: usize = 64 * 1024;
    let data = testkit::body(LEN, 0xcc02);
    for route in ["by-name", "by-enum", "default"] {
        let mut cc = pair::client_config(pair::ALPN_HQ);
        let mut sc = pair::server_config(pair::ALPN_HQ);
        match route {
            "by-name" => {
                cc.set_cc_algorithm_name("reno").expect("reno by name");
                sc.set_cc_algorithm_name("reno").expect("reno by name");
            },
            "by-enum" => {
                cc.set_cc_algorithm(quiche::CongestionControlAlgorithm::Reno);
                sc.set_cc_algorithm(quiche::CongestionControlAlgorithm::Reno);
            },
            _ => {},
        }

        let mut p = pair::Pair::with_configs(&mut cc, &mut sc);
        p.handshake();

        let initial_cwnd = p.client.stats().cwnd;
        assert!(
            initial_cwnd > 0,
            "{route}: the congestion window is {initial_cwnd} before any data has moved"
        );

        let mut sent = 0usize;
        let mut recv = Vec::new();
        let mut rounds = 0usize;
        while sent < LEN || recv.len() < LEN {
            rounds += 1;
            assert!(
                rounds <= 512,
                "{route}: transfer stalled after {rounds} rounds with {sent} of {LEN} sent and \
                 {} received",
                recv.len()
            );
            if sent < LEN {
                let n = p.client.stream_send(0, &data[sent..], sent + 1 >= LEN).expect("stream_send");
                sent += n;
            }
            p.settle(16);
            let (chunk, _) = pair::read_stream(&mut p.server, 0);
            recv.extend_from_slice(&chunk);
        }

        assert_eq!(recv.len(), LEN, "{route}: server read {} of {LEN} bytes", recv.len());
        assert!(recv == data, "{route}: the bytes the server read are not the bytes sent");

        let grown = p.client.stats().cwnd;
        assert!(
            grown > initial_cwnd,
            "{route}: the congestion window did not grow across a {LEN}-byte transfer -- it was \
             {initial_cwnd} before and {grown} after. A selector wired to a real controller opens \
             its window under acknowledged data."
        );
    }
}

/// `cap-delivery-rate-stats`: `Stats` gained a delivery-rate estimate, and it
/// is an estimate of something rather than a constant.
///
/// The estimate is a rate sample: bytes acknowledged in the most recent
/// interval, divided by that interval. So the way to show it is wired to
/// reality is to change reality and watch it move -- the same transfer, once
/// as fast as the harness can drive it and once with the acknowledgements
/// deliberately held back, must not report the same rate.
#[test]
fn cap_delivery_rate_stats() {
    const LEN: usize = 256 * 1024;

    /// Move `LEN` bytes through a fresh pair, sleeping `pace` before each
    /// exchange. Returns the reported delivery rate and the wall-clock rate
    /// the harness measured for itself.
    fn transfer(pace: Duration) -> (f64, f64, quiche::Stats) {
        let mut cc = pair::client_config(pair::ALPN_HQ);
        let mut sc = pair::server_config(pair::ALPN_HQ);
        let mut p = pair::Pair::with_configs(&mut cc, &mut sc);
        p.handshake();

        let data = testkit::body(LEN, 0xd4e1);
        let started = Instant::now();
        let mut sent = 0usize;
        let mut recv = Vec::new();
        let mut rounds = 0usize;
        while sent < LEN || recv.len() < LEN {
            rounds += 1;
            assert!(
                rounds <= 4096,
                "transfer stalled after {rounds} rounds with {sent} of {LEN} sent and {} received",
                recv.len()
            );
            if sent < LEN {
                let n =
                    p.client.stream_send(0, &data[sent..], sent + 1 >= LEN).expect("stream_send");
                sent += n;
            }
            if !pace.is_zero() {
                std::thread::sleep(pace);
            }
            p.settle(4);
            let (chunk, _) = pair::read_stream(&mut p.server, 0);
            recv.extend_from_slice(&chunk);
        }
        let elapsed = started.elapsed();

        assert_eq!(recv.len(), LEN, "server read {} of {LEN} bytes", recv.len());
        assert!(recv == data, "the bytes the server read are not the bytes sent");

        let stats = p.client.stats();
        (stats.delivery_rate, LEN as f64 / elapsed.as_secs_f64().max(1e-9), stats)
    }

    // Held back: three milliseconds of silence before every exchange, which
    // dwarfs the microseconds an in-memory pair would otherwise take.
    let (slow_rate, slow_measured, slow_stats) = transfer(Duration::from_millis(3));
    // Unhindered.
    let (fast_rate, fast_measured, fast_stats) = transfer(Duration::ZERO);

    for (name, rate) in [("paced", slow_rate), ("unpaced", fast_rate)] {
        assert!(
            rate > 0.0 && rate.is_finite(),
            "after moving {LEN} bytes the {name} connection reports a delivery rate of \
             {rate} bytes/s"
        );
    }

    // The estimate has to move with the thing it estimates. A field that is
    // present but hard-coded, or that echoes the congestion window, or that
    // reports the same number for both transfers, fails here.
    assert!(
        fast_rate > slow_rate * 2.0,
        "the delivery-rate estimate does not track delivery. The same {LEN}-byte transfer \
         reported {slow_rate:.0} bytes/s when the harness paced it to {slow_measured:.0} \
         bytes/s and {fast_rate:.0} bytes/s when it ran at {fast_measured:.0} bytes/s -- a \
         real estimate of the first is well under half the second."
    );

    // And it is an estimate of *this* transfer, not an arbitrary positive
    // number: on the paced run, where the harness's own pacing dominates
    // every interval the sampler can see, the two must agree to within an
    // order of magnitude.
    let ratio = slow_rate / slow_measured;
    assert!(
        (0.1..=10.0).contains(&ratio),
        "on a transfer the harness paced to {slow_measured:.0} bytes/s the reported delivery \
         rate is {slow_rate:.0} bytes/s, a factor of {ratio:.3} out. A rate this far from the \
         one the harness timed is not an estimate of this transfer."
    );

    // The five fields step 01 established are still there and still populated.
    for (name, stats) in [("paced", &slow_stats), ("unpaced", &fast_stats)] {
        assert!(stats.sent > 0, "{name}: the sent-packet count is {}", stats.sent);
        assert!(stats.recv > 0, "{name}: the received-packet count is {}", stats.recv);
        assert!(stats.cwnd > 0, "{name}: the congestion window is {}", stats.cwnd);
        assert!(stats.rtt > Duration::ZERO, "{name}: the round-trip estimate is {:?}", stats.rtt);
        let _ = stats.lost;
    }
}

/// `cap-duplicate-push-removed`: HTTP/3 frame type 0x0E is no longer a frame
/// this release knows, and an unknown type must be skipped rather than
/// rejected.
///
/// PLAN CORRECTION. The group plan called this "duplicate push is now a
/// violation" and expected a connection error. It is the opposite: at 0.3.0
/// the DUPLICATE_PUSH type id is simply gone from the frame constants, the
/// parser's catch-all turns it into the unknown variant, and the HTTP/3 layer
/// drops unknown frames on the floor -- which is what the specification
/// requires. A candidate that raised an error here would be wrong.
#[test]
fn cap_duplicate_push_removed() {
    let mut cc = pair::client_config(pair::ALPN_H3);
    let mut sc = pair::server_config(pair::ALPN_H3);
    let mut p = pair::Pair::with_configs(&mut cc, &mut sc);
    p.handshake();

    let h3 = quiche::h3::Config::new().expect("h3::Config::new");
    let mut h3_client = quiche::h3::Connection::with_transport(&mut p.client, &h3)
        .expect("h3 client on an established connection");
    let mut h3_server = quiche::h3::Connection::with_transport(&mut p.server, &h3)
        .expect("h3 server on an established connection");
    p.settle(16);

    // Frame type 0x0E with a one-byte payload, written by hand onto the
    // stream the h3 client is about to make its request on. Both the type and
    // the length are varints; both fit in one byte here.
    let unknown_frame = [0x0E, 0x01, 0xff];
    p.client
        .stream_send(0, &unknown_frame, false)
        .expect("writing an unknown HTTP/3 frame onto the request stream");

    let req = vec![
        quiche::h3::Header::new(":method", "GET"),
        quiche::h3::Header::new(":scheme", "https"),
        quiche::h3::Header::new(":authority", "quic.test"),
        quiche::h3::Header::new(":path", "/after-unknown"),
    ];
    let stream_id = h3_client
        .send_request(&mut p.client, &req, true)
        .expect("send_request after the unknown frame");
    assert_eq!(
        stream_id, 0,
        "this group needs the request to land on the same stream as the unknown frame; it went \
         to stream {stream_id}"
    );

    p.settle(64);

    // The server must deliver the request that followed the unknown frame,
    // and must not have torn the connection down over it.
    let mut saw_headers = false;
    loop {
        match h3_server.poll(&mut p.server) {
            Ok((id, quiche::h3::Event::Headers { list, .. })) => {
                assert_eq!(id, 0, "headers arrived on stream {id}, expected 0");
                let path = list
                    .iter()
                    .find(|h| h.name() == ":path")
                    .map(|h| h.value().to_string())
                    .unwrap_or_default();
                assert_eq!(
                    path, "/after-unknown",
                    "the delivered request is not the one sent after the unknown frame"
                );
                saw_headers = true;
            },
            Ok((_, quiche::h3::Event::Finished)) => break,
            Ok(_) => {},
            Err(quiche::h3::Error::Done) => break,
            Err(e) => panic!(
                "an HTTP/3 frame of the unknown type 0x0E must be ignored, but polling the \
                 server raised {e:?}. At this milestone 0x0E is not a defined frame type and \
                 the rule for unknown types is to skip them."
            ),
        }
    }

    assert!(
        saw_headers,
        "the HEADERS frame that followed an unknown 0x0E frame on the same stream was never \
         delivered: the unknown frame was not skipped, it swallowed what came after it"
    );
    assert!(
        !p.server.is_closed(),
        "the server closed the connection after receiving an unknown HTTP/3 frame type"
    );
    assert!(
        !p.client.is_closed(),
        "the client connection was closed after sending an unknown HTTP/3 frame type"
    );
}

/// `cap-h3-27-alpn`: the HTTP/3 ALPN moved to `h3-27`, and the older names
/// stayed on offer.
///
/// The first half is a plain GET through the candidate's own binaries. The
/// second half asks the running server what it will actually negotiate, by
/// handshaking against it over UDP from this process -- the only way to
/// observe a process's ALPN preference rather than take its word for it.
#[test]
fn cap_h3_27_alpn() {
    let _lock = testkit::serial_t1();
    let dir = testkit::scratch("cap-h3-27-alpn");
    let want = testkit::body(16 * 1024, 0x27a1);
    let cwd = testkit::example_server_cwd(&dir, &[("alpn.bin", want.clone())]);

    let server_bin = testkit::h3_server_bin();
    let client_bin = testkit::h3_client_bin();

    let mut sc = testkit::cmd(&server_bin);
    sc.current_dir(&cwd);
    testkit::configure_server(&mut sc, &cwd, testkit::EXAMPLE_SERVER_PORT);
    let server =
        testkit::spawn_server(&mut sc, testkit::EXAMPLE_SERVER_PORT, &dir, "h3-server", SHORT);
    let to = SocketAddr::from((Ipv4Addr::LOCALHOST, testkit::EXAMPLE_SERVER_PORT));

    // Half one: a real request over the default ALPN list.
    let url = format!("https://127.0.0.1:{}/alpn.bin", testkit::EXAMPLE_SERVER_PORT);
    let mut cc = testkit::cmd(&client_bin);
    cc.arg(&url);
    testkit::configure_client(&mut cc);
    let run = testkit::run(&mut cc, LONG, &dir, "h3-client");
    let log = server.log();
    assert_eq!(
        run.status.and_then(|s| s.code()),
        Some(0),
        "expected {} to GET {url} and exit 0; observed status {:?} (timed out: {}).\n\
         --- client stderr ---\n{}\n--- server log ---\n{log}",
        client_bin.display(),
        run.status,
        run.timed_out,
        run.stderr
    );
    assert!(
        run.stdout_bytes == want,
        "the HTTP/3 response body did not come back byte for byte: expected {} bytes, observed \
         {}.\n--- server log ---\n{log}",
        want.len(),
        run.stdout_bytes.len()
    );

    // Half two: which ALPN names this server will answer to. Asking a
    // *process* this can only be done on the wire, so the verifier brings up
    // a real QUIC connection against the server the candidate is running and
    // reads the negotiated protocol off it.
    //
    // PLAN CORRECTION. The group plan wanted to prove that the server prefers
    // h3-27 by offering the four names worst-first. That is not observable:
    // the selection callback walks the *client's* list outermost and takes
    // the first name the server also knows, so the client's order decides and
    // offering h3-23 first yields h3-23 no matter what the server prefers.
    // What is observable, and is the actual milestone, is the membership of
    // the set: h3-27 is now accepted, h3-23 still is, and nothing outside the
    // release's four is.
    for name in ["h3-27", "h3-25", "h3-24", "h3-23"] {
        let mut offer = vec![name.len() as u8];
        offer.extend_from_slice(name.as_bytes());
        let chosen = udp_handshake(to, &offer, DRAFT27).unwrap_or_else(|e| {
            panic!(
                "a client offering only {name} could not handshake with the candidate's \
                 http3-server: {e}. All four HTTP/3 ALPN names are supposed to be on offer at \
                 this milestone -- h3-27 because it is new, the other three because adding it \
                 did not remove them.\n--- server log ---\n{}",
                server.log()
            )
        });
        assert_eq!(
            chosen,
            name.as_bytes().to_vec(),
            "a client offering only {name} negotiated {:?}",
            String::from_utf8_lossy(&chosen)
        );
    }

    // And the set really is a set. h3-29 is the next name this library will
    // grow into and is not on offer yet; a server that answered to it would
    // be matching a prefix or waving everything through.
    let outside = udp_handshake(to, b"\x05h3-29", DRAFT27);
    assert!(
        outside.as_deref().map(<[u8]>::is_empty).unwrap_or(true),
        "a client offering only h3-29 -- a name this release does not know -- negotiated {:?}. \
         The server is supposed to select nothing.\n--- server log ---\n{}",
        outside.as_ref().map(|v| String::from_utf8_lossy(v).into_owned()),
        server.log()
    );

    drop(server);
}
