//! Group `netdir_basics` — the directory view that path selection reads.
//!
//! This is step-03 capability that the cumulative workspace has to keep
//! working: relays are indexed by both identities, flags and exit policies come
//! from the consensus and the microdescriptor respectively, and a directory
//! that is missing too many microdescriptors is not usable yet.

// The synthetic directory fixtures. They are included here rather than in
// `tests/common/mod.rs` so that a candidate who has not built
// `tor_netdir::testnet` loses only the groups that need a directory; see
// that file's header.
#[path = "netdir_support.rs"]
mod netdir_support;

#[test]
fn netdir_basics__the_directory_lists_every_relay_it_was_given() {
    let dir = netdir_support::netdir_custom(|_idx, _nb| {});
    assert_eq!(dir.relays().count(), 40);
    assert_eq!(dir.all_relays().count(), 40);
    assert!(dir.all_relays().all(|r| r.is_usable()));

    // `all_relays()` yields the unchecked view of each consensus entry;
    // `into_relay()` is what turns one into a usable `Relay`, and it does so
    // exactly when the entry is usable.
    for unchecked in dir.all_relays() {
        assert!(unchecked.is_usable());
        let relay = unchecked.into_relay().expect("a usable entry converts");
        assert!(dir.by_id(relay.id()).is_some());
    }

    // Take one relay's microdescriptor away. Its consensus entry is still
    // there, so `all_relays()` still reports forty; but that entry is no longer
    // usable, `into_relay()` hands back nothing, and it is absent from
    // `relays()` and from the identity lookups.
    let incomplete = netdir_support::netdir_custom(|idx, nb| {
        if idx == 5 {
            nb.omit_md = true;
        }
    });
    assert_eq!(incomplete.all_relays().count(), 40);
    assert_eq!(incomplete.relays().count(), 39);
    let unusable = incomplete
        .all_relays()
        .find(|r| !r.is_usable())
        .expect("the relay whose microdescriptor was dropped is still listed");
    assert!(unusable.into_relay().is_none());
    assert!(incomplete.by_id(&netdir_support::ed_id(5)).is_none());
    assert!(incomplete.by_rsa_id(&netdir_support::rsa_id(5)).is_none());
    // ...while still being listed, which is what makes it *unusable* rather
    // than absent.
    assert!(incomplete.rsa_id_is_listed(&netdir_support::rsa_id(5)));
}

#[test]
fn netdir_basics__relays_are_reachable_by_either_identity() {
    let dir = netdir_support::netdir_custom(|_idx, _nb| {});

    let by_ed = dir.by_id(&netdir_support::ed_id(7)).expect("relay 7 by ed25519 id");
    assert_eq!(by_ed.rsa_id(), &netdir_support::rsa_id(7));

    let by_rsa = dir.by_rsa_id(&netdir_support::rsa_id(7)).expect("relay 7 by rsa id");
    assert_eq!(by_rsa.id(), &netdir_support::ed_id(7));

    assert_eq!(
        dir.id_pair_listed(&netdir_support::ed_id(7), &netdir_support::rsa_id(7)),
        Some(true)
    );
    // A pair that mixes two different relays' identities is not listed.
    assert_eq!(
        dir.id_pair_listed(&netdir_support::ed_id(7), &netdir_support::rsa_id(8)),
        Some(false)
    );
    assert!(dir.by_id(&netdir_support::ed_id(200)).is_none());
    assert!(!dir.rsa_id_is_listed(&netdir_support::rsa_id(200)));

    // `same_relay` compares the two identities, not the objects: the one relay
    // reached by two different lookups is the same relay, and two distinct
    // listed relays are not.
    assert!(by_ed.same_relay(&by_rsa));
    assert!(by_rsa.same_relay(&by_ed));
    assert!(by_ed.same_relay(&by_ed));
    let other = dir.by_id(&netdir_support::ed_id(8)).expect("relay 8");
    assert!(!by_ed.same_relay(&other));
    assert!(!other.same_relay(&by_ed));
}

#[test]
fn netdir_basics__flags_follow_the_documented_decades() {
    // Relays 0-9 carry no extra flags, 10-19 are exits, 20-29 are guards and
    // 30-39 are both.
    let dir = netdir_support::netdir_custom(|_idx, _nb| {});
    for idx in 0_u8..40 {
        let relay = dir.by_id(&netdir_support::ed_id(idx)).expect("relay is listed");
        let decade = idx / 10;
        assert_eq!(
            relay.is_flagged_guard(),
            decade >= 2,
            "guard flag wrong for relay {}",
            idx
        );
        assert_eq!(
            relay.policies_allow_some_port(),
            decade == 1 || decade == 3,
            "exit policy wrong for relay {}",
            idx
        );
    }
}

#[test]
fn netdir_basics__exit_policies_split_on_parity() {
    let dir = netdir_support::netdir_custom(|_idx, _nb| {});
    let odd_exit = dir.by_id(&netdir_support::ed_id(11)).expect("relay 11");
    let even_exit = dir.by_id(&netdir_support::ed_id(10)).expect("relay 10");
    let non_exit = dir.by_id(&netdir_support::ed_id(0)).expect("relay 0");

    // Odd-numbered exits accept only 80 and 443.
    assert!(odd_exit.supports_exit_port_ipv4(80));
    assert!(odd_exit.supports_exit_port_ipv4(443));
    assert!(!odd_exit.supports_exit_port_ipv4(22));
    // Even-numbered exits accept everything.
    assert!(even_exit.supports_exit_port_ipv4(22));
    assert!(even_exit.supports_exit_port_ipv4(65535));
    // Nobody advertises an IPv6 exit policy.
    assert!(!odd_exit.supports_exit_port_ipv6(80));
    assert!(!even_exit.supports_exit_port_ipv6(80));
    // Non-exits reject every port.
    assert!(!non_exit.supports_exit_port_ipv4(80));
    assert!(!non_exit.policies_allow_some_port());

    // For a relay the consensus does not flag, the effective policy and the
    // declared policy are the same policy, port for port.
    for port in [22_u16, 80, 443, 8080, 65535] {
        assert_eq!(
            odd_exit.ipv4_policy().allows_port(port),
            odd_exit.ipv4_declared_policy().allows_port(port),
            "declared and effective policies disagree at port {}",
            port
        );
        assert_eq!(
            even_exit.ipv4_policy().allows_port(port),
            even_exit.ipv4_declared_policy().allows_port(port),
            "declared and effective policies disagree at port {}",
            port
        );
    }

    // The BadExit consensus flag is what makes the two diverge. Relay 11 still
    // *declares* 80 and 443 in its microdescriptor, but its effective policy
    // rejects everything, so it advertises no exit port at all.
    use tor_netdoc::doc::netstatus::RelayFlags;
    let with_bad_exit = netdir_support::netdir_custom(|idx, nb| {
        if idx == 11 {
            nb.rs.add_flags(RelayFlags::BAD_EXIT);
        }
    });
    let flagged = with_bad_exit
        .by_id(&netdir_support::ed_id(11))
        .expect("a bad exit is still a listed, usable relay");
    assert!(flagged.ipv4_declared_policy().allows_port(80));
    assert!(flagged.ipv4_declared_policy().allows_port(443));
    assert!(!flagged.ipv4_policy().allows_port(80));
    assert!(!flagged.ipv4_policy().allows_port(443));
    assert!(!flagged.supports_exit_port_ipv4(80));
    assert!(!flagged.policies_allow_some_port());

    // Its neighbours in the same directory are untouched.
    let unflagged = with_bad_exit
        .by_id(&netdir_support::ed_id(13))
        .expect("relay 13");
    assert!(unflagged.supports_exit_port_ipv4(80));
    assert!(unflagged.ipv4_policy().allows_port(443));
    assert!(unflagged.policies_allow_some_port());
}

#[test]
fn netdir_basics__adjacent_relays_are_in_the_same_family() {
    let dir = netdir_support::netdir_custom(|_idx, _nb| {});
    let r0 = dir.by_id(&netdir_support::ed_id(0)).expect("relay 0");
    let r1 = dir.by_id(&netdir_support::ed_id(1)).expect("relay 1");
    let r2 = dir.by_id(&netdir_support::ed_id(2)).expect("relay 2");

    assert!(r0.in_same_family(&r1));
    assert!(r1.in_same_family(&r0));
    assert!(!r0.in_same_family(&r2));
    assert!(!r2.in_same_family(&r0));
}

#[test]
fn netdir_basics__even_numbered_relays_are_directory_caches() {
    let dir = netdir_support::netdir_custom(|_idx, _nb| {});
    for idx in 0_u8..40 {
        let relay = dir.by_id(&netdir_support::ed_id(idx)).expect("relay is listed");
        assert_eq!(
            relay.is_dir_cache(),
            idx % 2 == 0,
            "DirCache advertisement wrong for relay {}",
            idx
        );
    }
}

#[test]
fn netdir_basics__a_directory_missing_most_microdescriptors_is_not_sufficient() {
    let complete = netdir_support::partial_netdir_custom_with_params(|_idx, _nb| {}, None);
    assert!(complete.have_enough_paths());
    assert!(complete.unwrap_if_sufficient().is_ok());

    let sparse = netdir_support::partial_netdir_custom_with_params(
        |idx, nb| {
            if idx < 35 {
                nb.omit_md = true;
            }
        },
        None,
    );
    assert!(!sparse.have_enough_paths());
    assert!(sparse.unwrap_if_sufficient().is_err());

    // A directory for the same consensus does not have to re-fetch what the
    // previous one already holds. Both directories here are built from one
    // `construct_custom_network` call, so they want the same digests.
    use tor_netdir::{MdReceiver, PartialNetDir};
    let (consensus, mds) = tor_netdir::testnet::construct_custom_network(|_idx, _nb| {})
        .expect("cannot build test net");
    let mut previous = PartialNetDir::new(consensus.clone(), None);
    for md in mds {
        previous.add_microdesc(md);
    }
    let previous = previous
        .unwrap_if_sufficient()
        .expect("the fully populated network is sufficient");

    let mut fresh = PartialNetDir::new(consensus, None);
    assert_eq!(fresh.missing_microdescs().count(), 40);
    assert!(!fresh.have_enough_paths());
    let carried = fresh.fill_from_previous_netdir(&previous);
    assert_eq!(
        carried.len(),
        40,
        "every wanted descriptor the previous directory held should be carried over"
    );
    // A descriptor taken this way counts as one that arrived normally.
    assert_eq!(fresh.missing_microdescs().count(), 0);
    assert!(fresh.have_enough_paths());
    assert!(fresh.unwrap_if_sufficient().is_ok());

    // Descriptors the previous directory does not hold are not invented. This
    // directory wants forty descriptors that no relay of `unrelated` has, so
    // nothing is carried over and nothing stops being missing.
    let unrelated = netdir_support::netdir_custom(|_idx, _nb| {});
    let mut empty_handed = netdir_support::partial_netdir_custom_with_params(
        |_idx, nb| {
            nb.omit_md = true;
        },
        None,
    );
    assert_eq!(empty_handed.missing_microdescs().count(), 40);
    assert!(empty_handed.fill_from_previous_netdir(&unrelated).is_empty());
    assert_eq!(empty_handed.missing_microdescs().count(), 40);
    assert!(!empty_handed.have_enough_paths());
}
