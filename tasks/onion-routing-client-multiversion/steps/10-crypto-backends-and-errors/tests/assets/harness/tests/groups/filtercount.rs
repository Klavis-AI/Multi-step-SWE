//! `tor_basic_utils::iter`: structural counters that report how many
//! candidates a filter accepted and rejected.

#![allow(non_snake_case)]

use tor_basic_utils::iter::{FilterCount, IteratorExt};

#[test]
fn filtercount__tallies_accepted_and_rejected_items() {
    let mut count = FilterCount::default();
    let kept: Vec<u32> = (1..=10).filter_cnt(&mut count, |x| x % 2 == 0).collect();
    assert_eq!(kept, vec![2, 4, 6, 8, 10]);
    assert_eq!(
        count,
        FilterCount {
            n_accepted: 5,
            n_rejected: 5
        }
    );
}

#[test]
fn filtercount__a_default_counter_is_empty() {
    let count = FilterCount::default();
    assert_eq!(count.n_accepted, 0);
    assert_eq!(count.n_rejected, 0);
}

#[test]
fn filtercount__the_counter_is_reset_when_the_filter_is_installed() {
    let mut count = FilterCount {
        n_accepted: 99,
        n_rejected: 99,
    };
    let kept: Vec<u32> = (1..=3).filter_cnt(&mut count, |_| true).collect();
    assert_eq!(kept, vec![1, 2, 3]);
    assert_eq!(count.n_accepted, 3, "stale counts must not accumulate");
    assert_eq!(count.n_rejected, 0);
}

#[test]
fn filtercount__an_unexhausted_iterator_gives_a_partial_count() {
    let mut count = FilterCount::default();
    let first = (1..=100)
        .filter_cnt(&mut count, |x| x % 10 == 0)
        .next()
        .expect("10 is the first multiple of ten");
    assert_eq!(first, 10);
    assert_eq!(count.n_accepted, 1);
    assert_eq!(count.n_rejected, 9, "only the items examined are counted");
}

#[test]
fn filtercount__count_tallies_a_predicate_and_returns_it() {
    let mut count = FilterCount::default();
    assert!(count.count(true));
    assert!(!count.count(false));
    assert!(!count.count(false));
    assert_eq!(count.n_accepted, 1);
    assert_eq!(count.n_rejected, 2);
}

#[test]
fn filtercount__display_frac_rejected_is_rejected_over_total() {
    let mut count = FilterCount::default();
    let _: Vec<u32> = (1..=10).filter_cnt(&mut count, |x| x % 2 == 0).collect();
    assert_eq!(count.display_frac_rejected().to_string(), "5/10");
    let empty = FilterCount::default();
    assert_eq!(empty.display_frac_rejected().to_string(), "0/0");
}
