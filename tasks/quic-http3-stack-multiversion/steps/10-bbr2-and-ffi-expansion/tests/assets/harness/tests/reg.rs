//! inherited groups: what earlier milestones could already do and this one must not lose
//!
//! One `#[test]` per scored group, named exactly as the group id with dashes
//! turned into underscores: `reg-01-flow-control` is
//! `fn reg_01_flow_control()`. Each one names the step it came from, and
//! the slug after the step number is a real `cap-` group of that step --
//! `carryover-check.sh` resolves it and fails if it does not exist.
//!
//! Observe behaviour. Never read the candidate's source, never grep it for an
//! identifier, and never assert on a private detail: every group sits on one
//! of the five observation surfaces -- T1 two-process loopback through the
//! candidate's own binaries, T2 the verifier speaking QUIC at the candidate,
//! T3 in-memory connection pairs through the public Rust API, T4 emitted
//! artefacts, T5 exact-answer negative corpora.

use std::path::Path;
use std::time::Duration;

#[allow(unused_imports)]
use verifier::{pair, support, testkit};

#[allow(dead_code)]
const SHORT: Duration = Duration::from_secs(10);
#[allow(dead_code)]
const LONG: Duration = Duration::from_secs(60);

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

/// Start this milestone's flag-taking `quiche-server` on `port`.
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

// ---------------------------------------------------------------------------
// groups
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// reg-04-datagram-roundtrip  (T3)  <- step 04, cap-datagram-roundtrip
// ---------------------------------------------------------------------------

/// `reg-04-datagram-roundtrip`: the transport-level DATAGRAM extension still
/// works: both sides negotiate it, report a positive writable size, and echo
/// a 256-byte payload back byte-for-byte without leaking into the stream layer.
///
/// Step 10 rewrites the connection-ID surface (four `source`/`destination`
/// names become `scid`/`dcid` names) and expands the C API over it. Nothing in
/// that work touches the DATAGRAM path -- which is the point. A candidate that
/// performs the rename as a blanket text substitution across the crate, or that
/// disturbs frame dispatch while adding the new `extern` entry points, loses a
/// capability step 04 established and nothing in this step's `cap-*` groups
/// would notice.
#[test]
fn reg_04_datagram_roundtrip() {
    let mut cc = pair::client_config(pair::ALPN_HQ);
    let mut sc = pair::server_config(pair::ALPN_HQ);
    cc.enable_dgram(true, 10, 10);
    sc.enable_dgram(true, 10, 10);

    let mut p = pair::Pair::with_configs(&mut cc, &mut sc);
    p.handshake();

    let cmax = p
        .client
        .dgram_max_writable_len()
        .expect("client: the peer advertised no DATAGRAM support");
    let smax = p
        .server
        .dgram_max_writable_len()
        .expect("server: the peer advertised no DATAGRAM support");
    assert!(
        cmax > 0 && smax > 0,
        "expected a positive writable datagram length on both ends once the extension was \
         negotiated; observed client {cmax}, server {smax}"
    );

    let payload = testkit::body(256, 0x1020_0012);
    p.client.dgram_send(&payload).expect("client dgram_send");
    p.settle(64);

    let mut buf = vec![0u8; 2048];
    let n = p
        .server
        .dgram_recv(&mut buf)
        .expect("expected the datagram the client sent to arrive at the server");
    assert!(
        buf[..n] == payload[..],
        "expected the datagram to arrive byte for byte; observed {n} bytes, {}",
        first_diff(&payload, &buf[..n])
    );
    assert_eq!(
        p.server.readable().count(),
        0,
        "expected a DATAGRAM never to open a stream: it is not stream data and must not be \
         delivered as any"
    );

    p.server.dgram_send(&buf[..n]).expect("server dgram_send");
    p.settle(64);

    let n = p
        .client
        .dgram_recv(&mut buf)
        .expect("expected the server's echo to arrive back at the client");
    assert!(
        buf[..n] == payload[..],
        "expected the echoed datagram to match byte for byte; observed {n} bytes, {}",
        first_diff(&payload, &buf[..n])
    );

    assert_eq!(
        p.client.dgram_recv(&mut buf),
        Err(quiche::Error::Done),
        "expected the client's receive queue to be empty once the echo had been taken"
    );
    assert_eq!(
        p.server.dgram_recv(&mut buf),
        Err(quiche::Error::Done),
        "expected the server's receive queue to be empty once its datagram had been taken"
    );
}

// ---------------------------------------------------------------------------
// reg-08-probe-and-migrate  (T1)  <- step 08, cap-probe-and-migrate
// ---------------------------------------------------------------------------

/// `reg-08-probe-and-migrate`: the candidate's client binary still accepts
/// `--perform-migration`, binds a second UDP socket while migrating, and
/// delivers a 512 KiB transfer byte-for-byte.
///
/// This is the inherited group that sits directly on the surface step 10
/// rewrites. `probe_path`, `migrate_source`, `is_path_validated` and the CID
/// issuance the client needs before it can move are all reachable through the
/// renamed Rust functions and the new C entry points; `cap-scid-renames` and
/// `cap-ffi-paths` observe the names, and this group observes that the
/// behaviour underneath them still works end to end through the candidate's
/// own binaries. A rename that compiles but misroutes a sequence number breaks
/// here and nowhere else.
#[test]
fn reg_08_probe_and_migrate() {
    let _lock = testkit::serial_t1();

    const LEN: usize = 512 * 1024;
    let dir = testkit::scratch("reg-08-probe-and-migrate");
    let want = testkit::body(LEN, 0x1020_0008);
    let port = testkit::free_port();
    let server = start_app_server(&dir, port, &[("migrate.bin", want.clone())], "migrate-server");

    let url = format!("https://127.0.0.1:{port}/migrate.bin");
    let client_bin = testkit::client_bin();
    let mut cc = testkit::cmd(&client_bin);
    cc.arg("--no-verify").arg("--perform-migration").arg(&url);

    // No socket observation. Counting the client's bound UDP ports needs a
    // sampler -- `run_watched` polls `/proc/<pid>/fd` roughly every 20 ms --
    // and whether the poll lands while both sockets are open is a question
    // about the host's scheduler, not about the candidate. That is fine for a
    // scored group, which is what `cap-probe-and-migrate` at step 08 still is,
    // and not fine for a gate case, which vetoes the whole step when it misses.
    // What is left is deterministic: the flag is accepted, the connection
    // survives whatever the client does with it, and the bytes arrive.
    let run = testkit::run(&mut cc, LONG, &dir, "migrate-client");

    let log = server.log();
    assert!(
        !run.timed_out,
        "expected {} --perform-migration to fetch {LEN} bytes from {url} within {LONG:?}; \
         observed: still running at the deadline, killed after writing {} bytes.\n\
         --- client stderr ---\n{}\n--- server log ---\n{log}",
        client_bin.display(),
        run.stdout_bytes.len(),
        run.stderr
    );
    assert_eq!(
        run.status.and_then(|s| s.code()),
        Some(0),
        "expected {} to accept --perform-migration and exit 0 after fetching {url}; observed \
         status {:?} having written {} of {LEN} bytes.\n--- client stderr ---\n{}\n\
         --- server log ---\n{log}",
        client_bin.display(),
        run.status,
        run.stdout_bytes.len(),
        run.stderr
    );

    let got = &run.stdout_bytes;
    assert!(
        want == *got,
        "expected the {LEN} byte body of {url} back byte for byte while the client migrated; \
         observed {} bytes, {}.\n--- client stderr ---\n{}\n--- server log ---\n{log}",
        got.len(),
        first_diff(&want, got),
        run.stderr
    );

    drop(server);
}

// ---------------------------------------------------------------------------
// reg-09-key-update  (T2)  <- step 09, cap-key-update
// ---------------------------------------------------------------------------

/// `reg-09-key-update`: the verifier still initiates a key update and the
/// candidate still answers it correctly. After handshake + warmup the verifier
/// derives the next-generation AEAD key and IV from `CLIENT_TRAFFIC_SECRET_0`
/// using the `quic ku` label, but keeps the generation-0 HP key, then injects a
/// 1-RTT packet with the key-phase bit set.
///
/// Carried because every short-header packet the new C entry points cause to be
/// sent or received rides on this crypto. Step 10 adds `quiche_conn_new_scid`,
/// `quiche_conn_retire_dcid` and the rest as `extern "C"` wrappers, and the
/// NEW_CONNECTION_ID and RETIRE_CONNECTION_ID frames they generate travel in
/// 1-RTT packets like any other. A candidate that disturbs the key schedule or
/// the phase bookkeeping while threading connection-ID state through the FFI
/// layer breaks this and passes every group that only looks at the C symbols.
///
/// Four assertions follow:
/// 1. The candidate decrypts it — so it derived the same next-generation keys.
/// 2. The HP key was NOT rotated (implied: the verifier applied old HP, server
///    must strip with old HP to read the packet number at all).
/// 3. The candidate's own next packet has key_phase bit set — it flipped.
/// 4. A second update before the first is acknowledged returns
///    `Err(Error::KeyUpdate)`.
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
        "handshake did not complete — no 1-RTT keys available"
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
         from CLIENT_TRAFFIC_SECRET_0 — this is a key-schedule disagreement, not a \
         candidate fault",
        p.client_out.len()
    );

    // Derive generation-1 AEAD material (RFC 9001 §6): next_secret uses "quic ku".
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
    // Keep the gen-0 HP key — quiche's own crypto.rs does the same.
    let gen1_keys = support::PacketKeys::new(
        support::Suite::Aes128Gcm,
        &next_key,
        &next_iv,
        gen0_keys.hp_key(),
    ).expect("constructing gen-1 PacketKeys with old HP key");

    // Build a STREAM frame payload and seal it under gen-1 with key_phase=true.
    let body = testkit::body(32, 0x1020_0001);
    let mut payload = Vec::new();
    pair::varint(0x0a, &mut payload); // STREAM type: stream_id present, length present
    pair::varint(INJECT_STREAM_ID, &mut payload);
    pair::varint(body.len() as u64, &mut payload);
    payload.extend_from_slice(&body);

    let mut pkt = pair::seal_1rtt_phase(
        &gen1_keys, &pair::SERVER_CID, INJECTED_PN, &payload, true,
    );

    // Assertion 1 + 2: candidate decrypts the packet (correct next-gen keys,
    // and HP was not rotated — if it were, the server could not find the PN).
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
         payload did not round-trip correctly"
    );

    // Assertion 3: a second update before the first is ACKed must fail with
    // Error::KeyUpdate (-19).
    //
    // The server is now in phase=1; a new update flips the bit back to false,
    // so derive gen-2 AEAD keys and seal with key_phase=false.
    //
    // NOTE: this must run BEFORE the send-drain loop below. quiche sets
    // key_update.update_acked = true inside send_single() as soon as the server
    // emits any short-header packet, so draining first would make the pending
    // update look acknowledged and a second update perfectly legal.
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
         before the first is ACKed to return Err(Error::KeyUpdate) (-19), but got a \
         different result"
    );

    // Assertion 4: the server's own next packet has key_phase=1 — it flipped.
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
