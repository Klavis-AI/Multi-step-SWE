//! Group `netparams` - consensus network parameters.
//!
//! Graded through `tor_netdir::params::NetParameters` and
//! `tor_netdir::NetDir::params()`. These are the knobs the guard algorithm and
//! the circuit-build-timeout estimator read: the defaults have to match the
//! values a client uses when the consensus is silent, the consensus key names
//! have to be the ones directory authorities publish, and an out-of-range
//! value has to be clamped rather than accepted or rejected.

use std::convert::TryInto;
use std::time::Duration;
use tor_netdir::params::NetParameters;
use tor_netdoc::doc::netstatus::NetParams;

/// Build a consensus parameter map from a flat JSON object of name to value.
///
/// A directory publishes parameters on the consensus's `params` line, in
/// `Name=Value` text; this helper is a test convenience that reaches the same
/// type through the other door it has — `serde_json`, i.e. the `Deserialize`
/// impl over a flat map — so that a case can state its parameter set as data.
/// `TEST_INTERFACE.md` §4.1 names that impl as part of the contract, because a
/// `NetParams` that only implements `get`/`set`/`Default` cannot compile this
/// group.
fn published(json: &str) -> NetParams<i32> {
    serde_json::from_str(json).expect("consensus parameters did not deserialize")
}

/// Seconds in the `Duration` that `d` converts to.
fn secs(d: Duration) -> u64 {
    d.as_secs()
}

#[test]
fn netparams__the_defaults_are_the_values_a_silent_consensus_implies() {
    let p = NetParameters::default();
    assert_eq!(p.bw_weight_scale.get(), 10_000, "bwweightscale default");
    assert_eq!(p.circuit_window.get(), 1_000, "circwindow default");
    assert_eq!(p.extend_by_ed25519_id.get(), 0, "ExtendByEd25519ID default");
    assert_eq!(p.sendme_accept_min_version.get(), 0);
    assert_eq!(p.sendme_emit_min_version.get(), 0);
    assert_eq!(
        p.min_circuit_path_threshold.as_percent().get(),
        60,
        "min_paths_for_circs_pct default"
    );
    let half_life: Duration = p
        .circuit_priority_half_life
        .try_into()
        .expect("CircuitPriorityHalflifeMsec did not convert to a Duration");
    assert_eq!(half_life, Duration::from_millis(30_000));
}

#[test]
fn netparams__the_guard_defaults_match_the_guard_algorithm() {
    let p = NetParameters::default();
    assert_eq!(p.guard_n_primary.get(), 3, "guard-n-primary-guards default");
    assert_eq!(p.guard_use_parallelism.get(), 1);
    assert_eq!(p.guard_dir_use_parallelism.get(), 3);
    assert_eq!(p.guard_max_sample_size.get(), 60);
    assert_eq!(p.guard_filtered_min_sample_size.get(), 20);
    assert_eq!(p.guard_max_sample_threshold.as_percent().get(), 20);
    assert_eq!(p.guard_meaningful_restriction.as_percent().get(), 20);
    assert_eq!(p.guard_extreme_restriction.as_percent().get(), 1);

    // The guard lifetimes are expressed in days and confirmed guards are kept
    // for a shorter time than unconfirmed ones.
    let unconfirmed: Duration = p
        .guard_lifetime_unconfirmed
        .try_into()
        .expect("guard lifetime did not convert to a Duration");
    let confirmed: Duration = p
        .guard_lifetime_confirmed
        .try_into()
        .expect("guard lifetime did not convert to a Duration");
    assert_eq!(secs(unconfirmed), 120 * 86_400);
    assert_eq!(secs(confirmed), 60 * 86_400);

    let idle: Duration = p.guard_nonprimary_idle_timeout.try_into().expect("idle");
    let connect: Duration = p
        .guard_nonprimary_connect_timeout
        .try_into()
        .expect("connect");
    assert_eq!(secs(idle), 600);
    assert_eq!(secs(connect), 15);
}

#[test]
fn netparams__the_timeout_estimator_defaults_are_in_their_stated_units() {
    let p = NetParameters::default();
    assert_eq!(p.cbt_learning_disabled.get(), 0);
    assert_eq!(p.cbt_num_xm_modes.get(), 10);
    assert_eq!(p.cbt_success_count.get(), 20);
    assert_eq!(p.cbt_max_timeouts.get(), 18);
    assert_eq!(p.cbt_min_circs_for_estimate.get(), 100);
    assert_eq!(p.cbt_max_open_circuits_for_testing.get(), 10);
    assert_eq!(p.cbt_timeout_quantile.as_percent().get(), 80);
    assert_eq!(p.cbt_abandon_quantile.as_percent().get(), 99);
    assert!((p.cbt_timeout_quantile.as_fraction() - 0.80).abs() < 1e-9);

    // Milliseconds, seconds and days each convert to the same `Duration`.
    let min: Duration = p.cbt_min_timeout.try_into().expect("cbtmintimeout");
    let initial: Duration = p.cbt_initial_timeout.try_into().expect("cbtinitialtimeout");
    let delay: Duration = p.cbt_testing_delay.try_into().expect("cbttestfreq");
    assert_eq!(min, Duration::from_millis(10));
    assert_eq!(initial, Duration::from_secs(60));
    assert_eq!(delay, Duration::from_secs(10));

    let steady: Duration = p.unused_client_circ_timeout.try_into().expect("steady");
    let learning: Duration = p
        .unused_client_circ_timeout_while_learning_cbt
        .try_into()
        .expect("learning");
    assert_eq!(secs(steady), 1_800);
    assert_eq!(secs(learning), 180);
}

#[test]
fn netparams__published_values_override_the_defaults_by_key_name() {
    let p = NetParameters::from_map(&published(
        r#"{"circwindow":500,
            "guard-n-primary-guards":5,
            "ExtendByEd25519ID":1,
            "nf_conntimeout_clients":900,
            "cbtquantile":70,
            "cbtinitialtimeout":45000,
            "guard-lifetime-days":30,
            "sendme_accept_min_version":1}"#,
    ));
    assert_eq!(p.circuit_window.get(), 500);
    assert_eq!(p.guard_n_primary.get(), 5);
    assert_eq!(p.extend_by_ed25519_id.get(), 1);
    assert_eq!(p.cbt_timeout_quantile.as_percent().get(), 70);
    assert_eq!(p.sendme_accept_min_version.get(), 1);

    let steady: Duration = p.unused_client_circ_timeout.try_into().expect("steady");
    assert_eq!(secs(steady), 900);
    let initial: Duration = p.cbt_initial_timeout.try_into().expect("initial");
    assert_eq!(initial, Duration::from_millis(45_000));
    let life: Duration = p.guard_lifetime_unconfirmed.try_into().expect("lifetime");
    assert_eq!(secs(life), 30 * 86_400);

    // Anything the consensus did not mention keeps its default.
    assert_eq!(p.bw_weight_scale.get(), 10_000);
    assert_eq!(p.guard_dir_use_parallelism.get(), 3);
}

#[test]
fn netparams__out_of_range_values_are_clamped_and_unknown_keys_ignored() {
    // circwindow is bounded to 100..=1000 and cbtmaxopencircs to 0..=14.
    let low = NetParameters::from_map(&published(
        r#"{"circwindow":1,"cbtmaxopencircs":-5,"min_paths_for_circs_pct":0}"#,
    ));
    assert_eq!(low.circuit_window.get(), 100, "a low value did not clamp up");
    assert_eq!(low.cbt_max_open_circuits_for_testing.get(), 0);
    assert_eq!(low.min_circuit_path_threshold.as_percent().get(), 25);

    let high = NetParameters::from_map(&published(
        r#"{"circwindow":100000,"cbtmaxopencircs":99,"min_paths_for_circs_pct":100}"#,
    ));
    assert_eq!(
        high.circuit_window.get(),
        1_000,
        "a high value did not clamp down"
    );
    assert_eq!(high.cbt_max_open_circuits_for_testing.get(), 14);
    assert_eq!(high.min_circuit_path_threshold.as_percent().get(), 95);

    // A parameter this client does not recognise must not disturb the ones it
    // does, and must not be an error.
    let mixed = NetParameters::from_map(&published(
        r#"{"NotAParameterAnyoneKnows":7,"circwindow":700}"#,
    ));
    assert_eq!(mixed.circuit_window.get(), 700);
}

#[test]
fn netparams__a_network_directory_exposes_its_own_parameters() {
    // The stock test consensus publishes bwweightscale=1 and nothing else, so
    // `NetDir::params()` has to show that one override over the defaults.
    let nd = crate::common::testnet();
    let p = nd.params();
    assert_eq!(
        p.bw_weight_scale.get(),
        1,
        "NetDir::params() did not report the consensus value"
    );
    assert_eq!(p.circuit_window.get(), 1_000, "an unset key changed value");
    assert_eq!(p.guard_n_primary.get(), 3);
}
