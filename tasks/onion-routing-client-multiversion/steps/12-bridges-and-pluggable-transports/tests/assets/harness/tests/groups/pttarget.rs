//! Group: pttarget — what a transport is told about its bridge.
//!
//! `PtTarget` bundles the transport name, the address it should reach, and the
//! `key=value` settings that travel in the SOCKS handshake.  The setting syntax
//! is the bridge-line syntax, because a setting that cannot be written in a
//! bridge line cannot be configured at all.

use std::net::{IpAddr, Ipv4Addr, SocketAddr};
use std::str::FromStr;
use tor_linkspec::{PtTarget, PtTargetAddr, PtTargetInvalidSetting, PtTargetSettings, PtTransportName};

fn name(s: &str) -> PtTransportName {
    PtTransportName::from_str(s).unwrap()
}

#[test]
fn pttarget__new_has_no_settings() {
    let t = PtTarget::new(name("obfs4"), PtTargetAddr::None);
    assert_eq!(t.transport().to_string(), "obfs4");
    assert_eq!(t.addr(), &PtTargetAddr::None);
    assert_eq!(t.settings().count(), 0);
}

#[test]
fn pttarget__settings_keep_their_order_and_values() {
    let mut t = PtTarget::new(
        name("obfs4"),
        PtTargetAddr::IpPort(SocketAddr::new(IpAddr::V4(Ipv4Addr::LOCALHOST), 9001)),
    );
    t.push_setting("cert", "abcdef").unwrap();
    t.push_setting("iat-mode", "1").unwrap();
    t.push_setting("arg3", "val3").unwrap();
    let got: Vec<(&str, &str)> = t.settings().collect();
    assert_eq!(got, vec![("cert", "abcdef"), ("iat-mode", "1"), ("arg3", "val3")]);
}

#[test]
fn pttarget__a_key_carrying_an_equals_sign_is_refused() {
    let mut t = PtTarget::new(name("obfs4"), PtTargetAddr::None);
    match t.push_setting("cer=t", "v") {
        Err(PtTargetInvalidSetting::Key(ref k)) => assert_eq!(k, "cer=t"),
        other => panic!("a key with `=` cannot be written in a bridge line: {:?}", other),
    }
}

#[test]
fn pttarget__a_key_carrying_whitespace_is_refused() {
    let mut t = PtTarget::new(name("obfs4"), PtTargetAddr::None);
    match t.push_setting("cert key", "v") {
        Err(PtTargetInvalidSetting::Key(ref k)) => assert_eq!(k, "cert key"),
        other => panic!("a key with whitespace cannot be written in a bridge line: {:?}", other),
    }
}

#[test]
fn pttarget__a_value_carrying_whitespace_is_refused() {
    let mut t = PtTarget::new(name("obfs4"), PtTargetAddr::None);
    match t.push_setting("cert", "two words") {
        Err(PtTargetInvalidSetting::Value(ref v)) => assert_eq!(v, "two words"),
        other => panic!("a value with whitespace cannot be written in a bridge line: {:?}", other),
    }
}

#[test]
fn pttarget__into_parts_returns_everything_it_was_given() {
    let mut t = PtTarget::new(name("snowflake"), PtTargetAddr::HostPort("b.example".into(), 443));
    t.push_setting("key1", "val1").unwrap();
    let (trans, addr, settings) = t.into_parts();
    assert_eq!(trans.to_string(), "snowflake");
    assert_eq!(addr, PtTargetAddr::HostPort("b.example".to_string(), 443));
    assert_eq!(settings.into_inner(), vec![("key1".to_string(), "val1".to_string())]);
}

#[test]
fn pttarget__socket_addrs_are_offered_only_for_a_numeric_target() {
    let numeric = PtTarget::new(
        name("obfs4"),
        PtTargetAddr::IpPort(SocketAddr::new(IpAddr::V4(Ipv4Addr::LOCALHOST), 9001)),
    );
    assert_eq!(numeric.socket_addrs().map(|s| s.len()), Some(1));

    let named = PtTarget::new(name("obfs4"), PtTargetAddr::HostPort("b".into(), 443));
    assert_eq!(named.socket_addrs(), None);

    let absent = PtTarget::new(name("obfs4"), PtTargetAddr::None);
    assert_eq!(absent.socket_addrs(), None);
}

#[test]
fn pttarget__settings_bulk_conversion_checks_every_pair() {
    let good: Vec<(String, String)> = vec![("a".into(), "b".into()), ("c".into(), "d".into())];
    let settings = PtTargetSettings::try_from(good.clone()).expect("both pairs are well-formed");
    assert_eq!(Vec::<(String, String)>::from(settings), good);

    let bad: Vec<(String, String)> = vec![("a".into(), "b".into()), ("c d".into(), "e".into())];
    assert!(PtTargetSettings::try_from(bad).is_err());
}
