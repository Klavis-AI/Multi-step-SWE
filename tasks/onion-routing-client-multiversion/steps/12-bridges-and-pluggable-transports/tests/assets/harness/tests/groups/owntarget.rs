//! Group: owntarget — owned channel and circuit targets in the bridge world.
//!
//! An owned channel target used to be a list of addresses plus two identity
//! keys.  Now that a relay may be reachable only through a transport, it also
//! carries a `ChannelMethod`, and it is assembled through a builder whose
//! identity fields are a nested relay-id builder rather than positional
//! arguments.

use std::net::SocketAddr;
use std::str::FromStr;

use tor_linkspec::{
    ChanTarget, ChannelMethod, CircTarget, HasAddrs, HasChanMethod, HasRelayIds,
    IntoOwnedChanTarget, OwnedChanTarget, OwnedCircTarget, PtTarget, PtTargetAddr, PtTransportName,
    RelayIdType,
};

use crate::common::{ed_id, rsa_id};

/// Shorthand for a parsed socket address.
fn sa(s: &str) -> SocketAddr {
    s.parse().expect("address should parse")
}

#[test]
fn owntarget__an_unqualified_builder_makes_a_direct_method_from_its_addresses() {
    let t = OwnedChanTarget::builder()
        .addrs(vec![sa("198.51.100.5:9001"), sa("[2001:db8::5]:9001")])
        .ed_identity(ed_id())
        .rsa_identity(rsa_id())
        .build()
        .expect("addresses and identities are enough to build");

    assert_eq!(t.addrs(), &[sa("198.51.100.5:9001"), sa("[2001:db8::5]:9001")]);
    assert_eq!(
        t.chan_method(),
        ChannelMethod::Direct(vec![sa("198.51.100.5:9001"), sa("[2001:db8::5]:9001")]),
        "with no explicit method, the addresses are the method"
    );
}

#[test]
fn owntarget__a_builder_may_name_a_pluggable_method_instead() {
    let pt = PtTarget::new(
        PtTransportName::from_str("obfs4").unwrap(),
        PtTargetAddr::IpPort(sa("198.51.100.5:9001")),
    );
    let t = OwnedChanTarget::builder()
        .method(ChannelMethod::Pluggable(pt))
        .rsa_identity(rsa_id())
        .build()
        .expect("a pluggable method needs no direct addresses");

    // The relay is not reachable at any address of ours, so the address list
    // stays empty even though the transport knows where to go.
    assert_eq!(t.addrs(), &[] as &[SocketAddr]);
    match t.chan_method() {
        ChannelMethod::Pluggable(target) => {
            assert_eq!(target.transport().to_string(), "obfs4");
            assert_eq!(
                target.addr(),
                &PtTargetAddr::IpPort(sa("198.51.100.5:9001"))
            );
        }
        other => panic!("expected a pluggable method, got {:?}", other),
    }
}

#[test]
fn owntarget__the_identity_setters_fill_the_nested_relay_ids() {
    let both = OwnedChanTarget::builder()
        .ed_identity(ed_id())
        .rsa_identity(rsa_id())
        .build()
        .expect("identities alone are enough");
    assert_eq!(both.identity(RelayIdType::Ed25519), Some((&ed_id()).into()));
    assert_eq!(both.identity(RelayIdType::Rsa), Some((&rsa_id()).into()));

    // Each identity is optional on its own.
    let rsa_only = OwnedChanTarget::builder()
        .rsa_identity(rsa_id())
        .build()
        .expect("an RSA identity alone is enough");
    assert_eq!(rsa_only.identity(RelayIdType::Ed25519), None);
    assert!(rsa_only.has_identity((&rsa_id()).into()));
    assert!(both.has_all_relay_ids_from(&rsa_only));
    assert!(!rsa_only.has_all_relay_ids_from(&both));
}

#[test]
fn owntarget__copying_a_bridge_preserves_its_transport_and_identities() {
    let line = format!(
        "bridge obfs4 198.51.100.5:9001 {} cert=abcdef",
        crate::common::RSA_FINGERPRINT
    );
    let bridge: tor_guardmgr::bridge::BridgeConfig =
        line.parse().expect("bridge line should parse");

    let owned = OwnedChanTarget::from_chan_target(&bridge);
    assert!(owned.same_relay_ids(&bridge));
    assert_eq!(owned.chan_method(), bridge.chan_method());
    match owned.chan_method() {
        ChannelMethod::Pluggable(t) => {
            assert_eq!(t.transport().to_string(), "obfs4");
            assert_eq!(t.settings().collect::<Vec<_>>(), vec![("cert", "abcdef")]);
        }
        other => panic!("a transport bridge must stay pluggable: {:?}", other),
    }

    // The same conversion is available through the blanket `IntoOwnedChanTarget`
    // impl for any `&ChanTarget`, and it is the identity on an owned target.
    let via_trait = IntoOwnedChanTarget::to_owned(&bridge);
    assert_eq!(via_trait.to_string(), owned.to_string());
    assert_eq!(
        IntoOwnedChanTarget::to_owned(owned.clone()).to_string(),
        owned.to_string()
    );
}

#[test]
fn owntarget__the_method_can_be_replaced_after_construction() {
    let mut t = OwnedChanTarget::builder()
        .addrs(vec![sa("198.51.100.5:9001")])
        .rsa_identity(rsa_id())
        .build()
        .expect("builds");
    assert_eq!(
        t.chan_method(),
        ChannelMethod::Direct(vec![sa("198.51.100.5:9001")])
    );

    *t.chan_method_mut() = ChannelMethod::Pluggable(PtTarget::new(
        PtTransportName::from_str("snowflake").unwrap(),
        PtTargetAddr::None,
    ));
    match t.chan_method() {
        ChannelMethod::Pluggable(target) => {
            assert_eq!(target.transport().to_string(), "snowflake");
            assert_eq!(target.addr(), &PtTargetAddr::None);
        }
        other => panic!("the replacement method must stick: {:?}", other),
    }
    // Replacing the method does not rewrite the recorded addresses.
    assert_eq!(t.addrs(), &[sa("198.51.100.5:9001")]);
}

#[test]
fn owntarget__display_names_the_connection_point_then_every_identity() {
    let direct = OwnedChanTarget::builder()
        .addrs(vec![sa("198.51.100.5:9001")])
        .ed_identity(ed_id())
        .rsa_identity(rsa_id())
        .build()
        .unwrap();
    assert_eq!(
        direct.to_string(),
        format!(
            "[198.51.100.5:9001 ed25519:{} ${}]",
            crate::common::ED_B64,
            crate::common::RSA_HEX
        )
    );

    // More than one address is abbreviated to the first, with a trailing `+`.
    let many = OwnedChanTarget::builder()
        .addrs(vec![sa("198.51.100.5:9001"), sa("198.51.100.6:9001")])
        .rsa_identity(rsa_id())
        .build()
        .unwrap();
    assert_eq!(
        many.to_string(),
        format!("[198.51.100.5:9001+ ${}]", crate::common::RSA_HEX)
    );

    // A target with no way to reach it at all displays a question mark.
    let nowhere = OwnedChanTarget::builder()
        .rsa_identity(rsa_id())
        .build()
        .unwrap();
    assert_eq!(
        nowhere.to_string(),
        format!("[? ${}]", crate::common::RSA_HEX)
    );
}

#[test]
fn owntarget__a_circuit_target_wraps_a_channel_target() {
    let mut builder = OwnedCircTarget::builder();
    builder
        .chan_target()
        .addrs(vec![sa("198.51.100.5:9001")])
        .ed_identity(ed_id())
        .rsa_identity(rsa_id());
    let ct = builder
        .ntor_onion_key([0x42; 32].into())
        .protocols("FlowCtrl=1-2 Link=1-5".parse().unwrap())
        .build()
        .expect("a circuit target needs an onion key and a protocol list");

    // The channel-level view is forwarded from the nested chan target.
    assert_eq!(ct.addrs(), &[sa("198.51.100.5:9001")]);
    assert_eq!(
        ct.chan_method(),
        ChannelMethod::Direct(vec![sa("198.51.100.5:9001")])
    );
    assert_eq!(ct.identity(RelayIdType::Rsa), Some((&rsa_id()).into()));

    // The circuit-level view adds the extend keys.
    assert_eq!(ct.ntor_onion_key().as_bytes(), &[0x42; 32]);
    assert_eq!(ct.protovers().to_string(), "FlowCtrl=1-2 Link=1-5");
}
