//! What the serialization layer does with hostile input.
//!
//! Harbor verifier driver -- injected into `zebra-chain/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! Every length in the Zcash wire format is attacker-supplied. A peer can send
//! nine bytes that say "a vector of eighteen quintillion outputs follows" and
//! then close the connection. A node that turns that number into a
//! `Vec::with_capacity` aborts; a node that turns it into a loop bound spins
//! forever. Neither failure needs a valid signature, a valid proof, or even a
//! valid second byte, so this is the cheapest denial of service in the
//! protocol and the layer that has to stop it is this one.
//!
//! The other half of the job is telling failures apart. `SerializationError`
//! distinguishes `Io` -- the stream ended, come back with more bytes -- from
//! `Parse` -- these bytes are permanently invalid, drop the peer -- from
//! `Amount` -- a number outside the money supply. Conflating the first two
//! turns a partially-received message into a ban and a malformed message into
//! a retry loop, so the discrimination is asserted in both directions.

use std::convert::TryInto;
use std::io::Cursor;
use std::net::{IpAddr, Ipv4Addr, Ipv6Addr, SocketAddr};

use zebra_chain::amount::{Amount, NegativeAllowed, NonNegative};
use zebra_chain::block;
use zebra_chain::transaction::{LockTime, Transaction};
use zebra_chain::transparent::{Input, OutPoint, Output, Script};
use zebra_chain::serialization::{
    ReadZcashExt, SerializationError, WriteZcashExt, ZcashDeserialize, ZcashDeserializeInto,
    ZcashSerialize,
};

// -------------------------------------------------------------- constants

/// The maximum number of zatoshis that can ever exist: 21,000,000 ZEC at
/// 100,000,000 zatoshi each. Restated from the protocol specification rather
/// than read out of the crate, so that the range check below is a claim about
/// Zcash and not about `zebra_chain::amount::MAX_MONEY`.
const MAX_MONEY: i64 = 21_000_000 * 100_000_000;

// ----------------------------------------------------------------- helpers

/// A compactSize length prefix claiming `n` elements, written by hand in the
/// canonical form so that the prefix itself is never what is rejected.
fn count_prefix(n: u64) -> Vec<u8> {
    let mut out = Vec::new();
    if n <= 0xfc {
        out.push(n as u8);
    } else if n <= 0xffff {
        out.push(0xfd);
        out.extend_from_slice(&(n as u16).to_le_bytes());
    } else if n <= 0xffff_ffff {
        out.push(0xfe);
        out.extend_from_slice(&(n as u32).to_le_bytes());
    } else {
        out.push(0xff);
        out.extend_from_slice(&n.to_le_bytes());
    }
    out
}

/// Read a `Vec<T>` whose length prefix is a lie, and require the reader to
/// come back with an io error rather than an allocation, an abort or a spin.
///
/// The strongest part of this assertion is that it returns at all: a reader
/// that calls `Vec::with_capacity` on `u64::MAX as usize` panics with a
/// capacity overflow, one that allocates a smaller-but-still-absurd count is
/// killed by the allocator, and one that loops over the claimed count never
/// returns and is killed by the harness timeout. In all three the test never
/// reaches its own `match`, so liveness is witnessed by reaching the
/// assertions below rather than by reading a clock -- deliberately, because a
/// wall-clock bound inside a scored group makes the reward vary with the
/// machine the grader happens to get.
fn hostile_count_is_rejected<T: ZcashDeserialize>(type_name: &str, claimed: u64, tail: &[u8]) {
    let mut stream = count_prefix(claimed);
    stream.extend_from_slice(tail);

    let result: Result<Vec<T>, SerializationError> =
        Vec::<T>::zcash_deserialize(Cursor::new(&stream[..]));

    match result {
        Err(SerializationError::Io(_)) => {}
        Err(other) => panic!(
            "Vec<{}> claiming {} elements with only {} bytes after the prefix gave {:?}; \
             the input ran out, so this is an io error",
            type_name,
            claimed,
            tail.len(),
            other
        ),
        Ok(values) => panic!(
            "Vec<{}> claiming {} elements with only {} bytes after the prefix parsed \
             successfully, as {} elements",
            type_name,
            claimed,
            tail.len(),
            values.len()
        ),
    }
}

fn expect_parse_error<T: std::fmt::Debug>(what: &str, result: Result<T, SerializationError>) {
    match result {
        Err(SerializationError::Parse(_)) => {}
        Err(other) => panic!(
            "{}: this encoding is structurally invalid and must be a parse error, got {:?}",
            what, other
        ),
        Ok(value) => panic!("{}: expected a parse error, but it parsed as {:?}", what, value),
    }
}

fn expect_io_error<T: std::fmt::Debug>(what: &str, result: Result<T, SerializationError>) {
    match result {
        Err(SerializationError::Io(_)) => {}
        Err(other) => panic!(
            "{}: the input ran out, so this must be an io error, got {:?}",
            what, other
        ),
        Ok(value) => panic!("{}: expected an io error, but it parsed as {:?}", what, value),
    }
}

fn expect_amount_error<T: std::fmt::Debug>(what: &str, result: Result<T, SerializationError>) {
    match result {
        Err(SerializationError::Amount { .. }) => {}
        Err(other) => panic!(
            "{}: an out-of-range zatoshi value has its own error kind, got {:?}",
            what, other
        ),
        Ok(value) => panic!("{}: expected an amount error, but it parsed as {:?}", what, value),
    }
}

/// A transparent input spending a previous output, with a lock script of
/// `script_len` bytes. The pieces are assembled by hand here so that the
/// truncation points below can be chosen precisely.
fn prevout_input_bytes(script: &[u8]) -> Vec<u8> {
    let mut out = Vec::new();
    out.extend_from_slice(&[0x11u8; 32]); // a non-zero hash: not a coinbase
    out.extend_from_slice(&7u32.to_le_bytes()); // output index
    out.extend_from_slice(&count_prefix(script.len() as u64));
    out.extend_from_slice(script);
    out.extend_from_slice(&0xffff_fffeu32.to_le_bytes()); // sequence
    out
}

/// A coinbase input carrying `data` in place of an unlock script.
fn coinbase_input_bytes(data: &[u8], index: u32, with_sequence: bool) -> Vec<u8> {
    let mut out = Vec::new();
    out.extend_from_slice(&[0u8; 32]); // the all-zero hash marks a coinbase
    out.extend_from_slice(&index.to_le_bytes());
    out.extend_from_slice(&count_prefix(data.len() as u64));
    out.extend_from_slice(data);
    if with_sequence {
        out.extend_from_slice(&0xffff_ffffu32.to_le_bytes());
    }
    out
}

// ------------------------------------------------------------------- tests

#[test]
fn a_hostile_vector_count_is_never_an_allocation_or_a_loop_bound() {
    // The whole message is nine bytes: a compactSize saying 18,446,744,073,
    // 709,551,615 elements follow, and then nothing. A reader that trusts it
    // does not come back.
    let absurd: [u64; 4] = [
        u64::max_value(),
        0x8000_0000_0000_0000,
        0xffff_ffff,
        0x0100_0000,
    ];

    for &claimed in absurd.iter() {
        // One element type per wire shape: a bare 32-byte blob, a fixed 36-byte
        // record, a value plus a script, a variable-length record with its own
        // nested length prefixes, an 8-byte amount, a 4-byte lock time, and a
        // whole transaction. All of them must fail on the first missing
        // element, not after reserving room for the last.
        hostile_count_is_rejected::<block::Hash>("block::Hash", claimed, &[]);
        hostile_count_is_rejected::<OutPoint>("transparent::OutPoint", claimed, &[]);
        hostile_count_is_rejected::<Output>("transparent::Output", claimed, &[]);
        hostile_count_is_rejected::<Input>("transparent::Input", claimed, &[]);
        hostile_count_is_rejected::<Amount<NonNegative>>("Amount<NonNegative>", claimed, &[]);
        hostile_count_is_rejected::<LockTime>("LockTime", claimed, &[]);
        hostile_count_is_rejected::<Transaction>("Transaction", claimed, &[]);
    }

    // The same lie with a few real elements behind it: the reader must consume
    // those and then stop, rather than deciding up front how much room the
    // count deserves.
    let three_hashes = [0xabu8; 96];
    hostile_count_is_rejected::<block::Hash>("block::Hash", u64::max_value(), &three_hashes[..]);
    hostile_count_is_rejected::<block::Hash>("block::Hash", 0xffff_ffff, &three_hashes[..]);
}

#[test]
fn a_vector_reads_exactly_as_many_elements_as_its_count_promises() {
    let mut elements = Vec::new();
    for i in 0..4u8 {
        elements.extend_from_slice(&[i; 32]);
    }

    // Exactly the promised number, and the reader stops on the boundary.
    for &n in [0usize, 1, 2, 4].iter() {
        let mut stream = count_prefix(n as u64);
        stream.extend_from_slice(&elements[..n * 32]);
        let mut cursor = Cursor::new(&stream[..]);
        let hashes: Vec<block::Hash> = (&mut cursor)
            .zcash_deserialize_into()
            .unwrap_or_else(|e| panic!("a vector of {} hashes did not parse: {:?}", n, e));
        assert_eq!(hashes.len(), n, "a vector of {} hashes came back wrong", n);
        for (i, hash) in hashes.iter().enumerate() {
            assert_eq!(
                &hash.0[..],
                &elements[i * 32..(i + 1) * 32],
                "element {} of a {}-element vector",
                i,
                n
            );
        }
        assert_eq!(
            cursor.position() as usize,
            1 + n * 32,
            "a vector of {} hashes consumed the wrong number of bytes",
            n
        );
    }

    // More bytes than the count claims: the surplus belongs to whatever comes
    // next and must be left on the reader.
    let mut stream = count_prefix(2);
    stream.extend_from_slice(&elements[..]);
    let mut cursor = Cursor::new(&stream[..]);
    let hashes: Vec<block::Hash> = (&mut cursor).zcash_deserialize_into().expect("two hashes");
    assert_eq!(hashes.len(), 2);
    assert_eq!(
        cursor.position(),
        65,
        "a two-element vector followed by two more hashes swallowed the surplus"
    );

    // Fewer bytes than the count claims, by a single byte.
    let mut stream = count_prefix(4);
    stream.extend_from_slice(&elements[..127]);
    expect_io_error(
        "a vector of four hashes that is one byte short",
        Vec::<block::Hash>::zcash_deserialize(Cursor::new(&stream[..])),
    );

    // And the same shape for an element type with a different width.
    let mut stream = count_prefix(3);
    for value in [1u64, 2, 3].iter() {
        stream.extend_from_slice(&value.to_le_bytes());
    }
    let mut cursor = Cursor::new(&stream[..]);
    let amounts: Vec<Amount<NonNegative>> =
        (&mut cursor).zcash_deserialize_into().expect("three amounts");
    assert_eq!(amounts.len(), 3);
    assert_eq!(i64::from(amounts[2]), 3);
    assert_eq!(cursor.position(), 25);
}

#[test]
fn a_structurally_invalid_encoding_is_a_parse_error() {
    // The control: the same shapes, well formed, parse.
    let good_coinbase = coinbase_input_bytes(&[0x01, 0x64], 0xffff_ffff, true);
    match Input::zcash_deserialize(Cursor::new(&good_coinbase[..])) {
        Ok(Input::Coinbase { height, .. }) => assert_eq!(height.0, 100),
        other => panic!("a well-formed coinbase input gave {:?}", other),
    }
    let good_prevout = prevout_input_bytes(&[0x51, 0x52]);
    match Input::zcash_deserialize(Cursor::new(&good_prevout[..])) {
        Ok(Input::PrevOut { outpoint, .. }) => assert_eq!(outpoint.index, 7),
        other => panic!("a well-formed transparent input gave {:?}", other),
    }

    // An all-zero previous hash means coinbase, and a coinbase must spend
    // output 0xffffffff. Any other index is a contradiction in the encoding,
    // not a short read.
    expect_parse_error(
        "a coinbase input spending output 0",
        Input::zcash_deserialize(Cursor::new(
            &coinbase_input_bytes(&[0x01, 0x64], 0, true)[..],
        )),
    );

    // Consensus caps the coinbase data at 100 bytes.
    let too_much = vec![0x41u8; 101];
    expect_parse_error(
        "a coinbase input with 101 bytes of data",
        Input::zcash_deserialize(Cursor::new(
            &coinbase_input_bytes(&too_much[..], 0xffff_ffff, true)[..],
        )),
    );

    // The coinbase data begins with a BIP34 height push. Three zero bytes are
    // not any of the push opcodes the encoding allows.
    expect_parse_error(
        "a coinbase input whose data is not a BIP34 height push",
        Input::zcash_deserialize(Cursor::new(
            &coinbase_input_bytes(&[0x00, 0x00, 0x00], 0xffff_ffff, true)[..],
        )),
    );

    // A well-formed four-byte height push of 0x7fffffff, which is above the
    // largest height the chain admits.
    expect_parse_error(
        "a coinbase input claiming height 2147483647",
        Input::zcash_deserialize(Cursor::new(
            &coinbase_input_bytes(&[0x04, 0xff, 0xff, 0xff, 0x7f], 0xffff_ffff, true)[..],
        )),
    );
}

#[test]
fn a_truncated_encoding_is_an_io_error_except_where_script_deliberately_truncates() {
    // Every fixed-width and length-prefixed reader in this band must treat a
    // stream that ends early as an io error: the caller can distinguish "send
    // me the rest of the message" from "this peer is lying".
    let filler = [0x33u8; 128];

    expect_io_error(
        "a block hash one byte short",
        block::Hash::zcash_deserialize(Cursor::new(&filler[..31])),
    );
    expect_io_error(
        "an outpoint one byte short",
        OutPoint::zcash_deserialize(Cursor::new(&filler[..35])),
    );
    expect_io_error(
        "a non-negative amount one byte short",
        Amount::<NonNegative>::zcash_deserialize(Cursor::new(&filler[..7])),
    );
    expect_io_error(
        "a signed amount one byte short",
        Amount::<NegativeAllowed>::zcash_deserialize(Cursor::new(&filler[..7])),
    );
    expect_io_error(
        "a lock time one byte short",
        LockTime::zcash_deserialize(Cursor::new(&filler[..3])),
    );
    expect_io_error(
        "a transaction with three header bytes",
        Transaction::zcash_deserialize(Cursor::new(&filler[..3])),
    );
    expect_io_error(
        "an output whose value is truncated",
        Output::zcash_deserialize(Cursor::new(&filler[..7])),
    );
    // The value has to be a real amount here, otherwise the range check fires
    // first and the failure is an amount error rather than the short read of
    // the lock script that this case is about.
    expect_io_error(
        "an output with no lock script length at all",
        Output::zcash_deserialize(Cursor::new(&1_000u64.to_le_bytes()[..])),
    );

    // Inputs, cut at each internal boundary.
    let prevout = prevout_input_bytes(&[0x51, 0x52]);
    for &cut in [31usize, 32, 34, 36, 37, 38].iter() {
        expect_io_error(
            &format!("a transparent input truncated to {} bytes", cut),
            Input::zcash_deserialize(Cursor::new(&prevout[..cut])),
        );
    }
    let coinbase = coinbase_input_bytes(&[0x01, 0x64], 0xffff_ffff, false);
    expect_io_error(
        "a coinbase input with no sequence number",
        Input::zcash_deserialize(Cursor::new(&coinbase[..])),
    );

    // ---- the one place where truncation is NOT an error
    //
    // `Script::zcash_deserialize` reads its body with
    // `reader.take(len).read_to_end(..)`, which stops at end of input instead
    // of failing. A script whose prefix promises five bytes and whose stream
    // holds two therefore yields a two-byte `Script` and no error at all.
    // That is what the reference implementation does in this band, and it is
    // asserted here deliberately -- not because it is right -- so that the
    // group measures the band's actual contract.
    let short_script = [0x05u8, 0xaa, 0xbb];
    let script = Script::zcash_deserialize(Cursor::new(&short_script[..]))
        .expect("in this band a truncated script is silently shortened, not an error");
    assert_eq!(
        script.0,
        vec![0xaa, 0xbb],
        "a script promising five bytes over a two-byte stream should come back two bytes long"
    );

    // A wildly overlong prefix behaves the same way, and in particular does
    // not pre-allocate: 65535 promised, three delivered.
    let mut greedy = vec![0xfdu8, 0xff, 0xff];
    greedy.extend_from_slice(&[0x01, 0x02, 0x03]);
    // Reaching the next line at all is the no-pre-allocation witness: a reader
    // that reserved 65535 bytes up front would return a longer value, and one
    // that spun on the prefix would never get here.
    let script = Script::zcash_deserialize(Cursor::new(&greedy[..]))
        .expect("an overlong script prefix is not an error in this band either");
    assert_eq!(script.0, vec![0x01, 0x02, 0x03]);

    // And the quirk propagates: the lock script is the last field of an
    // output, so a truncated output script produces a short output rather
    // than an error.
    let mut short_output = 500u64.to_le_bytes().to_vec();
    short_output.extend_from_slice(&[0x05, 0xaa, 0xbb]);
    let output = Output::zcash_deserialize(Cursor::new(&short_output[..]))
        .expect("the script quirk reaches Output too");
    assert_eq!(i64::from(output.value), 500);
    assert_eq!(output.lock_script.0, vec![0xaa, 0xbb]);
}

#[test]
fn an_out_of_range_zatoshi_amount_is_neither_a_parse_nor_an_io_error() {
    // `Amount` has its own error variant because the failure is neither of the
    // other two: the bytes arrived in full and they are a well-formed integer,
    // it is just not a number of zatoshis that can exist.
    let ok_unsigned: [i64; 3] = [0, 1, MAX_MONEY];
    for &value in ok_unsigned.iter() {
        let bytes = (value as u64).to_le_bytes();
        let amount = Amount::<NonNegative>::zcash_deserialize(Cursor::new(&bytes[..]))
            .unwrap_or_else(|e| panic!("{} zatoshis should be a valid amount: {:?}", value, e));
        assert_eq!(i64::from(amount), value);
    }

    let bad_unsigned: [u64; 3] = [
        (MAX_MONEY as u64) + 1,
        0x8000_0000_0000_0000,
        u64::max_value(),
    ];
    for &value in bad_unsigned.iter() {
        expect_amount_error(
            &format!("an unsigned amount of {} zatoshis", value),
            Amount::<NonNegative>::zcash_deserialize(Cursor::new(&value.to_le_bytes()[..])),
        );
    }

    let ok_signed: [i64; 4] = [0, -1, MAX_MONEY, -MAX_MONEY];
    for &value in ok_signed.iter() {
        let amount = Amount::<NegativeAllowed>::zcash_deserialize(Cursor::new(
            &value.to_le_bytes()[..],
        ))
        .unwrap_or_else(|e| panic!("{} zatoshis should be a valid amount: {:?}", value, e));
        assert_eq!(i64::from(amount), value);
    }

    let bad_signed: [i64; 4] = [MAX_MONEY + 1, -MAX_MONEY - 1, i64::max_value(), i64::min_value()];
    for &value in bad_signed.iter() {
        expect_amount_error(
            &format!("a signed amount of {} zatoshis", value),
            Amount::<NegativeAllowed>::zcash_deserialize(Cursor::new(&value.to_le_bytes()[..])),
        );
    }

    // A non-negative amount is read as an unsigned integer, so the same eight
    // bytes mean different things to the two constraints: -1 is a valid
    // `NegativeAllowed` amount and an out-of-range `NonNegative` one.
    let minus_one = (-1i64).to_le_bytes();
    assert_eq!(
        i64::from(
            Amount::<NegativeAllowed>::zcash_deserialize(Cursor::new(&minus_one[..]))
                .expect("-1 is a valid signed amount")
        ),
        -1
    );
    expect_amount_error(
        "the bit pattern of -1 read as a non-negative amount",
        Amount::<NonNegative>::zcash_deserialize(Cursor::new(&minus_one[..])),
    );

    // The value error and the short read are not the same error.
    expect_io_error(
        "an amount that is seven bytes long",
        Amount::<NonNegative>::zcash_deserialize(Cursor::new(&[0u8; 7][..])),
    );
}

#[test]
fn fixed_width_reads_are_exact_and_short_reads_are_io_errors() {
    // 4 + 12 + 32 + 64 = 112, read back to back from one reader.
    let bytes: Vec<u8> = (0..112u32).map(|i| (i % 251) as u8).collect();
    let mut cursor = Cursor::new(&bytes[..]);

    let four = cursor.read_4_bytes().expect("four bytes");
    assert_eq!(&four[..], &bytes[0..4], "read_4_bytes returned the wrong bytes");
    assert_eq!(cursor.position(), 4, "read_4_bytes moved the reader wrongly");

    let twelve = cursor.read_12_bytes().expect("twelve bytes");
    assert_eq!(&twelve[..], &bytes[4..16], "read_12_bytes returned the wrong bytes");
    assert_eq!(cursor.position(), 16, "read_12_bytes moved the reader wrongly");

    let thirty_two = cursor.read_32_bytes().expect("thirty-two bytes");
    assert_eq!(&thirty_two[..], &bytes[16..48], "read_32_bytes returned the wrong bytes");
    assert_eq!(cursor.position(), 48, "read_32_bytes moved the reader wrongly");

    let sixty_four = cursor.read_64_bytes().expect("sixty-four bytes");
    assert_eq!(&sixty_four[..], &bytes[48..112], "read_64_bytes returned the wrong bytes");
    assert_eq!(cursor.position(), 112, "read_64_bytes moved the reader wrongly");

    // Exhausted: the next read of any width fails rather than returning zeros.
    assert!(
        cursor.read_4_bytes().is_err(),
        "reading past the end of the input returned bytes"
    );

    // One byte short of each width. A short read must come back as an error
    // rather than as zero-filled or partly-filled bytes; which `io::ErrorKind`
    // it reports is the implementation's own choice and is not pinned.
    let filler = [0x77u8; 64];
    match Cursor::new(&filler[..3]).read_4_bytes() {
        Err(_) => {}
        Ok(v) => panic!("read_4_bytes over 3 bytes returned {:?}", &v[..]),
    }
    match Cursor::new(&filler[..11]).read_12_bytes() {
        Err(_) => {}
        Ok(v) => panic!("read_12_bytes over 11 bytes returned {:?}", &v[..]),
    }
    match Cursor::new(&filler[..31]).read_32_bytes() {
        Err(_) => {}
        Ok(v) => panic!("read_32_bytes over 31 bytes returned {:?}", &v[..]),
    }
    match Cursor::new(&filler[..63]).read_64_bytes() {
        Err(_) => {}
        Ok(v) => panic!("read_64_bytes over 63 bytes returned {:?}", &v[..]),
    }
    match Cursor::new(&filler[..15]).read_ip_addr() {
        Err(_) => {}
        Ok(v) => panic!("read_ip_addr over 15 bytes returned {:?}", v),
    }
    match Cursor::new(&filler[..17]).read_socket_addr() {
        Err(_) => {}
        Ok(v) => panic!("read_socket_addr over 17 bytes returned {:?}", v),
    }
    expect_io_error(
        "a Bitcoin string whose body is shorter than its length prefix",
        Cursor::new(&[0x08u8, 0x61, 0x62][..]).read_string(),
    );
}

#[test]
fn write_zcash_ext_round_trips_through_read_zcash_ext() {
    // Fixed-width blobs.
    let mut thirty_two = [0u8; 32];
    let mut sixty_four = [0u8; 64];
    for i in 0..32 {
        thirty_two[i] = (i as u8).wrapping_mul(7).wrapping_add(3);
    }
    for i in 0..64 {
        sixty_four[i] = (i as u8).wrapping_mul(5).wrapping_add(11);
    }

    let mut buf = Vec::new();
    buf.write_32_bytes(&thirty_two).expect("a Vec always writes");
    assert_eq!(buf.len(), 32, "write_32_bytes wrote {} bytes", buf.len());
    assert_eq!(&buf[..], &thirty_two[..], "write_32_bytes is not the identity");
    assert_eq!(
        &Cursor::new(&buf[..]).read_32_bytes().expect("read back")[..],
        &thirty_two[..]
    );

    let mut buf = Vec::new();
    buf.write_64_bytes(&sixty_four).expect("a Vec always writes");
    assert_eq!(buf.len(), 64, "write_64_bytes wrote {} bytes", buf.len());
    assert_eq!(&buf[..], &sixty_four[..], "write_64_bytes is not the identity");
    assert_eq!(
        &Cursor::new(&buf[..]).read_64_bytes().expect("read back")[..],
        &sixty_four[..]
    );

    // Bitcoin strings: a compactSize byte count, then UTF-8. The count is a
    // byte count, not a character count, so the emoji below is four bytes.
    let strings = ["", "a", "zcash", "ZEC \u{1f984}"];
    for text in strings.iter() {
        let mut buf = Vec::new();
        buf.write_string(text).expect("a Vec always writes");
        assert_eq!(
            buf.len(),
            1 + text.len(),
            "{:?} should encode as one length byte plus {} body bytes, got {}",
            text,
            text.len(),
            buf.len()
        );
        assert_eq!(
            buf[0] as usize,
            text.len(),
            "{:?}: the length prefix counts bytes",
            text
        );
        let mut cursor = Cursor::new(&buf[..]);
        let back = cursor
            .read_string()
            .unwrap_or_else(|e| panic!("{:?} did not read back: {:?}", text, e));
        assert_eq!(&back, text, "{:?} did not round trip", text);
        assert_eq!(cursor.position() as usize, buf.len());
    }

    // A byte string that is not UTF-8 is a parse error, not an io error: the
    // bytes all arrived, they are just not a string.
    expect_parse_error(
        "a Bitcoin string whose body is not UTF-8",
        Cursor::new(&[0x02u8, 0xff, 0xfe][..]).read_string(),
    );

    // Addresses. Bitcoin writes every address as 16 bytes of IPv6, mapping
    // IPv4 into ::ffff:a.b.c.d, and writes the port big-endian.
    let v4 = IpAddr::V4(Ipv4Addr::new(8, 8, 4, 4));
    let mut buf = Vec::new();
    buf.write_ip_addr(v4).expect("a Vec always writes");
    assert_eq!(buf.len(), 16, "an address is 16 bytes on the wire");
    assert_eq!(
        &buf[..12],
        &[0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff][..],
        "IPv4 must be written as an IPv4-mapped IPv6 address"
    );
    assert_eq!(&buf[12..], &[8, 8, 4, 4][..]);
    assert_eq!(
        Cursor::new(&buf[..]).read_ip_addr().expect("read back"),
        v4,
        "an IPv4-mapped address must come back as IPv4"
    );

    let v6 = IpAddr::V6(Ipv6Addr::new(0x2001, 0x0db8, 0, 0, 0, 0, 0, 1));
    let mut buf = Vec::new();
    buf.write_ip_addr(v6).expect("a Vec always writes");
    assert_eq!(buf.len(), 16);
    assert_eq!(
        Cursor::new(&buf[..]).read_ip_addr().expect("read back"),
        v6,
        "an IPv6 address must not be collapsed to IPv4"
    );

    for &(addr, port) in [(v4, 8233u16), (v6, 18233u16)].iter() {
        let socket = SocketAddr::new(addr, port);
        let mut buf = Vec::new();
        buf.write_socket_addr(socket).expect("a Vec always writes");
        assert_eq!(buf.len(), 18, "a socket address is 16 + 2 bytes");
        assert_eq!(
            &buf[16..],
            &port.to_be_bytes()[..],
            "the port is big-endian on the wire, unlike every other integer"
        );
        assert_eq!(
            Cursor::new(&buf[..]).read_socket_addr().expect("read back"),
            socket,
            "{} did not round trip",
            socket
        );
    }
}

#[test]
fn zcash_deserialize_into_leaves_the_reader_where_the_object_ended() {
    // Consensus objects are concatenated with nothing between them -- a
    // transaction is followed immediately by the next transaction, a script by
    // a sequence number -- so every reader has to stop on its own boundary.
    // One stream, seven objects of five different shapes, and a sentinel that
    // must survive all of them.
    let hash_bytes = [0x5au8; 32];
    let script_body = [0x76u8, 0xa9, 0x14, 0x88, 0xac];
    let sentinel = [0xde_u8, 0xad, 0xbe, 0xef];

    let mut stream = Vec::new();
    let mut offsets = Vec::new();

    stream.extend_from_slice(&hash_bytes); // block::Hash
    offsets.push(stream.len()); // 32

    stream.extend_from_slice(&12_345u64.to_le_bytes()); // Amount<NonNegative>
    offsets.push(stream.len()); // 40

    stream.extend_from_slice(&7u32.to_le_bytes()); // LockTime
    offsets.push(stream.len()); // 44

    stream.extend_from_slice(&[0x11u8; 32]); // OutPoint: hash
    stream.extend_from_slice(&3u32.to_le_bytes()); // OutPoint: index
    offsets.push(stream.len()); // 80

    stream.extend_from_slice(&count_prefix(script_body.len() as u64)); // Script
    stream.extend_from_slice(&script_body);
    offsets.push(stream.len()); // 86

    stream.extend_from_slice(&999u64.to_le_bytes()); // Output: value
    stream.extend_from_slice(&count_prefix(2)); // Output: lock script
    stream.extend_from_slice(&[0x51, 0x52]);
    offsets.push(stream.len()); // 97

    stream.extend_from_slice(&count_prefix(2)); // Vec<block::Hash>
    stream.extend_from_slice(&[0x01u8; 32]);
    stream.extend_from_slice(&[0x02u8; 32]);
    offsets.push(stream.len()); // 162

    stream.extend_from_slice(&sentinel);

    assert_eq!(
        offsets,
        vec![32usize, 40, 44, 80, 86, 97, 162],
        "the test built the wrong stream"
    );

    let mut cursor = Cursor::new(&stream[..]);

    let hash: block::Hash = (&mut cursor).zcash_deserialize_into().expect("hash");
    assert_eq!(&hash.0[..], &hash_bytes[..]);
    assert_eq!(cursor.position() as usize, offsets[0], "after block::Hash");

    let amount: Amount<NonNegative> = (&mut cursor).zcash_deserialize_into().expect("amount");
    assert_eq!(i64::from(amount), 12_345);
    assert_eq!(cursor.position() as usize, offsets[1], "after Amount");

    let lock_time: LockTime = (&mut cursor).zcash_deserialize_into().expect("lock time");
    match lock_time {
        LockTime::Height(h) => assert_eq!(h.0, 7),
        other => panic!("7 should be a block height lock time, got {:?}", other),
    }
    assert_eq!(cursor.position() as usize, offsets[2], "after LockTime");

    let outpoint: OutPoint = (&mut cursor).zcash_deserialize_into().expect("outpoint");
    assert_eq!(outpoint.index, 3);
    assert_eq!(cursor.position() as usize, offsets[3], "after OutPoint");

    let script: Script = (&mut cursor).zcash_deserialize_into().expect("script");
    assert_eq!(script.0, script_body.to_vec());
    assert_eq!(cursor.position() as usize, offsets[4], "after Script");

    let output: Output = (&mut cursor).zcash_deserialize_into().expect("output");
    assert_eq!(i64::from(output.value), 999);
    assert_eq!(output.lock_script.0, vec![0x51, 0x52]);
    assert_eq!(cursor.position() as usize, offsets[5], "after Output");

    let hashes: Vec<block::Hash> = (&mut cursor).zcash_deserialize_into().expect("hashes");
    assert_eq!(hashes.len(), 2);
    assert_eq!(cursor.position() as usize, offsets[6], "after Vec<block::Hash>");

    // The sentinel is untouched, which is the whole point.
    let tail: [u8; 4] = stream[offsets[6]..]
        .try_into()
        .expect("four bytes of sentinel");
    assert_eq!(tail, sentinel);
    let read_back = cursor.read_4_bytes().expect("the sentinel is still there");
    assert_eq!(
        &read_back[..],
        &sentinel[..],
        "the readers above consumed part of the sentinel"
    );
    assert_eq!(cursor.position() as usize, stream.len());

    // Round tripping the pieces that own their encoding reproduces the stream
    // they came from, so the positions above were not luck.
    let mut again = Vec::new();
    script.zcash_serialize(&mut again).expect("a Vec always writes");
    assert_eq!(&again[..], &stream[offsets[3]..offsets[4]]);
    let mut again = Vec::new();
    output.zcash_serialize(&mut again).expect("a Vec always writes");
    assert_eq!(&again[..], &stream[offsets[4]..offsets[5]]);
}
