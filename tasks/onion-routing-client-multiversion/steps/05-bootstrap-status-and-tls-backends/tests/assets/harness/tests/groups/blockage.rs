//! Group `blockage` — turning a stalled connection into a diagnosis.
//!
//! The channel layer knows only "no TCP" or "no handshake". The client layer
//! has to translate that into something a user can act on, and the translation
//! is a public `From` impl so that callers can reason about it directly.
//!
//! `BlockageKind` is `#[non_exhaustive]` and carries no equality impl, so every
//! assertion here is on the rendered description rather than on the variant.

use arti_client::status::{Blockage, BlockageKind};
use tor_chanmgr::ConnBlockage;

fn described(b: ConnBlockage) -> String {
    BlockageKind::from(b).to_string()
}

#[test]
fn blockage__no_tcp_maps_to_offline() {
    assert_eq!(described(ConnBlockage::NoTcp), "We seem to be offline");
}

#[test]
fn blockage__no_handshake_maps_to_filtering() {
    assert_eq!(
        described(ConnBlockage::NoHandshake),
        "Our internet connection seems filtered"
    );
}

#[test]
fn blockage__kind_variants_have_documented_display_strings() {
    assert_eq!(BlockageKind::Offline.to_string(), "We seem to be offline");
    assert_eq!(
        BlockageKind::Filtering.to_string(),
        "Our internet connection seems filtered"
    );
    assert_eq!(
        BlockageKind::CantReachTor.to_string(),
        "Can't reach the Tor network"
    );
}

#[test]
fn blockage__kind_display_strings_are_distinct() {
    let all = [
        BlockageKind::Offline.to_string(),
        BlockageKind::Filtering.to_string(),
        BlockageKind::CantReachTor.to_string(),
    ];
    for (i, a) in all.iter().enumerate() {
        for (j, b) in all.iter().enumerate() {
            if i != j {
                assert_ne!(a, b, "two blockage kinds describe themselves identically");
            }
        }
    }
    assert_eq!(format!("{:?}", BlockageKind::CantReachTor), "CantReachTor");
}

#[test]
fn blockage__conversion_is_exposed_as_a_from_impl() {
    let convert: fn(ConnBlockage) -> BlockageKind = BlockageKind::from;
    assert_eq!(convert(ConnBlockage::NoTcp).to_string(), "We seem to be offline");
    // `Into` therefore works too, which is what callers actually write.
    let kind: BlockageKind = ConnBlockage::NoHandshake.into();
    assert_eq!(kind.to_string(), "Our internet connection seems filtered");
}

#[test]
fn blockage__blockage_type_is_displayable_and_cloneable() {
    // `Blockage` has no public constructor: it is only ever handed out by
    // `BootstrapStatus::blocked`. Its contract is that it is a cloneable,
    // printable diagnosis.
    crate::common::assert_clone::<Blockage>();
    crate::common::assert_debug::<Blockage>();
    crate::common::assert_display::<Blockage>();
    crate::common::assert_clone::<BlockageKind>();
    crate::common::assert_display::<BlockageKind>();
}
