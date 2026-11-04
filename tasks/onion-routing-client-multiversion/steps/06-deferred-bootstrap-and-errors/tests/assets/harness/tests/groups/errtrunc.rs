//! Group `errtrunc`: `tor_error::Truncated`, the "read more and try again"
//! signal that the taxonomy keeps separate from real errors.
//!
//! instruction.md: "a structured error taxonomy" and "a diagnostic for HTTP
//! sent to the SOCKS port" - the SOCKS handshake has to tell "incomplete" apart
//! from "wrong protocol", and `Truncated` is how it does so.

use std::error::Error as _;

use tor_error::Truncated;

#[test]
fn errtrunc__display_asks_for_more_input() {
    assert_eq!(
        Truncated::new().to_string(),
        "Incomplete data; more input needed"
    );
}

#[test]
fn errtrunc__default_matches_new() {
    assert_eq!(Truncated::new().to_string(), Truncated::default().to_string());
    assert_eq!(
        format!("{:?}", Truncated::new()),
        format!("{:?}", Truncated::default())
    );
    assert_eq!(format!("{:?}", Truncated::new()), "Truncated");
}

#[test]
fn errtrunc__is_an_error_without_a_source() {
    let t = Truncated::new();
    let as_err: &dyn std::error::Error = &t;
    assert!(as_err.source().is_none());
    assert_eq!(as_err.to_string(), "Incomplete data; more input needed");
}

#[test]
fn errtrunc__socksproto_reexports_the_same_type() {
    // tor-socksproto must publish the *same* Truncated, not a copy of it.
    let t: Truncated = tor_socksproto::Truncated::new();
    assert_eq!(t.to_string(), "Incomplete data; more input needed");
}
