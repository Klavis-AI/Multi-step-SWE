//! Group: bridgeerr — what a malformed bridge line is told.
//!
//! A bridge line is user input typed by hand and pasted from a web page, so the
//! error variants are part of the interface: each one names the specific thing
//! that went wrong so the user can fix it.  These cases pin the variant, not
//! the wording.

use crate::common::{ED_B64, RSA_FINGERPRINT};
use std::str::FromStr;
use tor_guardmgr::bridge::{BridgeConfig, BridgeParseError};

/// Parse a bridge line and require a specific error variant.
fn expect_err(line: &str, what: &str) -> BridgeParseError {
    match BridgeConfig::from_str(line) {
        Ok(cfg) => panic!("{} should be rejected, but parsed as {}", what, cfg),
        Err(e) => e,
    }
}

#[test]
fn bridgeerr__empty_line() {
    assert!(matches!(expect_err("", "an empty line"), BridgeParseError::Empty));
    assert!(matches!(expect_err("   \t ", "a blank line"), BridgeParseError::Empty));
}

#[test]
fn bridgeerr__line_with_the_keyword_but_nothing_else() {
    assert!(matches!(expect_err("bridge", "a bare keyword"), BridgeParseError::Empty));
}

#[test]
fn bridgeerr__no_rsa_identity() {
    let e = expect_err("bridge 198.51.100.5:9001", "a bridge with no identity key");
    assert!(matches!(e, BridgeParseError::NoRsaIdentity), "got {:?}", e);
}

#[test]
fn bridgeerr__malformed_identity_key() {
    let line = format!("bridge 198.51.100.5:9001 {}", &RSA_FINGERPRINT[..20]);
    match expect_err(&line, "a truncated fingerprint") {
        BridgeParseError::InvalidIdentityOrParameter { word, .. } => {
            assert_eq!(word, &RSA_FINGERPRINT[..20])
        }
        other => panic!("a truncated fingerprint must be named as such: {:?}", other),
    }
}

#[test]
fn bridgeerr__the_same_key_type_twice() {
    let line = format!(
        "bridge 198.51.100.5:9001 {} {}",
        RSA_FINGERPRINT, RSA_FINGERPRINT
    );
    let e = expect_err(&line, "the same RSA key twice");
    assert!(matches!(e, BridgeParseError::MultipleIdentitiesOfSameType { .. }), "got {:?}", e);

    let ed_line = format!(
        "bridge 198.51.100.5:9001 {} ed25519:{} ed25519:{}",
        RSA_FINGERPRINT, ED_B64, ED_B64
    );
    let e = expect_err(&ed_line, "the same ed25519 key twice");
    assert!(matches!(e, BridgeParseError::MultipleIdentitiesOfSameType { .. }), "got {:?}", e);
}

#[test]
fn bridgeerr__unparseable_address() {
    let line = format!("bridge 999.999.999.999:9001 {}", RSA_FINGERPRINT);
    let e = expect_err(&line, "an impossible IPv4 address");
    assert!(matches!(e, BridgeParseError::InvalidIpAddrOrPt { .. }), "got {:?}", e);
}

#[test]
fn bridgeerr__unparseable_transport_name() {
    // A word with no colon is read as a transport name, and this one is not one.
    let line = format!("bridge obfs-4 198.51.100.42:9001 {}", RSA_FINGERPRINT);
    let e = expect_err(&line, "a transport name outside the identifier syntax");
    assert!(matches!(e, BridgeParseError::InvalidPtOrAddr { .. }), "got {:?}", e);
}

#[test]
fn bridgeerr__parameter_without_an_equals_sign() {
    // Parameter position starts at the first word holding `=`; a bare word
    // after that point is neither an identity nor a `key=value`.
    let line = format!(
        "bridge obfs4 198.51.100.42:9001 {} cert=abc iat-mode",
        RSA_FINGERPRINT
    );
    match expect_err(&line, "a PT parameter with no value") {
        BridgeParseError::InvalidPtKeyValue { word } => assert_eq!(word, "iat-mode"),
        other => panic!("a bare word in parameter position must be named: {:?}", other),
    }
}

#[test]
fn bridgeerr__the_word_after_a_transport_is_always_its_target() {
    // Once a transport name has been read, the next word is consumed as the
    // transport's target, so a fingerprint there is reported as a bad address
    // and blamed on the transport word.
    let line = format!("bridge obfs4 notanaddress {}", RSA_FINGERPRINT);
    match expect_err(&line, "a transport whose target is not an address") {
        BridgeParseError::InvalidIPtHostAddr { word, .. } => assert_eq!(word, "obfs4"),
        other => panic!("the bad target must be blamed on the transport: {:?}", other),
    }
}

#[test]
fn bridgeerr__parameters_are_refused_on_a_direct_connection() {
    let line = format!("bridge 198.51.100.5:9001 {} cert=abc", RSA_FINGERPRINT);
    let e = expect_err(&line, "a setting with no transport to carry it");
    assert!(matches!(e, BridgeParseError::DirectParametersNotAllowed), "got {:?}", e);
}
