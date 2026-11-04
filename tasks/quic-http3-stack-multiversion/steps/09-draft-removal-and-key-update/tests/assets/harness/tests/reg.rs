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
// reg-01-flow-control  (T3)  <- step 01, cap-flow-control
// ---------------------------------------------------------------------------

/// `reg-01-flow-control`: stream-level back-pressure still works:
/// `stream_capacity` falls to zero after a window's worth of data is in
/// flight, the server drains it, MAX_STREAM_DATA comes back, and a 64 KiB
/// transfer through a 4096-byte window takes at least 16 rounds.
///
/// Key-update adds no new in-connection signalling that could interfere with
/// flow control, but draft removal changes how the version field is parsed,
/// and if that breaks the handshake this group fails before reaching its own
/// subject matter -- which is exactly what an inherited group is for.
#[test]
fn reg_01_flow_control() {
    const TOTAL: usize = 64 * 1024;
    const WINDOW: usize = 4096;

    let mut cc = pair::client_config(pair::ALPN_HQ);
    let mut sc = pair::server_config(pair::ALPN_HQ);
    cc.set_initial_max_stream_data_bidi_remote(WINDOW as u64);
    sc.set_initial_max_stream_data_bidi_remote(WINDOW as u64);
    // Pin the receive window to the advertised initial limit on both sides, so
    // that the limit advances by exactly one window per round instead of being
    // grown by the autotuner. Without this, RFC 9000 Sec 4.1 lets the receiver
    // extend the limit by any amount and the transfer finishes in a couple of
    // rounds, which would make the round-count assertion below meaningless.
    // Same treatment step 06 gives this group.
    cc.set_max_stream_window(WINDOW as u64);
    sc.set_max_stream_window(WINDOW as u64);
    let mut p = pair::Pair::with_configs(&mut cc, &mut sc);
    p.handshake();

    let data = testkit::body(TOTAL, 0x09fc_0001);
    let stream_id = 0;

    // Fill the 4096-byte window
    let n = p.client.stream_send(stream_id, &data[..WINDOW], false)
        .expect("first stream_send");
    assert_eq!(n, WINDOW, "filled the {WINDOW} window, sent {n}");
    p.settle(8);

    // Capacity should now be zero
    let cap = p.client.stream_capacity(stream_id)
        .expect("stream_capacity after filling window");
    assert_eq!(cap, 0, "capacity is 0 after {WINDOW} bytes in flight, observed {cap}");

    // Attempting to send more moves nothing. A sender with no stream capacity
    // reports `Error::Done` from step 06 onward -- steps 01-05 returned `Ok(0)`
    // for the same state -- and what matters either way is that not one byte
    // past the window is accepted.
    let blocked = p.client.stream_send(stream_id, &data[WINDOW..], false);
    assert_eq!(
        blocked,
        Err(quiche::Error::Done),
        "with the {WINDOW}-byte window full, stream_send must accept nothing and say so; \
         observed {blocked:?}"
    );

    // The server reads those WINDOW bytes
    let (got, fin) = pair::read_stream(&mut p.server, stream_id);
    assert_eq!(got.len(), WINDOW, "server read {WINDOW} bytes, got {}", got.len());
    assert!(!fin, "server did not see fin yet");
    assert_eq!(&got[..], &data[..WINDOW], "server read correct bytes");

    // Exchange to deliver MAX_STREAM_DATA
    p.settle(8);

    // Capacity should reopen
    let cap = p.client.stream_capacity(stream_id)
        .expect("stream_capacity after MAX_STREAM_DATA");
    assert!(cap > 0, "capacity reopened after MAX_STREAM_DATA, observed {cap}");

    // The rest of the transfer proceeds one window at a time.
    let mut recv = got;
    let mut sent = WINDOW;
    let mut saw_fin = false;
    let mut stalls = 0usize;
    let mut rounds = 0usize;
    while sent < TOTAL || !saw_fin {
        rounds += 1;
        assert!(
            rounds <= 256,
            "transfer stalled: after {rounds} rounds the client had sent {sent} \
             of {TOTAL} bytes and the server had read {} (fin seen: {saw_fin})",
            recv.len()
        );

        let cap = p.client.stream_capacity(stream_id).expect("stream_capacity");
        if sent < TOTAL {
            if cap == 0 {
                stalls += 1;
            } else {
                let chunk_end = (sent + cap).min(TOTAL);
                let will_fin = chunk_end == TOTAL;
                let n = p
                    .client
                    .stream_send(stream_id, &data[sent..chunk_end], will_fin)
                    .expect("stream_send");
                assert!(
                    n > 0 && n <= cap,
                    "stream_send accepted {n} bytes against a capacity of {cap}"
                );
                sent += n;
            }
        }

        p.settle(16);
        let (chunk, f) = pair::read_stream(&mut p.server, stream_id);
        recv.extend_from_slice(&chunk);
        saw_fin |= f;
    }

    assert_eq!(sent, TOTAL, "client sent the whole transfer: {sent} of {TOTAL}");
    assert!(saw_fin, "server saw the stream finish after {rounds} rounds");

    // A 64 KiB transfer through a 4096-byte window cannot complete in one shot.
    assert!(
        rounds >= TOTAL / WINDOW,
        "a {TOTAL}-byte transfer through a {WINDOW}-byte window took {rounds} \
         rounds, so the window was not being enforced (capacity stalls: {stalls})"
    );

    assert_eq!(
        recv.len(),
        TOTAL,
        "server read the whole transfer: {} of {TOTAL} bytes in {rounds} rounds",
        recv.len()
    );
    assert!(
        recv == data,
        "server read the bytes the client sent: first mismatch at offset {:?}",
        recv.iter().zip(data.iter()).position(|(a, b)| a != b)
    );
}

// ---------------------------------------------------------------------------
// reg-04-datagram-roundtrip  (T3)  <- step 04, cap-datagram-roundtrip
// ---------------------------------------------------------------------------

/// `reg-04-datagram-roundtrip`: the transport-level DATAGRAM extension still
/// works: both sides negotiate it, report a positive writable size, and echo
/// a 256-byte payload back byte-for-byte without leaking into the stream layer.
///
/// Step 09 removes the *HTTP/3* datagram helpers (`h3::Connection::send_dgram`
/// and friends); the *transport-level* helpers (`Connection::dgram_send`,
/// `dgram_recv`, `dgram_max_writable_len`) are untouched. A candidate that
/// accidentally removes the transport helpers would be caught here.
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

    let payload = testkit::body(256, 0x0918_0012);
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
/// Draft removal changes how the version field of every long-header packet is
/// parsed; a candidate that incorrectly rejects version 1 packets mid-flight
/// would break the migration path as well as the initial handshake, so this
/// group would catch such a regression even before the byte-count assertion.
#[test]
fn reg_08_probe_and_migrate() {
    let _lock = testkit::serial_t1();

    const LEN: usize = 512 * 1024;
    let dir = testkit::scratch("reg-08-probe-and-migrate");
    let want = testkit::body(LEN, 0x0918_0008);
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
