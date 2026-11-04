//! The consensus crate's public surface, as this milestone leaves it.
//!
//! Harbor verifier driver -- injected into `zebra-consensus/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! `zebra-consensus` is where the primitives this milestone adds are eventually
//! consumed, but almost all of that machinery is private: the batched signature
//! verification services live in a private `primitives` module and are reached
//! only through `tower` services that need a runtime and proving parameters.
//! What *is* public, and what this driver pins, is the crate's contract with
//! the rest of the node: the two checkpoint budget constants, the default
//! configuration, and the error enumeration that names one consensus rule per
//! variant -- including the two signature-verification rules whose primitives
//! this milestone rewrites.
//!
//! This is a breadth check, not the milestone. It should stay cheap.

use zebra_chain::block::MAX_BLOCK_BYTES;
use zebra_chain::primitives::{ed25519, redjubjub};
use zebra_consensus::error::{BlockError, SubsidyError, TransactionError};
use zebra_consensus::{
    BoxError, Config, MAX_CHECKPOINT_BYTE_COUNT, MAX_CHECKPOINT_HEIGHT_GAP,
};

#[test]
fn the_two_checkpoint_budgets_are_the_published_constants() {
    // A checkpoint spans at most this many blocks, chosen to fit inside a
    // single 500-hash `FindBlocks` response with room to spare.
    let gap: usize = MAX_CHECKPOINT_HEIGHT_GAP;
    assert_eq!(gap, 400, "MAX_CHECKPOINT_HEIGHT_GAP must be 400 blocks");

    // ...and at most this many serialized bytes, which bounds the memory a
    // single checkpoint's worth of queued blocks can occupy.
    let bytes: u64 = MAX_CHECKPOINT_BYTE_COUNT;
    assert_eq!(
        bytes,
        32 * 1024 * 1024,
        "MAX_CHECKPOINT_BYTE_COUNT must be 32 MiB"
    );
    assert_eq!(bytes, 33_554_432);
}

#[test]
fn the_byte_budget_is_the_binding_limit_not_the_height_gap() {
    // 400 maximum-size blocks would be 800 MB, so for any chain of large
    // blocks the byte budget closes a checkpoint long before the height gap
    // does. Both limits therefore have to exist; neither implies the other.
    let worst_case = (MAX_CHECKPOINT_HEIGHT_GAP as u64) * MAX_BLOCK_BYTES;
    assert!(
        worst_case > MAX_CHECKPOINT_BYTE_COUNT,
        "a height gap of {} maximum-size blocks ({} bytes) must be able to exceed the {}-byte \
         budget, or the byte budget is dead code",
        MAX_CHECKPOINT_HEIGHT_GAP,
        worst_case,
        MAX_CHECKPOINT_BYTE_COUNT
    );
    // And a single block always fits, so no block can be unverifiable.
    assert!(MAX_BLOCK_BYTES < MAX_CHECKPOINT_BYTE_COUNT);
}

#[test]
fn checkpoint_sync_is_off_by_default() {
    let config = Config::default();
    assert_eq!(
        config.checkpoint_sync, false,
        "checkpoint_sync defaults to false: the mandatory checkpoints are always applied, the \
         optional post-Canopy ones are opt-in"
    );
    // The config is cloneable and printable, because the node logs it.
    let clone = config.clone();
    assert_eq!(clone.checkpoint_sync, config.checkpoint_sync);
    assert!(!format!("{:?}", config).is_empty());
}

#[test]
fn a_failed_ed25519_verification_is_its_own_transaction_error() {
    // The JoinSplit signature rule. `From` must carry the underlying error
    // through, so that a batch rejection can be attributed to a rule.
    let converted: TransactionError = ed25519::Error::InvalidSignature.into();
    assert_eq!(
        converted,
        TransactionError::Ed25519(ed25519::Error::InvalidSignature)
    );

    let malformed: TransactionError = ed25519::Error::MalformedPublicKey.into();
    assert_eq!(
        malformed,
        TransactionError::Ed25519(ed25519::Error::MalformedPublicKey)
    );
    assert_ne!(
        converted, malformed,
        "the underlying ed25519 error must survive the conversion"
    );
}

#[test]
fn a_boxed_redjubjub_error_downcasts_back_into_a_transaction_error() {
    // Binding signatures are verified by a `tower` service, which erases the
    // error type; the conversion back has to recover it.
    let boxed: BoxError = Box::new(redjubjub::Error::InvalidSignature);
    assert_eq!(
        TransactionError::from(boxed),
        TransactionError::RedJubjub(redjubjub::Error::InvalidSignature)
    );

    // Anything else must not be silently turned into a signature failure.
    let unrelated: BoxError = Box::new(std::io::Error::new(
        std::io::ErrorKind::Other,
        "not a signature error",
    ));
    match TransactionError::from(unrelated) {
        TransactionError::InternalDowncastError(_) => {}
        other => panic!(
            "an unrelated boxed error must not become a signature failure, got {:?}",
            other
        ),
    }
}

#[test]
fn every_variant_names_one_consensus_rule() {
    // The small-order check on a Spend's `cv` and `rk` is a rule in its own
    // right -- it is not a signature failure and not a proof failure -- so it
    // has a variant of its own.
    assert_ne!(
        TransactionError::SmallOrder,
        TransactionError::Ed25519(ed25519::Error::InvalidSignature)
    );
    assert_ne!(TransactionError::SmallOrder, TransactionError::Groth16);
    assert_ne!(TransactionError::SmallOrder, TransactionError::BadBalance);

    // Subsidy failures nest: a block error can always be traced to the
    // transaction rule that produced it.
    assert_eq!(
        BlockError::from(SubsidyError::NoCoinbase),
        BlockError::Transaction(TransactionError::Subsidy(SubsidyError::NoCoinbase))
    );
    assert_eq!(
        BlockError::from(TransactionError::CoinbasePosition),
        BlockError::Transaction(TransactionError::CoinbasePosition)
    );
    assert_ne!(BlockError::NoTransactions, BlockError::DuplicateTransaction);
}
