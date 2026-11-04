//! Group `family_members` — mutual family membership as seen by a `NetDir`.
//!
//! Path selection must avoid putting two relays of the same family into one
//! circuit, and the guard restriction added by this step is built from the
//! chosen exit's family.  That makes `NetDir::known_family_members` the item
//! the rest of the milestone stands on: it reports the relays that both list
//! the given relay *and* are listed by it.

// The synthetic directory fixtures. They are included here rather than in
// `tests/common/mod.rs` so that a candidate who has not built
// `tor_netdir::testnet` loses only the groups that need a directory; see
// that file's header.
#[path = "netdir_support.rs"]
mod netdir_support;
use tor_netdir::NetDir;

/// The test-network indices of the relays `dir` considers family of relay
/// `idx`, sorted so the assertion does not depend on declaration order.
fn members(dir: &NetDir, idx: u8) -> Vec<u8> {
    let relay = dir
        .by_id(&netdir_support::ed_id(idx))
        .expect("relay should be listed in the test directory");
    let mut found: Vec<u8> = dir
        .known_family_members(&relay)
        .map(|other| netdir_support::ed_index(other.id()))
        .collect();
    found.sort_unstable();
    found
}

#[test]
fn family_members__adjacent_relays_declare_each_other() {
    // The shipped test network puts relay `n` in a family with relay `n ^ 1`,
    // and both sides say so, so each relay has exactly one known family member.
    let dir = netdir_support::netdir_custom(|_idx, _nb| {});
    assert_eq!(members(&dir, 4), vec![5]);
    assert_eq!(members(&dir, 5), vec![4]);
    assert_eq!(members(&dir, 30), vec![31]);
    assert_eq!(members(&dir, 31), vec![30]);
}

#[test]
fn family_members__a_one_sided_declaration_is_not_a_family() {
    // Relay 11 stops listing relay 10 and lists relay 12 instead.  Neither
    // claim is reciprocated, so no family relationship survives.
    let dir = netdir_support::netdir_custom(|idx, nb| {
        if idx == 11 {
            nb.md.family(
                netdir_support::family_of(&[12])
                    .parse()
                    .expect("family line should parse"),
            );
        }
    });
    assert_eq!(members(&dir, 10), Vec::<u8>::new());
    assert_eq!(members(&dir, 11), Vec::<u8>::new());
    // Relay 12 is still in a mutual family with relay 13, which is what it
    // declared; relay 11's unreciprocated claim does not enter into it.
    assert_eq!(members(&dir, 12), vec![13]);
}

#[test]
fn family_members__a_relay_that_declares_nothing_has_no_members() {
    let dir = netdir_support::netdir_custom(|idx, nb| {
        if idx == 20 {
            nb.md
                .family("".parse().expect("an empty family line should parse"));
        }
    });
    assert_eq!(members(&dir, 20), Vec::<u8>::new());
    // Relay 21 still lists relay 20, but that is only one half of a family.
    assert_eq!(members(&dir, 21), Vec::<u8>::new());
}

#[test]
fn family_members__members_without_a_microdescriptor_are_omitted() {
    // Relay 30 declares relays 31 and 32; relay 32 declares relay 30 back, but
    // its microdescriptor is not in the directory, so its half of the claim
    // cannot be checked and it must be left out.
    let dir = netdir_support::netdir_custom(|idx, nb| {
        match idx {
            30 => {
                nb.md.family(
                    netdir_support::family_of(&[31, 32])
                        .parse()
                        .expect("family line should parse"),
                );
            }
            32 => {
                nb.md.family(
                    netdir_support::family_of(&[30])
                        .parse()
                        .expect("family line should parse"),
                );
                nb.omit_md = true;
            }
            _ => {}
        };
    });
    assert!(
        dir.missing_descriptor_for(&netdir_support::rsa_id(32)),
        "the test setup should leave relay 32's microdescriptor missing"
    );
    assert_eq!(members(&dir, 30), vec![31]);
}

#[test]
fn family_members__a_larger_mutual_family_is_reported_in_full() {
    let dir = netdir_support::netdir_custom(|idx, nb| {
        let declared = match idx {
            34 => Some(netdir_support::family_of(&[36, 38])),
            36 => Some(netdir_support::family_of(&[34, 38])),
            38 => Some(netdir_support::family_of(&[34, 36])),
            _ => None,
        };
        if let Some(fam) = declared {
            nb.md
                .family(fam.parse().expect("family line should parse"));
        }
    });
    assert_eq!(members(&dir, 34), vec![36, 38]);
    assert_eq!(members(&dir, 36), vec![34, 38]);
    assert_eq!(members(&dir, 38), vec![34, 36]);
    // Relay 35 used to be relay 34's family; after relay 34 stopped listing it
    // the relationship is one-sided and therefore gone.
    assert_eq!(members(&dir, 35), Vec::<u8>::new());
}
