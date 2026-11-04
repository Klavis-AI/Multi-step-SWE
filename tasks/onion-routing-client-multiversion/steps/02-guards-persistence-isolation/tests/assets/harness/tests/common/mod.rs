//! Shared helpers for the step-02 conformance groups.
//!
//! Nothing here re-implements a behaviour that is scored: the helpers build
//! inputs (test directories, deterministic RNGs, a network directory) and
//! read raw bytes. Every assertion lives in the group files.
#![allow(dead_code)]
#![allow(unused_imports)]

use rand::rngs::StdRng;
use rand::SeedableRng;

/// A deterministic RNG for every case that needs one.
///
/// `StdRng` is a `CryptoRng`, which the cell encoder requires, and seeding it
/// explicitly keeps the padding bytes reproducible from run to run.
pub fn seeded_rng(seed: u64) -> StdRng {
    StdRng::seed_from_u64(seed)
}

/// Build a temporary directory that is removed when the returned guard drops.
pub fn tempdir() -> tempfile::TempDir {
    tempfile::TempDir::new().expect("could not create a temporary directory")
}

/// A runtime for the scored cases that need one.
///
/// `MockSleepRuntime` never advances its simulated clock unless a case asks it
/// to, so background tasks that wait on a timer (such as the guard manager's
/// periodic maintenance) stay parked and cannot perturb a scored observation.
pub type TestRt = tor_rtmock::MockSleepRuntime<async_executors::TokioTp>;

/// Construct the runtime used by the scored cases.
pub fn runtime() -> TestRt {
    let rt = tor_rtcompat::tokio::create_tokio_runtime().expect("could not create a tokio runtime");
    tor_rtmock::MockSleepRuntime::new(rt)
}

/// The stock 40-relay test network shipped by `tor_netdir::testnet`.
///
/// Relays 0..9 carry no extra flags, 10..19 are exits, 20..29 are guards and
/// 30..39 are both. Identities are `[idx; 32]` (Ed25519) and `[idx; 20]` (RSA).
pub fn testnet() -> tor_netdir::NetDir {
    tor_netdir::testnet::construct_netdir()
        .expect("could not build the test network")
        .unwrap_if_sufficient()
        .expect("test network did not have enough paths")
}

/// Build a test network in which exactly one relay carries the `Guard` flag.
///
/// Sampling from such a network has only one possible outcome, which makes the
/// guard cases deterministic without touching the guard manager's internals.
///
/// `guard_idx` must be even: the stock test network only advertises
/// `DirCache=2` on even-numbered relays, and a guard candidate has to be a
/// directory cache as well as `Guard`-flagged.
pub fn netdir_with_one_guard(guard_idx: usize) -> tor_netdir::NetDir {
    assert_eq!(guard_idx % 2, 0, "the sole guard must be a directory cache");
    use tor_netdoc::doc::netstatus::RelayFlags;
    let base = RelayFlags::RUNNING | RelayFlags::VALID | RelayFlags::V2DIR;
    tor_netdir::testnet::construct_custom_netdir(|idx, nb| {
        if idx == guard_idx {
            nb.rs.set_flags(base | RelayFlags::GUARD);
        } else if (10..20).contains(&idx) || (30..40).contains(&idx) {
            nb.rs.set_flags(base | RelayFlags::EXIT);
        } else {
            nb.rs.set_flags(base);
        }
    })
    .expect("could not build the custom test network")
    .unwrap_if_sufficient()
    .expect("custom test network did not have enough paths")
}

/// The Ed25519 identity that `tor_netdir::testnet` gives relay number `idx`.
pub fn testnet_ed_id(idx: u8) -> tor_llcrypto::pk::ed25519::Ed25519Identity {
    [idx; 32].into()
}
