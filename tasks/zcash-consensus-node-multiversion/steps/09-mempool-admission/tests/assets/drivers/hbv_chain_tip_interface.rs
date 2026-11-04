//! The chain-tip interface the mempool is driven by.
//!
//! Harbor verifier driver -- injected into `zebra-chain/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! A mempool is only meaningful relative to a chain tip: a transaction that is
//! valid on top of block N may be invalid on top of block N+1, and every
//! transaction in the tip block itself must be dropped. So the node needs a way
//! for the parts that hold unconfirmed transactions to ask "where is the tip,
//! and what was in it?" without depending on the state service, and without
//! the state service depending on them. That is `zebra_chain::chain_tip`.
//!
//! Two things live there and both are new at this milestone:
//!
//!   * the `ChainTip` trait, with exactly three queries -- the tip height, the
//!     tip hash, and the *mined* transaction IDs of the transactions in the tip
//!     block. The last one is the mempool's eviction list, which is why it is
//!     the mined ID (`transaction::Hash`) and not the unmined ID: a transaction
//!     in the tip block must evict the mempool entry for the same transaction
//!     even if the mempool holds it with different authorizing data.
//!   * `NoChainTip`, the always-empty implementation, which is what a component
//!     is given when there is no state to talk to. It reports no height, no
//!     hash, and an empty transaction list -- never a panic and never a
//!     placeholder value.
//!
//! The other half of the contract is `NetworkUpgrade::is_activation_height`,
//! also new here. ZIP-200 requires the mempool to be cleared when the tip
//! reaches a network upgrade activation height, so the tip-change consumer has
//! to be able to recognise such a height without reconstructing the activation
//! table itself.
//!
//! The concrete tip types that the state service publishes are *not* reachable
//! from an integration test at this milestone (`zebra_state`'s `service` module
//! is private and only `LatestChainTip` is re-exported, with no public
//! constructor), so this group scores the trait, its empty implementation, and
//! the activation-height predicate -- the parts every consumer is written
//! against. See TEST_INTERFACE §9.
//!
//! Expected values: the shipped seventeen-block corpus (heights, hashes and
//! merkle roots come from the TSV, which was derived from the block bytes in
//! Python, not from this crate) and the published activation heights.

use std::collections::BTreeMap;
use std::io::Cursor;
use std::path::PathBuf;
use std::sync::Arc;

use zebra_chain::block::{self, Block};
use zebra_chain::chain_tip::{ChainTip, NoChainTip};
use zebra_chain::parameters::{Network, NetworkUpgrade};
use zebra_chain::serialization::ZcashDeserialize;
use zebra_chain::transaction;

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

fn thirty_two(text: &str, name: &str, column: &str) -> [u8; 32] {
    let bytes =
        hex::decode(text).unwrap_or_else(|e| panic!("{}: {} is not hex: {}", name, column, e));
    assert_eq!(bytes.len(), 32, "{}: {} is not 32 bytes", name, column);
    let mut out = [0u8; 32];
    out.copy_from_slice(&bytes);
    out
}

/// One real block from the shipped corpus, with the values the TSV records for
/// it.
struct BlockFixture {
    name: String,
    network: Network,
    height: u32,
    /// Display (byte-reversed) hex, as `block::Hash: Display` prints it.
    block_hash: String,
    /// Raw wire order, as it appears in the 1487-byte header.
    merkle_root: [u8; 32],
    block: Block,
}

fn corpus() -> Vec<BlockFixture> {
    let dir = fixture_dir();
    let mut out = Vec::new();
    for row in rows("blocks.tsv") {
        let name = row
            .get("name")
            .expect("blocks.tsv has a name column")
            .clone();
        let network_text = row
            .get("network")
            .expect("blocks.tsv has a network column")
            .to_lowercase();
        let network = match network_text.as_str() {
            "mainnet" | "main" => Network::Mainnet,
            "testnet" | "test" => Network::Testnet,
            other => panic!("{}: unknown network {}", name, other),
        };
        let height: u32 = row
            .get("height")
            .expect("blocks.tsv has a height column")
            .parse()
            .unwrap_or_else(|e| panic!("{}: height is not a number: {}", name, e));
        let block_hash = row
            .get("block_hash")
            .expect("blocks.tsv has a block_hash column")
            .clone();
        let merkle_root = thirty_two(
            row.get("merkle_root_hex")
                .expect("blocks.tsv has a merkle_root_hex column"),
            &name,
            "merkle_root_hex",
        );
        let text = std::fs::read_to_string(dir.join("blocks").join(format!("{}.hex", name)))
            .unwrap_or_else(|e| panic!("cannot read block {}: {}", name, e));
        let raw = hex::decode(text.trim()).expect("block fixture is hex");
        let block = Block::zcash_deserialize(Cursor::new(&raw[..]))
            .unwrap_or_else(|e| panic!("{} did not parse: {:?}", name, e));

        out.push(BlockFixture {
            name,
            network,
            height,
            block_hash,
            merkle_root,
            block,
        });
    }
    // Anti-degeneracy guard only: this is a property of the shipped corpus, not
    // of the code under test. A failure here means blocks.tsv changed.
    assert_eq!(
        out.len(),
        17,
        "the corpus has {} blocks; the verifier expects the shipped 17",
        out.len()
    );
    out
}

// ----------------------------------------------------------------- helpers

/// A chain tip built in this test file, out of a real block, using nothing but
/// the trait's own declaration. If `ChainTip` does not have exactly the three
/// documented methods with the documented signatures, this does not compile.
struct TestChainTip {
    height: Option<block::Height>,
    hash: Option<block::Hash>,
    mined_ids: Arc<[transaction::Hash]>,
}

impl TestChainTip {
    fn from_block(block: &Block) -> TestChainTip {
        let mined_ids: Vec<transaction::Hash> =
            block.transactions.iter().map(|tx| tx.hash()).collect();
        TestChainTip {
            height: block.coinbase_height(),
            hash: Some(block.hash()),
            mined_ids: mined_ids.into(),
        }
    }
}

impl ChainTip for TestChainTip {
    fn best_tip_height(&self) -> Option<block::Height> {
        self.height
    }

    fn best_tip_hash(&self) -> Option<block::Hash> {
        self.hash
    }

    fn best_tip_mined_transaction_ids(&self) -> Arc<[transaction::Hash]> {
        self.mined_ids.clone()
    }
}

/// Reads a tip through a generic bound, the way every consumer in the node is
/// written. Returns the three queries as one tuple.
fn read_tip<T: ChainTip>(
    tip: &T,
) -> (
    Option<block::Height>,
    Option<block::Hash>,
    Arc<[transaction::Hash]>,
) {
    (
        tip.best_tip_height(),
        tip.best_tip_hash(),
        tip.best_tip_mined_transaction_ids(),
    )
}

/// Reads a tip through a trait object, which only compiles if `ChainTip` is
/// object safe.
fn read_tip_dyn(tip: &dyn ChainTip) -> (Option<block::Height>, usize) {
    (
        tip.best_tip_height(),
        tip.best_tip_mined_transaction_ids().len(),
    )
}

/// The published network upgrade activation heights, restated here so that
/// nothing in the crate contributes to the expected value.
///
/// Mainnet: Genesis 0, BeforeOverwinter 1, Overwinter 347_500, Sapling
/// 419_200, Blossom 653_600, Heartwood 903_000, Canopy 1_046_400.
/// Testnet: Genesis 0, BeforeOverwinter 1, Overwinter 207_500, Sapling
/// 280_000, Blossom 584_000, Heartwood 903_800, Canopy 1_028_500.
/// Nu5 has no activation height on either network.
fn published_activation_heights(network: Network) -> Vec<u32> {
    match network {
        Network::Mainnet => vec![0, 1, 347_500, 419_200, 653_600, 903_000, 1_046_400],
        Network::Testnet => vec![0, 1, 207_500, 280_000, 584_000, 903_800, 1_028_500],
    }
}

/// Heights that are deliberately *not* activation heights: one either side of
/// every published height, plus a few round numbers.
fn non_activation_heights(network: Network) -> Vec<u32> {
    let mut out: Vec<u32> = vec![2, 100, 1_000, 500_000, 2_000_000];
    for height in published_activation_heights(network) {
        if height > 1 {
            out.push(height - 1);
            out.push(height + 1);
        }
    }
    let published = published_activation_heights(network);
    out.retain(|h| !published.contains(h));
    out
}

// ------------------------------------------------------------------- tests

#[test]
fn an_empty_chain_tip_reports_no_height_no_hash_and_no_transactions() {
    let tip = NoChainTip;

    assert_eq!(
        tip.best_tip_height(),
        None,
        "NoChainTip reported a tip height"
    );
    assert_eq!(tip.best_tip_hash(), None, "NoChainTip reported a tip hash");

    let ids = tip.best_tip_mined_transaction_ids();
    assert_eq!(
        ids.len(),
        0,
        "NoChainTip reported {} mined transaction ids, expected none",
        ids.len()
    );

    // Asked twice, it must still be empty: an empty tip is a value, not a
    // one-shot.
    assert_eq!(tip.best_tip_mined_transaction_ids().len(), 0);
    assert_eq!(tip.best_tip_height(), None);
}

#[test]
fn the_empty_chain_tip_is_a_copyable_unit_value() {
    // `NoChainTip` is handed to components by value and kept, so it must be
    // usable after it is passed somewhere else.
    let tip = NoChainTip;
    let copied = tip;

    assert_eq!(tip, copied, "two NoChainTip values must compare equal");
    assert_eq!(
        NoChainTip, NoChainTip,
        "NoChainTip must implement PartialEq/Eq"
    );

    // `tip` is still usable here, which only compiles if `NoChainTip: Copy`.
    assert_eq!(tip.best_tip_height(), None);
    assert_eq!(copied.best_tip_hash(), None);

    let cloned = tip.clone();
    assert_eq!(cloned.best_tip_mined_transaction_ids().len(), 0);
}

#[test]
fn the_chain_tip_trait_can_be_implemented_outside_zebra_chain() {
    // The trait exists so that zebra-network and zebrad can be written against
    // a tip without depending on zebra-state. If its method set or signatures
    // differ from the contract, `impl ChainTip for TestChainTip` above does not
    // compile.
    for fixture in corpus() {
        let tip = TestChainTip::from_block(&fixture.block);

        assert_eq!(
            tip.best_tip_height(),
            Some(block::Height(fixture.height)),
            "{}: the tip height is not the block's height",
            fixture.name
        );
        assert_eq!(
            tip.best_tip_hash().map(|hash| hash.to_string()),
            Some(fixture.block_hash.clone()),
            "{}: the tip hash is not the block's hash",
            fixture.name
        );
        assert_eq!(
            tip.best_tip_mined_transaction_ids().len(),
            fixture.block.transactions.len(),
            "{}: the tip reported a different number of transactions than the block has",
            fixture.name
        );
    }
}

#[test]
fn the_mined_transaction_ids_are_the_tip_blocks_transactions_in_block_order() {
    // The mempool drops an entry when its *mined* id appears in the tip block,
    // so this list must be the block's transaction hashes, in the block's own
    // order. The merkle root in the header fixes both the values and the order:
    // it was computed by miners years ago and is read here from the TSV.
    for fixture in corpus() {
        let tip = TestChainTip::from_block(&fixture.block);
        let ids = tip.best_tip_mined_transaction_ids();

        let root: block::merkle::Root = ids.iter().cloned().collect();
        assert_eq!(
            root.0, fixture.merkle_root,
            "{}: the merkle root of the tip's mined transaction ids is not the root in the header",
            fixture.name
        );

        // The ids are mined ids, 32 bytes each, not witnessed ids.
        for (index, id) in ids.iter().enumerate() {
            assert_eq!(
                id.to_string().len(),
                64,
                "{}#{}: a mined transaction id must display as 64 hex characters",
                fixture.name,
                index
            );
        }
    }
}

#[test]
fn a_chain_tip_is_readable_through_a_generic_bound_and_a_trait_object() {
    let fixture = corpus().remove(0);
    let tip = TestChainTip::from_block(&fixture.block);

    let (height, hash, ids) = read_tip(&tip);
    assert_eq!(height, Some(block::Height(fixture.height)));
    assert_eq!(hash.map(|h| h.to_string()), Some(fixture.block_hash.clone()));
    assert_eq!(ids.len(), fixture.block.transactions.len());

    let (empty_height, empty_hash, empty_ids) = read_tip(&NoChainTip);
    assert_eq!(empty_height, None);
    assert_eq!(empty_hash, None);
    assert_eq!(empty_ids.len(), 0);

    // Object safety: the node stores tips behind pointers, so the trait must be
    // usable as `dyn ChainTip`.
    let boxed: Box<dyn ChainTip> = Box::new(NoChainTip);
    assert_eq!(read_tip_dyn(boxed.as_ref()), (None, 0));
    assert_eq!(
        read_tip_dyn(&tip),
        (
            Some(block::Height(fixture.height)),
            fixture.block.transactions.len()
        )
    );
}

#[test]
fn is_activation_height_is_true_exactly_at_the_published_activation_heights() {
    for network in &[Network::Mainnet, Network::Testnet] {
        for height in published_activation_heights(*network) {
            assert!(
                NetworkUpgrade::is_activation_height(*network, block::Height(height)),
                "{:?} height {} is a published activation height but was not recognised",
                network,
                height
            );
        }
        for height in non_activation_heights(*network) {
            assert!(
                !NetworkUpgrade::is_activation_height(*network, block::Height(height)),
                "{:?} height {} is not an activation height but was recognised as one",
                network,
                height
            );
        }
    }
}

#[test]
fn is_activation_height_does_not_confuse_the_two_networks() {
    // Every mainnet activation height above 1 differs from every testnet one,
    // so a table that is shared between the networks is visible here.
    for height in &[347_500u32, 419_200, 653_600, 903_000, 1_046_400] {
        assert!(
            NetworkUpgrade::is_activation_height(Network::Mainnet, block::Height(*height)),
            "mainnet height {} was not recognised",
            height
        );
        assert!(
            !NetworkUpgrade::is_activation_height(Network::Testnet, block::Height(*height)),
            "mainnet height {} was recognised as a testnet activation height",
            height
        );
    }
    for height in &[207_500u32, 280_000, 584_000, 903_800, 1_028_500] {
        assert!(
            NetworkUpgrade::is_activation_height(Network::Testnet, block::Height(*height)),
            "testnet height {} was not recognised",
            height
        );
        assert!(
            !NetworkUpgrade::is_activation_height(Network::Mainnet, block::Height(*height)),
            "testnet height {} was recognised as a mainnet activation height",
            height
        );
    }
}

#[test]
fn is_activation_height_agrees_with_the_upgrade_table_it_summarises() {
    // Cross-check against `activation_height`, which is inherited from an
    // earlier milestone: the new predicate must be true for a height if and
    // only if some upgrade activates there.
    let upgrades = [
        NetworkUpgrade::Genesis,
        NetworkUpgrade::BeforeOverwinter,
        NetworkUpgrade::Overwinter,
        NetworkUpgrade::Sapling,
        NetworkUpgrade::Blossom,
        NetworkUpgrade::Heartwood,
        NetworkUpgrade::Canopy,
        NetworkUpgrade::Nu5,
    ];

    for network in &[Network::Mainnet, Network::Testnet] {
        let mut activation_heights: Vec<u32> = Vec::new();
        for upgrade in upgrades.iter() {
            if let Some(height) = upgrade.activation_height(*network) {
                activation_heights.push(height.0);
            }
        }
        activation_heights.sort_unstable();

        let mut published = published_activation_heights(*network);
        published.sort_unstable();
        assert_eq!(
            activation_heights, published,
            "{:?}: activation_height does not reproduce the published table",
            network
        );

        for height in activation_heights.iter() {
            assert!(
                NetworkUpgrade::is_activation_height(*network, block::Height(*height)),
                "{:?}: height {} has an upgrade but is_activation_height says otherwise",
                network,
                height
            );
        }
    }
}

#[test]
fn every_corpus_block_is_classified_by_is_activation_height() {
    // The corpus straddles four mainnet activations and one testnet one, and
    // deliberately includes the block before and the block after several of
    // them. A failure naming one of those neighbours means the predicate is off
    // by one.
    let mut recognised = 0usize;
    for fixture in corpus() {
        let expected = published_activation_heights(fixture.network).contains(&fixture.height);
        let actual =
            NetworkUpgrade::is_activation_height(fixture.network, block::Height(fixture.height));

        assert_eq!(
            actual, expected,
            "{}: is_activation_height({:?}, {}) returned {}, expected {}",
            fixture.name, fixture.network, fixture.height, actual, expected
        );
        if expected {
            recognised += 1;
        }
    }
    // Corpus property, not a code property: if the corpus stops containing
    // activation blocks this test stops testing anything.
    assert!(
        recognised >= 5,
        "only {} corpus blocks sit at an activation height; the positive case is barely covered",
        recognised
    );
}
