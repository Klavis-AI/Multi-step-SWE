//! Group `pathintro`: introspection of the relays a client is about to talk
//! to.  Errors and logs identify a peer by a compact rendering of its first
//! address and its Ed25519 identity, and a directory request is described by a
//! one-hop path built from whatever channel target was picked.
//!
//! Graded items: `tor_linkspec::OwnedChanTarget` (including its `Display`),
//! and `tor_circmgr::path::TorPath`.

use tor_circmgr::path::TorPath;
use tor_linkspec::{ChanTarget, OwnedChanTarget};
use tor_netdir::Relay;

// The step-07 helpers this group needs live in `current`, apart from `common`
// so that no gate group is forced to compile them; see the note at the top of
// common/current.rs. current.rs re-exports common, so one path reaches both.
use crate::current::{addrs, chan_target, ed_id, fallback, rsa_id};

#[test]
fn pathintro__an_owned_channel_target_keeps_its_addresses_and_identities() {
    let target = OwnedChanTarget::new(
        addrs(&["127.0.0.1:9001", "127.0.0.2:9001"]),
        ed_id(3),
        rsa_id(3),
    );
    assert_eq!(target.addrs(), &addrs(&["127.0.0.1:9001", "127.0.0.2:9001"])[..]);
    assert_eq!(target.ed_identity(), &ed_id(3));
    assert_eq!(target.rsa_identity(), &rsa_id(3));
}

#[test]
fn pathintro__any_channel_target_can_be_captured_by_value() {
    // A fallback directory is a channel target; capturing it produces an
    // owned copy with exactly the same contents.
    let fb = fallback(&["127.0.0.1:9001", "127.0.0.2:9001"], 8);
    let owned = OwnedChanTarget::from_chan_target(&fb);
    assert_eq!(owned.addrs(), fb.addrs());
    assert_eq!(owned.ed_identity(), fb.ed_identity());
    assert_eq!(owned.rsa_identity(), fb.rsa_identity());

    // And capturing the copy again is idempotent.
    let again = OwnedChanTarget::from_chan_target(&owned);
    assert_eq!(again.to_string(), owned.to_string());

    // `from_chan_target` accepts an unsized target as well.
    let dynamic: &dyn ChanTarget = &fb;
    let from_dyn = OwnedChanTarget::from_chan_target(dynamic);
    assert_eq!(from_dyn.to_string(), owned.to_string());
}

#[test]
fn pathintro__a_peer_renders_as_its_first_address_and_identity() {
    let one = chan_target(&["127.0.0.1:9001"], 2);
    assert_eq!(one.to_string(), format!("[127.0.0.1:9001{}]", ed_id(2)));

    // With more than one address, the extras are elided with a `+`.
    let many = chan_target(&["127.0.0.1:9001", "127.0.0.2:9001", "127.0.0.3:9001"], 2);
    assert_eq!(many.to_string(), format!("[127.0.0.1:9001+{}]", ed_id(2)));

    // The rendering identifies the relay: a different identity renders
    // differently even at the same address.
    assert_ne!(one.to_string(), chan_target(&["127.0.0.1:9001"], 3).to_string());
}

#[test]
fn pathintro__a_peer_with_no_known_address_renders_a_placeholder() {
    let nowhere = OwnedChanTarget::new(vec![], ed_id(4), rsa_id(4));
    assert_eq!(nowhere.to_string(), format!("[?{}]", ed_id(4)));
    assert!(nowhere.addrs().is_empty());
    // It is still distinguishable from a peer that has an address.
    assert_ne!(
        nowhere.to_string(),
        chan_target(&["127.0.0.1:9001"], 4).to_string()
    );
}

#[test]
fn pathintro__a_directory_request_uses_a_one_hop_path() {
    let fb = fallback(&["127.0.0.1:9001"], 1);
    assert_eq!(TorPath::new_fallback_one_hop(&fb).len(), 1);

    let target = chan_target(&["127.0.0.1:9001", "127.0.0.2:9001"], 1);
    assert_eq!(TorPath::new_one_hop_owned(&target).len(), 1);

    // Any channel target will do, including a guard derived from a fallback.
    assert_eq!(TorPath::new_one_hop_owned(&fb.as_guard()).len(), 1);
    assert_eq!(TorPath::new_one_hop_owned(&fb).len(), 1);
}

#[test]
fn pathintro__a_path_reports_how_many_relays_it_contains() {
    // An empty multi-hop path has no relays at all ...
    let empty: Vec<Relay<'_>> = Vec::new();
    assert_eq!(TorPath::new_multihop(empty).len(), 0);
    // ... while every one-hop construction reports exactly one.
    let fb = fallback(&["127.0.0.1:9001"], 12);
    let target = chan_target(&["127.0.0.1:9001"], 12);
    for len in [
        TorPath::new_fallback_one_hop(&fb).len(),
        TorPath::new_one_hop_owned(&target).len(),
        TorPath::new_one_hop_owned(&fb.as_guard()).len(),
    ] {
        assert_eq!(len, 1);
    }
}
