//! Group: bridgedesc — the downloaded bridge descriptor and how it is stored.
//!
//! Once a bridge descriptor has been fetched it is wrapped in a `BridgeDesc`,
//! filed under the `BridgeConfig` it was fetched for, and handed to the guard
//! manager through a provider trait.  A slot in that map may equally hold a
//! failure, which is why the error side is a boxed trait object with a kind and
//! a retry time rather than a concrete type.

use std::sync::Arc;

use futures::stream::{self, BoxStream, StreamExt};
use tor_checkable::{SelfSigned, Timebound};
use tor_error::{ErrorKind, HasKind, HasRetryTime, RetryTime};
use tor_guardmgr::bridge::{
    BridgeConfig, BridgeDesc, BridgeDescError, BridgeDescEvent, BridgeDescList, BridgeDescProvider,
};
use tor_linkspec::{HasRelayIds, RelayIdType};
use tor_netdoc::doc::routerdesc::RouterDesc;

use crate::common::fixture_text;

/// The fixture descriptor, checked and wrapped as a bridge descriptor.
fn bridge_desc() -> BridgeDesc {
    let text = fixture_text("routerdesc-akka.txt");
    let rd = RouterDesc::parse(&text)
        .expect("well-formed descriptor")
        .check_signature()
        .expect("correctly self-signed")
        .dangerously_assume_timely();
    BridgeDesc::new(Arc::new(rd))
}

/// The bridge line naming the same relay the fixture descriptor describes.
const AKKA_LINE: &str = "bridge 95.216.33.58:443 $56927E61B51E6F363FB55498150A6DDFCF7077F2";

/// Parse `line` as a bridge configuration.
fn bridge(line: &str) -> BridgeConfig {
    line.parse().expect("bridge line should parse")
}

/// A first-party failure that can occupy a slot in a `BridgeDescList`.
#[derive(Clone, Debug)]
struct StubDescError;

impl std::fmt::Display for StubDescError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str("could not reach the bridge")
    }
}
impl std::error::Error for StubDescError {}
impl HasKind for StubDescError {
    fn kind(&self) -> ErrorKind {
        ErrorKind::TorAccessFailed
    }
}
impl HasRetryTime for StubDescError {
    fn retry_time(&self) -> RetryTime {
        RetryTime::AfterWaiting
    }
}
impl BridgeDescError for StubDescError {}

/// A first-party provider holding a fixed list.
#[derive(Clone)]
struct StubProvider {
    /// What `bridges()` hands back.
    list: Arc<BridgeDescList>,
}

impl BridgeDescProvider for StubProvider {
    fn bridges(&self) -> Arc<BridgeDescList> {
        Arc::clone(&self.list)
    }
    fn events(&self) -> BoxStream<'static, BridgeDescEvent> {
        stream::once(async { BridgeDescEvent::SomethingChanged }).boxed()
    }
    fn set_bridges(&self, _bridges: &[BridgeConfig]) {}
}

#[test]
fn bridgedesc__identities_are_the_descriptors_own() {
    let d = bridge_desc();
    assert_eq!(
        d.identity(RelayIdType::Ed25519).unwrap().to_string(),
        "ed25519:CVTjf1oeaL616hH+1+UvYZ8OgkwF3z7UMITvJzm5r7A"
    );
    assert_eq!(
        d.identity(RelayIdType::Rsa).unwrap().to_string(),
        "$56927e61b51e6f363fb55498150a6ddfcf7077f2"
    );
}

#[test]
fn bridgedesc__the_wrapped_descriptor_stays_readable() {
    let d = bridge_desc();
    let rd: &RouterDesc = d.as_ref();
    let ports: Vec<_> = rd.or_ports().collect();
    assert_eq!(ports.len(), 2);
    assert_eq!(ports[0], "95.216.33.58:443".parse().unwrap());
    assert_eq!(
        hex::encode(rd.ntor_onion_key().as_bytes()),
        "329b3b52991613392e35d1a821dd6753e1210458ecc3337f7b7d39bfcf5da273"
    );
}

#[test]
fn bridgedesc__a_descriptor_can_confirm_the_identities_a_bridge_line_claimed() {
    let d = bridge_desc();

    // The line names only the RSA identity; the descriptor carries that one and
    // an Ed25519 identity besides, so it holds everything the line asked for.
    let rsa_only = bridge(AKKA_LINE);
    assert!(d.has_all_relay_ids_from(&rsa_only));
    assert!(!rsa_only.has_all_relay_ids_from(&d));

    // Naming the matching Ed25519 identity as well is still a match.
    let both = bridge(&format!(
        "{} ed25519:CVTjf1oeaL616hH+1+UvYZ8OgkwF3z7UMITvJzm5r7A",
        AKKA_LINE
    ));
    assert!(d.has_all_relay_ids_from(&both));
    assert!(d.same_relay_ids(&both));

    // Naming a different Ed25519 identity is not.
    let wrong = bridge(&format!("{} ed25519:{}", AKKA_LINE, crate::common::ED_B64));
    assert!(!d.has_all_relay_ids_from(&wrong));
}

#[test]
fn bridgedesc__descriptors_are_filed_under_the_bridge_line_that_fetched_them() {
    let mut list: BridgeDescList = BridgeDescList::new();
    list.insert(bridge(AKKA_LINE), Ok(bridge_desc()));

    // An independently parsed copy of the same line is the same key.
    let again = bridge(AKKA_LINE);
    match list.get(&again) {
        Some(Ok(d)) => assert_eq!(
            d.identity(RelayIdType::Rsa).unwrap().to_string(),
            "$56927e61b51e6f363fb55498150a6ddfcf7077f2"
        ),
        other => panic!("re-parsing the same line must hit the same slot: {:?}", other.is_some()),
    }

    // A line for a different port is a different bridge and a different key.
    let other = bridge("bridge 95.216.33.58:9001 $56927E61B51E6F363FB55498150A6DDFCF7077F2");
    assert!(list.get(&other).is_none());
    assert_eq!(list.len(), 1);
}

#[test]
fn bridgedesc__a_slot_may_hold_a_boxed_failure_instead() {
    let mut list: BridgeDescList = BridgeDescList::new();
    let boxed: Box<dyn BridgeDescError> = Box::new(StubDescError);
    list.insert(bridge(AKKA_LINE), Err(boxed));

    match list.get(&bridge(AKKA_LINE)) {
        Some(Err(e)) => {
            assert_eq!(e.kind(), ErrorKind::TorAccessFailed);
            assert_eq!(e.retry_time(), RetryTime::AfterWaiting);
            // The error side has to be clonable as a trait object, because the
            // whole list is handed out by value to every consumer.
            let cloned = e.clone();
            assert_eq!(cloned.kind(), ErrorKind::TorAccessFailed);
        }
        _ => panic!("the failing slot must survive being stored"),
    }
}

#[test]
fn bridgedesc__a_provider_hands_out_the_list_and_announces_changes() {
    let mut list: BridgeDescList = BridgeDescList::new();
    list.insert(bridge(AKKA_LINE), Ok(bridge_desc()));
    let provider = StubProvider {
        list: Arc::new(list),
    };

    // Consumers hold the provider as a trait object and clone it freely.
    let boxed: Box<dyn BridgeDescProvider> = Box::new(provider);
    let cloned = boxed.clone();
    assert_eq!(cloned.bridges().len(), 1);
    assert!(cloned.bridges().get(&bridge(AKKA_LINE)).is_some());

    // Changing the wanted set is a `&self` method: the provider is shared.
    cloned.set_bridges(&[bridge(AKKA_LINE)]);

    let mut events = cloned.events();
    let first = futures::executor::block_on(events.next());
    assert_eq!(first, Some(BridgeDescEvent::SomethingChanged));
}
