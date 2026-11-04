//! Signature hashes, against the ZIPs' own worked examples.
//!
//! Harbor verifier driver -- injected into `zebra-chain/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! The signature hash is the one value in the protocol that a node cannot get
//! approximately right. It is the message every transparent signature, every
//! JoinSplit signature and every Sapling spend authorization was made over, so
//! a digest that differs from the rest of the network's by a single bit turns
//! every valid signature on the chain into an invalid one -- the node forks off
//! at the first transaction it tries to verify.
//!
//! ZIP-143 replaced Bitcoin's quadratic `SignatureHash` with a fixed sequence
//! of BLAKE2b-256 sub-hashes for Overwinter; ZIP-243 extended it for Sapling
//! with `hashShieldedSpends`, `hashShieldedOutputs` and `valueBalance`. Both
//! ZIPs publish worked examples in their normative text, and
//! `$HBV_FIXTURES/sighash.tsv` carries five of them verbatim: two from ZIP-143
//! and three from ZIP-243. Nothing in this file computes an expected digest;
//! every one of the five comes from the ZIP.
//!
//! Two details of this band's API that a reader will otherwise trip over:
//!
//!   * `Transaction::sighash` returns the BLAKE2b state's own output, so
//!     `hex::encode(hash.as_bytes())` is the digest in exactly the order the
//!     ZIP prints it. It is *not* reversed the way `block::Hash` and
//!     `transaction::Hash` are when they are displayed, and
//!     `the_digest_is_thirty_two_bytes_in_the_order_the_zip_prints_them`
//!     pins that down rather than leaving it to a comment.
//!
//!   * ZIP-143 signs `scriptCode`, which is the *serialized* script -- the
//!     compactSize length prefix included. This band writes the spent output's
//!     `lock_script.0` into the hasher verbatim, without adding a prefix of its
//!     own, so the caller has to put the prefix inside the `Script` newtype.
//!     That is the opposite of what `Script`'s own `ZcashDeserialize` produces,
//!     so a `Script` read off the wire cannot be handed to `sighash` unchanged.
//!     `scriptcode` below does the prefixing, and says so where it does it.

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
// without the compactSize prefix that `scriptCode` carries.

struct Vector {
    name: String,
    /// 143 or 243.
    zip: u32,
    network_upgrade: NetworkUpgrade,
    /// The raw `nHashType` byte from the ZIP, before it becomes a `HashType`.
    hash_type_bits: u32,
    /// The index of the input being signed, or -1 for "no input".
    input_index: i64,
    /// The scriptCode of the spent output, without its length prefix.
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

/// Every network upgrade that has a consensus branch id, and so a signature
/// hash. Restated here from ZIP-200's deployment table rather than read out of
/// the crate.
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

/// The `scriptCode` that ZIP-143 section "Signature Digest" signs: the spent
/// output's script *as serialized*, i.e. with its compactSize length prefix.
///
/// This band's `SigHasher::hash_input_script_code` writes `lock_script.0`
/// straight into the hasher and adds no prefix, so the prefix has to be inside
/// the newtype -- even though `Script::zcash_serialize` would add one and
/// `Script::zcash_deserialize` strips one. The prefix is built here by hand so
/// that the vector's scriptCode is not assembled with the crate's own
/// compactSize writer.
fn scriptcode(raw_script: &[u8]) -> transparent::Script {
    let len = raw_script.len() as u64;
    let mut bytes = Vec::with_capacity(raw_script.len() + 9);
    if len <= 0xfc {
        bytes.push(len as u8);
    } else if len <= 0xffff {
        bytes.push(0xfd);
        bytes.extend_from_slice(&(len as u16).to_le_bytes());
    } else if len <= 0xffff_ffff {
        bytes.push(0xfe);
        bytes.extend_from_slice(&(len as u32).to_le_bytes());
    } else {
        bytes.push(0xff);
        bytes.extend_from_slice(&len.to_le_bytes());
    }
    bytes.extend_from_slice(raw_script);
    transparent::Script(bytes)
}

fn spent_output(vector: &Vector, amount: i64, script: &[u8]) -> transparent::Output {
    transparent::Output {
        value: Amount::<NonNegative>::try_from(amount).unwrap_or_else(|e| {
            panic!(
                "{}: {} zatoshi is not a valid non-negative amount: {:?}",
                vector.name, amount, e
            )
        }),
        lock_script: scriptcode(script),
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
fn every_zip_vector_reproduces_the_digest_printed_in_the_zip() {
    // The whole group rests on this one: five digests that ZIP-143 and ZIP-243
    // print in their own text, reproduced from the vectors' transaction bytes.
    // Between them they cover both ZIPs, all three nHashType values the ZIPs
    // exercise (ALL = 1, NONE = 2, SINGLE = 3), and both shapes of the call --
    // signing no particular input, and signing one with its scriptCode and its
    // value.
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
    // verifier up to this function. The answer is that `sighash` hands back the
    // BLAKE2b output itself, in the same order the ZIP writes it down -- no
    // reversal anywhere. Asserting that the reversed form does *not* match is
    // what makes that claim bite rather than being a comment.
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
    // v3/Overwinter transactions and ZIP-243 for v4/Sapling ones, and getting
    // this pairing wrong is how a node ends up hashing a Sapling transaction
    // with Overwinter's rules.
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
                }
            ),
        }
    }
}

#[test]
fn changing_the_hash_type_changes_the_digest() {
    // nHashType is hashed in directly, and it also selects which of hashPrevouts,
    // hashSequence and hashOutputs are replaced by 32 zero bytes. A signature
    // made under SIGHASH_SINGLE must therefore not be replayable as SIGHASH_ALL,
    // which is only true if all four of these produce different digests.
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
fn changing_the_signed_input_index_changes_the_digest() {
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
fn changing_the_spent_amount_changes_the_digest() {
    // ZIP-143 is the upgrade that made the value of the output being spent part
    // of the signed message -- it is the `amount` field appended to the digest
    // preimage alongside the scriptCode, not a `hashAmounts`-style sub-hash, and
    // ZIP-243 keeps it in the same place. So the binding holds for *both* ZIPs
    // here, via the trailing per-input block rather than via any whole-transaction
    // sub-hash, and this test asserts it for both rather than for Sapling alone.
    //
    // Without this, a wallet could be shown a small value, sign it, and have the
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
fn every_network_upgrade_gives_a_different_digest_for_the_same_transaction() {
    // The consensus branch id goes into the BLAKE2b personalization, not into
    // the hashed bytes: `ZcashSigHash` followed by the branch id as a
    // little-endian u32. That is ZIP-143's replay protection -- the same
    // transaction signed for one upgrade cannot be replayed on the chain of
    // another -- and it is invisible to anyone who only checks the preimage.
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
fn signing_a_specific_input_appends_a_block_that_signing_none_omits() {
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
