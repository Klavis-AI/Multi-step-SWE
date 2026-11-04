//! Group `deferboot`: deferred bootstrap.  A `TorClient` can be constructed
//! without touching the network, and until it is bootstrapped every operation
//! that would need directory material fails with `ErrorKind::BootstrapRequired`
//! rather than blocking or silently succeeding.
//!
//! Inherited tier.  Deferred bootstrap arrived at step 06; step 07's
//! instruction requires it to be preserved, so this group re-runs it against
//! the cumulative tree.  Every item used here is present at both checkpoints,
//! and every assertion made here already held at v0.1.0 -- which is a stronger
//! claim than the first and the one that actually matters for a gate group.
//!
//! Forward `resolve` before bootstrap is NOT here, though it looks like it
//! belongs: it reported `InvalidStreamTarget` at v0.1.0 and only reports
//! `BootstrapRequired` from this step on.  That is a transition step 07
//! delivers, so it is graded in the scored group `resolvedefer`; see its
//! module comment for the one-character cause.
//!
//! Graded items: `arti_client::{BootstrapBehavior, TorClient,
//! TorClientBuilder, TorClient::isolated_client, TorClient::bootstrap_status}`,
//! `arti_client::config::TorClientConfigBuilder::from_directories`, and
//! `tor_error::ErrorKind::BootstrapRequired`.

use std::net::IpAddr;

use arti_client::BootstrapBehavior;
use tor_error::{ErrorKind, HasKind};
use tor_rtcompat::BlockOn;

use crate::common::OfflineClient;

#[test]
fn deferboot__bootstrap_behavior_defaults_to_on_demand() {
    // The default is to bootstrap lazily on first use; deferring bootstrap
    // entirely is the opt-in `Manual` behaviour.
    assert_eq!(BootstrapBehavior::default(), BootstrapBehavior::OnDemand);
    assert_ne!(BootstrapBehavior::OnDemand, BootstrapBehavior::Manual);
    assert_eq!(format!("{:?}", BootstrapBehavior::Manual), "Manual");
    assert_eq!(format!("{:?}", BootstrapBehavior::OnDemand), "OnDemand");
    // It is `Copy`, so it can be handed to a builder by value and reused.
    let b = BootstrapBehavior::Manual;
    let copy = b;
    assert_eq!(b, copy);
}

#[test]
fn deferboot__a_manual_client_is_built_without_any_bootstrap_progress() {
    // Construction must not have bootstrapped anything: progress is zero and
    // the client reports itself as not ready to carry traffic.
    let held = OfflineClient::new();
    let status = held.client.bootstrap_status();
    assert_eq!(status.as_frac(), 0.0, "status was {status:?}");
    assert!(
        !status.ready_for_traffic(),
        "an unbootstrapped client must not claim to be ready: {status:?}"
    );
}

#[test]
fn deferboot__connect_before_bootstrap_fails_with_bootstrap_required() {
    let held = OfflineClient::new();
    let err = held
        .client
        .runtime()
        .block_on(held.client.connect("example.com:80"))
        .err()
        .expect("connecting before bootstrap must fail rather than block");
    assert_eq!(err.kind(), ErrorKind::BootstrapRequired);
    // The failure must not be reported as a network problem: the client never
    // tried to reach the network at all.
    assert_ne!(err.kind(), ErrorKind::TorAccessFailed);
}

#[test]
fn deferboot__reverse_resolution_before_bootstrap_fails_with_bootstrap_required() {
    let held = OfflineClient::new();
    // 192.0.2.1 is in the TEST-NET-1 documentation range, so this address is
    // never routable and the case cannot accidentally touch the network.
    let addr: IpAddr = [192, 0, 2, 1].into();
    let err = held
        .client
        .runtime()
        .block_on(held.client.resolve_ptr(addr))
        .err()
        .expect("reverse resolution before bootstrap must fail");
    assert_eq!(err.kind(), ErrorKind::BootstrapRequired);
}

#[test]
fn deferboot__derived_handles_inherit_the_unbootstrapped_state() {
    // Clones and isolated handles share the same underlying client, so they
    // must report the same refusal rather than each deciding for themselves.
    let held = OfflineClient::new();
    let isolated = held.client.isolated_client();
    let cloned = held.client.clone();

    for (which, client) in [("isolated", &isolated), ("cloned", &cloned)] {
        let err = held
            .client
            .runtime()
            .block_on(client.connect("example.com:443"))
            .err()
            .unwrap_or_else(|| panic!("{which} handle must also refuse to connect"));
        assert_eq!(err.kind(), ErrorKind::BootstrapRequired, "{which} handle");
        assert_eq!(client.bootstrap_status().as_frac(), 0.0, "{which} handle");
    }
}
