//! The four `TransactionError` variants that name an internal double-spend.
//!
//! Harbor verifier driver -- injected into `zebra-consensus/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! `spend_conflicts()` itself lives in `zebra_consensus::transaction::check`,
//! a private module, so a graded test cannot call it.  What *is* reachable is
//! the error type it returns: `zebra_consensus::error::TransactionError` is a
//! public enum in a public module, and at v1.0.0-beta.0 it gained four
//! variants, one per shielded/transparent pool:
//!
//!   DuplicateTransparentSpend(transparent::OutPoint)
//!   DuplicateSproutNullifier(sprout::Nullifier)
//!   DuplicateSaplingNullifier(sapling::Nullifier)
//!   DuplicateOrchardNullifier(orchard::Nullifier)
//!
//! Each carries the offending item, and each has a `thiserror` `Display`
//! string that names the pool it came from.  A refactor that collapses the
//! four into one variant, or that loses the payload, is caught here.
//!
//! This group scores the variants as values: their construction, their public
//! payload types, their `Display` text, and their `PartialEq`/`Debug` behaviour.
//! The duplicate-detection arithmetic that produces them runs inside the
//! unconstructible transaction verifier and is not scored.
//!
//! Negative direction: none of these four variants exists at the predecessor
//! tag -- `app-10`'s `TransactionError` ends at `IncorrectFee`.  A driver
//! naming them therefore fails to compile against app-10, which is the
//! strongest possible negative direction.

use std::convert::TryFrom;

use zebra_chain::orchard;
use zebra_chain::sapling;
use zebra_chain::sprout;
use zebra_chain::transparent;
use zebra_consensus::error::TransactionError;

// ----------------------------------------------------------------- helpers

/// A 32-byte transaction hash, built from a seed so that different seeds give
/// different outpoints.  `transaction::Hash`'s field is public, so the value
/// is constructed directly rather than parsed from hex.
fn tx_hash(seed: u8) -> zebra_chain::transaction::Hash {
    let mut bytes = [0u8; 32];
    for (i, b) in bytes.iter_mut().enumerate() {
        *b = seed.wrapping_add(i as u8);
    }
    zebra_chain::transaction::Hash(bytes)
}

/// A sprout nullifier from a single byte pattern.
fn sprout_nullifier(seed: u8) -> sprout::Nullifier {
    let mut bytes = [0u8; 32];
    bytes[0] = seed;
    sprout::Nullifier(bytes)
}

/// A sapling nullifier from a single byte pattern.
fn sapling_nullifier(seed: u8) -> sapling::Nullifier {
    let mut bytes = [0u8; 32];
    bytes[0] = seed;
    sapling::Nullifier(bytes)
}

/// A valid orchard nullifier: the little-endian encoding of a small integer is
/// a canonical `pallas::Base` encoding, so the low byte alone is enough.
fn orchard_nullifier(seed: u8) -> orchard::Nullifier {
    let mut bytes = [0u8; 32];
    bytes[0] = seed;
    orchard::Nullifier::try_from(bytes).expect("a small integer is a valid pallas::Base")
}

// ------------------------------------------------------------------- tests

#[test]
fn transparent_double_spend_carries_the_outpoint() {
    let outpoint = transparent::OutPoint {
        hash: tx_hash(0x11),
        index: 3,
    };
    let err = TransactionError::DuplicateTransparentSpend(outpoint);

    // The payload must be the outpoint that was passed in -- public, Copy,
    // and compared by value.
    match err {
        TransactionError::DuplicateTransparentSpend(inner) => {
            assert_eq!(inner, outpoint, "the variant must carry the outpoint itself");
            assert_eq!(inner.index, 3, "the outpoint's index must survive");
            assert_eq!(
                inner.hash,
                tx_hash(0x11),
                "the outpoint's hash must survive"
            );
        }
        other => panic!("expected DuplicateTransparentSpend, got {:?}", other),
    }
}

#[test]
fn sprout_double_spend_carries_the_nullifier() {
    let nullifier = sprout_nullifier(0x22);
    let err = TransactionError::DuplicateSproutNullifier(nullifier);

    match err {
        TransactionError::DuplicateSproutNullifier(inner) => {
            assert_eq!(inner, nullifier, "the variant must carry the nullifier itself");
        }
        other => panic!("expected DuplicateSproutNullifier, got {:?}", other),
    }
}

#[test]
fn sapling_double_spend_carries_the_nullifier() {
    let nullifier = sapling_nullifier(0x33);
    let err = TransactionError::DuplicateSaplingNullifier(nullifier);

    match err {
        TransactionError::DuplicateSaplingNullifier(inner) => {
            assert_eq!(inner, nullifier, "the variant must carry the nullifier itself");
        }
        other => panic!("expected DuplicateSaplingNullifier, got {:?}", other),
    }
}

#[test]
fn orchard_double_spend_carries_the_nullifier() {
    let nullifier = orchard_nullifier(0x44);
    let err = TransactionError::DuplicateOrchardNullifier(nullifier);

    match err {
        TransactionError::DuplicateOrchardNullifier(inner) => {
            assert_eq!(inner, nullifier, "the variant must carry the nullifier itself");
        }
        other => panic!("expected DuplicateOrchardNullifier, got {:?}", other),
    }
}

#[test]
fn the_four_variants_are_distinct_and_compare_by_value() {
    // Four errors built from the same seed byte, one per pool, are four
    // different values.  A candidate who collapses the variants into one
    // cannot satisfy this.
    let a = TransactionError::DuplicateTransparentSpend(transparent::OutPoint {
        hash: tx_hash(0x55),
        index: 0,
    });
    let b = TransactionError::DuplicateSproutNullifier(sprout_nullifier(0x55));
    let c = TransactionError::DuplicateSaplingNullifier(sapling_nullifier(0x55));
    let d = TransactionError::DuplicateOrchardNullifier(orchard_nullifier(0x55));

    assert_ne!(a, b);
    assert_ne!(b, c);
    assert_ne!(c, d);
    assert_ne!(a, d);

    // Equality is structural: rebuilding the same payload gives an equal value.
    let a2 = TransactionError::DuplicateTransparentSpend(transparent::OutPoint {
        hash: tx_hash(0x55),
        index: 0,
    });
    let b2 = TransactionError::DuplicateSproutNullifier(sprout_nullifier(0x55));
    let c2 = TransactionError::DuplicateSaplingNullifier(sapling_nullifier(0x55));
    let d2 = TransactionError::DuplicateOrchardNullifier(orchard_nullifier(0x55));

    assert_eq!(a, a2, "transparent variant must compare by value");
    assert_eq!(b, b2, "sprout variant must compare by value");
    assert_eq!(c, c2, "sapling variant must compare by value");
    assert_eq!(d, d2, "orchard variant must compare by value");

    // Different payloads under the same variant are different errors.
    let b3 = TransactionError::DuplicateSproutNullifier(sprout_nullifier(0x56));
    assert_ne!(b, b3, "two different nullifiers must not compare equal");
}

#[test]
fn transparent_display_names_the_pool() {
    let outpoint = transparent::OutPoint {
        hash: tx_hash(0x66),
        index: 7,
    };
    let err = TransactionError::DuplicateTransparentSpend(outpoint);
    let text = err.to_string();

    assert!(
        text.starts_with("transparent double-spend:"),
        "Display must say which pool the conflict is in; got {:?}",
        text
    );
    assert!(
        text.contains("is spent twice"),
        "Display must describe the conflict; got {:?}",
        text
    );
}

#[test]
fn sprout_display_names_the_pool() {
    let err = TransactionError::DuplicateSproutNullifier(sprout_nullifier(0x77));
    let text = err.to_string();

    assert!(
        text.starts_with("sprout double-spend:"),
        "Display must say which pool the conflict is in; got {:?}",
        text
    );
    assert!(
        text.contains("duplicate nullifier"),
        "Display must describe the conflict; got {:?}",
        text
    );
}

#[test]
fn sapling_display_names_the_pool() {
    let err = TransactionError::DuplicateSaplingNullifier(sapling_nullifier(0x88));
    let text = err.to_string();

    assert!(
        text.starts_with("sapling double-spend:"),
        "Display must say which pool the conflict is in; got {:?}",
        text
    );
    assert!(
        text.contains("duplicate nullifier"),
        "Display must describe the conflict; got {:?}",
        text
    );
}

#[test]
fn orchard_display_names_the_pool() {
    let err = TransactionError::DuplicateOrchardNullifier(orchard_nullifier(0x99));
    let text = err.to_string();

    assert!(
        text.starts_with("orchard double-spend:"),
        "Display must say which pool the conflict is in; got {:?}",
        text
    );
    assert!(
        text.contains("duplicate nullifier"),
        "Display must describe the conflict; got {:?}",
        text
    );
}

#[test]
fn the_four_display_strings_are_pairwise_different() {
    // Each pool's message is distinguishable, so a log line says which kind of
    // conflict was found without further context.
    let a = TransactionError::DuplicateTransparentSpend(transparent::OutPoint {
        hash: tx_hash(0xaa),
        index: 0,
    })
    .to_string();
    let b = TransactionError::DuplicateSproutNullifier(sprout_nullifier(0xaa)).to_string();
    let c = TransactionError::DuplicateSaplingNullifier(sapling_nullifier(0xaa)).to_string();
    let d = TransactionError::DuplicateOrchardNullifier(orchard_nullifier(0xaa)).to_string();

    assert_ne!(a, b);
    assert_ne!(a, c);
    assert_ne!(a, d);
    assert_ne!(b, c);
    assert_ne!(b, d);
    assert_ne!(c, d);
}

#[test]
fn the_variants_are_debug_printable() {
    // These errors reach logs, so every variant must satisfy Debug.  The
    // assertion is that formatting does not panic and produces something.
    let cases = vec![
        TransactionError::DuplicateTransparentSpend(transparent::OutPoint {
            hash: tx_hash(0xbb),
            index: 1,
        }),
        TransactionError::DuplicateSproutNullifier(sprout_nullifier(0xbb)),
        TransactionError::DuplicateSaplingNullifier(sapling_nullifier(0xbb)),
        TransactionError::DuplicateOrchardNullifier(orchard_nullifier(0xbb)),
    ];

    for err in cases {
        let rendered = format!("{:?}", err);
        assert!(
            !rendered.is_empty(),
            "Debug rendering must not be empty"
        );
        // Debug output names the variant.
        assert!(
            rendered.contains("Duplicate"),
            "Debug rendering must name the variant; got {:?}",
            rendered
        );
    }
}

#[test]
fn earlier_variants_still_exist_beside_the_new_ones() {
    // The four additions must not have displaced what was already there.
    // `IncorrectFee` is the variant step 10 added; it must still be reachable
    // and still render its own message.
    let err = TransactionError::IncorrectFee;
    assert_eq!(
        err.to_string(),
        "could not calculate the transaction fee",
        "the existing IncorrectFee variant must keep its Display string"
    );

    // And a handful of long-standing variants must still be constructible.
    let _ = TransactionError::NoInputs;
    let _ = TransactionError::NoOutputs;
    let _ = TransactionError::CoinbasePosition;
    let _ = TransactionError::WrongVersion;
}
