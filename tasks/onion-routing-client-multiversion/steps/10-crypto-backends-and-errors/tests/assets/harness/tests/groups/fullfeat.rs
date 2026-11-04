//! Full feature set milestone

#![allow(non_snake_case)]
use crate::common;

#[test]
fn fullfeat__arti_declares_full_feature() {
    let features = common::features_of("arti");
    assert!(features.contains_key("full"), "arti must declare full feature");
}

#[test]
fn fullfeat__arti_full_activates_journald() {
    common::assert_feature_contains("arti", "full", "journald");
}

#[test]
fn fullfeat__arti_client_declares_full() {
    let features = common::features_of("arti-client");
    assert!(features.contains_key("full"), "arti-client must declare full feature");
}

#[test]
fn fullfeat__arti_client_full_activates_tokio() {
    common::assert_feature_contains("arti-client", "full", "tokio");
}

#[test]
fn fullfeat__arti_client_full_activates_async_std() {
    common::assert_feature_contains("arti-client", "full", "async-std");
}

#[test]
fn fullfeat__tor_dirmgr_declares_full() {
    let features = common::features_of("tor-dirmgr");
    assert!(features.contains_key("full"), "tor-dirmgr must declare full feature");
}

#[test]
fn fullfeat__tor_netdoc_declares_full() {
    let features = common::features_of("tor-netdoc");
    assert!(features.contains_key("full"), "tor-netdoc must declare full feature");
}
