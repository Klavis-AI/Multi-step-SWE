//! `HistoryTree` -- the runtime Merkle Mountain Range this milestone adds.
//!
//! Harbor verifier driver -- injected into `zebra-chain/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! ZIP-221's chain history root has been decodable out of a block header
//! since an earlier milestone (`Block::commitment` returning
//! `Commitment::ChainHistoryRoot(ChainHistoryMmrRootHash)`, and
//! `impl From<[u8; 32]> for ChainHistoryMmrRootHash`) -- but nothing before
//! this milestone could *build* that root from block data. This milestone
//! adds `zebra_chain::history_tree::HistoryTree`, the runtime structure that
//! does exactly that: seed it with the network-upgrade activation block via
//! `HistoryTree::from_block`, extend it one block at a time with `push` (or
//! several at once with `try_extend`), and read the committed root back out
//! with `hash`.
//!
//! This driver does not re-derive the Merkle Mountain Range algorithm itself
//! (that stays inside the pinned `zcash_history`/`primitives::zcash_history`
//! bridge, out of scope here, the same way signature and proof internals are
//! out of scope elsewhere in this corpus). Instead it takes the one
//! independently-verifiable fact real chain data hands us for free: the
//! block immediately after Heartwood activation (mainnet height 903,001)
//! commits, in its own header, to the chain history root of a tree
//! containing exactly one entry -- the activation block itself (mainnet
//! height 903,000). That expected root is already in this corpus's own
//! `blocks.tsv` (`root_bytes_hex` for `main-0903001`), sourced from a mined
//! block, not computed by this driver. The only fixture data this group adds
//! is `history_tree.tsv`, which supplies the one input a caller must have
//! that a block's own bytes do not carry: each block's Sapling final root,
//! passed to `from_block`/`push` as a separate argument (the activation
//! block's own header field is ZIP-221's reserved placeholder, not a usable
//! root -- see `blocks.tsv`'s all-zero `root_bytes_hex` for `main-0903000`).
//! Those two Sapling roots are real historical values (as printed by a full
//! node's `getblock <height> 1` RPC, `finalsaplingroot` field), byte-reversed
//! into wire order the same way `blocks.tsv`'s own hash columns are;
//! `history_tree.tsv` records nothing this driver or the crate computed.
//!
//! `HistoryTree`'s fields are all private; every test below drives it
//! through `from_block`, `push`, `try_extend`, `hash`, and `Clone` only.

use std::collections::BTreeMap;
use std::io::Cursor;
use std::panic::{self, AssertUnwindSafe};
use std::path::PathBuf;
use std::sync::Arc;

use zebra_chain::block::{Block, ChainHistoryMmrRootHash, Commitment};
use zebra_chain::history_tree::HistoryTree;
use zebra_chain::parameters::Network;
use zebra_chain::sapling;
use zebra_chain::serialization::ZcashDeserialize;

// ---------------------------------------------------------------- fixtures

fn fixture_dir() -> PathBuf {
    PathBuf::from(
        std::env::var("HBV_FIXTURES")
            .expect("HBV_FIXTURES must point at the shipped fixture directory"),
    )
}

fn cell<'a>(row: &BTreeMap<&'a str, &'a str>, key: &str, file: &str) -> &'a str {
    *row.get(key)
        .unwrap_or_else(|| panic!("{} has no column {}", file, key))
}

fn thirty_two(hex_str: &str, context: &str) -> [u8; 32] {
    let bytes = hex::decode(hex_str).unwrap_or_else(|e| panic!("{}: not hex: {}", context, e));
    assert_eq!(bytes.len(), 32, "{}: expected 32 bytes, got {}", context, bytes.len());
    let mut out = [0u8; 32];
    out.copy_from_slice(&bytes);
    out
}

/// The header's raw 32-byte commitment field for a named `blocks.tsv` row,
/// read independently of the block bytes -- this is the real chain history
/// root the corpus's own tooling recorded for that block, not a value this
/// driver or the crate computes.
fn committed_root_bytes(name: &str) -> [u8; 32] {
    let dir = fixture_dir();
    let tsv = std::fs::read_to_string(dir.join("blocks.tsv")).expect("blocks.tsv is readable");
    let mut lines = tsv.lines();
    let header: Vec<&str> = lines
        .next()
        .expect("blocks.tsv has a header row")
        .trim_start_matches("# ")
        .split('\t')
        .collect();
    for line in lines {
        if line.is_empty() {
            continue;
        }
        let cells: Vec<&str> = line.split('\t').collect();
        let row: BTreeMap<&str, &str> = header.iter().cloned().zip(cells).collect();
        if cell(&row, "name", "blocks.tsv") != name {
            continue;
        }
        return thirty_two(cell(&row, "root_bytes_hex", "blocks.tsv"), "blocks.tsv root_bytes_hex");
    }
    panic!("blocks.tsv has no row named {}", name);
}

fn load_block(name: &str) -> Arc<Block> {
    let dir = fixture_dir();
    let text = std::fs::read_to_string(dir.join("blocks").join(format!("{}.hex", name)))
        .unwrap_or_else(|e| panic!("cannot read block {}: {}", name, e));
    let raw = hex::decode(text.trim()).expect("block fixture is hex");
    let block = Block::zcash_deserialize(Cursor::new(&raw[..]))
        .unwrap_or_else(|e| panic!("{} did not parse: {:?}", name, e));
    Arc::new(block)
}

/// The real Sapling final root for a named block, read from
/// `history_tree.tsv`. See that file's header comment for provenance.
fn sapling_final_root(name: &str) -> sapling::tree::Root {
    let dir = fixture_dir();
    let tsv = std::fs::read_to_string(dir.join("history_tree.tsv"))
        .expect("history_tree.tsv is readable");
    let mut lines = tsv.lines();
    let header: Vec<&str> = lines
        .next()
        .expect("history_tree.tsv has a header row")
        .trim_start_matches("# ")
        .split('\t')
        .collect();
    for line in lines {
        if line.is_empty() {
            continue;
        }
        let cells: Vec<&str> = line.split('\t').collect();
        let row: BTreeMap<&str, &str> = header.iter().cloned().zip(cells).collect();
        if cell(&row, "name", "history_tree.tsv") != name {
            continue;
        }
        let bytes = thirty_two(
            cell(&row, "sapling_final_root_hex", "history_tree.tsv"),
            "history_tree.tsv sapling_final_root_hex",
        );
        return sapling::tree::Root(bytes);
    }
    panic!("history_tree.tsv has no row named {}", name);
}

/// A tree seeded from the Heartwood activation block (mainnet 903,000),
/// built fresh for each test so no test can leak mutations into another.
fn seeded_tree() -> HistoryTree {
    let activation_block = load_block("main-0903000");
    let activation_root = sapling_final_root("main-0903000");
    HistoryTree::from_block(Network::Mainnet, activation_block, &activation_root, None)
        .expect("building a tree from a single well-formed block must succeed")
}

// ------------------------------------------------------------------- tests

#[test]
fn a_tree_seeded_from_the_activation_block_reproduces_the_next_blocks_committed_chain_history_root(
) {
    // The crown-jewel property of this milestone: `HistoryTree` did not
    // exist before it, and there is no expected value this driver computes.
    // The Heartwood activation block (903000) commits to nothing usable (it
    // carries ZIP-221's reserved placeholder), but the very next real
    // mainnet block (903001) commits, in its own mined header, to the root
    // of a tree containing exactly one entry: the activation block. That
    // next block's header field is real chain data, read independently of
    // both this driver's tree and of `Block::commitment`'s own parser.
    let tree = seeded_tree();
    let expected = ChainHistoryMmrRootHash::from(committed_root_bytes("main-0903001"));

    assert_eq!(
        tree.hash(),
        expected,
        "a tree built from the Heartwood activation block alone does not reproduce the chain \
         history root the very next mainnet block actually commits to"
    );

    // The same fact, read the other way: the next block's own parsed
    // commitment must agree with this tree's hash.
    let next_block = load_block("main-0903001");
    assert_eq!(
        next_block.commitment(Network::Mainnet).unwrap(),
        Commitment::ChainHistoryRoot(tree.hash()),
        "Block::commitment and HistoryTree::hash disagree about main-0903001's chain history root"
    );
}

#[test]
fn the_seeded_trees_hash_is_not_a_trivial_all_zero_placeholder() {
    // Guards against a stub that always returns the all-zero reserved value
    // (which is what the activation block's own header carries) instead of
    // actually computing a Merkle Mountain Range root.
    let tree = seeded_tree();
    assert_ne!(
        tree.hash(),
        ChainHistoryMmrRootHash::from([0u8; 32]),
        "a tree seeded from a real block must not hash to the all-zero reserved placeholder"
    );
}

#[test]
fn feeding_the_wrong_sapling_root_produces_a_hash_that_does_not_match_the_real_next_block() {
    // The Sapling final root is an explicit argument to `from_block`, not
    // something derived solely from the block's own bytes -- a caller who
    // passes the wrong one must get a different, wrong answer, not a tree
    // that silently ignores the argument and comes out right anyway.
    let activation_block = load_block("main-0903000");
    let wrong_root = sapling::tree::Root([0xabu8; 32]);
    let tree = HistoryTree::from_block(Network::Mainnet, activation_block, &wrong_root, None)
        .expect("building a tree from a single well-formed block must succeed");

    let expected = ChainHistoryMmrRootHash::from(committed_root_bytes("main-0903001"));
    assert_ne!(
        tree.hash(),
        expected,
        "seeding the tree with an arbitrary, wrong Sapling root still reproduced the real \
         chain history root -- the tree must depend on the sapling_root argument"
    );
}

#[test]
fn pushing_the_correct_next_block_changes_the_trees_hash() {
    let mut tree = seeded_tree();
    let hash_before = tree.hash();

    let next_block = load_block("main-0903001");
    let next_root = sapling_final_root("main-0903001");
    tree.push(next_block, &next_root, None)
        .expect("pushing the immediate next block must succeed");

    assert_ne!(
        tree.hash(),
        hash_before,
        "the tree's root hash did not change after a second block was pushed onto it"
    );
}

#[test]
fn pushing_a_block_that_repeats_the_current_height_panics() {
    let activation_block = load_block("main-0903000");
    let activation_root = sapling_final_root("main-0903000");
    let mut tree = seeded_tree();

    // Pushing the same block the tree was seeded with again does not
    // advance the height at all, let alone by the required one --
    // `HistoryTree::push` documents this as a panic, not a `Result`, so a
    // caller cannot silently corrupt the tree by getting the sequence wrong.
    let result = panic::catch_unwind(AssertUnwindSafe(|| {
        tree.push(activation_block, &activation_root, None)
    }));
    let payload = result.expect_err("pushing a block that repeats the current height must panic");
    let message = payload
        .downcast_ref::<String>()
        .cloned()
        .or_else(|| payload.downcast_ref::<&str>().map(|s| s.to_string()))
        .unwrap_or_default();
    assert!(
        message.contains("903000") || message.contains("903_000"),
        "the panic message does not mention the offending height 903000: {:?}",
        message
    );
}

#[test]
fn try_extend_over_one_block_matches_pushing_it_directly() {
    let next_block = load_block("main-0903001");
    let next_root = sapling_final_root("main-0903001");

    let mut by_push = seeded_tree();
    by_push
        .push(next_block.clone(), &next_root, None)
        .expect("pushing the immediate next block must succeed");

    let mut by_extend = seeded_tree();
    by_extend
        .try_extend(vec![(next_block, &next_root, None)])
        .expect("try_extend over one block must succeed");

    assert_eq!(
        by_push.hash(),
        by_extend.hash(),
        "HistoryTree::try_extend does not agree with the same block pushed through HistoryTree::push"
    );
}

#[test]
fn try_extend_with_no_blocks_leaves_the_trees_hash_unchanged() {
    let mut tree = seeded_tree();
    let hash_before = tree.hash();

    let empty: Vec<(Arc<Block>, &sapling::tree::Root, Option<&zebra_chain::orchard::tree::Root>)> =
        Vec::new();
    tree.try_extend(empty)
        .expect("extending by zero blocks must succeed");

    assert_eq!(
        tree.hash(),
        hash_before,
        "try_extend with an empty sequence of blocks must not change the tree's hash"
    );
}

#[test]
fn cloning_a_tree_takes_an_independent_snapshot() {
    let mut tree = seeded_tree();
    let snapshot = tree.clone();
    assert_eq!(
        snapshot.hash(),
        tree.hash(),
        "a freshly taken clone does not have the same root hash as the tree it was cloned from"
    );

    let next_block = load_block("main-0903001");
    let next_root = sapling_final_root("main-0903001");
    tree.push(next_block, &next_root, None)
        .expect("pushing the immediate next block must succeed");

    assert_ne!(
        tree.hash(),
        snapshot.hash(),
        "mutating the original tree also changed a snapshot taken before the mutation -- \
         HistoryTree::clone is not producing an independent copy"
    );
}
