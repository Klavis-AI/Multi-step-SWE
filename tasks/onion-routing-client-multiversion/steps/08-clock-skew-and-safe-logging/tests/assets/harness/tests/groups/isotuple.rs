//! Group `isotuple` (inherited from step 7): several isolation rules can be
//! combined by tupling them, and the combination is compatible only when every
//! member is.
//!
//! Traces to instruction.md section 1: "Preserve user-supplied isolation ...
//! from step 7."  Interface: the `IsolationHelper` implementations for tuples
//! in `tor_circmgr::isolation`.

use tor_circmgr::isolation::{IsolationHelper, IsolationToken};

#[test]
fn isotuple__a_pair_is_compatible_when_both_members_are() {
    let a = IsolationToken::new();
    let b = IsolationToken::new();
    assert!((a, b).compatible_same_type(&(a, b)));
}

#[test]
fn isotuple__a_pair_is_incompatible_when_either_member_differs() {
    let a = IsolationToken::new();
    let b = IsolationToken::new();
    let c = IsolationToken::new();
    assert!(!(a, b).compatible_same_type(&(c, b)));
    assert!(!(a, b).compatible_same_type(&(a, c)));
    assert!(!(a, b).compatible_same_type(&(c, c)));
}

#[test]
fn isotuple__joining_a_pair_joins_each_member() {
    let a = IsolationToken::new();
    let b = IsolationToken::new();
    let c = IsolationToken::new();
    assert_eq!((a, b).join_same_type(&(a, b)), Some((a, b)));
    // One failing member makes the whole join fail.
    assert_eq!((a, b).join_same_type(&(a, c)), None);
    assert_eq!((a, b).join_same_type(&(c, b)), None);
}

#[test]
fn isotuple__tuples_up_to_twelve_members_are_supported() {
    let t = IsolationToken::new();
    let u = IsolationToken::new();
    let twelve = (t, t, t, t, t, t, t, t, t, t, t, t);
    assert!(twelve.compatible_same_type(&twelve));
    assert_eq!(twelve.join_same_type(&twelve), Some(twelve));

    let differs_in_last = (t, t, t, t, t, t, t, t, t, t, t, u);
    assert!(!twelve.compatible_same_type(&differs_in_last));
    assert_eq!(twelve.join_same_type(&differs_in_last), None);

    // A three-member tuple works the same way.
    assert!((t, u, t).compatible_same_type(&(t, u, t)));
    assert!(!(t, u, t).compatible_same_type(&(t, t, t)));
}
