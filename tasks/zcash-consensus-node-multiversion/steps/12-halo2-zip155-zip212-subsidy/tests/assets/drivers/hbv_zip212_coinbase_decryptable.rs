//! ZIP-212 coinbase output decryptability — new at app-12.
//!
//! Harbor verifier driver -- injected into `zebra-consensus/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! Capability 3: `coinbase_outputs_are_decryptable` in
//! `zebra_consensus::transaction::check` is new at app-12. It accepts a
//! coinbase transaction, a network, and a block height, and returns
//! `Ok(())` when all Sapling and Orchard outputs decrypt under the all-zeros
//! ovk (as ZIP-212 requires for Heartwood-or-later coinbases), or
//! `Err(TransactionError::CoinbaseOutputsNotDecryptable)` otherwise.
//!
//! This driver tests the accepting direction against two real mainnet/testnet
//! corpus blocks that straddle the Canopy note-plaintext boundary, tests the
//! rejecting direction against a synthetic V5 transaction whose Orchard note
//! ciphertext is all zeros, and confirms the error variant's existence and
//! Display message. Only the four tests in this driver are required to earn
//! this group's score; the Orchard flags rule lives in `orchard-flags-rule`.

use std::io::Cursor;
use std::path::PathBuf;

use zebra_chain::block::{Block, Height};
use zebra_chain::orchard::shielded_data::Flags;
use zebra_chain::parameters::{Network, NetworkUpgrade};
use zebra_chain::serialization::ZcashDeserialize;
use zebra_chain::transaction::arbitrary::insert_fake_orchard_shielded_data;
use zebra_chain::transaction::{LockTime, Transaction};
use zebra_consensus::error::TransactionError;
use zebra_consensus::transaction::check;

// ------------------------------------------------------------------ helpers

/// Parse one block out of the shipped corpus.
fn corpus_block(name: &str) -> Block {
    let fixtures = PathBuf::from(
        std::env::var("HBV_FIXTURES")
            .expect("HBV_FIXTURES must point at the shipped fixture directory"),
    );
    let hex = std::fs::read_to_string(fixtures.join("blocks").join(name))
        .unwrap_or_else(|e| panic!("fixture {} is readable: {}", name, e));
    let raw = hex::decode(hex.trim()).expect("fixture hex decodes");
    Block::zcash_deserialize(Cursor::new(&raw[..])).expect("fixture parses")
}

/// A V5 transaction with no shielded data of any kind.
fn empty_v5() -> Transaction {
    Transaction::V5 {
        network_upgrade: NetworkUpgrade::Nu5,
        lock_time: LockTime::Height(Height(0)),
        expiry_height: Height(0),
        inputs: Vec::new(),
        outputs: Vec::new(),
        sapling_shielded_data: None,
        orchard_shielded_data: None,
    }
}

/// A V5 transaction carrying exactly one dummy Orchard action, with `flags`
/// set to the caller's value.
fn v5_with_one_orchard_action(flags: Flags) -> Transaction {
    let mut tx = empty_v5();
    let shielded_data = insert_fake_orchard_shielded_data(&mut tx);
    shielded_data.flags = flags;
    tx
}

// ---- error-variant shape test ----

/// `TransactionError::CoinbaseOutputsNotDecryptable` exists, compares equal,
/// and its Display contains the word "coinbase" (per the error annotation).
#[test]
fn coinbase_outputs_not_decryptable_variant() {
    let err = TransactionError::CoinbaseOutputsNotDecryptable;
    assert_eq!(err, TransactionError::CoinbaseOutputsNotDecryptable);
    let msg = err.to_string();
    assert!(
        msg.to_lowercase().contains("coinbase"),
        "CoinbaseOutputsNotDecryptable message must mention 'coinbase', got: {:?}", msg
    );
}

// ---- ZIP-212 coinbase output decryptability, both directions ----

/// A Heartwood-era, pre-Canopy mainnet coinbase whose Sapling output is
/// decryptable with the all-zero ovk must be accepted.
///
/// `main-0982681.hex` is a mainnet block above Heartwood (`903_000`) and below
/// Canopy (`1_046_400`), so its coinbase note plaintext has lead byte `0x01`.
/// The companion test at testnet 1_101_629 covers the `0x02` epoch. Its
/// coinbase carries one Sapling output, asserted before the call, so a corpus
/// swap that removed the shielded output would fail loudly rather than turn
/// this into a transparent-only no-op. The genesis coinbase is checked too: it
/// is below Heartwood, where the rule does not apply at all.
#[test]
fn coinbase_outputs_are_decryptable_accepts_conforming_coinbases() {
    let heartwood = NetworkUpgrade::Heartwood
        .activation_height(Network::Mainnet)
        .expect("Heartwood has a mainnet activation height");
    let canopy = NetworkUpgrade::Canopy
        .activation_height(Network::Mainnet)
        .expect("Canopy has a mainnet activation height");
    assert!(
        Height(982_681) >= heartwood && Height(982_681) < canopy,
        "main-0982681 must sit in the Heartwood-to-Canopy window ({:?}..{:?}) for \
         this test to exercise the 0x01 lead-byte epoch",
        heartwood,
        canopy
    );

    let block = corpus_block("main-0982681.hex");
    let coinbase = block.transactions[0].as_ref();
    assert_eq!(
        coinbase.sapling_outputs().count(),
        1,
        "main-0982681's coinbase must carry a Sapling output for this test to \
         exercise the decryption path"
    );
    assert_eq!(
        check::coinbase_outputs_are_decryptable(coinbase, Network::Mainnet, Height(982_681)),
        Ok(()),
        "a real post-Heartwood mainnet coinbase output must decrypt with the \
         all-zero ovk"
    );

    let genesis = corpus_block("main-0000000.hex");
    assert_eq!(
        check::coinbase_outputs_are_decryptable(
            genesis.transactions[0].as_ref(),
            Network::Mainnet,
            Height(0),
        ),
        Ok(()),
        "below Heartwood the ZIP-212 coinbase rule does not apply"
    );
}

/// A post-Canopy testnet coinbase whose Sapling output carries a `0x02` note
/// plaintext lead byte must be accepted.
///
/// This is the Canopy half of capability 3. `test-1101629.hex` is testnet
/// height 1_101_629, past the testnet Canopy activation at 1_028_500, so the
/// call routes through the `0x02` plaintext epoch. An implementation that
/// hands the note-decryption bridge a fixed height passes one acceptance test
/// and fails the other.
#[test]
fn coinbase_outputs_are_decryptable_accepts_a_post_canopy_testnet_coinbase() {
    let height = Height(1_101_629);
    let canopy = NetworkUpgrade::Canopy
        .activation_height(Network::Testnet)
        .expect("Canopy has a testnet activation height at this tag");
    assert!(
        height >= canopy,
        "the fixture must sit at or after testnet Canopy ({:?}) for this test to \
         exercise the 0x02 lead-byte epoch, but it is at {:?}",
        canopy,
        height
    );

    let block = corpus_block("test-1101629.hex");
    let coinbase = block.transactions[0].as_ref();
    assert_eq!(
        coinbase.sapling_outputs().count(),
        1,
        "test-1101629's coinbase must carry a Sapling output for this test to \
         exercise the decryption path"
    );

    assert_eq!(
        check::coinbase_outputs_are_decryptable(coinbase, Network::Testnet, height),
        Ok(()),
        "a real post-Canopy testnet coinbase output must decrypt to a note \
         plaintext with lead byte 0x02 under the all-zero ovk"
    );
}

/// An Orchard action whose note ciphertext is all zeros does not decrypt to a
/// valid note plaintext under the all-zero ovk, so a coinbase carrying one at
/// a Heartwood-or-later height must be rejected.
///
/// Testnet is used because NU5 has no mainnet activation height at this tag.
/// A `coinbase_outputs_are_decryptable` that returns `Ok(())` unconditionally
/// fails only here, which is what makes the acceptance tests above non-vacuous.
#[test]
fn coinbase_outputs_are_decryptable_rejects_undecryptable_orchard_output() {
    let nu5_height = NetworkUpgrade::Nu5
        .activation_height(Network::Testnet)
        .expect("NU5 has a testnet activation height at this tag");

    let tx = v5_with_one_orchard_action(Flags::ENABLE_SPENDS | Flags::ENABLE_OUTPUTS);
    assert_eq!(
        tx.orchard_actions().count(),
        1,
        "the fixture must carry an Orchard action whose ciphertext is all zeros"
    );

    assert_eq!(
        check::coinbase_outputs_are_decryptable(&tx, Network::Testnet, nu5_height),
        Err(TransactionError::CoinbaseOutputsNotDecryptable),
        "an all-zero Orchard note ciphertext must not decrypt with the all-zero ovk"
    );
}
