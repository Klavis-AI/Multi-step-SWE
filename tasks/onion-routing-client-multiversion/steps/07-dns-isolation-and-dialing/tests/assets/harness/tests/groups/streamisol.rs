//! Group `streamisol`: `StreamIsolation`, the composite rule the circuit
//! manager actually applies.  It pairs the caller's own isolation rule with
//! the "owner" token of the client that created the stream, so that an
//! isolated client never shares a circuit with its parent even when both use
//! the same stream rule.
//!
//! Graded items: `tor_circmgr::isolation::{StreamIsolation,
//! StreamIsolationBuilder}`.

use tor_circmgr::isolation::{Isolation, IsolationHelper, IsolationToken, StreamIsolation};

/// A user-defined stream rule.
#[derive(Clone, Debug, PartialEq, Eq)]
struct Tag(&'static str);

impl IsolationHelper for Tag {
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

/// Build a `StreamIsolation` with both fields set explicitly.
fn iso(owner: IsolationToken, stream: Box<dyn Isolation>) -> StreamIsolation {
    let mut b = StreamIsolation::builder();
    b.owner_token(owner);
    b.stream_isolation(stream);
    b.build().expect("StreamIsolation builds from both fields")
}

#[test]
fn streamisol__default_isolation_is_compatible_with_itself() {
    let a = StreamIsolation::no_isolation();
    let b = StreamIsolation::no_isolation();
    assert!(a.compatible(&b));
    assert!(b.compatible(&a));
    assert!(a.join(&b).is_some());

    // The builder with nothing set gives the same thing.
    let built = StreamIsolation::builder()
        .build()
        .expect("an empty builder yields the unisolated default");
    assert!(built.compatible(&a));

    // ... which means "no owner token and no stream rule".
    let explicit = iso(
        IsolationToken::no_isolation(),
        Box::new(IsolationToken::no_isolation()),
    );
    assert!(built.compatible(&explicit));
}

#[test]
fn streamisol__differing_owner_tokens_never_share_a_circuit() {
    let owner_a = IsolationToken::new();
    let owner_b = IsolationToken::new();
    // Identical stream rules, different owners.
    let a = iso(owner_a, Box::new(Tag("same")));
    let b = iso(owner_b, Box::new(Tag("same")));
    assert!(!a.compatible(&b));
    assert!(!b.compatible(&a));
    assert!(a.join(&b).is_none());

    // Same owner, same rule: compatible.
    let c = iso(owner_a, Box::new(Tag("same")));
    assert!(a.compatible(&c));
}

#[test]
fn streamisol__differing_stream_rules_never_share_a_circuit() {
    let owner = IsolationToken::new();
    let a = iso(owner, Box::new(Tag("alpha")));
    let b = iso(owner, Box::new(Tag("beta")));
    assert!(!a.compatible(&b));
    assert!(a.join(&b).is_none());

    // A rule of a different type is also incompatible, even under the same
    // owner token.
    let c = iso(owner, Box::new(IsolationToken::no_isolation()));
    assert!(!a.compatible(&c));
    assert!(a.join(&c).is_none());
}

#[test]
fn streamisol__matching_owner_and_stream_rules_join() {
    let owner = IsolationToken::new();
    let a = iso(owner, Box::new(Tag("shared")));
    let b = iso(owner, Box::new(Tag("shared")));
    let joined = a.join(&b).expect("identical isolation joins");

    // The circuit's new isolation still accepts a third identical stream ...
    assert!(joined.compatible(&iso(owner, Box::new(Tag("shared")))));
    // ... but not one with a different owner or a different rule.
    assert!(!joined.compatible(&iso(IsolationToken::new(), Box::new(Tag("shared")))));
    assert!(!joined.compatible(&iso(owner, Box::new(Tag("other")))));
}

#[test]
fn streamisol__a_user_rule_can_be_carried_as_the_stream_isolation() {
    // The stream half accepts any user-supplied rule, not just a token.
    let owner = IsolationToken::new();
    let tag = iso(owner, Box::new(Tag("dns")));
    let tok = IsolationToken::new();
    let by_token = iso(owner, Box::new(tok));

    assert!(tag.compatible(&iso(owner, Box::new(Tag("dns")))));
    assert!(by_token.compatible(&iso(owner, Box::new(tok))));
    assert!(!tag.compatible(&by_token));

    // A tuple rule works too, so several independent rules can be combined.
    let pair = iso(owner, Box::new((Tag("dns"), tok)));
    assert!(pair.compatible(&iso(owner, Box::new((Tag("dns"), tok)))));
    assert!(!pair.compatible(&iso(owner, Box::new((Tag("dns"), IsolationToken::new())))));
    assert!(!pair.compatible(&tag));
}

#[test]
fn streamisol__joining_is_commutative_and_idempotent() {
    let owner = IsolationToken::new();
    let a = iso(owner, Box::new(Tag("x")));
    let b = iso(owner, Box::new(Tag("x")));

    let ab = a.join(&b).expect("join succeeds");
    let ba = b.join(&a).expect("join is commutative");
    assert!(ab.compatible(ba.as_ref()));
    assert!(ba.compatible(ab.as_ref()));

    let aa = a.join(&a).expect("join is idempotent");
    assert!(aa.compatible(&a));
    assert!(a.compatible(aa.as_ref()));

    // `compatible` and `join(..).is_some()` agree on every pairing.
    let c = iso(IsolationToken::new(), Box::new(Tag("x")));
    for (x, y) in [(&a, &b), (&a, &c), (&b, &c), (&a, &a)] {
        assert_eq!(x.compatible(y), x.join(y).is_some());
    }
}
