//! Group `guard_restrict` — per-request restrictions on guard selection.
//!
//! A request may now carry more than one restriction, and a restriction may
//! name a whole set of identities to avoid rather than a single one.  That is
//! what lets a caller keep a chosen exit's entire family out of the guard slot.

// The synthetic directory fixtures. They are included here rather than in
// `tests/common/mod.rs` so that a candidate who has not built
// `tor_netdir::testnet` loses only the groups that need a directory; see
// that file's header.
#[path = "netdir_support.rs"]
mod netdir_support;
use std::collections::HashSet;
use tor_guardmgr::{GuardMgr, GuardRestriction, GuardUsageBuilder, GuardUsageKind, PickGuardError};
use tor_netdir::NetDir;
use tor_persist::TestingStateMgr;

/// A directory in which relays 30 and 32 are the only relays that are both
/// flagged as guards and advertising the directory-cache subprotocol, so the
/// guard sample is exactly those two relays.
fn two_candidate_netdir() -> NetDir {
    netdir_support::netdir_custom(|idx, nb| netdir_support::only_these_are_dir_caches(&[30, 32], idx, nb))
}

/// Select a guard for a `Data` circuit under `restrictions`, returning the
/// test-network index of the chosen relay.
fn select(restrictions: Vec<GuardRestriction>) -> Result<u8, PickGuardError> {
    let netdir = two_candidate_netdir();
    tor_rtcompat::tokio::test_with_runtime(move |rt| async move {
        let guardmgr =
            GuardMgr::new(rt, TestingStateMgr::new()).expect("could not create a guard manager");
        guardmgr.update_network(&netdir);

        let mut builder = GuardUsageBuilder::new();
        builder.kind(GuardUsageKind::Data);
        for r in restrictions {
            builder.push_restriction(r);
        }
        let usage = builder.build().expect("could not build a guard usage");

        match guardmgr.select_guard(usage, Some(&netdir)) {
            Ok((guard, _mon, _usable)) => {
                let relay = guard
                    .get_relay(&netdir)
                    .expect("the selected guard should be listed in the directory");
                Ok(netdir_support::ed_index(relay.id()))
            }
            Err(e) => Err(e),
        }
    })
}

/// A set restriction naming the given test-network relays.
fn avoid_all(indices: &[u8]) -> GuardRestriction {
    let ids: HashSet<_> = indices.iter().map(|idx| netdir_support::ed_id(*idx)).collect();
    GuardRestriction::AvoidAllIds(ids)
}

#[test]
fn guard_restrict__an_unrestricted_request_picks_one_of_the_candidates() {
    let chosen = select(Vec::new()).expect("a guard should be available");
    assert!(
        chosen == 30 || chosen == 32,
        "unexpected guard {} chosen",
        chosen
    );
}

#[test]
fn guard_restrict__avoiding_one_identity_selects_the_other_candidate() {
    assert_eq!(select(vec![avoid_all(&[30])]).ok(), Some(32));
    assert_eq!(select(vec![avoid_all(&[32])]).ok(), Some(30));
}

#[test]
fn guard_restrict__avoiding_every_candidate_leaves_no_guard() {
    let outcome = select(vec![avoid_all(&[30, 32])]);
    assert!(
        matches!(outcome, Err(PickGuardError::EveryoneIsDown)),
        "a fully restricted request must not yield a guard"
    );

    // The same variant, and not some separate "empty sample" error, is what a
    // manager with nothing in its sample reports: never given a network, and
    // asked for a guard with no directory to expand the sample from, it has
    // nothing to choose and says everyone is down.
    let never_given_a_network = tor_rtcompat::tokio::test_with_runtime(|rt| async move {
        let guardmgr =
            GuardMgr::new(rt, TestingStateMgr::new()).expect("could not create a guard manager");
        let mut builder = GuardUsageBuilder::new();
        builder.kind(GuardUsageKind::Data);
        let usage = builder.build().expect("could not build a guard usage");
        guardmgr.select_guard(usage, None).map(|_| ())
    });
    match never_given_a_network {
        Err(PickGuardError::EveryoneIsDown) => {}
        other => panic!("an empty guard sample should report EveryoneIsDown, got {:?}", other),
    }
}

#[test]
fn guard_restrict__an_empty_set_restriction_excludes_nobody() {
    let chosen = select(vec![avoid_all(&[])]).expect("a guard should be available");
    assert!(
        chosen == 30 || chosen == 32,
        "unexpected guard {} chosen",
        chosen
    );
}

#[test]
fn guard_restrict__restrictions_accumulate_rather_than_replace() {
    // Two separately pushed single-identity restrictions must both apply; if
    // the second one replaced the first, relay 30 would still be selectable.
    let both = vec![
        GuardRestriction::AvoidId(netdir_support::ed_id(30)),
        GuardRestriction::AvoidId(netdir_support::ed_id(32)),
    ];
    assert!(matches!(
        select(both),
        Err(PickGuardError::EveryoneIsDown)
    ));

    // One of them on its own still leaves the other candidate.
    assert_eq!(
        select(vec![GuardRestriction::AvoidId(netdir_support::ed_id(30))]).ok(),
        Some(32)
    );
}
