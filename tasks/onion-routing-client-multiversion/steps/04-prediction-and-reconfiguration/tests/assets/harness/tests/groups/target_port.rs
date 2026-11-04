//! Group `target_port`: the value the port predictor records, hashes, orders
//! and persists.
//!
//! `instruction.md` section 1: "Preemptive circuits built from port
//! prediction".  A predictor has to keep a set of ports, sort them into a
//! stable order and serialise them, so `tor_circmgr::TargetPort` has to be
//! hashable, totally ordered and serde-representable as well as comparable.

use std::collections::{BTreeSet, HashSet};

// The synthetic directory fixtures. They are included here rather than in
// `tests/common/mod.rs` so that a candidate who has not built
// `tor_netdir::testnet` loses only the groups that need a directory; see
// that file's header.
#[path = "netdir_support.rs"]
mod netdir_support;
use tor_circmgr::TargetPort;

#[test]
fn target_port__ipv4_and_ipv6_requests_are_distinct_values() {
    assert_eq!(TargetPort::ipv4(80), TargetPort::ipv4(80));
    assert_ne!(TargetPort::ipv4(80), TargetPort::ipv4(443));
    assert_ne!(TargetPort::ipv4(80), TargetPort::ipv6(80));
    assert_eq!(TargetPort::ipv6(443), TargetPort::ipv6(443));
}

#[test]
fn target_port__is_supported_by_follows_the_relay_exit_policy() {
    let dir = netdir_support::netdir_custom(|_, _| {});

    // The shipped test network gives odd-numbered exits "accept 80,443",
    // even-numbered exits "accept 1-65535", and everything else a reject-all
    // policy.  No relay advertises an IPv6 exit policy.
    let restricted_exit = dir.by_id(&netdir_support::ed_id(11)).expect("relay 11");
    let open_exit = dir.by_id(&netdir_support::ed_id(10)).expect("relay 10");
    let non_exit = dir.by_id(&netdir_support::ed_id(0)).expect("relay 0");

    assert!(TargetPort::ipv4(80).is_supported_by(&restricted_exit));
    assert!(TargetPort::ipv4(443).is_supported_by(&restricted_exit));
    assert!(!TargetPort::ipv4(8080).is_supported_by(&restricted_exit));

    assert!(TargetPort::ipv4(80).is_supported_by(&open_exit));
    assert!(TargetPort::ipv4(8080).is_supported_by(&open_exit));

    assert!(!TargetPort::ipv4(80).is_supported_by(&non_exit));
    assert!(!TargetPort::ipv4(8080).is_supported_by(&non_exit));

    // The IPv6 flag is honoured separately from the port number.
    assert!(!TargetPort::ipv6(80).is_supported_by(&open_exit));
    assert!(!TargetPort::ipv6(443).is_supported_by(&restricted_exit));
}

#[test]
fn target_port__is_hashable_so_a_predictor_can_keep_a_set() {
    let mut set: HashSet<TargetPort> = HashSet::new();
    assert!(set.insert(TargetPort::ipv4(80)));
    assert!(!set.insert(TargetPort::ipv4(80)));
    assert!(set.insert(TargetPort::ipv6(80)));
    assert!(set.insert(TargetPort::ipv4(443)));
    assert_eq!(set.len(), 3);
    assert!(set.contains(&TargetPort::ipv4(443)));
    assert!(!set.contains(&TargetPort::ipv6(443)));
}

#[test]
fn target_port__is_totally_ordered_with_ipv4_before_ipv6() {
    let mut v = vec![
        TargetPort::ipv6(80),
        TargetPort::ipv4(443),
        TargetPort::ipv6(22),
        TargetPort::ipv4(22),
    ];
    v.sort();
    assert_eq!(
        v,
        vec![
            TargetPort::ipv4(22),
            TargetPort::ipv4(443),
            TargetPort::ipv6(22),
            TargetPort::ipv6(80),
        ]
    );

    assert!(TargetPort::ipv4(65535) < TargetPort::ipv6(1));
    assert!(TargetPort::ipv4(80) < TargetPort::ipv4(81));

    // Being `Ord` also means it works in an ordered set.
    let set: BTreeSet<TargetPort> = v.into_iter().collect();
    assert_eq!(set.len(), 4);
    assert_eq!(set.iter().next(), Some(&TargetPort::ipv4(22)));
}

#[test]
fn target_port__round_trips_through_serde() {
    for port in [TargetPort::ipv4(80), TargetPort::ipv6(9001)] {
        let json = serde_json::to_string(&port).expect("serialize");
        let back: TargetPort = serde_json::from_str(&json).expect("deserialize");
        assert_eq!(back, port);
    }

    // The representation names both components, so a stored prediction is
    // readable and the IPv6 flag survives.
    let v: serde_json::Value =
        serde_json::to_value(TargetPort::ipv6(443)).expect("serialize to value");
    assert_eq!(v["port"], serde_json::json!(443));
    assert_eq!(v["ipv6"], serde_json::json!(true));

    let v4: serde_json::Value = serde_json::to_value(TargetPort::ipv4(443)).expect("serialize");
    assert_eq!(v4["ipv6"], serde_json::json!(false));
}

#[test]
fn target_port__is_copy_so_predictions_can_be_recorded_by_value() {
    fn consume(p: TargetPort) -> u16 {
        let cloned = p;
        serde_json::to_value(cloned).expect("serialize")["port"]
            .as_u64()
            .expect("port is an integer") as u16
    }

    let p = TargetPort::ipv4(9050);
    assert_eq!(consume(p), 9050);
    // `p` is still usable: `TargetPort` is `Copy`.
    assert_eq!(consume(p), 9050);
    assert_eq!(p, TargetPort::ipv4(9050));
}
