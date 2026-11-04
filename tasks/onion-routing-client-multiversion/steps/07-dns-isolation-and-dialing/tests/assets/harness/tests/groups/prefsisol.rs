//! Group `prefsisol`: the isolation half of `arti_client::StreamPrefs`.
//!
//! `StreamPrefs` keeps its fields private, so the documented observable is its
//! derived `Debug` rendering: two `StreamPrefs` render identically exactly when
//! they carry the same preferences.  Every assertion below compares one
//! rendering against another rendering of the same type; no case depends on
//! the literal text of the format.
//!
//! Graded items: `arti_client::StreamPrefs::{new, new_isolation_group,
//! set_isolation, isolate_every_stream}` and `arti_client::isolation`.

use arti_client::isolation::IsolationHelper;
use arti_client::{IsolationToken, StreamPrefs};

/// Render a `StreamPrefs` built by applying `f` to a fresh one.
fn render(f: impl FnOnce(&mut StreamPrefs)) -> String {
    let mut prefs = StreamPrefs::new();
    f(&mut prefs);
    format!("{:?}", prefs)
}

/// A user-supplied isolation rule.
#[derive(Clone, Debug, PartialEq, Eq)]
struct Realm(u8);

impl IsolationHelper for Realm {
    fn compatible_same_type(&self, other: &Self) -> bool {
        self == other
    }
    fn join_same_type(&self, other: &Self) -> Option<Self> {
        if self == other {
            Some(self.clone())
        } else {
            None
        }
    }
}

#[test]
fn prefsisol__default_prefs_request_no_extra_isolation() {
    assert_eq!(render(|_| {}), format!("{:?}", StreamPrefs::default()));
    // Two independently-created default preference sets are indistinguishable:
    // by default all connections on a client may share circuits.
    assert_eq!(render(|_| {}), render(|_| {}));
}

#[test]
fn prefsisol__each_new_isolation_group_is_distinct() {
    let a = render(|p| {
        p.new_isolation_group();
    });
    let b = render(|p| {
        p.new_isolation_group();
    });
    assert_ne!(
        a, b,
        "new_isolation_group() must allocate a fresh token each call"
    );
    assert_ne!(a, render(|_| {}));

    // A clone carries the same group as its source.
    let mut prefs = StreamPrefs::new();
    prefs.new_isolation_group();
    let clone = prefs.clone();
    assert_eq!(format!("{:?}", prefs), format!("{:?}", clone));
}

#[test]
fn prefsisol__set_isolation_records_the_supplied_rule() {
    let tok = IsolationToken::new();
    let other = IsolationToken::new();

    // The same token given to two preference sets makes them equivalent ...
    assert_eq!(
        render(|p| {
            p.set_isolation(tok);
        }),
        render(|p| {
            p.set_isolation(tok);
        })
    );
    // ... and a different token makes them differ.
    assert_ne!(
        render(|p| {
            p.set_isolation(tok);
        }),
        render(|p| {
            p.set_isolation(other);
        })
    );
    // Setting an isolation rule is not the same as leaving the default.
    assert_ne!(
        render(|p| {
            p.set_isolation(tok);
        }),
        render(|_| {})
    );
    // The last call wins.
    assert_eq!(
        render(|p| {
            p.set_isolation(other);
            p.set_isolation(tok);
        }),
        render(|p| {
            p.set_isolation(tok);
        })
    );
}

#[test]
fn prefsisol__set_isolation_accepts_any_user_rule() {
    // `set_isolation` is generic over anything convertible into a boxed
    // isolation object, so a caller's own rule may be used directly.
    assert_eq!(
        render(|p| {
            p.set_isolation(Realm(3));
        }),
        render(|p| {
            p.set_isolation(Realm(3));
        })
    );
    assert_ne!(
        render(|p| {
            p.set_isolation(Realm(3));
        }),
        render(|p| {
            p.set_isolation(Realm(4));
        })
    );
    // A user rule is distinguishable from a token rule.
    assert_ne!(
        render(|p| {
            p.set_isolation(Realm(3));
        }),
        render(|p| {
            p.set_isolation(IsolationToken::no_isolation());
        })
    );
    // Tuple rules are accepted as well.
    assert_eq!(
        render(|p| {
            p.set_isolation((Realm(1), Realm(2)));
        }),
        render(|p| {
            p.set_isolation((Realm(1), Realm(2)));
        })
    );
}

#[test]
fn prefsisol__isolate_every_stream_differs_from_a_shared_group() {
    let every = render(|p| {
        p.isolate_every_stream();
    });
    assert_ne!(every, render(|_| {}));
    assert_ne!(
        every,
        render(|p| {
            p.new_isolation_group();
        })
    );
    assert_ne!(
        every,
        render(|p| {
            p.set_isolation(IsolationToken::no_isolation());
        })
    );
    // Unlike `new_isolation_group`, this setting allocates nothing, so two
    // preference sets configured this way render identically.
    assert_eq!(
        every,
        render(|p| {
            p.isolate_every_stream();
        })
    );
}
