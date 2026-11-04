//! Inherited milestones from steps 04-11 -- regression guard, gate group.
//!
//! Harbor verifier driver -- injected into `zebra-chain/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! **Nothing in this file is new at step 12.** Every symbol it touches existed
//! at app-11, and a tree that stopped at step 11 passes all twenty-two tests
//! unchanged -- that is the point. `inherited-block-codec` guards the step-01
//! block codec; this group guards the later milestones that `cargo check`
//! alone cannot distinguish from a gutted implementation, because each is
//! arithmetic or a digest rather than a type:
//!
//!   * **ZIP-401 mempool cost and eviction weight** (steps 10-11). Arithmetic
//!     on `VerifiedUnminedTx`, so a stub that returns `0` compiles and this
//!     group catches it.
//!   * **Orchard action size constants and the `Flags` wire format** (step 04,
//!     ZIP-225). Values and a rejection path, not just names.
//!   * **ZIP-244 v5 signature hashes** (step 06), against two of the published
//!     test vectors -- one that signs a transparent input and publishes all six
//!     digests, one that signs none and publishes only `SIGHASH_ALL`.
//!   * **ZIP-209 transaction value pools** (step 08). `ValueBalance` summing
//!     and the non-negative remaining-value rule.
//!
//! All of it is gate material rather than scored material: it earns nothing and
//! exists only to veto a step-12 submission that broke earlier work. The
//! constants and the `Flags` serde in particular were previously asserted
//! inside step 12's `orchard-flags-limits` scored group, where inherited
//! regression was collecting a share of a positive weight; that group is gone
//! and this is where its inherited half landed.
//!
//! ZIP-401 defines two quantities used to govern weighted random eviction from
//! the mempool when it is full:
//!
//!   cost(tx)           = max(serialized_size_bytes, 4000)
//!   eviction_weight(tx)= cost(tx) + low_fee_penalty
//!                        where low_fee_penalty = 16000 if fee < 1000 zatoshis
//!                                                     else 0
//!
//! Both methods live on `VerifiedUnminedTx` in `zebra_chain::transaction`.
//! `cost` takes `&self`; `eviction_weight` consumes `self`.
//!
//! NOTE: The shipped spec file `zip-0401.rst` states cost_threshold = 10000
//! and low_fee_penalty = 40000. The code at this band implements 4000 and
//! 16000 respectively (with a 1000-zatoshi conventional fee). The CODE is
//! normative here; the spec was updated in a later release.

use std::collections::BTreeMap;
use std::convert::TryFrom;
use std::io::Cursor;
use std::path::PathBuf;

use zebra_chain::amount::{self, Amount, Constraint, NegativeAllowed, NonNegative, MAX_MONEY};
use zebra_chain::orchard::shielded_data::{
    ACTION_SIZE, AUTHORIZED_ACTION_SIZE, Flags, SPEND_AUTH_SIG_SIZE,
};
use zebra_chain::parameters::NetworkUpgrade;
use zebra_chain::serialization::{SerializationError, ZcashDeserialize, ZcashSerialize};
use zebra_chain::transaction::{HashType, LockTime, Transaction, UnminedTx, VerifiedUnminedTx};
use zebra_chain::transparent;
use zebra_chain::value_balance::ValueBalance;
use zebra_chain::block;

// ----------------------------------------------------------------- helpers

/// Build a minimal v4 transaction with no inputs, no outputs, no shielded data.
/// This is structurally valid; the serialized size will be small and well-defined.
fn minimal_v4_tx() -> Transaction {
    Transaction::V4 {
        inputs: vec![],
        outputs: vec![],
        lock_time: LockTime::min_lock_time(),
        expiry_height: block::Height(0),
        joinsplit_data: None,
        sapling_shielded_data: None,
    }
}

/// Build a minimal v5 transaction.
fn minimal_v5_tx() -> Transaction {
    Transaction::V5 {
        network_upgrade: NetworkUpgrade::Nu5,
        lock_time: LockTime::min_lock_time(),
        expiry_height: block::Height(0),
        inputs: vec![],
        outputs: vec![],
        sapling_shielded_data: None,
        orchard_shielded_data: None,
    }
}

/// Compute the serialized size of a transaction independently of the interface.
fn serialized_size(tx: &Transaction) -> u64 {
    let mut buf = Vec::new();
    tx.zcash_serialize(&mut buf)
        .expect("in-memory serialization never fails");
    buf.len() as u64
}

/// Construct a `VerifiedUnminedTx` from a transaction and a fee in zatoshis.
fn verified(tx: Transaction, fee_zatoshis: u64) -> VerifiedUnminedTx {
    let fee = Amount::<NonNegative>::try_from(fee_zatoshis)
        .expect("fee fits in Amount<NonNegative>");
    let unmined: UnminedTx = tx.into();
    VerifiedUnminedTx::new(unmined, fee)
}

// The cost threshold constant as used in this step's code.
const COST_THRESHOLD: u64 = 4000;
// The conventional fee (zatoshis) below which the low-fee penalty applies.
const CONVENTIONAL_FEE: u64 = 1000;
// The low-fee penalty added when fee < conventional_fee.
const LOW_FEE_PENALTY: u64 = 16_000;

// ------------------------------------------------------------------- tests

#[test]
fn cost_of_small_transaction_is_the_threshold() {
    // A minimal v4 transaction serializes to well under 4000 bytes.
    // cost() must return max(serialized_size, 4000) = 4000.
    let tx = minimal_v4_tx();
    let sz = serialized_size(&tx);
    assert!(
        sz < COST_THRESHOLD,
        "test assumption failed: minimal v4 tx serialized to {} bytes, expected < {}",
        sz,
        COST_THRESHOLD
    );
    let vtx = verified(tx, 5000);
    assert_eq!(
        vtx.cost(),
        COST_THRESHOLD,
        "cost of a small tx must equal the 4000-byte threshold, not its actual {} bytes",
        sz
    );
}

#[test]
fn cost_of_large_transaction_is_its_size() {
    // Build a transaction whose serialized size exceeds the threshold by
    // padding with many outputs. Each output serializes as 8 (value) + 1 (script
    // compactSize) + script_len bytes. With a 50-byte script body that is 59
    // bytes per output. 100 outputs = ~5900 bytes, safely above 4000.
    use zebra_chain::transparent;
    use zebra_chain::amount::Amount;

    // 50-byte dummy script so each output is at least 58 bytes.
    let script_body = [0x51u8; 50];
    let mut outputs = Vec::new();
    for _ in 0..150 {
        outputs.push(transparent::Output {
            value: Amount::zero(),
            lock_script: transparent::Script::new(&script_body[..]),
        });
    }
    let tx = Transaction::V4 {
        inputs: vec![],
        outputs,
        lock_time: LockTime::min_lock_time(),
        expiry_height: block::Height(0),
        joinsplit_data: None,
        sapling_shielded_data: None,
    };
    let sz = serialized_size(&tx);
    assert!(
        sz > COST_THRESHOLD,
        "test assumption failed: padded v4 tx serialized to {} bytes, expected > {}",
        sz,
        COST_THRESHOLD
    );
    let vtx = verified(tx, 5000);
    assert_eq!(
        vtx.cost(),
        sz,
        "cost of a large tx must equal its serialized size {}, got different value",
        sz
    );
}

#[test]
fn cost_equals_max_of_size_and_threshold() {
    // Invariant: cost() == max(size, 4000) for any transaction.
    let tx_small = minimal_v4_tx();
    let sz_small = serialized_size(&tx_small);
    let vtx_small = verified(tx_small, 0);
    assert_eq!(
        vtx_small.cost(),
        std::cmp::max(sz_small, COST_THRESHOLD),
        "cost must equal max(size={}, 4000)", sz_small
    );

    let tx_v5 = minimal_v5_tx();
    let sz_v5 = serialized_size(&tx_v5);
    let vtx_v5 = verified(tx_v5, 0);
    assert_eq!(
        vtx_v5.cost(),
        std::cmp::max(sz_v5, COST_THRESHOLD),
        "cost must equal max(size={}, 4000) for v5 tx", sz_v5
    );
}

#[test]
fn eviction_weight_with_zero_fee_adds_penalty() {
    // fee = 0 < 1000 (conventional_fee): low_fee_penalty = 16000 applies.
    let tx = minimal_v4_tx();
    let expected_cost = COST_THRESHOLD; // small tx
    let vtx = verified(tx, 0);
    let expected_weight = expected_cost + LOW_FEE_PENALTY;
    assert_eq!(
        vtx.eviction_weight(),
        expected_weight,
        "eviction_weight with zero fee must be cost + 16000 = {}",
        expected_weight
    );
}

#[test]
fn eviction_weight_with_fee_below_conventional_adds_penalty() {
    // fee = 999 < 1000: penalty applies.
    let tx = minimal_v4_tx();
    let vtx = verified(tx, 999);
    let expected = COST_THRESHOLD + LOW_FEE_PENALTY;
    assert_eq!(
        vtx.eviction_weight(),
        expected,
        "eviction_weight with fee=999 must add low-fee penalty; expected {}", expected
    );
}

#[test]
fn eviction_weight_at_conventional_fee_has_no_penalty() {
    // fee = 1000 == conventional_fee: no penalty.
    let tx = minimal_v4_tx();
    let vtx = verified(tx, 1000);
    let expected = COST_THRESHOLD; // no penalty
    assert_eq!(
        vtx.eviction_weight(),
        expected,
        "eviction_weight at conventional fee must equal cost with no penalty; expected {}", expected
    );
}

#[test]
fn eviction_weight_above_conventional_fee_has_no_penalty() {
    // fee = 50000 >> 1000: no penalty.
    let tx = minimal_v4_tx();
    let vtx = verified(tx, 50_000);
    let expected = COST_THRESHOLD;
    assert_eq!(
        vtx.eviction_weight(),
        expected,
        "eviction_weight with high fee must equal cost; expected {}", expected
    );
}

#[test]
fn eviction_weight_large_tx_low_fee() {
    // Large tx + low fee: weight = size + 16000.
    use zebra_chain::transparent;
    use zebra_chain::amount::Amount;
    let script_body = [0x51u8; 50];
    let mut outputs = Vec::new();
    for _ in 0..150 {
        outputs.push(transparent::Output {
            value: Amount::zero(),
            lock_script: transparent::Script::new(&script_body[..]),
        });
    }
    let tx = Transaction::V4 {
        inputs: vec![],
        outputs,
        lock_time: LockTime::min_lock_time(),
        expiry_height: block::Height(0),
        joinsplit_data: None,
        sapling_shielded_data: None,
    };
    let sz = serialized_size(&tx);
    assert!(sz > COST_THRESHOLD, "assumption: padded tx ({}b) > 4000 bytes", sz);
    let vtx = verified(tx, 0); // zero fee => penalty
    let expected = sz + LOW_FEE_PENALTY;
    assert_eq!(
        vtx.eviction_weight(),
        expected,
        "eviction_weight for large tx + zero fee must be {} + {} = {}",
        sz, LOW_FEE_PENALTY, expected
    );
}

#[test]
fn eviction_weight_large_tx_adequate_fee() {
    // Large tx + adequate fee: weight = size, no penalty.
    use zebra_chain::transparent;
    use zebra_chain::amount::Amount;
    let script_body = [0x51u8; 50];
    let mut outputs = Vec::new();
    for _ in 0..150 {
        outputs.push(transparent::Output {
            value: Amount::zero(),
            lock_script: transparent::Script::new(&script_body[..]),
        });
    }
    let tx = Transaction::V4 {
        inputs: vec![],
        outputs,
        lock_time: LockTime::min_lock_time(),
        expiry_height: block::Height(0),
        joinsplit_data: None,
        sapling_shielded_data: None,
    };
    let sz = serialized_size(&tx);
    assert!(sz > COST_THRESHOLD, "assumption: padded tx ({}b) > 4000 bytes", sz);
    let vtx = verified(tx, 1000); // exactly at conventional fee => no penalty
    assert_eq!(
        vtx.eviction_weight(),
        sz,
        "eviction_weight for large tx at conventional fee must equal size={}", sz
    );
}

#[test]
fn cost_does_not_consume_self() {
    // `cost` takes &self; calling it twice on the same binding must compile.
    let tx = minimal_v4_tx();
    let vtx = verified(tx, 5000);
    let c1 = vtx.cost();
    let c2 = vtx.cost();
    assert_eq!(c1, c2, "cost() must be idempotent");
}

#[test]
fn verified_unmined_tx_fields_are_accessible() {
    // `transaction` (UnminedTx) and `miner_fee` (Amount<NonNegative>) are pub.
    let tx = minimal_v4_tx();
    let sz = serialized_size(&tx);
    let vtx = verified(tx, 2000);
    // Access the pub fields directly.
    let _ = &vtx.transaction;   // UnminedTx
    let fee_u64: u64 = vtx.miner_fee.into();
    assert_eq!(fee_u64, 2000, "miner_fee field must hold the fee passed to new()");
    // The UnminedTx carries the size.
    assert_eq!(
        vtx.transaction.size as u64,
        sz,
        "UnminedTx.size must equal the serialized byte count"
    );
}

// ───────────── Orchard action size constants (ZIP-225, step 04) ────────────
//
// `ACTION_SIZE`, `SPEND_AUTH_SIG_SIZE` and `AUTHORIZED_ACTION_SIZE` have been
// `pub` since step 04 and are unchanged at step 12. They are values, not just
// names: a candidate who renames the constants fails to compile here, and one
// who changes an arithmetic literal fails an assertion.

/// ACTION_SIZE must equal 5*32 + 580 + 80 = 820.
#[test]
fn action_size_constant_value() {
    assert_eq!(ACTION_SIZE, 820u64, "ACTION_SIZE must be 5*32+580+80 per ZIP-225");
}

/// AUTHORIZED_ACTION_SIZE must equal ACTION_SIZE + SPEND_AUTH_SIG_SIZE = 884.
/// SPEND_AUTH_SIG_SIZE = 64 (RedPallas signature).
#[test]
fn authorized_action_size_constant_value() {
    assert_eq!(AUTHORIZED_ACTION_SIZE, 884u64, "AUTHORIZED_ACTION_SIZE must be ACTION_SIZE+64");
    assert_eq!(SPEND_AUTH_SIG_SIZE, 64u64, "SPEND_AUTH_SIG_SIZE must be 64 (RedPallas)");
    assert_eq!(
        ACTION_SIZE + SPEND_AUTH_SIG_SIZE,
        AUTHORIZED_ACTION_SIZE,
        "AUTHORIZED_ACTION_SIZE must be the sum of the other two"
    );
}

/// ENABLE_SPENDS is bit 0x01, ENABLE_OUTPUTS is bit 0x02.
#[test]
fn flags_bit_values() {
    let spends = Flags::ENABLE_SPENDS;
    let outputs = Flags::ENABLE_OUTPUTS;
    assert_eq!(spends.bits(), 0x01, "ENABLE_SPENDS must be 0b00000001");
    assert_eq!(outputs.bits(), 0x02, "ENABLE_OUTPUTS must be 0b00000010");
    assert_eq!((spends | outputs).bits(), 0x03);
}

/// Flags::empty() does NOT contain ENABLE_SPENDS or ENABLE_OUTPUTS.
#[test]
fn empty_flags_has_no_enable_bits() {
    let empty = Flags::empty();
    assert!(
        !empty.intersects(Flags::ENABLE_SPENDS | Flags::ENABLE_OUTPUTS),
        "Flags::empty() must have neither ENABLE_SPENDS nor ENABLE_OUTPUTS set"
    );
}

// ───────── Orchard `Flags` wire format (ZIP-225, step 04) ──────────────────
//
// The `Flags` serde is byte-identical at app-11: `diff`ing
// `zebra-chain/src/orchard/shielded_data.rs` between app-11 and app-12 shows
// the only change in the whole file is `Action::max_allocation` hoisting its
// expression into a `const MAX` and adding a `static_assertions::const_assert!`
// -- the `impl ZcashSerialize for Flags` and `impl ZcashDeserialize for Flags`
// blocks, including the reserved-bit rejection, are unchanged. These two tests
// are therefore inherited regression, not step-12 evidence, and they live here
// rather than in a scored group.

/// Only bits 0 and 1 are defined, so every one of the four legal bytes must
/// deserialize to exactly those bits and serialize back to the same byte.
///
/// Not vacuous: an implementation that rejected `0x03`, or that silently masked
/// the input instead of round-tripping it, fails here.
#[test]
fn defined_flag_bits_round_trip_through_the_wire_format() {
    for (byte, expected) in [
        (0x00u8, Flags::empty()),
        (0x01, Flags::ENABLE_SPENDS),
        (0x02, Flags::ENABLE_OUTPUTS),
        (0x03, Flags::ENABLE_SPENDS | Flags::ENABLE_OUTPUTS),
    ] {
        let parsed = Flags::zcash_deserialize(&[byte][..])
            .unwrap_or_else(|e| panic!("{:#04x} is a legal Flags byte, got {:?}", byte, e));
        assert_eq!(
            parsed, expected,
            "{:#04x} must deserialize to {:?}",
            byte, expected
        );
        assert_eq!(parsed.bits(), byte, "bits() must report the wire byte back");

        let mut written = Vec::new();
        parsed
            .zcash_serialize(&mut written)
            .expect("in-memory serialization never fails");
        assert_eq!(
            written,
            vec![byte],
            "Flags must serialize to the single byte it was parsed from"
        );
    }
}

/// Bits 2-7 are reserved and MUST be zero: a byte with any of them set is a
/// parse error, not a silently-masked value.
///
/// An implementation written as `Flags::from_bits_truncate(byte)` -- the
/// obvious wrong shortcut -- returns `Ok` for every input and fails every case.
#[test]
fn reserved_flag_bits_are_rejected() {
    for byte in [0b0000_0100u8, 0b0000_1000, 0b1000_0000, 0xff] {
        let result = Flags::zcash_deserialize(&[byte][..]);
        assert!(
            matches!(result, Err(SerializationError::Parse(_))),
            "{:#04x} sets a reserved bit and must be rejected with \
             SerializationError::Parse, got {:?}",
            byte,
            result
        );
    }
}

// ───────── ZIP-244 v5 signature hashes (step 06) ───────────────────────────
//
// `Transaction::sighash(network_upgrade, hash_type, input) -> SigHash` routes
// v5 transactions through a librustzcash bridge; before step 06 the v5 arm was
// an unconditional `todo!()`. Nothing at step 12 touches it, and
// `workspace-checks` cannot tell a correct bridge from one that returns a
// constant, so two published vectors are checked here.
//
// `fixtures/zip244.tsv` ships two rows of the ten-vector ZIP-244 corpus: one
// that names a transparent input and publishes all six digests, and one that
// signs no particular input and publishes only `SIGHASH_ALL`.
//
// These are NOT byte-identical to the two rows of the same name that steps
// 06-09 ship. NU5's consensus branch id changed from `0xf919a198` to
// `0x37519621` between app-09 and app-10, so the older vectors no longer
// deserialize at this band -- `Transaction::zcash_deserialize` rejects them
// with `Parse("expected a valid network upgrade from the consensus branch
// id")`. The rows here are the same two vectors regenerated by upstream for the
// current branch id; the transaction bodies, the signed input's script code and
// the amount are unchanged, and every digest moved because the branch id is
// covered by all of them.

/// One row of `zip244.tsv`, reduced to the columns this file reads.
struct Zip244Vector {
    name: String,
    tx: Vec<u8>,
    txid: [u8; 32],
    transparent_input: Option<u32>,
    script_code: Option<Vec<u8>>,
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

fn hex32(name: &str, column: &str, value: &str) -> [u8; 32] {
    let bytes = hex::decode(value)
        .unwrap_or_else(|e| panic!("{}: {} is not hex: {}", name, column, e));
    let mut out = [0u8; 32];
    assert_eq!(
        bytes.len(),
        32,
        "{}: {} must be 32 bytes, found {}",
        name,
        column,
        bytes.len()
    );
    out.copy_from_slice(&bytes);
    out
}

fn opt_hex32(name: &str, column: &str, value: &str) -> Option<[u8; 32]> {
    if value == "-" {
        None
    } else {
        Some(hex32(name, column, value))
    }
}

/// Parse `zip244.tsv` by column name, so a reordered or extended fixture keeps
/// working and a missing column fails loudly instead of reading the wrong one.
fn zip244_vectors() -> Vec<Zip244Vector> {
    let path = fixture_dir().join("zip244.tsv");
    let text = std::fs::read_to_string(&path)
        .unwrap_or_else(|e| panic!("missing fixture {:?}: {}", path, e));
    let mut lines = text.lines();
    let header: Vec<&str> = lines
        .next()
        .expect("zip244.tsv is empty")
        .trim_start_matches("# ")
        .split('\t')
        .collect();

    let mut out = Vec::new();
    for line in lines {
        if line.trim().is_empty() || line.starts_with('#') {
            continue;
        }
        let row: BTreeMap<&str, &str> = header.iter().cloned().zip(line.split('\t')).collect();
        let cell = |key: &str| -> &str {
            *row.get(key)
                .unwrap_or_else(|| panic!("zip244.tsv has no {} column", key))
        };
        let name = cell("name").to_string();
        out.push(Zip244Vector {
            tx: hex::decode(cell("tx_hex"))
                .unwrap_or_else(|e| panic!("{}: tx_hex is not hex: {}", name, e)),
            txid: hex32(&name, "txid_hex", cell("txid_hex")),
            transparent_input: match cell("transparent_input") {
                "-" => None,
                other => Some(
                    other
                        .parse::<u32>()
                        .unwrap_or_else(|e| panic!("{}: transparent_input: {}", name, e)),
                ),
            },
            script_code: match cell("script_code_hex") {
                "-" => None,
                other => Some(
                    hex::decode(other)
                        .unwrap_or_else(|e| panic!("{}: script_code_hex is not hex: {}", name, e)),
                ),
            },
            amount: match cell("amount") {
                "-" => None,
                other => Some(
                    other
                        .parse::<i64>()
                        .unwrap_or_else(|e| panic!("{}: amount: {}", name, e)),
                ),
            },
            sighash_all: hex32(&name, "sighash_all", cell("sighash_all")),
            sighash_none: opt_hex32(&name, "sighash_none", cell("sighash_none")),
            sighash_single: opt_hex32(&name, "sighash_single", cell("sighash_single")),
            sighash_all_anyone: opt_hex32(&name, "sighash_all_anyone", cell("sighash_all_anyone")),
            sighash_none_anyone: opt_hex32(
                &name,
                "sighash_none_anyone",
                cell("sighash_none_anyone"),
            ),
            sighash_single_anyone: opt_hex32(
                &name,
                "sighash_single_anyone",
                cell("sighash_single_anyone"),
            ),
            name,
        });
    }
    out
}

fn zip244_vector(name: &str) -> Zip244Vector {
    zip244_vectors()
        .into_iter()
        .find(|v| v.name == name)
        .unwrap_or_else(|| panic!("zip244.tsv has no row for {}", name))
}

fn parse_v5(vector: &Zip244Vector) -> Transaction {
    let tx = Transaction::zcash_deserialize(Cursor::new(&vector.tx[..]))
        .unwrap_or_else(|e| panic!("{}: the vector did not parse: {:?}", vector.name, e));
    assert_eq!(
        tx.version(),
        5,
        "{}: the ZIP-244 vectors are v5 transactions",
        vector.name
    );
    tx
}

/// All six ZIP-244 signature hashes of the vector that names a transparent
/// input must come back exactly as published.
///
/// A `sighash` that ignored `hash_type`, or that ignored the signed input's
/// `script_code`/`amount`, reproduces at most one of the six.
#[test]
fn zip244_sighash_reproduces_all_six_variants_for_a_transparent_input_vector() {
    let vector = zip244_vector("zip244-06");
    let tx = parse_v5(&vector);

    let index = vector
        .transparent_input
        .expect("zip244-06 names a transparent input");
    let script = vector
        .script_code
        .as_ref()
        .expect("zip244-06 publishes a script code");
    let amount = vector.amount.expect("zip244-06 publishes an amount");
    let output = transparent::Output {
        value: Amount::<NonNegative>::try_from(amount)
            .unwrap_or_else(|e| panic!("{} zatoshi is not a valid amount: {:?}", amount, e)),
        lock_script: transparent::Script::new(&script[..]),
    };

    let cases: [(&str, HashType, [u8; 32]); 6] = [
        ("ALL", HashType::ALL, vector.sighash_all),
        (
            "NONE",
            HashType::NONE,
            vector.sighash_none.expect("zip244-06 publishes SIGHASH_NONE"),
        ),
        (
            "SINGLE",
            HashType::SINGLE,
            vector
                .sighash_single
                .expect("zip244-06 publishes SIGHASH_SINGLE"),
        ),
        (
            "ALL|ANYONECANPAY",
            HashType::ALL | HashType::ANYONECANPAY,
            vector
                .sighash_all_anyone
                .expect("zip244-06 publishes SIGHASH_ALL|ANYONECANPAY"),
        ),
        (
            "NONE|ANYONECANPAY",
            HashType::NONE | HashType::ANYONECANPAY,
            vector
                .sighash_none_anyone
                .expect("zip244-06 publishes SIGHASH_NONE|ANYONECANPAY"),
        ),
        (
            "SINGLE|ANYONECANPAY",
            HashType::SINGLE | HashType::ANYONECANPAY,
            vector
                .sighash_single_anyone
                .expect("zip244-06 publishes SIGHASH_SINGLE|ANYONECANPAY"),
        ),
    ];

    for (label, hash_type, expected) in cases {
        let got = tx.sighash(NetworkUpgrade::Nu5, hash_type, Some((index, output.clone())));
        assert_eq!(
            hex::encode(&got.0[..]),
            hex::encode(&expected[..]),
            "{}: nHashType {} does not reproduce the published ZIP-244 sighash",
            vector.name,
            label
        );
    }
}

/// A v5 transaction that signs no particular transparent input still has a
/// `SIGHASH_ALL` digest, and ZIP-244 makes it equal to the transaction's own
/// txid. Both the published column and the crate's own `Hash::from(&tx)` must
/// agree with what `sighash` computes.
#[test]
fn zip244_sighash_all_for_a_no_input_vector_matches_its_txid() {
    let vector = zip244_vector("zip244-03");
    let tx = parse_v5(&vector);
    assert!(
        vector.transparent_input.is_none(),
        "zip244-03 must be the vector that signs no particular input"
    );

    let got = tx.sighash(NetworkUpgrade::Nu5, HashType::ALL, None);
    assert_eq!(
        hex::encode(&got.0[..]),
        hex::encode(&vector.sighash_all[..]),
        "{}: SIGHASH_ALL with no signed input must match the published digest",
        vector.name
    );
    assert_eq!(
        hex::encode(&got.0[..]),
        hex::encode(&vector.txid[..]),
        "{}: with no signed input the SIGHASH_ALL digest is the txid",
        vector.name
    );
    assert_eq!(
        hex::encode(&vector.txid[..]),
        hex::encode(tx.hash().0),
        "{}: the crate's own txid must agree with the published one",
        vector.name
    );
}

// ───────── ZIP-209 transaction value pools (step 08) ───────────────────────
//
// `ValueBalance` and `remaining_transaction_value` are the shielded-value
// accounting the chain value pools are built on. They are pure arithmetic, so
// a gutted implementation compiles and `workspace-checks` sees nothing.

fn zats<C: Constraint>(value: i64) -> Amount<C> {
    Amount::try_from(value).unwrap_or_else(|e| panic!("{} is not a valid Amount: {:?}", value, e))
}

/// Reads an `Amount` back out as an `i64` without using the arithmetic of the
/// type under test.
fn zatoshis<C>(amount: Amount<C>) -> i64 {
    i64::from_le_bytes(amount.to_bytes())
}

fn pools<C: Constraint + Copy>(balance: ValueBalance<C>) -> (i64, i64, i64, i64) {
    (
        zatoshis(balance.transparent_amount()),
        zatoshis(balance.sprout_amount()),
        zatoshis(balance.sapling_amount()),
        zatoshis(balance.orchard_amount()),
    )
}

/// Builds a `ValueBalance` from four pool amounts using only the four
/// single-pool constructors and addition.
fn value_balance<C: Constraint + Copy>(
    transparent_value: i64,
    sprout: i64,
    sapling: i64,
    orchard: i64,
) -> ValueBalance<C> {
    let sum = ValueBalance::<C>::from_transparent_amount(zats(transparent_value))
        + ValueBalance::<C>::from_sprout_amount(zats(sprout))
        + ValueBalance::<C>::from_sapling_amount(zats(sapling))
        + ValueBalance::<C>::from_orchard_amount(zats(orchard));
    sum.unwrap_or_else(|e| {
        panic!(
            "({}, {}, {}, {}) should be a valid ValueBalance: {:?}",
            transparent_value, sprout, sapling, orchard, e
        )
    })
}

/// The remaining transaction value is the *sum* of all four pools.
///
/// Under the older "transparent minus shielded" convention the first case comes
/// out as 1600 rather than 400, so this distinguishes the two.
#[test]
fn zip209_remaining_transaction_value_is_the_sum_of_the_four_pools() {
    let spending: ValueBalance<NegativeAllowed> = value_balance(1000, -250, -300, -50);
    assert_eq!(
        spending.remaining_transaction_value().map(zatoshis),
        Ok(400),
        "transparent 1000 with shielded balances -250/-300/-50 leaves 400, not 1600"
    );

    let unshielding: ValueBalance<NegativeAllowed> = value_balance(-9_000, 4_000, 5_000, 250);
    assert_eq!(
        unshielding.remaining_transaction_value().map(zatoshis),
        Ok(250),
        "-9000 transparent against +4000/+5000/+250 shielded leaves 250"
    );

    // `remaining_transaction_value` is implemented only for `NegativeAllowed`
    // at this tag, which is itself part of the contract: the sum of the four
    // pools may legitimately be negative before the rule rejects it.
    assert_eq!(
        ValueBalance::<NegativeAllowed>::zero()
            .remaining_transaction_value()
            .map(zatoshis),
        Ok(0),
        "the zero value balance has zero remaining transaction value"
    );
}

/// The ZIP-209 consensus rule: the remaining value in the transparent
/// transaction value pool MUST be non-negative, and the reported value is the
/// shortfall itself rather than one of the individual pools.
#[test]
fn zip209_a_negative_pool_sum_is_rejected_with_the_non_negative_range() {
    let short: ValueBalance<NegativeAllowed> = value_balance(100, -50, -51, 0);
    assert_eq!(
        short.remaining_transaction_value(),
        Err(amount::Error::Constraint {
            value: -1,
            range: 0..=MAX_MONEY,
        }),
        "a balance summing to -1 must be rejected as out of the non-negative range"
    );

    let very_short: ValueBalance<NegativeAllowed> = value_balance(0, 0, -21_000, 0);
    assert_eq!(
        very_short.remaining_transaction_value(),
        Err(amount::Error::Constraint {
            value: -21_000,
            range: 0..=MAX_MONEY,
        }),
        "the rejected value is the sum itself, not one of the individual pools"
    );
}

/// Each single-pool constructor must leave the other three pools at zero, so a
/// value balance cannot silently smear one pool's amount across the others.
#[test]
fn zip209_each_pool_constructor_leaves_the_other_three_at_zero() {
    assert_eq!(
        pools(ValueBalance::<NonNegative>::from_transparent_amount(zats(7))),
        (7, 0, 0, 0),
        "from_transparent_amount must only set the transparent pool"
    );
    assert_eq!(
        pools(ValueBalance::<NonNegative>::from_sprout_amount(zats(7))),
        (0, 7, 0, 0),
        "from_sprout_amount must only set the sprout pool"
    );
    assert_eq!(
        pools(ValueBalance::<NonNegative>::from_sapling_amount(zats(7))),
        (0, 0, 7, 0),
        "from_sapling_amount must only set the sapling pool"
    );
    assert_eq!(
        pools(ValueBalance::<NonNegative>::from_orchard_amount(zats(7))),
        (0, 0, 0, 7),
        "from_orchard_amount must only set the orchard pool"
    );
    assert_eq!(
        pools(ValueBalance::<NonNegative>::zero()),
        (0, 0, 0, 0),
        "zero() must set all four pools to zero"
    );
}
