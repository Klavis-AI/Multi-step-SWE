//! `zcash_serialize_external_count` / `zcash_deserialize_external_count`.
//!
//! Harbor verifier driver -- injected into `zebra-chain/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! `new` group. `zebra_chain::serialization::{zcash_serialize_external_count,
//! zcash_deserialize_external_count}` did not exist before this step. ZIP-225
//! needs them because a V5 transaction's Sapling bundle carries several arrays
//! whose lengths are never written on the wire a second time: `nSpendsSapling`
//! prefixes the spend array, but the spend proofs, spend auth sigs and output
//! proofs that follow later in the transaction have exactly that many entries
//! each with no compactSize of their own -- the count is "external" to them.
//! `Vec<T>::zcash_deserialize` (which reads its own compactSize) cannot be
//! used for those arrays; `zcash_deserialize_external_count` can.
//!
//! `T::max_allocation()` (the `TrustedPreallocate` trait, also public, also
//! part of this milestone's serialization surface) is what keeps an
//! attacker-supplied count from being used as a blind allocation size before a
//! single byte is even read. This driver picks `block::Hash` as `T` -- a
//! plain 32-byte array with no curve-point validity to worry about -- and
//! spells out its `max_allocation()` from public numbers only: this band's
//! `MAX_PROTOCOL_MESSAGE_LEN` constant and the 32-byte width `block::Hash`'s
//! own doc comment and serialize impl state, not from `BLOCK_HASH_SIZE`
//! (`pub(crate)`, unreachable here).

use std::io::Cursor;

use zebra_chain::block;
use zebra_chain::serialization::{
    zcash_deserialize_external_count, zcash_serialize_external_count, SerializationError,
    TrustedPreallocate, ZcashDeserialize, ZcashSerialize, MAX_PROTOCOL_MESSAGE_LEN,
};

// ----------------------------------------------------------------- helpers

/// A `block::Hash` with a distinct, easy-to-recognise byte pattern.
fn hash(fill: u8) -> block::Hash {
    block::Hash([fill; 32])
}

fn serialize_one(h: &block::Hash) -> Vec<u8> {
    let mut out = Vec::new();
    h.zcash_serialize(&mut out).expect("writing to a Vec is infallible");
    out
}

// ------------------------------------------------------------------- tests

#[test]
fn zcash_serialize_external_count_writes_each_elements_bytes_with_no_length_prefix() {
    let hashes = vec![hash(0x11), hash(0x22), hash(0x33)];
    let mut out = Vec::new();
    zcash_serialize_external_count(&hashes, &mut out).expect("writing to a Vec is infallible");

    let mut expected = Vec::new();
    for h in &hashes {
        expected.extend_from_slice(&serialize_one(h));
    }
    assert_eq!(
        out, expected,
        "external-count serialization should be exactly the elements' own bytes, back to back"
    );
    assert_eq!(
        out.len(),
        hashes.len() * 32,
        "there should be no compactSize or other length prefix anywhere in the output"
    );
}

#[test]
fn zcash_deserialize_external_count_is_the_exact_inverse_of_zcash_serialize_external_count() {
    let hashes = vec![hash(0xaa), hash(0xbb), hash(0xcc), hash(0xdd)];
    let mut wire = Vec::new();
    zcash_serialize_external_count(&hashes, &mut wire).expect("infallible");

    let restored: Vec<block::Hash> =
        zcash_deserialize_external_count(hashes.len(), Cursor::new(&wire[..]))
            .expect("the exact number of bytes for 4 hashes should deserialize");
    assert_eq!(restored, hashes, "round trip should preserve every hash, in order");
}

#[test]
fn zcash_deserialize_external_count_reads_exactly_the_requested_number_and_leaves_the_rest() {
    // Simulates the V5 shape this exists for: a count taken from elsewhere,
    // with more data following in the same reader that a later field will
    // read. Requesting fewer elements than the buffer holds must stop exactly
    // at the requested count's byte boundary.
    let hashes = vec![hash(1), hash(2), hash(3), hash(4), hash(5)];
    let mut wire = Vec::new();
    zcash_serialize_external_count(&hashes, &mut wire).expect("infallible");

    let mut cursor = Cursor::new(&wire[..]);
    let restored: Vec<block::Hash> =
        zcash_deserialize_external_count(3, &mut cursor).expect("first 3 of 5 should deserialize");
    assert_eq!(restored, &hashes[..3], "should read exactly the first 3 hashes");
    assert_eq!(
        cursor.position(),
        3 * 32,
        "the reader should stop at the 3rd hash's boundary, not consume the other 2"
    );
}

#[test]
fn hashes_max_allocation_matches_two_megabytes_divided_by_a_thirty_two_byte_hash() {
    // Independently derived from public numbers: this band's own
    // MAX_PROTOCOL_MESSAGE_LEN constant (2 * 1024 * 1024) and the 32-byte
    // width a block::Hash serializes to (checked here, not assumed).
    assert_eq!(MAX_PROTOCOL_MESSAGE_LEN, 2 * 1024 * 1024);
    assert_eq!(
        serialize_one(&hash(0)).len(),
        32,
        "a block::Hash should serialize to exactly 32 bytes"
    );
    let expected_max: u64 = (MAX_PROTOCOL_MESSAGE_LEN as u64 - 1) / 32;
    assert_eq!(expected_max, 65_535, "sanity-check the arithmetic itself");

    assert_eq!(
        block::Hash::max_allocation(),
        expected_max,
        "block::Hash::max_allocation() should be (MAX_PROTOCOL_MESSAGE_LEN - 1) / 32"
    );
}

#[test]
fn an_external_count_one_past_max_allocation_is_rejected_before_any_byte_is_read() {
    let too_many = (block::Hash::max_allocation() + 1) as usize;
    // An empty reader: if the implementation tried to read even one byte
    // before checking the count against max_allocation, this would fail with
    // an `Io` error instead, which is exactly what this test tells apart.
    let empty: &[u8] = &[];
    match zcash_deserialize_external_count::<_, block::Hash>(too_many, Cursor::new(empty)) {
        Err(SerializationError::Io(_)) => panic!(
            "count {} past max_allocation should be rejected by the max_allocation \
             check itself, not by running out of an empty reader",
            too_many
        ),
        Err(_) => {}
        Ok(_) => panic!(
            "a count of {} hashes (one past max_allocation) should not succeed",
            too_many
        ),
    }
}

#[test]
fn an_external_count_exactly_at_max_allocation_succeeds() {
    let at_max = block::Hash::max_allocation() as usize;
    let mut wire = Vec::with_capacity(at_max * 32);
    for i in 0..at_max {
        wire.extend_from_slice(&serialize_one(&hash((i % 256) as u8)));
    }
    let restored: Result<Vec<block::Hash>, _> =
        zcash_deserialize_external_count(at_max, Cursor::new(&wire[..]));
    assert!(
        restored.is_ok(),
        "a count exactly at max_allocation ({}) should be accepted, got {:?}",
        at_max,
        restored.err()
    );
    assert_eq!(restored.unwrap().len(), at_max);
}

#[test]
fn zero_is_a_valid_external_count_and_produces_an_empty_vector() {
    let empty: &[u8] = &[];
    let restored: Vec<block::Hash> =
        zcash_deserialize_external_count(0, Cursor::new(empty)).expect("count 0 should succeed");
    assert!(restored.is_empty(), "count 0 should produce an empty Vec");
}

#[test]
fn plain_vec_deserialize_reads_a_compactsize_prefix_that_external_count_does_not() {
    // The two entry points differ in exactly one way: `Vec<T>::zcash_deserialize`
    // reads a compactSize count off the wire first; `zcash_deserialize_external_count`
    // is handed the count from elsewhere and reads none. Feeding the plain
    // reader's wire format (with its leading compactSize byte) directly to
    // `zcash_deserialize_external_count` must therefore misparse: the count
    // byte gets consumed as if it were the first hash's first byte.
    let hashes = vec![hash(0x41), hash(0x42)];
    let mut wire_with_prefix = Vec::new();
    hashes
        .zcash_serialize(&mut wire_with_prefix)
        .expect("Vec<Hash>::zcash_serialize writes a compactsize count then the hashes");
    assert_eq!(
        wire_with_prefix.len(),
        1 + hashes.len() * 32,
        "Vec<Hash>::zcash_serialize should prefix a 1-byte compactsize count for 2 elements"
    );

    let via_plain_vec: Vec<block::Hash> = Vec::zcash_deserialize(Cursor::new(&wire_with_prefix[..]))
        .expect("the prefixed wire format should round trip through Vec::zcash_deserialize");
    assert_eq!(via_plain_vec, hashes);

    // Reading the *same* prefixed bytes through the external-count function
    // with the correct count does not skip the leading compactsize byte, so
    // it does not reproduce the original hashes.
    let via_external_count: Vec<block::Hash> =
        zcash_deserialize_external_count(hashes.len(), Cursor::new(&wire_with_prefix[..]))
            .expect("2 hashes' worth of bytes are still available even misaligned by one byte");
    assert_ne!(
        via_external_count, hashes,
        "zcash_deserialize_external_count should not have skipped the leading compactsize byte \
         the way Vec::zcash_deserialize does"
    );
}

#[test]
fn a_reader_that_runs_out_partway_through_the_requested_count_is_an_io_error() {
    // Contrasts with the max_allocation rejection above: a count within
    // max_allocation but with insufficient bytes behind it fails while
    // actually reading, so the error is `Io`, not `Parse`.
    let hashes = vec![hash(9), hash(9), hash(9)];
    let mut wire = Vec::new();
    zcash_serialize_external_count(&hashes, &mut wire).expect("infallible");
    let short = &wire[..wire.len() - 1];

    match zcash_deserialize_external_count::<_, block::Hash>(3, Cursor::new(short)) {
        Err(SerializationError::Io(_)) => {}
        Err(other) => panic!(
            "a reader one byte short of 3 hashes gave {:?}, expected an Io error",
            other
        ),
        Ok(_) => panic!("a reader one byte short of 3 hashes parsed successfully"),
    }
}

#[test]
fn duplicate_elements_are_preserved_not_deduplicated() {
    let same = hash(0x77);
    let hashes = vec![same, same, same];
    let mut wire = Vec::new();
    zcash_serialize_external_count(&hashes, &mut wire).expect("infallible");
    assert_eq!(wire.len(), 3 * 32, "3 identical hashes should still take 3 * 32 bytes");

    let restored: Vec<block::Hash> =
        zcash_deserialize_external_count(3, Cursor::new(&wire[..])).expect("3 identical hashes");
    assert_eq!(restored.len(), 3, "duplicates must not be collapsed into fewer elements");
    assert!(restored.iter().all(|h| *h == same));
}
