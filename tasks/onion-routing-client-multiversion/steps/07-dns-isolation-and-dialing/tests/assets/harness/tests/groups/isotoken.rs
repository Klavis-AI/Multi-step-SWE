//! Group `isotoken`: the isolation token, the simplest user-supplied
//! isolation rule.
//!
//! Graded items: `tor_circmgr::isolation::{Isolation, IsolationToken}`.

use tor_circmgr::isolation::{Isolation, IsolationToken, StreamIsolation};

#[test]
fn isotoken__new_tokens_are_all_distinct() {
    let tokens: Vec<IsolationToken> = (0..64).map(|_| IsolationToken::new()).collect();
    for (i, a) in tokens.iter().enumerate() {
        assert_ne!(*a, IsolationToken::no_isolation());
        for b in tokens.iter().skip(i + 1) {
            assert_ne!(a, b, "IsolationToken::new() handed out a duplicate");
        }
    }
}

#[test]
fn isotoken__no_isolation_tokens_are_equal_to_each_other_and_to_nothing_else() {
    assert_eq!(IsolationToken::no_isolation(), IsolationToken::no_isolation());
    let fresh = IsolationToken::new();
    assert_ne!(fresh, IsolationToken::no_isolation());

    // ... and the same holds through the `Isolation` trait.
    assert!(IsolationToken::no_isolation().compatible(&IsolationToken::no_isolation()));
    assert!(!fresh.compatible(&IsolationToken::no_isolation()));
    assert!(!IsolationToken::no_isolation().compatible(&fresh));
}

#[test]
fn isotoken__tokens_are_compatible_only_with_themselves() {
    let a = IsolationToken::new();
    let b = IsolationToken::new();
    // Reflexive.
    assert!(a.compatible(&a));
    assert!(b.compatible(&b));
    // Symmetric, and false for distinct tokens.
    assert!(!a.compatible(&b));
    assert!(!b.compatible(&a));
    // A value of a different isolation type is never compatible.
    let other_type = StreamIsolation::no_isolation();
    assert!(!a.compatible(&other_type));
    assert!(!other_type.compatible(&a));
}

#[test]
fn isotoken__join_returns_the_token_when_compatible_and_none_otherwise() {
    let a = IsolationToken::new();
    let b = IsolationToken::new();

    let joined = a.join(&a).expect("a token must join with itself");
    assert!(joined.compatible(&a));
    assert!(!joined.compatible(&b));

    assert!(a.join(&b).is_none());
    assert!(b.join(&a).is_none());
    assert!(a.join(&StreamIsolation::no_isolation()).is_none());

    let none = IsolationToken::no_isolation();
    let joined = none
        .join(&IsolationToken::no_isolation())
        .expect("no_isolation joins with no_isolation");
    assert!(joined.compatible(&IsolationToken::no_isolation()));

    // `compatible` must agree with `join(..).is_some()` in every case.
    for (x, y) in [(a, a), (a, b), (b, a), (none, none), (a, none)] {
        assert_eq!(x.compatible(&y), x.join(&y).is_some());
    }
}

#[test]
fn isotoken__tokens_are_copyable_and_totally_ordered() {
    let a = IsolationToken::new();
    let b = IsolationToken::new();
    // Copy, not move: `a` is still usable after being passed by value.
    let copy = a;
    assert_eq!(copy, a);
    // Tokens are handed out in increasing order, so they sort stably.
    assert!(a < b, "tokens are ordered by allocation");
    let mut v = vec![b, a, copy];
    v.sort();
    assert_eq!(v, vec![a, copy, b]);
    assert_eq!(
        IsolationToken::no_isolation().cmp(&IsolationToken::no_isolation()),
        std::cmp::Ordering::Equal
    );
}

#[test]
fn isotoken__a_token_converts_into_a_boxed_isolation() {
    let tok = IsolationToken::new();
    let boxed: Box<dyn Isolation> = tok.into();
    assert!(boxed.compatible(&tok));
    assert!(!boxed.compatible(&IsolationToken::no_isolation()));

    // Boxed isolation objects are clonable (trait objects included).
    let cloned = boxed.clone();
    assert!(cloned.compatible(boxed.as_ref()));
    assert!(boxed.compatible(cloned.as_ref()));

    // And the boxed form joins exactly like the token did.
    let joined = boxed
        .join(cloned.as_ref())
        .expect("identical isolation joins");
    assert!(joined.compatible(&tok));
}
