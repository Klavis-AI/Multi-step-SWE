//! Group `padparams`: padding timing parameters (tor-proto).
//!
//! The `Parameters` struct carries `low`/`high` bounds in milliseconds for the
//! padding timer. At v1.0.0 these fields exist, `default_padding()` and
//! `disabled()` are public, and `padding_negotiate_cell()` converts to a cell.
//! At v0.6.0 the fields were named `low_ms`/`high_ms` and none of those methods
//! existed.
//!
//! `padding_negotiate_cell` widens nothing: the parameters are `u32`
//! milliseconds and the cell fields are `u16`, so the conversion is fallible
//! and returns `tor_error::Bug` on overflow.

use tor_cell::chancell::msg::{Body, PaddingNegotiate, PaddingNegotiateCmd};
use tor_proto::channel::padding::Parameters;
use tor_units::IntegerMilliseconds;

use crate::common::encode_body;

/// The wire body of the PADDING_NEGOTIATE cell these parameters ask for.
fn negotiate_body(p: &Parameters) -> Vec<u8> {
    let cell = p
        .padding_negotiate_cell()
        .expect("parameters in u16 range convert to a cell");
    encode_body(cell.into_message())
}

/// The spec default distribution is 1500..9500 ms, encoded as START.
#[test]
fn padparams__default_padding_returns_1500_9500() {
    let body = negotiate_body(&Parameters::default_padding());
    assert_eq!(
        body,
        [0x00, 0x02, 0x05, 0xDC, 0x25, 0x1C],
        "default_padding should negotiate START(1500,9500), got {body:02x?}"
    );
}

/// `disabled()` is the both-bounds-zero sentinel, which still encodes as START.
#[test]
fn padparams__disabled_returns_zero_bounds() {
    let body = negotiate_body(&Parameters::disabled());
    assert_eq!(
        body,
        [0x00, 0x02, 0x00, 0x00, 0x00, 0x00],
        "disabled should negotiate START(0,0), got {body:02x?}"
    );
}

/// The builder carries explicit bounds through to the wire, big-endian.
#[test]
fn padparams__builder_constructs_with_explicit_bounds() {
    let p = Parameters::builder()
        .low(IntegerMilliseconds::new(2000u32))
        .high(IntegerMilliseconds::new(8000u32))
        .build()
        .expect("2000..8000 is a buildable range");
    let body = negotiate_body(&p);
    assert_eq!(body[0], 0x00, "version byte");
    assert_eq!(body[1], u8::from(PaddingNegotiateCmd::START), "command byte");
    assert_eq!(u16::from_be_bytes([body[2], body[3]]), 2000);
    assert_eq!(u16::from_be_bytes([body[4], body[5]]), 8000);
}

/// An unconfigured builder supplies the spec defaults rather than zeroes.
#[test]
fn padparams__builder_defaults_to_1500_9500() {
    let p = Parameters::builder()
        .build()
        .expect("every field has a builder default");
    assert_eq!(negotiate_body(&p), [0x00, 0x02, 0x05, 0xDC, 0x25, 0x1C]);
}

/// `padding_negotiate_cell` produces START, not STOP: it says "pad me like
/// this", and stopping is a separate message.
#[test]
fn padparams__padding_negotiate_cell_converts_to_start() {
    let p = Parameters::builder()
        .low(IntegerMilliseconds::new(1500u32))
        .high(IntegerMilliseconds::new(9500u32))
        .build()
        .expect("1500..9500 is a buildable range");
    let cell = p.padding_negotiate_cell().expect("in-range conversion");
    assert_eq!(
        cell,
        PaddingNegotiate::start(
            IntegerMilliseconds::new(1500u16),
            IntegerMilliseconds::new(9500u16)
        )
    );
    assert_ne!(cell, PaddingNegotiate::stop());
}

/// A `low` above `u16::MAX` cannot be expressed in the cell, so the conversion
/// reports an internal error instead of truncating.
#[test]
fn padparams__out_of_range_bounds_are_refused() {
    let p = Parameters::builder()
        .low(IntegerMilliseconds::new(70_000u32))
        .high(IntegerMilliseconds::new(90_000u32))
        .build()
        .expect("the builder itself does not range-check");
    let err = p
        .padding_negotiate_cell()
        .expect_err("70000 ms does not fit in the u16 cell field");
    assert!(
        err.to_string().contains("padding negotiate out of range"),
        "unexpected error for an out-of-range bound: {err}"
    );
}
