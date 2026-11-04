//! Group `isolation` - the circuit-isolation API.
//!
//! Graded through `tor_circmgr::{IsolationToken, StreamIsolation,
//! StreamIsolationBuilder}` and `arti_client::ConnectPrefs`. Two streams that
//! carry different isolation tokens must never compare equal, because equality
//! is exactly what the circuit manager uses to decide whether a circuit may be
//! shared.

use tor_circmgr::{IsolationToken, StreamIsolation, StreamIsolationBuilder};

#[test]
fn isolation__freshly_minted_tokens_are_pairwise_distinct() {
    let a = IsolationToken::new();
    let b = IsolationToken::new();
    let c = IsolationToken::new();
    assert_ne!(a, b, "two fresh isolation tokens compared equal");
    assert_ne!(b, c, "two fresh isolation tokens compared equal");
    assert_ne!(a, c, "two fresh isolation tokens compared equal");

    // The token is `Copy`, and a copy names the same isolation group.
    let a_copy = a;
    assert_eq!(a, a_copy);
}

#[test]
fn isolation__the_shared_token_is_equal_to_itself_and_to_nothing_else() {
    assert_eq!(
        IsolationToken::no_isolation(),
        IsolationToken::no_isolation(),
        "no_isolation() tokens must all name the same group"
    );
    // A fresh token is isolated from the shared group as well as from its
    // siblings; otherwise `set_isolation_group` would be a no-op for any
    // client that had not opted in.
    for _ in 0..4 {
        assert_ne!(IsolationToken::new(), IsolationToken::no_isolation());
    }
}

#[test]
fn isolation__a_default_stream_isolation_uses_the_shared_token() {
    let from_ctor = StreamIsolation::no_isolation();
    let from_builder = StreamIsolationBuilder::new()
        .build()
        .expect("an empty StreamIsolationBuilder failed to build");
    assert_eq!(
        from_ctor, from_builder,
        "an unset StreamIsolation must default to the shared isolation group"
    );

    let spelled_out = StreamIsolation::builder()
        .stream_token(IsolationToken::no_isolation())
        .owner_token(IsolationToken::no_isolation())
        .build()
        .expect("StreamIsolationBuilder::build failed");
    assert_eq!(from_ctor, spelled_out);
}

#[test]
fn isolation__the_stream_token_separates_two_isolations() {
    let t1 = IsolationToken::new();
    let t2 = IsolationToken::new();

    let a = StreamIsolation::builder()
        .stream_token(t1)
        .build()
        .expect("build failed");
    let b = StreamIsolation::builder()
        .stream_token(t2)
        .build()
        .expect("build failed");
    let a_again = StreamIsolation::builder()
        .stream_token(t1)
        .build()
        .expect("build failed");

    assert_ne!(a, b, "different stream tokens must not share a circuit");
    assert_eq!(a, a_again, "the same stream token must compare equal");
    assert_ne!(
        a,
        StreamIsolation::no_isolation(),
        "an isolated stream must not match the shared group"
    );
}

#[test]
fn isolation__the_owner_token_separates_two_isolations() {
    let t1 = IsolationToken::new();
    let t2 = IsolationToken::new();

    let a = StreamIsolation::builder()
        .owner_token(t1)
        .build()
        .expect("build failed");
    let b = StreamIsolation::builder()
        .owner_token(t2)
        .build()
        .expect("build failed");
    assert_ne!(a, b, "different owner tokens must not share a circuit");

    // The two tokens are independent: a token placed on the owner is not the
    // same isolation as the same token placed on the stream.
    let on_stream = StreamIsolation::builder()
        .stream_token(t1)
        .build()
        .expect("build failed");
    assert_ne!(
        a, on_stream,
        "the owner token and the stream token must be tracked separately"
    );

    // ... and setting both narrows the group further still.
    let both = StreamIsolation::builder()
        .owner_token(t1)
        .stream_token(t2)
        .build()
        .expect("build failed");
    assert_ne!(both, a);
    assert_ne!(both, on_stream);
}

#[test]
fn isolation__connect_preferences_record_the_isolation_group() {
    let t1 = IsolationToken::new();
    let t2 = IsolationToken::new();

    let unset = format!("{:?}", arti_client::ConnectPrefs::new());

    let mut p1 = arti_client::ConnectPrefs::new();
    p1.set_isolation_group(t1);
    let p1 = format!("{:?}", p1);

    let mut p1_again = arti_client::ConnectPrefs::new();
    p1_again.set_isolation_group(t1);
    let p1_again = format!("{:?}", p1_again);

    let mut p2 = arti_client::ConnectPrefs::new();
    p2.set_isolation_group(t2);
    let p2 = format!("{:?}", p2);

    assert_ne!(
        unset, p1,
        "set_isolation_group did not record the token in the preferences"
    );
    assert_eq!(p1, p1_again, "the same token produced different preferences");
    assert_ne!(p1, p2, "two different tokens produced the same preferences");

    // The setter chains, which is what `ConnectPrefs` is documented to allow.
    let mut chained = arti_client::ConnectPrefs::new();
    chained.ipv4_only().set_isolation_group(t1).ipv6_preferred();
    assert_ne!(format!("{:?}", chained), unset);
}
