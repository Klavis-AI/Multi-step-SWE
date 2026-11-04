//! `scriptCode` in the signature hash preamble, after the wire-layout fix.
//!
//! Harbor verifier driver -- injected into `zebra-chain/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! ZIP-143's "Signature Digest" section signs `scriptCode`, which is the
//! *serialized* spent script -- the compactSize length prefix included. The
//! previous milestone's `SigHasher::hash_input_script_code` wrote the spent
//! output's inner bytes straight into the hasher and relied on the *caller*
//! to have baked the length prefix into the `Script` newtype by hand, because
//! `Script`'s inner `Vec<u8>` field was public (`pub struct Script(pub
//! Vec<u8>)`) and there was no other way to build one.
//!
//! This milestone corrects `hash_input_script_code` to call
//! `Script::zcash_serialize`, which writes the length prefix itself. To make
//! that fix hold, `Script`'s field became private and two accessors took its
//! place: `Script::new(raw_bytes: &[u8])`, which stores `raw_bytes` verbatim
//! and does **not** expect a prefix, and `Script::as_raw_bytes(&self) ->
//! &[u8]`, which hands the unprefixed bytes back. A caller now builds the
//! spent output's lock script from the *raw* scriptPubKey and lets
//! `sighash` add the one prefix the ZIP requires -- doing it by hand, the
//! way the previous milestone needed, would now add a *second* prefix and
//! change the digest.
//!
//! `$HBV_FIXTURES/sighash.tsv` is the same five ZIP-143/ZIP-243 vectors used
//! at step 1; `script_hex` there is documented as "the raw script, without
//! the compactSize prefix that scriptCode carries" -- exactly what
//! `Script::new` now wants directly.

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

struct Vector {
    name: String,
    zip: u32,
    network_upgrade: NetworkUpgrade,
    hash_type_bits: u32,
    input_index: i64,
    /// The scriptCode of the spent output, without its length prefix -- the
    /// exact bytes `Script::new` now expects.
    script: Vec<u8>,
    amount: i64,
    raw_tx: Vec<u8>,
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

/// Build the spent output straight from the vector's *raw*, unprefixed
/// script bytes via `Script::new` -- the fixed API -- with no manual
/// compactSize prefix added by this driver. `Script::zcash_serialize` (called
/// inside `sighash`) is solely responsible for the one prefix scriptCode
/// carries.
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
    let hash = tx.sighash(network_upgrade, hash_type, input);
    let bytes = hash.as_bytes();
    assert_eq!(
        bytes.len(),
        32,
        "a signature hash must be 32 bytes, this one is {}",
        bytes.len()
    );
    let mut out = [0u8; 32];
    out.copy_from_slice(bytes);
    out
}

fn vector_digest(vector: &Vector) -> [u8; 32] {
    digest(
        &parse(vector),
        vector.network_upgrade,
        hash_type_of(vector),
        signed_input(vector),
    )
}

// ------------------------------------------------------------------- tests

#[test]
fn script_new_stores_raw_bytes_and_serialization_adds_exactly_one_length_prefix() {
    // The API-shape claim this whole group rests on: `Script::new` takes
    // bytes with *no* prefix and hands them back unchanged from
    // `as_raw_bytes`, while `zcash_serialize` is the only thing that adds a
    // compactSize prefix, and it adds exactly one.
    for vector in vectors() {
        if vector.script.is_empty() {
            continue;
        }
        let script = transparent::Script::new(&vector.script);
        assert_eq!(
            script.as_raw_bytes(),
            &vector.script[..],
            "{}: Script::new(...).as_raw_bytes() != the bytes passed in",
            vector.name
        );

        let mut serialized = Vec::new();
        script
            .zcash_serialize(&mut serialized)
            .expect("writing to a Vec is infallible");

        // A compactSize prefix for a length this small (every fixture script
        // here is under 0xfd bytes) is exactly one byte, equal to the length.
        assert!(
            vector.script.len() < 0xfd,
            "{}: fixture script is long enough to need a multi-byte compactSize; \
             this test assumes a one-byte prefix",
            vector.name
        );
        assert_eq!(
            serialized.len(),
            vector.script.len() + 1,
            "{}: serialized script should be exactly one prefix byte longer than the raw bytes",
            vector.name
        );
        assert_eq!(
            serialized[0], vector.script.len() as u8,
            "{}: the prefix byte does not equal the raw script's length",
            vector.name
        );
        assert_eq!(
            &serialized[1..],
            &vector.script[..],
            "{}: the bytes after the prefix differ from the raw script",
            vector.name
        );
    }
}

#[test]
fn every_zip_vector_reproduces_the_digest_printed_in_the_zip_via_script_new() {
    // The five ZIP-143/ZIP-243 worked examples, computed by building the
    // spent output through `Script::new` (the fixed API) rather than by
    // hand-assembling a prefixed byte string. Getting the prefix wrong here
    // -- adding one, or adding it twice -- changes every one of these five
    // digests, because scriptCode is hashed directly into the preimage.
    let mut by_zip = BTreeMap::new();
    let mut with_input = 0usize;
    let mut without_input = 0usize;

    for vector in vectors() {
        let tx = parse(&vector);
        let got = hex::encode(&vector_digest(&vector)[..]);
        assert_eq!(
            got, vector.expected,
            "{}: ZIP-{} prints sighash {}, this crate computes {}",
            vector.name, vector.zip, vector.expected, got
        );
        assert_eq!(
            tx.sighash(
                vector.network_upgrade,
                hash_type_of(&vector),
                signed_input(&vector)
            )
            .as_bytes()
            .len(),
            32
        );

        *by_zip.entry(vector.zip).or_insert(0usize) += 1;
        if vector.input_index < 0 {
            without_input += 1;
        } else {
            with_input += 1;
        }
    }

    assert!(
        by_zip.contains_key(&143) && by_zip.contains_key(&243),
        "the corpus must exercise both ZIPs, it covers {:?}",
        by_zip.keys().collect::<Vec<_>>()
    );
    assert!(
        with_input >= 3 && without_input >= 2,
        "the corpus must exercise both call shapes: {} with an input, {} without",
        with_input,
        without_input
    );
}

#[test]
fn a_manually_double_prefixed_script_gives_a_different_digest_than_script_new() {
    // The regression this milestone fixes, made concrete: if a caller still
    // pre-pends a compactSize prefix by hand before calling `Script::new`
    // (the previous milestone's required pattern, now wrong), `sighash` adds
    // a second one via `zcash_serialize`, and the digest no longer matches
    // the ZIP.
    let mut exercised = 0usize;
    for vector in vectors() {
        if vector.input_index < 0 || vector.script.is_empty() {
            continue;
        }
        let tx = parse(&vector);

        let correct = hex::encode(&vector_digest(&vector)[..]);
        assert_eq!(correct, vector.expected, "{}: ZIP digest", vector.name);

        // Hand-build a once-prefixed byte string the way step 1's band
        // required, then feed *that* to `Script::new` as if it were still
        // the raw script -- the mistake this milestone's API is designed to
        // make impossible to do silently.
        let mut double_prefixed = vec![vector.script.len() as u8];
        double_prefixed.extend_from_slice(&vector.script);

        let index = vector.input_index as u32;
        let wrong_output = transparent::Output {
            value: Amount::<NonNegative>::try_from(vector.amount).expect("valid amount"),
            lock_script: transparent::Script::new(&double_prefixed),
        };
        let wrong = hex::encode(
            &digest(
                &tx,
                vector.network_upgrade,
                hash_type_of(&vector),
                Some((index, wrong_output)),
            )[..],
        );

        assert_ne!(
            wrong, correct,
            "{}: a manually pre-prefixed script must not reproduce the ZIP digest, \
             but it matched {}",
            vector.name, correct
        );

        exercised += 1;
    }

    assert!(
        exercised >= 2,
        "expected at least two vectors with an input and a non-empty script, exercised {}",
        exercised
    );
}

#[test]
fn changing_the_hash_type_changes_the_digest() {
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

        let mine = hex::encode(&vector_digest(&vector)[..]);
        assert_eq!(
            mine, vector.expected,
            "{}: the vector's own hash type no longer reproduces the ZIP digest",
            vector.name
        );
    }
}

#[test]
fn changing_the_spent_scriptcode_changes_the_digest() {
    // scriptCode is part of the hashed preimage; a one-byte change to the
    // raw script passed into `Script::new` must move the digest.
    let mut exercised = 0usize;

    for vector in vectors() {
        if vector.input_index < 0 {
            continue;
        }
        let tx = parse(&vector);
        let index = vector.input_index as u32;

        let mine = hex::encode(&vector_digest(&vector)[..]);
        assert_eq!(
            mine, vector.expected,
            "{}: the vector's own scriptCode no longer reproduces the ZIP digest",
            vector.name
        );

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
        exercised += 1;
    }

    assert!(
        exercised >= 3,
        "expected at least three vectors with an input, exercised {}",
        exercised
    );
}

#[test]
fn signing_a_specific_input_appends_a_block_that_signing_none_omits() {
    let mut with_input = 0usize;
    let mut without_input = 0usize;

    for vector in vectors() {
        let tx = parse(&vector);
        let mine = hex::encode(&vector_digest(&vector)[..]);
        assert_eq!(mine, vector.expected, "{}: ZIP digest", vector.name);

        if vector.input_index < 0 {
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
