//! Group `circ_paths` — the shape of the paths the circuit manager asks for.
//!
//! Exercised without a guard manager, so the only randomness is the seeded
//! generator this group supplies.

// The synthetic directory fixtures. They are included here rather than in
// `tests/common/mod.rs` so that a candidate who has not built
// `tor_netdir::testnet` loses only the groups that need a directory; see
// that file's header.
#[path = "netdir_support.rs"]
mod netdir_support;
use rand::SeedableRng;
use std::collections::BTreeSet;
use tor_circmgr::path::{dirpath::DirPathBuilder, exitpath::ExitPathBuilder, TorPath};
use tor_circmgr::{DirInfo, Error, PathConfig, TargetPort};
use tor_guardmgr::GuardMgr;
use tor_rtcompat::tokio::TokioRuntimeHandle;

/// The "no guard manager" argument, spelled out so its runtime type is known.
const NO_GUARDS: Option<&GuardMgr<TokioRuntimeHandle>> = None;

/// A deterministic generator, so a path choice never depends on the machine.
fn rng() -> rand_chacha::ChaCha20Rng {
    rand_chacha::ChaCha20Rng::seed_from_u64(0x0405_0607)
}

#[test]
fn circ_paths__a_one_hop_path_holds_a_single_relay() {
    let dir = netdir_support::netdir_custom(|_idx, _nb| {});
    let relay = dir.by_id(&netdir_support::ed_id(12)).expect("relay 12");
    assert_eq!(TorPath::new_one_hop(relay).len(), 1);
}

#[test]
fn circ_paths__a_multihop_path_reports_the_relays_it_was_given() {
    let dir = netdir_support::netdir_custom(|_idx, _nb| {});
    let relays: Vec<_> = [20_u8, 1, 15]
        .iter()
        .map(|idx| dir.by_id(&netdir_support::ed_id(*idx)).expect("relay is listed"))
        .collect();
    assert_eq!(TorPath::new_multihop(relays.clone()).len(), 3);
    assert_eq!(TorPath::new_multihop(relays[..1].to_vec()).len(), 1);
}

#[test]
fn circ_paths__a_directory_path_is_a_single_hop() {
    let dir = netdir_support::netdir_custom(|_idx, _nb| {});
    let (path, mon, usable) = DirPathBuilder::new()
        .pick_path(&mut rng(), DirInfo::Directory(&dir), NO_GUARDS)
        .expect("the test network has directory caches");
    assert_eq!(path.len(), 1);
    // Without a guard manager there is nothing to report circuit status to.
    assert!(mon.is_none());
    assert!(usable.is_none());
}

#[test]
fn circ_paths__an_exit_path_is_three_hops() {
    let dir = netdir_support::netdir_custom(|_idx, _nb| {});
    // A sorted, de-duplicated set of wanted ports: `TargetPort` is totally
    // ordered so a predictor can keep its wanted ports in one.
    let ports: BTreeSet<TargetPort> = [TargetPort::ipv4(443), TargetPort::ipv4(80)]
        .iter()
        .copied()
        .collect();
    assert_eq!(ports.len(), 2);

    let (path, _, _) = ExitPathBuilder::from_target_ports(ports.iter().copied())
        .pick_path(&mut rng(), DirInfo::Directory(&dir), NO_GUARDS, &PathConfig::default())
        .expect("the test network has exits for ports 80 and 443");
    assert_eq!(path.len(), 3);

    let (any, _, _) = ExitPathBuilder::for_any_exit()
        .pick_path(&mut rng(), DirInfo::Directory(&dir), NO_GUARDS, &PathConfig::default())
        .expect("the test network has exits");
    assert_eq!(any.len(), 3);
}

#[test]
fn circ_paths__a_port_no_relay_exits_to_has_no_path() {
    let dir = netdir_support::netdir_custom(|_idx, _nb| {});
    // No relay in the test network advertises an IPv6 exit policy.
    let outcome = ExitPathBuilder::from_target_ports(vec![TargetPort::ipv6(80)]).pick_path(
        &mut rng(),
        DirInfo::Directory(&dir),
        NO_GUARDS,
        &PathConfig::default(),
    );
    assert!(
        matches!(outcome, Err(Error::NoRelays(_))),
        "expected NoRelays for an IPv6 exit request"
    );

    // Building a path without a consensus is a different failure.
    let no_consensus = ExitPathBuilder::for_any_exit().pick_path(
        &mut rng(),
        DirInfo::Fallbacks(&[]),
        NO_GUARDS,
        &PathConfig::default(),
    );
    assert!(matches!(no_consensus, Err(Error::NeedConsensus)));
}
