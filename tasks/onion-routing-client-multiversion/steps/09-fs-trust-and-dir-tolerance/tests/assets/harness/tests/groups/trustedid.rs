//! `fs_mistrust::TrustedUser` / `TrustedGroup`: the identity model that step 9
//! introduces in place of the older `trust_group_id(u32)` setter.
#![allow(non_snake_case)]
use crate::common::{Tree, BAD_DIR_GROUP_W, GOOD_DIR};
use fs_mistrust::{Error, Mistrust, TrustedGroup, TrustedUser};

/// A name that cannot plausibly exist in any password or group database.
const ABSENT: &str = "arti-step09-no-such-principal";

#[test]
fn trustedid__numeric_conversion_is_an_id() {
    assert_eq!(TrustedUser::from(413_u32), TrustedUser::Id(413));
    assert_eq!(TrustedGroup::from(414_u32), TrustedGroup::Id(414));
}

#[test]
fn trustedid__string_conversion_is_a_name() {
    assert_eq!(
        TrustedUser::from("jane"),
        TrustedUser::Name("jane".to_string().into())
    );
    assert_eq!(
        TrustedGroup::from(String::from("wheel")),
        TrustedGroup::Name("wheel".to_string().into())
    );
}

#[test]
fn trustedid__defaults_are_current_user_and_self_named_group() {
    assert_eq!(TrustedUser::default(), TrustedUser::Current);
    assert_eq!(TrustedGroup::default(), TrustedGroup::SelfNamed);
}

#[test]
fn trustedid__unknown_user_name_is_reported_by_name() {
    let e = Mistrust::builder().trust_user(ABSENT).build().unwrap_err();
    match &e {
        Error::NoSuchUser(name) => assert_eq!(name, ABSENT),
        other => panic!("unexpected error: {other:?}"),
    }
    // A missing principal is a configuration problem, not a permission problem.
    assert!(!e.is_bad_permission());
    assert_eq!(e.path(), None);
}

#[test]
fn trustedid__unknown_group_name_is_reported_by_name() {
    let e = Mistrust::builder().trust_group(ABSENT).build().unwrap_err();
    match &e {
        Error::NoSuchGroup(name) => assert_eq!(name, ABSENT),
        other => panic!("unexpected error: {other:?}"),
    }
    assert!(!e.is_bad_permission());
}

#[test]
fn trustedid__numeric_ids_need_no_database_lookup() {
    // `Id` never consults the user database, so an id that exists on no host
    // still builds.
    let m = Mistrust::builder()
        .trust_user(TrustedUser::Id(31337))
        .trust_group(TrustedGroup::Id(31338))
        .build()
        .unwrap();
    assert_ne!(m, Mistrust::new());

    // ... and `None` really means "trust nobody but root": a directory owned
    // by us is then rejected for its owner, not its mode.
    let t = Tree::new();
    let d = t.dir("d", GOOD_DIR);
    let strict = {
        let mut b = Mistrust::builder();
        b.ignore_prefix(t.root())
            .trust_user(TrustedUser::None)
            .trust_group(TrustedGroup::None);
        b.build().unwrap()
    };
    let e = strict.verifier().check(&d).unwrap_err();
    assert_eq!(e.path(), Some(d.as_path()));
    assert!(matches!(e, Error::BadOwner(_, uid) if uid == t.uid()));
}

#[test]
fn trustedid__trusting_the_owning_group_admits_group_write() {
    // A group-writable directory is rejected when no group is trusted, and
    // accepted when the group that owns it is named by id.
    let t = Tree::new();
    let d = t.dir("shared", BAD_DIR_GROUP_W);
    let gid = t.gid();

    let build = |g: TrustedGroup| {
        let mut b = Mistrust::builder();
        b.ignore_prefix(t.root())
            .trust_user(TrustedUser::Current)
            .trust_group(g);
        b.build().unwrap()
    };

    let e = build(TrustedGroup::None).verifier().check(&d).unwrap_err();
    assert!(matches!(e, Error::BadPermission(_, bits) if bits == 0o020), "{e:?}");

    build(TrustedGroup::Id(gid))
        .verifier()
        .check(&d)
        .unwrap_or_else(|e| panic!("trusted group still rejected: {e}"));
}
