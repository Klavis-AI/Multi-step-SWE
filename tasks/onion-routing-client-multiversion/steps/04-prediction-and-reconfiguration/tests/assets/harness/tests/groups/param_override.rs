//! Group `param_override` — replacing the locally overridden consensus
//! parameters of a live `NetDir`.
//!
//! Reconfiguration has to be able to push a new `override_net_params` setting
//! into a directory that is already in use, without rebuilding it and without
//! leaving the previous override in place.

// The synthetic directory fixtures. They are included here rather than in
// `tests/common/mod.rs` so that a candidate who has not built
// `tor_netdir::testnet` loses only the groups that need a directory; see
// that file's header.
#[path = "netdir_support.rs"]
mod netdir_support;

#[test]
fn param_override__an_override_replaces_a_consensus_value() {
    let mut dir = netdir_support::netdir_custom(|_idx, _nb| {});
    // The shipped test consensus votes bwweightscale=1.
    assert_eq!(dir.params().bw_weight_scale.get(), 1);

    dir.replace_overridden_parameters(&netdir_support::net_params("bwweightscale=5678"));
    assert_eq!(dir.params().bw_weight_scale.get(), 5678);
}

#[test]
fn param_override__a_new_override_discards_the_previous_one() {
    let mut dir = netdir_support::netdir_custom_with_params(
        |_idx, _nb| {},
        Some(&netdir_support::net_params("circwindow=500 bwweightscale=7")),
    );
    assert_eq!(dir.params().circuit_window.get(), 500);
    assert_eq!(dir.params().bw_weight_scale.get(), 7);

    dir.replace_overridden_parameters(&netdir_support::net_params("circwindow=900"));
    assert_eq!(dir.params().circuit_window.get(), 900);
    // bwweightscale was not mentioned this time, so it falls back to the
    // consensus value rather than keeping the old override.
    assert_eq!(dir.params().bw_weight_scale.get(), 1);
}

#[test]
fn param_override__an_empty_override_restores_the_consensus_values() {
    let mut dir = netdir_support::netdir_custom_with_params(
        |_idx, _nb| {},
        Some(&netdir_support::net_params("circwindow=500 bwweightscale=7")),
    );
    dir.replace_overridden_parameters(&netdir_support::net_params(""));
    // Back to the consensus value for the one parameter the consensus votes,
    // and back to the built-in default for the one it does not.
    assert_eq!(dir.params().bw_weight_scale.get(), 1);
    assert_eq!(dir.params().circuit_window.get(), 1000);
}

#[test]
fn param_override__unrecognized_keys_are_ignored() {
    let mut dir = netdir_support::netdir_custom(|_idx, _nb| {});
    dir.replace_overridden_parameters(&netdir_support::net_params(
        "not-a-real-parameter=5 circwindow=222",
    ));
    assert_eq!(dir.params().circuit_window.get(), 222);
    assert_eq!(dir.params().bw_weight_scale.get(), 1);
}

#[test]
fn param_override__out_of_range_values_saturate_to_the_declared_bounds() {
    // circwindow is bounded to 100..=1000.
    let mut dir = netdir_support::netdir_custom(|_idx, _nb| {});
    dir.replace_overridden_parameters(&netdir_support::net_params("circwindow=99999"));
    assert_eq!(dir.params().circuit_window.get(), 1000);

    dir.replace_overridden_parameters(&netdir_support::net_params("circwindow=1"));
    assert_eq!(dir.params().circuit_window.get(), 100);
}

#[test]
fn param_override__the_bandwidth_weight_scale_never_drops_below_one() {
    // A zero or negative scale would make the weighting arithmetic divide by
    // zero, so the lower bound of this parameter is one.
    let mut dir = netdir_support::netdir_custom(|_idx, _nb| {});
    dir.replace_overridden_parameters(&netdir_support::net_params("bwweightscale=0"));
    assert_eq!(dir.params().bw_weight_scale.get(), 1);

    dir.replace_overridden_parameters(&netdir_support::net_params("bwweightscale=-20"));
    assert_eq!(dir.params().bw_weight_scale.get(), 1);
}
