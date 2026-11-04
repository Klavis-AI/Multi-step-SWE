//! inherited groups: what earlier milestones could already do and this one must not lose
//!
//! One `#[test]` per scored group, named exactly as the group id with dashes
//! turned into underscores: `reg-01-h3-get` is `fn reg_01_h3_get()`. Each one
//! names the step it came from, and the slug after the step number is a real
//! `cap-` group of that step -- `carryover-check.sh` resolves it and fails if
//! it does not exist.
//!
//! Observe behaviour. Never read the candidate's source, never grep it for an
//! identifier, and never assert on a private detail: every group sits on one
//! of the five observation surfaces -- T1 two-process loopback through the
//! candidate's own binaries, T2 the verifier speaking QUIC at the candidate,
//! T3 in-memory connection pairs through the public Rust API, T4 emitted
//! artefacts, T5 exact-answer negative corpora.

use std::time::Duration;

// `Header::name`/`Header::value` live on this trait at this milestone.
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

// ---------------------------------------------------------------------------
// groups
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// reg-01-h3-get  (T1)  <- step 01, cap-h3-get
// ---------------------------------------------------------------------------

/// `reg-01-h3-get`: HTTP/3 still moves a whole file between the candidate's
/// own two programs.
///
/// Step 01 established this with `examples/http3-server` and
/// `examples/http3-client`; steps 02 to 05 kept it with the no-argument
/// versions of those programs, which bound 127.0.0.1:4433 and read their
/// certificate out of the working directory. Neither of those programs is what
/// this milestone ships. The request path is now `quiche-server` and
/// `quiche-client` out of `apps/`, configured from the command line, and
/// `--http-version HTTP/3` pins the application protocol to `h3` so that this
/// stays the HTTP/3 claim step 01 made rather than quietly becoming an
/// HTTP/0.9 one.
///
/// The *observation* is unchanged -- 64 KiB requested over HTTP/3 comes back
/// byte for byte on the client's stdout, exit status 0 -- which is the point
/// of an inherited group: the interface moved, the capability did not. This is
/// the group that catches a workspace reshuffle that moved the code and broke
/// the request path.
#[test]
fn reg_01_h3_get() {
    const LEN: usize = 64 * 1024;

    let dir = testkit::scratch("reg-01-h3-get");
    let want = testkit::body(LEN, 0x0333_0001);
    let root = testkit::docroot(&dir, &[("payload.bin", want.clone())]);
    let tls = testkit::tls();
    let port = testkit::free_port();

    let server_bin = testkit::need_bin(&["quiche-server"]);
    let client_bin = testkit::need_bin(&["quiche-client"]);

    let mut sc = testkit::cmd(&server_bin);
    sc.arg("--listen")
        .arg(format!("127.0.0.1:{port}"))
        .arg("--cert")
        .arg(&tls.cert)
        .arg("--key")
        .arg(&tls.key)
        .arg("--root")
        .arg(&root)
        .arg("--http-version")
        .arg("HTTP/3");
    let server = testkit::spawn_server(&mut sc, port, &dir, "h3-server", LONG);

    let url = format!("https://127.0.0.1:{port}/payload.bin");
    let mut cc = testkit::cmd(&client_bin);
    cc.arg("--no-verify")
        .arg("--http-version")
        .arg("HTTP/3")
        .arg(&url);
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

// ---------------------------------------------------------------------------
// reg-01-flow-control  (T3)  <- step 01, cap-flow-control
// ---------------------------------------------------------------------------

/// `reg-01-flow-control`: stream flow control still throttles a sender.
///
/// A 64 KiB transfer through a 4096-byte stream window. The receiver's limit
/// has to be enforced at the sender (`stream_capacity` reaching 0, and
/// `stream_send` then accepting not one byte more), extended as the receiver
/// drains (MAX_STREAM_DATA), and the whole transfer has to arrive byte for
/// byte having taken at least one round per window.
///
/// How a blocked sender reports itself changed with this milestone:
/// `stream_send` returns `Err(Error::Done)` here where 0.9.0 returned `Ok(0)`.
/// This group gates, so it is asserted against the previous milestone's tree
/// as well as this one, and neither spelling is something step 01 claimed or
/// this milestone's instruction asks for. What step 01 claimed is that not one
/// byte past the window is accepted; the group takes either spelling of
/// "nothing was accepted" and nothing else.
///
/// This milestone adds an autotuner to the receiver's side of exactly that
/// mechanism, so the group pins the window with `set_max_stream_window(4096)`:
/// the claim being carried forward is step 01's claim -- the receiver extends
/// its limit by one window at a time and the sender respects it -- and not a
/// claim about the autotuner, which `cap-stream-window-autotune` owns. With
/// the window pinned the arithmetic is the same arithmetic step 01 did.
#[test]
fn reg_01_flow_control() {
    const TOTAL: usize = 64 * 1024;
    const WINDOW: usize = 4096;

    let mut cc = pair::client_config(pair::ALPN_HQ);
    let mut sc = pair::server_config(pair::ALPN_HQ);
    cc.set_initial_max_stream_data_bidi_remote(WINDOW as u64);
    sc.set_initial_max_stream_data_bidi_remote(WINDOW as u64);
    // Pin the receive window to the advertised initial limit on both sides, so
    // that the limit advances by exactly one window per round the way it did
    // before this milestone's autotuner existed. The setter arrives *with* the
    // autotuner, so at the previous milestone there is nothing to pin and
    // nothing to call. This group asserts behaviour inherited from step 01 and
    // gates on it, so it has to be assertable on both sides of the change.
    #[cfg(quiche_stream_window)]
    {
        cc.set_max_stream_window(WINDOW as u64);
        sc.set_max_stream_window(WINDOW as u64);
    }
    let mut p = pair::Pair::with_configs(&mut cc, &mut sc);
    p.handshake();

    let data = testkit::body(TOTAL, 0xfc01);
    let stream_id = 0;

    // Fill the 4096-byte window.
    let n = p
        .client
        .stream_send(stream_id, &data[..WINDOW], false)
        .expect("first stream_send");
    assert_eq!(n, WINDOW, "filled the {WINDOW} window, sent {n}");
    p.settle(8);

    // Capacity should now be zero.
    let cap = p
        .client
        .stream_capacity(stream_id)
        .expect("stream_capacity after filling window");
    assert_eq!(cap, 0, "capacity is 0 after {WINDOW} bytes in flight, observed {cap}");

    // Attempting to send more moves nothing. A sender with no stream capacity
    // reports `Error::Done` at this milestone and returned `Ok(0)` at the
    // previous one; what step 01 claimed, and what this group carries forward,
    // is that not one byte past the window is accepted. Any other error, and
    // any `Ok(n)` with `n > 0`, is a failure.
    let blocked = p.client.stream_send(stream_id, &data[WINDOW..], false);
    let accepted = match blocked {
        Ok(n) => n,
        Err(quiche::Error::Done) => 0,
        Err(e) => panic!(
            "with the {WINDOW}-byte window full, stream_send must accept nothing and say so \
             -- Err(Done) at this milestone, Ok(0) at the previous one; observed Err({e:?})"
        ),
    };
    assert_eq!(
        accepted, 0,
        "with the {WINDOW}-byte window full, stream_send must accept nothing; it took \
         {accepted} bytes past the window (returned {blocked:?})"
    );

    // The server reads those WINDOW bytes.
    let (got, fin) = pair::read_stream(&mut p.server, stream_id);
    assert_eq!(got.len(), WINDOW, "server read {WINDOW} bytes, got {}", got.len());
    assert!(!fin, "server did not see fin yet");
    assert_eq!(&got[..], &data[..WINDOW], "server read correct bytes");

    // Exchange to deliver MAX_STREAM_DATA.
    p.settle(8);

    // Capacity should reopen.
    let cap = p
        .client
        .stream_capacity(stream_id)
        .expect("stream_capacity after MAX_STREAM_DATA");
    assert!(cap > 0, "capacity reopened after MAX_STREAM_DATA, observed {cap}");
    assert!(
        cap <= WINDOW,
        "the receive window was pinned to {WINDOW} with set_max_stream_window, so the reopened \
         capacity cannot exceed it; observed {cap}"
    );

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
        assert!(
            cap <= WINDOW,
            "round {rounds}: the receiver's window is pinned to {WINDOW}, but the sender was \
             offered a capacity of {cap}"
        );
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
        "server read the bytes the client sent: first mismatch at offset {:?}",
        recv.iter().zip(data.iter()).position(|(a, b)| a != b)
    );
}

// ---------------------------------------------------------------------------
// reg-02-duplicate-push-removed  (T5)  <- step 02, cap-duplicate-push-removed
// ---------------------------------------------------------------------------

/// `reg-02-duplicate-push-removed`: an HTTP/3 frame type this implementation
/// does not define is skipped, and what follows it on the same stream still
/// arrives.
///
/// The three bytes `0e 01 ff` are the frame type DUPLICATE_PUSH as draft-27
/// carried it, a length of 1, and one byte of payload. The type was never
/// given a meaning by the published RFC and is still undefined here, so the
/// exact answer is "ignore it": the parser skips type and length, the HEADERS
/// frame written after it on the same request stream is delivered with its
/// `:path` intact, and neither endpoint closes the connection.
///
/// It is carried into this step because this step rewrites the crate's
/// directory layout and reshapes its HTTP/3 settings table around the very
/// frame parser that has to keep doing this.
#[test]
fn reg_02_duplicate_push_removed() {
    let mut cc = pair::client_config(pair::ALPN_H3);
    let mut sc = pair::server_config(pair::ALPN_H3);
    let mut p = pair::Pair::with_configs(&mut cc, &mut sc);
    p.handshake();

    let h3 = pair::h3_config();
    let mut h3_client = quiche::h3::Connection::with_transport(&mut p.client, &h3)
        .expect("h3 client on an established connection");
    let mut h3_server = quiche::h3::Connection::with_transport(&mut p.server, &h3)
        .expect("h3 server on an established connection");
    p.settle(16);

    // Frame type 0x0E with a one-byte payload, written by hand onto the stream
    // the h3 client is about to make its request on. Both the type and the
    // length are varints; both fit in one byte here.
    let unknown_frame = [0x0E, 0x01, 0xff];
    p.client
        .stream_send(0, &unknown_frame, false)
        .expect("writing an unknown HTTP/3 frame onto the request stream");

    let req = vec![
        quiche::h3::Header::new(b":method", b"GET"),
        quiche::h3::Header::new(b":scheme", b"https"),
        quiche::h3::Header::new(b":authority", b"quic.test"),
        quiche::h3::Header::new(b":path", b"/after-unknown"),
    ];
    let stream_id = h3_client
        .send_request(&mut p.client, &req, true)
        .expect("send_request after the unknown frame");
    assert_eq!(
        stream_id, 0,
        "this group needs the request to land on the same stream as the unknown frame; it went \
         to stream {stream_id}"
    );

    p.settle(64);

    // The server must deliver the request that followed the unknown frame, and
    // must not have torn the connection down over it.
    let mut saw_headers = false;
    loop {
        match h3_server.poll(&mut p.server) {
            Ok((id, quiche::h3::Event::Headers { list, .. })) => {
                assert_eq!(id, 0, "headers arrived on stream {id}, expected 0");
                let path = list
                    .iter()
                    .find(|h| h.name() == b":path")
                    .map(|h| String::from_utf8_lossy(h.value()).to_string())
                    .unwrap_or_default();
                assert_eq!(
                    path, "/after-unknown",
                    "the delivered request is not the one sent after the unknown frame"
                );
                saw_headers = true;
            },
            Ok((_, quiche::h3::Event::Finished)) => break,
            Ok(_) => {},
            Err(quiche::h3::Error::Done) => break,
            Err(e) => panic!(
                "an HTTP/3 frame of the unknown type 0x0E must be ignored, but polling the \
                 server raised {e:?}. 0x0E is not a defined frame type at this milestone and \
                 the rule for unknown types is to skip them."
            ),
        }
    }

    assert!(
        saw_headers,
        "the HEADERS frame that followed an unknown 0x0E frame on the same stream was never \
         delivered: the unknown frame was not skipped, it swallowed what came after it"
    );
    assert!(
        !p.server.is_closed(),
        "the server closed the connection after receiving an unknown HTTP/3 frame type"
    );
    assert!(
        !p.client.is_closed(),
        "the client connection was closed after sending an unknown HTTP/3 frame type"
    );
}
