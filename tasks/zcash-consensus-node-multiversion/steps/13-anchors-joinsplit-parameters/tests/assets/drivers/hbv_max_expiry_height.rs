//! MAX_EXPIRY_HEIGHT constant — step 13.
//!
//! Harbor verifier driver -- injected into `zebra-chain/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! Tests the block::Height::MAX_EXPIRY_HEIGHT constant added at step 13 and
//! its relationship to block::Height::MAX and the serialization invariants.

use zebra_chain::block::Height;

// ------------------------------------------------------------------- tests

#[test]
fn max_expiry_height_value_is_499_999_999() {
    assert_eq!(
        Height::MAX_EXPIRY_HEIGHT,
        Height(499_999_999),
        "MAX_EXPIRY_HEIGHT must be Height(499_999_999) per the Zcash protocol spec"
    );
}

#[test]
fn max_expiry_height_fits_within_max_block_height() {
    // At v1.0.0-beta.3, MAX_EXPIRY_HEIGHT == Height::MAX (both 499_999_999).
    // A later PR (#1113) will lower MAX_EXPIRY_HEIGHT; for now the relationship
    // we assert is <=, which is always true and reflects the design intent that
    // MAX_EXPIRY_HEIGHT must never exceed the block height ceiling.
    assert!(
        Height::MAX_EXPIRY_HEIGHT.0 <= Height::MAX.0,
        "MAX_EXPIRY_HEIGHT ({}) must not exceed MAX ({})",
        Height::MAX_EXPIRY_HEIGHT.0,
        Height::MAX.0
    );
}

#[test]
fn height_one_above_max_expiry_is_above_limit() {
    let above = Height(500_000_000);
    assert!(
        above.0 > Height::MAX_EXPIRY_HEIGHT.0,
        "Height(500_000_000) should be above MAX_EXPIRY_HEIGHT"
    );
}

#[test]
fn max_expiry_height_is_exactly_on_limit() {
    let at_limit = Height(499_999_999);
    assert!(at_limit.0 <= Height::MAX_EXPIRY_HEIGHT.0);
}

#[test]
fn max_expiry_height_constant_is_copy_and_debug() {
    let h = Height::MAX_EXPIRY_HEIGHT;
    let h2 = h; // Copy
    assert_eq!(h, h2);
    let s = format!("{:?}", h);
    assert!(s.contains("499999999") || s.contains("Height"), "Debug output: {}", s);
}

#[test]
fn max_expiry_height_ord() {
    let below = Height(499_999_998);
    let at = Height::MAX_EXPIRY_HEIGHT;
    let above = Height(500_000_000);
    assert!(below < at);
    assert!(at < above);
    assert_eq!(at, Height(499_999_999));
}

#[test]
fn max_expiry_height_not_zero() {
    assert_ne!(Height::MAX_EXPIRY_HEIGHT, Height(0));
}

#[test]
fn max_expiry_height_equals_max_at_beta3() {
    // At v1.0.0-beta.3, MAX_EXPIRY_HEIGHT and Height::MAX are both 499_999_999.
    // The source has a TODO noting they will diverge in #1113; this test
    // documents the current state rather than the future intent.
    assert_eq!(Height::MAX_EXPIRY_HEIGHT, Height::MAX);
}

#[test]
fn max_expiry_height_decimal_text() {
    // Ensure the constant's numeric value matches the spec text 499_999_999.
    let expected: u32 = 499_999_999;
    assert_eq!(Height::MAX_EXPIRY_HEIGHT.0, expected);
}

#[test]
fn heights_compare_with_max_expiry_height() {
    // All valid block heights below the limit compare less than the constant.
    for h in [0u32, 1, 1_000, 419_200, 903_000, 499_999_998] {
        assert!(
            Height(h) < Height::MAX_EXPIRY_HEIGHT,
            "Height({}) should be < MAX_EXPIRY_HEIGHT", h
        );
    }
}
