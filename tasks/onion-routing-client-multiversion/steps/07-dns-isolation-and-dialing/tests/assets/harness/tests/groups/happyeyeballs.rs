//! Group `happyeyeballs`: RFC 8305 happy-eyeballs connection attempts.
//! At step 07, `ChanMgr::get_or_launch` tries multiple addresses in
//! parallel with 150ms stagger, and `Error::ChannelBuild` reports all
//! failed addresses when all attempts fail.
//!
//! Graded items: `tor_chanmgr::{ChanMgr, Error}`.

use std::net::SocketAddr;

use tor_chanmgr::{ChanMgr, Error};
use tor_linkspec::OwnedChanTarget;

use crate::common::{ed_id, rsa_id};

#[test]
fn happyeyeballs__unusable_target_with_no_addresses() {
    tor_rtcompat::test_with_one_runtime!(|rt| async move {
        let mgr = ChanMgr::new(rt);
        let target = OwnedChanTarget::new(vec![], ed_id(42), rsa_id(42));
        let err = mgr.get_or_launch(&target).await.err().expect("must fail");
        assert!(matches!(err, Error::UnusableTarget(_)));
    });
}

#[test]
fn happyeyeballs__channel_build_error_carries_all_failed_addresses() {
    tor_rtcompat::test_with_one_runtime!(|rt| async move {
        let mgr = ChanMgr::new(rt);
        // Two loopback addresses that will refuse connection.
        let addr1: SocketAddr = "127.0.0.1:1".parse().unwrap();
        let addr2: SocketAddr = "127.0.0.1:2".parse().unwrap();
        let target = OwnedChanTarget::new(vec![addr1, addr2], ed_id(99), rsa_id(99));
        let err = mgr.get_or_launch(&target).await.err().expect("must fail");
        if let Error::ChannelBuild { addresses } = &err {
            assert_eq!(addresses.len(), 2);
            let reported: Vec<SocketAddr> = addresses.iter().map(|(a, _)| *a).collect();
            assert!(reported.contains(&addr1));
            assert!(reported.contains(&addr2));
        } else {
            panic!("expected ChannelBuild error, got {err:?}");
        }
    });
}

#[test]
fn happyeyeballs__channel_build_at_0_1_0_does_not_exist() {
    // At step 06, the error is `Error::Io` for each attempt; at step 07 it
    // becomes `Error::ChannelBuild`. This case documents the migration.
    use tor_error::HasKind;
    tor_rtcompat::test_with_one_runtime!(|rt| async move {
        let mgr = ChanMgr::new(rt);
        let addr: SocketAddr = "127.0.0.1:3".parse().unwrap();
        let target = OwnedChanTarget::new(vec![addr], ed_id(7), rsa_id(7));
        let err = mgr.get_or_launch(&target).await.err().expect("must fail");
        // The kind is still TorAccessFailed for both variants.
        assert_eq!(err.kind(), tor_error::ErrorKind::TorAccessFailed);
    });
}

#[test]
fn happyeyeballs__error_display_mentions_both_addresses() {
    tor_rtcompat::test_with_one_runtime!(|rt| async move {
        let mgr = ChanMgr::new(rt);
        let addr1: SocketAddr = "127.0.0.1:4".parse().unwrap();
        let addr2: SocketAddr = "127.0.0.1:5".parse().unwrap();
        let target = OwnedChanTarget::new(vec![addr1, addr2], ed_id(11), rsa_id(11));
        let err = mgr.get_or_launch(&target).await.err().expect("must fail");
        let rendered = err.to_string();
        assert!(rendered.contains("127.0.0.1:4"));
        assert!(rendered.contains("127.0.0.1:5"));
    });
}

#[test]
fn happyeyeballs__three_refused_addresses_are_each_reported_once() {
    // Every address that was attempted contributes exactly one (address,
    // error) pair to `ChannelBuild`: the report is per-address, it is not
    // deduplicated, and it is not multiplied by the internal retry count.
    // This is the structural consequence of dialing all addresses in
    // parallel; no wall-clock timing is asserted.
    tor_rtcompat::test_with_one_runtime!(|rt| async move {
        let mgr = ChanMgr::new(rt);
        let wanted: Vec<SocketAddr> = vec!["127.0.0.1:6", "127.0.0.1:7", "127.0.0.1:8"]
            .into_iter()
            .map(|s| s.parse().unwrap())
            .collect();
        let target = OwnedChanTarget::new(wanted.clone(), ed_id(13), rsa_id(13));
        let err = mgr.get_or_launch(&target).await.err().expect("must fail");
        let Error::ChannelBuild { addresses } = &err else {
            panic!("expected ChannelBuild error, got {err:?}");
        };
        assert_eq!(
            addresses.len(),
            3,
            "one entry per attempted address, got {addresses:?}"
        );
        let mut reported: Vec<SocketAddr> = addresses.iter().map(|(a, _)| *a).collect();
        reported.sort();
        let mut expected = wanted;
        expected.sort();
        assert_eq!(reported, expected);
    });
}
