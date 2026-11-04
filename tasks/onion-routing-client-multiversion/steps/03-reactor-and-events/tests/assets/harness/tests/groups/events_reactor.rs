//! Group `events_reactor` - the event reactor singleton and its parameters.
//!
//! instruction.md section 1: "a typed event framework".
//!
//! Graded items: `tor_events::EventReactor::{new, receiver, run}`,
//! `tor_events::BROADCAST_CAPACITY`, `tor_events::ReceiverError`, and the
//! subscriber gate in front of `tor_events::broadcast`.

// The step-03 helpers live apart from `common` so that no gate group is
// forced to compile them; see the note at the top of common/current.rs.
use crate::current as common;

use tor_events::events::{TorEvent, TorEventKind};
use tor_events::{broadcast, event_has_subscribers, EventReactor, ReceiverError};

#[test]
fn events_reactor__broadcast_capacity_is_512() {
    let _events = common::events_guard();
    assert_eq!(tor_events::BROADCAST_CAPACITY, 512);
}

#[test]
fn events_reactor__reactor_is_a_singleton() {
    let _events = common::events_guard();
    // `init_events` performs the one successful `EventReactor::new()` for this
    // process and drives it. Every later attempt must decline.
    common::init_events();
    assert!(EventReactor::new().is_none());
    assert!(EventReactor::new().is_none());
}

#[test]
fn events_reactor__receiver_is_available_once_initialized() {
    let _events = common::events_guard();
    assert!(EventReactor::receiver().is_some());
    // Receivers are handed out repeatedly, not once.
    assert!(EventReactor::receiver().is_some());
}

#[test]
fn events_reactor__broadcast_is_gated_on_subscribers() {
    let _events = common::events_guard();
    assert!(!event_has_subscribers(TorEventKind::Empty));

    // Broadcasting with nobody listening is a no-op: it must not panic and
    // must not invent a subscriber.
    broadcast(TorEvent::Empty);
    assert!(!event_has_subscribers(TorEventKind::Empty));

    // The system is still live afterwards.
    let mut rx = common::event_receiver();
    rx.subscribe(TorEventKind::Empty);
    assert!(event_has_subscribers(TorEventKind::Empty));
    broadcast(TorEvent::Empty);
    assert_eq!(common::next_event(&mut rx), Some(TorEvent::Empty));
    drop(rx);
    assert!(!event_has_subscribers(TorEventKind::Empty));
}

#[test]
fn events_reactor__receiver_error_messages() {
    let _events = common::events_guard();
    assert_eq!(
        ReceiverError::NoSubscriptions.to_string(),
        "no event subscriptions"
    );
    assert_eq!(
        ReceiverError::ChannelClosed.to_string(),
        "internal event broadcast channel closed"
    );
}
