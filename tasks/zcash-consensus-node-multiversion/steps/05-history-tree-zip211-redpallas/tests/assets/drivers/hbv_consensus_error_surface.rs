//! Peripheral breadth check: `zebra_consensus::error` grows this milestone's
//! three new consensus rules.
//!
//! Harbor verifier driver -- injected into `zebra-consensus/tests/` at
//! grading time. Public API only.
//!
//! Three of this milestone's headline rules are implemented inside
//! `zebra-consensus`, whose `transaction` and `primitives` modules are both
//! declared `mod` rather than `pub mod` in `zebra-consensus/src/lib.rs`. The
//! check functions themselves are therefore unreachable from an integration
//! test, which is a separate crate. What *is* reachable is `pub mod error`,
//! and every consensus rule in this codebase is required to have exactly one
//! enumerated variant there -- the module's own doc comment says so: "Each
//! error variant corresponds to a consensus rule, so enumerating all possible
//! verification failures enumerates the consensus rules we implement".
//!
//! So this group scores the rules' public *surface*: that the three variants
//! exist under the right names, carry the right payloads, display the rule in
//! English, and compose through `BlockError`'s `#[from]` wrapper. The rules
//! this milestone adds are:
//!
//!   * `UnsupportedByNetworkUpgrade(u32, NetworkUpgrade)` -- a transaction
//!     version that the block's network upgrade does not accept. This is the
//!     variant that rejects a v5 transaction before NU5 activates, which is
//!     why it carries the version *and* the upgrade rather than being a unit
//!     variant.
//!   * `DisabledAddToSproutPool` -- ZIP-211. From Canopy onward, `vpub_old`
//!     must be zero in every JoinSplit, so no new value can enter the Sprout
//!     value pool.
//!   * `RedPallas(redpallas::Error)` -- the Orchard binding signature rule,
//!     the consensus-visible end of this milestone's batched RedPallas
//!     verifier. It is a separate variant from the existing `RedJubjub`
//!     (Sapling) one, and the two Sapling/Sprout messages were reworded in
//!     the same change to name their pool, so a validator cannot report a
//!     Sapling failure for an Orchard one and go unnoticed.

use zebra_chain::parameters::NetworkUpgrade;
use zebra_chain::primitives::{redjubjub, redpallas};
use zebra_consensus::error::{BlockError, TransactionError};

// ------------------------------------------------------------------- tests

#[test]
fn the_three_new_variants_are_public_debug_and_pairwise_distinct() {
    let unsupported = TransactionError::UnsupportedByNetworkUpgrade(5, NetworkUpgrade::Canopy);
    let sprout = TransactionError::DisabledAddToSproutPool;
    let orchard_binding = TransactionError::RedPallas(redpallas::Error::InvalidSignature);

    // Debug is derived and must name the variant.
    assert_eq!(
        format!("{:?}", unsupported),
        "UnsupportedByNetworkUpgrade(5, Canopy)"
    );
    assert_eq!(format!("{:?}", sprout), "DisabledAddToSproutPool");

    // PartialEq is derived: each is equal to itself...
    assert_eq!(
        unsupported,
        TransactionError::UnsupportedByNetworkUpgrade(5, NetworkUpgrade::Canopy)
    );
    assert_eq!(sprout, TransactionError::DisabledAddToSproutPool);
    assert_eq!(
        orchard_binding,
        TransactionError::RedPallas(redpallas::Error::InvalidSignature)
    );

    // ...and distinct from one another, and from the older neighbours each is
    // most likely to be confused with. `WrongVersion` is the blanket
    // "version must be >= 4" rule that `UnsupportedByNetworkUpgrade` refines;
    // `RedJubjub` is the Sapling binding signature rule that `RedPallas` is
    // the Orchard counterpart of.
    assert_ne!(unsupported, sprout);
    assert_ne!(unsupported, orchard_binding);
    assert_ne!(sprout, orchard_binding);
    assert_ne!(unsupported, TransactionError::WrongVersion);
    assert_ne!(sprout, TransactionError::BadBalance);
    assert_ne!(
        orchard_binding,
        TransactionError::Groth16,
        "the Orchard binding signature rule must not be reported as a Groth16 proof failure"
    );
}

#[test]
fn the_three_new_variants_display_the_rule_they_enforce() {
    assert_eq!(
        TransactionError::UnsupportedByNetworkUpgrade(5, NetworkUpgrade::Canopy).to_string(),
        "transaction version 5 not supported by the network upgrade Canopy",
        "the version/upgrade rejection's displayed message has changed"
    );
    assert_eq!(
        TransactionError::DisabledAddToSproutPool.to_string(),
        "adding to the sprout pool is disabled after Canopy",
        "the ZIP-211 rule's displayed message has changed"
    );
    assert_eq!(
        TransactionError::RedPallas(redpallas::Error::InvalidSignature).to_string(),
        "Orchard bindingSig MUST represent a valid signature under the transaction binding \
         validating key bvk of SigHash",
        "the Orchard binding signature rule's displayed message has changed"
    );

    // The two older binding/signature rules were reworded in the same change
    // so that each names its own pool. A message that does not say which pool
    // failed is the defect this checks for.
    let sapling = TransactionError::RedJubjub(redjubjub::Error::InvalidSignature).to_string();
    assert!(
        sapling.starts_with("Sapling bindingSig"),
        "the Sapling binding signature rule must name its pool, got {:?}",
        sapling
    );
}

#[test]
fn unsupported_by_network_upgrade_distinguishes_the_version_from_the_upgrade() {
    // Both payload fields are significant. A v5 transaction rejected under
    // Canopy and a v4 transaction rejected under Canopy are different
    // failures, and so are the same version rejected under two different
    // upgrades -- this is the variant that has to say "v5 is not allowed
    // *yet*", so collapsing either field would lose the rule.
    let v5_at_canopy = TransactionError::UnsupportedByNetworkUpgrade(5, NetworkUpgrade::Canopy);
    let v4_at_canopy = TransactionError::UnsupportedByNetworkUpgrade(4, NetworkUpgrade::Canopy);
    let v5_at_nu5 = TransactionError::UnsupportedByNetworkUpgrade(5, NetworkUpgrade::Nu5);

    assert_ne!(v5_at_canopy, v4_at_canopy, "the version field is ignored");
    assert_ne!(v5_at_canopy, v5_at_nu5, "the network upgrade field is ignored");

    // Display interpolates the version with `{0}` and the upgrade with
    // `{1:?}`, so both reach the message a node operator reads.
    let message = v5_at_canopy.to_string();
    assert!(
        message.contains('5') && message.contains("Canopy"),
        "the message must carry both payload fields, got {:?}",
        message
    );
    assert!(
        v5_at_nu5.to_string().contains("Nu5"),
        "the upgrade is not rendered with its Debug name"
    );
    assert!(
        !v4_at_canopy.to_string().contains('5'),
        "the version rendered in the message is not the one in the payload"
    );
}

#[test]
fn every_new_variant_propagates_through_block_error_transaction() {
    // `BlockError::Transaction` carries a `TransactionError` via `#[from]`,
    // and its own Display is a fixed wrapper string. A new rule that cannot
    // propagate to block verification is a dead end, so check each one makes
    // the trip and keeps its identity.
    for original in vec![
        TransactionError::UnsupportedByNetworkUpgrade(5, NetworkUpgrade::Canopy),
        TransactionError::DisabledAddToSproutPool,
        TransactionError::RedPallas(redpallas::Error::InvalidSignature),
    ] {
        let expected = format!("{:?}", original);
        let wrapped: BlockError = original.into();

        assert_eq!(
            wrapped.to_string(),
            "block contains invalid transactions",
            "BlockError::Transaction's wrapper message has changed"
        );
        match wrapped {
            BlockError::Transaction(inner) => assert_eq!(
                format!("{:?}", inner),
                expected,
                "the wrapped error lost its identity"
            ),
            other => panic!("expected BlockError::Transaction({}), got {:?}", expected, other),
        }
    }
}
