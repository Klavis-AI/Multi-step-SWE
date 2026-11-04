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

/// `gate-endtoend`: the candidate's own `quiche-server` and `quiche-client`,
/// two processes, one UDP loopback hop, two requests.
///
/// Nothing clever on purpose. The binaries this milestone ships take their
/// whole configuration from the command line, so the group picks a free
/// ephemeral port, writes its own document root and index file, starts the
/// server on that port with the verifier's certificate and key, and then runs
/// the client twice: once for a 96 KiB generated body, which must come back
/// byte for byte on stdout with exit status 0, and once for `/`, which must be
/// answered with the index file. The server must still be running afterwards.
///
/// If the workspace does not build both binaries, if the flags are not
/// honoured, if the handshake does not complete, or if the stack cannot move a
/// body of that size, this group fails and it is the first thing to look at:
/// every other T1 observation at this step is noise until it passes.
#[test]
fn gate_endtoend() {
    const LEN: usize = 96 * 1024;

    let dir = testkit::scratch("gate-endtoend");
    let want = testkit::body(LEN, 0x9a7e_0006);
    let index = b"gate index\n".to_vec();
    let root = testkit::docroot(
        &dir,
        &[("gate.bin", want.clone()), ("index.html", index.clone())],
    );

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
        .arg(&root);
    let mut server = testkit::spawn_server(&mut sc, port, &dir, "gate-server", LONG);

    // --- the body ----------------------------------------------------------
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
        LEN,
        "expected the {LEN} byte body of {url} on the client's stdout; observed {} bytes ({}).\n\
         --- client stderr ---\n{}\n--- server log ---\n{log}",
        got.len(),
        first_diff(&want, got),
        run.stderr
    );
    assert!(
        want == *got,
        "expected the body of {url} back byte for byte; observed {}.\n\
         --- server log ---\n{log}",
        first_diff(&want, got)
    );

    // --- the index ---------------------------------------------------------
    let index_url = format!("https://127.0.0.1:{port}/");
    let mut ic = testkit::cmd(&client_bin);
    ic.arg("--no-verify").arg(&index_url);
    let run = testkit::run(&mut ic, LONG, &dir, "gate-client-index");

    let log = server.log();
    assert!(
        !run.timed_out,
        "expected {} to fetch {index_url} and exit within {LONG:?}; observed: killed at the \
         deadline.\n--- client stderr ---\n{}\n--- server log ---\n{log}",
        client_bin.display(),
        run.stderr
    );
    assert_eq!(
        run.status.and_then(|s| s.code()),
        Some(0),
        "expected {} to exit 0 after fetching {index_url}; observed status {:?}.\n\
         --- client stderr ---\n{}\n--- server log ---\n{log}",
        client_bin.display(),
        run.status,
        run.stderr
    );
    assert!(
        index == run.stdout_bytes,
        "a request for `/` must be answered with the index file in the directory `--root` \
         named; observed {}.\n--- server log ---\n{log}",
        first_diff(&index, &run.stdout_bytes)
    );

    assert!(
        server.alive(),
        "the server exited while serving two requests.\n--- server log ---\n{}",
        server.log()
    );

    drop(server);
}
