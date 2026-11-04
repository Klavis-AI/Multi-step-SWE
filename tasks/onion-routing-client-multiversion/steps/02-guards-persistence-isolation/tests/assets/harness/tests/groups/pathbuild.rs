//! Group `pathbuild` - building a path through the network.
//!
//! Graded through `tor_circmgr::path::{TorPath, exitpath::ExitPathBuilder,
//! dirpath::DirPathBuilder}` and `tor_circmgr::DirInfo`. A path is only useful
//! if it has the right number of hops, ends at a relay that can exit to the
//! requested port, and is refused outright when no such relay exists.

use tor_circmgr::path::{dirpath::DirPathBuilder, exitpath::ExitPathBuilder};
use tor_circmgr::{DirInfo, Error, PathConfig, TargetPort};
use tor_netdir::fallback::FallbackDir;

/// The "no guard manager" argument, spelled out so the runtime type is known.
type NoGuards<'a> = Option<&'a tor_guardmgr::GuardMgr<crate::common::TestRt>>;

#[test]
fn pathbuild__an_exit_path_has_three_hops() {
    let nd = crate::common::testnet();
    let mut rng = crate::common::seeded_rng(1);
    let guards: NoGuards<'_> = None;

    for _ in 0..8 {
        let (path, mon, usable) = ExitPathBuilder::for_any_exit()
            .pick_path(
                &mut rng,
                DirInfo::Directory(&nd),
                guards,
                &PathConfig::default(),
            )
            .expect("could not build an exit path over the test network");
        assert_eq!(path.len(), 3, "an exit path must be guard, middle, exit");
        // With no guard manager there is nothing to report success to.
        assert!(mon.is_none(), "a guardless path produced a guard monitor");
        assert!(usable.is_none(), "a guardless path produced a usability handle");
    }
}

#[test]
fn pathbuild__a_directory_path_over_a_consensus_is_one_hop() {
    let nd = crate::common::testnet();
    let mut rng = crate::common::seeded_rng(2);
    let guards: NoGuards<'_> = None;

    let (path, mon, usable) = DirPathBuilder::new()
        .pick_path(&mut rng, DirInfo::Directory(&nd), guards)
        .expect("could not build a directory path over the test network");
    assert_eq!(path.len(), 1, "a directory path is a single hop");
    assert!(mon.is_none() && usable.is_none());

    // `DirPathBuilder::default()` is the same builder.
    let (path, _, _) = DirPathBuilder::default()
        .pick_path(&mut rng, DirInfo::Directory(&nd), guards)
        .expect("the default directory path builder failed");
    assert_eq!(path.len(), 1);
}

#[test]
fn pathbuild__a_directory_path_can_come_from_a_guard() {
    // The guard manager, not the weighted relay picker, chooses the hop once
    // one is supplied; this network leaves exactly one legal answer.
    let nd = crate::common::netdir_with_one_guard(38);
    let mut rng = crate::common::seeded_rng(3);
    let gm = tor_guardmgr::GuardMgr::new(crate::common::runtime(), tor_persist::TestingStateMgr::new())
        .expect("GuardMgr::new failed");
    gm.update_network(&nd);

    let (path, mon, usable) = DirPathBuilder::new()
        .pick_path(&mut rng, DirInfo::Directory(&nd), Some(&gm))
        .expect("could not build a directory path through a guard");
    assert_eq!(path.len(), 1, "a directory path is a single hop");
    assert!(
        mon.is_some() && usable.is_some(),
        "a guarded path must report its guard's fate"
    );
    mon.expect("guard monitor").attempt_abandoned();
    drop(usable);
}

#[test]
fn pathbuild__a_multi_hop_path_needs_a_consensus() {
    let mut rng = crate::common::seeded_rng(4);
    let guards: NoGuards<'_> = None;
    let empty: &[FallbackDir] = &[];

    // Fallback directories are enough to reach a directory cache, but not to
    // pick an anonymous three-hop path.
    let err = match ExitPathBuilder::for_any_exit().pick_path(
        &mut rng,
        DirInfo::Fallbacks(empty),
        guards,
        &PathConfig::default(),
    ) {
        Ok(_) => panic!("built a multi-hop path without a consensus"),
        Err(e) => e,
    };
    assert!(
        matches!(err, Error::NeedConsensus),
        "expected NeedConsensus, got {:?}",
        err
    );

    // With no fallbacks at all there is not even a directory hop to be had.
    let err = match DirPathBuilder::new().pick_path(&mut rng, DirInfo::Fallbacks(empty), guards) {
        Ok(_) => panic!("built a directory path out of an empty fallback list"),
        Err(e) => e,
    };
    match err {
        // Only the variant is graded. The `String` is a human-readable
        // explanation whose wording is not part of the contract; all that is
        // required of it is that the refusal carries one.
        Error::NoRelays(msg) => assert!(!msg.is_empty(), "NoRelays carried no explanation"),
        other => panic!("expected NoRelays, got {:?}", other),
    }
}

#[test]
fn pathbuild__the_exit_must_support_the_requested_port() {
    let nd = crate::common::testnet();
    let mut rng = crate::common::seeded_rng(5);
    let guards: NoGuards<'_> = None;

    // Every exit in the test network accepts port 443 over IPv4.
    let (path, _, _) = ExitPathBuilder::from_target_ports(vec![TargetPort::ipv4(443)])
        .pick_path(
            &mut rng,
            DirInfo::Directory(&nd),
            guards,
            &PathConfig::default(),
        )
        .expect("no path to a relay that exits to 443");
    assert_eq!(path.len(), 3);

    // No relay in the test network exits over IPv6, so the request has no
    // answer and must be refused rather than downgraded.
    let err = match ExitPathBuilder::from_target_ports(vec![TargetPort::ipv6(443)]).pick_path(
        &mut rng,
        DirInfo::Directory(&nd),
        guards,
        &PathConfig::default(),
    ) {
        Ok(_) => panic!("built a path to an IPv6 exit that does not exist"),
        Err(e) => e,
    };
    match err {
        // As above: the variant is the contract, not the wording.
        Error::NoRelays(msg) => assert!(!msg.is_empty(), "NoRelays carried no explanation"),
        other => panic!("expected NoRelays, got {:?}", other),
    }

    // An empty port list is the same request as "any exit at all".
    let (path, _, _) = ExitPathBuilder::from_target_ports(Vec::new())
        .pick_path(
            &mut rng,
            DirInfo::Directory(&nd),
            guards,
            &PathConfig::default(),
        )
        .expect("an empty port list did not behave as `for_any_exit`");
    assert_eq!(path.len(), 3);
}
