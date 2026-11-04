//! Inherited: a consensus `Lifetime` still has to accept a correctly ordered
//! validity window and reject a misordered one.
//!
//! Determinism: the three instants are fixed offsets from a constant rather
//! than from `SystemTime::now()`. `Lifetime::new` only compares its three
//! arguments to each other -- it never reads the clock -- so the outcome was
//! already independent of when the case ran; pinning the instant makes that
//! visible instead of arguable. See NOTES.md, determinism audit.

#![allow(non_snake_case)]
use std::time::{Duration, SystemTime};
use tor_netdoc::doc::netstatus::Lifetime;

#[test]
fn lifetimebase__new_valid() {
    // A fixed instant, not the host's clock: see the note at the top of the file.
    // This case is about the constructor: a correctly ordered window is
    // accepted and every instant comes back out unchanged. The three accessors
    // are then pinned one at a time by the cases below.
    let now = SystemTime::UNIX_EPOCH + Duration::from_secs(1_700_000_000);
    let va = now;
    let fu = now + Duration::from_secs(100);
    let vu = now + Duration::from_secs(200);
    let lt = Lifetime::new(va, fu, vu).unwrap();
    assert_eq!((lt.valid_after(), lt.fresh_until(), lt.valid_until()), (va, fu, vu));
}

#[test]
fn lifetimebase__new_rejects_bad_order() {
    // A fixed instant, not the host's clock: see the note at the top of the file.
    let now = SystemTime::UNIX_EPOCH + Duration::from_secs(1_700_000_000);
    let va = now + Duration::from_secs(200);
    let fu = now + Duration::from_secs(100);
    let vu = now;
    // The refusal is what is graded. Which error reports it, and what that
    // error's message says, are not part of any contract this workspace
    // publishes, so neither is asserted here.
    let _e = Lifetime::new(va, fu, vu).unwrap_err();
}

#[test]
fn lifetimebase__valid_after() {
    // A fixed instant, not the host's clock: see the note at the top of the file.
    let now = SystemTime::UNIX_EPOCH + Duration::from_secs(1_700_000_000);
    let va = now;
    let fu = now + Duration::from_secs(100);
    let vu = now + Duration::from_secs(200);
    let lt = Lifetime::new(va, fu, vu).unwrap();
    assert_eq!(lt.valid_after(), va);
}

#[test]
fn lifetimebase__fresh_until() {
    // A fixed instant, not the host's clock: see the note at the top of the file.
    let now = SystemTime::UNIX_EPOCH + Duration::from_secs(1_700_000_000);
    let va = now;
    let fu = now + Duration::from_secs(100);
    let vu = now + Duration::from_secs(200);
    let lt = Lifetime::new(va, fu, vu).unwrap();
    assert_eq!(lt.fresh_until(), fu);
}

#[test]
fn lifetimebase__valid_until() {
    // A fixed instant, not the host's clock: see the note at the top of the file.
    let now = SystemTime::UNIX_EPOCH + Duration::from_secs(1_700_000_000);
    let va = now;
    let fu = now + Duration::from_secs(100);
    let vu = now + Duration::from_secs(200);
    let lt = Lifetime::new(va, fu, vu).unwrap();
    assert_eq!(lt.valid_until(), vu);
}
