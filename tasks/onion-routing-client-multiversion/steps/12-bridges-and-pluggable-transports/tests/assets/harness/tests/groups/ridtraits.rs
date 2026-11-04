//! Group: ridtraits — the relay-identity traits, re-checked here.
//!
//! `HasRelayIds` is the interface every bridge, guard and channel target is
//! matched through, and almost all of it is default methods built on a single
//! `identity()` lookup.  The types here are first-party, so what is graded is
//! the trait's own behaviour and not any particular arti struct.

use tor_linkspec::{HasRelayIds, RelayId, RelayIdSet, RelayIdType, RelayIdRef};
use tor_llcrypto::pk::ed25519::Ed25519Identity;
use tor_llcrypto::pk::rsa::RsaIdentity;

use crate::common::{ed_id, rsa_id, ED_B64, RSA_HEX};

/// A relay known by both of its identity keys.
struct FullyKnown {
    /// Its Ed25519 identity.
    ed: Ed25519Identity,
    /// Its RSA identity.
    rsa: RsaIdentity,
}

// Implementing the legacy two-key trait is enough to get the general one.
impl tor_linkspec::HasRelayIdsLegacy for FullyKnown {
    fn ed_identity(&self) -> &Ed25519Identity {
        &self.ed
    }
    fn rsa_identity(&self) -> &RsaIdentity {
        &self.rsa
    }
}

/// A relay known only by its RSA identity, as a bridge line often is.
struct RsaOnly {
    /// Its RSA identity.
    rsa: RsaIdentity,
}

impl HasRelayIds for RsaOnly {
    fn identity(&self, key_type: RelayIdType) -> Option<RelayIdRef<'_>> {
        match key_type {
            RelayIdType::Rsa => Some((&self.rsa).into()),
            _ => None,
        }
    }
}

/// The relay both fixtures describe.
fn full() -> FullyKnown {
    FullyKnown {
        ed: ed_id(),
        rsa: rsa_id(),
    }
}

/// A different Ed25519 key.
fn other_ed() -> Ed25519Identity {
    Ed25519Identity::from([0xAB; 32])
}

#[test]
fn ridtraits__the_legacy_two_key_trait_supplies_the_general_lookup() {
    let r = full();
    assert_eq!(
        r.identity(RelayIdType::Ed25519).unwrap().to_string(),
        format!("ed25519:{}", ED_B64)
    );
    assert_eq!(
        r.identity(RelayIdType::Rsa).unwrap().to_string(),
        format!("${}", RSA_HEX)
    );
}

#[test]
fn ridtraits__identities_are_listed_ed25519_first() {
    let r = full();
    let listed: Vec<String> = r.identities().map(|id| id.to_string()).collect();
    assert_eq!(
        listed,
        vec![format!("ed25519:{}", ED_B64), format!("${}", RSA_HEX)]
    );

    // A relay that is missing a key simply does not list it.
    let partial = RsaOnly { rsa: rsa_id() };
    let listed: Vec<String> = partial.identities().map(|id| id.to_string()).collect();
    assert_eq!(listed, vec![format!("${}", RSA_HEX)]);
}

#[test]
fn ridtraits__has_identity_matches_only_within_a_type() {
    let r = full();
    assert!(r.has_identity((&rsa_id()).into()));
    assert!(r.has_identity((&ed_id()).into()));
    assert!(!r.has_identity((&other_ed()).into()));

    let partial = RsaOnly { rsa: rsa_id() };
    assert!(partial.has_identity((&rsa_id()).into()));
    assert!(
        !partial.has_identity((&ed_id()).into()),
        "an unknown identity is not a match, even though nothing contradicts it"
    );
}

#[test]
fn ridtraits__same_relay_ids_compares_every_type_including_the_absent_ones() {
    let a = full();
    let b = full();
    assert!(a.same_relay_ids(&b));

    let different = FullyKnown {
        ed: other_ed(),
        rsa: rsa_id(),
    };
    assert!(!a.same_relay_ids(&different));

    let partial = RsaOnly { rsa: rsa_id() };
    assert!(
        !a.same_relay_ids(&partial),
        "knowing one more key is not the same set of keys"
    );
}

#[test]
fn ridtraits__having_all_of_another_relays_ids_is_a_one_way_test() {
    let a = full();
    let partial = RsaOnly { rsa: rsa_id() };

    assert!(
        a.has_all_relay_ids_from(&partial),
        "the fuller record satisfies everything the sparser one asks"
    );
    assert!(
        !partial.has_all_relay_ids_from(&a),
        "the sparser record cannot vouch for a key it does not have"
    );

    let conflicting = RsaOnly {
        rsa: RsaIdentity::from([0x11; 20]),
    };
    assert!(!a.has_all_relay_ids_from(&conflicting));
}

#[test]
fn ridtraits__a_relay_id_set_keeps_one_slot_per_key_type() {
    let mut set = RelayIdSet::new();
    assert!(set.is_empty());

    assert!(set.insert(rsa_id()), "a fresh key is newly inserted");
    assert!(!set.insert(rsa_id()), "the same key again is not new");
    assert!(set.insert(ed_id()));
    assert_eq!(set.len(), 2);

    assert!(set.contains(&rsa_id()));
    assert!(set.contains(&ed_id()));
    assert!(!set.contains(&other_ed()));

    assert!(set.remove(&ed_id()));
    assert!(!set.remove(&ed_id()));
    assert_eq!(set.len(), 1);
    assert!(!set.is_empty());

    // The same set can be collected straight out of a relay's identities.
    let set: RelayIdSet = full().identities().map(|id| id.to_owned()).collect();
    assert_eq!(set.len(), 2);
    assert!(set.contains(&ed_id()));
    assert!(set.contains(&rsa_id()));

    // Collecting the same keys twice does not grow the set.
    let doubled: RelayIdSet = vec![
        RelayId::from(rsa_id()),
        RelayId::from(rsa_id()),
        RelayId::from(ed_id()),
    ]
    .into_iter()
    .collect();
    assert_eq!(doubled.len(), 2);
    assert_eq!(doubled, set);
}
