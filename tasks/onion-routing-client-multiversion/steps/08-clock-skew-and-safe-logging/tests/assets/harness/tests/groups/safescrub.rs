//! Group `safescrub`: a value marked sensitive renders as `[scrubbed]` through
//! every formatting trait, with no configuration and no opt-in.
//!
//! Traces to instruction.md section 1 ("safe logging on by default") and to the
//! migration note ("Logs stop carrying target addresses unless sensitive
//! logging is explicitly enabled").  Interface: the `std::fmt` implementations
//! on `safelog::Sensitive<T>`.

use safelog::{sensitive, Sensitive};
use std::net::{IpAddr, Ipv4Addr};

/// The exact replacement text.
const SCRUBBED: &str = "[scrubbed]";

#[test]
fn safescrub__display_is_scrubbed() {
    let addr: IpAddr = Ipv4Addr::new(192, 0, 2, 17).into();
    assert_eq!(format!("{}", sensitive(addr)), SCRUBBED);
    assert_eq!(
        format!("connecting to {}", sensitive("www.example.com:443")),
        "connecting to [scrubbed]"
    );
}

#[test]
fn safescrub__debug_is_scrubbed() {
    let addr: IpAddr = Ipv4Addr::new(192, 0, 2, 17).into();
    assert_eq!(format!("{:?}", sensitive(addr)), SCRUBBED);
    assert_eq!(format!("{:?}", Sensitive::new(vec![104_u32, 49])), SCRUBBED);
    // The alternate form is scrubbed too.
    assert_eq!(format!("{:#?}", Sensitive::new(vec![104_u32, 49])), SCRUBBED);
}

#[test]
fn safescrub__integer_radix_formats_are_scrubbed() {
    let v = Sensitive::new(0x0ed19a_u32);
    assert_eq!(format!("{:b}", v), SCRUBBED);
    assert_eq!(format!("{:o}", v), SCRUBBED);
    assert_eq!(format!("{:x}", v), SCRUBBED);
    assert_eq!(format!("{:X}", v), SCRUBBED);
}

#[test]
fn safescrub__exponent_formats_are_scrubbed() {
    let v = Sensitive::new(1.0e32_f64);
    assert_eq!(format!("{:e}", v), SCRUBBED);
    assert_eq!(format!("{:E}", v), SCRUBBED);
}

#[test]
fn safescrub__pointer_format_is_scrubbed() {
    let ptr: *const u8 = std::ptr::null();
    let v = Sensitive::new(ptr);
    assert_eq!(format!("{:p}", v), SCRUBBED);
    assert_eq!(format!("{:?}", v), SCRUBBED);
}

#[test]
fn safescrub__format_flags_do_not_change_the_replacement() {
    let v = Sensitive::new(7_u32);
    // Width, fill, alignment, precision, sign and `#` are all discarded: the
    // replacement is written literally so that no length information leaks.
    assert_eq!(format!("{:>20}", v), SCRUBBED);
    assert_eq!(format!("{:*<20}", v), SCRUBBED);
    assert_eq!(format!("{:08}", v), SCRUBBED);
    assert_eq!(format!("{:+}", v), SCRUBBED);
    assert_eq!(format!("{:#x}", v), SCRUBBED);
}

#[test]
fn safescrub__the_underlying_value_is_still_reachable_in_code() {
    // Scrubbing is a formatting decision only: the program can still use the
    // value, it just cannot print it by accident.
    let v = Sensitive::new(String::from("swordfish"));
    assert_eq!(v.len(), 9);
    assert_eq!(v.as_str(), "swordfish");
    assert_eq!(format!("{}", v), SCRUBBED);
    assert_eq!(Sensitive::unwrap(v), "swordfish");
}
