//! the end-to-end gate: the one group that fails if the stack does not work at all
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

// ---------------------------------------------------------------------------
// gate-endtoend  (T1)
// ---------------------------------------------------------------------------

/// `gate-endtoend`: the candidate's own `server` and `client`, two processes,
/// one UDP loopback hop, one small file.
///
/// Nothing clever on purpose. The document root holds one 2 KiB file and the
/// group asks for it; the version, the congestion controller, the priority
/// scheme and address validation all have groups of their own. If this one
/// fails, every other T1 observation at this step is noise.
///
/// The binaries at this milestone take no flags: the server binds
/// `127.0.0.1:4433` and opens `examples/cert.crt`, `examples/cert.key` and
/// `examples/root` relative to wherever it was started, so the group builds
/// that layout in a scratch directory and starts it there. The candidate's own
/// tree is never written to.
#[test]
fn endtoend() {
    let _g = port_guard();
    let dir = testkit::scratch("gate-endtoend");
    let want = testkit::body(2048, 0x9a7e_0001);
    oracle_cwd(&dir, &[("gate.bin", want.clone())]);

    let server_bin = testkit::server_bin();
    let client_bin = testkit::client_bin();

    let mut sc = testkit::cmd(&server_bin);
    sc.current_dir(&dir);
    let server = testkit::spawn_server(&mut sc, ORACLE_PORT, &dir, "gate-server", SHORT);

    let url = format!("https://127.0.0.1:{ORACLE_PORT}/gate.bin");
    let mut cc = testkit::cmd(&client_bin);
    cc.current_dir(&dir).arg(&url);
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
