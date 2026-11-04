//! Group `circ_isolation` (inherited) - stream isolation tokens.
//!
//! instruction.md section 1: "**Preserve** guards, persistence and circuit
//! isolation from step 2."
//!
//! Graded items: `tor_circmgr::IsolationToken::{new, no_isolation}`,
//! `tor_circmgr::StreamIsolation::{no_isolation, builder}`,
//! `tor_circmgr::StreamIsolationBuilder`, and `tor_circmgr::TargetPort::ipv4`.
//!
//! Tokens come from a process-wide counter, so no case asserts a particular
//! numeric value - only the invariants: fresh tokens are distinct from each
//! other and from the no-isolation token, and equal tokens compare equal.

use tor_circmgr::{IsolationToken, StreamIsolation, StreamIsolationBuilder, TargetPort};

#[test]
fn circ_isolation__fresh_tokens_are_distinct() {
    let a = IsolationToken::new();
    let b = IsolationToken::new();
    let c = IsolationToken::new();
    assert_ne!(a, b);
    assert_ne!(b, c);
    assert_ne!(a, c);
    // A token equals itself and its copies.
    assert_eq!(a, a);
    let copied = a;
    assert_eq!(a, copied);
}

#[test]
fn circ_isolation__no_isolation_is_stable_and_distinct() {
    assert_eq!(IsolationToken::no_isolation(), IsolationToken::no_isolation());
    let fresh = IsolationToken::new();
    assert_ne!(fresh, IsolationToken::no_isolation());
}

#[test]
fn circ_isolation__default_stream_isolation_shares_everything() {
    let a = StreamIsolation::no_isolation();
    let b = StreamIsolation::no_isolation();
    assert_eq!(a, b);

    let built = StreamIsolationBuilder::new()
        .build()
        .expect("an untouched StreamIsolationBuilder must build");
    assert_eq!(built, a);

    let via_accessor = StreamIsolation::builder()
        .build()
        .expect("StreamIsolation::builder().build() must succeed");
    assert_eq!(via_accessor, a);
}

#[test]
fn circ_isolation__different_stream_tokens_do_not_match() {
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

    assert_ne!(a, b);
    assert_eq!(a, a_again);
    assert_ne!(a, StreamIsolation::no_isolation());
}

#[test]
fn circ_isolation__owner_token_is_part_of_the_identity() {
    let stream = IsolationToken::new();
    let owner1 = IsolationToken::new();
    let owner2 = IsolationToken::new();

    let a = StreamIsolation::builder()
        .stream_token(stream)
        .owner_token(owner1)
        .build()
        .expect("build failed");
    let b = StreamIsolation::builder()
        .stream_token(stream)
        .owner_token(owner2)
        .build()
        .expect("build failed");
    let a_again = StreamIsolation::builder()
        .stream_token(stream)
        .owner_token(owner1)
        .build()
        .expect("build failed");

    assert_ne!(a, b);
    assert_eq!(a, a_again);

    // Target ports are compared the same structural way.
    assert_eq!(TargetPort::ipv4(80), TargetPort::ipv4(80));
    assert_ne!(TargetPort::ipv4(80), TargetPort::ipv4(443));
}
