//! The Sapling shielded bundle as it appears inside a parsed `Transaction::V5`:
//! `sapling::{ShieldedData, TransferData, AnchorVariant, SharedAnchor,
//! PerSpendAnchor, FieldNotPresent, Spend, SpendPrefixInTransactionV5, Output,
//! OutputPrefixInTransactionV5}`.
//!
//! Harbor verifier driver -- injected into `zebra-chain/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! `new` group. None of these types is usable this way before this step:
//! `ShieldedData` existed only in its V4 shape — the generic `AnchorVariant`
//! parameter, the `TransferData` split, the `SharedAnchor` variant and the
//! V5 spend/output "prefix" arrays are all new here, and the split between a
//! `Spend`/`Output`'s
//! "prefix" fields (written in one array) and its proof/signature (written in
//! a separate, externally-counted array -- see `tx-v5-zip225-codec` and
//! `external-count-vectors`) is new to V5. This group is deliberately
//! orthogonal to `serialization-at-least-one`, which tests `AtLeastOne<T>` on
//! a bare `u32` in isolation; here it only appears as the type of
//! `TransferData`'s `spends`/`outputs` fields, wired to real data.
//!
//! Everything here is exercised against the two real, independently-decoded
//! V5 fixtures in `$HBV_FIXTURES` (`v5tx_spends_and_outputs.hex`,
//! `v5tx_just_outputs.hex`). The wire offsets used below were derived by hand
//! from the ZIP-225 layout (header 20 bytes; 1-byte transparent input count;
//! 1-byte transparent output count; 1-byte sapling spend count; a 96-byte
//! spend prefix per spend; a 756-byte output prefix per output), then
//! cross-checked against the fixture's total length and its
//! `$HBV_FIXTURES/v5tx.tsv` row before being hardcoded here -- not derived by
//! calling the crate's own parser.

use std::io::Cursor;
use std::path::PathBuf;

use zebra_chain::primitives::redjubjub::{Binding, VerificationKeyBytes};
use zebra_chain::sapling::{FieldNotPresent, PerSpendAnchor, SharedAnchor, TransferData};
use zebra_chain::serialization::{ZcashDeserialize, ZcashSerialize};
use zebra_chain::transaction::Transaction;

// ---------------------------------------------------------------- fixtures

fn fixture_dir() -> PathBuf {
    PathBuf::from(
        std::env::var("HBV_FIXTURES")
            .expect("HBV_FIXTURES must point at the shipped fixture directory"),
    )
}

fn hex_fixture(name: &str) -> Vec<u8> {
    let path = fixture_dir().join(format!("{}.hex", name));
    let text = std::fs::read_to_string(&path)
        .unwrap_or_else(|e| panic!("cannot read {}: {}", path.display(), e));
    hex::decode(text.trim()).unwrap_or_else(|e| panic!("{}: not valid hex: {}", path.display(), e))
}

fn parse(bytes: &[u8]) -> Transaction {
    Transaction::zcash_deserialize(Cursor::new(bytes)).expect("fixture should be well-formed")
}

/// The `sapling_shielded_data` of a parsed `Transaction::V5`.
fn shielded(tx: &Transaction) -> zebra_chain::sapling::ShieldedData<SharedAnchor> {
    match tx {
        Transaction::V5 {
            sapling_shielded_data: Some(shielded),
            ..
        } => shielded.clone(),
        other => panic!("expected a V5 transaction with a Sapling bundle, got {:?}", other),
    }
}

// ------------------------------------------------------------------- tests

#[test]
fn spends_and_outputs_bundle_matches_spends_and_maybe_outputs_shape() {
    let wire = hex_fixture("v5tx_spends_and_outputs");
    let shielded = shielded(&parse(&wire));
    match &shielded.transfers {
        TransferData::SpendsAndMaybeOutputs {
            shared_anchor,
            spends,
            maybe_outputs,
        } => {
            assert_eq!(spends.as_vec().len(), 1, "v5tx.tsv says 1 sapling spend");
            assert_eq!(maybe_outputs.len(), 2, "v5tx.tsv says 2 sapling outputs");
            let anchor_bytes: [u8; 32] = (*shared_anchor).into();
            assert_eq!(
                hex::encode(anchor_bytes),
                "914e8b02fd17010db178285840a89c4de9b78fc2c0d6b926028b45626e1adf49",
                "v5tx.tsv's shared_anchor_hex column"
            );
        }
        TransferData::JustOutputs { .. } => {
            panic!("a bundle with a spend must be TransferData::SpendsAndMaybeOutputs")
        }
    }
}

#[test]
fn just_outputs_bundle_matches_just_outputs_shape() {
    let wire = hex_fixture("v5tx_just_outputs");
    let shielded = shielded(&parse(&wire));
    match &shielded.transfers {
        TransferData::JustOutputs { outputs } => {
            assert_eq!(outputs.as_vec().len(), 1, "v5tx.tsv says 1 sapling output");
        }
        TransferData::SpendsAndMaybeOutputs { .. } => {
            panic!("a bundle with 0 spends must be TransferData::JustOutputs")
        }
    }
}

#[test]
fn spend_into_v5_parts_and_from_v5_parts_is_a_lossless_round_trip() {
    let wire = hex_fixture("v5tx_spends_and_outputs");
    let shielded = shielded(&parse(&wire));
    let spend = shielded.spends().next().expect("this bundle has one spend").clone();

    let (prefix, zkproof, spend_auth_sig) = spend.clone().into_v5_parts();
    let rebuilt = zebra_chain::sapling::Spend::<SharedAnchor>::from_v5_parts(prefix, zkproof, spend_auth_sig);
    assert_eq!(
        rebuilt, spend,
        "splitting a spend into its V5 wire parts and rebuilding it should reproduce the original"
    );
}

#[test]
fn both_outputs_into_v5_parts_and_from_v5_parts_are_lossless_round_trips() {
    let wire = hex_fixture("v5tx_spends_and_outputs");
    let shielded = shielded(&parse(&wire));
    let outputs: Vec<_> = shielded.outputs().cloned().collect();
    assert_eq!(outputs.len(), 2);

    for (i, output) in outputs.into_iter().enumerate() {
        let (prefix, zkproof) = output.clone().into_v5_parts();
        let rebuilt = zebra_chain::sapling::Output::from_v5_parts(prefix, zkproof);
        assert_eq!(
            rebuilt, output,
            "output {} should round trip losslessly through into_v5_parts/from_v5_parts",
            i
        );
    }
}

#[test]
fn spend_prefix_reserializes_to_the_independently_computed_wire_bytes() {
    let wire = hex_fixture("v5tx_spends_and_outputs");
    let shielded = shielded(&parse(&wire));
    let spend = shielded.spends().next().expect("one spend").clone();
    let (prefix, _zkproof, _sig) = spend.into_v5_parts();

    let mut out = Vec::new();
    prefix.zcash_serialize(&mut out).expect("infallible");
    assert_eq!(
        out, &wire[23..119],
        "the spend prefix (cv || nullifier || rk) should be exactly bytes 23..119 of the wire"
    );
}

#[test]
fn both_output_prefixes_reserialize_to_the_independently_computed_wire_bytes() {
    let wire = hex_fixture("v5tx_spends_and_outputs");
    let shielded = shielded(&parse(&wire));
    let outputs: Vec<_> = shielded.outputs().cloned().collect();
    assert_eq!(outputs.len(), 2);

    let expected_ranges = [(120usize, 876usize), (876usize, 1632usize)];
    for (output, (start, end)) in outputs.into_iter().zip(expected_ranges) {
        let (prefix, _zkproof) = output.into_v5_parts();
        let mut out = Vec::new();
        prefix.zcash_serialize(&mut out).expect("infallible");
        assert_eq!(
            out.len(),
            756,
            "an output prefix (cv || cmu || ephemeral_key || enc_ciphertext || out_ciphertext) is 756 bytes"
        );
        assert_eq!(
            out, &wire[start..end],
            "output prefix should match wire bytes {}..{}",
            start, end
        );
    }
}

#[test]
fn nullifier_bytes_match_the_independently_computed_wire_offset() {
    let wire = hex_fixture("v5tx_spends_and_outputs");
    let shielded = shielded(&parse(&wire));
    let spend = shielded.spends().next().expect("one spend");
    let nullifier_bytes: [u8; 32] = spend.nullifier.into();
    assert_eq!(
        nullifier_bytes,
        wire[55..87],
        "the spend's nullifier should be exactly bytes 55..87 of the wire \
         (cv is bytes 23..55, nullifier follows it, rk follows that)"
    );
}

#[test]
fn converting_a_shared_anchor_spend_to_per_spend_anchor_preserves_every_field_but_the_anchor() {
    let wire = hex_fixture("v5tx_spends_and_outputs");
    let shielded = shielded(&parse(&wire));
    let spend = shielded.spends().next().expect("one spend").clone();
    let root = shielded.shared_anchor().expect("this bundle has a shared anchor");

    let converted: zebra_chain::sapling::Spend<PerSpendAnchor> = (spend.clone(), root).into();
    assert_eq!(converted.cv, spend.cv, "cv should be unchanged by the anchor conversion");
    assert_eq!(converted.nullifier, spend.nullifier, "nullifier should be unchanged");
    assert_eq!(converted.rk, spend.rk, "rk should be unchanged");
    assert_eq!(converted.zkproof, spend.zkproof, "zkproof should be unchanged");
    assert_eq!(converted.spend_auth_sig, spend.spend_auth_sig, "spend_auth_sig should be unchanged");
    assert_eq!(
        converted.per_spend_anchor, root,
        "the only field that should change is per_spend_anchor, which becomes the shared root"
    );
}

#[test]
fn binding_verification_key_round_trips_through_its_byte_form() {
    let wire = hex_fixture("v5tx_spends_and_outputs");
    let shielded = shielded(&parse(&wire));
    let vk: VerificationKeyBytes<Binding> = shielded.binding_verification_key();
    let bytes: [u8; 32] = vk.into();
    let restored = VerificationKeyBytes::<Binding>::from(bytes);
    assert_eq!(
        vk, restored,
        "VerificationKeyBytes -> [u8; 32] -> VerificationKeyBytes should be lossless"
    );
}

#[test]
fn field_not_present_and_the_anchor_marker_types_are_zero_sized() {
    // These types exist purely as typestate markers selecting which of
    // AnchorVariant::Shared/PerSpend is present; none of them should carry
    // any actual data.
    assert_eq!(std::mem::size_of::<FieldNotPresent>(), 0);
    assert_eq!(std::mem::size_of::<PerSpendAnchor>(), 0);
    assert_eq!(std::mem::size_of::<SharedAnchor>(), 0);
    // And FieldNotPresent really is a usable, constructible unit-like value,
    // not just an uninhabited type.
    let _ = FieldNotPresent;
}

#[test]
fn just_outputs_bundle_has_no_shared_anchor_and_its_output_round_trips() {
    let wire = hex_fixture("v5tx_just_outputs");
    let shielded = shielded(&parse(&wire));
    assert!(
        shielded.shared_anchor().is_none(),
        "a bundle with 0 spends must not report a shared anchor"
    );
    let output = shielded.outputs().next().expect("one output").clone();
    let (prefix, zkproof) = output.clone().into_v5_parts();
    let rebuilt = zebra_chain::sapling::Output::from_v5_parts(prefix, zkproof);
    assert_eq!(rebuilt, output, "the single output should round trip losslessly");
}
