//! TrustedPreallocate DoS hardening — alpha.6's headline serialization defence.
//!
//! Every `Vec<T: TrustedPreallocate>` deserialize checks `len > T::max_allocation()` and fails
//! with a `SerializationError::Parse` instead of allocating unbounded memory.
//! We confirm that the limit gates before allocation, that `len == max_allocation()` passes the
//! check and only then runs out of data (`Io`), and that the published constants are the exact
//! floors and ceilings an honest peer can produce.

use zebra_chain::{
    block::{CountedHeader, Hash},
    primitives::{Bctv14Proof, Groth16Proof},
    sapling::{Output, PerSpendAnchor, SharedAnchor, Spend},
    serialization::{
        SerializationError, TrustedPreallocate, ZcashDeserialize,
        MAX_PROTOCOL_MESSAGE_LEN,
    },
    sprout::JoinSplit,
    transaction::Transaction,
    transparent,
};
use std::sync::Arc;

/// A `block::Hash` is 32 bytes on the wire, and every vector carries at least a 1-byte length.
const BLOCK_HASH_SIZE: u64 = 32;
/// A Bitcoind-style transparent input: OutPoint (32 + 4), sequence (4), minimal script (1).
const MIN_TRANSPARENT_INPUT_SIZE: u64 = 32 + 4 + 4 + 1;
/// A transparent output: 8-byte value plus a minimal script.
const MIN_TRANSPARENT_OUTPUT_SIZE: u64 = 8 + 1;
/// The smallest possible transaction: one input, a locktime, one output.
const MIN_TRANSPARENT_TX_SIZE: u64 =
    MIN_TRANSPARENT_INPUT_SIZE + 4 + MIN_TRANSPARENT_OUTPUT_SIZE;
/// `block::serialize::MAX_BLOCK_BYTES` — the largest block the consensus rules permit.
const MAX_BLOCK_BYTES: u64 = 2_000_000;
/// A Sapling output: cv, cmu, epk, enc_ciphertext, out_ciphertext, zkproof.
const SAPLING_OUTPUT_SIZE: u64 = 32 + 32 + 32 + 580 + 80 + 192;
/// A Sapling spend without its anchor: cv, nullifier, rk. The anchor is 32 further bytes
/// for `PerSpendAnchor`, and the zkproof (192) and spend_auth_sig (64) always follow.
const SAPLING_SPEND_INITIAL_SIZE: u64 = 32 + 32 + 32;
const SAPLING_SPEND_FULL_SIZE: u64 = SAPLING_SPEND_INITIAL_SIZE + 192 + 64;
const SAPLING_SPEND_PER_ANCHOR_SIZE: u64 = SAPLING_SPEND_FULL_SIZE + 32;
/// A Sprout JoinSplit without its proof: vpub_old, vpub_new, anchor, two nullifiers, two
/// commitments, the ephemeral key, the random seed, two MACs, and two 601-byte ciphertexts.
const JOINSPLIT_WITHOUT_ZKPROOF: u64 =
    8 + 8 + 32 + (32 * 2) + (32 * 2) + 32 + 32 + (32 * 2) + (601 * 2);
/// BCTV14 proofs are 296 bytes, Groth16 proofs are 192.
const BCTV14_ZKPROOF_SIZE: u64 = 296;
const GROTH16_ZKPROOF_SIZE: u64 = 192;
/// Equihash solution input, the 32-byte nonce, the 3-byte solution length, and the solution.
const EQUIHASH_INPUT_LENGTH: u64 = 4 + 32 * 3 + 4 * 2;
const EQUIHASH_SOLUTION_SIZE: u64 = 1344;

/// Serialize a `u64` in Bitcoin CompactSize form, restated from the ZIP-243 / protocol spec
/// rules so the fixtures below do not depend on the crate's own writer.
fn compactsize(n: u64) -> Vec<u8> {
    if n < 0xfd {
        vec![n as u8]
    } else if n <= 0xffff {
        let mut v = vec![0xfd];
        v.extend_from_slice(&(n as u16).to_le_bytes());
        v
    } else if n <= 0xffff_ffff {
        let mut v = vec![0xfe];
        v.extend_from_slice(&(n as u32).to_le_bytes());
        v
    } else {
        let mut v = vec![0xff];
        v.extend_from_slice(&n.to_le_bytes());
        v
    }
}

// ---- the published ceilings ----

#[test]
fn block_hash_max_allocation_is_the_hash_count_a_message_can_hold() {
    let expected = ((MAX_PROTOCOL_MESSAGE_LEN - 1) as u64) / BLOCK_HASH_SIZE;
    assert_eq!(expected, 65535);
    assert_eq!(
        Hash::max_allocation(),
        expected,
        "block::Hash::max_allocation() must be (MAX_PROTOCOL_MESSAGE_LEN - 1) / 32"
    );
}

#[test]
fn counted_header_max_allocation_is_derived_from_the_header_length() {
    let min_counted_header = EQUIHASH_INPUT_LENGTH + 32 + 3 + EQUIHASH_SOLUTION_SIZE + 1;
    let expected = ((MAX_PROTOCOL_MESSAGE_LEN - 1) / min_counted_header as usize) as u64;
    assert_eq!(
        CountedHeader::max_allocation(),
        expected,
        "CountedHeader::max_allocation() must be (MAX_PROTOCOL_MESSAGE_LEN - 1) / (1487 + 1)"
    );
    assert_eq!(CountedHeader::max_allocation(), 1409);
}

#[test]
fn arc_transaction_max_allocation_is_bounded_by_the_max_block_size() {
    let expected = MAX_BLOCK_BYTES / MIN_TRANSPARENT_TX_SIZE;
    assert_eq!(expected, 37037);
    assert_eq!(
        Arc::<Transaction>::max_allocation(),
        expected,
        "Arc<Transaction>::max_allocation() must be MAX_BLOCK_BYTES / MIN_TRANSPARENT_TX_SIZE"
    );
}

#[test]
fn transparent_input_max_allocation_is_bounded_by_the_max_block_size() {
    let expected = MAX_BLOCK_BYTES / MIN_TRANSPARENT_INPUT_SIZE;
    assert_eq!(expected, 48780);
    assert_eq!(transparent::Input::max_allocation(), expected);
}

#[test]
fn transparent_output_max_allocation_is_bounded_by_the_max_block_size() {
    let expected = MAX_BLOCK_BYTES / MIN_TRANSPARENT_OUTPUT_SIZE;
    assert_eq!(expected, 222222);
    assert_eq!(transparent::Output::max_allocation(), expected);
}

#[test]
fn sapling_output_max_allocation_uses_the_full_output_size() {
    let expected = (MAX_BLOCK_BYTES - 1) / SAPLING_OUTPUT_SIZE;
    assert_eq!(expected, 2109);
    assert_eq!(Output::max_allocation(), expected);
}

#[test]
fn sapling_shared_anchor_spend_uses_the_full_spend_size() {
    let expected = (MAX_BLOCK_BYTES - 1) / SAPLING_SPEND_FULL_SIZE;
    assert_eq!(expected, 5681);
    assert_eq!(Spend::<SharedAnchor>::max_allocation(), expected);
}

#[test]
fn sapling_per_spend_anchor_spend_costs_32_extra_bytes() {
    let expected = (MAX_BLOCK_BYTES - 1) / SAPLING_SPEND_PER_ANCHOR_SIZE;
    assert_eq!(expected, 5208);
    assert_eq!(Spend::<PerSpendAnchor>::max_allocation(), expected);
}

#[test]
fn sprout_bctv14_joinsplits_use_296_byte_proofs() {
    let expected = (MAX_BLOCK_BYTES - 1) / (JOINSPLIT_WITHOUT_ZKPROOF + BCTV14_ZKPROOF_SIZE);
    assert_eq!(expected, 1109);
    assert_eq!(JoinSplit::<Bctv14Proof>::max_allocation(), expected);
}

#[test]
fn sprout_groth16_joinsplits_use_192_byte_proofs() {
    let expected = (MAX_BLOCK_BYTES - 1) / (JOINSPLIT_WITHOUT_ZKPROOF + GROTH16_ZKPROOF_SIZE);
    assert_eq!(expected, 1177);
    assert_eq!(JoinSplit::<Groth16Proof>::max_allocation(), expected);
}

// ---- the gate itself ----

#[test]
fn byte_vector_limit_is_five_bytes_below_the_protocol_message_limit() {
    // The largest `Vec<u8>` an honest peer can send is MAX_PROTOCOL_MESSAGE_LEN - 5, because the
    // CompactSize prefix for a length in that range takes exactly 5 bytes (0xff plus a u64).
    let payload_len = MAX_PROTOCOL_MESSAGE_LEN - 5;
    let mut wire = compactsize(payload_len as u64);
    assert_eq!(wire.len(), 5);
    wire.resize(MAX_PROTOCOL_MESSAGE_LEN, 0xab);
    let decoded = <Vec<u8>>::zcash_deserialize(&wire[..])
        .expect("a full-size byte vector must deserialize");
    assert_eq!(decoded.len(), payload_len);
    assert!(
        decoded.iter().all(|b| *b == 0xab),
        "every byte of a maximal Vec<u8> must survive the round trip"
    );
}

#[test]
fn byte_vector_rejects_one_byte_more_than_the_protocol_limit() {
    let mut wire = compactsize((MAX_PROTOCOL_MESSAGE_LEN - 4) as u64);
    wire.resize(16, 0);
    let result = <Vec<u8>>::zcash_deserialize(&wire[..]);
    assert!(
        matches!(result, Err(SerializationError::Parse(_))),
        "a Vec<u8> of length MAX_PROTOCOL_MESSAGE_LEN - 4 must be rejected before allocation, \
         got {:?}",
        result
    );
}

#[test]
fn hash_vector_rejects_one_hash_past_the_limit_before_reading_bodies() {
    let count = Hash::max_allocation() + 1;
    // Deliberately supply no hash bodies at all: if the length gate did not run before the
    // read loop, this would fail with an end-of-file error instead.
    let wire = compactsize(count);
    let result = Vec::<Hash>::zcash_deserialize(&wire[..]);
    assert!(
        matches!(result, Err(SerializationError::Parse(_))),
        "a Vec<block::Hash> one element over the limit must be rejected without reading bodies, \
         got {:?}",
        result
    );
}

#[test]
fn hash_vector_at_the_limit_passes_the_gate_then_reports_eof() {
    let count = Hash::max_allocation();
    let wire = compactsize(count);
    let result = Vec::<Hash>::zcash_deserialize(&wire[..]);
    match result {
        Err(SerializationError::Io(_)) => {}
        other => panic!(
            "a Vec<block::Hash> of exactly max_allocation() elements must pass the length gate \
             and then fail for want of data, got {:?}",
            other
        ),
    }
}
