//! the end-to-end gate: the one group that fails if the stack does not work at all
//!
//! One `#[test]` per scored group, named exactly as the group id with dashes
//! turned into underscores: `cap-initial-keys` is `fn cap_initial_keys()`.
//! The driver runs each one on its own with `--exact` and asserts that
//! exactly one test executed, so a renamed, `#[ignore]`d or accidentally
//! deleted function scores zero for that group rather than passing silently.
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

/// `gate-endtoend`: the candidate's own `server` and `client`, two processes,
/// one UDP loopback hop, one small file.
///
/// Nothing clever on purpose: an HQ GET over the candidate's own binaries. If
/// this one fails, every other T1 observation at this step is noise.
///
/// At 0.7.0 these binaries take no configuration. `tools/apps` -- the
/// flag-taking `quiche-client`/`quiche-server` -- is not a workspace member
/// until step 06, so the only things built are the examples, and they compile
/// in their listen address, certificate paths and document root. The group
/// therefore stages a directory of the shape the server expects and starts it
/// there. See REQUIREMENTS.md, "T1 binary CLI eras".
#[test]
fn gate_endtoend() {
    // The port is compiled into the binary, so only one T1 group may hold it.
    let _lock = testkit::serial_t1();

    let dir = testkit::scratch("gate-endtoend");
    let want = testkit::body(2048, 0x9a7e_0001);
    let cwd = testkit::example_server_cwd(&dir, &[("gate.bin", want.clone())]);
    let port = testkit::EXAMPLE_SERVER_PORT;

    let server_bin = testkit::server_bin();
    let client_bin = testkit::client_bin();

    let mut sc = testkit::cmd(&server_bin);
    sc.current_dir(&cwd);
    let server = testkit::spawn_server(&mut sc, port, &dir, "gate-server", SHORT);

    let url = format!("https://127.0.0.1:{port}/gate.bin");
    let mut cc = testkit::cmd(&client_bin);
    cc.arg(&url);
    let run = testkit::run(&mut cc, SHORT, &dir, "gate-client");

    let log = server.log();
    assert!(
        !run.timed_out,
        "expected {} to fetch {url} and exit within {SHORT:?}; observed: still running at the \
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
