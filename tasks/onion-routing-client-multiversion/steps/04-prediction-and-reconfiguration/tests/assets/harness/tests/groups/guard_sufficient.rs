//! Group `guard_sufficient` — a directory is not usable until the primary
//! guards' microdescriptors have arrived.
//!
//! A new consensus can list a primary guard whose microdescriptor has not been
//! downloaded yet.  Installing that directory would make the guard look
//! unusable and push the client onto a fresh guard, so the guard manager gets
//! to veto it.

// The synthetic directory fixtures. They are included here rather than in
// `tests/common/mod.rs` so that a candidate who has not built
// `tor_netdir::testnet` loses only the groups that need a directory; see
// that file's header.
#[path = "netdir_support.rs"]
mod netdir_support;
use tor_guardmgr::GuardMgr;
use tor_netdir::NetDir;
use tor_persist::TestingStateMgr;

#[test]
fn guard_sufficient__a_manager_with_no_primary_guards_accepts_any_directory() {
    let complete = pinned_netdir(|_idx, _nb| {});
    let missing = pinned_netdir(|idx, nb| {
        if idx == 30 || idx == 32 {
            nb.omit_md = true;
        }
    });
    tor_rtcompat::tokio::test_with_runtime(move |rt| async move {
        let guardmgr = GuardMgr::new(rt, TestingStateMgr::new()).expect("guard manager");
        // Nothing has been sampled yet, so there is no primary guard whose
        // microdescriptor could be missing.
        assert!(guardmgr.netdir_is_sufficient(&complete));
        assert!(guardmgr.netdir_is_sufficient(&missing));
    });
}

#[test]
fn guard_sufficient__a_directory_listing_every_primary_guard_is_sufficient() {
    let netdir = pinned_netdir(|_idx, _nb| {});
    tor_rtcompat::tokio::test_with_runtime(move |rt| async move {
        let guardmgr = GuardMgr::new(rt, TestingStateMgr::new()).expect("guard manager");
        guardmgr.update_network(&netdir);
        assert!(guardmgr.netdir_is_sufficient(&netdir));
    });
}

#[test]
fn guard_sufficient__a_missing_primary_microdescriptor_makes_a_directory_insufficient() {
    let netdir = pinned_netdir(|_idx, _nb| {});
    let without_mds = pinned_netdir(|idx, nb| {
        if idx == 30 || idx == 32 {
            nb.omit_md = true;
        }
    });
    tor_rtcompat::tokio::test_with_runtime(move |rt| async move {
        let guardmgr = GuardMgr::new(rt, TestingStateMgr::new()).expect("guard manager");
        guardmgr.update_network(&netdir);
        assert!(guardmgr.netdir_is_sufficient(&netdir));

        // Relays 30 and 32 are still listed in the consensus, but we cannot yet
        // tell whether their ed25519 identities still match.
        assert!(without_mds.missing_descriptor_for(&netdir_support::rsa_id(30)));
        assert!(without_mds.missing_descriptor_for(&netdir_support::rsa_id(32)));
        assert!(!guardmgr.netdir_is_sufficient(&without_mds));
    });
}

#[test]
fn guard_sufficient__a_guard_dropped_from_the_consensus_is_not_a_missing_descriptor() {
    let netdir = pinned_netdir(|_idx, _nb| {});
    let without_relays = pinned_netdir(|idx, nb| {
        if idx == 30 || idx == 32 {
            nb.omit_rs = true;
        }
    });
    tor_rtcompat::tokio::test_with_runtime(move |rt| async move {
        let guardmgr = GuardMgr::new(rt, TestingStateMgr::new()).expect("guard manager");
        guardmgr.update_network(&netdir);

        // The relays are gone from the consensus entirely: that is a definite
        // answer, not an outstanding download, so the directory is usable.
        assert!(!without_relays.rsa_id_is_listed(&netdir_support::rsa_id(30)));
        assert!(!without_relays.missing_descriptor_for(&netdir_support::rsa_id(30)));
        assert!(guardmgr.netdir_is_sufficient(&without_relays));
    });
}

#[test]
fn guard_sufficient__only_the_primary_guards_are_considered() {
    let netdir = pinned_netdir(|_idx, _nb| {});
    let other_missing = pinned_netdir(|idx, nb| {
        if idx == 34 || idx == 7 {
            nb.omit_md = true;
        }
    });
    tor_rtcompat::tokio::test_with_runtime(move |rt| async move {
        let guardmgr = GuardMgr::new(rt, TestingStateMgr::new()).expect("guard manager");
        guardmgr.update_network(&netdir);

        // Two microdescriptors are missing, but neither belongs to a sampled
        // guard, so they do not hold the directory back.
        assert!(other_missing.missing_descriptor_for(&netdir_support::rsa_id(34)));
        assert!(other_missing.missing_descriptor_for(&netdir_support::rsa_id(7)));
        assert!(guardmgr.netdir_is_sufficient(&other_missing));
    });
}

/// A directory in which relays 30 and 32 are the only guard-flagged directory
/// caches, so those two relays are the whole guard sample, further customised
/// by `func`.
fn pinned_netdir<F>(mut func: F) -> NetDir
where
    F: FnMut(usize, &mut tor_netdir::testnet::NodeBuilders),
{
    netdir_support::netdir_custom(move |idx, nb| {
        netdir_support::only_these_are_dir_caches(&[30, 32], idx, nb);
        func(idx, nb);
    })
}
