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

use std::path::Path;
use std::time::Duration;

#[allow(unused_imports)]
use verifier::{pair, support, testkit};

#[allow(dead_code)]
const SHORT: Duration = Duration::from_secs(10);
#[allow(dead_code)]
const LONG: Duration = Duration::from_secs(60);

/// HTTP/3 frame type of a PRIORITY_UPDATE that prioritises a request stream.
const PRIORITY_UPDATE_REQUEST: u64 = 0xF0700;

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

/// Send one HTTP/3 frame down a raw QUIC stream, bypassing the h3 layer.
fn send_h3_frame(conn: &mut quiche::Connection, stream_id: u64, ty: u64, payload: &[u8]) {
    let frame = pair::h3_frame(ty, payload);
    let n = conn.stream_send(stream_id, &frame, false).unwrap_or_else(|e| {
        panic!(
            "stream_send of a {frame_len} byte HTTP/3 frame on stream {stream_id}: {e:?}",
            frame_len = frame.len()
        )
    });
    assert_eq!(
        n,
        frame.len(),
        "expected the whole {} byte HTTP/3 frame to fit on stream {stream_id}; {n} bytes were \
         accepted",
        frame.len()
    );
}

// ---------------------------------------------------------------------------
// groups
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// reg-01-stream-transfer  (T1)  <- step 01, cap-stream-transfer
// ---------------------------------------------------------------------------

/// `reg-01-stream-transfer`: half a megabyte still crosses two processes
/// unchanged.
///
/// The oldest observation in the ladder, and the one this step is most likely
/// to break without noticing. Every outbound packet now goes through a path
/// lookup, because a connection may have more than one path; the single-path
/// case is the one nobody retests after that refactor, and it is the case
/// every real deployment is in.
///
/// 512 KiB is chosen to be well past the initial congestion window and past
/// the initial stream flow-control limit, so the transfer cannot complete
/// without the window-reopening and loss-recovery machinery running for many
/// rounds on the path that was found by lookup.
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
    let want = testkit::body(LEN, 0x0816_0011);
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
// reg-04-datagram-roundtrip  (T3)  <- step 04, cap-datagram-roundtrip
// ---------------------------------------------------------------------------

/// `reg-04-datagram-roundtrip`: a DATAGRAM still crosses in both directions,
/// byte for byte, and still never becomes stream data.
///
/// Of everything carried forward, this is the one the multi-path refactor is
/// most likely to break and least likely to break loudly. A datagram belongs
/// to the path it was sent on and is never retransmitted, so once sending is
/// routed through a path lookup, a datagram that is dropped on the floor by a
/// mistaken lookup simply never arrives -- no error, no close, no retry.
///
/// Same shape and same claims as step 04's group: negotiation is visible in
/// `dgram_max_writable_len`, the payload survives a round trip, nothing is
/// delivered as stream data, and both queues end empty.
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

    let payload = testkit::body(256, 0x0816_0012);
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
// reg-07-priority-update  (T3)  <- step 07, cap-priority-update
// ---------------------------------------------------------------------------

/// `reg-07-priority-update`: PRIORITY_UPDATE still stores the latest value,
/// still raises an edge-triggered event, and still tracks elements apart.
///
/// This step touches the HTTP/3 layer from an angle that has nothing to do
/// with priorities: `APPLICATION_PROTOCOL` changed shape and
/// `set_application_protos` changed signature. A connection whose ALPN no
/// longer negotiates never opens a control stream at all, so this group fails
/// long before it reaches its own subject matter -- which is exactly what an
/// inherited group is for.
///
/// Same five stages as step 07's group, in the same order, with the same
/// claims: one frame raises one event, a second update for an unread element
/// raises none but replaces the stored value, taking it hands back the latest
/// value once, the event then rearms, and a different prioritised element is
/// stored beside the first rather than over it.
#[test]
fn reg_07_priority_update() {
    let mut h = pair::H3Pair::new();

    // 1. the first update raises exactly one event
    send_h3_frame(
        &mut h.t.client,
        pair::H3_CLIENT_CONTROL_STREAM,
        PRIORITY_UPDATE_REQUEST,
        &pair::priority_update_payload(0, b"u=3"),
    );
    h.settle();
    assert_eq!(
        h.poll_server(),
        Ok((0, quiche::h3::Event::PriorityUpdate)),
        "expected a PRIORITY_UPDATE for element 0 on the peer's control stream to raise \
         (0, Event::PriorityUpdate) at the server"
    );
    assert_eq!(
        h.poll_server(),
        Err(quiche::h3::Error::Done),
        "expected exactly one event for one PRIORITY_UPDATE frame"
    );

    // 2. a second update for the same element, unread, raises nothing but
    //    replaces the stored value
    send_h3_frame(
        &mut h.t.client,
        pair::H3_CLIENT_CONTROL_STREAM,
        PRIORITY_UPDATE_REQUEST,
        &pair::priority_update_payload(0, b"u=5"),
    );
    h.settle();
    assert_eq!(
        h.poll_server(),
        Err(quiche::h3::Error::Done),
        "expected a second PRIORITY_UPDATE for element 0 not to raise a second event while the \
         first one is still unread: the event tracks stored state, not frames"
    );

    // 3. taking it yields the latest value, once
    assert_eq!(
        h.server.take_last_priority_update(0),
        Ok(b"u=5".to_vec()),
        "expected take_last_priority_update(0) to hand back the most recent priority field \
         value, byte for byte"
    );
    assert_eq!(
        h.server.take_last_priority_update(0),
        Err(quiche::h3::Error::Done),
        "expected take_last_priority_update to take: a second call with nothing stored is Done"
    );

    // 4. and the event rearms
    send_h3_frame(
        &mut h.t.client,
        pair::H3_CLIENT_CONTROL_STREAM,
        PRIORITY_UPDATE_REQUEST,
        &pair::priority_update_payload(0, b"u=7, i"),
    );
    h.settle();
    assert_eq!(
        h.poll_server(),
        Ok((0, quiche::h3::Event::PriorityUpdate)),
        "expected the event to rearm once the stored update had been taken"
    );
    assert_eq!(
        h.server.take_last_priority_update(0),
        Ok(b"u=7, i".to_vec()),
        "expected the field value to come back uninterpreted, including its spaces"
    );

    // 5. a different element is tracked separately
    send_h3_frame(
        &mut h.t.client,
        pair::H3_CLIENT_CONTROL_STREAM,
        PRIORITY_UPDATE_REQUEST,
        &pair::priority_update_payload(4, b"u=1"),
    );
    h.settle();
    assert_eq!(
        h.poll_server(),
        Ok((4, quiche::h3::Event::PriorityUpdate)),
        "expected the prioritized element ID from the frame to be the event's ID"
    );
    assert_eq!(
        h.server.take_last_priority_update(4),
        Ok(b"u=1".to_vec()),
        "expected element 4's update to be stored beside element 0's, not to replace it"
    );
    assert_eq!(
        h.server.take_last_priority_update(0),
        Err(quiche::h3::Error::Done),
        "expected element 0 to still have nothing stored after element 4 was updated"
    );

    assert!(
        !h.t.server.is_closed() && !h.t.client.is_closed(),
        "expected five well-formed PRIORITY_UPDATE frames to leave the connection open; \
         server local error {:?}, client local error {:?}",
        h.t.server.local_error().map(|e| e.error_code),
        h.t.client.local_error().map(|e| e.error_code)
    );
}
