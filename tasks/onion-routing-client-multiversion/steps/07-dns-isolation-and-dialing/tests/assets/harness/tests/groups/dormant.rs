//! Group `dormant`: the client-level dormant-mode toggle. `DormantMode`
//! and `TorClient::set_dormant` are the step-07 public surface for
//! suspending the background tasks of a Tor client.
//!
//! Graded items: `arti_client::{DormantMode, TorClient, TorClientConfigBuilder,
//! BootstrapBehavior}`.

use arti_client::config::TorClientConfigBuilder;
use arti_client::{BootstrapBehavior, DormantMode, TorClient};
use tor_error::{ErrorKind, HasKind};
use tor_rtcompat::{BlockOn, PreferredRuntime};

fn offline_client() -> TorClient<PreferredRuntime> {
    let state_dir = tempfile::tempdir().unwrap();
    let cache_dir = tempfile::tempdir().unwrap();
    let cfg = TorClientConfigBuilder::from_directories(&state_dir, &cache_dir)
        .build()
        .unwrap();
    // Keep the temp dirs alive for the lifetime of the process: the client
    // only reads/writes them lazily, and nothing here bootstraps.
    std::mem::forget(state_dir);
    std::mem::forget(cache_dir);
    TorClient::with_runtime(PreferredRuntime::create().unwrap())
        .config(cfg)
        .bootstrap_behavior(BootstrapBehavior::Manual)
        .create_unbootstrapped()
        .unwrap()
}

#[test]
fn dormant__dormant_mode_variants_are_distinct() {
    assert_ne!(DormantMode::Normal, DormantMode::Soft);
    assert_eq!(DormantMode::Soft, DormantMode::Soft);
    assert_eq!(DormantMode::Normal, DormantMode::Normal);
}

#[test]
fn dormant__set_dormant_soft_then_normal_is_idempotent() {
    let client = offline_client();
    client.set_dormant(DormantMode::Soft);
    client.set_dormant(DormantMode::Soft);
    client.set_dormant(DormantMode::Normal);
    client.set_dormant(DormantMode::Normal);
}

#[test]
fn dormant__soft_dormant_client_still_reports_bootstrap_required() {
    let client = offline_client();
    client.set_dormant(DormantMode::Soft);
    let err = client
        .runtime()
        .block_on(client.connect("example.com:80"))
        .err()
        .expect("unbootstrapped client must fail");
    assert_eq!(err.kind(), ErrorKind::BootstrapRequired);
}

#[test]
fn dormant__unbootstrapped_client_rejects_resolve_until_bootstrapped() {
    let client = offline_client();
    let err = client
        .runtime()
        .block_on(client.resolve("example.com"))
        .err()
        .expect("unbootstrapped client must fail");
    assert_eq!(err.kind(), ErrorKind::BootstrapRequired);
}

#[test]
fn dormant__unbootstrapped_client_rejects_ptr_resolve() {
    let client = offline_client();
    let addr = std::net::IpAddr::from([192, 0, 2, 1]);
    let err = client
        .runtime()
        .block_on(client.resolve_ptr(addr))
        .err()
        .expect("unbootstrapped client must fail");
    assert_eq!(err.kind(), ErrorKind::BootstrapRequired);
}
