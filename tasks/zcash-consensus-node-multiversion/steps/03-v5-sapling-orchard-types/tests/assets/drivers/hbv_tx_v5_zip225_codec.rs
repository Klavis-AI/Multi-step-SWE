//! `Transaction::V5`'s wire codec, the ZIP-225 header and Sapling-in-V5 layout.
//!
//! Harbor verifier driver -- injected into `zebra-chain/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! `new` group. Before this step, `Transaction` only had `V1`..`V4` variants.
//! ZIP-225 defines a new `V5` transaction format: a header of
//! `(header, nVersionGroupId, nConsensusBranchId, lockTime, nExpiryHeight)`,
//! then plain transparent input/output arrays, then a Sapling bundle laid out
//! as *separate* arrays (spend prefixes, output prefixes, then proofs and
//! signatures counted by those arrays' lengths rather than their own
//! compactSize -- see `tx-v5-sapling-transfers` for that bundle's internals),
//! and finally an Orchard section that, at this step, must be exactly a
//! single zero compactSize byte (real Orchard-in-V5 wiring is a later
//! milestone).
//!
//! `$HBV_FIXTURES/v5tx_spends_and_outputs.hex` and `v5tx_just_outputs.hex` are
//! two real, well-formed V5 transactions; `$HBV_FIXTURES/v5tx.tsv` states,
//! independently of any Rust implementation, the fields each one must decode
//! to (network upgrade, lock time, expiry height, transparent/shielded
//! counts, value balance, shared anchor, total byte length). The other four
//! `.hex` fixtures are the first fixture with exactly one field corrupted --
//! a garbage consensus branch id, a swapped-in Sapling version group id, a
//! cleared overwintered bit, and a non-zero Orchard section -- to pin down
//! the four distinct rejection paths a V5 parser must have.

use std::io::Cursor;
use std::path::PathBuf;

use zebra_chain::block;
use zebra_chain::parameters::NetworkUpgrade;
use zebra_chain::parameters::TX_V5_VERSION_GROUP_ID;
use zebra_chain::serialization::{SerializationError, ZcashDeserialize, ZcashSerialize};
use zebra_chain::transaction::{LockTime, Transaction};

// ---------------------------------------------------------------- fixtures

fn fixture_dir() -> PathBuf {
    PathBuf::from(
        std::env::var("HBV_FIXTURES")
            .expect("HBV_FIXTURES must point at the shipped fixture directory"),
    )
}

fn hex_fixture(name: &str) -> Vec<u8> {
    let path = fixture_dir().join(format!("{}.hex", name));
    let text = std::fs::read_to_string(&path)
        .unwrap_or_else(|e| panic!("cannot read {}: {}", path.display(), e));
    hex::decode(text.trim()).unwrap_or_else(|e| panic!("{}: not valid hex: {}", path.display(), e))
}

/// The `bytes` column of `v5tx.tsv`, for the two well-formed fixtures. Not
/// read from the TSV parser used by `v5-consensus-branch-id`'s driver, so
/// that a bug in one driver's TSV reader can't hide a bug in the other's.
fn expected_length(name: &str) -> usize {
    match name {
        "v5tx_spends_and_outputs" => 2377,
        "v5tx_just_outputs" => 1226,
        other => panic!("no known expected length for {}", other),
    }
}

fn parse(bytes: &[u8]) -> Result<Transaction, SerializationError> {
    Transaction::zcash_deserialize(Cursor::new(bytes))
}

fn parse_message(bytes: &[u8]) -> &'static str {
    match parse(bytes) {
        Err(SerializationError::Parse(msg)) => msg,
        Err(other) => panic!("expected a Parse error, got {:?}", other),
        Ok(_) => panic!("expected parsing to fail, but it succeeded"),
    }
}

// ------------------------------------------------------------------- tests

#[test]
fn spends_and_outputs_fixture_round_trips_byte_for_byte() {
    let wire = hex_fixture("v5tx_spends_and_outputs");
    assert_eq!(
        wire.len(),
        expected_length("v5tx_spends_and_outputs"),
        "v5tx.tsv says this fixture is {} bytes",
        expected_length("v5tx_spends_and_outputs")
    );
    let tx = parse(&wire).expect("a well-formed V5 transaction should parse");
    let reserialized = tx.zcash_serialize_to_vec().expect("serializing to a Vec is infallible");
    assert_eq!(
        reserialized, wire,
        "re-serializing a parsed V5 transaction should reproduce its exact original bytes"
    );
}

#[test]
fn just_outputs_fixture_round_trips_byte_for_byte() {
    let wire = hex_fixture("v5tx_just_outputs");
    assert_eq!(wire.len(), expected_length("v5tx_just_outputs"));
    let tx = parse(&wire).expect("a well-formed V5 transaction should parse");
    let reserialized = tx.zcash_serialize_to_vec().expect("infallible");
    assert_eq!(reserialized, wire, "round trip should be exact");
}

#[test]
fn spends_and_outputs_fixture_decodes_the_fields_v5tx_tsv_says_it_should() {
    // v5tx.tsv: branch_id 0xe9ff75a6 (Canopy), 0 transparent inputs, 0
    // transparent outputs, 1 sapling spend, 2 sapling outputs, value_balance
    // 10000, shared_anchor_hex
    // 914e8b02fd17010db178285840a89c4de9b78fc2c0d6b926028b45626e1adf49.
    let wire = hex_fixture("v5tx_spends_and_outputs");
    let tx = parse(&wire).expect("should parse");
    match tx {
        Transaction::V5 {
            network_upgrade,
            lock_time,
            expiry_height,
            inputs,
            outputs,
            sapling_shielded_data,
        } => {
            assert_eq!(network_upgrade, NetworkUpgrade::Canopy, "v5tx.tsv names Canopy");
            assert_eq!(lock_time, LockTime::Height(block::Height(0)), "v5tx.tsv's raw bytes encode lockTime 0");
            assert_eq!(expiry_height, block::Height(903_040), "v5tx.tsv's raw bytes encode this expiry height");
            assert!(inputs.is_empty(), "v5tx.tsv says 0 transparent inputs");
            assert!(outputs.is_empty(), "v5tx.tsv says 0 transparent outputs");

            let shielded = sapling_shielded_data.expect("v5tx.tsv says this fixture has a Sapling bundle");
            assert_eq!(i64::from(shielded.value_balance), 10_000, "v5tx.tsv's value_balance column");
            let spends: Vec<_> = shielded.spends().collect();
            let outputs: Vec<_> = shielded.outputs().collect();
            assert_eq!(spends.len(), 1, "v5tx.tsv says 1 sapling spend");
            assert_eq!(outputs.len(), 2, "v5tx.tsv says 2 sapling outputs");
            let shared_anchor: [u8; 32] = shielded
                .shared_anchor()
                .expect("a transaction with a spend must have a shared anchor")
                .into();
            assert_eq!(
                hex::encode(shared_anchor),
                "914e8b02fd17010db178285840a89c4de9b78fc2c0d6b926028b45626e1adf49",
                "v5tx.tsv's shared_anchor_hex column"
            );
        }
        other => panic!("expected Transaction::V5, got {:?}", other),
    }
}

#[test]
fn just_outputs_fixture_decodes_the_fields_v5tx_tsv_says_it_should() {
    // v5tx.tsv: branch_id 0x76b809bb (Sapling), 1 transparent input, 1
    // transparent output, 0 sapling spends, 1 sapling output, value_balance
    // -20000, no shared anchor (no spends).
    let wire = hex_fixture("v5tx_just_outputs");
    let tx = parse(&wire).expect("should parse");
    match tx {
        Transaction::V5 {
            network_upgrade,
            expiry_height,
            inputs,
            outputs,
            sapling_shielded_data,
            ..
        } => {
            assert_eq!(network_upgrade, NetworkUpgrade::Sapling, "v5tx.tsv names Sapling");
            assert_eq!(expiry_height, block::Height(419_220), "v5tx.tsv's raw bytes encode this expiry height");
            assert_eq!(inputs.len(), 1, "v5tx.tsv says 1 transparent input");
            assert_eq!(outputs.len(), 1, "v5tx.tsv says 1 transparent output");

            let shielded = sapling_shielded_data.expect("v5tx.tsv says this fixture has a Sapling bundle");
            assert_eq!(i64::from(shielded.value_balance), -20_000, "v5tx.tsv's value_balance column");
            assert_eq!(shielded.spends().count(), 0, "v5tx.tsv says 0 sapling spends");
            assert_eq!(shielded.outputs().count(), 1, "v5tx.tsv says 1 sapling output");
            assert!(
                shielded.shared_anchor().is_none(),
                "a transaction with no spends must not carry a shared anchor"
            );
        }
        other => panic!("expected Transaction::V5, got {:?}", other),
    }
}

#[test]
fn transaction_v5_version_group_id_constant_matches_the_fixtures_own_bytes() {
    // Ties the public TX_V5_VERSION_GROUP_ID constant to the raw wire bytes,
    // independently of anything the parser itself does with them: bytes 4..8
    // of any V5 transaction are its nVersionGroupId, little-endian.
    let wire = hex_fixture("v5tx_spends_and_outputs");
    let mut group_id_bytes = [0u8; 4];
    group_id_bytes.copy_from_slice(&wire[4..8]);
    assert_eq!(
        u32::from_le_bytes(group_id_bytes),
        TX_V5_VERSION_GROUP_ID,
        "bytes 4..8 of a V5 transaction should be TX_V5_VERSION_GROUP_ID, little-endian"
    );
}

#[test]
fn a_garbage_consensus_branch_id_is_rejected() {
    let msg = parse_message(&hex_fixture("v5tx_bad_branch_id"));
    assert_eq!(
        msg, "expected a valid network upgrade from the consensus branch id",
        "an nConsensusBranchId with no assigned network upgrade should give this exact message"
    );
}

#[test]
fn a_sapling_version_group_id_in_a_v5_header_is_rejected() {
    // v5tx_bad_version_group_id.hex has the same header word (version 5,
    // overwintered) as the good fixture, but SAPLING_VERSION_GROUP_ID in the
    // nVersionGroupId slot instead of TX_V5_VERSION_GROUP_ID.
    let msg = parse_message(&hex_fixture("v5tx_bad_version_group_id"));
    assert_eq!(msg, "expected TX_V5_VERSION_GROUP_ID");
}

#[test]
fn clearing_the_overwintered_bit_on_a_version_5_header_is_rejected() {
    let msg = parse_message(&hex_fixture("v5tx_no_overwinter_bit"));
    assert_eq!(
        msg, "bad tx header",
        "version 5 with the overwintered bit cleared matches none of V1..V5's header shapes"
    );
}

#[test]
fn a_nonzero_orchard_section_is_rejected() {
    // Independently confirm the fixture really is "the good transaction with
    // one byte changed" before trusting what that byte's position implies:
    // diff it against the known-good fixture and check the difference is
    // exactly the final byte (the hardcoded nActionsOrchard compactsize).
    let good = hex_fixture("v5tx_spends_and_outputs");
    let bad = hex_fixture("v5tx_bad_orchard_section");
    assert_eq!(good.len(), bad.len(), "the two fixtures should be the same length");
    let diff_positions: Vec<usize> = good
        .iter()
        .zip(bad.iter())
        .enumerate()
        .filter(|(_, (g, b))| g != b)
        .map(|(i, _)| i)
        .collect();
    assert_eq!(
        diff_positions,
        vec![good.len() - 1],
        "v5tx_bad_orchard_section.hex should differ from the good fixture in exactly its last byte"
    );

    let msg = parse_message(&bad);
    assert_eq!(msg, "expected orchard section to be just a zero");
}

#[test]
fn a_v5_transaction_with_no_sapling_bundle_round_trips_with_none() {
    // The other direction of §4's cardinality rule: zero spends *and* zero
    // outputs means there is no bundle at all, so `sapling_shielded_data` is
    // `None` rather than an empty `ShieldedData`, and the two counts write
    // back as two zero bytes. Only the four counts differ from the shape the
    // well-formed fixture already proves the parser accepts, so the bytes are
    // taken from that fixture rather than hand-built.
    let good = hex_fixture("v5tx_spends_and_outputs");
    let mut wire = good[..22].to_vec(); // 20-byte header, lock time, expiry height,
                                        // transparent in/out counts
    wire.extend_from_slice(&[0x00, 0x00]); // nSpendsSapling, nOutputsSapling
    wire.push(0x00); // nActionsOrchard

    let tx = parse(&wire).unwrap_or_else(|e| {
        panic!("a V5 with no Sapling bundle and no Orchard section should parse: {:?}", e)
    });
    match &tx {
        Transaction::V5 {
            sapling_shielded_data,
            ..
        } => assert!(
            sapling_shielded_data.is_none(),
            "zero spends and zero outputs must deserialize to None, not to an empty bundle"
        ),
        other => panic!("expected a V5 transaction, got {:?}", other),
    }

    let mut reserialized = Vec::new();
    tx.zcash_serialize(&mut reserialized).expect("serialize");
    assert_eq!(
        reserialized, wire,
        "a None Sapling bundle must re-serialize as two zero counts"
    );
    assert_eq!(wire.len(), 25, "the constructed transaction is 25 bytes");
}

#[test]
fn truncating_the_final_byte_of_a_well_formed_v5_transaction_is_an_io_error() {
    // The last byte of a well-formed V5 transaction is the hardcoded
    // nActionsOrchard zero compactsize. Removing it should make the parser
    // run out of bytes while reading that field, which is an `Io` error --
    // distinct from the `Parse` errors above, which all come from bytes that
    // *are* present but semantically wrong.
    let mut wire = hex_fixture("v5tx_spends_and_outputs");
    wire.pop();
    match parse(&wire) {
        Err(SerializationError::Io(_)) => {}
        Err(other) => panic!("expected an Io error from a truncated transaction, got {:?}", other),
        Ok(_) => panic!("a transaction truncated by one byte should not parse"),
    }
}
