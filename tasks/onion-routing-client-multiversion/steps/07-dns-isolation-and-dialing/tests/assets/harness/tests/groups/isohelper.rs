//! Group `isohelper`: user-supplied isolation rules.  A caller defines its own
//! rule by implementing `IsolationHelper`; the blanket implementation turns it
//! into a full `Isolation` object, with cross-type separation and tuple
//! composition supplied by the library.
//!
//! Graded items: `tor_circmgr::isolation::{Isolation, IsolationHelper}` and the
//! tuple implementations of `IsolationHelper`.

use tor_circmgr::isolation::{Isolation, IsolationHelper, IsolationToken};

/// A user-defined rule: streams from the same local user may share a circuit.
#[derive(Clone, Debug, PartialEq, Eq)]
struct UserId(u32);

impl IsolationHelper for UserId {
    fn compatible_same_type(&self, other: &Self) -> bool {
        self == other
    }
    fn join_same_type(&self, other: &Self) -> Option<Self> {
        if self == other {
            Some(self.clone())
        } else {
            None
        }
    }
}

/// A user-defined rule whose `join` is a genuine intersection rather than an
/// equality test: two streams may share a circuit if their permitted port
/// ranges overlap, and the circuit then carries the overlap.
#[derive(Clone, Debug, PartialEq, Eq)]
struct PortRange {
    lo: u16,
    hi: u16,
}

impl PortRange {
    fn new(lo: u16, hi: u16) -> Self {
        PortRange { lo, hi }
    }
}

impl IsolationHelper for PortRange {
    fn compatible_same_type(&self, other: &Self) -> bool {
        self.lo.max(other.lo) <= self.hi.min(other.hi)
    }
    fn join_same_type(&self, other: &Self) -> Option<Self> {
        let lo = self.lo.max(other.lo);
        let hi = self.hi.min(other.hi);
        if lo <= hi {
            Some(PortRange { lo, hi })
        } else {
            None
        }
    }
}

#[test]
fn isohelper__a_user_type_gains_the_isolation_trait() {
    // Implementing `IsolationHelper` is enough: `Isolation` comes for free.
    let a = UserId(7);
    let b = UserId(7);
    let c = UserId(8);
    assert!(a.compatible(&b));
    assert!(!a.compatible(&c));
    assert!(a.join(&b).is_some());
    assert!(a.join(&c).is_none());

    // And the rule can be used wherever a boxed isolation object is wanted.
    let boxed: Box<dyn Isolation> = a.clone().into();
    assert!(boxed.compatible(&b));
    assert!(!boxed.compatible(&c));
}

#[test]
fn isohelper__values_of_different_isolation_types_never_share_a_circuit() {
    let user = UserId(1);
    let ports = PortRange::new(0, u16::MAX);
    let token = IsolationToken::no_isolation();

    // Every cross-type pairing is incompatible in both directions, even though
    // each rule on its own is maximally permissive.
    assert!(!user.compatible(&ports));
    assert!(!ports.compatible(&user));
    assert!(!user.compatible(&token));
    assert!(!token.compatible(&user));
    assert!(!ports.compatible(&token));
    assert!(!token.compatible(&ports));

    assert!(user.join(&ports).is_none());
    assert!(ports.join(&user).is_none());
    assert!(token.join(&ports).is_none());
}

#[test]
fn isohelper__join_of_overlapping_rules_is_their_intersection() {
    let a = PortRange::new(10, 100);
    let b = PortRange::new(50, 200);
    let joined = a.join(&b).expect("overlapping ranges join");

    // The circuit now carries the intersection: it still accepts a stream
    // inside 50..=100 but no longer accepts one that only overlaps 10..=49.
    assert!(joined.compatible(&PortRange::new(60, 70)));
    assert!(!joined.compatible(&PortRange::new(10, 20)));
    assert!(!joined.compatible(&PortRange::new(150, 160)));

    // Joining is commutative and idempotent.
    let joined_rev = b.join(&a).expect("overlapping ranges join");
    assert!(joined.compatible(joined_rev.as_ref()));
    let again = joined.join(joined.as_ref()).expect("join is idempotent");
    assert!(again.compatible(joined.as_ref()));

    // Disjoint ranges do not join at all.
    assert!(PortRange::new(1, 9).join(&PortRange::new(10, 20)).is_none());
}

#[test]
fn isohelper__compatible_agrees_with_join_being_some() {
    let cases = [
        (PortRange::new(0, 0), PortRange::new(0, 0)),
        (PortRange::new(0, 10), PortRange::new(10, 20)),
        (PortRange::new(0, 10), PortRange::new(11, 20)),
        (PortRange::new(100, 200), PortRange::new(0, 5)),
        (PortRange::new(1, u16::MAX), PortRange::new(1, u16::MAX)),
    ];
    for (a, b) in cases.iter() {
        assert_eq!(
            a.compatible(b),
            a.join(b).is_some(),
            "compatible() and join() disagree for {:?} / {:?}",
            a,
            b
        );
        // Symmetry is required of both operations.
        assert_eq!(a.compatible(b), b.compatible(a));
        assert_eq!(a.join(b).is_some(), b.join(a).is_some());
        // Reflexivity.
        assert!(a.compatible(a));
    }
}

#[test]
fn isohelper__tuples_combine_component_rules() {
    // A tuple of rules is itself a rule, and it is compatible only when every
    // component is compatible.
    let a = (UserId(4), PortRange::new(0, 100));
    let b = (UserId(4), PortRange::new(50, 150));
    let c = (UserId(5), PortRange::new(50, 150));

    assert!(a.compatible(&b));
    assert!(!a.compatible(&c));

    let joined = a.join(&b).expect("both components join");
    // The joined isolation carries UserId(4) and the port intersection 50..=100.
    assert!(joined.compatible(&(UserId(4), PortRange::new(60, 80))));
    assert!(!joined.compatible(&(UserId(4), PortRange::new(0, 10))));
    assert!(!joined.compatible(&(UserId(5), PortRange::new(60, 80))));

    // Tuples of different arity are different types, hence never compatible.
    let triple = (UserId(4), PortRange::new(0, 100), IsolationToken::no_isolation());
    assert!(!a.compatible(&triple));
    assert!(triple.compatible(&(
        UserId(4),
        PortRange::new(0, 100),
        IsolationToken::no_isolation()
    )));
}

#[test]
fn isohelper__tuple_join_fails_when_any_component_fails() {
    let base = (UserId(1), PortRange::new(10, 20), IsolationToken::no_isolation());

    // Every component agrees: the join succeeds.
    assert!(base
        .join(&(UserId(1), PortRange::new(15, 25), IsolationToken::no_isolation()))
        .is_some());
    // First component disagrees.
    assert!(base
        .join(&(UserId(2), PortRange::new(15, 25), IsolationToken::no_isolation()))
        .is_none());
    // Second component disagrees.
    assert!(base
        .join(&(UserId(1), PortRange::new(30, 40), IsolationToken::no_isolation()))
        .is_none());
    // Third component disagrees.
    assert!(base
        .join(&(UserId(1), PortRange::new(15, 25), IsolationToken::new()))
        .is_none());
}

#[test]
fn isohelper__boxed_isolation_objects_are_clonable_and_debug() {
    let rule: Box<dyn Isolation> = PortRange::new(10, 20).into();
    let copy = rule.clone();
    // The clone behaves identically to the original.
    assert!(rule.compatible(copy.as_ref()));
    assert!(copy.compatible(&PortRange::new(15, 30)));
    assert!(!copy.compatible(&PortRange::new(21, 30)));
    // `Isolation` requires `Debug`, and the boxed form forwards to the rule.
    assert_eq!(
        format!("{:?}", rule),
        format!("{:?}", PortRange::new(10, 20))
    );
}
