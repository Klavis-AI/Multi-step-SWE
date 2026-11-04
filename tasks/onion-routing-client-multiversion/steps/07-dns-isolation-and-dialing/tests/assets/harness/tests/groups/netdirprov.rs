//! Group `netdirprov`: the `NetDirProvider` trait from `tor-netdir`,
//! returning the latest directory and an event stream.
//!
//! Graded items: `tor_netdir::{NetDirProvider, DirEvent}`.

use std::sync::Arc;

use futures::StreamExt;
use tor_netdir::{DirEvent, NetDirProvider};
use tor_rtcompat::SleepProviderExt;

/// A minimal first-party `NetDirProvider` that always returns None.
struct EmptyProvider;

impl NetDirProvider for EmptyProvider {
    fn latest_netdir(&self) -> Option<Arc<tor_netdir::NetDir>> {
        None
    }

    fn events(&self) -> futures::stream::BoxStream<'static, DirEvent> {
        Box::pin(futures::stream::empty())
    }
}

#[test]
fn netdirprov__trait_implemented_for_empty_provider() {
    let prov = EmptyProvider;
    assert!(prov.latest_netdir().is_none());
}

#[test]
fn netdirprov__events_stream_from_empty_provider() {
    tor_rtcompat::test_with_one_runtime!(|rt| async move {
        let prov = EmptyProvider;
        let mut stream = prov.events();

        // A consumer of `events()` has to tell two situations apart, because
        // they call for opposite responses: "no events yet, keep waiting" and
        // "no events ever, stop waiting".  A terminated stream yields `None`
        // promptly; a stream that is merely idle yields nothing at all.  A
        // consumer that treated termination as idleness would wait forever.
        let next = rt
            .timeout(std::time::Duration::from_millis(50), stream.next())
            .await;
        assert!(
            next.is_ok(),
            "a terminated event stream must resolve, not hang"
        );
        assert!(
            next.unwrap().is_none(),
            "a provider with no events must end its stream rather than yield one"
        );

        // The contrast: an idle stream does time out, and that is what the
        // `is_err()` branch above would have been asserting if `empty()` pended.
        let mut idle = futures::stream::pending::<DirEvent>();
        let nothing = rt
            .timeout(std::time::Duration::from_millis(50), idle.next())
            .await;
        assert!(nothing.is_err(), "an idle stream must not resolve");
    });
}

#[test]
fn netdirprov__arc_wrapper_delegates() {
    let prov = Arc::new(EmptyProvider);
    assert!(prov.latest_netdir().is_none());
}

#[test]
fn netdirprov__direvent_variants_exist() {
    let _new_consensus = DirEvent::NewConsensus;
    let _new_descriptors = DirEvent::NewDescriptors;
}

#[test]
fn netdirprov__direvent_equality() {
    assert_eq!(DirEvent::NewConsensus, DirEvent::NewConsensus);
    assert_eq!(DirEvent::NewDescriptors, DirEvent::NewDescriptors);
    assert_ne!(DirEvent::NewConsensus, DirEvent::NewDescriptors);
}
