//! Step 11 driver: the peer-set connection limits that `Config` derives from
//! `peerset_initial_target_size`.
//!
//! Format follows the other drivers in this step: integration tests injected into
//! `zebra-network/tests/`.  `Config` and the `zebra_network::constants` module are
//! both public, so everything here is reachable from outside the crate.
//!
//! The three limit accessors and the bias denominator they share do not exist in
//! the predecessor tree, so this file does not compile there.

use zebra_network::{constants, Config};

/// The bias denominator is a small integer: outbound = inbound + inbound / 2.
#[test]
fn outbound_bias_denominator_is_two() {
    assert_eq!(constants::OUTBOUND_PEER_BIAS_DENOMINATOR, 2);
}

/// The inbound limit is exactly the configured target size -- no scaling.
#[test]
fn inbound_limit_equals_the_target_size() {
    let config = Config::default();

    assert_eq!(
        config.peerset_inbound_connection_limit(),
        config.peerset_initial_target_size
    );
}

/// A default `Config` targets 50 peers, so the inbound limit is 50.
#[test]
fn default_config_targets_fifty_peers() {
    let config = Config::default();

    assert_eq!(config.peerset_initial_target_size, 50);
    assert_eq!(config.peerset_inbound_connection_limit(), 50);
}

/// Outbound is inbound plus inbound / 2, so the node prefers its own choices.
#[test]
fn outbound_limit_is_biased_above_the_inbound_limit() {
    let config = Config::default();

    let inbound = config.peerset_inbound_connection_limit();
    let outbound = config.peerset_outbound_connection_limit();

    assert_eq!(outbound, inbound + inbound / constants::OUTBOUND_PEER_BIAS_DENOMINATOR);
    assert!(outbound > inbound, "the bias must favour outbound peers");
}

/// With the default target size the numbers land on 50 inbound and 75 outbound.
#[test]
fn default_limits_are_fifty_inbound_and_seventy_five_outbound() {
    let config = Config::default();

    assert_eq!(config.peerset_inbound_connection_limit(), 50);
    assert_eq!(config.peerset_outbound_connection_limit(), 75);
}

/// The total is simply the two halves added together.
#[test]
fn total_limit_is_inbound_plus_outbound() {
    let config = Config::default();

    assert_eq!(
        config.peerset_total_connection_limit(),
        config.peerset_inbound_connection_limit() + config.peerset_outbound_connection_limit()
    );
    assert_eq!(config.peerset_total_connection_limit(), 125);
}

/// The limits track the configured target size rather than being hard-coded.
#[test]
fn limits_follow_a_configured_target_size() {
    for target_size in [0usize, 1, 2, 3, 4, 10, 7, 100, 1000].iter().copied() {
        let mut config = Config::default();
        config.peerset_initial_target_size = target_size;

        let inbound = config.peerset_inbound_connection_limit();
        let outbound = config.peerset_outbound_connection_limit();

        assert_eq!(inbound, target_size, "inbound limit for target {}", target_size);
        assert_eq!(
            outbound,
            target_size + target_size / constants::OUTBOUND_PEER_BIAS_DENOMINATOR,
            "outbound limit for target {}",
            target_size
        );
        assert_eq!(
            config.peerset_total_connection_limit(),
            inbound + outbound,
            "total limit for target {}",
            target_size
        );
        assert!(outbound >= inbound, "outbound never falls below inbound");
    }
}

/// Integer division truncates: an odd target size does not round the bias up.
#[test]
fn odd_target_sizes_truncate_the_bias() {
    let mut config = Config::default();
    config.peerset_initial_target_size = 3;

    assert_eq!(config.peerset_inbound_connection_limit(), 3);
    assert_eq!(config.peerset_outbound_connection_limit(), 4);
    assert_eq!(config.peerset_total_connection_limit(), 7);

    config.peerset_initial_target_size = 1;

    assert_eq!(config.peerset_inbound_connection_limit(), 1);
    assert_eq!(config.peerset_outbound_connection_limit(), 1);
}

/// Two configs that differ only in the target size really do differ in limits.
#[test]
fn a_larger_target_size_yields_strictly_larger_limits() {
    let mut small = Config::default();
    small.peerset_initial_target_size = 10;
    let mut large = Config::default();
    large.peerset_initial_target_size = 20;

    assert!(
        small.peerset_outbound_connection_limit() < large.peerset_outbound_connection_limit(),
        "a larger peer set must be allowed more outbound connections"
    );
    assert!(
        small.peerset_total_connection_limit() < large.peerset_total_connection_limit(),
        "a larger peer set must be allowed more connections in total"
    );
}
