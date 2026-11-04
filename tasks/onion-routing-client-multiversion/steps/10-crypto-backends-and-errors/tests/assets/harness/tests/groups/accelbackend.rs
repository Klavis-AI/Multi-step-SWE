//! Accelerated backend feature milestones (OpenSSL & SHA-1 asm)

#![allow(non_snake_case)]
use crate::common;

use std::collections::BTreeMap;

#[test]
fn accelbackend__llcrypto_declares_with_openssl_feature() {
    let features = common::features_of("tor-llcrypto");
    assert!(features.contains_key("with-openssl"), 
        "tor-llcrypto must declare with-openssl feature");
}

#[test]
fn accelbackend__llcrypto_declares_with_sha1_asm_feature() {
    let features = common::features_of("tor-llcrypto");
    assert!(features.contains_key("with-sha1-asm"),
        "tor-llcrypto must declare with-sha1-asm feature");
}

#[test]
fn accelbackend__with_openssl_pulls_openssl_dep() {
    let members = common::feature_members("tor-llcrypto", "with-openssl")
        .expect("with-openssl feature must exist");
    assert!(members.iter().any(|m| m.contains("openssl")),
        "with-openssl should activate openssl dependency");
}

#[test]
fn accelbackend__with_sha1_asm_pulls_sha1_asm() {
    common::assert_feature_contains("tor-llcrypto", "with-sha1-asm", "sha-1/asm");
}

#[test]
fn accelbackend__arti_client_declares_accel_sha1_asm() {
    let features = common::features_of("arti-client");
    assert!(features.contains_key("accel-sha1-asm"),
        "arti-client must declare accel-sha1-asm feature");
}

#[test]
fn accelbackend__arti_client_accel_sha1_asm_activates_llcrypto() {
    common::assert_feature_contains("arti-client", "accel-sha1-asm", "tor-llcrypto/with-sha1-asm");
}

#[test]
fn accelbackend__arti_client_declares_accel_openssl() {
    let features = common::features_of("arti-client");
    assert!(features.contains_key("accel-openssl"),
        "arti-client must declare accel-openssl feature");
}

#[test]
fn accelbackend__arti_client_accel_openssl_activates_llcrypto() {
    common::assert_feature_contains("arti-client", "accel-openssl", "tor-llcrypto/with-openssl");
}

#[test]
fn accelbackend__arti_declares_accel_sha1_asm() {
    let features = common::features_of("arti");
    assert!(features.contains_key("accel-sha1-asm"),
        "arti must declare accel-sha1-asm feature");
}

#[test]
fn accelbackend__arti_accel_sha1_asm_activates_client() {
    common::assert_feature_contains("arti", "accel-sha1-asm", "arti-client/accel-sha1-asm");
}
