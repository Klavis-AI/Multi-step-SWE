//! Sprout note commitment tree -- real `append` and `count` (app-12 vs stub).
//!
//! Harbor verifier driver -- injected into `zebra-chain/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! In app-11 `NoteCommitmentTree::append` was
//! `pub fn append(&mut self, _cm: &NoteCommitment)` -- it took the commitment
//! **by reference**, returned `()`, and its body was
//! `todo!("implement sprout note commitment trees #2485")`, so calling it
//! panicked rather than doing nothing. `count()` did not exist at all.
//!
//! In app-12 both are live: `append` takes the commitment **by value**, drives
//! `incrementalmerkletree::bridgetree::Frontier` and returns
//! `Result<(), NoteCommitmentTreeError>`, and `count` returns a `u64` leaf
//! count. The tests in this driver do not compile against app-11: the
//! parameter mode, the return type of `append`, and the existence of `count`
//! are all different there.

use zebra_chain::sprout::tree::{NoteCommitmentTree, NoteCommitmentTreeError, Root};
use zebra_chain::sprout::commitment::NoteCommitment;

// The hex strings are the COMMITMENTS test vectors from
// zebra-chain/src/sprout/tests/test_vectors.rs, byte-for-byte.
const COMMITMENT_HEX: [&str; 4] = [
    "62fdad9bfbf17c38ea626a9c9b8af8a748e6b4367c8494caf0ca592999e8b6ba",
    "68eb35bc5e1ddb80a761718e63a1ecf4d4977ae22cc19fa732b85515b2a4c943",
    "836045484077cf6390184ea7cd48b460e2d0f22b2293b69633bb152314a692fb",
    "92498a8295ea36d593eaee7cb8b55be3a3e37b8185d3807693184054cd574ae4",
];

// Expected Merkle roots after appending the i-th commitment, from the ROOTS
// test vector in the same file.
const ROOTS_AFTER: [&str; 4] = [
    "b8e10b6c157be92c43a733e2c9bddb963a2fb9ea80ebcb307acdcc5fc89f1656",
    "83a7754b8240699dd1b63bf70cf70db28ffeb74ef87ce2f4dd32c28ae5009f4f",
    "c45297124f50dcd3f78eed017afd1e30764cd74cdf0a57751978270fd0721359",
    "b61f588fcba9cea79e94376adae1c49583f716d2f20367141f1369a235b95c98",
];

fn from_hex(s: &str) -> [u8; 32] {
    let v = hex::decode(s).expect("valid hex");
    let mut out = [0u8; 32];
    out.copy_from_slice(&v);
    out
}

fn commitment(hex: &str) -> NoteCommitment {
    NoteCommitment::from(from_hex(hex))
}

/// Empty tree root is all zeros (the `uncommitted` leaf).
#[test]
fn empty_tree_root_is_uncommitted() {
    let tree = NoteCommitmentTree::default();
    let root: [u8; 32] = tree.root().into();
    // The root of an all-empty tree at depth 29, taken from the inherited
    // test-vector list's *last* entry, HEX_EMPTY_ROOTS[29]: that list runs
    // leaf-first (index 0 is the all-zero leaf, index 29 the root), the
    // reverse of this crate's own EMPTY_ROOTS, which is indexed
    // root-first.
    let expected: [u8; 32] =
        from_hex("d7c612c817793191a1e68652121876d6b3bde40f4fa52bc314145ce6e5cdd259");
    assert_eq!(root, expected, "empty root must equal the all-zeros propagated hash");
}

/// count() returns the number of leaves appended so far.
#[test]
fn count_tracks_appended_leaves() {
    let mut tree = NoteCommitmentTree::default();
    assert_eq!(tree.count(), 0);
    for (i, hex) in COMMITMENT_HEX.iter().enumerate() {
        tree.append(commitment(hex)).expect("append must succeed on a non-full tree");
        assert_eq!(tree.count(), (i + 1) as u64, "count must equal number of appended leaves");
    }
}

/// Each root after appending commitment i must match the ROOTS test vector.
#[test]
fn roots_match_test_vectors() {
    let mut tree = NoteCommitmentTree::default();
    for (i, (cm_hex, root_hex)) in COMMITMENT_HEX.iter().zip(ROOTS_AFTER.iter()).enumerate() {
        tree.append(commitment(cm_hex))
            .unwrap_or_else(|e| panic!("append #{} failed: {:?}", i, e));
        let root: [u8; 32] = tree.root().into();
        let expected = from_hex(root_hex);
        assert_eq!(root, expected, "root after {} leaves must match test vector", i + 1);
    }
}

/// Appending to a full tree yields `NoteCommitmentTreeError::FullTree`.
/// The tree has depth 29 (2^29 = 536_870_912 leaves), so filling it to observe
/// the error is impractical; this test pins the error *type* instead.
///
/// It asserts three separable things, none of which the previous
/// `assert_eq!(err, NoteCommitmentTreeError::FullTree)` self-comparison could:
/// that `FullTree` is the **only** variant (the exhaustive `match` below stops
/// compiling the moment a second one is added), that `Display` carries the
/// exact spec text, and that `Debug` is the bare variant name.
#[test]
fn full_tree_error_is_the_sole_variant_with_the_specified_display() {
    let err = NoteCommitmentTreeError::FullTree;

    // Exhaustive, with no wildcard arm: adding a variant breaks this line.
    match err {
        NoteCommitmentTreeError::FullTree => {}
    }

    assert_eq!(
        err.to_string(),
        "the note commitment tree is full",
        "the #[error(..)] text is part of the specified surface"
    );
    assert_eq!(format!("{:?}", err), "FullTree");
}

/// hash() and root() agree on a fresh tree.
#[test]
fn hash_and_root_agree() {
    let mut tree = NoteCommitmentTree::default();
    tree.append(commitment(COMMITMENT_HEX[0])).expect("append ok");
    let root_bytes: [u8; 32] = tree.root().into();
    let hash_bytes = tree.hash();
    assert_eq!(root_bytes, hash_bytes, "hash() and root() must return the same 32 bytes");
}

/// Root is convertible to and from [u8; 32].
#[test]
fn root_round_trips_bytes() {
    let bytes = from_hex(ROOTS_AFTER[0]);
    let root = Root::from(bytes);
    let back: [u8; 32] = root.into();
    assert_eq!(bytes, back, "Root must round-trip through [u8; 32]");
}

/// `NoteCommitmentTree::uncommitted()` returns the all-zeros 32-byte leaf value,
/// and `Root`'s custom `Debug` hex-encodes its bytes. Both are contract
/// elements of §2 (`TEST_INTERFACE.md` ll. 63, 43).
///
/// `uncommitted()` is a new associated function at app-12 (absent at app-11),
/// so this test also fails to *compile* against the predecessor.
#[test]
fn uncommitted_leaf_and_root_hex_debug() {
    let zero = NoteCommitmentTree::uncommitted();
    assert_eq!(zero, [0u8; 32], "uncommitted() must be the all-zeros 32-byte leaf");

    let empty_root = NoteCommitmentTree::default().root();
    let rendered = format!("{:?}", empty_root);
    // The custom Debug is `f.debug_tuple("Root").field(&hex::encode(bytes))`,
    // i.e. `Root("<64 lowercase hex chars>")`. Asserting on the rendered string
    // is what pins the hex encoding; without it nothing in the suite formats a
    // `Root` at all.
    let expected_hex = "d7c612c817793191a1e68652121876d6b3bde40f4fa52bc314145ce6e5cdd259";
    assert!(
        rendered.contains(expected_hex),
        "Root Debug must hex-encode its bytes; expected {:?} in {:?}",
        expected_hex,
        rendered
    );
    assert!(
        rendered.starts_with("Root(") && rendered.ends_with(')'),
        "Root Debug must render as Root(\"<hex>\") per TEST_INTERFACE.md §2, got {:?}",
        rendered
    );
}
