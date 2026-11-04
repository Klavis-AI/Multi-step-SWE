//! the end-to-end gate: the one group that fails if the stack does not work at all
//!
//! One `#[test]` per scored group, named exactly as the group id with dashes
//! turned into underscores: `gate-endtoend` is `fn gate_endtoend()`. The
//! driver runs it on its own with `--exact` and asserts that exactly one test
//! executed, so a renamed, `#[ignore]`d or accidentally deleted function
//! scores zero rather than passing silently.
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

// ---------------------------------------------------------------------------
// groups
// ---------------------------------------------------------------------------

/// `gate-endtoend`: the candidate's own `quiche-server` and `quiche-client`,
/// two processes, one UDP loopback hop, one small file.
///
/// Nothing clever on purpose. This milestone is in the `apps/` era, so the
/// binaries take flags: the server is given `--listen`, `--cert`, `--key` and
/// `--root`, and the client one URL and `--no-verify`, because the fixture
/// certificate is the verifier's self-signed one. Everything else is the
/// reference's own default -- version 1, ALPN negotiated from the app's own
/// list, HTTP/3 alongside HTTP/0.9. If this one fails, every other T1
/// observation at this step is noise.
#[test]
fn gate_endtoend() {
    let dir = testkit::scratch("gate-endtoend");
    let want = testkit::body(2048, 0x9a7e_0010);
    let root = testkit::docroot(&dir, &[("gate.bin", want.clone())]);
    let tls = testkit::tls();
    let port = testkit::free_port();

    let server_bin = testkit::server_bin();
    let client_bin = testkit::client_bin();

    let mut sc = testkit::cmd(&server_bin);
    sc.arg("--listen")
        .arg(format!("127.0.0.1:{port}"))
        .arg("--cert")
        .arg(&tls.cert)
        .arg("--key")
        .arg(&tls.key)
        .arg("--root")
        .arg(&root)
        .arg("--no-retry");
    let server = testkit::spawn_server(&mut sc, port, &dir, "gate-server", LONG);

    let url = format!("https://127.0.0.1:{port}/gate.bin");
    let mut cc = testkit::cmd(&client_bin);
    cc.arg("--no-verify").arg(&url);
    let run = testkit::run(&mut cc, LONG, &dir, "gate-client");

    let log = server.log();
    assert!(
        !run.timed_out,
        "expected {} to fetch {url} and exit within {LONG:?}; observed: still running at the \
         deadline, killed. It wrote {} stdout bytes and {} stderr bytes.\n\
         --- client stderr ---\n{}\n--- server log ---\n{log}",
        client_bin.display(),
        run.stdout_bytes.len(),
        run.stderr_bytes.len(),
        run.stderr
    );
    assert_eq!(
        run.status.and_then(|s| s.code()),
        Some(0),
        "expected {} to exit 0 after fetching {url}; observed status {:?}.\n\
         --- client stderr ---\n{}\n--- server log ---\n{log}",
        client_bin.display(),
        run.status,
        run.stderr
    );

    let got = &run.stdout_bytes;
    assert_eq!(
        got.len(),
        want.len(),
        "expected the {} byte body of {url} on the client's stdout; observed {} bytes.\n\
         first 32 expected: {}\nfirst 32 observed: {}\n\
         --- client stderr ---\n{}\n--- server log ---\n{log}",
        want.len(),
        got.len(),
        testkit::hex(&want[..32.min(want.len())]),
        testkit::hex(&got[..32.min(got.len())]),
        run.stderr
    );
    let diff = want.iter().zip(got.iter()).position(|(a, b)| a != b);
    assert!(
        diff.is_none(),
        "expected the body of {url} back byte for byte; observed the first difference at offset \
         {0:?}: expected {1}, observed {2} (16 bytes from that offset).\n\
         --- server log ---\n{log}",
        diff,
        testkit::hex(&want[diff.unwrap_or(0)..(diff.unwrap_or(0) + 16).min(want.len())]),
        testkit::hex(&got[diff.unwrap_or(0)..(diff.unwrap_or(0) + 16).min(got.len())]),
    );

    drop(server);
}
