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

use std::path::{Path, PathBuf};
use std::time::Duration;

#[allow(unused_imports)]
use verifier::{pair, support, testkit};

#[allow(dead_code)]
const SHORT: Duration = Duration::from_secs(10);
#[allow(dead_code)]
const LONG: Duration = Duration::from_secs(60);

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

/// First `n` bytes of a datagram as a lowercase hex string, for error messages.
fn head(d: &[u8], n: usize) -> String {
    testkit::hex(&d[..d.len().min(n)])
}

/// Start the apps-era `quiche-server` (or `server`) on `port` with no retry,
/// serving a single placeholder file so it stays alive.
fn start_draft_server(dir: &Path, port: u16, tag: &str) -> testkit::Server {
    let root = testkit::docroot(dir, &[("index.txt", b"ok\n".to_vec())]);
    let tls = testkit::tls();
    let bin = testkit::server_bin();
    let mut c = testkit::cmd(&bin);
    c.arg("--listen")
        .arg(format!("127.0.0.1:{port}"))
        .arg("--cert").arg(&tls.cert)
        .arg("--key").arg(&tls.key)
        .arg("--root").arg(&root)
        .arg("--no-retry");
    testkit::spawn_server(&mut c, port, dir, tag, SHORT)
}

/// Write a throwaway crate under `dir` whose only dependency is the
/// candidate's `quiche` package, and return its directory.
fn write_quiche_probe(dir: &Path, probe_name: &str, main_rs: &str) -> PathBuf {
    let probe = dir.join(probe_name);
    std::fs::create_dir_all(probe.join("src")).expect("create probe crate");
    let quiche_path = testkit::quiche_crate_dir();
    std::fs::write(
        probe.join("Cargo.toml"),
        format!(
            "[package]\nname = \"{probe_name}\"\nversion = \"0.0.0\"\nedition = \"2021\"\n\
             publish = false\n\n[workspace]\n\n[dependencies]\n\
             quiche = {{ path = \"{}\" }}\n",
            quiche_path.display()
        ),
    ).expect("write probe Cargo.toml");
    std::fs::write(probe.join("src/main.rs"), main_rs).expect("write probe main.rs");
    probe
}

/// `cargo build --offline` the probe crate and return the `Run` result.
fn build_quiche_probe(dir: &Path, probe: &Path, tag: &str) -> testkit::Run {
    let target = dir.join("target");
    let mut c = testkit::cmd("cargo");
    c.arg("build")
        .arg("--offline")
        .arg("--manifest-path").arg(probe.join("Cargo.toml"))
        .env("CARGO_TARGET_DIR", &target);
    testkit::run(&mut c, LONG, dir, tag)
}

// ---------------------------------------------------------------------------
// groups
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// cap-drafts-gone  (T2)
// ---------------------------------------------------------------------------

/// `cap-drafts-gone`: draft-27, -28 and -29 each draw a Version Negotiation
/// packet from the candidate's server, and the version list in it is exactly
/// one entry: VERSION_V1 (0x00000001). No draft is advertised any longer.
///
/// The verifier builds each Initial itself, so the candidate's library is not
/// involved. A server that still supports any of the three answers with an
/// Initial or a Retry; a server that negotiates but still lists a draft version
/// fails on the list assertion; a server that echoes the wrong CIDs fails the
/// RFC 9000 §6.1 CID-echo check.
#[test]
fn cap_drafts_gone() {
    let _serial = testkit::serial_t1();
    let dir = testkit::scratch("cap-drafts-gone");
    let port = testkit::free_port();
    let server = start_draft_server(&dir, port, "drafts-gone-server");
    let to = testkit::loopback(port);

    // Only VERSION_V1 is supported at this milestone.
    let want: &[u32] = &[support::VERSION_V1];

    for (name, version) in [
        ("draft-27", support::VERSION_DRAFT27),
        ("draft-28", support::VERSION_DRAFT28),
        ("draft-29", support::VERSION_DRAFT29),
    ] {
        let ep = testkit::Endpoint::new();
        // Distinct CIDs so the echo assertions are non-trivial.
        let dcid: Vec<u8> = (0u8..16).map(|i| 0xa0 ^ i).collect();
        let scid: Vec<u8> = (0u8..16).map(|i| 0x5f ^ i).collect();
        let crypto = vec![0x02u8; 300];

        let pkt = support::ClientInitial::new(version, &dcid, &scid, &crypto)
            .build()
            .unwrap_or_else(|e| {
                panic!("the verifier could not build a {name} Initial: {e}")
            });
        ep.send_to(&pkt, to);

        let got = ep.recv_all(Duration::from_millis(900));
        let log = server.log();
        assert!(
            !got.is_empty(),
            "expected a Version Negotiation packet in reply to a {name} ({version:#010x}) \
             Initial; no datagram arrived within 900 ms.\n--- server log ---\n{log}"
        );

        for d in &got {
            let vn = support::VersionNegotiationPacket::parse(d).unwrap_or_else(|e| {
                panic!(
                    "expected every reply to a {name} ({version:#010x}) Initial to be a \
                     Version Negotiation packet (zero-version long header); parse error: {e} \
                     on first {} bytes.\n--- server log ---\n{log}",
                    head(d, 48)
                )
            });
            let mut offered = vn.versions.clone();
            offered.sort_unstable();
            let mut expected = want.to_vec();
            expected.sort_unstable();
            assert_eq!(
                offered, expected,
                "expected the VN list for a {name} Initial to be exactly [VERSION_V1 \
                 (0x00000001)] — one entry, no drafts — but observed {:?}.\n\
                 --- server log ---\n{log}",
                vn.versions.iter().map(|v| format!("{v:#010x}")).collect::<Vec<_>>()
            );
            // RFC 9000 §6.1: VN.DCID = client SCID, VN.SCID = client DCID.
            assert_eq!(
                vn.dcid, scid.as_slice(),
                "VN packet DCID should echo the client's SCID ({}) but got {}.\n\
                 --- server log ---\n{log}",
                testkit::hex(&scid), testkit::hex(vn.dcid)
            );
            assert_eq!(
                vn.scid, dcid.as_slice(),
                "VN packet SCID should echo the client's DCID ({}) but got {}.\n\
                 --- server log ---\n{log}",
                testkit::hex(&dcid), testkit::hex(vn.scid)
            );
        }
    }

    drop(server);
}

// ---------------------------------------------------------------------------
// cap-key-update  (T2)
// ---------------------------------------------------------------------------

/// `cap-key-update`: the verifier initiates a key update (the candidate only
/// needs to implement the responder side). After handshake + warmup, it derives
/// the next-generation AEAD key and IV from `CLIENT_TRAFFIC_SECRET_0` using
/// the `quic ku` label, but keeps the generation-0 HP key, then injects a
/// 1-RTT packet with the key-phase bit set.
///
/// Five assertions follow:
/// 1. The candidate decrypts it — so it derived the same next-generation keys.
/// 2. The HP key was NOT rotated (implied: the verifier applied old HP, server
///    must strip with old HP to read the packet number at all).
/// 3. The candidate's own next packet has key_phase bit set — it flipped.
/// 4. A second update before the first is acknowledged returns
///    `Err(Error::KeyUpdate)`.
/// 5. The previous generation's opening key is retained across reordering: a
///    packet sealed with the old key phase, numbered below the one that
///    triggered the update, still decrypts on a fresh pair. (A sixth
///    assertion — that retention is *bounded* — was tried and deleted; see
///    the comment at its deletion site below and
///    `misc/quic-http3-stack-multiversion/NOTES.md`.)
#[test]
fn cap_key_update() {
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
    let body = testkit::body(32, 0x0918_0001);
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
    // NOTE: this must run BEFORE the send-drain loop below (assertion 4). The
    // contract requires the second update to be rejected only while the first
    // is genuinely unacknowledged; injecting it here, before anything drains,
    // keeps that condition unambiguous. Draining first risks a candidate
    // treating its own subsequent send as evidence the first update was
    // acknowledged -- legalising the second update for a reason that has
    // nothing to do with the property this assertion checks.
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

    // Assertion 5 uses a *second*, independent pair. Assertion 3 above
    // deliberately drives `p.server` into `Err(Error::KeyUpdate)`, and quiche
    // treats any error out of `recv()` as fatal: it calls `self.close(...)`
    // internally, which makes every later `recv()` on that same connection
    // return `Error::Done` unconditionally. Reusing `p` here would make
    // assertion 5 fail for a reason that has nothing to do with retention.
    // A fresh pair, which never attempts a premature second update, keeps
    // `recv()` live for as long as this assertion needs it.
    let mut cc2 = pair::client_config(pair::ALPN_HQ);
    cc2.log_keys();
    let mut sc2 = pair::server_config(pair::ALPN_HQ);
    let (mut p2, keylog2) = pair::Pair::with_keylog(&mut cc2, &mut sc2);
    p2.handshake();
    p2.client.stream_send(STREAM_ID, b"warmup", false).expect("stream_send warmup (pair 2)");
    p2.settle(16);

    let secret0_2 = pair::keylog_secret(&keylog2.contents(), "CLIENT_TRAFFIC_SECRET_0")
        .expect("pair 2 client did not write CLIENT_TRAFFIC_SECRET_0 to its keylog");
    let gen0_keys_2 = support::PacketKeys::from_secret(support::Suite::Aes128Gcm, &secret0_2)
        .expect("deriving pair-2 gen-0 1-RTT keys");
    let (_next_secret_2, gen1_keys_2) =
        derive_next_gen(&secret0_2, gen0_keys_2.hp_key());

    let body2 = testkit::body(32, 0x0918_0003);
    let mut payload2 = Vec::new();
    pair::varint(0x0a, &mut payload2);
    pair::varint(INJECT_STREAM_ID, &mut payload2);
    pair::varint(body2.len() as u64, &mut payload2);
    payload2.extend_from_slice(&body2);
    let mut update_pkt2 = pair::seal_1rtt_phase(
        &gen1_keys_2, &pair::SERVER_CID, INJECTED_PN, &payload2, true,
    );
    let update_result2 = p2.server.recv(
        &mut update_pkt2,
        quiche::RecvInfo { from: pair::client_addr(), to: pair::server_addr() },
    );
    assert!(
        update_result2.is_ok(),
        "pair 2: the key update that assertion 5 depends on was itself rejected \
         ({:?}); this is the same update assertion 1 already proved works, replayed on \
         a fresh pair",
        update_result2.unwrap_err()
    );

    // Assertion 5: the previous generation's opening key is retained across
    // reordering. TEST_INTERFACE.md: the receiver "keeps the previous opening
    // key so that reordered packets from before the update still decrypt". A
    // packet sealed under the *old* key phase and generation, carrying a
    // packet number below INJECTED_PN (the packet that triggered the
    // update), simulates one that was genuinely in flight before the peer
    // updated and simply arrived late.
    const REORDERED_PN: u64 = INJECTED_PN - 1;
    const REORDER_STREAM_ID: u64 = 12;
    let reorder_body = testkit::body(24, 0x0918_0002);
    let mut reorder_payload = Vec::new();
    pair::varint(0x0a, &mut reorder_payload);
    pair::varint(REORDER_STREAM_ID, &mut reorder_payload);
    pair::varint(reorder_body.len() as u64, &mut reorder_payload);
    reorder_payload.extend_from_slice(&reorder_body);
    let mut reordered_pkt = pair::seal_1rtt_phase(
        &gen0_keys_2, &pair::SERVER_CID, REORDERED_PN, &reorder_payload, false,
    );
    let reordered_result = p2.server.recv(
        &mut reordered_pkt,
        quiche::RecvInfo { from: pair::client_addr(), to: pair::server_addr() },
    );
    assert!(
        reordered_result.is_ok(),
        "a packet numbered below the one that triggered the key update, sealed with the \
         *previous* generation's keys and key phase, was rejected ({:?}); the previous \
         opening key should still be retained for exactly this case",
        reordered_result.unwrap_err()
    );
    let (reordered_got, _fin) = pair::read_stream(&mut p2.server, REORDER_STREAM_ID);
    assert_eq!(
        reordered_got.as_slice(), reorder_body.as_slice(),
        "the reordered gen-0 packet decrypted but its STREAM payload did not round-trip"
    );

    // Assertion 6 (deleted, round 1, step 09): "that retention is bounded".
    //
    // The observation this asserted -- that a packet sealed under the previous
    // generation's keys stops decrypting once the three-PTO retention timer
    // fires -- cannot be made from outside the candidate, and the assertion as
    // written could not fail against any candidate at all:
    //
    //   * `recv()` reports a wrong-key packet as `Err(Error::Done)`, which is
    //     also what it reports for a packet it processed successfully. See
    //     `drop_pkt_on_err` in the candidate's `lib.rs`: once `recv_count > 0`
    //     (i.e. after the handshake this group starts with) every unauthenticated
    //     packet is flattened to `Error::Done` to avoid letting an off-path
    //     attacker distinguish "bad key" from "good packet". So `is_err()` was
    //     never true for the stale packet even while retention was working, and
    //     the assertion was un-failable.
    //   * Independently, `Pair::tick` drives *both* connections' timers, so the
    //     three-second wait also lets the client reach its own key update. The
    //     server's phase then advances to generation 2, and a generation-0
    //     packet is no longer recognisable as stale: it is read as a fresh
    //     update attempt.
    //
    // Assertion 5 above (the previous opening key is still usable for a packet
    // numbered below the one that triggered the update, while the update is
    // outstanding) does stand on its own and is retained: the reordered packet
    // is decrypted *and* its STREAM payload is read back. What was dropped here
    // is only the second half of the pair, which had no observation behind it.
    //
    // Recorded in misc/quic-http3-stack-multiversion/NOTES.md.
}

/// HKDF-Expand-Label with `"quic ku"` from `secret`, producing the next
/// generation's packet-protection key and IV while keeping `hp_key` (header
/// protection is never re-derived on a key update, RFC 9001 §6).
fn derive_next_gen(secret: &[u8], hp_key: &[u8]) -> (Vec<u8>, support::PacketKeys) {
    let next_secret = support::kdf::next_secret(secret).expect("kdf::next_secret");
    let next_key = support::kdf::expand_label_to_vec(
        &next_secret, support::kdf::LABEL_KEY, b"",
        support::Suite::Aes128Gcm.key_len(),
    ).expect("expand next key");
    let next_iv = support::kdf::expand_label_to_vec(
        &next_secret, support::kdf::LABEL_IV, b"",
        support::Suite::Aes128Gcm.iv_len(),
    ).expect("expand next iv");
    let keys = support::PacketKeys::new(
        support::Suite::Aes128Gcm, &next_key, &next_iv, hp_key,
    ).expect("constructing next-gen PacketKeys");
    (next_secret, keys)
}

// ---------------------------------------------------------------------------
// cap-h3-datagram-removed  (T3)
// ---------------------------------------------------------------------------

/// `cap-h3-datagram-removed`: the HTTP/3-level datagram API
/// (`h3::Connection::send_dgram`, `recv_dgram`, `dgram_max_writable_len`) is
/// gone at this milestone. Scored as the compile failure it is, not by
/// inspecting the candidate's source.
///
/// Two fixture crates are built with `cargo build --offline`:
/// * The **subject** calls all three removed methods → must fail to compile,
///   and the error output must name `send_dgram`.
/// * The **control** uses only the transport-level `dgram_send` / `dgram_recv`
///   / `Connection::dgram_max_writable_len` → must succeed.
///
/// The pair is the point: the subject alone passes on any broken crate; the
/// control alone proves nothing about the deletion.
#[test]
fn cap_h3_datagram_removed() {
    let dir = testkit::scratch("cap-h3-datagram-removed");

    // ------------------------------------------------------------------
    // Subject: calls the h3-level dgram API that was removed at 0.18.0.
    // It must fail to build.
    // ------------------------------------------------------------------
    let subject_src = r#"
fn main() {
    // This crate is only built to check that these names are gone.
    // It is never run.
    use quiche::h3;
    let _config = h3::Config::new().unwrap();
    // The three deleted methods: send_dgram, recv_dgram, dgram_max_writable_len.
    // Referencing them as function items is enough to make the compiler report
    // the missing names whether or not we have a live connection to call them on.
    let _ = h3::Connection::send_dgram as fn(_, _, _, _) -> _;
    let _ = h3::Connection::recv_dgram as fn(_, _, _) -> _;
    let _ = h3::Connection::dgram_max_writable_len as fn(_) -> _;
}
"#;
    let subject = write_quiche_probe(&dir, "h3-dgram-subject", subject_src);
    let subject_run = build_quiche_probe(&dir, &subject, "h3-dgram-subject");
    assert!(
        !subject_run.ok(),
        "the subject crate that calls h3::Connection::send_dgram / recv_dgram / \
         dgram_max_writable_len built successfully, but those three methods should \
         have been removed at this milestone (quiche 0.18.0)"
    );
    assert!(
        subject_run.output().contains("send_dgram"),
        "the subject crate failed to build (good), but the compiler output did not \
         mention `send_dgram`. A build failure unrelated to the API deletion would \
         satisfy the first assertion by accident. Compiler output:\n{}",
        subject_run.output()
    );

    // ------------------------------------------------------------------
    // Control: uses only transport-level datagram calls, which survive.
    // It must build cleanly.
    // ------------------------------------------------------------------
    let control_src = r#"
fn main() {
    // Transport-level datagram API: still present at 0.18.0.
    // We reference the methods as function items to confirm they exist,
    // without needing a live Config or Connection.
    let _ = quiche::Connection::dgram_send as fn(_, _) -> _;
    let _ = quiche::Connection::dgram_recv as fn(_, _) -> _;
    let _ = quiche::Connection::dgram_max_writable_len as fn(_) -> _;
}
"#;
    let control = write_quiche_probe(&dir, "h3-dgram-control", control_src);
    let control_run = build_quiche_probe(&dir, &control, "h3-dgram-control");
    assert!(
        control_run.ok(),
        "the control crate that uses only transport-level dgram_send / dgram_recv / \
         dgram_max_writable_len failed to build, but those methods should still be \
         present at this milestone. Build output:\n{}",
        control_run.output()
    );
}

// ---------------------------------------------------------------------------
// cap-initial-cwnd-configurable  (T3)
// ---------------------------------------------------------------------------

/// `cap-initial-cwnd-configurable`: `set_initial_congestion_window_packets`
/// sets the initial window in units of `max_send_udp_payload_size` packets.
/// The default is 10 (RFC 9002 §7.2). Assertions are relative — 20 packets is
/// twice default, 5 packets is half — so they hold for any `max_send_udp_payload_size`.
#[test]
fn cap_initial_cwnd_configurable() {
    // Helper: handshake a pair and read the client's initial cwnd, together
    // with that same connection's max_send_udp_payload_size -- packets are
    // sized against the latter, so the default's byte value can be checked
    // independently as ten of it (RFC 9002 §7.2), not just as a ratio against
    // another config's reading.
    fn initial_cwnd(packets: Option<usize>) -> (usize, usize) {
        let mut cc = pair::client_config(pair::ALPN_HQ);
        let mut sc = pair::server_config(pair::ALPN_HQ);
        if let Some(n) = packets {
            cc.set_initial_congestion_window_packets(n);
            sc.set_initial_congestion_window_packets(n);
        }
        let mut p = pair::Pair::with_configs(&mut cc, &mut sc);
        p.handshake();
        let cwnd = p.client
            .path_stats()
            .next()
            .expect("no path stats available after handshake")
            .cwnd;
        let mss = p.client.max_send_udp_payload_size();
        (cwnd, mss)
    }

    let (cwnd_default, mss_default) = initial_cwnd(None);
    let (cwnd_20, _) = initial_cwnd(Some(20));
    let (cwnd_5, _) = initial_cwnd(Some(5));

    // 20-packet window is exactly 2× default.
    assert_eq!(
        cwnd_20, cwnd_default * 2,
        "expected set_initial_congestion_window_packets(20) to produce a window \
         exactly twice the default ({cwnd_default} bytes), but got {cwnd_20} bytes. \
         A candidate that ignores the call produces the same value for all three."
    );

    // 5-packet window is exactly half the default.
    assert_eq!(
        cwnd_5, cwnd_default / 2,
        "expected set_initial_congestion_window_packets(5) to produce a window \
         exactly half the default ({cwnd_default} bytes), but got {cwnd_5} bytes."
    );

    // Default is the 10-packet RFC 9002 §7.2 recommendation, established
    // independently of the 20-/5-packet readings above: it must equal ten of
    // this same connection's own max_send_udp_payload_size, not merely be
    // self-consistent with the ratios asserted from the other two configs.
    let expected_default = 10 * mss_default;
    assert_eq!(
        cwnd_default, expected_default,
        "expected the default initial congestion window to be ten packets of \
         max_send_udp_payload_size ({mss_default} bytes each, RFC 9002 §7.2), i.e. \
         {expected_default} bytes, but got {cwnd_default} bytes."
    );
    // More directly: default == cwnd_20 / 2 == cwnd_5 * 2.
    assert_eq!(
        cwnd_default, cwnd_5 * 2,
        "expected default (10 packets) = 2 × five-packet ({cwnd_5} bytes); \
         got default = {cwnd_default} bytes"
    );
}

// ---------------------------------------------------------------------------
// cap-stream-next-prioritised  (T3)
// ---------------------------------------------------------------------------

/// `cap-stream-next-prioritised`: `stream_readable_next` returns streams in
/// priority order: urgency ascending (lower = higher priority), non-incremental
/// before incremental at the same urgency, lower stream ID for ties within a
/// class, and round-robin rotation among same-urgency incremental streams
/// (RFC 9218 §4.2). `stream_writable_next` returns `None` when send capacity
/// is exhausted.
///
/// The rotation clause is checked on a second, fresh `pair::Pair` (below),
/// not on the five-stream fixture above: stream 8 there is popped from the
/// readable queue but never consumed via `stream_recv`, which would leave it
/// in an ambiguous readable state for a second round on the same connection.
#[test]
fn cap_stream_next_prioritised() {
    let mut cc = pair::client_config(pair::ALPN_HQ);
    let mut sc = pair::server_config(pair::ALPN_HQ);
    let mut p = pair::Pair::with_configs(&mut cc, &mut sc);
    p.handshake();

    // Five streams, priorities set before any data is written.
    // Stream IDs 0, 4, 8, 12, 16 are client-initiated bidirectional.
    //   stream  0: urgency 1, non-incremental
    //   stream  4: urgency 1, non-incremental
    //   stream  8: urgency 1, incremental
    //   stream 12: urgency 3, non-incremental
    //   stream 16: urgency 3, non-incremental
    for (id, urgency, incremental) in [
        (0u64,  1u8, false),
        (4u64,  1u8, false),
        (8u64,  1u8, true),
        (12u64, 3u8, false),
        (16u64, 3u8, false),
    ] {
        p.client
            .stream_priority(id, urgency, incremental)
            .unwrap_or_else(|e| {
                panic!("stream_priority({id}, {urgency}, {incremental}): {e:?}")
            });
    }

    // Write one byte to each stream so the server has readable data.
    let body = b"x";
    for id in [0u64, 4, 8, 12, 16] {
        p.client.stream_send(id, body, false).unwrap_or_else(|e| {
            panic!("stream_send on stream {id}: {e:?}")
        });
    }

    p.settle(16);

    // Drain stream_readable_next and record the order.
    let mut order = Vec::new();
    loop {
        match p.server.stream_readable_next() {
            Some(id) => order.push(id),
            None => break,
        }
        if order.len() > 10 {
            panic!("stream_readable_next did not drain: {order:?}");
        }
    }

    // Expected order: urgency-1 non-incr by ascending ID, then urgency-1 incr,
    // then urgency-3 non-incr by ascending ID.
    let expected = vec![0u64, 4, 8, 12, 16];
    assert_eq!(
        order, expected,
        "stream_readable_next returned streams in order {order:?}; expected {expected:?}. \
         The documented rule: urgency ascending, non-incremental before incremental at \
         same urgency, lower stream ID for ties within the same class."
    );

    // Disarming property: calling stream_readable_next again with no new data
    // must return None.
    assert_eq!(
        p.server.stream_readable_next(), None,
        "stream_readable_next returned a stream after the queue was already drained to \
         None with no new data delivered. It should return None until new data arrives."
    );

    // NOT asserted here: that `readable()` still lists all five streams after
    // the drain above. An earlier round-2 revision added that assertion on the
    // strength of a sentence in the contract, and it FAILED against the Oracle.
    // The sentence is what is wrong, not the candidate. In the Oracle:
    //
    //   pub fn readable(&self) -> StreamIter { self.streams.readable() }
    //
    // `readable()` iterates the very same `streams.readable` priority queue
    // that `stream_readable_next` pops from (`remove_readable`). Draining one
    // necessarily empties the other, so after the loop above `readable()` is
    // legitimately empty. There is no independent view to compare against, and
    // no candidate behaviour can make the two disagree. The contract sentence
    // was corrected in the same round; see misc/.../NOTES.md Sweep 15.

    // stream_writable_next: drain the ordinary per-stream writable queue first,
    // with connection-level send capacity still wide open.
    let mut writable_count = 0usize;
    loop {
        match p.client.stream_writable_next() {
            Some(_) => { writable_count += 1; }
            None => break,
        }
        if writable_count > 20 {
            panic!("stream_writable_next did not drain after 20 calls");
        }
    }
    // Disarming property, the per-stream half: once the writable queue is
    // empty, a further call must return None until capacity changes.
    assert_eq!(
        p.client.stream_writable_next(), None,
        "stream_writable_next returned a stream after the writable queue was already \
         drained to None with no capacity change."
    );

    // Disarming property, the connection-level half: fill the client's
    // connection-level send capacity and confirm stream_writable_next still
    // reads None even though the streams have plenty of their own
    // flow-control room left (`pair::tune` sets every per-stream limit to
    // 1,000,000 bytes; the initial congestion window is far smaller). Writing
    // into stream 0 alone until send returns `Error::Done` proves the
    // connection, not that one stream's own window, is what emptied: 64
    // writes of 4096 bytes is 262,144 bytes, well past any initial congestion
    // window and well short of stream 0's own 1,000,000-byte limit.
    let filler = vec![0u8; 4096];
    let mut exhausted = false;
    let mut total_sent = 0usize;
    for _ in 0..64 {
        match p.client.stream_send(0, &filler, false) {
            Ok(n) => total_sent += n,
            Err(quiche::Error::Done) => {
                exhausted = true;
                break;
            },
            Err(e) => panic!(
                "stream_send(0, ..): {e:?} while filling connection-level send capacity \
                 ({total_sent} bytes written so far)"
            ),
        }
    }
    assert!(
        exhausted,
        "connection-level send capacity did not exhaust after writing {total_sent} bytes \
         into stream 0; widen the fill loop to reach the initial congestion window"
    );
    // `stream_capacity` is documented as `min(connection capacity, this
    // stream's own capacity)`. Stream 16 was never written to, so its own
    // flow-control window is untouched; it reading 0 here is evidence the
    // connection-level ceiling, not stream 16's own window, is what is zero.
    let untouched_cap = p
        .client
        .stream_capacity(16)
        .unwrap_or_else(|e| panic!("stream_capacity(16): {e:?}"));
    assert_eq!(
        untouched_cap, 0,
        "stream 16 was never written to and its own flow-control window is untouched \
         (1,000,000 bytes, see pair::tune), yet stream_capacity(16) reports {untouched_cap} \
         instead of 0. With connection-level send capacity genuinely exhausted this must \
         read 0, because stream_capacity is min(connection-level capacity, this stream's \
         own capacity)."
    );
    assert_eq!(
        p.client.stream_writable_next(), None,
        "stream_writable_next returned a stream while connection-level send capacity was \
         exhausted (stream_capacity(16) == 0, checked above), even though individual \
         streams still have their own flow-control room. It should return None until \
         connection-level capacity is restored, not merely until each stream's own queue \
         entry is drained."
    );

    // ------------------------------------------------------------------
    // Round-robin among equal-urgency incremental streams (RFC 9218 §4.2).
    //
    // A fresh pair, not the fixture above: reusing streams 0-16 would leave
    // stream 8 (already popped from the readable queue above, but never
    // consumed via stream_recv) in an ambiguous readable state. Two new
    // incremental streams at the same urgency are written in one order,
    // drained and consumed, then written in the reverse order. If the
    // candidate rotates -- the just-served stream moves to the back of the
    // line, per RFC 9218 -- the second drain's order is the reverse of the
    // first's rather than the write order repeating or ascending ID winning
    // every time.
    // ------------------------------------------------------------------
    let mut cc2 = pair::client_config(pair::ALPN_HQ);
    let mut sc2 = pair::server_config(pair::ALPN_HQ);
    let mut p2 = pair::Pair::with_configs(&mut cc2, &mut sc2);
    p2.handshake();

    const ROT_A: u64 = 20;
    const ROT_B: u64 = 24;
    for id in [ROT_A, ROT_B] {
        p2.client
            .stream_priority(id, 1, true)
            .unwrap_or_else(|e| panic!("stream_priority({id}, 1, true): {e:?}"));
    }

    // Round 1: write A then B. With no prior history, the write/arrival
    // order is the serve order.
    p2.client.stream_send(ROT_A, b"x", false).expect("stream_send(ROT_A) round 1");
    p2.client.stream_send(ROT_B, b"x", false).expect("stream_send(ROT_B) round 1");
    p2.settle(16);

    let mut rotation_order_1 = Vec::new();
    let mut rotation_buf = [0u8; 16];
    loop {
        match p2.server.stream_readable_next() {
            Some(id) => {
                p2.server.stream_recv(id, &mut rotation_buf).unwrap_or_else(|e| {
                    panic!("stream_recv({id}) draining rotation round 1: {e:?}")
                });
                rotation_order_1.push(id);
            },
            None => break,
        }
    }
    assert_eq!(
        rotation_order_1, vec![ROT_A, ROT_B],
        "rotation round 1: two same-urgency incremental streams with no prior history \
         should be served in arrival order {:?}; got {rotation_order_1:?}",
        vec![ROT_A, ROT_B]
    );

    // Round 2: write B then A -- the reverse of round 1. Rotation means the
    // previously-served stream (A) yields to its sibling (B) again, so the
    // order should mirror round 1's arrival order (B, A), not repeat round
    // 1's serve order (A, B) and not favour the lower stream ID (A) again.
    p2.client.stream_send(ROT_B, b"y", false).expect("stream_send(ROT_B) round 2");
    p2.client.stream_send(ROT_A, b"y", false).expect("stream_send(ROT_A) round 2");
    p2.settle(16);

    let mut rotation_order_2 = Vec::new();
    loop {
        match p2.server.stream_readable_next() {
            Some(id) => rotation_order_2.push(id),
            None => break,
        }
    }
    assert_eq!(
        rotation_order_2, vec![ROT_B, ROT_A],
        "rotation round 2: streams {ROT_A} and {ROT_B} are equal-urgency incremental and \
         were written in the opposite order from round 1 (B, then A). RFC 9218 §4.2 \
         round-robins equal-urgency incremental streams, so stream {ROT_A} (served first \
         in round 1) must not be served first again here; expected {:?}, got \
         {rotation_order_2:?}",
        vec![ROT_B, ROT_A]
    );
}

// ---------------------------------------------------------------------------
// cap-ack-eliciting-on-demand  (T3)
// ---------------------------------------------------------------------------

/// `cap-ack-eliciting-on-demand`: after the connection settles to quiescence,
/// `send_ack_eliciting()` produces a datagram that draws a reply from the
/// server (proving it is ACK-eliciting, not a bare ACK). Then
/// `send_ack_eliciting_on_path(local, peer)` is checked: a known four-tuple
/// returns `Ok`, an unknown four-tuple returns `Err(Error::InvalidState)`.
#[test]
fn cap_ack_eliciting_on_demand() {
    let mut cc = pair::client_config(pair::ALPN_HQ);
    let mut sc = pair::server_config(pair::ALPN_HQ);
    let mut p = pair::Pair::with_configs(&mut cc, &mut sc);
    p.handshake();

    // Drive to quiescence: settle until no more datagrams are exchanged.
    p.settle(32);

    // Baseline: client send() returns Done — nothing pending.
    let mut buf = [0u8; pair::MAX_DATAGRAM];
    assert_eq!(
        p.client.send(&mut buf).unwrap_err(), quiche::Error::Done,
        "baseline failed: client.send() should return Done after settling to quiescence"
    );

    // send_ack_eliciting() must succeed.
    p.client.send_ack_eliciting()
        .expect("send_ack_eliciting() returned an error on an established connection");

    // Now client.send() must produce at least one datagram.
    let (len, info) = p.client.send(&mut buf)
        .expect("send() returned Done immediately after send_ack_eliciting() — \
                 the call must produce at least one datagram");

    // Deliver that datagram to the server.
    let mut pkt = buf[..len].to_vec();
    p.server.recv(&mut pkt, quiche::RecvInfo { from: info.from, to: info.to })
        .expect("server rejected the ACK-eliciting datagram");

    // The server must reply — that is what makes it ACK-eliciting rather than
    // a bare ACK (which would not draw a response).
    let mut server_replied = false;
    loop {
        match p.server.send(&mut buf) {
            Ok(_) => { server_replied = true; },
            Err(quiche::Error::Done) => break,
            Err(e) => panic!("server.send() after receiving ACK-eliciting packet: {e:?}"),
        }
    }
    assert!(
        server_replied,
        "the server did not reply to the ACK-eliciting datagram. A datagram carrying \
         only an ACK does not draw a response; a PING or data frame does."
    );

    // send_ack_eliciting_on_path with the connection's own four-tuple: Ok.
    let local = pair::client_addr();
    let peer  = pair::server_addr();
    p.client.send_ack_eliciting_on_path(local, peer)
        .unwrap_or_else(|e| {
            panic!(
                "send_ack_eliciting_on_path({local}, {peer}) returned {e:?} for the \
                 connection's own four-tuple; expected Ok"
            )
        });

    // send_ack_eliciting_on_path with an unknown four-tuple: Err(InvalidState).
    let unknown = pair::unknown_addr();
    let result = p.client.send_ack_eliciting_on_path(local, unknown);
    assert_eq!(
        result.unwrap_err(), quiche::Error::InvalidState,
        "send_ack_eliciting_on_path({local}, {unknown}) should return \
         Err(Error::InvalidState) for a four-tuple the connection has never seen"
    );
}


// ---------------------------------------------------------------------------
// cap-source-ids-and-retired  (T3)
// ---------------------------------------------------------------------------

/// `cap-source-ids-and-retired`: `source_ids()` iterates the connection's
/// source connection IDs in defined order (handshake first, then each issued
/// ID). `retired_scids()` counts CIDs the peer asked us to retire.
/// `active_source_cids()` and `source_cids_left()` are read alongside them
/// and asserted to agree, and a trailing compile-fail probe confirms
/// `max_active_source_cids` itself is gone (mirrors
/// `cap-h3-datagram-removed`'s subject-crate pattern; the in-body calls to
/// the two accessors already double as the "control" half, so no separate
/// control probe is needed).
///
/// Uses `set_active_connection_id_limit(3)` on both configs (bypassing
/// `tune()`'s default of 2) so there is room to issue a spare ID.
#[test]
fn cap_source_ids_and_retired() {
    let mut cc = pair::client_config(pair::ALPN_HQ);
    let mut sc = pair::server_config(pair::ALPN_HQ);
    // Need limit=3 on both sides to have room to issue a new SCID.
    cc.set_active_connection_id_limit(3);
    sc.set_active_connection_id_limit(3);
    let mut p = pair::Pair::with_configs(&mut cc, &mut sc);
    p.handshake();

    // Eagerly collect to owned bytes so there are no live borrows while we
    // call mutable methods below.
    let client_ids_initial: Vec<Vec<u8>> = p.client
        .source_ids()
        .map(|id| id.as_ref().to_vec())
        .collect();
    assert_eq!(
        client_ids_initial.len(), 1,
        "expected exactly 1 source CID on the client immediately after handshake; \
         got {}",
        client_ids_initial.len()
    );
    assert_eq!(
        p.client.retired_scids(), 0,
        "expected 0 retired SCIDs on the client immediately after handshake; \
         got {}",
        p.client.retired_scids()
    );

    let server_ids_initial: Vec<Vec<u8>> = p.server
        .source_ids()
        .map(|id| id.as_ref().to_vec())
        .collect();
    assert_eq!(
        server_ids_initial.len(), 1,
        "expected exactly 1 source CID on the server immediately after handshake; \
         got {}",
        server_ids_initial.len()
    );
    assert_eq!(
        p.server.retired_scids(), 0,
        "expected 0 retired SCIDs on the server immediately after handshake; \
         got {}",
        p.server.retired_scids()
    );

    // The initial source CID on the client is the handshake CID (CLIENT_CID).
    let handshake_cid_bytes = client_ids_initial[0].clone();
    assert_eq!(
        handshake_cid_bytes.as_slice(),
        pair::CLIENT_CID.as_ref(),
        "expected the handshake source CID to be CLIENT_CID"
    );

    // Issue a new source CID on the client. Sequence 0 is the handshake's;
    // the first issued one gets sequence 1.
    let (spare_cid, spare_token) = pair::spare_cid(0x71);
    let spare_bytes: Vec<u8> = spare_cid.as_ref().to_vec();
    let new_seq = p.client
        .new_source_cid(&spare_cid, spare_token, false)
        .expect("new_source_cid failed");
    assert_eq!(
        new_seq, 1,
        "expected new_source_cid to return sequence 1 (0 is the handshake's); got {new_seq}"
    );

    // source_ids() now yields two CIDs: handshake first, new second.
    let client_ids_after: Vec<Vec<u8>> = p.client
        .source_ids()
        .map(|id| id.as_ref().to_vec())
        .collect();
    assert_eq!(
        client_ids_after.len(), 2,
        "expected 2 source CIDs after issuing one; got {}",
        client_ids_after.len()
    );
    assert_eq!(
        client_ids_after[0].as_slice(), handshake_cid_bytes.as_slice(),
        "expected source_ids() to yield the handshake CID first"
    );
    assert_eq!(
        client_ids_after[1].as_slice(), spare_bytes.as_slice(),
        "expected source_ids() to yield the newly issued CID second"
    );

    // active_source_cids() and source_cids_left() must agree with source_ids().
    let active = p.client.active_source_cids();
    assert_eq!(
        active as usize, client_ids_after.len(),
        "active_source_cids() ({active}) disagrees with source_ids().count() ({})",
        client_ids_after.len()
    );
    let left = p.client.source_cids_left();
    assert_eq!(
        active as usize + left, 3,
        "active_source_cids ({active}) + source_cids_left ({left}) should equal \
         the limit (3)"
    );

    // Exchange packets so the server learns the new CID via NEW_CONNECTION_ID.
    p.settle(8);

    // Server retires the client's original DCID (sequence 0).
    p.server
        .retire_destination_cid(0)
        .expect("retire_destination_cid(0) failed");

    // Exchange until the RETIRE_CONNECTION_ID frame reaches the client.
    p.settle(8);

    // After retirement: retired_scids() on the client is 1.
    let retired = p.client.retired_scids();
    assert_eq!(
        retired, 1,
        "expected retired_scids() == 1 on the client after the server retired \
         sequence 0; got {retired}"
    );

    // source_ids() now yields one CID again — the retired one is gone.
    let client_ids_final: Vec<Vec<u8>> = p.client
        .source_ids()
        .map(|id| id.as_ref().to_vec())
        .collect();
    assert_eq!(
        client_ids_final.len(), 1,
        "expected source_ids() to yield 1 CID after the handshake CID was \
         retired; got {}",
        client_ids_final.len()
    );
    assert_ne!(
        client_ids_final[0].as_slice(), handshake_cid_bytes.as_slice(),
        "the handshake CID should have been retired and removed from source_ids()"
    );

    // ------------------------------------------------------------------
    // `max_active_source_cids` is removed at this milestone, not just
    // superseded. A candidate that kept the old name alongside the two new
    // accessors above would pass every assertion so far; only a compile-fail
    // probe distinguishes "removed" from "still present but unused".
    // ------------------------------------------------------------------
    let dir = testkit::scratch("cap-source-ids-and-retired");
    let subject_src = r#"
fn main() {
    // This crate is only built to check that the name is gone.
    // It is never run.
    let _ = quiche::Connection::max_active_source_cids as fn(_) -> _;
}
"#;
    let subject = write_quiche_probe(&dir, "max-active-scids-subject", subject_src);
    let subject_run = build_quiche_probe(&dir, &subject, "max-active-scids-subject");
    assert!(
        !subject_run.ok(),
        "the subject crate that references Connection::max_active_source_cids built \
         successfully, but that method should have been removed at this milestone \
         (quiche 0.18.0), replaced by active_source_cids() and source_cids_left()"
    );
    assert!(
        subject_run.output().contains("max_active_source_cids"),
        "the subject crate failed to build (good), but the compiler output did not \
         mention `max_active_source_cids`. A build failure unrelated to the API \
         deletion would satisfy the first assertion by accident. Compiler output:\n{}",
        subject_run.output()
    );
}
