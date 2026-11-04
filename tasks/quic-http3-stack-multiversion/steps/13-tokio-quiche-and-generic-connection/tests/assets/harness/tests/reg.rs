//! inherited groups: what earlier milestones could already do and this one must not lose
//!
//! One `#[test]` per gate group, named exactly as the group id with dashes
//! turned into underscores: `reg-10-bbr2` is `fn reg_10_bbr2()`. Each one
//! names the step it came from, and the slug after the step number is a real
//! `cap-` group of that step -- `carryover-check.sh` resolves it and fails if
//! it does not exist.
//!
//! These are gate cases. They earn nothing. Under `reward = gate x score` a
//! single failure here vetoes the whole step, which is exactly why every case
//! in this file has to be deterministic on a loaded host: no wall-clock
//! readings, no sampled process state, no window-size comparison that depends
//! on when the scheduler happened to run the test loop.
//!
//! Observe behaviour. Never read the candidate's source, never grep it for an
//! identifier, and never assert on a private detail: every group sits on one
//! of the five observation surfaces -- T1 two-process loopback through the
//! candidate's own binaries, T2 the verifier speaking QUIC at the candidate,
//! T3 in-memory connection pairs through the public Rust API, T4 emitted
//! artefacts, T5 exact-answer negative corpora.
//!
//! This milestone is the largest structural change since the workspace
//! conversion: the repository gains four async members, `Connection` becomes
//! generic over a buffer factory, and every send path in the crate is rewritten
//! to go through it. The three groups below are the inherited behaviours most
//! exposed by that. The address descriptors on the hot path (step 05) are
//! rewritten wholesale by the generic conversion; the congestion controllers
//! (step 10) gain a fifth discriminant beside them; and path probing and
//! migration (step 08) run the whole stack end to end through the candidate's
//! own binaries, which is the only way to notice that the crate still builds a
//! working client and server after the workspace grew.

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
// reg-05-recvinfo-from  (T3)  <- step 05, cap-recvinfo-from
// ---------------------------------------------------------------------------

/// `reg-05-recvinfo-from`: the receive descriptor still names the address the
/// datagram came from, and egress is still addressed to the peer it belongs to.
///
/// This is the step-5 hot-path signature re-check the plan requires of this
/// step. The group it re-runs was written when `RecvInfo` carried a single
/// `from`; step 08 added `to` and this milestone rewrites every function that
/// takes either of them, because `recv` and `send` are now methods on
/// `Connection<F>` rather than on a concrete type. The intent is unchanged:
/// the descriptor is a value that carries the addresses it was built with, and
/// the connection answers to the address its `recv` was told about rather than
/// to whatever it was configured with.
///
/// Deliberately clock-free. The pacing half of step 05's hot-path pair,
/// `cap-sendinfo-at`, asserted things about `SendInfo::at` against the wall
/// clock and was deleted rather than carried when the gate was re-tiered; see
/// NOTES.md. Nothing below reads a clock.
#[test]
fn reg_05_recvinfo_from() {
    let a = pair::client_addr();
    let b = pair::server_addr();

    // The descriptor is a value type that carries two addresses and nothing
    // else. Equality is over both of them.
    assert_eq!(
        quiche::RecvInfo { from: a, to: b },
        quiche::RecvInfo { from: a, to: b },
        "expected two RecvInfo naming the same pair of addresses to compare equal"
    );
    assert_ne!(
        quiche::RecvInfo { from: a, to: b },
        quiche::RecvInfo { from: b, to: b },
        "expected RecvInfo to carry the source address it was built with; two RecvInfo naming \
         {a} and {b} as `from` compared equal"
    );
    assert_ne!(
        quiche::RecvInfo { from: a, to: b },
        quiche::RecvInfo { from: a, to: a },
        "expected RecvInfo to carry the destination address it was built with; two RecvInfo \
         naming {b} and {a} as `to` compared equal"
    );

    let mut p = pair::Pair::new(pair::ALPN_HQ);
    p.handshake();

    let payload = testkit::body(4096, 0x1324_0005);
    let (got, fin) = p.client_to_server(4, &payload, true);
    assert_eq!(
        got.len(),
        payload.len(),
        "expected the server to have read all {} bytes the client sent on stream 4 through \
         recv(buf, RecvInfo); observed {} bytes",
        payload.len(),
        got.len()
    );
    assert!(
        got == payload,
        "the {} bytes the server read differ from the bytes sent: {}",
        got.len(),
        first_diff(&payload, &got)
    );
    assert!(fin, "expected the server to have seen stream 4 finish");

    assert!(
        !p.client_info.is_empty() && !p.server_info.is_empty(),
        "expected datagrams in both directions; observed {} from the client and {} from the \
         server",
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
// reg-10-bbr2  (T3)  <- step 10, cap-bbr2
// ---------------------------------------------------------------------------

/// `reg-10-bbr2`: BBRv2 is still selectable by both routes, still numbered 3,
/// and still moves bytes.
///
/// This milestone adds a fifth controller -- `Bbr2Gcongestion`, the same
/// algorithm on the new recovery implementation -- and moves the enum out of
/// `recovery.rs` into `recovery/mod.rs`. Adding a variant beside the existing
/// four is exactly the change that renumbers them by accident, and the
/// discriminants cross the C boundary as plain integers, so a C caller that
/// asked for BBRv2 would silently get something else.
///
/// `cap-gcongestion-bbr2` scores the new variant. This group asserts nothing
/// about it: it re-runs step 10's observation unchanged, so that a candidate
/// who adds the fifth controller correctly but disturbs the four below it
/// fails here rather than passing on a technicality.
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
    //    numbers across the boundary, so adding a fifth controller must not
    //    renumber the four that were already there.
    assert_eq!(
        (CCA::Reno as i32, CCA::CUBIC as i32, CCA::BBR as i32, CCA::BBR2 as i32),
        (0, 1, 2, 3),
        "expected the congestion-control discriminants to read (Reno, CUBIC, BBR, BBR2) = \
         (0, 1, 2, 3). These cross the C boundary as plain integers, so renumbering them while \
         adding a fifth controller silently changes what every C caller selects."
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

        // A positive window on a handshaken connection, and nothing about how
        // it moves afterwards. Step 10's own group compared the window before
        // and after the transfer; that comparison is not carried into the
        // gate. BBRv2 sizes its window from a bandwidth estimate and a minimum
        // RTT measured off the real clock, and an in-memory pair has no
        // propagation delay for either to be about -- so both are readings of
        // how promptly this host happened to schedule the test loop. Under
        // enough load the pair spends long enough between rounds to enter
        // ProbeRTT, whose window is four packets, below where it started. A
        // gate case must not depend on that; `cap-bbr2` at step 10 still makes
        // the comparison, where a miss costs its own weight and vetoes
        // nothing.
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
        let want = testkit::body(LEN, 0x1324_0010);
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

// ---------------------------------------------------------------------------
// reg-08-probe-and-migrate  (T1)  <- step 08, cap-probe-and-migrate
// ---------------------------------------------------------------------------

/// `reg-08-probe-and-migrate`: the candidate's client binary still accepts
/// `--perform-migration`, and a 512 KiB transfer still arrives byte-for-byte
/// while it moves.
///
/// The end-to-end obligation of this step's gate, and the only case in the
/// package that puts the *whole* new workspace on the critical path: `apps`
/// now sits in a nine-member workspace beside four async crates, and a
/// candidate who converts the manifest carelessly -- a feature unified into the
/// wrong place, a `[workspace.dependencies]` entry that resolves `quiche`
/// differently for `apps` than for `tokio-quiche` -- produces binaries that
/// build and then do not talk to each other. Migration is the inherited
/// behaviour most sensitive to that, because it needs connection-ID issuance,
/// path validation and loss recovery all working at once.
///
/// No socket observation. Counting the client's bound UDP ports needs a
/// sampler -- `run_watched` polls `/proc/<pid>/fd` roughly every 20 ms -- and
/// whether the poll lands while both sockets are open is a question about the
/// host's scheduler, not about the candidate. That is fine for a scored group,
/// which is what `cap-probe-and-migrate` at step 08 still is, and not fine for
/// a gate case, which vetoes the whole step when it misses. What is left is
/// deterministic: the flag is accepted, the connection survives whatever the
/// client does with it, and the bytes arrive.
#[test]
fn reg_08_probe_and_migrate() {
    let _lock = testkit::serial_t1();

    const LEN: usize = 512 * 1024;
    let dir = testkit::scratch("reg-08-probe-and-migrate");
    let want = testkit::body(LEN, 0x1324_0008);
    let port = testkit::free_port();
    let server = start_app_server(&dir, port, &[("migrate.bin", want.clone())], "migrate-server");

    let url = format!("https://127.0.0.1:{port}/migrate.bin");
    let client_bin = testkit::client_bin();
    let mut cc = testkit::cmd(&client_bin);
    cc.arg("--no-verify").arg("--perform-migration").arg(&url);
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
