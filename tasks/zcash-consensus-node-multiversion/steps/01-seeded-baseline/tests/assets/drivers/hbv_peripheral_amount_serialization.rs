//! Zatoshi amounts: the range check, the arithmetic, and the eight bytes.
//!
//! Harbor verifier driver -- injected into `zebra-chain/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! This is a peripheral breadth group. Step 1 owns serialization, and `Amount`
//! is the value type every serialized value in the protocol passes through: an
//! output's value, a transaction's `valueBalance`, a JoinSplit's `vpub_old` and
//! `vpub_new`. It is eight little-endian bytes on the wire and an `i64` in
//! memory, but the wire is wider than the protocol -- 2^63 zatoshi is several
//! million times more money than will ever exist -- so the constraint types are
//! what stop a parser from admitting a value the rest of the network rejects.
//!
//! The two constraints are `NonNegative` (`0..=MAX_MONEY`) and `NegativeAllowed`
//! (`-MAX_MONEY..=MAX_MONEY`); `MAX_MONEY` is 21,000,000 ZEC in zatoshi. The
//! range is checked at construction, and arithmetic returns a `Result` rather
//! than a value, so leaving the range is an error and never a silent wrap --
//! which is the only reason a node cannot be talked into believing in money
//! that does not exist.
//!
//! The `valueBalance` case is checked against a real v4 transaction: the
//! ZIP-243 test vector shipped in `$HBV_FIXTURES/sighash.tsv`. Its value
//! balance and the offset it sits at were read out of the vector's bytes by an
//! independent parser, not by this crate.

use std::collections::BTreeMap;
use std::convert::TryFrom;
use std::io::Cursor;
use std::path::PathBuf;

use zebra_chain::amount::{
    Amount, Constraint, Error, NegativeAllowed, NonNegative, COIN, MAX_MONEY,
};
use zebra_chain::serialization::{SerializationError, ZcashDeserialize, ZcashSerialize};
use zebra_chain::transaction::Transaction;
use zebra_chain::transparent::Output;

/// Byte offset of `valueBalance` inside the raw bytes of `zip243-vector2`, and
/// the value that sits there. Both were read out of the vector's own bytes by
/// an independent parser, not by this crate.
const VALUE_BALANCE_OFFSET: usize = 139;
const VALUE_BALANCE: i64 = 843_771_036_901_478;

/// The bytes of a `transparent::Output` carrying `value` zatoshi and an empty
/// lock script: eight little-endian value bytes, then a zero compactSize.
fn output_bytes(value: u64) -> Vec<u8> {
    let mut out = value.to_le_bytes().to_vec();
    out.push(0x00);
    out
}

// ---------------------------------------------------------------- fixtures
//
// Only one fixture is needed here: the raw bytes of a ZIP-243 test vector
// transaction, which is a v4 transaction and so carries a `valueBalance`.
// `sighash.tsv` ships it as hex in its `raw_tx_hex` column.

fn fixture_dir() -> PathBuf {
    PathBuf::from(
        std::env::var("HBV_FIXTURES")
            .expect("HBV_FIXTURES must point at the shipped fixture directory"),
    )
}

/// The raw transaction bytes of the named vector in `sighash.tsv`.
fn vector_transaction(name: &str) -> Vec<u8> {
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

    for line in lines {
        if line.is_empty() {
            continue;
        }
        let cells: Vec<&str> = line.split('\t').collect();
        assert_eq!(cells.len(), header.len(), "malformed sighash.tsv row");
        let row: BTreeMap<&str, &str> = header.iter().cloned().zip(cells).collect();
        if *row.get("name").expect("sighash.tsv has a name column") == name {
            let hex_text = *row
                .get("raw_tx_hex")
                .expect("sighash.tsv has a raw_tx_hex column");
            return hex::decode(hex_text)
                .unwrap_or_else(|e| panic!("{}: raw_tx_hex is not hex: {}", name, e));
        }
    }
    panic!("sighash.tsv has no vector named {}", name)
}

// ----------------------------------------------------------------- helpers

/// `MAX_MONEY` restated from the protocol specification rather than read from
/// the crate: 21,000,000 ZEC, at 10^8 zatoshi per ZEC.
const TWENTY_ONE_MILLION_ZEC: i64 = 21_000_000 * 100_000_000;

fn serialize<T: ZcashSerialize>(value: &T) -> Vec<u8> {
    let mut buf = Vec::new();
    value
        .zcash_serialize(&mut buf)
        .expect("writing to a Vec is infallible");
    buf
}

/// Assert that constructing an `Amount<C>` from `value` failed because `value`
/// is outside `range`, and that the error reports the value unchanged -- an
/// error that reported a wrapped value would mean the wrap had already happened.
fn expect_out_of_range<C: Constraint + std::fmt::Debug>(
    what: &str,
    value: i64,
    result: Result<Amount<C>, Error>,
) {
    match result {
        Err(Error::Contains { range, value: got }) => {
            assert_eq!(
                got, value,
                "{}: the error reports {} but {} was offered",
                what, got, value
            );
            assert_eq!(
                range,
                C::valid_range(),
                "{}: the error reports the wrong valid range",
                what
            );
            let shown = Error::Contains { range, value: got }.to_string();
            assert!(
                shown.contains(&value.to_string()),
                "{}: the error message {:?} does not name the offending value",
                what,
                shown
            );
        }
        other => panic!(
            "{}: {} should be outside {:?}, got {:?}",
            what,
            value,
            C::valid_range(),
            other
        ),
    }
}

// ------------------------------------------------------------------- tests

#[test]
fn max_money_is_twenty_one_million_zec_and_bounds_both_constraints() {
    assert_eq!(
        COIN, 100_000_000,
        "one ZEC is 10^8 zatoshi, this crate says {}",
        COIN
    );
    assert_eq!(
        MAX_MONEY, TWENTY_ONE_MILLION_ZEC,
        "MAX_MONEY should be 21,000,000 ZEC = {} zatoshi, this crate says {}",
        TWENTY_ONE_MILLION_ZEC, MAX_MONEY
    );
    assert_eq!(MAX_MONEY, 2_100_000_000_000_000);

    assert_eq!(
        NonNegative::valid_range(),
        0..=MAX_MONEY,
        "NonNegative should allow 0..=MAX_MONEY"
    );
    assert_eq!(
        NegativeAllowed::valid_range(),
        -MAX_MONEY..=MAX_MONEY,
        "NegativeAllowed should allow -MAX_MONEY..=MAX_MONEY"
    );

    // The whole point of the constraint: an i64 is about four thousand times
    // wider than the money supply (9223372036854775807 / 2100000000000000 =
    // 4392), so the wire type cannot be trusted to keep a parser honest on its
    // own -- most i64 bit patterns are not valid amounts.
    assert!(
        MAX_MONEY < i64::MAX / 4_000,
        "MAX_MONEY {} should be far below i64::MAX {}",
        MAX_MONEY,
        i64::MAX
    );

    // Both ends of both ranges are reachable.
    for &value in [0i64, 1, MAX_MONEY].iter() {
        let amount = Amount::<NonNegative>::try_from(value)
            .unwrap_or_else(|e| panic!("{} should be a valid NonNegative amount: {:?}", value, e));
        assert_eq!(i64::from(amount), value, "NonNegative {} round trip", value);
    }
    for &value in [-MAX_MONEY, -1i64, 0, MAX_MONEY].iter() {
        let amount = Amount::<NegativeAllowed>::try_from(value).unwrap_or_else(|e| {
            panic!("{} should be a valid NegativeAllowed amount: {:?}", value, e)
        });
        assert_eq!(
            i64::from(amount),
            value,
            "NegativeAllowed {} round trip",
            value
        );
    }

    // And the bound is the one the wire enforces, not just a constant: a
    // transparent output is an `Amount<NonNegative>` followed by a script, so
    // `Output::zcash_deserialize` is where an impossible value has to be
    // refused. MAX_MONEY itself is a real output value and must parse.
    let at_the_limit = Output::zcash_deserialize(Cursor::new(&output_bytes(MAX_MONEY as u64)[..]))
        .expect("an output carrying exactly MAX_MONEY zatoshi is a valid output");
    assert_eq!(
        i64::from(at_the_limit.value),
        MAX_MONEY,
        "an output at the money supply should parse to MAX_MONEY"
    );
    assert!(
        at_the_limit.lock_script.0.is_empty(),
        "the test output has an empty lock script"
    );
    match Output::zcash_deserialize(Cursor::new(&output_bytes(MAX_MONEY as u64 + 1)[..])) {
        Err(SerializationError::Amount { .. }) => {}
        other => panic!(
            "an output one zatoshi above the money supply should be an amount error, got {:?}",
            other
        ),
    }
}

#[test]
fn values_outside_the_constraint_are_rejected_at_construction() {
    // One zatoshi past each boundary, from both directions.
    expect_out_of_range::<NonNegative>(
        "one zatoshi above the money supply",
        MAX_MONEY + 1,
        Amount::<NonNegative>::try_from(MAX_MONEY + 1),
    );
    expect_out_of_range::<NonNegative>(
        "a negative NonNegative amount",
        -1,
        Amount::<NonNegative>::try_from(-1i64),
    );
    expect_out_of_range::<NegativeAllowed>(
        "one zatoshi below minus the money supply",
        -MAX_MONEY - 1,
        Amount::<NegativeAllowed>::try_from(-MAX_MONEY - 1),
    );
    expect_out_of_range::<NegativeAllowed>(
        "one zatoshi above the money supply",
        MAX_MONEY + 1,
        Amount::<NegativeAllowed>::try_from(MAX_MONEY + 1),
    );

    // A u64 that does not even fit in an i64 is a different failure: it is
    // rejected before the range is consulted, so the error names the u64.
    match Amount::<NonNegative>::try_from(u64::MAX) {
        Err(Error::Convert { value, .. }) => assert_eq!(
            value,
            u64::MAX,
            "the conversion error should report the offered u64"
        ),
        other => panic!(
            "u64::MAX should not convert to an Amount, got {:?}",
            other
        ),
    }

    // `constrain` re-checks the range when the constraint changes, so widening
    // always works and narrowing can fail.
    let negative = Amount::<NegativeAllowed>::try_from(-1i64).expect("-1 is NegativeAllowed");
    let positive = Amount::<NonNegative>::try_from(1i64).expect("1 is NonNegative");
    positive
        .constrain::<NegativeAllowed>()
        .expect("widening a NonNegative amount always succeeds");
    expect_out_of_range::<NonNegative>(
        "narrowing a negative amount to NonNegative",
        -1,
        negative.constrain::<NonNegative>(),
    );

    // Construction is not the only place the range has to hold: every amount
    // on the wire is constructed by a deserializer, so the same rejection has
    // to happen mid-parse. Take a real v4 transaction and overwrite its
    // `valueBalance` -- and nothing else -- with one zatoshi more than can
    // exist. The bytes stay a structurally valid v4 transaction, so the only
    // thing that can refuse them is the range check inside the amount reader.
    let raw = vector_transaction("zip243-vector2");
    Transaction::zcash_deserialize(Cursor::new(&raw[..]))
        .expect("the unmodified ZIP-243 vector transaction parses");

    let mut tampered = raw.clone();
    tampered[VALUE_BALANCE_OFFSET..VALUE_BALANCE_OFFSET + 8]
        .copy_from_slice(&(MAX_MONEY + 1).to_le_bytes());
    assert_ne!(
        tampered, raw,
        "the tampered transaction should differ from the vector"
    );
    assert_eq!(
        tampered.len(),
        raw.len(),
        "overwriting the value balance should not change the transaction's length"
    );
    match Transaction::zcash_deserialize(Cursor::new(&tampered[..])) {
        Err(SerializationError::Amount { .. }) => {}
        other => panic!(
            "a v4 transaction whose valueBalance is MAX_MONEY + 1 should fail to parse \
             with an amount error, got {:?}",
            other
        ),
    }

    // The offset really is the value balance and not some neighbouring field:
    // writing a value that *is* in range leaves a transaction that parses and
    // reports it.
    let mut rewritten = raw.clone();
    rewritten[VALUE_BALANCE_OFFSET..VALUE_BALANCE_OFFSET + 8]
        .copy_from_slice(&(-MAX_MONEY).to_le_bytes());
    match Transaction::zcash_deserialize(Cursor::new(&rewritten[..])) {
        Ok(Transaction::V4 { value_balance, .. }) => assert_eq!(
            i64::from(value_balance),
            -MAX_MONEY,
            "the rewritten value balance should be read back"
        ),
        other => panic!(
            "a v4 transaction with valueBalance = -MAX_MONEY should parse, got {:?}",
            other.map(|tx| std::mem::discriminant(&tx))
        ),
    }
}

#[test]
fn arithmetic_that_leaves_the_range_is_an_error_not_a_wrap() {
    let one_nonneg = Amount::<NonNegative>::try_from(1i64).expect("1 is NonNegative");
    let zero_nonneg = Amount::<NonNegative>::try_from(0i64).expect("0 is NonNegative");
    let max_nonneg = Amount::<NonNegative>::try_from(MAX_MONEY).expect("MAX_MONEY is NonNegative");

    // MAX_MONEY + 1 reports MAX_MONEY + 1, which is the whole claim: a wrapping
    // implementation would report a large negative number, or nothing at all.
    expect_out_of_range::<NonNegative>(
        "MAX_MONEY + 1",
        MAX_MONEY + 1,
        max_nonneg + one_nonneg,
    );
    expect_out_of_range::<NonNegative>("0 - 1 as NonNegative", -1, zero_nonneg - one_nonneg);

    // The same subtraction is fine once negative values are allowed, so the
    // error above came from the constraint and not from the arithmetic.
    let one = Amount::<NegativeAllowed>::try_from(1i64).expect("1 is NegativeAllowed");
    let zero = Amount::<NegativeAllowed>::try_from(0i64).expect("0 is NegativeAllowed");
    let minus_one = (zero - one).expect("NegativeAllowed permits -1");
    assert_eq!(i64::from(minus_one), -1, "0 - 1 should be -1");

    let min = Amount::<NegativeAllowed>::try_from(-MAX_MONEY).expect("-MAX_MONEY is in range");
    expect_out_of_range::<NegativeAllowed>("-MAX_MONEY - 1", -MAX_MONEY - 1, min - one);

    // Additions that stay in range still add.
    let sum = (max_nonneg - one_nonneg)
        .expect("MAX_MONEY - 1 is in range")
        + one_nonneg;
    assert_eq!(
        i64::from(sum.expect("MAX_MONEY - 1 + 1 is in range")),
        MAX_MONEY,
        "arithmetic inside the range should be exact"
    );

    // Multiplication and division have their own errors, and neither wraps.
    expect_out_of_range::<NonNegative>(
        "MAX_MONEY * 2",
        MAX_MONEY * 2,
        max_nonneg * 2u64,
    );
    match max_nonneg * u64::MAX {
        Err(Error::MultiplicationOverflow { amount, multiplier }) => {
            assert_eq!(amount, MAX_MONEY, "the error should report the amount");
            assert_eq!(
                multiplier,
                u64::MAX,
                "the error should report the multiplier"
            );
        }
        other => panic!("MAX_MONEY * u64::MAX should overflow, got {:?}", other),
    }
    match max_nonneg / 0u64 {
        Err(Error::DivideByZero { amount }) => {
            assert_eq!(amount, MAX_MONEY, "the error should report the amount")
        }
        other => panic!("dividing by zero should be an error, got {:?}", other),
    }
}

#[test]
fn a_v4_value_balance_is_a_negative_allowed_amount_of_eight_little_endian_bytes() {
    // `valueBalance` is the one signed amount in a transaction: it is the net
    // value moving out of the Sapling pool, so it is negative whenever more
    // value is shielded than unshielded. It sits immediately after
    // `nExpiryHeight` in a v4 transaction.
    //
    // Both numbers below were read out of the ZIP-243 vector's own bytes by an
    // independent parser: the value balance is at byte offset 139 of
    // `zip243-vector2`, and it is 843,771,036,901,478 zatoshi.
    let raw = vector_transaction("zip243-vector2");
    let tx = Transaction::zcash_deserialize(Cursor::new(&raw[..]))
        .expect("the ZIP-243 vector transaction parses");

    let value_balance: Amount<NegativeAllowed> = match tx {
        Transaction::V4 { value_balance, .. } => value_balance,
        ref other => panic!(
            "zip243-vector2 should be a v4 transaction, parsed as {:?}",
            std::mem::discriminant(other)
        ),
    };

    assert_eq!(
        i64::from(value_balance),
        VALUE_BALANCE,
        "the value balance read out of the vector's bytes is {}",
        VALUE_BALANCE
    );
    assert_eq!(
        value_balance.to_bytes(),
        VALUE_BALANCE.to_le_bytes(),
        "to_bytes should be the little-endian i64"
    );
    assert_eq!(
        &value_balance.to_bytes()[..],
        &raw[VALUE_BALANCE_OFFSET..VALUE_BALANCE_OFFSET + 8],
        "the value balance's bytes are not the bytes at offset {} of the vector",
        VALUE_BALANCE_OFFSET
    );
    assert_eq!(
        serialize(&value_balance),
        VALUE_BALANCE.to_le_bytes().to_vec(),
        "zcash_serialize should agree with to_bytes"
    );

    // Eight little-endian bytes, both ways, for values a v4 transaction can
    // really carry -- including the negative ones only `NegativeAllowed` admits.
    for &value in [-MAX_MONEY, -1i64, 0, 1, VALUE_BALANCE, MAX_MONEY].iter() {
        let amount = Amount::<NegativeAllowed>::try_from(value)
            .unwrap_or_else(|e| panic!("{} should be in range: {:?}", value, e));
        let bytes = serialize(&amount);
        assert_eq!(
            bytes,
            value.to_le_bytes().to_vec(),
            "{} should serialize to its little-endian i64 bytes",
            value
        );
        let back = Amount::<NegativeAllowed>::zcash_deserialize(Cursor::new(&bytes[..]))
            .unwrap_or_else(|e| panic!("{} did not round trip: {:?}", value, e));
        assert_eq!(i64::from(back), value, "{} round trip", value);
    }

    // The same eight bytes mean different things under the two constraints:
    // -1 is `ff` eight times, which a `NonNegative` reader must refuse rather
    // than read as 18,446,744,073,709,551,615 zatoshi.
    let minus_one = serialize(&Amount::<NegativeAllowed>::try_from(-1i64).expect("-1 is in range"));
    assert_eq!(minus_one, vec![0xffu8; 8], "-1 should be eight 0xff bytes");
    match Amount::<NonNegative>::zcash_deserialize(Cursor::new(&minus_one[..])) {
        Err(SerializationError::Amount { .. }) => {}
        other => panic!(
            "eight 0xff bytes should not deserialize as a NonNegative amount, got {:?}",
            other
        ),
    }
    // ... and a value one zatoshi above the money supply is refused by both.
    let too_much = (MAX_MONEY + 1).to_le_bytes();
    for what in ["NonNegative", "NegativeAllowed"].iter() {
        let result = if *what == "NonNegative" {
            Amount::<NonNegative>::zcash_deserialize(Cursor::new(&too_much[..])).map(i64::from)
        } else {
            Amount::<NegativeAllowed>::zcash_deserialize(Cursor::new(&too_much[..])).map(i64::from)
        };
        match result {
            Err(SerializationError::Amount { .. }) => {}
            other => panic!(
                "{}: MAX_MONEY + 1 should not deserialize, got {:?}",
                what, other
            ),
        }
    }
}
