//! The two NU5 consensus parameters this release moves.
//!
//! Harbor verifier driver -- injected into `zebra-chain/tests/` at grading
//! time. Public API only; no `zebra_test`, no proptest.
//!
//! NU5 is still a moving target while it is being specified, and this release
//! picks up two changes to its parameters. Neither is cosmetic: both change
//! which bytes a node will accept.
//!
//! 1. **The NU5 consensus branch id changes from `0xf919a198` to
//!    `0x37519621`.** A v5 transaction carries its branch id in its own
//!    header, so the branch id is a wire-format gate: a v5 transaction that
//!    names the superseded id must now be rejected at deserialization, and one
//!    this node builds must carry the new id. Every earlier upgrade keeps the
//!    branch id it has always had, because a branch id only changes for an
//!    upgrade that has not activated yet.
//!
//! 2. **NU5 gets a Testnet activation height, `1_599_200`.** Before this
//!    release the Testnet activation list simply stopped at Canopy, so every
//!    Testnet height from `1_028_500` upward was "currently Canopy". Now
//!    Testnet heights from `1_599_200` are NU5. Mainnet still has no NU5
//!    activation height, because none had been announced.
//!
//! The assertions below are all against literal constants, taken from the
//! branch-id and activation-height tables rather than from any value this
//! crate computes, and against raw serialized bytes read at fixed offsets.
//!
//! Wire layout of a v5 transaction header, little-endian throughout:
//!
//! ```text
//! offset 0..4    header          0x80000000 | 5   (fOverwintered set)
//! offset 4..8    nVersionGroupId 0x26A7270A
//! offset 8..12   nConsensusBranchId
//! offset 12..16  lock_time
//! offset 16..20  nExpiryHeight
//! ```

use std::io::Cursor;

use zebra_chain::block::Height;
use zebra_chain::parameters::{ConsensusBranchId, Network, NetworkUpgrade};
use zebra_chain::serialization::{ZcashDeserialize, ZcashSerialize};
use zebra_chain::transaction::{LockTime, Transaction};

// ------------------------------------------------ constants under test

/// The NU5 consensus branch id this release adopts.
const NU5_BRANCH_ID: u32 = 0x3751_9621;

/// The NU5 consensus branch id this release replaces.
const SUPERSEDED_NU5_BRANCH_ID: u32 = 0xf919_a198;

/// The NU5 Testnet activation height this release adds.
const NU5_TESTNET_ACTIVATION_HEIGHT: u32 = 1_599_200;

/// `nVersionGroupId` for a version 5 transaction.
const TX_V5_VERSION_GROUP_ID: u32 = 0x26A7_270A;

// ----------------------------------------------------------------- helpers

/// A v5 transaction on the NU5 branch with no inputs, outputs or bundles.
fn empty_v5() -> Transaction {
    Transaction::V5 {
        network_upgrade: NetworkUpgrade::Nu5,
        lock_time: LockTime::min_lock_time(),
        expiry_height: Height(0),
        inputs: Vec::new(),
        outputs: Vec::new(),
        sapling_shielded_data: None,
        orchard_shielded_data: None,
    }
}

fn little_endian_u32(bytes: &[u8], offset: usize, what: &str) -> u32 {
    assert!(
        bytes.len() >= offset + 4,
        "{}: only {} serialized bytes, need at least {}",
        what,
        bytes.len(),
        offset + 4
    );
    (bytes[offset] as u32)
        | ((bytes[offset + 1] as u32) << 8)
        | ((bytes[offset + 2] as u32) << 16)
        | ((bytes[offset + 3] as u32) << 24)
}

fn put_little_endian_u32(bytes: &mut [u8], offset: usize, value: u32) {
    bytes[offset] = (value & 0xff) as u8;
    bytes[offset + 1] = ((value >> 8) & 0xff) as u8;
    bytes[offset + 2] = ((value >> 16) & 0xff) as u8;
    bytes[offset + 3] = ((value >> 24) & 0xff) as u8;
}

fn branch_id_of(upgrade: NetworkUpgrade) -> Option<u32> {
    upgrade.branch_id().map(u32::from)
}

// ------------------------------------------------------------------- tests

#[test]
fn nu5_reports_the_branch_id_this_release_adopts() {
    assert_eq!(
        branch_id_of(NetworkUpgrade::Nu5),
        Some(NU5_BRANCH_ID),
        "NU5 reported branch id {:?}, expected {:#010x}",
        branch_id_of(NetworkUpgrade::Nu5),
        NU5_BRANCH_ID
    );
    assert_ne!(
        branch_id_of(NetworkUpgrade::Nu5),
        Some(SUPERSEDED_NU5_BRANCH_ID),
        "NU5 still reports the superseded branch id {:#010x}",
        SUPERSEDED_NU5_BRANCH_ID
    );
}

#[test]
fn every_upgrade_before_nu5_keeps_its_published_branch_id() {
    // Branch ids for activated upgrades are fixed forever; only an upgrade
    // that has not activated can still change its id.
    let published = [
        (NetworkUpgrade::Overwinter, 0x5ba8_1b19u32),
        (NetworkUpgrade::Sapling, 0x76b8_09bb),
        (NetworkUpgrade::Blossom, 0x2bb4_0e60),
        (NetworkUpgrade::Heartwood, 0xf5b9_230b),
        (NetworkUpgrade::Canopy, 0xe9ff_75a6),
    ];
    for (upgrade, expected) in published.iter() {
        assert_eq!(
            branch_id_of(*upgrade),
            Some(*expected),
            "{:?} reported branch id {:?}, expected {:#010x}",
            upgrade,
            branch_id_of(*upgrade),
            expected
        );
    }
}

#[test]
fn the_upgrades_that_predate_branch_ids_report_none() {
    // Branch ids were introduced by Overwinter, so nothing before it has one.
    for upgrade in [NetworkUpgrade::Genesis, NetworkUpgrade::BeforeOverwinter].iter() {
        assert_eq!(
            branch_id_of(*upgrade),
            None,
            "{:?} reported a branch id, but branch ids start at Overwinter",
            upgrade
        );
    }
}

#[test]
fn a_branch_id_maps_back_to_exactly_one_network_upgrade() {
    assert_eq!(
        NetworkUpgrade::from_branch_id(NU5_BRANCH_ID),
        Some(NetworkUpgrade::Nu5),
        "{:#010x} does not map back to NU5",
        NU5_BRANCH_ID
    );
    assert_eq!(
        NetworkUpgrade::from_branch_id(SUPERSEDED_NU5_BRANCH_ID),
        None,
        "the superseded branch id {:#010x} is still recognised",
        SUPERSEDED_NU5_BRANCH_ID
    );
    assert_eq!(
        NetworkUpgrade::from_branch_id(0xe9ff_75a6),
        Some(NetworkUpgrade::Canopy),
        "0xe9ff75a6 does not map back to Canopy"
    );
    assert_eq!(
        NetworkUpgrade::from_branch_id(0),
        None,
        "branch id 0 is not a network upgrade"
    );
}

#[test]
fn a_v5_transaction_writes_the_current_branch_id_on_the_wire() {
    let raw = empty_v5()
        .zcash_serialize_to_vec()
        .expect("an empty v5 transaction serializes");

    assert_eq!(
        little_endian_u32(&raw, 0, "v5 header"),
        0x8000_0000 | 5,
        "a v5 transaction's header word is not fOverwintered | 5"
    );
    assert_eq!(
        little_endian_u32(&raw, 4, "v5 version group id"),
        TX_V5_VERSION_GROUP_ID,
        "a v5 transaction's version group id is not {:#010x}",
        TX_V5_VERSION_GROUP_ID
    );
    assert_eq!(
        little_endian_u32(&raw, 8, "v5 consensus branch id"),
        NU5_BRANCH_ID,
        "a v5 transaction carries branch id {:#010x} on the wire, expected {:#010x}",
        little_endian_u32(&raw, 8, "v5 consensus branch id"),
        NU5_BRANCH_ID
    );
}

#[test]
fn a_v5_transaction_naming_the_superseded_branch_id_is_rejected() {
    let good = empty_v5()
        .zcash_serialize_to_vec()
        .expect("an empty v5 transaction serializes");

    // The same bytes round trip unchanged while the branch id is current.
    let parsed = Transaction::zcash_deserialize(Cursor::new(&good[..]))
        .expect("a v5 transaction with the current branch id parses");
    assert_eq!(
        parsed.network_upgrade(),
        Some(NetworkUpgrade::Nu5),
        "a v5 transaction with branch id {:#010x} did not parse as NU5",
        NU5_BRANCH_ID
    );

    // Rewriting only the branch id makes the transaction unparseable, because
    // the superseded id names no network upgrade any more.
    let mut stale = good.clone();
    put_little_endian_u32(&mut stale, 8, SUPERSEDED_NU5_BRANCH_ID);
    assert_ne!(
        stale, good,
        "rewriting the branch id did not change the serialized bytes"
    );
    let result = Transaction::zcash_deserialize(Cursor::new(&stale[..]));
    assert!(
        result.is_err(),
        "a v5 transaction naming the superseded branch id {:#010x} was accepted: {:?}",
        SUPERSEDED_NU5_BRANCH_ID,
        result.map(|t| t.version())
    );
}

#[test]
fn nu5_activates_on_testnet_at_the_published_height() {
    assert_eq!(
        NetworkUpgrade::Nu5.activation_height(Network::Testnet),
        Some(Height(NU5_TESTNET_ACTIVATION_HEIGHT)),
        "NU5 reported Testnet activation height {:?}, expected {}",
        NetworkUpgrade::Nu5.activation_height(Network::Testnet),
        NU5_TESTNET_ACTIVATION_HEIGHT
    );
    assert!(
        NetworkUpgrade::is_activation_height(
            Network::Testnet,
            Height(NU5_TESTNET_ACTIVATION_HEIGHT)
        ),
        "Testnet height {} is not reported as an activation height",
        NU5_TESTNET_ACTIVATION_HEIGHT
    );

    assert_eq!(
        NetworkUpgrade::current(Network::Testnet, Height(NU5_TESTNET_ACTIVATION_HEIGHT)),
        NetworkUpgrade::Nu5,
        "Testnet height {} is not in NU5",
        NU5_TESTNET_ACTIVATION_HEIGHT
    );
    assert_eq!(
        NetworkUpgrade::current(Network::Testnet, Height(NU5_TESTNET_ACTIVATION_HEIGHT - 1)),
        NetworkUpgrade::Canopy,
        "the Testnet height just below NU5 activation is not in Canopy"
    );
    assert_eq!(
        ConsensusBranchId::current(Network::Testnet, Height(NU5_TESTNET_ACTIVATION_HEIGHT))
            .map(u32::from),
        Some(NU5_BRANCH_ID),
        "the branch id in force at Testnet NU5 activation is not {:#010x}",
        NU5_BRANCH_ID
    );
}

#[test]
fn nu5_has_no_mainnet_activation_height_yet() {
    assert_eq!(
        NetworkUpgrade::Nu5.activation_height(Network::Mainnet),
        None,
        "NU5 reported a Mainnet activation height, but none has been set at this milestone"
    );
    // Canopy therefore remains the current Mainnet upgrade at every height
    // above its own activation, including the highest block in the corpus.
    assert_eq!(
        NetworkUpgrade::current(Network::Mainnet, Height(1_046_400)),
        NetworkUpgrade::Canopy,
        "Mainnet Canopy activation height is not in Canopy"
    );
    assert_eq!(
        NetworkUpgrade::next(Network::Mainnet, Height(1_046_400)),
        None,
        "Mainnet reported an upgrade after Canopy, but NU5 has no Mainnet height yet"
    );
}
