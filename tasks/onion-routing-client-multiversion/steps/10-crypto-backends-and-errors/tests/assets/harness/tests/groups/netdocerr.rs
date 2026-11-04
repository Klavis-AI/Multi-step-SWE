//! Document-parsing errors expose the parse failure that produced them
//! through `Error::parse_error_kind()`.

#![allow(non_snake_case)]

use std::str::FromStr;

use tor_netdoc::types::policy::{AddrPortPattern, PortPolicy};
use tor_netdoc::types::version::TorVersion;
use tor_netdoc::{Error, ParseErrorKind, Pos};

fn version_err(s: &str) -> Error {
    TorVersion::from_str(s).err().unwrap_or_else(|| panic!("{:?} should not parse", s))
}

fn pattern_err(s: &str) -> Error {
    AddrPortPattern::from_str(s)
        .err()
        .unwrap_or_else(|| panic!("{:?} should not parse", s))
        .into()
}

#[test]
fn netdocerr__an_unparsable_version_reports_bad_tor_version() {
    let e = version_err("not-a-version");
    assert_eq!(e.parse_error_kind(), ParseErrorKind::BadTorVersion);
    assert!(
        TorVersion::from_str("0.4.7.8").is_ok(),
        "a well-formed version must still parse"
    );
}

#[test]
fn netdocerr__an_unparsable_address_pattern_reports_bad_policy() {
    let e = pattern_err("this is not a pattern");
    assert_eq!(e.parse_error_kind(), ParseErrorKind::BadPolicy);
}

#[test]
fn netdocerr__an_unparsable_port_policy_reports_bad_policy() {
    let e: Error = PortPolicy::from_str("accept 1-2-3")
        .err()
        .expect("a malformed port policy")
        .into();
    assert_eq!(e.parse_error_kind(), ParseErrorKind::BadPolicy);
}

#[test]
fn netdocerr__kinds_describe_themselves() {
    assert_eq!(
        crate::common::shown(&ParseErrorKind::BadPolicy),
        "invalid policy entry"
    );
    assert_eq!(
        crate::common::shown(&ParseErrorKind::BadTorVersion),
        "couldn't parse Tor version"
    );
    assert_eq!(
        crate::common::shown(&ParseErrorKind::BadSignature),
        "couldn't validate signature"
    );
    assert_eq!(
        crate::common::shown(&ParseErrorKind::MissingToken),
        "didn't find required entry"
    );
}

#[test]
fn netdocerr__distinct_kinds_are_not_equal() {
    assert_ne!(ParseErrorKind::BadPolicy, ParseErrorKind::BadTorVersion);
    assert_eq!(ParseErrorKind::BadPolicy, ParseErrorKind::BadPolicy);
    assert_ne!(
        version_err("nope").parse_error_kind(),
        pattern_err("nope").parse_error_kind(),
        "the two failures must be distinguishable"
    );
}

#[test]
fn netdocerr__annotating_an_error_preserves_its_kind() {
    let e = pattern_err("nope");
    let kind = e.parse_error_kind();
    let annotated = e.within("a document we were reading").at_pos(Pos::None);
    assert_eq!(
        annotated.parse_error_kind(),
        kind,
        "adding context must not change the classification"
    );
    assert!(
        crate::common::shown(&annotated).starts_with("invalid policy entry"),
        "the displayed error leads with its kind; got {:?}",
        crate::common::shown(&annotated)
    );
}
