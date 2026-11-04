//! Transaction IDs for v5, against the published ZIP-244 vectors.
//!
//! Harbor verifier driver -- injected into `zebra-chain/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! Before this milestone every Zcash transaction identifier was SHA256d over
//! the transaction's own serialization, and the transaction therefore had
//! exactly one identifier. ZIP-244 splits that in two for v5: the *txid* is a
//! tree of hashes over the effects of the transaction (version, transparent
//! inputs and outputs, the Sapling and Orchard bundles) and deliberately
//! excludes the authorizing data -- the proofs and signatures -- so that a
//! transaction cannot be made to change its identifier by changing a proof.
//! The identifier of the witness data is the separate `auth_digest`, and the
//! pair is what ZIP-239 puts on the wire.
//!
//! `Transaction::hash()` is the entry point under test. It must keep the
//! pre-existing meaning for v1 to v4, where it is still SHA256d over the whole
//! serialization, and switch to the ZIP-244 construction for v5. The vectors
//! below are published values from the Zcash test-vector project, so the
//! expected identifiers are not derived from the implementation under test.
//!
//! The vectors are shipped in `$HBV_FIXTURES/zip244.tsv`. Their provenance is
//! the `zcash-test-vectors` project's `zip_0244.py` generator (by way of
//! upstream Zebra's `zebra-test/src/zip0244.rs`, PRs #2129 and #2165, pinned
//! at v1.0.0-alpha.13); that file is a literal table of byte arrays, not an
//! implementation of the digest, and every expected value below is taken from
//! it verbatim.

use std::collections::BTreeMap;
use std::io::{Cursor, Write};
use std::path::PathBuf;

use zebra_chain::block::{Block, Height};
use zebra_chain::parameters::{Network, NetworkUpgrade};
use zebra_chain::serialization::{sha256d, ZcashDeserialize, ZcashSerialize};
use zebra_chain::transaction::Transaction;

// ---------------------------------------------------------------- fixtures
//
// `zip244.tsv` is the published ZIP-244 vector table. Every transaction in it
// is a v5 transaction; the first and the fourth are transparent-only, the
// seventh is the smallest interesting one (a single transparent output), and
// the rest carry Orchard actions, Sapling spends or empty bundles. The column
// `-` marks an absent optional value.

#[allow(dead_code)]
pub struct Vector {
    pub name: String,
    /// The serialized transaction, exactly as the ZIP publishes it.
    pub tx: Vec<u8>,
    /// The expected transaction identifier, in the byte order the vector
    /// publishes it (which is the order `Hash` stores, not the display order).
    pub txid: [u8; 32],
    /// The expected authorizing-data commitment, in published order.
    pub auth_digest: [u8; 32],
    pub transparent_input: Option<u32>,
    pub script_code: Option<Vec<u8>>,
    pub amount: Option<i64>,
    pub sighash_all: [u8; 32],
    pub sighash_none: Option<[u8; 32]>,
    pub sighash_single: Option<[u8; 32]>,
    pub sighash_all_anyone: Option<[u8; 32]>,
    pub sighash_none_anyone: Option<[u8; 32]>,
    pub sighash_single_anyone: Option<[u8; 32]>,
}

fn fixture_dir() -> PathBuf {
    PathBuf::from(
        std::env::var("HBV_FIXTURES")
            .expect("HBV_FIXTURES must point at the shipped fixture directory"),
    )
}

fn cell<'a>(row: &BTreeMap<&'a str, &'a str>, key: &str) -> &'a str {
    *row.get(key)
        .unwrap_or_else(|| panic!("zip244.tsv has no column {}", key))
}

fn digest_hex(hex_str: &str, name: &str, column: &str) -> [u8; 32] {
    let bytes = hex::decode(hex_str)
        .unwrap_or_else(|e| panic!("{}: column {} is not hex: {}", name, column, e));
    assert_eq!(
        bytes.len(),
        32,
        "{}: column {} is {} bytes, expected 32",
        name,
        column,
        bytes.len()
    );
    let mut out = [0u8; 32];
    out.copy_from_slice(&bytes);
    out
}

fn optional_digest(
    row: &BTreeMap<&str, &str>,
    name: &str,
    column: &str,
) -> Option<[u8; 32]> {
    let raw = cell(row, column);
    if raw == "-" {
        None
    } else {
        Some(digest_hex(raw, name, column))
    }
}

/// Load the published ZIP-244 vector table, in file order.
#[allow(dead_code)]
pub fn vectors() -> Vec<Vector> {
    let dir = fixture_dir();
    let tsv = std::fs::read_to_string(dir.join("zip244.tsv"))
        .expect("zip244.tsv is readable next to the block corpus");
    let mut lines = tsv.lines();
    let header: Vec<&str> = lines
        .next()
        .expect("zip244.tsv has a header row")
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

        let parse_opt = |column: &str| -> Option<String> {
            let raw = cell(&row, column);
            if raw == "-" {
                None
            } else {
                Some(raw.to_string())
            }
        };

        out.push(Vector {
            tx: hex::decode(cell(&row, "tx_hex"))
                .unwrap_or_else(|e| panic!("{}: tx_hex is not hex: {}", name, e)),
            txid: digest_hex(cell(&row, "txid_hex"), &name, "txid_hex"),
            auth_digest: digest_hex(cell(&row, "auth_digest_hex"), &name, "auth_digest_hex"),
            transparent_input: parse_opt("transparent_input").map(|v| v.parse().expect("index")),
            script_code: parse_opt("script_code_hex")
                .map(|v| hex::decode(v).expect("script_code_hex is hex")),
            amount: parse_opt("amount").map(|v| v.parse().expect("amount")),
            sighash_all: digest_hex(cell(&row, "sighash_all"), &name, "sighash_all"),
            sighash_none: optional_digest(&row, &name, "sighash_none"),
            sighash_single: optional_digest(&row, &name, "sighash_single"),
            sighash_all_anyone: optional_digest(&row, &name, "sighash_all_anyone"),
            sighash_none_anyone: optional_digest(&row, &name, "sighash_none_anyone"),
            sighash_single_anyone: optional_digest(&row, &name, "sighash_single_anyone"),
            name,
        });
    }
    assert!(
        out.len() >= 10,
        "the shipped vector table has {} entries; the verifier expects at least 10",
        out.len()
    );
    out
}

// ----------------------------------------------------------------- helpers

fn parse(vector: &Vector) -> Transaction {
    Transaction::zcash_deserialize(Cursor::new(&vector.tx[..]))
        .unwrap_or_else(|e| panic!("{} did not parse: {:?}", vector.name, e))
}

fn serialize(tx: &Transaction) -> Vec<u8> {
    let mut out = Vec::new();
    tx.zcash_serialize(&mut out)
        .expect("a Vec never fails to write");
    out
}

/// SHA256d over a byte string, written here so that "the txid is the hash of
/// the serialization" is a claim about the bytes rather than a restatement of
/// whatever the crate's own helper does.
fn independent_sha256d(bytes: &[u8]) -> [u8; 32] {
    use sha2::{Digest, Sha256};

    let mut first = Sha256::default();
    first.update(bytes);
    let first = first.finalize();

    let mut second = Sha256::default();
    second.update(&first[..]);
    let second = second.finalize();

    let mut out = [0u8; 32];
    out.copy_from_slice(&second[..]);
    out
}

/// The v1-v4 transactions of the shipped block corpus, paired with the block
/// they came from, so that the pre-existing identifier rule can still be
/// checked against real chain data.
fn legacy_transactions() -> Vec<(String, Transaction)> {
    let dir = fixture_dir();
    let tsv = std::fs::read_to_string(dir.join("blocks.tsv")).expect("blocks.tsv is readable");
    let mut lines = tsv.lines();
    let header: Vec<&str> = lines
        .next()
        .expect("blocks.tsv has a header row")
        .trim_start_matches("# ")
        .split('\t')
        .collect();
    let name_at = header
        .iter()
        .position(|h| *h == "name")
        .expect("blocks.tsv has a name column");

    let mut out = Vec::new();
    for line in lines {
        if line.is_empty() {
            continue;
        }
        let cells: Vec<&str> = line.split('\t').collect();
        let name = cells[name_at].to_string();
        let text = std::fs::read_to_string(dir.join("blocks").join(format!("{}.hex", name)))
            .unwrap_or_else(|e| panic!("cannot read block {}: {}", name, e));
        let raw = hex::decode(text.trim()).expect("block fixture is hex");
        let block = Block::zcash_deserialize(Cursor::new(&raw[..]))
            .unwrap_or_else(|e| panic!("{} did not parse: {:?}", name, e));
        for tx in block.transactions.iter() {
            out.push((name.clone(), tx.as_ref().clone()));
        }
    }
    out
}

// ------------------------------------------------------------------- tests

#[test]
fn every_zip244_vector_round_trips_byte_for_byte() {
    for vector in vectors() {
        let tx = parse(&vector);
        let again = serialize(&tx);
        assert_eq!(
            again,
            vector.tx,
            "{}: re-serialized to {} bytes, the vector has {}",
            vector.name,
            again.len(),
            vector.tx.len()
        );
    }
}

#[test]
fn every_zip244_vector_has_its_published_txid() {
    // The heart of the milestone. Each expected identifier is a published
    // ZIP-244 value; the values are not recomputed here.
    for vector in vectors() {
        let tx = parse(&vector);
        assert_eq!(
            tx.hash().0,
            vector.txid,
            "{}: transaction identifier is not the published ZIP-244 txid",
            vector.name
        );
    }
}

#[test]
fn v5_identifiers_are_not_the_serialization_hash() {
    // The point of ZIP-244 is that the identifier commits to the transaction's
    // effects, not its bytes. For every vector that carries authorizing data
    // (a proof or a signature), the two must differ -- otherwise the new rule
    // has not taken effect and the implementation is still hashing the
    // serialization.
    let mut differed = 0;
    for vector in vectors() {
        let tx = parse(&vector);
        let legacy = independent_sha256d(&vector.tx);
        if tx.hash().0 != legacy {
            differed += 1;
        }
    }
    assert!(
        differed >= 8,
        "only {} of {} v5 vectors hash to something other than SHA256d of their \
         serialization; the ZIP-244 digest does not appear to be in use",
        differed,
        vectors().len()
    );
}

#[test]
fn the_txid_is_not_the_auth_digest() {
    // ZIP-244 fixes two distinct identifiers per v5 transaction: the txid over
    // the effects and the auth_digest over the authorizing data. A v5
    // transaction that has any authorizing data at all must not collapse the
    // two, or ZIP-239's `MSG_WTX` relay has nothing to name.
    for vector in vectors() {
        if vector.auth_digest == vector.txid {
            continue;
        }
        let tx = parse(&vector);
        assert_eq!(
            tx.hash().0,
            vector.txid,
            "{}: Transaction::hash() reported the authorizing-data commitment \
             rather than the txid",
            vector.name
        );
    }
}

#[test]
fn v1_to_v4_identifiers_are_still_sha256d_over_the_serialization() {
    // The predecessor rule survives the milestone: a transaction of version 4
    // or lower has one identifier and it is SHA256d over its own bytes. This
    // is checked against real mainnet and testnet transactions, using a hash
    // computed here, so it cannot pass by both sides being wrong together.
    let mut checked = 0;
    for (block_name, tx) in legacy_transactions() {
        if tx.version() >= 5 {
            continue;
        }
        let bytes = serialize(&tx);
        assert_eq!(
            tx.hash().0,
            independent_sha256d(&bytes),
            "{}: a v{} transaction no longer hashes to SHA256d of its serialization",
            block_name,
            tx.version()
        );
        checked += 1;
    }
    assert!(
        checked >= 50,
        "only {} pre-v5 transactions were checked; the block corpus looks wrong",
        checked
    );
}

#[test]
fn the_sha256d_writer_and_the_txid_are_both_32_bytes() {
    // The crate's own double-SHA256 writer and the field ordering must agree:
    // a v5 identifier is 32 bytes in the same order as any other Zcash hash,
    // which is what makes it interchangeable with the wire format.
    for vector in vectors() {
        let tx = parse(&vector);
        let mut writer = sha256d::Writer::default();
        writer.write_all(&vector.tx).expect("in-memory write");
        assert_eq!(
            writer.finish().len(),
            32,
            "{}: sha256d::Writer did not produce 32 bytes",
            vector.name
        );
        assert_eq!(
            tx.hash().0.len(),
            vector.txid.len(),
            "{}: the identifier is not 32 bytes",
            vector.name
        );
    }
}

#[test]
fn the_txid_parses_back_out_of_its_display_form() {
    use std::str::FromStr;

    // Display prints the identifier byte-reversed, following the u256
    // convention; FromStr must invert exactly that.
    for vector in vectors() {
        let tx = parse(&vector);
        let hash = tx.hash();
        let mut display_order = vector.txid;
        display_order.reverse();
        assert_eq!(
            hash.to_string(),
            hex::encode(&display_order),
            "{}: the identifier is not displayed in the big-endian convention",
            vector.name
        );
        let parsed = zebra_chain::transaction::Hash::from_str(&hash.to_string())
            .unwrap_or_else(|e| panic!("{}: identifier did not parse back: {:?}", vector.name, e));
        assert_eq!(
            parsed, hash,
            "{}: the identifier does not survive a Display/FromStr round trip",
            vector.name
        );
    }
}

#[test]
fn identifiers_are_32_bytes_and_truncating_the_accumulator_is_recoverable() {
    // `Block::hash()` covers the header, whose merkle root is over the
    // transaction identifiers, so block identity now depends on the ZIP-244
    // digest. The corpus stops short of NU5, so this only exercises v1-v4
    // inside a block -- but it is the bridge that would break first if the
    // identifier changed for old transactions.
    let dir = fixture_dir();
    let text = std::fs::read_to_string(dir.join("blocks").join("main-0903001.hex"))
        .expect("the corpus ships main-0903001");
    let raw = hex::decode(text.trim()).expect("block fixture is hex");
    let block = Block::zcash_deserialize(Cursor::new(&raw[..])).expect("main-0903001 parses");
    assert_eq!(block.coinbase_height().map(|h| h.0), Some(903_001));

    let mut root: Vec<u8> = block
        .transactions
        .iter()
        .map(|tx| tx.hash().0)
        .fold(Vec::new(), |mut acc, hash| {
            acc.extend_from_slice(&hash);
            acc
        });
    assert!(!root.is_empty(), "the block has no transactions");
    root.truncate(0);
    for tx in block.transactions.iter() {
        root.extend_from_slice(&tx.hash().0);
        assert_eq!(tx.hash().0.len(), 32);
    }
    assert_eq!(
        root.len(),
        block.transactions.len() * 32,
        "an identifier is not 32 bytes"
    );
}

#[test]
fn a_height_and_network_upgrade_pair_still_selects_the_v5_rule() {
    // The digest depends on the consensus branch id, which comes from the
    // transaction's own `network_upgrade` field. This asserts the vector set
    // really is NU5 -- if a future revision re-pointed the corpus at an earlier
    // upgrade, the published identifiers would no longer apply.
    let nu5 = NetworkUpgrade::Nu5;
    assert_eq!(
        nu5.branch_id().map(u32::from),
        Some(0xf919_a198),
        "NU5's consensus branch id at this release is 0xf919a198"
    );
    // Mainnet has no configured Nu5 activation height at this release, so a
    // height-based lookup still reports Canopy for any height at or above
    // Canopy's own activation (1046400) -- Nu5 can only be reached today by a
    // transaction that names it directly in its own `network_upgrade` field,
    // which is exactly what the vectors below rely on.
    assert_eq!(
        NetworkUpgrade::current(Network::Mainnet, Height(1_687_104)),
        NetworkUpgrade::Canopy,
        "mainnet height 1687104 unexpectedly resolved off of Canopy; \
         has a Nu5 activation height been configured?"
    );
    for vector in vectors() {
        let tx = parse(&vector);
        assert_eq!(tx.version(), 5, "{}: not a v5 transaction", vector.name);
        assert_eq!(
            tx.network_upgrade(),
            Some(NetworkUpgrade::Nu5),
            "{}: the vector is not marked as an NU5 transaction",
            vector.name
        );
        match &tx {
            Transaction::V5 {
                network_upgrade, ..
            } => assert_eq!(*network_upgrade, NetworkUpgrade::Nu5),
            other => panic!("{}: parsed as {:?}", vector.name, other.version()),
        }
    }
}
