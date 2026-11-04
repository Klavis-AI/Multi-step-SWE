//! Regression guard: ZIP-143/ZIP-243 signature hashes, against the ZIPs' own
//! worked examples.
//!
//! Harbor verifier driver -- injected into `zebra-chain/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! Adapted from step 01's `hbv_sighash_zip143_zip243.rs`. This milestone
//! rewrites `Transaction::sighash` to delegate to a bridge crate for every
//! post-Overwinter network upgrade (including the new v5/NU5 case), and
//! changes its return type from a BLAKE2b state to a plain `SigHash` newtype.
//! The five ZIP-143/ZIP-243 vectors do not involve NU5 at all, so this group
//! exists purely to prove that rewrite did not disturb the four upgrades that
//! came before it -- the same five ZIP-published digests must still come out
//! byte for byte.
//!
//! Two details of *this* band's API that differ from step 01's:
//!
//!   * `Transaction::sighash` now returns `transaction::SigHash`, a
//!     `pub struct SigHash(pub [u8; 32])`. The inner array is a public field,
//!     so `hash.0` is the digest, already exactly 32 bytes by the type
//!     itself -- there is no slice length left to check. It is in the same
//!     byte order the ZIP prints it in; nothing reverses it the way
//!     `block::Hash` and `transaction::Hash` are reversed on `Display`.
//!
//!   * `transparent::Script`'s inner `Vec<u8>` is private in this band, and
//!     the bridge this milestone routes signature hashing through consumes
//!     `Script::as_raw_bytes()` directly, with no length prefix folded in.
//!     So a vector's `script_hex` column (already prefix-free, per
//!     `sighash.tsv`'s own column description) is handed to
//!     `transparent::Script::new` unmodified -- no hand-rolled compactSize
//!     prefix is assembled here, unlike step 01's `scriptcode` helper.

use std::collections::BTreeMap;
use std::convert::TryFrom;
use std::io::Cursor;
use std::path::PathBuf;

use zebra_chain::amount::{Amount, NonNegative};
use zebra_chain::parameters::NetworkUpgrade;
use zebra_chain::serialization::{ZcashDeserialize, ZcashSerialize};
use zebra_chain::transaction::{HashType, Transaction};
use zebra_chain::transparent;

// ---------------------------------------------------------------- fixtures
//
// `sighash.tsv` holds five signature-hash vectors lifted out of the normative
// text of ZIP-143 and ZIP-243 -- the `.rst` sources, not any implementation of
// them. Columns:
//
//   name  zip  network_upgrade  hash_type  input_index
//   script_hex  amount  raw_tx_hex  expected_sighash
//
// `input_index = -1` marks a vector that signs no particular input, so it has
// neither a script nor an amount to supply; `script_hex` is the *raw* script,
// without any compactSize prefix.

struct Vector {
    name: String,
    /// 143 or 243.
    zip: u32,
    network_upgrade: NetworkUpgrade,
    /// The raw `nHashType` byte from the ZIP, before it becomes a `HashType`.
    hash_type_bits: u32,
    /// The index of the input being signed, or -1 for "no input".
    input_index: i64,
    /// The scriptCode of the spent output, raw, with no length prefix.
    script: Vec<u8>,
    /// The value of the spent output, in zatoshi.
    amount: i64,
    raw_tx: Vec<u8>,
    /// The digest, in the byte order the ZIP prints it.
    expected: String,
}

fn fixture_dir() -> PathBuf {
    PathBuf::from(
        std::env::var("HBV_FIXTURES")
            .expect("HBV_FIXTURES must point at the shipped fixture directory"),
    )
}

fn cell<'a>(row: &BTreeMap<&'a str, &'a str>, key: &str) -> &'a str {
    *row.get(key)
        .unwrap_or_else(|| panic!("sighash.tsv has no column {}", key))
}

/// Load the five ZIP-143/ZIP-243 vectors, in the order the TSV lists them.
fn vectors() -> Vec<Vector> {
    let path = fixture_dir().join("sighash.tsv");
    let tsv = std::fs::read_to_string(&path)
        .unwrap_or_else(|e| panic!("cannot read {}: {}", path.display(), e));
    let mut lines = tsv.lines();
    let header: Vec<&str> = lines
        .next()
        .expect("sighash.tsv has a header row")
        .trim_start_matches("# ")
        .split('\t')
        .collect();

    let mut out = Vec::new();
    for line in lines {
        if line.is_empty() {
            continue;
        }
        let cells: Vec<&str> = line.split('\t').collect();
        assert_eq!(
            cells.len(),
            header.len(),
            "malformed sighash.tsv row: {} cells, header has {}",
            cells.len(),
            header.len()
        );
        let row: BTreeMap<&str, &str> = header.iter().cloned().zip(cells).collect();

        let name = cell(&row, "name").to_string();
        out.push(Vector {
            zip: cell(&row, "zip")
                .parse()
                .unwrap_or_else(|e| panic!("{}: zip column: {}", name, e)),
            network_upgrade: upgrade_named(&name, cell(&row, "network_upgrade")),
            hash_type_bits: cell(&row, "hash_type")
                .parse()
                .unwrap_or_else(|e| panic!("{}: hash_type column: {}", name, e)),
            input_index: cell(&row, "input_index")
                .parse()
                .unwrap_or_else(|e| panic!("{}: input_index column: {}", name, e)),
            script: hex::decode(cell(&row, "script_hex"))
                .unwrap_or_else(|e| panic!("{}: script_hex is not hex: {}", name, e)),
            amount: cell(&row, "amount")
                .parse()
                .unwrap_or_else(|e| panic!("{}: amount column: {}", name, e)),
            raw_tx: hex::decode(cell(&row, "raw_tx_hex"))
                .unwrap_or_else(|e| panic!("{}: raw_tx_hex is not hex: {}", name, e)),
            expected: cell(&row, "expected_sighash").to_lowercase(),
            name,
        });
    }

    assert_eq!(
        out.len(),
        5,
        "sighash.tsv should ship the five ZIP-143/ZIP-243 vectors, found {}",
        out.len()
    );
    out
}

fn upgrade_named(vector: &str, name: &str) -> NetworkUpgrade {
    match name {
        "Overwinter" => NetworkUpgrade::Overwinter,
        "Sapling" => NetworkUpgrade::Sapling,
        "Blossom" => NetworkUpgrade::Blossom,
        "Heartwood" => NetworkUpgrade::Heartwood,
        "Canopy" => NetworkUpgrade::Canopy,
        other => panic!("{}: unknown network upgrade {:?}", vector, other),
    }
}

// ----------------------------------------------------------------- helpers

/// Every pre-NU5 network upgrade that has a consensus branch id, and so a
/// signature hash. Restated here from ZIP-200's deployment table rather than
/// read out of the crate.
static SIGNING_UPGRADES: &[(&str, NetworkUpgrade)] = &[
    ("Overwinter", NetworkUpgrade::Overwinter),
    ("Sapling", NetworkUpgrade::Sapling),
    ("Blossom", NetworkUpgrade::Blossom),
    ("Heartwood", NetworkUpgrade::Heartwood),
    ("Canopy", NetworkUpgrade::Canopy),
];

fn parse(vector: &Vector) -> Transaction {
    Transaction::zcash_deserialize(Cursor::new(&vector.raw_tx[..]))
        .unwrap_or_else(|e| panic!("{}: the vector transaction did not parse: {:?}", vector.name, e))
}

fn hash_type_of(vector: &Vector) -> HashType {
    HashType::from_bits(vector.hash_type_bits).unwrap_or_else(|| {
        panic!(
            "{}: nHashType {} is not a HashType this crate knows",
            vector.name, vector.hash_type_bits
        )
    })
}

fn spent_output(vector: &Vector, amount: i64, script: &[u8]) -> transparent::Output {
    transparent::Output {
        value: Amount::<NonNegative>::try_from(amount).unwrap_or_else(|e| {
            panic!(
                "{}: {} zatoshi is not a valid non-negative amount: {:?}",
                vector.name, amount, e
            )
        }),
        lock_script: transparent::Script::new(script),
    }
}

/// The `input` argument for this vector: `None` when the vector signs no
/// particular input, otherwise the index and the output it spends.
fn signed_input(vector: &Vector) -> Option<(u32, transparent::Output)> {
    if vector.input_index < 0 {
        None
    } else {
        Some((
            vector.input_index as u32,
            spent_output(vector, vector.amount, &vector.script),
        ))
    }
}

fn digest(
    tx: &Transaction,
    network_upgrade: NetworkUpgrade,
    hash_type: HashType,
    input: Option<(u32, transparent::Output)>,
) -> [u8; 32] {
    // `SigHash`'s inner array is a public field, and it is already exactly
    // 32 bytes by construction -- there is no runtime length left to assert.
    tx.sighash(network_upgrade, hash_type, input).0
}

/// The digest of a vector exactly as the ZIP specifies it.
fn vector_digest(vector: &Vector) -> [u8; 32] {
    digest(
        &parse(vector),
        vector.network_upgrade,
        hash_type_of(vector),
        signed_input(vector),
    )
}

fn serialize(tx: &Transaction) -> Vec<u8> {
    let mut out = Vec::new();
    tx.zcash_serialize(&mut out)
        .expect("writing to a Vec is infallible");
    out
}

// ------------------------------------------------------------------- tests

#[test]
fn every_zip_vector_still_reproduces_the_digest_printed_in_the_zip() {
    // The whole group rests on this one: five digests that ZIP-143 and ZIP-243
    // print in their own text, reproduced from the vectors' transaction bytes
    // through the rewritten `Transaction::sighash`. Between them the vectors
    // cover both ZIPs, all three nHashType values the ZIPs exercise (ALL = 1,
    // NONE = 2, SINGLE = 3), and both shapes of the call -- signing no
    // particular input, and signing one with its scriptCode and its value.
    let mut by_zip = BTreeMap::new();
    let mut hash_types = BTreeMap::new();
    let mut with_input = 0usize;
    let mut without_input = 0usize;

    for vector in vectors() {
        // The vector transaction must also be exactly what its bytes say it
        // is: if the parse dropped or invented a field, the digest below would
        // be over a different transaction than the ZIP hashed.
        let tx = parse(&vector);
        assert_eq!(
            serialize(&tx),
            vector.raw_tx,
            "{}: the parsed transaction does not re-serialize to the vector's bytes",
            vector.name
        );

        let got = hex::encode(&digest(
            &tx,
            vector.network_upgrade,
            hash_type_of(&vector),
            signed_input(&vector),
        )[..]);
        assert_eq!(
            got, vector.expected,
            "{}: ZIP-{} prints sighash {}, this crate computes {}",
            vector.name, vector.zip, vector.expected, got
        );

        *by_zip.entry(vector.zip).or_insert(0usize) += 1;
        *hash_types.entry(vector.hash_type_bits).or_insert(0usize) += 1;
        if vector.input_index < 0 {
            without_input += 1;
        } else {
            with_input += 1;
        }
    }

    // Guard against the corpus quietly losing its coverage.
    assert!(
        by_zip.contains_key(&143) && by_zip.contains_key(&243),
        "the corpus must exercise both ZIPs, it covers {:?}",
        by_zip.keys().collect::<Vec<_>>()
    );
    assert_eq!(
        hash_types.keys().cloned().collect::<Vec<u32>>(),
        vec![1, 2, 3],
        "the corpus should exercise nHashType ALL, NONE and SINGLE"
    );
    assert!(
        with_input >= 3 && without_input >= 2,
        "the corpus must exercise both call shapes: {} with an input, {} without",
        with_input,
        without_input
    );
}

#[test]
fn the_digest_is_thirty_two_bytes_in_the_order_the_zip_prints_them() {
    // `block::Hash` and `transaction::Hash` are displayed byte-reversed, so
    // "which end is which" is a real question for anyone wiring a signature
    // verifier up to this function. The answer is that `SigHash`'s public
    // `[u8; 32]` field holds the digest in the same order the ZIP writes it
    // down -- no reversal anywhere. Asserting that the reversed form does
    // *not* match is what makes that claim bite rather than being a comment.
    for vector in vectors() {
        let bytes = vector_digest(&vector);
        assert_eq!(bytes.len(), 32, "{}: digest length", vector.name);

        let forwards = hex::encode(&bytes[..]);
        let mut reversed_bytes = bytes;
        reversed_bytes.reverse();
        let backwards = hex::encode(&reversed_bytes[..]);

        assert_eq!(
            forwards, vector.expected,
            "{}: the digest is not in the order ZIP-{} prints it",
            vector.name, vector.zip
        );
        assert_ne!(
            backwards, vector.expected,
            "{}: this digest reads the same in both directions, so it cannot \
             witness a byte order; the fixture is unusable for this test",
            vector.name
        );
    }
}

#[test]
fn zip143_vectors_are_v3_overwinter_and_zip243_vectors_are_v4_sapling() {
    // Read straight out of the vector's first eight bytes, without the crate:
    // the header word is the overwintered flag in bit 31 and the version in the
    // low 31 bits, then the four-byte version group id. ZIP-143 is defined for
    // v3/Overwinter transactions and ZIP-243 for v4/Sapling ones, and this
    // milestone's rewrite of `sighash.rs` must not have blurred that pairing.
    const OVERWINTER_VERSION_GROUP_ID: u32 = 0x03C4_8270;
    const SAPLING_VERSION_GROUP_ID: u32 = 0x892F_2085;

    for vector in vectors() {
        assert!(
            vector.raw_tx.len() >= 8,
            "{}: the vector is too short to hold a header",
            vector.name
        );
        let mut word = [0u8; 4];
        word.copy_from_slice(&vector.raw_tx[0..4]);
        let header = u32::from_le_bytes(word);
        word.copy_from_slice(&vector.raw_tx[4..8]);
        let group_id = u32::from_le_bytes(word);

        assert_eq!(
            header & 0x8000_0000,
            0x8000_0000,
            "{}: the overwintered flag is not set in header word {:#010x}",
            vector.name,
            header
        );
        let version = header & 0x7fff_ffff;

        let (expected_version, expected_group_id, expected_upgrade) = match vector.zip {
            143 => (3u32, OVERWINTER_VERSION_GROUP_ID, NetworkUpgrade::Overwinter),
            243 => (4u32, SAPLING_VERSION_GROUP_ID, NetworkUpgrade::Sapling),
            other => panic!("{}: unexpected zip column {}", vector.name, other),
        };

        assert_eq!(
            version, expected_version,
            "{}: ZIP-{} vectors are version {} transactions, the bytes say {}",
            vector.name, vector.zip, expected_version, version
        );
        assert_eq!(
            group_id, expected_group_id,
            "{}: version group id {:#010x}, expected {:#010x}",
            vector.name, group_id, expected_group_id
        );
        assert_eq!(
            vector.network_upgrade, expected_upgrade,
            "{}: the TSV signs this vector under the wrong network upgrade",
            vector.name
        );

        // ... and the parsed transaction must agree with the wire bytes.
        let tx = parse(&vector);
        match (vector.zip, &tx) {
            (143, &Transaction::V3 { .. }) => {}
            (243, &Transaction::V4 { .. }) => {}
            (zip, other) => panic!(
                "{}: a ZIP-{} vector parsed as {}",
                vector.name,
                zip,
                match other {
                    Transaction::V1 { .. } => "V1",
                    Transaction::V2 { .. } => "V2",
                    Transaction::V3 { .. } => "V3",
                    Transaction::V4 { .. } => "V4",
                    Transaction::V5 { .. } => "V5",
                }
            ),
        }
    }
}

#[test]
fn changing_the_hash_type_still_changes_the_digest() {
    // nHashType is hashed in directly, and it also selects which of hashPrevouts,
    // hashSequence and hashOutputs are replaced by 32 zero bytes. A signature
    // made under SIGHASH_SINGLE must therefore not be replayable as SIGHASH_ALL,
    // which is only true if all four of these produce different digests -- a
    // property the rewritten `sighash` must preserve.
    //
    // No new expected digest is needed for this: the claim is inequality.
    static TYPES: &[(&str, u32)] = &[
        ("ALL", 0b0000_0001),
        ("NONE", 0b0000_0010),
        ("SINGLE", 0b0000_0011),
        ("ALL|ANYONECANPAY", 0b1000_0001),
    ];

    for vector in vectors() {
        let tx = parse(&vector);
        let mut seen: BTreeMap<String, &str> = BTreeMap::new();

        for &(label, bits) in TYPES.iter() {
            let hash_type = HashType::from_bits(bits)
                .unwrap_or_else(|| panic!("{} is not a valid HashType", label));
            assert_eq!(
                hash_type.bits(),
                bits,
                "HashType::{} does not round trip through its bits",
                label
            );

            let got = hex::encode(
                &digest(&tx, vector.network_upgrade, hash_type, signed_input(&vector))[..],
            );
            if let Some(other) = seen.insert(got.clone(), label) {
                panic!(
                    "{}: nHashType {} and {} both hash to {}",
                    vector.name, other, label, got
                );
            }
        }

        // The vector's own hash type must be one of the four, and must still
        // land on the digest the ZIP prints.
        let mine = hex::encode(&vector_digest(&vector)[..]);
        assert_eq!(
            mine, vector.expected,
            "{}: the vector's own hash type no longer reproduces the ZIP digest",
            vector.name
        );
    }
}

#[test]
fn changing_the_signed_input_index_still_changes_the_digest() {
    // Each input of a multi-input transaction is signed separately, over a
    // digest that names the outpoint, scriptCode, value and sequence of *that*
    // input. If two indices hashed the same, a signature authorizing the spend
    // of one input would authorize the spend of another.
    let mut exercised = 0usize;
    let mut zips = BTreeMap::new();

    for vector in vectors() {
        if vector.input_index < 0 {
            continue;
        }
        let tx = parse(&vector);
        let inputs = tx.inputs().len();
        if inputs < 2 {
            continue;
        }

        let signed = vector.input_index as usize;
        assert!(
            signed < inputs,
            "{}: signs input {} of {}",
            vector.name,
            signed,
            inputs
        );
        let other = if signed == 0 { 1 } else { 0 };

        let mine = hex::encode(&vector_digest(&vector)[..]);
        let theirs = hex::encode(
            &digest(
                &tx,
                vector.network_upgrade,
                hash_type_of(&vector),
                Some((
                    other as u32,
                    spent_output(&vector, vector.amount, &vector.script),
                )),
            )[..],
        );

        assert_eq!(
            mine, vector.expected,
            "{}: signing input {} no longer reproduces the ZIP digest",
            vector.name, signed
        );
        assert_ne!(
            theirs, mine,
            "{}: signing input {} and input {} give the same digest {}",
            vector.name, signed, other, mine
        );

        exercised += 1;
        *zips.entry(vector.zip).or_insert(0usize) += 1;
    }

    assert!(
        exercised >= 2 && zips.contains_key(&143) && zips.contains_key(&243),
        "this test needs a multi-input vector from each ZIP, it exercised {:?}",
        zips
    );
}

#[test]
fn changing_the_spent_amount_still_changes_the_digest() {
    // ZIP-143 is the upgrade that made the value of the output being spent part
    // of the signed message, and ZIP-243 keeps it in the same place. Without
    // this, a wallet could be shown a small value, sign it, and have the
    // signature reused against a much larger UTXO.
    let mut zips = BTreeMap::new();

    for vector in vectors() {
        if vector.input_index < 0 {
            continue;
        }
        let tx = parse(&vector);
        let index = vector.input_index as u32;

        let mine = hex::encode(&vector_digest(&vector)[..]);
        assert_eq!(
            mine, vector.expected,
            "{}: the vector's own amount no longer reproduces the ZIP digest",
            vector.name
        );

        // One zatoshi either side of the real value: the smallest change a
        // value can undergo, and still enough to change the digest.
        for &delta in [-1i64, 1].iter() {
            let tampered = vector.amount + delta;
            if tampered < 0 {
                continue;
            }
            let got = hex::encode(
                &digest(
                    &tx,
                    vector.network_upgrade,
                    hash_type_of(&vector),
                    Some((index, spent_output(&vector, tampered, &vector.script))),
                )[..],
            );
            assert_ne!(
                got, mine,
                "{}: spending {} zatoshi instead of {} gives the same digest {}",
                vector.name, tampered, vector.amount, mine
            );
        }

        // The scriptCode is bound in the same block; a one-byte change to it
        // must move the digest too.
        let mut other_script = vector.script.clone();
        other_script.push(0x51);
        let got = hex::encode(
            &digest(
                &tx,
                vector.network_upgrade,
                hash_type_of(&vector),
                Some((index, spent_output(&vector, vector.amount, &other_script))),
            )[..],
        );
        assert_ne!(
            got, mine,
            "{}: appending a byte to the scriptCode gives the same digest {}",
            vector.name, mine
        );

        *zips.entry(vector.zip).or_insert(0usize) += 1;
    }

    assert!(
        zips.contains_key(&143) && zips.contains_key(&243),
        "this test needs a vector with an input from each ZIP, it exercised {:?}",
        zips
    );
}

#[test]
fn every_pre_nu5_network_upgrade_still_gives_a_different_digest_for_the_same_transaction() {
    // The consensus branch id is folded into the digest, not just appended to
    // the hashed bytes -- that is ZIP-143's replay protection -- and it is
    // invisible to anyone who only checks the preimage. This must hold across
    // the rewrite that made NU5 a sixth branch feeding the same bridge.
    //
    // Only the v4 vectors are used. A v3 transaction has no valueBalance and no
    // shielded bundles, so ZIP-243 has nothing to hash for it and this band
    // refuses the combination outright; the reverse direction (a v4 transaction
    // under ZIP-143 rules) is well defined and is exercised below.
    let mut exercised = 0usize;

    for vector in vectors() {
        if vector.zip != 243 {
            continue;
        }
        let tx = parse(&vector);
        let hash_type = hash_type_of(&vector);
        let mut seen: BTreeMap<String, &str> = BTreeMap::new();

        for &(label, upgrade) in SIGNING_UPGRADES.iter() {
            let got = hex::encode(&digest(&tx, upgrade, hash_type, signed_input(&vector))[..]);
            if let Some(other) = seen.insert(got.clone(), label) {
                panic!(
                    "{}: {} and {} produce the same digest {}, so a signature \
                     made for one upgrade would be replayable on the other",
                    vector.name, other, label, got
                );
            }
            if label == "Sapling" {
                assert_eq!(
                    got, vector.expected,
                    "{}: under Sapling rules this must be the ZIP-243 digest",
                    vector.name
                );
            } else {
                assert_ne!(
                    got, vector.expected,
                    "{}: {} reproduces the Sapling digest",
                    vector.name, label
                );
            }
        }

        assert_eq!(
            seen.len(),
            SIGNING_UPGRADES.len(),
            "{}: {} upgrades collapsed into {} digests",
            vector.name,
            SIGNING_UPGRADES.len(),
            seen.len()
        );
        exercised += 1;
    }

    assert!(
        exercised >= 3,
        "expected the three ZIP-243 vectors, exercised {}",
        exercised
    );
}

#[test]
fn signing_a_specific_input_still_appends_a_block_that_signing_none_omits() {
    // The two shapes the ZIPs define. With no input the digest ends at
    // nHashType; with an input, the outpoint, scriptCode, amount and sequence
    // of that input follow. The two must never coincide, or a signature over
    // the transaction-wide digest would be usable as a signature over a
    // particular input.
    let mut with_input = 0usize;
    let mut without_input = 0usize;

    for vector in vectors() {
        let tx = parse(&vector);
        let mine = hex::encode(&vector_digest(&vector)[..]);
        assert_eq!(mine, vector.expected, "{}: ZIP digest", vector.name);

        if vector.input_index < 0 {
            // A vector that signs no input has no transparent inputs at all, so
            // there is no index that could be passed instead; what is checked
            // here is that the ZIP's own digest is the one produced by `None`.
            assert_eq!(
                tx.inputs().len(),
                0,
                "{}: a no-input vector should have no transparent inputs, it has {}",
                vector.name,
                tx.inputs().len()
            );
            without_input += 1;
        } else {
            let none = hex::encode(
                &digest(&tx, vector.network_upgrade, hash_type_of(&vector), None)[..],
            );
            assert_ne!(
                none, mine,
                "{}: dropping the signed input left the digest unchanged at {}",
                vector.name, mine
            );
            with_input += 1;
        }
    }

    assert!(
        with_input >= 3 && without_input >= 2,
        "the corpus should exercise both shapes: {} with an input, {} without",
        with_input,
        without_input
    );
}
