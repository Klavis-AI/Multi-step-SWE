//! Group `padupdates`: ChannelPaddingInstructionsUpdates and its builder.
//!
//! The update type is a set of *changes*: each field is `Some` only for a value
//! that actually differs from what the instructions already hold, so a setter
//! called with the value already in force adds nothing. `combine()` merges a
//! later update over an earlier one, field by field. The builder panics if
//! dropped, so every case here calls `finish()`.
//!
//! Neither this type nor its builder existed at v0.6.0: the reactor was
//! reconfigured wholesale rather than by diff.

use tor_proto::channel::padding::Parameters;
use tor_proto::channel::{ChannelPaddingInstructions, ChannelPaddingInstructionsUpdates};

/// An update that turns padding on, taken against freshly defaulted
/// instructions (whose `padding_enable` is false).
fn enable_update() -> ChannelPaddingInstructionsUpdates {
    let mut i = ChannelPaddingInstructions::default();
    i.start_update()
        .padding_enable(true)
        .finish()
        .expect("true differs from the default false, so this is a change")
}

/// An update that installs the spec-default timing parameters, taken against
/// freshly defaulted instructions (whose parameters are `disabled()`).
fn params_update() -> ChannelPaddingInstructionsUpdates {
    let mut i = ChannelPaddingInstructions::default();
    i.start_update()
        .padding_parameters(Parameters::default_padding())
        .finish()
        .expect("default_padding differs from the disabled default, so this is a change")
}

/// A setter called with the value already in force records nothing, so an
/// update carrying one real change is indistinguishable from the same change
/// plus a redundant setter.
#[test]
fn padupdates__setters_record_only_real_changes() {
    let mut i = ChannelPaddingInstructions::default();
    let both = i
        .start_update()
        .padding_enable(true)
        .padding_parameters(Parameters::disabled())
        .finish()
        .expect("at least padding_enable changed");
    assert_eq!(
        both,
        enable_update(),
        "setting padding_parameters to the value already in force must not appear in the update"
    );
}

/// An update with no changes at all is `None`, not an empty update: there is
/// nothing to send.
#[test]
fn padupdates__no_change_yields_none() {
    let mut i = ChannelPaddingInstructions::default();
    assert!(i.start_update().finish().is_none(), "no setter ran");

    let mut j = ChannelPaddingInstructions::default();
    assert!(
        j.start_update().padding_enable(false).finish().is_none(),
        "false is already the default, so this is not a change"
    );
    assert_eq!(j, ChannelPaddingInstructions::default());
}

/// A later update overrides an earlier one field by field.
#[test]
fn padupdates__combine_overrides_with_later_values() {
    let mut i = ChannelPaddingInstructions::default();
    let mut first = i
        .start_update()
        .padding_enable(true)
        .finish()
        .expect("padding_enable changed");
    // `i` now holds padding_enable == true, so turning it back off is a change.
    let second = i
        .start_update()
        .padding_enable(false)
        .finish()
        .expect("padding_enable changed back");
    assert_ne!(first, second);

    let before = first.clone();
    first.combine(&second);
    assert_eq!(first, second, "the later value wins");
    assert_ne!(first, before);
}

/// Combining disjoint updates keeps both fields.
#[test]
fn padupdates__combine_merges_disjoint_fields() {
    let mut merged = params_update();
    merged.combine(&enable_update());

    let mut i = ChannelPaddingInstructions::default();
    let want = i
        .start_update()
        .padding_parameters(Parameters::default_padding())
        .padding_enable(true)
        .finish()
        .expect("both fields changed");
    assert_eq!(merged, want, "combine must not drop the earlier field");
}

/// An empty update overrides nothing: every field of it is `None`.
#[test]
fn padupdates__combining_an_empty_update_changes_nothing() {
    let mut u = params_update();
    let before = u.clone();
    u.combine(&ChannelPaddingInstructionsUpdates::default());
    assert_eq!(u, before);
}
