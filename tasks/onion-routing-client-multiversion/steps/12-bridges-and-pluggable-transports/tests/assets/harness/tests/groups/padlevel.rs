//! Group: padlevel — how much channel padding was asked for, re-checked here.
//!
//! The padding level is a configuration knob, so what matters is the spelling
//! accepted in a configuration file and the value it lands on.  Step 12 leaves
//! this alone while the channel manager becomes bridge-aware, so it has to
//! keep behaving exactly as it did.

use std::str::FromStr;

use tor_chanmgr::{ChannelConfig, ChannelConfigBuilder};
use tor_config::PaddingLevel;

#[test]
fn padlevel__normal_padding_is_what_you_get_by_saying_nothing() {
    assert_eq!(PaddingLevel::default(), PaddingLevel::Normal);
}

#[test]
fn padlevel__each_level_has_one_snake_case_name() {
    assert_eq!(PaddingLevel::None.to_string(), "none");
    assert_eq!(PaddingLevel::Reduced.to_string(), "reduced");
    assert_eq!(PaddingLevel::Normal.to_string(), "normal");

    assert_eq!(PaddingLevel::from_str("none").unwrap(), PaddingLevel::None);
    assert_eq!(
        PaddingLevel::from_str("reduced").unwrap(),
        PaddingLevel::Reduced
    );
    assert_eq!(
        PaddingLevel::from_str("normal").unwrap(),
        PaddingLevel::Normal
    );
}

#[test]
fn padlevel__a_boolean_is_accepted_as_a_shorthand_for_off_and_on() {
    let off: PaddingLevel = serde_json::from_str("false").unwrap();
    assert_eq!(off, PaddingLevel::None);
    let on: PaddingLevel = serde_json::from_str("true").unwrap();
    assert_eq!(on, PaddingLevel::Normal);

    // There is no boolean spelling of the reduced level.
    let reduced: PaddingLevel = serde_json::from_str("\"reduced\"").unwrap();
    assert_eq!(reduced, PaddingLevel::Reduced);
}

#[test]
fn padlevel__a_level_is_always_written_back_out_as_its_name() {
    assert_eq!(serde_json::to_string(&PaddingLevel::None).unwrap(), "\"none\"");
    assert_eq!(
        serde_json::to_string(&PaddingLevel::Reduced).unwrap(),
        "\"reduced\""
    );
    assert_eq!(
        serde_json::to_string(&PaddingLevel::Normal).unwrap(),
        "\"normal\"",
        "a level read from a boolean is still written back as a name"
    );
}

#[test]
fn padlevel__an_unknown_spelling_is_refused() {
    assert!(PaddingLevel::from_str("lots").is_err());
    assert!(serde_json::from_str::<PaddingLevel>("\"lots\"").is_err());
    assert!(serde_json::from_str::<PaddingLevel>("2").is_err());
}

#[test]
fn padlevel__the_channel_configuration_carries_the_level_it_was_given() {
    let default = ChannelConfig::default();
    assert_eq!(
        default,
        ChannelConfig::builder().build().expect("all fields default")
    );

    let reduced: ChannelConfigBuilder =
        toml::from_str("padding = \"reduced\"").expect("a level is a valid channel setting");
    let reduced = reduced.build().expect("builds");
    assert_ne!(reduced, default);

    let explicit_normal: ChannelConfigBuilder = toml::from_str("padding = \"normal\"").unwrap();
    assert_eq!(explicit_normal.build().unwrap(), default);

    let switched_off: ChannelConfigBuilder = toml::from_str("padding = false").unwrap();
    assert_ne!(switched_off.build().unwrap(), default);
    assert_ne!(switched_off.build().unwrap(), reduced);
}
