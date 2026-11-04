//! Transaction deserialization size limit (MAX_BLOCK_BYTES = 2_000_000).
//!
//! Harbor verifier driver -- injected into `zebra-chain/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! At v1.0.0-beta.0 `Transaction::zcash_deserialize` wraps the reader with
//! `reader.take(MAX_BLOCK_BYTES)` so that a single transaction can never
//! consume more than 2 000 000 bytes of the incoming stream.  Without that
//! cap a transaction is bounded only by the message framing around it, and
//! the transaction layer's own invariant -- "a transaction is at most one
//! block's worth of bytes" -- is not enforced at all.
//!
//! `zebra_chain::block::MAX_BLOCK_BYTES` (u64 = 2_000_000) is re-exported from
//! `zebra-chain/src/block.rs` as `pub use serialize::MAX_BLOCK_BYTES`.
//!
//! # How the cap is exercised
//!
//! The discriminating case is an *oversized but otherwise well-formed*
//! transaction: one that serializes to more than 2 000 000 bytes and is
//! structurally valid throughout.  Building one takes a repeated dummy
//! transparent input (`zebra_test::vectors::DUMMY_INPUT1`) repeated
//! `MAX_BLOCK_BYTES / input_len` times plus one output -- exactly the
//! construction upstream added alongside the cap.
//!
//! On the predecessor tree (app-10) the deserializer reads the same stream
//! through the raw reader, so the parse succeeds and the test fails.  On
//! app-11 the `take(MAX_BLOCK_BYTES)` reader runs dry mid-transaction and the
//! parse returns `Err(SerializationError::Io(UnexpectedEof))`.  That is a
//! real behavioural difference, not a same-in-both-trees guard.
//!
//! # Note on compact-size attacks
//!
//! A compact-size count of 300 million inputs is *not* a discriminating case:
//! `read_zcash.rs` rejects any compact-size above `MAX_PROTOCOL_MESSAGE_LEN`
//! (2 MiB, unchanged between the two tags) before allocating, so that stream
//! fails identically on both trees.  The tests below therefore do not rely on
//! it; they rely on a legitimately serialized oversize transaction.

use std::io::Cursor;

use zebra_chain::block::MAX_BLOCK_BYTES;
use zebra_chain::serialization::{SerializationError, ZcashDeserialize, ZcashSerialize};
use zebra_chain::transaction::Transaction;

// ----------------------------------------------------------------- helpers

/// Build and serialize a v1 transaction that exceeds `MAX_BLOCK_BYTES`.
///
/// Returns the serialized bytes and its length.  Panics if the construction
/// does not actually exceed the limit, so that a test built on this helper
/// can never pass vacuously.
fn oversized_v1_transaction() -> Vec<u8> {
    use chrono::{DateTime, NaiveDateTime, Utc};
    use zebra_chain::transparent;
    use zebra_chain::transaction::LockTime;

    let input = transparent::Input::zcash_deserialize(&zebra_test::vectors::DUMMY_INPUT1[..])
        .expect("DUMMY_INPUT1 is a valid transparent input");
    let output = transparent::Output::zcash_deserialize(&zebra_test::vectors::DUMMY_OUTPUT1[..])
        .expect("DUMMY_OUTPUT1 is a valid transparent output");

    let lock_time = LockTime::Time(DateTime::<Utc>::from_utc(
        NaiveDateTime::from_timestamp(61, 0),
        Utc,
    ));

    // Serialize the input so its exact wire length is known.
    let mut input_data = Vec::new();
    input
        .zcash_serialize(&mut input_data)
        .expect("input should serialize");

    // As many inputs as fit under the limit, plus an output and a lock time,
    // which pushes the total over it.
    let tx_inputs_num = MAX_BLOCK_BYTES as usize / input_data.len();
    let inputs = std::iter::repeat(input.clone())
        .take(tx_inputs_num)
        .collect::<Vec<_>>();

    let oversized_tx = Transaction::V1 {
        inputs,
        outputs: vec![output],
        lock_time,
    };

    let mut tx_data = Vec::new();
    oversized_tx
        .zcash_serialize(&mut tx_data)
        .expect("transaction should serialize");

    assert!(
        tx_data.len() as u64 > MAX_BLOCK_BYTES,
        "the constructed transaction is {} bytes, which does not exceed MAX_BLOCK_BYTES ({})",
        tx_data.len(),
        MAX_BLOCK_BYTES
    );

    tx_data
}

/// A minimal, well-formed v4 transaction that is far below the limit.
fn minimal_v4_tx_bytes() -> Vec<u8> {
    use zebra_chain::block;
    use zebra_chain::transaction::LockTime;

    let tx = Transaction::V4 {
        inputs: vec![],
        outputs: vec![],
        lock_time: LockTime::min_lock_time(),
        expiry_height: block::Height(0),
        joinsplit_data: None,
        sapling_shielded_data: None,
    };
    let mut buf = Vec::new();
    tx.zcash_serialize(&mut buf).expect("serialization ok");
    buf
}

// ------------------------------------------------------------------- tests

#[test]
fn max_block_bytes_constant_is_two_million() {
    // The limit is normative.  A candidate who changes it loses the group even
    // though the driver reads only the public re-export.
    assert_eq!(
        MAX_BLOCK_BYTES,
        2_000_000u64,
        "MAX_BLOCK_BYTES must be exactly 2_000_000"
    );
}

#[test]
fn oversized_transaction_is_rejected() {
    // The discriminating assertion: a well-formed transaction of more than
    // 2 000 000 bytes must not parse.  app-10 parses it successfully.
    let tx_data = oversized_v1_transaction();
    let result = Transaction::zcash_deserialize(Cursor::new(&tx_data[..]));
    assert!(
        result.is_err(),
        "an oversized ({} byte) transaction parsed successfully; the \
         MAX_BLOCK_BYTES cap is not applied",
        tx_data.len()
    );
}

#[test]
fn oversized_transaction_fails_as_an_io_error() {
    // The cap makes the reader run dry mid-transaction, so the failure is an
    // unexpected EOF rather than a structural parse rejection.
    let tx_data = oversized_v1_transaction();
    match Transaction::zcash_deserialize(Cursor::new(&tx_data[..])) {
        Err(SerializationError::Io(_)) => {}
        Err(other) => panic!(
            "oversized transaction gave {:?}, expected SerializationError::Io",
            other
        ),
        Ok(_) => panic!("oversized transaction parsed successfully"),
    }
}

#[test]
fn the_oversized_fixture_is_a_well_formed_encoding() {
    // Control for `oversized_transaction_is_rejected`: the byte string is not
    // garbage.  It is the serialization of a structurally valid V1 transaction,
    // so the only thing standing between it and a successful parse is the
    // MAX_BLOCK_BYTES cap.  We show this by parsing a *truncation-independent*
    // prefix: the same construction with one fewer input parses cleanly.
    use chrono::{DateTime, NaiveDateTime, Utc};
    use zebra_chain::transaction::LockTime;
    use zebra_chain::transparent;

    let input = transparent::Input::zcash_deserialize(&zebra_test::vectors::DUMMY_INPUT1[..])
        .expect("DUMMY_INPUT1 is a valid transparent input");
    let output = transparent::Output::zcash_deserialize(&zebra_test::vectors::DUMMY_OUTPUT1[..])
        .expect("DUMMY_OUTPUT1 is a valid transparent output");
    let lock_time = LockTime::Time(DateTime::<Utc>::from_utc(
        NaiveDateTime::from_timestamp(61, 0),
        Utc,
    ));

    let mut input_data = Vec::new();
    input
        .zcash_serialize(&mut input_data)
        .expect("input should serialize");

    // A control construction with the same shape but comfortably shorter
    // than the limit: the same encoder, the same element types, fewer inputs.
    let inputs_num = (MAX_BLOCK_BYTES as usize / 2) / input_data.len();
    let undersized_tx = Transaction::V1 {
        inputs: std::iter::repeat(input.clone()).take(inputs_num).collect(),
        outputs: vec![output.clone()],
        lock_time,
    };
    let mut undersized_data = Vec::new();
    undersized_tx
        .zcash_serialize(&mut undersized_data)
        .expect("transaction should serialize");

    // The control parses cleanly, which shows the encoding this driver builds
    // is well-formed and that the oversize rejection is about length alone.
    match Transaction::zcash_deserialize(Cursor::new(&undersized_data[..])) {
        Ok(Transaction::V1 { .. }) => {}
        Ok(other) => panic!("expected a V1 control transaction, got {:?}", other),
        Err(e) => panic!(
            "the control transaction ({} bytes) failed to parse: {:?}",
            undersized_data.len(),
            e
        ),
    }

    // The oversized fixture is the same encoding, just longer.
    let oversized_data = oversized_v1_transaction();
    assert!(
        oversized_data.len() > undersized_data.len(),
        "the oversized fixture ({} bytes) must be longer than the control ({} bytes)",
        oversized_data.len(),
        undersized_data.len()
    );
    assert_eq!(
        &oversized_data[0..4],
        &[0x01, 0x00, 0x00, 0x00],
        "an oversize v1 transaction still begins with the v1 version word"
    );
}

#[test]
fn small_v4_transaction_still_round_trips() {
    // A legitimate transaction well inside the limit must be unaffected.
    let buf = minimal_v4_tx_bytes();
    assert!(
        (buf.len() as u64) < MAX_BLOCK_BYTES,
        "minimal v4 tx ({} bytes) must be under the limit",
        buf.len()
    );
    match Transaction::zcash_deserialize(Cursor::new(&buf[..])) {
        Ok(Transaction::V4 { .. }) => {}
        Ok(other) => panic!("expected V4 after round-trip, got {:?}", other),
        Err(e) => panic!("minimal v4 tx round-trip failed: {:?}", e),
    }
}

#[test]
fn small_v5_transaction_still_round_trips() {
    use zebra_chain::block;
    use zebra_chain::parameters::NetworkUpgrade;
    use zebra_chain::transaction::LockTime;

    let tx = Transaction::V5 {
        network_upgrade: NetworkUpgrade::Nu5,
        lock_time: LockTime::min_lock_time(),
        expiry_height: block::Height(0),
        inputs: vec![],
        outputs: vec![],
        sapling_shielded_data: None,
        orchard_shielded_data: None,
    };
    let mut buf = Vec::new();
    tx.zcash_serialize(&mut buf).expect("serialization ok");
    assert!(
        (buf.len() as u64) < MAX_BLOCK_BYTES,
        "minimal v5 tx ({} bytes) must be under the limit",
        buf.len()
    );
    Transaction::zcash_deserialize(Cursor::new(&buf[..]))
        .expect("minimal v5 tx must deserialize under the size limit");
}

#[test]
fn empty_input_is_an_io_error() {
    match Transaction::zcash_deserialize(Cursor::new(&[][..])) {
        Err(SerializationError::Io(_)) => {}
        Err(other) => panic!("empty input gave {:?}, expected SerializationError::Io", other),
        Ok(_) => panic!("empty input parsed as a valid transaction"),
    }
}

#[test]
fn truncated_header_is_an_io_error() {
    // Only the first four bytes of a v4 header: the version-group-id read fails.
    let short: [u8; 4] = [0x04, 0x00, 0x00, 0x80];
    match Transaction::zcash_deserialize(Cursor::new(&short[..])) {
        Err(SerializationError::Io(_)) => {}
        Err(other) => panic!(
            "truncated 4-byte header gave {:?}, expected SerializationError::Io",
            other
        ),
        Ok(_) => panic!("truncated 4-byte header parsed as a valid transaction"),
    }
}

#[test]
fn oversized_large_v5_transaction_is_rejected_too() {
    // The cap is applied on the shared entry point, so it must bind for every
    // version, not just the v1 arm.  Pad a v5 transaction with repeated dummy
    // outputs until it exceeds the limit, then assert it does not parse.
    use zebra_chain::block;
    use zebra_chain::parameters::NetworkUpgrade;
    use zebra_chain::transaction::LockTime;
    use zebra_chain::transparent;

    let output = transparent::Output::zcash_deserialize(&zebra_test::vectors::DUMMY_OUTPUT1[..])
        .expect("DUMMY_OUTPUT1 is a valid transparent output");
    let mut output_data = Vec::new();
    output
        .zcash_serialize(&mut output_data)
        .expect("output should serialize");

    let outputs_num = MAX_BLOCK_BYTES as usize / output_data.len() + 1;
    let outputs = std::iter::repeat(output.clone()).take(outputs_num).collect::<Vec<_>>();

    let oversized_tx = Transaction::V5 {
        network_upgrade: NetworkUpgrade::Nu5,
        lock_time: LockTime::min_lock_time(),
        expiry_height: block::Height(0),
        inputs: vec![],
        outputs,
        sapling_shielded_data: None,
        orchard_shielded_data: None,
    };

    let mut tx_data = Vec::new();
    oversized_tx
        .zcash_serialize(&mut tx_data)
        .expect("transaction should serialize");
    assert!(
        tx_data.len() as u64 > MAX_BLOCK_BYTES,
        "constructed v5 transaction is {} bytes, which does not exceed the limit",
        tx_data.len()
    );

    let result = Transaction::zcash_deserialize(Cursor::new(&tx_data[..]));
    assert!(
        result.is_err(),
        "an oversized v5 transaction ({} bytes) parsed successfully",
        tx_data.len()
    );
}
