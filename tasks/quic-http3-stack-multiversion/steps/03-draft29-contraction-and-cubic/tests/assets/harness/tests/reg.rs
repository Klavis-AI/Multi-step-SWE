//! inherited groups: an earlier milestone's decisive observation, re-run verbatim in intent against the cumulative worktree
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
//! These three come from step 01. "Verbatim in intent" is the standard, not
//! verbatim in bytes: step 01 pinned the wire version with `--wire-version
//! ff000017` and pointed the binaries at a free port with `--listen`, and
//! neither flag exists at this milestone -- nor does draft-23. What is
//! re-observed is the thing step 01 observed: that the candidate's first
//! Initial opens under the era's salt and carries a ClientHello, that its two
//! HTTP/3 binaries complete a GET, and that half a megabyte survives a stream.

use std::path::{Path, PathBuf};
use std::sync::{Mutex, MutexGuard};
use std::time::Duration;

#[allow(unused_imports)]
use verifier::{pair, support, testkit};

#[allow(dead_code)]
const SHORT: Duration = Duration::from_secs(10);
#[allow(dead_code)]
const LONG: Duration = Duration::from_secs(60);

/// The address the 0.5.0 example servers hard-code.
const ORACLE_PORT: u16 = 4433;

static ORACLE_PORT_LOCK: Mutex<()> = Mutex::new(());

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

/// Serialise the groups that need `127.0.0.1:4433`. See `tests/cap.rs`.
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

/// Build the working directory the 0.5.0 example servers expect -- they open
/// `examples/cert.crt`, `examples/cert.key` and `examples/root` relative to
/// the current directory -- and return the document root inside it.
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

// ---------------------------------------------------------------------------
// reg-01-initial-keys  (T2)
// ---------------------------------------------------------------------------

/// `reg-01-initial-keys`: step 01's `cap-initial-keys`, re-run on this
/// milestone's wire version.
///
/// The first datagram the candidate's `client` sends is opened by the
/// verifier's own implementation -- Initial secrets from this version's salt
/// over the packet's destination connection ID, header protection removed with
/// the AES-ECB mask, packet protection removed with AEAD_AES_128_GCM -- and
/// the plaintext must contain a CRYPTO frame at offset 0 whose bytes begin a
/// well-formed TLS 1.3 ClientHello. Nothing in the candidate's library is used
/// to read it.
///
/// Step 01 pinned the version with `--wire-version ff000017`; this milestone's
/// `client` takes no flags and draft-23 is gone, so the version under test is
/// whatever the client puts on the wire, which the group checks is a version
/// this milestone still supports. What is inherited is the whole Initial
/// packet-protection path, which this step rewrites: the salt stops being a
/// constant and becomes a function of the version, and a candidate that
/// mis-derives it breaks every connection it makes.
#[test]
fn initial_keys() {
    let dir = testkit::scratch("reg-01-initial-keys");
    let ep = testkit::Endpoint::new();
    let addr = ep.local_addr();

    // The verifier's socket is not a QUIC server; it exists to catch the
    // client's first flight. The client is killed as soon as that flight
    // arrives.
    let client_bin = testkit::client_bin();
    let mut cc = testkit::cmd(&client_bin);
    cc.current_dir(&dir).arg(format!("https://127.0.0.1:{}/initial", addr.port()));
    let client = testkit::spawn_proc(&mut cc, &dir, "keys-client");

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
    assert!(
        dgram.len() >= 1200,
        "expected the client's Initial datagram to be padded to at least 1200 bytes, as RFC 9000 \
         Section 14.1 requires of a client's first flight; observed {} bytes.\n\
         --- client log ---\n{log}",
        dgram.len()
    );

    let version = support::peek_version(&dgram).unwrap_or_else(|e| {
        panic!(
            "expected a readable long-header version field in the client's first datagram; \
             observed {e}, datagram {}.\n--- client log ---\n{log}",
            head(&dgram, 48)
        )
    });
    let supported = [
        support::VERSION_DRAFT27,
        support::VERSION_DRAFT28,
        support::VERSION_DRAFT29,
    ];
    assert!(
        supported.contains(&version),
        "expected the client to offer one of this milestone's three versions -- draft-27 \
         (0xff00001b), draft-28 (0xff00001c) or draft-29 (0xff00001d); observed {version:#010x}, \
         datagram {}.\n--- client log ---\n{log}",
        head(&dgram, 48)
    );

    let initial = support::decrypt_initial(&dgram, version, support::Perspective::Client)
        .unwrap_or_else(|e| {
            panic!(
                "expected the verifier's own Initial keys for {version:#010x} -- the version's \
                 salt over the packet's destination connection id, AES-ECB header protection, \
                 AEAD_AES_128_GCM -- to open the client's first Initial; observed {e}.\n\
                 datagram {}\n--- client log ---\n{log}",
                head(&dgram, 64)
            )
        });

    let crypto = initial
        .frames()
        .collect::<Vec<_>>()
        .into_iter()
        .find_map(|f| match f {
            Ok(support::Frame::Crypto { offset: 0, data }) => Some(data.to_vec()),
            _ => None,
        })
        .unwrap_or_else(|| {
            let seen: Vec<String> = initial.frames().map(|f| format!("{f:?}")).collect();
            panic!(
                "expected a CRYPTO frame at offset 0 in the decrypted Initial; observed the \
                 frames {seen:?}.\npayload {}\n--- client log ---\n{log}",
                head(&initial.payload, 64)
            )
        });

    assert!(
        crypto.len() >= 44,
        "expected the CRYPTO frame to carry a TLS 1.3 ClientHello, which cannot be shorter than \
         its own 4-byte handshake header plus legacy_version, random and two length bytes; \
         observed {} bytes: {}.\n--- client log ---\n{log}",
        crypto.len(),
        head(&crypto, 44)
    );
    assert_eq!(
        crypto[0], 0x01,
        "expected TLS handshake type 1 (client_hello) as the first byte of the crypto stream; \
         observed {:#04x}, crypto stream {}.\n--- client log ---\n{log}",
        crypto[0],
        head(&crypto, 48)
    );
    let declared = ((crypto[1] as usize) << 16) | ((crypto[2] as usize) << 8) | crypto[3] as usize;
    assert!(
        declared + 4 >= crypto.len() && declared >= 40,
        "expected the client_hello's 3-byte length to describe a message at least as long as the \
         bytes in this packet; observed a declared length of {declared} against {} bytes of \
         crypto stream: {}.\n--- client log ---\n{log}",
        crypto.len(),
        head(&crypto, 48)
    );
    assert_eq!(
        &crypto[4..6],
        &[0x03, 0x03],
        "expected legacy_version 0x0303 in the ClientHello, as TLS 1.3 requires; observed {}, \
         crypto stream {}.\n--- client log ---\n{log}",
        testkit::hex(&crypto[4..6]),
        head(&crypto, 48)
    );
    let session_id_len = crypto[38] as usize;
    assert!(
        session_id_len <= 32,
        "expected a legacy_session_id of at most 32 bytes after the 32-byte random; observed a \
         length byte of {session_id_len}, crypto stream {}.\n--- client log ---\n{log}",
        head(&crypto, 48)
    );
    let cs_off = 39 + session_id_len;
    assert!(
        crypto.len() >= cs_off + 2,
        "expected a cipher_suites length after the legacy_session_id at offset {cs_off}; \
         observed only {} bytes of crypto stream: {}.\n--- client log ---\n{log}",
        crypto.len(),
        head(&crypto, 64)
    );
    let cs_len = ((crypto[cs_off] as usize) << 8) | crypto[cs_off + 1] as usize;
    assert!(
        cs_len >= 2 && cs_len % 2 == 0 && cs_off + 2 + cs_len <= crypto.len(),
        "expected a non-empty, even-length cipher_suites list inside the ClientHello; observed a \
         declared length of {cs_len} at offset {cs_off} in {} bytes of crypto stream: {}.\n\
         --- client log ---\n{log}",
        crypto.len(),
        head(&crypto, 64)
    );
}

// ---------------------------------------------------------------------------
// reg-01-h3-get  (T1)
// ---------------------------------------------------------------------------

/// `reg-01-h3-get`: step 01's `cap-h3-get`, re-run against the cumulative
/// worktree.
///
/// Everything HTTP/3 has to be right for the body to come back: the ALPN has
/// to be negotiated, both peers have to open and read each other's control
/// streams and SETTINGS, the request has to be a QPACK-encoded HEADERS frame
/// the server can decode into a `:path`, and the response has to be HEADERS
/// followed by DATA that the client can frame back out again. This step moves
/// the ALPN to `h3-29` and routes every response through
/// `send_response_with_priority`, so a careless edit here shows up as a hang
/// or an empty body rather than as a subtle difference.
#[test]
fn h3_get() {
    const LEN: usize = 64 * 1024;

    let _g = port_guard();
    let dir = testkit::scratch("reg-01-h3-get");
    let want = testkit::body(LEN, 0x0333_0001);
    oracle_cwd(&dir, &[("payload.bin", want.clone())]);

    let server_bin = testkit::h3_server_bin();
    let client_bin = testkit::h3_client_bin();

    let mut sc = testkit::cmd(&server_bin);
    sc.current_dir(&dir);
    let server = testkit::spawn_server(&mut sc, ORACLE_PORT, &dir, "h3-server", SHORT);

    let url = format!("https://127.0.0.1:{ORACLE_PORT}/payload.bin");
    let mut cc = testkit::cmd(&client_bin);
    cc.current_dir(&dir).arg(&url);
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

    // A path that is not in the document root must still produce a clean,
    // answered request rather than a hang: step 01's HTTP/3 server answered
    // 404 and this one still has to.
    let url = format!("https://127.0.0.1:{ORACLE_PORT}/definitely-absent.bin");
    let mut cc = testkit::cmd(&client_bin);
    cc.current_dir(&dir).arg(&url);
    let run = testkit::run(&mut cc, SHORT, &dir, "h3-client-404");
    let log = server.log();
    assert!(
        !run.timed_out,
        "expected {} to get an answer for the missing resource {url} and exit within {SHORT:?}; \
         observed: still running at the deadline, killed.\n--- client stderr ---\n{}\n\
         --- server log ---\n{log}",
        client_bin.display(),
        run.stderr
    );
    assert_ne!(
        run.stdout_bytes, want,
        "expected the missing resource {url} not to return the body of payload.bin.\n\
         --- server log ---\n{log}"
    );

    drop(server);
}

// ---------------------------------------------------------------------------
// reg-01-stream-transfer  (T1)
// ---------------------------------------------------------------------------

/// `reg-01-stream-transfer`: step 01's `cap-stream-transfer`, re-run against
/// the cumulative worktree.
///
/// 512 KiB is 370 packets at the 1452-byte maximum, so it cannot fit in the
/// initial congestion window or the initial flow-control window and cannot be
/// delivered without the receiver reassembling stream data that arrived across
/// many packets and possibly out of order. A stack that drops, duplicates or
/// misorders a range produces wrong bytes rather than right ones by luck --
/// and this step replaces the congestion controller underneath all of it, so
/// the transfer is exactly what a bad CUBIC or a bad HyStart++ breaks.
#[test]
fn stream_transfer() {
    const LEN: usize = 512 * 1024;

    let _g = port_guard();
    let dir = testkit::scratch("reg-01-stream-transfer");
    let want = testkit::body(LEN, 0x5123_4567);
    oracle_cwd(&dir, &[("big.bin", want.clone())]);

    let server_bin = testkit::server_bin();
    let client_bin = testkit::client_bin();

    let mut sc = testkit::cmd(&server_bin);
    sc.current_dir(&dir);
    let server = testkit::spawn_server(&mut sc, ORACLE_PORT, &dir, "xfer-server", SHORT);

    let url = format!("https://127.0.0.1:{ORACLE_PORT}/big.bin");
    let mut cc = testkit::cmd(&client_bin);
    cc.current_dir(&dir).arg(&url);
    let run = testkit::run(&mut cc, LONG, &dir, "xfer-client");

    let log = server.log();
    assert!(
        !run.timed_out,
        "expected {} to fetch {LEN} bytes from {url} within {LONG:?}; observed: still running at \
         the deadline, killed after writing {} of {LEN} bytes.\n--- client stderr ---\n{}\n\
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
    assert_eq!(
        got.len(),
        LEN,
        "expected exactly {LEN} bytes of {url} on the client's stdout; observed {} bytes ({}). \
         The 0.5.0 HTTP/0.9 server answers a request for a file it cannot open with \"Not \
         Found!\", so a short body usually means the request line or the document root, not the \
         transfer.\n--- client stderr ---\n{}\n--- server log ---\n{log}",
        got.len(),
        first_diff(&want, got),
        run.stderr
    );
    assert!(
        want == *got,
        "expected the 512 KiB body of {url} back byte for byte; observed {}.\n\
         --- server log ---\n{log}",
        first_diff(&want, got)
    );

    drop(server);
}
