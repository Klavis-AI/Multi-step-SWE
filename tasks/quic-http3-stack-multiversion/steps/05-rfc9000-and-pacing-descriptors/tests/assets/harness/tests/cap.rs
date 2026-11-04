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

use std::net::SocketAddr;
use std::time::{Duration, Instant};

#[allow(unused_imports)]
use verifier::{pair, support, testkit};

#[allow(dead_code)]
const SHORT: Duration = Duration::from_secs(10);
#[allow(dead_code)]
const LONG: Duration = Duration::from_secs(60);

// ---------------------------------------------------------------------------
// shared helpers
// ---------------------------------------------------------------------------

/// The head of a datagram as hex, with its true length, for panic messages.
fn head(d: &[u8], n: usize) -> String {
    format!("{} ({} bytes total)", testkit::hex(&d[..n.min(d.len())]), d.len())
}

/// Start the flagless example server on [`testkit::EXAMPLE_SERVER_PORT`].
///
/// At this milestone the example server takes no arguments at all: it binds
/// 127.0.0.1:4433 and opens `examples/cert.crt`, `examples/cert.key` and
/// `examples/root` relative to its working directory. So the group stages
/// that shape under its own scratch space and starts the server there --
/// nothing is written into the candidate's tree. The caller must already
/// hold [`testkit::serial_t1`], because the port is compiled in.
fn start_example_server(dir: &std::path::Path, tag: &str) -> testkit::Server {
    let cwd = testkit::example_server_cwd(dir, &[("index.txt", b"hello\n".to_vec())]);
    let bin = testkit::server_bin();
    let mut c = testkit::cmd(&bin);
    c.current_dir(&cwd);
    testkit::spawn_server(&mut c, testkit::EXAMPLE_SERVER_PORT, dir, tag, LONG)
}

/// A well-formed *start* of a ClientHello that promises more bytes than the
/// packet carries.
///
/// A server that gets this far buffers it and waits for the rest instead of
/// tearing the connection down, which is what makes the answer to a retried
/// Initial a plain acknowledgement rather than a race between error paths.
/// These groups are about version support and packet protection, not TLS.
fn partial_client_hello() -> Vec<u8> {
    let mut crypto = vec![0x01, 0x00, 0x04, 0x00, 0x03, 0x03];
    crypto.extend(std::iter::repeat(0x42).take(32));
    crypto
}

/// What one address-validation exchange left behind.
struct Dance {
    /// The Retry datagram the server sent in answer to the token-less Initial.
    retry: Vec<u8>,
    /// The Retry's Source Connection ID: the client's new Destination
    /// Connection ID, and the input to the Initial key schedule from here on.
    new_dcid: Vec<u8>,
    /// The server's answer to the retried Initial.
    answer: Vec<u8>,
}

/// Drive one complete `Initial -> Retry -> Initial+token -> Initial` exchange
/// at `version` against the server at `to`, and hand back the two datagrams
/// the server produced.
///
/// Everything here is the verifier's own QUIC: the candidate's library builds
/// neither of the two packets the verifier sends, and reads neither of the
/// two it checks.
fn retry_dance(
    ep: &testkit::Endpoint, to: SocketAddr, version: u32, odcid: &[u8], scid: &[u8],
    server: &testkit::Server,
) -> Dance {
    let crypto = partial_client_hello();

    let first = support::ClientInitial::new(version, odcid, scid, &crypto)
        .build()
        .unwrap_or_else(|e| {
            panic!("the verifier could not build its own {version:#010x} Initial: {e}")
        });
    ep.send_to(&first, to);

    let retry = ep.recv(SHORT).unwrap_or_else(|| {
        panic!(
            "expected the server at {to} to answer a token-less version {version:#010x} Initial \
             within {SHORT:?}; observed no datagram at all. The example server does address \
             validation on every new connection, so the answer should have been a Retry.\n\
             --- server log ---\n{}",
            server.log()
        )
    });

    assert!(
        support::is_long_header(retry[0]),
        "expected a long-header Retry in answer to a token-less version {version:#010x} Initial; \
         observed first byte {:02x}, datagram {}.\n--- server log ---\n{}",
        retry[0],
        head(&retry, 64),
        server.log()
    );
    assert_eq!(
        (retry[0] & 0x30) >> 4,
        3,
        "expected the Retry value 3 in the long header type bits (4-5 of the first byte) in \
         answer to a token-less version {version:#010x} Initial; observed first byte {:02x}, so \
         type bits {}, datagram {}.\n--- server log ---\n{}",
        retry[0],
        (retry[0] & 0x30) >> 4,
        head(&retry, 64),
        server.log()
    );

    let parsed = support::RetryPacket::parse(&retry, version).unwrap_or_else(|e| {
        panic!(
            "expected a Retry carrying the version the verifier used, {version:#010x}, then the \
             two connection ids, the token and a 16-byte integrity tag; observed {e}, datagram \
             {}.\n--- server log ---\n{}",
            head(&retry, 64),
            server.log()
        )
    });

    assert_eq!(
        testkit::hex(parsed.dcid),
        testkit::hex(scid),
        "expected the Retry to be addressed to the source connection id the verifier used, {}; \
         observed a destination connection id of {}, datagram {}.",
        testkit::hex(scid),
        testkit::hex(parsed.dcid),
        head(&retry, 64)
    );
    assert!(
        !parsed.token.is_empty(),
        "expected a non-empty address validation token in the Retry; observed an empty one, \
         datagram {}.\n--- server log ---\n{}",
        head(&retry, 64),
        server.log()
    );
    assert!(
        !parsed.scid.is_empty(),
        "expected the Retry to carry a source connection id for the client to address its next \
         Initial to; observed an empty one, datagram {}.",
        head(&retry, 64)
    );

    let new_dcid = parsed.scid.to_vec();
    let token = parsed.token.to_vec();

    let mut second = support::ClientInitial::new(version, &new_dcid, scid, &crypto);
    second.token = &token;
    let second = second.build().unwrap_or_else(|e| {
        panic!("the verifier could not build its retried {version:#010x} Initial: {e}")
    });
    ep.send_to(&second, to);

    let answer = ep.recv(SHORT).unwrap_or_else(|| {
        panic!(
            "expected the server at {to} to answer a version {version:#010x} Initial carrying \
             the token it had just minted ({}) within {SHORT:?}; observed no datagram at \
             all.\n--- server log ---\n{}",
            testkit::hex(&token),
            server.log()
        )
    });

    assert_ne!(
        (answer[0] & 0x30) >> 4,
        3,
        "expected the server to accept the token it minted and proceed; observed a second Retry, \
         datagram {}.\n--- server log ---\n{}",
        head(&answer, 64),
        server.log()
    );
    assert_eq!(
        (answer[0] & 0x30) >> 4,
        0,
        "expected an Initial packet (long header type bits 0) in answer to the retried version \
         {version:#010x} Initial; observed first byte {:02x}, so type bits {}, datagram {}.\n\
         --- server log ---\n{}",
        answer[0],
        (answer[0] & 0x30) >> 4,
        head(&answer, 64),
        server.log()
    );

    Dance { retry, new_dcid, answer }
}

/// Open the server's answer with Initial keys the verifier derives itself from
/// `version`'s salt and `dcid`, and assert it acknowledges packet number 0.
fn expect_ack_under(d: &Dance, version: u32, server: &testkit::Server) {
    assert_eq!(
        support::peek_version(&d.answer).unwrap_or(0),
        version,
        "expected the server's answer to carry the same version the verifier used, \
         {version:#010x}; observed {:#010x}, datagram {}.",
        support::peek_version(&d.answer).unwrap_or(0),
        head(&d.answer, 64)
    );

    let opened = support::decrypt_initial_with_odcid(
        &d.answer,
        version,
        support::Perspective::Server,
        &d.new_dcid,
    )
    .unwrap_or_else(|e| {
        panic!(
            "expected the server's answer to the retried version {version:#010x} Initial to be \
             protected with Initial keys derived from that version's salt and the Retry's source \
             connection id {}; observed {e}, datagram {}.\n--- server log ---\n{}",
            testkit::hex(&d.new_dcid),
            head(&d.answer, 64),
            server.log()
        )
    });

    let frames: Vec<String> = opened.frames().map(|f| format!("{f:?}")).collect();
    let acked = opened.frames().any(|f| {
        matches!(f, Ok(support::Frame::Ack { ranges, .. }) if ranges.iter().any(|(lo, _hi)| *lo == 0))
    });
    assert!(
        acked,
        "expected the server to acknowledge the retried version {version:#010x} Initial it just \
         accepted, so an ACK covering packet number 0; observed the frames {frames:?}.\n\
         payload {}\n--- server log ---\n{}",
        head(&opened.payload, 64),
        server.log()
    );
}

// ---------------------------------------------------------------------------
// groups
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// cap-rfc9000-v1  (T2)
// ---------------------------------------------------------------------------

/// `cap-rfc9000-v1`: the candidate speaks QUIC version 1, `0x00000001`, and
/// keys it from the RFC 9001 salts rather than a draft's.
///
/// The verifier makes the candidate's server do address validation over
/// version 1 and then satisfies it. Three independent things have to be right
/// for that to work, and none of them is right in a draft-only stack:
///
/// * `0x00000001` has to be in the supported-version list at all, or the
///   answer to the first Initial is a Version Negotiation packet and not a
///   Retry;
/// * the Retry Integrity Tag has to be computed with RFC 9001 Section 5.8's
///   version 1 key and nonce, which differ from every draft's -- the verifier
///   recomputes it over the Original Destination Connection ID it chose;
/// * the answer to the retried Initial has to be protected with keys derived
///   from RFC 9001 Section 5.2's version 1 Initial salt, which the verifier
///   derives for itself from the Retry's Source Connection ID.
///
/// `quiche::PROTOCOL_VERSION` is checked first because it is the one-line
/// statement of the same fact, and a stack whose default is still a draft
/// fails here with a legible message instead of a timeout.
#[test]
fn cap_rfc9000_v1() {
    assert_eq!(
        quiche::PROTOCOL_VERSION,
        0x0000_0001,
        "expected PROTOCOL_VERSION to be RFC 9000's version 1, 0x00000001; observed {:#010x}",
        quiche::PROTOCOL_VERSION
    );

    let _lock = testkit::serial_t1();
    let dir = testkit::scratch("cap-rfc9000-v1");
    let server = start_example_server(&dir, "v1-server");
    let to = server.addr();

    let ep = testkit::Endpoint::new();
    let odcid: Vec<u8> = (0xa0u8..0xa8).collect();
    let scid: Vec<u8> = (0x50u8..0x58).collect();

    let d = retry_dance(&ep, to, support::VERSION_V1, &odcid, &scid, &server);

    support::verify_retry_integrity(&d.retry, support::VERSION_V1, &odcid).unwrap_or_else(|e| {
        panic!(
            "expected the Retry's integrity tag to authenticate the original destination \
             connection id {} under RFC 9001 Section 5.8's version 1 key and nonce; observed {e}, \
             datagram {}.\n--- server log ---\n{}",
            testkit::hex(&odcid),
            head(&d.retry, 64),
            server.log()
        )
    });

    expect_ack_under(&d, support::VERSION_V1, &server);

    drop(server);
}

// ---------------------------------------------------------------------------
// cap-two-salts  (T2)
// ---------------------------------------------------------------------------

/// `cap-two-salts`: one server process, two live key schedules.
///
/// The interesting part of adding version 1 is not that the drafts were
/// replaced -- they were not. A single server has to hold RFC 9001's salt and
/// draft-29's at the same time and pick per packet, and it has to advertise
/// both when it negotiates versions. The group proves all three:
///
/// 1. an Initial at a version the candidate does not support (draft-23,
///    `0xff000017`) draws a Version Negotiation packet, and the list in it
///    contains `0x00000001` *and* `0xff00001d`;
/// 2. a complete draft-29 exchange succeeds and its answer opens under the
///    draft-29 Initial salt;
/// 3. a complete version 1 exchange against the same process succeeds and its
///    answer opens under the version 1 Initial salt.
///
/// Each Retry is then checked against the *other* version's Retry Integrity
/// key and has to fail: the two schedules are genuinely different keys and not
/// one schedule with the version number painted on.
#[test]
fn cap_two_salts() {
    let _lock = testkit::serial_t1();
    let dir = testkit::scratch("cap-two-salts");
    let server = start_example_server(&dir, "salts-server");
    let to = server.addr();

    // 1. What does this server say it supports?
    let vn_ep = testkit::Endpoint::new();
    let vn_odcid: Vec<u8> = (0xc0u8..0xc8).collect();
    let vn_scid: Vec<u8> = (0x60u8..0x68).collect();
    let probe = support::ClientInitial::new(
        support::VERSION_DRAFT23,
        &vn_odcid,
        &vn_scid,
        &partial_client_hello(),
    )
    .build()
    .expect("the verifier could not build its own draft-23 Initial");
    vn_ep.send_to(&probe, to);

    let vn = vn_ep.recv(SHORT).unwrap_or_else(|| {
        panic!(
            "expected the server at {to} to answer a draft-23 Initial (version 0xff000017, which \
             this milestone does not support) with a Version Negotiation packet within {SHORT:?}; \
             observed no datagram at all.\n--- server log ---\n{}",
            server.log()
        )
    });
    let vn_parsed = support::VersionNegotiationPacket::parse(&vn).unwrap_or_else(|e| {
        panic!(
            "expected a Version Negotiation packet -- long header, a zero version field, the two \
             connection ids and then a list of 32-bit versions; observed {e}, datagram {}.\n\
             --- server log ---\n{}",
            head(&vn, 64),
            server.log()
        )
    });
    let offered: Vec<String> = vn_parsed.versions.iter().map(|v| format!("{v:#010x}")).collect();
    assert!(
        vn_parsed.versions.contains(&support::VERSION_V1),
        "expected the Version Negotiation list to offer RFC 9000's version 1, 0x00000001; \
         observed {offered:?}.\n--- server log ---\n{}",
        server.log()
    );
    assert!(
        vn_parsed.versions.contains(&support::VERSION_DRAFT29),
        "expected the Version Negotiation list to still offer draft-29, 0xff00001d, beside \
         version 1; observed {offered:?}.\n--- server log ---\n{}",
        server.log()
    );

    // 2. A whole draft-29 connection attempt.
    let d29_ep = testkit::Endpoint::new();
    let d29_odcid: Vec<u8> = (0xb0u8..0xb8).collect();
    let d29_scid: Vec<u8> = (0x70u8..0x78).collect();
    let d29 = retry_dance(
        &d29_ep,
        to,
        support::VERSION_DRAFT29,
        &d29_odcid,
        &d29_scid,
        &server,
    );
    support::verify_retry_integrity(&d29.retry, support::VERSION_DRAFT29, &d29_odcid)
        .unwrap_or_else(|e| {
            panic!(
                "expected the draft-29 Retry's integrity tag to authenticate {} under draft-29's \
                 Retry key and nonce; observed {e}, datagram {}.\n--- server log ---\n{}",
                testkit::hex(&d29_odcid),
                head(&d29.retry, 64),
                server.log()
            )
        });
    expect_ack_under(&d29, support::VERSION_DRAFT29, &server);

    // 3. A whole version 1 connection attempt against the same process.
    let v1_ep = testkit::Endpoint::new();
    let v1_odcid: Vec<u8> = (0xd0u8..0xd8).collect();
    let v1_scid: Vec<u8> = (0x80u8..0x88).collect();
    let v1 = retry_dance(&v1_ep, to, support::VERSION_V1, &v1_odcid, &v1_scid, &server);
    support::verify_retry_integrity(&v1.retry, support::VERSION_V1, &v1_odcid).unwrap_or_else(
        |e| {
            panic!(
                "expected the version 1 Retry's integrity tag to authenticate {} under RFC 9001 \
                 Section 5.8's key and nonce; observed {e}, datagram {}.\n--- server log ---\n{}",
                testkit::hex(&v1_odcid),
                head(&v1.retry, 64),
                server.log()
            )
        },
    );
    expect_ack_under(&v1, support::VERSION_V1, &server);

    // The two schedules have to be different keys, not one key relabelled.
    // `verify_retry_integrity` recomputes the tag and does not look at the
    // version in the header, so asking it for the wrong version is a real
    // question about the key and nonce.
    assert!(
        support::verify_retry_integrity(&d29.retry, support::VERSION_V1, &d29_odcid).is_err(),
        "the draft-29 Retry's integrity tag also verified under RFC 9001's version 1 Retry key, \
         so the server is using one key schedule for both versions; draft-29 Retry {}.",
        head(&d29.retry, 64)
    );
    assert!(
        support::verify_retry_integrity(&v1.retry, support::VERSION_DRAFT29, &v1_odcid).is_err(),
        "the version 1 Retry's integrity tag also verified under draft-29's Retry key, so the \
         server is using one key schedule for both versions; version 1 Retry {}.",
        head(&v1.retry, 64)
    );

    drop(server);
}

// ---------------------------------------------------------------------------
// cap-client-version-v1  (T2)
// ---------------------------------------------------------------------------

/// `cap-client-version-v1`: the candidate's own client offers version 1 first.
///
/// The example client takes one argument -- the URL -- and no flags at this
/// milestone, so there is nothing to ask it for: whatever version it puts in
/// its first long header is the version the library defaults to. The verifier
/// owns the socket the client is pointed at, catches the first flight, reads
/// the version field, and then opens the packet with version 1 Initial keys it
/// derives itself from the client's own Destination Connection ID. A client
/// that still defaults to a draft fails at the version field; one that writes
/// `0x00000001` in the header but keys from a draft salt fails at the open.
#[test]
fn cap_client_version_v1() {
    let dir = testkit::scratch("cap-client-version-v1");
    let ep = testkit::Endpoint::new();
    let addr = ep.local_addr();

    let client_bin = testkit::client_bin();
    let mut cc = testkit::cmd(&client_bin);
    cc.arg(format!("https://127.0.0.1:{}/index.txt", addr.port()));
    let client = testkit::spawn_proc(&mut cc, &dir, "v1-client");

    let dgram = ep.recv(SHORT);
    let log = client.stop();
    let dgram = dgram.unwrap_or_else(|| {
        panic!(
            "expected {} to send a first flight to 127.0.0.1:{} within {SHORT:?}; observed no \
             datagram at all.\n--- client log ---\n{log}",
            client_bin.display(),
            addr.port()
        )
    });

    assert!(
        support::is_long_header(dgram[0]),
        "expected a long-header packet as the client's first datagram; observed first byte \
         {:02x}, datagram {}.\n--- client log ---\n{log}",
        dgram[0],
        head(&dgram, 48)
    );

    let version = support::peek_version(&dgram).unwrap_or_else(|e| {
        panic!(
            "expected a readable long-header version field in the client's first datagram; \
             observed {e}, datagram {}.\n--- client log ---\n{log}",
            head(&dgram, 48)
        )
    });
    assert_eq!(
        version,
        support::VERSION_V1,
        "expected the client to offer RFC 9000's version 1, 0x00000001, in its first flight; \
         observed {version:#010x}, datagram {}.\n--- client log ---\n{log}",
        head(&dgram, 48)
    );

    let initial = support::decrypt_initial(&dgram, support::VERSION_V1, support::Perspective::Client)
        .unwrap_or_else(|e| {
            panic!(
                "expected the client's first Initial to be protected with keys derived from RFC \
                 9001 Section 5.2's version 1 salt and its own destination connection id; \
                 observed {e}, datagram {}.\n--- client log ---\n{log}",
                head(&dgram, 48)
            )
        });

    let crypto = initial
        .frames()
        .find_map(|f| match f {
            Ok(support::Frame::Crypto { offset: 0, data }) => Some(data.to_vec()),
            _ => None,
        })
        .unwrap_or_else(|| {
            let seen: Vec<String> = initial.frames().map(|f| format!("{f:?}")).collect();
            panic!(
                "expected a CRYPTO frame at offset 0 carrying the ClientHello in the client's \
                 first Initial; observed the frames {seen:?}.\npayload {}\n--- client log ---\n{log}",
                head(&initial.payload, 64)
            )
        });
    assert_eq!(
        crypto.first().copied(),
        Some(0x01),
        "expected the CRYPTO frame at offset 0 to start a TLS ClientHello (handshake type 1); \
         observed {}.\n--- client log ---\n{log}",
        head(&crypto, 48)
    );
}

// ---------------------------------------------------------------------------
// cap-sendinfo-at  (T3)
// ---------------------------------------------------------------------------

/// `cap-sendinfo-at`: `send` hands back a descriptor, not just a length.
///
/// At this milestone `Connection::send` returns `(usize, SendInfo)`, and the
/// `SendInfo` carries the two things a pacing event loop needs: `to`, the
/// address to send the datagram to, and `at`, the earliest instant at which it
/// should leave. That is the whole reason a caller no longer has to guess a
/// peer address or busy-loop on a timer.
///
/// The group drives a full handshake through the in-memory pair, which records
/// every `SendInfo` both ends produced, and then checks all of them: `to` has
/// to be the address the connection was actually given, and `at` has to be a
/// real send time for this connection -- inside the window the group itself
/// was running in. A descriptor whose `at` is the epoch, or an hour away,
/// would pace a real event loop into a stall and is rejected here.
#[test]
fn cap_sendinfo_at() {
    let opened = Instant::now();
    let mut p = pair::Pair::new(pair::ALPN_HQ);
    p.handshake();
    p.settle(16);
    let settled = Instant::now();

    assert!(
        !p.client_info.is_empty() && !p.server_info.is_empty(),
        "expected both ends to have produced send descriptors during the handshake; observed {} \
         from the client and {} from the server",
        p.client_info.len(),
        p.server_info.len()
    );
    assert_eq!(
        p.client_info.len(),
        p.client_out.len(),
        "expected one SendInfo beside every datagram the client sent"
    );

    // A generous upper edge: the pair is in memory, so no datagram it produced
    // can legitimately be scheduled a second past the moment it stopped
    // exchanging them.
    let latest = settled + Duration::from_secs(1);
    for (i, info) in p.client_info.iter().enumerate() {
        assert!(
            info.at >= opened && info.at <= latest,
            "client datagram {i} came back with a send time outside the window this group ran \
             in: SendInfo.at was {:?} from the group's start and {:?} after it finished \
             exchanging ({} datagrams in all)",
            info.at.checked_duration_since(opened),
            info.at.checked_duration_since(settled),
            p.client_info.len()
        );
        assert_eq!(
            info.to,
            pair::server_addr(),
            "expected every client SendInfo to name the peer address the connection was created \
             with, {}; datagram {i} named {}",
            pair::server_addr(),
            info.to
        );
    }
    for (i, info) in p.server_info.iter().enumerate() {
        assert!(
            info.at >= opened && info.at <= latest,
            "server datagram {i} came back with a send time outside the window this group ran in: \
             SendInfo.at was {:?} from the group's start",
            info.at.checked_duration_since(opened)
        );
        assert_eq!(
            info.to,
            pair::client_addr(),
            "expected every server SendInfo to name the peer address the connection was accepted \
             from, {}; datagram {i} named {}",
            pair::client_addr(),
            info.to
        );
    }

    // One send, bracketed as tightly as the clock allows.
    p.client.stream_send(4, b"GET /index.txt\r\n", false).expect("client stream_send");
    let mut buf = [0u8; pair::MAX_DATAGRAM];
    let before = Instant::now();
    let (len, info) = p
        .client
        .send(&mut buf)
        .expect("expected the established client to have a datagram to send after stream_send");
    let after = Instant::now();

    assert!(
        len > 0,
        "expected send to report how many bytes of the buffer it filled; observed {len}"
    );
    assert!(
        info.at >= opened,
        "the post-handshake send was scheduled before this group even created the connection: \
         SendInfo.at is {:?} earlier than the group's start",
        opened.checked_duration_since(info.at)
    );
    assert!(
        info.at <= after + Duration::from_secs(1),
        "the post-handshake send was scheduled {:?} after the call that produced it returned; a \
         pacing event loop would sit on this datagram",
        info.at.checked_duration_since(after)
    );
    assert!(
        info.at.checked_duration_since(before).is_some(),
        "the post-handshake send was scheduled {:?} before the call that produced it; \
         SendInfo.at has to be an instant at or after the moment the packet was built, and it \
         has to be usable as one -- duration_since from a reading taken just before the call \
         must not underflow",
        before.checked_duration_since(info.at)
    );
    assert_eq!(
        info.to,
        pair::server_addr(),
        "expected the post-handshake SendInfo to name the peer address {}; observed {}",
        pair::server_addr(),
        info.to
    );

    // The descriptor is a plain value: copying it must not move it.
    let copy = info;
    assert_eq!(copy, info, "expected SendInfo to be a comparable, copyable descriptor");
}

// ---------------------------------------------------------------------------
// cap-recvinfo-from  (T3)
// ---------------------------------------------------------------------------

/// `cap-recvinfo-from`: `recv` is told where the datagram came from.
///
/// The counterpart to `SendInfo`. `Connection::recv` takes `(&mut [u8],
/// RecvInfo)` at this milestone, and `RecvInfo.from` is the source address the
/// caller read off its socket -- the connection no longer has to assume every
/// datagram came from the address it was created with.
///
/// The pair feeds every datagram across with the real source address of the
/// side that produced it, so a handshake completing at all already means the
/// two-argument form works end to end. On top of that the group checks that
/// the addresses are carried rather than invented: each side's descriptors
/// name the *other* side's address, and a request crosses and comes back
/// byte for byte, so `recv` is doing the work and not swallowing the datagram.
#[test]
fn cap_recvinfo_from() {
    // The descriptor is a value type that carries an address and nothing else.
    let a = pair::client_addr();
    let b = pair::server_addr();
    assert_eq!(
        quiche::RecvInfo { from: a },
        quiche::RecvInfo { from: a },
        "expected two RecvInfo naming the same source address to compare equal"
    );
    assert_ne!(
        quiche::RecvInfo { from: a },
        quiche::RecvInfo { from: b },
        "expected RecvInfo to carry the source address it was built with; two RecvInfo naming {a} \
         and {b} compared equal"
    );

    let mut p = pair::Pair::new(pair::ALPN_HQ);
    p.handshake();

    let payload = testkit::body(4096, 0x5e_1f);
    let (got, fin) = p.client_to_server(4, &payload, true);
    assert_eq!(
        got.len(),
        payload.len(),
        "expected the server to have read all {} bytes the client sent on stream 4 through \
         recv(buf, RecvInfo); observed {} bytes",
        payload.len(),
        got.len()
    );
    assert!(got == payload, "the {} bytes the server read differ from the bytes sent", got.len());
    assert!(fin, "expected the server to have seen stream 4 finish");

    assert!(
        !p.client_info.is_empty() && !p.server_info.is_empty(),
        "expected datagrams in both directions; observed {} from the client and {} from the server",
        p.client_info.len(),
        p.server_info.len()
    );
    for (i, info) in p.client_info.iter().enumerate() {
        assert_eq!(
            info.to, b,
            "expected client datagram {i} to be addressed to the peer address the connection was \
             created with, {b}; observed {}",
            info.to
        );
    }
    for (i, info) in p.server_info.iter().enumerate() {
        assert_eq!(
            info.to, a,
            "expected server datagram {i} to be addressed back to the source address its recv was \
             told about, {a}; observed {}",
            info.to
        );
    }
}

// ---------------------------------------------------------------------------
// cap-0rtt-client  (T3)
// ---------------------------------------------------------------------------

/// `cap-0rtt-client`: a client can export a session and resume with it.
///
/// This milestone is where the client side of session resumption arrives:
/// `Connection::session` hands out an opaque blob once the server's session
/// ticket has landed, `Connection::set_session` installs one on a fresh
/// connection before the handshake starts, and `Connection::is_resumed` says
/// afterwards whether it was used. Without all three a caller can enable early
/// data on the server and still never resume anything.
///
/// Three connections to the same server configuration. The first has no
/// session and must report that it did not resume; its ticket is exported.
/// The second installs that ticket and both ends must report a resumption.
/// The third is the control: same configuration, no ticket installed, and it
/// must go back to a full handshake -- so `is_resumed` is answering the
/// question rather than returning a constant.
#[test]
fn cap_0rtt_client() {
    // The server's ticket key has to be stable across the two connections or
    // the ticket it issued on the first is undecryptable on the second.
    const TICKET_KEY: [u8; 48] = [0xa; 48];

    // The 48 is a contract, not a convention, and this is the one place it is
    // put under test: BoringSSL's `SSL_CTX_set_tlsext_ticket_keys` refuses any
    // other length outright, and quiche maps that refusal to
    // `Err(Error::TlsFail)` rather than panicking or silently truncating to
    // fit. Run on a throwaway config so nothing below can inherit a
    // half-configured one. No handshake, no socket, no clock.
    {
        let mut probe = pair::server_config(pair::ALPN_HQ);
        for bad in [47_usize, 49] {
            let key = vec![0xa; bad];
            match probe.set_ticket_key(&key) {
                Err(quiche::Error::TlsFail) => {},
                other => panic!(
                    "set_ticket_key() with a {bad}-byte key must be rejected with \
                     Err(Error::TlsFail): the key is a 16-byte key name, a 16-byte AES-128 key \
                     and a 16-byte HMAC key, so 48 bytes is the only well-formed length; \
                     observed {other:?}"
                ),
            }
        }
        probe.set_ticket_key(&TICKET_KEY).expect(
            "set_ticket_key() must still accept a well-formed 48-byte key after refusing a \
             wrong-length one",
        );
    }

    let mut cc = pair::client_config(pair::ALPN_HQ);
    let mut sc = pair::server_config(pair::ALPN_HQ);
    sc.set_ticket_key(&TICKET_KEY).expect("set_ticket_key on the first server config");
    let mut first = pair::Pair::with_configs(&mut cc, &mut sc);
    first.handshake();
    // The session ticket arrives after the handshake is established, so the
    // pair has to be allowed to go quiet before the blob exists.
    first.settle(16);

    assert!(
        !first.client.is_resumed(),
        "expected a client with no session installed to report a full handshake; is_resumed was \
         true on a connection that had nothing to resume from"
    );
    assert!(
        !first.server.is_resumed(),
        "expected the server to report a full handshake for a client with no session; is_resumed \
         was true"
    );

    let session = first.client.session().unwrap_or_else(|| {
        panic!(
            "expected the client to export a session blob once the server's session ticket had \
             arrived; session() was None after an established handshake and {} further datagrams \
             from the server",
            first.server_out.len()
        )
    });
    assert!(!session.is_empty(), "expected a non-empty session blob; observed 0 bytes");

    // Resume.
    let mut cc2 = pair::client_config(pair::ALPN_HQ);
    let mut sc2 = pair::server_config(pair::ALPN_HQ);
    sc2.set_ticket_key(&TICKET_KEY).expect("set_ticket_key on the second server config");
    let mut second = pair::Pair::with_configs(&mut cc2, &mut sc2);
    second
        .client
        .set_session(&session)
        .unwrap_or_else(|e| panic!("set_session rejected the {} byte blob session() produced: {e:?}", session.len()));
    second.handshake();
    second.settle(16);

    assert!(
        second.client.is_resumed(),
        "expected the client to report a resumed handshake after set_session with the {} byte \
         blob the first connection exported; is_resumed was false",
        session.len()
    );
    assert!(
        second.server.is_resumed(),
        "expected the server to report a resumed handshake once the client presented the ticket \
         it had issued; is_resumed was false"
    );

    // Control: same configuration, no session installed.
    let mut cc3 = pair::client_config(pair::ALPN_HQ);
    let mut sc3 = pair::server_config(pair::ALPN_HQ);
    sc3.set_ticket_key(&TICKET_KEY).expect("set_ticket_key on the control server config");
    let mut control = pair::Pair::with_configs(&mut cc3, &mut sc3);
    control.handshake();
    control.settle(16);
    assert!(
        !control.client.is_resumed(),
        "expected a client that installed no session to report a full handshake even against a \
         server that issues tickets; is_resumed was true"
    );
    assert!(
        !control.server.is_resumed(),
        "expected the server to report a full handshake for a client that presented no ticket; \
         is_resumed was true"
    );
}

// ---------------------------------------------------------------------------
// cap-h3-alpn-h3  (T3)
// ---------------------------------------------------------------------------

/// `cap-h3-alpn-h3`: HTTP/3 now offers the unversioned `h3` token.
///
/// `h3::APPLICATION_PROTOCOL` gained `h3` at the front when the transport
/// reached version 1, and kept `h3-29`, `h3-28` and `h3-27` behind it, because
/// a version 1 stack still has to talk to draft peers. Both halves matter, so
/// the group checks both: a pair offering the constant on both sides settles
/// on `h3`, and a pair whose server offers only `h3-29` still settles on
/// `h3-29` rather than failing to negotiate.
///
/// The constant itself is checked byte for byte, including the length prefixes
/// -- it is a wire-format value, and `h3` without its `\x02` is a different
/// ALPN list.
#[test]
fn cap_h3_alpn_h3() {
    assert_eq!(
        quiche::h3::APPLICATION_PROTOCOL,
        b"\x02h3\x05h3-29\x05h3-28\x05h3-27",
        "expected h3::APPLICATION_PROTOCOL to offer the unversioned h3 token first and keep the \
         three drafts behind it; observed {}",
        testkit::hex(quiche::h3::APPLICATION_PROTOCOL)
    );

    // Both ends offer the whole list: the unversioned token wins.
    let mut p = pair::Pair::new(quiche::h3::APPLICATION_PROTOCOL);
    p.handshake();
    assert_eq!(
        p.client.application_proto(),
        b"h3",
        "expected two peers offering h3::APPLICATION_PROTOCOL to negotiate the unversioned h3; \
         the client settled on {:?}",
        String::from_utf8_lossy(p.client.application_proto())
    );
    assert_eq!(
        p.server.application_proto(),
        b"h3",
        "expected the server to agree on h3; it settled on {:?}",
        String::from_utf8_lossy(p.server.application_proto())
    );

    // And the transport is actually usable as an HTTP/3 connection.
    let h3_config = quiche::h3::Config::new().expect("h3::Config::new");
    quiche::h3::Connection::with_transport(&mut p.client, &h3_config)
        .expect("expected an h3 connection over a transport that negotiated h3 (client side)");
    quiche::h3::Connection::with_transport(&mut p.server, &h3_config)
        .expect("expected an h3 connection over a transport that negotiated h3 (server side)");

    // A draft-only peer is still reachable, so the drafts were added to rather
    // than replaced.
    let mut cc = pair::client_config(quiche::h3::APPLICATION_PROTOCOL);
    let mut sc = pair::server_config(b"\x05h3-29");
    let mut q = pair::Pair::with_configs(&mut cc, &mut sc);
    q.handshake();
    assert_eq!(
        q.client.application_proto(),
        b"h3-29",
        "expected a client offering h3::APPLICATION_PROTOCOL to fall back to h3-29 against a \
         server that offers only h3-29; it settled on {:?}",
        String::from_utf8_lossy(q.client.application_proto())
    );
}

// ---------------------------------------------------------------------------
// cap-invalid-stream-state  (T3)
// ---------------------------------------------------------------------------

/// `cap-invalid-stream-state`: the error says *which* stream.
///
/// `Error::InvalidStreamState` carries the stream id at this milestone. It
/// used to be a bare variant, which made it useless to an application
/// multiplexing hundreds of streams: the error told it something was wrong
/// without telling it where.
///
/// Reading your own unidirectional stream is the cheapest way to provoke it --
/// a client-initiated unidirectional stream is write-only from the client's
/// side, by RFC 9000 Section 2.1. The group provokes it on two different
/// streams and requires two different payloads, so a variant that carries a
/// constant, or the first stream id ever seen, fails.
#[test]
fn cap_invalid_stream_state() {
    let mut p = pair::Pair::new(pair::ALPN_HQ);
    p.handshake();

    let mut buf = [0u8; 1024];

    // Streams 2 and 6: client-initiated (low bit 0), unidirectional (bit 1).
    for stream_id in [2u64, 6u64] {
        p.client
            .stream_send(stream_id, b"write-only", false)
            .unwrap_or_else(|e| panic!("client stream_send on its own uni stream {stream_id}: {e:?}"));

        let err = p
            .client
            .stream_recv(stream_id, &mut buf)
            .expect_err(&format!(
                "expected reading the client's own unidirectional stream {stream_id} to fail; it \
                 returned a read instead"
            ));

        match err {
            quiche::Error::InvalidStreamState(id) => assert_eq!(
                id, stream_id,
                "expected InvalidStreamState to carry the stream that was in the wrong state, \
                 {stream_id}; it carried {id}"
            ),
            other => panic!(
                "expected Error::InvalidStreamState({stream_id}) from reading the client's own \
                 unidirectional stream {stream_id}; observed {other:?}"
            ),
        }
    }

    // The payload is the stream id, not a constant.
    let e2 = p.client.stream_recv(2, &mut buf).expect_err("stream 2 is still write-only");
    let e6 = p.client.stream_recv(6, &mut buf).expect_err("stream 6 is still write-only");
    assert_ne!(
        e2, e6,
        "expected InvalidStreamState for stream 2 and stream 6 to differ; both were {e2:?}, so the \
         variant is not carrying the stream id"
    );

    // The server sees the same rule from its own side of the numbering: a
    // server-initiated unidirectional stream is write-only for the server.
    p.server.stream_send(3, b"write-only", false).expect("server stream_send on stream 3");
    let err = p
        .server
        .stream_recv(3, &mut buf)
        .expect_err("expected reading the server's own unidirectional stream 3 to fail");
    assert_eq!(
        err,
        quiche::Error::InvalidStreamState(3),
        "expected Error::InvalidStreamState(3) when the server reads its own unidirectional \
         stream; observed {err:?}"
    );
}
