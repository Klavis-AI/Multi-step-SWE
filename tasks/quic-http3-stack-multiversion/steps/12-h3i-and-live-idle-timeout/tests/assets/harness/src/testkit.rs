//! Process, socket and fixture helpers that know nothing about the candidate's
//! API -- only about the filesystem layout the driver sets up and the fact
//! that QUIC runs over UDP.
//!
//! Everything here is deliberately dependency-free: `std` only. A helper that
//! needed a crate would have to resolve inside the verifier image against a
//! registry cache that is frozen per step, and the point of this module is
//! that it is the one file which never has to change.

use std::collections::{BTreeMap, BTreeSet};
use std::ffi::OsStr;
use std::fs;
use std::io::ErrorKind;
use std::net::{Ipv4Addr, SocketAddr, SocketAddrV4, UdpSocket};
use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};
use std::sync::atomic::{AtomicU32, Ordering};
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

// ---------------------------------------------------------------------------
// Where things are
// ---------------------------------------------------------------------------

/// Root of the restored candidate workspace. `/app` unless the driver says
/// otherwise; `val-test.sh` says otherwise when it runs a group against an
/// Oracle worktree outside a container.
pub fn candidate_root() -> PathBuf {
    match std::env::var_os("CANDIDATE_ROOT") {
        Some(v) => PathBuf::from(v),
        None => PathBuf::from("/app"),
    }
}

/// Where the candidate's `cargo build` put its output. The driver builds with
/// an explicit `CARGO_TARGET_DIR` so that the verifier never has to guess
/// between `/app/target` and `/app/quiche/target` after the workspace
/// conversion.
pub fn candidate_target_dir() -> PathBuf {
    match std::env::var_os("CANDIDATE_TARGET_DIR") {
        Some(v) => PathBuf::from(v),
        None => candidate_root().join("target"),
    }
}

/// A file the verifier owns, under `assets/`. Resolved from the harness's own
/// manifest directory, so it is correct wherever the harness was copied to.
pub fn asset(rel: &str) -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("assets").join(rel)
}

/// The verifier's TLS material: a self-signed RSA-2048 leaf for `quic.test`,
/// `localhost` and `127.0.0.1`, valid well past any plausible run date.
///
/// It is the verifier's, not the candidate's, on purpose. Several releases
/// carry example certificates in-tree and several do not, and one of them
/// expired years ago; a group that failed because of that would be reporting
/// on the fixture rather than on the candidate.
pub struct Tls {
    /// PEM certificate chain.
    pub cert: PathBuf,
    /// PEM private key, PKCS#8.
    pub key: PathBuf,
}

/// The verifier's TLS fixture paths. See [`Tls`].
pub fn tls() -> Tls {
    Tls { cert: asset("tls/cert.pem"), key: asset("tls/key.pem") }
}

// ---------------------------------------------------------------------------
// Scratch space
// ---------------------------------------------------------------------------

static SCRATCH_SEQ: AtomicU32 = AtomicU32::new(0);

/// A private directory for one group: document roots, server logs, keylogs,
/// qlog output.
///
/// It is not removed on drop. A failing group's directory is the first thing
/// anyone reads when a run goes wrong, and the container is discarded
/// afterwards anyway.
pub fn scratch(tag: &str) -> PathBuf {
    let root = match std::env::var_os("VERIFIER_SCRATCH") {
        Some(v) => PathBuf::from(v),
        None => std::env::temp_dir().join("verifier"),
    };
    let n = SCRATCH_SEQ.fetch_add(1, Ordering::Relaxed);
    let uniq = SystemTime::now().duration_since(UNIX_EPOCH).map(|d| d.subsec_nanos()).unwrap_or(0);
    let dir = root.join(format!("{tag}-{}-{n}-{uniq:08x}", std::process::id()));
    fs::create_dir_all(&dir).unwrap_or_else(|e| panic!("cannot create scratch {}: {e}", dir.display()));
    dir
}

/// Write a document root and return its path. `files` maps a request path
/// without the leading slash -- `"index.html"` -- to its exact bytes.
///
/// Serving these back byte for byte is what most T1 groups actually check, so
/// the bodies are generated rather than typed: an off-by-one in a length or a
/// truncation at a buffer boundary shows up, and a candidate cannot satisfy
/// the group by hard-coding a short constant.
pub fn docroot(parent: &Path, files: &[(&str, Vec<u8>)]) -> PathBuf {
    let root = parent.join("root");
    for (name, body) in files {
        let p = root.join(name);
        if let Some(d) = p.parent() {
            fs::create_dir_all(d).unwrap_or_else(|e| panic!("cannot create {}: {e}", d.display()));
        }
        fs::write(&p, body).unwrap_or_else(|e| panic!("cannot write {}: {e}", p.display()));
    }
    fs::create_dir_all(&root).ok();
    root
}

/// Stage the working directory the 0.3.0 - 0.9.0 `examples/http3-server`
/// needs, and return it.
///
/// That server takes no arguments: it opens `examples/cert.crt`,
/// `examples/cert.key` and `examples/root` relative to wherever it was
/// started. So rather than write into the candidate's own tree, a group
/// builds a directory with that shape under its scratch space and starts the
/// server there. `files` is staged into `examples/root`.
///
/// Pair this with [`serial_t1`]: the port is compiled in as well.
pub fn example_server_cwd(dir: &Path, files: &[(&str, Vec<u8>)]) -> PathBuf {
    let ex = dir.join("examples");
    let root = ex.join("root");
    fs::create_dir_all(&root)
        .unwrap_or_else(|e| panic!("cannot create {}: {e}", root.display()));
    let t = tls();
    for (from, to) in [(&t.cert, "cert.crt"), (&t.key, "cert.key")] {
        let dst = ex.join(to);
        fs::copy(from, &dst).unwrap_or_else(|e| {
            panic!("cannot stage {} as {}: {e}", from.display(), dst.display())
        });
    }
    for (name, body) in files {
        let f = root.join(name);
        if let Some(d) = f.parent() {
            fs::create_dir_all(d).unwrap_or_else(|e| panic!("cannot create {}: {e}", d.display()));
        }
        fs::write(&f, body).unwrap_or_else(|e| panic!("cannot write {}: {e}", f.display()));
    }
    dir.to_path_buf()
}

/// `len` deterministic bytes. A cheap xorshift keyed by `seed`, so two groups
/// asking for different seeds get different bodies and every run of one group
/// gets the same body.
pub fn body(len: usize, seed: u64) -> Vec<u8> {
    let mut s = seed.wrapping_mul(0x9e37_79b9_7f4a_7c15) | 1;
    let mut out = Vec::with_capacity(len);
    while out.len() < len {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        out.extend_from_slice(&s.to_le_bytes());
    }
    out.truncate(len);
    out
}

// ---------------------------------------------------------------------------
// Finding the candidate's binaries
// ---------------------------------------------------------------------------

/// Every directory a candidate's executable could plausibly land in, in the
/// order the verifier prefers them.
fn bin_dirs() -> Vec<PathBuf> {
    let t = candidate_target_dir();
    let mut v = vec![
        t.join("debug"),
        t.join("debug").join("examples"),
        t.join("release"),
        t.join("release").join("examples"),
    ];
    // Belt and braces for a candidate that ignored CARGO_TARGET_DIR and for
    // the workspace releases, where a member can carry its own target dir.
    let r = candidate_root();
    for base in [r.join("target"), r.join("quiche").join("target"), r.join("apps").join("target")] {
        v.push(base.join("debug"));
        v.push(base.join("debug").join("examples"));
        v.push(base.join("release"));
        v.push(base.join("release").join("examples"));
    }
    v
}

/// First existing executable among `names`, searched across [`bin_dirs`].
///
/// `names` is a list because the same program is called different things at
/// different milestones: the client is `examples/client` at 0.1.0, `client`
/// under `apps/` later, and `quiche-client` later still. A group names all the
/// spellings it will accept, most-preferred first.
///
/// **Name-major, not directory-major.** `names[0]` is searched in every
/// directory before `names[1]` is tried anywhere. The name is the stronger
/// signal: a candidate is free to build one program as a `[[bin]]` and another
/// as an `examples/` target, and a directory-major search would then hand a
/// group the wrong program purely because of where cargo happened to put it.
/// The directory list is only a list of plausible places, in preference order
/// for ties.
///
/// Every step's `TEST_INTERFACE.md` states this ordering to the candidate, so
/// the two must not drift: a directory-major search here silently makes that
/// document false.
pub fn find_bin(names: &[&str]) -> Option<PathBuf> {
    let dirs = bin_dirs();
    for n in names {
        for dir in &dirs {
            let p = dir.join(n);
            if is_exec(&p) {
                return Some(p);
            }
        }
    }
    None
}

fn is_exec(p: &Path) -> bool {
    use std::os::unix::fs::PermissionsExt;
    match fs::metadata(p) {
        Ok(m) => m.is_file() && m.permissions().mode() & 0o111 != 0,
        Err(_) => false,
    }
}

/// [`find_bin`], but a missing binary fails the group with a message that says
/// what was looked for and where.
pub fn need_bin(names: &[&str]) -> PathBuf {
    find_bin(names).unwrap_or_else(|| {
        let dirs: Vec<String> = bin_dirs().iter().map(|d| d.display().to_string()).collect();
        panic!(
            "none of {names:?} was built. Looked in:\n  {}\n\
             The step's instruction lists the binaries the milestone must produce; \
             `cargo build --examples` output counts.",
            dirs.join("\n  ")
        )
    })
}

/// The QUIC client, under any of its names.
pub fn client_bin() -> PathBuf {
    need_bin(&["quiche-client", "client"])
}

/// The QUIC server, under any of its names.
pub fn server_bin() -> PathBuf {
    need_bin(&["quiche-server", "server"])
}

/// The HTTP/3 client, under any of its names. Some milestones fold HTTP/3 into
/// the plain client; a group that can accept either asks [`find_bin`] itself.
pub fn h3_client_bin() -> PathBuf {
    need_bin(&["quiche-client", "http3-client"])
}

/// The HTTP/3 server, under any of its names.
pub fn h3_server_bin() -> PathBuf {
    need_bin(&["quiche-server", "http3-server"])
}

// ---------------------------------------------------------------------------
// Finding the candidate's package manifest
// ---------------------------------------------------------------------------

/// The directory holding the candidate's `quiche` package -- the one a probe
/// crate should point a `path` dependency at.
///
/// `TEST_INTERFACE.md`, "Where your crate has to be", commits to a specific
/// search order: a `Cargo.toml` named `quiche` first at `<root>/quiche`, then
/// at `<root>` itself. Both layouts are legal at every step, so a resolver
/// that only tried one of them would reject a conforming candidate. This
/// tries them in the order the document states and accepts the first that
/// exists, the same "first existing wins" shape as [`find_bin`] for
/// binaries.
pub fn quiche_crate_dir() -> PathBuf {
    let root = candidate_root();
    let nested = root.join("quiche");
    if nested.join("Cargo.toml").is_file() {
        return nested;
    }
    if root.join("Cargo.toml").is_file() {
        return root;
    }
    panic!(
        "no `quiche` package manifest found at {} or {}. TEST_INTERFACE.md's \
         \"Where your crate has to be\" names exactly these two paths; neither \
         has a Cargo.toml.",
        nested.join("Cargo.toml").display(),
        root.join("Cargo.toml").display()
    );
}

// ---------------------------------------------------------------------------
// Ports
// ---------------------------------------------------------------------------

/// A loopback UDP port nothing is listening on right now.
///
/// The usual bind-and-release race is narrowed rather than closed: the port
/// comes from the kernel's ephemeral range and is handed to a server that
/// binds it within milliseconds, and the whole verifier is the only thing
/// running in its container. [`wait_bound`] then confirms the server actually
/// got it, so a lost race fails as "server never bound" and not as a hang.
pub fn free_port() -> u16 {
    let s = UdpSocket::bind((Ipv4Addr::LOCALHOST, 0)).expect("cannot bind loopback UDP");
    s.local_addr().expect("no local addr").port()
}

/// The fixed port the example HTTP/3 server binds at releases 0.3.0 - 0.9.0.
///
/// Those releases' `examples/http3-server` takes no arguments at all: the
/// listen address, the certificate paths and the document root are compiled
/// in. See REQUIREMENTS.md, "T1 binary CLI eras".
pub const EXAMPLE_SERVER_PORT: u16 = 4433;

/// Serialise the groups that have to bind [`EXAMPLE_SERVER_PORT`].
///
/// `cargo test` runs `#[test]` functions on parallel threads, and a hardcoded
/// port cannot be shared. Any group that starts the example server must take
/// this lock as its first statement and hold the guard for the whole group;
/// it is released when the guard drops, including on a panic.
///
/// The lock is an exclusively-created file rather than a `Mutex` so that it
/// still holds if the harness ever splits groups across processes. A run that
/// is killed outright cannot run its destructor, so a lock older than
/// [`STALE_LOCK`] is treated as abandoned and taken over -- otherwise one
/// killed run would wedge every T1 group in every later validation.
///
/// Holding the lock is not the same as holding the port. The previous group's
/// server is a child process killed when its handle drops, and the kill and
/// the socket's release are not the same instant, so taking the lock the
/// moment the file disappears can still find the address occupied -- which a
/// group would then report as a candidate fault. After the lock is ours we
/// therefore wait for the port to actually come free, and say plainly in the
/// failure message that whatever is holding it is not this group's own server.
pub fn serial_t1() -> SerialGuard {
    let path = std::env::temp_dir().join("quiche-verifier-t1.lock");
    let deadline = Instant::now() + Duration::from_secs(600);
    loop {
        match fs::OpenOptions::new().create_new(true).write(true).open(&path) {
            Ok(_) => {
                let g = SerialGuard { path };
                assert!(
                    until(Duration::from_secs(10), || !is_bound(EXAMPLE_SERVER_PORT)),
                    "127.0.0.1:{EXAMPLE_SERVER_PORT} was still bound 10s after this group took \
                     the T1 lock: something other than this group's own server is holding the \
                     address the no-argument example binaries hard-code"
                );
                return g;
            },
            Err(e) if e.kind() == ErrorKind::AlreadyExists => {
                let held_for = fs::metadata(&path)
                    .and_then(|m| m.modified())
                    .and_then(|t| SystemTime::now().duration_since(t).map_err(|_| {
                        std::io::Error::new(ErrorKind::Other, "lock stamped in the future")
                    }))
                    .unwrap_or_default();
                if held_for > STALE_LOCK {
                    let _ = fs::remove_file(&path);
                    continue;
                }
                if Instant::now() >= deadline {
                    panic!(
                        "waited {:?} for the T1 lock at {} and never got it",
                        deadline.elapsed(),
                        path.display()
                    );
                }
                std::thread::sleep(Duration::from_millis(25));
            },
            Err(e) => panic!("cannot take the T1 lock at {}: {e}", path.display()),
        }
    }
}

/// How long a [`serial_t1`] lock may stand before it is assumed abandoned.
pub const STALE_LOCK: Duration = Duration::from_secs(300);

/// Held for as long as a group owns [`EXAMPLE_SERVER_PORT`].
pub struct SerialGuard {
    path: PathBuf,
}

impl Drop for SerialGuard {
    fn drop(&mut self) {
        let _ = fs::remove_file(&self.path);
    }
}

/// A `127.0.0.1:port` address.
pub fn loopback(port: u16) -> SocketAddr {
    SocketAddr::V4(SocketAddrV4::new(Ipv4Addr::LOCALHOST, port))
}

/// True once something holds `port` on loopback.
///
/// Asking the kernel to bind it is the test: success means it is still free,
/// and `AddrInUse` means the server is up. This needs no `ss`, no `/proc`
/// parsing and no sleep-and-hope, and it is why a server-backed group either
/// starts promptly or reports a clean failure.
pub fn is_bound(port: u16) -> bool {
    match UdpSocket::bind((Ipv4Addr::LOCALHOST, port)) {
        Ok(_) => false,
        Err(e) => e.kind() == ErrorKind::AddrInUse,
    }
}

/// Block until `port` is bound, or `dur` elapses. Returns whether it was.
pub fn wait_bound(port: u16, dur: Duration) -> bool {
    let end = Instant::now() + dur;
    while Instant::now() < end {
        if is_bound(port) {
            return true;
        }
        std::thread::sleep(Duration::from_millis(20));
    }
    is_bound(port)
}

// ---------------------------------------------------------------------------
// Running processes
// ---------------------------------------------------------------------------

/// What a finished (or killed) process left behind.
pub struct Run {
    /// Exit status, or `None` if the deadline killed it.
    pub status: Option<std::process::ExitStatus>,
    /// Everything on stdout, decoded as UTF-8; empty when it was not UTF-8.
    pub stdout: String,
    /// Everything on stderr, decoded as UTF-8; empty when it was not UTF-8.
    pub stderr: String,
    /// Everything on stdout, undecoded.
    ///
    /// A group that transfers a binary body cannot use `stdout`: the client
    /// writes the body straight through, so the capture is not UTF-8 and the
    /// decoded form is empty. Compare against these bytes instead.
    pub stdout_bytes: Vec<u8>,
    /// Everything on stderr, undecoded.
    pub stderr_bytes: Vec<u8>,
    /// Whether the deadline killed it.
    pub timed_out: bool,
}

impl Run {
    /// True when the process exited 0.
    pub fn ok(&self) -> bool {
        matches!(self.status, Some(s) if s.success())
    }

    /// stdout and stderr together, for the `assert!` message of a group that
    /// does not care which stream a line came out on.
    pub fn output(&self) -> String {
        format!("{}{}", self.stdout, self.stderr)
    }

    /// Fail the group unless the process exited 0, quoting both streams.
    pub fn expect_ok(&self, what: &str) -> &Run {
        assert!(
            self.ok(),
            "{what} failed (status {:?}, timed_out {})\n--- stdout ---\n{}\n--- stderr ---\n{}",
            self.status, self.timed_out, self.stdout, self.stderr
        );
        self
    }
}

/// Build a `Command` with a clean, reproducible environment.
///
/// `RUST_BACKTRACE` is off and `RUST_LOG` is whatever the group asked for, so
/// that a candidate whose logging is chattier than the reference's cannot
/// flood a group's output, and a group that greps a log line gets the same
/// text on every run.
pub fn cmd<S: AsRef<OsStr>>(program: S) -> Command {
    let mut c = Command::new(program);
    c.env_remove("RUST_BACKTRACE");
    c.env_remove("RUST_LOG");
    c.env("LC_ALL", "C");
    c
}

/// Run `c` to completion under `dur`, capturing both streams to files.
///
/// Files rather than pipes: a candidate that writes more than a pipe buffer
/// before exiting would deadlock a `wait()` on a piped child, and at least one
/// of these releases logs generously.
pub fn run(c: &mut Command, dur: Duration, scratch_dir: &Path, tag: &str) -> Run {
    run_watched(c, dur, scratch_dir, tag, |_| {})
}

/// [`run`], with a chance to look at the process while it is still alive.
///
/// `watch` is called with the child's process ID roughly every 20 ms for as
/// long as the process runs, and once more after it exits. A group that has to
/// observe something transient -- a socket that is only bound for part of the
/// run, a file that is created and removed -- cannot see it from the exit
/// status and the captured streams, so it looks here instead.
///
/// The callback must be cheap and must not panic: it runs inside the wait
/// loop, so a slow one stretches the deadline and a panicking one leaves the
/// child alive.
pub fn run_watched(
    c: &mut Command, dur: Duration, scratch_dir: &Path, tag: &str, mut watch: impl FnMut(u32),
) -> Run {
    let out_p = scratch_dir.join(format!("{tag}.out"));
    let err_p = scratch_dir.join(format!("{tag}.err"));
    let out_f = fs::File::create(&out_p).expect("scratch stdout");
    let err_f = fs::File::create(&err_p).expect("scratch stderr");
    let mut child = c
        .stdin(Stdio::null())
        .stdout(Stdio::from(out_f))
        .stderr(Stdio::from(err_f))
        .spawn()
        .unwrap_or_else(|e| panic!("cannot spawn {:?}: {e}", c.get_program()));

    let pid = child.id();
    let end = Instant::now() + dur;
    let mut status = None;
    let mut timed_out = false;
    loop {
        watch(pid);
        match child.try_wait().expect("try_wait") {
            Some(s) => {
                status = Some(s);
                break;
            },
            None => {
                if Instant::now() >= end {
                    timed_out = true;
                    let _ = child.kill();
                    let _ = child.wait();
                    break;
                }
                std::thread::sleep(Duration::from_millis(20));
            },
        }
    }
    let stdout_bytes = fs::read(&out_p).unwrap_or_default();
    let stderr_bytes = fs::read(&err_p).unwrap_or_default();
    Run {
        status,
        stdout: String::from_utf8(stdout_bytes.clone()).unwrap_or_default(),
        stderr: String::from_utf8(stderr_bytes.clone()).unwrap_or_default(),
        stdout_bytes,
        stderr_bytes,
        timed_out,
    }
}

/// A server process the group started, and the port it was told to use.
///
/// Dropping it kills the process. A group that leaves one running would leak
/// a listener into the next group in the same target, and the two would fight
/// over nothing in particular for the rest of the run.
pub struct Server {
    child: Child,
    port: u16,
    out_p: PathBuf,
    err_p: PathBuf,
}

impl Server {
    /// The address the server was told to listen on.
    pub fn addr(&self) -> SocketAddr {
        loopback(self.port)
    }

    /// The port the server was told to listen on.
    pub fn port(&self) -> u16 {
        self.port
    }

    /// The operating system process ID of the process this handle started.
    ///
    /// For [`udp_local_ports`], which asks the kernel what the process did
    /// rather than asking the process to say what it did.
    pub fn pid(&self) -> u32 {
        self.child.id()
    }

    /// Everything the server has written so far, both streams concatenated.
    pub fn log(&self) -> String {
        let o = fs::read_to_string(&self.out_p).unwrap_or_default();
        let e = fs::read_to_string(&self.err_p).unwrap_or_default();
        format!("{o}{e}")
    }

    /// True while the process is still running.
    pub fn alive(&mut self) -> bool {
        matches!(self.child.try_wait(), Ok(None))
    }

    /// Kill it and return its log.
    pub fn stop(mut self) -> String {
        let _ = self.child.kill();
        let _ = self.child.wait();
        self.log()
    }
}

impl Drop for Server {
    fn drop(&mut self) {
        let _ = self.child.kill();
        let _ = self.child.wait();
    }
}

/// Spawn `c` as a server on `port` and wait until it has bound it.
///
/// Fails the group -- rather than hanging, and rather than letting a client
/// time out against nothing -- if the port is not bound within `dur`, quoting
/// whatever the server managed to say first. "The server exited immediately
/// because it could not read its certificate" is a much better failure message
/// than "the client got no response".
pub fn spawn_server(c: &mut Command, port: u16, scratch_dir: &Path, tag: &str, dur: Duration) -> Server {
    let mut s = spawn_proc(c, scratch_dir, tag);
    s.port = port;
    if !wait_bound(port, dur) {
        let log = s.log();
        let alive = s.alive();
        panic!(
            "server did not bind 127.0.0.1:{port} within {dur:?} (still running: {alive})\n--- server log ---\n{log}"
        );
    }
    s
}

/// Spawn `c` in the background and return a handle that kills it on drop.
///
/// The counterpart to [`spawn_server`] for a process that is not a listener.
/// A T2 group points the candidate's *client* at a socket the verifier owns
/// and then reads what arrives: there is no port of the candidate's to wait
/// for, but the process still has to be killed when the group ends, because
/// a client that has lost its peer keeps retransmitting for its whole idle
/// timeout. `port()` is 0 on the returned handle and `addr()` means nothing;
/// `log()`, `alive()`, `stop()` and the kill on drop work as they do for a
/// server.
pub fn spawn_proc(c: &mut Command, scratch_dir: &Path, tag: &str) -> Server {
    let out_p = scratch_dir.join(format!("{tag}.out"));
    let err_p = scratch_dir.join(format!("{tag}.err"));
    let out_f = fs::File::create(&out_p).expect("scratch stdout");
    let err_f = fs::File::create(&err_p).expect("scratch stderr");
    let child = c
        .stdin(Stdio::null())
        .stdout(Stdio::from(out_f))
        .stderr(Stdio::from(err_f))
        .spawn()
        .unwrap_or_else(|e| panic!("cannot spawn {:?}: {e}", c.get_program()));

    Server { child, port: 0, out_p, err_p }
}

/// Every local UDP port the process `pid` currently holds a bound socket on.
///
/// Reads `/proc/<pid>/fd` for the process's own socket inodes and resolves
/// them against `/proc/net/udp` and `/proc/net/udp6`. Both steps are ordinary
/// reads of a process the group started itself, so this needs no privileges,
/// no capture and no cooperation from the candidate -- which is the point. A
/// group that wants to know whether an implementation really opened a second
/// socket cannot ask it, and must not settle for a log line saying it did.
///
/// Returns an empty set once the process has exited, and on any platform
/// without procfs; a group that calls it exactly once, after the run, will
/// therefore see nothing. Call it from [`run_watched`].
pub fn udp_local_ports(pid: u32) -> BTreeSet<u16> {
    let mut inodes = BTreeSet::new();
    let fd_dir = PathBuf::from(format!("/proc/{pid}/fd"));
    let entries = match fs::read_dir(&fd_dir) {
        Ok(v) => v,
        Err(_) => return BTreeSet::new(),
    };
    for e in entries.flatten() {
        // The process can close a descriptor between the listing and the
        // readlink; that is not an error, it is just a descriptor we missed.
        if let Ok(target) = fs::read_link(e.path()) {
            let t = target.to_string_lossy();
            if let Some(rest) = t.strip_prefix("socket:[") {
                if let Some(n) = rest.strip_suffix(']') {
                    if let Ok(ino) = n.parse::<u64>() {
                        inodes.insert(ino);
                    }
                }
            }
        }
    }
    if inodes.is_empty() {
        return BTreeSet::new();
    }

    let mut ports = BTreeSet::new();
    for table in ["/proc/net/udp", "/proc/net/udp6"] {
        let text = match fs::read_to_string(table) {
            Ok(v) => v,
            Err(_) => continue,
        };
        // sl  local_address rem_address st tx_queue:rx_queue tr:tm->when
        // retrnsmt uid timeout inode ...  -- local_address is field 1 and
        // inode is field 9, both counting the leading `sl` as field 0.
        for line in text.lines().skip(1) {
            let f: Vec<&str> = line.split_whitespace().collect();
            if f.len() < 10 {
                continue;
            }
            let ino: u64 = match f[9].parse() {
                Ok(v) => v,
                Err(_) => continue,
            };
            if !inodes.contains(&ino) {
                continue;
            }
            let port_hex = match f[1].rsplit(':').next() {
                Some(v) => v,
                None => continue,
            };
            if let Ok(port) = u16::from_str_radix(port_hex, 16) {
                ports.insert(port);
            }
        }
    }
    ports
}

// ---------------------------------------------------------------------------
// Raw UDP, for the groups that speak QUIC themselves
// ---------------------------------------------------------------------------

/// A loopback UDP socket the verifier drives directly: the T2 surface.
///
/// A group builds a packet with [`crate::support`], sends it here, and reads
/// what the candidate's server sends back. The candidate's library is not
/// involved in either direction, which is the whole point.
pub struct Endpoint {
    sock: UdpSocket,
}

impl Endpoint {
    /// Bind an ephemeral loopback port.
    pub fn new() -> Endpoint {
        let sock = UdpSocket::bind((Ipv4Addr::LOCALHOST, 0)).expect("cannot bind loopback UDP");
        sock.set_read_timeout(Some(Duration::from_millis(200))).expect("set_read_timeout");
        Endpoint { sock }
    }

    /// This endpoint's address, for a candidate that needs to be told where to
    /// reply.
    pub fn local_addr(&self) -> SocketAddr {
        self.sock.local_addr().expect("no local addr")
    }

    /// Send one datagram.
    pub fn send_to(&self, buf: &[u8], to: SocketAddr) {
        let n = self.sock.send_to(buf, to).unwrap_or_else(|e| panic!("send_to {to}: {e}"));
        assert_eq!(n, buf.len(), "short UDP send: {n} of {}", buf.len());
    }

    /// Wait up to `dur` for one datagram.
    pub fn recv(&self, dur: Duration) -> Option<Vec<u8>> {
        let end = Instant::now() + dur;
        let mut buf = vec![0u8; 65535];
        loop {
            match self.sock.recv_from(&mut buf) {
                Ok((n, _)) => {
                    buf.truncate(n);
                    return Some(buf);
                },
                Err(e) if e.kind() == ErrorKind::WouldBlock || e.kind() == ErrorKind::TimedOut => {
                    if Instant::now() >= end {
                        return None;
                    }
                },
                Err(e) if e.kind() == ErrorKind::ConnectionRefused => {
                    // Loopback ICMP port-unreachable from a server that has
                    // already exited. Keep waiting; the deadline decides.
                    if Instant::now() >= end {
                        return None;
                    }
                },
                Err(e) => panic!("recv_from: {e}"),
            }
        }
    }

    /// Collect every datagram that arrives within `dur`.
    pub fn recv_all(&self, dur: Duration) -> Vec<Vec<u8>> {
        let end = Instant::now() + dur;
        let mut v = Vec::new();
        while Instant::now() < end {
            match self.recv(end.saturating_duration_since(Instant::now())) {
                Some(d) => v.push(d),
                None => break,
            }
        }
        v
    }

    /// Wait up to `dur` for a datagram that `pred` accepts, discarding the
    /// rest. Returns the matching datagram and everything discarded before it.
    pub fn recv_matching(
        &self, dur: Duration, pred: impl Fn(&[u8]) -> bool,
    ) -> (Option<Vec<u8>>, Vec<Vec<u8>>) {
        let end = Instant::now() + dur;
        let mut skipped = Vec::new();
        while Instant::now() < end {
            match self.recv(end.saturating_duration_since(Instant::now())) {
                Some(d) => {
                    if pred(&d) {
                        return (Some(d), skipped);
                    }
                    skipped.push(d);
                },
                None => break,
            }
        }
        (None, skipped)
    }
}

impl Default for Endpoint {
    fn default() -> Self {
        Endpoint::new()
    }
}

// ---------------------------------------------------------------------------
// Small conveniences
// ---------------------------------------------------------------------------

/// Lowercase hex, for assertion messages about packets.
pub fn hex(b: &[u8]) -> String {
    let mut s = String::with_capacity(b.len() * 2);
    for x in b {
        s.push_str(&format!("{x:02x}"));
    }
    s
}

/// Parse `key=value` lines -- the shape most of these releases use for their
/// `--help` output, their qlog `common_fields`, and their stats dumps -- into
/// a map, ignoring anything that is not a pair.
pub fn kv(text: &str) -> BTreeMap<String, String> {
    let mut m = BTreeMap::new();
    for line in text.lines() {
        if let Some((k, v)) = line.split_once('=') {
            m.insert(k.trim().to_string(), v.trim().to_string());
        }
    }
    m
}

/// Poll `f` until it returns true or `dur` elapses. Returns whether it did.
///
/// For the handful of observations that are genuinely asynchronous -- a log
/// line appearing, a file being written -- and never as a substitute for
/// waiting on something the kernel can tell us about.
pub fn until(dur: Duration, mut f: impl FnMut() -> bool) -> bool {
    let end = Instant::now() + dur;
    loop {
        if f() {
            return true;
        }
        if Instant::now() >= end {
            return false;
        }
        std::thread::sleep(Duration::from_millis(20));
    }
}
