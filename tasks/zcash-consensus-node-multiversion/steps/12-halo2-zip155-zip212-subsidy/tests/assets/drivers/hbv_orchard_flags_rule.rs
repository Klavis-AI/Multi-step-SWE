//! Orchard flags rule -- new at app-12.
//!
//! Harbor verifier driver -- injected into `zebra-consensus/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! Capability 4: `Transaction::has_enough_orchard_flags` (the `zebra-chain`
//! predicate) and `zebra_consensus::transaction::check::has_enough_orchard_flags`
//! (the consensus wrapper) are both new at app-12. The rule: a V5 transaction
//! that carries Orchard actions must have at least one of ENABLE_SPENDS or
//! ENABLE_OUTPUTS set. Pre-V5 transactions and V5 transactions with no Orchard
//! actions are exempt via early returns.
//!
//! All three branches of the predicate are asserted here alongside the consensus
//! wrapper. The error variant `TransactionError::NotEnoughFlags` is also pinned.
//! This driver lives in `zebra-consensus/tests/` -- not `zebra-chain/tests/` --
//! because building a V5 transaction that carries an Orchard action requires
//! `zebra-chain`'s `proptest-impl` feature, which is enabled only for
//! `zebra-consensus`'s tests. Without it `insert_fake_orchard_shielded_data` is
//! not in scope.

use zebra_chain::block::{Height};
use zebra_chain::orchard::shielded_data::Flags;
use zebra_chain::parameters::NetworkUpgrade;
use zebra_chain::serialization::ZcashDeserialize;
use zebra_chain::transaction::arbitrary::insert_fake_orchard_shielded_data;
use zebra_chain::transaction::{LockTime, Transaction};
use zebra_consensus::error::TransactionError;
use zebra_consensus::transaction::check;

use std::io::Cursor;
use std::path::PathBuf;

// ------------------------------------------------------------------ helpers

fn corpus_block_coinbase(name: &str) -> Transaction {
    let fixtures = PathBuf::from(
        std::env::var("HBV_FIXTURES")
            .expect("HBV_FIXTURES must point at the shipped fixture directory"),
    );
    let hex = std::fs::read_to_string(fixtures.join("blocks").join(name))
        .unwrap_or_else(|e| panic!("fixture {} is readable: {}", name, e));
    let raw = hex::decode(hex.trim()).expect("fixture hex decodes");
    let block = zebra_chain::block::Block::zcash_deserialize(Cursor::new(&raw[..]))
        .expect("fixture parses");
    block.transactions[0].as_ref().clone()
}

/// A V5 transaction with no shielded data of any kind.
fn empty_v5() -> Transaction {
    Transaction::V5 {
        network_upgrade: NetworkUpgrade::Nu5,
        lock_time: LockTime::Height(Height(0)),
        expiry_height: Height(0),
        inputs: Vec::new(),
        outputs: Vec::new(),
        sapling_shielded_data: None,
        orchard_shielded_data: None,
    }
}

/// A V5 transaction carrying exactly one dummy Orchard action, with `flags`
/// set to the caller's value.
fn v5_with_one_orchard_action(flags: Flags) -> Transaction {
    let mut tx = empty_v5();
    let shielded_data = insert_fake_orchard_shielded_data(&mut tx);
    shielded_data.flags = flags;
    tx
}

// `has_enough_orchard_flags` signature (compile-time pin).
//
// Never called. The tests below exercise the behaviour; this pins the shape, so
// that a rename, a change of receiver, or a different return type is a compile
// error rather than a silently-passing test.
#[allow(dead_code)]
fn assert_has_enough_orchard_flags_signature() {
    let _f: fn(&Transaction) -> bool = Transaction::has_enough_orchard_flags;
}

// ---- error-variant shape test ----

/// `TransactionError::NotEnoughFlags` exists and its Display mentions
/// "orchard" (per the error annotation about the flag rule).
#[test]
fn not_enough_flags_variant() {
    let err = TransactionError::NotEnoughFlags;
    assert_eq!(err, TransactionError::NotEnoughFlags);
    let msg = err.to_string();
    assert!(
        msg.to_lowercase().contains("orchard"),
        "NotEnoughFlags message must mention 'orchard', got: {:?}", msg
    );
}

// ---- the Orchard flags rule, all three predicate branches ----

/// Pre-V5 transactions are exempt from the Orchard flag rule.
///
/// This is the `self.version() < 5` early return. An implementation that
/// omits it falls through to reading `orchard_flags()`, which is `None` for a
/// pre-V5 transaction, and `Flags::empty().intersects(..)` is `false` -- so
/// dropping the early return turns this assertion red.
#[test]
fn has_enough_orchard_flags_true_for_pre_v5_transaction() {
    let tx = corpus_block_coinbase("main-0000000.hex");

    assert!(
        tx.version() < 5,
        "the genesis coinbase must be pre-V5 for this test to exercise the early \
         return, but its version is {}",
        tx.version()
    );
    assert!(
        tx.has_enough_orchard_flags(),
        "a pre-V5 transaction is not subject to the Orchard flag rule"
    );
    assert_eq!(
        check::has_enough_orchard_flags(&tx),
        Ok(()),
        "the check function must accept a pre-V5 transaction"
    );
}

/// A V5 transaction with zero Orchard actions is exempt.
///
/// This is the `self.orchard_actions().count() == 0` early return. An
/// implementation that asked "are the enable bits set?" instead -- for example
/// `self.orchard_flags().map_or(false, |f| f.intersects(..))` -- returns
/// `false` here because `orchard_flags()` is `None` when there is no shielded
/// data.
#[test]
fn has_enough_orchard_flags_true_for_v5_without_orchard_actions() {
    let tx = empty_v5();

    assert_eq!(tx.version(), 5, "the fixture must be a V5 transaction");
    assert_eq!(
        tx.orchard_actions().count(),
        0,
        "the fixture must carry no Orchard actions"
    );
    assert!(
        tx.orchard_flags().is_none(),
        "with no shielded data there are no flags to read"
    );
    assert!(
        tx.has_enough_orchard_flags(),
        "a V5 transaction with zero Orchard actions is not subject to the flag rule"
    );
    assert_eq!(
        check::has_enough_orchard_flags(&tx),
        Ok(()),
        "the check function must accept it as well"
    );
}

/// A V5 transaction with one Orchard action and no enable bits must be rejected.
///
/// This is the branch no early return covers, and the one a stub cannot fake.
/// The predicate and the consensus wrapper are both asserted so that a wrapper
/// which never consults the predicate -- or a predicate hardwired to `true` --
/// is caught here.
#[test]
fn flags_rule_rejects_orchard_actions_with_no_enable_bits() {
    let tx = v5_with_one_orchard_action(Flags::empty());

    assert_eq!(
        tx.orchard_actions().count(),
        1,
        "the fixture must carry exactly one Orchard action for the rule to bite"
    );
    assert_eq!(
        tx.orchard_flags(),
        Some(Flags::empty()),
        "the fixture must have both enable bits clear"
    );
    assert!(
        !tx.has_enough_orchard_flags(),
        "V5 + one Orchard action + no enable bit must not satisfy the flag rule"
    );
    assert_eq!(
        check::has_enough_orchard_flags(&tx),
        Err(TransactionError::NotEnoughFlags),
        "the check function must surface the rule as NotEnoughFlags"
    );
}

/// Either enable bit alone is sufficient: ZIP-225 requires *at least one* of
/// ENABLE_SPENDS and ENABLE_OUTPUTS, not both.
///
/// An implementation using `contains(ENABLE_SPENDS | ENABLE_OUTPUTS)` -- which
/// means "all of" in bitflags -- passes the rejection test above and fails both
/// halves of this one.
#[test]
fn flags_rule_accepts_either_enable_bit_alone() {
    for flags in [Flags::ENABLE_SPENDS, Flags::ENABLE_OUTPUTS] {
        let tx = v5_with_one_orchard_action(flags);
        assert_eq!(tx.orchard_actions().count(), 1);
        assert!(
            tx.has_enough_orchard_flags(),
            "{:?} alone must satisfy the flag rule",
            flags
        );
        assert_eq!(
            check::has_enough_orchard_flags(&tx),
            Ok(()),
            "{:?} alone must be accepted by the check function",
            flags
        );
    }
}
