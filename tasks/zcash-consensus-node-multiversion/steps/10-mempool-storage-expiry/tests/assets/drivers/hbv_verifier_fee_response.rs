//! What the transaction verifier answers with, now that the answer has to
//! carry a fee.
//!
//! Harbor verifier driver -- injected into `zebra-consensus/tests/` at grading
//! time. Public API only; no `zebra_test`, no proptest. `zebra-consensus` does
//! not depend on the `hex` crate, so this file carries its own decoder.
//!
//! At the previous milestone the verifier's reply was a bare identifier:
//! `pub type Response = UnminedTxId`. That was enough to match a reply to a
//! request and no more. A mempool that has to bound its own size cannot work
//! with that, because every decision it makes -- which transaction to keep,
//! which to evict, what to charge for admission -- is a decision about fees,
//! and the fee is only known to the component that just finished checking the
//! transaction's value balance.
//!
//! So the reply becomes a two-variant enum that mirrors the request:
//!
//! ```text
//! Response::Block   { tx_id: UnminedTxId, miner_fee: Option<Amount<NonNegative>> }
//! Response::Mempool { transaction: VerifiedUnminedTx }
//! ```
//!
//! A block reply carries only the identifier and the fee, because the block
//! verifier already holds the transaction. A mempool reply carries the whole
//! `VerifiedUnminedTx` -- transaction, identifier, serialized size and fee --
//! because that struct is exactly what the mempool stores. A block's coinbase
//! transaction has no miner fee at all (its remaining transparent value is
//! destroyed rather than paid), which is why only the `Block` variant's fee is
//! optional; a mempool transaction always has one, because coinbase
//! transactions are rejected from the mempool.
//!
//! The failure path gets its own name at the same time:
//! `TransactionError::IncorrectFee`.
//!
//! This file does not drive `transaction::Verifier` itself: constructing one
//! needs a `script::Verifier`, and `zebra_consensus`'s `script` module is
//! private at this tag, so the service is unreachable from an integration test
//! -- see TEST_INTERFACE. What is reachable, and what every caller of the
//! verifier is written against, is the response type, its accessors, its
//! conversion and the error variant; that is what is scored here.
//!
//! Expected values: the shipped seventeen-block corpus, read from the fixture
//! directory. The ZIP-244 vector file shipped at earlier steps is not used at
//! this milestone (its v5 transactions carry the superseded NU5 branch id and
//! no longer deserialize). No expected value is computed with the accessor it
//! checks.

use std::convert::TryFrom;
use std::io::Cursor;
use std::path::PathBuf;
use std::sync::Arc;

use zebra_chain::amount::{Amount, NonNegative};
use zebra_chain::block::Block;
use zebra_chain::serialization::ZcashDeserialize;
use zebra_chain::transaction::{Transaction, UnminedTx, UnminedTxId, VerifiedUnminedTx};

use zebra_consensus::error::TransactionError;
use zebra_consensus::transaction::Response;

// ------------------------------------------------------------ hex, locally

fn nibble(c: u8) -> u8 {
    match c {
        b'0'..=b'9' => c - b'0',
        b'a'..=b'f' => c - b'a' + 10,
        b'A'..=b'F' => c - b'A' + 10,
        other => panic!("{:?} is not a hex digit", other as char),
    }
}

fn unhex(text: &str) -> Vec<u8> {
    let text = text.trim();
    assert_eq!(text.len() % 2, 0, "hex string has an odd number of digits");
    let bytes = text.as_bytes();
    let mut out = Vec::with_capacity(text.len() / 2);
    for pair in bytes.chunks(2) {
        out.push((nibble(pair[0]) << 4) | nibble(pair[1]));
    }
    out
}

// ---------------------------------------------------------------- fixtures

fn fixture_dir() -> PathBuf {
    PathBuf::from(
        std::env::var("HBV_FIXTURES")
            .expect("HBV_FIXTURES must point at the shipped fixture directory"),
    )
}

fn tsv(file: &str) -> (Vec<String>, Vec<Vec<String>>) {
    let text = std::fs::read_to_string(fixture_dir().join(file))
        .unwrap_or_else(|e| panic!("cannot read {}: {}", file, e));
    let mut lines = text.lines();
    let header: Vec<String> = lines
        .next()
        .unwrap_or_else(|| panic!("{} has no header row", file))
        .trim_start_matches("# ")
        .split('\t')
        .map(|s| s.to_string())
        .collect();

    let mut rows = Vec::new();
    for line in lines {
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        let cells: Vec<String> = line.split('\t').map(|s| s.to_string()).collect();
        assert_eq!(
            cells.len(),
            header.len(),
            "{}: row has {} cells, header has {}",
            file,
            cells.len(),
            header.len()
        );
        rows.push(cells);
    }
    assert!(!rows.is_empty(), "{} has no data rows", file);
    (header, rows)
}

fn column<'a>(header: &[String], row: &'a [String], name: &str) -> &'a str {
    let index = header
        .iter()
        .position(|h| h == name)
        .unwrap_or_else(|| panic!("no column named {}", name));
    row[index].as_str()
}

fn thirty_two(text: &str, what: &str) -> [u8; 32] {
    let bytes = unhex(text);
    assert_eq!(bytes.len(), 32, "{} is not 32 bytes", what);
    let mut out = [0u8; 32];
    out.copy_from_slice(&bytes);
    out
}

/// One named block from the shipped corpus, parsed.
fn block_of(name: &str) -> Block {
    let path = fixture_dir().join("blocks").join(format!("{}.hex", name));
    let text =
        std::fs::read_to_string(&path).unwrap_or_else(|e| panic!("cannot read {}: {}", name, e));
    let raw = unhex(text.trim());
    Block::zcash_deserialize(Cursor::new(&raw[..]))
        .unwrap_or_else(|e| panic!("{} did not parse: {:?}", name, e))
}

/// Every transaction of every corpus block, with the name of the block it came
/// from and its index inside that block.
fn corpus_transactions() -> Vec<(String, usize, Arc<Transaction>)> {
    let (header, rows) = tsv("blocks.tsv");
    let mut out = Vec::new();
    for row in &rows {
        let name = column(&header, row, "name").to_string();
        let block = block_of(&name);
        for (index, transaction) in block.transactions.iter().enumerate() {
            out.push((name.clone(), index, transaction.clone()));
        }
    }
    // Corpus assertion, not a code assertion.
    assert_eq!(rows.len(), 17, "the corpus should have 17 blocks");
    assert!(
        out.len() >= 40,
        "the corpus yielded only {} transactions",
        out.len()
    );
    out
}

// ----------------------------------------------------------------- helpers

fn to_hex(bytes: &[u8]) -> String {
    let mut out = String::with_capacity(bytes.len() * 2);
    for byte in bytes {
        out.push_str(&format!("{:02x}", byte));
    }
    out
}

fn fee(zatoshis: i64) -> Amount<NonNegative> {
    Amount::try_from(zatoshis)
        .unwrap_or_else(|e| panic!("{} zatoshis is not a valid fee: {:?}", zatoshis, e))
}

fn verified(transaction: Arc<Transaction>, zatoshis: i64) -> VerifiedUnminedTx {
    VerifiedUnminedTx::new(UnminedTx::from(transaction), fee(zatoshis))
}

// ------------------------------------------------------------------- tests

#[test]
fn a_mempool_response_carries_the_whole_verified_transaction() {
    for (name, index, transaction) in corpus_transactions().into_iter().take(20) {
        let zatoshis = 1_000 + (index as i64) * 7;
        let expected = verified(transaction.clone(), zatoshis);

        let response = Response::Mempool {
            transaction: expected.clone(),
        };

        match response {
            Response::Mempool { transaction: got } => {
                assert_eq!(
                    got, expected,
                    "{} transaction {}: the mempool response changed the verified transaction",
                    name, index
                );
                assert_eq!(
                    got.miner_fee,
                    fee(zatoshis),
                    "{} transaction {}: the mempool response changed the fee",
                    name,
                    index
                );
                assert_eq!(
                    *got.transaction.transaction, *transaction,
                    "{} transaction {}: the mempool response changed the transaction",
                    name, index
                );
            }
            other => panic!(
                "{} transaction {}: a Mempool response matched another variant: {:?}",
                name, index, other
            ),
        }
    }
}

#[test]
fn a_mempool_response_always_reports_a_fee() {
    // A coinbase transaction can never enter the mempool, so a mempool reply
    // never has to say "no fee": its fee is always present.
    for (name, index, transaction) in corpus_transactions().into_iter().take(20) {
        let zatoshis = 5_000 + (index as i64);
        let response = Response::Mempool {
            transaction: verified(transaction, zatoshis),
        };
        assert_eq!(
            response.miner_fee(),
            Some(fee(zatoshis)),
            "{} transaction {}: the mempool response reported {:?} instead of {} zatoshis",
            name,
            index,
            response.miner_fee(),
            zatoshis
        );
    }
}

#[test]
fn a_block_response_reports_the_miner_fee_it_was_built_with() {
    for (name, index, transaction) in corpus_transactions().into_iter().take(20) {
        let zatoshis = 10_000 + (index as i64) * 3;
        let response = Response::Block {
            tx_id: UnminedTxId::from(&*transaction),
            miner_fee: Some(fee(zatoshis)),
        };
        assert_eq!(
            response.miner_fee(),
            Some(fee(zatoshis)),
            "{} transaction {}: the block response reported {:?} instead of {} zatoshis",
            name,
            index,
            response.miner_fee(),
            zatoshis
        );
    }
}

#[test]
fn a_block_response_for_a_coinbase_transaction_reports_no_miner_fee() {
    // The remaining transparent value of a coinbase transaction is destroyed
    // rather than paid to the miner, so a coinbase reply has no fee at all.
    // That is the whole reason a block reply's fee is optional.
    let (header, rows) = tsv("blocks.tsv");
    let mut checked = 0;
    for row in &rows {
        let name = column(&header, row, "name").to_string();
        let block = block_of(&name);
        let coinbase = block.transactions[0].clone();

        let response = Response::Block {
            tx_id: UnminedTxId::from(&*coinbase),
            miner_fee: None,
        };
        assert_eq!(
            response.miner_fee(),
            None,
            "{}: a coinbase block response reported a miner fee",
            name
        );
        checked += 1;
    }
    // Corpus assertion, not a code assertion.
    assert_eq!(checked, 17, "the corpus should have 17 blocks");
}

#[test]
fn only_a_mempool_response_yields_a_verified_transaction() {
    for (name, index, transaction) in corpus_transactions().into_iter().take(20) {
        let expected = verified(transaction.clone(), 777);

        let mempool = Response::Mempool {
            transaction: expected.clone(),
        };
        assert_eq!(
            mempool.into_mempool_transaction(),
            Some(expected),
            "{} transaction {}: a mempool response did not yield its verified transaction",
            name,
            index
        );

        let block = Response::Block {
            tx_id: UnminedTxId::from(&*transaction),
            miner_fee: Some(fee(777)),
        };
        assert_eq!(
            block.into_mempool_transaction(),
            None,
            "{} transaction {}: a block response yielded a verified transaction",
            name,
            index
        );
    }
}

#[test]
fn a_response_reports_which_kind_of_request_it_answers() {
    for (name, index, transaction) in corpus_transactions().into_iter().take(20) {
        let mempool = Response::Mempool {
            transaction: verified(transaction.clone(), 42),
        };
        assert!(
            mempool.is_mempool(),
            "{} transaction {}: a mempool response does not report itself as one",
            name,
            index
        );

        let block = Response::Block {
            tx_id: UnminedTxId::from(&*transaction),
            miner_fee: None,
        };
        assert!(
            !block.is_mempool(),
            "{} transaction {}: a block response reports itself as a mempool response",
            name,
            index
        );
    }
}

#[test]
fn both_response_variants_report_the_transaction_id_the_corpus_records() {
    // In a block with exactly one transaction the merkle root IS that
    // transaction's id, in wire byte order. The corpus records the root
    // independently of anything this crate computes, so it is the expected
    // value here.
    let (header, rows) = tsv("blocks.tsv");
    let mut checked = 0;
    for row in &rows {
        let name = column(&header, row, "name").to_string();
        let tx_count: usize = column(&header, row, "tx_count")
            .parse()
            .unwrap_or_else(|e| panic!("{}: tx_count is not a number: {}", name, e));
        if tx_count != 1 {
            continue;
        }
        let expected = thirty_two(column(&header, row, "merkle_root_hex"), "merkle_root_hex");
        let block = block_of(&name);
        let only = block.transactions[0].clone();

        let block_response = Response::Block {
            tx_id: UnminedTxId::from(&*only),
            miner_fee: Some(fee(1_500)),
        };
        assert_eq!(
            to_hex(&block_response.tx_id().mined_id().0),
            to_hex(&expected),
            "{}: the block response's mined id is not the block's merkle root",
            name
        );

        let mempool_response = Response::Mempool {
            transaction: verified(only, 1_500),
        };
        assert_eq!(
            to_hex(&mempool_response.tx_id().mined_id().0),
            to_hex(&expected),
            "{}: the mempool response's mined id is not the block's merkle root",
            name
        );
        assert_eq!(
            mempool_response.tx_id().auth_digest(),
            None,
            "{}: a v1-v4 transaction id reported an authorizing data digest",
            name
        );

        checked += 1;
    }
    // Corpus assertion, not a code assertion: the corpus holds nine blocks
    // whose single transaction is the coinbase.
    assert_eq!(
        checked, 9,
        "the corpus supplied {} single-transaction blocks, the verifier expects 9",
        checked
    );
}

#[test]
fn a_verified_transaction_converts_straight_into_a_mempool_response() {
    for (name, index, transaction) in corpus_transactions().into_iter().take(20) {
        let expected = verified(transaction, 9_999);
        let converted = Response::from(expected.clone());
        assert_eq!(
            converted,
            Response::Mempool {
                transaction: expected
            },
            "{} transaction {}: From<VerifiedUnminedTx> did not build a Mempool response",
            name,
            index
        );
        assert!(
            converted.is_mempool(),
            "{} transaction {}: the converted response is not a mempool response",
            name,
            index
        );
    }
}

#[test]
fn two_responses_are_equal_exactly_when_their_contents_are() {
    let transactions = corpus_transactions();
    let (name, _, first) = transactions[0].clone();
    let (_, _, second) = transactions
        .iter()
        .find(|(_, _, t)| **t != *first)
        .cloned()
        .unwrap_or_else(|| panic!("the corpus has only one distinct transaction"));

    let a = Response::Mempool {
        transaction: verified(first.clone(), 100),
    };
    assert_eq!(a.clone(), a, "{}: a mempool response is not equal to itself", name);

    let different_fee = Response::Mempool {
        transaction: verified(first.clone(), 101),
    };
    assert_ne!(
        a, different_fee,
        "{}: two mempool responses with different fees compared equal",
        name
    );

    let different_transaction = Response::Mempool {
        transaction: verified(second.clone(), 100),
    };
    assert_ne!(
        a, different_transaction,
        "{}: two mempool responses with different transactions compared equal",
        name
    );

    let block = Response::Block {
        tx_id: UnminedTxId::from(&*first),
        miner_fee: Some(fee(100)),
    };
    assert_ne!(
        a, block,
        "{}: a mempool response compared equal to a block response",
        name
    );
    assert_ne!(
        block,
        Response::Block {
            tx_id: UnminedTxId::from(&*first),
            miner_fee: None,
        },
        "{}: a block response with a fee compared equal to one without",
        name
    );
}

#[test]
fn the_fee_error_reports_that_the_fee_could_not_be_calculated() {
    let error = TransactionError::IncorrectFee;
    assert_eq!(
        format!("{}", error),
        "could not calculate the transaction fee",
        "TransactionError::IncorrectFee does not display its documented message"
    );
    assert_eq!(
        error.clone(),
        TransactionError::IncorrectFee,
        "TransactionError::IncorrectFee is not equal to itself"
    );
    assert_ne!(
        error,
        TransactionError::CoinbaseInMempool,
        "IncorrectFee compared equal to a different transaction error"
    );
}
