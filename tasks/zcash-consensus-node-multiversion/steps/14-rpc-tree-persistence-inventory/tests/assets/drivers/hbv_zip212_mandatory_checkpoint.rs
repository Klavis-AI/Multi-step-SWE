//! ZIP-212 grace-period checkpoint height.
//!
//! Harbor verifier driver -- injected into `zebra-chain/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! At v1.0.0-beta.6 (app-14) `Network::mandatory_checkpoint_height()` returns
//! Canopy activation height + 32256 (the ZIP-212 grace period duration).
//! At the predecessor (app-13) it returned the bare Canopy activation height.
//!
//! Negative direction: the assertions below check exact Height values that
//! exceed the bare Canopy activation heights, so they fail on app-13 where
//! mandatory_checkpoint_height returns the Canopy activation directly.

use zebra_chain::parameters::Network;
use zebra_chain::block::Height;

/// ZIP-212 grace period duration, in blocks.
///
/// From zebra-chain/src/parameters/network.rs:
///   const ZIP_212_GRACE_PERIOD_DURATION: i32 = 32_256;
const ZIP_212_GRACE_PERIOD_DURATION: u32 = 32_256;

/// Mainnet Canopy activation height.
const MAINNET_CANOPY: u32 = 1_046_400;

/// Testnet Canopy activation height.
const TESTNET_CANOPY: u32 = 1_028_500;

#[test]
fn mainnet_mandatory_checkpoint_is_canopy_plus_grace_period() {
    let height = Network::Mainnet.mandatory_checkpoint_height();
    let expected = Height(MAINNET_CANOPY + ZIP_212_GRACE_PERIOD_DURATION);
    assert_eq!(
        height, expected,
        "Mainnet mandatory checkpoint must be Canopy ({}) + ZIP-212 grace period ({}) = {}",
        MAINNET_CANOPY, ZIP_212_GRACE_PERIOD_DURATION,
        MAINNET_CANOPY + ZIP_212_GRACE_PERIOD_DURATION,
    );
}

#[test]
fn testnet_mandatory_checkpoint_is_canopy_plus_grace_period() {
    let height = Network::Testnet.mandatory_checkpoint_height();
    let expected = Height(TESTNET_CANOPY + ZIP_212_GRACE_PERIOD_DURATION);
    assert_eq!(
        height, expected,
        "Testnet mandatory checkpoint must be Canopy ({}) + ZIP-212 grace period ({}) = {}",
        TESTNET_CANOPY, ZIP_212_GRACE_PERIOD_DURATION,
        TESTNET_CANOPY + ZIP_212_GRACE_PERIOD_DURATION,
    );
}

#[test]
fn mandatory_checkpoint_exceeds_canopy_activation() {
    use zebra_chain::parameters::NetworkUpgrade;

    let canopy_mainnet = NetworkUpgrade::Canopy
        .activation_height(Network::Mainnet)
        .expect("Canopy activation height must be present for Mainnet");
    let checkpoint_mainnet = Network::Mainnet.mandatory_checkpoint_height();
    assert!(
        checkpoint_mainnet > canopy_mainnet,
        "mandatory_checkpoint_height ({}) must exceed Canopy activation ({}) by the ZIP-212 grace period",
        checkpoint_mainnet.0, canopy_mainnet.0,
    );

    let canopy_testnet = NetworkUpgrade::Canopy
        .activation_height(Network::Testnet)
        .expect("Canopy activation height must be present for Testnet");
    let checkpoint_testnet = Network::Testnet.mandatory_checkpoint_height();
    assert!(
        checkpoint_testnet > canopy_testnet,
        "mandatory_checkpoint_height ({}) must exceed Canopy activation ({}) by the ZIP-212 grace period",
        checkpoint_testnet.0, canopy_testnet.0,
    );
}

#[test]
fn mandatory_checkpoint_gap_equals_zip212_grace_period() {
    use zebra_chain::parameters::NetworkUpgrade;

    for network in [Network::Mainnet, Network::Testnet] {
        let canopy = NetworkUpgrade::Canopy
            .activation_height(network)
            .expect("Canopy activation height must be present");
        let checkpoint = network.mandatory_checkpoint_height();
        let gap = checkpoint.0 - canopy.0;
        assert_eq!(
            gap, ZIP_212_GRACE_PERIOD_DURATION,
            "gap between mandatory checkpoint and Canopy activation must equal \
             ZIP_212_GRACE_PERIOD_DURATION ({}) for {:?}, got {}",
            ZIP_212_GRACE_PERIOD_DURATION, network, gap,
        );
    }
}

#[test]
fn mandatory_checkpoint_is_inside_canopy_epoch_not_at_activation() {
    use zebra_chain::parameters::NetworkUpgrade;

    // The checkpoint sits partway through the Canopy epoch: the grace period
    // pushes it past Canopy activation, but not as far as the next upgrade.
    // At the predecessor tag the checkpoint *is* the Canopy activation height,
    // so `is_activation_height` is true there and this test fails.
    for network in [Network::Mainnet, Network::Testnet] {
        let checkpoint = network.mandatory_checkpoint_height();

        assert_eq!(
            NetworkUpgrade::current(network, checkpoint),
            NetworkUpgrade::Canopy,
            "the mandatory checkpoint for {:?} must fall inside the Canopy epoch, \
             but height {} belongs to {:?}",
            network, checkpoint.0,
            NetworkUpgrade::current(network, checkpoint),
        );

        assert!(
            !NetworkUpgrade::is_activation_height(network, checkpoint),
            "the mandatory checkpoint for {:?} must be offset past Canopy activation \
             by the ZIP-212 grace period, but height {} is itself an activation height",
            network, checkpoint.0,
        );
    }
}

#[test]
fn mandatory_checkpoint_exact_values() {
    // Belt-and-suspenders: hardcoded expected values.
    // Mainnet: 1_046_400 + 32_256 = 1_078_656
    // Testnet: 1_028_500 + 32_256 = 1_060_756
    assert_eq!(
        Network::Mainnet.mandatory_checkpoint_height().0,
        1_078_656u32,
        "Mainnet mandatory_checkpoint_height must be 1_078_656"
    );
    assert_eq!(
        Network::Testnet.mandatory_checkpoint_height().0,
        1_060_756u32,
        "Testnet mandatory_checkpoint_height must be 1_060_756"
    );
}
