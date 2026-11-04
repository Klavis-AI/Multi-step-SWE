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
//! This milestone takes the QPACK Huffman coder out of the transport crate
//! and puts it in `octets` behind a feature flag, and it rewrites the
//! lost-frame handler. The first change is invisible if it works and total if
//! it does not: every header field either endpoint writes goes through the
//! relocated coder. Two of the three groups below are therefore header-coding
//! observations, one in process and one across the two binaries, and the
//! third is the wire-level key schedule -- the machinery every short-header
//! packet carrying those headers is sealed with.
//!
//! **What is deliberately not here.** Step 11's `cap-pmtud` was carried at
//! step 12 and is left behind now: this step changes path MTU discovery on
//! purpose -- it adds `Config::set_pmtud_max_probes`, `Connection::pmtu` and
//! `Connection::revalidate_pmtu`, and it rewrites the lost-probe arm of the
//! retransmit handler. Behaviour a step deliberately changes is not
//! regression, and carrying it would gate the step on the candidate
//! reproducing the *old* probe schedule. For the same reason no group here
//! observes retransmission after loss: that is this step's own subject and it
//! is scored, not gated.

use std::path::Path;
use std::time::Duration;

use quiche::h3::NameValue;

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

/// Poll one side of an HTTP/3 pair until it yields an event or `rounds` polls
/// have come back `Done`.
fn poll_until<F>(mut poll: F, rounds: usize) -> quiche::h3::Result<(u64, quiche::h3::Event)>
where
    F: FnMut() -> quiche::h3::Result<(u64, quiche::h3::Event)>,
{
    let mut last = Err(quiche::h3::Error::Done);
    for _ in 0..rounds {
        last = poll();
        if !matches!(last, Err(quiche::h3::Error::Done)) {
            return last;
        }
    }
    last
}

// ---------------------------------------------------------------------------
// groups
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// reg-01-h3-static-table  (T3)  <- step 01, cap-h3-static-table
// ---------------------------------------------------------------------------

/// `reg-01-h3-static-table`: a request and a response still cross the QPACK
/// coder with every field intact, indexed and literal alike.
///
/// Inherited from step 01, and the single most direct check on what this
/// milestone moved. The four request pseudo-headers are all QPACK static
/// table entries; the response mixes one static entry (`:status: 200`) with
/// two literals, one of which (`x-test: cafe` with an e-acute) is not ASCII.
/// A literal is where the Huffman coder is reached: the encoder writes the
/// shorter of the raw and Huffman forms, and the decoder has to invert
/// whichever it finds. This step took that coder out of
/// `quiche/src/h3/qpack/huffman/` and put it in the `octets` crate behind the
/// `huffman_hpack` feature, so the failure this group exists to catch is a
/// candidate that relocates the code and forgets to turn the feature on for
/// the transport crate: the build succeeds, the static-table fields still
/// arrive, and every literal comes back wrong or not at all.
///
/// Field *order* is asserted as well as content. QPACK is allowed to choose
/// its representation per field but not to reorder a header section, and a
/// decoder that emits the static entries first and the literals afterwards
/// would satisfy a set comparison while breaking every HTTP/3 client.
#[test]
fn reg_01_h3_static_table() {
    let mut h = pair::H3Pair::new();

    let req = [
        quiche::h3::Header::new(b":method", b"GET"),
        quiche::h3::Header::new(b":scheme", b"https"),
        quiche::h3::Header::new(b":path", b"/"),
        quiche::h3::Header::new(b":authority", b"quic.test"),
    ];
    let stream_id = h
        .client
        .send_request(&mut h.t.client, &req, true)
        .expect("h3::Connection::send_request on the client");
    assert_eq!(
        stream_id & 0x3,
        0,
        "a request opens a client-initiated bidirectional stream, whose id is 0 mod 4; \
         observed {stream_id}"
    );
    h.settle();

    let (sid, ev) = poll_until(|| h.poll_server(), 16)
        .expect("the server's HTTP/3 connection never produced the request headers");
    assert_eq!(sid, stream_id, "the server saw the request on the wrong stream");
    let got_req = match ev {
        quiche::h3::Event::Headers { list, .. } => list,
        other => panic!("expected the server's first event to be Headers; observed {other:?}"),
    };
    let want_req: Vec<(Vec<u8>, Vec<u8>)> =
        req.iter().map(|h| (h.name().to_vec(), h.value().to_vec())).collect();
    let saw_req: Vec<(Vec<u8>, Vec<u8>)> =
        got_req.iter().map(|h| (h.name().to_vec(), h.value().to_vec())).collect();
    assert_eq!(
        saw_req,
        want_req,
        "expected the four static-table request fields back in order.\n\
         --- expected ---\n{}\n--- observed ---\n{}",
        show(&want_req),
        show(&saw_req)
    );

    // "cafe" with a U+00E9: two bytes of UTF-8 that no Huffman table entry
    // was built for as a pair, so the value exercises the encoder's choice
    // between the raw and the Huffman form rather than a canned answer.
    let resp = [
        quiche::h3::Header::new(b":status", b"200"),
        quiche::h3::Header::new(b"content-type", b"text/plain; charset=utf-8"),
        quiche::h3::Header::new(b"x-test", "caf\u{e9}".as_bytes()),
    ];
    h.server
        .send_response(&mut h.t.server, stream_id, &resp, true)
        .expect("h3::Connection::send_response on the server");
    h.settle();

    let (sid, ev) = poll_until(|| h.poll_client(), 16)
        .expect("the client's HTTP/3 connection never produced the response headers");
    assert_eq!(sid, stream_id, "the client saw the response on the wrong stream");
    let got_resp = match ev {
        quiche::h3::Event::Headers { list, .. } => list,
        other => panic!("expected the client's first event to be Headers; observed {other:?}"),
    };
    let want_resp: Vec<(Vec<u8>, Vec<u8>)> =
        resp.iter().map(|h| (h.name().to_vec(), h.value().to_vec())).collect();
    let saw_resp: Vec<(Vec<u8>, Vec<u8>)> =
        got_resp.iter().map(|h| (h.name().to_vec(), h.value().to_vec())).collect();
    assert_eq!(
        saw_resp,
        want_resp,
        "expected the response's one static-table field and two literals back in order, \
         the non-ASCII value byte for byte.\n--- expected ---\n{}\n--- observed ---\n{}",
        show(&want_resp),
        show(&saw_resp)
    );
}

/// A header list as `name: value` lines, for an assertion message.
fn show(list: &[(Vec<u8>, Vec<u8>)]) -> String {
    list.iter()
        .map(|(n, v)| {
            format!(
                "  {}: {}  [{}]",
                String::from_utf8_lossy(n),
                String::from_utf8_lossy(v),
                testkit::hex(v)
            )
        })
        .collect::<Vec<_>>()
        .join("\n")
}

// ---------------------------------------------------------------------------
// reg-09-key-update  (T2)  <- step 09, cap-key-update
// ---------------------------------------------------------------------------

/// `reg-09-key-update`: the verifier still initiates a key update and the
/// candidate still answers it correctly.
///
/// Re-seeded from step 12's copy and unchanged in what it observes. There is
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
/// Inherited from step 01 and re-seeded from step 12, the most recent step to
/// carry it: 64 KiB requested over HTTP/3 from the candidate's own
/// `quiche-server` comes back byte for byte on the candidate's own
/// `quiche-client`'s stdout, as two real processes over UDP loopback.
/// `--http-version HTTP/3` is pinned rather than left to the apps' own
/// negotiation, so the inherited observation stays HTTP/3 and does not quietly
/// become a different group.
///
/// Carried for two reasons. It is the only inherited group here that
/// exercises the whole stack as the user gets it, across two processes and a
/// real socket -- `gate-endtoend` does the same over HTTP/0.9, so this one
/// is what says the HTTP/3 path in particular still works. And the request
/// and response it moves carry literal header fields -- `:authority`, the
/// path, the server's `content-type` -- through the Huffman coder this step
/// relocated, in the built binaries rather than in the harness's own linkage
/// of the crate. `reg-01-h3-static-table` makes the same observation in
/// process; if the two disagree, the difference is the candidate's feature
/// configuration and not its coder.
#[test]
fn reg_01_h3_get() {
    const LEN: usize = 64 * 1024;

    let dir = testkit::scratch("reg-01-h3-get");
    let want = testkit::body(LEN, 0x0333_0014);
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
