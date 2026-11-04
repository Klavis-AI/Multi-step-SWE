//! Transparent outputs that remember where in the block they were created.
//!
//! Harbor verifier driver -- injected into `zebra-chain/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! Until this milestone a `Utxo` carried the output, the height of the block
//! that created it, and whether it came from a coinbase transaction. That is
//! enough to validate a spend from an *earlier* block, and not enough to
//! validate a spend from the *same* block, because Bitcoin-derived consensus
//! allows a transaction to spend an output created by an earlier transaction
//! in the block it is in -- but only an earlier one:
//!
//!   "Any input within this block can spend an output which also appears in
//!    this block (assuming the spend is otherwise valid). However, the TXID
//!    corresponding to the output must be placed at some point before the
//!    TXID corresponding to the input."
//!
//! So this milestone adds `OrderedUtxo`, which is a `Utxo` plus the index of
//! the transaction that created it within its block, and the two functions
//! that build and then discard that index. It also adds
//! `CoinbaseSpendRestriction`, the second half of the coinbase spending rule:
//! a transaction spending a coinbase output must itself have no transparent
//! outputs, and a coinbase output is immature for 100 blocks.
//!
//! The assertions bite because the index is easy to get subtly wrong. The two
//! indexes in play are different numbers with the same shape -- the index of
//! the *transaction in the block*, and the index of the *output in the
//! transaction*, which is `OutPoint.index`. An implementation that stores the
//! latter in `tx_index_in_block` passes any test that only looks at
//! single-output transactions. Every corpus block with more than one
//! transaction is walked here, and both indexes are recomputed independently
//! by enumerating the block's own structure.

use std::collections::HashMap;
use std::convert::TryFrom;

use zebra_chain::block::{Block, Height};
use zebra_chain::serialization::ZcashDeserialize;
use zebra_chain::transaction::{LockTime, Transaction};
use zebra_chain::transparent::{
    self, CoinbaseSpendRestriction, OrderedUtxo, OutPoint, Utxo,
};

// ---------------------------------------------------------------- fixtures
//
// The corpus is seventeen real blocks from the Zcash mainnet and testnet,
// spanning the genesis block and every network upgrade activation height up to
// Canopy, shipped as hex alongside a TSV of the values a correct
// implementation must reproduce. Every expected value in the TSV was derived
// from the block bytes themselves -- the block hash by SHA256d over the
// 1487-byte header, the transaction count by a compactSize reader -- and not
// from any Rust implementation of this interface.

use std::collections::BTreeMap;
use std::path::PathBuf;

#[allow(dead_code)]
pub struct Fixture {
    pub name: String,
    pub network: String,
    pub height: u32,
    /// The block's length in bytes, as recorded in the TSV.
    pub size: usize,
    pub block_hash: String,
    pub previous_block_hash: String,
    pub merkle_root: [u8; 32],
    pub root_bytes: [u8; 32],
    pub version: u32,
    pub time: i64,
    pub difficulty_threshold: u32,
    pub nonce: [u8; 32],
    pub tx_count: usize,
    pub note: String,
    pub raw: Vec<u8>,
}

fn fixture_dir() -> PathBuf {
    PathBuf::from(
        std::env::var("HBV_FIXTURES")
            .expect("HBV_FIXTURES must point at the shipped fixture directory"),
    )
}

fn cell<'a>(row: &BTreeMap<&'a str, &'a str>, key: &str) -> &'a str {
    *row.get(key)
        .unwrap_or_else(|| panic!("blocks.tsv has no column {}", key))
}

fn thirty_two(hex_str: &str) -> [u8; 32] {
    let bytes = hex::decode(hex_str).expect("fixture field is hex");
    assert_eq!(bytes.len(), 32, "fixture field is 32 bytes");
    let mut out = [0u8; 32];
    out.copy_from_slice(&bytes);
    out
}

/// Load the whole block corpus, in the order the TSV lists it.
#[allow(dead_code)]
pub fn blocks() -> Vec<Fixture> {
    let dir = fixture_dir();
    let tsv = std::fs::read_to_string(dir.join("blocks.tsv")).expect("blocks.tsv is readable");
    let mut lines = tsv.lines();
    let header: Vec<&str> = lines
        .next()
        .expect("blocks.tsv has a header row")
        .trim_start_matches("# ")
        .split('\t')
        .collect();

    let mut out = Vec::new();
    for line in lines {
        if line.is_empty() {
            continue;
        }
        let cells: Vec<&str> = line.split('\t').collect();
        assert_eq!(cells.len(), header.len(), "malformed row: {}", line);
        let row: BTreeMap<&str, &str> = header.iter().cloned().zip(cells).collect();

        let name = cell(&row, "name").to_string();
        let hex_text = std::fs::read_to_string(dir.join("blocks").join(format!("{}.hex", name)))
            .unwrap_or_else(|e| panic!("cannot read block {}: {}", name, e));
        let raw = hex::decode(hex_text.trim()).expect("block fixture is hex");

        out.push(Fixture {
            name,
            raw,
            network: cell(&row, "network").to_string(),
            height: cell(&row, "height").parse().expect("height"),
            size: cell(&row, "bytes").parse().expect("bytes"),
            block_hash: cell(&row, "block_hash").to_string(),
            previous_block_hash: cell(&row, "previous_block_hash").to_string(),
            merkle_root: thirty_two(cell(&row, "merkle_root_hex")),
            root_bytes: thirty_two(cell(&row, "root_bytes_hex")),
            version: cell(&row, "version").parse().expect("version"),
            time: cell(&row, "time").parse().expect("time"),
            difficulty_threshold: cell(&row, "difficulty_threshold")
                .parse()
                .expect("difficulty_threshold"),
            nonce: thirty_two(cell(&row, "nonce_hex")),
            tx_count: cell(&row, "tx_count").parse().expect("tx_count"),
            note: cell(&row, "note").to_string(),
        });
    }
    assert!(
        out.len() >= 17,
        "the shipped corpus has {} blocks; the verifier expects at least 17",
        out.len()
    );
    out
}

// ---------------------------------------------------------------- helpers

/// Parse a fixture into a `Block`, naming the fixture if it does not parse.
fn parse(fixture: &Fixture) -> Block {
    Block::zcash_deserialize(&fixture.raw[..])
        .unwrap_or_else(|e| panic!("fixture {} must deserialize as a block: {:?}", fixture.name, e))
}

/// The transaction hashes of a block, in block order.
///
/// `new_ordered_outputs` takes these precomputed, because the caller in the
/// state already has them. They are the map keys, not an expected value.
fn transaction_hashes(block: &Block) -> Vec<zebra_chain::transaction::Hash> {
    block.transactions.iter().map(|tx| tx.hash()).collect()
}

/// Fixtures with more than one transaction: the only ones where the
/// transaction index and the output index can disagree.
fn multi_transaction_blocks() -> Vec<Fixture> {
    let out: Vec<Fixture> = blocks().into_iter().filter(|f| f.tx_count > 1).collect();
    assert!(
        out.len() >= 5,
        "the corpus must contain at least five multi-transaction blocks, found {}",
        out.len()
    );
    out
}

/// A transparent output lifted out of a real block, to build synthetic
/// `OrderedUtxo`s from without inventing a script.
fn some_real_output() -> transparent::Output {
    let fixture = blocks()
        .into_iter()
        .find(|f| f.name == "main-0000010")
        .expect("the corpus contains mainnet block 10");
    let block = parse(&fixture);
    block.transactions[0].outputs()[0].clone()
}

// ---------------------------------------------------------------- the index

#[test]
fn every_new_ordered_output_is_tagged_with_the_index_of_its_own_transaction() {
    for fixture in multi_transaction_blocks() {
        let block = parse(&fixture);
        let hashes = transaction_hashes(&block);
        let height = Height(fixture.height);

        let ordered = transparent::new_ordered_outputs(&block, &hashes);

        // Recompute what the map must contain by walking the block directly.
        let mut expected_entries = 0usize;
        for (tx_index_in_block, transaction) in block.transactions.iter().enumerate() {
            for (output_index_in_transaction, output) in transaction.outputs().iter().enumerate() {
                let index = u32::try_from(output_index_in_transaction)
                    .expect("a real transaction has fewer than 2^32 outputs");
                let outpoint = OutPoint {
                    hash: hashes[tx_index_in_block],
                    index,
                };

                let entry = ordered.get(&outpoint).unwrap_or_else(|| {
                    panic!(
                        "{}: no ordered UTXO for transaction {} output {}",
                        fixture.name, tx_index_in_block, output_index_in_transaction
                    )
                });

                assert_eq!(
                    entry.tx_index_in_block, tx_index_in_block,
                    "{}: transaction {} output {} must be tagged with transaction index {}, got {}",
                    fixture.name,
                    tx_index_in_block,
                    output_index_in_transaction,
                    tx_index_in_block,
                    entry.tx_index_in_block
                );
                assert_eq!(
                    &entry.utxo.output, output,
                    "{}: transaction {} output {} must carry its own output",
                    fixture.name, tx_index_in_block, output_index_in_transaction
                );
                assert_eq!(
                    entry.utxo.height, height,
                    "{}: every new output is created at the block's height {:?}, got {:?}",
                    fixture.name, height, entry.utxo.height
                );

                expected_entries += 1;
            }
        }

        assert_eq!(
            ordered.len(),
            expected_entries,
            "{}: the index must have exactly one entry per output, expected {}, got {}",
            fixture.name,
            expected_entries,
            ordered.len()
        );
    }
}

#[test]
fn the_transaction_index_is_not_the_output_index() {
    // The two indexes are distinct numbers, and a block where some
    // transaction other than the first has more than one output proves it:
    // there the transaction index is fixed across outputs whose OutPoint
    // index runs 0, 1, 2...
    let mut checked = 0usize;

    for fixture in multi_transaction_blocks() {
        let block = parse(&fixture);
        let hashes = transaction_hashes(&block);
        let ordered = transparent::new_ordered_outputs(&block, &hashes);

        for (tx_index_in_block, transaction) in block.transactions.iter().enumerate() {
            if tx_index_in_block == 0 || transaction.outputs().len() < 2 {
                continue;
            }

            for output_index_in_transaction in 0..transaction.outputs().len() {
                let outpoint = OutPoint {
                    hash: hashes[tx_index_in_block],
                    index: u32::try_from(output_index_in_transaction).expect("small index"),
                };
                let entry = ordered.get(&outpoint).expect("the output is indexed");

                assert_eq!(
                    entry.tx_index_in_block, tx_index_in_block,
                    "{}: tx_index_in_block must stay {} across the transaction's outputs, \
                     but output {} reports {}",
                    fixture.name,
                    tx_index_in_block,
                    output_index_in_transaction,
                    entry.tx_index_in_block
                );
            }
            checked += 1;
        }
    }

    assert!(
        checked > 0,
        "the corpus must contain a non-first transaction with two or more outputs"
    );
}

#[test]
fn only_the_first_transaction_of_a_block_creates_coinbase_outputs() {
    for fixture in blocks() {
        let block = parse(&fixture);
        let hashes = transaction_hashes(&block);
        let ordered = transparent::new_ordered_outputs(&block, &hashes);

        for (outpoint, entry) in ordered.iter() {
            let expected_from_coinbase = entry.tx_index_in_block == 0;
            assert_eq!(
                entry.utxo.from_coinbase, expected_from_coinbase,
                "{}: the output at {:?} came from transaction {}, so from_coinbase must be {}",
                fixture.name, outpoint, entry.tx_index_in_block, expected_from_coinbase
            );
        }
    }
}

// ---------------------------------------------------------------- dropping the index

#[test]
fn utxos_from_ordered_utxos_keeps_every_utxo_and_drops_only_the_index() {
    for fixture in multi_transaction_blocks() {
        let block = parse(&fixture);
        let hashes = transaction_hashes(&block);

        let ordered = transparent::new_ordered_outputs(&block, &hashes);
        let plain = transparent::utxos_from_ordered_utxos(ordered.clone());

        assert_eq!(
            plain.len(),
            ordered.len(),
            "{}: dropping the index must not drop any entries, {} vs {}",
            fixture.name,
            plain.len(),
            ordered.len()
        );

        for (outpoint, entry) in ordered.iter() {
            let utxo = plain
                .get(outpoint)
                .unwrap_or_else(|| panic!("{}: {:?} lost when dropping the index", fixture.name, outpoint));
            assert_eq!(
                utxo, &entry.utxo,
                "{}: {:?} must keep the same Utxo when the index is dropped",
                fixture.name, outpoint
            );
        }
    }
}

#[test]
fn new_outputs_is_new_ordered_outputs_with_the_index_removed() {
    for fixture in blocks() {
        let block = parse(&fixture);
        let hashes = transaction_hashes(&block);

        let direct: HashMap<OutPoint, Utxo> = transparent::new_outputs(&block, &hashes);
        let via_ordered: HashMap<OutPoint, Utxo> =
            transparent::utxos_from_ordered_utxos(transparent::new_ordered_outputs(&block, &hashes));

        assert_eq!(
            direct, via_ordered,
            "{}: new_outputs must agree with new_ordered_outputs once the index is dropped",
            fixture.name
        );
    }
}

#[test]
fn an_ordered_utxo_is_built_from_its_four_fields() {
    let output = some_real_output();
    let height = Height(419_200);

    let ordered = OrderedUtxo::new(output.clone(), height, true, 7);

    assert_eq!(
        ordered.tx_index_in_block, 7,
        "OrderedUtxo::new must store the transaction index it was given, got {}",
        ordered.tx_index_in_block
    );
    assert_eq!(
        ordered.utxo.output, output,
        "OrderedUtxo::new must store the output it was given"
    );
    assert_eq!(
        ordered.utxo.height, height,
        "OrderedUtxo::new must store the height it was given, got {:?}",
        ordered.utxo.height
    );
    assert!(
        ordered.utxo.from_coinbase,
        "OrderedUtxo::new must store the from_coinbase flag it was given"
    );

    // ...and the flag is not hard-coded.
    let not_coinbase = OrderedUtxo::new(output.clone(), height, false, 0);
    assert!(
        !not_coinbase.utxo.from_coinbase,
        "OrderedUtxo::new(.., false, ..) must produce from_coinbase == false"
    );
    assert_eq!(
        not_coinbase.tx_index_in_block, 0,
        "a transaction index of 0 must be stored as 0"
    );

    // The inner Utxo is exactly the one the equivalent literal would build.
    let expected_utxo = Utxo {
        output,
        height,
        from_coinbase: false,
    };
    assert_eq!(
        not_coinbase.utxo, expected_utxo,
        "OrderedUtxo::new must build the same Utxo as the equivalent struct literal"
    );
}

// ---------------------------------------------------------------- spend restriction

#[test]
fn a_transaction_with_transparent_outputs_may_not_spend_coinbase_outputs() {
    // Every coinbase transaction in the corpus has at least one transparent
    // output, so every one of them reports the restricted case.
    let mut checked = 0usize;

    for fixture in blocks() {
        let block = parse(&fixture);
        let height = Height(fixture.height);

        for transaction in block.transactions.iter() {
            if transaction.outputs().is_empty() {
                continue;
            }

            assert_eq!(
                transaction.coinbase_spend_restriction(height),
                CoinbaseSpendRestriction::SomeTransparentOutputs,
                "{}: a transaction with {} transparent outputs must report SomeTransparentOutputs",
                fixture.name,
                transaction.outputs().len()
            );
            checked += 1;
        }
    }

    assert!(
        checked >= 17,
        "the corpus must contain at least seventeen transactions with transparent outputs, found {}",
        checked
    );
}

#[test]
fn a_transaction_with_no_transparent_outputs_is_restricted_to_the_shielded_case() {
    // A transaction with no transparent outputs at all. The consensus rules
    // guarantee such a transaction has shielded outputs; the restriction only
    // cares that the transparent output list is empty.
    let fully_shielded = Transaction::V1 {
        inputs: Vec::new(),
        outputs: Vec::new(),
        lock_time: LockTime::min_lock_time(),
    };

    for spend_height in [Height(1), Height(419_200), Height(1_046_400)].iter() {
        let restriction = fully_shielded.coinbase_spend_restriction(*spend_height);

        match restriction {
            CoinbaseSpendRestriction::OnlyShieldedOutputs { spend_height: got } => assert_eq!(
                got, *spend_height,
                "the restriction must carry the height the spend happens at, expected {:?}, got {:?}",
                spend_height, got
            ),
            other => panic!(
                "a transaction with no transparent outputs must report OnlyShieldedOutputs, got {:?}",
                other
            ),
        }
    }

    // The two variants are distinguishable, and the shielded one is not equal
    // to a shielded one at a different height.
    assert_ne!(
        fully_shielded.coinbase_spend_restriction(Height(1)),
        fully_shielded.coinbase_spend_restriction(Height(2)),
        "OnlyShieldedOutputs at different heights must not compare equal"
    );
    assert_ne!(
        fully_shielded.coinbase_spend_restriction(Height(1)),
        CoinbaseSpendRestriction::SomeTransparentOutputs,
        "the two CoinbaseSpendRestriction variants must not compare equal"
    );
}

#[test]
fn a_prevout_input_exposes_its_outpoint_and_a_coinbase_input_does_not() {
    let mut coinbase_inputs = 0usize;
    let mut prevout_inputs = 0usize;

    for fixture in blocks() {
        let block = parse(&fixture);

        for (tx_index_in_block, transaction) in block.transactions.iter().enumerate() {
            for input in transaction.inputs().iter() {
                match input {
                    transparent::Input::Coinbase { .. } => {
                        assert_eq!(
                            input.outpoint(),
                            None,
                            "{}: the coinbase input of transaction {} must have no outpoint",
                            fixture.name,
                            tx_index_in_block
                        );
                        coinbase_inputs += 1;
                    }
                    transparent::Input::PrevOut { outpoint, .. } => {
                        assert_eq!(
                            input.outpoint(),
                            Some(*outpoint),
                            "{}: a PrevOut input of transaction {} must report its own outpoint",
                            fixture.name,
                            tx_index_in_block
                        );
                        prevout_inputs += 1;
                    }
                }
            }
        }
    }

    assert_eq!(
        coinbase_inputs, 17,
        "the corpus has seventeen blocks, so exactly seventeen coinbase inputs, found {}",
        coinbase_inputs
    );
    assert!(
        prevout_inputs > 0,
        "the corpus must contain at least one PrevOut input"
    );
}
