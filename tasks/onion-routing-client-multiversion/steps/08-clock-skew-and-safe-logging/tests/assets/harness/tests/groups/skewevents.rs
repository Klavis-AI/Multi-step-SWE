//! Group `skewevents`: the guard manager publishes a clock-skew estimate that
//! other crates can observe, and the circuit manager re-exports that channel.
//!
//! Traces to instruction.md section 1: "Clock-skew inference from guards and
//! fallbacks, used to explain bootstrap failure".  Interface:
//! `tor_guardmgr::GuardMgr::skew_events`, `tor_guardmgr::ClockSkewEvents::get`,
//! `tor_circmgr::ClockSkewEvents`.
//!
//! No network is used: the manager is built over an empty fallback list and a
//! throw-away state directory, and never contacts anything.

use crate::common::Tree;
use tor_guardmgr::{fallback::FallbackList, ClockSkewEvents, GuardMgr};
use tor_persist::FsStateMgr;
use tor_rtcompat::PreferredRuntime;

/// Build an idle guard manager over a scratch state directory.
fn idle_guard_mgr(tree: &Tree) -> GuardMgr<PreferredRuntime> {
    let runtime = PreferredRuntime::create().expect("could not create a runtime");
    let state = FsStateMgr::from_path(tree.root()).expect("could not open a state directory");
    GuardMgr::new(runtime, state, FallbackList::default()).expect("could not build a GuardMgr")
}

#[test]
fn skewevents__a_fresh_guard_manager_reports_no_estimate() {
    let tree = Tree::new();
    let mgr = idle_guard_mgr(&tree);
    // With no guard or fallback contacted, there is nothing to infer from, so
    // the published estimate is absent rather than "zero skew".
    assert!(mgr.skew_events().get().is_none());
}

#[test]
fn skewevents__each_call_returns_a_handle_to_the_same_estimate() {
    let tree = Tree::new();
    let mgr = idle_guard_mgr(&tree);
    let first = mgr.skew_events();
    let second = mgr.skew_events();
    assert!(first.get().is_none());
    assert!(second.get().is_none());
}

#[test]
fn skewevents__the_handle_is_cloneable() {
    let tree = Tree::new();
    let mgr = idle_guard_mgr(&tree);
    let handle = mgr.skew_events();
    let copy: ClockSkewEvents = handle.clone();
    assert!(handle.get().is_none());
    assert!(copy.get().is_none());
    drop(handle);
    assert!(copy.get().is_none());
}

#[test]
fn skewevents__tor_circmgr_reexports_the_same_event_type() {
    let tree = Tree::new();
    let mgr = idle_guard_mgr(&tree);
    // The same value must satisfy both paths: `tor-circmgr` re-exports
    // `tor-guardmgr`'s type rather than defining its own.
    let from_guardmgr: tor_guardmgr::ClockSkewEvents = mgr.skew_events();
    let via_circmgr: tor_circmgr::ClockSkewEvents = from_guardmgr;
    assert!(via_circmgr.get().is_none());
}
