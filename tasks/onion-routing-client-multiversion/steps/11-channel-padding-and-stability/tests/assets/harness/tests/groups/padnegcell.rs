//! Group `padnegcell`: the PADDING_NEGOTIATE channel cell body.
//!
//! Spec: tor-spec.txt section 7.3 and padding-spec.txt section 2.5. The body is
//! `version u8 | command u8 | ito_low_ms u16 | ito_high_ms u16` with
//! `version == 0`; START is command 2 and STOP is command 1.
//!
//! Before this milestone the cell's command was a bare `bool` and there was no
//! `PaddingNegotiateCmd` type at all, so the type-level assertions below are
//! discriminators as well as the behavioural ones.

use tor_bytes::Reader;
use tor_cell::chancell::msg::{Body, ChanMsg, PaddingNegotiate, PaddingNegotiateCmd};
use tor_units::IntegerMilliseconds;

use crate::common::encode_body;

/// Decode a body fixture as a PADDING_NEGOTIATE cell.
fn decode(bytes: &[u8]) -> tor_bytes::Result<PaddingNegotiate> {
    let mut r = Reader::from_slice(bytes);
    let msg = ChanMsg::take(&mut r, tor_cell::chancell::ChanCmd::PADDING_NEGOTIATE)?;
    match msg {
        ChanMsg::PaddingNegotiate(p) => Ok(p),
        other => panic!("expected a PADDING_NEGOTIATE cell, got {:?}", other.cmd()),
    }
}

/// START is command value 2.
#[test]
fn padnegcell__start_command_value_is_two() {
    assert_eq!(u8::from(PaddingNegotiateCmd::START), 2);
}

/// STOP is command value 1.
#[test]
fn padnegcell__stop_command_value_is_one() {
    assert_eq!(u8::from(PaddingNegotiateCmd::STOP), 1);
}

/// The command byte round-trips through `From<u8>`.
#[test]
fn padnegcell__command_byte_round_trips() {
    assert_eq!(PaddingNegotiateCmd::from(2u8), PaddingNegotiateCmd::START);
    assert_eq!(PaddingNegotiateCmd::from(1u8), PaddingNegotiateCmd::STOP);
    assert_ne!(PaddingNegotiateCmd::START, PaddingNegotiateCmd::STOP);
}

/// `start_default()` requests the consensus timing parameters: START with both
/// bounds zeroed, which is the state the peer already assumes at startup.
#[test]
fn padnegcell__start_default_encodes_zero_bounds() {
    let got = encode_body(PaddingNegotiate::start_default().into_message());
    let want: &[u8] = include_bytes!("../../fixtures/padneg_start_default.bin");
    assert_eq!(got, want, "start_default body was {:02x?}", got);
}

/// `Default::default()` agrees with `start_default()`.
#[test]
fn padnegcell__default_matches_start_default() {
    let def = encode_body(PaddingNegotiate::default().into_message());
    let sd = encode_body(PaddingNegotiate::start_default().into_message());
    assert_eq!(def, sd);
}

/// An explicit START carries both bounds in network byte order.
#[test]
fn padnegcell__start_carries_explicit_bounds() {
    let p = PaddingNegotiate::start(
        IntegerMilliseconds::new(1500u16),
        IntegerMilliseconds::new(9500u16),
    );
    let got = encode_body(p.into_message());
    let want: &[u8] = include_bytes!("../../fixtures/padneg_start_1500_9500.bin");
    assert_eq!(got, want, "start(1500,9500) body was {:02x?}", got);
}

/// STOP encodes command 1 with both bounds zeroed.
#[test]
fn padnegcell__stop_encodes_command_one() {
    let got = encode_body(PaddingNegotiate::stop().into_message());
    let want: &[u8] = include_bytes!("../../fixtures/padneg_stop.bin");
    assert_eq!(got, want, "stop body was {:02x?}", got);
}

/// A well-formed START body decodes back to the message that encoded it.
#[test]
fn padnegcell__start_body_round_trips() {
    let fixture: &[u8] = include_bytes!("../../fixtures/padneg_start_1500_9500.bin");
    let p = decode(fixture).expect("a well-formed PADDING_NEGOTIATE body");
    assert_eq!(
        p,
        PaddingNegotiate::start(
            IntegerMilliseconds::new(1500u16),
            IntegerMilliseconds::new(9500u16)
        )
    );
}

/// A well-formed STOP body decodes back to `stop()`.
#[test]
fn padnegcell__stop_body_round_trips() {
    let fixture: &[u8] = include_bytes!("../../fixtures/padneg_stop.bin");
    let p = decode(fixture).expect("a well-formed PADDING_NEGOTIATE body");
    assert_eq!(p, PaddingNegotiate::stop());
}

/// Only version 0 is defined; any other version byte is refused.
#[test]
fn padnegcell__unrecognized_version_is_refused() {
    let mut body = [0x00u8, 0x01, 0x00, 0x00, 0x00, 0x00];
    body[0] = 0x01; // was: version 0
    let err = decode(&body).expect_err("version 1 must be refused");
    assert!(
        err.to_string().contains("Unrecognized padding negotiation version"),
        "unexpected error for a bad version byte: {err}"
    );
}
