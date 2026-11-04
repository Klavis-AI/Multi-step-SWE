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
/// own two HTTP/3 binaries.
///
/// Step 01 established this over draft-23 with the example binaries taking
/// `--listen`, `--cert`, `--key` and `--root`. Those flags are gone at this
/// milestone: `examples/http3-server` takes no arguments at all and opens
/// `examples/cert.crt`, `examples/cert.key` and `examples/root` relative to
/// its working directory, and `examples/http3-client` takes exactly one URL.
/// The *observation* is unchanged -- 64 KiB requested over HTTP/3 comes back
/// byte for byte on the client's stdout -- which is the point of an inherited
/// group: the interface moved, the capability did not.
#[test]
fn reg_01_h3_get() {
    const LEN: usize = 64 * 1024;

    let _lock = testkit::serial_t1();
    let dir = testkit::scratch("reg-01-h3-get");
    let want = testkit::body(LEN, 0x0333_0001);
    let cwd = testkit::example_server_cwd(&dir, &[("payload.bin", want.clone())]);

    let server_bin = testkit::h3_server_bin();
    let client_bin = testkit::h3_client_bin();

    let mut sc = testkit::cmd(&server_bin);
    sc.current_dir(&cwd);
    let server = testkit::spawn_server(
        &mut sc,
        testkit::EXAMPLE_SERVER_PORT,
        &dir,
        "h3-server",
        LONG,
    );

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

    drop(server);
}

// ---------------------------------------------------------------------------
// reg-01-flow-control  (T3)  <- step 01, cap-flow-control
// ---------------------------------------------------------------------------

/// `reg-01-flow-control`: stream flow control still throttles a sender.
///
/// A 64 KiB transfer through a 4096-byte stream window. The receiver's limit
/// has to be enforced at the sender (`stream_capacity` reaching 0, and
/// `stream_send` then accepting `Ok(0)`), extended as the receiver drains
/// (MAX_STREAM_DATA), and the whole transfer has to arrive byte for byte
/// having taken at least one round per window.
///
/// This is the oldest thing in the ladder and the least likely to be touched
/// on purpose, which is exactly why it is worth carrying: this milestone
/// rewrites the send path around `SendInfo` and the pacer, and a pacer that
/// ignores the flow-control limiter loses the transfer rather than the API.
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
    let n = p.client.stream_send(stream_id, &data[..WINDOW], false)
        .expect("first stream_send");
    assert_eq!(n, WINDOW, "filled the {WINDOW} window, sent {n}");
    p.settle(8);

    // Capacity should now be zero
    let cap = p.client.stream_capacity(stream_id)
        .expect("stream_capacity after filling window");
    assert_eq!(cap, 0, "capacity is 0 after {WINDOW} bytes in flight, observed {cap}");

    // Attempting to send more returns Ok(0)
    let n = p.client.stream_send(stream_id, &data[WINDOW..], false)
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
    let cap = p.client.stream_capacity(stream_id)
        .expect("stream_capacity after MAX_STREAM_DATA");
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
        "server read the bytes the client sent: first mismatch at offset {:?}",
        recv.iter().zip(data.iter()).position(|(a, b)| a != b)
    );
}


// ---------------------------------------------------------------------------
// reg-04-error-enum  (T3)  <- step 04, cap-error-enum
// ---------------------------------------------------------------------------

/// `reg-04-error-enum`: `Error::StreamStopped` still carries the peer's
/// application error code.
///
/// Step 04 is where the error enum learned to carry payloads, and the one this
/// group pins is `StreamStopped(u64)`: the code from a STOP_SENDING frame,
/// surfaced to the application through `stream_send` and `stream_capacity`.
/// This milestone rewrites the same enum again -- `InvalidStreamState` gains a
/// stream id -- so the risk being covered is that the rewrite drops or
/// reshuffles a payload that was already there.
///
/// The candidate never sends STOP_SENDING itself at this era, so the verifier
/// builds the frame and protects the packet with its own crypto, using the
/// 1-RTT secret the candidate exported through its keylog. The encoding is the
/// verifier's; only the keys are shared. The one thing that had to change from
/// step 04's version of this group is the call that hands the packet over:
/// `recv` now takes a `RecvInfo` beside the buffer, so this case hands the
/// packet over through [`pair::recv_from`], which carries both shapes: the
/// behaviour asserted here is inherited, and has to be assertable at the
/// previous milestone as well as this one.
#[test]
fn reg_04_error_enum() {
    let mut cc = pair::client_config(pair::ALPN_HQ);
    let mut sc = pair::server_config(pair::ALPN_HQ);
    cc.log_keys();

    let (mut p, keylog) = pair::Pair::with_keylog(&mut cc, &mut sc);
    p.handshake();

    // Open stream 4 so both sides agree it exists.
    p.client.stream_send(4, b"hello", false).expect("stream_send");
    p.settle(64);

    let secret = pair::keylog_secret(&keylog.contents(), "SERVER_TRAFFIC_SECRET_0")
        .expect("no SERVER_TRAFFIC_SECRET_0 in the keylog");
    let keys = support::PacketKeys::from_secret(support::Suite::Aes128Gcm, &secret)
        .expect("derive server 1-RTT keys");

    let stop_code = 0x123u64;
    // The packet number must be past anything the server already sent in the
    // 1-RTT space during the handshake, or the client dedupes our packet and
    // silently drops the frame.
    let mut pkt = build_stop_sending(&keys, &pair::CLIENT_CID, 4, stop_code, 1000);
    pair::recv_from(&mut p.client, &mut pkt, pair::server_addr())
        .expect("client recv of the verifier's STOP_SENDING");

    match p.client.stream_send(4, b"more", false) {
        Err(quiche::Error::StreamStopped(code)) => assert_eq!(
            code, stop_code,
            "expected StreamStopped to carry the application error code the verifier sent, \
             {stop_code:#x}; it carried {code:#x}"
        ),
        Ok(n) => panic!(
            "expected stream_send to fail with StreamStopped({stop_code:#x}) after the peer sent \
             STOP_SENDING; it accepted {n} bytes"
        ),
        Err(e) => panic!("expected StreamStopped({stop_code:#x}); observed {e:?}"),
    }

    // The same identity shows through stream_capacity.
    match p.client.stream_capacity(4) {
        Err(quiche::Error::StreamStopped(code)) => assert_eq!(
            code, stop_code,
            "expected stream_capacity to report StreamStopped({stop_code:#x}); it reported \
             StreamStopped({code:#x})"
        ),
        other => panic!(
            "expected stream_capacity to report StreamStopped({stop_code:#x}) on a stopped \
             stream; observed {other:?}"
        ),
    }
}

/// Build a 1-RTT packet carrying one STOP_SENDING frame, protected with
/// `keys`. Entirely the verifier's own encoding.
fn build_stop_sending(
    keys: &support::PacketKeys, dcid: &[u8], stream_id: u64, error_code: u64, pn: u64,
) -> Vec<u8> {
    // Frame payload: STOP_SENDING (0x05), stream id, error code. Padded so the
    // packet is long enough to sample for header protection.
    let mut payload = Vec::new();
    support::encode_varint(0x05, &mut payload).expect("frame type");
    support::encode_varint(stream_id, &mut payload).expect("stream id");
    support::encode_varint(error_code, &mut payload).expect("error code");
    payload.resize(payload.len() + 64, 0); // PADDING

    // Short header: fixed bit set, key phase 0, 4-byte packet number.
    let pn_len = 4usize;
    let mut pkt = Vec::new();
    pkt.push(0x40 | (pn_len as u8 - 1));
    pkt.extend_from_slice(dcid);
    let pn_offset = pkt.len();
    pkt.extend_from_slice(&(pn as u32).to_be_bytes());

    let sealed = keys.seal(pn, &pkt, &payload).expect("seal");
    pkt.extend_from_slice(&sealed);

    support::apply_header_protection(keys, &mut pkt, pn_offset, pn_len)
        .expect("apply header protection");
    pkt
}
