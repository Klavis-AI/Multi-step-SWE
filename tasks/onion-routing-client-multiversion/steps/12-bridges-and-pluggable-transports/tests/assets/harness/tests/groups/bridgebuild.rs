//! Group: bridgebuild — building a bridge from its parts.
//!
//! The line grammar is one way in; the builder is the other, and it is the one
//! the configuration layer uses.  The builder is deliberately stricter than the
//! parser where a combination cannot mean anything, and these cases pin both
//! the acceptances and the refusals together with the fields they name.

use crate::common::RSA_FINGERPRINT;
use std::net::{IpAddr, Ipv4Addr, SocketAddr};
use std::str::FromStr;
use tor_config::ConfigBuildError;
use tor_guardmgr::bridge::{BridgeConfig, BridgeConfigBuilder};
use tor_linkspec::HasAddrs;
use tor_linkspec::{BridgeAddr, ChannelMethod, HasChanMethod, HasRelayIds, RelayId};

fn sock(port: u16) -> SocketAddr {
    SocketAddr::new(IpAddr::V4(Ipv4Addr::new(198, 51, 100, 40)), port)
}

/// The RSA identity the fixtures use, as the builder wants it.
fn rsa_id() -> RelayId {
    RelayId::from_str(RSA_FINGERPRINT).expect("the fixture fingerprints should parse")
}

#[test]
fn bridgebuild__direct_builder_from_parts() {
    let mut b = BridgeConfigBuilder::default();
    b.transport("").addrs().push(BridgeAddr::new_addr_from_sockaddr(sock(9001)));
    b.ids().push(rsa_id());
    let cfg = b.build().expect("a direct bridge with one address and one key is valid");
    assert!(cfg.chan_method().is_direct());
    assert_eq!(cfg.addrs(), &[sock(9001)]);
}

#[test]
fn bridgebuild__pluggable_builder_carries_settings() {
    let mut b = BridgeConfigBuilder::default();
    b.transport("obfs4")
        .addrs()
        .push(BridgeAddr::new_named_host_port("bridges.example.org", 443));
    b.ids().push(rsa_id());
    b.push_setting("cert", "abcdef");
    b.push_setting("iat-mode", "1");
    let cfg = b.build().expect("a pluggable bridge with a hostname is valid");
    match cfg.chan_method() {
        ChannelMethod::Pluggable(t) => {
            assert_eq!(t.transport().to_string(), "obfs4");
            assert_eq!(t.settings().collect::<Vec<_>>(), vec![("cert", "abcdef"), ("iat-mode", "1")]);
        }
        other => panic!("expected a pluggable method, got {:?}", other),
    }
}

#[test]
fn bridgebuild__direct_refuses_transport_settings() {
    let mut b = BridgeConfigBuilder::default();
    b.direct().addrs().push(BridgeAddr::new_addr_from_sockaddr(sock(9001)));
    b.ids().push(rsa_id());
    b.push_setting("cert", "abcdef");
    match b.build() {
        Err(ConfigBuildError::Inconsistent { fields, .. }) => {
            assert!(fields.contains(&"settings".to_string()), "fields were {:?}", fields);
            assert!(fields.contains(&"transport".to_string()), "fields were {:?}", fields);
        }
        other => panic!("settings need a transport to travel in: {:?}", other),
    }
}

#[test]
fn bridgebuild__direct_refuses_a_hostname() {
    let mut b = BridgeConfigBuilder::default();
    b.direct().addrs().push(BridgeAddr::new_named_host_port("bridges.example.org", 443));
    b.ids().push(rsa_id());
    match b.build() {
        Err(ConfigBuildError::Inconsistent { fields, .. }) => {
            assert!(fields.contains(&"addrs".to_string()), "fields were {:?}", fields);
        }
        other => panic!("a direct connection has no way to resolve a name: {:?}", other),
    }
}

#[test]
fn bridgebuild__direct_refuses_an_empty_address_list() {
    let mut b = BridgeConfigBuilder::default();
    b.direct();
    b.ids().push(rsa_id());
    assert!(matches!(
        b.build(),
        Err(ConfigBuildError::Inconsistent { .. })
    ));
}

#[test]
fn bridgebuild__pluggable_refuses_more_than_one_address() {
    let mut b = BridgeConfigBuilder::default();
    b.transport("obfs4");
    b.addrs().push(BridgeAddr::new_addr_from_sockaddr(sock(9001)));
    b.addrs().push(BridgeAddr::new_addr_from_sockaddr(sock(443)));
    b.ids().push(rsa_id());
    match b.build() {
        Err(ConfigBuildError::Inconsistent { fields, .. }) => {
            assert!(fields.contains(&"addrs".to_string()), "fields were {:?}", fields);
        }
        other => panic!("a transport takes a single nominal target: {:?}", other),
    }
}

#[test]
fn bridgebuild__a_bridge_without_an_rsa_identity_is_refused() {
    let mut b = BridgeConfigBuilder::default();
    b.direct().addrs().push(BridgeAddr::new_addr_from_sockaddr(sock(9001)));
    match b.build() {
        Err(ConfigBuildError::Invalid { field, .. }) => assert_eq!(field, "ids"),
        other => panic!("an RSA identity is mandatory for a bridge: {:?}", other),
    }
}

#[test]
fn bridgebuild__an_unparseable_transport_name_is_refused() {
    let mut b = BridgeConfigBuilder::default();
    b.transport("obfs-4").addrs().push(BridgeAddr::new_addr_from_sockaddr(sock(9001)));
    b.ids().push(rsa_id());
    match b.build() {
        Err(ConfigBuildError::Invalid { field, .. }) => assert_eq!(field, "transport"),
        other => panic!("`obfs-4` is not a well-formed method name: {:?}", other),
    }
}

#[test]
fn bridgebuild__a_setting_with_no_usable_value_is_refused() {
    let mut b = BridgeConfigBuilder::default();
    b.transport("obfs4");
    b.addrs().push(BridgeAddr::new_addr_from_sockaddr(sock(9001)));
    b.ids().push(rsa_id());
    b.push_setting("cert", "two words");
    match b.build() {
        Err(ConfigBuildError::Invalid { field, .. }) => {
            assert_eq!(field, "settings.0", "the offending setting should be indexed")
        }
        other => panic!("a value with whitespace cannot be written down: {:?}", other),
    }
}
