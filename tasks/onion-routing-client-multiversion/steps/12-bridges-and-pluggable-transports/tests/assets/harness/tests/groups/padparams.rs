//! Group: padparams — channel padding timings, re-checked here.
//!
//! Step 11 added channel padding; step 12 has to keep it working while the
//! channel manager learns about bridges.  The negotiation cell is a fixed
//! six-byte body, and the timing bounds in it are the ones the padding
//! specification names as the client default.

use tor_bytes::Reader;
use tor_cell::chancell::msg::{Body, PaddingNegotiate};
use tor_proto::channel::padding::Parameters;

/// Encode a negotiation message's body.
fn body(msg: PaddingNegotiate) -> Vec<u8> {
    let mut v = Vec::new();
    msg.write_body_onto(&mut v).expect("encoding cannot fail");
    v
}

#[test]
fn padparams__the_default_timings_are_the_specified_client_bounds() {
    let built = Parameters::builder()
        .low(1500_u32.into())
        .high(9500_u32.into())
        .build()
        .expect("both bounds given");
    assert_eq!(
        Parameters::default_padding(),
        built,
        "the default is 1500ms to 9500ms"
    );
    assert_ne!(Parameters::default_padding(), Parameters::disabled());
}

#[test]
fn padparams__parameters_turn_into_a_start_message_carrying_those_bounds() {
    let msg = Parameters::default_padding()
        .padding_negotiate_cell()
        .expect("the default bounds fit in the message");
    assert_eq!(
        msg.clone(),
        PaddingNegotiate::start(1500_u16.into(), 9500_u16.into())
    );
    assert_ne!(msg.clone(), PaddingNegotiate::start_default());

    // The body is a version byte, a command byte, and two 16-bit big-endian
    // millisecond bounds.
    assert_eq!(body(msg), vec![0x00, 0x02, 0x05, 0xDC, 0x25, 0x1C]);
}

#[test]
fn padparams__asking_for_no_padding_sends_zero_bounds_rather_than_omitting_them() {
    let msg = Parameters::disabled()
        .padding_negotiate_cell()
        .expect("zero is always in range");
    assert_eq!(msg, PaddingNegotiate::start(0_u16.into(), 0_u16.into()));
    // Zero bounds mean "use the consensus defaults", which is also the message
    // the channel is in when it opens.
    assert_eq!(msg, PaddingNegotiate::start_default());
    assert_eq!(msg, PaddingNegotiate::default());
    assert_eq!(body(msg), vec![0x00, 0x02, 0x00, 0x00, 0x00, 0x00]);
}

#[test]
fn padparams__stopping_padding_is_a_different_command_in_the_same_body() {
    let stop = PaddingNegotiate::stop();
    assert_ne!(stop, PaddingNegotiate::start_default());
    assert_eq!(
        body(stop),
        vec![0x00, 0x01, 0x00, 0x00, 0x00, 0x00],
        "STOP is command 1 and carries no timings"
    );
}

#[test]
fn padparams__a_body_is_read_back_into_the_same_message() {
    let bytes = [0x00_u8, 0x02, 0x05, 0xDC, 0x25, 0x1C];
    let mut r = Reader::from_slice(&bytes);
    let msg: PaddingNegotiate = r.extract().expect("well-formed body");
    assert_eq!(msg, PaddingNegotiate::start(1500_u16.into(), 9500_u16.into()));
    assert_eq!(r.remaining(), 0);

    let stop = [0x00_u8, 0x01, 0x00, 0x00, 0x00, 0x00];
    let msg: PaddingNegotiate = Reader::from_slice(&stop).extract().unwrap();
    assert_eq!(msg, PaddingNegotiate::stop());

    // A body whose leading version byte is not zero is rejected outright.
    let bad = [0x01_u8, 0x02, 0x00, 0x00, 0x00, 0x00];
    let e: Result<PaddingNegotiate, _> = Reader::from_slice(&bad).extract();
    assert!(e.is_err(), "an unknown negotiation version must be refused");
}

#[test]
fn padparams__a_timing_that_will_not_fit_the_field_is_refused() {
    let too_big = Parameters::builder()
        .low(1500_u32.into())
        .high(70_000_u32.into())
        .build()
        .expect("the builder itself does not range-check");
    assert!(
        too_big.padding_negotiate_cell().is_err(),
        "a bound above 65535ms cannot be expressed in the message"
    );
}
