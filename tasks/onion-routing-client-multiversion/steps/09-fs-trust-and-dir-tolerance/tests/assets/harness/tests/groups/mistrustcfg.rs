//! `fs_mistrust::MistrustBuilder`: the configuration object for filesystem
//! permission checking that step 9 introduces.
//!
//! Every case pins `trust_user` and `trust_group` explicitly, because the
//! defaults (`TrustedUser::Current` / `TrustedGroup::SelfNamed`) resolve
//! against the running account's `/etc/passwd` entry and would otherwise make
//! the outcome depend on the host.
#![allow(non_snake_case)]
use crate::common::{Tree, BAD_DIR_WORLD_W, GOOD_DIR};
use fs_mistrust::{Mistrust, MistrustBuilder, TrustedGroup, TrustedUser};
use std::path::Path;

/// A builder with the host-dependent knobs pinned: current user trusted, no
/// group trusted.
fn pinned() -> MistrustBuilder {
    let mut b = Mistrust::builder();
    b.trust_user(TrustedUser::Current)
        .trust_group(TrustedGroup::None);
    b
}

/// As [`pinned`], plus an ignore-prefix of `root`.
fn pinned_below(root: &Path) -> Mistrust {
    let mut b = pinned();
    b.ignore_prefix(root);
    b.build().unwrap()
}

#[test]
fn mistrustcfg__new_equals_builder_default() {
    // `Mistrust::new()`, `Mistrust::default()` and a freshly built builder all
    // denote the same configuration.
    let from_new = Mistrust::new();
    let from_builder = Mistrust::builder().build().unwrap();
    let from_default = Mistrust::default();
    assert_eq!(from_new, from_builder);
    assert_eq!(from_new, from_default);
}

#[test]
fn mistrustcfg__trusted_ids_are_part_of_the_identity() {
    // Two configurations that differ only in the trusted uid/gid are distinct
    // values, and the same ids compare equal.
    let a = pinned().trust_user(1000_u32).build().unwrap();
    let b = pinned().trust_user(1001_u32).build().unwrap();
    let a2 = pinned().trust_user(1000_u32).build().unwrap();
    assert_ne!(a, b);
    assert_eq!(a, a2);

    let g1 = pinned().trust_group(2000_u32).build().unwrap();
    let g2 = pinned().trust_group(2001_u32).build().unwrap();
    assert_ne!(g1, g2);
    assert_ne!(g1, a);
}

#[test]
fn mistrustcfg__dangerously_trust_everyone_accepts_world_writable() {
    let t = Tree::new();
    let open = t.dir("open", BAD_DIR_WORLD_W);

    let lax = Mistrust::new_dangerously_trust_everyone();
    let lax2 = Mistrust::builder()
        .dangerously_trust_everyone()
        .build()
        .unwrap();
    assert_eq!(lax, lax2);
    assert_ne!(lax, Mistrust::new());

    // The permissive configuration accepts the very directory the strict one
    // rejects.
    lax.verifier().check(&open).unwrap();
    let e = pinned_below(t.root()).verifier().check(&open).unwrap_err();
    assert!(e.is_bad_permission(), "{e}");
}

#[test]
fn mistrustcfg__ignore_prefix_stops_the_upward_walk() {
    // `open` is world-writable; `open/inner` is not. A checker that ignores
    // everything at or above `open` accepts `inner`; one that only ignores the
    // tree root walks into `open` and rejects it.
    let t = Tree::new();
    let open = t.dir("open", BAD_DIR_WORLD_W);
    let inner = t.dir("open/inner", GOOD_DIR);

    pinned_below(&open).verifier().check(&inner).unwrap();

    let e = pinned_below(t.root()).verifier().check(&inner).unwrap_err();
    assert_eq!(e.path(), Some(open.as_path()));
    assert!(e.is_bad_permission(), "{e}");
}

#[test]
fn mistrustcfg__ignore_prefix_is_canonicalized() {
    // The documented behaviour is that the prefix is stored canonicalized, so
    // two spellings of the same directory produce equal configurations.
    let t = Tree::new();
    let open = t.dir("open", GOOD_DIR);
    let _ = t.dir("open/inner", GOOD_DIR);

    let direct = pinned_below(&open);
    let roundabout = pinned_below(&t.path("open/inner/../../open"));
    assert_eq!(direct, roundabout);
}

#[test]
fn mistrustcfg__ignore_prefix_of_absent_path_is_an_error() {
    let t = Tree::new();
    let e = {
        let mut b = pinned();
        b.ignore_prefix(t.path("no-such-directory"));
        b.build().unwrap_err()
    };
    assert!(
        matches!(e, fs_mistrust::Error::NotFound(_)),
        "unexpected error: {e:?}"
    );
}

#[test]
fn mistrustcfg__remove_ignored_prefix_undoes_ignore_prefix() {
    let t = Tree::new();
    let open = t.dir("open", GOOD_DIR);

    let mut b = pinned();
    b.ignore_prefix(&open).remove_ignored_prefix();
    let cleared = b.build().unwrap();

    assert_eq!(cleared, pinned().build().unwrap());
    assert_ne!(cleared, pinned_below(&open));
}

#[test]
fn mistrustcfg__trust_admin_only_drops_user_and_group() {
    // `trust_admin_only` is documented to disable group trust as well as user
    // trust, so it must equal an explicit `None`/`None` configuration.
    let admin_only = pinned().trust_admin_only().build().unwrap();
    let explicit = Mistrust::builder()
        .trust_user(TrustedUser::None)
        .trust_group(TrustedGroup::None)
        .build()
        .unwrap();
    assert_eq!(admin_only, explicit);
    assert_ne!(admin_only, pinned().build().unwrap());
}

#[test]
fn mistrustcfg__trust_no_group_id_only_drops_the_group() {
    let no_group = Mistrust::builder()
        .trust_user(TrustedUser::Current)
        .trust_no_group_id()
        .build()
        .unwrap();
    assert_eq!(no_group, pinned().build().unwrap());

    // ... and it is not the same as also dropping the trusted user.
    let admin_only = pinned().trust_admin_only().build().unwrap();
    assert_ne!(no_group, admin_only);
}
