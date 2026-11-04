//! inherited groups: what earlier milestones could already do and this one must not lose
//!
//! One `#[test]` per group, named exactly as the group id with dashes turned
//! into underscores: `reg-01-h3-get` is `fn reg_01_h3_get()`. Each one names
//! the step it came from, and the slug after the step number is a real
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
//! Oracle included.
//!
//! This milestone deletes the BoringSSL submodule and takes its crypto from
//! the `boring` crates.io binding instead. Everything the stack does is built
//! on that library: the handshake, every AEAD seal and open, every key
//! schedule. A backend swap that compiles and links and then derives one
//! secret differently is invisible to a layout check and total in practice,
//! so the three groups below are chosen to put the new library under load
//! from three directions. `reg-09-key-update` drives the key schedule from
//! the wire with a second implementation that shares none of the candidate's
//! crypto. `reg-01-h3-get` moves a whole file through the built binaries.
//! `reg-04-datagram-roundtrip` carries an unreliable payload that never
//! touches the stream layer, through the same AEAD.
//!
//! **What is deliberately not here.** No group observes a qlog file, an
//! HTTP/3 SETTINGS frame, or the `dgram_*_vec` pair. This step rewrites all
//! three on purpose -- the qlog schema jumps, `SETTINGS_MAX_FIELD_SECTION_SIZE`
//! acquires a default, and the vector datagram API is renamed -- and
//! behaviour a step deliberately changes is not regression. Carrying any of
//! them would gate the step on the candidate reproducing what this step is
//! asking it to replace. `reg-04-datagram-roundtrip` is on the datagram
//! surface but observes only `dgram_send`/`dgram_recv`, which this release
//! leaves exactly as they were.


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
/// This is the inherited group that sits on the surface step 15 rewrites.
/// The release renames `dgram_recv_vec` and `dgram_send_vec` to
/// `dgram_recv_buf` and `dgram_send_buf` and re-types them over the buffer
/// factory, and `cap-dgram-buf-api` scores the new pair. The four functions
/// underneath -- `dgram_send`, `dgram_recv`, `dgram_max_writable_len` and the
/// queue that backs them -- are untouched by that work, which is exactly why
/// they are worth gating: a rename carried out over the whole datagram module
/// can compile, satisfy `cap-dgram-buf-api`, and still misroute the
/// slice-taking pair that every existing caller uses. Nothing in this step's
/// `cap-*` groups would notice.
///
/// It is also a second reading on the new crypto backend, from an angle the
/// other two groups do not cover: a DATAGRAM frame is sealed with the same
/// 1-RTT keys as stream data but is never retransmitted, so it fails
/// differently if the AEAD is wrong.
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

    let payload = testkit::body(256, 0x1020_0015);
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
// reg-09-key-update  (T2)  <- step 09, cap-key-update
// ---------------------------------------------------------------------------

/// `reg-09-key-update`: the verifier still initiates a key update and the
/// candidate still answers it correctly.
///
/// Re-seeded from step 14's copy and unchanged in what it observes. There is
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
/// Carried because it is the one group here that reaches the wire. The other
/// two inherited groups both end at the HTTP/3 API, and a candidate can
/// satisfy both while producing short-header packets the peer cannot open:
/// this step rewrites the lost-frame handler, which decides what goes into a
/// retransmitted packet and therefore runs inside the same send path that
/// seals every 1-RTT packet. A key schedule or key-phase bookkeeping
/// disturbed there breaks this group and nothing else in the file.
///
/// It is also the cheapest orthogonality this step can buy. Everything else
/// inherited is header coding, because header coding is what moved.
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
    let body = testkit::body(32, 0x1020_0014);
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
/// Inherited from step 01 and re-seeded from step 14, the most recent step to
/// carry it: 64 KiB requested over HTTP/3 from the candidate's own
/// `quiche-server` comes back byte for byte on the candidate's own
/// `quiche-client`'s stdout, as two real processes over UDP loopback.
/// `--http-version HTTP/3` is pinned rather than left to the apps' own
/// negotiation, so the inherited observation stays HTTP/3 and does not quietly
/// become a different group.
///
/// Carried for two reasons. It is the only inherited group here that
/// exercises the whole stack as the user gets it, across two processes and a
/// real socket -- `gate-endtoend` does the same over HTTP/0.9, so this one is
/// what says the HTTP/3 path in particular still works. And it is the only
/// group at this step that links the new crypto backend the way a user does:
/// through the workspace's own `apps` member, built from the candidate's
/// `Cargo.toml` with whatever features that manifest resolves, rather than
/// through the harness's path dependency on `quiche`. A candidate that gets
/// the feature flip right in `quiche` and wrong in `apps` builds two binaries
/// that cannot complete a handshake, and this is where that is seen.
///
/// Note what it does NOT assert: nothing about SETTINGS. The exchange
/// underneath carries this step's new default `SETTINGS_MAX_FIELD_SECTION_SIZE`,
/// and asserting on the frame here would gate the step on its own subject.
/// `cap-h3-default-field-section` scores that; this group only requires the
/// bytes to arrive.
#[test]
fn reg_01_h3_get() {
    const LEN: usize = 64 * 1024;

    let dir = testkit::scratch("reg-01-h3-get");
    let want = testkit::body(LEN, 0x0333_0015);
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
