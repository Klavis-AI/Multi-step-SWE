//! Group `owntarget`: the owned relay-target types
//! (`tor_linkspec::{RelayIds, OwnedChanTarget, OwnedCircTarget}`).
//!
//! A `ChanTarget` is usually a borrow into a live directory object; these owned
//! forms are the snapshot that gets carried into a channel manager, a retry
//! queue, or a log line long after that directory has been replaced. What has
//! to survive the copy is precisely the identity set -- including the *absence*
//! of an identity, which is now representable -- the address list, and, for a
//! circuit target, the ntor key and the protocol list. These cases grade the
//! copy, the address-restriction helper used when retrying a single address,
//! the link-specifier ordering that goes on the wire, and the logging form.
#![allow(non_snake_case)]

use std::net::SocketAddr;

use tor_linkspec::{
    ChanTarget, CircTarget, HasAddrs, HasRelayIds, LinkSpec, OwnedChanTarget, OwnedCircTarget,
    RelayId, RelayIdRef, RelayIdType, RelayIds,
};
use tor_llcrypto::pk::{curve25519, ed25519::Ed25519Identity, rsa::RsaIdentity};

use crate::common::{ed_id, rsa_id};

/// A borrowed-style channel target that may be missing an identity.
struct Source {
    addrs: Vec<SocketAddr>,
    ed: Option<Ed25519Identity>,
    rsa: Option<RsaIdentity>,
}
impl HasAddrs for Source {
    fn addrs(&self) -> &[SocketAddr] {
        &self.addrs[..]
    }
}
impl HasRelayIds for Source {
    fn identity(&self, key_type: RelayIdType) -> Option<RelayIdRef<'_>> {
        match key_type {
            RelayIdType::Ed25519 => self.ed.as_ref().map(|k| k.into()),
            RelayIdType::Rsa => self.rsa.as_ref().map(|k| k.into()),
            _ => None,
        }
    }
}
impl ChanTarget for Source {}

/// Parse a socket address in a test.
fn sa(s: &str) -> SocketAddr {
    s.parse().expect("test address parses")
}

/// Constructing from parts stores the addresses in order and both identities,
/// and the result answers the `HasAddrs`/`HasRelayIds` interface.
#[test]
fn owntarget__new_records_addrs_and_ids() {
    let t = OwnedChanTarget::new(
        vec![sa("127.0.0.1:11"), sa("[::1]:12")],
        ed_id(0x2a),
        rsa_id(0x2d),
    );

    assert_eq!(t.addrs(), &[sa("127.0.0.1:11"), sa("[::1]:12")][..]);
    assert_eq!(t.ed_identity(), Some(&ed_id(0x2a)));
    assert_eq!(t.rsa_identity(), Some(&rsa_id(0x2d)));
    assert_eq!(t.identities().count(), 2);
    assert!(t.has_identity(RelayIdRef::from(&ed_id(0x2a))));
}

/// Copying from a borrowed target preserves whatever identities it had --
/// including none at all. An implementation that invented a zero identity for a
/// missing key would make an unauthenticated relay look fully identified.
#[test]
fn owntarget__from_chan_target_preserves_partial_ids() {
    let full = Source {
        addrs: vec![sa("10.0.0.1:443")],
        ed: Some(ed_id(1)),
        rsa: Some(rsa_id(2)),
    };
    let copied = OwnedChanTarget::from_chan_target(&full);
    assert!(copied.same_relay_ids(&full));
    assert_eq!(copied.addrs(), full.addrs());

    let ed_only = Source {
        addrs: vec![],
        ed: Some(ed_id(1)),
        rsa: None,
    };
    let copied = OwnedChanTarget::from_chan_target(&ed_only);
    assert_eq!(copied.ed_identity(), Some(&ed_id(1)));
    assert_eq!(copied.rsa_identity(), None, "an absent id stays absent");
    assert_eq!(copied.identities().count(), 1);
    assert!(copied.addrs().is_empty());
    assert!(copied.same_relay_ids(&ed_only));
}

/// The `Display` form is what shows up in logs and errors. It summarises the
/// address list rather than printing it: no addresses at all is `?`, exactly
/// one is that address, and more than one is the first address with a trailing
/// `+`. The identities follow, in iteration order.
#[test]
fn owntarget__display_summarizes_addrs() {
    let ids = format!(" {} {}", RelayId::from(ed_id(1)), RelayId::from(rsa_id(2)));

    let none = OwnedChanTarget::new(vec![], ed_id(1), rsa_id(2));
    assert_eq!(none.to_string(), format!("[?{}]", ids));

    let one = OwnedChanTarget::new(vec![sa("127.0.0.1:11")], ed_id(1), rsa_id(2));
    assert_eq!(one.to_string(), format!("[127.0.0.1:11{}]", ids));

    let many = OwnedChanTarget::new(
        vec![sa("127.0.0.1:11"), sa("127.0.0.2:22")],
        ed_id(1),
        rsa_id(2),
    );
    assert_eq!(many.to_string(), format!("[127.0.0.1:11+{}]", ids));
}

/// `restrict_addr` narrows a target to a single address for a retry. It reports
/// through `Result` whether the address was actually one of the target's: `Ok`
/// carries the narrowed target, `Err` hands back the unmodified original. Both
/// arms keep the identities, so neither can be used to connect to a relay
/// without checking who it is.
#[test]
fn owntarget__restrict_addr_reports_membership() {
    let t = OwnedChanTarget::new(
        vec![sa("127.0.0.1:11"), sa("127.0.0.2:22")],
        ed_id(1),
        rsa_id(2),
    );

    let narrowed = t
        .restrict_addr(&sa("127.0.0.2:22"))
        .expect("that address belongs to the target");
    assert_eq!(narrowed.addrs(), &[sa("127.0.0.2:22")][..]);
    assert!(narrowed.same_relay_ids(&t));

    let rejected = t
        .restrict_addr(&sa("127.0.0.3:33"))
        .expect_err("that address does not belong to the target");
    assert_eq!(rejected.addrs(), t.addrs(), "the original is returned intact");
    assert!(rejected.same_relay_ids(&t));

    // Narrowing to the one address it already has is still Ok.
    assert!(narrowed.restrict_addr(&sa("127.0.0.2:22")).is_ok());
    assert!(narrowed.restrict_addr(&sa("127.0.0.1:11")).is_err());
}

/// A circuit target adds the ntor onion key and the relay's subprotocol list,
/// and still forwards the channel-target half unchanged. Copying one with
/// `from_circ_target` must carry all four pieces.
#[test]
fn owntarget__circ_target_carries_ntor_and_protovers() {
    let chan = OwnedChanTarget::new(vec![sa("127.0.0.1:11")], ed_id(1), rsa_id(2));
    let protos: tor_protover::Protocols = "FlowCtrl=7".parse().unwrap();
    let ct = OwnedCircTarget::new(
        chan.clone(),
        curve25519::PublicKey::from([99_u8; 32]),
        protos,
    );

    assert_eq!(ct.ntor_onion_key().as_bytes(), &[99_u8; 32]);
    assert_eq!(ct.protovers().to_string(), "FlowCtrl=7");
    assert_eq!(ct.addrs(), chan.addrs());
    assert!(ct.same_relay_ids(&chan));
    assert_eq!(ct.to_string(), chan.to_string());

    let copy = OwnedCircTarget::from_circ_target(&ct);
    assert_eq!(copy.ntor_onion_key().as_bytes(), ct.ntor_onion_key().as_bytes());
    assert_eq!(copy.protovers().to_string(), ct.protovers().to_string());
    assert_eq!(copy.addrs(), ct.addrs());
    assert!(copy.same_relay_ids(&ct));
}

/// The link specifiers sent in an EXTEND2 cell are built from the target, and
/// their order is fixed by the protocol: the Ed25519 identity, then the RSA
/// identity, then the addresses in order. A different order is a different
/// cell, and the far relay will refuse it.
#[test]
fn owntarget__linkspecs_order_ids_before_addrs() {
    let chan = OwnedChanTarget::new(
        vec![sa("127.0.0.1:99"), sa("[::1]:909")],
        ed_id(1),
        rsa_id(2),
    );
    let ct = OwnedCircTarget::new(
        chan,
        curve25519::PublicKey::from([7_u8; 32]),
        tor_protover::Protocols::default(),
    );

    let specs = ct.linkspecs();
    assert_eq!(specs.len(), 4);
    assert_eq!(specs[0], LinkSpec::Ed25519Id(ed_id(1)));
    assert_eq!(specs[1], LinkSpec::RsaId(rsa_id(2)));
    assert_eq!(
        specs[2],
        LinkSpec::OrPort("127.0.0.1".parse().unwrap(), 99)
    );
    assert_eq!(specs[3], LinkSpec::OrPort("::1".parse().unwrap(), 909));
}

/// `RelayIds` is the standalone owned identity set, and it is serializable so
/// that a relay can be named in a config or state file. Each identity is an
/// optional field under its algorithm name, so "we do not know this relay's RSA
/// identity" survives a round trip.
#[test]
fn owntarget__relay_ids_are_optional_and_serializable() {
    let ids = RelayIds::new(ed_id(1), rsa_id(2));
    assert_eq!(ids.ed_identity(), Some(&ed_id(1)));
    assert_eq!(ids.rsa_identity(), Some(&rsa_id(2)));

    let json: serde_json::Value = serde_json::to_value(&ids).unwrap();
    assert_eq!(json["rsa"], serde_json::json!(hex::encode([2_u8; 20])));
    assert!(json["ed25519"].is_string());
    let back: RelayIds = serde_json::from_value(json).unwrap();
    assert_eq!(back, ids);

    // Built from a partial source, the missing identity stays missing.
    let ed_only = Source {
        addrs: vec![],
        ed: Some(ed_id(1)),
        rsa: None,
    };
    let partial = RelayIds::from_relay_ids(&ed_only);
    assert_eq!(partial.rsa_identity(), None);
    assert_ne!(partial, ids);
    let json = serde_json::to_value(&partial).unwrap();
    assert_eq!(json["rsa"], serde_json::Value::Null);
    assert_eq!(
        serde_json::from_value::<RelayIds>(json).unwrap(),
        partial,
        "an absent identity round-trips as absent"
    );
}
