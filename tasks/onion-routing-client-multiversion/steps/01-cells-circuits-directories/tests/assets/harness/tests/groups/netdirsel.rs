//! Group `netdirsel`: assembling a directory from a consensus plus
//! microdescriptors, and the relay view it exposes.

use tor_linkspec::{ChanTarget, CircTarget};
use tor_netdir::{MdReceiver, NetDir, PartialNetDir, Relay};
use tor_netdoc::doc::microdesc::Microdesc;
use tor_netdoc::doc::netstatus::RouterStatus;

use crate::common::{fixture_text, load_consensus, load_mds, partial_netdir, test_netdir};

/// The RSA identity the fixture consensus gives each relay.
fn rsa_of(nick: &str) -> [u8; 20] {
    let b = match nick {
        "guardy" => 0x11,
        "exity" => 0x22,
        "middly" => 0x33,
        "bothy" => 0x44,
        other => panic!("no such fixture relay: {}", other),
    };
    [b; 20]
}

/// Find a relay of the fixture directory by nickname.
fn relay<'a>(dir: &'a NetDir, nick: &str) -> Relay<'a> {
    let id = rsa_of(nick);
    dir.relays()
        .find(|r| r.rsa_id().as_bytes() == id)
        .unwrap_or_else(|| panic!("no relay {} in directory", nick))
}

#[test]
fn netdirsel__a_partial_directory_asks_for_the_microdescriptors_it_lacks() {
    let consensus = load_consensus("consensus-md.txt");
    let want: Vec<[u8; 32]> = consensus.relays().iter().map(|rs| *rs.md_digest()).collect();
    let mut partial = PartialNetDir::new(consensus, None);

    let mut missing: Vec<[u8; 32]> = partial.missing_microdescs().copied().collect();
    missing.sort_unstable();
    let mut want_sorted = want.clone();
    want_sorted.sort_unstable();
    assert_eq!(missing, want_sorted);

    // A microdescriptor named by the consensus is wanted.
    let mds = load_mds("mds-all.txt");
    assert!(partial.add_microdesc(mds[0].clone()));
    assert_eq!(partial.missing_microdescs().count(), 3);

    // One that no router status names is not.
    let text = fixture_text("md-exity.txt").replace("accept 80,443", "accept 80,444");
    let stranger = Microdesc::parse(&text).expect("modified microdesc should parse");
    assert!(!want.contains(stranger.digest()));
    assert!(!partial.add_microdesc(stranger));
    assert_eq!(partial.missing_microdescs().count(), 3);
}

#[test]
fn netdirsel__a_directory_missing_a_microdescriptor_is_not_usable() {
    let consensus = load_consensus("consensus-md.txt");
    let middly = consensus
        .relays()
        .iter()
        .find(|rs| rs.nickname() == "middly")
        .map(|rs| *rs.md_digest())
        .expect("fixture consensus lists middly");

    let partial = partial_netdir("consensus-md.txt", "mds-partial.txt");
    let missing: Vec<[u8; 32]> = partial.missing_microdescs().copied().collect();
    assert_eq!(missing, vec![middly]);
    assert!(!partial.have_enough_paths());

    let partial = partial
        .unwrap_if_sufficient()
        .err()
        .expect("one missing microdescriptor is not enough");
    assert_eq!(partial.missing_microdescs().count(), 1);
    assert_eq!(
        partial.lifetime().valid_after(),
        load_consensus("consensus-md.txt").lifetime().valid_after()
    );
}

#[test]
fn netdirsel__a_complete_directory_lists_every_relay_in_the_consensus() {
    let dir = test_netdir();
    assert_eq!(dir.missing_microdescs().count(), 0);
    assert_eq!(dir.relays().count(), 4);

    let mut ids: Vec<Vec<u8>> = dir.relays().map(|r| r.rsa_id().as_bytes().to_vec()).collect();
    ids.sort();
    let mut want: Vec<Vec<u8>> = ["guardy", "exity", "middly", "bothy"]
        .iter()
        .map(|n| rsa_of(n).to_vec())
        .collect();
    want.sort();
    assert_eq!(ids, want);
    assert_eq!(
        dir.lifetime().valid_after(),
        load_consensus("consensus-md.txt").lifetime().valid_after()
    );
}

#[test]
fn netdirsel__a_relay_joins_its_router_status_to_its_microdescriptor() {
    let dir = test_netdir();
    let mds = load_mds("mds-all.txt");
    for (i, nick) in ["guardy", "exity", "middly", "bothy"].iter().enumerate() {
        let r = relay(&dir, nick);
        // Addresses and identities come from the consensus...
        assert_eq!(r.addrs().len(), 1);
        assert_eq!(
            r.addrs()[0],
            format!("10.0.0.{}:{}", i + 1, 9001 + i).parse().unwrap()
        );
        assert_eq!(r.rsa_identity().as_bytes(), &rsa_of(nick)[..]);
        // ... and the onion key and ed25519 identity from the microdescriptor.
        assert_eq!(r.ntor_onion_key().as_bytes(), mds[i].ntor_key().as_bytes());
        assert_eq!(r.ed_identity().as_bytes(), mds[i].ed25519_id().as_bytes());
        assert_eq!(r.id(), r.ed_identity());
        assert!(r.protovers().supports_known_subver(
            tor_protover::ProtoKind::DirCache,
            2
        ));
    }
}

#[test]
fn netdirsel__relays_are_identified_by_their_two_identity_keys() {
    let dir = test_netdir();
    let guardy = relay(&dir, "guardy");
    let exity = relay(&dir, "exity");

    assert_eq!(guardy.rsa_id().as_bytes(), &[0x11u8; 20]);
    assert_eq!(exity.rsa_id().as_bytes(), &[0x22u8; 20]);
    assert!(guardy.same_relay(&relay(&dir, "guardy")));
    assert!(!guardy.same_relay(&exity));

    // A relay is always in its own family; the fixture relays list only an
    // outside identity, so no two of them are mutual family members.
    assert!(guardy.in_same_family(&guardy));
    assert!(!guardy.in_same_family(&exity));
    assert!(!exity.in_same_family(&guardy));
}

#[test]
fn netdirsel__exit_support_comes_from_the_microdescriptor_policy() {
    let dir = test_netdir();
    assert!(relay(&dir, "exity").supports_exit_port_ipv4(443));
    assert!(relay(&dir, "bothy").supports_exit_port_ipv4(22));
    assert!(!relay(&dir, "bothy").supports_exit_port_ipv4(9001));
    assert!(!relay(&dir, "guardy").supports_exit_port_ipv4(443));
    assert!(!relay(&dir, "middly").supports_exit_port_ipv4(80));

    for nick in ["guardy", "exity", "middly", "bothy"] {
        let r = relay(&dir, nick);
        let is_exit = nick == "exity" || nick == "bothy";
        assert_eq!(r.ipv4_policy().allows_port(80), is_exit, "{}", nick);
        // No fixture relay declares an IPv6 policy, so none exits over IPv6.
        assert!(!r.supports_exit_port_ipv6(443), "{}", nick);
        assert!(!r.ipv6_policy().allows_port(80), "{}", nick);
        // All four are flagged V2Dir and advertise DirCache=2.
        assert!(r.is_dir_cache(), "{}", nick);
    }
}

#[test]
fn netdirsel__microdescriptors_carry_over_from_an_earlier_directory() {
    let previous = test_netdir();
    let mut next = PartialNetDir::new(load_consensus("consensus-md.txt"), None);
    assert_eq!(next.missing_microdescs().count(), 4);

    let loaded = next.fill_from_previous_netdir(&previous);
    assert_eq!(loaded.len(), 4);
    assert_eq!(next.missing_microdescs().count(), 0);
    assert!(next.have_enough_paths());
    assert_eq!(next.unwrap_if_sufficient().expect("sufficient").relays().count(), 4);

    // The same works for a directory that is only partly filled in.
    let mut partial = PartialNetDir::new(load_consensus("consensus-md.txt"), None);
    for md in load_mds("mds-partial.txt") {
        assert!(partial.add_microdesc(md));
    }
    let middly = *partial
        .missing_microdescs()
        .next()
        .expect("middly's microdescriptor is still missing");
    let loaded: Vec<[u8; 32]> = partial
        .fill_from_previous_netdir(&previous)
        .into_iter()
        .copied()
        .collect();
    assert!(loaded.contains(&middly));
    assert_eq!(partial.missing_microdescs().count(), 0);
    assert!(partial.have_enough_paths());
}
