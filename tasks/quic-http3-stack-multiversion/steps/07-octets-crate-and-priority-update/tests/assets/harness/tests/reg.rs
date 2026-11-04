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
// reg-01-header-parse  (T5)  <- step 01, cap-header-parse
// ---------------------------------------------------------------------------

/// `reg-01-header-parse`: `Header::from_slice` still reads a packet header
/// out of fixed bytes.
///
/// Step 01 established this over the draft-23 wire image. This milestone
/// moves the byte reader those functions are written against out of the
/// transport crate entirely -- `Octets`, `OctetsMut`, `get_varint`,
/// `get_bytes_with_varint_length` now live in a package of their own -- and
/// header parsing is the code that leans on it hardest. So the corpus is
/// carried forward, restated on QUIC version 1: six datagrams with exactly one
/// correct answer each, and two truncations that have to come back as
/// `Error::BufferTooShort` rather than as a panic or a wrong answer.
///
/// The Retry case is the interesting one after the move, because its token is
/// "everything up to the 16-byte integrity tag": it is defined in terms of the
/// reader's remaining capacity, so an off-by-one in the extracted crate's
/// `cap()` lands here and nowhere else.
#[test]
fn reg_01_header_parse() {
    // 1. Initial with a token
    let mut d1 = vec![
        0xc3, // long header, Initial
        0x00, 0x00, 0x00, 0x01, // version 1
        0x08, // DCID length
        0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x11, 0x22, // DCID
        0x04, // SCID length
        0x33, 0x44, 0x55, 0x66, // SCID
        0x06, // token length (varint)
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, // token
        0x00, // payload length varint
    ];
    let hdr = quiche::Header::from_slice(&mut d1, 0).expect("parse Initial with token");
    assert_eq!(hdr.ty, quiche::Type::Initial, "Initial: ty");
    assert_eq!(hdr.version, 1, "Initial: version");
    assert_eq!(
        hdr.dcid.as_ref(),
        &[0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x11, 0x22][..],
        "Initial: dcid"
    );
    assert_eq!(hdr.scid.as_ref(), &[0x33, 0x44, 0x55, 0x66][..], "Initial: scid");
    assert_eq!(
        hdr.token,
        Some(vec![0x01, 0x02, 0x03, 0x04, 0x05, 0x06]),
        "Initial: the varint-prefixed token"
    );

    // 2. Retry: the token is everything before the 16-byte integrity tag
    let mut d2 = vec![
        0xf0, // long header, Retry
        0x00, 0x00, 0x00, 0x01, // version 1
        0x05, // DCID length
        0xd1, 0xd2, 0xd3, 0xd4, 0xd5, // DCID
        0x03, // SCID length
        0xe1, 0xe2, 0xe3, // SCID
        0xa1, 0xa2, 0xa3, 0xa4, // token
    ];
    d2.extend_from_slice(&[0x5a; 16]); // Retry Integrity Tag
    let hdr = quiche::Header::from_slice(&mut d2, 0).expect("parse Retry");
    assert_eq!(hdr.ty, quiche::Type::Retry, "Retry: ty");
    assert_eq!(hdr.version, 1, "Retry: version");
    assert_eq!(hdr.dcid.as_ref(), &[0xd1, 0xd2, 0xd3, 0xd4, 0xd5][..], "Retry: dcid");
    assert_eq!(hdr.scid.as_ref(), &[0xe1, 0xe2, 0xe3][..], "Retry: scid");
    assert_eq!(
        hdr.token,
        Some(vec![0xa1, 0xa2, 0xa3, 0xa4]),
        "Retry: the token is the bytes before the 16-byte integrity tag, and only those"
    );

    // 3. Handshake
    let mut d3 = vec![
        0xe3, // long header, Handshake
        0x00, 0x00, 0x00, 0x01, // version 1
        0x07, // DCID length
        0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, // DCID
        0x02, // SCID length
        0xc1, 0xc2, // SCID
        0x00, // payload length varint
    ];
    let hdr = quiche::Header::from_slice(&mut d3, 0).expect("parse Handshake");
    assert_eq!(hdr.ty, quiche::Type::Handshake, "Handshake: ty");
    assert_eq!(hdr.version, 1, "Handshake: version");
    assert_eq!(
        hdr.dcid.as_ref(),
        &[0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7][..],
        "Handshake: dcid"
    );
    assert_eq!(hdr.scid.as_ref(), &[0xc1, 0xc2][..], "Handshake: scid");
    assert_eq!(hdr.token, None, "Handshake: no token");

    // 4. 0-RTT
    let mut d4 = vec![
        0xd1, // long header, 0-RTT
        0x00, 0x00, 0x00, 0x01, // version 1
        0x04, // DCID length
        0x71, 0x72, 0x73, 0x74, // DCID
        0x03, // SCID length
        0x81, 0x82, 0x83, // SCID
        0x00, // payload length varint
    ];
    let hdr = quiche::Header::from_slice(&mut d4, 0).expect("parse 0-RTT");
    assert_eq!(hdr.ty, quiche::Type::ZeroRTT, "0-RTT: ty");
    assert_eq!(hdr.version, 1, "0-RTT: version");
    assert_eq!(hdr.dcid.as_ref(), &[0x71, 0x72, 0x73, 0x74][..], "0-RTT: dcid");
    assert_eq!(hdr.scid.as_ref(), &[0x81, 0x82, 0x83][..], "0-RTT: scid");
    assert_eq!(hdr.token, None, "0-RTT: no token");

    // 5. Version Negotiation
    let mut d5 = vec![
        0xc0, // long header
        0x00, 0x00, 0x00, 0x00, // version 0 => Version Negotiation
        0x05, // DCID length
        0x91, 0x92, 0x93, 0x94, 0x95, // DCID
        0x04, // SCID length
        0xa1, 0xa2, 0xa3, 0xa4, // SCID
        0x00, 0x00, 0x00, 0x01, // version 1
        0xff, 0x00, 0x00, 0x17, // draft-23
        0xba, 0xba, 0xba, 0xba, // a version nobody implements
    ];
    let hdr = quiche::Header::from_slice(&mut d5, 0).expect("parse Version Negotiation");
    assert_eq!(hdr.ty, quiche::Type::VersionNegotiation, "VN: ty");
    assert_eq!(hdr.version, 0, "VN: version");
    assert_eq!(hdr.dcid.as_ref(), &[0x91, 0x92, 0x93, 0x94, 0x95][..], "VN: dcid");
    assert_eq!(hdr.scid.as_ref(), &[0xa1, 0xa2, 0xa3, 0xa4][..], "VN: scid");
    assert_eq!(
        hdr.versions,
        Some(vec![0x0000_0001, 0xff00_0017, 0xbaba_baba]),
        "VN: the whole supported-version list, in order"
    );

    // 6. Short header with a 16-byte connection ID
    let mut d6 = vec![
        0x41, // short header, fixed bit set
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d,
        0x1e, 0x1f, // DCID
        0x00, // packet number, still protected
    ];
    let hdr = quiche::Header::from_slice(&mut d6, 16).expect("parse short header");
    assert_eq!(hdr.ty, quiche::Type::Short, "Short: ty");
    assert_eq!(hdr.version, 0, "Short: version");
    assert_eq!(
        hdr.dcid.as_ref(),
        &[
            0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d,
            0x1e, 0x1f
        ][..],
        "Short: the caller's dcid_len decides how many bytes are the connection ID"
    );
    assert!(hdr.scid.as_ref().is_empty(), "Short: no scid on the wire");
    assert_eq!(hdr.token, None, "Short: no token");

    // 7. A DCID length that runs off the end of the datagram
    let mut d7 = vec![
        0xc3, 0x00, 0x00, 0x00, 0x01, //
        0x10, // claims 16 bytes of DCID
        0x00, 0x01, 0x02, // and carries three
    ];
    assert_eq!(
        quiche::Header::from_slice(&mut d7, 0),
        Err(quiche::Error::BufferTooShort),
        "a DCID length past the end of the datagram is BufferTooShort, not a panic"
    );

    // 8. A token length varint that runs off the end
    let mut d8 = vec![
        0xc3, 0x00, 0x00, 0x00, 0x01, //
        0x04, 0xaa, 0xbb, 0xcc, 0xdd, // DCID
        0x02, 0xee, 0xff, // SCID
        0x20, // claims 32 bytes of token
        0x01, 0x02, // and carries two
    ];
    assert_eq!(
        quiche::Header::from_slice(&mut d8, 0),
        Err(quiche::Error::BufferTooShort),
        "a token length past the end of the datagram is BufferTooShort, not a panic"
    );
}

// ---------------------------------------------------------------------------
// reg-01-h3-get  (T1)  <- step 01, cap-h3-get
// ---------------------------------------------------------------------------

/// `reg-01-h3-get`: HTTP/3 still moves a whole file between the candidate's
/// own two binaries.
///
/// Step 01 established this over draft-23 with `examples/http3-client` and
/// `examples/http3-server`. At this milestone the workspace builds
/// `quiche-client` and `quiche-server` under `apps/`, they take flags, and
/// HTTP/3 is selected with `--http-version HTTP/3`. The *observation* is
/// unchanged -- 64 KiB requested over HTTP/3 comes back byte for byte on the
/// client's stdout -- which is the point of an inherited group.
///
/// It is worth carrying at this step in particular: HTTP/3 grew a whole qlog
/// path and a new control-stream frame type here, and both of those run
/// inside the same frame reader that serves SETTINGS, HEADERS and DATA.
#[test]
fn reg_01_h3_get() {
    const LEN: usize = 64 * 1024;

    let dir = testkit::scratch("reg-01-h3-get");
    let want = testkit::body(LEN, 0x0333_0001);
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

// ---------------------------------------------------------------------------
// reg-06-qlog-jsonseq  (T4)  <- step 06, cap-qlog-jsonseq
// ---------------------------------------------------------------------------

/// `reg-06-qlog-jsonseq`: the qlog crate still writes a JSON-SEQ transport
/// trace, and this milestone's HTTP/3 `http:*` events have not changed its
/// shape or dropped an event underneath them.
///
/// Step 06 established the transport-only trace: a JSON-SEQ file whose header
/// record names `qlog_version` `"0.3"` and `qlog_format` `"JSON-SEQ"`, with no
/// `"traces"` array, and a fixed set of five transport event names for a
/// completed transfer, and asserted that set was *exactly* those five. This
/// milestone adds `http:*` events to the very same file when HTTP/3 is
/// selected, so that exact-set assertion cannot be carried forward as
/// written -- a correct step-07 stack now legitimately writes more names than
/// step 06 saw, and re-asserting the exact set would fail it for doing its
/// job. This group sidesteps that by deliberately not asking for HTTP/3 (no
/// `--http-version` flag, exactly step 06's own invocation), so no `http:*`
/// event can appear at all; `cap-h3-qlog` is what already covers those.
/// What is checked here is presence: the header shape, and every one of the
/// five transport names, must still be exactly what step 06 required.
/// The trace read here is the **client's**, not the server's, and that choice
/// is load-bearing. `qlog`'s streamer flushes its writer in exactly one place
/// -- `finish_log()`, reached only from `Connection::on_timeout()` when the
/// draining or idle timer expires -- so every event otherwise sits in an 8 KiB
/// `BufWriter` until that buffer spills. The server is a long-lived listener
/// this harness can only end with a signal, and a signal runs no destructors:
/// a server trace read after the kill is whatever happened to cross a buffer
/// boundary, truncated mid-record. Which of the five names survive that cut
/// depends on how verbose a candidate's early records are, which nothing in
/// the contract pins -- a conforming stack could fail this gate for being
/// chatty during the handshake. The client instead exits on its own once the
/// transfer completes, returning from `main` rather than calling
/// `process::exit` (reserved for the handshake and HTTP failure paths that
/// `expect_ok` below has already ruled out), so its writer is dropped and
/// flushed. `testkit::run` waited for that exit before this code runs, so the
/// file is complete and no longer changing: there is nothing left to race.
#[test]
fn reg_06_qlog_jsonseq() {
    let dir = testkit::scratch("reg-06-qlog-jsonseq");
    let qlogdir = dir.join("qlog");
    std::fs::create_dir_all(&qlogdir).expect("create QLOGDIR");

    let body = testkit::body(4096, 0x0706_0001);
    let port = testkit::free_port();
    let root = testkit::docroot(&dir, &[("payload.bin", body.clone())]);
    let tls = testkit::tls();

    let server_bin = testkit::server_bin();
    let mut sc = testkit::cmd(&server_bin);
    sc.env("QLOGDIR", &qlogdir)
        .arg("--listen")
        .arg(format!("127.0.0.1:{port}"))
        .arg("--cert")
        .arg(&tls.cert)
        .arg("--key")
        .arg(&tls.key)
        .arg("--root")
        .arg(&root)
        .arg("--no-retry");
    let server = testkit::spawn_server(&mut sc, port, &dir, "qlog-reg-server", LONG);

    let client_bin = testkit::client_bin();
    let mut cc = testkit::cmd(&client_bin);
    cc.env("QLOGDIR", &qlogdir)
        .arg("--no-verify")
        .arg(format!("https://127.0.0.1:{port}/payload.bin"));
    let run = testkit::run(&mut cc, LONG, &dir, "qlog-reg-client");
    let log = server.stop();
    run.expect_ok("GET /payload.bin with QLOGDIR set, no --http-version (step 06's invocation)");
    assert!(
        body == run.stdout_bytes,
        "the carried qlog group needs a completed transfer to have something to describe: {}\n\
         --- server log ---\n{log}",
        first_diff(&body, &run.stdout_bytes)
    );

    fn qlog_file(dir: &Path, prefix: &str) -> Option<std::path::PathBuf> {
        std::fs::read_dir(dir).ok()?.filter_map(|e| e.ok()).map(|e| e.path()).find(|p| {
            p.file_name().map(|n| n.to_string_lossy().starts_with(prefix)).unwrap_or(false)
        })
    }

    let client_path = qlog_file(&qlogdir, "client").unwrap_or_else(|| {
        panic!(
            "expected a client-*.sqlog trace in {} after one completed transfer with QLOGDIR \
             set.\n--- server log ---\n{log}",
            qlogdir.display()
        )
    });
    let client_log = std::fs::read_to_string(&client_path)
        .unwrap_or_else(|e| panic!("cannot read {}: {e}", client_path.display()));

    let hdr = client_log.lines().next().unwrap_or_else(|| {
        panic!("expected at least a header record in {}", client_path.display())
    });
    assert!(
        hdr.contains("\"qlog_version\":\"0.3\""),
        "expected the header record's qlog_version to still be \"0.3\" (unchanged since step \
         06); observed: {hdr}"
    );
    assert!(
        hdr.contains("\"qlog_format\":\"JSON-SEQ\""),
        "expected the header record's qlog_format to still be \"JSON-SEQ\"; observed: {hdr}"
    );
    assert!(
        !hdr.contains("\"traces\":"),
        "expected no \"traces\" array in the header record -- that belongs to the \
         non-streaming format step 06 replaced; observed: {hdr}"
    );

    for name in [
        "recovery:metrics_updated",
        "transport:data_moved",
        "transport:packet_received",
        "transport:packet_sent",
        "transport:parameters_set",
    ] {
        let needle = format!("\"name\":\"{name}\"");
        assert!(
            client_log.lines().any(|l| l.contains(&needle)),
            "expected the transport event {name:?} to still be present in the client qlog \
             trace -- step 06 required exactly this set and this milestone only adds http:* \
             events beside it (none of which can appear here, since this invocation carries no \
             --http-version flag); it does not remove any transport event. No line contained \
             {needle:?}.\n--- full trace ---\n{client_log}"
        );
    }
}
