#![allow(non_snake_case)]
use safelog::{sensitive, Sensitive};

#[test]
fn safelogbase__sensitive_new() {
    let s = Sensitive::new(42);
    let _: &i32 = &*s;
}

#[test]
fn safelogbase__sensitive_display_scrubbed() {
    let s = sensitive("secret");
    let display = format!("{}", s);
    assert_eq!(display, "[scrubbed]");
}

#[test]
fn safelogbase__sensitive_debug_scrubbed() {
    let s = sensitive(vec![1, 2, 3]);
    let debug = format!("{:?}", s);
    assert_eq!(debug, "[scrubbed]");
}

#[test]
fn safelogbase__with_safe_logging_suppressed() {
    let s = sensitive("visible");
    let display = safelog::with_safe_logging_suppressed(|| format!("{}", s));
    assert_eq!(display, "visible");
}

#[test]
fn safelogbase__suppression_is_local() {
    let s = sensitive("data");
    let outer = format!("{}", s);
    assert_eq!(outer, "[scrubbed]");
    safelog::with_safe_logging_suppressed(|| {
        let inner = format!("{}", s);
        assert_eq!(inner, "data");
    });
    let after = format!("{}", s);
    assert_eq!(after, "[scrubbed]");
}

#[test]
fn safelogbase__sensitive_unwrap() {
    let s = Sensitive::new(String::from("hidden"));
    let val = Sensitive::unwrap(s);
    assert_eq!(val, "hidden");
}
