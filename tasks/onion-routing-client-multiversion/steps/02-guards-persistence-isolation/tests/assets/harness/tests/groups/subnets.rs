//! Group `subnets` - `EnforceDistinctSubnets`.
//!
//! Graded through `tor_netdir::SubnetConfig` and
//! `tor_netdir::Relay::{in_same_subnet, in_same_family}`, plus the effect the
//! configuration has on `tor_circmgr::path::exitpath::ExitPathBuilder`. Two
//! relays whose OR addresses share the configured prefix must be treated as
//! one operator, so a path may not use both.

use std::net::SocketAddr;
use tor_netdir::{NetDir, Relay, SubnetConfig};

/// Parse a subnet configuration from its documented field names.
fn cfg(v4: u8, v6: u8) -> SubnetConfig {
    let text = format!(
        r#"{{"subnets_family_v4":{},"subnets_family_v6":{}}}"#,
        v4, v6
    );
    serde_json::from_str(&text).expect("SubnetConfig did not deserialize")
}

/// Relay number `idx` of the stock test network.
fn relay(nd: &NetDir, idx: u8) -> Relay<'_> {
    nd.by_id(&crate::common::testnet_ed_id(idx))
        .expect("test network is missing a relay")
}

/// The stock test network with every relay also reachable inside 10.0.0.0/16.
fn one_big_subnet() -> NetDir {
    tor_netdir::testnet::construct_custom_netdir(|idx, nb| {
        nb.rs
            .add_or_port(SocketAddr::from(([10, 0, 0, idx as u8], 9001)));
    })
    .expect("could not build the single-subnet test network")
    .unwrap_if_sufficient()
    .expect("single-subnet test network did not have enough paths")
}

#[test]
fn subnets__the_default_configuration_groups_by_a_sixteen_bit_prefix() {
    let nd = crate::common::testnet();
    let dflt = SubnetConfig::default();

    // Relays 0 and 5 share an OR address outright.
    assert!(
        relay(&nd, 0).in_same_subnet(&relay(&nd, 5), &dflt),
        "relays with the same address were not in the same subnet"
    );
    // Relays 0 and 1 differ in the first octet, so they differ in the first
    // sixteen bits too.
    assert!(
        !relay(&nd, 0).in_same_subnet(&relay(&nd, 1), &dflt),
        "relays with different /16 prefixes were treated as one subnet"
    );
    // A /16 default has to agree with the same width spelled out.
    assert!(relay(&nd, 0).in_same_subnet(&relay(&nd, 5), &cfg(16, 32)));
    assert!(!relay(&nd, 0).in_same_subnet(&relay(&nd, 1), &cfg(16, 32)));
}

#[test]
fn subnets__a_shorter_prefix_merges_relays_the_default_separates() {
    let nd = crate::common::testnet();

    // Under a seven-bit prefix, 0.0.0.3 and 1.0.0.3 agree.
    assert!(
        relay(&nd, 0).in_same_subnet(&relay(&nd, 1), &cfg(7, 32)),
        "a seven-bit prefix did not merge two addresses that agree in it"
    );
    // Under a twenty-four bit prefix they still differ ...
    assert!(!relay(&nd, 0).in_same_subnet(&relay(&nd, 1), &cfg(24, 32)));
    // ... while the identical pair stays merged at every usable width.
    for bits in [8_u8, 16, 24, 32] {
        assert!(
            relay(&nd, 0).in_same_subnet(&relay(&nd, 5), &cfg(bits, 128)),
            "identical addresses were separated at a /{} prefix",
            bits
        );
    }
}

#[test]
fn subnets__a_prefix_longer_than_the_address_matches_nothing() {
    let nd = crate::common::testnet();
    // 33 bits cannot be compared in a 32-bit address; the answer is "not the
    // same subnet" rather than a panic or an accidental match.
    assert!(
        !relay(&nd, 0).in_same_subnet(&relay(&nd, 5), &cfg(33, 32)),
        "an over-long IPv4 prefix still reported a match"
    );
    assert!(!relay(&nd, 0).in_same_subnet(&relay(&nd, 0), &cfg(33, 129)));
    // The IPv6 width does not disturb the IPv4 comparison.
    assert!(relay(&nd, 0).in_same_subnet(&relay(&nd, 5), &cfg(16, 129)));
}

#[test]
fn subnets__the_configuration_names_exactly_two_fields() {
    // Both widths are required, and an unrecognised name is refused rather
    // than silently dropped: a typo in a torrc must not disable the check.
    let good: Result<SubnetConfig, _> =
        serde_json::from_str(r#"{"subnets_family_v4":16,"subnets_family_v6":32}"#);
    assert!(good.is_ok(), "the documented field names did not parse");

    let unknown: Result<SubnetConfig, _> = serde_json::from_str(
        r#"{"subnets_family_v4":16,"subnets_family_v6":32,"subnets_family_v5":8}"#,
    );
    assert!(unknown.is_err(), "an unknown field was accepted");

    let empty: Result<SubnetConfig, _> = serde_json::from_str("{}");
    assert!(empty.is_err(), "a configuration with no widths was accepted");
}

#[test]
fn subnets__a_relay_is_always_in_its_own_family() {
    let nd = crate::common::testnet();
    let r0 = relay(&nd, 0);
    let r1 = relay(&nd, 1);
    let r2 = relay(&nd, 2);

    assert!(r0.in_same_family(&r0), "a relay was not in its own family");
    // The test network pairs each relay with its neighbour, and the claim has
    // to be mutual before it counts.
    assert!(r0.in_same_family(&r1), "a declared family pair was missed");
    assert!(r1.in_same_family(&r0), "family membership was not symmetric");
    assert!(
        !r0.in_same_family(&r2),
        "two unrelated relays were reported as family"
    );
}

#[test]
fn subnets__path_building_honours_the_configured_distance() {
    let nd = one_big_subnet();
    let mut rng = crate::common::seeded_rng(0x5117);
    let guards: Option<&tor_guardmgr::GuardMgr<crate::common::TestRt>> = None;

    // Every relay now shares 10.0.0.0/16, so under the default rule no two
    // relays may appear on one path and there is no path at all.
    let err = match tor_circmgr::path::exitpath::ExitPathBuilder::for_any_exit().pick_path(
        &mut rng,
        tor_circmgr::DirInfo::Directory(&nd),
        guards,
        &tor_circmgr::PathConfig::default(),
    ) {
        Ok(_) => panic!("built a path through a network that is one single subnet"),
        Err(e) => e,
    };
    assert!(
        matches!(err, tor_circmgr::Error::NoRelays(_)),
        "expected NoRelays, got {:?}",
        err
    );

    // Narrowing the rule to a full-length prefix makes those relays distinct
    // again, and a three-hop path exists.
    let relaxed = tor_circmgr::PathConfigBuilder::default()
        .enforce_distance(cfg(32, 128))
        .build()
        .expect("PathConfigBuilder::build failed");
    let (path, mon, usable) = tor_circmgr::path::exitpath::ExitPathBuilder::for_any_exit()
        .pick_path(
            &mut rng,
            tor_circmgr::DirInfo::Directory(&nd),
            guards,
            &relaxed,
        )
        .expect("no path even with a full-length subnet prefix");
    assert_eq!(path.len(), 3, "an exit path must have three hops");
    assert!(mon.is_none() && usable.is_none());
}
