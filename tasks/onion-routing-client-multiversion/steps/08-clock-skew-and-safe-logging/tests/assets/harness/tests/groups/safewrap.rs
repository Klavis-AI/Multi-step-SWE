//! Group `safewrap`: `Sensitive<T>` is a transparent wrapper -- it changes how
//! a value prints, and nothing else.
//!
//! Traces to instruction.md section 1: "safe logging on by default".
//! Interface: `safelog::{Sensitive, sensitive}`, its `Deref`/`DerefMut`,
//! `From`, `Default`, `Clone`, `Eq`, `Ord` and `Hash` implementations, and the
//! associated function `Sensitive::unwrap`.

use safelog::{sensitive, Sensitive};
use std::collections::HashSet;

#[test]
fn safewrap__new_and_unwrap_round_trip() {
    let v = Sensitive::new(1234_u32);
    assert_eq!(Sensitive::unwrap(v), 1234_u32);

    let s = Sensitive::new(String::from("tor"));
    assert_eq!(Sensitive::unwrap(s), String::from("tor"));
}

#[test]
fn safewrap__the_free_function_is_an_alias_for_new() {
    assert_eq!(sensitive(9_u8), Sensitive::new(9_u8));
    assert_eq!(Sensitive::unwrap(sensitive("x")), "x");
}

#[test]
fn safewrap__from_wraps_the_value() {
    let v: Sensitive<u16> = 443_u16.into();
    assert_eq!(v, Sensitive::new(443_u16));
    assert_eq!(Sensitive::unwrap(Sensitive::from(443_u16)), 443_u16);
}

#[test]
fn safewrap__deref_exposes_the_inner_value() {
    let v = Sensitive::new(vec![1_u8, 2, 3]);
    assert_eq!(v.len(), 3);
    assert_eq!(*v, vec![1_u8, 2, 3]);
    assert_eq!(v.iter().copied().sum::<u8>(), 6);
}

#[test]
fn safewrap__deref_mut_allows_mutation_in_place() {
    let mut v: Sensitive<Vec<u32>> = Sensitive::default();
    assert!(v.is_empty());
    v.push(104);
    v.push(49);
    assert_eq!(v.len(), 2);
    assert_eq!(Sensitive::unwrap(v), vec![104, 49]);
}

#[test]
fn safewrap__comparison_follows_the_inner_value() {
    assert_eq!(Sensitive::new(5_i32), Sensitive::new(5_i32));
    assert_ne!(Sensitive::new(5_i32), Sensitive::new(6_i32));
    assert!(Sensitive::new(5_i32) < Sensitive::new(6_i32));

    let mut v = vec![Sensitive::new(3_u8), Sensitive::new(1), Sensitive::new(2)];
    v.sort();
    assert_eq!(
        v,
        vec![Sensitive::new(1_u8), Sensitive::new(2), Sensitive::new(3)]
    );

    let mut set = HashSet::new();
    set.insert(Sensitive::new("a"));
    set.insert(Sensitive::new("a"));
    set.insert(Sensitive::new("b"));
    assert_eq!(set.len(), 2);
    assert!(set.contains(&Sensitive::new("b")));
}

#[test]
fn safewrap__default_and_clone_delegate_to_the_inner_type() {
    let d: Sensitive<u64> = Sensitive::default();
    assert_eq!(Sensitive::unwrap(d), 0);

    let original = Sensitive::new(String::from("guard"));
    let copy = original.clone();
    assert_eq!(original, copy);
    assert_eq!(Sensitive::unwrap(copy), "guard");
}
