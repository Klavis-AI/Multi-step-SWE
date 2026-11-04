//! Group: chanmethod — how a relay is to be reached.
//!
//! `ChannelMethod` is the single place that answers "directly, or through which
//! transport"; it is what `HasChanMethod` hands the channel manager, so its
//! answers about addresses, transport identity and coverage are contract.

use std::net::{IpAddr, Ipv4Addr, SocketAddr};
use std::str::FromStr;
use tor_linkspec::{BridgeAddr, ChannelMethod, PtTarget, PtTargetAddr, PtTransportName};

fn sa(a: u8, port: u16) -> SocketAddr {
    SocketAddr::new(IpAddr::V4(Ipv4Addr::new(198, 51, 100, a)), port)
}

fn pt_target(name: &str, addr: PtTargetAddr) -> PtTarget {
    PtTarget::new(PtTransportName::from_str(name).unwrap(), addr)
}

#[test]
fn chanmethod__direct_keeps_every_address_in_order() {
    let m = ChannelMethod::Direct(vec![sa(1, 9001), sa(2, 443), sa(3, 80)]);
    assert!(m.is_direct());
    assert_eq!(m.socket_addrs(), Some(&[sa(1, 9001), sa(2, 443), sa(3, 80)][..]));
    assert_eq!(m.transport_id(), tor_linkspec::TransportId::new_builtin());
}

#[test]
fn chanmethod__direct_reports_its_first_address_as_the_target() {
    let m = ChannelMethod::Direct(vec![sa(1, 9001), sa(2, 443)]);
    assert_eq!(m.target_addr(), Some(PtTargetAddr::IpPort(sa(1, 9001))));
}

#[test]
fn chanmethod__empty_direct_has_no_target() {
    let m = ChannelMethod::Direct(vec![]);
    assert_eq!(m.target_addr(), None);
    assert_eq!(m.socket_addrs(), Some(&[][..]));
}

#[test]
fn chanmethod__pluggable_names_its_transport() {
    let t = pt_target("obfs4", PtTargetAddr::IpPort(sa(1, 9001)));
    let m = ChannelMethod::Pluggable(t);
    assert!(!m.is_direct());
    assert_eq!(m.transport_id().to_string(), "obfs4");
    assert_eq!(m.target_addr(), Some(PtTargetAddr::IpPort(sa(1, 9001))));
}

#[test]
fn chanmethod__pluggable_without_an_address_advertises_none() {
    let t = pt_target("snowflake", PtTargetAddr::None);
    let m = ChannelMethod::Pluggable(t);
    assert_eq!(m.socket_addrs(), None);
    assert_eq!(m.target_addr(), Some(PtTargetAddr::None));
}

#[test]
fn chanmethod__retain_addrs_drops_what_the_predicate_rejects() {
    let mut m = ChannelMethod::Direct(vec![sa(1, 9001), sa(2, 443)]);
    m.retain_addrs(|a| a.port() == 443).expect("one address survives");
    assert_eq!(m.socket_addrs(), Some(&[sa(2, 443)][..]));
}

#[test]
fn chanmethod__retain_addrs_reports_when_nothing_is_left() {
    // Direct: rejecting the only address empties the list and is an error.
    let mut m = ChannelMethod::Direct(vec![sa(1, 9001)]);
    assert!(m.retain_addrs(|_| false).is_err(), "removing every address is an error");
    assert_eq!(m.socket_addrs(), Some(&[][..]), "the rejected address is gone");

    // Pluggable, numeric target: the address is replaced by the absent form.
    let mut p = ChannelMethod::Pluggable(pt_target("obfs4", PtTargetAddr::IpPort(sa(1, 9001))));
    assert!(p.retain_addrs(|a| a.port() == 443).is_err());
    assert_eq!(p.target_addr(), Some(PtTargetAddr::None));

    // Pluggable, named or absent target: nothing to remove, so no error.
    let mut h = ChannelMethod::Pluggable(pt_target(
        "obfs4",
        PtTargetAddr::HostPort("example.com".to_string(), 443),
    ));
    assert!(h.retain_addrs(|_| false).is_ok(), "a hostname is never removed");
    assert_eq!(
        h.target_addr(),
        Some(PtTargetAddr::HostPort("example.com".to_string(), 443))
    );
}

#[test]
fn chanmethod__contained_by_is_pointwise_for_direct_methods() {
    let narrow = ChannelMethod::Direct(vec![sa(1, 9001)]);
    let wide = ChannelMethod::Direct(vec![sa(1, 9001), sa(2, 443)]);
    assert!(narrow.contained_by(&wide));
    assert!(!wide.contained_by(&narrow));

    let plug = ChannelMethod::Pluggable(pt_target("obfs4", PtTargetAddr::None));
    assert!(!plug.contained_by(&wide), "no direct method covers a pluggable one");
    assert!(plug.contained_by(&plug.clone()));
}
