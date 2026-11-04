//! Group `chanexpire`: expiry of unused channels.
//!
//! instruction.md: "expiry of unused channels". The channel manager gains a
//! public sweep entry point that closes channels which have been idle too long
//! and reports how long the caller may wait before sweeping again; a channel
//! reports how long it has been idle.

use std::time::Duration;

use tor_chanmgr::ChanMgr;
use tor_proto::channel::Channel;

use crate::common::runtime;

/// The documented lower bound on the sweep interval: no channel is ever
/// expired before it has been idle for this long, so there is never any point
/// in sweeping again sooner.
const MIN_INTERVAL: Duration = Duration::from_secs(180);

#[test]
fn chanexpire__empty_manager_reports_the_minimum_interval() {
    let mgr: ChanMgr<_> = ChanMgr::new(runtime());
    let wait = mgr.expire_channels();
    assert_eq!(wait, MIN_INTERVAL);
}

#[test]
fn chanexpire__interval_is_exactly_180_seconds() {
    let mgr: ChanMgr<_> = ChanMgr::new(runtime());
    let wait = mgr.expire_channels();
    assert_eq!(wait.as_secs(), 180);
    assert_eq!(wait.subsec_nanos(), 0);
}

#[test]
fn chanexpire__sweeping_is_idempotent() {
    // Sweeping an empty manager must not accumulate state or shrink the
    // reported interval: the answer is the same every time.
    let mgr: ChanMgr<_> = ChanMgr::new(runtime());
    let first = mgr.expire_channels();
    for _ in 0..4 {
        assert_eq!(mgr.expire_channels(), first);
    }
    // A second, independent manager agrees.
    let other: ChanMgr<_> = ChanMgr::new(runtime());
    assert_eq!(other.expire_channels(), first);
}

#[test]
fn chanexpire__channel_reports_how_long_it_has_been_unused() {
    // The sweep is driven by per-channel idle time, so a channel must expose
    // it: `None` while the channel still carries circuits, `Some(d)` once it
    // is idle.
    let idle: fn(&Channel) -> Option<Duration> = Channel::duration_unused;
    let sig = std::any::type_name_of_val(&idle);
    assert!(sig.contains("Option"), "unexpected signature: {}", sig);
    assert!(sig.contains("Duration"), "unexpected signature: {}", sig);
    assert!(sig.contains("Channel"), "unexpected signature: {}", sig);
}
