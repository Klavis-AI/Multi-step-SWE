//! inherited groups: what earlier milestones could already do and this one must not lose
//!
//! One `#[test]` per scored group, named exactly as the group id with dashes
//! turned into underscores: `reg-11-pmtud` is `fn reg_11_pmtud()`. Each one
//! names the step it came from, and the slug after the step number is a real
//! `cap-` group of that step -- `carryover-check.sh` resolves it and fails if
//! it does not exist.
//!
//! Observe behaviour. Never read the candidate's source, never grep it for an
//! identifier, and never assert on a private detail: every group sits on one
//! of the five observation surfaces -- T1 two-process loopback through the
//! candidate's own binaries, T2 the verifier speaking QUIC at the candidate,
//! T3 in-memory connection pairs through the public Rust API, T4 emitted
//! artefacts, T5 exact-answer negative corpora.
//!
//! Every group in this file is a **gate** group: it earns no weight and can
//! only veto. That is what decides what may be in it. An inherited assertion
//! whose answer could move under load, or across hosts, is not weaker coverage
//! here -- it is a step that scores zero at random for every candidate, the
//! Oracle included. Three of step 11's `cap-pmtud` legs are left behind for
//! exactly that reason, and the omissions are argued where they would have
//! gone.
//!
//! This milestone adds a workspace member that opens its own connections, a
//! setter that re-encodes a live connection's transport parameters, and two
//! reaches into the HTTP/3 layer. The three groups below are the ones whose
//! answers those changes could disturb without any of the new code being
//! wrong.

use std::path::Path;
use std::time::Duration;

#[allow(unused_imports)]
use verifier::{pair, support, testkit};

#[allow(dead_code)]
const SHORT: Duration = Duration::from_secs(10);
#[allow(dead_code)]
const LONG: Duration = Duration::from_secs(60);

/// The datagram size every QUIC endpoint may assume before it has learned
/// anything about the path.
const QUIC_FLOOR: usize = 1200;

/// Where two byte strings first differ, for a message that says something.
fn first_diff(want: &[u8], got: &[u8]) -> String {
    match want.iter().zip(got.iter()).position(|(a, b)| a != b) {
        Some(i) => format!(
            "first difference at offset {i}: expected {}, observed {} (16 bytes from there)",
            testkit::hex(&want[i..(i + 16).min(want.len())]),
            testkit::hex(&got[i..(i + 16).min(got.len())])
        ),
        None if want.len() == got.len() => "identical".to_string(),
        None => format!(
            "identical for the first {} bytes, then one of them ends ({} expected, {} observed)",
            want.len().min(got.len()),
            want.len(),
            got.len()
        ),
    }
}

/// The candidate's own `quiche-server`, serving `files` on `port`.
fn start_app_server(dir: &Path, port: u16, files: &[(&str, Vec<u8>)], tag: &str) -> testkit::Server {
    let root = testkit::docroot(dir, files);
    let tls = testkit::tls();
    let bin = testkit::server_bin();
    let mut c = testkit::cmd(&bin);
    c.arg("--listen")
        .arg(format!("127.0.0.1:{port}"))
        .arg("--cert")
        .arg(&tls.cert)
        .arg("--key")
        .arg(&tls.key)
        .arg("--root")
        .arg(&root)
        .arg("--no-retry");
    testkit::spawn_server(&mut c, port, dir, tag, LONG)
}

/// `{size: count}`, sorted by size, for an assertion message that has to say
/// what it saw without printing a thousand numbers.
fn histogram(sizes: &[usize]) -> std::collections::BTreeMap<usize, usize> {
    let mut h = std::collections::BTreeMap::new();
    for &n in sizes {
        *h.entry(n).or_insert(0) += 1;
    }
    h
}

/// Both sides configured for a `size`-byte UDP payload, with path MTU
/// discovery on or off and nothing else different between the two.
fn pmtud_configs(on: bool, size: usize) -> (quiche::Config, quiche::Config) {
    let mut cc = pair::client_config(pair::ALPN_HQ);
    let mut sc = pair::server_config(pair::ALPN_HQ);
    for c in [&mut cc, &mut sc] {
        c.set_max_send_udp_payload_size(size);
        c.set_max_recv_udp_payload_size(size);
        c.discover_pmtu(on);
    }
    (cc, sc)
}

// ---------------------------------------------------------------------------
// groups
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// reg-11-pmtud  (T3)  <- step 11, cap-pmtud
// ---------------------------------------------------------------------------

/// `reg-11-pmtud`: a server still climbs off the 1200-byte floor by probing,
/// and still does not when discovery is switched off.
///
/// The previous milestone's headline, and the youngest behaviour in the stack:
/// if this step's work disturbed the send path, it shows here first. Carried
/// because the step adds a workspace member that opens connections of its own
/// and a setter that re-encodes a live connection's transport parameters --
/// the maximum UDP payload size is one of those parameters, and re-serialising
/// the block is exactly the kind of change that can leave the path estimate
/// reading from the wrong place.
///
/// Both legs are the same two connections apart from `discover_pmtu`, so
/// nothing here can be satisfied by a datagram budget, a pacing quantum or a
/// buffer size -- only by discovery:
///
/// 1. on  -- at least one server datagram is exactly `PROBE` bytes, and every
///    server datagram is either at or under the floor or exactly `PROBE`, with
///    nothing in between: discovery raises the estimate by probing, not by
///    writing bigger packets.
/// 2. off -- with everything else identical the server never exceeds the
///    floor, so leg 1's probe is discovery and not the handshake flight.
/// 3. both -- a 64 KiB body still arrives byte for byte with the stream
///    finished, and the on-configuration pair is still open afterwards:
///    raising the estimate and then black-holing the connection is not
///    discovery.
///
/// **Three of step 11's legs are deliberately not here**, and each for the
/// same reason -- a gate group is an unpriced veto, so an assertion in it that
/// could read differently under load costs every candidate the whole step:
///
/// * the **probe count and position** were not asserted at step 11 either;
/// * the **client histograms** -- the multiset of datagram sizes a 64 KiB
///   write is chopped into, with discovery on against off -- are dropped. How
///   a bulk write is split depends on the congestion window at each `send`,
///   and the exact multiset is not something a gate should be pinning. The
///   one-sided *bound* survives in the server legs above;
/// * the **binary-search-on-loss** leg was already absent at step 11: the
///   reference needs more timer ticks than a bounded case can justify before
///   it re-probes at a smaller size.
///
/// `PathStats::pmtu` is not read, at step 11 or here. It reports the
/// *configured* maximum from the moment the connection is created and reads
/// `PROBE` in both legs before either has sent a packet, so it cannot tell
/// them apart.
#[test]
fn reg_11_pmtud() {
    const PROBE: usize = pair::MAX_DATAGRAM;
    const STREAM_ID: u64 = 4;
    const BODY: usize = 64 * 1024;

    assert!(
        PROBE > QUIC_FLOOR,
        "the probe size must be above the floor or the group proves nothing"
    );

    // --- leg 1: discovery on ------------------------------------------------
    //
    // The probe goes out on the first `send` after HANDSHAKE_DONE, which is
    // inside the handshake flight itself, so the window opened here is the
    // whole connection up to the point application data is queued -- not just
    // what follows `handshake()`.
    let (mut cc, mut sc) = pmtud_configs(true, PROBE);
    let mut on = pair::Pair::with_configs(&mut cc, &mut sc);
    on.handshake();
    on.settle(64);
    let on_sv: Vec<usize> = on.server_out.iter().map(|d| d.len()).collect();

    assert!(
        on_sv.iter().any(|&n| n == PROBE),
        "expected a server with discover_pmtu(true) to send at least one padded PMTU probe of \
         exactly {PROBE} bytes; observed {} datagrams with sizes {:?}. A connection starts at \
         the {QUIC_FLOOR}-byte floor and has to probe to leave it.",
        on_sv.len(),
        histogram(&on_sv)
    );
    assert!(
        on_sv.iter().all(|&n| n <= QUIC_FLOOR || n == PROBE),
        "expected every server datagram to be either at or under the {QUIC_FLOOR}-byte floor \
         or a probe padded to exactly {PROBE} bytes, with nothing in between; observed sizes \
         {:?}. Discovery raises the path estimate by probing for it, so ordinary output stays \
         at the floor until a probe is acknowledged.",
        histogram(&on_sv)
    );

    // --- leg 2: discovery off, everything else identical --------------------
    let (mut cc, mut sc) = pmtud_configs(false, PROBE);
    let mut off = pair::Pair::with_configs(&mut cc, &mut sc);
    off.handshake();
    off.settle(64);
    let off_sv: Vec<usize> = off.server_out.iter().map(|d| d.len()).collect();

    assert!(
        off_sv.iter().all(|&n| n <= QUIC_FLOOR),
        "expected a server with discover_pmtu(false) to send nothing above the {QUIC_FLOOR}-byte \
         floor -- there is no probe to send; observed {} datagrams with sizes {:?}, under a \
         config asking for a {PROBE}-byte payload exactly as the discovery-on leg did.",
        off_sv.len(),
        histogram(&off_sv)
    );

    // --- leg 3: the search did not break either connection -------------------
    let body = testkit::body(BODY, 0x1122_0012);
    let (got, fin) = on.bulk_client_to_server(STREAM_ID, &body);
    let (off_got, off_fin) = off.bulk_client_to_server(STREAM_ID, &body);

    for (what, got, fin) in
        [("discover_pmtu(true)", &got, fin), ("discover_pmtu(false)", &off_got, off_fin)]
    {
        assert_eq!(
            got.len(),
            body.len(),
            "expected the {} byte body to arrive under {what}; observed {} bytes ({}). Raising \
             the path estimate and then black-holing the connection is not discovery.",
            body.len(),
            got.len(),
            first_diff(&body, got)
        );
        assert_eq!(
            got, &body,
            "expected the body back byte for byte under {what}; observed {}",
            first_diff(&body, got)
        );
        assert!(fin, "expected the stream to be finished under {what}");
    }
    assert!(
        !on.client.is_closed() && !on.server.is_closed(),
        "expected both ends still open after the PMTU search; client closed={} server closed={}",
        on.client.is_closed(),
        on.server.is_closed()
    );
}

// ---------------------------------------------------------------------------
// reg-09-key-update  (T2)  <- step 09, cap-key-update
// ---------------------------------------------------------------------------

/// `reg-09-key-update`: the verifier still initiates a key update and the
/// candidate still answers it correctly.
///
/// Re-seeded from step 10's copy and unchanged in what it observes. There is
/// no public API for *initiating* an update and the candidate is only required
/// to implement the responder, so the verifier becomes the peer that starts
/// one: after the handshake and a warmup write it derives the
/// next-generation AEAD key and IV from the candidate's own
/// `CLIENT_TRAFFIC_SECRET_0` -- HKDF-Expand-Label with `quic ku`, then
/// `quic key` and `quic iv` -- keeps the generation-0 header-protection key,
/// and injects a 1-RTT packet with the key-phase bit set.
///
/// It proves those keys before using them, by opening one of the candidate's
/// own packets with the generation-0 set. A key-schedule disagreement between
/// verifier and candidate would otherwise read as a candidate fault at the
/// first assertion.
///
/// Carried because this step's new setter re-encodes a live connection's
/// transport parameters and its new workspace member drives connections of its
/// own, and every short-header packet either of those produces is sealed by
/// exactly this machinery. A candidate that disturbs the key schedule or the
/// phase bookkeeping while re-serialising a parameter block breaks this and
/// passes every group that only looks at the new surface.
///
/// Four assertions follow:
/// 1. the candidate decrypts the injected packet -- so it derived the same
///    next-generation keys;
/// 2. the header-protection key was *not* rotated. This is implied rather than
///    separately asserted, as at step 09: the verifier applied generation-0 HP,
///    so a candidate that rotated it could not find the packet number and
///    nothing after this point would succeed;
/// 3. a second update before the first is acknowledged is
///    `Err(Error::KeyUpdate)`;
/// 4. the candidate's own next short-header packet has the phase bit set -- it
///    flipped.
#[test]
fn reg_09_key_update() {
    const STREAM_ID: u64 = 4;
    // Far beyond real client packet numbers; fits in 4-byte PN encoding.
    const INJECTED_PN: u64 = 8192;
    // Use a fresh stream so the warmup bytes on stream 4 do not overlap.
    const INJECT_STREAM_ID: u64 = 8;

    let mut cc = pair::client_config(pair::ALPN_HQ);
    cc.log_keys();
    let mut sc = pair::server_config(pair::ALPN_HQ);
    let (mut p, keylog) = pair::Pair::with_keylog(&mut cc, &mut sc);
    p.handshake();

    // Warm up: move some data so both sides have sent 1-RTT packets.
    p.client.stream_send(STREAM_ID, b"warmup", false).expect("stream_send warmup");
    p.settle(16);
    assert!(
        p.client.is_established() && p.server.is_established(),
        "handshake did not complete -- no 1-RTT keys available"
    );

    // Derive generation-0 keys from CLIENT_TRAFFIC_SECRET_0.
    let secret0 = pair::keylog_secret(&keylog.contents(), "CLIENT_TRAFFIC_SECRET_0")
        .expect("client did not write CLIENT_TRAFFIC_SECRET_0 to its keylog");
    let gen0_keys = support::PacketKeys::from_secret(support::Suite::Aes128Gcm, &secret0)
        .expect("deriving gen-0 1-RTT keys from CLIENT_TRAFFIC_SECRET_0");

    // Prove gen-0 keys by opening one of the candidate's own client packets.
    let proof = p
        .client_out
        .iter()
        .rev()
        .filter(|d| !d.is_empty() && !support::is_long_header(d[0]))
        .find_map(|d| pair::open_1rtt(d, pair::SERVER_CID.len(), &gen0_keys, None));
    assert!(
        proof.is_some(),
        "the verifier could not open any of the {} client datagrams with the keys derived \
         from CLIENT_TRAFFIC_SECRET_0 -- this is a key-schedule disagreement, not a \
         candidate fault",
        p.client_out.len()
    );

    // Derive generation-1 AEAD material (RFC 9001 6): next_secret uses "quic ku".
    // HP key is intentionally NOT re-derived: the server uses the gen-0 HP key
    // to unmask the incoming packet header.
    let next_secret = support::kdf::next_secret(&secret0)
        .expect("kdf::next_secret on CLIENT_TRAFFIC_SECRET_0");
    let next_key = support::kdf::expand_label_to_vec(
        &next_secret, support::kdf::LABEL_KEY, b"",
        support::Suite::Aes128Gcm.key_len(),
    ).expect("expand next key");
    let next_iv = support::kdf::expand_label_to_vec(
        &next_secret, support::kdf::LABEL_IV, b"",
        support::Suite::Aes128Gcm.iv_len(),
    ).expect("expand next iv");
    // Keep the gen-0 HP key -- quiche's own crypto code does the same.
    let gen1_keys = support::PacketKeys::new(
        support::Suite::Aes128Gcm,
        &next_key,
        &next_iv,
        gen0_keys.hp_key(),
    ).expect("constructing gen-1 PacketKeys with old HP key");

    // Build a STREAM frame payload and seal it under gen-1 with key_phase=true.
    let body = testkit::body(32, 0x1020_0012);
    let mut payload = Vec::new();
    pair::varint(0x0a, &mut payload); // STREAM type: stream_id present, length present
    pair::varint(INJECT_STREAM_ID, &mut payload);
    pair::varint(body.len() as u64, &mut payload);
    payload.extend_from_slice(&body);

    let mut pkt = pair::seal_1rtt_phase(
        &gen1_keys, &pair::SERVER_CID, INJECTED_PN, &payload, true,
    );

    // Assertion 1 + 2: candidate decrypts the packet (correct next-gen keys,
    // and HP was not rotated -- if it were, the server could not find the PN).
    let recv_result = p.server.recv(
        &mut pkt,
        quiche::RecvInfo { from: pair::client_addr(), to: pair::server_addr() },
    );
    assert!(
        recv_result.is_ok(),
        "the server rejected the key-phase=true packet: {:?}. Either the candidate did \
         not derive the next-generation traffic keys with label 'quic ku', or it \
         re-derived the header-protection key (HP must stay from generation 0).",
        recv_result.unwrap_err()
    );

    let (got, _fin) = pair::read_stream(&mut p.server, INJECT_STREAM_ID);
    assert_eq!(
        got.as_slice(), body.as_slice(),
        "the server received and decrypted the key-phase=true packet but the STREAM \
         payload did not round-trip correctly: {}",
        first_diff(&body, &got)
    );

    // Assertion 3: a second update before the first is ACKed must fail with
    // Error::KeyUpdate.
    //
    // The server is now in phase=1; a new update flips the bit back to false,
    // so derive gen-2 AEAD keys and seal with key_phase=false.
    //
    // This must run BEFORE the send-drain loop below. quiche marks the pending
    // update acknowledged as soon as the server emits any short-header packet,
    // so draining first would make a second update perfectly legal.
    let gen2_secret = support::kdf::next_secret(&next_secret)
        .expect("kdf::next_secret on gen-1 secret (gen-2)");
    let gen2_key = support::kdf::expand_label_to_vec(
        &gen2_secret, support::kdf::LABEL_KEY, b"",
        support::Suite::Aes128Gcm.key_len(),
    ).expect("expand gen-2 key");
    let gen2_iv = support::kdf::expand_label_to_vec(
        &gen2_secret, support::kdf::LABEL_IV, b"",
        support::Suite::Aes128Gcm.iv_len(),
    ).expect("expand gen-2 iv");
    let gen2_keys = support::PacketKeys::new(
        support::Suite::Aes128Gcm,
        &gen2_key,
        &gen2_iv,
        gen0_keys.hp_key(),   // HP key stays at generation 0 throughout
    ).expect("constructing gen-2 PacketKeys");
    let mut pkt2 = pair::seal_1rtt_phase(
        &gen2_keys, &pair::SERVER_CID, INJECTED_PN + 1, &payload, false,
    );
    let result2 = p.server.recv(
        &mut pkt2,
        quiche::RecvInfo { from: pair::client_addr(), to: pair::server_addr() },
    );
    assert_eq!(
        result2.unwrap_err(), quiche::Error::KeyUpdate,
        "expected a second key update attempt (key_phase flipped to false with gen-2 keys) \
         before the first is ACKed to return Err(Error::KeyUpdate), but got a \
         different result"
    );

    // Assertion 4: the server's own next packet has key_phase=1 -- it flipped.
    let server_secret = pair::keylog_secret(&keylog.contents(), "SERVER_TRAFFIC_SECRET_0")
        .expect("client did not write SERVER_TRAFFIC_SECRET_0 to its keylog");
    let server_gen0 = support::PacketKeys::from_secret(support::Suite::Aes128Gcm, &server_secret)
        .expect("deriving server gen-0 keys");
    // Derive gen-1 server keys (gen-0 HP key kept, same rule as the injected packet).
    let srv_next_secret = support::kdf::next_secret(&server_secret)
        .expect("kdf::next_secret on SERVER_TRAFFIC_SECRET_0");
    let srv_next_key = support::kdf::expand_label_to_vec(
        &srv_next_secret, support::kdf::LABEL_KEY, b"",
        support::Suite::Aes128Gcm.key_len(),
    ).expect("expand server next key");
    let srv_next_iv = support::kdf::expand_label_to_vec(
        &srv_next_secret, support::kdf::LABEL_IV, b"",
        support::Suite::Aes128Gcm.iv_len(),
    ).expect("expand server next iv");
    let server_gen1 = support::PacketKeys::new(
        support::Suite::Aes128Gcm,
        &srv_next_key,
        &srv_next_iv,
        server_gen0.hp_key(),
    ).expect("constructing server gen-1 PacketKeys");

    let mut out = [0u8; pair::MAX_DATAGRAM];
    let mut saw_key_phase_one = false;
    loop {
        let len = match p.server.send(&mut out) {
            Ok((len, _)) => len,
            Err(quiche::Error::Done) => break,
            Err(e) => panic!("server.send() after key update: {e:?}"),
        };
        if support::is_long_header(out[0]) {
            continue;
        }
        // Try gen-0 first; if that fails, try gen-1 (server flipped after key update).
        let opened_opt =
            pair::open_1rtt(&out[..len], pair::CLIENT_CID.len(), &server_gen0, None)
            .or_else(|| pair::open_1rtt(&out[..len], pair::CLIENT_CID.len(), &server_gen1, None));
        if let Some(opened) = opened_opt {
            if opened.key_phase {
                saw_key_phase_one = true;
                break;
            }
        }
    }
    assert!(
        saw_key_phase_one,
        "after accepting a key-phase=true packet, the server's own next short-header \
         packet should have key_phase=1 (it flipped its send phase to match). None of \
         the server packets had key_phase=true when opened with gen-0 or gen-1 server keys."
    );
}

// ---------------------------------------------------------------------------
// reg-01-h3-get  (T1)  <- step 01, cap-h3-get
// ---------------------------------------------------------------------------

/// `reg-01-h3-get`: HTTP/3 still moves a whole file between the candidate's
/// own two binaries.
///
/// Inherited from step 01 and re-seeded from step 07, the most recent step to
/// carry it: 64 KiB requested over HTTP/3 from the candidate's own
/// `quiche-server` comes back byte for byte on the candidate's own
/// `quiche-client`'s stdout, as two real processes over UDP loopback.
/// `--http-version HTTP/3` is pinned rather than left to the apps' own
/// negotiation, so the inherited observation stays HTTP/3 and does not quietly
/// become a different group.
///
/// Carried because this step reaches into the HTTP/3 layer twice -- a new way
/// to send header sections and a new statistics surface -- and because it is
/// the only inherited gate case here that exercises the whole stack as the
/// user gets it. `cap-h3i-replay` also crosses the process boundary, but
/// through a new binary and against a hand-written action file; this one is
/// the ordinary path, and if it fails, that one's diagnosis is guesswork.
#[test]
fn reg_01_h3_get() {
    const LEN: usize = 64 * 1024;

    let dir = testkit::scratch("reg-01-h3-get");
    let want = testkit::body(LEN, 0x0333_0012);
    let port = testkit::free_port();
    let server = start_app_server(&dir, port, &[("payload.bin", want.clone())], "h3-server");

    let url = format!("https://127.0.0.1:{port}/payload.bin");
    let client_bin = testkit::client_bin();
    let mut cc = testkit::cmd(&client_bin);
    cc.arg("--no-verify").arg("--http-version").arg("HTTP/3").arg(&url);
    let run = testkit::run(&mut cc, LONG, &dir, "h3-client");

    let log = server.log();
    assert!(
        !run.timed_out,
        "expected {} to GET {url} over HTTP/3 within {LONG:?}; observed: still running at the \
         deadline, killed after writing {} of {LEN} body bytes.\n--- client stderr ---\n{}\n\
         --- server log ---\n{log}",
        client_bin.display(),
        run.stdout_bytes.len(),
        run.stderr
    );
    assert_eq!(
        run.status.and_then(|s| s.code()),
        Some(0),
        "expected {} to exit 0 after GET {url}; observed status {:?} having written {} of {LEN} \
         body bytes.\n--- client stderr ---\n{}\n--- server log ---\n{log}",
        client_bin.display(),
        run.status,
        run.stdout_bytes.len(),
        run.stderr
    );

    let got = &run.stdout_bytes;
    assert_eq!(
        got.len(),
        LEN,
        "expected the {LEN} byte response body of {url} on the HTTP/3 client's stdout; observed \
         {} bytes ({}).\n--- client stderr ---\n{}\n--- server log ---\n{log}",
        got.len(),
        first_diff(&want, got),
        run.stderr
    );
    assert!(
        want == *got,
        "expected the HTTP/3 response body of {url} back byte for byte; observed {}.\n\
         --- server log ---\n{log}",
        first_diff(&want, got)
    );

    drop(server);
}
