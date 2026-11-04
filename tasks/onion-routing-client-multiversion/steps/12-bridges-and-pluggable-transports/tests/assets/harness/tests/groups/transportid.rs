//! Group: transportid — the transport namespace.
//!
//! A `TransportId` names either the built-in (direct TLS) transport or exactly
//! one pluggable transport.  pt-spec.txt gives a method name the syntax of a
//! C99 identifier and reserves the word `bridge`; the four built-in spellings
//! are arti's configuration convention for "no transport at all".

use std::str::FromStr;
use tor_linkspec::{PtTransportName, TransportId, TransportIdError};

#[test]
fn transportid__default_is_the_builtin_transport() {
    let id = TransportId::default();
    assert!(id.is_builtin(), "the default transport is the built-in one");
    assert!(id.as_pluggable().is_none());
    assert_eq!(id, TransportId::new_builtin());
}

#[test]
fn transportid__four_spellings_all_mean_builtin() {
    for s in ["-", "", "bridge", "<none>"] {
        let id = TransportId::from_str(s).unwrap_or_else(|e| panic!("{:?} should parse: {}", s, e));
        assert!(id.is_builtin(), "{:?} must denote the built-in transport", s);
    }
}

#[test]
fn transportid__builtin_displays_as_a_single_hyphen() {
    assert_eq!(TransportId::from_str("<none>").unwrap().to_string(), "-");
    assert_eq!(TransportId::from_str("bridge").unwrap().to_string(), "-");
    assert_eq!(TransportId::from_str("").unwrap().to_string(), "-");
}

#[test]
fn transportid__pluggable_name_round_trips_through_display() {
    let id = TransportId::from_str("obfs4").expect("obfs4 is a well-formed method name");
    assert_eq!(id.as_pluggable().map(|p| p.to_string()), Some("obfs4".to_string()));
    assert_eq!(id.to_string(), "obfs4");
    assert!(!id.is_builtin());
}

#[test]
fn transportid__c_identifier_syntax_is_accepted() {
    for s in ["obfs4", "meek_lite", "_private", "snowflake", "webtunnel", "a0", "Z_9"] {
        let name = PtTransportName::from_str(s)
            .unwrap_or_else(|e| panic!("{:?} should be a valid method name: {}", s, e));
        assert_eq!(name.to_string(), s, "the name must be preserved verbatim");
    }
}

#[test]
fn transportid__names_outside_c_identifier_syntax_are_rejected() {
    for s in ["4obfs", "obfs-4", "obfs 4", "obfs.4", "obfs/4", "", "obfs4!"] {
        match PtTransportName::from_str(s) {
            Err(TransportIdError::BadId(ref got)) => assert_eq!(got, s),
            other => panic!("{:?} must be rejected as a bad id, got {:?}", s, other),
        }
    }
}

#[test]
fn transportid__bridge_is_reserved_in_any_case() {
    for s in ["bridge", "Bridge", "BRIDGE"] {
        match PtTransportName::from_str(s) {
            Err(TransportIdError::BadId(_)) => {}
            other => panic!("`{}` is reserved and must not name a transport: {:?}", s, other),
        }
    }
}

#[test]
fn transportid__new_pluggable_matches_the_parsed_form() {
    let name = PtTransportName::from_str("snowflake").unwrap();
    let a = TransportId::new_pluggable(name.clone());
    let b = TransportId::from_str("snowflake").unwrap();
    assert_eq!(a, b);
    assert_eq!(a.into_pluggable(), Some(name));
}
