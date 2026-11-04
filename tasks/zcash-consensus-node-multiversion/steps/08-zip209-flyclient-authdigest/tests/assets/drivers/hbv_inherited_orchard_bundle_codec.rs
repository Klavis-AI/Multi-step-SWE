//! Orchard bundles inside V5 transactions, and the nullifiers they carry.
//!
//! Harbor verifier driver -- injected into `zebra-chain/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! **Gate group, weight 0.** Nothing here is new at this milestone. Parsing a
//! V5 transaction that carries a *real* Orchard bundle -- rather than the empty,
//! all-zero Orchard section the older parser accepted -- was established several
//! milestones back, together with the nullifier accessor the state service reads
//! through. It earns nothing and only vetoes: an inherited capability that no
//! graded case touches can be deleted or broken for free while the step still
//! scores full marks, and `orchard::ShieldedData` sits in `zebra-chain`,
//! the crate this step rewrites `amount`, `value_balance` and `history_tree` in.
//!
//! The corpus is the shipped ZIP-244 vector set: five of its ten transactions
//! carry Orchard bundles, of one, two or three actions, and five carry none.
//! Nothing here recomputes an expected value with the interface under test:
//!
//!   * the action counts are the `nActionsOrchard` field of each shipped
//!     vector, read off the wire format;
//!   * each nullifier is asserted to appear *verbatim* in the very bytes it was
//!     parsed from, which a parser reading the wrong field offset, or returning
//!     a derived value, cannot satisfy;
//!   * the round trip compares bytes against bytes.

use std::collections::BTreeMap;
use std::io::Cursor;
use std::path::PathBuf;

use zebra_chain::serialization::{ZcashDeserialize, ZcashSerialize};
use zebra_chain::transaction::Transaction;

// ---- fixtures -------------------------------------------------------------

/// The ten shipped ZIP-244 vectors, with the number of Orchard actions each one
/// encodes in its `nActionsOrchard` field.
const VECTORS: [(&str, usize); 10] = [
    ("zip244-00", 2),
    ("zip244-01", 0),
    ("zip244-02", 0),
    ("zip244-03", 0),
    ("zip244-04", 0),
    ("zip244-05", 2),
    ("zip244-06", 0),
    ("zip244-07", 3),
    ("zip244-08", 1),
    ("zip244-09", 3),
];

fn fixture_dir() -> PathBuf {
    PathBuf::from(std::env::var("HBV_FIXTURES").expect("the verifier must export HBV_FIXTURES"))
}

/// Every `name -> tx_hex` row of `zip244.tsv`, decoded to raw bytes.
fn raw_transactions() -> BTreeMap<String, Vec<u8>> {
    let path = fixture_dir().join("zip244.tsv");
    let text = std::fs::read_to_string(&path)
        .unwrap_or_else(|e| panic!("missing fixture {:?}: {}", path, e));
    let mut lines = text.lines();
    let header: Vec<&str> = lines
        .next()
        .expect("zip244.tsv is empty")
        .trim_start_matches("# ")
        .split('\t')
        .collect();

    let mut out = BTreeMap::new();
    for line in lines {
        if line.trim().is_empty() {
            continue;
        }
        let row: BTreeMap<&str, &str> = header.iter().cloned().zip(line.split('\t')).collect();
        let name = row.get("name").expect("zip244.tsv has a name column");
        let tx_hex = row.get("tx_hex").expect("zip244.tsv has a tx_hex column");
        let raw = hex::decode(tx_hex)
            .unwrap_or_else(|e| panic!("{} tx_hex is not hex: {}", name, e));
        out.insert((*name).to_string(), raw);
    }
    out
}

fn raw(name: &str) -> Vec<u8> {
    raw_transactions()
        .remove(name)
        .unwrap_or_else(|| panic!("zip244.tsv has no row for {}", name))
}

fn parse(name: &str) -> Transaction {
    let bytes = raw(name);
    Transaction::zcash_deserialize(Cursor::new(&bytes[..]))
        .unwrap_or_else(|e| panic!("{} did not parse as a transaction: {:?}", name, e))
}

/// Whether `needle` occurs anywhere in `haystack`.
fn contains(haystack: &[u8], needle: &[u8]) -> bool {
    haystack
        .windows(needle.len())
        .any(|window| window == needle)
}

// ---- tests ----------------------------------------------------------------

#[test]
fn a_v5_transaction_reports_whether_it_carries_an_orchard_bundle() {
    // `orchard_shielded_data()` is the accessor the block and transaction
    // verifiers branch on, and it is the *inherited* one: this step adds the
    // `has_orchard_shielded_data()` predicate over it, which a scored group
    // asserts and which this gate group must therefore not reach for. An
    // implementation that parses the Orchard section into `None`, or that
    // reports a bundle for a transaction that has none, is caught here before
    // any of this step's own groups run.
    for (name, actions) in VECTORS.iter() {
        let transaction = parse(name);
        assert_eq!(
            transaction.orchard_shielded_data().is_some(),
            *actions > 0,
            "{} encodes {} Orchard actions, so orchard_shielded_data should be {}",
            name,
            actions,
            if *actions > 0 { "Some" } else { "None" }
        );
        assert_eq!(
            transaction.orchard_actions().count(),
            *actions,
            "{} must expose its {} Orchard actions through orchard_actions()",
            name,
            actions
        );
    }
}

#[test]
fn every_orchard_action_contributes_exactly_one_nullifier() {
    // `orchard_nullifiers` is what the state service reads to build the
    // nullifier set, so a bundle that parses but loses an action is a
    // double-spend hole rather than a cosmetic defect.
    for (name, actions) in VECTORS.iter() {
        let transaction = parse(name);
        assert_eq!(
            transaction.orchard_nullifiers().count(),
            *actions,
            "{} encodes {} Orchard actions and must yield one nullifier for each",
            name,
            actions
        );
    }
}

#[test]
fn every_reported_nullifier_appears_verbatim_in_the_bytes_it_was_parsed_from() {
    // The nullifier is a 32-byte field of the action encoding, copied through
    // unchanged. This is the strongest check available without a second
    // implementation: it fails for a parser that reads the wrong offset inside
    // the action, that swaps the nullifier with `cv`, `rk` or `cmx`, or that
    // returns anything derived rather than the field itself.
    let mut checked = 0usize;
    for (name, actions) in VECTORS.iter() {
        if *actions == 0 {
            continue;
        }
        let bytes = raw(name);
        let transaction = parse(name);
        for nullifier in transaction.orchard_nullifiers() {
            let field: [u8; 32] = (*nullifier).into();
            assert!(
                contains(&bytes, &field),
                "{}: nullifier {} does not occur in the {} bytes it was parsed from",
                name,
                hex::encode(field),
                bytes.len()
            );
            checked += 1;
        }
    }
    assert_eq!(
        checked, 11,
        "the five Orchard-carrying vectors hold eleven actions between them"
    );
}

#[test]
fn an_orchard_carrying_transaction_round_trips_byte_for_byte() {
    // Serialization is the other half of the codec, and the one that shows
    // whether the bundle was parsed into a structure that remembers exactly what
    // it read: the actions, the flags byte, `valueBalanceOrchard`, the anchor,
    // the proof, the per-action signatures and the binding signature. A single
    // dropped or reordered field changes the bytes.
    for (name, _) in VECTORS.iter() {
        let bytes = raw(name);
        let transaction = parse(name);
        let reserialized = transaction
            .zcash_serialize_to_vec()
            .unwrap_or_else(|e| panic!("{} did not serialize: {:?}", name, e));
        assert_eq!(
            hex::encode(&reserialized),
            hex::encode(&bytes),
            "{} must serialize back to the {} bytes it was parsed from",
            name,
            bytes.len()
        );
    }
}
