//! this step's new capability: the groups that must fail on the previous milestone
//!
//! One `#[test]` per scored group, named exactly as the `case` value the
//! generated `tests/test.sh` passes to `cargo test -- --exact`. The driver runs
//! each one on its own and asserts that exactly one test executed, so a
//! renamed, `#[ignore]`d or accidentally deleted function scores zero for that
//! group rather than passing silently.
//!
//! Observe behaviour. Never read the candidate's source, never grep it for an
//! identifier, and never assert on a private detail: every group sits on one
//! of the five observation surfaces -- T1 two-process loopback through the
//! candidate's own binaries, T2 the verifier speaking QUIC at the candidate,
//! T3 in-memory connection pairs through the public Rust API, T4 emitted
//! artefacts, T5 exact-answer negative corpora.
//!
//! # The 0.5.0 T1 surface
//!
//! At this milestone the four example binaries are *not* the flag-taking
//! `tools/apps` programs of the later releases, and `tools/apps` is not a
//! member of the root package, so `cargo build --examples` is all the driver
//! gets. `examples/server` and `examples/http3-server` take **no arguments**,
//! bind the fixed address `127.0.0.1:4433`, and resolve `examples/cert.crt`,
//! `examples/cert.key` and `examples/root` **relative to the working
//! directory**; `examples/client` and `examples/http3-client` take exactly one
//! argument, a URL, and accept no flags. Every T1 group here therefore builds
//! that directory layout in its own scratch dir and launches the server with
//! `Command::current_dir`, and serialises on [`port_guard`] because the port is
//! not the group's to choose.

use std::path::{Path, PathBuf};
use std::sync::{Mutex, MutexGuard};
use std::time::Duration;

#[allow(unused_imports)]
use verifier::{pair, support, testkit};

#[allow(dead_code)]
const SHORT: Duration = Duration::from_secs(10);
#[allow(dead_code)]
const LONG: Duration = Duration::from_secs(60);

/// The address the 0.5.0 example servers hard-code. Not a choice this harness
/// gets to make, so the groups that need it take turns.
const ORACLE_PORT: u16 = 4433;

static ORACLE_PORT_LOCK: Mutex<()> = Mutex::new(());

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

/// Serialise the groups that need `127.0.0.1:4433`, and wait for a previous
/// group's server to have actually let go of it.
///
/// The driver runs one case per process with `--exact`, so in a scored run this
/// is a no-op; it matters when the whole target is run at once during
/// authoring, where two `#[test]`s would otherwise race for the port and the
/// loser would report a candidate fault.
#[allow(dead_code)]
fn port_guard() -> MutexGuard<'static, ()> {
    let g = ORACLE_PORT_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    assert!(
        testkit::until(Duration::from_secs(10), || !testkit::is_bound(ORACLE_PORT)),
        "127.0.0.1:{ORACLE_PORT} was still bound when this group wanted it: something other \
         than this group's own server is holding the address the 0.5.0 examples hard-code"
    );
    g
}

/// Build the working directory the 0.5.0 example servers expect and return the
/// document root inside it.
///
/// `examples/cert.crt` and `examples/cert.key` are the verifier's fixture
/// keypair under the names the binaries open, and `examples/root/` holds
/// `files`. The candidate's own tree is never written to.
#[allow(dead_code)]
fn oracle_cwd(dir: &Path, files: &[(&str, Vec<u8>)]) -> PathBuf {
    let ex = dir.join("examples");
    std::fs::create_dir_all(&ex)
        .unwrap_or_else(|e| panic!("cannot create {}: {e}", ex.display()));
    let tls = testkit::tls();
    std::fs::copy(&tls.cert, ex.join("cert.crt")).unwrap_or_else(|e| {
        panic!("cannot install the fixture certificate at {}: {e}", ex.join("cert.crt").display())
    });
    std::fs::copy(&tls.key, ex.join("cert.key")).unwrap_or_else(|e| {
        panic!("cannot install the fixture key at {}: {e}", ex.join("cert.key").display())
    });
    testkit::docroot(&ex, files)
}

/// First 2n hex digits of `d`, plus its length. For assertion messages.
#[allow(dead_code)]
fn head(d: &[u8], n: usize) -> String {
    format!("{} ({} bytes total)", testkit::hex(&d[..n.min(d.len())]), d.len())
}

/// Where two byte strings first differ, rendered for an assertion message.
#[allow(dead_code)]
fn first_diff(want: &[u8], got: &[u8]) -> String {
    match want.iter().zip(got.iter()).position(|(a, b)| a != b) {
        Some(i) => format!(
            "first difference at offset {i}: expected {}, observed {}",
            testkit::hex(&want[i..(i + 16).min(want.len())]),
            testkit::hex(&got[i..(i + 16).min(got.len())])
        ),
        None => format!("no differing byte in the first {} bytes", want.len().min(got.len())),
    }
}

/// Open an Initial packet using Initial keys derived for `key_version`, while
/// leaving every other input -- the packet, its header, its associated data --
/// exactly as it arrived.
///
/// This is the only way to isolate the salt. Handing the same datagram to
/// [`support::decrypt_initial`] with a different version argument would fail
/// for a second reason (the version field is part of the AEAD's associated
/// data), so it could not distinguish "the salt changed" from "the header
/// changed". Here the sole difference between the two calls is which salt went
/// into HKDF-Extract.
#[allow(dead_code)]
fn open_with_salt_of(key_version: u32, datagram: &[u8]) -> Result<Vec<u8>, support::Error> {
    let hdr = support::LongHeader::parse(datagram)?;
    let keys = support::derive_initial_keys(key_version, hdr.dcid)?.client;

    let mut pkt = datagram[..hdr.packet_end].to_vec();
    let pn = support::remove_header_protection(&keys, &mut pkt, hdr.pn_offset)?;
    let packet_number = support::decode_packet_number(None, pn.truncated, pn.len);

    let header = pkt.get(..pn.end()).ok_or(support::Error::Truncated("packet number"))?.to_vec();
    let ciphertext =
        pkt.get(pn.end()..hdr.packet_end).ok_or(support::Error::Truncated("payload"))?.to_vec();

    keys.open(packet_number, &header, &ciphertext)
}

/// Drain everything a connection wants to send right now, without delivering
/// any of it.
#[allow(dead_code)]
fn drain(conn: &mut quiche::Connection) -> Vec<Vec<u8>> {
    let mut out = Vec::new();
    let mut buf = [0u8; pair::MAX_DATAGRAM];
    loop {
        match conn.send(&mut buf) {
            Ok(n) => out.push(buf[..n].to_vec()),
            Err(quiche::Error::Done) => return out,
            Err(e) => panic!("send on {}: {e:?}", conn.trace_id()),
        }
        assert!(out.len() <= 4096, "send did not terminate");
    }
}

/// Hand one datagram to a connection. `Done` is not an error.
#[allow(dead_code)]
fn deliver(conn: &mut quiche::Connection, datagram: &[u8]) {
    let mut d = datagram.to_vec();
    match conn.recv(&mut d) {
        Ok(_) | Err(quiche::Error::Done) => {},
        Err(e) => panic!("recv on {}: {e:?} ({} byte datagram)", conn.trace_id(), datagram.len()),
    }
}

/// Queue at least `want` more bytes on stream 4 so the sender is window
/// limited rather than application limited.
#[allow(dead_code)]
fn top_up(conn: &mut quiche::Connection, payload: &[u8], want: usize) {
    let mut queued = 0usize;
    for _ in 0..64 {
        if queued >= want {
            return;
        }
        match conn.stream_send(4, payload, false) {
            Ok(0) => return,
            Ok(n) => queued += n,
            Err(quiche::Error::Done) => return,
            Err(e) => panic!("client stream_send: {e:?}"),
        }
    }
}

// ---------------------------------------------------------------------------
// cap-draft29-salt  (T2)
// ---------------------------------------------------------------------------

/// `cap-draft29-salt`: the verifier opens the candidate client's first
/// datagram with draft-29's Initial salt, and shows that draft-27's does not
/// open it.
///
/// The candidate's library reads neither packet, so nothing it believes about
/// its own key schedule counts. The positive half proves the salt
/// `afbfec289993d24c9e9786f19c6111e04390a899` is the one that went into
/// HKDF-Extract; the negative half re-derives keys from
/// `c3eef712c72ebb5a11a7d2432bb46365bef9f502` -- the salt every version up to
/// and including draft-28 uses, and the one this milestone leaves behind --
/// over the same bytes, the same header and the same associated data, and
/// requires the AEAD to reject them. A candidate that kept a single constant
/// salt fails the second assertion; one that never moved to draft-29 fails the
/// version assertion before either.
#[test]
fn draft29_salt() {
    let dir = testkit::scratch("cap-draft29-salt");
    let ep = testkit::Endpoint::new();
    let addr = ep.local_addr();

    // The verifier's socket is not a QUIC server: it exists to catch the
    // client's first flight, and the client is killed as soon as it arrives.
    let client_bin = testkit::client_bin();
    let mut cc = testkit::cmd(&client_bin);
    cc.current_dir(&dir).arg(format!("https://127.0.0.1:{}/salt", addr.port()));
    let client = testkit::spawn_proc(&mut cc, &dir, "salt-client");

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
        support::VERSION_DRAFT29,
        "expected this milestone's client to put draft-29 (0xff00001d) on the wire; observed \
         {version:#010x}, datagram {}.\n--- client log ---\n{log}",
        head(&dgram, 48)
    );

    // Positive: draft-29's salt opens it.
    let initial =
        support::decrypt_initial(&dgram, support::VERSION_DRAFT29, support::Perspective::Client)
            .unwrap_or_else(|e| {
                panic!(
                    "expected the verifier's own draft-29 Initial keys -- salt \
                     afbfec289993d24c9e9786f19c6111e04390a899 over the packet's destination \
                     connection id, AES-ECB header protection, AEAD_AES_128_GCM -- to open the \
                     client's first Initial; observed {e}.\ndatagram {}\n--- client log ---\n{log}",
                    head(&dgram, 64)
                )
            });

    // Negative: the pre-draft-29 salt, and nothing else, changed.
    let with_old_salt = open_with_salt_of(support::VERSION_DRAFT27, &dgram);
    assert!(
        with_old_salt.is_err(),
        "expected the same Initial to stay shut when its keys are re-derived from the draft-23 \
         salt c3eef712c72ebb5a11a7d2432bb46365bef9f502, which is what draft-27 still uses: only \
         the salt differs between this call and the one that succeeded, so opening both ways \
         means the version-dependent salt was not implemented. Observed {} bytes of plaintext \
         instead.\n--- client log ---\n{log}",
        with_old_salt.map(|p| p.len()).unwrap_or(0)
    );

    // And the packet really was a client first flight, not something that
    // happened to decrypt.
    assert_eq!(
        initial.packet_number, 0,
        "expected packet number 0 in a client's first Initial; observed {}.\n--- client log \
         ---\n{log}",
        initial.packet_number
    );
    let crypto = initial
        .crypto_data()
        .unwrap_or_else(|e| panic!("CRYPTO reassembly failed: {e}\n--- client log ---\n{log}"));
    assert!(
        crypto.len() >= 44 && crypto[0] == 0x01,
        "expected the decrypted payload to carry a TLS ClientHello (handshake type 1) at crypto \
         offset 0; observed {}.\n--- client log ---\n{log}",
        head(&crypto, 48)
    );
}

// ---------------------------------------------------------------------------
// cap-drafts-removed  (T2)
// ---------------------------------------------------------------------------

/// `cap-drafts-removed`: drafts 23, 24 and 25 draw a Version Negotiation
/// packet from the candidate's server, and the list it offers is exactly
/// 27, 28, 29.
///
/// The verifier builds each Initial itself, so the candidate's library is not
/// involved in producing the stimulus. A server that still supports any of the
/// three answers with an Initial or a Retry instead of a VN and fails; a server
/// that negotiates but still advertises a removed version fails on the list.
#[test]
fn drafts_removed() {
    let _g = port_guard();
    let dir = testkit::scratch("cap-drafts-removed");
    oracle_cwd(&dir, &[("index.txt", b"drafts\n".to_vec())]);

    let server_bin = testkit::server_bin();
    let mut sc = testkit::cmd(&server_bin);
    sc.current_dir(&dir);
    let server = testkit::spawn_server(&mut sc, ORACLE_PORT, &dir, "drafts-server", SHORT);
    let to = testkit::loopback(ORACLE_PORT);

    // Newest first, which is the order this milestone's `negotiate_version`
    // puts them in. RFC 9000 Section 6.1 leaves the order to the server, so
    // the list is compared as a set below and as a sequence only to keep the
    // failure message readable.
    let want = [
        support::VERSION_DRAFT29,
        support::VERSION_DRAFT28,
        support::VERSION_DRAFT27,
    ];

    for (name, version) in [
        ("draft-23", support::VERSION_DRAFT23),
        ("draft-24", support::VERSION_DRAFT24),
        ("draft-25", support::VERSION_DRAFT25),
    ] {
        let ep = testkit::Endpoint::new();
        let dcid: Vec<u8> = (0..8u8).map(|i| 0x83 ^ i).collect();
        let scid: Vec<u8> = (0..8u8).map(|i| 0xc1 ^ i).collect();
        let crypto = vec![0x01u8; 300];

        let pkt = support::ClientInitial::new(version, &dcid, &scid, &crypto)
            .build()
            .unwrap_or_else(|e| panic!("the verifier could not build a {name} Initial: {e}"));
        ep.send_to(&pkt, to);

        let got = ep.recv_all(Duration::from_millis(900));
        let log = server.log();
        assert!(
            !got.is_empty(),
            "expected a Version Negotiation packet in reply to a {name} ({version:#010x}) \
             Initial; observed no datagram at all within 900ms.\n--- server log ---\n{log}"
        );

        for d in &got {
            let vn = support::VersionNegotiationPacket::parse(d).unwrap_or_else(|e| {
                panic!(
                    "expected every datagram the server sends in reply to a {name} \
                     ({version:#010x}) Initial to be a Version Negotiation packet -- a long \
                     header with a zero Version field -- and never a server Initial; observed \
                     {e} parsing {}.\n--- server log ---\n{log}",
                    head(d, 48)
                )
            });
            let mut offered = vn.versions.clone();
            offered.sort_unstable();
            let mut expected = want.to_vec();
            expected.sort_unstable();
            assert_eq!(
                offered, expected,
                "expected the Version Negotiation list for a {name} Initial to be exactly \
                 draft-27, draft-28 and draft-29 (0xff00001b, 0xff00001c, 0xff00001d) in some \
                 order, and in particular to have dropped {name} itself; observed {:?}.\n\
                 --- server log ---\n{log}",
                vn.versions.iter().map(|v| format!("{v:#010x}")).collect::<Vec<_>>()
            );
            assert_eq!(
                vn.dcid, scid,
                "expected the Version Negotiation packet to echo the client's Source Connection \
                 ID as its Destination Connection ID (RFC 9000 Section 6.1); observed {}.\n\
                 --- server log ---\n{log}",
                testkit::hex(vn.dcid)
            );
            assert_eq!(
                vn.scid, dcid,
                "expected the Version Negotiation packet to echo the client's Destination \
                 Connection ID as its Source Connection ID; observed {}.\n--- server log \
                 ---\n{log}",
                testkit::hex(vn.scid)
            );
        }
    }

    drop(server);
}

// ---------------------------------------------------------------------------
// cap-cubic-default  (T3)
// ---------------------------------------------------------------------------

/// Drive one in-memory connection into exactly one congestion event and report
/// `(cwnd before, cwnd after, datagrams in the lossy burst)`.
///
/// The recipe is deterministic because of the order this milestone's recovery
/// module runs things in: `on_ack_received` calls `detect_lost_packets` before
/// `on_packets_acked`, and `on_packet_acked` returns early while
/// `in_congestion_recovery(packet.time_sent)` holds. So a burst whose first
/// datagram is dropped and whose remaining datagrams are delivered produces one
/// multiplicative decrease and no increase at all: the ratio of the two
/// readings is the algorithm's beta and nothing else. Three is the packet
/// reordering threshold, so a burst of at least four proves the loss.
#[allow(dead_code)]
fn one_congestion_event(cc: Option<&str>) -> (usize, usize, usize) {
    let mut ccfg = pair::client_config(pair::ALPN_HQ);
    let mut scfg = pair::server_config(pair::ALPN_HQ);
    if let Some(name) = cc {
        ccfg.set_cc_algorithm_name(name)
            .unwrap_or_else(|e| panic!("Config::set_cc_algorithm_name({name:?}): {e:?}"));
        scfg.set_cc_algorithm_name(name)
            .unwrap_or_else(|e| panic!("Config::set_cc_algorithm_name({name:?}): {e:?}"));
    }

    let mut p = pair::Pair::with_configs(&mut ccfg, &mut scfg);
    p.handshake();
    p.settle(16);

    let before = p.client.stats().cwnd;

    let payload = vec![0x5au8; 64 * 1024];
    top_up(&mut p.client, &payload, before * 2 + 65536);

    let burst = drain(&mut p.client);
    assert!(
        burst.len() >= 5,
        "expected the client to fill its {before}-byte congestion window with at least five \
         datagrams before blocking; observed {}",
        burst.len()
    );

    // Drop the first, deliver the rest: the largest acknowledged packet number
    // then exceeds the dropped one by more than the packet reordering
    // threshold of 3, which is what declares it lost.
    for d in burst.iter().skip(1) {
        deliver(&mut p.server, d);
    }
    for d in drain(&mut p.server) {
        deliver(&mut p.client, &d);
    }

    (before, p.client.stats().cwnd, burst.len())
}

/// `cap-cubic-default`: a `Config` nobody told about congestion control
/// reduces its window by CUBIC's beta, 0.7, and an explicitly Reno one by
/// 0.5.
///
/// Both numbers are read out of the candidate's own `Stats::cwnd` across a
/// single, deliberately constructed loss event, so the measurement does not
/// depend on any private state. The default is compared against both named
/// algorithms rather than against a bare constant: "the default is CUBIC" is
/// the claim, and a candidate whose default is Reno fails it even if its CUBIC
/// is perfect.
#[test]
fn cubic_default() {
    let (d_before, d_after, d_n) = one_congestion_event(None);
    let (c_before, c_after, _) = one_congestion_event(Some("cubic"));
    let (r_before, r_after, _) = one_congestion_event(Some("reno"));

    println!(
        "cap-cubic-default: default {d_before} -> {d_after} ({d_n} datagrams), \
         cubic {c_before} -> {c_after}, reno {r_before} -> {r_after}"
    );

    // The minimum window is 2 * 1452 = 2904 bytes and clamps the decrease, so
    // a window that was never large enough would make every ratio meaningless.
    for (what, before) in [("default", d_before), ("cubic", c_before), ("reno", r_before)] {
        assert!(
            before >= 6000,
            "expected the {what} connection's congestion window to still be well above the \
             2904-byte minimum when the loss happened, so that the multiplicative decrease is \
             not clamped; observed {before}"
        );
    }

    let ratio = |a: usize, b: usize| a as f64 / b as f64;
    let d = ratio(d_after, d_before);
    let c = ratio(c_after, c_before);
    let r = ratio(r_after, r_before);

    assert!(
        (0.68..=0.72).contains(&d),
        "expected a Config::new connection with no set_cc_algorithm call to reduce its window by \
         CUBIC's beta_cubic = 0.7 after one congestion event (RFC 8312 Section 4.5); observed \
         {d_before} -> {d_after}, a factor of {d:.4}. Reno's 0.5 would be {:.0} bytes.",
        d_before as f64 * 0.5
    );
    assert!(
        (0.68..=0.72).contains(&c),
        "expected set_cc_algorithm_name(\"cubic\") to reduce its window by 0.7; observed \
         {c_before} -> {c_after}, a factor of {c:.4}"
    );
    assert!(
        (0.48..=0.52).contains(&r),
        "expected set_cc_algorithm_name(\"reno\") to reduce its window by the Reno loss \
         reduction factor of 0.5; observed {r_before} -> {r_after}, a factor of {r:.4}. If this \
         is 0.7 as well then the algorithm is not selectable, and if the default above was also \
         0.5 then the default is still Reno."
    );
    assert!(
        d > r,
        "expected the default algorithm to be the gentler of the two -- CUBIC's 0.7 against \
         Reno's 0.5 -- which is what makes CUBIC the default rather than Reno; observed default \
         {d:.4} against reno {r:.4}"
    );

    // A name that is neither recognised algorithm is rejected outright, on a
    // fresh Config that has never had a CC algorithm selected -- this is the
    // rejection path documented alongside "reno" and "cubic", not a detail of
    // either of them.
    let mut bogus = quiche::Config::new(quiche::PROTOCOL_VERSION).expect("Config::new");
    match bogus.set_cc_algorithm_name("not-a-real-algorithm") {
        Err(quiche::Error::CongestionControl) => {},
        other => panic!(
            "expected Config::set_cc_algorithm_name to reject a name that is neither \"reno\" \
             nor \"cubic\" with Error::CongestionControl; observed {other:?}"
        ),
    }
}

// ---------------------------------------------------------------------------
// cap-h3-priorities  (T1 + T2)
// ---------------------------------------------------------------------------

/// `cap-h3-priorities`: the HTTP/3 pair negotiates `h3-29` and completes a
/// GET.
///
/// Two observations, because either on its own is weak. The ALPN is read off
/// the wire: the verifier catches the candidate HTTP/3 client's first flight,
/// opens it with its own draft-29 Initial keys and looks for the protocol name
/// inside the ClientHello, so the claim is about bytes the candidate sent and
/// not about a constant it exports. The transfer is then run for real between
/// the candidate's own two binaries, which is the only way to see that the
/// response -- sent through this milestone's priority-carrying
/// `send_response_with_priority` path -- actually arrives.
#[test]
fn h3_priorities() {
    let h3_client_bin = testkit::h3_client_bin();
    let h3_server_bin = testkit::h3_server_bin();

    // --- the ALPN, on the wire ---------------------------------------------
    let dir = testkit::scratch("cap-h3-priorities-alpn");
    let ep = testkit::Endpoint::new();
    let addr = ep.local_addr();
    let mut cc = testkit::cmd(&h3_client_bin);
    cc.current_dir(&dir).arg(format!("https://127.0.0.1:{}/alpn", addr.port()));
    let probe = testkit::spawn_proc(&mut cc, &dir, "alpn-client");

    let dgram = ep.recv(SHORT);
    let log = probe.stop();
    let dgram = dgram.unwrap_or_else(|| {
        panic!(
            "expected {} to send a first flight to 127.0.0.1:{} within {SHORT:?}; observed no \
             datagram at all.\n--- client log ---\n{log}",
            h3_client_bin.display(),
            addr.port()
        )
    });

    let initial =
        support::decrypt_initial(&dgram, support::VERSION_DRAFT29, support::Perspective::Client)
            .unwrap_or_else(|e| {
                panic!(
                    "expected the HTTP/3 client's first Initial to be a draft-29 Initial the \
                     verifier can open; observed {e}, datagram {}.\n--- client log ---\n{log}",
                    head(&dgram, 64)
                )
            });
    let crypto = initial
        .crypto_data()
        .unwrap_or_else(|e| panic!("CRYPTO reassembly failed: {e}\n--- client log ---\n{log}"));

    let find = |needle: &[u8]| crypto.windows(needle.len()).position(|w| w == needle);
    let at29 = find(b"\x05h3-29");
    assert!(
        at29.is_some(),
        "expected the HTTP/3 client's ClientHello to offer the h3-29 ALPN, wire-encoded as the \
         length byte 0x05 followed by \"h3-29\" (draft-ietf-quic-http-29 Section 3.3); observed \
         no such byte string in {} bytes of crypto stream: {}.\n--- client log ---\n{log}",
        crypto.len(),
        head(&crypto, 96)
    );
    for older in [&b"\x05h3-28"[..], &b"\x05h3-27"[..], &b"\x05h3-25"[..], &b"\x05h3-23"[..]] {
        if let Some(at) = find(older) {
            assert!(
                at29.unwrap() < at,
                "expected h3-29 to be offered ahead of {:?} in the ALPN list, since draft-29 is \
                 this milestone's preferred version; observed h3-29 at offset {:?} and it at \
                 offset {at}.\n--- client log ---\n{log}",
                String::from_utf8_lossy(&older[1..]),
                at29
            );
        }
    }

    // --- the transfer, between the candidate's own binaries ----------------
    let _g = port_guard();
    let dir = testkit::scratch("cap-h3-priorities");
    let want = testkit::body(4096, 0x0329_0001);
    oracle_cwd(&dir, &[("priority.bin", want.clone())]);

    let mut sc = testkit::cmd(&h3_server_bin);
    sc.current_dir(&dir);
    let server = testkit::spawn_server(&mut sc, ORACLE_PORT, &dir, "h3-server", SHORT);

    let url = format!("https://127.0.0.1:{ORACLE_PORT}/priority.bin");
    let mut cc = testkit::cmd(&h3_client_bin);
    cc.current_dir(&dir).arg(&url);
    let run = testkit::run(&mut cc, SHORT, &dir, "h3-client");
    let log = server.log();

    assert!(
        !run.timed_out,
        "expected {} to fetch {url} and exit within {SHORT:?}; observed: still running at the \
         deadline, killed after {} stdout bytes.\n--- client stderr ---\n{}\n--- server log \
         ---\n{log}",
        h3_client_bin.display(),
        run.stdout_bytes.len(),
        run.stderr
    );
    assert_eq!(
        run.status.and_then(|s| s.code()),
        Some(0),
        "expected {} to exit 0 after fetching {url}; observed status {:?}.\n--- client stderr \
         ---\n{}\n--- server log ---\n{log}",
        h3_client_bin.display(),
        run.status,
        run.stderr
    );
    assert_eq!(
        run.stdout_bytes.len(),
        want.len(),
        "expected the {} byte body of {url} on the HTTP/3 client's stdout; observed {} bytes, \
         {}.\n--- client stderr ---\n{}\n--- server log ---\n{log}",
        want.len(),
        run.stdout_bytes.len(),
        first_diff(&want, &run.stdout_bytes),
        run.stderr
    );
    assert!(
        run.stdout_bytes == want,
        "expected the body of {url} back byte for byte over HTTP/3; observed {}.\n--- server \
         log ---\n{log}",
        first_diff(&want, &run.stdout_bytes)
    );

    drop(server);
}

// ---------------------------------------------------------------------------
// cap-retry-integrity-draft29  (T2)
// ---------------------------------------------------------------------------

/// `cap-retry-integrity-draft29`: the Retry the candidate's server sends in
/// answer to a draft-29 Initial authenticates under draft-29's integrity key,
/// and not under the one drafts 25 to 28 share.
///
/// The Retry is taken off the wire rather than produced by a library call, so
/// what is measured is a packet the candidate actually emitted. The verifier
/// then recomputes the tag over the identical Retry Pseudo-Packet with the
/// older key: the only thing that differs between that computation and the one
/// that verified is the 16-byte key, so a candidate that kept
/// `4d32ecdb2a2133c841e4043df27d4430` produces a tag that matches the old key
/// and fails here.
#[test]
fn retry_integrity_draft29() {
    let _g = port_guard();
    let dir = testkit::scratch("cap-retry-integrity-draft29");
    oracle_cwd(&dir, &[("index.txt", b"retry\n".to_vec())]);

    let server_bin = testkit::server_bin();
    let mut sc = testkit::cmd(&server_bin);
    sc.current_dir(&dir);
    let server = testkit::spawn_server(&mut sc, ORACLE_PORT, &dir, "retry-server", SHORT);
    let to = testkit::loopback(ORACLE_PORT);

    let ep = testkit::Endpoint::new();
    let odcid: Vec<u8> = vec![0x83, 0x94, 0xc8, 0xf0, 0x3e, 0x51, 0x57, 0x08];
    let scid: Vec<u8> = vec![0x29, 0x29, 0x29, 0x29, 0x29, 0x29, 0x29, 0x29];
    let crypto = vec![0x01u8; 300];

    // No token, so a server doing address validation must answer with a Retry.
    let pkt = support::ClientInitial::new(support::VERSION_DRAFT29, &odcid, &scid, &crypto)
        .build()
        .unwrap_or_else(|e| panic!("the verifier could not build a draft-29 Initial: {e}"));
    ep.send_to(&pkt, to);

    let (found, skipped) = ep.recv_matching(SHORT, |d| {
        support::RetryPacket::parse(d, support::VERSION_DRAFT29).is_ok()
    });
    let log = server.log();
    let retry = found.unwrap_or_else(|| {
        panic!(
            "expected a draft-29 Retry packet in answer to a token-less draft-29 Initial; \
             observed {} other datagrams and no Retry: {:?}.\n--- server log ---\n{log}",
            skipped.len(),
            skipped.iter().map(|d| head(d, 24)).collect::<Vec<_>>()
        )
    });

    let parsed = support::RetryPacket::parse(&retry, support::VERSION_DRAFT29)
        .unwrap_or_else(|e| panic!("Retry parse: {e}\n--- server log ---\n{log}"));
    assert_eq!(
        parsed.dcid, &scid[..],
        "expected the Retry to be addressed to the client's Source Connection ID; observed \
         {}.\n--- server log ---\n{log}",
        testkit::hex(parsed.dcid)
    );
    assert!(
        parsed.odcid.is_none(),
        "expected a draft-29 Retry to authenticate the Original Destination Connection ID \
         through the integrity tag rather than carrying it inline, the way drafts 23 and 24 \
         did.\n--- server log ---\n{log}"
    );
    assert!(
        !parsed.token.is_empty(),
        "expected the Retry to carry an address validation token.\n--- server log ---\n{log}"
    );
    let tag = parsed.integrity_tag.unwrap_or_else(|| {
        panic!("expected a 16-byte Retry Integrity Tag.\n--- server log ---\n{log}")
    });

    support::verify_retry_integrity(&retry, support::VERSION_DRAFT29, &odcid).unwrap_or_else(|e| {
        let (k, n) = support::retry_integrity_keys(support::VERSION_DRAFT29).unwrap();
        panic!(
            "expected the Retry Integrity Tag to verify under draft-29's key/nonce pair \
             {}/{} over the Retry Pseudo-Packet built from the original destination connection \
             id {}; observed {e}.\nretry {}\n--- server log ---\n{log}",
            testkit::hex(k),
            testkit::hex(n),
            testkit::hex(&odcid),
            head(&retry, 64)
        )
    });

    // The same pseudo-packet, the older key. Only the key differs.
    let old = support::retry_integrity_tag(
        support::VERSION_DRAFT25,
        &odcid,
        &retry[..retry.len() - tag.len()],
    )
    .unwrap_or_else(|e| panic!("the verifier could not compute the draft-25 tag: {e}"));
    let (ok, _) = support::retry_integrity_keys(support::VERSION_DRAFT25).unwrap();
    assert_ne!(
        old,
        tag,
        "expected the Retry Integrity Tag to change with the version: this tag matches the key \
         {} that drafts 25 through 28 share, so the candidate is still using the pre-draft-29 \
         key rather than ccce187ed09a09d05728155a6cb96be1.\nretry {}\n--- server log ---\n{log}",
        testkit::hex(ok),
        head(&retry, 64)
    );

    drop(server);
}

// ---------------------------------------------------------------------------
// cap-hystart-default  (T3)
// ---------------------------------------------------------------------------

/// Run `rounds` send-and-acknowledge rounds on an in-memory connection and
/// report the congestion window at the end of each.
///
/// `hystart` is `None` for "whatever `Config::new` decided", which is the
/// thing under test. Every round after the first waits before delivering the
/// burst, which is the only lever the harness has on the round trip time the
/// candidate measures: HyStart++ leaves slow start when a round's minimum RTT
/// rises by at least `clamp(last_round_min_rtt/8, 4ms, 16ms)` over the
/// previous round's, and from then on grows the window by roughly a quarter as
/// much per acknowledgement. Without it the connection stays in Reno slow
/// start and keeps doubling.
///
/// The congestion control algorithm is pinned to Reno so that the only thing
/// varying between the three runs is `enable_hystart`. It is not incidental:
/// CUBIC's congestion avoidance computes `W_est` from the elapsed time divided
/// by `min_rtt` (RFC 8312 Section 4.2), and on an in-memory pair `min_rtt` is
/// the handshake's, which is tens of microseconds. That makes the CUBIC window
/// a function of how fast the test host happens to be, and two runs of the
/// *same* configuration disagree by a factor of two. Reno's slow start,
/// congestion avoidance and Limited Slow Start are all pure functions of the
/// packet sizes and the window, so these numbers are reproducible.
#[allow(dead_code)]
fn slow_start_rounds(hystart: Option<bool>, rounds: usize, delay: Duration) -> Vec<usize> {
    let mut ccfg = pair::client_config(pair::ALPN_HQ);
    let mut scfg = pair::server_config(pair::ALPN_HQ);
    ccfg.set_cc_algorithm_name("reno").expect("set_cc_algorithm_name(\"reno\")");
    scfg.set_cc_algorithm_name("reno").expect("set_cc_algorithm_name(\"reno\")");
    if let Some(v) = hystart {
        ccfg.enable_hystart(v);
        scfg.enable_hystart(v);
    }

    let mut p = pair::Pair::with_configs(&mut ccfg, &mut scfg);
    p.handshake();
    p.settle(16);

    let payload = vec![0x3cu8; 64 * 1024];
    let mut cwnds = Vec::with_capacity(rounds);

    for round in 0..rounds {
        let cwnd = p.client.stats().cwnd;
        top_up(&mut p.client, &payload, cwnd * 2 + 65536);

        let burst = drain(&mut p.client);
        if burst.is_empty() {
            break;
        }

        // The first round measures the connection's floor; every later round
        // is deliberately slower than it.
        if round > 0 {
            std::thread::sleep(delay);
        }

        for d in &burst {
            deliver(&mut p.server, d);
        }
        // Read the stream so the server keeps extending flow control.
        let _ = pair::read_stream(&mut p.server, 4);
        for d in drain(&mut p.server) {
            deliver(&mut p.client, &d);
        }

        cwnds.push(p.client.stats().cwnd);
    }

    cwnds
}

/// `cap-hystart-default`: a `Config::new` connection behaves like one with
/// `enable_hystart(true)` and unlike one with `enable_hystart(false)`.
///
/// The observable is the congestion window across five slow-start rounds whose
/// round trip time the harness inflates after the first. A connection using
/// HyStart++ notices the delay increase, moves into Limited Slow Start and
/// stops doubling; one without it keeps doubling until it loses a packet,
/// which never happens here because every datagram is delivered. Comparing the
/// default against *both* explicit settings is what makes this a statement
/// about the default rather than about HyStart++.
#[test]
fn hystart_default() {
    const ROUNDS: usize = 5;
    let delay = Duration::from_millis(80);
    // The threshold is clamp(last_round_min_rtt/8, 4ms, 16ms) and the first
    // round is as fast as the machine can go, so 80ms clears the 16ms ceiling
    // by 5x -- widened from an earlier 40ms for headroom against scheduler
    // jitter on a loaded host, since std::thread::sleep only guarantees *at
    // least* the requested duration and a busy host can overshoot the shorter
    // rounds enough to erode a narrower margin. The dependency on a real
    // wall-clock sleep itself is not removable at this milestone: quiche
    // 0.5.0 has no clock-injection point, so "the round trip time rose" can
    // only be produced by an RTT that actually rose.

    let deflt = slow_start_rounds(None, ROUNDS, delay);
    let on = slow_start_rounds(Some(true), ROUNDS, delay);
    let off = slow_start_rounds(Some(false), ROUNDS, delay);

    println!("cap-hystart-default: default={deflt:?} enabled={on:?} disabled={off:?}");

    for (what, v) in [("default", &deflt), ("enable_hystart(true)", &on), ("enable_hystart(false)", &off)] {
        assert_eq!(
            v.len(),
            ROUNDS,
            "expected {ROUNDS} slow-start rounds on the {what} connection; observed {} before \
             the sender stopped producing datagrams: {v:?}",
            v.len()
        );
    }

    let (d, t, f) = (deflt[ROUNDS - 1], on[ROUNDS - 1], off[ROUNDS - 1]);

    // The control: with HyStart++ off, Reno slow start doubles the window
    // every round and nothing here ever loses a packet, so five rounds is a
    // factor of sixteen.
    assert!(
        f as f64 > off[0] as f64 * 8.0,
        "expected an enable_hystart(false) connection to stay in Reno slow start and keep \
         doubling its window across the five rounds, since every datagram was delivered and \
         nothing was ever lost; observed {off:?}. If this connection did not double then the \
         harness, not the candidate, failed to keep it window limited."
    );

    assert!(
        f as f64 > d as f64 * 3.0,
        "expected a Config::new connection to have HyStart++ enabled, so that the round trip \
         time rising from its first-round floor to {delay:?} -- well past the \
         clamp(last_round_min_rtt/8, 4ms, 16ms) threshold -- takes it into Limited Slow Start \
         and leaves it far short of an enable_hystart(false) connection given exactly the same \
         treatment; observed default {d} against disabled {f}.\n\
         default={deflt:?}\nenabled={on:?}\ndisabled={off:?}"
    );
    assert!(
        f as f64 > t as f64 * 3.0,
        "expected enable_hystart(true) to leave slow start on the same RTT increase; observed \
         enabled {t} against disabled {f}.\nenabled={on:?}\ndisabled={off:?}"
    );
    assert!(
        d as f64 <= t as f64 * 1.25 && t as f64 <= d as f64 * 1.25,
        "expected a Config::new connection and an enable_hystart(true) connection to be within \
         a quarter of each other at every round, because the default is supposed to be `true`; \
         observed default {d} against enabled {t}.\ndefault={deflt:?}\nenabled={on:?}"
    );
}

// ---------------------------------------------------------------------------
// cap-stream-priority-api  (T3)
// ---------------------------------------------------------------------------

/// `cap-stream-priority-api`: `Connection::stream_priority` exists, accepts
/// every urgency the HTTP/3 extensible priority scheme defines, and does not
/// disturb the connection.
///
/// The call is made on a live HTTP/3 request stream on both ends, over the
/// full 0 to 7 urgency range and both values of `incremental`, and the request
/// is then completed: an implementation that accepted the call by ignoring it
/// and an implementation that wedged the stream are both visible here, whereas
/// a bare `is_ok()` on a fresh connection would not tell them apart.
#[test]
fn stream_priority_api() {
    let mut ccfg = pair::client_config(pair::ALPN_H3);
    let mut scfg = pair::server_config(pair::ALPN_H3);
    let mut h3 = pair::H3Pair::with_configs(&mut ccfg, &mut scfg);

    let req = vec![
        quiche::h3::Header::new(":method", "GET"),
        quiche::h3::Header::new(":scheme", "https"),
        quiche::h3::Header::new(":authority", "quic.test"),
        quiche::h3::Header::new(":path", "/priority"),
    ];
    let stream_id = h3
        .client
        .send_request(&mut h3.pair.client, &req, true)
        .expect("h3 send_request");

    let (sid, ev) = h3
        .poll_server(64)
        .unwrap_or_else(|| panic!("expected the server to see the request on stream {stream_id}"));
    assert_eq!(sid, stream_id, "request arrived on stream {sid}, not {stream_id}");
    assert!(
        matches!(ev, quiche::h3::Event::Headers { .. }),
        "expected a Headers event for the request; observed {ev:?}"
    );

    // Every urgency the scheme defines, on both ends, including the repeat of
    // a value already set -- which this milestone short-circuits.
    for urgency in 0..=7u8 {
        for incremental in [false, true] {
            h3.pair.server.stream_priority(stream_id, urgency, incremental).unwrap_or_else(|e| {
                panic!(
                    "expected Connection::stream_priority({stream_id}, {urgency}, \
                     {incremental}) on the server to succeed; observed {e:?}"
                )
            });
            h3.pair.client.stream_priority(stream_id, urgency, incremental).unwrap_or_else(|e| {
                panic!(
                    "expected Connection::stream_priority({stream_id}, {urgency}, \
                     {incremental}) on the client to succeed; observed {e:?}"
                )
            });
        }
    }

    // ... and the request still completes afterwards.
    let body = testkit::body(2048, 0x0329_0007);
    let resp = vec![
        quiche::h3::Header::new(":status", "200"),
        quiche::h3::Header::new("server", "verifier"),
        quiche::h3::Header::new("content-length", &body.len().to_string()),
    ];
    h3.server
        .send_response(&mut h3.pair.server, stream_id, &resp, false)
        .expect("h3 send_response");
    let n = h3
        .server
        .send_body(&mut h3.pair.server, stream_id, &body, true)
        .expect("h3 send_body");
    assert_eq!(n, body.len(), "server wrote {n} of {} body bytes", body.len());

    let (_, ev) = h3
        .poll_client(64)
        .unwrap_or_else(|| panic!("expected the client to see the response headers"));
    match ev {
        quiche::h3::Event::Headers { list, .. } => {
            let status = list
                .iter()
                .find(|h| h.name() == ":status")
                .map(|h| h.value().to_string())
                .unwrap_or_default();
            assert_eq!(status, "200", "expected a 200 response after the priority calls");
        },
        other => panic!("expected Headers after the priority calls; observed {other:?}"),
    }

    let mut got = Vec::new();
    let mut buf = [0u8; 65535];
    for _ in 0..64 {
        match h3.poll_client(64) {
            Some((s, quiche::h3::Event::Data)) => {
                while let Ok(n) = h3.client.recv_body(&mut h3.pair.client, s, &mut buf) {
                    if n == 0 {
                        break;
                    }
                    got.extend_from_slice(&buf[..n]);
                }
            },
            Some((_, quiche::h3::Event::Finished)) => break,
            Some((_, other)) => panic!("unexpected HTTP/3 event {other:?}"),
            None => break,
        }
        if got.len() >= body.len() {
            break;
        }
    }
    assert_eq!(
        got.len(),
        body.len(),
        "expected the whole {} byte response body after the priority calls; observed {} bytes, \
         {}",
        body.len(),
        got.len(),
        first_diff(&body, &got)
    );
    assert!(got == body, "response body differed: {}", first_diff(&body, &got));
}

// ---------------------------------------------------------------------------
// cap-qlog-wired  (T3)
// ---------------------------------------------------------------------------

/// A `Write` that appends into a shared buffer, standing in for the log file
/// `set_qlog` would otherwise be given.
#[allow(dead_code)]
struct SharedWriter(std::sync::Arc<std::sync::Mutex<Vec<u8>>>);

impl std::io::Write for SharedWriter {
    fn write(&mut self, buf: &[u8]) -> std::io::Result<usize> {
        self.0.lock().unwrap_or_else(|e| e.into_inner()).extend_from_slice(buf);
        Ok(buf.len())
    }

    fn flush(&mut self) -> std::io::Result<()> {
        Ok(())
    }
}

/// `cap-qlog-wired`: `Connection::set_qlog` is wired into the transport core,
/// not a stub that accepts a writer and never calls it.
///
/// The writer is a `Vec<u8>` behind a mutex so the harness stays in-process
/// and never touches a real filesystem path. Two points are checked, because
/// either alone is weak: the handshake alone must already have produced some
/// structured output (a `set_qlog` that only arms on the first application
/// write would fail here), and more must appear after a further round of
/// traffic (a `set_qlog` that dumps a single snapshot at connection creation
/// and never writes again would fail there). Nothing about the bytes'
/// internal structure -- which qlog encoding, which event names -- is
/// checked; that is a private detail this milestone does not fix.
#[test]
fn qlog_wired() {
    let mut ccfg = pair::client_config(pair::ALPN_HQ);
    let mut scfg = pair::server_config(pair::ALPN_HQ);
    let mut p = pair::Pair::with_configs(&mut ccfg, &mut scfg);

    let log = std::sync::Arc::new(std::sync::Mutex::new(Vec::<u8>::new()));
    p.client.set_qlog(
        Box::new(SharedWriter(log.clone())),
        "cap-qlog-wired".to_string(),
        "verifier T3 qlog check".to_string(),
    );

    p.handshake();
    p.settle(16);

    let after_handshake = log.lock().unwrap_or_else(|e| e.into_inner()).len();
    assert!(
        after_handshake > 0,
        "expected Connection::set_qlog, installed before the handshake started, to have \
         produced some structured output by the time the handshake completed; observed zero \
         bytes"
    );

    let payload = vec![0x71u8; 32 * 1024];
    top_up(&mut p.client, &payload, 64 * 1024);
    for d in drain(&mut p.client) {
        deliver(&mut p.server, &d);
    }
    for d in drain(&mut p.server) {
        deliver(&mut p.client, &d);
    }

    let after_transfer = log.lock().unwrap_or_else(|e| e.into_inner()).len();
    assert!(
        after_transfer > after_handshake,
        "expected the qlog output to keep growing as the connection sent and received more \
         packets after the handshake, rather than stop at a one-time dump taken when set_qlog \
         was called; observed {after_handshake} bytes after the handshake and {after_transfer} \
         after a further round of traffic"
    );
}
