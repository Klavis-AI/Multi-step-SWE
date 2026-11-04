//! Network::bip70_network_name() — BIP-70 network naming.
//!
//! Harbor verifier driver -- injected into `zebra-chain/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! `bip70_network_name()` is new at v1.0.0-beta.6: absent from app-13
//! (`grep -c bip70_network_name app-13/.../network.rs` returns 0), so any
//! driver that calls it fails to compile against the predecessor tree.

use zebra_chain::parameters::Network;

#[test]
fn mainnet_bip70_name_is_main() {
    assert_eq!(
        Network::Mainnet.bip70_network_name(),
        "main",
        "Network::Mainnet bip70_network_name must be \"main\" (matches zcashd)"
    );
}

#[test]
fn testnet_bip70_name_is_test() {
    assert_eq!(
        Network::Testnet.bip70_network_name(),
        "test",
        "Network::Testnet bip70_network_name must be \"test\" (matches zcashd)"
    );
}

#[test]
fn bip70_names_are_distinct() {
    assert_ne!(
        Network::Mainnet.bip70_network_name(),
        Network::Testnet.bip70_network_name(),
        "Mainnet and Testnet bip70 names must be different"
    );
}

#[test]
fn bip70_names_are_non_empty() {
    assert!(!Network::Mainnet.bip70_network_name().is_empty());
    assert!(!Network::Testnet.bip70_network_name().is_empty());
}
