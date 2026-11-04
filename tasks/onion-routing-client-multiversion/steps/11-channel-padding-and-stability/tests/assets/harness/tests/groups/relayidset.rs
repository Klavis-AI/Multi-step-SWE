//! Group `relayidset`: `tor_linkspec::RelayIdSet`, the set-of-identities type
//! introduced alongside `RelayId`.
//!
//! This is the type a caller uses to answer "have I already seen this relay?"
//! when the relay may be named by either of its identities. The subtle part is
//! that it is *not* one flat hash set: each identity type occupies its own
//! slot, so membership of an Ed25519 key says nothing about the RSA key, and
//! the insert/remove return values (which callers use to detect duplicates)
//! have to report per-key novelty rather than per-relay novelty. These cases
//! grade that behaviour plus the serialized form, which is a plain list.
#![allow(non_snake_case)]

use tor_linkspec::{RelayId, RelayIdSet};

use crate::common::{ed_id, rsa_id};

/// A fresh set holds nothing, and `is_empty`/`len`/`iter`/`contains` all agree
/// about that. `Default` and `new` must produce the same thing.
#[test]
fn relayidset__new_set_is_empty() {
    let set = RelayIdSet::new();
    assert!(set.is_empty());
    assert_eq!(set.len(), 0);
    assert_eq!(set.iter().count(), 0);
    assert!(!set.contains(&ed_id(1)));
    assert!(!set.contains(&rsa_id(1)));

    assert_eq!(set, RelayIdSet::default());
}

/// `insert` returns whether the key was *new*. A caller that treats the return
/// value as "success" would count every re-announcement of a known relay as a
/// new one; a caller that ignores it cannot dedup at all.
#[test]
fn relayidset__insert_reports_novelty() {
    let mut set = RelayIdSet::new();

    assert!(set.insert(ed_id(1)), "first insert is new");
    assert_eq!(set.len(), 1);
    assert!(!set.insert(ed_id(1)), "second insert is not new");
    assert_eq!(set.len(), 1, "a duplicate must not grow the set");
    assert!(!set.is_empty());

    // Inserting the same key wrapped as a `RelayId` is still the same key.
    assert!(!set.insert(RelayId::from(ed_id(1))));
    assert_eq!(set.len(), 1);
}

/// The two identity types are indexed independently: knowing a relay's Ed25519
/// key does not make its RSA key a member. Collapsing them into one namespace
/// would make `contains` answer the wrong question.
#[test]
fn relayidset__identity_types_are_independent() {
    let mut set = RelayIdSet::new();
    assert!(set.insert(ed_id(7)));

    assert!(set.contains(&ed_id(7)));
    assert!(!set.contains(&rsa_id(7)), "an RSA key is a different member");

    assert!(set.insert(rsa_id(7)), "the RSA key is new even so");
    assert_eq!(set.len(), 2);
    assert!(set.contains(&rsa_id(7)));
    assert!(!set.contains(&ed_id(8)));
}

/// Membership can be tested with a bare key, an owned `RelayId`, or a borrowed
/// one; all three name the same member.
#[test]
fn relayidset__contains_accepts_every_id_form() {
    let mut set = RelayIdSet::new();
    set.insert(ed_id(3));
    set.insert(rsa_id(4));

    let owned_ed = RelayId::from(ed_id(3));
    assert!(set.contains(&ed_id(3)));
    assert!(set.contains(&owned_ed));
    assert!(set.contains(owned_ed.as_ref()));

    let absent = RelayId::from(rsa_id(9));
    assert!(!set.contains(&absent));
    assert!(!set.contains(absent.as_ref()));
}

/// `remove` returns whether the key was present, and removing the same key
/// twice is not an error -- it just reports `false` the second time.
#[test]
fn relayidset__remove_reports_presence() {
    let mut set = RelayIdSet::new();
    set.insert(ed_id(1));
    set.insert(rsa_id(2));
    assert_eq!(set.len(), 2);

    assert!(set.remove(&ed_id(1)));
    assert_eq!(set.len(), 1);
    assert!(!set.contains(&ed_id(1)));
    assert!(!set.remove(&ed_id(1)), "removing it again is a no-op");
    assert_eq!(set.len(), 1);

    // The other type is untouched by the removal.
    assert!(set.contains(&rsa_id(2)));
    assert!(set.remove(&rsa_id(2)));
    assert!(set.is_empty());
}

/// Iteration visits exactly the members, once each, across both identity types.
/// (The order is explicitly undefined, so this compares sorted renderings.)
#[test]
fn relayidset__iter_yields_every_member_once() {
    let mut set = RelayIdSet::new();
    set.insert(ed_id(1));
    set.insert(ed_id(2));
    set.insert(rsa_id(3));
    set.insert(ed_id(1)); // duplicate

    assert_eq!(set.len(), 3);
    assert_eq!(set.iter().count(), 3);

    let mut got: Vec<String> = set.iter().map(|r| r.to_string()).collect();
    got.sort();
    let mut want: Vec<String> = vec![
        RelayId::from(ed_id(1)).to_string(),
        RelayId::from(ed_id(2)).to_string(),
        RelayId::from(rsa_id(3)).to_string(),
    ];
    want.sort();
    assert_eq!(got, want);

    // The borrowed items convert back to the owned members.
    assert!(set.iter().any(|r| r.to_owned() == RelayId::from(rsa_id(3))));
}

/// Collecting an iterator of identities into a set deduplicates, and `extend`
/// adds to an existing set with the same rules. Both are how a caller turns a
/// relay's advertised identity list into a lookup structure.
#[test]
fn relayidset__collect_and_extend_deduplicate() {
    let ids = vec![
        RelayId::from(ed_id(1)),
        RelayId::from(rsa_id(2)),
        RelayId::from(ed_id(1)),
    ];
    let set: RelayIdSet = ids.into_iter().collect();
    assert_eq!(set.len(), 2);
    assert!(set.contains(&ed_id(1)));
    assert!(set.contains(&rsa_id(2)));

    let mut set2 = set.clone();
    set2.extend(vec![RelayId::from(ed_id(1)), RelayId::from(ed_id(5))]);
    assert_eq!(set2.len(), 3, "only the genuinely new key was added");
    assert_eq!(set, set.clone());
    assert_ne!(set, set2);

    // `extend` also accepts bare key types.
    let mut set3 = RelayIdSet::new();
    set3.extend(vec![ed_id(1), ed_id(1), ed_id(2)]);
    assert_eq!(set3.len(), 2);
}

/// The serialized form is a flat list of the canonical identity strings, so a
/// config file can name a relay by either identity, and a list containing a
/// repeat still deserializes to a set.
#[test]
fn relayidset__serde_round_trips_as_a_list() {
    let mut set = RelayIdSet::new();
    set.insert(ed_id(1));
    set.insert(rsa_id(2));

    let json = serde_json::to_string(&set).unwrap();
    let as_values: Vec<String> = serde_json::from_str(&json).unwrap();
    let mut sorted = as_values.clone();
    sorted.sort();
    let mut want = vec![
        RelayId::from(ed_id(1)).to_string(),
        RelayId::from(rsa_id(2)).to_string(),
    ];
    want.sort();
    assert_eq!(sorted, want);

    let back: RelayIdSet = serde_json::from_str(&json).unwrap();
    assert_eq!(back, set);

    // Duplicates in the input collapse; a malformed identity is rejected.
    let dup = format!("[{0},{0}]", serde_json::to_string(&as_values[0]).unwrap());
    let from_dup: RelayIdSet = serde_json::from_str(&dup).unwrap();
    assert_eq!(from_dup.len(), 1);
    assert!(serde_json::from_str::<RelayIdSet>("[\"bogus\"]").is_err());
}
