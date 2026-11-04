//! Group: rdesc — router-descriptor parsing for bridges.
//!
//! A bridge is contacted before its Ed25519 identity is known, so the client
//! has to parse the bridge's own router descriptor and read the identities,
//! addresses and onion key out of it.  Every assertion here is against the
//! bytes of a real, self-consistent descriptor shipped as a fixture, so the
//! group grades the parser rather than a round trip through arti's writer.

use std::time::{Duration, SystemTime};

use tor_checkable::{SelfSigned, Timebound};
use tor_netdoc::doc::routerdesc::RouterDesc;

use crate::common::fixture_text;

/// Parse the fixture, check its self-signatures, and hand back the descriptor.
fn akka() -> RouterDesc {
    let text = fixture_text("routerdesc-akka.txt");
    RouterDesc::parse(&text)
        .expect("the fixture is a well-formed router descriptor")
        .check_signature()
        .expect("the fixture is correctly self-signed")
        .dangerously_assume_timely()
}

#[test]
fn rdesc__rsa_identity_is_the_fingerprint_line() {
    let d = akka();
    // dir-spec.txt §2.1.1: `fingerprint` is the SHA-1 digest of the identity
    // key, and `RsaIdentity` displays it as `$` + 40 lowercase hex digits.
    assert_eq!(
        d.rsa_identity().to_string(),
        "$56927e61b51e6f363fb55498150a6ddfcf7077f2"
    );
    assert_eq!(d.rsa_identity().as_bytes().len(), 20);
}

#[test]
fn rdesc__ed_identity_comes_from_the_identity_certificate() {
    let d = akka();
    // The Ed25519 identity is the signing key of the `identity-ed25519`
    // certificate, and must agree with the `master-key-ed25519` line.
    assert_eq!(
        d.ed_identity().to_string(),
        "CVTjf1oeaL616hH+1+UvYZ8OgkwF3z7UMITvJzm5r7A"
    );
    assert_eq!(
        hex::encode(d.ed_identity().as_bytes()),
        "0954e37f5a1e68beb5ea11fed7e52f619f0e824c05df3ed43084ef2739b9afb0"
    );
}

#[test]
fn rdesc__or_ports_are_the_router_line_then_the_or_address_lines() {
    let d = akka();
    let ports: Vec<_> = d.or_ports().collect();
    assert_eq!(
        ports,
        vec![
            "95.216.33.58:443".parse().unwrap(),
            "[2a01:4f9:2a:2145::2]:443".parse().unwrap(),
        ],
        "the IPv4 address of the `router` line comes first"
    );
}

#[test]
fn rdesc__published_is_the_published_line_in_utc() {
    let d = akka();
    // `published 2022-11-14 19:58:52`, which dir-spec.txt §2.1.1 defines as UTC.
    assert_eq!(
        d.published(),
        SystemTime::UNIX_EPOCH + Duration::from_secs(1_668_455_932)
    );
}

#[test]
fn rdesc__ntor_onion_key_is_the_curve25519_key_from_its_own_line() {
    let d = akka();
    assert_eq!(
        hex::encode(d.ntor_onion_key().as_bytes()),
        "329b3b52991613392e35d1a821dd6753e1210458ecc3337f7b7d39bfcf5da273"
    );
}

#[test]
fn rdesc__protocols_are_the_proto_line() {
    let d = akka();
    assert_eq!(
        d.protocols().to_string(),
        "Cons=1-2 Desc=1-2 DirCache=2 FlowCtrl=1-2 HSDir=2 HSIntro=4-5 \
         HSRend=1-2 Link=1-5 LinkAuth=1,3 Microdesc=1-2 Padding=2 Relay=1-4"
    );
}

#[test]
fn rdesc__trailing_empty_lines_are_tolerated() {
    // Some relays append blank lines after `router-signature`.  A bridge client
    // has to accept them, because it cannot ask the bridge to resend.
    let text = fixture_text("routerdesc-akka.txt");
    let padded = format!("{}\n\n\n", text.trim_end());
    let d = RouterDesc::parse(&padded)
        .expect("trailing empty lines must not stop the parse")
        .check_signature()
        .expect("trailing empty lines are outside the signed material")
        .dangerously_assume_timely();
    assert_eq!(
        d.rsa_identity().to_string(),
        "$56927e61b51e6f363fb55498150a6ddfcf7077f2"
    );
}

#[test]
fn rdesc__a_tampered_body_fails_its_own_signature() {
    let text = fixture_text("routerdesc-akka.txt");
    let tampered = text.replacen("router Akka ", "router Bkka ", 1);
    assert_ne!(tampered, text, "the fixture must contain the nickname");
    let unchecked = RouterDesc::parse(&tampered).expect("still well-formed");
    assert!(
        unchecked.check_signature().is_err(),
        "one changed byte must invalidate the router signature"
    );
}
