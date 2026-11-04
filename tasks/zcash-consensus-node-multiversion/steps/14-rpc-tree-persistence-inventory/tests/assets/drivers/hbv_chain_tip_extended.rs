//! Extended ChainTip trait methods.
//!
//! Harbor verifier driver -- injected into `zebra-chain/tests/` at grading
//! time. Public API only; no dependency the crate does not already declare.
//!
//! At v1.0.0-beta.6 the ChainTip trait gained four new methods:
//!   - best_tip_height_and_hash
//!   - best_tip_block_time
//!   - best_tip_height_and_block_time
//!   - estimate_network_chain_tip_height (default method using a helper)
//!
//! The predecessor tree (app-13) only had:
//!   - best_tip_height
//!   - best_tip_hash
//!   - best_tip_mined_transaction_ids
//!
//! Negative direction: any test that calls best_tip_height_and_hash,
//! best_tip_block_time, or best_tip_height_and_block_time on a ChainTip
//! implementation fails to compile against app-13, which does not declare
//! those methods on the trait.

use std::sync::Arc;

use chrono::{DateTime, Duration, TimeZone, Utc};

use zebra_chain::block::{self, Height};
use zebra_chain::chain_tip::{ChainTip, NoChainTip};
use zebra_chain::parameters::Network;
use zebra_chain::transaction;

// ── NoChainTip returns None for all optional methods ────────────────────────

#[test]
fn no_chain_tip_height_and_hash_is_none() {
    // best_tip_height_and_hash must return None when there is no tip.
    assert_eq!(
        NoChainTip.best_tip_height_and_hash(),
        None,
        "NoChainTip::best_tip_height_and_hash must return None"
    );
}

#[test]
fn no_chain_tip_block_time_is_none() {
    // best_tip_block_time must return None when there is no tip.
    assert_eq!(
        NoChainTip.best_tip_block_time(),
        None,
        "NoChainTip::best_tip_block_time must return None"
    );
}

#[test]
fn no_chain_tip_height_and_block_time_is_none() {
    // best_tip_height_and_block_time must return None when there is no tip.
    assert_eq!(
        NoChainTip.best_tip_height_and_block_time(),
        None,
        "NoChainTip::best_tip_height_and_block_time must return None"
    );
}

#[test]
fn no_chain_tip_estimate_network_height_is_none() {
    // The default method estimate_network_chain_tip_height delegates to
    // best_tip_height_and_block_time, which returns None for NoChainTip,
    // so it must also return None.
    let now = Utc::now();
    assert_eq!(
        NoChainTip.estimate_network_chain_tip_height(Network::Mainnet, now),
        None,
        "NoChainTip::estimate_network_chain_tip_height must return None when no tip"
    );
}

// ── Trait is object-safe: verify through trait object ───────────────────────

#[test]
fn chain_tip_is_usable_as_trait_object() {
    // Verify all new methods are callable through a &dyn ChainTip reference.
    let tip: &dyn ChainTip = &NoChainTip;
    assert!(tip.best_tip_height_and_hash().is_none());
    assert!(tip.best_tip_block_time().is_none());
    assert!(tip.best_tip_height_and_block_time().is_none());
    assert!(tip.estimate_network_chain_tip_height(Network::Testnet, Utc::now()).is_none());
}

// ── existing methods still present ──────────────────────────────────────────

#[test]
fn no_chain_tip_original_methods_still_none() {
    // The three methods that already existed in app-13 must still work.
    assert!(NoChainTip.best_tip_height().is_none());
    assert!(NoChainTip.best_tip_hash().is_none());
    assert!(NoChainTip.best_tip_mined_transaction_ids().is_empty());
}

#[test]
fn no_chain_tip_all_methods_coherent() {
    // height_and_hash returns None iff individual height/hash return None.
    let hh = NoChainTip.best_tip_height_and_hash();
    let h = NoChainTip.best_tip_height();
    let hash = NoChainTip.best_tip_hash();
    assert_eq!(hh.is_some(), h.is_some() && hash.is_some(),
        "height_and_hash Some-ness must agree with individual height and hash methods");
}

// ── A tip that is actually present ──────────────────────────────────────────
//
// Everything above this line calls the trait on `NoChainTip`, whose methods all
// return `None`. That is a complete description of the *absent* case and no
// description at all of the present one: an implementation that declares the
// four new methods on the trait with bodies `{ None }` and gives `NoChainTip`
// nothing passes every assertion above, including the trait-object test --
// `&dyn ChainTip` is object-safe either way. The whole
// `estimate_network_chain_tip_height` path would be dead code.
//
// `MockChainTip` exists upstream (`zebra-chain/src/chain_tip/mock.rs`) but sits
// behind `#[cfg(any(test, feature = "proptest-impl"))]`, and an integration
// test is compiled without that feature, so it is unreachable here. A
// driver-local implementor is the right shape -- and it doubles as a gate on
// the trait's exact method set, since a missing or renamed method makes this
// `impl` block fail to compile.

struct FixedTip(Height, DateTime<Utc>);

impl ChainTip for FixedTip {
    fn best_tip_height(&self) -> Option<Height> {
        Some(self.0)
    }
    fn best_tip_hash(&self) -> Option<block::Hash> {
        None
    }
    fn best_tip_height_and_hash(&self) -> Option<(Height, block::Hash)> {
        None
    }
    fn best_tip_block_time(&self) -> Option<DateTime<Utc>> {
        Some(self.1)
    }
    fn best_tip_height_and_block_time(&self) -> Option<(Height, DateTime<Utc>)> {
        Some((self.0, self.1))
    }
    fn best_tip_mined_transaction_ids(&self) -> Arc<[transaction::Hash]> {
        Arc::new([])
    }
    // `estimate_network_chain_tip_height` is deliberately NOT overridden: it
    // must be a provided (default) method on the trait, and the test below
    // exercises that default body.
}

#[test]
fn estimate_network_chain_tip_height_advances_with_wall_clock() {
    let t0 = Utc.timestamp(1_600_000_000, 0);
    let tip = FixedTip(Height(1_000_000), t0);

    // At the tip's own block time, zero time has elapsed, so the estimate is
    // the tip height itself. An implementation that ignores the block time and
    // returns `best_tip_height()` unchanged also passes this line -- the next
    // one is what separates them.
    let at_tip = tip
        .estimate_network_chain_tip_height(Network::Mainnet, t0)
        .expect("a present tip must produce an estimate");
    assert_eq!(
        at_tip,
        Height(1_000_000),
        "at the tip's own block time the estimate must be the tip height"
    );

    // A day later the network has mined more blocks. The exact count depends on
    // the target spacing in force at this height, so assert the direction and a
    // conservative lower bound rather than an exact value: mainnet spacing is
    // at most 150s, so a day is at least 576 blocks.
    let later = tip
        .estimate_network_chain_tip_height(Network::Mainnet, t0 + Duration::days(1))
        .expect("a present tip must produce an estimate");
    assert!(
        later.0 > at_tip.0,
        "the estimate must advance with wall-clock time, got {} then {}",
        at_tip.0,
        later.0
    );
    assert!(
        later.0 - at_tip.0 >= 576,
        "a day at mainnet target spacing is at least 576 blocks, got {}",
        later.0 - at_tip.0
    );

    // Going backwards from the tip must not run the estimate forwards.
    let earlier = tip
        .estimate_network_chain_tip_height(Network::Mainnet, t0 - Duration::days(1))
        .expect("a present tip must produce an estimate");
    assert!(
        earlier.0 <= at_tip.0,
        "an earlier wall clock must not produce a higher estimate, got {}",
        earlier.0
    );
}
