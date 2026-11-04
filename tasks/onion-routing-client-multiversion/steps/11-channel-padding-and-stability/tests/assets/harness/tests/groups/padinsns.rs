//! Group `padinsns`: ChannelPaddingInstructions (tor-proto).
//!
//! A `ChannelPaddingInstructions` carries three fields: `padding_enable`,
//! `padding_parameters`, and `padding_negotiate`. The type is `Default`,
//! `Eq`, and has `initial_update()` to diff against a default reactor state.

use tor_proto::channel::{ChannelPaddingInstructions, ChannelPaddingInstructionsUpdates};

#[test]
fn padinsns__default_constructs() {
    let _i = ChannelPaddingInstructions::default();
}

#[test]
fn padinsns__eq_reflexive() {
    let a = ChannelPaddingInstructions::default();
    let b = ChannelPaddingInstructions::default();
    assert_eq!(a, b);
}

#[test]
fn padinsns__initial_update_returns_none_for_default() {
    let i = ChannelPaddingInstructions::default();
    let u = i.initial_update();
    assert!(u.is_none(), "default instructions should diff to None against a default reactor");
}

#[test]
fn padinsns__initial_update_returns_some_when_padding_differs() {
    use tor_proto::channel::padding::Parameters;
    let mut i = ChannelPaddingInstructions::default();
    let u1 = i.start_update()
        .padding_parameters(Parameters::default_padding())
        .finish();
    assert!(u1.is_some(), "changing padding_parameters should produce an update");
    let u2 = i.initial_update();
    assert!(u2.is_some(), "non-default instructions should produce an initial_update");
}

#[test]
fn padinsns__start_update_records_changes() {
    use tor_proto::channel::padding::Parameters;
    let mut i = ChannelPaddingInstructions::default();
    let u = i.start_update()
        .padding_parameters(Parameters::default_padding())
        .finish();
    assert!(u.is_some());
}

#[test]
fn padinsns__start_update_returns_none_when_nothing_changed() {
    let mut i = ChannelPaddingInstructions::default();
    let u = i.start_update().finish();
    assert!(u.is_none(), "an update that changes nothing should return None");
}

#[test]
fn padinsns__updates_are_eq() {
    let a = ChannelPaddingInstructionsUpdates::default();
    let b = ChannelPaddingInstructionsUpdates::default();
    assert_eq!(a, b);
}
