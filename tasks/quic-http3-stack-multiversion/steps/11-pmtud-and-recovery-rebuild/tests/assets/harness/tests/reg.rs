//! inherited groups: what earlier milestones could already do and this one must not lose
//!
//! One `#[test]` per scored group, named exactly as the group id with dashes
//! turned into underscores: `reg-01-stream-transfer` is
//! `fn reg_01_stream_transfer()`. Each one names the step it came from, and
//! the slug after the step number is a real `cap-` group of that step --
//! `carryover-check.sh` resolves it and fails if it does not exist.
//!
//! Observe behaviour. Never read the candidate's source, never grep it for an
//! identifier, and never assert on a private detail: every group sits on one
//! of the five observation surfaces -- T1 two-process loopback through the
//! candidate's own binaries, T2 the verifier speaking QUIC at the candidate,
//! T3 in-memory connection pairs through the public Rust API, T4 emitted
//! artefacts, T5 exact-answer negative corpora.
//!
//! This milestone takes `recovery/` apart -- the round-trip estimator into its
//! own file, every congestion controller a directory level down behind a new
//! module root -- and turns `crypto.rs` into `crypto/`. The two groups below
//! are the ones whose answers come out of the moved code.
//!
//! A third, step 05's `SendInfo.at` pacing descriptor, was carried here until
//! the gate was re-tiered. Everything it asserted was a statement about the
//! wall clock -- that `at` fell inside the window the group ran in, that it
//! never went backwards between two sends, that it was within a second of the
//! call either way. Those are true of the reference and not provably true of
//! the reference on a loaded host, and a gate case that can fail at random
//! fails every candidate at random. Deleted, not downgraded; see NOTES.md.

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

/// The congestion window of the connection's active path.
fn active_cwnd(conn: &quiche::Connection) -> usize {
    conn.path_stats()
        .find(|s| s.active)
        .unwrap_or_else(|| panic!("{} has no active path", conn.trace_id()))
        .cwnd
}

// ---------------------------------------------------------------------------
// groups
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// reg-01-stream-transfer  (T1)  <- step 01, cap-stream-transfer
// ---------------------------------------------------------------------------

/// `reg-01-stream-transfer`: half a megabyte still crosses the process
/// boundary intact.
///
/// The simplest thing in the package that runs every byte through the rebuilt
/// recovery path: loss detection, the round-trip estimator that is now its own
/// file, the pacer that is now a directory down, and the acknowledgement
/// bookkeeping that had to be re-plumbed to produce `Stats::acked_bytes`. If
/// the reorganisation dropped a wire, a half-megabyte transfer is where it
/// shows, and it shows as a stall rather than as an error.
///
/// 512 KiB is well past the initial congestion window and past the initial
/// stream flow-control limit, so the transfer cannot complete without the
/// window-reopening and loss-recovery machinery running for many rounds.
///
/// Step 01 scored this over the byte-stream protocol, so it is pinned here
/// with `--http-version HTTP/0.9` rather than left to the app's default
/// negotiation: the inherited observation is the transport's, and routing it
/// through HTTP/3 would quietly turn it into a different group.
#[test]
fn reg_01_stream_transfer() {
    let _lock = testkit::serial_t1();

    const LEN: usize = 512 * 1024;
    let dir = testkit::scratch("reg-01-stream-transfer");
    let want = testkit::body(LEN, 0x1122_0011);
    let port = testkit::free_port();
    let server = start_app_server(&dir, port, &[("bulk.bin", want.clone())], "bulk-server");

    let url = format!("https://127.0.0.1:{port}/bulk.bin");
    let client_bin = testkit::client_bin();
    let mut cc = testkit::cmd(&client_bin);
    cc.arg("--no-verify").arg("--http-version").arg("HTTP/0.9").arg(&url);
    let run = testkit::run(&mut cc, LONG, &dir, "bulk-client");

    let log = server.log();
    assert!(
        !run.timed_out,
        "expected {} to fetch {LEN} bytes from {url} within {LONG:?}; observed: still running at \
         the deadline, killed after writing {} bytes.\n--- client stderr ---\n{}\n\
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
    assert!(
        want == *got,
        "expected the {LEN} byte body of {url} back byte for byte; observed {} bytes, {}.\n\
         --- client stderr ---\n{}\n--- server log ---\n{log}",
        got.len(),
        first_diff(&want, got),
        run.stderr
    );

    drop(server);
}
// ---------------------------------------------------------------------------
// reg-10-bbr2  (T3)  <- step 10, cap-bbr2
// ---------------------------------------------------------------------------

/// `reg-10-bbr2`: BBRv2 is still selectable by both routes and still moves
/// bytes.
///
/// Step 10 added it; this milestone takes every congestion controller apart
/// and reassembles it a directory down behind `recovery/congestion/`. Nothing
/// in the package endangers BBRv2 more than this step does, and nothing had
/// carried step 10 yet -- this is its first re-run.
///
/// Re-run unchanged from step 10: the name table with its rejections, the
/// `(0, 1, 2, 3)` discriminant tuple that crosses the C boundary, and a
/// 256 KiB transfer under each of the two selection routes.
#[test]
fn reg_10_bbr2() {
    use quiche::CongestionControlAlgorithm as CCA;
    use std::str::FromStr;

    // 1. The name table. `bbr2` is there; the match is exact -- no case
    //    folding, no trimming, no near-miss.
    assert_eq!(
        CCA::from_str("bbr2"),
        Ok(CCA::BBR2),
        "expected the string \"bbr2\" to name the BBRv2 congestion controller"
    );
    for (name, why) in [
        ("BBR2", "upper case: the table is matched exactly, not case-folded"),
        ("bbr2 ", "a trailing space: the name is not trimmed before matching"),
        ("bbr3", "a controller this release does not have"),
        ("", "the empty string"),
    ] {
        assert_eq!(
            CCA::from_str(name),
            Err(quiche::Error::CongestionControl),
            "expected {name:?} to be rejected with Error::CongestionControl ({why})"
        );
    }
    for (name, want) in [("reno", CCA::Reno), ("cubic", CCA::CUBIC), ("bbr", CCA::BBR)] {
        assert_eq!(
            CCA::from_str(name),
            Ok(want),
            "expected the three controllers step 05 established to keep their names"
        );
    }

    // 2. The discriminants. The enum is `#[repr(C)]` and the C API hands these
    //    numbers across the boundary, so a reorganisation of the Rust module
    //    tree must not renumber them.
    assert_eq!(
        (CCA::Reno as i32, CCA::CUBIC as i32, CCA::BBR as i32, CCA::BBR2 as i32),
        (0, 1, 2, 3),
        "expected the congestion-control discriminants to read (Reno, CUBIC, BBR, BBR2) = \
         (0, 1, 2, 3). These cross the C boundary as plain integers, so renumbering them while \
         moving the controllers into recovery/congestion/ silently changes what every C caller \
         selects."
    );

    // 3. Both selection paths reach it, and a transfer under it completes.
    for (how, by_name) in
        [("set_cc_algorithm_name(\"bbr2\")", true), ("set_cc_algorithm(BBR2)", false)]
    {
        let mut cc = pair::client_config(pair::ALPN_HQ);
        let mut sc = pair::server_config(pair::ALPN_HQ);
        if by_name {
            cc.set_cc_algorithm_name("bbr2")
                .unwrap_or_else(|e| panic!("{how} on a client Config: {e:?}"));
            sc.set_cc_algorithm_name("bbr2")
                .unwrap_or_else(|e| panic!("{how} on a server Config: {e:?}"));
        } else {
            cc.set_cc_algorithm(CCA::BBR2);
            sc.set_cc_algorithm(CCA::BBR2);
        }

        let mut p = pair::Pair::with_configs(&mut cc, &mut sc);
        p.handshake();

        // A positive window on a handshaken connection, and nothing about how it
        // moves afterwards. Step 10's own group compared the window before and
        // after the transfer; that comparison is not carried into the gate.
        // BBRv2 sizes its window from a bandwidth estimate and a minimum RTT
        // measured off the real clock, and an in-memory pair has no propagation
        // delay for either to be about -- so both are readings of how promptly
        // this host happened to schedule the test loop. Under enough load the
        // pair spends long enough between rounds to enter ProbeRTT, whose window
        // is four packets, below where it started. A gate case must not depend
        // on that; `cap-bbr2` at step 10 still makes the comparison, where a
        // miss costs its own weight and vetoes nothing.
        let initial_cwnd = active_cwnd(&p.client);
        assert!(
            initial_cwnd > 0,
            "expected a positive congestion window on a handshaken connection selected via \
             {how}; observed {initial_cwnd}"
        );

        // A congestion-controlled connection does not accept a quarter of a
        // megabyte in one call: `stream_send` truncates at the connection's
        // send capacity and returns `Err(Done)` when that capacity is zero.
        const LEN: usize = 256 * 1024;
        let want = testkit::body(LEN, 0x1122_0010);
        let mut sent = 0usize;
        let mut got: Vec<u8> = Vec::new();
        let mut fin = false;
        let mut rounds = 0usize;
        while sent < LEN || !fin {
            rounds += 1;
            assert!(
                rounds <= 4096,
                "the transfer under {how} stalled after {rounds} rounds with {sent} of {LEN} \
                 bytes accepted and {} delivered",
                got.len()
            );
            if sent < LEN {
                match p.client.stream_send(4, &want[sent..], false) {
                    Ok(n) => sent += n,
                    // Zero send capacity. Settling below collects the
                    // acknowledgements that reopen it.
                    Err(quiche::Error::Done) => {},
                    Err(e) => {
                        panic!("stream_send under {how} after {sent} of {LEN} bytes: {e:?}")
                    },
                }
                if sent == LEN {
                    // Written exactly once, and separately, because a
                    // truncated `stream_send` drops the `fin` it was given.
                    p.client.stream_send(4, &[], true).expect("mark the stream finished");
                }
            }
            p.settle(8);
            let (chunk, f) = pair::read_stream(&mut p.server, 4);
            got.extend_from_slice(&chunk);
            fin |= f;
        }
        assert!(
            got == want,
            "expected {LEN} bytes to arrive intact under the controller selected via {how}; \
             observed {} bytes, {}",
            got.len(),
            first_diff(&want, &got)
        );

        let stats = p.client.stats();
        assert!(
            stats.sent_bytes >= LEN as u64,
            "expected the client to have put at least the {LEN} payload bytes on the wire \
             under {how}; observed sent_bytes={}",
            stats.sent_bytes
        );
    }
}
