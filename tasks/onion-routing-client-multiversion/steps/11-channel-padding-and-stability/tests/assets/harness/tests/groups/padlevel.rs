//! Group `padlevel`: the `PaddingLevel` configuration enum.
//!
//! Spec: padding-spec.txt section 2.2 (the `padding` level knob) and the
//! `tor-config` configuration vocabulary. The type lives in
//! `tor_config::PaddingLevel` and is re-exported from `tor-config`'s root.
//!
//! At the previous milestone there was no such enum anywhere in the tree: the
//! channel manager had a bare `padding: bool` and `arti` spelled the setting as
//! a boolean. Everything below is therefore a discriminator.

use std::str::FromStr;

use tor_config::PaddingLevel;

/// `Normal` is the default: padding on, at the consensus rate.
#[test]
fn padlevel__default_is_normal() {
    assert_eq!(PaddingLevel::default(), PaddingLevel::Normal);
}

/// The three levels are distinct, and `Ord` follows declaration order.
#[test]
fn padlevel__levels_are_distinct_and_ordered() {
    assert_ne!(PaddingLevel::None, PaddingLevel::Reduced);
    assert_ne!(PaddingLevel::Reduced, PaddingLevel::Normal);
    assert_ne!(PaddingLevel::None, PaddingLevel::Normal);
    assert!(PaddingLevel::None < PaddingLevel::Reduced);
    assert!(PaddingLevel::Reduced < PaddingLevel::Normal);
}

/// `Display` renders the snake_case spelling used in configuration files.
#[test]
fn padlevel__display_is_snake_case() {
    assert_eq!(PaddingLevel::None.to_string(), "none");
    assert_eq!(PaddingLevel::Reduced.to_string(), "reduced");
    assert_eq!(PaddingLevel::Normal.to_string(), "normal");
}

/// The same snake_case spelling parses back through `FromStr`.
#[test]
fn padlevel__from_str_round_trips() {
    for level in [PaddingLevel::None, PaddingLevel::Reduced, PaddingLevel::Normal] {
        let text = level.to_string();
        assert_eq!(
            PaddingLevel::from_str(&text).expect("its own Display output must parse"),
            level
        );
    }
    assert_eq!(PaddingLevel::from_str("none").unwrap(), PaddingLevel::None);
}

/// An unrecognised spelling is refused rather than silently defaulted.
#[test]
fn padlevel__from_str_rejects_unknown() {
    assert!(PaddingLevel::from_str("louder").is_err());
    assert!(PaddingLevel::from_str("").is_err());
    assert!(PaddingLevel::from_str("Normal").is_err());
}

/// The `&'static str` conversion agrees with `Display`.
#[test]
fn padlevel__static_str_conversion() {
    assert_eq!(<&str>::from(&PaddingLevel::None), "none");
    assert_eq!(<&str>::from(&PaddingLevel::Reduced), "reduced");
    assert_eq!(<&str>::from(&PaddingLevel::Normal), "normal");
}

/// The level serialises to a string and deserialises back from it.
#[test]
fn padlevel__serde_round_trips_as_string() {
    let json = serde_json::to_string(&PaddingLevel::Reduced).expect("serialising a level");
    assert_eq!(json, "\"reduced\"");
    let back: PaddingLevel =
        serde_json::from_str(&json).expect("a level we just serialised must deserialise");
    assert_eq!(back, PaddingLevel::Reduced);
}

/// For backwards compatibility the boolean spellings are still accepted, and
/// map to `Normal`/`None`; anything else is an error.
#[test]
fn padlevel__serde_accepts_legacy_bool() {
    let on: PaddingLevel = serde_json::from_str("true").expect("`true` is the legacy spelling");
    assert_eq!(on, PaddingLevel::Normal);
    let off: PaddingLevel = serde_json::from_str("false").expect("`false` is the legacy spelling");
    assert_eq!(off, PaddingLevel::None);
    assert!(serde_json::from_str::<PaddingLevel>("\"sideways\"").is_err());
}
