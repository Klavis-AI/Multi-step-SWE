//! Group `fallbackdir`: the fallback directory entry.  It moved into the guard
//! manager at this checkpoint, because a fallback is now picked, tried and
//! retried by the same machinery as a guard, and it can therefore be turned
//! into a first hop directly.
//!
//! Graded items: `tor_guardmgr::fallback::{FallbackDir, FallbackDirBuilder}`,
//! `tor_guardmgr::FirstHop`, and the `tor_dirmgr` / `arti_client` re-exports.

use tor_guardmgr::fallback::{FallbackDir, FallbackDirBuilder};
use tor_linkspec::ChanTarget;
use tor_config::ConfigBuildError;

// The step-07 helpers this group needs live in `current`, apart from `common`
// so that no gate group is forced to compile them; see the note at the top of
// common/current.rs. current.rs re-exports common, so one path reaches both.
use crate::current::{addrs, ed_id, fallback, rsa_id};

#[test]
fn fallbackdir__a_fallback_records_its_identities_and_orports() {
    let fb = fallback(&["127.0.0.1:9001", "[::1]:9002"], 5);
    assert_eq!(fb.ed_identity(), &ed_id(5));
    assert_eq!(fb.rsa_identity(), &rsa_id(5));
    assert_eq!(fb.addrs(), &addrs(&["127.0.0.1:9001", "[::1]:9002"])[..]);
}

#[test]
fn fallbackdir__the_builder_accumulates_orports_across_calls() {
    let mut b = FallbackDir::builder();
    b.rsa_identity(rsa_id(1)).ed_identity(ed_id(1));
    for a in addrs(&["127.0.0.1:1", "127.0.0.1:2", "127.0.0.1:3"]) {
        b.orport(a);
    }
    let fb = b.build().expect("three ORPorts is a valid fallback");
    // Each call appends; order is preserved.
    assert_eq!(
        fb.addrs(),
        &addrs(&["127.0.0.1:1", "127.0.0.1:2", "127.0.0.1:3"])[..]
    );

    // `FallbackDirBuilder::new` is equivalent to `FallbackDir::builder`.
    let mut b = FallbackDirBuilder::new();
    b.rsa_identity(rsa_id(1)).ed_identity(ed_id(1));
    b.orport(addrs(&["127.0.0.1:1"])[0]);
    b.orport(addrs(&["127.0.0.1:2"])[0]);
    b.orport(addrs(&["127.0.0.1:3"])[0]);
    assert_eq!(b.build().expect("builds"), fb);
}

#[test]
fn fallbackdir__a_fallback_without_an_orport_is_rejected() {
    let mut b = FallbackDir::builder();
    b.rsa_identity(rsa_id(2)).ed_identity(ed_id(2));
    let err = b.build().expect_err("a fallback needs at least one ORPort");
    match err {
        ConfigBuildError::MissingField { field } => assert_eq!(field, "orports"),
        other => panic!("expected a missing-field error, got {:?}", other),
    }

    // The identities are required too.
    let mut b = FallbackDir::builder();
    b.orport(addrs(&["127.0.0.1:9001"])[0]);
    assert!(b.build().is_err(), "identities are required");
}

#[test]
fn fallbackdir__an_empty_orport_list_is_rejected() {
    let mut b = FallbackDir::builder();
    b.rsa_identity(rsa_id(3))
        .ed_identity(ed_id(3))
        .orports(vec![]);
    let err = b.build().expect_err("an empty ORPort list is unusable");
    match err {
        ConfigBuildError::Invalid { field, problem } => {
            assert_eq!(field, "orport");
            assert_eq!(problem, "list was empty");
        }
        other => panic!("expected an invalid-field error, got {:?}", other),
    }
}

#[test]
fn fallbackdir__a_fallback_can_be_used_as_a_first_hop() {
    let fb = fallback(&["127.0.0.1:9001", "127.0.0.2:9001"], 9);
    let hop = fb.as_guard();
    // The first hop describes the same relay ...
    assert_eq!(hop.ed_identity(), fb.ed_identity());
    assert_eq!(hop.rsa_identity(), fb.rsa_identity());
    assert_eq!(hop.addrs(), fb.addrs());
    // ... and its identity is stable and distinguishes relays.
    assert_eq!(hop.id(), fb.as_guard().id());
    let other = fallback(&["127.0.0.1:9001"], 10);
    assert_ne!(hop.id(), other.as_guard().id());
}

#[test]
fn fallbackdir__fallbacks_compare_and_clone_by_value() {
    let a = fallback(&["127.0.0.1:9001"], 4);
    let b = fallback(&["127.0.0.1:9001"], 4);
    assert_eq!(a, b);
    assert_eq!(a, a.clone());
    // Any differing part makes them unequal.
    assert_ne!(a, fallback(&["127.0.0.1:9002"], 4));
    assert_ne!(a, fallback(&["127.0.0.1:9001"], 5));
    assert_ne!(a, fallback(&["127.0.0.1:9001", "127.0.0.1:9002"], 4));
}

#[test]
fn fallbackdir__the_fallback_type_is_shared_with_the_directory_manager() {
    // The directory manager and the client both re-export the very same type,
    // so a fallback built here can be handed to either.  These functions only
    // compile if all three names refer to one type.
    fn from_dirmgr(fb: tor_dirmgr::FallbackDir) -> FallbackDir {
        fb
    }
    fn from_client(fb: arti_client::config::dir::FallbackDir) -> FallbackDir {
        fb
    }
    fn builder_from_dirmgr(b: tor_dirmgr::FallbackDirBuilder) -> FallbackDirBuilder {
        b
    }

    let fb = fallback(&["127.0.0.1:9001"], 6);
    assert_eq!(from_dirmgr(fb.clone()), fb);
    assert_eq!(from_client(fb.clone()), fb);
    let mut b = builder_from_dirmgr(tor_dirmgr::FallbackDirBuilder::new());
    b.rsa_identity(rsa_id(6))
        .ed_identity(ed_id(6))
        .orport(addrs(&["127.0.0.1:9001"])[0]);
    assert_eq!(b.build().expect("builds"), fb);
}
