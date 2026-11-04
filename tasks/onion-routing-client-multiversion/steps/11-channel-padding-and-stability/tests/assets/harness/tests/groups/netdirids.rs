//! Group `netdirids`: looking a relay up in a `tor_netdir::NetDir` by its
//! identities (`by_id`, `by_ids`, `ids_listed`).
//!
//! Before this milestone a lookup took a concrete key type; now it takes
//! anything that can be borrowed as a relay identity, and the directory has to
//! answer three genuinely different questions: "give me the usable relay with
//! this one identity", "give me the usable relay matching *all* of these
//! identities", and "is a relay with these identities in the consensus at all".
//! The third is tri-state on purpose -- `None` means "we cannot tell yet,
//! because a microdescriptor has not arrived" and must not be conflated with
//! `Some(false)`, or a client will conclude a bridge or guard has been removed
//! from the network when it has merely not been downloaded.
#![allow(non_snake_case)]

use tor_linkspec::{HasRelayIds, RelayIdRef, RelayIdType, RelayIds};
use tor_llcrypto::pk::{ed25519::Ed25519Identity, rsa::RsaIdentity};
use tor_netdir::{testnet, NetDir};

use crate::common::{ed_id, rsa_id};

/// The standard 40-relay test network. Relay `i` has Ed25519 identity `[i; 32]`
/// and RSA identity `[i; 20]`.
fn netdir() -> NetDir {
    testnet::construct_netdir()
        .unwrap_if_sufficient()
        .expect("the test network has enough paths")
}

/// The same network, but relay `skip` has no microdescriptor, so it is listed
/// in the consensus yet not usable.
fn netdir_missing_md(skip: usize) -> NetDir {
    testnet::construct_custom_netdir(|idx, nb| {
        if idx == skip {
            nb.omit_md = true;
        }
    })
    .expect("custom test network builds")
    .unwrap_if_sufficient()
    .expect("the test network still has enough paths")
}

/// A relay can be found by either of its identities, and both lookups reach the
/// same relay. This is what makes the identity abstraction useful: the caller
/// does not have to know which kind of identity it was handed.
#[test]
fn netdirids__by_id_accepts_either_identity() {
    let dir = netdir();

    let by_ed = dir.by_id(&ed_id(5)).expect("relay 5 by ed25519 identity");
    let by_rsa = dir.by_id(&rsa_id(5)).expect("relay 5 by RSA identity");

    assert_eq!(by_ed.id(), &ed_id(5));
    assert_eq!(by_ed.rsa_id(), &rsa_id(5));
    assert!(by_ed.same_relay(&by_rsa));
    assert!(by_ed.same_relay_ids(&by_rsa));

    // An owned identity reference works too.
    let ids = RelayIds::new(ed_id(5), rsa_id(5));
    let r = dir
        .by_id(ids.identity(RelayIdType::Ed25519).unwrap())
        .expect("lookup by a borrowed identity");
    assert!(r.same_relay(&by_ed));
}

/// An identity that belongs to no relay yields `None` rather than an arbitrary
/// relay. The two identity namespaces are separate, so an RSA identity whose
/// bytes resemble a known Ed25519 identity is still not a match.
#[test]
fn netdirids__by_id_rejects_unknown_identity() {
    let dir = netdir();

    assert!(dir.by_id(&ed_id(200)).is_none());
    assert!(dir.by_id(&rsa_id(200)).is_none());
    // Relay 39 exists; relay 40 does not (the network has 40 relays, 0..=39).
    assert!(dir.by_id(&ed_id(39)).is_some());
    assert!(dir.by_id(&ed_id(40)).is_none());
}

/// A relay whose microdescriptor has not arrived is listed but not *usable*,
/// and `by_id` returns only usable relays. `all_relays` still shows it, which
/// is how the directory manager knows what it is still missing.
#[test]
fn netdirids__by_id_skips_unusable_relays() {
    let dir = netdir_missing_md(3);

    assert!(dir.by_id(&ed_id(3)).is_none(), "no microdesc means unusable");
    assert!(dir.by_id(&rsa_id(3)).is_none());
    assert!(dir.by_id(&ed_id(4)).is_some(), "its neighbours are fine");

    assert_eq!(dir.all_relays().count(), 40);
    assert_eq!(dir.relays().count(), 39);
    let unusable: Vec<_> = dir.all_relays().filter(|r| !r.is_usable()).collect();
    assert_eq!(unusable.len(), 1);
    assert_eq!(unusable[0].rsa_identity(), Some(&rsa_id(3)));
}

/// `by_ids` requires *every* identity of the request to match the relay it
/// finds. A relay that matches only one of the two requested identities is not
/// the relay that was asked for, and returning it would defeat the point of
/// carrying two identities.
#[test]
fn netdirids__by_ids_requires_all_identities_to_match() {
    let dir = netdir();

    let matching = RelayIds::new(ed_id(6), rsa_id(6));
    let found = dir.by_ids(&matching).expect("a fully matching relay");
    assert_eq!(found.id(), &ed_id(6));

    let mismatched = RelayIds::new(ed_id(6), rsa_id(7));
    assert!(
        dir.by_ids(&mismatched).is_none(),
        "a half match is not a match"
    );

    // A request naming only one identity is satisfied by that relay.
    let ed_only = RelayIds::from_relay_ids(&dir.by_id(&ed_id(6)).unwrap());
    assert!(dir.by_ids(&ed_only).is_some());

    // A request naming no identities at all cannot be satisfied.
    let empty = RelayIds::from_relay_ids(&EmptyIds);
    assert!(dir.by_ids(&empty).is_none());
}

/// A target with no identities, used to probe the degenerate cases.
struct EmptyIds;
impl HasRelayIds for EmptyIds {
    fn identity(&self, _t: RelayIdType) -> Option<RelayIdRef<'_>> {
        None
    }
}

/// `ids_listed` is tri-state. `Some(true)`/`Some(false)` are durable answers;
/// `None` means "not yet knowable" and must be returned when the relay's
/// microdescriptor -- the only place its Ed25519 identity is recorded -- has
/// not arrived.
#[test]
fn netdirids__ids_listed_is_tri_state() {
    let dir = netdir();

    assert_eq!(dir.ids_listed(&RelayIds::new(ed_id(8), rsa_id(8))), Some(true));
    assert_eq!(
        dir.ids_listed(&RelayIds::new(ed_id(8), rsa_id(9))),
        Some(false),
        "a known RSA id paired with the wrong ed id is definitely not listed"
    );
    assert_eq!(
        dir.ids_listed(&RelayIds::new(ed_id(200), rsa_id(200))),
        Some(false),
        "an entirely unknown RSA id is definitely not listed"
    );
    assert_eq!(dir.ids_listed(&EmptyIds), None, "nothing was asked about");

    // Without the microdescriptor we cannot confirm the ed25519 identity.
    let partial = netdir_missing_md(3);
    assert_eq!(
        partial.ids_listed(&RelayIds::new(ed_id(3), rsa_id(3))),
        None,
        "listed in the consensus but not yet confirmable"
    );
    assert_eq!(
        partial.ids_listed(&RelayIds::new(ed_id(4), rsa_id(4))),
        Some(true)
    );
}

/// When only one identity type is supplied, the answer depends on which one:
/// the RSA identity alone is enough to say "listed" or "not listed", because
/// the consensus is indexed by it; an unknown Ed25519 identity alone can only
/// be reported as "not yet knowable", since a microdescriptor might still
/// introduce it.
#[test]
fn netdirids__ids_listed_with_one_identity_type() {
    let dir = netdir();

    let rsa_only = RelayIds::from_relay_ids(&OneId::Rsa(rsa_id(10)));
    assert_eq!(dir.ids_listed(&rsa_only), Some(true));
    let rsa_absent = RelayIds::from_relay_ids(&OneId::Rsa(rsa_id(200)));
    assert_eq!(dir.ids_listed(&rsa_absent), Some(false));

    let ed_only = RelayIds::from_relay_ids(&OneId::Ed(ed_id(10)));
    assert_eq!(dir.ids_listed(&ed_only), Some(true));
    let ed_absent = RelayIds::from_relay_ids(&OneId::Ed(ed_id(200)));
    assert_eq!(
        dir.ids_listed(&ed_absent),
        None,
        "an unknown ed25519 identity alone is not a durable 'no'"
    );
}

/// A target carrying exactly one identity.
enum OneId {
    Ed(Ed25519Identity),
    Rsa(RsaIdentity),
}
impl HasRelayIds for OneId {
    fn identity(&self, t: RelayIdType) -> Option<RelayIdRef<'_>> {
        match (self, t) {
            (OneId::Ed(k), RelayIdType::Ed25519) => Some(k.into()),
            (OneId::Rsa(k), RelayIdType::Rsa) => Some(k.into()),
            _ => None,
        }
    }
}
