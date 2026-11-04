//! NU5 enters the parameter tables, without an activation height.
//!
//! Harbor verifier driver -- injected into `zebra-chain/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! A network upgrade arrives in a node long before it arrives on the network.
//! This milestone adds `NetworkUpgrade::Nu5` and its consensus branch id so
//! that v5 transactions can name it, while deliberately leaving its activation
//! height unset on both networks: NU5 had no announced height at this point,
//! and inventing one would fork the node off mainnet.
//!
//! That "declared but not scheduled" state is the interesting one to test.
//! `Nu5` must have a branch id, must have a target block spacing, and must
//! never be returned by `current()` or `next()` at any height -- including
//! `Height::MAX`. The same release also promotes the transaction version group
//! IDs out of a private serialization constant into `parameters`, and adds the
//! Testnet-only start height for the maximum block time rule.

use chrono::Duration;

use zebra_chain::block::Height;
use zebra_chain::parameters::{
    ConsensusBranchId, Network, NetworkUpgrade, OVERWINTER_VERSION_GROUP_ID,
    SAPLING_VERSION_GROUP_ID, TESTNET_MAX_TIME_START_HEIGHT, TX_V5_VERSION_GROUP_ID,
};

// ----------------------------------------------------------------- fixtures
//
// The activation heights and consensus branch ids below are the ones published
// in ZIP-200's deployment table and on https://zips.z.cash; the version group
// IDs are from ZIP-202 (Overwinter), ZIP-243 (Sapling) and ZIP-225 (v5). None
// of them is read out of the crate under test.

const MAINNET_ACTIVATIONS: [(&str, u32); 7] = [
    ("Genesis", 0),
    ("BeforeOverwinter", 1),
    ("Overwinter", 347_500),
    ("Sapling", 419_200),
    ("Blossom", 653_600),
    ("Heartwood", 903_000),
    ("Canopy", 1_046_400),
];

const TESTNET_ACTIVATIONS: [(&str, u32); 7] = [
    ("Genesis", 0),
    ("BeforeOverwinter", 1),
    ("Overwinter", 207_500),
    ("Sapling", 280_000),
    ("Blossom", 584_000),
    ("Heartwood", 903_800),
    ("Canopy", 1_028_500),
];

/// Every upgrade, and the consensus branch id it binds transactions to.
/// `None` for the two upgrades that predate ZIP-200.
const BRANCH_IDS: [(&str, Option<u32>); 8] = [
    ("Genesis", None),
    ("BeforeOverwinter", None),
    ("Overwinter", Some(0x5ba8_1b19)),
    ("Sapling", Some(0x76b8_09bb)),
    ("Blossom", Some(0x2bb4_0e60)),
    ("Heartwood", Some(0xf5b9_230b)),
    ("Canopy", Some(0xe9ff_75a6)),
    ("Nu5", Some(0xf919_a198)),
];

/// The target block spacing in seconds: 150 before Blossom, 75 from Blossom on.
const TARGET_SPACING: [(&str, i64); 8] = [
    ("Genesis", 150),
    ("BeforeOverwinter", 150),
    ("Overwinter", 150),
    ("Sapling", 150),
    ("Blossom", 75),
    ("Heartwood", 75),
    ("Canopy", 75),
    ("Nu5", 75),
];

// ------------------------------------------------------------------ helpers

fn upgrade(name: &str) -> NetworkUpgrade {
    match name {
        "Genesis" => NetworkUpgrade::Genesis,
        "BeforeOverwinter" => NetworkUpgrade::BeforeOverwinter,
        "Overwinter" => NetworkUpgrade::Overwinter,
        "Sapling" => NetworkUpgrade::Sapling,
        "Blossom" => NetworkUpgrade::Blossom,
        "Heartwood" => NetworkUpgrade::Heartwood,
        "Canopy" => NetworkUpgrade::Canopy,
        "Nu5" => NetworkUpgrade::Nu5,
        other => panic!("the fixture names an upgrade that does not exist: {}", other),
    }
}

fn all_upgrades() -> Vec<NetworkUpgrade> {
    BRANCH_IDS.iter().map(|(name, _)| upgrade(name)).collect()
}

fn activations(network: Network) -> &'static [(&'static str, u32); 7] {
    match network {
        Network::Mainnet => &MAINNET_ACTIVATIONS,
        Network::Testnet => &TESTNET_ACTIVATIONS,
    }
}

// -------------------------------------------------------------------- tests

#[test]
fn nu5_is_a_network_upgrade_in_its_own_right() {
    let nu5 = NetworkUpgrade::Nu5;
    assert_ne!(nu5, NetworkUpgrade::Canopy);
    assert_eq!(
        all_upgrades().len(),
        8,
        "the upgrade ladder runs Genesis, BeforeOverwinter, Overwinter, Sapling, Blossom, \
         Heartwood, Canopy, Nu5"
    );
    // The variant's *spelling* is pinned by compilation -- `NetworkUpgrade::Nu5`
    // is named here and in the neighbouring tests -- so nothing asserts what its
    // `Debug` renders as. The contract does not score `Debug` wording.
}

#[test]
fn every_upgrade_reports_the_published_consensus_branch_id() {
    for (name, expected) in BRANCH_IDS.iter() {
        let actual = upgrade(name).branch_id().map(u32::from);
        assert_eq!(
            actual, *expected,
            "{} must report branch id {:?}, got {:?}",
            name, expected, actual
        );
    }
}

#[test]
fn the_nu5_branch_id_is_distinct_from_every_earlier_one() {
    let nu5 = NetworkUpgrade::Nu5
        .branch_id()
        .map(u32::from)
        .expect("NU5 has a consensus branch id");
    assert_eq!(nu5, 0xf919_a198);

    for (name, expected) in BRANCH_IDS.iter() {
        if *name == "Nu5" {
            continue;
        }
        assert_ne!(
            *expected,
            Some(nu5),
            "{} must not share NU5's consensus branch id",
            name
        );
    }
}

#[test]
fn nu5_has_no_activation_height_on_either_network() {
    for network in [Network::Mainnet, Network::Testnet].iter() {
        assert_eq!(
            NetworkUpgrade::Nu5.activation_height(*network),
            None,
            "NU5 has no announced activation height on {:?}; a node that invents one forks off \
             the network",
            network
        );
    }
}

#[test]
fn the_activation_heights_are_the_published_ones() {
    for network in [Network::Mainnet, Network::Testnet].iter() {
        for (name, height) in activations(*network).iter() {
            assert_eq!(
                upgrade(name).activation_height(*network),
                Some(Height(*height)),
                "{} activates at {:?} height {}",
                name,
                network,
                height
            );
        }
    }
}

#[test]
fn no_height_is_ever_in_nu5_yet() {
    // `current()` walks the activation table, so an upgrade with no entry can
    // never be current. Checking Height::MAX as well as the real boundaries
    // rules out an implementation that defaults to the newest variant.
    for network in [Network::Mainnet, Network::Testnet].iter() {
        let mut heights = vec![Height(0), Height(1), Height::MAX, Height(Height::MAX_AS_U32 - 1)];
        for (_, height) in activations(*network).iter() {
            heights.push(Height(*height));
            heights.push(Height(height + 1));
        }
        for height in heights {
            assert_ne!(
                NetworkUpgrade::current(*network, height),
                NetworkUpgrade::Nu5,
                "{:?} height {:?} was reported as being in NU5",
                network,
                height
            );
            assert_ne!(
                NetworkUpgrade::next(*network, height),
                Some(NetworkUpgrade::Nu5),
                "{:?} height {:?} reported NU5 as the next upgrade",
                network,
                height
            );
        }
    }
}

#[test]
fn current_reports_the_upgrade_that_has_most_recently_activated() {
    for network in [Network::Mainnet, Network::Testnet].iter() {
        let table = activations(*network);
        for (index, (name, height)) in table.iter().enumerate() {
            assert_eq!(
                NetworkUpgrade::current(*network, Height(*height)),
                upgrade(name),
                "{:?} height {} is the {} activation block",
                network,
                height,
                name
            );
            if index + 1 < table.len() {
                let (_, next_height) = table[index + 1];
                assert_eq!(
                    NetworkUpgrade::current(*network, Height(next_height - 1)),
                    upgrade(name),
                    "{:?} height {} is the last block of {}",
                    network,
                    next_height - 1,
                    name
                );
            }
        }
    }
}

#[test]
fn canopy_is_the_last_scheduled_upgrade_so_next_runs_out() {
    for network in [Network::Mainnet, Network::Testnet].iter() {
        let table = activations(*network);
        let canopy = table[table.len() - 1].1;
        assert_eq!(
            NetworkUpgrade::next(*network, Height(canopy)),
            None,
            "{:?}: Canopy is the last upgrade with a height, so nothing follows it",
            network
        );
        assert_eq!(
            NetworkUpgrade::next(*network, Height(canopy - 1)),
            Some(NetworkUpgrade::Canopy)
        );
        assert_eq!(
            NetworkUpgrade::next(*network, Height(0)),
            Some(NetworkUpgrade::BeforeOverwinter)
        );
    }
}

#[test]
fn nu5_inherits_the_post_blossom_target_block_spacing() {
    for (name, seconds) in TARGET_SPACING.iter() {
        assert_eq!(
            upgrade(name).target_spacing(),
            Duration::seconds(*seconds),
            "{} targets one block every {} seconds",
            name,
            seconds
        );
    }
}

#[test]
fn target_spacing_for_height_changes_at_blossom_activation() {
    for network in [Network::Mainnet, Network::Testnet].iter() {
        let blossom = activations(*network)[4].1;
        assert_eq!(
            NetworkUpgrade::target_spacing_for_height(*network, Height(blossom - 1)),
            Duration::seconds(150),
            "{:?}: the block before Blossom still targets 150 seconds",
            network
        );
        assert_eq!(
            NetworkUpgrade::target_spacing_for_height(*network, Height(blossom)),
            Duration::seconds(75),
            "{:?}: Blossom halves the target block spacing",
            network
        );
    }
}

#[test]
fn the_testnet_maximum_block_time_rule_starts_at_height_653_606() {
    assert_eq!(TESTNET_MAX_TIME_START_HEIGHT, Height(653_606));

    let start = TESTNET_MAX_TIME_START_HEIGHT;
    assert!(
        !NetworkUpgrade::is_max_block_time_enforced(Network::Testnet, Height(start.0 - 1)),
        "the rule is not active on Testnet one block before its start height"
    );
    assert!(
        NetworkUpgrade::is_max_block_time_enforced(Network::Testnet, start),
        "the rule is active on Testnet from its start height"
    );
    assert!(
        NetworkUpgrade::is_max_block_time_enforced(Network::Testnet, Height::MAX),
        "the rule stays active on Testnet once it has started"
    );
    assert!(
        !NetworkUpgrade::is_max_block_time_enforced(Network::Testnet, Height(0)),
        "the rule is not active at the Testnet genesis block"
    );
}

#[test]
fn the_maximum_block_time_rule_is_always_enforced_on_mainnet() {
    for height in [
        Height(0),
        Height(1),
        Height(653_605),
        Height(653_606),
        Height::MAX,
    ]
    .iter()
    {
        assert!(
            NetworkUpgrade::is_max_block_time_enforced(Network::Mainnet, *height),
            "the maximum block time rule has always applied on Mainnet, including at {:?}",
            height
        );
    }
}

#[test]
fn the_transaction_version_group_ids_are_exported_parameters() {
    assert_eq!(OVERWINTER_VERSION_GROUP_ID, 0x03C4_8270);
    assert_eq!(SAPLING_VERSION_GROUP_ID, 0x892F_2085);
    assert_eq!(TX_V5_VERSION_GROUP_ID, 0x26A7_270A);
    assert_ne!(TX_V5_VERSION_GROUP_ID, SAPLING_VERSION_GROUP_ID);
    assert_ne!(TX_V5_VERSION_GROUP_ID, OVERWINTER_VERSION_GROUP_ID);
}

#[test]
fn the_consensus_branch_id_at_a_height_follows_the_upgrade_at_that_height() {
    for network in [Network::Mainnet, Network::Testnet].iter() {
        for (name, height) in activations(*network).iter() {
            assert_eq!(
                ConsensusBranchId::current(*network, Height(*height)),
                upgrade(name).branch_id(),
                "{:?} height {} should report the {} branch id",
                network,
                height,
                name
            );
        }
        // The two pre-ZIP-200 upgrades have no branch id at all.
        assert_eq!(ConsensusBranchId::current(*network, Height(0)), None);
        assert_eq!(ConsensusBranchId::current(*network, Height(1)), None);
    }
}
