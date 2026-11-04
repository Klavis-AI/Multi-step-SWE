//! Group `isostream` (inherited from step 7): a stream's isolation combines the
//! caller-supplied token with the token owned by the client it came from.
//!
//! Traces to instruction.md section 1: "Preserve user-supplied isolation ...
//! from step 7."  Interface:
//! `tor_circmgr::isolation::{StreamIsolation, StreamIsolationBuilder}`.

use tor_circmgr::isolation::{
    Isolation, IsolationHelper, IsolationToken, StreamIsolation, StreamIsolationBuilder,
};

/// Build a `StreamIsolation` from an owner token and a stream token.
fn iso(owner: IsolationToken, stream: IsolationToken) -> StreamIsolation {
    StreamIsolationBuilder::new()
        .owner_token(owner)
        .stream_isolation(Box::new(stream))
        .build()
        .expect("StreamIsolation should build")
}

#[test]
fn isostream__the_default_build_is_the_unisolated_value() {
    let built = StreamIsolationBuilder::new()
        .build()
        .expect("StreamIsolation should build");
    let none = StreamIsolation::no_isolation();
    assert!(built.compatible_same_type(&none));
    assert!(none.compatible_same_type(&built));
    // It is exactly "no isolation on either axis".
    let explicit = iso(
        IsolationToken::no_isolation(),
        IsolationToken::no_isolation(),
    );
    assert!(built.compatible_same_type(&explicit));
}

#[test]
fn isostream__matching_owner_and_stream_tokens_are_compatible() {
    let owner = IsolationToken::new();
    let stream = IsolationToken::new();
    assert!(iso(owner, stream).compatible_same_type(&iso(owner, stream)));
}

#[test]
fn isostream__a_different_owner_token_isolates() {
    let stream = IsolationToken::new();
    let a = iso(IsolationToken::new(), stream);
    let b = iso(IsolationToken::new(), stream);
    assert!(!a.compatible_same_type(&b));
    assert!(!b.compatible_same_type(&a));
    assert!(a.join_same_type(&b).is_none());
}

#[test]
fn isostream__a_different_stream_token_isolates() {
    let owner = IsolationToken::new();
    let a = iso(owner, IsolationToken::new());
    let b = iso(owner, IsolationToken::new());
    assert!(!a.compatible_same_type(&b));
    assert!(!b.compatible_same_type(&a));
    assert!(a.join_same_type(&b).is_none());
}

#[test]
fn isostream__joining_preserves_both_tokens() {
    let owner = IsolationToken::new();
    let stream = IsolationToken::new();
    let joined = iso(owner, stream)
        .join_same_type(&iso(owner, stream))
        .expect("identical isolations must join");

    assert!(joined.compatible_same_type(&iso(owner, stream)));
    // The joined value is still isolated from anything with another owner or
    // another stream token.
    assert!(!joined.compatible_same_type(&iso(IsolationToken::new(), stream)));
    assert!(!joined.compatible_same_type(&iso(owner, IsolationToken::new())));
}

#[test]
fn isostream__the_default_stream_isolation_shares_circuits() {
    let owner = IsolationToken::new();
    let a = iso(owner, IsolationToken::no_isolation());
    let b = iso(owner, IsolationToken::no_isolation());
    assert!(a.compatible_same_type(&b));

    // Compatibility through the object-safe trait agrees with the typed form.
    let a_dyn: Box<dyn Isolation> = Box::new(a);
    let b_dyn: Box<dyn Isolation> = Box::new(b);
    assert!(a_dyn.compatible(b_dyn.as_ref()));
    assert!(a_dyn.join(b_dyn.as_ref()).is_some());
}
