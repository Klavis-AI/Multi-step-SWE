//! The on-disk state format version, and the state-side surface of this
//! milestone's new chain data.
//!
//! This release changes what `zebra-state` writes to disk: chain value pool
//! balances and a chain history tree are now part of the finalized state, and the
//! Sapling/Orchard note commitment tree roots are cached alongside them.  None of
//! that is backwards compatible with an existing database, so the format version
//! must be bumped and the old directory must not be reused.  `zebra-state` keys
//! its cache directory on `DATABASE_FORMAT_VERSION`, so the constant *is* the
//! migration: leaving it alone silently corrupts a node that upgrades in place.
//!
//! This is a small group on purpose.  Most of the state-side work at this tag --
//! the finalized-state column families, the legacy chain check, the contextual
//! value-pool checks -- lives behind `mod service;` and is not reachable from an
//! integration test.  What is reachable is the version, the tuning constants, and
//! the error conversions, and those are what this scores.

use std::io;

use zebra_chain::history_tree::HistoryTreeError;
use zebra_state::constants::{
    DATABASE_FORMAT_VERSION, MAX_BLOCK_REORG_HEIGHT, MAX_LEGACY_CHAIN_BLOCKS,
    MIN_TRANSPARENT_COINBASE_MATURITY,
};
use zebra_state::ValidateContextError;

#[test]
fn the_on_disk_state_format_version_is_bumped_past_the_previous_release() {
    // The previous release wrote format 6.  This release adds chain value pool
    // balances, the chain history tree, and cached note commitment tree roots to
    // the finalized state, so a database written by the previous release cannot
    // be read by this one.
    assert_eq!(
        DATABASE_FORMAT_VERSION, 9,
        "the on-disk state format version must be 9 at this release"
    );
    assert!(
        DATABASE_FORMAT_VERSION > 6,
        "the format version must be strictly greater than the previous release's 6, \
         or an upgraded node will reopen an incompatible database; got {}",
        DATABASE_FORMAT_VERSION
    );
}

#[test]
fn the_legacy_chain_check_scans_a_bounded_number_of_blocks() {
    // A node that finds no NU5 transaction within this many blocks concludes it is
    // following a pre-NU5 legacy chain.  The bound must be finite and small enough
    // to be checked at startup.
    assert_eq!(
        MAX_LEGACY_CHAIN_BLOCKS, 100,
        "the legacy chain check looks at 100 blocks"
    );
}

#[test]
fn the_transparent_coinbase_maturity_is_shared_with_zebra_chain() {
    // This constant moved into `zebra_chain::transparent` and is re-exported here,
    // so that consensus code can reach it without depending on `zebra-state`.
    // Both paths must name one value, not two copies that can drift apart.
    assert_eq!(
        MIN_TRANSPARENT_COINBASE_MATURITY, 100,
        "a transparent coinbase output matures after 100 blocks"
    );
    assert_eq!(
        MIN_TRANSPARENT_COINBASE_MATURITY,
        zebra_chain::transparent::MIN_TRANSPARENT_COINBASE_MATURITY,
        "zebra_state::constants::MIN_TRANSPARENT_COINBASE_MATURITY must be the \
         zebra_chain::transparent constant, re-exported"
    );
    // A chain can be rolled back to just short of the maturity window: the
    // hundredth block back is the one that matured the coinbase, so only the 99
    // blocks after it may be reorganised away.
    assert_eq!(
        MAX_BLOCK_REORG_HEIGHT,
        MIN_TRANSPARENT_COINBASE_MATURITY - 1,
        "the maximum reorg height is one block short of the coinbase maturity"
    );
}

#[test]
fn a_history_tree_failure_becomes_a_contextual_validation_error() {
    // Building the chain history tree is now part of contextual validation, so its
    // failures have to be representable as a ValidateContextError.
    let inner = HistoryTreeError::from(io::Error::new(io::ErrorKind::Other, "no room"));
    let contextual = ValidateContextError::from(inner);

    let rendered = format!("{}", contextual);
    assert!(
        rendered.contains("history tree"),
        "a history tree failure must describe itself as one, got {:?}",
        rendered
    );
    assert!(
        std::error::Error::source(&contextual).is_some(),
        "the history tree error must be reachable as the source of the \
         contextual validation error"
    );
}
