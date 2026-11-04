//! Group `streamprefs` — the per-stream preference object carried by
//! `clone_with_prefs`.
//!
//! `StreamPrefs` exposes no getters, so its recorded state is observed through
//! its derived `Debug`. That rendering is part of the graded interface and is
//! written out in full in `TEST_INTERFACE.md`.

use arti_client::StreamPrefs;

const DEFAULT_DEBUG: &str =
    "StreamPrefs { ip_ver_pref: Ipv4Preferred, isolation: None, optimistic_stream: false }";

fn rendered(p: &StreamPrefs) -> String {
    format!("{p:?}")
}

#[test]
fn streamprefs__default_prefers_ipv4_without_isolation() {
    assert_eq!(rendered(&StreamPrefs::default()), DEFAULT_DEBUG);
}

#[test]
fn streamprefs__new_matches_default() {
    assert_eq!(rendered(&StreamPrefs::new()), DEFAULT_DEBUG);
}

#[test]
fn streamprefs__ipv6_preferred_changes_only_the_ip_preference() {
    let mut p = StreamPrefs::new();
    p.ipv6_preferred();
    assert_eq!(
        rendered(&p),
        "StreamPrefs { ip_ver_pref: Ipv6Preferred, isolation: None, optimistic_stream: false }"
    );
}

#[test]
fn streamprefs__ipv6_only_and_ipv4_only_are_distinct() {
    let mut six = StreamPrefs::new();
    six.ipv6_only();
    let mut four = StreamPrefs::new();
    four.ipv4_only();
    assert!(
        rendered(&six).contains("Ipv6Only"),
        "ipv6_only was not recorded: {}",
        rendered(&six)
    );
    assert!(
        rendered(&four).contains("Ipv4Only"),
        "ipv4_only was not recorded: {}",
        rendered(&four)
    );
    assert_ne!(rendered(&six), rendered(&four));
    // ipv4_preferred restores the starting state rather than being a no-op.
    four.ipv4_preferred();
    assert_eq!(rendered(&four), DEFAULT_DEBUG);
}

#[test]
fn streamprefs__optimistic_sets_the_optimistic_flag() {
    let mut p = StreamPrefs::new();
    p.optimistic();
    assert_eq!(
        rendered(&p),
        "StreamPrefs { ip_ver_pref: Ipv4Preferred, isolation: None, optimistic_stream: true }"
    );
}

#[test]
fn streamprefs__setters_return_the_same_builder_for_chaining() {
    let mut p = StreamPrefs::new();
    {
        let chained = p.ipv6_only().optimistic();
        assert!(format!("{chained:?}").contains("Ipv6Only"));
    }
    assert_eq!(
        rendered(&p),
        "StreamPrefs { ip_ver_pref: Ipv6Only, isolation: None, optimistic_stream: true }"
    );
}

#[test]
fn streamprefs__preferences_are_cloneable_and_independent() {
    let mut original = StreamPrefs::new();
    original.ipv6_only();
    let copy = original.clone();
    original.ipv4_only();
    assert!(rendered(&copy).contains("Ipv6Only"));
    assert!(rendered(&original).contains("Ipv4Only"));
}
