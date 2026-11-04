//! Group `destroy_reason` — the reason codes a DESTROY or TRUNCATED cell may
//! carry, their wire encodings, and the human-readable strings they map to.
//!
//! The integer codes and the assigned strings are the ones recorded in
//! `SPEC_NOTES.md` under "Tearing down a circuit, and saying why", and the
//! strings from `TEST_INTERFACE.md` section 10.1; nothing here is derived from
//! the candidate's own
//! encoding of a value it was just handed.

use tor_cell::chancell::msg::{Destroy, DestroyReason};
use tor_cell::relaycell::msg::Truncated;

/// The wire number of a destroy reason.
fn code(reason: DestroyReason) -> u8 {
    reason.into()
}

#[test]
fn destroy_reason__a_destroy_cell_encodes_the_reason_it_was_built_with() {
    let cell = Destroy::new(DestroyReason::PROTOCOL);
    assert_eq!(code(cell.reason()), 1);
    assert_eq!(cell.reason(), DestroyReason::PROTOCOL);

    let cell = Destroy::new(DestroyReason::NOSUCHSERVICE);
    assert_eq!(code(cell.reason()), 12);
}

#[test]
fn destroy_reason__a_truncated_cell_carries_its_reason() {
    let cell = Truncated::new(DestroyReason::TIMEOUT);
    assert_eq!(code(cell.reason()), 10);

    let cell = Truncated::new(DestroyReason::REQUESTED);
    assert_eq!(code(cell.reason()), 3);
}

#[test]
fn destroy_reason__the_recognized_reasons_have_documented_strings() {
    let examples = [
        (DestroyReason::NONE, "No reason"),
        (DestroyReason::PROTOCOL, "Protocol violation"),
        (DestroyReason::INTERNAL, "Internal error"),
        (DestroyReason::REQUESTED, "Client sent a TRUNCATE command"),
        (
            DestroyReason::HIBERNATING,
            "Relay is hibernating and not accepting requests",
        ),
        (DestroyReason::RESOURCELIMIT, "Relay ran out of resources"),
        (DestroyReason::CONNECTFAILED, "Couldn't connect to relay"),
        (
            DestroyReason::OR_IDENTITY,
            "Connected to relay with different OR identity",
        ),
        (
            DestroyReason::CHANNEL_CLOSED,
            "The OR channels carrying this circuit died",
        ),
        (
            DestroyReason::FINISHED,
            "Circuit expired for being too dirty or old",
        ),
        (DestroyReason::TIMEOUT, "Circuit construction took too long"),
        (
            DestroyReason::DESTROYED,
            "Circuit was destroyed without client truncate",
        ),
        (DestroyReason::NOSUCHSERVICE, "No such onion service"),
    ];
    for (reason, expected) in &examples {
        assert_eq!(reason.human_str(), *expected);
    }
}

#[test]
fn destroy_reason__an_unknown_reason_code_is_recognized_as_such() {
    let unassigned = DestroyReason::from(99);
    assert_eq!(unassigned.human_str(), "Unrecognized reason");
    assert!(!unassigned.is_recognized());

    // Every assigned code is recognized; nothing else is.
    for c in 0..=12_u8 {
        assert!(
            DestroyReason::from(c).is_recognized(),
            "code {} should be recognized",
            c
        );
    }
    assert!(!DestroyReason::from(13).is_recognized());
    assert!(!DestroyReason::from(255).is_recognized());
}

#[test]
fn destroy_reason__each_assigned_code_round_trips_through_from_and_into() {
    let codes = [0_u8, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12];
    let mut seen: Vec<&'static str> = Vec::new();
    for wire in codes {
        let reason = DestroyReason::from(wire);
        // Decoding a wire number and re-encoding it yields the same number.
        assert_eq!(u8::from(reason), wire);
        assert_eq!(code(reason), wire);
        assert!(reason.is_recognized(), "code {} should be recognized", wire);
        // Every assigned code has a string of its own; none falls through to
        // the unrecognized case, and no two assigned codes share a string.
        let text = reason.human_str();
        assert_ne!(
            text, "Unrecognized reason",
            "code {} must have an assigned string",
            wire
        );
        assert!(
            !seen.contains(&text),
            "code {} reuses the string {:?}",
            wire,
            text
        );
        seen.push(text);
    }
    assert_eq!(seen.len(), 13);
}

#[test]
fn destroy_reason__the_code_integers_match_the_specification() {
    assert_eq!(code(DestroyReason::NONE), 0);
    assert_eq!(code(DestroyReason::PROTOCOL), 1);
    assert_eq!(code(DestroyReason::INTERNAL), 2);
    assert_eq!(code(DestroyReason::REQUESTED), 3);
    assert_eq!(code(DestroyReason::HIBERNATING), 4);
    assert_eq!(code(DestroyReason::RESOURCELIMIT), 5);
    assert_eq!(code(DestroyReason::CONNECTFAILED), 6);
    assert_eq!(code(DestroyReason::OR_IDENTITY), 7);
    assert_eq!(code(DestroyReason::CHANNEL_CLOSED), 8);
    assert_eq!(code(DestroyReason::FINISHED), 9);
    assert_eq!(code(DestroyReason::TIMEOUT), 10);
    assert_eq!(code(DestroyReason::DESTROYED), 11);
    assert_eq!(code(DestroyReason::NOSUCHSERVICE), 12);
}

#[test]
fn destroy_reason__unknown_codes_preserve_their_wire_value() {
    for code in [13_u8, 14, 77, 255] {
        let reason = DestroyReason::from(code);
        assert_eq!(u8::from(reason), code);
        assert_eq!(reason.human_str(), "Unrecognized reason");
    }
}
