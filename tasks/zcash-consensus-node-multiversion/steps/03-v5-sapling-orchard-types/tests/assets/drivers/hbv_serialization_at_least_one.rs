//! `AtLeastOne<T>`, the wrapper that carries the "at least one" cardinality.
//!
//! Harbor verifier driver -- injected into `zebra-chain/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! `new` group. `zebra_chain::serialization::AtLeastOne<T>` did not exist
//! before this step. It exists now because ZIP-225's V5 shielded bundle is
//! defined as "a shared anchor plus at least one spend" or "at least one
//! output" -- `TransferData::SpendsAndMaybeOutputs.spends` and
//! `TransferData::JustOutputs.outputs` are each an `AtLeastOne<_>`, not a
//! plain `Vec`, so that a value of the type itself witnesses the ZIP's
//! cardinality requirement instead of leaving it to a runtime check. This
//! group tests the wrapper in isolation, over `u32`, so that it stays
//! orthogonal to `tx-v5-sapling-transfers`, which tests it as it actually
//! appears inside a parsed V5 transaction.
//!
//! None of these tests calls anything the crate does not already export:
//! `TryFrom<Vec<T>>`, `TryFrom<&[T]>`, `TryFrom<[T; N]>`, `TryFrom<&[T; N]>`,
//! `Deref<Target = Vec<T>>`, `From<AtLeastOne<T>> for Vec<T>`, and the
//! `as_vec`/`into_vec`/`first`/`split_first` inherent methods -- all public,
//! all documented on the type itself.

use std::convert::TryFrom;
use std::convert::TryInto;

use zebra_chain::serialization::AtLeastOne;

// ------------------------------------------------------------------- tests

#[test]
fn try_from_vec_rejects_the_empty_vector() {
    let result = AtLeastOne::<u32>::try_from(Vec::<u32>::new());
    assert!(
        result.is_err(),
        "AtLeastOne::try_from(vec![]) should be an error, got {:?}",
        result
    );
}

#[test]
fn try_from_a_nonempty_vector_succeeds_and_preserves_order() {
    let source = vec![10u32, 20, 30];
    let wrapped =
        AtLeastOne::try_from(source.clone()).expect("a 3-element vector satisfies AtLeastOne");
    assert_eq!(
        wrapped.as_vec(),
        &source,
        "AtLeastOne should preserve the vector's element order"
    );
}

#[test]
fn try_from_slice_and_try_from_array_agree_with_try_from_vec_on_emptiness() {
    // Four different entry points into the same constraint: an owned `Vec`, a
    // borrowed slice, a fixed-size array by value, and a fixed-size array by
    // reference. All four must reject empty input and accept non-empty input
    // the same way `TryFrom<Vec<T>>` does.
    assert!(AtLeastOne::<u32>::try_from(Vec::<u32>::new()).is_err());
    assert!(AtLeastOne::<u32>::try_from(&[][..]).is_err());
    assert!(AtLeastOne::<u32>::try_from([0u32; 0]).is_err());
    assert!(AtLeastOne::<u32>::try_from(&[0u32; 0]).is_err());

    let from_vec = AtLeastOne::try_from(vec![7u32]).expect("vec![7]");
    let from_slice = AtLeastOne::try_from(&[7u32][..]).expect("&[7][..]");
    let from_array = AtLeastOne::try_from([7u32]).expect("[7]");
    let from_array_ref = AtLeastOne::try_from(&[7u32]).expect("&[7]");

    for (label, got) in [
        ("Vec", &from_vec),
        ("&[T]", &from_slice),
        ("[T; N]", &from_array),
        ("&[T; N]", &from_array_ref),
    ] {
        assert_eq!(
            got.as_vec(),
            &vec![7u32],
            "{}: should build the same single-element AtLeastOne",
            label
        );
    }
}

#[test]
fn first_returns_the_first_element_directly_not_wrapped_in_an_option() {
    let one: AtLeastOne<u32> = vec![42, 43, 44].try_into().expect("non-empty");
    // `.first()` on `AtLeastOne` returns `&T`, unlike `<[T]>::first`, which
    // returns `Option<&T>`. Using the result directly in arithmetic, with no
    // `.unwrap()` or `if let Some(..)` anywhere in this test, is what pins the
    // return type down rather than just its value.
    let doubled: u32 = *one.first() * 2;
    assert_eq!(doubled, 84, "first() should be the vector's first element, 42");
}

#[test]
fn split_first_returns_the_head_and_the_untouched_tail() {
    let three: AtLeastOne<u32> = vec![1, 2, 3].try_into().expect("non-empty");
    let (head, tail) = three.split_first();
    assert_eq!(*head, 1, "split_first's head should be the first element");
    assert_eq!(tail, [2, 3], "split_first's tail should be every remaining element, in order");

    let one: AtLeastOne<u32> = vec![9].try_into().expect("non-empty");
    let (head, tail) = one.split_first();
    assert_eq!(*head, 9);
    assert!(
        tail.is_empty(),
        "a single-element AtLeastOne should split into a head and an empty tail, got {:?}",
        tail
    );
}

#[test]
fn deref_gives_full_read_access_to_the_inner_vector() {
    let wrapped: AtLeastOne<u32> = vec![5, 6, 7].try_into().expect("non-empty");
    // Everything below goes through `Deref<Target = Vec<u32>>`, not through
    // any `AtLeastOne`-specific method.
    assert_eq!(wrapped.len(), 3, "len() via Deref");
    assert_eq!(wrapped.iter().sum::<u32>(), 18, "iter() via Deref");
    assert_eq!(wrapped[0], 5, "indexing via Deref");
    assert_eq!(wrapped.as_slice(), &[5, 6, 7], "as_slice() via Deref to Vec");
}

#[test]
fn into_vec_and_as_vec_agree_and_into_vec_consumes_the_wrapper() {
    let wrapped: AtLeastOne<u32> = vec![1, 2, 3].try_into().expect("non-empty");
    let via_as_vec = wrapped.as_vec().clone();
    let via_into_vec = wrapped.into_vec();
    assert_eq!(
        via_as_vec, via_into_vec,
        "as_vec() and into_vec() should agree on the same wrapper's contents"
    );
    assert_eq!(via_into_vec, vec![1, 2, 3]);
}

#[test]
fn from_at_least_one_for_vec_matches_into_vec() {
    let a: AtLeastOne<u32> = vec![100, 200].try_into().expect("non-empty");
    let b: AtLeastOne<u32> = vec![100, 200].try_into().expect("non-empty");
    let via_from: Vec<u32> = Vec::from(a);
    let via_into_vec: Vec<u32> = b.into_vec();
    assert_eq!(
        via_from, via_into_vec,
        "From<AtLeastOne<T>> for Vec<T> should agree with into_vec()"
    );
}

#[test]
fn a_value_that_round_trips_through_vec_and_back_is_unchanged() {
    let original: AtLeastOne<u32> = vec![9, 8, 7].try_into().expect("non-empty");
    let plain: Vec<u32> = original.clone().into_vec();
    let restored: AtLeastOne<u32> = plain.try_into().expect("a non-empty vec round trips");
    assert_eq!(
        original, restored,
        "AtLeastOne -> Vec -> AtLeastOne should be lossless"
    );
}

#[test]
fn equality_and_ordering_follow_the_inner_vectors_own_equality_and_ordering() {
    let a: AtLeastOne<u32> = vec![1, 2].try_into().expect("non-empty");
    let b: AtLeastOne<u32> = vec![1, 2].try_into().expect("non-empty");
    let c: AtLeastOne<u32> = vec![1, 3].try_into().expect("non-empty");
    let shorter: AtLeastOne<u32> = vec![1].try_into().expect("non-empty");

    assert_eq!(a, b, "two AtLeastOnes with equal contents should be equal");
    assert_ne!(a, c, "two AtLeastOnes with different contents should not be equal");

    assert!(
        a < c,
        "[1, 2] should order before [1, 3], the same as the inner Vec<u32> would"
    );
    assert!(
        shorter < a,
        "[1] should order before [1, 2], the same as the inner Vec<u32> would"
    );
}
