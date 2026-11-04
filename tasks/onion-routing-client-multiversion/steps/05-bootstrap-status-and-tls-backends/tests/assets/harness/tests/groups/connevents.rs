//! Group `connevents` — the connection-status *stream* handed out by the
//! channel manager.
//!
//! The stream must be an ordinary `futures::Stream` of `ConnStatus`, it must be
//! cloneable, each clone must observe the current value immediately (no waiting
//! for the next change), and the manager must be able to hand out more than one
//! independent subscription.

use futures::{FutureExt, StreamExt};
use tor_chanmgr::{ChanMgr, ConnStatus, ConnStatusEvents};
use tor_rtcompat::tokio::TokioNativeTlsRuntime;

fn assert_stream_of<S, I>()
where
    S: futures::Stream<Item = I>,
{
}

/// A channel manager that has never been asked to do anything. Constructing one
/// performs no I/O, so this is deterministic and offline.
fn idle_manager() -> ChanMgr<TokioNativeTlsRuntime> {
    let rt = TokioNativeTlsRuntime::create().expect("tokio/native-tls runtime");
    ChanMgr::new(rt)
}

#[test]
fn connevents__debug_is_opaque() {
    let mgr = idle_manager();
    assert_eq!(
        format!("{:?}", mgr.bootstrap_events()),
        "ConnStatusEvents { .. }"
    );
}

#[test]
fn connevents__is_a_stream_of_connection_status() {
    assert_stream_of::<ConnStatusEvents, ConnStatus>();
    assert_eq!(
        std::any::type_name::<<ConnStatusEvents as futures::Stream>::Item>(),
        std::any::type_name::<ConnStatus>()
    );
}

#[test]
fn connevents__clone_observes_current_status() {
    let mgr = idle_manager();
    let events = mgr.bootstrap_events();
    let mut cloned = events.clone();
    let seen = cloned
        .next()
        .now_or_never()
        .flatten()
        .expect("a clone of the event stream must yield the current status at once");
    assert_eq!(seen.to_string(), "connecting to the internet");
    crate::common::assert_frac(seen.frac(), 0.0);
}

#[test]
fn connevents__first_observation_is_immediate() {
    let mgr = idle_manager();
    let mut events = mgr.bootstrap_events();
    // The current value is always available without any progress having been
    // made, so polling once resolves rather than pending.
    let first = events.next().now_or_never().flatten().expect("current status");
    assert!(!first.usable());
    assert!(first.blockage().is_none());
}

#[test]
fn connevents__channel_manager_hands_out_independent_subscriptions() {
    let mgr = idle_manager();
    let mut a = mgr.bootstrap_events();
    let mut b = mgr.bootstrap_events();
    let sa = a.next().now_or_never().flatten().expect("first subscription");
    let sb = b.next().now_or_never().flatten().expect("second subscription");
    assert_eq!(sa.to_string(), sb.to_string());
    crate::common::assert_frac(sa.frac(), sb.frac());
}
