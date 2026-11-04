//! The state crate's contextual-validation surface.
//!
//! Harbor verifier driver -- injected into `zebra-state/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! This milestone is where `zebra-state` first has to *reason about the
//! contents of a block* rather than just store it. Three things in its public
//! surface change shape as a result, and this file pins all three:
//!
//!   1. `PreparedBlock::new_outputs` stops being a plain UTXO index and
//!      becomes an *ordered* one, `HashMap<OutPoint, OrderedUtxo>`. The extra
//!      field is `tx_index_in_block`, and it exists for exactly one reason:
//!      a transaction may spend an output created by an *earlier* transaction
//!      in the same block, but not by a later one, and nothing else in the
//!      block gives you that ordering once the outputs are in a hash map.
//!      This is a type change, so the struct literal below simply does not
//!      compile against the previous milestone.
//!
//!   2. `ValidateContextError` grows the contextual failure modes, including
//!      the two note-commitment-tree wrappers. Almost every variant is
//!      `#[non_exhaustive]` and therefore cannot be built from outside the
//!      crate -- see the negative-space note in TEST_INTERFACE. The two tree
//!      variants are the exception: they are `#[from]` conversions, so an
//!      integration test can build them, and they are the ones this file
//!      scores. What is checked is the *observable* contract: which error
//!      type each conversion lands in, what it prints, and how it chains.
//!
//!   3. `DATABASE_FORMAT_VERSION` steps, because the on-disk layout has to
//!      hold the new note commitment trees and the ordered UTXO index. An
//!      implementation that adds the fields but leaves the version alone
//!      would silently reuse an incompatible database.
//!
//! Nothing here starts a state service or touches the disk: the scored
//! surface is types, constants and error chaining, all of which are reachable
//! without a running `tower::Service`.

use std::collections::HashMap;
use std::error::Error;
use std::path::PathBuf;
use std::sync::Arc;

use zebra_chain::block::Block;
use zebra_chain::orchard;
use zebra_chain::sapling;
use zebra_chain::serialization::ZcashDeserialize;
use zebra_chain::transparent::{self, OrderedUtxo, OutPoint};

use zebra_state::{
    constants::{
        DATABASE_FORMAT_VERSION, MAX_BLOCK_REORG_HEIGHT, MIN_TRANSPARENT_COINBASE_MATURITY,
    },
    CloneError, CommitBlockError, PreparedBlock, ValidateContextError,
};

// ---------------------------------------------------------------- fixtures

fn fixture_dir() -> PathBuf {
    PathBuf::from(
        std::env::var("HBV_FIXTURES")
            .expect("HBV_FIXTURES must point at the shipped fixture directory"),
    )
}

/// Load the block corpus as `(name, block)` pairs, in TSV order.
fn blocks() -> Vec<(String, Block)> {
    let dir = fixture_dir();
    let tsv = std::fs::read_to_string(dir.join("blocks.tsv")).expect("blocks.tsv is readable");

    let mut out = Vec::new();
    for line in tsv.lines() {
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        let name = line
            .split('\t')
            .next()
            .expect("every row has a name column")
            .to_string();
        let hex = std::fs::read_to_string(dir.join("blocks").join(format!("{}.hex", name)))
            .unwrap_or_else(|_| panic!("block fixture {}.hex is readable", name));
        let raw: Vec<u8> = hex
            .split_whitespace()
            .collect::<String>()
            .as_bytes()
            .chunks(2)
            .map(|pair| {
                u8::from_str_radix(
                    std::str::from_utf8(pair).expect("hex is ASCII"),
                    16,
                )
                .expect("block fixture is valid hex")
            })
            .collect();
        let block = Block::zcash_deserialize(&raw[..])
            .unwrap_or_else(|error| panic!("block {} must deserialize: {:?}", name, error));
        out.push((name, block));
    }

    assert!(out.len() >= 17, "the corpus must hold every shipped block");
    out
}

// ------------------------------------------------------------------- tests

/// A `PreparedBlock` is built entirely from public fields, and its
/// `new_outputs` index is an *ordered* one.
///
/// The struct literal is the load-bearing part: if `new_outputs` were still
/// `HashMap<OutPoint, Utxo>` this function would not compile. The assertions
/// then pin what the extra ordering field has to contain.
#[test]
fn a_prepared_block_carries_an_ordered_index_of_the_outputs_its_block_creates() {
    let mut checked = 0;
    let mut multi_transaction_blocks = 0;

    for (name, block) in blocks() {
        let block = Arc::new(block);
        let height = block
            .coinbase_height()
            .unwrap_or_else(|| panic!("block {} has a coinbase height", name));
        let hash = block.hash();
        let transaction_hashes: Vec<_> =
            block.transactions.iter().map(|tx| tx.hash()).collect();

        let new_outputs: HashMap<OutPoint, OrderedUtxo> =
            transparent::new_ordered_outputs(&block, transaction_hashes.as_slice());

        let prepared = PreparedBlock {
            block: block.clone(),
            hash,
            height,
            new_outputs,
            transaction_hashes: transaction_hashes.clone(),
        };

        assert_eq!(
            prepared.hash,
            prepared.block.hash(),
            "a prepared block's hash must be its block's hash ({})",
            name
        );
        assert_eq!(
            prepared.height, height,
            "a prepared block's height must be its block's coinbase height ({})",
            name
        );
        assert_eq!(
            prepared.transaction_hashes.len(),
            prepared.block.transactions.len(),
            "a prepared block must precompute one hash per transaction ({})",
            name
        );

        // Every output of every transaction must appear exactly once, keyed by
        // its own outpoint, and tagged with the index of the transaction that
        // created it.
        let mut expected_outputs = 0;
        for (tx_index, transaction) in block.transactions.iter().enumerate() {
            let tx_hash = transaction_hashes[tx_index];
            for (output_index, output) in transaction.outputs().iter().enumerate() {
                let outpoint = OutPoint {
                    hash: tx_hash,
                    index: output_index as u32,
                };
                let ordered = prepared.new_outputs.get(&outpoint).unwrap_or_else(|| {
                    panic!(
                        "output {} of transaction {} of {} must be indexed by its outpoint",
                        output_index, tx_index, name
                    )
                });

                assert_eq!(
                    ordered.tx_index_in_block, tx_index,
                    "the ordered UTXO for output {} of transaction {} of {} must record the \
                     index of the transaction that created it",
                    output_index, tx_index, name
                );
                assert_eq!(
                    ordered.utxo.height, height,
                    "an output created by {} must be recorded at that block's height",
                    name
                );
                assert_eq!(
                    ordered.utxo.from_coinbase,
                    tx_index == 0,
                    "only the outputs of the first transaction of {} are coinbase outputs",
                    name
                );
                assert_eq!(
                    &ordered.utxo.output, output,
                    "the indexed output must be the output itself ({})",
                    name
                );

                expected_outputs += 1;
            }
        }

        assert_eq!(
            prepared.new_outputs.len(),
            expected_outputs,
            "the ordered index of {} must hold exactly the outputs its block creates",
            name
        );

        if block.transactions.len() > 1 {
            multi_transaction_blocks += 1;
        }
        checked += 1;
    }

    assert!(checked >= 17, "every corpus block must be checked");
    assert!(
        multi_transaction_blocks >= 5,
        "at least five corpus blocks must hold more than one transaction, otherwise \
         `tx_index_in_block` is never anything but zero, found {}",
        multi_transaction_blocks
    );
}

/// Dropping the ordering tag must give back exactly the plain UTXO index, so
/// the ordered form is a strict refinement of what the previous milestone had.
#[test]
fn dropping_the_transaction_index_turns_the_ordered_index_back_into_a_plain_utxo_index() {
    let mut checked = 0;

    for (name, block) in blocks() {
        let transaction_hashes: Vec<_> =
            block.transactions.iter().map(|tx| tx.hash()).collect();

        let ordered = transparent::new_ordered_outputs(&block, transaction_hashes.as_slice());
        let plain = transparent::new_outputs(&block, transaction_hashes.as_slice());
        let flattened = transparent::utxos_from_ordered_utxos(ordered.clone());

        assert_eq!(
            flattened, plain,
            "flattening the ordered index of {} must give the plain UTXO index",
            name
        );
        assert_eq!(
            ordered.len(),
            plain.len(),
            "flattening must not lose or add an entry ({})",
            name
        );

        for (outpoint, ordered_utxo) in ordered.iter() {
            let utxo = plain
                .get(outpoint)
                .unwrap_or_else(|| panic!("{:?} must be in both indexes ({})", outpoint, name));
            assert_eq!(
                &ordered_utxo.utxo, utxo,
                "the two indexes must agree on the UTXO itself ({})",
                name
            );
            assert_eq!(
                &OrderedUtxo::new(
                    utxo.output.clone(),
                    utxo.height,
                    utxo.from_coinbase,
                    ordered_utxo.tx_index_in_block,
                ),
                ordered_utxo,
                "an ordered UTXO rebuilt from its four parts must equal the indexed one ({})",
                name
            );
        }

        checked += 1;
    }

    assert!(checked >= 17, "every corpus block must be checked");
}

/// A note commitment tree failure in either shielded pool must surface as its
/// own `ValidateContextError` variant, reachable by `From`, and each must name
/// the pool it came from.
#[test]
fn a_note_commitment_tree_failure_becomes_a_contextual_validation_error_naming_its_pool() {
    let sapling_error: ValidateContextError =
        sapling::tree::NoteCommitmentTreeError::FullTree.into();
    let orchard_error: ValidateContextError =
        orchard::tree::NoteCommitmentTreeError::FullTree.into();

    assert_eq!(
        sapling_error.to_string(),
        "error in Sapling note commitment tree",
        "the Sapling tree error variant must say which tree failed"
    );
    assert_eq!(
        orchard_error.to_string(),
        "error in Orchard note commitment tree",
        "the Orchard tree error variant must say which tree failed"
    );
    assert_ne!(
        sapling_error, orchard_error,
        "a Sapling tree failure and an Orchard tree failure must be distinguishable"
    );

    // The conversion must be a wrapper, not a discard: the originating tree
    // error stays reachable as the error's source.
    let source = sapling_error
        .source()
        .expect("the tree error variant must keep the tree error as its source");
    assert_eq!(
        source.to_string(),
        "The note commitment tree is full",
        "the source of the Sapling variant must be the tree error itself"
    );
    let source = orchard_error
        .source()
        .expect("the tree error variant must keep the tree error as its source");
    assert_eq!(
        source.to_string(),
        "The note commitment tree is full",
        "the source of the Orchard variant must be the tree error itself"
    );

    // The tree errors themselves compare equal only within their own pool.
    assert_eq!(
        sapling::tree::NoteCommitmentTreeError::FullTree,
        sapling::tree::NoteCommitmentTreeError::FullTree,
        "the tree error must be comparable"
    );
}

/// `CommitBlockError` is the outer error the state returns; it wraps a
/// `ValidateContextError` without losing it.
#[test]
fn a_commit_block_error_wraps_a_contextual_error_and_keeps_it_as_its_source() {
    let inner: ValidateContextError = sapling::tree::NoteCommitmentTreeError::FullTree.into();
    let inner_text = inner.to_string();
    let outer = CommitBlockError::from(inner);

    assert_eq!(
        outer.to_string(),
        "block is not contextually valid",
        "the commit error must report the failure category, not the detail"
    );
    assert_ne!(
        outer.to_string(),
        inner_text,
        "the commit error must not just forward the inner message, or the two levels \
         would be indistinguishable"
    );

    let source = outer
        .source()
        .expect("a commit error must keep the contextual error as its source");
    assert_eq!(
        source.to_string(),
        inner_text,
        "the source of a commit error must be the contextual error it wrapped"
    );

    // Two commit errors over the same contextual error must compare equal, so
    // a caller can match on a specific failure.
    let again =
        CommitBlockError::from(ValidateContextError::from(
            sapling::tree::NoteCommitmentTreeError::FullTree,
        ));
    assert_eq!(
        outer, again,
        "commit errors over the same contextual error must compare equal"
    );
    assert_ne!(
        outer,
        CommitBlockError::from(ValidateContextError::from(
            orchard::tree::NoteCommitmentTreeError::FullTree
        )),
        "commit errors over different contextual errors must not compare equal"
    );
}

/// `CloneError` is the clonable, type-erased form. It must display exactly
/// what it wraps, and it must survive being cloned.
#[test]
fn a_clone_error_is_clonable_and_displays_the_error_it_erased() {
    let commit = CommitBlockError::from(ValidateContextError::from(
        orchard::tree::NoteCommitmentTreeError::FullTree,
    ));
    let expected = commit.to_string();

    let erased = CloneError::from(commit);
    assert_eq!(
        erased.to_string(),
        expected,
        "a clone error must display transparently as the error it wrapped"
    );

    let copy = erased.clone();
    assert_eq!(
        copy.to_string(),
        expected,
        "cloning a clone error must preserve its message"
    );

    // The same type must also erase a plain boxed error, which is how the
    // state service reports failures that are not contextual validation.
    let boxed: Box<dyn std::error::Error + Send + Sync + 'static> =
        "some other state failure".into();
    let erased = CloneError::from(boxed);
    assert_eq!(
        erased.to_string(),
        "some other state failure",
        "a clone error over a boxed error must display that error's message"
    );
    assert_eq!(
        erased.clone().to_string(),
        "some other state failure",
        "cloning must preserve a boxed error's message too"
    );
}

/// The maturity rule's constants, and the database format version that the
/// new on-disk contents force forward.
#[test]
fn the_coinbase_maturity_reorg_limit_and_database_format_version_are_fixed() {
    assert_eq!(
        MIN_TRANSPARENT_COINBASE_MATURITY, 100,
        "a transparent coinbase output matures 100 blocks after it is created"
    );
    assert_eq!(
        MAX_BLOCK_REORG_HEIGHT,
        MIN_TRANSPARENT_COINBASE_MATURITY - 1,
        "the reorg limit is one less than the coinbase maturity, so a matured coinbase \
         output can never be un-matured by a legal reorg"
    );
    assert_eq!(
        MAX_BLOCK_REORG_HEIGHT, 99,
        "the reorg limit is 99 blocks"
    );
    assert_eq!(
        zebra_state::MAX_BLOCK_REORG_HEIGHT,
        MAX_BLOCK_REORG_HEIGHT,
        "the reorg limit must also be re-exported at the crate root"
    );

    assert_eq!(
        DATABASE_FORMAT_VERSION, 6,
        "the on-disk format version must step when the stored state gains the note \
         commitment trees and the ordered UTXO index"
    );
}
