//! Group: bridgeline — the bridge-line grammar.
//!
//! A bridge line is the one configuration format bridge users actually handle,
//! so both directions matter: what the parser accepts, and what `Display`
//! writes back.  Every string here is an example of the documented syntax, and
//! every case asserts on a public accessor rather than on the raw struct.

use crate::common::{RSA_FINGERPRINT, ED_B64};
use std::str::FromStr;
use tor_guardmgr::bridge::BridgeConfig;
use tor_linkspec::HasAddrs;
use tor_linkspec::{HasChanMethod, HasRelayIds};

#[test]
fn bridgeline__bridge_keyword_is_optional() {
    let with = format!("bridge 198.51.100.5:9001 {}", RSA_FINGERPRINT);
    let without = format!("198.51.100.5:9001 {}", RSA_FINGERPRINT);
    let a = BridgeConfig::from_str(&with).unwrap();
    let b = BridgeConfig::from_str(&without).unwrap();
    assert_eq!(a, b, "the keyword carries no information");
}

#[test]
fn bridgeline__keyword_is_case_insensitive() {
    let lower = format!("bridge 198.51.100.5:9001 {}", RSA_FINGERPRINT);
    let mixed = format!("BrIdGe 198.51.100.5:9001 {}", RSA_FINGERPRINT);
    assert_eq!(
        BridgeConfig::from_str(&lower).unwrap(),
        BridgeConfig::from_str(&mixed).unwrap()
    );
}

#[test]
fn bridgeline__direct_numeric_address_round_trips() {
    let line = format!("bridge 198.51.100.5:9001 {}", RSA_FINGERPRINT);
    let cfg = BridgeConfig::from_str(&line).unwrap();
    let printed = cfg.to_string();
    assert!(
        printed.contains("198.51.100.5:9001"),
        "the address must survive printing: {}",
        printed
    );
    assert_eq!(
        BridgeConfig::from_str(&printed).unwrap(),
        cfg,
        "printing then reparsing must be a no-op"
    );
}

#[test]
fn bridgeline__direct_ipv6_address_is_bracketed() {
    let line = format!("bridge [2001:db8::1]:443 {}", RSA_FINGERPRINT);
    let cfg = BridgeConfig::from_str(&line).unwrap();
    assert!(cfg.to_string().contains("[2001:db8::1]:443"));
}

#[test]
fn bridgeline__a_direct_line_names_exactly_one_address() {
    // The grammar gives a direct bridge line one `Host:ORPort` word; everything
    // after it is an identity or a parameter, never a second address.
    let line = format!("bridge 198.51.100.5:9001 {}", RSA_FINGERPRINT);
    let cfg = BridgeConfig::from_str(&line).unwrap();
    assert_eq!(
        cfg.addrs(),
        &["198.51.100.5:9001".parse::<std::net::SocketAddr>().unwrap()]
    );

    // A second address-shaped word therefore lands in identity position.
    let two = format!("bridge 198.51.100.5:9001 198.51.100.6:443 {}", RSA_FINGERPRINT);
    let e = BridgeConfig::from_str(&two).expect_err("a second address is not an address");
    match e {
        tor_guardmgr::bridge::BridgeParseError::InvalidIdentityOrParameter { word, .. } => {
            assert_eq!(word, "198.51.100.6:443");
        }
        other => panic!("the second word must be read as an identity: {:?}", other),
    }
}

#[test]
fn bridgeline__pluggable_transport_names_a_numeric_target() {
    let line = format!(
        "bridge obfs4 198.51.100.42:9001 {} cert=abc iat-mode=0",
        RSA_FINGERPRINT
    );
    let cfg = BridgeConfig::from_str(&line).unwrap();
    let printed = cfg.to_string();
    assert!(printed.contains("obfs4"), "{}", printed);
    assert!(printed.contains("cert=abc"), "{}", printed);
    assert!(printed.contains("iat-mode=0"), "{}", printed);
    assert_eq!(BridgeConfig::from_str(&printed).unwrap(), cfg);
}

#[test]
fn bridgeline__pluggable_transport_may_name_a_hostname() {
    let line = format!(
        "bridge obfs4 bridges.example.org:443 {} cert=xyz",
        RSA_FINGERPRINT
    );
    let cfg = BridgeConfig::from_str(&line).unwrap();
    assert_eq!(
        cfg.chan_method().target_addr(),
        Some(tor_linkspec::PtTargetAddr::HostPort(
            "bridges.example.org".to_string(),
            443
        ))
    );
}

#[test]
fn bridgeline__pluggable_transport_may_omit_the_target_entirely() {
    // The documented spelling for "the transport decides where to connect".
    let line = format!("bridge snowflake - {} ice=stun", RSA_FINGERPRINT);
    let cfg = BridgeConfig::from_str(&line).unwrap();
    assert_eq!(cfg.chan_method().target_addr(), Some(tor_linkspec::PtTargetAddr::None));
    assert_eq!(cfg.chan_method().transport_id().to_string(), "snowflake");
}

#[test]
fn bridgeline__ed25519_identity_is_carried_alongside_the_rsa_one() {
    let line = format!(
        "bridge 198.51.100.5:9001 {} ed25519:{}",
        RSA_FINGERPRINT, ED_B64
    );
    let cfg = BridgeConfig::from_str(&line).unwrap();
    assert_eq!(cfg.identities().count(), 2, "both keys should be reported");
    let printed = cfg.to_string();
    assert!(printed.contains("ed25519:"), "{}", printed);
    assert_eq!(BridgeConfig::from_str(&printed).unwrap(), cfg);
}

#[test]
fn bridgeline__surrounding_whitespace_is_ignored() {
    let line = format!("\t  bridge   198.51.100.5:9001   {}  \n", RSA_FINGERPRINT);
    let cfg = BridgeConfig::from_str(&line).unwrap();
    assert!(cfg.chan_method().is_direct());
}
