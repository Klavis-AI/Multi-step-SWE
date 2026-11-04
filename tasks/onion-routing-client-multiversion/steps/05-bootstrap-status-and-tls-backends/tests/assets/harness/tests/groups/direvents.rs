//! Group `direvents` — the directory manager's event vocabulary and its
//! bootstrap-status stream type.

use tor_dirmgr::{DirBootstrapEvents, DirBootstrapStatus, DirEvent, DirMgr};
use tor_rtcompat::tokio::TokioNativeTlsRuntime;

fn assert_stream_of<S, I>()
where
    S: futures::Stream<Item = I>,
{
}

#[test]
fn direvents__new_consensus_and_new_descriptors_are_distinct() {
    assert_ne!(DirEvent::NewConsensus, DirEvent::NewDescriptors);
    assert_eq!(format!("{:?}", DirEvent::NewConsensus), "NewConsensus");
    assert_eq!(format!("{:?}", DirEvent::NewDescriptors), "NewDescriptors");
}

#[test]
fn direvents__events_are_copy_and_comparable() {
    let ev = DirEvent::NewConsensus;
    let copied = ev;
    // `Copy`, so `ev` is still usable after the move-looking binding above.
    assert_eq!(ev, copied);
    crate::common::assert_clone::<DirEvent>();
    crate::common::assert_send_sync::<DirEvent>();
}

#[test]
fn direvents__bootstrap_event_stream_type_is_exported() {
    assert_stream_of::<DirBootstrapEvents, DirBootstrapStatus>();
    assert_eq!(
        std::any::type_name::<<DirBootstrapEvents as futures::Stream>::Item>(),
        std::any::type_name::<DirBootstrapStatus>()
    );
}

#[test]
fn direvents__bootstrap_event_stream_is_cloneable() {
    crate::common::assert_clone::<DirBootstrapEvents>();
    crate::common::assert_debug::<DirBootstrapEvents>();
    crate::common::assert_send_sync::<DirBootstrapEvents>();
}

#[test]
fn direvents__dirmgr_publishes_bootstrap_events() {
    // A `DirMgr` cannot be constructed without a store and a network, so the
    // accessor is pinned by its exact signature rather than by calling it.
    let f: fn(&DirMgr<TokioNativeTlsRuntime>) -> DirBootstrapEvents = DirMgr::bootstrap_events;
    let _ = f;
    assert_eq!(
        std::any::type_name::<DirBootstrapEvents>()
            .rsplit("::")
            .next(),
        Some("DirBootstrapEvents")
    );
}
