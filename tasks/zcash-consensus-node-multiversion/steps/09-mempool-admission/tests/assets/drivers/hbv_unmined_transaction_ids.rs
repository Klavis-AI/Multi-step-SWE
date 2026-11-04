//! Unmined transaction identifiers: `UnminedTxId` and `UnminedTx`.
//!
//! Harbor verifier driver -- injected into `zebra-chain/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! Until this milestone a transaction had one identifier and the node had no
//! use for another: everything it handled was already in a block. A mempool
//! changes that. An unconfirmed v5 transaction must be identified by something
//! its own signatures cannot be swapped underneath, which is the witnessed
//! transaction ID; but versions 1 to 4 have no such identifier, and the Zcash
//! network protocol does not define one for them. So the node needs a single
//! type that carries the *right* identifier for the version in hand.
//!
//! `UnminedTxId` is that type, and the rule it encodes is the whole group:
//!
//!   * v1, v2, v3 and v4 transactions get `Legacy(hash)`, where `hash` is the
//!     ordinary transaction hash;
//!   * v5 transactions get `Witnessed(wtx_id)`;
//!   * `mined_id()` recovers the identifier the transaction will have once it
//!     is in a block -- the legacy hash, or the witnessed id's `id` half --
//!     for every version;
//!   * `auth_digest()` exists only for v5, because only v5 commits to its
//!     authorizing data separately;
//!   * and the two variants are *distinct values* even when their mined ids
//!     are equal, which is what lets a mempool hold a legacy-keyed entry and a
//!     witness-keyed entry without either shadowing the other.
//!
//! Expected values: the seventeen-block corpus supplies real v1..v4
//! transactions (every block in it predates NU5, so none of them is v5), and
//! `zip244.tsv` supplies the published v5 vectors together with their txids and
//! authorizing-data digests in internal byte order. Nothing here recomputes a
//! digest.

use std::collections::BTreeMap;
use std::collections::HashSet;
use std::io::Cursor;
use std::path::PathBuf;
use std::sync::Arc;

use zebra_chain::block::Block;
use zebra_chain::serialization::ZcashDeserialize;
use zebra_chain::transaction::{
    AuthDigest, Hash, Transaction, UnminedTx, UnminedTxId, WtxId,
};

// ---------------------------------------------------------------- fixtures

fn fixture_dir() -> PathBuf {
    PathBuf::from(
        std::env::var("HBV_FIXTURES")
            .expect("HBV_FIXTURES must point at the shipped fixture directory"),
    )
}

/// Every row of a shipped TSV, as a column-name -> value map.
fn rows(file: &str) -> Vec<BTreeMap<String, String>> {
    let text = std::fs::read_to_string(fixture_dir().join(file))
        .unwrap_or_else(|e| panic!("cannot read {}: {}", file, e));
    let mut lines = text.lines();
    let header: Vec<&str> = lines
        .next()
        .unwrap_or_else(|| panic!("{} has no header row", file))
        .trim_start_matches("# ")
        .split('\t')
        .collect();

    let mut out = Vec::new();
    for line in lines {
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        let cells: Vec<&str> = line.split('\t').collect();
        assert_eq!(cells.len(), header.len(), "{}: malformed row {}", file, line);
        let row: BTreeMap<&str, &str> = header.iter().cloned().zip(cells).collect();
        out.push(
            row.into_iter()
                .map(|(k, v)| (k.to_string(), v.to_string()))
                .collect(),
        );
    }
    assert!(!out.is_empty(), "{} has no data rows", file);
    out
}

fn thirty_two(text: &str, name: &str, column: &str) -> [u8; 32] {
    let bytes =
        hex::decode(text).unwrap_or_else(|e| panic!("{}: {} is not hex: {}", name, column, e));
    assert_eq!(bytes.len(), 32, "{}: {} is not 32 bytes", name, column);
    let mut out = [0u8; 32];
    out.copy_from_slice(&bytes);
    out
}

/// One pre-NU5 transaction from the block corpus, with the block it came from.
struct Legacy {
    name: String,
    tx: Transaction,
}

fn pre_v5_transactions() -> Vec<Legacy> {
    let dir = fixture_dir();
    let mut out = Vec::new();
    for row in rows("blocks.tsv") {
        let name = row
            .get("name")
            .expect("blocks.tsv has a name column")
            .clone();
        let text = std::fs::read_to_string(dir.join("blocks").join(format!("{}.hex", name)))
            .unwrap_or_else(|e| panic!("cannot read block {}: {}", name, e));
        let raw = hex::decode(text.trim()).expect("block fixture is hex");
        let block = Block::zcash_deserialize(Cursor::new(&raw[..]))
            .unwrap_or_else(|e| panic!("{} did not parse: {:?}", name, e));
        for (index, tx) in block.transactions.iter().enumerate() {
            out.push(Legacy {
                name: format!("{}#{}", name, index),
                tx: (**tx).clone(),
            });
        }
    }
    // Anti-degeneracy guard only: a property of the shipped corpus, not of the
    // code under test. A failure here means blocks.tsv or blocks/ changed.
    assert!(
        out.len() >= 17,
        "the corpus yielded {} transactions; the verifier expects at least 17",
        out.len()
    );
    out
}

/// One published ZIP-244 v5 vector, with its two published identifiers in
/// internal byte order.
struct V5 {
    name: String,
    tx: Transaction,
    txid: [u8; 32],
    auth_digest: [u8; 32],
}

fn v5_transactions() -> Vec<V5> {
    let mut out = Vec::new();
    for row in rows("zip244.tsv") {
        let name = row
            .get("name")
            .expect("zip244.tsv has a name column")
            .clone();
        let raw = hex::decode(row.get("tx_hex").expect("zip244.tsv has a tx_hex column"))
            .unwrap_or_else(|e| panic!("{}: tx_hex is not hex: {}", name, e));
        let tx = Transaction::zcash_deserialize(Cursor::new(&raw[..]))
            .unwrap_or_else(|e| panic!("{}: the v5 vector did not parse: {:?}", name, e));
        let txid = thirty_two(
            row.get("txid_hex").expect("zip244.tsv has a txid_hex column"),
            &name,
            "txid_hex",
        );
        let auth_digest = thirty_two(
            row.get("auth_digest_hex")
                .expect("zip244.tsv has an auth_digest_hex column"),
            &name,
            "auth_digest_hex",
        );
        out.push(V5 {
            name,
            tx,
            txid,
            auth_digest,
        });
    }
    // Anti-degeneracy guard only; see above.
    assert!(
        out.len() >= 10,
        "zip244.tsv shrank to {} rows; the verifier expects at least 10",
        out.len()
    );
    out
}

// ----------------------------------------------------------------- helpers

fn is_v5(tx: &Transaction) -> bool {
    match tx {
        Transaction::V5 { .. } => true,
        _ => false,
    }
}

// ------------------------------------------------------------------- tests

#[test]
fn a_pre_v5_transaction_is_identified_by_its_legacy_hash() {
    for fixture in pre_v5_transactions() {
        assert!(
            !is_v5(&fixture.tx),
            "{}: the block corpus predates NU5 and must contain no v5 transaction",
            fixture.name
        );

        let id = fixture.tx.unmined_id();
        match id {
            UnminedTxId::Legacy(hash) => assert_eq!(
                hash,
                fixture.tx.hash(),
                "{}: the legacy unmined id is not the transaction hash",
                fixture.name
            ),
            UnminedTxId::Witnessed(wtx_id) => panic!(
                "{}: a v1-v4 transaction was given the witnessed id {}",
                fixture.name, wtx_id
            ),
        }
    }
}

#[test]
fn a_v5_transaction_is_identified_by_its_witnessed_id() {
    for fixture in v5_transactions() {
        assert!(
            is_v5(&fixture.tx),
            "{}: the ZIP-244 vectors are all v5 transactions",
            fixture.name
        );

        let id = fixture.tx.unmined_id();
        match id {
            UnminedTxId::Witnessed(wtx_id) => {
                assert_eq!(
                    wtx_id.id.0, fixture.txid,
                    "{}: the witnessed id's mined half is not the published txid",
                    fixture.name
                );
                assert_eq!(
                    <[u8; 32]>::from(wtx_id.auth_digest),
                    fixture.auth_digest,
                    "{}: the witnessed id's auth digest is not the published one",
                    fixture.name
                );
            }
            UnminedTxId::Legacy(hash) => panic!(
                "{}: a v5 transaction was given the legacy id {}",
                fixture.name, hash
            ),
        }
    }
}

#[test]
fn the_mined_id_is_the_transaction_hash_whatever_the_version() {
    for fixture in pre_v5_transactions() {
        assert_eq!(
            fixture.tx.unmined_id().mined_id(),
            fixture.tx.hash(),
            "{}: mined_id() is not the transaction hash",
            fixture.name
        );
    }
    for fixture in v5_transactions() {
        assert_eq!(
            fixture.tx.unmined_id().mined_id().0,
            fixture.txid,
            "{}: mined_id() is not the published txid",
            fixture.name
        );
        assert_eq!(
            fixture.tx.unmined_id().mined_id(),
            fixture.tx.hash(),
            "{}: mined_id() disagrees with Transaction::hash",
            fixture.name
        );
    }
}

#[test]
fn only_a_v5_transaction_has_an_authorizing_data_digest() {
    for fixture in pre_v5_transactions() {
        assert!(
            fixture.tx.unmined_id().auth_digest().is_none(),
            "{}: a v1-v4 transaction reported an authorizing data digest",
            fixture.name
        );
    }
    for fixture in v5_transactions() {
        let digest = fixture
            .tx
            .unmined_id()
            .auth_digest()
            .unwrap_or_else(|| panic!("{}: a v5 transaction reported no auth digest", fixture.name));
        assert_eq!(
            <[u8; 32]>::from(digest),
            fixture.auth_digest,
            "{}: auth_digest() is not the published digest",
            fixture.name
        );
    }
}

#[test]
fn from_legacy_id_builds_the_legacy_variant_and_carries_the_hash_through() {
    // This is the constructor the inventory layer uses when a peer advertises
    // a bare 32-byte transaction hash: there is no authorizing data to go with
    // it, so the result must be `Legacy`.
    for fixture in pre_v5_transactions() {
        let hash = fixture.tx.hash();
        let id = UnminedTxId::from_legacy_id(hash);

        assert_eq!(
            id,
            UnminedTxId::Legacy(hash),
            "{}: from_legacy_id did not build the Legacy variant",
            fixture.name
        );
        assert_eq!(
            id.mined_id(),
            hash,
            "{}: from_legacy_id lost the hash",
            fixture.name
        );
        assert!(
            id.auth_digest().is_none(),
            "{}: a legacy id invented an authorizing data digest",
            fixture.name
        );
        assert_eq!(
            id,
            fixture.tx.unmined_id(),
            "{}: from_legacy_id disagrees with Transaction::unmined_id",
            fixture.name
        );
    }
}

#[test]
fn the_displayed_id_is_the_hash_for_v1_to_v4_and_the_witnessed_id_for_v5() {
    for fixture in pre_v5_transactions() {
        let id = fixture.tx.unmined_id();
        assert_eq!(
            id.to_string(),
            fixture.tx.hash().to_string(),
            "{}: a legacy unmined id must display as its transaction hash",
            fixture.name
        );
        assert_eq!(
            id.to_string().len(),
            64,
            "{}: a legacy unmined id must display as 64 hex characters",
            fixture.name
        );
    }
    for fixture in v5_transactions() {
        let id = fixture.tx.unmined_id();
        let wtx_id = WtxId::from(&fixture.tx);
        assert_eq!(
            id.to_string(),
            wtx_id.to_string(),
            "{}: a witnessed unmined id must display as its witnessed id",
            fixture.name
        );
        assert_eq!(
            id.to_string().len(),
            128,
            "{}: a witnessed unmined id must display as 128 hex characters",
            fixture.name
        );
    }
}

#[test]
fn every_way_of_naming_a_transaction_or_a_witnessed_id_agrees() {
    for fixture in v5_transactions() {
        let expected = fixture.tx.unmined_id();
        let wtx_id = WtxId::from(&fixture.tx);

        assert_eq!(
            UnminedTxId::from(&fixture.tx),
            expected,
            "{}: From<&Transaction> disagrees with Transaction::unmined_id",
            fixture.name
        );
        assert_eq!(
            UnminedTxId::from(fixture.tx.clone()),
            expected,
            "{}: From<Transaction> disagrees with Transaction::unmined_id",
            fixture.name
        );
        assert_eq!(
            UnminedTxId::from(Arc::new(fixture.tx.clone())),
            expected,
            "{}: From<Arc<Transaction>> disagrees with Transaction::unmined_id",
            fixture.name
        );
        assert_eq!(
            UnminedTxId::from(wtx_id),
            expected,
            "{}: From<WtxId> must build the Witnessed variant",
            fixture.name
        );
        assert_eq!(
            UnminedTxId::from(&wtx_id),
            expected,
            "{}: From<&WtxId> disagrees with the by-value conversion",
            fixture.name
        );
    }

    for fixture in pre_v5_transactions() {
        let expected = fixture.tx.unmined_id();
        assert_eq!(
            UnminedTxId::from(&fixture.tx),
            expected,
            "{}: From<&Transaction> disagrees with Transaction::unmined_id",
            fixture.name
        );
        assert_eq!(
            UnminedTxId::from(Arc::new(fixture.tx.clone())),
            expected,
            "{}: From<Arc<Transaction>> disagrees with Transaction::unmined_id",
            fixture.name
        );
    }
}

#[test]
fn an_unmined_transaction_carries_its_precalculated_id_beside_the_transaction() {
    for fixture in v5_transactions() {
        let expected_id = fixture.tx.unmined_id();

        let by_value = UnminedTx::from(fixture.tx.clone());
        assert_eq!(
            by_value.id, expected_id,
            "{}: UnminedTx::from(Transaction) stored the wrong id",
            fixture.name
        );
        assert_eq!(
            *by_value.transaction, fixture.tx,
            "{}: UnminedTx::from(Transaction) changed the transaction",
            fixture.name
        );

        let by_ref = UnminedTx::from(&fixture.tx);
        let arc = Arc::new(fixture.tx.clone());
        let by_arc = UnminedTx::from(arc.clone());
        let by_arc_ref = UnminedTx::from(&arc);

        assert_eq!(
            by_ref, by_value,
            "{}: UnminedTx::from(&Transaction) disagrees with the by-value conversion",
            fixture.name
        );
        assert_eq!(
            by_arc, by_value,
            "{}: UnminedTx::from(Arc<Transaction>) disagrees with the by-value conversion",
            fixture.name
        );
        assert_eq!(
            by_arc_ref, by_value,
            "{}: UnminedTx::from(&Arc<Transaction>) disagrees with the by-value conversion",
            fixture.name
        );
    }

    for fixture in pre_v5_transactions() {
        let unmined = UnminedTx::from(&fixture.tx);
        assert_eq!(
            unmined.id,
            UnminedTxId::Legacy(fixture.tx.hash()),
            "{}: an unmined v1-v4 transaction did not get a legacy id",
            fixture.name
        );
        assert_eq!(
            *unmined.transaction, fixture.tx,
            "{}: UnminedTx changed the transaction",
            fixture.name
        );
    }
}

#[test]
fn a_legacy_id_never_collides_with_a_witnessed_id_over_the_same_mined_id() {
    // The load-bearing mempool property. A peer may advertise the same
    // transaction under either identifier; the two must stay distinct keys, and
    // both must still report the same mined id, so that a block containing the
    // transaction evicts it either way.
    for fixture in v5_transactions() {
        let witnessed = fixture.tx.unmined_id();
        let mined = witnessed.mined_id();
        let legacy = UnminedTxId::from_legacy_id(mined);

        assert_eq!(
            legacy.mined_id(),
            witnessed.mined_id(),
            "{}: the two identifiers disagree about the mined id",
            fixture.name
        );
        assert_ne!(
            legacy, witnessed,
            "{}: a legacy id and a witnessed id over the same mined id compared equal",
            fixture.name
        );

        let mut set: HashSet<UnminedTxId> = HashSet::new();
        set.insert(legacy);
        set.insert(witnessed);
        assert_eq!(
            set.len(),
            2,
            "{}: a legacy id and a witnessed id collapsed onto one set entry",
            fixture.name
        );
        set.insert(fixture.tx.unmined_id());
        assert_eq!(
            set.len(),
            2,
            "{}: an equal witnessed id was not deduplicated",
            fixture.name
        );
    }
}

#[test]
fn a_witnessed_id_changes_when_the_authorizing_data_changes() {
    // Two witnessed ids that share a mined id but not an auth digest are
    // different unmined transactions: that is the whole reason v5 needs a
    // second identifier in the mempool.
    for fixture in v5_transactions() {
        let original = fixture.tx.unmined_id();

        let mut other_digest = fixture.auth_digest;
        other_digest[0] ^= 0xff;
        let other = UnminedTxId::from(WtxId {
            id: Hash(fixture.txid),
            auth_digest: AuthDigest::from(other_digest),
        });

        assert_eq!(
            other.mined_id(),
            original.mined_id(),
            "{}: changing the auth digest must not change the mined id",
            fixture.name
        );
        assert_ne!(
            other, original,
            "{}: changing the auth digest must change the unmined id",
            fixture.name
        );
        assert_ne!(
            other.auth_digest(),
            original.auth_digest(),
            "{}: auth_digest() did not follow the change",
            fixture.name
        );
    }
}
