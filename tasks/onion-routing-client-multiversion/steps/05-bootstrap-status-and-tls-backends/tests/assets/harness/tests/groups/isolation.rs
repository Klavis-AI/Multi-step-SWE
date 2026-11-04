//! Group `isolation` — the isolate-every-stream mode and the isolation tokens
//! it sits beside.
//!
//! The recorded mode is observed through `StreamPrefs`'s derived `Debug`.
//! Token *values* are drawn from a process-global counter, so no case here
//! asserts on a token's printed number — only on which mode is recorded, and
//! on the documented equality relationships between tokens.

use arti_client::{IsolationToken, StreamPrefs};

fn rendered(p: &StreamPrefs) -> String {
    format!("{p:?}")
}

#[test]
fn isolation__every_stream_mode_is_recorded_in_the_preferences() {
    let mut p = StreamPrefs::new();
    p.isolate_every_stream();
    assert!(
        rendered(&p).contains("isolation: EveryStream"),
        "isolate_every_stream was not recorded: {}",
        rendered(&p)
    );
}

#[test]
fn isolation__explicit_group_is_recorded_in_the_preferences() {
    let mut p = StreamPrefs::new();
    p.set_isolation_group(IsolationToken::new());
    let text = rendered(&p);
    assert!(
        text.contains("isolation: Explicit("),
        "an explicit isolation group was not recorded: {text}"
    );
    assert!(!text.contains("EveryStream"));
}

#[test]
fn isolation__new_isolation_group_replaces_every_stream_mode() {
    let mut p = StreamPrefs::new();
    p.isolate_every_stream();
    p.new_isolation_group();
    let text = rendered(&p);
    assert!(
        text.contains("isolation: Explicit("),
        "new_isolation_group must install a concrete group: {text}"
    );
    assert!(!text.contains("EveryStream"));
}

#[test]
fn isolation__every_stream_mode_replaces_an_explicit_group() {
    let mut p = StreamPrefs::new();
    p.set_isolation_group(IsolationToken::new());
    p.isolate_every_stream();
    let text = rendered(&p);
    assert!(
        text.contains("isolation: EveryStream"),
        "isolate_every_stream must override an explicit group: {text}"
    );
    assert!(!text.contains("Explicit("));
}

#[test]
fn isolation__fresh_tokens_are_distinct() {
    let a = IsolationToken::new();
    let b = IsolationToken::new();
    assert_ne!(a, b, "each fresh isolation token must be unique");
    assert_eq!(a, a);
}

#[test]
fn isolation__no_isolation_tokens_are_all_equal() {
    // The "no isolation" token is a fixed sentinel, so two of them compare
    // equal and neither collides with a freshly issued token.
    assert_eq!(IsolationToken::no_isolation(), IsolationToken::no_isolation());
    assert_ne!(IsolationToken::no_isolation(), IsolationToken::new());
}
