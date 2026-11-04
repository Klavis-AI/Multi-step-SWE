//! Group: brlog — what a bridge looks like in a log line.
//!
//! Bridge addresses are the one thing a bridge user most needs kept out of
//! logs, so every type that can name a bridge has a redacted rendering as well
//! as a full one.  A redacted rendering keeps just enough to correlate two log
//! lines and drops the rest.

use std::net::SocketAddr;
use std::str::FromStr;

use safelog::Redactable;
use tor_linkspec::{
    BridgeAddr, ChannelMethod, OwnedChanTarget, PtTarget, PtTargetAddr, PtTransportName,
};

use crate::common::{ed_id, rsa_id, ED_B64, RSA_HEX};

/// Shorthand for a parsed socket address.
fn sa(s: &str) -> SocketAddr {
    s.parse().expect("address should parse")
}

#[test]
fn brlog__a_redacted_address_keeps_only_its_leading_field() {
    assert_eq!(sa("198.51.100.5:9001").redacted().to_string(), "198.x.x.x:9001");
    assert_eq!(
        sa("[2001:db8:1:2::5]:9001").redacted().to_string(),
        "[2001:x:x:…]:9001"
    );
    // The unredacted form is unchanged.
    assert_eq!(sa("198.51.100.5:9001").to_string(), "198.51.100.5:9001");
}

#[test]
fn brlog__a_redacted_bridge_address_keeps_two_characters_of_a_hostname() {
    let named = BridgeAddr::from_str("bridge.example:9001").unwrap();
    assert_eq!(named.to_string(), "bridge.example:9001");
    assert_eq!(named.redacted().to_string(), "br…:9001");

    let numeric = BridgeAddr::from_str("198.51.100.5:9001").unwrap();
    assert_eq!(numeric.redacted().to_string(), "198.x.x.x:9001");

    // An absent transport target has nothing to hide, so it is the same
    // placeholder either way.
    assert_eq!(PtTargetAddr::None.to_string(), "-");
    assert_eq!(PtTargetAddr::None.redacted().to_string(), "-");
    assert_eq!(
        PtTargetAddr::IpPort(sa("198.51.100.5:9001"))
            .redacted()
            .to_string(),
        "198.x.x.x:9001"
    );
}

#[test]
fn brlog__redacted_identities_keep_a_two_character_prefix() {
    assert_eq!(ed_id().redacted().to_string(), format!("{}…", &ED_B64[..2]));
    assert_eq!(
        rsa_id().redacted().to_string(),
        format!("${}…", &RSA_HEX[..2])
    );
    // `maybe_redacted(false)` is the ordinary rendering.
    assert_eq!(rsa_id().maybe_redacted(false).to_string(), rsa_id().to_string());
    assert_eq!(
        rsa_id().maybe_redacted(true).to_string(),
        rsa_id().redacted().to_string()
    );
}

#[test]
fn brlog__a_redacted_channel_target_stops_after_the_first_identity() {
    let t = OwnedChanTarget::builder()
        .addrs(vec![sa("198.51.100.5:9001")])
        .ed_identity(ed_id())
        .rsa_identity(rsa_id())
        .build()
        .unwrap();

    assert_eq!(
        t.to_string(),
        format!("[198.51.100.5:9001 ed25519:{} ${}]", ED_B64, RSA_HEX)
    );
    assert_eq!(
        t.redacted().to_string(),
        format!("[198.x.x.x:9001 {}…]", &ED_B64[..2]),
        "only the first identity survives redaction, and untagged at that"
    );
}

#[test]
fn brlog__a_redacted_transport_target_still_names_its_transport() {
    let pt = PtTarget::new(
        PtTransportName::from_str("obfs4").unwrap(),
        PtTargetAddr::HostPort("bridge.example".to_string(), 9001),
    );
    let t = OwnedChanTarget::builder()
        .method(ChannelMethod::Pluggable(pt))
        .rsa_identity(rsa_id())
        .build()
        .unwrap();

    assert_eq!(
        t.to_string(),
        format!("[bridge.example:9001 via obfs4 ${}]", RSA_HEX)
    );
    assert_eq!(
        t.redacted().to_string(),
        format!("[br…:9001 via obfs4 ${}…]", &RSA_HEX[..2]),
        "the transport name is not secret; the address is"
    );
}
