//! Group `resolvedefer`: forward hostname resolution joins the bootstrap gate.
//!
//! New tier, and it exists as its own group because of a one-character change
//! upstream.  `TorClient::resolve_with_prefs` builds a throwaway `TorAddr` from
//! the hostname before it reaches the bootstrap gate, and at v0.1.0 it used
//! port 0 as the placeholder:
//!
//! ```ignore
//! let addr = (hostname, 0).into_tor_addr().map_err(wrap_err)?;   // v0.1.0
//! let addr = (hostname, 1).into_tor_addr().map_err(wrap_err)?;   // v0.2.0
//! ```
//!
//! Port 0 is not a valid stream target, so `into_tor_addr` rejected it and the
//! call failed with `ErrorKind::InvalidStreamTarget` without ever consulting
//! the bootstrap state.  `resolve` therefore did NOT report `BootstrapRequired`
//! before this step, even though `connect` and `resolve_ptr` both did -- which
//! is why step 06's `bootdefer` grades `resolve_ptr` and pointedly leaves
//! forward `resolve` alone.
//!
//! §10.3 states plainly that `connect`, `resolve` and `resolve_ptr` all report
//! `BootstrapRequired`, and §11.1 repeats it.  Making that sentence true for
//! the middle one is something this step delivers, so it is priced here rather
//! than sitting in the inherited gate group `deferboot`, where it would have
//! been an unpriced veto that no candidate could earn anything for satisfying.
//!
//! Graded items: `arti_client::TorClient::resolve` and
//! `tor_error::ErrorKind::BootstrapRequired`.

use tor_error::{ErrorKind, HasKind};
use tor_rtcompat::BlockOn;

use crate::common::OfflineClient;

#[test]
fn resolvedefer__hostname_resolution_before_bootstrap_fails_with_bootstrap_required() {
    let held = OfflineClient::new();
    let err = held
        .client
        .runtime()
        .block_on(held.client.resolve("example.com"))
        .err()
        .expect("resolving before bootstrap must fail");
    assert_eq!(
        err.kind(),
        ErrorKind::BootstrapRequired,
        "the placeholder port must no longer fail target validation ahead of \
         the bootstrap gate; InvalidStreamTarget here is the v0.1.0 behaviour"
    );
}
