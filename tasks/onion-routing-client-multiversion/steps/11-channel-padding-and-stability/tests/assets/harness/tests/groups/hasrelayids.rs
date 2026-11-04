//! Group `hasrelayids`: the `tor_linkspec::HasRelayIds` /
//! `HasRelayIdsLegacy` trait pair.
//!
//! This is the abstraction that lets the rest of Arti stop assuming every
//! description of a relay carries exactly one Ed25519 key and one RSA key. The
//! interesting content is entirely in the provided methods: an implementor
//! supplies only `identity()`, and everything else -- iteration order, the
//! optional typed accessors, and the two *different* notions of "are these the
//! same relay" -- is derived from it. Those two comparison methods are a
//! security boundary: `same_relay_ids` is an equality check, while
//! `has_all_relay_ids_from` is the deliberately asymmetric "does this concrete
//! relay satisfy this (possibly partial) request" check. Confusing them lets a
//! relay that matches only some of the requested identities be accepted.
#![allow(non_snake_case)]

use tor_linkspec::{HasRelayIds, HasRelayIdsLegacy, RelayId, RelayIdRef, RelayIdType};
use tor_llcrypto::pk::{ed25519::Ed25519Identity, rsa::RsaIdentity};

use crate::common::{ed_id, rsa_id};

/// A relay description in the pre-milestone style: both identities, always.
struct Legacy {
    ed: Ed25519Identity,
    rsa: RsaIdentity,
}
impl HasRelayIdsLegacy for Legacy {
    fn ed_identity(&self) -> &Ed25519Identity {
        &self.ed
    }
    fn rsa_identity(&self) -> &RsaIdentity {
        &self.rsa
    }
}

/// A relay description that may be missing either identity.
struct Partial {
    ed: Option<Ed25519Identity>,
    rsa: Option<RsaIdentity>,
}
impl HasRelayIds for Partial {
    fn identity(&self, key_type: RelayIdType) -> Option<RelayIdRef<'_>> {
        match key_type {
            RelayIdType::Ed25519 => self.ed.as_ref().map(|k| k.into()),
            RelayIdType::Rsa => self.rsa.as_ref().map(|k| k.into()),
            _ => None,
        }
    }
}

/// Build a `Partial` with both identities present.
fn both(ed: u8, rsa: u8) -> Partial {
    Partial {
        ed: Some(ed_id(ed)),
        rsa: Some(rsa_id(rsa)),
    }
}

/// Implementing only the legacy two-key trait must be enough to get the whole
/// `HasRelayIds` interface, and the blanket implementation must report both
/// identities as present.
#[test]
fn hasrelayids__legacy_impl_supplies_identity() {
    let l = Legacy {
        ed: ed_id(1),
        rsa: rsa_id(2),
    };

    assert_eq!(
        l.identity(RelayIdType::Ed25519).map(|r| r.to_owned()),
        Some(RelayId::from(ed_id(1)))
    );
    assert_eq!(
        l.identity(RelayIdType::Rsa).map(|r| r.to_owned()),
        Some(RelayId::from(rsa_id(2)))
    );
    // Every recognized type is present for a legacy object.
    assert_eq!(l.identities().count(), RelayIdType::COUNT);
}

/// Iteration follows `RelayIdType::all_types()`, so the modern Ed25519 identity
/// comes first. Callers encode a relay's identities into link specifiers in
/// exactly this order, and the wire format requires it.
#[test]
fn hasrelayids__identities_iterate_ed25519_first() {
    let l = Legacy {
        ed: ed_id(3),
        rsa: rsa_id(4),
    };

    let got: Vec<RelayId> = l.identities().map(|r| r.to_owned()).collect();
    assert_eq!(
        got,
        vec![RelayId::from(ed_id(3)), RelayId::from(rsa_id(4))]
    );
    assert_eq!(got[0].id_type(), RelayIdType::Ed25519);
    assert_eq!(got[1].id_type(), RelayIdType::Rsa);
}

/// An object that lacks an identity type simply does not yield it; the iterator
/// skips the gap instead of yielding a placeholder, and it is fused, so it
/// keeps returning `None` once exhausted.
#[test]
fn hasrelayids__identities_skip_absent_types() {
    let rsa_only = Partial {
        ed: None,
        rsa: Some(rsa_id(5)),
    };
    let got: Vec<RelayId> = rsa_only.identities().map(|r| r.to_owned()).collect();
    assert_eq!(got, vec![RelayId::from(rsa_id(5))]);

    let ed_only = Partial {
        ed: Some(ed_id(6)),
        rsa: None,
    };
    assert_eq!(ed_only.identities().count(), 1);
    assert_eq!(
        ed_only.identities().next().map(|r| r.id_type()),
        Some(RelayIdType::Ed25519)
    );

    let mut iter = rsa_only.identities();
    assert!(iter.next().is_some());
    assert!(iter.next().is_none());
    assert!(iter.next().is_none(), "the iterator must be fused");
}

/// The typed accessors are `Option`-returning on `HasRelayIds`: "the relay has
/// no key of this type" is representable, unlike in the legacy trait where a
/// reference always had to be produced.
#[test]
fn hasrelayids__typed_accessors_return_options() {
    let full = both(7, 8);
    assert_eq!(full.ed_identity(), Some(&ed_id(7)));
    assert_eq!(full.rsa_identity(), Some(&rsa_id(8)));

    let none = Partial {
        ed: None,
        rsa: None,
    };
    assert_eq!(none.ed_identity(), None);
    assert_eq!(none.rsa_identity(), None);

    let ed_only = Partial {
        ed: Some(ed_id(9)),
        rsa: None,
    };
    assert_eq!(ed_only.ed_identity(), Some(&ed_id(9)));
    assert_eq!(ed_only.rsa_identity(), None);
}

/// `has_identity` answers about one specific key. A match on a different type,
/// or a different key of the same type, is not a match -- otherwise an attacker
/// could present a relay whose RSA identity happens to equal the requested
/// Ed25519 bytes.
#[test]
fn hasrelayids__has_identity_is_type_aware() {
    let full = both(1, 2);
    let want_ed = ed_id(1);
    let want_rsa = rsa_id(2);
    let other_ed = ed_id(0xff);

    assert!(full.has_identity(RelayIdRef::from(&want_ed)));
    assert!(full.has_identity(RelayIdRef::from(&want_rsa)));
    assert!(!full.has_identity(RelayIdRef::from(&other_ed)));

    // Absent type: "false" here means "not known to have it".
    let rsa_only = Partial {
        ed: None,
        rsa: Some(rsa_id(2)),
    };
    assert!(!rsa_only.has_identity(RelayIdRef::from(&want_ed)));
    assert!(rsa_only.has_identity(RelayIdRef::from(&want_rsa)));
}

/// `same_relay_ids` is a strict equality over *every* identity type, including
/// the absent ones: an object that is missing an identity is not the same as
/// one that has it, even if the identities they share agree.
#[test]
fn hasrelayids__same_relay_ids_compares_every_type() {
    let a = both(1, 2);
    let b = both(1, 2);
    assert!(a.same_relay_ids(&b));
    assert!(b.same_relay_ids(&a));

    let different_ed = both(3, 2);
    assert!(!a.same_relay_ids(&different_ed));

    let missing_rsa = Partial {
        ed: Some(ed_id(1)),
        rsa: None,
    };
    assert!(!a.same_relay_ids(&missing_rsa));
    assert!(!missing_rsa.same_relay_ids(&a));

    // Two objects missing the same identity are still "the same ids".
    let missing_rsa2 = Partial {
        ed: Some(ed_id(1)),
        rsa: None,
    };
    assert!(missing_rsa.same_relay_ids(&missing_rsa2));

    // It works across implementing types too.
    let legacy = Legacy {
        ed: ed_id(1),
        rsa: rsa_id(2),
    };
    assert!(a.same_relay_ids(&legacy));
}

/// `has_all_relay_ids_from` asks a one-directional question: does `self` carry
/// every identity that `other` demands? A concrete relay satisfies a partial
/// request, but a partial description does not satisfy a full one.
#[test]
fn hasrelayids__has_all_relay_ids_from_is_asymmetric() {
    let full = both(1, 2);
    let request_ed_only = Partial {
        ed: Some(ed_id(1)),
        rsa: None,
    };

    assert!(full.has_all_relay_ids_from(&request_ed_only));
    assert!(
        !request_ed_only.has_all_relay_ids_from(&full),
        "a partial description does not satisfy a full request"
    );

    // Disagreement on a shared identity fails in both directions.
    let wrong_ed = Partial {
        ed: Some(ed_id(0xaa)),
        rsa: None,
    };
    assert!(!full.has_all_relay_ids_from(&wrong_ed));
    assert!(!wrong_ed.has_all_relay_ids_from(&full));

    // Equality implies the relation in both directions.
    let same = both(1, 2);
    assert!(full.has_all_relay_ids_from(&same));
    assert!(same.has_all_relay_ids_from(&full));
}

/// The degenerate "no identities at all" object: it demands nothing of anyone,
/// and nothing but another empty object is the same as it.
#[test]
fn hasrelayids__empty_object_boundary() {
    let empty = Partial {
        ed: None,
        rsa: None,
    };
    let full = both(1, 2);

    assert_eq!(empty.identities().count(), 0);
    assert_eq!(empty.identity(RelayIdType::Ed25519), None);
    assert_eq!(empty.identity(RelayIdType::Rsa), None);

    // An empty request is satisfied by anything, including itself.
    assert!(full.has_all_relay_ids_from(&empty));
    assert!(empty.has_all_relay_ids_from(&empty));
    // ...but an empty object satisfies no non-empty request.
    assert!(!empty.has_all_relay_ids_from(&full));

    assert!(!empty.same_relay_ids(&full));
    assert!(empty.same_relay_ids(&Partial {
        ed: None,
        rsa: None
    }));
}
