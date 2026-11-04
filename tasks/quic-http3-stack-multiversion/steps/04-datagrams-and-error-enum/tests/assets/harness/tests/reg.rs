//! inherited-milestone groups: earlier steps' decisive observations, re-run
//!
//! One `#[test]` per scored group, named exactly as the group id with dashes
//! turned into underscores. These are not "three more tests": each one is an
//! earlier step's `cap-*` observation, unchanged in intent, chosen because
//! something this step does could plausibly break it.
//!
//! Step 04 rewrites the error type, wraps connection IDs in a new type and
//! adds a second delivery path beside the stream layer. So the two groups
//! here are step 01's HTTP/3 request (does the stack still serve a request
//! end to end) and step 01's flow control (does the `FlowControl` path survive
//! the enum's change of identity, and does the DATAGRAM queue interfere with
//! stream credit).
//!
//! Step 01's idle timeout was carried here until the gate was re-tiered. It
//! asserted across wall-clock deadlines -- a 350 ms sleep past a 300 ms
//! `set_max_idle_timeout`, and a connection kept busy at 40 ms intervals that
//! had to still be open 400 ms later -- so a scheduling stall on a loaded host
//! failed it for reasons that had nothing to do with the candidate. A gate case
//! that can fail at random fails every candidate at random, the Oracle
//! included. It is deleted, not downgraded; see NOTES.md.
//!
//! The HTTP/3 group is step 01's *observation* re-run, not step 01's *code*
//! re-run. At 0.7.0 the flag-taking `quiche-client`/`quiche-server` do not
//! exist -- they live in `tools/apps`, which is not built until the workspace
//! arrives at step 06 -- so the only HTTP/3 binaries are the examples, whose
//! entire configuration is compiled in. See REQUIREMENTS.md, "T1 binary CLI
//! eras".

use std::time::Duration;

#[allow(unused_imports)]
use verifier::{pair, support, testkit};

const SHORT: Duration = Duration::from_secs(10);
const LONG: Duration = Duration::from_secs(60);

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

fn first_diff(want: &[u8], got: &[u8]) -> String {
    match want.iter().zip(got.iter()).position(|(a, b)| a != b) {
        Some(off) => format!(
            "first difference at offset {off}: expected {}, observed {}",
            testkit::hex(&want[off..(off + 16).min(want.len())]),
            testkit::hex(&got[off..(off + 16).min(got.len())])
        ),
        None => format!(
            "no differing byte in the first {} bytes (expected {} bytes, observed {})",
            want.len().min(got.len()),
            want.len(),
            got.len()
        ),
    }
}

// ---------------------------------------------------------------------------
// reg-01-h3-get  (T1) -- step 01's cap-h3-get
// ---------------------------------------------------------------------------

/// `reg-01-h3-get`: a 64 KiB body still crosses the candidate's own HTTP/3
/// client and server, byte for byte.
///
/// Step 04 changes the type every fallible call in the stack returns and the
/// type every connection ID is carried in. Both are load-bearing for a real
/// request, and neither is exercised by a library-level group that never
/// starts a process.
#[test]
fn reg_01_h3_get() {
    const LEN: usize = 64 * 1024;

    // The example server's port is compiled in, so only one T1 group at a
    // time may hold it.
    let _lock = testkit::serial_t1();

    let dir = testkit::scratch("reg-01-h3-get");
    let want = testkit::body(LEN, 0x0333_0001);
    let cwd = testkit::example_server_cwd(&dir, &[("payload.bin", want.clone())]);

    let server_bin = testkit::h3_server_bin();
    let client_bin = testkit::h3_client_bin();

    // No arguments: this release's server reads examples/cert.crt,
    // examples/cert.key and examples/root out of its working directory.
    let mut sc = testkit::cmd(&server_bin);
    sc.current_dir(&cwd);
    let server =
        testkit::spawn_server(&mut sc, testkit::EXAMPLE_SERVER_PORT, &dir, "h3-server", SHORT);

    let url = format!("https://127.0.0.1:{}/payload.bin", testkit::EXAMPLE_SERVER_PORT);
    let mut cc = testkit::cmd(&client_bin);
    cc.arg(&url);
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
}

// ---------------------------------------------------------------------------
// reg-01-flow-control  (T3) -- step 01's cap-flow-control
// ---------------------------------------------------------------------------

/// `reg-01-flow-control`: a 64 KiB transfer through a 4096-byte stream window
/// still completes, still takes many rounds, and still arrives intact.
///
/// The interesting failure this guards is step 04's: `Error` is rewritten, and
/// `stream_capacity` reporting zero is how a sender learns to stop. An enum
/// whose `Done`/`FlowControl` discrimination shifted would either stall here
/// or let the whole transfer through in one shot.
#[test]
fn reg_01_flow_control() {
    let mut cc = pair::client_config(pair::ALPN_HQ);
    let mut sc = pair::server_config(pair::ALPN_HQ);
    cc.set_initial_max_stream_data_bidi_remote(4096);
    sc.set_initial_max_stream_data_bidi_remote(4096);
    let mut p = pair::Pair::with_configs(&mut cc, &mut sc);
    p.handshake();

    const TOTAL: usize = 64 * 1024;
    const WINDOW: usize = 4096;
    let data = testkit::body(TOTAL, 0xfc01);
    let stream_id = 0;

    // Fill the 4096-byte window
    let n = p.client.stream_send(stream_id, &data[..WINDOW], false).expect("first stream_send");
    assert_eq!(n, WINDOW, "filled the {WINDOW} window, sent {n}");
    p.settle(8);

    // Capacity should now be zero
    let cap = p.client.stream_capacity(stream_id).expect("stream_capacity after filling window");
    assert_eq!(cap, 0, "capacity is 0 after {WINDOW} bytes in flight, observed {cap}");

    // Attempting to send more returns Ok(0)
    let n = p
        .client
        .stream_send(stream_id, &data[WINDOW..], false)
        .expect("stream_send when capacity is 0");
    assert_eq!(n, 0, "stream_send when capacity=0 returns Ok(0), observed Ok({n})");

    // The server reads those WINDOW bytes
    let (got, fin) = pair::read_stream(&mut p.server, stream_id);
    assert_eq!(got.len(), WINDOW, "server read {WINDOW} bytes, got {}", got.len());
    assert!(!fin, "server did not see fin yet");
    assert_eq!(&got[..], &data[..WINDOW], "server read correct bytes");

    // Exchange to deliver MAX_STREAM_DATA
    p.settle(8);

    // Capacity should reopen
    let cap = p.client.stream_capacity(stream_id).expect("stream_capacity after MAX_STREAM_DATA");
    assert!(cap > 0, "capacity reopened after MAX_STREAM_DATA, observed {cap}");

    // The rest of the transfer proceeds one window at a time. Each round hands
    // the sender whatever the window currently allows, lets the datagrams
    // cross, and drains the receiver -- which is what makes the next
    // MAX_STREAM_DATA appear. A sender that ignores the limit would have
    // pushed the whole 64 KiB in the first round; a receiver that never
    // extends the limit would stall here and trip the round bound.
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

    // The window really was the limiter: a 64 KiB transfer through a 4096-byte
    // window cannot complete in one shot.
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
        "server read the bytes the client sent, but {}",
        first_diff(&data, &recv)
    );
}
