//! Shared support code for the step-06 conformance driver.
//!
//! Nothing in here asserts anything on its own: it only builds the inputs that
//! the scored groups then make assertions about. Every helper is deterministic
//! and offline - no wall clock, no sockets, no randomness, no sleeps.
#![allow(dead_code)]

use std::path::PathBuf;

use arti_client::config::TorClientConfigBuilder;
use arti_client::{BootstrapBehavior, TorClient, TorClientConfig};
use tor_rtcompat::PreferredRuntime;

/// A scratch area holding the state and cache directories of one `TorClient`.
///
/// Held by the caller for the lifetime of the client so that the directories
/// are not removed while the client still has them open.
pub struct ClientDirs {
    /// Directory passed as the client's `storage.state_dir`.
    pub state: tempfile::TempDir,
    /// Directory passed as the client's `storage.cache_dir`.
    pub cache: tempfile::TempDir,
}

impl ClientDirs {
    /// Create a fresh pair of empty directories.
    pub fn new() -> Self {
        ClientDirs {
            state: tempfile::tempdir().expect("could not create a state tempdir"),
            cache: tempfile::tempdir().expect("could not create a cache tempdir"),
        }
    }

    /// A `TorClientConfig` that keeps all persistent state inside this scratch
    /// area, so that running the suite never touches the invoking user's
    /// real Arti state or cache directories.
    pub fn config(&self) -> TorClientConfig {
        TorClientConfigBuilder::from_directories(self.state.path(), self.cache.path())
            .build()
            .expect("state/cache directory config should always build")
    }
}

impl Default for ClientDirs {
    fn default() -> Self {
        Self::new()
    }
}

/// Build the runtime the driver uses for every client-level group.
pub fn runtime() -> PreferredRuntime {
    PreferredRuntime::create().expect("could not create a PreferredRuntime")
}

/// Build a `TorClient` that will never contact the network on its own.
///
/// `BootstrapBehavior::Manual` is what makes this safe to run offline: the
/// client will not start a bootstrap when it is used, so every operation that
/// needs directory information fails immediately instead of dialing a relay.
pub fn manual_client(rt: PreferredRuntime, dirs: &ClientDirs) -> TorClient<PreferredRuntime> {
    TorClient::with_runtime(rt)
        .config(dirs.config())
        .bootstrap_behavior(BootstrapBehavior::Manual)
        .create_unbootstrapped()
        .expect("create_unbootstrapped() should succeed without any network")
}

/// Write `text` to `name` inside `dir` and return the full path.
pub fn write_file(dir: &tempfile::TempDir, name: &str, text: &str) -> PathBuf {
    let path = dir.path().join(name);
    std::fs::write(&path, text).expect("could not write a configuration file");
    path
}

/// A minimal SOCKS5 greeting offering only the "no authentication" method.
pub const SOCKS5_GREETING_NOAUTH: &[u8] = &[5, 1, 0];

/// The first byte of every HTTP request method that arti's SOCKS listener is
/// required to recognise as a misdirected HTTP request.
///
/// CONNECT, DELETE, GET, HEAD, OPTIONS, POST/PUT/PATCH, TRACE.
pub const HTTP_FIRST_BYTES: &[u8] = b"CDGHOPT";
