//! Group `events_subscription` - who is listening for which kind of event.
//!
//! instruction.md section 1: "a typed event framework".
//!
//! Graded items: `tor_events::EventReactor::receiver`,
//! `tor_events::TorEventReceiver::{subscribe, unsubscribe}`, the `Drop`
//! behaviour of `TorEventReceiver`, and `tor_events::event_has_subscribers`.
//!
//! Every case leaves the subscription count back at zero - each receiver it
//! creates is dropped before the case returns - so the cases are independent
//! of the order in which they run.

// The step-03 helpers live apart from `common` so that no gate group is
// forced to compile them; see the note at the top of common/current.rs.
use crate::current as common;

use tor_events::event_has_subscribers;
use tor_events::events::TorEventKind;

#[test]
fn events_subscription__receiver_alone_subscribes_to_nothing() {
    let _events = common::events_guard();
    assert!(!event_has_subscribers(TorEventKind::Empty));

    let rx = common::event_receiver();
    // Merely obtaining a receiver must not register interest in anything.
    assert!(!event_has_subscribers(TorEventKind::Empty));
    drop(rx);
    assert!(!event_has_subscribers(TorEventKind::Empty));
}

#[test]
fn events_subscription__subscribe_registers_interest() {
    let _events = common::events_guard();
    let mut rx = common::event_receiver();
    assert!(!event_has_subscribers(TorEventKind::Empty));
    rx.subscribe(TorEventKind::Empty);
    assert!(event_has_subscribers(TorEventKind::Empty));
    rx.unsubscribe(TorEventKind::Empty);
    assert!(!event_has_subscribers(TorEventKind::Empty));
}

#[test]
fn events_subscription__subscribe_is_idempotent() {
    let _events = common::events_guard();
    let mut rx = common::event_receiver();
    rx.subscribe(TorEventKind::Empty);
    rx.subscribe(TorEventKind::Empty);
    rx.subscribe(TorEventKind::Empty);
    assert!(event_has_subscribers(TorEventKind::Empty));

    // Three subscribes must be undone by a single unsubscribe.
    rx.unsubscribe(TorEventKind::Empty);
    assert!(!event_has_subscribers(TorEventKind::Empty));
}

#[test]
fn events_subscription__unsubscribe_is_idempotent() {
    let _events = common::events_guard();
    let mut rx = common::event_receiver();
    rx.subscribe(TorEventKind::Empty);
    rx.unsubscribe(TorEventKind::Empty);
    rx.unsubscribe(TorEventKind::Empty);
    rx.unsubscribe(TorEventKind::Empty);
    assert!(!event_has_subscribers(TorEventKind::Empty));

    // ... and the receiver is still usable afterwards.
    rx.subscribe(TorEventKind::Empty);
    assert!(event_has_subscribers(TorEventKind::Empty));
    drop(rx);
    assert!(!event_has_subscribers(TorEventKind::Empty));
}

#[test]
fn events_subscription__dropping_a_receiver_unsubscribes_it() {
    let _events = common::events_guard();
    let mut rx = common::event_receiver();
    rx.subscribe(TorEventKind::Empty);
    assert!(event_has_subscribers(TorEventKind::Empty));
    drop(rx);
    assert!(!event_has_subscribers(TorEventKind::Empty));
}

#[test]
fn events_subscription__receivers_are_counted_independently() {
    let _events = common::events_guard();
    let mut rx1 = common::event_receiver();
    let mut rx2 = common::event_receiver();
    rx1.subscribe(TorEventKind::Empty);
    rx2.subscribe(TorEventKind::Empty);
    assert!(event_has_subscribers(TorEventKind::Empty));

    // One of two subscribers going away must not clear the flag.
    drop(rx1);
    assert!(event_has_subscribers(TorEventKind::Empty));
    drop(rx2);
    assert!(!event_has_subscribers(TorEventKind::Empty));
}
