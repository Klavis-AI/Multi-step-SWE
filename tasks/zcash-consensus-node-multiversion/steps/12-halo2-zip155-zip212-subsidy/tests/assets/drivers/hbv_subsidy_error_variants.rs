//! Subsidy error variants new at app-12.
//!
//! Harbor verifier driver -- injected into `zebra-consensus/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! `SubsidyError` had two variants in app-11:
//!   - `NoCoinbase`
//!   - `FoundersRewardNotFound`
//!
//! At app-12 a third variant is added:
//!   - `FundingStreamNotFound` -- raised when the expected funding stream
//!     output is absent from a coinbase transaction that is past the
//!     Canopy activation height and within the ZIP-214 height range.
//!
//! The `FundingStreamNotFound` variant only exists in app-12, so this file
//! does not compile against app-11.
//!
//! The driver confirms the full `SubsidyError` set by exhaustive, wildcard-free
//! pattern matching, to catch any accidental removal of an existing variant.
//! `SubsidyError` is not `#[non_exhaustive]`, so a wildcard-free `match`
//! written here -- in a separate integration-test crate -- stops compiling if a
//! variant is renamed or dropped. That is legitimate because `TEST_INTERFACE.md`
//! `§5` gives the enum body in full.
//!
//! `BlockError` is deliberately **not** matched exhaustively. Its pre-existing
//! variants are inherited surface that no step's documents enumerate, and a
//! candidate cannot act on a step-11 constraint that first appears at step 12.
//! Only the variant this step adds -- `TooManyTransparentSignatureOperations`,
//! specified in full in `§4` and `§5` -- is pinned, by construction and
//! `Display`, in `too_many_sigops_display_includes_count` below.

use zebra_consensus::error::{BlockError, SubsidyError};

use zebra_chain::block::{Height, Hash};

/// `SubsidyError::FundingStreamNotFound` is present, equates to itself,
/// and its Display message mentions "funding stream".
#[test]
fn funding_stream_not_found_variant_exists() {
    let err = SubsidyError::FundingStreamNotFound;
    assert_eq!(err, SubsidyError::FundingStreamNotFound);
    let msg = err.to_string();
    assert!(
        msg.to_lowercase().contains("funding"),
        "FundingStreamNotFound message must mention 'funding', got: {:?}", msg
    );
}

/// All three `SubsidyError` variants are accessible by name.
///
/// The `match` below is **exhaustive over the enum**: it names every variant
/// and has no wildcard arm, so *adding* a fourth variant is a compile error
/// here, keeping the test in sync with the enum. Removing one instead makes
/// the corresponding `SubsidyError::X` path fail to resolve, which is also a
/// compile error. Both directions of drift are therefore caught at build time.
#[test]
fn subsidy_error_exhaustive_match() {
    let variants: &[SubsidyError] = &[
        SubsidyError::NoCoinbase,
        SubsidyError::FoundersRewardNotFound,
        SubsidyError::FundingStreamNotFound,
    ];
    for v in variants {
        // Exhaustive, wildcard-free match: a new variant breaks compilation.
        match v {
            SubsidyError::NoCoinbase
            | SubsidyError::FoundersRewardNotFound
            | SubsidyError::FundingStreamNotFound => {}
        }
        // `Copy` is a documented derive (TEST_INTERFACE.md §5); dereferencing
        // a shared reference exercises it. Dropping `Copy` breaks this line.
        let copied: SubsidyError = *v;
        // Check each variant round-trips through Debug and is equal to itself.
        let _ = format!("{:?}", copied);
        assert_eq!(copied, *v);
    }
}

/// `SubsidyError` implements `std::error::Error` (via thiserror), so it can
/// be wrapped in a `Box<dyn std::error::Error>`.
#[test]
fn subsidy_error_implements_std_error() {
    let err: Box<dyn std::error::Error> = Box::new(SubsidyError::FundingStreamNotFound);
    let _ = err.to_string();
}

/// `SubsidyError` variants have distinct Display strings.
#[test]
fn subsidy_error_variants_have_distinct_messages() {
    let msgs = [
        SubsidyError::NoCoinbase.to_string(),
        SubsidyError::FoundersRewardNotFound.to_string(),
        SubsidyError::FundingStreamNotFound.to_string(),
    ];
    for i in 0..msgs.len() {
        for j in (i + 1)..msgs.len() {
            assert_ne!(
                msgs[i], msgs[j],
                "SubsidyError variants {} and {} must have different messages",
                i, j
            );
        }
    }
}

/// `BlockError::TooManyTransparentSignatureOperations` carries `height`,
/// `hash`, and `legacy_sigop_count`; the Display includes the count value.
#[test]
fn too_many_sigops_display_includes_count() {
    let count: u64 = 99_999;
    let err = BlockError::TooManyTransparentSignatureOperations {
        height: Height(500_000),
        hash: Hash([0xabu8; 32]),
        legacy_sigop_count: count,
    };
    let msg = err.to_string();
    assert!(
        msg.contains("99999"),
        "TooManyTransparentSignatureOperations must include the sigop count in its message, got: {:?}", msg
    );
}

/// `BlockError::TooManyTransparentSignatureOperations` exists with `height`,
/// `hash`, and `legacy_sigop_count` fields, and its Display mentions the
/// sigop count.
///
/// Moved here from `zip212-orchard-flags` because this variant is capability 6
/// (the sigops rule), not capability 3 or 4; it belongs with the other error
/// variant shape tests, not with the ZIP-212 and Orchard flags tests.
#[test]
fn too_many_transparent_sigops_variant() {
    let height = Height(123_456);
    let hash = Hash([0u8; 32]);
    let err = BlockError::TooManyTransparentSignatureOperations {
        height,
        hash,
        legacy_sigop_count: 20_001,
    };
    let msg = err.to_string();
    assert!(
        msg.contains("20001") || msg.contains("20_001"),
        "TooManyTransparentSignatureOperations message must contain the sigop count, got: {:?}", msg
    );
}
