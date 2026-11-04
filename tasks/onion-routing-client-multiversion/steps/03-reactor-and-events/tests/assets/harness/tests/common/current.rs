//! Step-03 fixture helpers that reach for capability THIS step introduces.
//!
//! Kept apart from `common` on purpose. Every generated attribution target
//! compiles the fixture modules its group names, so a helper built on an API
//! the step adds -- here `tor_events` and `arti_config`, neither of which
//! exists at v0.0.1 -- would veto any gate group that shared a module with it,
//! over code that group never calls. `build-package.sh` refuses a gate group
//! that names this module, so the separation cannot quietly lapse.
//!
//! Anything in here may assume the step has been implemented. Anything in
//! `common` may not: it has to compile against the previous checkpoint.
#![allow(dead_code)]

// `live_channel` below is built from the cell-framing helpers in `common`, and
// `proto_reactor` wants nothing from `common` except what it reaches through
// here. Re-exporting means one path -- `crate::current` -- reaches both halves,
// so a group needing one helper from each says `use crate::current as common;`
// once rather than importing two modules that used to be one.
pub use crate::common::*;

use std::collections::HashMap;
use std::sync::{Arc, Mutex, Once};

// =======================================================================
// tor-events: one reactor per process, driven on a dedicated thread
// =======================================================================

static EVENTS_INIT: Once = Once::new();

/// Create the process-wide event reactor (if it does not exist yet) and drive
/// it on a dedicated thread.
///
/// `EventReactor::new()` is a process singleton, so this is idempotent and
/// every case that touches the event system calls it first. The verifier runs
/// one process per group, so no group can observe another group's reactor.
pub fn init_events() {
    EVENTS_INIT.call_once(|| {
        let reactor = tor_events::EventReactor::new()
            .expect("EventReactor::new() returned None on first call in this process");
        std::thread::Builder::new()
            .name("step03-event-reactor".into())
            .spawn(move || futures::executor::block_on(reactor.run()))
            .expect("could not spawn event reactor thread");
    });
}

/// A process-wide lock over the event system's global state.
///
/// `tor-events` keeps its subscriber counts in process-global state, so two
/// cases that subscribe at the same moment would observe each other's
/// subscriptions. Every case that touches the event system holds this guard
/// for its whole body, which makes those cases correct under any number of
/// test threads. It is a plain mutex: no sleep, no timeout, no ordering
/// assumption.
static EVENTS_LOCK: Mutex<()> = Mutex::new(());

/// Initialise the event reactor and take the process-wide event lock.
pub fn events_guard() -> std::sync::MutexGuard<'static, ()> {
    init_events();
    // A case that panics while holding the guard has already failed its group,
    // so poisoning carries no information here.
    EVENTS_LOCK.lock().unwrap_or_else(|e| e.into_inner())
}

/// Obtain a receiver from the process-wide reactor, creating the reactor first
/// if needed.
pub fn event_receiver() -> tor_events::TorEventReceiver {
    init_events();
    tor_events::EventReactor::receiver().expect("EventReactor::receiver() returned None")
}

/// Await exactly one event from a receiver.
///
/// This blocks the calling thread until the reactor forwards an event, or
/// until the receiver's stream ends. There is no timeout and no sleep: the
/// caller subscribes before broadcasting, and `subscribe` activates the
/// receiver, so the event is guaranteed to be queued before this is entered.
pub fn next_event(rx: &mut tor_events::TorEventReceiver) -> Option<tor_events::events::TorEvent> {
    use futures::stream::StreamExt;
    futures::executor::block_on(rx.next())
}

// =======================================================================
// tor-config: variable recognition without touching the environment
// =======================================================================

/// True if `CfgPath` recognises `${name}` as a variable it knows how to
/// expand.
///
/// Expansion itself depends on the invoking user's home and XDG directories,
/// which is not something a conformance case may depend on. Recognition does
/// not: an unknown variable is reported as "unrecognized variable NAME", and any
/// other outcome (success, or a failure to locate the base directories) means
/// the variable was recognised.
pub fn cfg_var_is_recognized(name: &str) -> bool {
    use tor_config::CfgPath;
    let p = CfgPath::new(format!("${{{}}}", name));
    match p.path() {
        Ok(_) => true,
        // The error type itself is not re-exported from `tor_config`, so the
        // distinction is drawn on the documented message text.
        Err(e) => !e.to_string().starts_with("unrecognized variable"),
    }
}

// =======================================================================
// arti-config: loading without reading any file from the real filesystem
// =======================================================================

/// The type `arti_config::load` expects for its (path, required) list when no
/// files are supplied at all.
pub const NO_CONFIG_FILES: &[(&str, bool)] = &[];

/// Load the built-in defaults with no files and no command-line overrides.
pub fn load_defaults() -> config::Config {
    arti_config::load(NO_CONFIG_FILES, Vec::new()).expect("built-in defaults failed to load")
}

/// Load the built-in defaults plus a list of `--option`-style overrides.
pub fn load_with_opts(opts: &[&str]) -> config::Config {
    arti_config::load(
        NO_CONFIG_FILES,
        opts.iter().map(|s| (*s).to_string()).collect(),
    )
    .expect("defaults plus overrides failed to load")
}

/// Write `body` to a file named `name` inside `dir` and return its path.
pub fn write_file(dir: &std::path::Path, name: &str, body: &str) -> std::path::PathBuf {
    let p = dir.join(name);
    std::fs::write(&p, body).expect("could not write test config file");
    p
}

/// Collect a `config::Source` into its flat key/value map.
pub fn collect_source<S: config::Source>(s: &S) -> std::collections::HashMap<String, config::Value> {
    s.collect().expect("Source::collect() failed")
}

// =======================================================================
// tor-proto: a live channel, which THIS step changes the shape of
// =======================================================================
//
// `VerifiedChannel::finish` returns `(Arc<Channel>, Reactor<SplitStream<...>>)`
// at v0.0.1 and `(Channel, Reactor)` at v0.0.2 -- the `Arc` is gone and the
// reactor has lost its type parameter. That transition is part of what step 03
// delivers, so by rule 3 its superseded form is not regression and nothing
// built on either form may sit where a gate group compiles it. These two items
// lived in `common` and cost the gate group `chan_handshake` a clean E0308 on
// `expected Channel, found Arc<Channel>` -- over code `chan_handshake` never
// calls, since it only uses the cell-framing helpers.
/// The channel's reactor, behind a first-party handle.
///
/// `VerifiedChannel::finish` hands back a reactor whose type lives in a
/// private module, so no external crate can name it. It can still be run,
/// which is the only thing a case here wants to do with it.
pub struct ReactorHandle(Box<dyn FnOnce() -> tor_proto::Result<()>>);

impl ReactorHandle {
    /// Run the reactor on this thread until it stops.
    ///
    /// The relay script is finite and the duplex reports end-of-file once it
    /// is drained, so this always returns; it cannot hang a case.
    pub fn run_to_completion(self) -> tor_proto::Result<()> {
        (self.0)()
    }
}

/// Complete a whole channel handshake against the fixture relay and return the
/// live `Channel` and its reactor.
///
/// This is the full chain of TEST_INTERFACE section 7 and section 6.2:
/// `launch` -> `connect` -> `check` -> `finish`. The certificate check is the
/// real one; nothing here is stubbed or bypassed.
pub fn live_channel() -> (tor_proto::channel::Channel, ReactorHandle) {
    let mut script = versions_cell(&[4]);
    script.extend_from_slice(&var_cell_v4(0, CMD_CERTS, &relay_certs_body()));
    script.extend_from_slice(&fixed_cell_v4(0, CMD_NETINFO, &netinfo_body_minimal()));

    let outbuf = Arc::new(Mutex::new(Vec::new()));
    let stream = MsgBuf::new(script, outbuf);
    let hs = tor_proto::channel::ChannelBuilder::new().launch(stream);
    let unverified = futures::executor::block_on(hs.connect())
        .expect("the fixture relay negotiates link protocol 4 and sends CERTS then NETINFO");

    let now = std::time::UNIX_EPOCH + std::time::Duration::from_secs(HANDSHAKE_TIME_SECS);
    let verified = unverified
        .check(&relay_chan_target(), PEER_TLS_CERT, Some(now))
        .expect("the fixture certificate chain authenticates the fixture relay");
    let (chan, reactor) = futures::executor::block_on(verified.finish())
        .expect("a verified channel finishes into a channel and a reactor");

    (
        chan,
        ReactorHandle(Box::new(move || {
            futures::executor::block_on(reactor.run())
        })),
    )
}
