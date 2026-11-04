//! `NetworkUpgrade::from_branch_id`, against the fixture's thirteen probes.
//!
//! Harbor verifier driver -- injected into `zebra-chain/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! `new` group. Before this step, a `NetworkUpgrade` could tell you its own
//! consensus branch id (`NetworkUpgrade::branch_id`), but nothing could go the
//! other way: given the four-byte `nConsensusBranchId` field off the wire of a
//! v3/v4/v5 transaction, there was no public way to recover which upgrade
//! signed it. `NetworkUpgrade::from_branch_id(u32) -> Option<NetworkUpgrade>`
//! is that inverse, and it is exactly what a V5 transaction parser needs: a V5
//! header carries a raw branch id, not a `NetworkUpgrade`, and ZIP-225 requires
//! the deserializer to reject a branch id nothing has ever assigned.
//!
//! `$HBV_FIXTURES/branch_ids.tsv` carries thirteen probes: the six ids ZIP-200
//! and its successors have actually assigned (`status = active`), four values
//! nothing has assigned (`status = unassigned`, including all-zero, all-ones
//! and the bit right below Overwinter's own id), and three deliberately
//! confusable values (`status = not_a_branch_id`) that are real four-byte
//! constants of this protocol -- just not branch ids: they are the v3/v4/v5
//! transaction version group ids. None of the thirteen expected results below
//! is computed by calling the function under test; they come from the ZIP-200
//! deployment table (restated in `ACTIVE_UPGRADES` the same way step 1's
//! sighash driver restates it) and from the fixture's own `status` column.

use std::collections::{BTreeMap, HashSet};
use std::path::PathBuf;

use zebra_chain::parameters::{
    NetworkUpgrade, OVERWINTER_VERSION_GROUP_ID, SAPLING_VERSION_GROUP_ID,
    TX_V5_VERSION_GROUP_ID,
};

// ---------------------------------------------------------------- fixtures

struct BranchIdRow {
    hex: u32,
    /// `Some(name)` for `status = active`, `None` otherwise.
    network_upgrade: Option<String>,
    status: String,
}

struct V5TxRow {
    branch_id: u32,
    network_upgrade: String,
}

fn fixture_dir() -> PathBuf {
    PathBuf::from(
        std::env::var("HBV_FIXTURES")
            .expect("HBV_FIXTURES must point at the shipped fixture directory"),
    )
}

fn cell<'a>(row: &BTreeMap<&'a str, &'a str>, key: &str, tsv: &str) -> &'a str {
    *row.get(key)
        .unwrap_or_else(|| panic!("{} has no column {}", tsv, key))
}

fn rows(name: &str) -> Vec<BTreeMap<String, String>> {
    let path = fixture_dir().join(name);
    let text = std::fs::read_to_string(&path)
        .unwrap_or_else(|e| panic!("cannot read {}: {}", path.display(), e));
    let mut lines = text.lines();
    let header: Vec<&str> = lines
        .next()
        .expect("fixture has a header row")
        .trim_start_matches("# ")
        .split('\t')
        .collect();

    let mut out = Vec::new();
    for line in lines {
        if line.is_empty() {
            continue;
        }
        let cells: Vec<&str> = line.split('\t').collect();
        assert_eq!(
            cells.len(),
            header.len(),
            "{}: malformed row: {}",
            name,
            line
        );
        let raw: BTreeMap<&str, &str> = header.iter().cloned().zip(cells).collect();
        out.push(
            raw.iter()
                .map(|(k, v)| (k.to_string(), v.to_string()))
                .collect(),
        );
    }
    out
}

fn parse_u32_hex(field: &str) -> u32 {
    let s = field.trim_start_matches("0x");
    u32::from_str_radix(s, 16).unwrap_or_else(|e| panic!("{:?} is not hex u32: {}", field, e))
}

fn branch_id_rows() -> Vec<BranchIdRow> {
    let mut out = Vec::new();
    for r in rows("branch_ids.tsv") {
        let get = |k: &str| r.get(k).unwrap_or_else(|| panic!("branch_ids.tsv has no column {}", k)).clone();
        let status = get("status");
        let network_upgrade = if status == "active" {
            Some(get("network_upgrade"))
        } else {
            None
        };
        out.push(BranchIdRow {
            hex: parse_u32_hex(&get("hex")),
            network_upgrade,
            status,
        });
    }
    assert_eq!(out.len(), 13, "branch_ids.tsv should ship 13 probes, found {}", out.len());
    out
}

fn v5tx_rows() -> Vec<V5TxRow> {
    let mut out = Vec::new();
    for r in rows("v5tx.tsv") {
        let get = |k: &str| r.get(k).unwrap_or_else(|| panic!("v5tx.tsv has no column {}", k)).clone();
        out.push(V5TxRow {
            branch_id: parse_u32_hex(&get("branch_id")),
            network_upgrade: get("network_upgrade"),
        });
    }
    assert_eq!(out.len(), 2, "v5tx.tsv should ship 2 transactions, found {}", out.len());
    out
}

// ----------------------------------------------------------------- helpers

/// ZIP-200's deployment table, restated here rather than read out of the
/// crate -- the same technique step 1's sighash driver uses for its own
/// `SIGNING_UPGRADES` table.
static ACTIVE_UPGRADES: &[(&str, NetworkUpgrade)] = &[
    ("Overwinter", NetworkUpgrade::Overwinter),
    ("Sapling", NetworkUpgrade::Sapling),
    ("Blossom", NetworkUpgrade::Blossom),
    ("Heartwood", NetworkUpgrade::Heartwood),
    ("Canopy", NetworkUpgrade::Canopy),
    ("Nu5", NetworkUpgrade::Nu5),
];

fn named(vector: &str, name: &str) -> NetworkUpgrade {
    ACTIVE_UPGRADES
        .iter()
        .find(|(n, _)| *n == name)
        .unwrap_or_else(|| panic!("{}: unknown network upgrade {:?}", vector, name))
        .1
}

// ------------------------------------------------------------------- tests

#[test]
fn active_branch_ids_from_the_fixture_resolve_to_their_named_network_upgrade() {
    let mut seen = 0usize;
    for row in branch_id_rows() {
        if row.status != "active" {
            continue;
        }
        let expected = named("branch_ids.tsv", row.network_upgrade.as_ref().unwrap());
        assert_eq!(
            NetworkUpgrade::from_branch_id(row.hex),
            Some(expected),
            "branch id {:#010x} should resolve to {:?}",
            row.hex,
            expected
        );
        seen += 1;
    }
    assert_eq!(seen, 6, "expected 6 active rows in branch_ids.tsv, found {}", seen);
}

#[test]
fn each_active_upgrades_own_branch_id_matches_the_fixtures_hex_value() {
    for row in branch_id_rows() {
        if row.status != "active" {
            continue;
        }
        let upgrade = named("branch_ids.tsv", row.network_upgrade.as_ref().unwrap());
        let mine: u32 = upgrade
            .branch_id()
            .unwrap_or_else(|| panic!("{:?} should have a branch id", upgrade))
            .into();
        assert_eq!(
            mine, row.hex,
            "{:?}.branch_id() is {:#010x}, the fixture says {:#010x}",
            upgrade, mine, row.hex
        );
    }
}

#[test]
fn from_branch_id_is_the_exact_inverse_of_branch_id_for_every_active_upgrade() {
    for &(name, upgrade) in ACTIVE_UPGRADES.iter() {
        let id: u32 = upgrade
            .branch_id()
            .unwrap_or_else(|| panic!("{}: should have a branch id", name))
            .into();
        assert_eq!(
            NetworkUpgrade::from_branch_id(id),
            Some(upgrade),
            "{}: from_branch_id(branch_id()) did not round trip",
            name
        );
    }
}

#[test]
fn genesis_and_before_overwinter_have_no_consensus_branch_id() {
    // Neither upgrade predates a chain that has ever had a consensus branch
    // id, so `from_branch_id` must never be able to produce either one -- a
    // fact `from_branch_id`'s own contract depends on the other tests below
    // pinning down, since it is the only thing that would make "no unassigned
    // value resolves to Genesis" a meaningful claim rather than a vacuous one.
    assert_eq!(NetworkUpgrade::Genesis.branch_id(), None);
    assert_eq!(NetworkUpgrade::BeforeOverwinter.branch_id(), None);
}

#[test]
fn the_fixtures_unassigned_probe_values_resolve_to_no_upgrade() {
    let mut seen = 0usize;
    for row in branch_id_rows() {
        if row.status != "unassigned" {
            continue;
        }
        assert_eq!(
            NetworkUpgrade::from_branch_id(row.hex),
            None,
            "branch id {:#010x} is unassigned, but from_branch_id resolved it",
            row.hex
        );
        seen += 1;
    }
    assert_eq!(seen, 4, "expected 4 unassigned rows in branch_ids.tsv, found {}", seen);
}

#[test]
fn the_three_version_group_id_constants_are_rejected_as_branch_ids() {
    // The fixture's three `not_a_branch_id` rows are not arbitrary: they are
    // this protocol's own transaction version group ids, restated here as the
    // crate's own public constants so the coincidence is checked rather than
    // assumed. A `from_branch_id` that mixed these two four-byte namespaces up
    // would accept a V4 transaction's version group id as if it were the
    // branch id of the upgrade that happens to share its low bits.
    let constants: HashSet<u32> = [
        OVERWINTER_VERSION_GROUP_ID,
        SAPLING_VERSION_GROUP_ID,
        TX_V5_VERSION_GROUP_ID,
    ]
    .iter()
    .cloned()
    .collect();

    let mut seen = 0usize;
    for row in branch_id_rows() {
        if row.status != "not_a_branch_id" {
            continue;
        }
        assert!(
            constants.contains(&row.hex),
            "branch_ids.tsv's not_a_branch_id row {:#010x} is not one of the \
             three version group id constants",
            row.hex
        );
        assert_eq!(
            NetworkUpgrade::from_branch_id(row.hex),
            None,
            "{:#010x} is a version group id, not a branch id, but from_branch_id resolved it",
            row.hex
        );
        seen += 1;
    }
    assert_eq!(seen, 3, "expected 3 not_a_branch_id rows in branch_ids.tsv, found {}", seen);
    assert_eq!(
        constants.len(),
        3,
        "the three version group id constants should be pairwise distinct"
    );
}

#[test]
fn flipping_overwinters_low_order_bit_lands_exactly_on_the_fixtures_unassigned_probe() {
    // Not a general property -- a specific numeric coincidence the fixture was
    // built to exercise: Overwinter's branch id is 0x5ba81b19, and the fixture
    // lists 0x5ba81b18 (the same value with its low bit cleared) as its fourth
    // unassigned probe. Checking the two against each other, rather than
    // hardcoding both sides, is what makes this a fixture-driven test instead
    // of a restatement of a magic number.
    let overwinter: u32 = NetworkUpgrade::Overwinter
        .branch_id()
        .expect("Overwinter has a branch id")
        .into();
    let flipped = overwinter ^ 1;

    let has_probe = branch_id_rows()
        .iter()
        .any(|row| row.status == "unassigned" && row.hex == flipped);
    assert!(
        has_probe,
        "expected branch_ids.tsv's unassigned probes to include {:#010x} \
         (Overwinter's branch id {:#010x} with its low bit cleared)",
        flipped, overwinter
    );
    assert_eq!(
        NetworkUpgrade::from_branch_id(flipped),
        None,
        "{:#010x} is one bit off from an active branch id but is itself unassigned",
        flipped
    );
}

#[test]
fn flipping_the_low_order_bit_of_every_active_branch_id_makes_it_unresolvable() {
    for &(name, upgrade) in ACTIVE_UPGRADES.iter() {
        let id: u32 = upgrade
            .branch_id()
            .unwrap_or_else(|| panic!("{}: should have a branch id", name))
            .into();
        let flipped = id ^ 1;
        assert_ne!(
            NetworkUpgrade::from_branch_id(flipped),
            Some(upgrade),
            "{}: flipping branch id {:#010x}'s low bit still resolved to {}",
            name,
            id,
            name
        );
    }
}

#[test]
fn all_six_active_branch_ids_are_pairwise_distinct() {
    let ids: HashSet<u32> = branch_id_rows()
        .into_iter()
        .filter(|row| row.status == "active")
        .map(|row| row.hex)
        .collect();
    assert_eq!(
        ids.len(),
        6,
        "expected 6 pairwise-distinct active branch ids, found {} distinct values",
        ids.len()
    );
}

#[test]
fn from_branch_id_never_resolves_to_genesis_or_before_overwinter_for_any_fixture_value() {
    for row in branch_id_rows() {
        let resolved = NetworkUpgrade::from_branch_id(row.hex);
        assert_ne!(
            resolved,
            Some(NetworkUpgrade::Genesis),
            "{:#010x} resolved to Genesis, which has never had a branch id",
            row.hex
        );
        assert_ne!(
            resolved,
            Some(NetworkUpgrade::BeforeOverwinter),
            "{:#010x} resolved to BeforeOverwinter, which has never had a branch id",
            row.hex
        );
    }
}

#[test]
fn the_branch_id_and_network_upgrade_columns_of_the_v5_transaction_fixtures_agree_through_from_branch_id(
) {
    // Ties this group to the V5 wire format the codec driver parses: each
    // shipped V5 transaction fixture states its own nConsensusBranchId field
    // and the network upgrade that field is supposed to name, independently of
    // each other in the TSV, and from_branch_id is what connects them.
    for row in v5tx_rows() {
        let expected = named("v5tx.tsv", &row.network_upgrade);
        assert_eq!(
            NetworkUpgrade::from_branch_id(row.branch_id),
            Some(expected),
            "v5tx.tsv says branch id {:#010x} is {}, from_branch_id disagrees",
            row.branch_id,
            row.network_upgrade
        );
    }
}

#[test]
fn the_fixture_covers_all_six_network_upgrades_with_an_assigned_branch_id_and_no_duplicates() {
    // Guards the corpus itself: if a future edit to branch_ids.tsv dropped or
    // duplicated one of the six active rows, every test above would keep
    // passing on a silently smaller corpus.
    let active_names: HashSet<String> = branch_id_rows()
        .into_iter()
        .filter(|row| row.status == "active")
        .map(|row| row.network_upgrade.unwrap())
        .collect();
    let expected: HashSet<String> = ACTIVE_UPGRADES
        .iter()
        .map(|(name, _)| name.to_string())
        .collect();
    assert_eq!(
        active_names, expected,
        "branch_ids.tsv's active rows should name exactly the six upgrades with a branch id"
    );
}
