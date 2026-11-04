//! Incremental note commitment trees, against the Zcash project's own vectors.
//!
//! Harbor verifier driver -- injected into `zebra-chain/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! **Gate group, weight 0.** Nothing here is new at this milestone. The
//! incremental Sapling and Orchard trees -- Merkle frontiers that accept one
//! commitment at a time and report the depth-32 root at any point -- were
//! established at an earlier milestone, together with their public `Root`
//! types. It earns nothing and only vetoes, for two reasons specific to this
//! step. First, an inherited capability that no graded case touches can be
//! deleted or broken for free while the step still scores full marks, and both
//! trees live in `zebra-chain`, the crate this step rewrites `amount`,
//! `value_balance` and `history_tree` in. Second, this step *does* touch them:
//! it memoizes the Sapling and Orchard roots. That cache is explicitly not
//! scored -- only the root's value is observable, and it must not change --
//! which is precisely a gate condition rather than a scored one.
//!
//! The assertions bite because the empty-node convention is the thing
//! everybody gets wrong. The tree is a fixed depth 32, so a tree with three
//! leaves is not a three-leaf tree: it is a 2^32-leaf tree whose remaining
//! leaves are the distinguished `Uncommitted` value, hashed up 32 layers with
//! a *layer-dependent* hash. Get the layer numbering backwards -- the spec
//! numbers layers from the root downwards, `incrementalmerkletree` numbers
//! altitudes from the leaves upwards -- and every empty root is wrong while
//! every one-leaf-per-layer intermediate still looks plausible. The published
//! empty roots for all 33 layers catch exactly that, and the 16 Sapling and 10
//! Orchard incremental roots catch an off-by-one in the frontier -- including
//! one produced by a stale memoized root.
//!
//! Provenance of every expected value, none of it computed by this crate:
//! Sapling empty roots from librustzcash `zcash_primitives::merkle_tree`,
//! Sapling commitments and roots from `zcash`'s
//! `merkle_commitments_sapling.json`, Orchard empty roots and commitments from
//! `zcash-hackworks/zcash-test-vectors`. They are shipped in
//! `$HBV_FIXTURES/note_commitment_trees.tsv`. Every value is a constant read
//! from that file, so the group is deterministic across runs and machines.

use std::collections::BTreeMap;
use std::convert::TryFrom;
use std::path::PathBuf;

use halo2::arithmetic::FieldExt;
use halo2::pasta::pallas;

use zebra_chain::orchard;
use zebra_chain::sapling;
use zebra_chain::sprout;

// ---------------------------------------------------------------- fixtures

/// One row of `note_commitment_trees.tsv`: a 32-byte value, tagged with the
/// pool it belongs to, what it is, and its position in the published list.
struct Vector {
    pool: String,
    kind: String,
    index: usize,
    bytes: [u8; 32],
}

fn fixture_dir() -> PathBuf {
    PathBuf::from(
        std::env::var("HBV_FIXTURES")
            .expect("HBV_FIXTURES must point at the shipped fixture directory"),
    )
}

fn thirty_two(hex_str: &str) -> [u8; 32] {
    let bytes = hex::decode(hex_str).expect("fixture field is hex");
    assert_eq!(bytes.len(), 32, "fixture field is 32 bytes");
    let mut out = [0u8; 32];
    out.copy_from_slice(&bytes);
    out
}

/// Load the whole vector file, keyed by `(pool, kind)` and ordered by index.
fn vectors() -> BTreeMap<(String, String), Vec<[u8; 32]>> {
    let path = fixture_dir().join("note_commitment_trees.tsv");
    let text = std::fs::read_to_string(&path)
        .unwrap_or_else(|e| panic!("cannot read {:?}: {}", path, e));

    let mut rows: Vec<Vector> = Vec::new();
    for line in text.lines() {
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        let cells: Vec<&str> = line.split('\t').collect();
        assert_eq!(cells.len(), 4, "malformed vector row: {}", line);
        rows.push(Vector {
            pool: cells[0].to_string(),
            kind: cells[1].to_string(),
            index: cells[2].parse().expect("index is a number"),
            bytes: thirty_two(cells[3]),
        });
    }

    let mut grouped: BTreeMap<(String, String), Vec<(usize, [u8; 32])>> = BTreeMap::new();
    for row in rows {
        grouped
            .entry((row.pool, row.kind))
            .or_insert_with(Vec::new)
            .push((row.index, row.bytes));
    }

    let mut out: BTreeMap<(String, String), Vec<[u8; 32]>> = BTreeMap::new();
    for (key, mut values) in grouped {
        values.sort_by_key(|(index, _)| *index);
        for (position, (index, _)) in values.iter().enumerate() {
            assert_eq!(
                *index, position,
                "{:?} vectors must be indexed 0..n with no gaps",
                key
            );
        }
        out.insert(key, values.into_iter().map(|(_, bytes)| bytes).collect());
    }
    out
}

fn group(pool: &str, kind: &str) -> Vec<[u8; 32]> {
    let all = vectors();
    all.get(&(pool.to_string(), kind.to_string()))
        .unwrap_or_else(|| panic!("the fixture has no {} {} vectors", pool, kind))
        .clone()
}

/// `jubjub::Fq::from_bytes` returns a constant-time `CtOption`, not an
/// `Option`, so it has to be converted before it can carry a message.
fn sapling_commitment(bytes: &[u8; 32], i: usize) -> jubjub::Fq {
    Option::<jubjub::Fq>::from(jubjub::Fq::from_bytes(bytes)).unwrap_or_else(|| {
        panic!(
            "Sapling test commitment {} must be a valid jubjub base field element",
            i
        )
    })
}

/// The Pallas equivalent. `pallas::Base::from_bytes` is a `FieldExt` method
/// and likewise returns a `CtOption`.
fn orchard_commitment(bytes: &[u8; 32], i: usize) -> pallas::Base {
    Option::<pallas::Base>::from(pallas::Base::from_bytes(bytes)).unwrap_or_else(|| {
        panic!(
            "Orchard test commitment {} must be a valid Pallas base field element",
            i
        )
    })
}

/// The Sapling and Orchard trees are both depth 32, so the published empty
/// root lists have 33 entries: one per layer, leaf (0) to root (32).
const MERKLE_DEPTH: usize = 32;

// ---------------------------------------------------------------- sapling

#[test]
fn an_empty_sapling_tree_has_the_published_depth_32_empty_root() {
    let empty_roots = group("sapling", "empty_root");
    assert_eq!(
        empty_roots.len(),
        MERKLE_DEPTH + 1,
        "the published Sapling empty roots cover 33 layers"
    );

    let tree = sapling::tree::NoteCommitmentTree::default();

    assert_eq!(
        hex::encode(tree.hash()),
        hex::encode(empty_roots[MERKLE_DEPTH]),
        "an empty Sapling tree's root must be the published empty root for layer 32, got {}",
        hex::encode(tree.hash())
    );
    assert_eq!(
        <[u8; 32]>::from(tree.root()),
        empty_roots[MERKLE_DEPTH],
        "root() and hash() must report the same empty root"
    );
    assert_eq!(
        tree.count(),
        0,
        "an empty Sapling tree must have appended no commitments, got {}",
        tree.count()
    );
}

#[test]
fn the_sapling_uncommitted_leaf_is_the_published_layer_zero_value() {
    // Uncommitted^Sapling = I2LEBSP_256(1), which is the byte string 0x01
    // followed by 31 zero bytes, and is the leaf-layer entry of the published
    // empty-root list.
    let empty_roots = group("sapling", "empty_root");
    let uncommitted = sapling::tree::NoteCommitmentTree::uncommitted();

    assert_eq!(
        hex::encode(uncommitted),
        hex::encode(empty_roots[0]),
        "Uncommitted^Sapling must be the published layer-0 empty node, got {}",
        hex::encode(uncommitted)
    );

    let mut expected = [0u8; 32];
    expected[0] = 1;
    assert_eq!(
        uncommitted, expected,
        "Uncommitted^Sapling is the little-endian encoding of the field element 1"
    );
}

#[test]
fn appending_sapling_commitments_reproduces_every_published_incremental_root() {
    let commitments = group("sapling", "commitment");
    let roots = group("sapling", "root");
    assert_eq!(
        commitments.len(),
        roots.len(),
        "there must be one published Sapling root per published commitment"
    );
    assert!(
        commitments.len() >= 16,
        "the Sapling vector set must have at least sixteen commitments, found {}",
        commitments.len()
    );

    let mut tree = sapling::tree::NoteCommitmentTree::default();

    for (i, commitment_bytes) in commitments.iter().enumerate() {
        let cm_u = sapling_commitment(commitment_bytes, i);

        tree.append(cm_u)
            .unwrap_or_else(|e| panic!("appending Sapling commitment {} must succeed: {:?}", i, e));

        assert_eq!(
            hex::encode(tree.hash()),
            hex::encode(roots[i]),
            "after appending Sapling commitment {} the root must be {}, got {}",
            i,
            hex::encode(roots[i]),
            hex::encode(tree.hash())
        );
    }
}

#[test]
fn a_sapling_tree_built_from_a_vector_matches_one_built_by_appending() {
    let commitments = group("sapling", "commitment");
    let roots = group("sapling", "root");

    let mut leaves: Vec<jubjub::Fq> = Vec::new();

    for (i, commitment_bytes) in commitments.iter().enumerate() {
        let cm_u = sapling_commitment(commitment_bytes, i);
        leaves.push(cm_u);

        let bulk = sapling::tree::NoteCommitmentTree::from(leaves.clone());

        assert_eq!(
            hex::encode(bulk.hash()),
            hex::encode(roots[i]),
            "a tree built from the first {} commitments at once must have root {}, got {}",
            i + 1,
            hex::encode(roots[i]),
            hex::encode(bulk.hash())
        );
    }

    // An empty vector must give the empty tree, not a tree with one leaf.
    let empty_from_vec = sapling::tree::NoteCommitmentTree::from(Vec::<jubjub::Fq>::new());
    let empty_default = sapling::tree::NoteCommitmentTree::default();
    assert_eq!(
        hex::encode(empty_from_vec.hash()),
        hex::encode(empty_default.hash()),
        "From<Vec<_>> on an empty vector must give the default empty tree"
    );
    assert_eq!(
        empty_from_vec, empty_default,
        "two Sapling trees with the same root must compare equal"
    );
}

#[test]
fn the_sapling_tree_counts_the_commitments_it_has_appended() {
    let commitments = group("sapling", "commitment");
    let mut tree = sapling::tree::NoteCommitmentTree::default();

    assert_eq!(tree.count(), 0, "a fresh Sapling tree has appended nothing");

    for (i, commitment_bytes) in commitments.iter().enumerate() {
        let cm_u = sapling_commitment(commitment_bytes, i);
        tree.append(cm_u).expect("the tree is nowhere near full");

        let expected = u64::try_from(i + 1).expect("a small count");
        assert_eq!(
            tree.count(),
            expected,
            "after {} appends the Sapling count must be {}, got {}",
            i + 1,
            expected,
            tree.count()
        );
    }

    assert_eq!(
        sapling::tree::NoteCommitmentTreeError::FullTree.to_string(),
        "The note commitment tree is full",
        "the Sapling tree's only error variant must display its documented message"
    );
}

#[test]
fn a_sapling_root_round_trips_through_its_byte_array() {
    let roots = group("sapling", "root");
    let bytes = roots[0];

    let root = sapling::tree::Root::from(bytes);
    assert_eq!(
        <[u8; 32]>::from(root),
        bytes,
        "a Sapling Root built from bytes must convert back to the same bytes"
    );
    assert_eq!(
        root.0, bytes,
        "the Sapling Root's inner byte array is public and holds the root"
    );

    // The reference-taking conversions must agree with the by-value ones.
    assert_eq!(
        sapling::tree::Root::from(&bytes),
        root,
        "From<&[u8; 32]> must agree with From<[u8; 32]>"
    );
    assert_eq!(
        <[u8; 32]>::from(&root),
        bytes,
        "From<&Root> must agree with From<Root>"
    );

    // Debug prints the root as hex, not as a byte list.
    let debug = format!("{:?}", root);
    assert!(
        debug.contains(&hex::encode(bytes)),
        "a Sapling Root's Debug output must contain its hex encoding, got {}",
        debug
    );
}

// ---------------------------------------------------------------- orchard

#[test]
fn an_empty_orchard_tree_has_the_published_depth_32_empty_root() {
    let empty_roots = group("orchard", "empty_root");
    assert_eq!(
        empty_roots.len(),
        MERKLE_DEPTH + 1,
        "the published Orchard empty roots cover 33 layers"
    );

    let tree = orchard::tree::NoteCommitmentTree::default();

    assert_eq!(
        hex::encode(tree.hash()),
        hex::encode(empty_roots[MERKLE_DEPTH]),
        "an empty Orchard tree's root must be the published empty root for layer 32, got {}",
        hex::encode(tree.hash())
    );
    assert_eq!(
        <[u8; 32]>::from(tree.root()),
        empty_roots[MERKLE_DEPTH],
        "root() and hash() must report the same empty Orchard root"
    );
    assert_eq!(
        tree.count(),
        0,
        "an empty Orchard tree must have appended no commitments, got {}",
        tree.count()
    );
}

#[test]
fn the_orchard_uncommitted_leaf_is_the_published_layer_zero_value() {
    // Uncommitted^Orchard = I2LEBSP_255(2): the Orchard convention differs
    // from Sapling's, and using 1 here is the classic copy-paste bug.
    let empty_roots = group("orchard", "empty_root");
    let uncommitted = orchard::tree::NoteCommitmentTree::uncommitted();

    assert_eq!(
        hex::encode(uncommitted.to_bytes()),
        hex::encode(empty_roots[0]),
        "Uncommitted^Orchard must be the published layer-0 empty node, got {}",
        hex::encode(uncommitted.to_bytes())
    );

    let mut expected = [0u8; 32];
    expected[0] = 2;
    assert_eq!(
        uncommitted.to_bytes(),
        expected,
        "Uncommitted^Orchard is the little-endian encoding of the field element 2"
    );
}

#[test]
fn appending_orchard_commitments_reproduces_every_published_incremental_root() {
    let commitments = group("orchard", "commitment");
    let roots = group("orchard", "root");

    assert!(
        !roots.is_empty(),
        "the Orchard vector set must have at least one published root"
    );
    assert_eq!(
        commitments.len(),
        roots.len() * 5,
        "the published Orchard vectors are groups of five commitments per root, \
         found {} commitments for {} roots",
        commitments.len(),
        roots.len()
    );

    let mut tree = orchard::tree::NoteCommitmentTree::default();

    for (group_index, root_bytes) in roots.iter().enumerate() {
        for position in 0..5 {
            let i = group_index * 5 + position;
            let cm_x = orchard_commitment(&commitments[i], i);
            tree.append(cm_x)
                .unwrap_or_else(|e| panic!("appending Orchard commitment {} must succeed: {:?}", i, e));
        }

        assert_eq!(
            hex::encode(tree.hash()),
            hex::encode(root_bytes),
            "after appending {} Orchard commitments the anchor must be {}, got {}",
            (group_index + 1) * 5,
            hex::encode(root_bytes),
            hex::encode(tree.hash())
        );
    }
}

#[test]
fn an_orchard_tree_built_from_a_vector_matches_one_built_by_appending() {
    let commitments = group("orchard", "commitment");
    let roots = group("orchard", "root");

    let mut leaves: Vec<pallas::Base> = Vec::new();

    for (group_index, root_bytes) in roots.iter().enumerate() {
        for position in 0..5 {
            let i = group_index * 5 + position;
            let cm_x = orchard_commitment(&commitments[i], i);
            leaves.push(cm_x);
        }

        let bulk = orchard::tree::NoteCommitmentTree::from(leaves.clone());

        assert_eq!(
            hex::encode(bulk.hash()),
            hex::encode(root_bytes),
            "an Orchard tree built from the first {} commitments at once must have anchor {}, got {}",
            leaves.len(),
            hex::encode(root_bytes),
            hex::encode(bulk.hash())
        );
    }

    let empty_from_vec = orchard::tree::NoteCommitmentTree::from(Vec::<pallas::Base>::new());
    let empty_default = orchard::tree::NoteCommitmentTree::default();
    assert_eq!(
        empty_from_vec, empty_default,
        "From<Vec<_>> on an empty vector must give the default empty Orchard tree"
    );
}

#[test]
fn the_orchard_tree_counts_its_commitments_and_its_root_parses_back() {
    let commitments = group("orchard", "commitment");
    let mut tree = orchard::tree::NoteCommitmentTree::default();

    assert_eq!(tree.count(), 0, "a fresh Orchard tree has appended nothing");

    for (i, commitment_bytes) in commitments.iter().enumerate() {
        let cm_x = orchard_commitment(commitment_bytes, i);
        tree.append(cm_x).expect("the tree is nowhere near full");

        let expected = u64::try_from(i + 1).expect("a small count");
        assert_eq!(
            tree.count(),
            expected,
            "after {} appends the Orchard count must be {}, got {}",
            i + 1,
            expected,
            tree.count()
        );
    }

    // The Orchard root is a Pallas field element, so not every 32-byte string
    // is one: the conversion back is fallible.
    let root_bytes = tree.hash();
    let parsed = orchard::tree::Root::try_from(root_bytes)
        .expect("the root of a real Orchard tree is a valid Pallas base field element");
    assert_eq!(
        parsed,
        tree.root(),
        "parsing an Orchard root back from its bytes must give the same Root"
    );
    assert_eq!(
        <[u8; 32]>::from(&parsed),
        root_bytes,
        "From<&Root> must agree with the bytes the root was parsed from"
    );

    assert_eq!(
        orchard::tree::NoteCommitmentTreeError::FullTree.to_string(),
        "The note commitment tree is full",
        "the Orchard tree's only error variant must display its documented message"
    );
}

// ---------------------------------------------------------------- sprout

#[test]
fn a_sprout_root_converts_between_owned_and_borrowed_byte_arrays() {
    // Sprout's tree is not incremental in this milestone -- it is the one
    // pool whose tree is still rebuilt in bulk -- but its `Root` gains the
    // same pair of reference-taking conversions the other two pools have, and
    // the tree type itself becomes public so the state can hold one.
    let bytes: [u8; 32] = [
        0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd,
        0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab,
        0xcd, 0xef,
    ];

    let owned = sprout::tree::Root::from(bytes);
    let borrowed = sprout::tree::Root::from(&bytes);

    assert_eq!(
        owned, borrowed,
        "From<&[u8; 32]> must agree with From<[u8; 32]> for a Sprout Root"
    );
    assert_eq!(
        <[u8; 32]>::from(owned),
        bytes,
        "a Sprout Root must convert back to the bytes it was built from"
    );
    assert_eq!(
        <[u8; 32]>::from(&owned),
        bytes,
        "From<&Root> must agree with From<Root> for a Sprout Root"
    );

    // The Sprout tree type is public, and a default one reports a root.
    let tree = sprout::tree::NoteCommitmentTree::default();
    let root = tree.root();
    assert_eq!(
        <[u8; 32]>::from(&root),
        <[u8; 32]>::from(root),
        "the two Sprout Root conversions must agree on a real tree root"
    );
}
