//! Group `safesupp`: code that knows a particular sink is safe can reveal
//! sensitive values for the duration of one call, without changing what any
//! other code sees.
//!
//! Traces to instruction.md section 1 ("safe logging on by default") and the
//! migration note.  Interface: `safelog::with_safe_logging_suppressed`.

use safelog::{sensitive, with_safe_logging_suppressed, Sensitive};

#[test]
fn safesupp__suppression_reveals_the_value() {
    let target = Sensitive::new("www.example.com:443");
    assert_eq!(format!("{}", target), "[scrubbed]");
    assert_eq!(
        with_safe_logging_suppressed(|| format!("{}", target)),
        "www.example.com:443"
    );
}

#[test]
fn safesupp__suppression_ends_with_the_closure() {
    let v = Sensitive::new(1234_u32);
    let inside = with_safe_logging_suppressed(|| format!("{}", v));
    let outside = format!("{}", v);
    assert_eq!(inside, "1234");
    assert_eq!(outside, "[scrubbed]");
}

#[test]
fn safesupp__the_closure_result_is_returned_unchanged() {
    let v = Sensitive::new(41_u32);
    let doubled: u32 = with_safe_logging_suppressed(|| *v + 1);
    assert_eq!(doubled, 42);
    // Any type can come back out, including one that borrows nothing.
    let unit: () = with_safe_logging_suppressed(|| {});
    assert_eq!(unit, ());
}

#[test]
fn safesupp__suppression_nests() {
    let v = sensitive(7_u8);
    let out = with_safe_logging_suppressed(|| {
        let inner = with_safe_logging_suppressed(|| format!("{}", v));
        // Still suppressed after the inner scope ends, because the outer scope
        // is still in force.
        format!("{}/{}", inner, v)
    });
    assert_eq!(out, "7/7");
    assert_eq!(format!("{}", v), "[scrubbed]");
}

#[test]
fn safesupp__suppression_covers_every_format_trait() {
    let v = Sensitive::new(0x0ed19a_u32);
    let rendered = with_safe_logging_suppressed(|| {
        format!("{:?} {} {:o} {:x} {:X} {:b}", v, v, v, v, v, v)
    });
    assert_eq!(rendered, "971162 971162 3550632 ed19a ED19A 11101101000110011010");

    let f = Sensitive::new(1.5_f64);
    assert_eq!(
        with_safe_logging_suppressed(|| format!("{:e} {:E}", f, f)),
        "1.5e0 1.5E0"
    );
}
