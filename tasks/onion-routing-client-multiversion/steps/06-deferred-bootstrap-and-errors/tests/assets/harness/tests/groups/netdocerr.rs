//! Group `netdocerr`: Public error types from `tor-netdoc`.
//!
//! instruction.md: "Configuration-file watching; expiry of unused channels; an
//! unbootstrapped `TorClient` with deferred and lazy bootstrap; a structured
//! error taxonomy; an HTTP client over Arti; and a diagnostic for HTTP sent
//! to the SOCKS port."
//!
//! TEST_INTERFACE.md: `tor_netdoc::{ParseErrorKind, Pos}`

use tor_netdoc::{ParseErrorKind, Pos};

#[test]
fn netdocerr__parseerrorkind_display_internal() {
    assert_eq!(ParseErrorKind::Internal.to_string(), "internal error");
}

#[test]
fn netdocerr__parseerrorkind_display_badapiusage() {
    assert_eq!(ParseErrorKind::BadApiUsage.to_string(), "bad API usage");
}

#[test]
fn netdocerr__parseerrorkind_display_missingkeyword() {
    assert_eq!(ParseErrorKind::MissingKeyword.to_string(), "no keyword for entry");
}

#[test]
fn netdocerr__parseerrorkind_display_badsignature() {
    assert_eq!(ParseErrorKind::BadSignature.to_string(), "couldn't validate signature");
}

#[test]
fn netdocerr__parseerrorkind_copy_and_comparable() {
    let a = ParseErrorKind::Internal;
    let b = a; // Copy
    assert_eq!(a, b);
    assert_ne!(a, ParseErrorKind::BadApiUsage);
}

#[test]
fn netdocerr__pos_display_from_offset_inside_string() {
    let text = "abc\ndef";
    let pos = Pos::from_offset(text, 5);
    assert_eq!(pos.to_string(), " on line 2, byte 2");
}

#[test]
fn netdocerr__pos_display_from_offset_outside_string() {
    let text = "abc";
    let pos = Pos::from_offset(text, 10);
    assert_eq!(pos.to_string(), " at invalid offset at index 10");
}
