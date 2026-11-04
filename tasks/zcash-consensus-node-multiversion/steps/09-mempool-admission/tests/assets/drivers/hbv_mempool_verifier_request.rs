//! The transaction verifier's two request kinds: block and mempool.
//!
//! Harbor verifier driver -- injected into `zebra-consensus/tests/` at grading
//! time. Public API only; no `zebra_test`, no proptest. `zebra-consensus` does
//! not depend on the `hex` crate, so this file carries its own decoder.
//!
//! This milestone is where transaction validity becomes tip-dependent. A
//! transaction is no longer verified only as "transaction number `i` of block
//! `h`"; it can also be verified speculatively, as a candidate for the block
//! that has not been mined yet. Those two situations use the same consensus
//! rules for almost everything and differ in three ways, so the verifier takes
//! one request type with two variants:
//!
//! ```text
//! Request::Block   { transaction: Arc<Transaction>, known_utxos, height }
//! Request::Mempool { transaction: UnminedTx,                    height }
//! ```
//!
//! 1. A block request already knows the transaction's position, so the block
//!    verifier can hand down the outputs created earlier in the same block as
//!    `known_utxos`. A mempool transaction has no such context: its inputs must
//!    already be on chain, so `known_utxos()` is empty for it.
//! 2. A block request carries a bare `Arc<Transaction>`; a mempool request
//!    carries an `UnminedTx`, because the identifier a mempool entry is keyed
//!    by was computed when it arrived over the network and must not change.
//! 3. `height` means different things: for a block it is that block's height,
//!    for the mempool it is the height of the *next* block -- the first block
//!    that could possibly contain the transaction. Both select the consensus
//!    rules through `upgrade()`.
//!
//! The verifier answers with `UnminedTxId` (a type alias, not a new enum), so a
//! caller that pipelined several transactions can match each reply to its
//! request, and the mempool can insert under exactly the key the network layer
//! used.
//!
//! Finally, the one rule that is genuinely new rather than restructured:
//! a coinbase transaction can never enter the mempool, and that rejection has
//! its own error variant, `TransactionError::CoinbaseInMempool`.
//!
//! This file does not drive `transaction::Verifier` itself. Constructing one
//! needs a `script::Verifier`, and `zebra_consensus`'s `script` module is
//! private at this tag, so the service is unreachable from an integration test
//! -- see TEST_INTERFACE §9. What is reachable, and what every caller of the
//! verifier is written against, is the request type, its accessors, the
//! response alias and the error variant; that is what is scored here.
//!
//! Expected values: the published ZIP-244 v5 test vectors and the shipped
//! seventeen-block corpus, both read from the fixture directory.

use std::collections::HashMap;
use std::convert::TryFrom;
use std::io::Cursor;
use std::path::PathBuf;
use std::sync::Arc;

use zebra_chain::amount::{Amount, NonNegative};
use zebra_chain::block::{self, Block};
use zebra_chain::parameters::{Network, NetworkUpgrade};
use zebra_chain::serialization::ZcashDeserialize;
use zebra_chain::transaction::{self, Transaction, UnminedTx, UnminedTxId};
use zebra_chain::transparent;

use zebra_consensus::error::TransactionError;
use zebra_consensus::transaction::{Request, Response};

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

fn to_hex(bytes: &[u8]) -> String {
    let mut out = String::with_capacity(bytes.len() * 2);
    for byte in bytes {
        out.push_str(&format!("{:02x}", byte));
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

/// A published ZIP-244 vector: a v5 transaction with its txid and auth digest.
struct V5Vector {
    name: String,
    transaction: Arc<Transaction>,
    txid: [u8; 32],
    auth_digest: [u8; 32],
}

fn v5_vectors() -> Vec<V5Vector> {
    let (header, rows) = tsv("zip244.tsv");
    let mut out = Vec::new();
    for row in &rows {
        let name = column(&header, row, "name").to_string();
        let raw = unhex(column(&header, row, "tx_hex"));
        let transaction = Transaction::zcash_deserialize(Cursor::new(&raw[..]))
            .unwrap_or_else(|e| panic!("{} did not parse: {:?}", name, e));
        out.push(V5Vector {
            txid: thirty_two(column(&header, row, "txid_hex"), "txid_hex"),
            auth_digest: thirty_two(column(&header, row, "auth_digest_hex"), "auth_digest_hex"),
            name,
            transaction: Arc::new(transaction),
        });
    }
    // Corpus guard, not a code assertion: if zip244.tsv changes, this is where
    // the change is reported.
    assert_eq!(out.len(), 10, "zip244.tsv should hold the 10 shipped vectors");
    out
}

/// One named block from the shipped corpus.
fn block_named(name: &str) -> Block {
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
        let block = block_named(&name);
        for (index, transaction) in block.transactions.iter().enumerate() {
            out.push((name.clone(), index, transaction.clone()));
        }
    }
    // Corpus guard, not a code assertion.
    assert_eq!(rows.len(), 17, "the corpus should have 17 blocks");
    assert!(
        out.len() >= 40,
        "the corpus yielded only {} transactions",
        out.len()
    );
    out
}

// ----------------------------------------------------------------- helpers

/// A plausible set of outputs created earlier in the same block, built entirely
/// from public constructors.
fn some_known_utxos(
    tip: &Transaction,
    height: block::Height,
) -> Arc<HashMap<transparent::OutPoint, transparent::OrderedUtxo>> {
    let mut map = HashMap::new();
    let output = transparent::Output {
        value: Amount::<NonNegative>::try_from(12_345).expect("12345 zatoshi is a valid amount"),
        lock_script: transparent::Script::new(&[0x51]),
    };
    let outpoint = transparent::OutPoint {
        hash: tip.hash(),
        index: 0,
    };
    map.insert(
        outpoint,
        transparent::OrderedUtxo::new(output, height, false, 1),
    );
    Arc::new(map)
}

fn mempool_request(transaction: Arc<Transaction>, height: u32) -> Request {
    Request::Mempool {
        transaction: transaction.into(),
        height: block::Height(height),
    }
}

fn block_request(transaction: Arc<Transaction>, height: u32) -> Request {
    let height = block::Height(height);
    Request::Block {
        known_utxos: some_known_utxos(transaction.as_ref(), height),
        transaction,
        height,
    }
}

/// Accepts the verifier's declared response type and returns an `UnminedTxId`.
/// This only compiles if `Response` is an alias for `UnminedTxId` rather than a
/// distinct type.
fn response_is_an_unmined_tx_id(response: Response) -> UnminedTxId {
    response
}

// ------------------------------------------------------------------- tests

#[test]
fn the_two_request_kinds_are_distinguished_by_is_mempool() {
    // Every consensus check that differs between a block and the mempool
    // branches on this, so it must be exact -- not a heuristic on the payload.
    let vector = v5_vectors().remove(0);

    let mempool = mempool_request(vector.transaction.clone(), 1_100_000);
    assert!(
        mempool.is_mempool(),
        "{}: a Request::Mempool did not report itself as a mempool request",
        vector.name
    );

    let in_block = block_request(vector.transaction.clone(), 1_100_000);
    assert!(
        !in_block.is_mempool(),
        "{}: a Request::Block reported itself as a mempool request",
        vector.name
    );

    for (name, index, transaction) in corpus_transactions() {
        assert!(
            !block_request(transaction.clone(), 1).is_mempool(),
            "{}#{}: a Request::Block reported itself as a mempool request",
            name,
            index
        );
        assert!(
            mempool_request(transaction, 1).is_mempool(),
            "{}#{}: a Request::Mempool did not report itself as a mempool request",
            name,
            index
        );
    }
}

#[test]
fn a_mempool_request_reuses_the_identifier_the_transaction_arrived_with() {
    // The mempool is keyed by the id the network layer produced. If the request
    // recomputed it, a v5 transaction whose authorizing data was mangled in
    // transit would silently be filed under the wrong key.
    for vector in v5_vectors() {
        let unmined: UnminedTx = vector.transaction.clone().into();
        let expected = unmined.id;

        let request = Request::Mempool {
            transaction: unmined,
            height: block::Height(1_100_000),
        };

        assert_eq!(
            request.tx_id(),
            expected,
            "{}: the mempool request's tx_id is not the id it was given",
            vector.name
        );

        let id = request.tx_id();
        assert_eq!(
            to_hex(&id.mined_id().0),
            to_hex(&vector.txid),
            "{}: the request's mined id is not the published ZIP-244 txid",
            vector.name
        );
        let auth = id
            .auth_digest()
            .unwrap_or_else(|| panic!("{}: a v5 request must carry an auth digest", vector.name));
        assert_eq!(
            to_hex(&<[u8; 32]>::from(&auth)),
            to_hex(&vector.auth_digest),
            "{}: the request's auth digest is not the published ZIP-244 auth digest",
            vector.name
        );
    }
}

#[test]
fn a_block_request_derives_the_identifier_from_the_transaction_it_carries() {
    // A block request has no precomputed id, so it must produce the same
    // `UnminedTxId` the transaction itself does -- legacy for v1-v4.
    for (name, index, transaction) in corpus_transactions() {
        let request = block_request(transaction.clone(), 100);
        let id = request.tx_id();

        assert_eq!(
            id,
            transaction.unmined_id(),
            "{}#{}: the block request's tx_id is not the transaction's unmined id",
            name,
            index
        );
        assert_eq!(
            to_hex(&id.mined_id().0),
            to_hex(&transaction.hash().0),
            "{}#{}: the block request's mined id is not the transaction hash",
            name,
            index
        );
        assert!(
            id.auth_digest().is_none(),
            "{}#{}: a v1-v4 transaction must have a legacy id with no auth digest",
            name,
            index
        );
    }
}

#[test]
fn both_request_kinds_hand_back_the_transaction_they_were_given() {
    for (name, index, transaction) in corpus_transactions() {
        let from_block = block_request(transaction.clone(), 7);
        assert_eq!(
            from_block.transaction().as_ref(),
            transaction.as_ref(),
            "{}#{}: Request::Block::transaction() returned a different transaction",
            name,
            index
        );

        let from_mempool = mempool_request(transaction.clone(), 7);
        assert_eq!(
            from_mempool.transaction().as_ref(),
            transaction.as_ref(),
            "{}#{}: Request::Mempool::transaction() returned a different transaction",
            name,
            index
        );
    }

    for vector in v5_vectors() {
        let request = mempool_request(vector.transaction.clone(), 1_100_000);
        assert_eq!(
            request.transaction().as_ref(),
            vector.transaction.as_ref(),
            "{}: the mempool request unwrapped a different transaction",
            vector.name
        );
    }
}

#[test]
fn only_a_block_request_carries_known_utxos() {
    // A mempool transaction's inputs must already be on chain; there is no
    // partially-built block to draw extra outputs from. Returning anything
    // non-empty here would let an unconfirmed transaction spend an output that
    // does not exist yet.
    let (name, _index, transaction) = corpus_transactions().remove(0);

    let in_block = block_request(transaction.clone(), 100);
    let utxos = in_block.known_utxos();
    assert_eq!(
        utxos.len(),
        1,
        "{}: a block request lost the known utxos it was built with",
        name
    );
    let outpoint = transparent::OutPoint {
        hash: transaction.hash(),
        index: 0,
    };
    let utxo = utxos
        .get(&outpoint)
        .unwrap_or_else(|| panic!("{}: the known utxo is missing from the request", name));
    assert_eq!(
        utxo.tx_index_in_block, 1,
        "{}: the known utxo's position in the block changed",
        name
    );

    let in_mempool = mempool_request(transaction, 100);
    assert!(
        in_mempool.known_utxos().is_empty(),
        "{}: a mempool request offered {} known utxos, expected none",
        name,
        in_mempool.known_utxos().len()
    );
}

#[test]
fn the_request_height_selects_the_consensus_rules() {
    // `height` is the block's own height for a block request, and the height of
    // the *next* block for a mempool request; in both cases it is what
    // `upgrade()` resolves against. The pairs below are the published
    // activation heights and their immediate predecessors.
    let (_name, _index, transaction) = corpus_transactions().remove(0);

    let mainnet_cases: Vec<(u32, NetworkUpgrade)> = vec![
        (0, NetworkUpgrade::Genesis),
        (1, NetworkUpgrade::BeforeOverwinter),
        (347_499, NetworkUpgrade::BeforeOverwinter),
        (347_500, NetworkUpgrade::Overwinter),
        (419_199, NetworkUpgrade::Overwinter),
        (419_200, NetworkUpgrade::Sapling),
        (653_599, NetworkUpgrade::Sapling),
        (653_600, NetworkUpgrade::Blossom),
        (902_999, NetworkUpgrade::Blossom),
        (903_000, NetworkUpgrade::Heartwood),
        (1_046_400, NetworkUpgrade::Canopy),
    ];
    let testnet_cases: Vec<(u32, NetworkUpgrade)> = vec![
        (207_500, NetworkUpgrade::Overwinter),
        (280_000, NetworkUpgrade::Sapling),
        (584_000, NetworkUpgrade::Blossom),
        (903_800, NetworkUpgrade::Heartwood),
        (1_028_500, NetworkUpgrade::Canopy),
    ];

    for (network, cases) in vec![
        (Network::Mainnet, mainnet_cases),
        (Network::Testnet, testnet_cases),
    ] {
        for (height, expected) in cases {
            for request in vec![
                block_request(transaction.clone(), height),
                mempool_request(transaction.clone(), height),
            ] {
                assert_eq!(
                    request.height(),
                    block::Height(height),
                    "{:?}: the request did not keep the height it was built with",
                    network
                );
                assert_eq!(
                    request.upgrade(network),
                    expected,
                    "{:?} height {}: the request selected {:?}, expected {:?}",
                    network,
                    height,
                    request.upgrade(network),
                    expected
                );
            }
        }
    }
}

#[test]
fn a_verified_transaction_is_answered_with_its_unmined_id() {
    // `Response` is an alias for `UnminedTxId`, not a wrapper: the caller
    // matches the reply against the id it submitted, with no unwrapping.
    for vector in v5_vectors() {
        let request = mempool_request(vector.transaction.clone(), 1_100_000);
        let response: Response = request.tx_id();

        assert_eq!(
            response_is_an_unmined_tx_id(response),
            request.tx_id(),
            "{}: the response id is not the request id",
            vector.name
        );
        assert_eq!(
            to_hex(&response.mined_id().0),
            to_hex(&vector.txid),
            "{}: the response's mined id is not the published txid",
            vector.name
        );
    }

    for (name, index, transaction) in corpus_transactions().into_iter().take(12) {
        let response: Response = block_request(transaction.clone(), 100).tx_id();
        assert_eq!(
            response,
            UnminedTxId::from_legacy_id(transaction.hash()),
            "{}#{}: a v1-v4 response is not the legacy id of its transaction",
            name,
            index
        );
    }
}

#[test]
fn a_coinbase_transaction_is_rejected_from_the_mempool_by_its_own_error() {
    // A coinbase transaction can only ever be the first transaction of a block,
    // so it can never be admitted as an unconfirmed transaction. This rejection
    // is distinct from the block-context coinbase rules, and has its own
    // variant, so a caller can tell "you sent me a coinbase" apart from "your
    // coinbase is malformed".
    let error = TransactionError::CoinbaseInMempool;

    assert_eq!(
        error.to_string(),
        "coinbase inputs MUST NOT exist in mempool",
        "CoinbaseInMempool displayed as {:?}",
        error.to_string()
    );
    assert_ne!(
        error,
        TransactionError::CoinbasePosition,
        "CoinbaseInMempool must be distinct from CoinbasePosition"
    );
    assert_ne!(
        error,
        TransactionError::CoinbaseAfterFirst,
        "CoinbaseInMempool must be distinct from CoinbaseAfterFirst"
    );
    assert_ne!(
        error,
        TransactionError::CoinbaseHasPrevOutInput,
        "CoinbaseInMempool must be distinct from CoinbaseHasPrevOutInput"
    );
    assert_eq!(
        error.clone(),
        TransactionError::CoinbaseInMempool,
        "TransactionError must be Clone and compare equal to itself"
    );

    // The condition the variant names: the first transaction of every corpus
    // block is exactly the one that would trigger it.
    let mut coinbases = 0usize;
    for (name, index, transaction) in corpus_transactions() {
        let is_coinbase = transaction.has_any_coinbase_inputs();
        assert_eq!(
            is_coinbase,
            index == 0,
            "{}#{}: has_any_coinbase_inputs() returned {}, expected {}",
            name,
            index,
            is_coinbase,
            index == 0
        );

        if is_coinbase {
            coinbases += 1;
            let request = mempool_request(transaction, 100);
            assert!(
                request.is_mempool() && request.transaction().has_any_coinbase_inputs(),
                "{}: a mempool request carrying a coinbase must be recognisable as such",
                name
            );
        }
    }
    // Corpus guard, not a code assertion.
    assert_eq!(coinbases, 17, "every corpus block has one coinbase");
}

#[test]
fn requests_compare_by_their_contents() {
    // The verifier deduplicates in-flight work by comparing requests, so two
    // requests for the same transaction at the same height are the same
    // request, and a change to any field makes a different one.
    let (name, _index, transaction) = corpus_transactions().remove(0);

    let a = mempool_request(transaction.clone(), 100);
    let b = mempool_request(transaction.clone(), 100);
    assert_eq!(
        a, b,
        "{}: two identical mempool requests did not compare equal",
        name
    );

    let different_height = mempool_request(transaction.clone(), 101);
    assert_ne!(
        a, different_height,
        "{}: mempool requests at different heights compared equal",
        name
    );

    let as_block = block_request(transaction.clone(), 100);
    assert_ne!(
        a, as_block,
        "{}: a mempool request compared equal to a block request",
        name
    );

    let (other_name, _other_index, other_transaction) = corpus_transactions()
        .into_iter()
        .find(|(_, index, tx)| *index == 0 && tx.hash() != transaction.hash())
        .expect("the corpus has more than one distinct coinbase transaction");
    assert_ne!(
        a,
        mempool_request(other_transaction, 100),
        "{} and {}: requests for different transactions compared equal",
        name,
        other_name
    );
}

#[test]
fn a_mempool_entry_is_keyed_by_the_identifier_its_version_requires() {
    // The whole point of routing v1-v4 and v5 through one request type: the
    // caller never chooses the identifier, the version does.
    for vector in v5_vectors() {
        let id = mempool_request(vector.transaction.clone(), 1_100_000).tx_id();
        match id {
            UnminedTxId::Witnessed(wtx_id) => {
                assert_eq!(
                    to_hex(&wtx_id.as_bytes()[..]),
                    format!("{}{}", to_hex(&vector.txid), to_hex(&vector.auth_digest)),
                    "{}: the witnessed id is not txid || auth_digest",
                    vector.name
                );
            }
            UnminedTxId::Legacy(hash) => panic!(
                "{}: a v5 transaction was keyed by the legacy id {}",
                vector.name, hash
            ),
        }
    }

    for (name, index, tx) in corpus_transactions() {
        let id = mempool_request(tx.clone(), 100).tx_id();
        match id {
            UnminedTxId::Legacy(hash) => {
                let expected: transaction::Hash = tx.hash();
                assert_eq!(
                    hash, expected,
                    "{}#{}: the legacy id is not the transaction hash",
                    name, index
                );
            }
            UnminedTxId::Witnessed(wtx_id) => panic!(
                "{}#{}: a v1-v4 transaction was keyed by the witnessed id {}",
                name, index, wtx_id
            ),
        }
    }
}
