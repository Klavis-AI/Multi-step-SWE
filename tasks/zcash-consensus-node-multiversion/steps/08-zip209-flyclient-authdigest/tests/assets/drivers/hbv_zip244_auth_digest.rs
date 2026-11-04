//! ZIP-244 authorizing-data commitments: `AuthDigest` and `AuthDataRoot`.
//!
//! Harbor verifier driver -- injected into `zebra-chain/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! An earlier milestone split a v5 transaction's identifier in two: the txid
//! commits to the transaction's *effects*, and deliberately does not commit to
//! its signatures or proofs. That leaves the authorizing data uncommitted, so
//! ZIP-244 adds a second digest -- `auth_digest` -- over exactly the data the
//! txid leaves out, and a block-level `hashAuthDataRoot` over one such digest
//! per transaction. This milestone is where both appear.
//!
//! Three rules carry the whole group, and all three come from ZIP-244's
//! "Authorizing Data Commitment" and "Block Header Changes" sections:
//!
//!   * a transaction has an authorizing-data commitment only from v5 on;
//!     v1..v4 have none, and the tree uses the fixed placeholder
//!     `[0xFF; 32]` in their place;
//!   * the tree is a *full* binary Merkle tree -- the leaves are padded up to
//!     the next power of two with the all-zero digest `[0x00; 32]`, which is
//!     why a three-leaf tree and the same tree with a fourth all-zero leaf
//!     have the same root;
//!   * interior nodes are BLAKE2b-256 with the personalisation string
//!     `ZcashAuthDatHash`.
//!
//! The expected digests for real v5 transactions are the `auth_digest_hex`
//! column of `zip244.tsv`, which carries the published ZIP-244 test vectors;
//! nothing in this file derives them. The Merkle-tree assertions are checked
//! against a re-implementation in this file that shares no code with the crate
//! -- it calls `blake2b_simd` directly, which `zebra-chain` already depends on.
//!
//! Reading the root back out: `AuthDataRoot`'s inner array is not public, and
//! the crate exposes no accessor for it, so every byte-level assertion below
//! goes through `Debug`, which the interface requires to render the root as a
//! 64-character lowercase hex string. `AuthDigest` is only ever observed
//! through a one-leaf `AuthDataRoot`, whose root is that digest unchanged.

use std::collections::BTreeMap;
use std::convert::TryInto;
use std::io::Cursor;
use std::iter;
use std::path::PathBuf;
use std::sync::Arc;

use zebra_chain::block::merkle::AuthDataRoot;
use zebra_chain::block::{Block, Height};
use zebra_chain::serialization::ZcashDeserialize;
use zebra_chain::transaction::{AuthDigest, LockTime, Transaction};

// ---------------------------------------------------------------- fixtures

fn fixture_dir() -> PathBuf {
    PathBuf::from(
        std::env::var("HBV_FIXTURES")
            .expect("HBV_FIXTURES must point at the shipped fixture directory"),
    )
}

/// Every row of a shipped TSV, as a column-name -> value map.
fn rows(file: &str) -> Vec<BTreeMap<String, String>> {
    let text = std::fs::read_to_string(fixture_dir().join(file))
        .unwrap_or_else(|e| panic!("cannot read {}: {}", file, e));
    let mut lines = text.lines();
    let header: Vec<&str> = lines
        .next()
        .unwrap_or_else(|| panic!("{} has no header row", file))
        .trim_start_matches("# ")
        .split('\t')
        .collect();

    let mut out = Vec::new();
    for line in lines {
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        let cells: Vec<&str> = line.split('\t').collect();
        assert_eq!(cells.len(), header.len(), "{}: malformed row {}", file, line);
        let row: BTreeMap<&str, &str> = header.iter().cloned().zip(cells).collect();
        out.push(
            row.into_iter()
                .map(|(k, v)| (k.to_string(), v.to_string()))
                .collect(),
        );
    }
    assert!(!out.is_empty(), "{} has no data rows", file);
    out
}

struct BlockFixture {
    name: String,
    tx_count: usize,
    block: Block,
}

/// The seventeen-block corpus, parsed. Every one of these blocks predates NU5,
/// so every transaction in it is v1..v4 and none of them has an
/// authorizing-data commitment.
fn corpus() -> Vec<BlockFixture> {
    let dir = fixture_dir();
    let mut out = Vec::new();
    for row in rows("blocks.tsv") {
        let name = row
            .get("name")
            .expect("blocks.tsv has a name column")
            .clone();
        let tx_count: usize = row
            .get("tx_count")
            .expect("blocks.tsv has a tx_count column")
            .parse()
            .expect("tx_count is decimal");
        let text = std::fs::read_to_string(dir.join("blocks").join(format!("{}.hex", name)))
            .unwrap_or_else(|e| panic!("cannot read block {}: {}", name, e));
        let raw = hex::decode(text.trim()).expect("block fixture is hex");
        let block = Block::zcash_deserialize(Cursor::new(&raw[..]))
            .unwrap_or_else(|e| panic!("{} did not parse: {:?}", name, e));
        assert_eq!(
            block.transactions.len(),
            tx_count,
            "{}: parsed {} transactions, blocks.tsv says {}",
            name,
            block.transactions.len(),
            tx_count
        );
        out.push(BlockFixture {
            name,
            tx_count,
            block,
        });
    }
    assert!(
        out.len() >= 17,
        "the shipped corpus has {} blocks; the verifier expects at least 17",
        out.len()
    );
    out
}

struct V5Vector {
    name: String,
    tx: Transaction,
    auth_digest_hex: String,
}

/// The published ZIP-244 vectors, as v5 transactions plus their expected
/// authorizing-data commitments.
fn v5_vectors() -> Vec<V5Vector> {
    let mut out = Vec::new();
    for row in rows("zip244.tsv") {
        let name = row.get("name").expect("zip244.tsv has a name column").clone();
        let tx_hex = row
            .get("tx_hex")
            .expect("zip244.tsv has a tx_hex column")
            .clone();
        let auth_digest_hex = row
            .get("auth_digest_hex")
            .expect("zip244.tsv has an auth_digest_hex column")
            .to_lowercase();
        assert_eq!(
            auth_digest_hex.len(),
            64,
            "{}: auth_digest_hex is not 32 bytes",
            name
        );
        let raw = hex::decode(&tx_hex).unwrap_or_else(|e| panic!("{}: tx_hex not hex: {}", name, e));
        let tx = Transaction::zcash_deserialize(Cursor::new(&raw[..]))
            .unwrap_or_else(|e| panic!("{}: the v5 vector did not parse: {:?}", name, e));
        out.push(V5Vector {
            name,
            tx,
            auth_digest_hex,
        });
    }
    out
}

// ----------------------------------------------------------------- helpers

/// The root of an `AuthDataRoot`, as 32 bytes.
///
/// `AuthDataRoot` keeps its inner array private and exposes no accessor, so
/// the only documented way out is `Debug`, whose format §5.2 makes normative:
/// exactly `AuthRoot("<64 lowercase hex characters>")`. The wrapper, the
/// quoting and the lowercase hex are all asserted here — a rendering that
/// merely *contains* a 64-character hex run does not satisfy the contract.
fn root_bytes(root: &AuthDataRoot) -> [u8; 32] {
    let rendered = format!("{:?}", root);
    let inner = rendered
        .strip_prefix("AuthRoot(\"")
        .and_then(|rest| rest.strip_suffix("\")"))
        .unwrap_or_else(|| {
            panic!(
                "the Debug rendering of an AuthDataRoot must be \
                 `AuthRoot(\"<64 lowercase hex characters>\")`; got {:?}",
                rendered
            )
        });

    assert_eq!(
        inner.len(),
        64,
        "the Debug rendering must hold the 64-character hex encoding of the \
         32-byte root; got {:?}",
        rendered
    );
    assert!(
        inner
            .chars()
            .all(|c| c.is_ascii_digit() || ('a'..='f').contains(&c)),
        "the hex in the Debug rendering must be lowercase; got {:?}",
        rendered
    );

    let bytes = hex::decode(inner).expect("64 hex characters decode");
    bytes.try_into().expect("32 bytes")
}

fn root_hex(root: &AuthDataRoot) -> String {
    hex::encode(root_bytes(root))
}

/// The `AuthDataRoot` over a single transaction. For a one-leaf tree the root
/// is the leaf itself, so this is how the driver observes one transaction's
/// authorizing-data commitment without an accessor on `AuthDigest`.
fn root_of_one(tx: Transaction) -> AuthDataRoot {
    iter::once(Arc::new(tx)).collect()
}

// -- an independent re-implementation of ZIP-244's auth data Merkle tree -----

/// One interior node: BLAKE2b-256 over the two children, personalised.
fn node(h1: &[u8; 32], h2: &[u8; 32], personal: &[u8; 16]) -> [u8; 32] {
    let digest = blake2b_simd::Params::new()
        .hash_length(32)
        .personal(personal)
        .to_state()
        .update(h1)
        .update(h2)
        .finalize();
    let mut out = [0u8; 32];
    out.copy_from_slice(digest.as_bytes());
    out
}

/// The root of a full binary Merkle tree over `leaves`, padded up to the next
/// power of two with `pad`, with interior nodes personalised by `personal`.
fn merkle_root(leaves: &[[u8; 32]], pad: [u8; 32], personal: &[u8; 16]) -> [u8; 32] {
    assert!(!leaves.is_empty(), "an auth data tree has at least one leaf");
    let mut level: Vec<[u8; 32]> = leaves.to_vec();
    let full = level.len().next_power_of_two();
    while level.len() < full {
        level.push(pad);
    }
    while level.len() > 1 {
        let mut next = Vec::with_capacity(level.len() / 2);
        for pair in level.chunks(2) {
            next.push(node(&pair[0], &pair[1], personal));
        }
        level = next;
    }
    level[0]
}

/// ZIP-244's personalisation for the auth data tree's interior nodes.
const AUTH_DATA_PERSONAL: &[u8; 16] = b"ZcashAuthDatHash";

/// ZIP-244's placeholder commitment for a pre-v5 transaction.
const PRE_V5_PLACEHOLDER: [u8; 32] = [0xFF; 32];

/// ZIP-244's padding leaf.
const PADDING_LEAF: [u8; 32] = [0x00; 32];

/// The expected root for a block all of whose transactions are pre-v5.
fn expected_pre_v5_root(tx_count: usize) -> [u8; 32] {
    let leaves = vec![PRE_V5_PLACEHOLDER; tx_count];
    merkle_root(&leaves, PADDING_LEAF, AUTH_DATA_PERSONAL)
}

// ------------------------------------------------------------------- tests

#[test]
fn pre_v5_transactions_have_no_authorizing_data_commitment() {
    // > For transaction versions before v5, a placeholder value consisting of
    // > 32 bytes of 0xFF is used in place of the authorizing data commitment.
    // There is no digest to return for them, so the accessor reports None.
    let mut seen = 0usize;
    for fixture in corpus() {
        for (index, tx) in fixture.block.transactions.iter().enumerate() {
            assert!(
                tx.version() < 5,
                "{}: transaction {} is v{}; this corpus is all pre-NU5",
                fixture.name,
                index,
                tx.version()
            );
            assert!(
                tx.auth_digest().is_none(),
                "{}: transaction {} is v{} but reported an auth_digest",
                fixture.name,
                index,
                tx.version()
            );
            seen += 1;
        }
    }
    assert!(
        seen >= 17,
        "expected to inspect at least one transaction per corpus block, saw {}",
        seen
    );
}

#[test]
fn v5_transactions_have_an_authorizing_data_commitment() {
    let vectors = v5_vectors();
    assert!(vectors.len() >= 10, "zip244.tsv shrank to {} rows", vectors.len());
    for vector in vectors {
        assert_eq!(
            vector.tx.version(),
            5,
            "{}: the ZIP-244 vectors are all v5 transactions",
            vector.name
        );
        assert!(
            vector.tx.auth_digest().is_some(),
            "{}: a v5 transaction must have an auth_digest",
            vector.name
        );
    }
}

#[test]
fn the_auth_digest_matches_the_published_zip244_vectors() {
    // A one-transaction tree has one leaf and no interior node, so its root is
    // that transaction's authorizing-data commitment verbatim. That makes this
    // a direct comparison against the ZIP-244 test vectors.
    for vector in v5_vectors() {
        // §5.1 gives two entry points to the same commitment, and on a v5
        // transaction they must agree: the infallible `From<&Transaction>`
        // conversion and the fallible `auth_digest` accessor.  This is the
        // only place a scored assertion reads a *value* out of the conversion
        // -- the `#[should_panic]` case below reaches it only to show it
        // panics pre-v5, and the tree is built through `auth_digest`.  Without
        // this line a `From` impl that panics unconditionally, or answers a
        // constant, passes every case in this file.  Paired with the root
        // comparison below, which pins `auth_digest` to the published vector,
        // it pins the conversion to the published vector too.
        let via_accessor = vector
            .tx
            .auth_digest()
            .expect("a v5 transaction has an authorizing data commitment");
        assert_eq!(
            AuthDigest::from(&vector.tx),
            via_accessor,
            "{}: From<&Transaction> and Transaction::auth_digest must yield the \
             same authorizing data commitment for a v5 transaction",
            vector.name
        );

        let root = root_of_one(vector.tx);
        assert_eq!(
            root_hex(&root),
            vector.auth_digest_hex,
            "{}: the authorizing-data commitment does not match the ZIP-244 vector",
            vector.name
        );
    }
}

#[test]
fn a_lone_pre_v5_transaction_commits_to_the_all_ones_placeholder() {
    // > This is only used in the tree committed to by hashAuthDataRoot.
    let expected = hex::encode(PRE_V5_PLACEHOLDER);
    for fixture in corpus() {
        let first = (*fixture.block.transactions[0]).clone();
        let root = root_of_one(first);
        assert_eq!(
            root_hex(&root),
            expected,
            "{}: a one-leaf tree over a pre-v5 transaction must be the 0xFF placeholder",
            fixture.name
        );
    }
}

#[test]
fn the_block_auth_data_root_matches_an_independent_recomputation() {
    for fixture in corpus() {
        let expected = expected_pre_v5_root(fixture.tx_count);
        let actual = fixture.block.auth_data_root();
        assert_eq!(
            root_hex(&actual),
            hex::encode(expected),
            "{}: auth_data_root over {} pre-v5 transactions does not match the \
             independently computed ZIP-244 root",
            fixture.name,
            fixture.tx_count
        );
    }
}

#[test]
fn the_tree_is_padded_to_a_power_of_two_with_the_all_zero_leaf() {
    // Blocks whose transaction count is already a power of two cannot tell
    // padding apart from no padding, so this test insists on finding one that
    // is not, and then shows the padding leaf is 0x00 and not, say, 0xFF.
    let mut checked = 0usize;
    for fixture in corpus() {
        if fixture.tx_count.is_power_of_two() {
            continue;
        }
        let leaves = vec![PRE_V5_PLACEHOLDER; fixture.tx_count];
        let with_zero_padding = merkle_root(&leaves, PADDING_LEAF, AUTH_DATA_PERSONAL);
        let with_ones_padding = merkle_root(&leaves, PRE_V5_PLACEHOLDER, AUTH_DATA_PERSONAL);
        assert_ne!(
            with_zero_padding, with_ones_padding,
            "{}: the test itself is broken -- {} leaves should be sensitive to the padding value",
            fixture.name, fixture.tx_count
        );
        let actual = root_bytes(&fixture.block.auth_data_root());
        assert_eq!(
            hex::encode(actual),
            hex::encode(with_zero_padding),
            "{}: {} leaves must be padded up to {} with the all-zero digest",
            fixture.name,
            fixture.tx_count,
            fixture.tx_count.next_power_of_two()
        );
        checked += 1;
    }
    assert!(
        checked > 0,
        "no corpus block has a transaction count that is not a power of two; \
         the padding rule was not exercised"
    );
}

#[test]
fn interior_nodes_are_personalised_with_zcash_auth_dat_hash() {
    // Same tree, one byte different in the personalisation: if the crate were
    // using any other personalisation the roots below would collide with the
    // wrong one instead.
    let mut wrong = *AUTH_DATA_PERSONAL;
    wrong[15] = b'H';
    let mut checked = 0usize;
    for fixture in corpus() {
        if fixture.tx_count < 2 {
            continue;
        }
        let leaves = vec![PRE_V5_PLACEHOLDER; fixture.tx_count];
        let right = merkle_root(&leaves, PADDING_LEAF, AUTH_DATA_PERSONAL);
        let other = merkle_root(&leaves, PADDING_LEAF, &wrong);
        assert_ne!(
            right, other,
            "{}: the test itself is broken -- the personalisation must matter",
            fixture.name
        );
        let actual = root_bytes(&fixture.block.auth_data_root());
        assert_eq!(
            hex::encode(actual),
            hex::encode(right),
            "{}: interior nodes must be BLAKE2b-256 personalised ZcashAuthDatHash",
            fixture.name
        );
        checked += 1;
    }
    assert!(
        checked > 0,
        "no corpus block has two or more transactions; no interior node was built"
    );
}

#[test]
fn a_pre_v5_block_root_depends_only_on_its_transaction_count() {
    // Every leaf of a pre-v5 block's tree is the same fixed placeholder, so
    // two such blocks agree exactly when their transaction counts agree. This
    // holds for seventeen unrelated real blocks only if the placeholder rule
    // is being applied to every transaction, not just to some of them.
    let corpus = corpus();
    let mut compared = 0usize;
    for (i, a) in corpus.iter().enumerate() {
        for b in corpus.iter().skip(i + 1) {
            let ra = root_hex(&a.block.auth_data_root());
            let rb = root_hex(&b.block.auth_data_root());
            if a.tx_count == b.tx_count {
                assert_eq!(
                    ra, rb,
                    "{} and {} both have {} pre-v5 transactions but disagree on the root",
                    a.name, b.name, a.tx_count
                );
            } else {
                assert_ne!(
                    ra, rb,
                    "{} ({} transactions) and {} ({} transactions) share a root",
                    a.name, a.tx_count, b.name, b.tx_count
                );
            }
            compared += 1;
        }
    }
    assert!(compared > 100, "only {} block pairs compared", compared);
}

#[test]
fn block_auth_data_root_agrees_with_collecting_the_blocks_transactions() {
    // `Block::auth_data_root` is documented as exactly the `FromIterator`
    // collection over the block's transactions; nothing may be added, removed
    // or reordered on the way.
    for fixture in corpus() {
        let collected: AuthDataRoot = fixture.block.transactions.iter().collect();
        assert_eq!(
            root_hex(&fixture.block.auth_data_root()),
            root_hex(&collected),
            "{}: auth_data_root disagrees with collecting the block's transactions",
            fixture.name
        );
        assert_eq!(
            fixture.block.auth_data_root(),
            collected,
            "{}: AuthDataRoot equality disagrees with its own rendering",
            fixture.name
        );
    }
}

/// A V1 transaction built inline rather than read from the corpus, so that a
/// missing or unreadable fixture cannot panic and turn a panic-expecting case
/// into a false pass.
fn inline_pre_v5_transaction() -> Transaction {
    Transaction::V1 {
        inputs: Vec::new(),
        outputs: Vec::new(),
        lock_time: LockTime::Height(Height(0)),
    }
}

#[test]
fn a_pre_v5_transaction_has_no_authorizing_data_commitment() {
    // The interface gives two ways to reach the digest and they differ in what
    // they do with a pre-v5 transaction.  This case pins the fallible one:
    // `Transaction::auth_digest` answers `None`, it does not panic.
    let pre_v5 = inline_pre_v5_transaction();
    assert_eq!(
        pre_v5.auth_digest(),
        None,
        "a V1 transaction has no authorizing data commitment"
    );
}

#[test]
#[should_panic]
fn converting_a_pre_v5_transaction_into_an_auth_digest_panics() {
    // The other half of the same contract: the `From<&Transaction>` conversion
    // has no way to say "there is no commitment here" and panics instead.
    // Every other case in this file goes through `auth_digest`, so without this
    // one the panic half is never exercised and an implementation that returns
    // a zero digest, or the all-ones placeholder, passes.
    //
    // The conversion is the *only* call in the body, so no other panic site can
    // satisfy `#[should_panic]`: an `auth_digest` that panicked instead of
    // answering `None` is caught by the case above, not silently accepted here.
    // The intermediate binding keeps the result alive without unwrapping it --
    // `.expect(..)` would swallow the conversion's panic into a different one.
    let pre_v5 = inline_pre_v5_transaction();
    let _digest = AuthDigest::from(&pre_v5);
}

#[test]
fn the_auth_data_root_accumulates_a_plain_iterator_of_auth_digests() {
    // §4.2 requires `FromIterator<AuthDigest> for AuthDataRoot` alongside the
    // blanket `FromIterator<T> where T: AsRef<Transaction>`.  The blanket impl
    // is not the one a FlyClient-style caller reaches: it has the digests in
    // hand, not the transactions they came from, and re-deriving them from
    // transactions would be both slower and a second implementation to keep in
    // step.  Every other case in this file goes through the blanket impl (via
    // `root_of_one` and `Block::auth_data_root`); this one collects digests.
    let vectors = v5_vectors();
    let digests: Vec<AuthDigest> = vectors.iter().map(|v| AuthDigest::from(&v.tx)).collect();
    assert!(
        digests.len() >= 10,
        "zip244.tsv shrank to {} rows",
        digests.len()
    );

    // The same digests, collected directly, must produce the root the
    // transaction-based path produces over the same transactions.
    let from_digests: AuthDataRoot = digests.iter().copied().collect();
    let from_txs: AuthDataRoot = vectors.iter().map(|v| Arc::new(v.tx.clone())).collect();
    assert_eq!(
        root_hex(&from_digests),
        root_hex(&from_txs),
        "collecting AuthDigests directly must agree with collecting their transactions"
    );

    // And that root is the ZIP-244 one the published vectors pin, so this is
    // not merely two code paths agreeing with each other.
    // A one-leaf tree's root is its leaf, so `root_of_one` is the only way to
    // read a digest back out without an accessor on `AuthDigest`.
    let leaves: Vec<[u8; 32]> = vectors
        .iter()
        .map(|v| {
            let mut bytes = [0u8; 32];
            bytes.copy_from_slice(&root_bytes(&root_of_one(v.tx.clone())));
            bytes
        })
        .collect();
    assert_eq!(
        root_hex(&from_digests),
        hex::encode(merkle_root(&leaves, PADDING_LEAF, AUTH_DATA_PERSONAL)),
        "the digest-collected root must match an independent recomputation"
    );

    // A single digest is not padded away: one leaf is already a full tree.
    let one: AuthDataRoot = std::iter::once(digests[0]).collect();
    assert_eq!(
        root_hex(&one),
        root_hex(&root_of_one(vectors[0].tx.clone())),
        "a one-digest tree must be that digest's own leaf hash"
    );
}
