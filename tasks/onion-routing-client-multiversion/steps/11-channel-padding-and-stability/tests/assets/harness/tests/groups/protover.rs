//! Group `protover`: subprotocol version sets (tor-protover).
//!
//! INHERITED. Parsing and formatting of the `proto` line has not changed at
//! this milestone, and must not: the netdir code that this milestone extends
//! with by-identity lookups and congestion-control parameters still selects
//! relays by asking a `Protocols` set what it supports, so this vocabulary is
//! load-bearing for the new capability even though it is old.
//!
//! Grammar (dir-spec.txt section 2.1.1): a space-separated list of
//! `Keyword=Versions`, where `Versions` is a comma-separated list of version
//! numbers or inclusive `lo-hi` ranges, and a version number is 0..=63.
#![allow(non_snake_case)]

use tor_protover::{ParseError, ProtoKind, Protocols};

/// Ranges and comma lists both name individual versions.
#[test]
fn protover__parses_ranges_and_lists() {
    let p: Protocols = "Link=1-3 HSDir=2,4-5".parse().expect("a well-formed list");

    for ver in [1, 2, 3] {
        assert!(p.supports_known_subver(ProtoKind::Link, ver), "Link={ver}");
    }
    assert!(!p.supports_known_subver(ProtoKind::Link, 0));
    assert!(!p.supports_known_subver(ProtoKind::Link, 4));

    assert!(p.supports_known_subver(ProtoKind::HSDir, 2));
    assert!(!p.supports_known_subver(ProtoKind::HSDir, 3), "the list is 2,4-5");
    assert!(p.supports_known_subver(ProtoKind::HSDir, 4));
    assert!(p.supports_known_subver(ProtoKind::HSDir, 5));

    // A protocol that did not appear at all supports nothing.
    assert!(!p.supports_known_subver(ProtoKind::LinkAuth, 1));
}

/// A subprotocol we do not know is kept verbatim rather than discarded, so a
/// relay advertising a future protocol is still matched correctly by name.
#[test]
fn protover__unrecognized_protocols_are_kept_by_name() {
    let p: Protocols = "Link=1-3 Foobar=7".parse().expect("a well-formed list");
    assert!(p.supports_subver("Foobar", 7));
    assert!(!p.supports_subver("Foobar", 6));
    assert!(!p.supports_subver("Wombat", 7), "a protocol never mentioned");
    assert!(p.supports_subver("Link", 2), "named lookup finds known kinds too");
    assert!(!p.supports_subver("Link", 5));
    assert!(
        ProtoKind::from_name("Foobar").is_none(),
        "Foobar must not be a recognized kind, or this case proves nothing"
    );
}

/// Formatting is canonical and reparses to the same set.
#[test]
fn protover__display_round_trips() {
    let p: Protocols = "Link=1-3 HSDir=2,4-5".parse().expect("a well-formed list");
    let text = p.to_string();
    assert_eq!(
        text, "HSDir=2,4-5 Link=1-3",
        "runs are collapsed to ranges and entries are sorted by name"
    );
    let again: Protocols = text.parse().expect("reparsing our own output");
    assert_eq!(again, p);

    // Input that is not already canonical is normalised.
    let messy: Protocols = "Link=1,2,3".parse().expect("a well-formed list");
    assert_eq!(messy.to_string(), "Link=1-3");
    assert_eq!(messy, "Link=1-3".parse::<Protocols>().unwrap());
}

/// The three documented failure modes are distinguished, not collapsed.
#[test]
fn protover__malformed_input_is_classified() {
    assert_eq!(
        "Link=1-3 Link=4".parse::<Protocols>().unwrap_err(),
        ParseError::Duplicate,
        "a subprotocol may appear only once"
    );
    assert_eq!(
        "Link=64".parse::<Protocols>().unwrap_err(),
        ParseError::OutOfRange,
        "versions run 0..=63"
    );
    assert_eq!(
        "Link".parse::<Protocols>().unwrap_err(),
        ParseError::Malformed,
        "an entry needs a version list"
    );
    assert_eq!(
        "Link=3-1".parse::<Protocols>().unwrap_err(),
        ParseError::Malformed,
        "a range must not run backwards"
    );
}

/// The empty set is both parseable and printable, and supports nothing.
#[test]
fn protover__empty_set_supports_nothing() {
    let empty = Protocols::new();
    assert_eq!(empty, Protocols::default());
    assert_eq!(empty.to_string(), "");
    assert_eq!("".parse::<Protocols>().expect("empty is well-formed"), empty);
    assert!(!empty.supports_known_subver(ProtoKind::Link, 0));
    assert!(!empty.supports_subver("Link", 0));
    assert!(!empty.supports_subver("Foobar", 0));
}

/// The integer encodings of the recognized kinds are fixed by the consensus
/// format, so they can be used as array indices and sent on the wire.
#[test]
fn protover__kind_numbering_is_fixed() {
    assert_eq!(u16::from(ProtoKind::Link), 0);
    assert_eq!(u16::from(ProtoKind::Relay), 2);
    assert_eq!(u16::from(ProtoKind::DirCache), 3);
    assert_eq!(u16::from(ProtoKind::Desc), 7);
    assert_eq!(u16::from(ProtoKind::Padding), 10);
    assert_eq!(u16::from(ProtoKind::FlowCtrl), 11);

    assert_eq!(ProtoKind::from_name("HSDir"), Some(ProtoKind::HSDir));
    assert_eq!(ProtoKind::from_name("hsdir"), None, "names are case-sensitive");
    assert!(ProtoKind::Link.is_recognized());
    assert!(!ProtoKind::from(999u16).is_recognized());
    assert_eq!(ProtoKind::Padding.to_string(), "Padding");
}
