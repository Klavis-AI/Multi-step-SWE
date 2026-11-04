//! Group `errkind`: the `tor-error` classification surface.
//!
//! INHERITED. `ErrorKind`, `HasKind`, `Bug`, the `internal!` and
//! `bad_api_usage!` constructors and the `Report` wrapper all existed at the
//! previous milestone and must still behave the same way here: this milestone
//! adds a congestion module, a padding subsystem and a configuration directory,
//! all of which report failures through this vocabulary, so a regression in it
//! is a regression in every new subsystem's error reporting at once.
#![allow(non_snake_case)]

use std::error::Error as StdError;
use std::fmt::{self, Display};

use tor_error::{bad_api_usage, internal, ErrorKind, HasKind, Report};

/// The innermost cause of the chain used by the `Report` case.
#[derive(Debug)]
struct Inner;

impl Display for Inner {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "inner cause")
    }
}

impl StdError for Inner {}

/// An error that mentions its own source in its message.
#[derive(Debug)]
struct Verbose(Inner);

impl Display for Verbose {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "outer failure - {}", self.0)
    }
}

impl StdError for Verbose {
    fn source(&self) -> Option<&(dyn StdError + 'static)> {
        Some(&self.0)
    }
}

/// An error that does not mention its source.
#[derive(Debug)]
struct Terse(Inner);

impl Display for Terse {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "outer failure")
    }
}

impl StdError for Terse {
    fn source(&self) -> Option<&(dyn StdError + 'static)> {
        Some(&self.0)
    }
}

/// Each kind renders as a fixed human sentence; callers put these in messages.
#[test]
fn errkind__display_is_the_documented_sentence() {
    assert_eq!(ErrorKind::Internal.to_string(), "internal error (bug)");
    assert_eq!(ErrorKind::BadApiUsage.to_string(), "bad API usage (bug)");
    assert_eq!(
        ErrorKind::FeatureDisabled.to_string(),
        "operation not supported because Arti feature disabled"
    );
}

/// `internal!` builds a `Bug` classified as `Internal`, and its display names
/// the kind, the call site and the message.
#[test]
fn errkind__internal_macro_reports_internal_kind() {
    let bug = internal!("no cells left in {}", "the queue");
    assert_eq!(bug.kind(), ErrorKind::Internal);
    let text = bug.to_string();
    assert!(
        text.starts_with("internal error (bug) at "),
        "a Bug leads with its kind and location: {text}"
    );
    assert!(
        text.contains("errkind.rs"),
        "the captured location is this call site: {text}"
    );
    assert!(
        text.contains("no cells left in the queue"),
        "the formatted message survives: {text}"
    );
}

/// `bad_api_usage!` is the same construction with a different classification,
/// which is the whole point of having both.
#[test]
fn errkind__bad_api_usage_macro_reports_its_kind() {
    let bug = bad_api_usage!("called {} twice", "connect");
    assert_eq!(bug.kind(), ErrorKind::BadApiUsage);
    assert_ne!(bug.kind(), internal!("x").kind());
    let text = bug.to_string();
    assert!(
        text.starts_with("bad API usage (bug) at "),
        "unexpected rendering: {text}"
    );
    assert!(text.contains("called connect twice"), "{text}");
}

/// `ErrorKind` is a `Copy` value type with equality, so callers can match and
/// store it without borrowing the error it came from.
#[test]
fn errkind__kinds_are_copy_and_distinct() {
    let k = ErrorKind::Internal;
    let copied = k;
    assert_eq!(k, copied, "Copy, not move");
    assert_ne!(ErrorKind::Internal, ErrorKind::BadApiUsage);
    assert_ne!(ErrorKind::Internal, ErrorKind::FeatureDisabled);
    // Copy semantics: `k` is still usable after being passed by value.
    fn by_value(k: ErrorKind) -> ErrorKind {
        k
    }
    assert_eq!(by_value(k), ErrorKind::Internal);
}

/// `Report` prefixes with `error` and walks the source chain, skipping any
/// source whose text the previous message already quoted.
#[test]
fn errkind__report_chains_sources_without_repeating() {
    let terse: Box<dyn StdError> = Box::new(Terse(Inner));
    assert_eq!(
        Report(&terse).to_string(),
        "error: outer failure: inner cause"
    );

    let verbose: Box<dyn StdError> = Box::new(Verbose(Inner));
    assert_eq!(
        Report(&verbose).to_string(),
        "error: outer failure - inner cause",
        "a source already quoted by its parent is not repeated"
    );

    let alone: Box<dyn StdError> = Box::new(Inner);
    assert_eq!(Report(&alone).to_string(), "error: inner cause");
}
