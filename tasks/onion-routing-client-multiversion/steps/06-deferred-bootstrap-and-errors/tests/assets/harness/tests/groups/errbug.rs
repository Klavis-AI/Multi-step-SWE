//! Group `errbug`: `tor_error::Bug`, the "this is a programming error" type,
//! and the four macros that build one.
//!
//! instruction.md: "a structured error taxonomy". `Bug` is the part of that
//! taxonomy that reports internal faults; every crate in the tree funnels its
//! own programming errors through it, and its kind must survive that trip.

use std::error::Error as _;
use std::str::FromStr;

use tor_error::{bad_api_usage, internal, into_bad_api_usage, into_internal};
use tor_error::{Bug, ErrorKind, HasKind};

#[test]
fn errbug__new_reports_the_requested_kind() {
    assert_eq!(
        Bug::new(ErrorKind::Internal, "the kettle is empty").kind(),
        ErrorKind::Internal
    );
    assert_eq!(
        Bug::new(ErrorKind::BadApiUsage, "the kettle is empty").kind(),
        ErrorKind::BadApiUsage
    );
}

#[test]
fn errbug__display_starts_with_the_kind() {
    let b = Bug::new(ErrorKind::Internal, "the kettle is empty");
    assert!(
        b.to_string().starts_with("internal error (bug) at "),
        "unexpected rendering: {:?}",
        b.to_string()
    );
    let b = Bug::new(ErrorKind::BadApiUsage, "the kettle is empty");
    assert!(
        b.to_string().starts_with("bad API usage (bug) at "),
        "unexpected rendering: {:?}",
        b.to_string()
    );
}

#[test]
fn errbug__display_records_the_call_site() {
    // `Bug` captures the location of its construction, so that an internal
    // fault can be traced without a debugger.
    let b = Bug::new(ErrorKind::Internal, "the kettle is empty");
    let s = b.to_string();
    assert!(
        s.contains("errbug.rs:"),
        "the call site of Bug::new must be recorded, got {:?}",
        s
    );
}

#[test]
fn errbug__display_contains_the_message() {
    let b = Bug::new(ErrorKind::Internal, "the kettle is empty".to_string());
    assert!(b.to_string().contains("the kettle is empty"));
    // A freshly constructed Bug has no underlying cause.
    assert!(b.source().is_none());
}

#[test]
fn errbug__internal_macro_builds_an_internal_bug() {
    let b = internal!("Couldn't {} the {}.", "wobble", "wobbling device");
    assert_eq!(b.kind(), ErrorKind::Internal);
    let s = b.to_string();
    assert!(s.starts_with("internal error (bug) at "));
    assert!(s.contains("Couldn't wobble the wobbling device."));
    assert!(b.source().is_none());
}

#[test]
fn errbug__bad_api_usage_macro_builds_a_bad_api_usage_bug() {
    let b = bad_api_usage!("no {} available", "targets");
    assert_eq!(b.kind(), ErrorKind::BadApiUsage);
    let s = b.to_string();
    assert!(s.starts_with("bad API usage (bug) at "));
    assert!(s.contains("no targets available"));
}

#[test]
fn errbug__from_error_keeps_the_source() {
    let io = std::io::Error::new(std::io::ErrorKind::PermissionDenied, "no entry");
    let b = Bug::from_error(ErrorKind::Internal, io, "while opening the state file");
    assert_eq!(b.kind(), ErrorKind::Internal);
    assert!(b.to_string().contains("while opening the state file"));
    let src = b.source().expect("Bug::from_error must retain its source");
    assert_eq!(src.to_string(), "no entry");
}

#[test]
fn errbug__into_internal_maps_a_foreign_error() {
    let inner = u32::from_str("penguin").expect_err("'penguin' is not a number");
    let b = u32::from_str("penguin")
        .map_err(into_internal!("{} is not a number", "penguin"))
        .expect_err("'penguin' is not a number");
    assert_eq!(b.kind(), ErrorKind::Internal);
    assert!(b.to_string().contains("penguin is not a number"));
    assert_eq!(
        b.source()
            .expect("into_internal! must retain the source error")
            .to_string(),
        inner.to_string()
    );
}

#[test]
fn errbug__into_bad_api_usage_maps_a_foreign_error() {
    let inner = u32::from_str("penguin").expect_err("'penguin' is not a number");
    let b = u32::from_str("penguin")
        .map_err(into_bad_api_usage!("caller passed {:?}", "penguin"))
        .expect_err("'penguin' is not a number");
    assert_eq!(b.kind(), ErrorKind::BadApiUsage);
    assert!(b.to_string().contains(r#"caller passed "penguin""#));
    assert_eq!(
        b.source()
            .expect("into_bad_api_usage! must retain the source error")
            .to_string(),
        inner.to_string()
    );
}
