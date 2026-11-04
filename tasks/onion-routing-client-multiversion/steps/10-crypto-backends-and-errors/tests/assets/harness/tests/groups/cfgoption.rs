//! `tor_config::resolve_option`: how an optional configuration item
//! distinguishes "unset" from "explicitly nothing".

#![allow(non_snake_case)]

use tor_config::resolve_option;

#[test]
fn cfgoption__an_unset_item_takes_the_supplied_default() {
    let unset: Option<Option<u16>> = None;
    assert_eq!(resolve_option(&unset, || Some(9150)), Some(9150));
    assert_eq!(resolve_option(&unset, || None), None);
}

#[test]
fn cfgoption__an_explicit_none_overrides_the_default() {
    let disabled: Option<Option<u16>> = Some(None);
    assert_eq!(
        resolve_option(&disabled, || Some(9150)),
        None,
        "an explicit null must not fall back to the default"
    );
}

#[test]
fn cfgoption__an_explicit_default_value_means_nothing() {
    let zero: Option<Option<u16>> = Some(Some(0));
    assert_eq!(
        resolve_option(&zero, || Some(9150)),
        None,
        "the type's own default value is the `disable this` sentinel"
    );
    let empty: Option<Option<String>> = Some(Some(String::new()));
    assert_eq!(resolve_option(&empty, || Some("x".to_string())), None);
}

#[test]
fn cfgoption__an_explicit_value_is_passed_through() {
    let set: Option<Option<u16>> = Some(Some(1080));
    assert_eq!(resolve_option(&set, || Some(9150)), Some(1080));
    assert_eq!(resolve_option(&set, || None), Some(1080));
}
