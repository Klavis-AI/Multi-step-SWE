//! Group `pathsel`: weighted relay selection and the shape of the paths a
//! client builds from it.

use std::collections::BTreeMap;

use tor_circmgr::path::dirpath::DirPathBuilder;
use tor_circmgr::path::TorPath;
use tor_circmgr::path::exitpath::ExitPathBuilder;
use tor_circmgr::{DirInfo, Error as CircError, TargetPort};
use tor_linkspec::ChanTarget;
use tor_netdir::fallback::FallbackDir;
use tor_netdir::{NetDir, Relay, WeightRole};

use crate::common::{det_rng, test_netdir};

/// The error from a path build that was expected to fail.
/// (`TorPath` is not `Debug`, so `expect_err` is unavailable.)
fn path_err(r: Result<TorPath<'_>, CircError>) -> CircError {
    match r {
        Ok(p) => panic!("expected no path, got one of {} hops", p.len()),
        Err(e) => e,
    }
}

/// Nickname of a fixture relay, recovered from its RSA identity.
fn nick_of(r: &Relay<'_>) -> &'static str {
    match r.rsa_id().as_bytes()[0] {
        0x11 => "guardy",
        0x22 => "exity",
        0x33 => "middly",
        0x44 => "bothy",
        other => panic!("unexpected fixture relay {:#x}", other),
    }
}

/// Find a fixture relay by nickname.
fn relay<'a>(dir: &'a NetDir, nick: &str) -> Relay<'a> {
    dir.relays()
        .find(|r| nick_of(r) == nick)
        .unwrap_or_else(|| panic!("no relay {}", nick))
}

/// Draw `n` relays for `role` and tally them by nickname.
fn tally<P>(dir: &NetDir, role: WeightRole, n: usize, usable: P) -> BTreeMap<&'static str, usize>
where
    P: Fn(&Relay<'_>) -> bool,
{
    let mut rng = det_rng();
    let mut counts = BTreeMap::new();
    for _ in 0..n {
        let r = dir
            .pick_relay(&mut rng, role, |r| usable(r))
            .expect("a relay with nonzero weight should exist");
        *counts.entry(nick_of(&r)).or_insert(0) += 1;
    }
    counts
}

#[test]
fn pathsel__each_role_only_draws_relays_the_weights_allow() {
    let dir = test_netdir();
    // In the fixture consensus, Wgg/Wmm/Wee are the only nonzero weights for
    // their role, so each role has exactly one eligible relay.
    let guards = tally(&dir, WeightRole::Guard, 200, |_| true);
    assert_eq!(guards.get("guardy"), Some(&200), "{:?}", guards);
    let middles = tally(&dir, WeightRole::Middle, 200, |_| true);
    assert_eq!(middles.get("middly"), Some(&200), "{:?}", middles);
    let exits = tally(&dir, WeightRole::Exit, 200, |_| true);
    assert_eq!(exits.get("exity"), Some(&200), "{:?}", exits);
}

#[test]
fn pathsel__begindir_selection_is_proportional_to_weighted_bandwidth() {
    let dir = test_netdir();
    // Wbg and Wbd are both 10000, so guardy (1000) and bothy (4000) share the
    // BeginDir role in a 1:4 ratio; Wbm and Wbe are zero.
    const N: usize = 4000;
    let counts = tally(&dir, WeightRole::BeginDir, N, |r: &Relay<'_>| r.is_dir_cache());
    assert_eq!(counts.len(), 2, "{:?}", counts);
    let guardy = *counts.get("guardy").unwrap_or(&0);
    let bothy = *counts.get("bothy").unwrap_or(&0);
    assert_eq!(guardy + bothy, N);
    let frac = guardy as f64 / N as f64;
    assert!(
        (frac - 0.2).abs() < 0.03,
        "guardy should take about a fifth of BeginDir draws, got {}",
        frac
    );
}

#[test]
fn pathsel__unweighted_selection_uses_every_relay_and_honours_the_predicate() {
    let dir = test_netdir();
    // The Unweighted role applies no bandwidth-weight factor at all, so every
    // relay is drawn in proportion to its raw consensus bandwidth
    // (1000:2000:3000:4000 for guardy:exity:middly:bothy).
    const N: usize = 4000;
    let counts = tally(&dir, WeightRole::Unweighted, N, |_| true);
    assert_eq!(counts.len(), 4, "{:?}", counts);
    for (nick, want) in [
        ("guardy", 0.1_f64),
        ("exity", 0.2),
        ("middly", 0.3),
        ("bothy", 0.4),
    ] {
        let got = counts[nick] as f64 / N as f64;
        assert!(
            (got - want).abs() < 0.035,
            "{} drawn as {} of the time, wanted about {}",
            nick,
            got,
            want
        );
    }

    // A predicate that admits one relay always yields that relay.
    let mut rng = det_rng();
    for _ in 0..20 {
        let r = dir
            .pick_relay(&mut rng, WeightRole::Unweighted, |r| nick_of(r) == "middly")
            .expect("middly is usable");
        assert_eq!(nick_of(&r), "middly");
    }
    // A predicate that admits nothing yields None, not a panic.
    assert!(dir
        .pick_relay(&mut rng, WeightRole::Unweighted, |_| false)
        .is_none());
}

#[test]
fn pathsel__a_zero_weight_relay_is_never_chosen_for_that_role() {
    let dir = test_netdir();
    // bothy advertises port 22 and has the Exit flag, but Wed is zero, so it
    // has no weight in the Exit role and no exit can be found for port 22.
    let bothy = relay(&dir, "bothy");
    assert!(TargetPort::ipv4(22).is_supported_by(&bothy));
    assert!(bothy.supports_exit_port_ipv4(22));
    let mut rng = det_rng();
    assert!(dir
        .pick_relay(&mut rng, WeightRole::Exit, |r| TargetPort::ipv4(22)
            .is_supported_by(r))
        .is_none());

    // Port 443 is served by exity, which does have exit weight.
    let r = dir
        .pick_relay(&mut rng, WeightRole::Exit, |r| TargetPort::ipv4(443)
            .is_supported_by(r))
        .expect("exity serves 443");
    assert_eq!(nick_of(&r), "exity");
}

#[test]
fn pathsel__an_exit_path_is_three_hops_and_needs_a_consensus() {
    let dir = test_netdir();
    let mut rng = det_rng();
    let info = DirInfo::Directory(&dir);

    let path = ExitPathBuilder::from_target_ports(vec![TargetPort::ipv4(80)])
        .pick_path(&mut rng, info)
        .expect("a path to an exit for port 80");
    assert_eq!(path.len(), 3);

    let chosen = relay(&dir, "exity");
    let path = ExitPathBuilder::from_chosen_exit(chosen)
        .pick_path(&mut rng, info)
        .expect("a path to the chosen exit");
    assert_eq!(path.len(), 3);

    // No relay in the fixture consensus exits to port 25.
    let e = path_err(
        ExitPathBuilder::from_target_ports(vec![TargetPort::ipv4(25)]).pick_path(&mut rng, info),
    );
    assert!(matches!(e, CircError::NoRelays(_)), "{:?}", e);

    // Fallback directories cannot carry a multi-hop path.
    let fbs = vec![fallback()];
    let e = path_err(
        ExitPathBuilder::from_target_ports(vec![TargetPort::ipv4(80)])
            .pick_path(&mut rng, DirInfo::Fallbacks(&fbs[..])),
    );
    assert!(matches!(e, CircError::NeedConsensus), "{:?}", e);
}

#[test]
fn pathsel__a_directory_path_is_a_single_hop() {
    let dir = test_netdir();
    let mut rng = det_rng();

    let path = DirPathBuilder::new()
        .pick_path(&mut rng, DirInfo::Directory(&dir))
        .expect("a directory cache");
    assert_eq!(path.len(), 1);

    let fbs = vec![fallback()];
    let path = DirPathBuilder::default()
        .pick_path(&mut rng, DirInfo::Fallbacks(&fbs[..]))
        .expect("a fallback cache");
    assert_eq!(path.len(), 1);

    let none: Vec<FallbackDir> = Vec::new();
    let e = path_err(DirPathBuilder::new().pick_path(&mut rng, DirInfo::Fallbacks(&none[..])));
    assert!(matches!(e, CircError::NoRelays(_)), "{:?}", e);
}

/// A single fallback directory with fixed identities.
fn fallback() -> FallbackDir {
    let fb = FallbackDir::builder()
        .rsa_identity([0x55; 20].into())
        .ed_identity([0x66; 32].into())
        .orport("10.0.0.9:9009".parse().unwrap())
        .build()
        .expect("well-formed fallback");
    assert_eq!(fb.rsa_identity().as_bytes(), &[0x55; 20][..]);
    fb
}
