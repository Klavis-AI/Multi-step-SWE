//! Group `chanconfig`: the channel manager's `ChannelConfig`.
//!
//! Spec: padding-spec.txt section 2.2 (the `padding` level is a channel-manager
//! wide setting) plus tor-spec.txt section 7.3.
//!
//! At the previous milestone the channel manager took no configuration at all:
//! `ChanMgr::new` was `new(runtime)` and there was no `ChannelConfig` type.
//! This group therefore also pins the builder and serde surface, which is what
//! later layers (and `arti-client`) consume.

use tor_chanmgr::{ChannelConfig, ChannelConfigBuilder};
use tor_config::PaddingLevel;

/// The default configuration asks for normal padding.
#[test]
fn chanconfig__default_is_normal_padding() {
    assert_eq!(ChannelConfig::default(), ChannelConfig::builder().build().unwrap());
}

/// An explicit padding level is preserved through the builder, and the result
/// is equal to an independently built copy of the same configuration.
#[test]
fn chanconfig__builder_carries_padding_level() {
    let reduced = ChannelConfig::builder()
        .padding(PaddingLevel::Reduced)
        .build()
        .expect("reduced padding is a valid configuration");
    let again = ChannelConfig::builder()
        .padding(PaddingLevel::Reduced)
        .build()
        .expect("reduced padding is a valid configuration");
    assert_eq!(reduced, again);
    assert_ne!(reduced, ChannelConfig::default());
}

/// The generated builder type round-trips through serde, so a configuration
/// file can spell a `ChannelConfig` out directly.
#[test]
fn chanconfig__builder_deserialises_from_json() {
    let built: ChannelConfigBuilder =
        serde_json::from_str(r#"{"padding":"reduced"}"#).expect("a ChannelConfigBuilder from JSON");
    let cfg = built.build().expect("a valid configuration");
    let want = ChannelConfig::builder()
        .padding(PaddingLevel::Reduced)
        .build()
        .unwrap();
    assert_eq!(cfg, want);
}

/// An omitted field falls back to the struct default rather than failing.
#[test]
fn chanconfig__builder_defaults_omitted_field() {
    let built: ChannelConfigBuilder = serde_json::from_str("{}").expect("an empty builder");
    assert_eq!(built.build().unwrap(), ChannelConfig::default());
}

/// `ChannelConfig` itself is not `Serialize`: a configuration file names the
/// *builder*, and the built configuration is immutable. So the round trip a
/// caller can perform is builder -> text -> builder, and it must preserve the
/// padding level under its documented spelling.
#[test]
fn chanconfig__builder_serde_round_trips() {
    let built: ChannelConfigBuilder =
        serde_json::from_str(r#"{"padding":"none"}"#).expect("a ChannelConfigBuilder from JSON");
    let json = serde_json::to_string(&built).expect("serialising a ChannelConfigBuilder");
    assert!(
        json.contains("\"none\""),
        "PaddingLevel::None must serialise as the string \"none\", got {json}"
    );
    let back: ChannelConfigBuilder = serde_json::from_str(&json).expect("and back again");
    assert_eq!(
        back.build().expect("a valid configuration"),
        ChannelConfig::builder()
            .padding(PaddingLevel::None)
            .build()
            .unwrap()
    );
}
