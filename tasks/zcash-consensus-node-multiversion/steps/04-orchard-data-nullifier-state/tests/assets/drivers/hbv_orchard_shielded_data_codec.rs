//! Orchard ShieldedData and Action codec against hand-built V5 transaction fixtures.
//!
//! Harbor verifier driver — injected into `zebra-chain/tests/` at grading time.
//!
//! V5 transactions carry optional Orchard bundles (ShieldedData); each bundle
//! holds 1+ Actions, flags, value_balance, a shared tree anchor, a Halo2 proof
//! aggregating all actions, spend-auth signatures (one per action), and a
//! binding signature. The wire layout follows ZIP-225 §7.1. This group exercises
//! the round trip through four hand-built V5 transactions spanning 1-action and
//! 2-action bundles, flags variations (spends-only / outputs-only / both), and
//! positive/negative/zero value_balance.

use std::collections::BTreeMap;
use std::convert::TryInto;
use std::io::Cursor;
use std::path::PathBuf;

use zebra_chain::serialization::{ZcashDeserialize, ZcashSerialize};
use zebra_chain::transaction::Transaction;

struct Vector {
    name: String,
    n_actions: usize,
    nullifier_0: Option<[u8; 32]>,
    nullifier_1: Option<[u8; 32]>,
    flags: u8,
    value_balance: i64,
    size: usize,
    raw: Vec<u8>,
}

fn fixture_dir() -> PathBuf {
    PathBuf::from(
        std::env::var("HBV_FIXTURES")
            .expect("HBV_FIXTURES must point at the shipped fixture directory"),
    )
}

fn cell<'a>(row: &BTreeMap<&'a str, &'a str>, key: &str) -> &'a str {
    *row.get(key)
        .unwrap_or_else(|| panic!("v5_orchard.tsv has no column {}", key))
}

fn parse_nullifier(s: &str) -> Option<[u8; 32]> {
    if s.is_empty() {
        return None;
    }
    let bytes = hex::decode(s).expect("nullifier hex");
    assert_eq!(bytes.len(), 32);
    let mut out = [0u8; 32];
    out.copy_from_slice(&bytes);
    Some(out)
}

fn vectors() -> Vec<Vector> {
    let path = fixture_dir().join("v5_orchard.tsv");
    let tsv = std::fs::read_to_string(&path)
        .unwrap_or_else(|e| panic!("cannot read {}: {}", path.display(), e));
    let mut lines = tsv.lines();
    let header: Vec<&str> = lines
        .next()
        .expect("v5_orchard.tsv has a header")
        .trim_start_matches("# ")
        .split('\t')
        .collect();

    let mut out = Vec::new();
    for line in lines {
        if line.is_empty() {
            continue;
        }
        let cells: Vec<&str> = line.split('\t').collect();
        assert_eq!(cells.len(), header.len());
        let row: BTreeMap<&str, &str> = header.iter().cloned().zip(cells).collect();

        let name = cell(&row, "name").to_string();
        let hex_path = fixture_dir().join(format!("{}.hex", name));
        let hex_text = std::fs::read_to_string(&hex_path)
            .unwrap_or_else(|e| panic!("cannot read {}: {}", hex_path.display(), e));
        let raw = hex::decode(hex_text.trim()).expect("fixture hex");

        out.push(Vector {
            n_actions: cell(&row, "n_actions").parse().expect("n_actions"),
            nullifier_0: parse_nullifier(cell(&row, "nullifier_0_hex")),
            nullifier_1: parse_nullifier(cell(&row, "nullifier_1_hex")),
            flags: cell(&row, "flags").parse().expect("flags"),
            value_balance: cell(&row, "value_balance").parse().expect("value_balance"),
            size: cell(&row, "size").parse().expect("size"),
            raw,
            name,
        });
    }
    assert_eq!(out.len(), 4, "expected 4 V5 vectors");
    out
}

fn parse(vec: &Vector) -> Transaction {
    Transaction::zcash_deserialize(Cursor::new(&vec.raw[..]))
        .unwrap_or_else(|e| panic!("{}: failed to parse: {:?}", vec.name, e))
}

fn serialize(tx: &Transaction) -> Vec<u8> {
    let mut out = Vec::new();
    tx.zcash_serialize(&mut out).expect("serialize");
    out
}

#[test]
fn every_v5_transaction_round_trips_through_zcash_deserialize() {
    for vec in vectors() {
        let tx = parse(&vec);
        let reser = serialize(&tx);
        assert_eq!(
            reser, vec.raw,
            "{}: re-serialized bytes differ from fixture",
            vec.name
        );
        assert_eq!(
            reser.len(),
            vec.size,
            "{}: re-serialized size {} != TSV size {}",
            vec.name,
            reser.len(),
            vec.size
        );
    }
}

#[test]
fn v5_transactions_expose_orchard_shielded_data_when_present() {
    for vec in vectors() {
        let tx = parse(&vec);
        let shielded = tx
            .orchard_shielded_data()
            .unwrap_or_else(|| panic!("{}: orchard_shielded_data() returned None", vec.name));

        assert_eq!(
            shielded.actions.len(),
            vec.n_actions,
            "{}: actions.len() = {}, TSV says {}",
            vec.name,
            shielded.actions.len(),
            vec.n_actions
        );
    }
}

#[test]
fn orchard_actions_iterator_yields_all_actions_in_order() {
    for vec in vectors() {
        let tx = parse(&vec);
        let collected: Vec<_> = tx.orchard_actions().collect();
        assert_eq!(
            collected.len(),
            vec.n_actions,
            "{}: orchard_actions() iterator yielded {} actions, expected {}",
            vec.name,
            collected.len(),
            vec.n_actions
        );
    }
}

#[test]
fn orchard_nullifiers_iterator_yields_expected_nullifiers() {
    for vec in vectors() {
        let tx = parse(&vec);
        let nulls: Vec<_> = tx
            .orchard_nullifiers()
            .map(|n| <[u8; 32]>::from(*n))
            .collect();

        assert_eq!(
            nulls.len(),
            vec.n_actions,
            "{}: orchard_nullifiers() yielded {} nullifiers, expected {}",
            vec.name,
            nulls.len(),
            vec.n_actions
        );

        if let Some(expected_0) = vec.nullifier_0 {
            assert_eq!(
                nulls[0], expected_0,
                "{}: first nullifier mismatch: got {}, expected {}",
                vec.name,
                hex::encode(&nulls[0]),
                hex::encode(&expected_0)
            );
        }

        if vec.n_actions >= 2 {
            if let Some(expected_1) = vec.nullifier_1 {
                assert_eq!(
                    nulls[1], expected_1,
                    "{}: second nullifier mismatch: got {}, expected {}",
                    vec.name,
                    hex::encode(&nulls[1]),
                    hex::encode(&expected_1)
                );
            }
        }
    }
}

#[test]
fn orchard_shielded_data_flags_match_fixture_expectations() {
    for vec in vectors() {
        let tx = parse(&vec);
        let shielded = tx.orchard_shielded_data().expect("has orchard data");
        assert_eq!(
            shielded.flags.bits(),
            vec.flags,
            "{}: flags = {:#04x}, TSV says {:#04x}",
            vec.name,
            shielded.flags.bits(),
            vec.flags
        );
    }
}

#[test]
fn orchard_value_balance_matches_fixture_expectations() {
    let vectors = vectors();
    for vec in &vectors {
        let tx = parse(vec);
        let shielded = tx.orchard_shielded_data().expect("has orchard data");
        let vb_i64: i64 = shielded
            .value_balance
            .try_into()
            .expect("value_balance fits i64");
        assert_eq!(
            vb_i64, vec.value_balance,
            "{}: value_balance = {}, TSV says {}",
            vec.name, vb_i64, vec.value_balance
        );
    }

    let mut seen_negative = false;
    let mut seen_positive = false;
    let mut seen_zero = false;
    for vec in &vectors {
        if vec.value_balance < 0 {
            seen_negative = true;
        } else if vec.value_balance > 0 {
            seen_positive = true;
        } else {
            seen_zero = true;
        }
    }
    assert!(
        seen_negative && seen_positive && seen_zero,
        "corpus must exercise negative, positive, and zero value_balance"
    );
}

#[test]
fn authorized_action_into_parts_and_from_parts_are_inverses() {
    for vec in vectors() {
        let tx = parse(&vec);
        let shielded = tx.orchard_shielded_data().expect("has orchard data");
        for (i, auth_action) in shielded.actions.iter().enumerate() {
            let (action, sig) = auth_action.clone().into_parts();
            let reconstructed =
                zebra_chain::orchard::AuthorizedAction::from_parts(action.clone(), sig);

            assert_eq!(
                reconstructed.action.nullifier, action.nullifier,
                "{}: action {} reconstructed nullifier differs",
                vec.name, i
            );
            assert_eq!(
                reconstructed.spend_auth_sig, auth_action.spend_auth_sig,
                "{}: action {} reconstructed spend_auth_sig differs",
                vec.name, i
            );
        }
    }
}

#[test]
fn actions_iterator_from_shielded_data_and_atleastone_agree() {
    for vec in vectors() {
        let tx = parse(&vec);
        let shielded = tx.orchard_shielded_data().expect("has orchard data");

        let from_shielded: Vec<_> = shielded.actions().collect();
        let from_atleastone: Vec<_> = shielded.actions.actions().collect();

        assert_eq!(
            from_shielded.len(),
            from_atleastone.len(),
            "{}: ShieldedData::actions() and AtLeastOne::actions() yielded different counts",
            vec.name
        );

        for i in 0..from_shielded.len() {
            assert_eq!(
                from_shielded[i].nullifier, from_atleastone[i].nullifier,
                "{}: action {} nullifier differs between iterators",
                vec.name, i
            );
        }
    }
}

#[test]
fn nullifiers_from_shielded_data_and_transaction_agree() {
    for vec in vectors() {
        let tx = parse(&vec);
        let shielded = tx.orchard_shielded_data().expect("has orchard data");

        let from_shielded: Vec<_> = shielded
            .nullifiers()
            .map(|n| <[u8; 32]>::from(*n))
            .collect();
        let from_tx: Vec<_> = tx
            .orchard_nullifiers()
            .map(|n| <[u8; 32]>::from(*n))
            .collect();

        assert_eq!(
            from_shielded.len(),
            from_tx.len(),
            "{}: ShieldedData::nullifiers() and Transaction::orchard_nullifiers() differ in count",
            vec.name
        );

        for i in 0..from_shielded.len() {
            assert_eq!(
                from_shielded[i], from_tx[i],
                "{}: nullifier {} differs: shielded {}, tx {}",
                vec.name,
                i,
                hex::encode(&from_shielded[i]),
                hex::encode(&from_tx[i])
            );
        }
    }
}

#[test]
fn v5_with_no_orchard_actions_deserializes_orchard_shielded_data_as_none() {
    // A minimal V5 with nActionsOrchard = 0 (one compactSize byte = 0x00).
    let mut v5_no_orchard = vec![];
    // header: version 5 | overwintered, group id, branch id Nu5
    v5_no_orchard.extend_from_slice(&(5u32 | (1u32 << 31)).to_le_bytes());
    v5_no_orchard.extend_from_slice(&0x26A7_270Au32.to_le_bytes());
    v5_no_orchard.extend_from_slice(&0xf919_a198u32.to_le_bytes());
    // lock_time, expiry_height
    v5_no_orchard.extend_from_slice(&0u32.to_le_bytes());
    v5_no_orchard.extend_from_slice(&500000u32.to_le_bytes());
    // vin, vout
    v5_no_orchard.push(0);
    v5_no_orchard.push(0);
    // sapling: nSpendsSapling, nOutputsSapling
    v5_no_orchard.push(0);
    v5_no_orchard.push(0);
    // orchard: nActionsOrchard = 0
    v5_no_orchard.push(0);

    let tx = Transaction::zcash_deserialize(Cursor::new(&v5_no_orchard[..]))
        .expect("minimal V5 with no orchard should parse");
    assert!(
        tx.orchard_shielded_data().is_none(),
        "V5 with nActionsOrchard=0 must deserialize orchard_shielded_data as None"
    );
}

#[test]
fn action_has_expected_serialized_size() {
    // ZIP-225 §7.5: Action = cv(32) + nullifier(32) + rk(32) + cm_x(32) +
    // ephemeral_key(32) + enc_ciphertext(580) + out_ciphertext(80) = 820,
    // and §3 names the exported constant that carries that number.
    assert_eq!(
        zebra_chain::orchard::shielded_data::ACTION_SIZE,
        820,
        "orchard::shielded_data::ACTION_SIZE must name the 820-byte Action size"
    );
    for vec in vectors() {
        let tx = parse(&vec);
        let shielded = tx.orchard_shielded_data().expect("has orchard data");
        for (i, auth_action) in shielded.actions.iter().enumerate() {
            let mut action_bytes = Vec::new();
            auth_action
                .action
                .zcash_serialize(&mut action_bytes)
                .expect("action serialize");
            assert_eq!(
                action_bytes.len(),
                820,
                "{}: action {} serialized to {} bytes, expected 820",
                vec.name,
                i,
                action_bytes.len()
            );
        }
    }
}
