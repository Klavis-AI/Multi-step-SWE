//! The network service's unconfirmed-transaction request and response shapes.
//!
//! Harbor verifier driver -- injected into `zebra-network/tests/` at grading
//! time. Public API only; no `zebra_test`, no proptest.
//!
//! Before this milestone the network layer moved unconfirmed transactions
//! around by mined transaction hash: it asked for `HashSet<transaction::Hash>`,
//! pushed an `Arc<Transaction>` and answered with `Vec<transaction::Hash>`.
//! That is unusable for a mempool once v5 exists, because a v5 transaction's
//! mined ID does not commit to its authorizing data -- two different peers can
//! offer two different transactions under one mined ID.
//!
//! So the whole unconfirmed half of `zebra_network::{Request, Response}` moves
//! to the ZIP-239 identifiers at this milestone:
//!
//!   * `Request::TransactionsById(HashSet<UnminedTxId>)` -- download these
//!   * `Request::AdvertiseTransactionIds(HashSet<UnminedTxId>)` -- gossip these
//!   * `Request::PushTransaction(UnminedTx)` -- send this one unsolicited
//!   * `Request::MempoolTransactionIds` -- what is in your mempool?
//!   * `Response::Transactions(Vec<UnminedTx>)`
//!   * `Response::TransactionIds(Vec<UnminedTxId>)`
//!
//! and the block half (`BlocksByHash`, `AdvertiseBlock`, `FindBlocks`,
//! `FindHeaders`, `Blocks`, `BlockHashes`, `BlockHeaders`, `Peers`, `Nil`) is
//! untouched, because a block is still identified by its hash.
//!
//! This file scores the *shape* of that API: which variants exist, what each
//! carries, and that the pair composes -- a listing response must be usable
//! directly as a download request, which is the mempool crawler's entire loop.
//! It does not drive the peer set; a connected peer set is not constructible
//! from an integration test (see TEST_INTERFACE §9).
//!
//! `Request` and `Response` derive only `Clone` and `Debug` at this tag, so
//! every assertion below matches on the variant and inspects the payload rather
//! than comparing whole values.
//!
//! Expected values come from the shipped corpus: block transaction hashes are
//! fixed by the merkle roots miners committed years ago, and the v5 txids and
//! auth digests are the published ZIP-244 test vectors.

use std::collections::{BTreeSet, HashSet};
use std::io::Cursor;
use std::path::PathBuf;
use std::sync::Arc;

use zebra_chain::block::Block;
use zebra_chain::serialization::{ZcashDeserialize, ZcashSerialize};
use zebra_chain::transaction::{AuthDigest, Hash, Transaction, UnminedTx, UnminedTxId, WtxId};
use zebra_network::{Request, Response};

// ---------------------------------------------------------------- fixtures

fn fixture_dir() -> PathBuf {
    PathBuf::from(
        std::env::var("HBV_FIXTURES")
            .expect("HBV_FIXTURES must point at the shipped fixture directory"),
    )
}

/// Splits a shipped TSV into header names and data rows.
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
    let bytes = hex::decode(text).unwrap_or_else(|e| panic!("{} is not hex: {}", what, e));
    assert_eq!(bytes.len(), 32, "{} is not 32 bytes", what);
    let mut out = [0u8; 32];
    out.copy_from_slice(&bytes);
    out
}

/// Every transaction in the shipped seventeen-block corpus. These are all v1-v4
/// transactions, so every one of them has a legacy unmined ID.
fn corpus_transactions() -> Vec<Arc<Transaction>> {
    let dir = fixture_dir();
    let (header, rows) = tsv("blocks.tsv");
    let mut out = Vec::new();
    let mut block_count = 0usize;
    for row in &rows {
        let name = column(&header, row, "name");
        let text = std::fs::read_to_string(dir.join("blocks").join(format!("{}.hex", name)))
            .unwrap_or_else(|e| panic!("cannot read block {}: {}", name, e));
        let raw = hex::decode(text.trim()).expect("block fixture is hex");
        let block = Block::zcash_deserialize(Cursor::new(&raw[..]))
            .unwrap_or_else(|e| panic!("{} did not parse: {:?}", name, e));
        block_count += 1;
        out.extend(block.transactions.iter().cloned());
    }
    // Anti-degeneracy guard on the shipped corpus, not on the code under test:
    // a failure here means the fixtures changed, not that a candidate is wrong.
    assert_eq!(block_count, 17, "the corpus should have 17 blocks");
    assert!(
        out.len() >= 40,
        "the corpus yielded only {} transactions; the set-based assertions below need more",
        out.len()
    );
    out
}

/// The published ZIP-244 vectors: the v5 transaction, its txid and its auth
/// digest, all read from the fixture rather than recomputed here.
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
        let raw = hex::decode(column(&header, row, "tx_hex"))
            .unwrap_or_else(|e| panic!("{}: tx_hex is not hex: {}", name, e));
        let transaction = Transaction::zcash_deserialize(Cursor::new(&raw[..]))
            .unwrap_or_else(|e| panic!("{} did not parse: {:?}", name, e));
        let txid = thirty_two(column(&header, row, "txid_hex"), "txid_hex");
        let auth_digest = thirty_two(column(&header, row, "auth_digest_hex"), "auth_digest_hex");
        out.push(V5Vector {
            name,
            transaction: Arc::new(transaction),
            txid,
            auth_digest,
        });
    }
    // Corpus guard, as above.
    assert_eq!(out.len(), 10, "zip244.tsv should hold the 10 shipped vectors");
    out
}

// ----------------------------------------------------------------- helpers

/// `UnminedTxId`, rendered as bytes in a form that is comparable and orderable
/// without relying on any ordering impl in the crate under test.
fn id_bytes(id: &UnminedTxId) -> Vec<u8> {
    let mut out = id.mined_id().0.to_vec();
    if let Some(auth) = id.auth_digest() {
        out.extend_from_slice(&<[u8; 32]>::from(&auth));
    }
    out
}

fn id_set(ids: impl IntoIterator<Item = UnminedTxId>) -> BTreeSet<Vec<u8>> {
    ids.into_iter().map(|id| id_bytes(&id)).collect()
}

// ------------------------------------------------------------------- tests

#[test]
fn a_transaction_download_request_carries_unmined_ids() {
    // The mempool downloader asks for transactions it saw advertised. It has
    // only their unmined IDs, so that is what the request carries -- and it is
    // a set, so the same identifier offered by two peers is one download.
    let transactions = corpus_transactions();
    let ids: Vec<UnminedTxId> = transactions
        .iter()
        .map(|tx| UnminedTxId::from(tx.as_ref()))
        .collect();
    let wanted = id_set(ids.iter().cloned());

    let request = Request::TransactionsById(ids.iter().cloned().collect::<HashSet<UnminedTxId>>());

    match request {
        Request::TransactionsById(set) => {
            assert_eq!(
                id_set(set.iter().cloned()),
                wanted,
                "TransactionsById did not preserve the requested identifiers"
            );
            assert_eq!(
                set.len(),
                wanted.len(),
                "TransactionsById holds {} ids but {} distinct ids were requested",
                set.len(),
                wanted.len()
            );
        }
        other => panic!("expected Request::TransactionsById, got {:?}", other),
    }
}

#[test]
fn the_mempool_listing_request_is_a_unit_variant_answered_with_unmined_ids() {
    // `MempoolTransactionIds` is what an inbound `mempool` message becomes, and
    // it takes no payload: the peer is asking for everything we hold.
    let request = Request::MempoolTransactionIds;
    match request {
        Request::MempoolTransactionIds => {}
        other => panic!("expected Request::MempoolTransactionIds, got {:?}", other),
    }

    let ids: Vec<UnminedTxId> = corpus_transactions()
        .iter()
        .map(|tx| UnminedTxId::from(tx.as_ref()))
        .collect();
    let wanted: Vec<Vec<u8>> = ids.iter().map(id_bytes).collect();

    let response = Response::TransactionIds(ids);
    match response {
        Response::TransactionIds(listed) => {
            // A `Vec`, not a set: the reply preserves the order the node chose.
            let actual: Vec<Vec<u8>> = listed.iter().map(id_bytes).collect();
            assert_eq!(
                actual, wanted,
                "Response::TransactionIds did not preserve the listed ids in order"
            );
        }
        other => panic!("expected Response::TransactionIds, got {:?}", other),
    }
}

#[test]
fn advertising_transactions_gossips_identifiers_not_transactions() {
    // An advertisement is an `inv`: it must carry identifiers only, so a peer
    // can decide whether it wants the bytes.
    let vectors = v5_vectors();
    let ids: Vec<UnminedTxId> = vectors
        .iter()
        .map(|v| UnminedTxId::from(v.transaction.as_ref()))
        .collect();
    let wanted = id_set(ids.iter().cloned());

    let request =
        Request::AdvertiseTransactionIds(ids.iter().cloned().collect::<HashSet<UnminedTxId>>());

    match request {
        Request::AdvertiseTransactionIds(set) => {
            assert_eq!(
                id_set(set.iter().cloned()),
                wanted,
                "AdvertiseTransactionIds did not preserve the advertised identifiers"
            );
            for id in set.iter() {
                assert!(
                    id.auth_digest().is_some(),
                    "a v5 advertisement must use a witnessed id, but {} carried no auth digest",
                    id
                );
            }
        }
        other => panic!("expected Request::AdvertiseTransactionIds, got {:?}", other),
    }
}

#[test]
fn pushing_a_transaction_sends_the_transaction_beside_its_precomputed_id() {
    // `PushTransaction` used to carry a bare `Arc<Transaction>`. It now carries
    // an `UnminedTx`, so the receiving side never has to recompute the id --
    // and for a v5 transaction it could not do so from the effects alone.
    for vector in v5_vectors() {
        let unmined: UnminedTx = vector.transaction.clone().into();
        let request = Request::PushTransaction(unmined);

        match request {
            Request::PushTransaction(pushed) => {
                assert_eq!(
                    pushed.id.mined_id().0,
                    vector.txid,
                    "{}: the pushed id's mined id is not the published txid",
                    vector.name
                );
                assert_eq!(
                    pushed.id.auth_digest().map(<[u8; 32]>::from),
                    Some(vector.auth_digest),
                    "{}: the pushed id's auth digest is not the published one",
                    vector.name
                );
                assert_eq!(
                    pushed.transaction.as_ref(),
                    vector.transaction.as_ref(),
                    "{}: the pushed transaction is not the one that was sent",
                    vector.name
                );
                assert_eq!(
                    pushed.id,
                    UnminedTxId::from(pushed.transaction.as_ref()),
                    "{}: the precomputed id does not match its own transaction",
                    vector.name
                );
            }
            other => panic!("expected Request::PushTransaction, got {:?}", other),
        }
    }
}

#[test]
fn a_transaction_response_carries_unmined_transactions_with_their_ids() {
    // The reply to `TransactionsById` is the transactions themselves, each
    // still paired with its identifier, so the downloader can match them back
    // to what it asked for without re-hashing.
    let vectors = v5_vectors();
    let unmined: Vec<UnminedTx> = vectors
        .iter()
        .map(|v| UnminedTx::from(v.transaction.clone()))
        .collect();

    let response = Response::Transactions(unmined);

    match response {
        Response::Transactions(delivered) => {
            assert_eq!(
                delivered.len(),
                vectors.len(),
                "Response::Transactions delivered {} of {} transactions",
                delivered.len(),
                vectors.len()
            );
            for (tx, vector) in delivered.iter().zip(vectors.iter()) {
                assert_eq!(
                    id_bytes(&tx.id),
                    [&vector.txid[..], &vector.auth_digest[..]].concat(),
                    "{}: the delivered transaction's id is not txid || auth_digest",
                    vector.name
                );
                let mut bytes = Vec::new();
                tx.transaction
                    .zcash_serialize(&mut bytes)
                    .expect("a parsed transaction re-serializes");
                assert!(
                    !bytes.is_empty(),
                    "{}: the delivered transaction serialized to nothing",
                    vector.name
                );
            }
        }
        other => panic!("expected Response::Transactions, got {:?}", other),
    }
}

#[test]
fn a_listing_response_feeds_straight_back_into_a_download_request() {
    // This composition is the crawler's whole loop: ask a peer what it has,
    // then ask for what you do not hold. It only type-checks if both sides use
    // the same identifier type.
    let transactions = corpus_transactions();
    let already_held: HashSet<Vec<u8>> = transactions
        .iter()
        .take(5)
        .map(|tx| id_bytes(&UnminedTxId::from(tx.as_ref())))
        .collect();

    let listing = Response::TransactionIds(
        transactions
            .iter()
            .map(|tx| UnminedTxId::from(tx.as_ref()))
            .collect(),
    );

    let download = match listing {
        Response::TransactionIds(ids) => {
            let wanted: HashSet<UnminedTxId> = ids
                .into_iter()
                .filter(|id| !already_held.contains(&id_bytes(id)))
                .collect();
            Request::TransactionsById(wanted)
        }
        other => panic!("expected Response::TransactionIds, got {:?}", other),
    };

    match download {
        Request::TransactionsById(set) => {
            assert!(
                !set.is_empty(),
                "every listed transaction was filtered out; the round trip tested nothing"
            );
            for id in set.iter() {
                assert!(
                    !already_held.contains(&id_bytes(id)),
                    "the download request asked for {}, which was already held",
                    id
                );
            }
        }
        other => panic!("expected Request::TransactionsById, got {:?}", other),
    }
}

#[test]
fn a_request_set_deduplicates_by_the_whole_witnessed_id() {
    // A `HashSet<UnminedTxId>` must treat two v5 offers of the same effects
    // with different authorizing data as two downloads, not one -- that is the
    // reason the network layer stopped using mined hashes here.
    let vector = v5_vectors().remove(0);
    let txid = Hash(vector.txid);
    let auth = AuthDigest::from(vector.auth_digest);

    let mut other_auth_bytes = vector.auth_digest;
    other_auth_bytes[0] ^= 0xff;
    let other_auth = AuthDigest::from(other_auth_bytes);

    let same = UnminedTxId::from(WtxId {
        id: txid,
        auth_digest: auth,
    });
    let malleated = UnminedTxId::from(WtxId {
        id: txid,
        auth_digest: other_auth,
    });
    let legacy = UnminedTxId::from_legacy_id(txid);

    let request = Request::TransactionsById(
        vec![same, same, malleated, legacy]
            .into_iter()
            .collect::<HashSet<UnminedTxId>>(),
    );

    match request {
        Request::TransactionsById(set) => {
            assert_eq!(
                set.len(),
                3,
                "a set of two identical witnessed ids, one malleated witnessed id and one legacy \
                 id collapsed to {} entries, expected 3",
                set.len()
            );
            assert!(
                set.contains(&same) && set.contains(&malleated) && set.contains(&legacy),
                "the request set lost one of the three distinct identifiers"
            );
        }
        other => panic!("expected Request::TransactionsById, got {:?}", other),
    }
}

#[test]
fn the_empty_response_answers_the_requests_that_produce_no_data() {
    // `PushTransaction` and `AdvertiseTransactionIds` are fire-and-forget, and
    // a node with nothing useful to say answers `Nil` rather than inventing a
    // `notfound`.
    for response in vec![Response::Nil, Response::Nil] {
        match response {
            Response::Nil => {}
            other => panic!("expected Response::Nil, got {:?}", other),
        }
    }

    // Both types are cloned by the peer set as it fans a request out to every
    // peer, and logged on the error path.
    let request = Request::MempoolTransactionIds;
    let clone = request.clone();
    assert!(
        !format!("{:?}", clone).is_empty(),
        "Request must implement Debug"
    );
    let nil = Response::Nil;
    assert!(
        !format!("{:?}", nil.clone()).is_empty(),
        "Response must implement Debug"
    );
}

#[test]
fn the_block_half_of_the_service_still_uses_block_hashes() {
    // Guard against an over-broad rename: blocks are not malleable in the way
    // v5 transactions are, so `BlocksByHash`, `AdvertiseBlock` and `FindBlocks`
    // keep taking `block::Hash`.
    let dir = fixture_dir();
    let (header, rows) = tsv("blocks.tsv");
    let row = &rows[0];
    let name = column(&header, row, "name");
    let text = std::fs::read_to_string(dir.join("blocks").join(format!("{}.hex", name)))
        .unwrap_or_else(|e| panic!("cannot read block {}: {}", name, e));
    let raw = hex::decode(text.trim()).expect("block fixture is hex");
    let block = Block::zcash_deserialize(Cursor::new(&raw[..])).expect("the block parses");
    let hash = block.hash();

    let by_hash: HashSet<zebra_chain::block::Hash> = vec![hash].into_iter().collect();
    match Request::BlocksByHash(by_hash) {
        Request::BlocksByHash(set) => {
            assert!(
                set.contains(&hash),
                "{}: BlocksByHash lost the requested block hash",
                name
            );
        }
        other => panic!("expected Request::BlocksByHash, got {:?}", other),
    }

    match Request::AdvertiseBlock(hash) {
        Request::AdvertiseBlock(advertised) => assert_eq!(
            advertised, hash,
            "{}: AdvertiseBlock changed the block hash",
            name
        ),
        other => panic!("expected Request::AdvertiseBlock, got {:?}", other),
    }

    match Response::Blocks(vec![Arc::new(block)]) {
        Response::Blocks(blocks) => {
            assert_eq!(blocks.len(), 1);
            assert_eq!(
                blocks[0].hash(),
                hash,
                "{}: Response::Blocks changed the block",
                name
            );
        }
        other => panic!("expected Response::Blocks, got {:?}", other),
    }
}
