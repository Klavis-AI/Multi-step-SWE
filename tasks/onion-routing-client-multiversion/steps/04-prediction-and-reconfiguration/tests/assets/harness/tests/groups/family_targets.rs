//! Group `family_targets` — how a directory relay presents itself as a
//! channel/circuit target, and the family and subnet predicates that decide
//! whether two such targets may share a circuit.
//!
//! The exit-family guard restriction introduced by this step is assembled from
//! exactly these pieces: a relay's ed25519 identity and the set of relays that
//! mutually declare a family with it.

// The synthetic directory fixtures. They are included here rather than in
// `tests/common/mod.rs` so that a candidate who has not built
// `tor_netdir::testnet` loses only the groups that need a directory; see
// that file's header.
#[path = "netdir_support.rs"]
mod netdir_support;
use tor_linkspec::{ChanTarget, CircTarget};
use tor_netdir::SubnetConfig;

#[test]
fn family_targets__a_relay_is_a_channel_target_with_both_identities() {
    let dir = netdir_support::netdir_custom(|_idx, _nb| {});
    let relay = dir.by_id(&netdir_support::ed_id(7)).expect("relay 7");

    assert_eq!(relay.ed_identity(), &netdir_support::ed_id(7));
    assert_eq!(relay.rsa_identity(), &netdir_support::rsa_id(7));
    // Each test relay advertises exactly one OR port, on port 9001.
    assert_eq!(relay.addrs().len(), 1);
    assert_eq!(relay.addrs()[0].port(), 9001);
}

#[test]
fn family_targets__a_relay_is_a_circuit_target_with_an_ntor_key_and_protocols() {
    let dir = netdir_support::netdir_custom(|_idx, _nb| {});
    let even = dir.by_id(&netdir_support::ed_id(8)).expect("relay 8");
    let odd = dir.by_id(&netdir_support::ed_id(9)).expect("relay 9");

    assert_eq!(
        even.ntor_onion_key().as_bytes(),
        b"----nothing in dirmgr uses this-"
    );
    // Only the even-numbered relays advertise the directory-cache subprotocol.
    assert_eq!(even.protovers().to_string(), "DirCache=2");
    assert_eq!(odd.protovers().to_string(), "");
    // One link specifier for each identity, plus one for the single OR port.
    assert_eq!(even.linkspecs().len(), 3);
}

#[test]
fn family_targets__declared_families_are_symmetric() {
    let dir = netdir_support::netdir_custom(|_idx, _nb| {});
    let r0 = dir.by_id(&netdir_support::ed_id(0)).expect("relay 0");
    let r1 = dir.by_id(&netdir_support::ed_id(1)).expect("relay 1");
    let r2 = dir.by_id(&netdir_support::ed_id(2)).expect("relay 2");

    assert!(r0.in_same_family(&r1));
    assert!(r1.in_same_family(&r0));
    assert!(!r0.in_same_family(&r2));
    // A relay is always its own family member.
    assert!(r0.in_same_family(&r0));
}

#[test]
fn family_targets__the_subnet_configuration_decides_when_addresses_are_too_close() {
    // The test network hands out 0.0.0.3 through 4.0.0.3, cycling every five
    // relays, so relays 0 and 5 share an address and relays 0 and 1 do not
    // share a /16.
    let dir = netdir_support::netdir_custom(|_idx, _nb| {});
    let r0 = dir.by_id(&netdir_support::ed_id(0)).expect("relay 0");
    let r1 = dir.by_id(&netdir_support::ed_id(1)).expect("relay 1");
    let r5 = dir.by_id(&netdir_support::ed_id(5)).expect("relay 5");

    let default_cfg = SubnetConfig::default();
    assert!(r0.in_same_subnet(&r5, &default_cfg));
    assert!(!r0.in_same_subnet(&r1, &default_cfg));

    // A one-bit prefix puts every address below 128.0.0.0 together.
    let coarse = SubnetConfig::new(1, 128);
    assert!(r0.in_same_subnet(&r1, &coarse));

    // A prefix longer than an IPv4 address disables the check entirely.
    let disabled = SubnetConfig::new(33, 129);
    assert!(!r0.in_same_subnet(&r5, &disabled));
}

#[test]
fn family_targets__known_family_members_are_the_mutually_declared_ones() {
    let dir = netdir_support::netdir_custom(|_idx, _nb| {});
    let relay = dir.by_id(&netdir_support::ed_id(20)).expect("relay 20");
    let found: Vec<u8> = dir
        .known_family_members(&relay)
        .map(|other| netdir_support::ed_index(other.id()))
        .collect();
    assert_eq!(found, vec![21]);
}
