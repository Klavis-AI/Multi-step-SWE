//! ZIP-221 FlyClient: the chain history Merkle mountain range, and the split
//! between a tree that exists and a tree that does not.
//!
//! The chain history tree does not exist before Heartwood activates, so this tag
//! models it as two types: `NonEmptyHistoryTree`, which always has at least one
//! node, and `HistoryTree`, a thin wrapper that is *empty* before the activation
//! height and derefs to `Option<NonEmptyHistoryTree>`.  Everything below is about
//! that split and about the one externally checkable fact the tree must satisfy:
//!
//!   the MMR root of the tree built from the Heartwood activation block must equal
//!   the `hashChainHistoryRoot` that the *next* block's header commits to.
//!
//! That expected root is not computed here.  It is read out of mainnet block
//! 903001's own header bytes, which are in the shipped corpus, so an implementation
//! whose MMR node hashing, peak bagging, or leaf encoding is wrong cannot pass.
//!
//! Sapling note commitment tree roots are taken from `$HBV_FIXTURES/history_tree.tsv`
//! in wire byte order, i.e. exactly the bytes the block's own `FinalSaplingRoot`
//! commitment holds.

use std::collections::BTreeMap;
use std::convert::TryInto;
use std::io::Cursor;
use std::path::PathBuf;
use std::sync::Arc;

use zebra_chain::block::{Block, ChainHistoryMmrRootHash, Commitment, CommitmentError, Height};
use zebra_chain::history_tree::{HistoryTree, NonEmptyHistoryTree};
use zebra_chain::orchard;
use zebra_chain::parameters::{Network, NetworkUpgrade};
use zebra_chain::sapling;
use zebra_chain::serialization::ZcashDeserialize;

// ---- fixtures -------------------------------------------------------------

/// Mainnet Heartwood activation height. The chain history tree starts here.
const HEARTWOOD: u32 = 903_000;

fn fixture_dir() -> PathBuf {
    PathBuf::from(std::env::var("HBV_FIXTURES").expect("the verifier must export HBV_FIXTURES"))
}

fn block(name: &str) -> Arc<Block> {
    let path = fixture_dir().join("blocks").join(format!("{}.hex", name));
    let text = std::fs::read_to_string(&path)
        .unwrap_or_else(|e| panic!("missing block fixture {:?}: {}", path, e));
    let raw = hex::decode(text.trim())
        .unwrap_or_else(|e| panic!("block fixture {} is not hex: {}", name, e));
    Arc::new(
        Block::zcash_deserialize(Cursor::new(&raw[..]))
            .unwrap_or_else(|e| panic!("block fixture {} did not parse: {:?}", name, e)),
    )
}

/// The Sapling final treestate root of `name`, from `history_tree.tsv`.
fn sapling_root(name: &str) -> sapling::tree::Root {
    let path = fixture_dir().join("history_tree.tsv");
    let text = std::fs::read_to_string(&path)
        .unwrap_or_else(|e| panic!("missing fixture {:?}: {}", path, e));
    let mut lines = text.lines();
    let header: Vec<&str> = lines
        .next()
        .expect("history_tree.tsv is empty")
        .trim_start_matches("# ")
        .split('\t')
        .collect();
    for line in lines {
        if line.trim().is_empty() {
            continue;
        }
        let row: BTreeMap<&str, &str> = header.iter().cloned().zip(line.split('\t')).collect();
        if row.get("name") == Some(&name) {
            let hex_root = row
                .get("sapling_final_root_hex")
                .expect("history_tree.tsv has a sapling_final_root_hex column");
            let bytes: [u8; 32] = hex::decode(hex_root)
                .unwrap_or_else(|e| panic!("{} sapling root is not hex: {}", name, e))
                .try_into()
                .unwrap_or_else(|_| panic!("{} sapling root is not 32 bytes", name));
            return sapling::tree::Root::from(bytes);
        }
    }
    panic!("history_tree.tsv has no row for {}", name);
}

/// The `hashChainHistoryRoot` a block's header commits to.
fn committed_chain_history_root(name: &str) -> [u8; 32] {
    match block(name)
        .commitment(Network::Mainnet)
        .unwrap_or_else(|e| panic!("{} has no valid commitment: {:?}", name, e))
    {
        Commitment::ChainHistoryRoot(root) => root.into(),
        other => panic!(
            "{} should commit to a chain history root, not {:?}",
            name, other
        ),
    }
}

fn no_orchard() -> orchard::tree::Root {
    orchard::tree::Root::default()
}

fn tree_from(name: &str) -> HistoryTree {
    HistoryTree::from_block(
        Network::Mainnet,
        block(name),
        &sapling_root(name),
        &no_orchard(),
    )
    .unwrap_or_else(|e| panic!("building a history tree from {} failed: {:?}", name, e))
}

fn root_of(tree: &HistoryTree) -> [u8; 32] {
    tree.as_ref()
        .expect("this history tree should not be empty")
        .hash()
        .into()
}

// ---- the empty / non-empty split ------------------------------------------

#[test]
fn a_pre_heartwood_block_makes_an_empty_history_tree() {
    assert_eq!(
        NetworkUpgrade::Heartwood.activation_height(Network::Mainnet),
        Some(Height(HEARTWOOD)),
        "mainnet Heartwood activates at height {}",
        HEARTWOOD
    );

    // The chain history tree does not exist before Heartwood, so building one from
    // an earlier block yields the empty tree rather than an error or a one-node tree.
    for name in ["main-0000010", "main-0419200", "main-0653600"].iter() {
        let tree = HistoryTree::from_block(
            Network::Mainnet,
            block(name),
            &sapling::tree::Root::from([0u8; 32]),
            &no_orchard(),
        )
        .unwrap_or_else(|e| panic!("{} should give an empty tree, not {:?}", name, e));
        assert!(
            tree.is_none(),
            "{} is pre-Heartwood, so its history tree must be empty",
            name
        );
    }

    // HistoryTree derefs to Option<NonEmptyHistoryTree>, and its Default is empty.
    let default = HistoryTree::default();
    let inner: &Option<NonEmptyHistoryTree> = &default;
    assert!(
        inner.is_none(),
        "the default HistoryTree must deref to None"
    );
}

#[test]
fn the_heartwood_activation_block_creates_a_one_node_tree() {
    let tree = tree_from("main-0903000");
    let inner = tree
        .as_ref()
        .expect("the Heartwood activation block creates a non-empty tree");

    assert_eq!(
        inner.size(),
        1,
        "a tree holding one block has exactly one node"
    );
    assert_eq!(
        inner.peaks().len(),
        1,
        "a one-node mountain range has a single peak"
    );
    assert_eq!(
        inner.current_height(),
        Height(HEARTWOOD),
        "the tree records the height of the last block added"
    );
    assert_eq!(
        inner.network(),
        Network::Mainnet,
        "the tree records the network it was built for"
    );
}

#[test]
fn the_activation_block_commits_to_a_reserved_all_zero_root() {
    // ZIP-221: the Heartwood activation block's commitment field MUST be all
    // zeroes and MUST NOT be read as a history root.  The block *after* it holds
    // the first real root.
    let activation = block("main-0903000");
    assert_eq!(
        activation.header.commitment_bytes,
        [0u8; 32],
        "the Heartwood activation block's commitment field is all zeroes"
    );
    match activation
        .commitment(Network::Mainnet)
        .expect("main-0903000 has a valid commitment")
    {
        Commitment::ChainHistoryActivationReserved => {}
        other => panic!(
            "the activation block's commitment is a reserved field, not {:?}",
            other
        ),
    }

    assert_ne!(
        committed_chain_history_root("main-0903001"),
        [0u8; 32],
        "the block after activation commits to a real chain history root"
    );
}

// ---- the root itself ------------------------------------------------------

#[test]
fn the_tree_root_matches_the_chain_history_root_committed_by_the_next_block() {
    // This is the whole point of ZIP-221: the tree built from block 903000 is what
    // block 903001's header commits to.  The expected value comes from the chain.
    let tree = tree_from("main-0903000");
    assert_eq!(
        root_of(&tree),
        committed_chain_history_root("main-0903001"),
        "the MMR root of the tree containing only the Heartwood activation block \
         must equal the hashChainHistoryRoot in block 903001's header"
    );

    // ChainHistoryMmrRootHash round trips through its 32 bytes, so the comparison
    // above is over the same byte order the header uses.
    let expected = committed_chain_history_root("main-0903001");
    let rebuilt: [u8; 32] = ChainHistoryMmrRootHash::from(expected).into();
    assert_eq!(
        rebuilt, expected,
        "ChainHistoryMmrRootHash must round trip through [u8; 32] unchanged"
    );
}

#[test]
fn the_tree_root_depends_on_the_sapling_note_commitment_root() {
    // The Sapling final treestate root is one of the fields hashed into the MMR
    // leaf, so a tree built with the wrong root must have a different hash.  If it
    // does not, the leaf encoding is ignoring the Sapling root entirely.
    let correct = tree_from("main-0903000");

    let mut wrong_bytes: [u8; 32] = sapling_root("main-0903000").into();
    wrong_bytes.reverse();
    let wrong = HistoryTree::from_block(
        Network::Mainnet,
        block("main-0903000"),
        &sapling::tree::Root::from(wrong_bytes),
        &no_orchard(),
    )
    .expect("a tree still builds with a different sapling root");

    assert_ne!(
        root_of(&correct),
        root_of(&wrong),
        "changing the Sapling note commitment root must change the MMR root"
    );
}

// ---- growing the tree -----------------------------------------------------

#[test]
fn pushing_the_next_block_grows_the_tree_and_advances_its_height() {
    let mut tree = tree_from("main-0903000");
    let root_before = root_of(&tree);

    tree.push(
        Network::Mainnet,
        block("main-0903001"),
        sapling_root("main-0903001"),
        no_orchard(),
    )
    .expect("903001 is the next block after 903000");

    let inner = tree.as_ref().expect("the tree is still non-empty");
    assert_eq!(
        inner.current_height(),
        Height(HEARTWOOD + 1),
        "pushing a block advances the tree's current height by one"
    );
    assert_eq!(
        inner.size(),
        3,
        "a mountain range over two leaves holds three nodes: two leaves and their parent"
    );
    assert_eq!(
        inner.peaks().len(),
        1,
        "two leaves bag into a single peak"
    );
    assert_ne!(
        root_of(&tree),
        root_before,
        "adding a block must change the chain history root"
    );
}

#[test]
fn the_surviving_peak_of_a_two_leaf_tree_is_the_parent_not_a_leaf() {
    // `pushing_the_next_block_grows_the_tree_and_advances_its_height` asserts
    // that one peak survives. That is necessary but not sufficient, and the
    // gap is not hypothetical: the peak walk in `prune` locates the highest
    // peak by "pretending it is the right sibling in a tree", which costs it a
    // -1 for an imaginary left sibling. Drop that -1 and a two-leaf range
    // prunes to {1} instead of {2} -- still exactly one entry, so a test that
    // counts peaks cannot see it. It keeps a leaf and throws the parent away.
    //
    // Node 2 is the parent of leaves 0 and 1, and it is the only peak a
    // mountain range over two leaves has. That is a property of the range's
    // shape, not a value this driver copies out of the implementation.
    let mut tree = tree_from("main-0903000");
    tree.push(
        Network::Mainnet,
        block("main-0903001"),
        sapling_root("main-0903001"),
        no_orchard(),
    )
    .expect("903001 is the next block after 903000");

    let inner = tree.as_ref().expect("the tree is still non-empty");
    let positions: Vec<u32> = inner.peaks().keys().copied().collect();
    assert_eq!(
        positions,
        vec![2u32],
        "a mountain range over two leaves must prune to its parent node at \
         position 2, but it retained {:?}; retaining one entry is not enough \
         if it is the wrong one",
        positions
    );
}

#[test]
fn pushing_across_the_activation_height_creates_the_tree_in_place() {
    // A node syncing through Heartwood starts with an empty tree and pushes
    // blocks into it. Pre-activation blocks leave it empty; the activation block
    // itself creates it.
    let mut tree = HistoryTree::default();

    tree.push(
        Network::Mainnet,
        block("main-0653600"),
        sapling::tree::Root::from([0u8; 32]),
        no_orchard(),
    )
    .expect("pushing a pre-Heartwood block is not an error");
    assert!(
        tree.is_none(),
        "a pre-Heartwood block must leave the history tree empty"
    );

    tree.push(
        Network::Mainnet,
        block("main-0903000"),
        sapling_root("main-0903000"),
        no_orchard(),
    )
    .expect("pushing the activation block creates the tree");
    assert!(
        tree.is_some(),
        "the Heartwood activation block must create the history tree"
    );
    assert_eq!(
        root_of(&tree),
        committed_chain_history_root("main-0903001"),
        "a tree created by pushing the activation block must have the same root \
         as one created by HistoryTree::from_block"
    );
}

#[test]
fn a_non_empty_tree_wraps_into_a_history_tree_with_the_same_root() {
    let bare = NonEmptyHistoryTree::from_block(
        Network::Mainnet,
        block("main-0903000"),
        &sapling_root("main-0903000"),
        &no_orchard(),
    )
    .expect("the activation block builds a non-empty tree directly");

    let expected_root: [u8; 32] = bare.hash().into();
    assert_eq!(
        expected_root,
        committed_chain_history_root("main-0903001"),
        "NonEmptyHistoryTree::from_block must produce the committed root too"
    );

    let wrapped: HistoryTree = bare.into();
    assert!(
        wrapped.is_some(),
        "From<NonEmptyHistoryTree> for HistoryTree must produce a non-empty tree"
    );
    assert_eq!(
        root_of(&wrapped),
        expected_root,
        "wrapping a tree must not change its root"
    );
}

#[test]
fn pushing_a_block_that_is_not_the_next_height_panics() {
    // librustzcash silently corrupts the tree if the heights are wrong, so
    // NonEmptyHistoryTree::push refuses a block that is not exactly one height
    // above the last one.
    let mut tree = NonEmptyHistoryTree::from_block(
        Network::Mainnet,
        block("main-0903000"),
        &sapling_root("main-0903000"),
        &no_orchard(),
    )
    .expect("the activation block builds a non-empty tree");

    let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        tree.push(block("main-0982681"), &sapling_root("main-0903001"), &no_orchard())
    }));
    assert!(
        result.is_err(),
        "pushing height 982681 onto a tree at height 903000 must panic, \
         not silently corrupt the mountain range"
    );
}

#[test]
fn the_commitment_field_is_read_as_the_variant_its_upgrade_uses() {
    // §3.2: `Commitment` is a five-variant enum and `Block::commitment` picks
    // the variant from the block's own network upgrade, not from the bytes.
    // Three of the five arms are otherwise untested: a genesis Sprout-era block
    // and a Sapling block hold no MMR root at all, so nothing above reaches
    // them, and a candidate that returned `ChainHistoryRoot` for every
    // post-Heartwood block would still pass every earlier case.
    //
    // Sprout era: the field is a reserved value to be ignored, and the bytes
    // are carried through unchanged.
    for name in ["main-0000000", "main-0000010", "main-0000202", "main-0347499"] {
        let sprout = block(name);
        match sprout
            .commitment(Network::Mainnet)
            .unwrap_or_else(|e| panic!("{} has a valid commitment: {:?}", name, e))
        {
            Commitment::PreSaplingReserved(bytes) => assert_eq!(
                bytes, sprout.header.commitment_bytes,
                "{}: the reserved value must be the header's own bytes",
                name
            ),
            other => panic!(
                "{} is a Sprout-era block, whose commitment is reserved, not {:?}",
                name, other
            ),
        }
    }

    // Sapling and Blossom: the field is the final Sapling treestate root.
    for name in ["main-0419200", "main-0653600"] {
        let sapling = block(name);
        match sapling
            .commitment(Network::Mainnet)
            .unwrap_or_else(|e| panic!("{} has a valid commitment: {:?}", name, e))
        {
            Commitment::FinalSaplingRoot(root) => assert_eq!(
                <[u8; 32]>::from(root),
                sapling.header.commitment_bytes,
                "{}: the Sapling root must be the header's own bytes",
                name
            ),
            other => panic!(
                "{} is a Sapling-era block, whose commitment is a Sapling root, not {:?}",
                name, other
            ),
        }
    }

    // Heartwood era (post-activation): the field is the MMR root.
    for name in ["main-0903001", "main-0982681"] {
        let heartwood = block(name);
        match heartwood
            .commitment(Network::Mainnet)
            .unwrap_or_else(|e| panic!("{} has a valid commitment: {:?}", name, e))
        {
            Commitment::ChainHistoryRoot(root) => assert_eq!(
                <[u8; 32]>::from(root),
                heartwood.header.commitment_bytes,
                "{}: the chain history root must be the header's own bytes",
                name
            ),
            other => panic!(
                "{} is a Heartwood-era block, whose commitment is a chain history root, not {:?}",
                name, other
            ),
        }
    }

    // The same 32 bytes mean different things on different networks: testnet's
    // Sapling activation, one height apart from mainnet's, is the same variant.
    let testnet = block("test-0280000");
    assert!(
        matches!(
            testnet.commitment(Network::Testnet),
            Ok(Commitment::FinalSaplingRoot(_))
        ),
        "testnet Sapling activation must be read as a Sapling root"
    );
    // Reading the same block against the wrong network is a different variant,
    // which is what makes the variant a function of the upgrade and not of the
    // bytes alone.
    assert!(
        !matches!(
            testnet.commitment(Network::Mainnet),
            Err(CommitmentError::MissingBlockHeight { .. })
        ),
        "a block with a coinbase height never reports a missing height"
    );

    // The block's height comes from its coinbase transaction, so a block that
    // does not carry one cannot be interpreted.  The fixture corpus has none,
    // so the height is removed from a real block and the accessor re-run.
    let mut headerless = (*block("main-0903001")).clone();
    headerless.transactions = Vec::new().into();
    assert!(
        headerless.coinbase_height().is_none(),
        "a block with no transactions has no coinbase height"
    );
    assert!(
        matches!(
            headerless.commitment(Network::Mainnet),
            Err(CommitmentError::MissingBlockHeight { .. })
        ),
        "a block without a height must report MissingBlockHeight, got {:?}",
        headerless.commitment(Network::Mainnet)
    );
}
