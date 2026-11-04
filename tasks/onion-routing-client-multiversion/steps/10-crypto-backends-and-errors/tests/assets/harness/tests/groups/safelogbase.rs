//! Inherited: `safelog::Sensitive` keeps privacy-relevant values out of
//! formatted output unless the program has explicitly opted in.  These cases
//! only read and restore the global flag through the crate's own guards, so
//! they leave the process exactly as they found it.

#![allow(non_snake_case)]

use safelog::{
    disable_safe_logging, enforce_safe_logging, sensitive, with_safe_logging_suppressed, Sensitive,
};

/// The value every case scrubs.
const SECRET: &str = "192.0.2.7:9001";

#[test]
fn safelogbase__a_sensitive_value_is_scrubbed_by_default() {
    let s = sensitive(SECRET);
    assert_eq!(format!("{}", s), "[scrubbed]");
    assert_eq!(format!("{:?}", s), "[scrubbed]");
    // The wrapper has to hide the value even when the caller asks for a
    // non-Display formatting.
    assert_eq!(format!("{:x}", sensitive(0xdead_beef_u32)), "[scrubbed]");
    assert_eq!(format!("{:o}", sensitive(64_u32)), "[scrubbed]");
}

#[test]
fn safelogbase__suppression_is_scoped_to_the_closure() {
    let shown = with_safe_logging_suppressed(|| format!("{}", sensitive(SECRET)));
    assert_eq!(shown, SECRET);
    // ... and the scrubbing is back as soon as the closure returns.
    assert_eq!(format!("{}", sensitive(SECRET)), "[scrubbed]");

    // The closure's return value is passed through unchanged.
    let n = with_safe_logging_suppressed(|| 17_u32);
    assert_eq!(n, 17);
}

#[test]
fn safelogbase__a_disable_guard_is_undone_when_it_drops() {
    {
        let _g = disable_safe_logging().expect("nothing is enforcing safe logging");
        assert_eq!(format!("{}", sensitive(SECRET)), SECRET);
        assert_eq!(format!("{:?}", sensitive(vec![1_u32, 2])), "[1, 2]");
    }
    assert_eq!(format!("{}", sensitive(SECRET)), "[scrubbed]");
}

#[test]
fn safelogbase__enforcement_and_disabling_cannot_overlap() {
    let guard = disable_safe_logging().expect("unsafe logging can be turned on");
    let err = enforce_safe_logging().expect_err("enforcement must lose to an active disable guard");
    assert!(matches!(err, safelog::Error::AlreadyUnsafe), "got {:?}", err);
    drop(guard);

    let guard = enforce_safe_logging().expect("safe logging can be enforced");
    let err = disable_safe_logging().expect_err("disabling must lose to active enforcement");
    assert!(matches!(err, safelog::Error::AlreadySafe), "got {:?}", err);
    // Enforcement blocks the *global* switch only: the per-closure escape
    // hatch is a deliberate, locally scoped opt-in and still works.
    assert_eq!(format!("{}", sensitive(SECRET)), "[scrubbed]");
    assert_eq!(
        with_safe_logging_suppressed(|| format!("{}", sensitive(SECRET))),
        SECRET
    );
    drop(guard);
    assert_eq!(format!("{}", sensitive(SECRET)), "[scrubbed]");
}

#[test]
fn safelogbase__the_wrapper_is_transparent_to_the_program_itself() {
    // Scrubbing is a formatting-time behaviour only: the inner value is still
    // fully available to code that asks for it.
    let s: Sensitive<u32> = 1234.into();
    assert_eq!(Sensitive::unwrap(s), 1234);
    // `Sensitive<T>` derefs to `T`, so the program reads it as usual.
    assert_eq!(*sensitive(SECRET), SECRET);
    assert_eq!(sensitive(SECRET).len(), SECRET.len());
    assert_eq!(Sensitive::new(7_u32), sensitive(7_u32));
    assert_eq!(format!("{}", sensitive(SECRET)), "[scrubbed]");
}
