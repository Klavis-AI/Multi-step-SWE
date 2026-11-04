//! Group `dir_events`: the directory manager's change notifications, and the
//! configuration object that a live reconfiguration hands it.
//!
//! `instruction.md` section 1: "live reconfiguration" and "a directory that is
//! incomplete until the primary guards' microdescriptors are present".  The
//! directory manager now coalesces its notifications through a flag-based
//! publisher, so `DirEvent` has to be a small copyable, comparable value.

use tor_dirmgr::{DirEvent, DirMgrConfig, DirMgrConfigBuilder, DownloadScheduleConfig};
use tor_netdoc::doc::netstatus::NetParams;

#[test]
fn dir_events__the_two_events_are_distinguishable() {
    assert_eq!(DirEvent::NewConsensus, DirEvent::NewConsensus);
    assert_eq!(DirEvent::NewDescriptors, DirEvent::NewDescriptors);
    assert_ne!(DirEvent::NewConsensus, DirEvent::NewDescriptors);
}

#[test]
fn dir_events__events_are_copyable_flags_not_owned_payloads() {
    fn note(ev: DirEvent) -> DirEvent {
        ev
    }
    let ev = DirEvent::NewConsensus;
    // A flag-based publisher records the same event more than once without
    // moving it, so `DirEvent` must be `Copy`.
    assert_eq!(note(ev), DirEvent::NewConsensus);
    assert_eq!(note(ev), DirEvent::NewConsensus);
    assert_eq!(ev, DirEvent::NewConsensus);

    // A set of pending events deduplicates by value.
    let pending = [ev, DirEvent::NewDescriptors, ev];
    assert_eq!(pending.iter().filter(|e| **e == ev).count(), 2);
}

#[test]
fn dir_events__override_net_params_is_part_of_the_directory_configuration() {
    let base = DirMgrConfigBuilder::default()
        .cache_path("/nonexistent/arti-cache")
        .build()
        .expect("build");

    let overridden = DirMgrConfigBuilder::default()
        .cache_path("/nonexistent/arti-cache")
        .override_net_params("circwindow=123".parse::<NetParams<i32>>().expect("params"))
        .build()
        .expect("build");

    assert_ne!(base, overridden);
    assert_eq!(
        overridden,
        DirMgrConfigBuilder::default()
            .cache_path("/nonexistent/arti-cache")
            .override_net_params("circwindow=123".parse::<NetParams<i32>>().expect("params"))
            .build()
            .expect("build")
    );
}

#[test]
fn dir_events__cache_path_and_schedule_are_part_of_the_directory_configuration() {
    let a: DirMgrConfig = DirMgrConfigBuilder::default()
        .cache_path("/nonexistent/a")
        .build()
        .expect("build");
    let b = DirMgrConfigBuilder::default()
        .cache_path("/nonexistent/b")
        .build()
        .expect("build");
    assert_ne!(a, b);

    let scheduled = DirMgrConfigBuilder::default()
        .cache_path("/nonexistent/a")
        .schedule_config(DownloadScheduleConfig::default())
        .build()
        .expect("build");
    assert_eq!(a, scheduled);

    // A missing cache path is a build error: the directory manager cannot be
    // configured without somewhere to store what it downloads.
    assert!(DirMgrConfigBuilder::default().build().is_err());
}
