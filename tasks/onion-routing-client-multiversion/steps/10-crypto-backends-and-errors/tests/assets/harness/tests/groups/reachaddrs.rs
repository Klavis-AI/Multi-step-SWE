//! `reachable_addrs` filtering: address/port patterns, the configuration list
//! that holds them, the guard filter they build, and address restriction.

#![allow(non_snake_case)]

use std::net::SocketAddr;
use std::str::FromStr;

use arti_client::config::TorClientConfig;
use tor_guardmgr::GuardFilter;
use tor_linkspec::ChanTarget;
use tor_linkspec::OwnedChanTarget;
use tor_llcrypto::pk::{ed25519::Ed25519Identity, rsa::RsaIdentity};
use tor_netdoc::types::policy::AddrPortPattern;

fn pat(s: &str) -> AddrPortPattern {
    AddrPortPattern::from_str(s).unwrap_or_else(|e| panic!("{:?} should parse: {}", s, e))
}

fn sock(s: &str) -> SocketAddr {
    SocketAddr::from_str(s).expect("literal socket address")
}

fn target(addrs: &[&str]) -> OwnedChanTarget {
    OwnedChanTarget::new(
        addrs.iter().map(|a| sock(a)).collect(),
        Ed25519Identity::from([0x42_u8; 32]),
        RsaIdentity::from_bytes(&[0x17_u8; 20]).expect("20-byte RSA identity"),
    )
}

#[test]
fn reachaddrs__a_wildcard_pattern_matches_every_address_and_port() {
    let all = AddrPortPattern::new_all();
    assert!(all.matches_sockaddr(&sock("1.2.3.4:443")));
    assert!(all.matches_sockaddr(&sock("[::1]:9001")));
    assert!(all.matches(&"10.0.0.1".parse().expect("ip"), 1));
    assert_eq!(all.to_string(), "*:*");
}

#[test]
fn reachaddrs__a_port_restricted_pattern_rejects_other_ports() {
    let p = pat("*:443");
    assert!(p.matches_sockaddr(&sock("1.2.3.4:443")));
    assert!(!p.matches_sockaddr(&sock("1.2.3.4:9001")));
    assert!(p.matches_sockaddr(&sock("[2001:db8::1]:443")));
}

#[test]
fn reachaddrs__a_subnet_pattern_rejects_addresses_outside_it() {
    let p = pat("10.0.0.0/8:*");
    assert!(p.matches_sockaddr(&sock("10.9.8.7:80")));
    assert!(!p.matches_sockaddr(&sock("11.0.0.1:80")));
    assert!(!p.matches_sockaddr(&sock("[::1]:80")));
}

#[test]
fn reachaddrs__a_port_range_covers_its_endpoints_only() {
    let p = pat("*:80-443");
    assert!(p.matches_sockaddr(&sock("1.1.1.1:80")));
    assert!(p.matches_sockaddr(&sock("1.1.1.1:443")));
    assert!(!p.matches_sockaddr(&sock("1.1.1.1:79")));
    assert!(!p.matches_sockaddr(&sock("1.1.1.1:444")));
}

#[test]
fn reachaddrs__the_path_config_default_permits_everything() {
    let mut b = TorClientConfig::builder();
    assert!(
        b.path_rules().opt_reachable_addrs().is_none(),
        "the list is unset until it is accessed"
    );
    assert_eq!(
        b.path_rules().reachable_addrs().clone(),
        vec![AddrPortPattern::new_all()],
        "the default reachable-address list is the wildcard"
    );
    assert!(
        b.path_rules().opt_reachable_addrs().is_some(),
        "accessing the list resolves its default"
    );
    assert_eq!(
        b.build().expect("explicit wildcard config"),
        TorClientConfig::default(),
        "listing the wildcard explicitly is the default"
    );
}

#[test]
fn reachaddrs__restricting_the_config_changes_it() {
    let mut b = TorClientConfig::builder();
    b.path_rules().reachable_addrs().push(pat("*:443"));
    assert_ne!(
        b.build().expect("restricted config"),
        TorClientConfig::default(),
        "a narrower reachable_addrs list is a different configuration"
    );
}

#[test]
fn reachaddrs__the_guard_filter_records_the_patterns_it_was_given() {
    let unfiltered = GuardFilter::unfiltered();
    assert_eq!(unfiltered, GuardFilter::default());
    let mut restricted = GuardFilter::unfiltered();
    restricted.push_reachable_addresses(vec![pat("*:443")]);
    assert_ne!(
        restricted, unfiltered,
        "pushing a restriction must change the filter"
    );
    let mut same = GuardFilter::unfiltered();
    same.push_reachable_addresses(vec![pat("*:443")]);
    assert_eq!(restricted, same, "equal restrictions compare equal");
    let mut other = GuardFilter::unfiltered();
    other.push_reachable_addresses(vec![pat("*:9001")]);
    assert_ne!(restricted, other);
}

#[test]
fn reachaddrs__restrict_addr_keeps_only_the_chosen_address() {
    let t = target(&["1.2.3.4:443", "[::1]:9001"]);
    let restricted = t
        .restrict_addr(&sock("1.2.3.4:443"))
        .expect("the address belongs to this target");
    assert_eq!(restricted.addrs(), &[sock("1.2.3.4:443")][..]);
    assert_eq!(
        restricted.ed_identity(),
        t.ed_identity(),
        "restricting addresses must not change identity"
    );
}

#[test]
fn reachaddrs__restrict_addr_reports_the_original_when_it_cannot() {
    let t = target(&["1.2.3.4:443"]);
    let err = t
        .restrict_addr(&sock("5.6.7.8:443"))
        .expect_err("that address is not one of the target's");
    assert_eq!(
        err.addrs(),
        t.addrs(),
        "the error carries the unrestricted target back"
    );
}
