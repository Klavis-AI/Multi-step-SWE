//! regression groups: what earlier milestones could already do and must not lose
//!
//! One `#[test]` per scored group, named exactly as the group id with dashes
//! turned into underscores: `reg-01-h3-get` is `fn reg_01_h3_get()`.
//!
//! These are earlier steps' capabilities, re-observed. A group here is chosen
//! because *this* step's change is what would plausibly break it, not because
//! it is cheap to re-run: the ALPN list grew, so HTTP/3 is re-checked; the
//! congestion controller became pluggable, so a bulk transfer is re-checked;
//! draft-23 stayed supported, so its Initial key schedule is re-checked.
//!
//! The programs these groups drive are not the same across the range they
//! span. 0.1.0 builds only `examples/{client,server,http3-client,http3-server}`;
//! from this milestone the driver also builds the `tools/apps` package, and
//! `testkit`'s `*_bin()` helpers prefer its `quiche-client`/`quiche-server`.
//! Both generations are docopt programs accepting the same flags, but their
//! defaults differ -- most sharply the certificate and document-root paths --
//! so these groups name every path, port and wire version explicitly and
//! inherit none of them. See REQUIREMENTS.md, "T1 binary CLI eras".

use std::time::Duration;

#[allow(unused_imports)]
use verifier::{pair, support, testkit};

#[allow(dead_code)]
const SHORT: Duration = Duration::from_secs(10);
#[allow(dead_code)]
const LONG: Duration = Duration::from_secs(60);

/// Where the first byte of `want` and `got` disagree, rendered for a message.
#[allow(dead_code)]
fn first_diff(want: &[u8], got: &[u8]) -> String {
    match want.iter().zip(got.iter()).position(|(a, b)| a != b) {
        Some(off) => format!(
            "first difference at offset {off}: expected {}, observed {}",
            testkit::hex(&want[off..(off + 16).min(want.len())]),
            testkit::hex(&got[off..(off + 16).min(got.len())])
        ),
        None => format!(
            "no differing byte in the first {} bytes (expected {} bytes, observed {})",
            want.len().min(got.len()),
            want.len(),
            got.len()
        ),
    }
}

/// `want` rendered as a short hex prefix plus its length.
#[allow(dead_code)]
fn head(d: &[u8], n: usize) -> String {
    format!("{} ({} bytes total)", testkit::hex(&d[..n.min(d.len())]), d.len())
}

// ---------------------------------------------------------------------------
// reg-01-initial-keys
// ---------------------------------------------------------------------------

/// `reg-01-initial-keys`: draft-23's Initial key schedule still produces
/// exactly the keys RFC 9001 Section 5 says it does, in both directions.
///
/// Step 01 could only speak draft-23, so getting this wrong was impossible to
/// miss. Here draft-23 is one of four, and the release has just grown a second
/// Initial salt to go with draft-27 -- so the way to break this is to apply
/// the new salt to the old version, or to key one direction from the wrong
/// label. Neither is visible from inside the candidate: the check is that the
/// verifier's own independent implementation opens both first flights.
///
/// PLAN CORRECTION. The group plan labelled this T5 and described replaying
/// the specification's worked key-derivation vectors. Those vectors would test
/// the *verifier's* crypto, not the candidate's -- the candidate exposes no
/// key-derivation API to feed them to. The observation that does bind the
/// candidate is T2: take the packets it puts on the wire and open them with
/// keys derived independently. There is still exactly one correct answer,
/// because an AEAD either opens or it does not.
#[test]
fn reg_01_initial_keys() {
    let mut cc = quiche::Config::new(support::VERSION_DRAFT23).expect("Config::new(draft-23)");
    cc.set_application_protos(b"\x05hq-23").expect("set_application_protos");
    cc.verify_peer(false);
    pair::set_idle(&mut cc, 30_000);
    cc.set_max_packet_size(pair::MAX_DATAGRAM as u64);
    cc.set_initial_max_data(1_000_000);
    cc.set_initial_max_stream_data_bidi_local(1_000_000);
    cc.set_initial_max_stream_data_bidi_remote(1_000_000);
    cc.set_initial_max_streams_bidi(100);

    let mut sc = pair::server_config(b"\x05hq-23");
    let mut p = pair::Pair::with_configs(&mut cc, &mut sc);

    // The client's first flight, taken off the pair before anyone processes
    // it. Its destination connection ID is the one the whole Initial key
    // schedule hangs off, and it is chosen by the candidate, not by us.
    let mut buf = vec![0u8; pair::MAX_DATAGRAM];
    let len = p.client.send(&mut buf).expect("the client's first flight");
    let first = buf[..len].to_vec();

    assert!(
        support::is_long_header(first[0]),
        "expected a long-header Initial as the client's first datagram; observed first byte \
         {:02x}, datagram {}",
        first[0],
        head(&first, 48)
    );
    let version = support::peek_version(&first).unwrap_or_else(|e| {
        panic!("cannot read the version field of the client's first datagram: {e}")
    });
    assert_eq!(
        version,
        support::VERSION_DRAFT23,
        "a Config built for draft-23 put {version:#010x} on the wire; expected {:#010x}. \
         datagram {}",
        support::VERSION_DRAFT23,
        head(&first, 48)
    );

    let client_initial = support::decrypt_initial(
        &first,
        support::VERSION_DRAFT23,
        support::Perspective::Client,
    )
    .unwrap_or_else(|e| {
        panic!(
            "expected the verifier's own draft-23 Initial keys -- salt \
             c3eef712c72ebb5a11a7d2432bb46365bef9f502 over the packet's destination connection \
             id, AES-ECB header protection, AEAD_AES_128_GCM -- to open the client's first \
             Initial; observed {e}.\ndatagram {}",
            head(&first, 64)
        )
    });

    let odcid = client_initial.dcid.clone();
    let crypto = client_initial.crypto_data().unwrap_or_else(|e| {
        panic!(
            "the client's Initial opened but its CRYPTO stream will not assemble: {e}\n\
             payload {}",
            head(&client_initial.payload, 64)
        )
    });
    assert_eq!(
        crypto.first().copied(),
        Some(0x01),
        "expected TLS handshake type 1 (client_hello) at the head of the crypto stream; \
         observed {} ({} crypto bytes assembled from a {}-byte decrypted payload, which \
         begins {})",
        head(&crypto, 16),
        crypto.len(),
        client_initial.payload.len(),
        head(&client_initial.payload, 64)
    );
    assert_eq!(
        crypto.get(4..6),
        Some(&[0x03, 0x03][..]),
        "expected legacy_version 0x0303 in the ClientHello, as TLS 1.3 requires; observed {}",
        head(&crypto, 16)
    );

    // The other direction. The server's Initial is keyed from the *client's*
    // original destination connection ID, with the server labels, so opening
    // it proves the schedule is right on both sides rather than symmetric and
    // wrong.
    let mut deliver = first.clone();
    p.server.recv(&mut deliver).expect("the server reads the client's first flight");

    let mut sbuf = vec![0u8; pair::MAX_DATAGRAM];
    let slen = p.server.send(&mut sbuf).expect("the server's reply to a draft-23 Initial");
    let reply = sbuf[..slen].to_vec();

    let server_initial = support::decrypt_initial_with_odcid(
        &reply,
        support::VERSION_DRAFT23,
        support::Perspective::Server,
        &odcid,
    )
    .unwrap_or_else(|e| {
        panic!(
            "expected the verifier's own draft-23 server Initial keys -- the same salt over the \
             client's original destination connection id {}, server labels -- to open the \
             server's reply; observed {e}.\ndatagram {}",
            testkit::hex(&odcid),
            head(&reply, 64)
        )
    });
    let scrypto = server_initial.crypto_data().unwrap_or_else(|e| {
        panic!("the server's Initial opened but its CRYPTO stream will not assemble: {e}")
    });
    assert_eq!(
        scrypto.first().copied(),
        Some(0x02),
        "expected TLS handshake type 2 (server_hello) at the head of the server's crypto \
         stream; observed {}",
        head(&scrypto, 16)
    );

    // Having proved the keys, let the handshake finish: a key schedule that is
    // right for the first packet and wrong afterwards should not pass here.
    // Both flights above were taken off the pair by hand, so they have to be
    // handed back before the pair can settle -- `Pair::exchange` only moves
    // what each side has *not* yet produced.
    let mut deliver = reply.clone();
    p.client.recv(&mut deliver).expect("the client reads the server's first flight");
    p.settle(64);
    assert!(
        p.client.is_established() && p.server.is_established(),
        "a draft-23 handshake whose Initial packets both decrypted did not complete: client \
         established={} closed={}, server established={} closed={}",
        p.client.is_established(),
        p.client.is_closed(),
        p.server.is_established(),
        p.server.is_closed()
    );
}

// ---------------------------------------------------------------------------
// reg-01-h3-get
// ---------------------------------------------------------------------------

/// `reg-01-h3-get`: a GET through the candidate's own HTTP/3 binaries still
/// returns the exact body.
///
/// The ALPN list grew from one name to four at this milestone. If the server
/// now advertises a name the client does not offer, or the h3 layer is keyed
/// off the wrong one, this stops working -- and it is the whole of HTTP/3,
/// end to end, so nothing subtle can hide behind it.
#[test]
fn reg_01_h3_get() {
    // Every T1 group here binds the same fixed port, so only one may hold it
    // at a time.
    let _lock = testkit::serial_t1();

    const LEN: usize = 64 * 1024;
    let dir = testkit::scratch("reg-01-h3-get");
    let want = testkit::body(LEN, 0x0233_0001);
    let cwd = testkit::example_server_cwd(&dir, &[("payload.bin", want.clone())]);

    let server_bin = testkit::h3_server_bin();
    let client_bin = testkit::h3_client_bin();

    let mut sc = testkit::cmd(&server_bin);
    sc.current_dir(&cwd);
    testkit::configure_server(&mut sc, &cwd, testkit::EXAMPLE_SERVER_PORT);
    testkit::disable_retry(&mut sc);
    let server =
        testkit::spawn_server(&mut sc, testkit::EXAMPLE_SERVER_PORT, &dir, "h3-server", SHORT);

    let url = format!("https://127.0.0.1:{}/payload.bin", testkit::EXAMPLE_SERVER_PORT);
    let mut cc = testkit::cmd(&client_bin);
    cc.arg(&url);
    testkit::configure_client(&mut cc);
    let run = testkit::run(&mut cc, LONG, &dir, "h3-client");

    let log = server.log();
    assert!(
        !run.timed_out,
        "expected {} to fetch {LEN} bytes from {url} within {LONG:?}; observed: still running at \
         the deadline, killed after writing {} of {LEN} bytes.\n--- client stderr ---\n{}\n\
         --- server log ---\n{log}",
        client_bin.display(),
        run.stdout_bytes.len(),
        run.stderr
    );
    assert_eq!(
        run.status.and_then(|s| s.code()),
        Some(0),
        "expected {} to exit 0 after fetching {url}; observed status {:?} having written {} of \
         {LEN} bytes.\n--- client stderr ---\n{}\n--- server log ---\n{log}",
        client_bin.display(),
        run.status,
        run.stdout_bytes.len(),
        run.stderr
    );

    let got = &run.stdout_bytes;
    assert_eq!(
        got.len(),
        LEN,
        "expected exactly {LEN} bytes of {url} on the client's stdout; observed {} bytes ({}).\n\
         --- client stderr ---\n{}\n--- server log ---\n{log}",
        got.len(),
        first_diff(&want, got),
        run.stderr
    );
    assert!(
        want == *got,
        "expected the body of {url} back byte for byte; observed {}.\n--- server log ---\n{log}",
        first_diff(&want, got)
    );

    drop(server);
}

// ---------------------------------------------------------------------------
// reg-01-stream-transfer
// ---------------------------------------------------------------------------

/// `reg-01-stream-transfer`: 512 KiB through the candidate's own `client` and
/// `server`, byte for byte.
///
/// 512 KiB is roughly 400 packets at this release's 1350-byte datagram size,
/// so it cannot fit in the initial congestion window and cannot arrive without
/// the controller growing its window, the sender retransmitting what is lost,
/// and the receiver reassembling ranges that arrived out of order. Congestion
/// control became a pluggable choice at this milestone, which makes exactly
/// that machinery the thing most likely to have been disturbed.
#[test]
fn reg_01_stream_transfer() {
    let _lock = testkit::serial_t1();

    const LEN: usize = 512 * 1024;
    let dir = testkit::scratch("reg-01-stream-transfer");
    let want = testkit::body(LEN, 0x5123_4567);
    let cwd = testkit::example_server_cwd(&dir, &[("big.bin", want.clone())]);

    let server_bin = testkit::server_bin();
    let client_bin = testkit::client_bin();

    let mut sc = testkit::cmd(&server_bin);
    sc.current_dir(&cwd);
    testkit::configure_server(&mut sc, &cwd, testkit::EXAMPLE_SERVER_PORT);
    testkit::disable_retry(&mut sc);
    let server =
        testkit::spawn_server(&mut sc, testkit::EXAMPLE_SERVER_PORT, &dir, "xfer-server", SHORT);

    let url = format!("https://127.0.0.1:{}/big.bin", testkit::EXAMPLE_SERVER_PORT);
    let mut cc = testkit::cmd(&client_bin);
    cc.arg(&url);
    testkit::configure_client(&mut cc);
    let run = testkit::run(&mut cc, LONG, &dir, "xfer-client");

    let log = server.log();
    assert!(
        !run.timed_out,
        "expected {} to fetch {LEN} bytes from {url} within {LONG:?}; observed: still running at \
         the deadline, killed after writing {} of {LEN} bytes.\n--- client stderr ---\n{}\n\
         --- server log ---\n{log}",
        client_bin.display(),
        run.stdout_bytes.len(),
        run.stderr
    );
    assert_eq!(
        run.status.and_then(|s| s.code()),
        Some(0),
        "expected {} to exit 0 after fetching {url}; observed status {:?} having written {} of \
         {LEN} bytes.\n--- client stderr ---\n{}\n--- server log ---\n{log}",
        client_bin.display(),
        run.status,
        run.stdout_bytes.len(),
        run.stderr
    );

    let got = &run.stdout_bytes;
    assert_eq!(
        got.len(),
        LEN,
        "expected exactly {LEN} bytes of {url} on the client's stdout; observed {} bytes ({}).\n\
         --- client stderr ---\n{}\n--- server log ---\n{log}",
        got.len(),
        first_diff(&want, got),
        run.stderr
    );
    assert!(
        want == *got,
        "expected the 512 KiB body of {url} back byte for byte; observed {}.\n\
         --- server log ---\n{log}",
        first_diff(&want, got)
    );

    drop(server);
}
