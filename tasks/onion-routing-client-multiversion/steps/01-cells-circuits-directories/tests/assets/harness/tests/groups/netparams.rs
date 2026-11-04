//! Group `netparams`: the network parameters a client takes from the
//! consensus, with their bounds and defaults.

use std::convert::TryFrom;
use std::time::Duration;

use tor_netdir::params::NetParameters;
use tor_netdir::{MdReceiver, NetDir, PartialNetDir};
use tor_netdoc::doc::netstatus::NetParams;

use crate::common::{load_consensus, load_mds, partial_netdir};

/// Build a directory from a consensus fixture and every microdescriptor.
fn netdir(consensus: &str) -> NetDir {
    partial_netdir(consensus, "mds-all.txt")
        .unwrap_if_sufficient()
        .unwrap_or_else(|_| panic!("{} should have enough paths", consensus))
}

#[test]
fn netparams__defaults_are_used_for_parameters_nobody_sets() {
    let p = NetParameters::default();
    assert_eq!(p.bw_weight_scale.get(), 10000);
    assert_eq!(p.circuit_window.get(), 1000);
    assert_eq!(
        Duration::try_from(p.circuit_priority_half_life).unwrap(),
        Duration::from_millis(30000)
    );
    assert_eq!(p.extend_by_ed25519_id.get(), 0);
    assert!(!bool::from(p.extend_by_ed25519_id));
    assert_eq!(p.min_circuit_path_threshold.as_percent().get(), 60);
    assert_eq!(p.send_me_accept_min_version.get(), 0);
    assert_eq!(p.send_me_emit_min_version.get(), 0);
}

#[test]
fn netparams__the_consensus_params_line_replaces_the_defaults() {
    let dir = netdir("consensus-md.txt");
    let p = dir.params();
    assert_eq!(p.circuit_window.get(), 888);
    assert_eq!(p.send_me_emit_min_version.get(), 1);
    assert_eq!(p.bw_weight_scale.get(), 10000);
    assert_eq!(p.min_circuit_path_threshold.as_percent().get(), 60);
    assert_eq!(
        Duration::try_from(p.circuit_priority_half_life).unwrap(),
        Duration::from_millis(30000)
    );
    // Parameters the consensus does not mention keep their default.
    assert_eq!(p.send_me_accept_min_version.get(), 0);
    assert_eq!(p.extend_by_ed25519_id.get(), 0);
}

#[test]
fn netparams__values_outside_their_bounds_are_clamped() {
    let dir = netdir("consensus-clamped.txt");
    let p = dir.params();
    // circwindow is bounded at 1000, so 5000 is clamped down...
    assert_eq!(p.circuit_window.get(), 1000);
    // ... and the path threshold is a percentage bounded at 95.
    assert_eq!(p.min_circuit_path_threshold.as_percent().get(), 95);
    assert!((p.min_circuit_path_threshold.as_fraction() - 0.95).abs() < 1e-9);
    // In-range values are taken as they are.
    assert_eq!(p.extend_by_ed25519_id.get(), 1);
    assert!(bool::from(p.extend_by_ed25519_id));
    assert_eq!(p.send_me_accept_min_version.get(), 3);
}

#[test]
fn netparams__parameters_the_client_does_not_know_are_ignored() {
    // The parser keeps the parameter; the client's view of it does not.
    let consensus = load_consensus("consensus-clamped.txt");
    assert_eq!(consensus.params().get("UnrecognizedParam"), Some(&77));

    let dir = netdir("consensus-clamped.txt");
    assert_eq!(dir.params().circuit_window.get(), 1000);
    assert_eq!(dir.relays().count(), 4);
}

#[test]
fn netparams__caller_supplied_overrides_beat_the_consensus() {
    let mut over: NetParams<i32> = NetParams::default();
    over.set("circwindow".to_string(), 250);
    over.set("sendme_emit_min_version".to_string(), 0);
    over.set("NotAParameterAtAll".to_string(), 12);

    let mut partial = PartialNetDir::new(load_consensus("consensus-md.txt"), Some(&over));
    for md in load_mds("mds-all.txt") {
        assert!(partial.add_microdesc(md));
    }
    let dir = partial.unwrap_if_sufficient().expect("sufficient");
    let p = dir.params();
    assert_eq!(p.circuit_window.get(), 250, "override should win");
    assert_eq!(p.send_me_emit_min_version.get(), 0, "override should win");
    // Untouched consensus values survive the override.
    assert_eq!(p.min_circuit_path_threshold.as_percent().get(), 60);
}
