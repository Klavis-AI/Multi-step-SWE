//! Peripheral breadth check: `zebra_consensus::error` grows a new rule.
//!
//! Harbor verifier driver -- injected into `zebra-consensus/tests/` at
//! grading time. Public API only.
//!
//! This milestone gives the Orchard shielded pool a `flags` byte on each V5
//! `Action` batch, one bit of which is `ENABLE_SPENDS_ORCHARD`. Consensus
//! forbids a coinbase transaction from setting it (a coinbase transaction may
//! not spend anything, shielded or not), and `zebra_consensus::error`
//! documents that as a new unit variant of the crate's own `TransactionError`,
//! independent of whatever `zebra-chain` does with the bit itself. This group
//! only checks that the crate's public error surface carries the new rule
//! correctly -- name, message text, `Debug`/`PartialEq`, and how it composes
//! with `BlockError`'s existing `#[from]` wrapping -- not the block/transaction
//! validation pipeline that will eventually raise it.

use zebra_consensus::error::{BlockError, TransactionError};

#[test]
fn the_variant_is_public_debug_partial_eq_and_distinct_from_its_neighbours() {
    let flag = TransactionError::CoinbaseHasEnableSpendsOrchard;

    // Debug is derived and must name the variant.
    assert_eq!(format!("{:?}", flag), "CoinbaseHasEnableSpendsOrchard");

    // PartialEq is derived: equal to itself...
    assert_eq!(flag, TransactionError::CoinbaseHasEnableSpendsOrchard);

    // ...and distinct from the other coinbase-shielded-material rules it sits
    // beside in the enum, so a validator cannot report the wrong one and have
    // this group fail to notice.
    assert_ne!(flag, TransactionError::CoinbaseHasSpend);
    assert_ne!(flag, TransactionError::CoinbaseHasOutputPreHeartwood);
    assert_ne!(flag, TransactionError::CoinbaseHasJoinSplit);
}

#[test]
fn the_display_message_states_the_rule_in_english() {
    let flag = TransactionError::CoinbaseHasEnableSpendsOrchard;
    assert_eq!(
        flag.to_string(),
        "coinbase transaction MUST NOT have the EnableSpendsOrchard flag set",
        "the error variant's displayed message has changed"
    );
}

#[test]
fn wrapping_the_variant_in_block_error_keeps_the_transaction_wrapper_message() {
    // BlockError::Transaction carries a TransactionError via #[from], and its
    // own Display is a fixed wrapper string -- confirm the new variant flows
    // through that existing composition correctly rather than being a
    // dead-end type that nothing can propagate.
    let wrapped: BlockError = TransactionError::CoinbaseHasEnableSpendsOrchard.into();

    assert_eq!(
        wrapped.to_string(),
        "block contains invalid transactions",
        "BlockError::Transaction's wrapper message has changed"
    );
    match wrapped {
        BlockError::Transaction(inner_again) => {
            assert_eq!(
                inner_again,
                TransactionError::CoinbaseHasEnableSpendsOrchard,
                "the wrapped error lost its identity"
            );
        }
        other => panic!(
            "expected BlockError::Transaction(CoinbaseHasEnableSpendsOrchard), got {:?}",
            other
        ),
    }
}
