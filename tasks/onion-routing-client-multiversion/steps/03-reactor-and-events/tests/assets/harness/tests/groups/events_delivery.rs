//! Group `events_delivery` - events actually reach subscribed receivers.
//!
//! instruction.md section 1: "a typed event framework".
//!
//! Graded items: `tor_events::broadcast`, the `futures::Stream` implementation
//! of `tor_events::TorEventReceiver`, and the interaction between the two.
//!
//! Determinism: `subscribe()` activates the receiver before `broadcast()` is
//! called, and a receiver only ever observes events broadcast after it was
//! activated. So each case waits for events it has itself queued, with no
//! sleep and no timeout, and no case can observe another case's events.

// The step-03 helpers live apart from `common` so that no gate group is
// forced to compile them; see the note at the top of common/current.rs.
use crate::current as common;

use tor_events::broadcast;
use tor_events::events::{TorEvent, TorEventKind};

#[test]
fn events_delivery__subscribed_receiver_gets_the_event() {
    let _events = common::events_guard();
    let mut rx = common::event_receiver();
    rx.subscribe(TorEventKind::Empty);
    broadcast(TorEvent::Empty);
    assert_eq!(common::next_event(&mut rx), Some(TorEvent::Empty));
}

#[test]
fn events_delivery__receiver_without_subscriptions_ends_its_stream() {
    let _events = common::events_guard();
    let mut rx = common::event_receiver();
    // A receiver that is subscribed to nothing would never yield anything, so
    // its stream reports end-of-stream rather than blocking forever.
    assert_eq!(common::next_event(&mut rx), None);
}

#[test]
fn events_delivery__every_subscriber_gets_a_copy() {
    let _events = common::events_guard();
    let mut rx1 = common::event_receiver();
    let mut rx2 = common::event_receiver();
    rx1.subscribe(TorEventKind::Empty);
    rx2.subscribe(TorEventKind::Empty);

    broadcast(TorEvent::Empty);

    assert_eq!(common::next_event(&mut rx1), Some(TorEvent::Empty));
    assert_eq!(common::next_event(&mut rx2), Some(TorEvent::Empty));
}

#[test]
fn events_delivery__queued_events_are_all_delivered() {
    let _events = common::events_guard();
    let mut rx = common::event_receiver();
    rx.subscribe(TorEventKind::Empty);

    broadcast(TorEvent::Empty);
    broadcast(TorEvent::Empty);
    broadcast(TorEvent::Empty);

    for _ in 0..3 {
        assert_eq!(common::next_event(&mut rx), Some(TorEvent::Empty));
    }
}

#[test]
fn events_delivery__unsubscribing_ends_the_stream() {
    let _events = common::events_guard();
    let mut rx = common::event_receiver();
    rx.subscribe(TorEventKind::Empty);
    broadcast(TorEvent::Empty);
    assert_eq!(common::next_event(&mut rx), Some(TorEvent::Empty));

    rx.unsubscribe(TorEventKind::Empty);
    // With no remaining subscriptions the stream reports end-of-stream.
    assert_eq!(common::next_event(&mut rx), None);
}
