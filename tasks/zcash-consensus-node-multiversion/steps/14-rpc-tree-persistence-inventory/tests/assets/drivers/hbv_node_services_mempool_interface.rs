//! The `zebra-node-services` mempool service interface.
//!
//! Harbor verifier driver -- injected into `zebra-node-services/tests/` at
//! grading time. Public API only; no dependency the crate does not already
//! declare -- `zebra-chain` is its single dependency and everything below is
//! reachable from it.
//!
//! `zebra-node-services` is a whole new crate at v1.0.0-beta.6, written from
//! the specification in `instruction.md` rather than adapted from an existing
//! file: it is not a workspace member at the predecessor tag and no directory
//! of that name exists there, so this group records zero on a no-op
//! continuation with the reason "crate zebra-node-services is absent".
//!
//! What is asserted is the part of the interface a caller depends on: the
//! request and response variant sets, `Gossip`'s two variants and its `id()`
//! accessor, both `From` conversions, and the `BoxError` alias. The enums
//! carry no behaviour beyond their shape, which is exactly why nothing else
//! in the package can reach them.

use std::collections::HashSet;

use zebra_chain::transaction::{self, LockTime, Transaction, UnminedTx, UnminedTxId};
use zebra_node_services::mempool::{Gossip, Request, Response};
use zebra_node_services::BoxError;

// ── helpers ──────────────────────────────────────────────────────────────────

/// The simplest structurally valid transaction: a transparent V1 with no
/// inputs and no outputs. `UnminedTx::from` serializes it to compute a size,
/// so it has to be a real transaction rather than a placeholder.
fn transaction() -> Transaction {
    Transaction::V1 {
        inputs: Vec::new(),
        outputs: Vec::new(),
        lock_time: LockTime::unlocked(),
    }
}

/// A second, distinguishable transaction.
fn other_transaction() -> Transaction {
    Transaction::V1 {
        inputs: Vec::new(),
        outputs: Vec::new(),
        lock_time: LockTime::min_lock_time_timestamp(),
    }
}

// ── Gossip ───────────────────────────────────────────────────────────────────

#[test]
fn gossip_id_variant_returns_its_own_id() {
    let id = UnminedTxId::from(&transaction());
    let gossip = Gossip::Id(id);

    assert_eq!(
        gossip.id(),
        id,
        "Gossip::Id(id).id() must return the id it was built from"
    );
}

#[test]
fn gossip_tx_variant_returns_the_transactions_id() {
    let unmined = UnminedTx::from(transaction());
    let expected = unmined.id;
    let gossip = Gossip::Tx(unmined);

    assert_eq!(
        gossip.id(),
        expected,
        "Gossip::Tx(tx).id() must return the wrapped transaction's own id, \
         not a recomputed or default one"
    );

    // The two variants are distinct even when they describe the same
    // transaction, so `id()` is the only way to compare them.
    assert_ne!(
        Gossip::Id(expected),
        Gossip::Tx(UnminedTx::from(transaction())),
        "the Id and Tx variants are not equal to each other"
    );
}

#[test]
fn gossip_from_unmined_tx_id_builds_the_id_variant() {
    // The variant is named `Id`, not `UnminedTxId`, and this conversion is how
    // the transaction downloader queues a crawled id.
    let id = UnminedTxId::from(&transaction());

    assert_eq!(
        Gossip::from(id),
        Gossip::Id(id),
        "From<UnminedTxId> for Gossip must produce the Id variant"
    );
}

#[test]
fn gossip_from_unmined_tx_builds_the_tx_variant() {
    // The full-transaction variant carries an `UnminedTx`, not an
    // `Arc<Transaction>`: this conversion is what `sendrawtransaction` uses.
    let unmined = UnminedTx::from(transaction());

    assert_eq!(
        Gossip::from(unmined.clone()),
        Gossip::Tx(unmined.clone()),
        "From<UnminedTx> for Gossip must produce the Tx variant"
    );
    assert_eq!(
        Gossip::from(unmined.clone()).id(),
        unmined.id,
        "the converted Gossip must keep the transaction's id"
    );
}

// ── mempool::Request ─────────────────────────────────────────────────────────

#[test]
fn mempool_request_variants_are_constructable_and_comparable() {
    let id = UnminedTxId::from(&transaction());
    let other_id = UnminedTxId::from(&other_transaction());
    let mined_id = transaction::Hash::from(&transaction());

    let mut ids = HashSet::new();
    ids.insert(id);
    let mut mined_ids = HashSet::new();
    mined_ids.insert(mined_id);

    // All six variants, with the documented payload types.
    let requests = vec![
        Request::TransactionIds,
        Request::TransactionsById(ids.clone()),
        Request::TransactionsByMinedId(mined_ids),
        Request::RejectedTransactionIds(ids.clone()),
        Request::Queue(vec![Gossip::Id(id), Gossip::Tx(UnminedTx::from(transaction()))]),
        Request::CheckForVerifiedTransactions,
    ];
    assert_eq!(requests.len(), 6, "mempool::Request has six variants");

    // Eq / PartialEq are derived, and the service caller relies on them.
    assert_eq!(
        Request::TransactionIds,
        Request::TransactionIds,
        "identical requests must compare equal"
    );
    assert_ne!(
        Request::TransactionIds,
        Request::CheckForVerifiedTransactions,
        "different unit variants must not compare equal"
    );

    // Same payload type, different variant: these must stay distinguishable,
    // which a single `Ids(HashSet<_>)` variant covering both would not be.
    assert_ne!(
        Request::TransactionsById(ids.clone()),
        Request::RejectedTransactionIds(ids.clone()),
        "TransactionsById and RejectedTransactionIds are different requests \
         even with the same payload"
    );

    // And the payload itself is part of the identity.
    let mut other_ids = HashSet::new();
    other_ids.insert(other_id);
    assert_ne!(
        Request::TransactionsById(ids),
        Request::TransactionsById(other_ids),
        "requests differing only in their id set must not compare equal"
    );
}

// ── mempool::Response ────────────────────────────────────────────────────────

#[test]
fn mempool_response_variants_are_constructable() {
    let id = UnminedTxId::from(&transaction());
    let mut ids = HashSet::new();
    ids.insert(id);

    let queued: Vec<Result<(), BoxError>> =
        vec![Ok(()), Err("the transaction was already queued".into())];

    let responses = vec![
        Response::TransactionIds(ids.clone()),
        Response::Transactions(vec![UnminedTx::from(transaction())]),
        Response::RejectedTransactionIds(ids),
        Response::Queued(queued),
        Response::CheckedForVerifiedTransactions,
    ];
    assert_eq!(responses.len(), 5, "mempool::Response has five variants");

    // `Response` derives only `Debug`, so the payloads are read back by
    // matching rather than by comparing.
    for response in responses {
        match response {
            Response::TransactionIds(set) => {
                assert_eq!(set.len(), 1, "TransactionIds carries the ids it was given")
            }
            Response::Transactions(txs) => assert_eq!(
                txs.len(),
                1,
                "Transactions carries UnminedTx values, not bare Transactions"
            ),
            Response::RejectedTransactionIds(set) => assert_eq!(
                set.len(),
                1,
                "RejectedTransactionIds carries the ids it was given"
            ),
            Response::Queued(results) => {
                assert_eq!(results.len(), 2, "Queued carries one result per queued item");
                assert!(results[0].is_ok(), "the first queue result is Ok(())");
                assert!(
                    results[1].is_err(),
                    "the second queue result carries a BoxError"
                );
            }
            Response::CheckedForVerifiedTransactions => {}
        }
    }
}

// ── BoxError ─────────────────────────────────────────────────────────────────

#[test]
fn box_error_alias_boxes_any_send_sync_error() {
    // The alias is what makes the mempool service usable as a Tower service:
    // `RpcImpl`'s bound names `Error = BoxError`, so it has to be this exact
    // boxed-trait-object shape and not a crate-local error enum.
    let from_str: BoxError = "queue is full".into();
    assert_eq!(
        from_str.to_string(),
        "queue is full",
        "BoxError must box a plain error message"
    );

    let from_io: BoxError = Box::new(std::io::Error::new(
        std::io::ErrorKind::Other,
        "downstream service failed",
    ));
    assert_eq!(
        from_io.to_string(),
        "downstream service failed",
        "BoxError must accept any std::error::Error + Send + Sync + 'static"
    );

    // Send + Sync are part of the alias, and Tower relies on both.
    fn assert_send_sync<T: Send + Sync>(_: &T) {}
    assert_send_sync(&from_io);
}
