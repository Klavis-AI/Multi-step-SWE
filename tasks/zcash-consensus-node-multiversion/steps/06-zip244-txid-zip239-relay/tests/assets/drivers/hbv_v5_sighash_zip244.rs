//! ZIP-244 signature hashes for v5 transactions, against the shipped vector
//! set's own published digests.
//!
//! Harbor verifier driver -- injected into `zebra-chain/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! `zip244.tsv` (the same fixture `hbv_zip244_txid.rs` uses for the txid and
//! auth digest columns) also carries, for each of its ten v5 transactions, the
//! six signature hashes ZIP-244 defines: `SIGHASH_ALL`, `SIGHASH_NONE` and
//! `SIGHASH_SINGLE`, each with and without `SIGHASH_ANYONECANPAY`. Four of the
//! ten vectors name a transparent input to sign (columns `transparent_input`,
//! `script_code_hex`, `amount`) and carry all six; the other six sign no
//! particular input, so only `sighash_all` is populated and the rest read `-`.
//! This group is untouched territory for `hbv_zip244_txid.rs`, which only
//! checks the txid and auth digest columns.
//!
//! `Transaction::sighash(network_upgrade, hash_type, input) -> SigHash` is the
//! same function `hbv_inherited_sighash_zip143_zip243.rs` calls for the older
//! ZIPs; this milestone is what taught it to route v5 transactions to a
//! librustzcash bridge instead of leaving `verify_v5_transaction` an
//! unconditional `todo!()`. `SigHash(pub [u8; 32])`'s field is public, so
//! `hash.0` is the digest, already in the byte order the vector prints it --
//! no reversal, the same convention as ZIP-143/ZIP-243.

use std::collections::BTreeMap;
use std::convert::TryFrom;
use std::io::Cursor;
use std::path::PathBuf;

use zebra_chain::amount::{Amount, NonNegative};
use zebra_chain::parameters::NetworkUpgrade;
use zebra_chain::serialization::ZcashDeserialize;
use zebra_chain::transaction::{HashType, Transaction};
use zebra_chain::transparent;

// ---------------------------------------------------------------- fixtures

struct Vector {
    name: String,
    tx: Vec<u8>,
    txid: [u8; 32],
    /// The transparent input being signed, if any.
    transparent_input: Option<u32>,
    /// The scriptCode of the spent output, raw, with no length prefix.
    script_code: Option<Vec<u8>>,
    /// The value of the spent output, in zatoshi.
    amount: Option<i64>,
    sighash_all: [u8; 32],
    sighash_none: Option<[u8; 32]>,
    sighash_single: Option<[u8; 32]>,
    sighash_all_anyone: Option<[u8; 32]>,
    sighash_none_anyone: Option<[u8; 32]>,
    sighash_single_anyone: Option<[u8; 32]>,
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

fn hex32(name: &str, column: &str, value: &str) -> [u8; 32] {
    let bytes = hex::decode(value)
        .unwrap_or_else(|e| panic!("{}: {} is not hex: {}", name, column, e));
    assert_eq!(
        bytes.len(),
        32,
        "{}: {} is {} bytes, expected 32",
        name,
        column,
        bytes.len()
    );
    let mut out = [0u8; 32];
    out.copy_from_slice(&bytes);
    out
}

/// A column that is `-` when the vector has no transparent input to sign.
fn opt_hex32(name: &str, column: &str, value: &str) -> Option<[u8; 32]> {
    if value == "-" {
        None
    } else {
        Some(hex32(name, column, value))
    }
}

/// Load the ten ZIP-244 vectors, in the order the TSV lists them.
fn vectors() -> Vec<Vector> {
    let path = fixture_dir().join("zip244.tsv");
    let tsv = std::fs::read_to_string(&path)
        .unwrap_or_else(|e| panic!("cannot read {}: {}", path.display(), e));
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
        assert_eq!(
            cells.len(),
            header.len(),
            "malformed zip244.tsv row: {} cells, header has {}",
            cells.len(),
            header.len()
        );
        let row: BTreeMap<&str, &str> = header.iter().cloned().zip(cells).collect();

        let name = cell(&row, "name").to_string();
        let transparent_input = match cell(&row, "transparent_input") {
            "-" => None,
            other => Some(
                other
                    .parse::<u32>()
                    .unwrap_or_else(|e| panic!("{}: transparent_input column: {}", name, e)),
            ),
        };
        let script_code = match cell(&row, "script_code_hex") {
            "-" => None,
            other => Some(
                hex::decode(other)
                    .unwrap_or_else(|e| panic!("{}: script_code_hex is not hex: {}", name, e)),
            ),
        };
        let amount = match cell(&row, "amount") {
            "-" => None,
            other => Some(
                other
                    .parse::<i64>()
                    .unwrap_or_else(|e| panic!("{}: amount column: {}", name, e)),
            ),
        };

        out.push(Vector {
            tx: hex::decode(cell(&row, "tx_hex"))
                .unwrap_or_else(|e| panic!("{}: tx_hex is not hex: {}", name, e)),
            txid: hex32(&name, "txid_hex", cell(&row, "txid_hex")),
            transparent_input,
            script_code,
            amount,
            sighash_all: hex32(&name, "sighash_all", cell(&row, "sighash_all")),
            sighash_none: opt_hex32(&name, "sighash_none", cell(&row, "sighash_none")),
            sighash_single: opt_hex32(&name, "sighash_single", cell(&row, "sighash_single")),
            sighash_all_anyone: opt_hex32(
                &name,
                "sighash_all_anyone",
                cell(&row, "sighash_all_anyone"),
            ),
            sighash_none_anyone: opt_hex32(
                &name,
                "sighash_none_anyone",
                cell(&row, "sighash_none_anyone"),
            ),
            sighash_single_anyone: opt_hex32(
                &name,
                "sighash_single_anyone",
                cell(&row, "sighash_single_anyone"),
            ),
            name,
        });
    }

    assert_eq!(
        out.len(),
        10,
        "zip244.tsv should ship ten vectors, found {}",
        out.len()
    );
    out
}

// ----------------------------------------------------------------- helpers

fn parse(vector: &Vector) -> Transaction {
    Transaction::zcash_deserialize(Cursor::new(&vector.tx[..]))
        .unwrap_or_else(|e| panic!("{}: the vector transaction did not parse: {:?}", vector.name, e))
}

/// The `input` argument for a vector that names a transparent input, built
/// from its own `script_code`/`amount` columns.
fn signed_input(vector: &Vector) -> (u32, transparent::Output) {
    let index = vector
        .transparent_input
        .unwrap_or_else(|| panic!("{}: this vector has no transparent input", vector.name));
    let script = vector
        .script_code
        .as_ref()
        .unwrap_or_else(|| panic!("{}: this vector has no script_code", vector.name));
    let amount = vector
        .amount
        .unwrap_or_else(|| panic!("{}: this vector has no amount", vector.name));
    (
        index,
        transparent::Output {
            value: Amount::<NonNegative>::try_from(amount).unwrap_or_else(|e| {
                panic!(
                    "{}: {} zatoshi is not a valid non-negative amount: {:?}",
                    vector.name, amount, e
                )
            }),
            lock_script: transparent::Script::new(script),
        },
    )
}

fn digest(
    tx: &Transaction,
    hash_type: HashType,
    input: Option<(u32, transparent::Output)>,
) -> [u8; 32] {
    tx.sighash(NetworkUpgrade::Nu5, hash_type, input).0
}

// ------------------------------------------------------------------- tests

#[test]
fn the_corpus_still_ships_ten_vectors_split_between_input_and_no_input_shapes() {
    // Guard against the shared fixture quietly losing the columns this group
    // depends on: `hbv_zip244_txid.rs` parses all six `sighash_*` columns into
    // its `Vector` but asserts on none of them, so a change that dropped the
    // sighash columns would not fail there.
    let vectors = vectors();
    let with_input = vectors
        .iter()
        .filter(|v| v.transparent_input.is_some())
        .count();
    let without_input = vectors.len() - with_input;
    assert!(
        with_input >= 3,
        "expected several vectors naming a transparent input, found {}",
        with_input
    );
    assert!(
        without_input >= 3,
        "expected several vectors with no transparent input, found {}",
        without_input
    );

    for vector in &vectors {
        let has_optional_columns = vector.sighash_none.is_some()
            || vector.sighash_single.is_some()
            || vector.sighash_all_anyone.is_some()
            || vector.sighash_none_anyone.is_some()
            || vector.sighash_single_anyone.is_some();
        assert_eq!(
            vector.transparent_input.is_some(),
            has_optional_columns,
            "{}: the five optional sighash columns must all be present together, \
             exactly when the vector names a transparent input",
            vector.name
        );
    }
}

#[test]
fn every_transparent_input_vector_reproduces_all_six_zip244_sighash_variants() {
    // The one property this whole group exists to check: for a v5 transaction
    // that names a transparent input, `Transaction::sighash` must reproduce
    // all six digests the vector publishes, under `NetworkUpgrade::Nu5`.
    let mut exercised = 0usize;

    for vector in vectors() {
        if vector.transparent_input.is_none() {
            continue;
        }
        let tx = parse(&vector);
        let input = signed_input(&vector);

        let cases: &[(&str, HashType, [u8; 32])] = &[
            ("ALL", HashType::ALL, vector.sighash_all),
            ("NONE", HashType::NONE, vector.sighash_none.unwrap()),
            ("SINGLE", HashType::SINGLE, vector.sighash_single.unwrap()),
            (
                "ALL|ANYONECANPAY",
                HashType::ALL | HashType::ANYONECANPAY,
                vector.sighash_all_anyone.unwrap(),
            ),
            (
                "NONE|ANYONECANPAY",
                HashType::NONE | HashType::ANYONECANPAY,
                vector.sighash_none_anyone.unwrap(),
            ),
            (
                "SINGLE|ANYONECANPAY",
                HashType::SINGLE | HashType::ANYONECANPAY,
                vector.sighash_single_anyone.unwrap(),
            ),
        ];

        for (label, hash_type, expected) in cases {
            let got = digest(&tx, *hash_type, Some(input.clone()));
            assert_eq!(
                hex::encode(&got[..]),
                hex::encode(&expected[..]),
                "{}: nHashType {} does not reproduce the vector's published sighash",
                vector.name,
                label
            );
        }

        exercised += 1;
    }

    assert!(
        exercised >= 3,
        "expected several transparent-input vectors to be exercised, got {}",
        exercised
    );
}

#[test]
fn every_no_input_vector_produces_a_sighash_all_that_matches_the_txid() {
    // A v5 transaction with no transparent input still has a `SIGHASH_ALL`
    // digest -- ZIP-244 defines it as the transaction digest itself, with no
    // personalisation beyond the hash type, so it comes out equal to the
    // transaction's own txid. Both the vector's `sighash_all` column and the
    // crate's own `Hash::from(&Transaction)` must agree with what `sighash`
    // computes here.
    let mut exercised = 0usize;

    for vector in vectors() {
        if vector.transparent_input.is_some() {
            continue;
        }
        let tx = parse(&vector);
        let got = digest(&tx, HashType::ALL, None);

        assert_eq!(
            hex::encode(&got[..]),
            hex::encode(&vector.sighash_all[..]),
            "{}: SIGHASH_ALL with no input does not match the vector",
            vector.name
        );
        assert_eq!(
            hex::encode(&got[..]),
            hex::encode(&vector.txid[..]),
            "{}: this vector's SIGHASH_ALL digest should equal its txid",
            vector.name
        );

        exercised += 1;
    }

    assert!(
        exercised >= 3,
        "expected several no-input vectors to be exercised, got {}",
        exercised
    );
}

#[test]
fn flipping_anyonecanpay_changes_the_digest_for_every_signed_input_vector() {
    // SIGHASH_ANYONECANPAY replaces the commitment to every transparent input
    // with a commitment to only the one being signed, so it must move the
    // digest for a transaction that has more than the one input it names --
    // and even when it does not, the hash type byte itself is part of what is
    // signed, so the three pairs below must differ regardless.
    //
    // The digests compared here are *recomputed* through `Transaction::sighash`
    // rather than read out of the fixture's columns, so this case exercises the
    // implementation; the columns are checked against it by the exhaustive case
    // above.
    let mut exercised = 0usize;

    for vector in vectors() {
        if vector.transparent_input.is_none() {
            continue;
        }
        let tx = parse(&vector);
        let signed = |hash_type| hex::encode(&digest(&tx, hash_type, Some(signed_input(&vector)))[..]);

        let all = signed(HashType::ALL);
        let all_anyone = signed(HashType::ALL | HashType::ANYONECANPAY);
        assert_ne!(
            all, all_anyone,
            "{}: SIGHASH_ALL and SIGHASH_ALL|ANYONECANPAY collapse to the same digest",
            vector.name
        );

        let none = signed(HashType::NONE);
        let none_anyone = signed(HashType::NONE | HashType::ANYONECANPAY);
        assert_ne!(
            none, none_anyone,
            "{}: SIGHASH_NONE and SIGHASH_NONE|ANYONECANPAY collapse to the same digest",
            vector.name
        );

        let single = signed(HashType::SINGLE);
        let single_anyone = signed(HashType::SINGLE | HashType::ANYONECANPAY);
        assert_ne!(
            single, single_anyone,
            "{}: SIGHASH_SINGLE and SIGHASH_SINGLE|ANYONECANPAY collapse to the same digest",
            vector.name
        );

        exercised += 1;
    }

    assert!(exercised >= 3, "exercised only {} vectors", exercised);
}

#[test]
fn the_all_hash_type_digest_differs_from_the_none_hash_type_digest() {
    // SIGHASH_ALL commits to every output; SIGHASH_NONE commits to none of
    // them. A transaction with at least one output must therefore sign a
    // different digest under the two, or SIGHASH_NONE could be replayed as
    // authorization for SIGHASH_ALL. Both digests are recomputed through
    // `Transaction::sighash`, not read from the fixture's columns.
    let mut exercised = 0usize;

    for vector in vectors() {
        if vector.transparent_input.is_none() {
            continue;
        }
        let tx = parse(&vector);
        let all = hex::encode(&digest(&tx, HashType::ALL, Some(signed_input(&vector)))[..]);
        let none = hex::encode(&digest(&tx, HashType::NONE, Some(signed_input(&vector)))[..]);
        assert_ne!(
            all, none,
            "{}: SIGHASH_ALL and SIGHASH_NONE collapse to the same digest",
            vector.name
        );
        exercised += 1;
    }

    assert!(exercised >= 3, "exercised only {} vectors", exercised);
}

#[test]
fn tampering_the_spent_amount_by_one_zatoshi_changes_the_signed_input_digest() {
    // The value of the output being spent is part of what a v5 transaction
    // signs, the same as it was for ZIP-143/ZIP-243. A wallet that only
    // checked the transaction bytes, and not the amount it was told it was
    // spending, could be tricked into authorizing a much larger spend.
    let mut exercised = 0usize;

    for vector in vectors() {
        if vector.transparent_input.is_none() {
            continue;
        }
        let tx = parse(&vector);
        let (index, output) = signed_input(&vector);
        let mine = digest(&tx, HashType::ALL, Some((index, output.clone())));
        assert_eq!(
            hex::encode(&mine[..]),
            hex::encode(&vector.sighash_all[..]),
            "{}: the vector's own amount no longer reproduces the published digest",
            vector.name
        );

        let tampered_amount = vector.amount.unwrap() + 1;
        let tampered_output = transparent::Output {
            value: Amount::<NonNegative>::try_from(tampered_amount).unwrap_or_else(|e| {
                panic!(
                    "{}: {} zatoshi is not a valid non-negative amount: {:?}",
                    vector.name, tampered_amount, e
                )
            }),
            lock_script: output.lock_script,
        };
        let tampered = digest(&tx, HashType::ALL, Some((index, tampered_output)));
        assert_ne!(
            hex::encode(&tampered[..]),
            hex::encode(&mine[..]),
            "{}: spending one zatoshi more gives the same digest {}",
            vector.name,
            hex::encode(&mine[..])
        );

        exercised += 1;
    }

    assert!(exercised >= 3, "exercised only {} vectors", exercised);
}
