//! Orchard-carrying V5 blocks committed to the zebra-state finalized state.
//!
//! Harbor verifier driver — injected into `zebra-state/tests/` at grading time.
//!
//! Before this milestone a V5 transaction could not even be parsed once it
//! carried a real Orchard bundle (the old parser only accepted an empty,
//! all-zero Orchard section). This group builds small, hand-linked chains of
//! synthetic blocks whose non-coinbase transaction is a V5 transaction with
//! one or more real Orchard actions, commits them to an ephemeral
//! `zebra_state` finalized state through `Request::CommitFinalizedBlock`, and
//! checks that the chain, its blocks, its transactions and their Orchard
//! nullifiers all round-trip back out through the read-side of the same
//! service. Every scenario here only performs *in-order* commits (each block
//! is the immediate child of the current tip): the finalized state asserts
//! chain order internally and is not expected to return a recoverable error
//! for out-of-order commits, so exercising that path is out of scope for this
//! driver.

use std::io::Cursor;
use std::sync::Arc;

use tower::util::BoxService;
use tower::{Service, ServiceExt};

use zebra_chain::block::{Block, Height};
use zebra_chain::parameters::Network;
use zebra_chain::serialization::{ZcashDeserialize, ZcashSerialize};
use zebra_state::{init, BoxError, Config, Request, Response};

// ---- synthetic wire construction -------------------------------------------
//
// These helpers build the smallest possible valid-shaped blocks: a version-4
// coinbase transaction (so `Block::coinbase_height` and `FinalizedBlock`'s
// `From<Arc<Block>>` conversion succeed) optionally followed by a version-5,
// NU5-branch-id transaction carrying a chosen number of Orchard actions with
// caller-chosen nullifier bytes. Every numeric field the state or block codec
// does not itself validate at this milestone (equihash solution, proofs,
// signatures) is left zeroed; only the fields that participate in chain
// linkage (`previous_block_hash`), height derivation (the coinbase height
// push) and the Orchard accessors under test (nullifiers) carry meaningful
// values.

fn push_compactsize(out: &mut Vec<u8>, n: u64) {
    if n < 0xfd {
        out.push(n as u8);
    } else if n <= 0xffff {
        out.push(0xfd);
        out.extend_from_slice(&(n as u16).to_le_bytes());
    } else {
        out.push(0xfe);
        out.extend_from_slice(&(n as u32).to_le_bytes());
    }
}

/// A version-4, single-input, single-output coinbase transaction whose
/// coinbase script pushes `height` (BIP34-style), so `Block::coinbase_height`
/// recovers exactly `height`.
fn coinbase_tx(height: u32) -> Vec<u8> {
    let mut script = vec![];
    if height >= 1 && height <= 16 {
        script.push(0x50 + height as u8);
    } else {
        let mut le = height.to_le_bytes().to_vec();
        while le.len() > 1 && *le.last().unwrap() == 0 {
            le.pop();
        }
        if *le.last().unwrap() >= 0x80 {
            le.push(0);
        }
        script.push(le.len() as u8);
        script.extend_from_slice(&le);
    }
    script.extend_from_slice(&[0x51, 0x51]);

    // Transaction::V1 wire layout (no overwinter bit): header, inputs,
    // outputs, lock_time -- in that order. There is no padding field; an
    // earlier draft of this helper wrongly inserted one, which shifted every
    // later byte and made `Block::coinbase_height` read garbage.
    let mut out = vec![];
    out.extend_from_slice(&1u32.to_le_bytes()); // header: version 1, not overwintered
    out.push(1); // tx_in count
    out.extend_from_slice(&[0u8; 32]); // prevout hash (coinbase: all zero)
    out.extend_from_slice(&0xffff_ffffu32.to_le_bytes()); // prevout index
    push_compactsize(&mut out, script.len() as u64);
    out.extend_from_slice(&script);
    out.extend_from_slice(&0xffff_ffffu32.to_le_bytes()); // sequence
    out.push(1); // tx_out count
    out.extend_from_slice(&0u64.to_le_bytes()); // value
    out.push(1); // pk_script length
    out.push(0x51); // OP_1
    out.extend_from_slice(&0u32.to_le_bytes()); // lock_time
    out
}

/// A version-5, NU5-branch-id transaction with `nullifiers.len()` Orchard
/// actions, each carrying the given single-byte nullifier tag in the first
/// byte of an otherwise-zeroed 32-byte nullifier field. `flags` is the raw
/// Orchard flags byte (bit 0 = enableSpendsOrchard, bit 1 =
/// enableOutputsOrchard); tests below always set both bits so the coinbase
/// rule added by this milestone (`CoinbaseHasEnableSpendsOrchard`) is never
/// accidentally triggered by mixing this transaction into a coinbase
/// position — it is always the second transaction in the block.
fn v5_tx_with_orchard(nullifiers: &[u8], flags: u8) -> Vec<u8> {
    let mut out = vec![];
    out.extend_from_slice(&(5u32 | (1u32 << 31)).to_le_bytes()); // header: overwintered | version 5
    out.extend_from_slice(&0x26A7_270Au32.to_le_bytes()); // version group id
    out.extend_from_slice(&0xf919_a198u32.to_le_bytes()); // consensus branch id (NU5)
    out.extend_from_slice(&0u32.to_le_bytes()); // lock_time
    out.extend_from_slice(&500_000u32.to_le_bytes()); // expiry_height
    out.push(0); // transparent inputs: 0
    out.push(0); // transparent outputs: 0
    out.push(0); // sapling spends: 0
    out.push(0); // sapling outputs: 0

    push_compactsize(&mut out, nullifiers.len() as u64);
    for &tag in nullifiers {
        out.extend_from_slice(&[0u8; 32]); // cv
        let mut nf = [0u8; 32];
        nf[0] = tag;
        out.extend_from_slice(&nf); // nullifier
        out.extend_from_slice(&[0u8; 32]); // rk
        out.extend_from_slice(&[0u8; 32]); // cm_x
        out.extend_from_slice(&[0u8; 32]); // ephemeral_key
        out.extend_from_slice(&[0u8; 580]); // enc_ciphertext
        out.extend_from_slice(&[0u8; 80]); // out_ciphertext
    }
    out.push(flags); // flagsOrchard
    out.extend_from_slice(&0i64.to_le_bytes()); // valueBalanceOrchard
    out.extend_from_slice(&[0u8; 32]); // anchorOrchard
    // ZIP-225 §7.1: sizeProofsOrchard is 2720 + 2272 * nActionsOrchard, and
    // the aggregated proof is that many bytes. The bytes themselves carry no
    // meaning at this milestone; the length is the documented one so that a
    // parser which validates the field against the spec accepts this bundle.
    let proof_len = 2720 + 2272 * nullifiers.len();
    push_compactsize(&mut out, proof_len as u64); // sizeProofsOrchard
    out.extend_from_slice(&vec![0u8; proof_len]); // proofsOrchard
    out.extend_from_slice(&vec![0u8; 64 * nullifiers.len()]); // vSpendAuthSigsOrchard
    out.extend_from_slice(&[0u8; 64]); // bindingSigOrchard
    out
}

/// A minimal version-5 transaction with zero Orchard actions (no sapling,
/// no orchard, no transparent data at all beyond the empty counts).
fn v5_tx_without_orchard() -> Vec<u8> {
    v5_tx_with_orchard(&[], 0)
}

fn block_bytes(previous_block_hash: [u8; 32], time: u32, txs: &[Vec<u8>]) -> Vec<u8> {
    let mut out = vec![];
    out.extend_from_slice(&4u32.to_le_bytes()); // header version
    out.extend_from_slice(&previous_block_hash);
    out.extend_from_slice(&[0u8; 32]); // merkle root (not checked by CommitFinalizedBlock)
    out.extend_from_slice(&[0u8; 32]); // block commitment / final sapling root
    out.extend_from_slice(&time.to_le_bytes());
    out.extend_from_slice(&0x1d00_ffffu32.to_le_bytes()); // difficulty threshold
    out.extend_from_slice(&[0u8; 32]); // nonce
    push_compactsize(&mut out, 1344);
    out.extend_from_slice(&[0u8; 1344]); // equihash solution
    push_compactsize(&mut out, txs.len() as u64);
    for tx in txs {
        out.extend_from_slice(tx);
    }
    out
}

fn parse_block(raw: &[u8]) -> Arc<Block> {
    Arc::new(
        Block::zcash_deserialize(Cursor::new(raw))
            .unwrap_or_else(|e| panic!("synthetic block failed to parse: {:?}", e)),
    )
}

/// Genesis block (height 0, all-zero previous hash), optionally carrying an
/// Orchard-bearing V5 transaction as its second transaction.
fn genesis_block(orchard_nullifiers: &[u8], time: u32) -> Arc<Block> {
    let mut txs = vec![coinbase_tx(0)];
    if !orchard_nullifiers.is_empty() {
        txs.push(v5_tx_with_orchard(orchard_nullifiers, 0b11));
    }
    parse_block(&block_bytes([0u8; 32], time, &txs))
}

/// A block that extends `parent` by one height, optionally carrying an
/// Orchard-bearing V5 transaction, or a V5 transaction with zero actions
/// when `zero_action_v5` is set (mutually exclusive with non-empty
/// `orchard_nullifiers` in the tests below).
fn child_block(
    parent: &Block,
    height: u32,
    orchard_nullifiers: &[u8],
    zero_action_v5: bool,
    time: u32,
) -> Arc<Block> {
    let hash = parent.hash();
    let previous_block_hash = hash.0;

    let mut txs = vec![coinbase_tx(height)];
    if !orchard_nullifiers.is_empty() {
        txs.push(v5_tx_with_orchard(orchard_nullifiers, 0b11));
    } else if zero_action_v5 {
        txs.push(v5_tx_without_orchard());
    }
    parse_block(&block_bytes(previous_block_hash, time, &txs))
}

// ---- state-service helpers --------------------------------------------------

fn new_state() -> BoxService<Request, Response, BoxError> {
    init(Config::ephemeral(), Network::Mainnet)
}

async fn commit_in_order(
    state: &mut BoxService<Request, Response, BoxError>,
    block: Arc<Block>,
) -> (zebra_chain::block::Hash, zebra_chain::block::Hash) {
    let expected_hash = block.hash();
    let response = state
        .ready_and()
        .await
        .expect("an ephemeral state service is always ready")
        .call(Request::CommitFinalizedBlock(block.into()))
        .await
        .unwrap_or_else(|e| {
            panic!(
                "an in-order, correctly linked block must commit without error, got {:?}",
                e
            )
        });
    match response {
        Response::Committed(hash) => (hash, expected_hash),
        other => panic!("CommitFinalizedBlock returned {:?}, expected Committed", other),
    }
}

async fn query(
    state: &mut BoxService<Request, Response, BoxError>,
    request: Request,
) -> Response {
    state
        .ready_and()
        .await
        .expect("an ephemeral state service is always ready")
        .call(request)
        .await
        .expect("read-only state queries never fail")
}

fn nullifier_bytes<'a, I: Iterator<Item = &'a zebra_chain::orchard::Nullifier>>(
    iter: I,
) -> Vec<[u8; 32]> {
    iter.map(|n| <[u8; 32]>::from(*n)).collect()
}

// ---- tests -------------------------------------------------------------------

#[test]
fn a_v5_block_with_real_orchard_actions_commits_to_the_finalized_state() {
    let rt = tokio::runtime::Runtime::new().expect("tokio runtime");
    rt.block_on(async {
        let mut state = new_state();

        let genesis = genesis_block(&[], 1_600_000_000);
        let result = commit_in_order(&mut state, genesis.clone()).await;
        assert_eq!(
            result.0, result.1,
            "genesis: Committed hash {} != block.hash() {}",
            result.0, result.1
        );

        let child = child_block(&genesis, 1, &[0x11, 0x22], false, 1_600_000_100);
        let result = commit_in_order(&mut state, child.clone()).await;
        assert_eq!(
            result.0, result.1,
            "orchard-carrying child: Committed hash {} != block.hash() {}",
            result.0, result.1
        );
    });
}

#[test]
fn the_committed_orchard_block_is_retrievable_by_hash_and_by_height_with_identical_bytes() {
    let rt = tokio::runtime::Runtime::new().expect("tokio runtime");
    rt.block_on(async {
        let mut state = new_state();
        let genesis = genesis_block(&[], 1_600_000_000);
        commit_in_order(&mut state, genesis.clone()).await;
        let child = child_block(&genesis, 1, &[0x33], false, 1_600_000_100);
        commit_in_order(&mut state, child.clone()).await;

        let mut expected = Vec::new();
        child
            .zcash_serialize(&mut expected)
            .expect("serialize expected block");

        let by_hash = query(&mut state, Request::Block(child.hash().into())).await;
        match by_hash {
            Response::Block(Some(got)) => {
                let mut got_bytes = Vec::new();
                got.zcash_serialize(&mut got_bytes).expect("serialize got block");
                assert_eq!(
                    got_bytes, expected,
                    "block retrieved by hash differs from the committed block"
                );
            }
            other => panic!("Block(hash) returned {:?}, expected Block(Some(_))", other),
        }

        let by_height = query(&mut state, Request::Block(Height(1).into())).await;
        match by_height {
            Response::Block(Some(got)) => {
                let mut got_bytes = Vec::new();
                got.zcash_serialize(&mut got_bytes).expect("serialize got block");
                assert_eq!(
                    got_bytes, expected,
                    "block retrieved by height differs from the committed block"
                );
            }
            other => panic!("Block(height) returned {:?}, expected Block(Some(_))", other),
        }
    });
}

#[test]
fn the_orchard_transaction_is_retrievable_by_hash_and_reports_the_expected_nullifiers() {
    let rt = tokio::runtime::Runtime::new().expect("tokio runtime");
    rt.block_on(async {
        let mut state = new_state();
        let genesis = genesis_block(&[], 1_600_000_000);
        commit_in_order(&mut state, genesis.clone()).await;
        let child = child_block(&genesis, 1, &[0x44, 0x55, 0x66], false, 1_600_000_100);
        commit_in_order(&mut state, child.clone()).await;

        // The Orchard-carrying transaction is transactions[1] (transactions[0]
        // is the coinbase).
        let orchard_tx = &child.transactions[1];
        let tx_hash = orchard_tx.hash();

        let response = query(&mut state, Request::Transaction(tx_hash)).await;
        let retrieved = match response {
            Response::Transaction(Some(tx)) => tx,
            other => panic!(
                "Transaction(hash) returned {:?}, expected Transaction(Some(_))",
                other
            ),
        };

        let got_nullifiers = nullifier_bytes(retrieved.orchard_nullifiers());
        let expected_nullifiers: Vec<[u8; 32]> = [0x44u8, 0x55, 0x66]
            .iter()
            .map(|&tag| {
                let mut n = [0u8; 32];
                n[0] = tag;
                n
            })
            .collect();
        assert_eq!(
            got_nullifiers, expected_nullifiers,
            "nullifiers retrieved from state ({:?}) differ from the ones committed ({:?})",
            got_nullifiers, expected_nullifiers
        );
    });
}

#[test]
fn a_second_child_block_advances_the_tip_while_earlier_blocks_stay_queryable() {
    let rt = tokio::runtime::Runtime::new().expect("tokio runtime");
    rt.block_on(async {
        let mut state = new_state();
        let genesis = genesis_block(&[], 1_600_000_000);
        commit_in_order(&mut state, genesis.clone()).await;
        let child1 = child_block(&genesis, 1, &[0x77], false, 1_600_000_100);
        commit_in_order(&mut state, child1.clone()).await;
        let child2 = child_block(&child1, 2, &[0x88], false, 1_600_000_200);
        commit_in_order(&mut state, child2.clone()).await;

        let tip = query(&mut state, Request::Tip).await;
        match tip {
            Response::Tip(Some((height, hash))) => {
                assert_eq!(height, Height(2), "tip height {:?} != 2", height);
                assert_eq!(
                    hash,
                    child2.hash(),
                    "tip hash {} != child2 hash {}",
                    hash,
                    child2.hash()
                );
            }
            other => panic!("Tip returned {:?}, expected Tip(Some(_))", other),
        }

        let genesis_again = query(&mut state, Request::Block(Height(0).into())).await;
        assert!(
            matches!(genesis_again, Response::Block(Some(_))),
            "genesis must remain queryable after two more blocks are committed, got {:?}",
            genesis_again
        );
        let child1_again = query(&mut state, Request::Block(child1.hash().into())).await;
        assert!(
            matches!(child1_again, Response::Block(Some(_))),
            "child1 must remain queryable after child2 is committed, got {:?}",
            child1_again
        );
    });
}

#[test]
fn a_v5_transaction_with_zero_orchard_actions_survives_commit_with_no_orchard_data() {
    let rt = tokio::runtime::Runtime::new().expect("tokio runtime");
    rt.block_on(async {
        let mut state = new_state();
        let genesis = genesis_block(&[], 1_600_000_000);
        commit_in_order(&mut state, genesis.clone()).await;
        let child = child_block(&genesis, 1, &[], true, 1_600_000_100);
        commit_in_order(&mut state, child.clone()).await;

        let zero_action_tx_hash = child.transactions[1].hash();
        let response = query(&mut state, Request::Transaction(zero_action_tx_hash)).await;
        match response {
            Response::Transaction(Some(tx)) => {
                assert!(
                    tx.orchard_shielded_data().is_none(),
                    "a V5 transaction with 0 Orchard actions must retrieve with orchard_shielded_data() == None"
                );
            }
            other => panic!(
                "Transaction(hash) returned {:?}, expected Transaction(Some(_))",
                other
            ),
        }

        // The other arm of the same rule: a transaction from a version below
        // V5 has no Orchard data at all. The block's first transaction is its
        // V1 coinbase.
        let coinbase_hash = child.transactions[0].hash();
        let response = query(&mut state, Request::Transaction(coinbase_hash)).await;
        match response {
            Response::Transaction(Some(tx)) => {
                assert!(
                    tx.orchard_shielded_data().is_none(),
                    "every version below V5 must retrieve with orchard_shielded_data() == None"
                );
            }
            other => panic!(
                "Transaction(hash) returned {:?}, expected Transaction(Some(_))",
                other
            ),
        }
    });
}

#[test]
fn an_unknown_hash_or_height_returns_none_rather_than_an_error() {
    let rt = tokio::runtime::Runtime::new().expect("tokio runtime");
    rt.block_on(async {
        let mut state = new_state();
        let genesis = genesis_block(&[], 1_600_000_000);
        commit_in_order(&mut state, genesis.clone()).await;

        let response = query(&mut state, Request::Block(Height(41).into())).await;
        assert!(
            matches!(response, Response::Block(None)),
            "an uncommitted height must return Block(None), got {:?}",
            response
        );

        let unknown_hash = zebra_chain::block::Hash([0xab; 32]);
        let response = query(&mut state, Request::Block(unknown_hash.into())).await;
        assert!(
            matches!(response, Response::Block(None)),
            "an unknown hash must return Block(None), got {:?}",
            response
        );

        let tip = query(&mut state, Request::Tip).await;
        match tip {
            Response::Tip(Some((height, _))) => {
                assert_eq!(height, Height(0), "tip after only genesis must be height 0")
            }
            other => panic!("Tip returned {:?}, expected Tip(Some(_))", other),
        }
    });
}
