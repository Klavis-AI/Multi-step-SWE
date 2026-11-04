//! Group: briaddr — bridge addresses and PT target addresses.
//!
//! A bridge may be named by a numeric address or by a hostname.  `BridgeAddr`
//! covers both and refuses to pretend one is the other; `PtTargetAddr` is the
//! same idea with the extra `-` spelling the bridge-line grammar uses for
//! "this transport finds its own address".

use std::net::{IpAddr, Ipv4Addr, Ipv6Addr, SocketAddr};
use std::str::FromStr;
use tor_linkspec::{BridgeAddr, BridgeAddrError, HasAddrs, PtTargetAddr};

fn v4(a: u8, b: u8, c: u8, d: u8, port: u16) -> SocketAddr {
    SocketAddr::new(IpAddr::V4(Ipv4Addr::new(a, b, c, d)), port)
}

#[test]
fn briaddr__numeric_address_keeps_its_socketaddr() {
    let sock = v4(198, 51, 100, 1, 9001);
    let addr = BridgeAddr::new_addr_from_sockaddr(sock);
    assert_eq!(addr.as_socketaddr(), Some(&sock));
    assert_eq!(addr.as_host_port(), None);
}

#[test]
fn briaddr__ipv6_address_keeps_its_socketaddr() {
    let sock = SocketAddr::new(IpAddr::V6(Ipv6Addr::LOCALHOST), 443);
    let addr = BridgeAddr::new_addr_from_sockaddr(sock);
    assert_eq!(addr.as_socketaddr(), Some(&sock));
    assert_eq!(addr.to_string(), "[::1]:443");
}

#[test]
fn briaddr__hostname_keeps_its_name_and_port() {
    let addr = BridgeAddr::new_named_host_port("example.com", 443);
    assert_eq!(addr.as_host_port(), Some(("example.com", 443)));
    assert_eq!(addr.as_socketaddr(), None);
    assert_eq!(addr.to_string(), "example.com:443");
}

#[test]
fn briaddr__numeric_target_parses_and_displays_identically() {
    let s = "198.51.100.7:9001";
    let addr = BridgeAddr::from_str(s).unwrap();
    assert_eq!(addr.as_socketaddr(), Some(&v4(198, 51, 100, 7, 9001)));
    assert_eq!(addr.to_string(), s);
}

#[test]
fn briaddr__named_target_parses_and_displays_identically() {
    let s = "bridges.torproject.org:443";
    let addr = BridgeAddr::from_str(s).unwrap();
    assert_eq!(addr.as_host_port(), Some(("bridges.torproject.org", 443)));
    assert_eq!(addr.to_string(), s);
}

#[test]
fn briaddr__target_without_a_port_is_rejected() {
    for s in ["example.com", "198.51.100.7"] {
        match BridgeAddr::from_str(s) {
            Err(BridgeAddrError::BadAddress(ref got)) => assert_eq!(got, s),
            other => panic!("{:?} names no port and must be rejected, got {:?}", s, other),
        }
    }
}

#[test]
fn briaddr__pt_target_none_is_the_hyphen() {
    let none = PtTargetAddr::from_str("-").expect("`-` is the absent-address spelling");
    assert_eq!(none, PtTargetAddr::None);
    assert_eq!(none.to_string(), "-");
    assert!(none.addrs().is_empty());
}

#[test]
fn briaddr__pt_target_mirrors_bridge_addr() {
    let from_str = PtTargetAddr::from_str("198.51.100.7:9001").unwrap();
    assert_eq!(from_str, PtTargetAddr::IpPort(v4(198, 51, 100, 7, 9001)));

    let named = PtTargetAddr::from_str("example.com:443").unwrap();
    assert_eq!(named, PtTargetAddr::HostPort("example.com".to_string(), 443));
    assert_eq!(named.to_string(), "example.com:443");

    let back: Option<BridgeAddr> = named.clone().into();
    assert_eq!(back, Some(BridgeAddr::new_named_host_port("example.com", 443)));
    let round: PtTargetAddr = back.into();
    assert_eq!(round, named);
}
