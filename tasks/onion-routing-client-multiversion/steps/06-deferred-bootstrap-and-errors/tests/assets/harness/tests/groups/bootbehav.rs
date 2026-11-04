//! Group `bootbehav`: `BootstrapBehavior`, the knob that selects deferred
//! versus lazy bootstrap.
//!
//! instruction.md: "an unbootstrapped `TorClient` with deferred and lazy
//! bootstrap". `OnDemand` is the lazy behaviour (bootstrap when first used);
//! `Manual` is the deferred behaviour (bootstrap only when asked). Either way
//! the client must exist before any bootstrap happens.

use arti_client::{BootstrapBehavior, ErrorKind, HasKind, TorClient};
use tor_rtcompat::BlockOn;

use crate::common::{manual_client, runtime, ClientDirs};

#[test]
fn bootbehav__default_is_on_demand() {
    assert_eq!(BootstrapBehavior::default(), BootstrapBehavior::OnDemand);
}

#[test]
fn bootbehav__variants_are_distinct() {
    assert_ne!(BootstrapBehavior::OnDemand, BootstrapBehavior::Manual);
    assert_eq!(BootstrapBehavior::Manual, BootstrapBehavior::Manual);
}

#[test]
fn bootbehav__is_copy_and_comparable() {
    let a = BootstrapBehavior::Manual;
    let b = a; // Copy: `a` is still usable below.
    assert_eq!(a, b);
    assert_eq!(a, BootstrapBehavior::Manual);
}

#[test]
fn bootbehav__debug_names_the_variant() {
    assert_eq!(format!("{:?}", BootstrapBehavior::OnDemand), "OnDemand");
    assert_eq!(format!("{:?}", BootstrapBehavior::Manual), "Manual");
}

#[test]
fn bootbehav__on_demand_client_is_not_bootstrapped_at_creation() {
    // The default (lazy) behaviour still defers: constructing the client is
    // not what bootstraps it.
    let dirs = ClientDirs::new();
    let rt = runtime();
    let client = TorClient::with_runtime(rt)
        .config(dirs.config())
        .bootstrap_behavior(BootstrapBehavior::OnDemand)
        .create_unbootstrapped()
        .expect("create_unbootstrapped() must not need the network");
    let status = client.bootstrap_status();
    assert_eq!(status.as_frac(), 0.0);
    assert!(!status.ready_for_traffic());
}

#[test]
fn bootbehav__manual_client_does_not_bootstrap_when_used() {
    // With `Manual`, a use attempt must fail instead of silently bootstrapping.
    let dirs = ClientDirs::new();
    let rt = runtime();
    let client = manual_client(rt.clone(), &dirs);
    let err = rt
        .block_on(client.connect("example.com:80"))
        .expect_err("Manual must not bootstrap on use");
    assert_eq!(err.kind(), ErrorKind::BootstrapRequired);
    let status = client.bootstrap_status();
    assert_eq!(status.as_frac(), 0.0);
    assert!(!status.ready_for_traffic());
}
