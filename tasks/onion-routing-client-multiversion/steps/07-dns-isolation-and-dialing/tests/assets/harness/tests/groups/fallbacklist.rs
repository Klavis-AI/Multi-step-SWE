//! Group `fallbacklist`: the configured set of fallback directories, and how a
//! member is chosen from it.  Choosing from an empty set must report a
//! structured "no fallbacks" error instead of panicking, because that is the
//! path a client takes on a network that is down.
//!
//! Graded items: `tor_guardmgr::fallback::FallbackList` and
//! `tor_guardmgr::PickGuardError::AllFallbacksDown`.

use std::collections::BTreeSet;

use tor_guardmgr::fallback::{FallbackDir, FallbackList};
use tor_guardmgr::PickGuardError;
use tor_linkspec::ChanTarget;

// The step-07 helpers this group needs live in `current`, apart from `common`
// so that no gate group is forced to compile them; see the note at the top of
// common/current.rs. current.rs re-exports common, so one path reaches both.
use crate::current::{fallback, fallback_list, rsa_id, seeded_rng};

#[test]
fn fallbacklist__an_empty_list_reports_its_length() {
    let empty = FallbackList::default();
    assert_eq!(empty.len(), 0);
    assert!(empty.is_empty());
    assert_eq!(empty, FallbackList::from(Vec::<FallbackDir>::new()));
}

#[test]
fn fallbacklist__a_list_is_built_from_any_iterator_of_fallbacks() {
    let dirs = vec![
        fallback(&["127.0.0.1:9001"], 1),
        fallback(&["127.0.0.1:9002"], 2),
        fallback(&["127.0.0.1:9003"], 3),
    ];
    let list = FallbackList::from(dirs.clone());
    assert_eq!(list.len(), 3);
    assert!(!list.is_empty());
    // An iterator works just as well as a vector.
    let from_iter = FallbackList::from(dirs.into_iter().collect::<Vec<_>>());
    assert_eq!(list, from_iter);
    // A differently-populated list is a different list.
    assert_ne!(list, fallback_list(&[1, 2]));
}

#[test]
fn fallbacklist__choosing_from_an_empty_list_reports_all_fallbacks_down() {
    let empty = FallbackList::default();
    let mut rng = seeded_rng(11);
    let err = empty
        .choose(&mut rng)
        .expect_err("an empty list cannot yield a fallback");
    match err {
        PickGuardError::AllFallbacksDown { retry_at } => {
            assert!(retry_at.is_none(), "there is nothing to retry");
        }
        other => panic!("expected AllFallbacksDown, got {:?}", other),
    }
}

#[test]
fn fallbacklist__choosing_from_a_single_entry_list_returns_that_entry() {
    let only = fallback(&["127.0.0.1:9001"], 7);
    let list = FallbackList::from(vec![only.clone()]);
    for seed in 0..8 {
        let mut rng = seeded_rng(seed);
        let chosen = list.choose(&mut rng).expect("a nonempty list yields a member");
        assert_eq!(chosen, &only);
    }
}

#[test]
fn fallbacklist__every_chosen_entry_is_a_member_of_the_list() {
    let list = fallback_list(&[1, 2, 3, 4, 5]);
    let members: BTreeSet<Vec<u8>> = (1u8..=5)
        .map(|s| rsa_id(s).as_bytes().to_vec())
        .collect();
    for seed in 0..64 {
        let mut rng = seeded_rng(seed);
        let chosen = list.choose(&mut rng).expect("a nonempty list yields a member");
        assert!(
            members.contains(&chosen.rsa_identity().as_bytes().to_vec()),
            "chose a directory that is not in the list"
        );
    }
}

#[test]
fn fallbacklist__the_choice_is_drawn_from_the_supplied_rng() {
    let list = fallback_list(&[1, 2, 3, 4, 5, 6, 7, 8]);

    // Same seed, same answer: the choice uses the caller's generator.
    let pick = |seed: u64| {
        let mut rng = seeded_rng(seed);
        list.choose(&mut rng)
            .expect("a nonempty list yields a member")
            .rsa_identity()
            .as_bytes()
            .to_vec()
    };
    for seed in 0..8 {
        assert_eq!(pick(seed), pick(seed));
    }

    // And it is a real choice, not always the first entry.
    let distinct: BTreeSet<Vec<u8>> = (0..64).map(pick).collect();
    assert!(
        distinct.len() > 1,
        "choose() always returned the same fallback"
    );
}
