//! Group `events_kinds` - the typed event vocabulary.
//!
//! instruction.md section 1: "a typed event framework".
//!
//! Graded items (see TEST_INTERFACE.md): `tor_events::events::TorEvent`,
//! `tor_events::events::TorEventKind`, `TorEvent::kind`.
//!
//! No case here touches the process-wide reactor, so nothing in this group
//! depends on ordering.

use std::collections::{BTreeSet, HashSet};
use tor_events::events::{TorEvent, TorEventKind};

#[test]
fn events_kinds__empty_event_reports_empty_kind() {
    assert_eq!(TorEvent::Empty.kind(), TorEventKind::Empty);
}

#[test]
fn events_kinds__kind_indexes_from_zero() {
    // `TorEventKind` doubles as an index into the subscription table, so its
    // first discriminant has to be zero.
    assert_eq!(TorEventKind::Empty as usize, 0);
}

#[test]
fn events_kinds__event_serde_round_trip() {
    let encoded = serde_json::to_string(&TorEvent::Empty).expect("TorEvent is not Serialize");
    assert_eq!(encoded, "\"Empty\"");
    let decoded: TorEvent = serde_json::from_str(&encoded).expect("TorEvent is not Deserialize");
    assert_eq!(decoded, TorEvent::Empty);
}

#[test]
fn events_kinds__kind_is_copy_ordered_and_hashable() {
    let k = TorEventKind::Empty;
    let copied = k; // requires Copy
    assert_eq!(k, copied);
    assert_eq!(k.cmp(&copied), std::cmp::Ordering::Equal);
    assert!(!(k < copied));

    let mut ordered = BTreeSet::new();
    assert!(ordered.insert(k));
    assert!(!ordered.insert(copied));
    assert_eq!(ordered.len(), 1);

    let mut hashed = HashSet::new();
    assert!(hashed.insert(k));
    assert!(!hashed.insert(copied));
    assert_eq!(hashed.len(), 1);
}

#[test]
fn events_kinds__debug_and_clone_preserve_the_variant() {
    assert_eq!(format!("{:?}", TorEvent::Empty), "Empty");
    assert_eq!(format!("{:?}", TorEventKind::Empty), "Empty");

    let cloned = TorEvent::Empty.clone();
    assert_eq!(cloned, TorEvent::Empty);
    assert_eq!(cloned.kind(), TorEvent::Empty.kind());
}
