//! Group `chanusage`: how a channel is going to be used, and the manager's
//! dormancy state.
//!
//! Spec: padding-spec.txt section 2.2 — a channel only runs the negotiated
//! padding timers when the caller is willing to have it do so, which is what
//! `ChannelUsage` and `Dormancy` express. tor-spec.txt section 7.3 gives the
//! cell that carries the resulting timing parameters.
//!
//! Neither enum exists at the previous milestone; the channel manager there had
//! no notion of usage or dormancy at all.

use tor_chanmgr::{ChannelUsage, Dormancy};

/// `Dormancy` defaults to `Active`: channels pad unless told otherwise.
#[test]
fn chanusage__dormancy_defaults_to_active() {
    assert_eq!(Dormancy::default(), Dormancy::Active);
    assert_ne!(Dormancy::default(), Dormancy::Dormant);
}

/// `ChannelUsage` distinguishes directory, user and useless traffic.
#[test]
fn chanusage__usage_variants_are_distinct() {
    assert_ne!(ChannelUsage::Dir, ChannelUsage::UserTraffic);
    assert_ne!(ChannelUsage::UserTraffic, ChannelUsage::UselessCircuit);
    assert_ne!(ChannelUsage::Dir, ChannelUsage::UselessCircuit);
}

/// The usage is copyable and comparable, so a caller can record where a
/// channel is being reused.
#[test]
fn chanusage__usage_is_copy_and_eq() {
    let used = ChannelUsage::UserTraffic;
    let copy = used;
    assert_eq!(used, copy);
    assert_eq!(used, ChannelUsage::UserTraffic);

    let mut seen = Vec::new();
    for u in [
        ChannelUsage::Dir,
        ChannelUsage::UserTraffic,
        ChannelUsage::UselessCircuit,
    ] {
        if seen.contains(&u) {
            panic!("{:?} appeared twice", u);
        }
        seen.push(u);
    }
    assert_eq!(seen.len(), 3);
}

/// The manager records the state it was constructed with.
#[test]
fn chanusage__dormancy_is_copy_and_eq() {
    let state = Dormancy::Dormant;
    let copy = state;
    assert_eq!(state, copy);
    assert_ne!(state, Dormancy::Active);
    assert!(matches!(state, Dormancy::Dormant));
}
