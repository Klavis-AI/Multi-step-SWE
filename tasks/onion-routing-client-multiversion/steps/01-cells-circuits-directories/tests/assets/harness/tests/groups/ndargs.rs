//! Group `ndargs`: the shared argument types of the directory-document
//! grammar -- port ranges, exit policies, address patterns, relay families
//! and Tor version numbers.

use std::net::IpAddr;

use tor_netdoc::types::family::RelayFamily;
use tor_netdoc::types::policy::{AddrPolicy, AddrPortPattern, PolicyError, PortPolicy, PortRange, RuleKind};
use tor_netdoc::types::version::TorVersion;

fn ip(s: &str) -> IpAddr {
    s.parse().unwrap()
}

#[test]
fn ndargs__port_ranges_are_inclusive_and_reject_empty_spans() {
    let r: PortRange = "22-8000".parse().unwrap();
    assert!(r.contains(22));
    assert!(r.contains(8000));
    assert!(!r.contains(21));
    assert!(!r.contains(8001));
    assert!(!r.is_all());

    // A bare number is a one-port range.
    let one: PortRange = "9001".parse().unwrap();
    assert!(one.contains(9001) && !one.contains(9002));

    assert!(PortRange::new_all().is_all());
    assert!(PortRange::new_all().contains(1));
    assert!(PortRange::new_all().contains(65535));

    // Port 0 is not a usable port, and a range must not run backwards.
    assert!(PortRange::new(0, 10).is_none());
    assert!(PortRange::new(500, 499).is_none());
    assert_eq!(PortRange::new(1, 1).map(|r| r.contains(1)), Some(true));
}

#[test]
fn ndargs__a_reject_policy_is_stored_as_the_complementary_accept_policy() {
    let p: PortPolicy = "reject 300-500".parse().unwrap();
    assert_eq!(p.to_string(), "accept 1-299,501-65535");
    assert!(p.allows_port(299));
    assert!(!p.allows_port(300));
    assert!(!p.allows_port(500));
    assert!(p.allows_port(501));

    let none: PortPolicy = "reject 1-65535".parse().unwrap();
    assert_eq!(none.to_string(), "reject 1-65535");
    assert!(!none.allows_port(443));
    assert_eq!(PortPolicy::new_reject_all().to_string(), "reject 1-65535");
}

#[test]
fn ndargs__accept_policies_keep_their_ranges_and_merge_adjacent_ones() {
    let p: PortPolicy = "accept 21-22,80,443".parse().unwrap();
    for allowed in [21u16, 22, 80, 443] {
        assert!(p.allows_port(allowed), "port {}", allowed);
    }
    for denied in [0u16, 20, 23, 79, 442, 444] {
        assert!(!p.allows_port(denied), "port {}", denied);
    }
    // Adjacent ranges collapse into one.
    let merged: PortPolicy = "accept 100-199,200-299".parse().unwrap();
    assert_eq!(merged.to_string(), "accept 100-299");
}

#[test]
fn ndargs__malformed_policies_are_refused() {
    assert_eq!(
        "allow 1-10".parse::<PortPolicy>().err(),
        Some(PolicyError::InvalidPolicy)
    );
    assert_eq!(
        "accept 700-600".parse::<PortPolicy>().err(),
        Some(PolicyError::InvalidRange)
    );
    assert_eq!(
        "accept 80,ssh".parse::<PortPolicy>().err(),
        Some(PolicyError::InvalidPort)
    );
    // Ranges must be listed in increasing, non-overlapping order.
    assert_eq!(
        "accept 100-200,150-300".parse::<PortPolicy>().err(),
        Some(PolicyError::InvalidPolicy)
    );
    assert!("192.0.2.1".parse::<AddrPortPattern>().is_err());
}

#[test]
fn ndargs__address_policies_apply_the_first_matching_pattern() {
    let pat: AddrPortPattern = "192.0.2.0/24:80-443".parse().unwrap();
    assert!(pat.matches(&ip("192.0.2.17"), 80));
    assert!(pat.matches(&ip("192.0.2.17"), 443));
    assert!(!pat.matches(&ip("192.0.2.17"), 444));
    assert!(!pat.matches(&ip("198.51.100.17"), 80));
    assert!(!pat.matches(&ip("::1"), 80));
    assert_eq!(pat.to_string(), "192.0.2.0/24:80-443");

    let star: AddrPortPattern = "*:*".parse().unwrap();
    assert!(star.matches(&ip("::1"), 1));
    assert!(star.matches_sockaddr(&"198.51.100.3:9001".parse().unwrap()));

    let mut policy = AddrPolicy::new();
    // An empty policy matches nothing at all.
    assert_eq!(policy.allows(&ip("192.0.2.17"), 80), None);
    policy.push(RuleKind::Reject, "192.0.2.0/24:*".parse().unwrap());
    policy.push(RuleKind::Accept, star);
    assert_eq!(
        policy.allows(&ip("192.0.2.17"), 80),
        Some(RuleKind::Reject),
        "the earlier rule wins"
    );
    assert_eq!(
        policy.allows_sockaddr(&"198.51.100.3:80".parse().unwrap()),
        Some(RuleKind::Accept)
    );
}

#[test]
fn ndargs__tor_versions_parse_and_order_by_series_then_status() {
    let stable: TorVersion = "0.4.6.5".parse().unwrap();
    let rc: TorVersion = "0.4.6.5-rc".parse().unwrap();
    let alpha: TorVersion = "0.4.6.5-alpha".parse().unwrap();
    let alpha_dev: TorVersion = "0.4.6.5-alpha-dev".parse().unwrap();
    let older: TorVersion = "0.4.6.4".parse().unwrap();
    let three: TorVersion = "0.4.6".parse().unwrap();

    assert!(alpha < rc && rc < stable);
    assert!(alpha < alpha_dev);
    assert!(older < stable);
    // A three-part version has an implicit patchlevel of zero.
    assert_eq!(three, "0.4.6.0".parse::<TorVersion>().unwrap());
    assert_eq!(stable.to_string(), "0.4.6.5");
    assert_eq!(alpha_dev.to_string(), "0.4.6.5-alpha-dev");

    assert!("0.4".parse::<TorVersion>().is_err());
    assert!("0.4.6.5-alpha-dev-x".parse::<TorVersion>().is_err());
    assert!("zero.4.6.5".parse::<TorVersion>().is_err());
}

#[test]
fn ndargs__relay_families_keep_identities_and_discard_nicknames() {
    let f: RelayFamily = "nickname $AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA \
                          bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
        .parse()
        .unwrap();
    let got: Vec<Vec<u8>> = f.members().map(|id| id.as_bytes().to_vec()).collect();
    assert_eq!(got, vec![vec![0xAAu8; 20], vec![0xBBu8; 20]]);

    let id_a = tor_llcrypto::pk::rsa::RsaIdentity::from_bytes(&[0xAA; 20]).unwrap();
    let id_c = tor_llcrypto::pk::rsa::RsaIdentity::from_bytes(&[0xCC; 20]).unwrap();
    assert!(f.contains(&id_a));
    assert!(!f.contains(&id_c));
    assert_eq!(RelayFamily::new().members().count(), 0);
    assert!(!RelayFamily::new().contains(&id_a));
}
