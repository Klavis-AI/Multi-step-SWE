//! Group: chantarget — a bridge as a channel target.
//!
//! `BridgeConfig` must be usable everywhere a relay is: the guard manager picks
//! it, the channel manager builds to it.  That means the `ChanTarget` supertraits
//! must report the bridge line's own facts back, not a re-derived guess.

use crate::common::{pt_bridge_line, RSA_FINGERPRINT};
use std::str::FromStr;
use tor_guardmgr::bridge::BridgeConfig;
use tor_linkspec::{ChannelMethod, ChanTarget, HasAddrs, HasChanMethod, HasRelayIds, RelayIdType};

#[test]
fn chantarget__direct_bridge_reports_a_direct_method() {
    let line = pt_bridge_line_builtin();
    let cfg = BridgeConfig::from_str(&line).unwrap();
    assert!(cfg.chan_method().is_direct());
    assert_eq!(cfg.chan_method().transport_id(), tor_linkspec::TransportId::new_builtin());
}

#[test]
fn chantarget__pluggable_bridge_reports_its_transport() {
    let line = pt_bridge_line("obfs4", "198.51.100.42:9001", &[("cert", "abc")]);
    let cfg = BridgeConfig::from_str(&line).unwrap();
    match cfg.chan_method() {
        ChannelMethod::Pluggable(t) => {
            assert_eq!(t.transport().to_string(), "obfs4");
            assert_eq!(t.settings().collect::<Vec<_>>(), vec![("cert", "abc")]);
        }
        other => panic!("a bridge line with a transport must be Pluggable: {:?}", other),
    }
    assert_eq!(cfg.chan_method().transport_id().to_string(), "obfs4");
}

#[test]
fn chantarget__rsa_identity_is_always_present() {
    let line = pt_bridge_line_builtin();
    let cfg = BridgeConfig::from_str(&line).unwrap();
    let rsa = cfg.rsa_identity().expect("an RSA identity is mandatory for a bridge");
    // `RsaIdentity` displays as `$` followed by the 40 hex digits of the
    // SHA-1 fingerprint; the bridge line spells the same digits without `$`.
    assert_eq!(rsa.to_string().to_uppercase(), format!("${}", RSA_FINGERPRINT));
    assert_eq!(rsa, &crate::common::rsa_id());
}

#[test]
fn chantarget__ed25519_identity_is_absent_unless_named() {
    let cfg = BridgeConfig::from_str(&pt_bridge_line_builtin()).unwrap();
    assert!(cfg.ed_identity().is_none(), "no ed25519 key was written");
    assert!(cfg.identity(RelayIdType::Ed25519).is_none());
    assert!(cfg.identity(RelayIdType::Rsa).is_some());
}

#[test]
fn chantarget__every_identity_the_line_named_is_reported() {
    let line = format!(
        "bridge 198.51.100.5:9001 {} ed25519:{}",
        RSA_FINGERPRINT, crate::common::ED_B64
    );
    let cfg = BridgeConfig::from_str(&line).unwrap();
    let ids: Vec<_> = cfg.identities().collect();
    assert_eq!(ids.len(), 2, "both identities should be reported: {:?}", ids);
    assert!(cfg.has_any_identity());
}

#[test]
fn chantarget__addresses_come_from_the_bridge_line() {
    let cfg = BridgeConfig::from_str(&pt_bridge_line_builtin()).unwrap();
    let addrs: Vec<_> = cfg.addrs().iter().collect();
    assert_eq!(addrs.len(), 1);
    assert_eq!(addrs[0].port(), 9001);

    // A hostname bridge has no socket address, but the method still names it.
    let named = pt_bridge_line("obfs4", "bridges.example.org:443", &[]);
    let cfg = BridgeConfig::from_str(&named).unwrap();
    assert!(cfg.addrs().is_empty(), "a hostname is not a socket address");
    assert_eq!(
        cfg.chan_method().target_addr(),
        Some(tor_linkspec::PtTargetAddr::HostPort("bridges.example.org".into(), 443))
    );
}

/// A bridge line with no pluggable transport.
fn pt_bridge_line_builtin() -> String {
    format!("bridge 198.51.100.5:9001 {}", RSA_FINGERPRINT)
}
