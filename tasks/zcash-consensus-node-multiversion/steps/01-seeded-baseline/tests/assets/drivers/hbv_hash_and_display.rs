//! Identifiers, and the byte order they are shown in.
//!
//! Harbor verifier driver -- injected into `zebra-chain/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! Bitcoin's u256 convention, which Zcash inherited, is that a block or
//! transaction hash is *stored* in the order SHA256d produced it and
//! *displayed* in the opposite order. Every block explorer, every RPC reply
//! and every line of log output uses the reversed form; every byte on the wire
//! and every byte fed back into a hash function uses the unreversed one. A
//! node that confuses the two is not subtly wrong: it computes a
//! `previous_block_hash` that matches nothing, and it follows no chain at all.
//!
//! So this group checks the convention from both ends. `block::Hash` is
//! recomputed here with `sha2` over the 1487 header bytes and `transaction::Hash`
//! over each transaction's own bytes, so "the hash is right" is a claim about
//! SHA256d rather than a restatement of whatever the crate computed. Display,
//! `Debug` and `FromStr` are checked against the corpus TSV's reversed hex.
//! The seventeen blocks are then chained to each other by hash value across
//! the six consecutive pairs the corpus contains. And the trap gets a test of
//! its own: `zcash_serialize` of a `block::Hash` writes the *unreversed*
//! bytes, so the serialized form and the printed form of the same hash are
//! byte-for-byte opposites.

use std::convert::TryInto;
use std::io::{Cursor, Write};
use std::str::FromStr;

use sha2::{Digest, Sha256};

use zebra_chain::block::{Block, Hash, Height};
use zebra_chain::serialization::{sha256d, SerializationError, ZcashDeserialize, ZcashSerialize};
use zebra_chain::transaction;

// ---------------------------------------------------------------- fixtures
//
// The corpus is seventeen real blocks from the Zcash mainnet and testnet,
// spanning the genesis block and every network upgrade activation height up to
// Canopy, shipped as hex alongside a TSV of the values a correct
// implementation must reproduce. Every expected value in the TSV was derived
// from the block bytes themselves -- the block hash by SHA256d over the
// 1487-byte header, the transaction count by a compactSize reader -- and not
// from any Rust implementation of this interface.

use std::collections::BTreeMap;
use std::path::PathBuf;

#[allow(dead_code)]
pub struct Fixture {
    pub name: String,
    pub network: String,
    pub height: u32,
    /// The block's length in bytes, as recorded in the TSV.
    pub size: usize,
    pub block_hash: String,
    pub previous_block_hash: String,
    pub merkle_root: [u8; 32],
    pub root_bytes: [u8; 32],
    pub version: u32,
    pub time: i64,
    pub difficulty_threshold: u32,
    pub nonce: [u8; 32],
    pub tx_count: usize,
    pub note: String,
    pub raw: Vec<u8>,
}

fn fixture_dir() -> PathBuf {
    PathBuf::from(
        std::env::var("HBV_FIXTURES")
            .expect("HBV_FIXTURES must point at the shipped fixture directory"),
    )
}

fn cell<'a>(row: &BTreeMap<&'a str, &'a str>, key: &str) -> &'a str {
    *row.get(key)
        .unwrap_or_else(|| panic!("blocks.tsv has no column {}", key))
}

fn thirty_two(hex_str: &str) -> [u8; 32] {
    let bytes = hex::decode(hex_str).expect("fixture field is hex");
    assert_eq!(bytes.len(), 32, "fixture field is 32 bytes");
    let mut out = [0u8; 32];
    out.copy_from_slice(&bytes);
    out
}

/// Load the whole block corpus, in the order the TSV lists it.
#[allow(dead_code)]
pub fn blocks() -> Vec<Fixture> {
    let dir = fixture_dir();
    let tsv = std::fs::read_to_string(dir.join("blocks.tsv")).expect("blocks.tsv is readable");
    let mut lines = tsv.lines();
    let header: Vec<&str> = lines
        .next()
        .expect("blocks.tsv has a header row")
        .trim_start_matches("# ")
        .split('\t')
        .collect();

    let mut out = Vec::new();
    for line in lines {
        if line.is_empty() {
            continue;
        }
        let cells: Vec<&str> = line.split('\t').collect();
        assert_eq!(cells.len(), header.len(), "malformed row: {}", line);
        let row: BTreeMap<&str, &str> = header.iter().cloned().zip(cells).collect();

        let name = cell(&row, "name").to_string();
        let hex_text = std::fs::read_to_string(dir.join("blocks").join(format!("{}.hex", name)))
            .unwrap_or_else(|e| panic!("cannot read block {}: {}", name, e));
        let raw = hex::decode(hex_text.trim()).expect("block fixture is hex");

        out.push(Fixture {
            name,
            raw,
            network: cell(&row, "network").to_string(),
            height: cell(&row, "height").parse().expect("height"),
            size: cell(&row, "bytes").parse().expect("bytes"),
            block_hash: cell(&row, "block_hash").to_string(),
            previous_block_hash: cell(&row, "previous_block_hash").to_string(),
            merkle_root: thirty_two(cell(&row, "merkle_root_hex")),
            root_bytes: thirty_two(cell(&row, "root_bytes_hex")),
            version: cell(&row, "version").parse().expect("version"),
            time: cell(&row, "time").parse().expect("time"),
            difficulty_threshold: cell(&row, "difficulty_threshold")
                .parse()
                .expect("difficulty_threshold"),
            nonce: thirty_two(cell(&row, "nonce_hex")),
            tx_count: cell(&row, "tx_count").parse().expect("tx_count"),
            note: cell(&row, "note").to_string(),
        });
    }
    assert!(
        out.len() >= 17,
        "the shipped corpus has {} blocks; the verifier expects at least 17",
        out.len()
    );
    out
}

// ----------------------------------------------------------------- helpers

const HEADER_BYTES: usize = 1487;

/// Transactions across the whole corpus, counted in Python from the corpus
/// bytes. Stated here so that a per-transaction assertion cannot pass by
/// examining nothing.
const CORPUS_TRANSACTIONS: usize = 72;

fn parse(fixture: &Fixture) -> Block {
    Block::zcash_deserialize(Cursor::new(&fixture.raw[..]))
        .unwrap_or_else(|e| panic!("{} did not parse: {:?}", fixture.name, e))
}

fn serialize<T: ZcashSerialize>(value: &T) -> Vec<u8> {
    let mut out = Vec::new();
    value
        .zcash_serialize(&mut out)
        .expect("writing to a Vec is infallible");
    out
}

/// SHA256d -- two rounds of SHA-256 -- computed here with `sha2` directly, so
/// that "the block hash is the hash of the header" is a claim about the bytes
/// and not about the crate's own hash writer.
fn sha256d(bytes: &[u8]) -> [u8; 32] {
    let once = Sha256::digest(bytes);
    let twice = Sha256::digest(&once);
    let mut out = [0u8; 32];
    out.copy_from_slice(&twice[..]);
    out
}

/// The display convention: hex of the bytes in the reverse of wire order.
fn reversed_hex(bytes: &[u8; 32]) -> String {
    let mut reversed = *bytes;
    reversed.reverse();
    hex::encode(&reversed[..])
}

/// A compactSize reader that shares no code with the interface under test.
fn read_compactsize(bytes: &[u8], at: usize) -> (u64, usize) {
    match bytes[at] {
        n @ 0x00..=0xfc => (u64::from(n), 1),
        0xfd => (
            u64::from(u16::from_le_bytes(bytes[at + 1..at + 3].try_into().unwrap())),
            3,
        ),
        0xfe => (
            u64::from(u32::from_le_bytes(bytes[at + 1..at + 5].try_into().unwrap())),
            5,
        ),
        _ => (
            u64::from_le_bytes(bytes[at + 1..at + 9].try_into().unwrap()),
            9,
        ),
    }
}

/// The `[start, end)` byte range of every transaction in the block, found by
/// walking forward from the transaction count.
fn transaction_ranges(fixture: &Fixture, block: &Block) -> Vec<(usize, usize)> {
    let (count, used) = read_compactsize(&fixture.raw, HEADER_BYTES);
    assert_eq!(
        count as usize, fixture.tx_count,
        "{}: the corpus bytes and the corpus TSV disagree about the transaction count",
        fixture.name
    );
    let mut at = HEADER_BYTES + used;
    let mut out = Vec::new();
    for (i, tx) in block.transactions.iter().enumerate() {
        let bytes = serialize(&**tx);
        let end = at + bytes.len();
        assert!(
            end <= fixture.raw.len(),
            "{}: transaction {} re-serialized past the end of the block",
            fixture.name,
            i
        );
        assert_eq!(
            &fixture.raw[at..end],
            &bytes[..],
            "{}: transaction {} does not re-serialize to the bytes at offset {}",
            fixture.name,
            i,
            at
        );
        out.push((at, end));
        at = end;
    }
    assert_eq!(
        at,
        fixture.raw.len(),
        "{}: the transactions account for {} of {} bytes",
        fixture.name,
        at,
        fixture.raw.len()
    );
    out
}

// ------------------------------------------------------------------- tests

#[test]
fn every_block_hash_is_sha256d_of_the_1487_header_bytes() {
    for fixture in blocks() {
        let block = parse(&fixture);

        // Recomputed here, from the block's own first 1487 bytes.
        let expected = sha256d(&fixture.raw[..HEADER_BYTES]);
        assert_eq!(
            hex::encode(&block.hash().0[..]),
            hex::encode(&expected[..]),
            "{}: the block hash is not SHA256d of the header bytes",
            fixture.name
        );
        // ... and matches the identifier the rest of the network uses, which
        // the TSV carries in display order.
        assert_eq!(
            block.hash().to_string(),
            fixture.block_hash,
            "{}: the block hash does not match the chain",
            fixture.name
        );
        assert_eq!(
            reversed_hex(&expected),
            fixture.block_hash,
            "{}: the corpus TSV disagrees with the corpus bytes",
            fixture.name
        );

        // The hash binds the header and nothing else: hashing the header
        // alone, the block, or the whole block's bytes truncated to the header
        // must all agree.
        assert_eq!(
            Hash::from(&block.header),
            block.hash(),
            "{}: hashing the header gave a different hash from hashing the block",
            fixture.name
        );
        assert_eq!(
            sha256d(&serialize(&block.header)),
            expected,
            "{}: the re-serialized header does not hash to the block hash",
            fixture.name
        );

        // The crate ships its own SHA256d helpers, and every other test in
        // this step only ever compiles them. Run them here, against the same
        // 1487 bytes and the same independently computed digest: the `Write`
        // adapter must finish to that digest, and the 4-byte `Checksum` must
        // be its first four bytes -- not its last four, and not the first four
        // of a single round of SHA-256.
        let mut writer = sha256d::Writer::default();
        writer
            .write_all(&fixture.raw[..HEADER_BYTES])
            .expect("writing to a sha256d::Writer is infallible");
        assert_eq!(
            writer.finish(),
            expected,
            "{}: sha256d::Writer does not produce SHA256d of what was written",
            fixture.name
        );
        assert_eq!(
            &sha256d::Checksum::from(&fixture.raw[..HEADER_BYTES]).0[..],
            &expected[..4],
            "{}: sha256d::Checksum is not the first four bytes of the SHA256d digest",
            fixture.name
        );
    }

    // The two genesis blocks share a coinbase, a merkle root and a version,
    // and differ only in the header fields the proof of work covers -- so
    // hashing the header, not the transactions, is what tells them apart.
    let corpus = blocks();
    let main = corpus
        .iter()
        .find(|f| f.name == "main-0000000")
        .expect("the corpus ships main-0000000");
    let test = corpus
        .iter()
        .find(|f| f.name == "test-0000000")
        .expect("the corpus ships test-0000000");
    assert_eq!(
        main.merkle_root, test.merkle_root,
        "the genesis blocks should share a merkle root"
    );
    assert_ne!(
        parse(main).hash(),
        parse(test).hash(),
        "the mainnet and testnet genesis blocks must not share a hash"
    );
}

#[test]
fn block_hash_display_and_debug_reverse_the_wire_bytes() {
    for fixture in blocks() {
        let hash = parse(&fixture).hash();
        let wire = hex::encode(&hash.0[..]);
        let shown = format!("{}", hash);

        assert_eq!(
            shown.len(),
            64,
            "{}: a hash prints as 64 hex characters, this one printed {}: {}",
            fixture.name,
            shown.len(),
            shown
        );
        assert_eq!(
            shown, fixture.block_hash,
            "{}: Display does not print the hash the way the chain names it",
            fixture.name
        );
        assert_eq!(
            shown,
            reversed_hex(&hash.0),
            "{}: Display is not the byte reversal of the stored bytes",
            fixture.name
        );
        // Every corpus hash has leading zero bytes in display order from the
        // proof of work, so the reversal is never a no-op and this really does
        // distinguish the two orders.
        assert_ne!(
            shown, wire,
            "{}: Display printed the bytes in wire order",
            fixture.name
        );
        assert!(
            shown.starts_with("00") || shown.starts_with("01") || shown.starts_with("05"),
            "{}: a block hash should have a small leading byte, this one is {}",
            fixture.name,
            shown
        );

        // Debug names the type and quotes the same reversed hex.
        assert_eq!(
            format!("{:?}", hash),
            format!("block::Hash(\"{}\")", fixture.block_hash),
            "{}: Debug should name the type and print in display order",
            fixture.name
        );

        // And the header's parent pointer prints the same way.
        let previous = parse(&fixture).header.previous_block_hash;
        assert_eq!(
            previous.to_string(),
            fixture.previous_block_hash,
            "{}: previous_block_hash does not print the parent the chain names",
            fixture.name
        );
    }
}

#[test]
fn block_hash_parses_back_from_the_order_it_prints_in() {
    for fixture in blocks() {
        let block = parse(&fixture);

        let from_tsv = Hash::from_str(&fixture.block_hash)
            .unwrap_or_else(|e| panic!("{}: block_hash did not parse: {:?}", fixture.name, e));
        assert_eq!(
            from_tsv,
            block.hash(),
            "{}: parsing the displayed hash gave {:?}",
            fixture.name,
            from_tsv
        );

        let parent: Hash = fixture.previous_block_hash.parse().unwrap_or_else(|e| {
            panic!(
                "{}: previous_block_hash did not parse: {:?}",
                fixture.name, e
            )
        });
        assert_eq!(
            parent, block.header.previous_block_hash,
            "{}: parsing the displayed parent hash gave {:?}",
            fixture.name, parent
        );

        // Display and FromStr are inverse in both directions.
        assert_eq!(
            block.hash().to_string().parse::<Hash>().unwrap(),
            block.hash(),
            "{}: Display then FromStr is not the identity",
            fixture.name
        );
    }

    // FromStr takes exactly 64 hex characters and nothing else.
    let sample = blocks().remove(0).block_hash;
    for bad in [
        String::new(),
        sample[..63].to_string(),
        sample[..62].to_string(),
        format!("{}00", sample),
        format!("0x{}", &sample[2..]),
        sample.replace('0', "g"),
    ]
    .iter()
    {
        match Hash::from_str(bad) {
            Err(SerializationError::Parse(_)) => {}
            other => panic!(
                "{:?} is not a hash, but parsing it gave {:?}",
                bad,
                other.map(|h| h.to_string())
            ),
        }
    }
}

#[test]
fn block_hash_serialization_uses_wire_order_not_display_order() {
    // This is the trap. `Display` reverses; `zcash_serialize` does not. A hash
    // written to disk or to a peer in display order would be a different hash,
    // and it would still print as something that looks like a plausible block
    // identifier, so nothing downstream would notice.
    for fixture in blocks() {
        let block = parse(&fixture);
        let hash = block.hash();

        let written = serialize(&hash);
        assert_eq!(
            written.len(),
            32,
            "{}: a hash serializes to 32 bytes, this one wrote {}",
            fixture.name,
            written.len()
        );
        assert_eq!(
            &written[..],
            &hash.0[..],
            "{}: serialization did not write the stored bytes",
            fixture.name
        );

        let display_order = hex::decode(&fixture.block_hash).expect("the TSV column is hex");
        let mut wire_order = display_order.clone();
        wire_order.reverse();
        assert_eq!(
            hex::encode(&written),
            hex::encode(&wire_order),
            "{}: serialization wrote {} but the wire order is {}",
            fixture.name,
            hex::encode(&written),
            hex::encode(&wire_order)
        );
        assert_ne!(
            written, display_order,
            "{}: serialization wrote the hash in display order",
            fixture.name
        );

        // The round trip is the identity...
        let back = Hash::zcash_deserialize(Cursor::new(&written[..]))
            .unwrap_or_else(|e| panic!("{}: a hash did not re-parse: {:?}", fixture.name, e));
        assert_eq!(back, hash, "{}: a hash did not round trip", fixture.name);

        // ... and feeding it the display-order bytes instead silently yields a
        // different, well-formed hash -- which is exactly why this has to be
        // checked rather than assumed.
        let mistaken = Hash::zcash_deserialize(Cursor::new(&display_order[..]))
            .expect("any 32 bytes are a well-formed hash");
        assert_ne!(
            mistaken, hash,
            "{}: reversing the bytes should give a different hash",
            fixture.name
        );
        assert_eq!(
            mistaken.to_string(),
            hex::encode(&hash.0[..]),
            "{}: the mistaken hash should print as the real one's wire order",
            fixture.name
        );

        // The parent pointer sits in the header in wire order too, at bytes
        // 4..36 of the block.
        assert_eq!(
            &block.header.previous_block_hash.0[..],
            &fixture.raw[4..36],
            "{}: previous_block_hash is not stored in the header's own byte order",
            fixture.name
        );
    }
}

#[test]
fn previous_block_hash_links_parents_to_children_as_hash_values() {
    // The chain is made of these links and nothing else. Comparing `Hash`
    // values rather than strings means neither side of the equality goes
    // through the display convention, so this passes only if the child's
    // header really does carry the SHA256d of the parent's header.
    let corpus = blocks();
    let parsed: Vec<Block> = corpus.iter().map(|f| parse(f)).collect();

    let mut links: Vec<(String, String)> = Vec::new();
    for (p, parent) in corpus.iter().enumerate() {
        for (c, child) in corpus.iter().enumerate() {
            if parent.network != child.network || child.height != parent.height + 1 {
                continue;
            }
            assert_eq!(
                parsed[c].header.previous_block_hash,
                parsed[p].hash(),
                "{} should name {} as its parent, but points at {}",
                child.name,
                parent.name,
                parsed[c].header.previous_block_hash
            );
            links.push((parent.name.clone(), child.name.clone()));
        }
    }

    assert!(
        links.len() >= 3,
        "the corpus should contain at least three parent/child pairs, found {:?}",
        links
    );
    for &(parent, child) in [
        ("main-0347499", "main-0347500"),
        ("main-0419199", "main-0419200"),
        ("main-0903000", "main-0903001"),
    ]
    .iter()
    {
        assert!(
            links
                .iter()
                .any(|l| l.0 == parent && l.1 == child),
            "the corpus should link {} to {}, but only found {:?}",
            parent,
            child,
            links
        );
    }

    // Both genesis blocks name the null hash as their parent, and it prints as
    // sixty-four zeroes in either order.
    for (i, fixture) in corpus.iter().enumerate() {
        if fixture.height == 0 {
            assert_eq!(
                parsed[i].header.previous_block_hash.0,
                [0u8; 32],
                "{}: genesis should point at the null hash",
                fixture.name
            );
            assert_eq!(
                parsed[i].header.previous_block_hash.to_string(),
                fixture.previous_block_hash,
                "{}: genesis parent hash",
                fixture.name
            );
        }
    }
}

#[test]
fn every_transaction_hash_is_sha256d_of_its_bytes_and_prints_reversed() {
    let mut seen = 0usize;
    let mut all: Vec<(String, String)> = Vec::new();

    for fixture in blocks() {
        let block = parse(&fixture);
        let ranges = transaction_ranges(&fixture, &block);
        let mut in_this_block: Vec<String> = Vec::new();

        for (i, tx) in block.transactions.iter().enumerate() {
            // The txid is SHA256d over the transaction's bytes exactly as they
            // appear in the block, recomputed here.
            let expected = sha256d(&fixture.raw[ranges[i].0..ranges[i].1]);
            let hash = tx.hash();
            assert_eq!(
                hex::encode(&hash.0[..]),
                hex::encode(&expected[..]),
                "{}: transaction {} does not hash to SHA256d of its own {} bytes",
                fixture.name,
                i,
                ranges[i].1 - ranges[i].0
            );

            let shown = hash.to_string();
            assert_eq!(
                shown,
                reversed_hex(&hash.0),
                "{}: transaction {} does not display in reversed order",
                fixture.name,
                i
            );
            assert_eq!(
                format!("{:?}", hash),
                format!("transaction::Hash(\"{}\")", shown),
                "{}: transaction {} Debug should name the type",
                fixture.name,
                i
            );
            let back = transaction::Hash::from_str(&shown).unwrap_or_else(|e| {
                panic!(
                    "{}: transaction {} txid {} did not parse: {:?}",
                    fixture.name, i, shown, e
                )
            });
            assert_eq!(
                back, hash,
                "{}: transaction {} txid did not survive Display then FromStr",
                fixture.name, i
            );

            assert!(
                !in_this_block.contains(&shown),
                "{}: transaction {} has the same txid as an earlier transaction in the same \
                 block: {}",
                fixture.name,
                i,
                shown
            );
            in_this_block.push(shown.clone());
            all.push((fixture.name.clone(), shown));
            seen += 1;
        }
    }

    assert_eq!(
        seen, CORPUS_TRANSACTIONS,
        "the corpus holds {} transactions, but {} were hashed",
        CORPUS_TRANSACTIONS, seen
    );

    // Across the whole corpus exactly one txid occurs twice, and it is not an
    // accident: Zcash gave the mainnet and testnet genesis blocks the same
    // coinbase transaction, byte for byte, which is why the two blocks share a
    // merkle root while having different block hashes. Any other collision
    // would mean two distinct transactions hashed alike.
    let mut sorted: Vec<String> = all.iter().map(|pair| pair.1.clone()).collect();
    sorted.sort();
    let mut repeated: Vec<String> = Vec::new();
    for window in sorted.windows(2) {
        if window[0] == window[1] && !repeated.contains(&window[0]) {
            repeated.push(window[0].clone());
        }
    }
    assert_eq!(
        repeated.len(),
        1,
        "exactly one txid should occur twice in the corpus, but these do: {:?}",
        repeated
    );

    let genesis_txids: Vec<String> = all
        .iter()
        .filter(|pair| pair.0 == "main-0000000" || pair.0 == "test-0000000")
        .map(|pair| pair.1.clone())
        .collect();
    assert_eq!(
        genesis_txids.len(),
        2,
        "the corpus should ship both single-transaction genesis blocks"
    );
    assert_eq!(
        genesis_txids[0], genesis_txids[1],
        "the two genesis blocks should share a coinbase transaction"
    );
    assert_eq!(
        repeated[0], genesis_txids[0],
        "the repeated txid should be the genesis coinbase, not {}",
        repeated[0]
    );
}

#[test]
fn height_is_debugged_and_parsed_as_a_plain_decimal_number() {
    // A height is a number, not a hash, so it has no byte order to get wrong
    // -- but it does have a maximum, because `LockTime` gives the same 32-bit
    // field two meanings and 500,000,000 is the boundary between them.
    for fixture in blocks() {
        let height = parse(&fixture)
            .coinbase_height()
            .unwrap_or_else(|| panic!("{}: no coinbase height", fixture.name));
        assert_eq!(
            height,
            Height(fixture.height),
            "{}: coinbase height",
            fixture.name
        );
        assert_eq!(
            format!("{:?}", height),
            format!("Height({})", fixture.height),
            "{}: Debug should name the type and the decimal height",
            fixture.name
        );
        let round_tripped = Height::from_str(&fixture.height.to_string()).unwrap_or_else(|e| {
            panic!("{}: {} did not parse as a height: {:?}", fixture.name, fixture.height, e)
        });
        assert_eq!(
            round_tripped, height,
            "{}: a height did not survive being written and read back",
            fixture.name
        );
    }

    assert_eq!(Height::MIN, Height(0), "the minimum height is genesis");
    assert_eq!(
        Height::MAX,
        Height(499_999_999),
        "the maximum height is one below the LockTime boundary"
    );
    assert_eq!(
        Height::from_str("499999999").expect("the maximum height parses"),
        Height::MAX
    );
    assert_eq!(Height::MAX_AS_U32, Height::MAX.0);

    for bad in ["500000000", "4294967295", "-1", "", " 1", "1 ", "0x10", "ten"].iter() {
        assert!(
            Height::from_str(bad).is_err(),
            "{:?} is not a valid height, but it parsed as {:?}",
            bad,
            Height::from_str(bad).ok()
        );
    }

    // Heights order and subtract as numbers.
    assert!(Height(347_499) < Height(347_500));
    assert_eq!(Height(347_500) - Height(347_499), 1);
    assert_eq!(Height(347_499) + 1, Some(Height(347_500)));
    assert_eq!(Height::MAX + 1, None, "there is no height above the maximum");

    // The other two arithmetic impls. Note the output types differ: adding two
    // heights is fallible and yields `Option<Height>`, while subtracting two
    // heights yields a bare `i32` (checked just above). Both of the fallible
    // ones saturate to `None` at the range edge rather than wrapping.
    assert_eq!(
        Height(347_499) + Height(1),
        Some(Height(347_500)),
        "adding two heights should add their numbers"
    );
    assert_eq!(
        Height::MAX + Height(0),
        Some(Height::MAX),
        "adding zero should stay at the maximum, not overflow past it"
    );
    assert_eq!(
        Height::MAX + Height(1),
        None,
        "there is no height above the maximum, however it is reached"
    );
    assert_eq!(
        Height(347_500) - 1,
        Some(Height(347_499)),
        "subtracting an i32 should subtract its number"
    );
    assert_eq!(
        Height(347_490) - -10,
        Some(Height(347_500)),
        "subtracting a negative i32 should move the height up"
    );
    assert_eq!(
        Height::MIN - 1,
        None,
        "there is no height below genesis"
    );
}

#[test]
fn block_display_names_the_height_and_the_hash_in_display_order() {
    // `Block`'s `Display` is what ends up in a log line when a node accepts or
    // rejects a block, so it has to name the two things an operator needs: the
    // height from the coinbase, and the hash in the order every explorer uses.
    for fixture in blocks() {
        let block = parse(&fixture);
        let shown = format!("{}", block);
        assert_eq!(
            shown,
            format!(
                "Block {{ height: Height({}), hash: {} }}",
                fixture.height, fixture.block_hash
            ),
            "{}: Display should name the height and the hash",
            fixture.name
        );
        // The hash appears unquoted and in display order, never in wire order.
        assert!(
            !shown.contains(&hex::encode(&block.hash().0[..])),
            "{}: Display printed the hash in wire order: {}",
            fixture.name,
            shown
        );
    }
}
