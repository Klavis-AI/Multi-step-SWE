//! The witnessed transaction ID: `transaction::WtxId` and its 64-byte form.
//!
//! Harbor verifier driver -- injected into `zebra-chain/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! A node that keeps unconfirmed transactions needs an identifier that the
//! transaction's own signatures cannot be changed underneath. The mined
//! transaction ID does not do this: from ZIP-244 on, a v5 txid commits only to
//! the transaction's *effects*, so two transactions with different signatures
//! share one txid. ZIP-239 fixes the gap by defining a *witnessed* transaction
//! ID -- the txid concatenated with the authorizing-data digest -- and uses it
//! on the wire for unmined transactions. This milestone is where that
//! identifier becomes a type.
//!
//! What this group pins down:
//!
//!   * the layout: `WtxId` is exactly the 32-byte `id` followed by the 32-byte
//!     `auth_digest`, in that order, in internal (wire) byte order;
//!   * the conversions in both directions, including the error a wrong-length
//!     slice must produce;
//!   * the text form: `Display` is the two identifiers, each in the reversed
//!     big-endian convention Zcash uses for hashes, concatenated to 128 hex
//!     characters, and `FromStr` inverts exactly that and rejects every other
//!     length;
//!   * the `ZcashSerialize`/`ZcashDeserialize` wire form, which is the same 64
//!     bytes with no length prefix and no framing -- asserted for all three
//!     types §2.4 names, so the `WtxId` layout is pinned against the two
//!     32-byte halves it is built from rather than on its own;
//!   * `AuthDigest`'s own newly public conversions and text forms, which the
//!     above is built out of.
//!
//! Expected values come from `zip244.tsv`, which carries the published ZIP-244
//! test vectors: `txid_hex` and `auth_digest_hex` are the two halves, in
//! internal byte order. Nothing here recomputes them.

use std::collections::BTreeMap;
use std::collections::HashSet;
use std::convert::{TryFrom, TryInto};
use std::io::Cursor;
use std::path::PathBuf;
use std::str::FromStr;
use std::sync::Arc;

use zebra_chain::serialization::{
    SerializationError, ZcashDeserialize, ZcashDeserializeInto, ZcashSerialize,
};
use zebra_chain::transaction::{AuthDigest, Hash, Transaction, WtxId};

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

/// One published ZIP-244 vector: a v5 transaction, its txid and its
/// authorizing-data digest, both in internal byte order.
struct Vector {
    name: String,
    tx: Transaction,
    txid: [u8; 32],
    auth_digest: [u8; 32],
}

fn vectors() -> Vec<Vector> {
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
        out.push(Vector {
            name,
            tx,
            txid,
            auth_digest,
        });
    }
    // Anti-degeneracy guard only: this is a property of the shipped corpus,
    // not of the code under test. A failure here means zip244.tsv changed.
    assert!(
        out.len() >= 10,
        "zip244.tsv shrank to {} rows; the verifier expects at least 10",
        out.len()
    );
    out
}

// ----------------------------------------------------------------- helpers

/// The 64-byte concatenation the interface requires, built here from the two
/// published halves so that nothing in the crate contributes to the expected
/// value.
fn expected_bytes(vector: &Vector) -> [u8; 64] {
    let mut out = [0u8; 64];
    out[..32].copy_from_slice(&vector.txid);
    out[32..].copy_from_slice(&vector.auth_digest);
    out
}

/// Zcash's big-endian display convention: the byte string, reversed, in
/// lowercase hex.
fn display_hex(bytes: &[u8; 32]) -> String {
    let mut reversed = *bytes;
    reversed.reverse();
    hex::encode(&reversed)
}

// ------------------------------------------------------------------- tests

#[test]
fn a_witnessed_id_is_the_txid_followed_by_the_authorizing_data_digest() {
    for vector in vectors() {
        let wtx_id = WtxId::from(&vector.tx);

        assert_eq!(
            wtx_id.id.0, vector.txid,
            "{}: WtxId.id is not the published ZIP-244 txid",
            vector.name
        );
        assert_eq!(
            <[u8; 32]>::from(wtx_id.auth_digest),
            vector.auth_digest,
            "{}: WtxId.auth_digest is not the published ZIP-244 auth digest",
            vector.name
        );
        assert_eq!(
            &wtx_id.as_bytes()[..],
            &expected_bytes(&vector)[..],
            "{}: the 64-byte form is not txid || auth_digest",
            vector.name
        );
    }
}

#[test]
fn every_way_of_naming_the_transaction_gives_the_same_witnessed_id() {
    for vector in vectors() {
        let from_ref = WtxId::from(&vector.tx);
        let owned = vector.tx.clone();
        let from_value = WtxId::from(owned);
        let from_arc = WtxId::from(Arc::new(vector.tx.clone()));

        assert_eq!(
            from_ref, from_value,
            "{}: WtxId::from(&Transaction) and WtxId::from(Transaction) disagree",
            vector.name
        );
        assert_eq!(
            from_ref, from_arc,
            "{}: WtxId::from(&Transaction) and WtxId::from(Arc<Transaction>) disagree",
            vector.name
        );
    }
}

#[test]
fn the_sixty_four_byte_form_round_trips_in_both_directions() {
    for vector in vectors() {
        let bytes = expected_bytes(&vector);

        // bytes -> WtxId: the halves must land in the documented order.
        let parsed = WtxId::from(bytes);
        assert_eq!(
            parsed.id.0, vector.txid,
            "{}: From<[u8; 64]> put the wrong 32 bytes in `id`",
            vector.name
        );
        assert_eq!(
            <[u8; 32]>::from(parsed.auth_digest),
            vector.auth_digest,
            "{}: From<[u8; 64]> put the wrong 32 bytes in `auth_digest`",
            vector.name
        );
        assert_eq!(
            parsed,
            WtxId::from(&vector.tx),
            "{}: the id rebuilt from bytes is not the id computed from the transaction",
            vector.name
        );

        // WtxId -> bytes, by value and by reference.
        assert_eq!(
            &<[u8; 64]>::from(parsed)[..],
            &bytes[..],
            "{}: From<WtxId> for [u8; 64] is not the inverse of From<[u8; 64]>",
            vector.name
        );
        assert_eq!(
            &<[u8; 64]>::from(&parsed)[..],
            &bytes[..],
            "{}: From<&WtxId> for [u8; 64] disagrees with the by-value conversion",
            vector.name
        );
    }
}

#[test]
fn a_slice_of_the_wrong_length_is_a_slice_length_error() {
    // The 64-byte form arrives from the network, so its length is
    // attacker-controlled and must be rejected with the dedicated error rather
    // than panicking or silently truncating.
    let vector = vectors().remove(0);
    let bytes = expected_bytes(&vector);

    for short_or_long in &[0usize, 32, 63, 65] {
        let mut slice = bytes.to_vec();
        slice.resize(*short_or_long, 0u8);

        match WtxId::try_from(&slice[..]) {
            Err(SerializationError::TryFromSliceError(_)) => {}
            Err(other) => panic!(
                "a {}-byte slice gave {:?}, expected SerializationError::TryFromSliceError",
                short_or_long, other
            ),
            Ok(id) => panic!(
                "a {}-byte slice parsed as the witnessed id {}",
                short_or_long, id
            ),
        }
    }

    // The owned and borrowed `Vec` conversions must agree with the slice one.
    let short: Vec<u8> = bytes[..63].to_vec();
    assert!(
        WtxId::try_from(short.clone()).is_err(),
        "TryFrom<Vec<u8>> accepted 63 bytes"
    );
    assert!(
        WtxId::try_from(&short).is_err(),
        "TryFrom<&Vec<u8>> accepted 63 bytes"
    );

    // ...and the right length must still succeed through all three.
    let exact: Vec<u8> = bytes.to_vec();
    let from_slice = WtxId::try_from(&exact[..]).expect("64 bytes is the right length");
    let from_vec = WtxId::try_from(exact.clone()).expect("64 bytes is the right length");
    let from_vec_ref = WtxId::try_from(&exact).expect("64 bytes is the right length");
    assert_eq!(from_slice, from_vec);
    assert_eq!(from_slice, from_vec_ref);
    assert_eq!(from_slice, WtxId::from(bytes));
}

#[test]
fn the_display_form_is_the_two_identifiers_in_big_endian_order() {
    for vector in vectors() {
        let wtx_id = WtxId::from(&vector.tx);
        let rendered = wtx_id.to_string();

        assert_eq!(
            rendered.len(),
            128,
            "{}: the displayed witnessed id is {} characters, expected 128",
            vector.name,
            rendered.len()
        );
        assert_eq!(
            &rendered[..64],
            display_hex(&vector.txid).as_str(),
            "{}: the first half of the displayed id is not the txid in big-endian order",
            vector.name
        );
        assert_eq!(
            &rendered[64..],
            display_hex(&vector.auth_digest).as_str(),
            "{}: the second half of the displayed id is not the auth digest in big-endian order",
            vector.name
        );
        assert_eq!(
            rendered,
            format!("{}{}", wtx_id.id, wtx_id.auth_digest),
            "{}: the displayed id is not the two halves' own Display output, concatenated",
            vector.name
        );
    }
}

#[test]
fn from_str_inverts_display_and_rejects_every_other_length() {
    for vector in vectors() {
        let wtx_id = WtxId::from(&vector.tx);
        let parsed = WtxId::from_str(&wtx_id.to_string()).unwrap_or_else(|e| {
            panic!(
                "{}: the displayed witnessed id did not parse back: {:?}",
                vector.name, e
            )
        });
        assert_eq!(
            parsed, wtx_id,
            "{}: the witnessed id does not survive a Display/FromStr round trip",
            vector.name
        );
    }

    let good = WtxId::from(&vectors().remove(0).tx).to_string();
    for wrong in &[
        String::new(),
        good[..64].to_string(),
        good[..127].to_string(),
        format!("{}0", good),
    ] {
        match WtxId::from_str(wrong) {
            Err(SerializationError::Parse(_)) => {}
            Err(other) => panic!(
                "a {}-character witnessed id gave {:?}, expected SerializationError::Parse",
                wrong.len(),
                other
            ),
            Ok(id) => panic!(
                "a {}-character witnessed id parsed as {}",
                wrong.len(),
                id
            ),
        }
    }
}

#[test]
fn the_wire_form_is_the_same_sixty_four_bytes_with_no_framing() {
    for vector in vectors() {
        let wtx_id = WtxId::from(&vector.tx);

        let mut written = Vec::new();
        wtx_id
            .zcash_serialize(&mut written)
            .expect("in-memory write");
        assert_eq!(
            &written[..],
            &expected_bytes(&vector)[..],
            "{}: the serialized witnessed id is not the bare 64 bytes",
            vector.name
        );

        let read: WtxId = (&written[..])
            .zcash_deserialize_into()
            .unwrap_or_else(|e| panic!("{}: the 64 bytes did not deserialize: {:?}", vector.name, e));
        assert_eq!(
            read, wtx_id,
            "{}: the witnessed id did not survive a wire round trip",
            vector.name
        );

        // The mined id serializes as its own bare 32 bytes, which is what
        // makes the concatenation above a valid wire encoding.
        let mut id_bytes = Vec::new();
        wtx_id.id.zcash_serialize(&mut id_bytes).expect("in-memory write");
        assert_eq!(
            &id_bytes[..],
            &vector.txid[..],
            "{}: transaction::Hash did not serialize as its 32 internal-order bytes",
            vector.name
        );
        let read_id: Hash = (&id_bytes[..])
            .zcash_deserialize_into()
            .unwrap_or_else(|e| panic!("{}: the 32 bytes did not deserialize: {:?}", vector.name, e));
        assert_eq!(read_id, wtx_id.id, "{}: transaction::Hash wire round trip", vector.name);
    }
}

#[test]
fn the_authorizing_data_digest_serializes_and_deserializes_as_its_bare_bytes() {
    // §2.4 states the same three-impl promise in one sentence: every one of the
    // three types is its bare bytes in internal order, with no length prefix and
    // no framing, and every `ZcashDeserialize` is the exact inverse of its
    // `ZcashSerialize`. The 64-byte case is asserted above; this is the other
    // 32-byte half, and it is asserted the same way -- the expected bytes come
    // from `zip244.tsv`, never from a round trip, so an implementation that
    // serialized something self-consistent but wrong still fails here.
    //
    // The digest's length arrives from the network on the same path the
    // witnessed ID's does, so the inverse has a boundary as well: a truncated
    // buffer is an error, not a panic and not a shorter value.
    for vector in vectors() {
        let digest = AuthDigest::from(vector.auth_digest);

        let mut written = Vec::new();
        digest
            .zcash_serialize(&mut written)
            .expect("in-memory write");
        assert_eq!(
            &written[..],
            &vector.auth_digest[..],
            "{}: AuthDigest did not serialize as its 32 internal-order bytes",
            vector.name
        );
        assert_eq!(
            written.len(),
            32,
            "{}: the serialized digest is not exactly 32 bytes -- a length prefix or a tag",
            vector.name
        );

        // ...and the deserializer inverts exactly that.
        let read: AuthDigest = (&written[..]).zcash_deserialize_into().unwrap_or_else(|e| {
            panic!("{}: the 32 bytes did not deserialize: {:?}", vector.name, e)
        });
        assert_eq!(
            read, digest,
            "{}: the authorizing data digest did not survive a wire round trip",
            vector.name
        );
        assert_eq!(
            <[u8; 32]>::from(read),
            vector.auth_digest,
            "{}: the deserialized digest is not the published bytes in internal order",
            vector.name
        );

        // One byte short is a read error, not a silent truncation.
        for short in &[0usize, 31] {
            let mut truncated = written.clone();
            truncated.resize(*short, 0u8);
            let result: Result<AuthDigest, SerializationError> =
                (&truncated[..]).zcash_deserialize_into();
            assert!(
                result.is_err(),
                "{}: a {}-byte buffer deserialized as {:?}",
                vector.name,
                short,
                result
            );
        }
    }
}

#[test]
fn the_authorizing_data_digest_converts_and_prints_like_any_other_zcash_hash() {
    for vector in vectors() {
        let digest = AuthDigest::from(vector.auth_digest);

        assert_eq!(
            <[u8; 32]>::from(digest),
            vector.auth_digest,
            "{}: AuthDigest's byte conversions are not inverses",
            vector.name
        );
        assert_eq!(
            <[u8; 32]>::from(&digest),
            vector.auth_digest,
            "{}: From<&AuthDigest> for [u8; 32] disagrees with the by-value conversion",
            vector.name
        );
        assert_eq!(
            digest,
            AuthDigest::from(&vector.tx),
            "{}: the digest built from bytes is not the digest computed from the transaction",
            vector.name
        );
        assert_eq!(
            digest.to_string(),
            display_hex(&vector.auth_digest),
            "{}: AuthDigest is not displayed in the big-endian convention",
            vector.name
        );
        let parsed = AuthDigest::from_str(&digest.to_string())
            .unwrap_or_else(|e| panic!("{}: the digest did not parse back: {:?}", vector.name, e));
        assert_eq!(
            parsed, digest,
            "{}: AuthDigest does not survive a Display/FromStr round trip",
            vector.name
        );

        let mut written = Vec::new();
        digest.zcash_serialize(&mut written).expect("in-memory write");
        assert_eq!(
            &written[..],
            &vector.auth_digest[..],
            "{}: AuthDigest did not serialize as its 32 internal-order bytes",
            vector.name
        );
    }
}

#[test]
fn the_authorizing_data_digest_rejects_a_hex_string_that_is_not_thirty_two_bytes() {
    // §2.3: a hex string that does not decode into exactly 32 bytes is a
    // `Parse` error. Structurally the same rule `WtxId::from_str` is held to
    // above, and the same attacker-controlled input -- a peer can send any
    // string it likes -- so it is exercised here at the boundary lengths the
    // conversion has to get right: empty, one byte short, one byte long, and
    // a non-hex string of legal length.
    let good = AuthDigest::from(vectors().remove(0).auth_digest).to_string();
    assert_eq!(good.len(), 64, "the fixture digest is not 64 hex characters");

    for wrong in &[
        String::new(),
        good[..62].to_string(),
        format!("{}ab", good),
        "z".repeat(64),
    ] {
        match AuthDigest::from_str(wrong) {
            Err(SerializationError::Parse(_)) => {}
            Err(other) => panic!(
                "a {}-character digest string gave {:?}, expected SerializationError::Parse",
                wrong.len(),
                other
            ),
            Ok(digest) => panic!(
                "a {}-character digest string parsed as {:?}; it is not 32 bytes of hex",
                wrong.len(),
                digest
            ),
        }
    }
}

#[test]
fn witnessed_ids_are_hashable_and_distinguish_the_published_vectors() {
    // The mempool and the inventory layer both key sets on this type, so it
    // must be usable as a `HashSet` key and must not collapse two distinct
    // published transactions onto one entry.
    let vectors = vectors();
    let mut set: HashSet<WtxId> = HashSet::new();
    for vector in &vectors {
        set.insert(WtxId::from(&vector.tx));
    }

    let distinct_txids: HashSet<[u8; 32]> = vectors.iter().map(|v| v.txid).collect();
    assert_eq!(
        set.len(),
        distinct_txids.len(),
        "the published vectors have {} distinct txids but only {} distinct witnessed ids",
        distinct_txids.len(),
        set.len()
    );

    // Re-inserting the same id must not grow the set.
    let before = set.len();
    set.insert(WtxId::from(&vectors[0].tx));
    assert_eq!(before, set.len(), "an equal WtxId was not deduplicated");

    // A witnessed id whose auth digest differs by one bit is a different id,
    // even though its mined id is unchanged.
    let mut flipped = expected_bytes(&vectors[0]);
    flipped[63] ^= 0x01;
    let flipped: WtxId = flipped.into();
    let original = WtxId::from(&vectors[0].tx);
    assert_eq!(
        flipped.id, original.id,
        "flipping an auth-digest bit must not change the mined id"
    );
    assert_ne!(
        flipped, original,
        "flipping an auth-digest bit must change the witnessed id"
    );
    assert!(
        !set.contains(&flipped),
        "a witnessed id with a different auth digest was treated as already present"
    );
}

#[test]
fn a_witnessed_id_is_sixty_four_bytes_that_split_cleanly() {
    // Guards against an implementation that stores the two halves the other
    // way round: for every published vector the two halves differ, so a
    // swapped layout is observable.
    for vector in vectors() {
        assert_ne!(
            vector.txid, vector.auth_digest,
            "{}: the published vector cannot distinguish the halves",
            vector.name
        );

        let wtx_id = WtxId::from(&vector.tx);
        let bytes = wtx_id.as_bytes();
        let head: [u8; 32] = bytes[..32].try_into().expect("64 bytes split at 32");
        let tail: [u8; 32] = bytes[32..].try_into().expect("64 bytes split at 32");

        assert_eq!(
            head, vector.txid,
            "{}: the first 32 bytes are not the txid",
            vector.name
        );
        assert_eq!(
            tail, vector.auth_digest,
            "{}: the last 32 bytes are not the auth digest",
            vector.name
        );
    }
}
