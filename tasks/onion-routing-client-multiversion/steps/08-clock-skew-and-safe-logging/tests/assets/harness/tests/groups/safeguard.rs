//! Group `safeguard`: the process-wide switches that turn safe logging off for
//! debugging, or pin it on so that nothing else can turn it off.
//!
//! Traces to instruction.md section 1 ("safe logging on by default") and the
//! migration note ("unless sensitive logging is explicitly enabled").
//! Interface: `safelog::{enforce_safe_logging, disable_safe_logging, Guard,
//! Error}`.
//!
//! Every case restores the global state before it returns, and the runner
//! executes one group per process, so no case can observe another's guards.

use safelog::{disable_safe_logging, enforce_safe_logging, Error, Sensitive};

#[test]
fn safeguard__safe_logging_is_on_without_any_guard() {
    let v = Sensitive::new("192.0.2.9:9001");
    assert_eq!(format!("{}", v), "[scrubbed]");
}

#[test]
fn safeguard__disabling_safe_logging_reveals_values() {
    let v = Sensitive::new("192.0.2.9:9001");
    let guard = disable_safe_logging().expect("nothing should be enforcing safe logging");
    assert_eq!(format!("{}", v), "192.0.2.9:9001");
    assert_eq!(format!("{:?}", v), "\"192.0.2.9:9001\"");
    drop(guard);
}

#[test]
fn safeguard__dropping_the_guard_restores_scrubbing() {
    let v = Sensitive::new(5150_u32);
    {
        let _g = disable_safe_logging().expect("nothing should be enforcing safe logging");
        assert_eq!(format!("{}", v), "5150");
    }
    assert_eq!(format!("{}", v), "[scrubbed]");
}

#[test]
fn safeguard__enforcing_safe_logging_blocks_disabling_it() {
    let v = Sensitive::new(5150_u32);
    let g = enforce_safe_logging().expect("nothing should have disabled safe logging");
    let err = disable_safe_logging().err().expect("disabling must be refused");
    assert!(
        matches!(err, Error::AlreadySafe),
        "expected AlreadySafe, got {:?}",
        err
    );
    assert_eq!(format!("{}", v), "[scrubbed]");
    drop(g);
    // Once the enforcement is gone, disabling works again.
    let g2 = disable_safe_logging().expect("enforcement should have been lifted");
    assert_eq!(format!("{}", v), "5150");
    drop(g2);
}

#[test]
fn safeguard__disabling_safe_logging_blocks_enforcing_it() {
    let g = disable_safe_logging().expect("nothing should be enforcing safe logging");
    let err = enforce_safe_logging()
        .err()
        .expect("enforcement must be refused");
    assert!(
        matches!(err, Error::AlreadyUnsafe),
        "expected AlreadyUnsafe, got {:?}",
        err
    );
    drop(g);
    let g2 = enforce_safe_logging().expect("the disable guard should have been dropped");
    drop(g2);
}

#[test]
fn safeguard__the_errors_explain_the_conflict() {
    assert_eq!(
        Error::AlreadySafe.to_string(),
        "Cannot enable unsafe logging: safe logging is already enforced."
    );
    assert_eq!(
        Error::AlreadyUnsafe.to_string(),
        "Cannot enforce safe logging: unsafe logging is already enabled."
    );
    assert_eq!(
        Error::Overflow.to_string(),
        "Too many calls to enforce or disable safe logging."
    );
}

#[test]
fn safeguard__guards_of_the_same_kind_nest() {
    let v = Sensitive::new(5150_u32);
    let outer = disable_safe_logging().expect("nothing should be enforcing safe logging");
    let inner = disable_safe_logging().expect("a second disable guard is allowed");
    assert_eq!(format!("{}", v), "5150");
    drop(inner);
    // One guard is still held, so logging is still unsafe.
    assert_eq!(format!("{}", v), "5150");
    drop(outer);
    assert_eq!(format!("{}", v), "[scrubbed]");
}
