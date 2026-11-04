//! Group `exit_family` — a chosen exit keeps its whole family out of the guard
//! position.
//!
//! Ruling out only the exit relay itself is not enough: a guard operated by the
//! same party as the exit sees both ends of the circuit.  When an exit is
//! chosen for us, the restriction handed to the guard manager must name the
//! exit *and* every relay that mutually declares a family with it.

// The synthetic directory fixtures. They are included here rather than in
// `tests/common/mod.rs` so that a candidate who has not built
// `tor_netdir::testnet` loses only the groups that need a directory; see
// that file's header.
#[path = "netdir_support.rs"]
mod netdir_support;
use rand::SeedableRng;
use tor_circmgr::path::exitpath::ExitPathBuilder;
use tor_circmgr::{DirInfo, Error, PathConfig};
use tor_guardmgr::GuardMgr;
use tor_netdir::{NetDir, Relay};
use tor_persist::TestingStateMgr;

/// A deterministic generator for the middle- and exit-relay choices.
fn rng() -> rand_chacha::ChaCha20Rng {
    rand_chacha::ChaCha20Rng::seed_from_u64(0x0405_0607)
}

/// Build a directory where only `caches` are guard-flagged directory caches
/// (and so the only possible guards), with the given family declarations
/// applied on top of the default ones.
fn netdir(caches: &[usize], families: &[(usize, &[u8])]) -> NetDir {
    let caches: Vec<usize> = caches.to_vec();
    let families: Vec<(usize, Vec<u8>)> = families
        .iter()
        .map(|(idx, members)| (*idx, members.to_vec()))
        .collect();
    netdir_support::netdir_custom(move |idx, nb| {
        netdir_support::only_these_are_dir_caches(&caches, idx, nb);
        for (target, members) in &families {
            if *target == idx {
                nb.md.family(
                    netdir_support::family_of(members)
                        .parse()
                        .expect("family line should parse"),
                );
            }
        }
    })
}

/// The test-network indices that `dir` considers family of `relay`.
fn family_indices(dir: &NetDir, relay: &Relay<'_>) -> Vec<u8> {
    let mut found: Vec<u8> = dir
        .known_family_members(relay)
        .map(|other| netdir_support::ed_index(other.id()))
        .collect();
    found.sort_unstable();
    found
}

/// Ask for a three-hop path whose last hop is relay `exit_idx`, using a fresh
/// guard manager, and report the path length or the error.
fn path_to_chosen_exit(dir: NetDir, exit_idx: u8, expect_family: &[u8]) -> Result<usize, Error> {
    let expect_family = expect_family.to_vec();
    tor_rtcompat::tokio::test_with_runtime(move |rt| async move {
        let guardmgr = GuardMgr::new(rt, TestingStateMgr::new()).expect("guard manager");
        let exit = dir
            .by_id(&netdir_support::ed_id(exit_idx))
            .expect("the chosen exit should be listed");
        assert_eq!(
            family_indices(&dir, &exit),
            expect_family,
            "the test directory does not declare the family this case needs"
        );
        ExitPathBuilder::from_chosen_exit(exit.clone())
            .pick_path(
                &mut rng(),
                DirInfo::Directory(&dir),
                Some(&guardmgr),
                &PathConfig::default(),
            )
            .map(|(path, _mon, _usable)| path.len())
    })
}

#[test]
fn exit_family__a_chosen_exit_rules_out_its_family_as_a_guard() {
    // Relays 30 and 32 are the only possible guards, and they declare each
    // other as family.  Choosing relay 30 as the exit therefore leaves nothing
    // that may serve as the guard.
    let dir = netdir(&[30, 32], &[(30, &[32]), (32, &[30])]);
    let outcome = path_to_chosen_exit(dir, 30, &[32]);
    assert!(
        matches!(outcome, Err(Error::Guard(_))),
        "a path was built even though every guard is family of the exit"
    );
}

#[test]
fn exit_family__a_guard_outside_the_exits_family_is_still_usable() {
    // Same as above, but relay 34 is also a candidate guard and is not family
    // of relay 30, so a path exists.
    let dir = netdir(&[30, 32, 34], &[(30, &[32]), (32, &[30])]);
    assert_eq!(path_to_chosen_exit(dir, 30, &[32]).ok(), Some(3));
}

#[test]
fn exit_family__an_exit_with_no_family_rules_out_only_itself() {
    // Relay 30 declares nobody, so only relay 30 is excluded and relay 32 can
    // still be the guard.
    let dir = netdir(&[30, 32], &[(30, &[])]);
    assert_eq!(path_to_chosen_exit(dir, 30, &[]).ok(), Some(3));
}

#[test]
fn exit_family__a_one_sided_family_declaration_does_not_exclude_a_guard() {
    // Relay 30 claims relay 32 as family, but relay 32 does not agree, so they
    // are not in a family and relay 32 remains usable as a guard.
    let dir = netdir(&[30, 32], &[(30, &[32])]);
    assert_eq!(path_to_chosen_exit(dir, 30, &[]).ok(), Some(3));
}

#[test]
fn exit_family__an_exit_unrelated_to_the_guards_leaves_them_alone() {
    // Relay 11 is an exit that is neither a guard nor family of one; its own
    // family (relay 10) is excluded, but the guards are untouched.
    let dir = netdir(&[30, 32], &[]);
    assert_eq!(path_to_chosen_exit(dir, 11, &[10]).ok(), Some(3));
}
