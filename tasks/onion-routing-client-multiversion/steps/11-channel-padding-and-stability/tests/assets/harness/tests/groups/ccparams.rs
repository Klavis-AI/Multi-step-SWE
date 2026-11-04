//! Group `ccparams`: the congestion-control and SENDME network parameters
//! carried in `tor_netdir::params::NetParameters`.
//!
//! These are the five `cc_*` values plus the two SENDME version parameters.
//! They are worth grading separately from the rest of the parameter block
//! because each one is stored under a consensus key that looks nothing like its
//! Rust field name (`cc_min_sendme_acks` comes from `cc_bwe_min`,
//! `cc_sendme_cell_ack_count` from `cc_sendme_inc`, and so on). A transcription
//! slip there is invisible: the client keeps working, it just silently ignores
//! what the authorities voted and runs on compiled-in defaults forever. The
//! type wrappers matter too -- a percentage that is read as a fraction is off
//! by a hundred.
#![allow(non_snake_case)]

use tor_netdir::{params::NetParameters, testnet, NetDir};

/// Build the standard test network with an explicit set of consensus params.
fn netdir_with_params(params: &[(&str, i32)]) -> NetDir {
    testnet::construct_custom_netdir_with_params(|_idx, _nb| {}, params.iter().copied())
        .expect("custom test network builds")
        .unwrap_if_sufficient()
        .expect("the test network has enough paths")
}

/// With nothing in the consensus, every congestion-control parameter falls back
/// to its documented default.
#[test]
fn ccparams__defaults_are_the_documented_values() {
    let p = NetParameters::default();

    assert_eq!(p.cc_min_sendme_acks.get(), 5);
    assert_eq!(p.cc_ewma_n_by_sendme_acks.as_percent().get(), 50);
    assert_eq!(p.cc_ewma_n_max.get(), 10);
    assert_eq!(p.cc_sendme_cell_ack_count.get(), 31);
    assert_eq!(p.cc_cwnd_inc_rate.get(), 1);

    assert_eq!(p.sendme_accept_min_version.get(), 0);
    assert_eq!(p.sendme_emit_min_version.get(), 0);
}

/// Each parameter is read from its own consensus key. Setting all of them at
/// once and checking each field catches a mapping that has been crossed over
/// between two parameters as well as one that was simply misspelled.
#[test]
fn ccparams__consensus_keys_map_to_the_right_fields() {
    let dir = netdir_with_params(&[
        ("cc_bwe_min", 7),
        ("cc_ewma_cwnd_pct", 120),
        ("cc_ewma_max", 40),
        ("cc_sendme_inc", 17),
        ("cc_cwnd_inc_rate", 3),
    ]);
    let p = dir.params();

    assert_eq!(p.cc_min_sendme_acks.get(), 7);
    assert_eq!(p.cc_ewma_n_by_sendme_acks.as_percent().get(), 120);
    assert_eq!(p.cc_ewma_n_max.get(), 40);
    assert_eq!(p.cc_sendme_cell_ack_count.get(), 17);
    assert_eq!(p.cc_cwnd_inc_rate.get(), 3);

    // The Rust field names are *not* consensus keys.
    let dir = netdir_with_params(&[("cc_min_sendme_acks", 7), ("cc_sendme_cell_ack_count", 17)]);
    let p = dir.params();
    assert_eq!(p.cc_min_sendme_acks.get(), 5, "still the default");
    assert_eq!(p.cc_sendme_cell_ack_count.get(), 31, "still the default");
}

/// Each parameter has its own range, and a value outside it saturates to the
/// nearest end. The ranges differ per parameter, so this also grades that the
/// right bounds were attached to the right field.
#[test]
fn ccparams__values_saturate_into_their_own_ranges() {
    let low = netdir_with_params(&[
        ("cc_bwe_min", 0),
        ("cc_ewma_cwnd_pct", 0),
        ("cc_ewma_max", 1),
        ("cc_sendme_inc", 0),
        ("cc_cwnd_inc_rate", 0),
    ]);
    let p = low.params();
    assert_eq!(p.cc_min_sendme_acks.get(), 2);
    assert_eq!(p.cc_ewma_n_by_sendme_acks.as_percent().get(), 1);
    assert_eq!(p.cc_ewma_n_max.get(), 2);
    assert_eq!(p.cc_sendme_cell_ack_count.get(), 1);
    assert_eq!(p.cc_cwnd_inc_rate.get(), 1);

    let high = netdir_with_params(&[
        ("cc_bwe_min", 1000),
        ("cc_ewma_cwnd_pct", 1000),
        ("cc_sendme_inc", 1000),
        ("cc_cwnd_inc_rate", 1000),
    ]);
    let p = high.params();
    assert_eq!(p.cc_min_sendme_acks.get(), 20);
    assert_eq!(p.cc_ewma_n_by_sendme_acks.as_percent().get(), 255);
    assert_eq!(p.cc_sendme_cell_ack_count.get(), 255);
    assert_eq!(p.cc_cwnd_inc_rate.get(), 250);

    // `cc_ewma_max` has no practical ceiling, so a large value survives.
    let p = netdir_with_params(&[("cc_ewma_max", i32::MAX)]);
    assert_eq!(p.params().cc_ewma_n_max.get(), i32::MAX);
}

/// The EWMA smoothing parameter is a *percentage* of a congestion window, and
/// may legitimately exceed 100%. Reading it as a fraction is a factor-of-100
/// error in the smoothing constant, so both views are graded.
#[test]
fn ccparams__ewma_is_a_percentage_not_a_fraction() {
    let p = NetParameters::default();
    assert_eq!(p.cc_ewma_n_by_sendme_acks.as_percent().get(), 50);
    assert!((p.cc_ewma_n_by_sendme_acks.as_fraction() - 0.5).abs() < 1e-9);

    let dir = netdir_with_params(&[("cc_ewma_cwnd_pct", 200)]);
    let p = dir.params();
    assert_eq!(p.cc_ewma_n_by_sendme_acks.as_percent().get(), 200);
    assert!(
        (p.cc_ewma_n_by_sendme_acks.as_fraction() - 2.0).abs() < 1e-9,
        "over 100% means more than one congestion window of smoothing"
    );
}

/// The SENDME version parameters are single bytes on the wire, so the i32 from
/// the consensus is clamped into `0..=255` rather than truncated -- truncation
/// would turn 256 into version 0 and quietly downgrade the client.
#[test]
fn ccparams__sendme_versions_clamp_to_one_byte() {
    let dir = netdir_with_params(&[
        ("sendme_accept_min_version", 1),
        ("sendme_emit_min_version", 2),
    ]);
    let p = dir.params();
    assert_eq!(p.sendme_accept_min_version.get(), 1);
    assert_eq!(p.sendme_emit_min_version.get(), 2);

    let dir = netdir_with_params(&[
        ("sendme_accept_min_version", 256),
        ("sendme_emit_min_version", -1),
    ]);
    let p = dir.params();
    assert_eq!(
        p.sendme_accept_min_version.get(),
        255,
        "clamped, not wrapped to 0"
    );
    assert_eq!(p.sendme_emit_min_version.get(), 0);

    let dir = netdir_with_params(&[("sendme_accept_min_version", i32::MAX)]);
    assert_eq!(dir.params().sendme_accept_min_version.get(), 255);
}
