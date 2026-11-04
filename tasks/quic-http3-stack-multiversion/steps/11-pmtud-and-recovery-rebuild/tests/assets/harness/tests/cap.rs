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
//! The one exception this step uses is `cap_recovery_and_crypto_layout`, which
//! asks whether a path exists and never opens it. That is T4 -- an emitted
//! artefact checked against a fixed expectation table -- and it is scored as
//! layout, never as behaviour.

use std::time::Duration;

#[allow(unused_imports)]
use verifier::{pair, support, testkit};

#[allow(dead_code)]
const SHORT: Duration = Duration::from_secs(10);
#[allow(dead_code)]
const LONG: Duration = Duration::from_secs(60);

/// The QUIC minimum every endpoint must support, and the size a connection
/// that has not discovered anything better is held to.
const QUIC_FLOOR: usize = 1200;

// ---------------------------------------------------------------------------
// groups
// ---------------------------------------------------------------------------

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

/// `cap-pmtud`: a server climbs off the 1200-byte floor by probing, a client
/// is held to it, and neither happens when discovery is switched off.
///
/// T3, not the planned T1. `--enable-pmtud` is read by `quiche-server` but
/// never declared in its usage string, so the reference binary rejects it;
/// even if it did not, the verifier owns neither socket in a T1 run and could
/// not see the datagram sizes at all. See `GROUP-PLAN.md`, "Amendments after
/// authoring".
///
/// Every leg is a differential between two connections that are identical
/// apart from `discover_pmtu`, so nothing here can be satisfied by a datagram
/// budget, a pacing quantum or a buffer size -- only by discovery.
///
/// 1. on  -- the server sends at least one padded probe of exactly `PROBE`
///    bytes, and every other datagram it sends stays at or under the floor:
///    discovery raises the estimate by probing, not by simply writing bigger
///    packets.
/// 2. off -- with everything else the same the server never exceeds the floor,
///    so leg 1's probe is discovery and not the handshake flight.
/// 3. both -- a client under `discover_pmtu(true)` is *clamped* to the floor
///    even while pushing a bulk transfer, because a probe is gated on having
///    sent HANDSHAKE_DONE and only a server sends that; the same client with
///    discovery off goes past the floor on the same transfer.
/// 4. on  -- the connection survives the search and still carries data.
#[test]
fn cap_pmtud() {
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
    let on_cl: Vec<usize> = on.client_out.iter().map(|d| d.len()).collect();
    let on_cmark = on.client_out.len();

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
    let off_cmark = off.client_out.len();

    assert!(
        off_sv.iter().all(|&n| n <= QUIC_FLOOR),
        "expected a server with discover_pmtu(false) to send nothing above the {QUIC_FLOOR}-byte \
         floor -- there is no probe to send; observed {} datagrams with sizes {:?}, under a \
         config asking for a {PROBE}-byte payload exactly as the discovery-on leg did.",
        off_sv.len(),
        histogram(&off_sv)
    );

    // --- leg 3: the client is clamped, not probing ---------------------------
    //
    // `should_send_pmtu_probe` is gated on the handshake being confirmed *and*
    // HANDSHAKE_DONE having been sent, and only a server sends that frame. So
    // a client with discovery on can never leave the floor -- and, because
    // discovery is what holds it there, it does not even get the 1452 bytes
    // its own config asked for. Switching discovery off removes the clamp.
    assert!(
        on_cl.iter().all(|&n| n <= QUIC_FLOOR),
        "expected a client under discover_pmtu(true) to stay at or under the {QUIC_FLOOR}-byte \
         floor while idle: it never sends HANDSHAKE_DONE, so it never qualifies to probe. \
         Observed sizes {:?}.",
        histogram(&on_cl)
    );

    let body = testkit::body(BODY, 0x1122_0001);
    let (got, fin) = on.bulk_client_to_server(STREAM_ID, &body);
    let (off_got, off_fin) = off.bulk_client_to_server(STREAM_ID, &body);
    let on_cl_data: Vec<usize> = on.client_out[on_cmark..].iter().map(|d| d.len()).collect();
    let off_cl_data: Vec<usize> = off.client_out[off_cmark..].iter().map(|d| d.len()).collect();

    assert!(
        on_cl_data.iter().all(|&n| n <= QUIC_FLOOR),
        "expected a client under discover_pmtu(true) to stay at or under the {QUIC_FLOOR}-byte \
         floor across a {BODY}-byte transfer as well: with discovery on, output is limited to \
         the path estimate, and a client's estimate never moves. Observed sizes {:?}.",
        histogram(&on_cl_data)
    );
    assert!(
        off_cl_data.iter().any(|&n| n > QUIC_FLOOR),
        "expected the same client with discover_pmtu(false) to exceed the {QUIC_FLOOR}-byte \
         floor on the same {BODY}-byte transfer -- nothing is clamping it there. Observed \
         sizes {:?}. Without this leg, leg 3's bound could be a datagram budget rather than \
         the discovery clamp.",
        histogram(&off_cl_data)
    );

    // --- leg 4: the search did not break the connection ---------------------
    for (what, got, fin) in
        [("discover_pmtu(true)", &got, fin), ("discover_pmtu(false)", &off_got, off_fin)]
    {
        assert_eq!(
            got.len(),
            body.len(),
            "expected the {} byte body to arrive under {what}; observed {} bytes. Raising the \
             path estimate and then black-holing the connection is not discovery.",
            body.len(),
            got.len()
        );
        assert_eq!(got, &body, "expected the body back byte for byte under {what}");
        assert!(fin, "expected the stream to be finished under {what}");
    }
    assert!(
        !on.client.is_closed() && !on.server.is_closed(),
        "expected both ends still open after the PMTU search; client closed={} server closed={}",
        on.client.is_closed(),
        on.server.is_closed()
    );
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

/// `cap-pn-encode-exact`: the packet-number length a sender writes follows the
/// unacknowledged range, not the packet number's magnitude.
///
/// T2/T3, not the planned T5: `decode_pkt_num` is byte-identical to step 10's,
/// so a decode case cannot discriminate, and `mod packet` is private, so the
/// observation has to be on the wire. T3 supplies the two endpoints, T2 the
/// crypto -- the verifier derives 1-RTT keys from the candidate's own keylog
/// and removes header protection itself. See `GROUP-PLAN.md`, "Amendments
/// after authoring".
///
/// Phase A keeps acknowledgements flowing and requires a one-byte packet
/// number throughout, past packet 255. Phase B freezes `largest_acked` and
/// requires the length to widen. A candidate that keeps the old magnitude rule
/// fails A; one that hardcodes a single byte fails B.
#[test]
fn cap_pn_encode_exact() {
    const PHASE_A_ROUNDS: usize = 300;
    const PHASE_B_ROUNDS: usize = 500;
    /// Fixed-size DATAGRAM frames: the frame encoding is constant, so nothing
    /// but the header varies round to round. Streams were tried first and
    /// rejected -- a stream-ID varint widens at stream 66 for reasons that
    /// have nothing to do with packet numbers.
    const DGRAM: [u8; 16] = [0x5a; 16];

    let mut cc = pair::client_config(pair::ALPN_HQ);
    let mut sc = pair::server_config(pair::ALPN_HQ);
    cc.log_keys();
    for c in [&mut cc, &mut sc] {
        c.enable_dgram(true, 4096, 4096);
        // So the congestion window is never the thing under test: phase B
        // sends 500 packets with nothing acknowledged.
        c.set_initial_congestion_window_packets(600);
    }
    let (mut p, keylog) = pair::Pair::with_keylog(&mut cc, &mut sc);
    p.handshake();
    p.settle(16);

    let secret = pair::keylog_secret(&keylog.contents(), "CLIENT_TRAFFIC_SECRET_0")
        .expect("client did not write CLIENT_TRAFFIC_SECRET_0 to its keylog");
    let keys = support::PacketKeys::from_secret(support::Suite::Aes128Gcm, &secret)
        .expect("deriving gen-0 1-RTT keys from CLIENT_TRAFFIC_SECRET_0");

    // Prove the keys before asserting anything with them: a key-schedule
    // disagreement is not a packet-number fault and must not read as one.
    let mark = p.client_out.len();
    p.client.dgram_send(&DGRAM).expect("dgram_send warmup");
    p.exchange();
    let mut largest: u64 = 0;
    let warm = p.client_out[mark..]
        .iter()
        .filter_map(|d| pair::open_1rtt(d, pair::SERVER_CID.len(), &keys, Some(largest)))
        .next();
    let warm = warm.unwrap_or_else(|| {
        panic!(
            "the verifier could not open any of the {} client datagrams with the keys derived \
             from CLIENT_TRAFFIC_SECRET_0 -- this is a key-schedule disagreement, not a \
             packet-number fault",
            p.client_out.len() - mark
        )
    });
    largest = warm.packet_number;

    // --- phase A: the peer is acknowledging --------------------------------
    let mut a_lens: Vec<usize> = Vec::new();
    let mut a_top = largest;
    for _ in 0..PHASE_A_ROUNDS {
        let mark = p.client_out.len();
        match p.client.dgram_send(&DGRAM) {
            Ok(()) => {},
            Err(quiche::Error::Done) => {},
            Err(e) => panic!("dgram_send in phase A: {e:?}"),
        }
        p.exchange();
        for d in &p.client_out[mark..] {
            if let Some(o) = pair::open_1rtt(d, pair::SERVER_CID.len(), &keys, Some(a_top)) {
                a_top = a_top.max(o.packet_number);
                a_lens.push(o.pn_len);
            }
        }
    }

    assert!(
        a_top > 255,
        "expected phase A to drive the client's packet number past 255 so the old \
         magnitude rule would have had to widen; observed a highest packet number of {a_top} \
         after {PHASE_A_ROUNDS} rounds. The group cannot discriminate below 255."
    );
    let a_wide = a_lens.iter().filter(|&&n| n != 1).count();
    assert_eq!(
        a_wide,
        0,
        "expected every one of the {} client 1-RTT packets in phase A to carry a one-byte \
         packet number: the peer is acknowledging, so the unacknowledged range stays small and \
         RFC 9000 §17.1 needs one byte however high the packet number climbs (it reached \
         {a_top} here). Observed {a_wide} packets with a wider one; lengths seen: {:?}. \
         Choosing the length from the packet number's magnitude -- one byte below 255, two \
         above -- is the rule this milestone replaced.",
        a_lens.len(),
        histogram(&a_lens)
    );

    // --- phase B: the peer has gone quiet ----------------------------------
    // The client's datagrams are collected and thrown away, so `largest_acked`
    // freezes while the packet number keeps climbing.
    let mut b_lens: Vec<usize> = Vec::new();
    let mut b_top = a_top;
    let mut blocked = 0usize;
    let mut buf = [0u8; pair::MAX_DATAGRAM];
    for _ in 0..PHASE_B_ROUNDS {
        match p.client.dgram_send(&DGRAM) {
            Ok(()) => {},
            Err(quiche::Error::Done) => {},
            Err(e) => panic!("dgram_send in phase B: {e:?}"),
        }
        let mut sent = 0usize;
        loop {
            match p.client.send(&mut buf) {
                Ok((len, _)) => {
                    sent += 1;
                    if let Some(o) =
                        pair::open_1rtt(&buf[..len], pair::SERVER_CID.len(), &keys, Some(b_top))
                    {
                        b_top = b_top.max(o.packet_number);
                        b_lens.push(o.pn_len);
                    }
                },
                Err(quiche::Error::Done) => break,
                Err(e) => panic!("send in phase B: {e:?}"),
            }
        }
        if sent == 0 {
            blocked += 1;
        }
    }

    assert_eq!(
        blocked, 0,
        "expected the client to produce a packet in every one of the {PHASE_B_ROUNDS} phase-B \
         rounds; observed {blocked} rounds where it produced none. A round that sent nothing \
         proves nothing about packet-number width, and the initial congestion window was set to \
         600 packets precisely so this could not happen."
    );
    let b_one = b_lens.iter().filter(|&&n| n == 1).count();
    let b_two = b_lens.iter().filter(|&&n| n == 2).count();
    assert!(
        b_two > 0,
        "expected the packet-number length to widen once the peer stopped acknowledging: after \
         {PHASE_B_ROUNDS} rounds with the server's output discarded, the unacknowledged range is \
         hundreds of packets and one byte can no longer represent it (RFC 9000 §17.1, §A.2). \
         Observed {} packets, lengths {:?}, none of them two bytes. A sender that always writes \
         one byte is as wrong as one that always writes two.",
        b_lens.len(),
        histogram(&b_lens)
    );
    assert!(
        b_one > 0,
        "expected phase B to start out still using one byte and widen as the unacknowledged \
         range grew; observed {} packets, lengths {:?}, none of them one byte. The width follows \
         the range -- it does not jump straight to the maximum.",
        b_lens.len(),
        histogram(&b_lens)
    );
}

/// The bytes a real client puts in its first flight, lifted off the wire from
/// a throwaway connection: `(dcid, scid, ClientHello)`.
///
/// The verifier needs a ClientHello the candidate's own server will accept,
/// and hand-writing one would mean reimplementing TLS. Borrowing one from a
/// `quiche::connect` that is then dropped costs nothing and keeps the
/// transport parameters -- `initial_source_connection_id` in particular --
/// consistent with the connection IDs the rebuilt packet carries.
fn borrowed_client_hello() -> (Vec<u8>, Vec<u8>, Vec<u8>) {
    let mut cc = pair::client_config(pair::ALPN_HQ);
    let ccid = quiche::ConnectionId::from_ref(&pair::CLIENT_CID);
    let mut c = quiche::connect(
        Some("quic.test"),
        &ccid,
        pair::client_addr(),
        pair::server_addr(),
        &mut cc,
    )
    .expect("quiche::connect for the throwaway client");

    let mut buf = [0u8; pair::MAX_DATAGRAM];
    let (len, _) = c.send(&mut buf).expect("the throwaway client's first flight");
    let d = support::decrypt_initial(
        &buf[..len],
        support::VERSION_V1,
        support::Perspective::Client,
    )
    .expect("decrypting the throwaway client's Initial");
    let crypto = d.crypto_data().expect("reassembling the throwaway client's CRYPTO frames");

    assert_eq!(
        crypto.first().copied(),
        Some(0x01),
        "expected the borrowed first flight to start with a TLS ClientHello (handshake type 1); \
         observed {:?}. The rest of the group is meaningless if this is not a ClientHello.",
        crypto.first()
    );
    (d.dcid.clone(), d.scid.clone(), crypto)
}

/// The verifier's certificate repeated `copies` times into a single chain
/// file, and the path to it.
///
/// The anti-amplification budget only discriminates when the server has more
/// to send than it is allowed to. With the frame's one small certificate the
/// whole first flight is under 2000 bytes, so every factor from 2 upwards
/// spends the same amount and the group could not tell 3 from 300. Repeating
/// the leaf as its own chain lengthens the flight until the low factors are
/// exact multiples of what the server received. Nothing validates the chain:
/// this group never runs a client at all, it replays a recorded Initial.
fn padded_chain(copies: usize) -> std::path::PathBuf {
    let leaf = std::fs::read_to_string(testkit::tls().cert)
        .expect("reading the verifier certificate");
    let path = testkit::scratch("amplification-chain").join("chain.pem");
    std::fs::write(&path, leaf.repeat(copies)).expect("writing the padded chain");
    path
}

/// Everything a fresh in-process server emits in response to one client
/// Initial, with `factor` applied to its config, or left at the default when
/// `factor` is `None`. Every leg loads the same `chain`, so the flight the
/// budget is measured against is the same length every time.
fn server_first_flight(
    factor: Option<usize>, initial: &[u8], chain: &std::path::Path,
) -> Vec<Vec<u8>> {
    let mut sc = pair::server_config(pair::ALPN_HQ);
    sc.load_cert_chain_from_pem_file(chain.to_str().expect("utf-8 chain path"))
        .expect("load_cert_chain_from_pem_file on the padded chain");
    if let Some(f) = factor {
        sc.set_max_amplification_factor(f);
    }
    let scid = quiche::ConnectionId::from_ref(&pair::SERVER_CID);
    let mut s = quiche::accept(&scid, None, pair::server_addr(), pair::client_addr(), &mut sc)
        .expect("quiche::accept");

    let mut d = initial.to_vec();
    s.recv(&mut d, quiche::RecvInfo { from: pair::client_addr(), to: pair::server_addr() })
        .expect("the server rejected the rebuilt client Initial");

    let mut out = Vec::new();
    let mut buf = [0u8; pair::MAX_DATAGRAM];
    loop {
        match s.send(&mut buf) {
            Ok((len, _)) => out.push(buf[..len].to_vec()),
            Err(quiche::Error::Done) => break,
            Err(e) => panic!("server send while draining the first flight: {e:?}"),
        }
    }
    out
}

/// `cap-anti-amplification-factor`: the 3x ceiling on what a server may send
/// to an unvalidated address became a number the application picks.
///
/// T2. The verifier owns the client side entirely -- it builds the Initial
/// packet itself and never runs a `quiche::Connection` as the client -- so the
/// only thing under test is how many bytes the candidate's server hands back.
/// There is no CLI flag for the factor at this milestone, so the group cannot
/// be a T1 one.
///
/// The budget is `received_bytes * factor` (RFC 9000 Section 8), so one
/// 1200-byte Initial buys `1200 * factor`. A server with a certificate flight
/// longer than the budget spends it exactly, which is what upstream's own test
/// asserts; a calibration leg with a factor far above anything binding
/// measures the unconstrained flight so the expectation is `min` of the two.
#[test]
fn cap_anti_amplification_factor() {
    /// Exactly the RFC 9000 Section 14.1 minimum, so the arithmetic below is a
    /// multiple of 1200 and not of whatever a client happened to pad to.
    const INITIAL_LEN: usize = QUIC_FLOOR;
    /// Far above any factor that could bind on a certificate flight.
    const UNLIMITED: usize = 1000;
    const DEFAULT_FACTOR: usize = 3;
    /// Enough copies of the leaf that a factor of 5 still binds, so the legs
    /// below bracket the default of 3 from both sides.
    const CHAIN_COPIES: usize = 8;
    /// The largest factor the legs below expect to be exact.
    const MAX_BINDING: usize = 5;

    let (dcid, scid, crypto) = borrowed_client_hello();
    let mut ci = support::ClientInitial::new(support::VERSION_V1, &dcid, &scid, &crypto);
    ci.datagram_len = INITIAL_LEN;
    let initial = ci.build().expect("building the rebuilt client Initial");
    assert_eq!(
        initial.len(),
        INITIAL_LEN,
        "the verifier's own Initial is {} bytes, not {INITIAL_LEN}; every expectation below is \
         a multiple of what the server received",
        initial.len()
    );

    let chain = padded_chain(CHAIN_COPIES);

    // Calibration: what would the server send if nothing held it back?
    let free: usize = server_first_flight(Some(UNLIMITED), &initial, &chain)
        .iter()
        .map(|d| d.len())
        .sum();
    assert!(
        free > MAX_BINDING * INITIAL_LEN,
        "expected an unconstrained server first flight longer than {} bytes so that every \
         factor from 0 to {MAX_BINDING} binds and the default of {DEFAULT_FACTOR} is bracketed \
         on both sides; observed {free} bytes with set_max_amplification_factor({UNLIMITED}) \
         over a {CHAIN_COPIES}-certificate chain.",
        MAX_BINDING * INITIAL_LEN
    );

    let mut totals: Vec<(String, usize, usize)> = Vec::new();
    for (label, factor) in [
        ("default".to_string(), None),
        ("5".to_string(), Some(5)),
        ("4".to_string(), Some(4)),
        ("3".to_string(), Some(3)),
        ("2".to_string(), Some(2)),
        ("1".to_string(), Some(1)),
        ("0".to_string(), Some(0)),
    ] {
        let flight = server_first_flight(factor, &initial, &chain);
        let total: usize = flight.iter().map(|d| d.len()).sum();
        let want = std::cmp::min(free, factor.unwrap_or(DEFAULT_FACTOR) * INITIAL_LEN);
        totals.push((label, total, want));
    }

    for (label, got, want) in &totals {
        assert_eq!(
            got, want,
            "with set_max_amplification_factor({label}) the server sent {got} bytes in response \
             to one {INITIAL_LEN}-byte Initial; expected {want} -- the anti-amplification budget \
             is received_bytes * factor (RFC 9000 Section 8), and the unconstrained flight is \
             {free} bytes, so the server should spend the smaller of the two. All legs: {:?}",
            totals
        );
    }

    let default_total = totals[0].1;
    let explicit_three = totals[3].1;
    assert_eq!(
        default_total, explicit_three,
        "expected set_max_amplification_factor(3) to be indistinguishable from not calling it \
         at all -- 3 is the documented default; observed {default_total} bytes by default and \
         {explicit_three} with the call. All legs: {totals:?}"
    );

    // The one datagram the factor-1 server sent must be a real server Initial,
    // not a filler packet that happens to have the right length.
    let one = server_first_flight(Some(1), &initial, &chain);
    assert_eq!(
        one.len(),
        1,
        "expected a budget of {INITIAL_LEN} bytes to fit in exactly one datagram; observed {} \
         datagrams with sizes {:?}",
        one.len(),
        one.iter().map(|d| d.len()).collect::<Vec<_>>()
    );
    let dec = support::decrypt_initial_with_odcid(
        &one[0],
        support::VERSION_V1,
        support::Perspective::Server,
        &dcid,
    )
    .expect("the factor-1 server's only datagram did not decrypt as a server Initial");
    let server_crypto = dec.crypto_data().expect("reassembling the server's CRYPTO frames");
    assert_eq!(
        server_crypto.first().copied(),
        Some(0x02),
        "expected the amplification-limited server to spend its one datagram on a ServerHello \
         (TLS handshake type 2); observed a CRYPTO stream starting {:?}. A server that stays \
         under the limit by sending nothing useful has not implemented the limit, it has \
         implemented a stall.",
        server_crypto.first()
    );
}

/// `cap-recovery-and-crypto-layout`: `crypto.rs` became `crypto/`, and the
/// congestion controllers moved a level down into `recovery/congestion/`.
///
/// T4: the candidate's source tree is the emitted artefact, checked against a
/// fixed expectation table. Nothing here opens a file -- the group asks which
/// paths exist and which do not, and is scored as layout, never as behaviour.
/// The previous milestone had every "must not" path and no "must" path, so a
/// tree that did not move scores zero.
#[test]
fn cap_recovery_and_crypto_layout() {
    let src = testkit::quiche_crate_dir().join("src");
    assert!(
        src.is_dir(),
        "{} is not a directory; the transport crate's own `src/` tree is a precondition of \
         this group, which scores where the modules sit inside that tree and not where the \
         crate itself sits",
        src.display()
    );

    let must_exist: &[(&str, &str)] = &[
        ("crypto/mod.rs", "the crypto module root, which `crypto.rs` became"),
        ("crypto/boringssl.rs", "the BoringSSL backend, split out of the old single file"),
        ("recovery/mod.rs", "the recovery module root, which stays where it was"),
        ("recovery/rtt.rs", "round-trip estimation, split out of recovery/mod.rs"),
        ("recovery/congestion/mod.rs", "the congestion module root, one level below recovery"),
        ("recovery/congestion/reno.rs", "Reno, moved under congestion/"),
        ("recovery/congestion/cubic.rs", "CUBIC, moved under congestion/"),
        ("recovery/congestion/pacer.rs", "the pacer, moved under congestion/"),
        ("recovery/congestion/bbr/mod.rs", "BBR, moved under congestion/"),
        ("recovery/congestion/bbr2/mod.rs", "BBRv2, moved under congestion/"),
    ];
    let must_not_exist: &[(&str, &str)] = &[
        ("crypto.rs", "the single-file crypto module this milestone replaced with a directory"),
        ("recovery/reno.rs", "Reno's old home, one level above congestion/"),
        ("recovery/cubic.rs", "CUBIC's old home"),
        ("recovery/pacer.rs", "the pacer's old home"),
        ("recovery/bbr/mod.rs", "BBR's old home"),
        ("recovery/bbr2/mod.rs", "BBRv2's old home"),
    ];

    let mut missing = Vec::new();
    for (rel, why) in must_exist {
        if !src.join(rel).exists() {
            missing.push(format!("{} ({why})", src.join(rel).display()));
        }
    }
    assert!(
        missing.is_empty(),
        "the candidate's source tree is missing {} of the {} paths this milestone's module \
         layout requires:\n  {}\nThe reorganisation is the milestone: the crypto module became \
         a directory with one file per TLS backend, and the congestion controllers moved from \
         recovery/ into recovery/congestion/.",
        missing.len(),
        must_exist.len(),
        missing.join("\n  ")
    );

    let mut left = Vec::new();
    for (rel, why) in must_not_exist {
        if src.join(rel).exists() {
            left.push(format!("{} ({why})", src.join(rel).display()));
        }
    }
    assert!(
        left.is_empty(),
        "the candidate's source tree still has {} path(s) from the layout this milestone \
         replaced:\n  {}\nThese are moves, not copies. A tree that has both the old path and \
         the new one has two of each module, and the group cannot tell which one the build \
         used.",
        left.len(),
        left.join("\n  ")
    );
}

/// `cap-wire-error-codes`: the transport and HTTP/3 error codes an endpoint
/// puts in CONNECTION_CLOSE became public enums with the RFCs' values.
///
/// T3. Leg A pins every discriminant of both enums, which is what a candidate
/// that transcribed RFC 9000 Section 20.1 and RFC 9114 Section 8.1 will have;
/// leg B proves the enum is not decoration by making the candidate close a
/// connection and checking the number that actually came out.
#[test]
fn cap_wire_error_codes() {
    // --- leg A: the tables -------------------------------------------------
    let transport: &[(quiche::WireErrorCode, u64, &str)] = &[
        (quiche::WireErrorCode::NoError, 0x00, "NO_ERROR"),
        (quiche::WireErrorCode::InternalError, 0x01, "INTERNAL_ERROR"),
        (quiche::WireErrorCode::ConnectionRefused, 0x02, "CONNECTION_REFUSED"),
        (quiche::WireErrorCode::FlowControlError, 0x03, "FLOW_CONTROL_ERROR"),
        (quiche::WireErrorCode::StreamLimitError, 0x04, "STREAM_LIMIT_ERROR"),
        (quiche::WireErrorCode::StreamStateError, 0x05, "STREAM_STATE_ERROR"),
        (quiche::WireErrorCode::FinalSizeError, 0x06, "FINAL_SIZE_ERROR"),
        (quiche::WireErrorCode::FrameEncodingError, 0x07, "FRAME_ENCODING_ERROR"),
        (quiche::WireErrorCode::TransportParameterError, 0x08, "TRANSPORT_PARAMETER_ERROR"),
        (quiche::WireErrorCode::ConnectionIdLimitError, 0x09, "CONNECTION_ID_LIMIT_ERROR"),
        (quiche::WireErrorCode::ProtocolViolation, 0x0a, "PROTOCOL_VIOLATION"),
        (quiche::WireErrorCode::InvalidToken, 0x0b, "INVALID_TOKEN"),
        (quiche::WireErrorCode::ApplicationError, 0x0c, "APPLICATION_ERROR"),
        (quiche::WireErrorCode::CryptoBufferExceeded, 0x0d, "CRYPTO_BUFFER_EXCEEDED"),
        (quiche::WireErrorCode::KeyUpdateError, 0x0e, "KEY_UPDATE_ERROR"),
        (quiche::WireErrorCode::AeadLimitReached, 0x0f, "AEAD_LIMIT_REACHED"),
        (quiche::WireErrorCode::NoViablePath, 0x10, "NO_VIABLE_PATH"),
    ];
    for (variant, want, rfc_name) in transport {
        assert_eq!(
            *variant as u64, *want,
            "quiche::WireErrorCode::{variant:?} is {:#x}; RFC 9000 Section 20.1 assigns {rfc_name} \
             the value {want:#x}. These numbers go on the wire, so a peer reads whatever the \
             enum says.",
            *variant as u64
        );
    }

    let h3: &[(quiche::h3::WireErrorCode, u64, &str)] = &[
        (quiche::h3::WireErrorCode::NoError, 0x100, "H3_NO_ERROR"),
        (quiche::h3::WireErrorCode::GeneralProtocolError, 0x101, "H3_GENERAL_PROTOCOL_ERROR"),
        (quiche::h3::WireErrorCode::InternalError, 0x102, "H3_INTERNAL_ERROR"),
        (quiche::h3::WireErrorCode::StreamCreationError, 0x103, "H3_STREAM_CREATION_ERROR"),
        (quiche::h3::WireErrorCode::ClosedCriticalStream, 0x104, "H3_CLOSED_CRITICAL_STREAM"),
        (quiche::h3::WireErrorCode::FrameUnexpected, 0x105, "H3_FRAME_UNEXPECTED"),
        (quiche::h3::WireErrorCode::FrameError, 0x106, "H3_FRAME_ERROR"),
        (quiche::h3::WireErrorCode::ExcessiveLoad, 0x107, "H3_EXCESSIVE_LOAD"),
        (quiche::h3::WireErrorCode::IdError, 0x108, "H3_ID_ERROR"),
        (quiche::h3::WireErrorCode::SettingsError, 0x109, "H3_SETTINGS_ERROR"),
        (quiche::h3::WireErrorCode::MissingSettings, 0x10a, "H3_MISSING_SETTINGS"),
        (quiche::h3::WireErrorCode::RequestRejected, 0x10b, "H3_REQUEST_REJECTED"),
        (quiche::h3::WireErrorCode::RequestCancelled, 0x10c, "H3_REQUEST_CANCELLED"),
        (quiche::h3::WireErrorCode::RequestIncomplete, 0x10d, "H3_REQUEST_INCOMPLETE"),
        (quiche::h3::WireErrorCode::MessageError, 0x10e, "H3_MESSAGE_ERROR"),
        (quiche::h3::WireErrorCode::ConnectError, 0x10f, "H3_CONNECT_ERROR"),
        (quiche::h3::WireErrorCode::VersionFallback, 0x110, "H3_VERSION_FALLBACK"),
    ];
    for (variant, want, rfc_name) in h3 {
        assert_eq!(
            *variant as u64, *want,
            "quiche::h3::WireErrorCode::{variant:?} is {:#x}; RFC 9114 Section 8.1 assigns \
             {rfc_name} the value {want:#x}.",
            *variant as u64
        );
    }

    // The transport error enum gained a variant at this milestone, in last
    // position, and is not #[non_exhaustive].
    assert_ne!(
        quiche::Error::CryptoBufferExceeded,
        quiche::Error::KeyUpdate,
        "quiche::Error::CryptoBufferExceeded compares equal to quiche::Error::KeyUpdate; it is a \
         distinct variant, added at this milestone for the CRYPTO_BUFFER_EXCEEDED code"
    );
    assert_ne!(
        quiche::Error::CryptoBufferExceeded,
        quiche::Error::CryptoFail,
        "quiche::Error::CryptoBufferExceeded compares equal to quiche::Error::CryptoFail; a \
         connection that buffered more CRYPTO than it can hold has not failed to decrypt \
         anything"
    );

    // --- leg B: the number that actually goes on the wire -------------------
    // Only the client runs HTTP/3. The server is raw QUIC, so the verifier can
    // write whatever it likes onto a server-initiated unidirectional stream
    // and watch what the candidate's HTTP/3 layer does with it.
    const SERVER_CONTROL: u64 = 3;
    let mut t = pair::Pair::new(pair::ALPN_H3);
    t.handshake();
    let mut client = quiche::h3::Connection::with_transport(&mut t.client, &pair::h3_config())
        .expect("h3::Connection::with_transport on the client");
    t.settle(8);

    let mut msg = Vec::new();
    // Stream type 0x00: this is a control stream.
    pair::varint(0x00, &mut msg);
    // An empty SETTINGS frame initialises it, which RFC 9114 Section 6.2.1
    // requires before anything else.
    msg.extend_from_slice(&pair::h3_frame(0x04, &[]));
    // A DATA frame, which Section 7.2.1 forbids on a control stream.
    msg.extend_from_slice(&pair::h3_frame(0x00, &[0x41]));
    t.server
        .stream_send(SERVER_CONTROL, &msg, false)
        .expect("server writing its control stream");
    t.settle(8);

    let mut seen = Vec::new();
    let mut hit: Option<quiche::h3::Error> = None;
    for _ in 0..16 {
        match client.poll(&mut t.client) {
            Ok((sid, ev)) => seen.push(format!("stream {sid}: {ev:?}")),
            Err(quiche::h3::Error::Done) => {
                t.settle(2);
            },
            Err(e) => {
                hit = Some(e);
                break;
            },
        }
    }

    assert_eq!(
        hit,
        Some(quiche::h3::Error::FrameUnexpected),
        "expected the client's HTTP/3 connection to reject a DATA frame on the peer's control \
         stream with h3::Error::FrameUnexpected (RFC 9114 Section 7.2.1: the control stream \
         carries neither DATA nor HEADERS); observed {hit:?} after these events: {seen:?}"
    );

    let local = t.client.local_error().unwrap_or_else(|| {
        panic!(
            "the client's HTTP/3 layer returned FrameUnexpected but left the transport \
             connection open: local_error() is None. Rejecting a frame on the control stream is \
             a connection error, not a stream error, so the HTTP/3 layer has to close the \
             connection with the code."
        )
    });
    assert_eq!(
        (local.is_app, local.error_code),
        (true, quiche::h3::WireErrorCode::FrameUnexpected as u64),
        "expected the connection to be closed with the application error code {:#x} \
         (H3_FRAME_UNEXPECTED); observed is_app={} code={:#x} reason={:?}. The enum exists so \
         this number comes from one place.",
        quiche::h3::WireErrorCode::FrameUnexpected as u64,
        local.is_app,
        local.error_code,
        String::from_utf8_lossy(&local.reason)
    );
}

/// `cap-acked-bytes-stat`: `Stats` grew a running total of the bytes this
/// endpoint sent that the peer has acknowledged.
///
/// T3. The counter is the sender's own, so a receiver that only sends
/// acknowledgements accumulates almost nothing however much it reads -- which
/// is the check that separates a real counter from `bytes_sent` or
/// `bytes_recv` under a new name.
#[test]
fn cap_acked_bytes_stat() {
    const BODY: usize = 256 * 1024;
    const STREAM_ID: u64 = 4;

    let mut p = pair::Pair::new(pair::ALPN_HQ);
    assert_eq!(
        p.client.stats().acked_bytes,
        0,
        "expected acked_bytes to be 0 on a connection that has not yet sent a packet; observed \
         {}",
        p.client.stats().acked_bytes
    );
    assert_eq!(
        p.server.stats().acked_bytes,
        0,
        "expected acked_bytes to be 0 on a server that has not yet seen a packet; observed {}",
        p.server.stats().acked_bytes
    );

    p.handshake();
    p.settle(8);
    let after_handshake = p.client.stats().acked_bytes;
    assert!(
        after_handshake > 0,
        "expected the client's acked_bytes to be above 0 once the handshake completed: its \
         Initial and Handshake packets were acknowledged. Observed 0, with bytes_sent={}.",
        p.client.stats().sent_bytes
    );

    let body = testkit::body(BODY, 0x1122_0002);
    let (got, fin) = p.bulk_client_to_server(STREAM_ID, &body);
    assert_eq!(got.len(), body.len(), "the body did not arrive intact");
    assert!(fin, "the stream did not finish");
    p.settle(16);

    let after_body = p.client.stats().acked_bytes;
    assert!(
        after_body >= BODY as u64,
        "expected the client's acked_bytes to be at least the {BODY} bytes of body it sent and \
         saw acknowledged -- the counter is of packet sizes, so it should exceed the payload, \
         not fall short of it; observed {after_body} (it was {after_handshake} after the \
         handshake)."
    );
    assert!(
        after_body > after_handshake,
        "expected acked_bytes to be monotonically increasing across a {BODY}-byte transfer; \
         observed {after_handshake} after the handshake and {after_body} after the transfer"
    );

    let server_acked = p.server.stats().acked_bytes;
    assert!(
        server_acked < (BODY / 4) as u64,
        "expected the receiving side's acked_bytes to stay small -- it sent acknowledgements and \
         a handshake flight, not {BODY} bytes of body -- but observed {server_acked}. \
         acked_bytes counts what *this* endpoint sent and had acknowledged; a counter that \
         tracks the peer's traffic is recv_bytes wearing a new name. For reference the server's \
         sent_bytes is {} and its recv_bytes is {}.",
        p.server.stats().sent_bytes,
        p.server.stats().recv_bytes
    );
}

/// `cap-h3-additional-settings`: an application can put its own SETTINGS
/// identifiers in the HTTP/3 settings frame, and the peer reports them.
///
/// T3. `set_additional_settings` refuses the six identifiers quiche writes
/// itself and refuses duplicates, because either would produce a SETTINGS
/// frame with two entries for one identifier.
#[test]
fn cap_h3_additional_settings() {
    // The six quiche sets from its own `h3::Config`: QPACK_MAX_TABLE_CAPACITY,
    // MAX_FIELD_SECTION_SIZE, QPACK_BLOCKED_STREAMS, ENABLE_CONNECT_PROTOCOL,
    // H3_DATAGRAM and the draft-00 H3_DATAGRAM codepoint.
    const RESERVED: &[(u64, &str)] = &[
        (0x1, "SETTINGS_QPACK_MAX_TABLE_CAPACITY"),
        (0x6, "SETTINGS_MAX_FIELD_SECTION_SIZE"),
        (0x7, "SETTINGS_QPACK_BLOCKED_STREAMS"),
        (0x8, "SETTINGS_ENABLE_CONNECT_PROTOCOL"),
        (0x33, "SETTINGS_H3_DATAGRAM"),
        (0x276, "SETTINGS_H3_DATAGRAM_00"),
    ];

    for (id, name) in RESERVED {
        let mut c = pair::h3_config();
        assert_eq!(
            c.set_additional_settings(vec![(*id, 42)]),
            Err(quiche::h3::Error::SettingsError),
            "expected set_additional_settings to refuse {name} ({id:#x}) with SettingsError: \
             quiche writes that identifier itself from the h3::Config, so accepting it would \
             emit a SETTINGS frame with two entries for one identifier"
        );
    }

    let mut dup = pair::h3_config();
    assert_eq!(
        dup.set_additional_settings(vec![(0x4f4f, 1), (0x4f4f, 2)]),
        Err(quiche::h3::Error::SettingsError),
        "expected set_additional_settings to refuse a list that names one identifier twice"
    );

    let mut ok = pair::h3_config();
    assert_eq!(
        ok.set_additional_settings(vec![(0x4f4f, 0x1234), (0x9a9a, 0x5678)]),
        Ok(()),
        "expected two distinct identifiers that quiche does not use itself to be accepted"
    );

    // --- the identifiers reach the peer ------------------------------------
    let client_extra = vec![(0x4f4f_u64, 0x1234_u64), (0x9a9a, 0x5678)];
    let server_extra = vec![(0x7f7f_u64, 0x99_u64)];

    let mut ccfg = pair::h3_config();
    ccfg.set_additional_settings(client_extra.clone())
        .expect("set_additional_settings on the client config");
    let mut scfg = pair::h3_config();
    scfg.set_additional_settings(server_extra.clone())
        .expect("set_additional_settings on the server config");

    let mut h = pair::H3Pair::with_h3_configs(pair::Pair::new(pair::ALPN_H3), &ccfg, &scfg);
    h.settle();
    // `peer_settings_raw` is filled in when the peer's control stream is
    // parsed, and that parsing happens inside `poll`. Settling moves the
    // datagrams across; polling is what reads them.
    for _ in 0..16 {
        let quiet = matches!(h.poll_server(), Err(quiche::h3::Error::Done)) &
            matches!(h.poll_client(), Err(quiche::h3::Error::Done));
        if quiet {
            break;
        }
        h.settle();
    }

    check_settings(h.server.peer_settings_raw(), &client_extra, "server", "client");
    check_settings(h.client.peer_settings_raw(), &server_extra, "client", "server");
}

/// Assert that `raw` carries every pair of `want`, with its value, in the same
/// relative order.
///
/// Deliberately not an equality: `peer_settings_raw` reports the whole SETTINGS
/// frame, quiche's own identifiers included, and which of those a milestone
/// happens to send is not what this group is about.
fn check_settings(
    raw: Option<&[(u64, u64)]>, want: &[(u64, u64)], observer: &str, sender: &str,
) {
    let raw = raw.unwrap_or_else(|| {
        panic!(
            "the {observer}'s peer_settings_raw() is None after the handshake settled; the \
             {sender} sent a SETTINGS frame with {} additional identifiers in it",
            want.len()
        )
    });

    let mut positions = Vec::new();
    for (id, value) in want {
        let at = raw.iter().position(|(i, _)| i == id).unwrap_or_else(|| {
            panic!(
                "the {observer} did not report identifier {id:#x} among the {sender}'s raw \
                 settings; observed {raw:?}. An identifier quiche does not recognise is \
                 carried through rather than dropped."
            )
        });
        assert_eq!(
            raw[at].1, *value,
            "the {observer} reports identifier {id:#x} with value {:#x}; the {sender} sent \
             {value:#x}. Observed settings: {raw:?}",
            raw[at].1
        );
        positions.push(at);
    }

    assert!(
        positions.windows(2).all(|w| w[0] < w[1]),
        "the {observer} reports the {sender}'s additional identifiers out of order: they were \
         sent as {want:?} and appear at positions {positions:?} of {raw:?}. The list is written \
         to the wire in the order the application gave it."
    );
}
