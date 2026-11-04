//! `sapling::ShieldedData<AnchorVariant>`: one bundle type for v4 and v5.
//!
//! Harbor verifier driver -- injected into `zebra-chain/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! A v4 transaction gives every Spend its own anchor; a v5 transaction carries
//! a single anchor for the whole bundle. Rather than two bundle types, this
//! milestone parameterises one by an `AnchorVariant` type trait, so that
//! verification code can be written once and run over both. The v4 instance
//! puts `tree::Root` in each Spend and `FieldNotPresent` in the bundle; the v5
//! instance does the reverse.
//!
//! The other change that shows up here is structural. A Zcash transaction must
//! carry a binding signature if and only if it has at least one Spend or
//! Output, so the bundle stores one mandatory `first: Either<Spend, Output>`
//! alongside the rest: an empty `ShieldedData` is unrepresentable, and
//! `Option<ShieldedData>` is then an exact model of "has shielded data".
//! Eleven real Sapling bundles from the corpus, every one of them a V4
//! `PerSpendAnchor` bundle, exercise the `spends_per_anchor()` identity and
//! the wire codec. No corpus bundle is a `SharedAnchor` bundle, so that arm
//! of the conversion is not exercised here.

use std::convert::TryFrom;
use std::io::Cursor;

use zebra_chain::block::Block;
use zebra_chain::parameters::{Network, NetworkUpgrade};
use zebra_chain::primitives::redjubjub::{Binding, VerificationKey, VerificationKeyBytes};
use zebra_chain::sapling::{FieldNotPresent, Output, PerSpendAnchor, ShieldedData, Spend};
use zebra_chain::serialization::{ZcashDeserialize, ZcashSerialize};
use zebra_chain::transaction::{HashType, Transaction};

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

// ----------------------------------------------------------------- helpers

fn parse(fixture: &Fixture) -> Block {
    Block::zcash_deserialize(Cursor::new(&fixture.raw[..]))
        .unwrap_or_else(|e| panic!("{} did not parse: {:?}", fixture.name, e))
}

fn write<T: ZcashSerialize>(value: &T) -> Vec<u8> {
    let mut out = Vec::new();
    value
        .zcash_serialize(&mut out)
        .expect("a Vec never fails to write");
    out
}

/// One Sapling bundle from the corpus, with everything needed to check its
/// binding signature: the transaction it came from, and the upgrade in force
/// at the height of the block that contains it.
struct Bundle {
    name: String,
    transaction: Transaction,
    shielded_data: ShieldedData<PerSpendAnchor>,
    upgrade: NetworkUpgrade,
    is_coinbase: bool,
}

/// Every Sapling bundle in the seventeen-block corpus.
fn bundles() -> Vec<Bundle> {
    let mut out = Vec::new();
    for fixture in blocks() {
        let network = match fixture.network.as_str() {
            "Mainnet" => Network::Mainnet,
            "Testnet" => Network::Testnet,
            other => panic!("blocks.tsv has an unknown network {:?}", other),
        };
        let upgrade = NetworkUpgrade::current(network, zebra_chain::block::Height(fixture.height));
        let block = parse(&fixture);
        for (index, tx) in block.transactions.iter().enumerate() {
            if let Transaction::V4 {
                sapling_shielded_data: Some(shielded_data),
                ..
            } = &**tx
            {
                out.push(Bundle {
                    name: format!("{}[{}]", fixture.name, index),
                    transaction: (**tx).clone(),
                    shielded_data: shielded_data.clone(),
                    upgrade,
                    is_coinbase: tx.contains_coinbase_input(),
                });
            }
        }
    }
    assert_eq!(
        out.len(),
        11,
        "the seventeen-block corpus holds eleven Sapling shielded bundles, found {}",
        out.len()
    );
    out
}

/// Check a RedJubjub binding signature against the key derived from the
/// bundle's value commitments and balance.
fn binding_signature_verifies(bundle: &Bundle) -> bool {
    let vk_bytes: VerificationKeyBytes<Binding> =
        bundle.shielded_data.binding_verification_key();
    let vk = match VerificationKey::<Binding>::try_from(vk_bytes) {
        Ok(vk) => vk,
        Err(_) => return false,
    };
    let sighash = bundle
        .transaction
        .sighash(bundle.upgrade, HashType::ALL, None);
    vk.verify(sighash.as_bytes(), &bundle.shielded_data.binding_sig)
        .is_ok()
}

// -------------------------------------------------------------------- tests

#[test]
fn a_v4_transaction_carries_a_per_spend_anchor_bundle() {
    // The type is fixed by the variant: naming it here is the assertion.
    for bundle in bundles() {
        let shielded_data: &ShieldedData<PerSpendAnchor> = &bundle.shielded_data;
        assert_eq!(
            shielded_data.shared_anchor, FieldNotPresent,
            "{}: a v4 bundle has no shared anchor",
            bundle.name
        );
        for spend in shielded_data.spends() {
            let _: &Spend<PerSpendAnchor> = spend;
            // `per_spend_anchor` is a `tree::Root` for this variant, so it has
            // 32 raw bytes of its own.
            assert_eq!(spend.per_spend_anchor.0.len(), 32);
        }
    }
}

#[test]
fn the_corpus_bundles_hold_the_spends_and_outputs_the_blocks_encode() {
    let bundles = bundles();
    let spends: usize = bundles.iter().map(|b| b.shielded_data.spends().count()).sum();
    let outputs: usize = bundles
        .iter()
        .map(|b| b.shielded_data.outputs().count())
        .sum();

    assert_eq!(
        spends, 2,
        "the corpus encodes two Sapling Spend descriptions, found {}",
        spends
    );
    assert_eq!(
        outputs, 13,
        "the corpus encodes thirteen Sapling Output descriptions, found {}",
        outputs
    );
}

#[test]
fn a_bundle_always_holds_at_least_one_spend_or_output() {
    // `first` is not optional, so an empty bundle cannot be built. Every
    // bundle the parser produced must therefore have something in it, and the
    // iterators must agree with the `first`/`rest` split.
    for bundle in bundles() {
        let sd = &bundle.shielded_data;
        let spends = sd.spends().count();
        let outputs = sd.outputs().count();
        assert!(
            spends + outputs >= 1,
            "{}: a ShieldedData with no spends and no outputs must be unrepresentable",
            bundle.name
        );
        assert_eq!(
            spends + outputs,
            1 + sd.rest_spends.len() + sd.rest_outputs.len(),
            "{}: the iterators must cover `first` and then the rest, exactly once each",
            bundle.name
        );
    }
}

#[test]
fn first_is_a_spend_exactly_when_the_bundle_has_spends() {
    let mut with_spends = 0;
    let mut without = 0;
    for bundle in bundles() {
        let sd = &bundle.shielded_data;
        let spends = sd.spends().count();
        if spends > 0 {
            with_spends += 1;
            assert_eq!(
                spends,
                1 + sd.rest_spends.len(),
                "{}: when there are spends, `first` is one of them",
                bundle.name
            );
            assert_eq!(
                sd.outputs().count(),
                sd.rest_outputs.len(),
                "{}: `first` cannot be both a spend and an output",
                bundle.name
            );
        } else {
            without += 1;
            assert!(
                sd.rest_spends.is_empty(),
                "{}: a bundle whose `first` is an output has no spends at all",
                bundle.name
            );
            assert_eq!(
                sd.outputs().count(),
                1 + sd.rest_outputs.len(),
                "{}: when there are no spends, `first` is an output",
                bundle.name
            );
        }
    }
    assert!(
        with_spends > 0 && without > 0,
        "the corpus should span both shapes, found {} with spends and {} without",
        with_spends,
        without
    );
}

#[test]
fn nullifiers_come_from_spends_and_note_commitments_from_outputs() {
    for bundle in bundles() {
        let sd = &bundle.shielded_data;
        let nullifiers: Vec<_> = sd.nullifiers().collect();
        let spend_nullifiers: Vec<_> = sd.spends().map(|spend| &spend.nullifier).collect();
        assert_eq!(
            nullifiers, spend_nullifiers,
            "{}: nullifiers() must be the spends' nullifiers, in order",
            bundle.name
        );

        let commitments: Vec<_> = sd.note_commitments().collect();
        let output_commitments: Vec<_> = sd.outputs().map(|output| &output.cm_u).collect();
        assert_eq!(
            commitments, output_commitments,
            "{}: note_commitments() must be the outputs' cm_u values, in order",
            bundle.name
        );
    }
}

#[test]
fn spends_per_anchor_returns_the_same_spends_for_a_v4_bundle() {
    // For the per-spend variant the conversion is the identity: every Spend
    // already carries its own anchor. The method exists so that verification
    // code can be written once for v4 and v5.
    for bundle in bundles() {
        let sd = &bundle.shielded_data;
        let generic: Vec<Spend<PerSpendAnchor>> = sd.spends_per_anchor().collect();
        let native: Vec<Spend<PerSpendAnchor>> = sd.spends().cloned().collect();
        assert_eq!(
            generic, native,
            "{}: spends_per_anchor() must agree with spends() on a v4 bundle",
            bundle.name
        );
    }
}

#[test]
fn a_spend_is_384_bytes_with_its_anchor_at_offset_32() {
    // cv (32) || anchor (32) || nullifier (32) || rk (32) || zkproof (192)
    // || spend_auth_sig (64).
    let mut seen = 0;
    for bundle in bundles() {
        for spend in bundle.shielded_data.spends() {
            let wire = write(spend);
            assert_eq!(
                wire.len(),
                384,
                "{}: a v4 Spend description is 384 bytes",
                bundle.name
            );
            assert_eq!(
                &wire[32..64],
                &spend.per_spend_anchor.0[..],
                "{}: the anchor is the second 32-byte field of a Spend",
                bundle.name
            );
            assert_eq!(
                &wire[64..96],
                &<[u8; 32]>::from(spend.nullifier)[..],
                "{}: the nullifier follows the anchor",
                bundle.name
            );
            assert_eq!(
                &wire[96..128],
                &<[u8; 32]>::from(spend.rk)[..],
                "{}: rk follows the nullifier",
                bundle.name
            );
            assert_eq!(
                &wire[320..384],
                &<[u8; 64]>::from(spend.spend_auth_sig)[..],
                "{}: the spend authorisation signature is the last 64 bytes",
                bundle.name
            );
            seen += 1;
        }
    }
    assert_eq!(seen, 2, "the corpus holds two Spend descriptions");
}

#[test]
fn an_output_is_948_bytes() {
    // cv (32) || cm_u (32) || ephemeral_key (32) || enc_ciphertext (580)
    // || out_ciphertext (80) || zkproof (192).
    let mut seen = 0;
    for bundle in bundles() {
        for output in bundle.shielded_data.outputs() {
            let wire = write(output);
            assert_eq!(
                wire.len(),
                948,
                "{}: a Sapling Output description is 948 bytes",
                bundle.name
            );
            assert_eq!(
                &wire[32..64],
                &output.cm_u.to_bytes()[..],
                "{}: cm_u is the second 32-byte field of an Output",
                bundle.name
            );
            seen += 1;
        }
    }
    assert_eq!(seen, 13, "the corpus holds thirteen Output descriptions");
}

#[test]
fn the_binding_signature_verifies_under_the_derived_key() {
    // The binding verification key is a function of the bundle alone -- the
    // value commitments and the balance -- and the message is the
    // transaction's SIGHASH_ALL digest with no transparent input.
    let mut checked = 0;
    for bundle in bundles() {
        if bundle.is_coinbase {
            continue;
        }
        assert!(
            binding_signature_verifies(&bundle),
            "{}: the binding signature did not verify under the key derived from the bundle",
            bundle.name
        );
        checked += 1;
    }
    assert!(
        checked >= 10,
        "the corpus should hold at least ten non-coinbase Sapling bundles, checked {}",
        checked
    );
}

#[test]
fn the_binding_verification_key_is_a_function_of_the_bundle() {
    for bundle in bundles() {
        let sd = &bundle.shielded_data;
        assert_eq!(
            <[u8; 32]>::from(sd.binding_verification_key()),
            <[u8; 32]>::from(sd.binding_verification_key()),
            "{}: the derived key must be deterministic",
            bundle.name
        );

        if sd.rest_outputs.is_empty() {
            continue;
        }
        let mut shortened = sd.clone();
        shortened.rest_outputs.pop();
        assert_ne!(
            <[u8; 32]>::from(sd.binding_verification_key()),
            <[u8; 32]>::from(shortened.binding_verification_key()),
            "{}: dropping an Output must change the binding verification key, or the key does \
             not commit to the outputs",
            bundle.name
        );
    }
}

#[test]
fn transparent_transactions_have_no_shielded_data() {
    let mut transparent = 0;
    for fixture in blocks() {
        let block = parse(&fixture);
        for (index, tx) in block.transactions.iter().enumerate() {
            match &**tx {
                Transaction::V4 {
                    sapling_shielded_data,
                    ..
                } => {
                    let outputs_and_spends: usize = sapling_shielded_data
                        .as_ref()
                        .map(|sd| sd.spends().count() + sd.outputs().count())
                        .unwrap_or(0);
                    if sapling_shielded_data.is_none() {
                        transparent += 1;
                    } else {
                        assert!(
                            outputs_and_spends > 0,
                            "{}[{}]: Some(ShieldedData) must mean at least one description",
                            fixture.name,
                            index
                        );
                    }
                }
                _ => transparent += 1,
            }
        }
    }
    assert!(
        transparent > 0,
        "most of the corpus is transparent, so some transaction must have no shielded data"
    );
}

#[test]
fn an_output_description_is_not_a_spend_description() {
    // A quick structural sanity check on the `Either`: the compiler enforces
    // the types, and the sizes confirm the two descriptions are distinct
    // objects on the wire.
    let bundles = bundles();
    let spend: &Spend<PerSpendAnchor> = bundles
        .iter()
        .flat_map(|b| b.shielded_data.spends())
        .next()
        .expect("the corpus holds a Spend");
    let output: &Output = bundles
        .iter()
        .flat_map(|b| b.shielded_data.outputs())
        .next()
        .expect("the corpus holds an Output");

    assert_ne!(write(spend).len(), write(output).len());
    assert_eq!(write(spend).len(), 384);
    assert_eq!(write(output).len(), 948);
}
