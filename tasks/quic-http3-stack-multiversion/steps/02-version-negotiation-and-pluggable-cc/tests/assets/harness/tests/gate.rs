//! the step's one gate group: does the stack work at all
//!
//! Exactly one `#[test]`, named `gate_endtoend`. It carries no weight and
//! earns nothing: under `reward = gate x score` a GATE group can only veto,
//! so if this fails the step scores zero however many capability groups
//! passed. Its real job is diagnostic: if it fails, every other T1
//! observation at this step is noise, and the scorecard should be read that
//! way.
//!
//! Nothing here is about *this* milestone. It is the plainest thing the stack
//! can be asked to do -- start the server, fetch a small file with the client,
//! exit zero -- over the candidate's own binaries, with the candidate's own
//! defaults, and no flags at all, because at 0.3.0 the example binaries take
//! none. See REQUIREMENTS.md, "T1 binary CLI eras".

use std::time::Duration;

#[allow(unused_imports)]
use verifier::{pair, support, testkit};

const SHORT: Duration = Duration::from_secs(10);
const LONG: Duration = Duration::from_secs(60);

/// `gate-endtoend`: the candidate's `server` starts, its `client` completes a
/// handshake against it over UDP loopback, retrieves a small file byte for
/// byte, and exits zero.
#[test]
fn gate_endtoend() {
    // Every T1 group here binds the same fixed port, so only one may hold it
    // at a time.
    let _lock = testkit::serial_t1();

    const LEN: usize = 2048;
    let dir = testkit::scratch("gate-endtoend");
    let want = testkit::body(LEN, 0x9a7e_0002);
    let cwd = testkit::example_server_cwd(&dir, &[("gate.bin", want.clone())]);
    let port = testkit::EXAMPLE_SERVER_PORT;

    let server_bin = testkit::server_bin();
    let client_bin = testkit::client_bin();

    let mut sc = testkit::cmd(&server_bin);
    sc.current_dir(&cwd);
    testkit::configure_server(&mut sc, &cwd, port);
    testkit::disable_retry(&mut sc);
    let server = testkit::spawn_server(&mut sc, port, &dir, "gate-server", SHORT);

    let url = format!("https://127.0.0.1:{port}/gate.bin");
    let mut cc = testkit::cmd(&client_bin);
    cc.arg(&url);
    testkit::configure_client(&mut cc);
    let run = testkit::run(&mut cc, LONG, &dir, "gate-client");

    let log = server.log();
    assert!(
        !run.timed_out,
        "expected {} to fetch {url} within {LONG:?}; observed: still running at the deadline.\n\
         --- client stderr ---\n{}\n--- server log ---\n{log}",
        client_bin.display(),
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
    assert!(
        run.stdout_bytes == want,
        "expected the {LEN}-byte body of {url} back byte for byte; observed {} bytes.\n\
         --- client stderr ---\n{}\n--- server log ---\n{log}",
        run.stdout_bytes.len(),
        run.stderr
    );

    drop(server);
}
