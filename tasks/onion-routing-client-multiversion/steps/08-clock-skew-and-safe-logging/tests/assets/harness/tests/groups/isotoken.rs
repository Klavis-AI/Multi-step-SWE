//! Group `isotoken` (inherited from step 7): isolation tokens keep unrelated
//! streams off the same circuit, and still do on the cumulative tree.
//!
//! Traces to instruction.md section 1: "Preserve user-supplied isolation ...
//! from step 7."  Interface: `tor_circmgr::isolation::{Isolation,
//! IsolationHelper, IsolationToken}`.

use tor_circmgr::isolation::{Isolation, IsolationHelper, IsolationToken, StreamIsolation};

#[test]
fn isotoken__freshly_made_tokens_are_all_different() {
    let a = IsolationToken::new();
    let b = IsolationToken::new();
    let c = IsolationToken::new();
    assert_ne!(a, b);
    assert_ne!(b, c);
    assert_ne!(a, c);
    assert_eq!(a, a);
}

#[test]
fn isotoken__the_no_isolation_token_is_a_single_shared_value() {
    assert_eq!(IsolationToken::no_isolation(), IsolationToken::no_isolation());
}

#[test]
fn isotoken__a_fresh_token_is_never_the_no_isolation_token() {
    for _ in 0..4 {
        assert_ne!(IsolationToken::new(), IsolationToken::no_isolation());
    }
}

#[test]
fn isotoken__compatibility_is_exactly_equality() {
    let a = IsolationToken::new();
    let b = IsolationToken::new();
    assert!(a.compatible_same_type(&a));
    assert!(!a.compatible_same_type(&b));
    assert!(!b.compatible_same_type(&a));
    assert!(IsolationToken::no_isolation().compatible_same_type(&IsolationToken::no_isolation()));
    assert!(!a.compatible_same_type(&IsolationToken::no_isolation()));
}

#[test]
fn isotoken__joining_keeps_the_token_or_fails() {
    let a = IsolationToken::new();
    let b = IsolationToken::new();
    assert_eq!(a.join_same_type(&a), Some(a));
    assert_eq!(a.join_same_type(&b), None);
    assert_eq!(b.join_same_type(&a), None);
    let none = IsolationToken::no_isolation();
    assert_eq!(none.join_same_type(&none), Some(none));
}

#[test]
fn isotoken__isolation_of_different_types_is_never_compatible() {
    let token: Box<dyn Isolation> = Box::new(IsolationToken::no_isolation());
    let stream: Box<dyn Isolation> = Box::new(StreamIsolation::no_isolation());

    assert!(!token.compatible(stream.as_ref()));
    assert!(!stream.compatible(token.as_ref()));
    assert!(token.join(stream.as_ref()).is_none());
    assert!(stream.join(token.as_ref()).is_none());
    // Each is still compatible with itself.
    assert!(token.compatible(token.as_ref()));
    assert!(stream.compatible(stream.as_ref()));
}
